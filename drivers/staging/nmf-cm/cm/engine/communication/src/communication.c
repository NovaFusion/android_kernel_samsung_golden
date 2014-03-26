/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#include <cm/inc/cm_type.h>
#include "../inc/communication.h"
#include <share/communication/inc/communication_fifo.h>
#include <cm/engine/api/control/irq_engine.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/communication/fifo/inc/nmf_fifo_arm.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/memory/inc/migration.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/trace/inc/xtitrace.h>

#include <cm/engine/component/inc/initializer.h>

#define ARM_DSP_EVENT_FIFO_SIZE 128

t_dsp_memory_type_id comsLocation;
t_dsp_memory_type_id paramsLocation;
t_dsp_memory_type_id extendedFieldLocation;

#define __DEBUG

#ifdef __DEBUG
PRIVATE volatile t_uint32 armdspCounter = 0;
PRIVATE volatile t_uint32 armdspIrqCounter = 0;
PRIVATE volatile t_uint32 dsparmCounter = 0;
PRIVATE volatile t_uint32 dsparmIrqCounter = 0;
#endif /* __DEBUG */

t_nmf_fifo_arm_desc* mpc2mpcComsFifoId[NB_CORE_IDS][NB_CORE_IDS];

PRIVATE const t_callback_method internalHostJumptable[] = {
    processAsyncAcknowledge,
    processAsyncAcknowledge,
    processAsyncAcknowledge,
    processSyncAcknowledge,
    processAsyncAcknowledge,
    processAsyncAcknowledge,
    processAsyncAcknowledge,
    processSyncAcknowledge,
    processAsyncAcknowledge,
    processSyncAcknowledge,
    processSyncAcknowledge, // Start sync
    processSyncAcknowledge // Stop sync
};

PUBLIC t_cm_error cm_COM_Init(t_nmf_coms_location _comsLocation)
{
    t_nmf_core_id coreId, localCoreId;

    /*
     * Configure the default location of coms and params fifo (configuration by user) */
    switch(_comsLocation)
    {
        case COMS_IN_SDRAM:
            comsLocation = SDRAM_EXT16;
            paramsLocation = SDRAM_EXT16;
            extendedFieldLocation = SDRAM_EXT24;
            break;
        case COMS_IN_ESRAM:
            comsLocation = ESRAM_EXT16;
            paramsLocation = ESRAM_EXT16;
            extendedFieldLocation = ESRAM_EXT24;
            break;
        default: CM_ASSERT(0);
    }

    for (coreId =  ARM_CORE_ID; coreId < NB_CORE_IDS; coreId++)
    {
        for (localCoreId = ARM_CORE_ID; localCoreId < NB_CORE_IDS; localCoreId++)
        {
            mpc2mpcComsFifoId[coreId][localCoreId] = NULL;
        }
    }

    return CM_OK;
}

PUBLIC t_cm_error cm_COM_AllocateMpc(t_nmf_core_id coreId)
{
    t_nmf_core_id localCoreId;

    /*
     * Allocation of the coms fifo with neighbor MPCs
     * if they are already initialized (known through initializedCoresMask)
     */
    for (localCoreId = ARM_CORE_ID; localCoreId < NB_CORE_IDS; localCoreId++)
    {
        if (localCoreId == coreId) continue; /* no coms fifo with itself ;) */
        if(cm_DSP_GetState(localCoreId)->state != MPC_STATE_BOOTED) continue;

        /*
         * coms fifo from other initialized MPCs to the given one
         */
        if (mpc2mpcComsFifoId[coreId][localCoreId] != NULL) continue; /* coms fifo already allocated */

        mpc2mpcComsFifoId[coreId][localCoreId] = fifo_alloc(
                coreId, localCoreId,
                EVENT_ELEM_SIZE_IN_BYTE/2, ARM_DSP_EVENT_FIFO_SIZE,
                0, comsLocation, extendedFieldLocation, cm_DSP_GetState(coreId)->domainEE
                );
        if (mpc2mpcComsFifoId[coreId][localCoreId] == NULL)
            goto oom;

        /*
         * coms fifo from the given MPC to the other initialized ones
         */
        if (mpc2mpcComsFifoId[localCoreId][coreId] != NULL) continue; /* coms fifo already allocated */

        mpc2mpcComsFifoId[localCoreId][coreId] = fifo_alloc(
                localCoreId, coreId,
                EVENT_ELEM_SIZE_IN_BYTE/2, ARM_DSP_EVENT_FIFO_SIZE,
                0, comsLocation, extendedFieldLocation, cm_DSP_GetState(coreId)->domainEE
                );
        if (mpc2mpcComsFifoId[localCoreId][coreId] == NULL)
            goto oom;
    }

    return CM_OK;
oom:
    cm_COM_FreeMpc(coreId);
    ERROR("CM_NO_MORE_MEMORY: fifo_alloc() failed in cm_COM_AllocateMpc()\n", 0, 0, 0, 0, 0, 0);
    return CM_NO_MORE_MEMORY;
}

