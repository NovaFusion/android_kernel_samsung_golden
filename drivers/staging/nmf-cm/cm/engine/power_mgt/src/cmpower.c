/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include "../inc/power.h"

#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/convert.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>

// -------------------------------------------------------------------------------
// Compilation flags
// -------------------------------------------------------------------------------
#define __PWR_DEBUG_TRACE_LEVEL 2  					// Debug trave level for CM power module

// -------------------------------------------------------------------------------
// Internal counter to store the TCM allocated chunk (by MPC)
// -------------------------------------------------------------------------------
static t_uint32 _pwrMPCHWIPCountT[NB_CORE_IDS];

// -------------------------------------------------------------------------------
// Internal counter to store the TCM allocated chunk (by MPC)
// -------------------------------------------------------------------------------
static t_uint32 _pwrMPCMemoryCountT[NB_CORE_IDS];


// -------------------------------------------------------------------------------
// Internal data to store the global Power Manager mode (see cm_PWR_Init fct)
// -------------------------------------------------------------------------------
t_nmf_power_mode powerMode = NORMAL_PWR_MODE;

// -------------------------------------------------------------------------------
// cm_PWR_Init
// -------------------------------------------------------------------------------
PUBLIC t_cm_error cm_PWR_Init(void)
{
	int i;

	for (i=0; i<NB_CORE_IDS;i++)
	{
        _pwrMPCHWIPCountT[i] = 0;
        _pwrMPCMemoryCountT[i] = 0;
	}

	return CM_OK;
}

// -------------------------------------------------------------------------------
// cm_PWR_SetMode
// -------------------------------------------------------------------------------
void cm_PWR_SetMode(t_nmf_power_mode aMode)
{
	powerMode = aMode;
}

t_nmf_power_mode cm_PWR_GetMode()
{
    return powerMode;
}

t_uint32 cm_PWR_GetMPCMemoryCount(t_nmf_core_id coreId)
{
    return _pwrMPCMemoryCountT[coreId];
}


