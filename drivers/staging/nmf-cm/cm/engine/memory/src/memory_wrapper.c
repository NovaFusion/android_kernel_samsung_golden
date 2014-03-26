/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/configuration/inc/configuration.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/trace/inc/trace.h>

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_AllocMpcMemory(
        t_cm_domain_id domainId,
        t_nmf_client_id clientId,
        t_cm_mpc_memory_type memType,
        t_cm_size size,
        t_cm_mpc_memory_alignment memAlignment,
        t_cm_memory_handle *pHandle
        )
{
    t_dsp_memory_type_id dspMemType;

    switch(memType)
    {
        case CM_MM_MPC_TCM16_X:
            dspMemType = INTERNAL_XRAM16;
            break;
        case CM_MM_MPC_TCM24_X:
            dspMemType = INTERNAL_XRAM24;
            break;
        case CM_MM_MPC_TCM16_Y:
            dspMemType = INTERNAL_YRAM16;
            break;
        case CM_MM_MPC_TCM24_Y:
            dspMemType = INTERNAL_YRAM24;
            break;
#ifndef __STN_8810
        case CM_MM_MPC_ESRAM16:
            dspMemType = ESRAM_EXT16;
            break;
        case CM_MM_MPC_ESRAM24:
            dspMemType = ESRAM_EXT24;
            break;
#endif /* ndef __STN_8810 */
        case CM_MM_MPC_SDRAM16:
            dspMemType = SDRAM_EXT16;
            break;
        case CM_MM_MPC_SDRAM24:
            dspMemType = SDRAM_EXT24;
            break;
        default:
            return CM_INVALID_PARAMETER;
    }

    OSAL_LOCK_API();
    {
        t_cm_error error;
        error = cm_DM_CheckDomainWithClient(domainId, DOMAIN_ANY, clientId);
        if (error != CM_OK) {
            OSAL_UNLOCK_API();
            return error;
        }
    }

    switch(memAlignment) {
    case CM_MM_MPC_ALIGN_NONE        :
    case CM_MM_MPC_ALIGN_HALFWORD    :
    case CM_MM_MPC_ALIGN_WORD        :
    case CM_MM_MPC_ALIGN_2WORDS      :
    case CM_MM_MPC_ALIGN_4WORDS      :
    case CM_MM_MPC_ALIGN_8WORDS      :
    case CM_MM_MPC_ALIGN_16WORDS     :
    case CM_MM_MPC_ALIGN_32WORDS     :
    case CM_MM_MPC_ALIGN_64WORDS     :
    case CM_MM_MPC_ALIGN_128WORDS    :
    case CM_MM_MPC_ALIGN_256WORDS    :
    case CM_MM_MPC_ALIGN_512WORDS    :
    case CM_MM_MPC_ALIGN_1024WORDS   :
    case CM_MM_MPC_ALIGN_65536BYTES  :
    //case CM_MM_MPC_ALIGN_16384WORDS  : maps to the same value as above
        break;
    default:
        OSAL_UNLOCK_API();
        return CM_INVALID_PARAMETER;
    }

    /* in case we allocate in tcm x be sure ee is load before */
    if ( memType == CM_MM_MPC_TCM16_X || memType == CM_MM_MPC_TCM24_X ||
         memType == CM_MM_MPC_TCM16_Y || memType == CM_MM_MPC_TCM24_Y )
    {
        t_cm_error error;
        if ((error = cm_CFG_CheckMpcStatus(cm_DM_GetDomainCoreId(domainId))) != CM_OK)
        {
            OSAL_UNLOCK_API();
            return error;
        }
    }

    /* alloc memory */
    *pHandle = (t_cm_memory_handle)cm_DM_Alloc(domainId, dspMemType, size, memAlignment, TRUE);
    if(*pHandle == (t_cm_memory_handle)INVALID_MEMORY_HANDLE)
    {
        OSAL_UNLOCK_API();
        ERROR("CM_NO_MORE_MEMORY: CM_AllocMpcMemory() failed\n", 0, 0, 0, 0, 0, 0);
        return CM_NO_MORE_MEMORY;
    }

    OSAL_UNLOCK_API();
    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_FreeMpcMemory(t_cm_memory_handle handle)
{
    t_cm_error error;
    t_memory_handle validHandle;
    t_nmf_core_id coreId;
    t_dsp_memory_type_id memType;

    OSAL_LOCK_API();

    if((error = cm_MM_getValidMemoryHandle(handle, &validHandle)) != CM_OK)
    {
        OSAL_UNLOCK_API();
        return error;
    }

    cm_DM_FreeWithInfo(validHandle, &coreId, &memType, TRUE);

    /* in case we allocate in tcm x be sure ee is load before */
    if ( memType == INTERNAL_XRAM16 || memType == INTERNAL_XRAM24 ||
            memType == INTERNAL_YRAM16 || memType == INTERNAL_YRAM24 )
    {
        cm_CFG_ReleaseMpc(coreId);
    }

    OSAL_UNLOCK_API();
    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetMpcMemorySystemAddress(t_cm_memory_handle handle, t_cm_system_address *pSystemAddress)
{
    t_cm_error error;
    t_memory_handle validHandle;

    OSAL_LOCK_API();

    if((error = cm_MM_getValidMemoryHandle(handle, &validHandle)) != CM_OK)
    {
        OSAL_UNLOCK_API();
        return error;
    }

    cm_DSP_GetHostSystemAddress(validHandle, pSystemAddress);

    OSAL_UNLOCK_API();
    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetMpcMemoryMpcAddress(t_cm_memory_handle handle, t_uint32 *pDspAddress)
{
    t_cm_error error;
    t_memory_handle validHandle;

    OSAL_LOCK_API();

    if((error = cm_MM_getValidMemoryHandle(handle, &validHandle)) != CM_OK)
    {
        OSAL_UNLOCK_API();
        return error;
    }

    cm_DSP_GetDspAddress(validHandle, pDspAddress);

    OSAL_UNLOCK_API();
    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetMpcMemoryStatus(t_nmf_core_id coreId, t_cm_mpc_memory_type memType, t_cm_allocator_status *pStatus)
{
    t_dsp_memory_type_id dspMemType;
    t_cm_error error;

    switch(memType)
    {
        case CM_MM_MPC_TCM16_X:
            dspMemType = INTERNAL_XRAM16;
            break;
        case CM_MM_MPC_TCM24_X:
            dspMemType = INTERNAL_XRAM24;
            break;
        case CM_MM_MPC_TCM16_Y:
            dspMemType = INTERNAL_YRAM16;
            break;
        case CM_MM_MPC_TCM24_Y:
            dspMemType = INTERNAL_YRAM24;
            break;
#ifndef __STN_8810
        case CM_MM_MPC_ESRAM16:
            dspMemType = ESRAM_EXT16;
            break;
        case CM_MM_MPC_ESRAM24:
            dspMemType = ESRAM_EXT24;
            break;
#endif /* ndef __STN_8810 */
        case CM_MM_MPC_SDRAM16:
            dspMemType = SDRAM_EXT16;
            break;
        case CM_MM_MPC_SDRAM24:
            dspMemType = SDRAM_EXT24;
            break;
        default:
            return CM_INVALID_PARAMETER;
    }

    OSAL_LOCK_API();
    error = cm_DSP_GetAllocatorStatus(coreId, dspMemType, 0, 0, pStatus);
    OSAL_UNLOCK_API();

    return error;
}