PUBLIC void cm_COM_InitMpc(t_nmf_core_id coreId)
{
    // Here we assume that attribute are in XRAM, thus we don't need memory type
    t_uint32* toNeighborsComsFifoIdSharedVar[NB_CORE_IDS];
    t_uint32* fromNeighborsComsFifoIdSharedVar[NB_CORE_IDS];

    t_nmf_core_id localCoreId;

    /*
     * Initialization of the core identifier of a given Executive Engine
     * Used into communication scheme so the init is done here, will be moved MAY BE into EE loading module!!!
     */
    cm_writeAttribute(cm_EEM_getExecutiveEngine(coreId)->instance, "semaphores/myCoreId", coreId);

    /*
     * Initialization of the coms fifo with the Host for the given coreId
     */
    for (localCoreId = FIRST_MPC_ID/* NOT ARM*/; localCoreId <= LAST_CORE_ID; localCoreId++)
    {
        // Note: This loop will also include coreId in order to fill
        if(cm_DSP_GetState(localCoreId)->state != MPC_STATE_BOOTED) continue;/* no coms fifo initialisation with not booted MPC */

        toNeighborsComsFifoIdSharedVar[localCoreId] = (t_uint32*)cm_getAttributeHostAddr(cm_EEM_getExecutiveEngine(localCoreId)->instance, "comms/toNeighborsComsFifoId");

        fromNeighborsComsFifoIdSharedVar[localCoreId] = (t_uint32*)cm_getAttributeHostAddr(cm_EEM_getExecutiveEngine(localCoreId)->instance, "comms/fromNeighborsComsFifoId");
    }

    toNeighborsComsFifoIdSharedVar[coreId][ARM_CORE_ID] = mpc2mpcComsFifoId[coreId][ARM_CORE_ID]->dspAdress;
    fromNeighborsComsFifoIdSharedVar[coreId][ARM_CORE_ID] = mpc2mpcComsFifoId[ARM_CORE_ID][coreId]->dspAdress;

    for (localCoreId = FIRST_MPC_ID/* NOT ARM*/; localCoreId <= LAST_CORE_ID; localCoreId++)
    {
        if (localCoreId == coreId) continue; /* no coms fifo with itself ;) */
        if(cm_DSP_GetState(localCoreId)->state != MPC_STATE_BOOTED) continue;/* no coms fifo initialisation with not booted MPC */

        fromNeighborsComsFifoIdSharedVar[coreId][localCoreId] = mpc2mpcComsFifoId[localCoreId][coreId]->dspAdress;
        toNeighborsComsFifoIdSharedVar[coreId][localCoreId] = mpc2mpcComsFifoId[coreId][localCoreId]->dspAdress;

        LOG_INTERNAL(1, "ARM: Force Try to wake up on core id : %d\n", localCoreId, 0, 0, 0, 0, 0);
        cm_EEM_ForceWakeup(localCoreId);

        fromNeighborsComsFifoIdSharedVar[localCoreId][coreId] = mpc2mpcComsFifoId[coreId][localCoreId]->dspAdress;
        toNeighborsComsFifoIdSharedVar[localCoreId][coreId] = mpc2mpcComsFifoId[localCoreId][coreId]->dspAdress;

        LOG_INTERNAL(1, "ARM: Force Allow sleep on core id : %d\n", localCoreId, 0, 0, 0, 0, 0);
        cm_EEM_AllowSleep(localCoreId);
    }
}

PUBLIC void cm_COM_FreeMpc(t_nmf_core_id coreId)
{
    t_nmf_core_id localCoreId;

    for (localCoreId = ARM_CORE_ID; localCoreId < NB_CORE_IDS; localCoreId++)
    {
        /*
         * Free coms fifo from other initialized MPCs to the given one
         */
        if ( mpc2mpcComsFifoId[coreId][localCoreId] != NULL)
        {
            fifo_free(mpc2mpcComsFifoId[coreId][localCoreId]);
            mpc2mpcComsFifoId[coreId][localCoreId] = NULL;
        }

        /*
         * Free coms fifo from the given MPC to the other initialized ones
         */
        if ( mpc2mpcComsFifoId[localCoreId][coreId] != NULL)
        {
            fifo_free(mpc2mpcComsFifoId[localCoreId][coreId]);
            mpc2mpcComsFifoId[localCoreId][coreId] = NULL;
        }
    }
}

PUBLIC t_event_params_handle cm_AllocEvent(t_nmf_fifo_arm_desc *pArmFifo)

{
    t_uint32 retValue;

    //migration impacts the ARM-side address of the fifoDesc,
    //thus translate the fifo desc adress systematically.
    pArmFifo->fifoDesc = (t_nmf_fifo_desc*)cm_migration_translate(pArmFifo->dspAddressInfo.segmentType, (t_shared_addr)pArmFifo->fifoDescShadow);

    retValue = fifo_getAndAckNextElemToWritePointer(pArmFifo);

    return (t_event_params_handle)retValue;
}

