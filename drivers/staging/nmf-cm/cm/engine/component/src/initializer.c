/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/inc/cm_type.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/communication/inc/communication.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/dsp/mmdsp/inc/mmdsp_hwp.h>

#include <cm/engine/power_mgt/inc/power.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

#include <cm/engine/trace/inc/trace.h>

#include "../inc/dspevent.h"
#include "../inc/initializer.h"

// Since now due to semaphore use call is synchrone so we only need a fifo size of three
// (due to updateStack + (InstructionCacheLock or InstructionCacheUnlock))
#define DEFAULT_INITIALIZER_FIFO_SIZE 3

/* private prototype */
PRIVATE t_cm_error cm_COMP_generic(t_nmf_core_id coreId, t_event_params_handle paramArray, t_uint32 paramNumber, t_uint32 serviceIndex);
PRIVATE void cm_COMP_generatePanic(t_nmf_core_id coreId);

/*
 * This module is tightly coupled with cm_DSP_components one (communication/initializer)
 */
static struct {
    t_nmf_fifo_arm_desc* downlinkFifo;
    t_nmf_fifo_arm_desc* uplinkFifo;
    t_memory_handle dspfifoHandle;
    t_nmf_osal_sem_handle fifoSemHandle;
    t_uint32 servicePending;                // TODO : Use sem counter instead of defining such variable (need to create new OSAL)
} initializerDesc[NB_CORE_IDS];

PUBLIC t_cm_error cm_COMP_INIT_Init(t_nmf_core_id coreId)
{
    t_uint32 i;
    t_cm_error error;
    t_component_instance *ee;
    t_dsp_offset sharedVarOffset;
    t_interface_provide_description itfProvide;
    t_interface_provide* provide;
    t_interface_provide_loaded* provideLoaded;

    ee = cm_EEM_getExecutiveEngine(coreId)->instance;

    // Get interface description
    if((error = cm_getProvidedInterface(ee, "service", &itfProvide)) != CM_OK)
        return error;
    provide = &ee->Template->provides[itfProvide.provideIndex];
    provideLoaded = &ee->Template->providesLoaded[itfProvide.provideIndex];


    if ((error = dspevent_createDspEventFifo(
                    ee, "comms/TOP",
                    DEFAULT_INITIALIZER_FIFO_SIZE,
                    DSP_REMOTE_EVENT_SIZE_IN_DSPWORD,
                    INTERNAL_XRAM24,
                    &initializerDesc[coreId].dspfifoHandle)) != CM_OK)
        return error;

    /* create fifo semaphore */
    initializerDesc[coreId].servicePending = 0;
    initializerDesc[coreId].fifoSemHandle = OSAL_CreateSemaphore(DEFAULT_INITIALIZER_FIFO_SIZE);
    if (initializerDesc[coreId].fifoSemHandle == 0) {
        dspevent_destroyDspEventFifo(initializerDesc[coreId].dspfifoHandle);
        return CM_NO_MORE_MEMORY;
    }

    /* static armTHis initialisation */
    /*
     * In the two next fifo_alloc call (1+n) means that we want to manage the hostThis_or_TOP and one method for each params fifos */
    initializerDesc[coreId].downlinkFifo =
        fifo_alloc(ARM_CORE_ID, coreId,
                INIT_COMPONENT_CMD_SIZE, DEFAULT_INITIALIZER_FIFO_SIZE,
                (1+provide->interface->methodNumber), paramsLocation,  extendedFieldLocation, cm_DSP_GetState(coreId)->domainEE);
    if (initializerDesc[coreId].downlinkFifo == NULL)
    {
        OSAL_DestroySemaphore(initializerDesc[coreId].fifoSemHandle);
        dspevent_destroyDspEventFifo(initializerDesc[coreId].dspfifoHandle);
        ERROR("CM_NO_MORE_MEMORY: fifo_alloc() failed in cm_COMP_INIT_Init()\n", 0, 0, 0, 0, 0, 0);
        return CM_NO_MORE_MEMORY;
    }

    initializerDesc[coreId].uplinkFifo =
        fifo_alloc(coreId, ARM_CORE_ID,
                INIT_COMPONENT_ACK_SIZE, DEFAULT_INITIALIZER_FIFO_SIZE,
                (1), paramsLocation, extendedFieldLocation, cm_DSP_GetState(coreId)->domainEE);              /* 1 is mandatory to compute internally the indexMask */
                                                                                              /* this statement is acceptable only written by skilled man ;) */
                                                                                              /* We don't used bcDescRef, since we assume that we don't need params size */
    if (initializerDesc[coreId].uplinkFifo == NULL)
    {
        OSAL_DestroySemaphore(initializerDesc[coreId].fifoSemHandle);
        fifo_free(initializerDesc[coreId].downlinkFifo);
        dspevent_destroyDspEventFifo(initializerDesc[coreId].dspfifoHandle);
        ERROR("CM_NO_MORE_MEMORY: fifo_alloc() failed in cm_COMP_INIT_Init()\n", 0, 0, 0, 0, 0, 0);
        return CM_NO_MORE_MEMORY;
    }

    cm_writeAttribute(ee, "comms/FIFOcmd", initializerDesc[coreId].downlinkFifo->dspAdress);

    cm_writeAttribute(ee, "comms/FIFOack", initializerDesc[coreId].uplinkFifo->dspAdress);

    sharedVarOffset = cm_getAttributeMpcAddress(ee, "comms/TOP");

    /* HOST->DSP ParamsFifo extended fields initialisation */
    fifo_params_setSharedField(
            initializerDesc[coreId].downlinkFifo,
            0,
            (t_shared_field)sharedVarOffset /* TOP DSP Address */
    );
    for(i=0; i<provide->interface->methodNumber; i++)
    {
        fifo_params_setSharedField(
                initializerDesc[coreId].downlinkFifo,
                i + 1,
                provideLoaded->indexesLoaded[itfProvide.collectionIndex][i].methodAddresses);
    }

    /* DSP->HOST ParamsFifo extended fields initialisation */
    fifo_params_setSharedField(
            initializerDesc[coreId].uplinkFifo,
            0,
            (t_shared_field)NMF_INTERNAL_USERTHIS
            );

    return CM_OK;
}


