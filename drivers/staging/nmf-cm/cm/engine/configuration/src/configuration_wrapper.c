/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/api/configuration_engine.h>
#include <cm/engine/communication/inc/communication.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/memory/inc/chunk_mgr.h>
#include <cm/engine/repository_mgt/inc/repository_mgt.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <cm/engine/semaphores/hw_semaphores/inc/hw_semaphores.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/configuration/inc/configuration.h>
#include <cm/engine/power_mgt/inc/power.h>
#include <cm/engine/utils/inc/string.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/component/inc/bind.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/api/executive_engine_mgt_engine.h>

#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/trace/inc/xtitrace.h>

t_dup_char anonymousDup, eventDup, skeletonDup, stubDup, traceDup;

PUBLIC t_cm_error CM_ENGINE_Init(
        const t_nmf_hw_mapping_desc *pNmfHwMappingDesc,
        const t_nmf_config_desc *pNmfConfigDesc
        )
{
    t_cm_error error;

    // The purpose of that is just to not free/unfree some String frequently used
    anonymousDup = cm_StringDuplicate("anonymous");
    eventDup = cm_StringDuplicate("event");
    skeletonDup = cm_StringDuplicate("skeleton");
    stubDup = cm_StringDuplicate("stub");
    traceDup = cm_StringDuplicate("trace");

    if ((
                error = cm_OSAL_Init()
        ) != CM_OK) { return error; }

    if ((
                error = cm_COMP_Init()
        ) != CM_OK) { return error; }

    if ((
    		    error = cm_PWR_Init()
         ) != CM_OK) { return error; }

    cm_TRC_traceReset();

    if ((
                error = cm_DM_Init()
         ) != CM_OK) {return error; }

    if ((
                error = cm_SEM_Init(&pNmfHwMappingDesc->hwSemaphoresMappingBaseAddr)
        ) != CM_OK) { return error; }

    if ((error = cm_COM_Init(pNmfConfigDesc->comsLocation)) != CM_OK)
        return error;

    cm_DSP_Init(&pNmfHwMappingDesc->esramDesc);

    return CM_OK;
}

PUBLIC void CM_ENGINE_Destroy(void)
{
    t_component_instance *instance;
    t_cm_error error;
    t_uint32 i;

    /* PP: Well, on Linux (and probably on Symbian too), this is called when driver is removed
     * => the module (driver) can't be removed if there are some pending clients
     * => all remaining components should have been destroyed in CM_ENGINE_FlushClient()
     * => So, if we found some components here, we are in BIG trouble ...
     */
    /* First, stop all remaining components  */
    for (i=0; i<ComponentTable.idxMax; i++)
    {
	t_nmf_client_id clientId;

	if ((instance = componentEntry(i)) == NULL)
		continue;
	clientId = domainDesc[instance->domainId].client;
        LOG_INTERNAL(0, "Found a remaining component %s (%s) when destroying the CM !!!\n", instance->pathname, instance->Template->name, 0, 0, 0, 0);
        if (/* skip EE */
                (instance->Template->classe == FIRMWARE) ||
                /* Skip all binding components */
                (cm_StringCompare(instance->Template->name, "_ev.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_st.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_sk.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_tr.", 4) == 0))
            continue;

        /*
         * Special code for SINGLETON handling
         */
        if(instance->Template->classe == SINGLETON)
        {
            struct t_client_of_singleton* cl = instance->clientOfSingleton;

            clientId = instance->clientOfSingleton->clientId;
            for( ; cl != NULL ; cl = cl->next)
            {
                if(cl == instance->clientOfSingleton)
                {
                    cl->numberOfStart = 1; // == 1 since it will go to 0 in cm_stopComponent
                    cl->numberOfInstance = 1; // == 1 since it will go to 0 in cm_destroyInstanceForClient
                }
                else
                {
                    cl->numberOfStart = 0;
                    cl->numberOfInstance = 0;
                }
                cl->numberOfBind = 0;
            }
        }

        // Stop the component
        error = cm_stopComponent(instance, clientId);
        if (error != CM_OK && error != CM_COMPONENT_NOT_STARTED)
            LOG_INTERNAL(0, "Error stopping component %s/%x (%s, error=%d, client=%u)\n", instance->pathname, instance, instance->Template->name, error, clientId, 0);

        // Destroy dependencies
        cm_destroyRequireInterface(instance, clientId);
    }

    /* Destroy all remaining components */
    for (i=0; i<ComponentTable.idxMax; i++)
    {
	t_nmf_client_id clientId;

	if ((instance = componentEntry(i)) == NULL)
		continue;
        clientId = domainDesc[instance->domainId].client;

        if (/* skip EE */
                (instance->Template->classe == FIRMWARE) ||
                /* Skip all binding components */
                (cm_StringCompare(instance->Template->name, "_ev.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_st.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_sk.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_tr.", 4) == 0)) {
            continue;
        }

        if(instance->Template->classe == SINGLETON)
        {
            clientId = instance->clientOfSingleton->clientId;
        }

        // Destroy the component
        error = cm_destroyInstanceForClient(instance, DESTROY_WITHOUT_CHECK, clientId);

        if (error != CM_OK)
        {
            /* FIXME : add component name instance in log message but need to make a copy before cm_flushComponent()
             *         because it's no more available after.
             */
            LOG_INTERNAL(0, "Error flushing component (error=%d, client=%u)\n", error, clientId, 0, 0, 0, 0);
        }
    }

    /* This will power off all ressources and destroy EE */
    cm_PWR_SetMode(NORMAL_PWR_MODE);
    cm_DSP_Destroy();
    cm_DM_Destroy();
    /* Nothing to do about SEM */
    //cm_MM_Destroy();
    cm_REP_Destroy();
    cm_COMP_Destroy();
    cm_OSAL_Destroy();

    cm_StringRelease(traceDup);
    cm_StringRelease(stubDup);
    cm_StringRelease(skeletonDup);
    cm_StringRelease(eventDup);
    cm_StringRelease(anonymousDup);
}