PUBLIC void cm_AcknowledgeEvent(t_nmf_fifo_arm_desc *pArmFifo)
{
    fifo_acknowledgeRead(pArmFifo);
}

PUBLIC t_cm_error cm_PushEventTrace(t_nmf_fifo_arm_desc *pArmFifo, t_event_params_handle h, t_uint32 methodIndex, t_uint32 isTrace)
{
    t_uint32 retValue;

    retValue = fifo_getNextElemToWritePointer(mpc2mpcComsFifoId[ARM_CORE_ID][pArmFifo->poperCoreId]);

    if(retValue != 0x0) {
        t_shared_field *pEvent = (t_shared_field *)retValue;

#ifdef __DEBUG
        armdspCounter++;
#endif /* __DEBUG */

        pEvent[EVENT_ELEM_METHOD_IDX]   = (t_shared_addr)methodIndex;
        pEvent[EVENT_ELEM_PARAM_IDX]    = pArmFifo->dspAdress + (((t_cm_logical_address)h - (t_cm_logical_address)pArmFifo->fifoDesc) >> 1); //note byte to half-word conversion
        pEvent[EVENT_ELEM_EXTFIELD_IDX] = pArmFifo->fifoDesc->extendedField;

        if (isTrace)
        {
            cm_TRC_traceCommunication(
                    TRACE_COMMUNICATION_COMMAND_SEND,
                    ARM_CORE_ID,
                    pArmFifo->poperCoreId);
        }
        fifo_coms_acknowledgeWriteAndInterruptGeneration(mpc2mpcComsFifoId[ARM_CORE_ID][pArmFifo->poperCoreId]);

        return CM_OK;
    }

    ERROR("CM_MPC_NOT_RESPONDING: FIFO COM full '%s'\n", 0, 0, 0, 0, 0, 0);
    return CM_MPC_NOT_RESPONDING;
}

PUBLIC t_cm_error cm_PushEvent(t_nmf_fifo_arm_desc *pArmFifo, t_event_params_handle h, t_uint32 methodIndex)
{
    return cm_PushEventTrace(pArmFifo,h,methodIndex,1);
}

static void cmProcessMPCFifo(t_nmf_core_id coreId)
{
    t_shared_field *pEvent;

    while((pEvent = (t_shared_field *)fifo_getNextElemToReadPointer(mpc2mpcComsFifoId[coreId][ARM_CORE_ID])) != NULL)
    {
        t_event_params_handle pParamsAddr;
        t_shared_field *pParamsFifoESFDesc;

        pParamsAddr = (t_event_params_handle)cm_DSP_ConvertDspAddressToHostLogicalAddress(
                coreId,
                pEvent[EVENT_ELEM_PARAM_IDX]);
        pParamsFifoESFDesc = (t_shared_field *)pEvent[EVENT_ELEM_EXTFIELD_IDX];
#ifdef __DEBUG
        dsparmCounter++;
#endif /* __DEBUG */

        if(pParamsFifoESFDesc[EXTENDED_FIELD_BCTHIS_OR_TOP] == (t_shared_field)NMF_INTERNAL_USERTHIS)
        {
            internalHostJumptable[pEvent[EVENT_ELEM_METHOD_IDX]](coreId, pParamsAddr);
        }
        else
        {
            cm_TRC_traceCommunication(
                    TRACE_COMMUNICATION_COMMAND_RECEIVE,
                    ARM_CORE_ID,
                    coreId);

            OSAL_PostDfc(
                    pParamsFifoESFDesc[EXTENDED_FIELD_BCTHIS_OR_TOP],
                    pEvent[EVENT_ELEM_METHOD_IDX],
                    pParamsAddr,
                    pParamsFifoESFDesc[EXTENDED_FIELD_BCDESC]);
        }

        // [Pwr] mpc2hostComsFifoId value is checked to support the case where
        //       CM_PostCleanUpAndFlush method is called under interrupt context
        //       -> mpc2hostComsFifoId can be released.
        if (mpc2mpcComsFifoId[coreId][ARM_CORE_ID] != NULL)
            fifo_acknowledgeRead(mpc2mpcComsFifoId[coreId][ARM_CORE_ID]);
        else
            break;
    }
}

PUBLIC EXPORT_SHARED void CM_ProcessMpcEvent(t_nmf_core_id coreId)
{
#ifdef __DEBUG
    dsparmIrqCounter++;
#endif /* __DEBUG */

    if (coreId != ARM_CORE_ID)
    {
        /* Acknowledge DSP communication interrupt */
        cm_DSP_AcknowledgeDspIrq(coreId, DSP2ARM_IRQ_0);

        cmProcessMPCFifo(coreId);
    }
    else
    {
        while((coreId = cm_HSEM_GetCoreIdFromIrqSrc()) <= LAST_MPC_ID)
            cmProcessMPCFifo(coreId);
    }
}