PUBLIC t_cm_error cm_COMP_CallService(
        int serviceIndex,
        t_component_instance *pComp,
        t_uint32 methodAddress) {
    t_cm_error error;
    t_uint16 params[INIT_COMPONENT_CMD_SIZE];
    t_bool isSynchronous = (serviceIndex == NMF_CONSTRUCT_SYNC_INDEX ||
                            serviceIndex == NMF_START_SYNC_INDEX ||
                            serviceIndex == NMF_STOP_SYNC_INDEX ||
                            serviceIndex == NMF_DESTROY_INDEX)?TRUE:FALSE;

    params[INIT_COMPONENT_CMD_HANDLE_INDEX] = (t_uint16)((unsigned int)pComp & 0xFFFF);
    params[INIT_COMPONENT_CMD_HANDLE_INDEX+1] =  (t_uint16)((unsigned int)pComp >> 16);
    params[INIT_COMPONENT_CMD_THIS_INDEX] =  (t_uint16)(pComp->thisAddress & 0xFFFF);
    params[INIT_COMPONENT_CMD_THIS_INDEX+1] =  (t_uint16)(pComp->thisAddress >> 16);
    params[INIT_COMPONENT_CMD_METHOD_INDEX] =  (t_uint16)(methodAddress & 0xFFFF);
    params[INIT_COMPONENT_CMD_METHOD_INDEX+1] =  (t_uint16)(methodAddress >> 16);

    error = cm_COMP_generic(pComp->Template->dspId, params, sizeof(params) / sizeof(t_uint16), serviceIndex);

    if (isSynchronous == TRUE && error == CM_OK) {
        if (OSAL_SEMAPHORE_WAIT_TIMEOUT(semHandle) != SYNC_OK) {
            cm_COMP_generatePanic(pComp->Template->dspId);
            error = CM_MPC_NOT_RESPONDING;
        }
    }

    return error;
}