PUBLIC t_cm_error CM_ENGINE_ConfigureMediaProcessorCore(
        t_nmf_core_id coreId,
        t_nmf_executive_engine_id executiveEngineId,
        t_nmf_semaphore_type_id semaphoreTypeId,
        t_uint8 nbYramBanks,
        const t_cm_system_address *mediaProcessorMappingBaseAddr,
        const t_cm_domain_id eeDomain,
        const t_cfg_allocator_id sdramCodeAllocId,
        const t_cfg_allocator_id sdramDataAllocId
        )
{
    return cm_CFG_ConfigureMediaProcessorCore(
            coreId,
            executiveEngineId,
            semaphoreTypeId,
            nbYramBanks,
            mediaProcessorMappingBaseAddr,
            eeDomain,
            (t_dsp_allocator_desc*)sdramCodeAllocId,
            (t_dsp_allocator_desc*)sdramDataAllocId
            );
}

PUBLIC t_cm_error CM_ENGINE_AddMpcSdramSegment(
        const t_nmf_memory_segment *pDesc,
        t_cfg_allocator_id         *id,
        const char                 *memoryname
        )
{
    return cm_CFG_AddMpcSdramSegment(pDesc, memoryname == NULL ? "" : memoryname, (t_dsp_allocator_desc**)id);
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_SetMode(t_cm_cmd_id aCmdID, t_sint32 aParam)
{
	t_cm_error error = CM_OK;
	int i;

    OSAL_LOCK_API();

    switch(aCmdID) {
    case CM_CMD_DBG_MODE:
        cm_PWR_SetMode(( aParam==1 ) ? DISABLE_PWR_MODE : NORMAL_PWR_MODE);
        switch(cm_PWR_GetMode())
        {
        case NORMAL_PWR_MODE:
            // Release the MPC (which will switch it off if no more used)
            for (i=FIRST_MPC_ID; i<NB_CORE_IDS; i++)
            {
                cm_CFG_ReleaseMpc(i);
            }
            break;
        case DISABLE_PWR_MODE:
            // Force the load of the EE if not already done.
            for (i=FIRST_MPC_ID; i<NB_CORE_IDS;i++)
            {
                if((error = cm_CFG_CheckMpcStatus(i)) != CM_OK)
                    break;
            }
            break;
        }
        break;
	case CM_CMD_TRACE_LEVEL:
	    if (aParam<-1) cm_debug_level = -1;
	    else cm_debug_level = aParam;
	    break;
	case CM_CMD_INTENSIVE_CHECK:
	    cmIntensiveCheckState = aParam;
	    break;

	case CM_CMD_TRACE_ON:
	    cm_trace_enabled = TRUE;
        cm_TRC_Dump();
	    break;
	case CM_CMD_TRACE_OFF:
	    cm_trace_enabled = FALSE;
	    break;

	case CM_CMD_MPC_TRACE_ON:
	    cm_EEM_setTraceMode((t_nmf_core_id)aParam, 1);
	    break;
	case CM_CMD_MPC_TRACE_OFF:
	    cm_EEM_setTraceMode((t_nmf_core_id)aParam, 0);
	    break;

	case CM_CMD_MPC_PRINT_OFF:
	    cm_EEM_setPrintLevel((t_nmf_core_id)aParam, 0);
	    break;
	case CM_CMD_MPC_PRINT_ERROR:
	    cm_EEM_setPrintLevel((t_nmf_core_id)aParam, 1);
	    break;
	case CM_CMD_MPC_PRINT_WARNING:
	    cm_EEM_setPrintLevel((t_nmf_core_id)aParam, 2);
	    break;
	case CM_CMD_MPC_PRINT_INFO:
	    cm_EEM_setPrintLevel((t_nmf_core_id)aParam, 3);
	    break;
	case CM_CMD_MPC_PRINT_VERBOSE:
	    cm_EEM_setPrintLevel((t_nmf_core_id)aParam, 4);
	    break;

	case CM_CMD_ULP_MODE_ON:
	    cmUlpEnable = TRUE;
	    break;

	default:
	    error = CM_INVALID_PARAMETER;
	    break;
	}

    OSAL_UNLOCK_API();

    return error;
}

