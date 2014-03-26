/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/configuration/inc/configuration.h>
#include <cm/engine/component/inc/initializer.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <cm/engine/communication/inc/communication.h>
#include <cm/engine/utils/inc/string.h>
#include <cm/engine/repository_mgt/inc/repository_mgt.h>
#include <inc/nmf-limits.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/memory/inc/domain.h>

#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/convert.h>

#include <cm/engine/power_mgt/inc/power.h>

t_sint32 cmIntensiveCheckState = 0;
t_sint32 cm_debug_level = 1;
t_sint32 cm_error_break = 0;
t_bool cmUlpEnable = FALSE;


#define MAX_EE_NAME_LENGTH      32
typedef struct {
    char eeName[MAX_EE_NAME_LENGTH];
    t_nmf_executive_engine_id executiveEngineId;
    t_uint32 EEmemoryCount;
} t_cfg_mpc_desc;

static t_cfg_mpc_desc        cfgMpcDescArray[NB_CORE_IDS];

PUBLIC t_cm_error cm_CFG_ConfigureMediaProcessorCore(
        t_nmf_core_id coreId,
        t_nmf_executive_engine_id executiveEngineId,
        t_nmf_semaphore_type_id semaphoreTypeId,
        t_uint8 nbYramBanks,
        const t_cm_system_address *mediaProcessorMappingBaseAddr,
        const t_cm_domain_id eeDomain,
        t_dsp_allocator_desc *sdramCodeAllocDesc,
        t_dsp_allocator_desc *sdramDataAllocDesc)
{
    /* Process requested configuration (save it) */
    cfgMpcDescArray[coreId].EEmemoryCount = 0;
    cfgMpcDescArray[coreId].executiveEngineId = executiveEngineId;
    /* Build Executive Engine Name */
    switch(executiveEngineId)
    {
        case SYNCHRONOUS_EXECUTIVE_ENGINE:
            cm_StringCopy(cfgMpcDescArray[coreId].eeName, "synchronous_", MAX_EE_NAME_LENGTH);
            break;
        case HYBRID_EXECUTIVE_ENGINE:
            cm_StringCopy(cfgMpcDescArray[coreId].eeName, "hybrid_", MAX_EE_NAME_LENGTH);
            break;
    }

    switch(semaphoreTypeId)
    {
        case LOCAL_SEMAPHORES:
            cm_StringConcatenate(cfgMpcDescArray[coreId].eeName, "lsem", MAX_EE_NAME_LENGTH);
            break;
        case SYSTEM_SEMAPHORES:
            cm_StringConcatenate(cfgMpcDescArray[coreId].eeName, "hsem", MAX_EE_NAME_LENGTH);
            break;
    }

    cm_SEM_InitMpc(coreId, semaphoreTypeId);

    return cm_DSP_Add(coreId, nbYramBanks, mediaProcessorMappingBaseAddr, eeDomain, sdramCodeAllocDesc, sdramDataAllocDesc);
}

// TODO JPF: Move in dsp.c
PUBLIC t_cm_error cm_CFG_AddMpcSdramSegment(const t_nmf_memory_segment *pDesc, const char* memoryname, t_dsp_allocator_desc **allocDesc)
{
    t_dsp_allocator_desc *desc;
    if ( (pDesc == NULL) ||
            ((pDesc->systemAddr.logical & CM_MM_ALIGN_64BYTES) != 0) )
        return CM_INVALID_PARAMETER;

    //TODO, juraj, the right place and way to do this?
    desc = (t_dsp_allocator_desc*)OSAL_Alloc(sizeof (t_dsp_allocator_desc));
    if (desc == 0)
        return CM_NO_MORE_MEMORY;

    desc->allocDesc = cm_MM_CreateAllocator(pDesc->size, 0, memoryname);
    if (desc->allocDesc == 0) {
    	OSAL_Free(desc);
    	return CM_NO_MORE_MEMORY;
    }
    desc->baseAddress = pDesc->systemAddr;
    desc->referenceCounter = 0;

    *allocDesc = desc;

    return CM_OK;
}

PUBLIC t_cm_error cm_CFG_CheckMpcStatus(t_nmf_core_id coreId)
{
    t_cm_error error;

    if (cm_DSP_GetState(coreId)->state == MPC_STATE_BOOTABLE)
    {
        /* Allocate coms fifo for a given MPC */
        if ((error = cm_COM_AllocateMpc(coreId)) != CM_OK)
            return error;

        /* Launch EE */
        if ((error = cm_EEM_Init(coreId,
                                 cfgMpcDescArray[coreId].eeName,
                                 cfgMpcDescArray[coreId].executiveEngineId)) != CM_OK)
        {
            cm_COM_FreeMpc(coreId);
            return error;
        }

        /* Initialize coms fifo for a given MPC */
        cm_COM_InitMpc(coreId);

        /* Initialisation of the dedicated communication channel for component initialization */
        if((error = cm_COMP_INIT_Init(coreId)) != CM_OK)
        {
            cm_EEM_Close(coreId);
            cm_COM_FreeMpc(coreId);
            return error;
        }

        cfgMpcDescArray[coreId].EEmemoryCount = cm_PWR_GetMPCMemoryCount(coreId);

        if(cmUlpEnable)
        {
            // We have finish boot, allow MMDSP to go in auto idle
            cm_EEM_AllowSleep(coreId);
        }
    }

    if (cm_DSP_GetState(coreId)->state != MPC_STATE_BOOTED)
        return CM_MPC_NOT_INITIALIZED;

    return CM_OK;
}

void cm_CFG_ReleaseMpc(t_nmf_core_id coreId)
{
    t_uint32 memoryCount = cm_PWR_GetMPCMemoryCount(coreId);

    // If No more memory and no more component (to avoid switch off in case of component using no memory)
    if(
            cm_PWR_GetMode() == NORMAL_PWR_MODE &&
            memoryCount != 0 /* Just to see if there is something */ &&
            memoryCount == cfgMpcDescArray[coreId].EEmemoryCount &&
            cm_isComponentOnCoreId(coreId) == FALSE)
    {
        LOG_INTERNAL(1, "\n##### Shutdown %s #####\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);

        (void)cm_EEM_ForceWakeup(coreId);

        /* remove ee from load map here */
        cm_COMP_INIT_Close(coreId);
        cm_EEM_Close(coreId);
        cm_COM_FreeMpc(coreId);

        cfgMpcDescArray[coreId].EEmemoryCount = 0; // For debug purpose
    }
}