PUBLIC void cm_COMP_Flush(t_nmf_core_id coreId) {

    if(initializerDesc[coreId].servicePending > 0)
    {
        t_uint16 params[INIT_COMPONENT_CMD_SIZE];
        t_uint32 methodAddress = cm_EEM_getExecutiveEngine(coreId)->voidAddr;

        // If service still pending on MMDSP side, send a flush command (today, we reuse Destroy to not create new empty service)
        // When we receive the result, this mean that we have flushed all previous request.

        params[INIT_COMPONENT_CMD_HANDLE_INDEX] = (t_uint16)(0x0 & 0xFFFF);
        params[INIT_COMPONENT_CMD_HANDLE_INDEX+1] =  (t_uint16)(0x0 >> 16);
        params[INIT_COMPONENT_CMD_THIS_INDEX] =  (t_uint16)(0x0 & 0xFFFF);
        params[INIT_COMPONENT_CMD_THIS_INDEX+1] =  (t_uint16)(0x0 >> 16);
        params[INIT_COMPONENT_CMD_METHOD_INDEX] =  (t_uint16)(methodAddress & 0xFFFF);
        params[INIT_COMPONENT_CMD_METHOD_INDEX+1] =  (t_uint16)(methodAddress >> 16);

        if (cm_COMP_generic(coreId, params, sizeof(params) / sizeof(t_uint16), NMF_DESTROY_INDEX) != CM_OK ||
                OSAL_SEMAPHORE_WAIT_TIMEOUT(semHandle) != SYNC_OK)
        {
            cm_COMP_generatePanic(coreId);
            ERROR("CM_MPC_NOT_RESPONDING: can't call flush service\n", 0, 0, 0, 0, 0, 0);
        }
    }
}

PUBLIC void cm_COMP_INIT_Close(t_nmf_core_id coreId)
{
    unsigned int i;

    /* wait for semaphore to be sure it would not be touch later on */
    /* in case of timeout we break and try to clean everythink */
    for(i = 0; i < DEFAULT_INITIALIZER_FIFO_SIZE; i++) {
        if (OSAL_SEMAPHORE_WAIT_TIMEOUT(initializerDesc[coreId].fifoSemHandle) != SYNC_OK)
            break;
    }

    /* destroy semaphore */
    OSAL_DestroySemaphore(initializerDesc[coreId].fifoSemHandle);

    /* Unallocate initializerDesc[index].uplinkFifo */
    /* (who is used in this particular case to store dummy (with no data space (only descriptor)) DSP->HOST params fifo */
    fifo_free(initializerDesc[coreId].uplinkFifo);

    /* Unallocate initializerDesc[index].downlinkFifo */
    fifo_free(initializerDesc[coreId].downlinkFifo);

    /* Unallocate initializerDesc[index].dspfifoHandle */
    dspevent_destroyDspEventFifo(initializerDesc[coreId].dspfifoHandle);
}

PUBLIC void processAsyncAcknowledge(t_nmf_core_id coreId, t_event_params_handle pParam)
{
    cm_AcknowledgeEvent(initializerDesc[coreId].uplinkFifo);

    initializerDesc[coreId].servicePending--;
    OSAL_SemaphorePost(initializerDesc[coreId].fifoSemHandle,1);
}

PUBLIC void processSyncAcknowledge(t_nmf_core_id coreId, t_event_params_handle pParam)
{
    cm_AcknowledgeEvent(initializerDesc[coreId].uplinkFifo);

    initializerDesc[coreId].servicePending--;
    OSAL_SemaphorePost(initializerDesc[coreId].fifoSemHandle,1);
    OSAL_SemaphorePost(semHandle,1);
}

PUBLIC t_cm_error cm_COMP_UpdateStack(
    t_nmf_core_id coreId,
    t_uint32 stackSize
)
{
    t_uint16 params[2];

    // Marshall parameter
    params[0] =  (t_uint16)((unsigned int)stackSize & 0xFFFF);
    params[1] =  (t_uint16)((unsigned int)stackSize >> 16);

    return cm_COMP_generic(coreId, params, sizeof(params) / sizeof(t_uint16), NMF_UPDATE_STACK);
}

PUBLIC t_cm_error cm_COMP_ULPForceWakeup(
    t_nmf_core_id coreId
)
{
    t_cm_error error;

    error = cm_COMP_generic(coreId, NULL, 0, NMF_ULP_FORCEWAKEUP);

    if (error == CM_OK) {
        if (OSAL_SEMAPHORE_WAIT_TIMEOUT(semHandle) != SYNC_OK) {
            cm_COMP_generatePanic(coreId);
            error = CM_MPC_NOT_RESPONDING;
        }
    }

    return error;
}