PUBLIC t_cm_error cm_PWR_EnableMPC(
        t_mpc_power_request             request,
        t_nmf_core_id                   coreId)
{
    t_cm_error error;

    switch(request)
    {
    case MPC_PWR_CLOCK:
        LOG_INTERNAL(__PWR_DEBUG_TRACE_LEVEL, "[Pwr] MPC %s enable clock\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);
        if((error = OSAL_EnablePwrRessource(CM_OSAL_POWER_SxA_CLOCK, coreId, 0)) != CM_OK)
        {
            ERROR("[Pwr] MPC %s clock can't be enabled\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);
            return error;
        }
        break;
    case MPC_PWR_AUTOIDLE:
        if((error = OSAL_EnablePwrRessource(CM_OSAL_POWER_SxA_AUTOIDLE, coreId, 0)) != CM_OK)
        {
            ERROR("[Pwr] MPC %s clock can't be auto-idle\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);
            return error;
        }
        break;
    case MPC_PWR_HWIP:
        if(_pwrMPCHWIPCountT[coreId]++ == 0)
        {
            LOG_INTERNAL(__PWR_DEBUG_TRACE_LEVEL, "[Pwr] MPC %s HW IP enable clock\n",cm_getDspName(coreId), 0, 0, 0, 0, 0);

            // The PRCMU seem not supporting the transition of asking HW IP on while DSP in retention
            // -> Thus force wake up of the MMDSP before asking the transition
            if ((error = cm_EEM_ForceWakeup(coreId)) != CM_OK)
                return error;

            if((error = OSAL_EnablePwrRessource(CM_OSAL_POWER_SxA_HARDWARE, coreId, 0)) != CM_OK)
            {
                ERROR("[Pwr] MPC %s HW IP clock can't be enabled\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);
                cm_EEM_AllowSleep(coreId);
                return error;
            }

            cm_EEM_AllowSleep(coreId);
        }
        break;
    }

    return CM_OK;
}

PUBLIC void cm_PWR_DisableMPC(
        t_mpc_power_request             request,
        t_nmf_core_id                   coreId)
{
    switch(request)
    {
    case MPC_PWR_CLOCK:
        LOG_INTERNAL(__PWR_DEBUG_TRACE_LEVEL, "[Pwr] MPC %s disable clock\n",cm_getDspName(coreId), 0, 0, 0, 0, 0);
        OSAL_DisablePwrRessource(CM_OSAL_POWER_SxA_CLOCK, coreId, 0);
        break;
    case MPC_PWR_AUTOIDLE:
        OSAL_DisablePwrRessource(CM_OSAL_POWER_SxA_AUTOIDLE, coreId, 0);
        break;
    case MPC_PWR_HWIP:
        if(--_pwrMPCHWIPCountT[coreId] == 0)
        {
            LOG_INTERNAL(__PWR_DEBUG_TRACE_LEVEL, "[Pwr] MPC %s HW IP disable clock\n",cm_getDspName(coreId), 0, 0, 0, 0, 0);

            // The PRCMU seem not supporting the transition of asking HW IP on while DSP in retention
            // -> Thus force wake up of the MMDSP before asking the transition
            if (cm_EEM_ForceWakeup(coreId) != CM_OK)
                return;

            OSAL_DisablePwrRessource(CM_OSAL_POWER_SxA_HARDWARE, coreId, 0);

            cm_EEM_AllowSleep(coreId);
        }
        break;
    }
}

PUBLIC t_cm_error cm_PWR_EnableHSEM(void)
{
    t_cm_error error;

    LOG_INTERNAL(__PWR_DEBUG_TRACE_LEVEL, "[Pwr] HSEM enable clock\n",0 , 0, 0, 0, 0, 0);
    if((error = OSAL_EnablePwrRessource(CM_OSAL_POWER_HSEM, 0, 0)) != CM_OK)
    {
        ERROR("[Pwr] HSEM clock can't be enabled\n", 0, 0, 0, 0, 0, 0);
        return error;
    }

    return CM_OK;
}

PUBLIC void cm_PWR_DisableHSEM(void)
{
    LOG_INTERNAL(__PWR_DEBUG_TRACE_LEVEL, "[Pwr] HSEM disable clock\n",0 , 0, 0, 0, 0, 0);
    OSAL_DisablePwrRessource(CM_OSAL_POWER_HSEM, 0, 0);
}

PUBLIC t_cm_error cm_PWR_EnableMemory(
        t_nmf_core_id                   coreId,
        t_dsp_memory_type_id            dspMemType,
        t_cm_physical_address           address,
        t_cm_size                       size)
{
    switch(dspMemType)
    {
    case INTERNAL_XRAM24:
    case INTERNAL_XRAM16:
    case INTERNAL_YRAM24:
    case INTERNAL_YRAM16:
        _pwrMPCMemoryCountT[coreId]++;
        break;
    case SDRAM_EXT24:
    case SDRAM_EXT16:
    case SDRAM_CODE:
    case LOCKED_CODE:
        return OSAL_EnablePwrRessource(
                CM_OSAL_POWER_SDRAM,
                address,
                size);
    case ESRAM_EXT24:
    case ESRAM_EXT16:
    case ESRAM_CODE:
        return OSAL_EnablePwrRessource(
                CM_OSAL_POWER_ESRAM,
                address,
                size);
    default:
	    CM_ASSERT(0);
    }

    return CM_OK;
}

PUBLIC void cm_PWR_DisableMemory(
        t_nmf_core_id                   coreId,
        t_dsp_memory_type_id            dspMemType,
        t_cm_physical_address           address,
        t_cm_size                       size)
{
    switch(dspMemType)
    {
    case INTERNAL_XRAM24:
    case INTERNAL_XRAM16:
    case INTERNAL_YRAM24:
    case INTERNAL_YRAM16:
        _pwrMPCMemoryCountT[coreId]--;
        break;
    case SDRAM_EXT24:
    case SDRAM_EXT16:
    case SDRAM_CODE:
    case LOCKED_CODE:
        OSAL_DisablePwrRessource(
                CM_OSAL_POWER_SDRAM,
                address,
                size);
        break;
    case ESRAM_EXT24:
    case ESRAM_EXT16:
    case ESRAM_CODE:
        OSAL_DisablePwrRessource(
                CM_OSAL_POWER_ESRAM,
                address,
                size);
        break;
    default:
	    CM_ASSERT(0);
    }
}