PUBLIC t_cm_error cm_COMP_ULPAllowSleep(
    t_nmf_core_id coreId
)
{
    return cm_COMP_generic(coreId, NULL, 0, NMF_ULP_ALLOWSLEEP);
}

PUBLIC t_cm_error cm_COMP_InstructionCacheLock(
    t_nmf_core_id coreId,
    t_uint32 mmdspAddr,
    t_uint32 mmdspSize
)
{
    t_uint16 params[4];
    t_uint32 startAddr = cm_DSP_GetState(coreId)->locked_offset;
    int way;

    for(way = 1; startAddr < mmdspAddr + mmdspSize; startAddr += MMDSP_CODE_CACHE_WAY_SIZE, way++)
    {
        if(mmdspAddr < startAddr + MMDSP_CODE_CACHE_WAY_SIZE)
        {
            t_cm_error error;

            // Marshall parameter
            params[0] =  (t_uint16)((unsigned int)startAddr & 0xFFFF);
            params[1] =  (t_uint16)((unsigned int)startAddr >> 16);
            params[2] =  (t_uint16)((unsigned int)way & 0xFFFF);
            params[3] =  (t_uint16)((unsigned int)way >> 16);

             if((error = cm_COMP_generic(coreId, params, sizeof(params) / sizeof(t_uint16), NMF_LOCK_CACHE)) != CM_OK)
                 return error;
        }
    }

    return CM_OK;
}

PUBLIC t_cm_error cm_COMP_InstructionCacheUnlock(
    t_nmf_core_id coreId,
    t_uint32 mmdspAddr,
    t_uint32 mmdspSize
)
{
    t_uint16 params[2];
    t_uint32 startAddr = cm_DSP_GetState(coreId)->locked_offset;
    int way;

    for(way = 1; startAddr < mmdspAddr + mmdspSize; startAddr += MMDSP_CODE_CACHE_WAY_SIZE, way++)
    {
        if(mmdspAddr < startAddr + MMDSP_CODE_CACHE_WAY_SIZE)
        {
            t_cm_error error;

            // Marshall parameter
            params[0] =  (t_uint16)((unsigned int)way & 0xFFFF);
            params[1] =  (t_uint16)((unsigned int)way >> 16);

             if((error = cm_COMP_generic(coreId, params, sizeof(params) / sizeof(t_uint16), NMF_UNLOCK_CACHE)) != CM_OK)
                 return error;
        }
    }

    return CM_OK;
}

/* private method */
PRIVATE t_cm_error cm_COMP_generic(
        t_nmf_core_id coreId,
    t_event_params_handle paramArray,
    t_uint32 paramNumber,
    t_uint32 serviceIndex
)
{
    t_event_params_handle _xyuv_data;
    t_cm_error error;
    t_uint32 i;

    // wait for an event in fifo
    if (OSAL_SEMAPHORE_WAIT_TIMEOUT(initializerDesc[coreId].fifoSemHandle) != SYNC_OK) {
        cm_COMP_generatePanic(coreId);
        return CM_MPC_NOT_RESPONDING;
    }


    // AllocEvent
    if((_xyuv_data = cm_AllocEvent(initializerDesc[coreId].downlinkFifo)) == NULL)
    {
        ERROR("CM_INTERNAL_FIFO_OVERFLOW: service FIFO full\n", 0, 0, 0, 0, 0, 0);
        error = CM_INTERNAL_FIFO_OVERFLOW;
        goto unlock;
    }

    // Copy param
    for(i=0;i<paramNumber;i++)
        _xyuv_data[i] = paramArray[i];

    OSAL_LOCK_COM();

    // Send Command
    error = cm_PushEventTrace(initializerDesc[coreId].downlinkFifo, _xyuv_data, serviceIndex,0);
    if(error == CM_OK)
        initializerDesc[coreId].servicePending++;

unlock:
    OSAL_UNLOCK_COM();

    return error;
}

PRIVATE void cm_COMP_generatePanic(t_nmf_core_id coreId)
{
    const t_dsp_desc* pDspDesc = cm_DSP_GetState(coreId);

    if (pDspDesc->state != MPC_STATE_PANIC) {
        cm_DSP_SetStatePanic(coreId);
        OSAL_GeneratePanic(coreId, 0);
    }
}
