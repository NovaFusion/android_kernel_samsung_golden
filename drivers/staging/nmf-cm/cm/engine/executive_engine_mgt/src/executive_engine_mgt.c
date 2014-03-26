/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*----------------------------------------------------------------------------*
 * This module provides functions that allow to manage DSPs' Firmwares.       *
 ******************************************************************************/


/******************************************************************* Includes
 ****************************************************************************/

#include "../inc/executive_engine_mgt.h"
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/utils/inc/convert.h>
#include <cm/engine/component/inc/initializer.h>
#include <cm/engine/power_mgt/inc/power.h>
#include <cm/engine/perfmeter/inc/mpcload.h>

#include <cm/engine/trace/inc/xtitrace.h>

#include <share/communication/inc/nmf_service.h>

t_ee_state eeState[NB_CORE_IDS];

/****************************************************************** Functions
 ****************************************************************************/
static t_cm_error cm_EEM_allocPanicArea(t_nmf_core_id coreId, t_cm_domain_id domainId);
static void cm_EEM_freePanicArea(t_nmf_core_id coreId);

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetExecutiveEngineHandle(
        t_cm_domain_id domainId,
        t_cm_instance_handle *executiveEngineHandle)
{
    t_nmf_core_id coreId;

    if (cm_DM_CheckDomain(domainId, DOMAIN_NORMAL) != CM_OK) {
        return CM_INVALID_DOMAIN_HANDLE;
    }

    coreId = cm_DM_GetDomainCoreId(domainId);
    //in case someone ask for ee on component manager !!!!
    if (coreId == ARM_CORE_ID) {*executiveEngineHandle = 0;}
    else {*executiveEngineHandle = eeState[coreId].instance->instance;}

    return CM_OK;
}

PUBLIC t_cm_error cm_EEM_Init(
    t_nmf_core_id coreId,
    const char *eeName,
    t_nmf_executive_engine_id executiveEngineId)
{
    t_rep_component *pRepComponent;
    t_cm_error error;
    t_uint32 i;

    eeState[coreId].instance = (t_component_instance *)0;
    eeState[coreId].executiveEngineId = executiveEngineId;
    for(i = NMF_SCHED_BACKGROUND; i < NMF_SCHED_URGENT + 1;i++)
    {
        eeState[coreId].currentStackSize[i] = MIN_STACK_SIZE;
    }

    // Try to load component file
    if((error = cm_REP_lookupComponent(eeName, &pRepComponent)) != CM_OK)
    {
        if (error == CM_COMPONENT_NOT_FOUND)
            ERROR("CM_COMPONENT_NOT_FOUND: Execution Engine %s\n", eeName, 0, 0, 0, 0, 0);
        return error;
    }

    // Set to 1 during bootstrap since MMDSP forceWakeup is to one also in order to not go in idle state
    // while configuration not finish !!!
    eeState[coreId].nbOfForceWakeup = 1;

    if ((error = cm_DSP_Boot(coreId)) != CM_OK)
        return error;

    if((error = cm_instantiateComponent(
            eeName,
            cm_DSP_GetState(coreId)->domainEE,
            NMF_SCHED_URGENT,
            eeName,
            pRepComponent->elfhandle,
            &eeState[coreId].instance)) != CM_OK)
    {
        cm_DSP_Shutdown(coreId);
        return error;
    }

    /* Get Void Function */
    eeState[coreId].voidAddr = cm_getFunction(eeState[coreId].instance, "helper", "Void");

    /* allocate xram space for stack */
    if (executiveEngineId == SYNCHRONOUS_EXECUTIVE_ENGINE)
    {
        error = cm_DSP_setStackSize(coreId, MIN_STACK_SIZE);
    }
    else
    {
        error = cm_DSP_setStackSize(coreId, (NMF_SCHED_URGENT + 1) * MIN_STACK_SIZE);
    }
    if (error != CM_OK)
    {
        cm_delayedDestroyComponent(eeState[coreId].instance);
        eeState[coreId].instance = (t_component_instance *)0;
        cm_DSP_Shutdown(coreId);
        return error;
    }

    /* allocate sdram memory for panic area */
    error = cm_EEM_allocPanicArea(coreId, cm_DSP_GetState(coreId)->domainEE);
    if (error != CM_OK) {
        cm_delayedDestroyComponent(eeState[coreId].instance);
        eeState[coreId].instance = (t_component_instance *)0;
        cm_DSP_Shutdown(coreId);
        return error;
    }

    /* allocate sdram memory to share perfmeters data */
    error = cm_PFM_allocatePerfmeterDataMemory(coreId, cm_DSP_GetState(coreId)->domainEE);
    if (error != CM_OK) {
        cm_EEM_freePanicArea(coreId);
        cm_delayedDestroyComponent(eeState[coreId].instance);
        eeState[coreId].instance = (t_component_instance *)0;
        cm_DSP_Shutdown(coreId);
        return error;
    }

    if((error = cm_SRV_configureTraceBufferMemory(coreId)) != CM_OK)
    {
        cm_PFM_deallocatePerfmeterDataMemory(coreId);
        cm_EEM_freePanicArea(coreId);
        cm_delayedDestroyComponent(eeState[coreId].instance);
        eeState[coreId].instance = (t_component_instance *)0;
        cm_DSP_Shutdown(coreId);
        return error;
    }

    /* set initial stack value */
    cm_writeAttribute(eeState[coreId].instance, "rtos/scheduler/topOfStack", cm_DSP_getStackAddr(coreId));

    /* set myCoreId for trace */
    cm_writeAttribute(eeState[coreId].instance, "xti/myCoreId", coreId - 1);
    
#if defined(__STN_8500) && (__STN_8500 > 10)
    /* set myCoreId for prcmu if exist */
    cm_writeAttribute(eeState[coreId].instance, "sleep/prcmu/myCoreId", coreId + 1);
#endif

    /* go go go ... */
    cm_DSP_Start(coreId);
    
    /* Waiting for End Of Boot */
    //TODO : remove infinite while loop
    //TODO : to be paranoiac, add a read to serviceReasonOffset before starting core and check value is MPC_SERVICE_BOOT as it should be
    {
        while(cm_readAttributeNoError(eeState[coreId].instance, "rtos/commonpart/serviceReason") == MPC_SERVICE_BOOT)
        {
            volatile t_uint32 i;
            for (i=0; i < 1000; i++);
        }
    }

    /* set some attributes after boot to avoid being erase by mmdsp boot */
    cm_writeAttribute(eeState[coreId].instance, "xti/traceActive", eeState[coreId].traceState);
    cm_writeAttribute(eeState[coreId].instance, "rtos/commonpart/printLevel", eeState[coreId].printLevel);

    cm_DSP_ConfigureAfterBoot(coreId);

    return CM_OK;
}

/****************************************************************************/
/* NAME:        cm_EEM_Close                                                */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: Inform us that ee for coreId has been destroyed             */
/*                                                                          */
/* PARAMETERS:  id: dsp identifier                                          */
/*                                                                          */
/* RETURN:      none                                                        */
/*                                                                          */
/****************************************************************************/
PUBLIC void cm_EEM_Close(t_nmf_core_id coreId)
{
    cm_SRV_saveTraceBufferMemory(coreId);
    cm_DSP_setStackSize(coreId, 0);
    cm_delayedDestroyComponent(eeState[coreId].instance);
    eeState[coreId].instance = (t_component_instance *)0;
    cm_PFM_deallocatePerfmeterDataMemory(coreId);
    cm_EEM_freePanicArea(coreId);
    cm_DSP_Shutdown(coreId);
}

/****************************************************************************/
/* NAME: cm_EEM_isStackUpdateNeed(                                          */
/*                                t_nmf_core_id id,                         */
/*                                t_nmf_ee_priority priority,               */
/*                                t_uint32 isInstantiate,                   */
/*                                t_uint32 needMinStackSize                 */
/*                               )                                          */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: Return a boolean to inform if a ee stack size update is need*/
/*              when instantiate or destroying a component                  */
/****************************************************************************/
PUBLIC t_uint32 cm_EEM_isStackUpdateNeed(
    t_nmf_core_id coreId,
    t_nmf_ee_priority priority,
    t_uint32 isInstantiate,
    t_uint32 needMinStackSize)
{
    /* in case of SYNCHRONOUS_EXECUTIVE_ENGINE we only use currentStackSize[NMF_SCHED_BACKGROUND] */
    if (eeState[coreId].executiveEngineId == SYNCHRONOUS_EXECUTIVE_ENGINE) {priority = NMF_SCHED_BACKGROUND;}
    if (isInstantiate)
    {
        if (needMinStackSize > eeState[coreId].currentStackSize[priority]) {return TRUE;}
    }
    else
    {
        if (needMinStackSize == eeState[coreId].currentStackSize[priority]) {return TRUE;}
    }

    return FALSE;
}

/****************************************************************************/
/* NAME: cm_EEM_UpdateStack(                                                */
/*                          t_nmf_core_id id,                               */
/*                          t_nmf_ee_priority priority,                     */
/*                          t_uint32 needMinStackSize,                      */
/*                          t_uint32 *pNewStackValue                        */
/*                         )                                                */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: If cm_EEM_isStackUpdateNeed() has return true then caller   */
/*              must inform EEM about new stack value for priority.         */
/*              cm_EEM_UpdateStack() will return new global stack size to   */
/*              provide to ee.                                              */
/****************************************************************************/
PUBLIC t_cm_error cm_EEM_UpdateStack(
    t_nmf_core_id coreId,
    t_nmf_ee_priority priority,
    t_uint32 needMinStackSize,
    t_uint32 *pNewStackValue)
{
    t_cm_error error;
    t_uint32 recoveryStackSize = eeState[coreId].currentStackSize[priority];
    t_uint32 i;

    /* in case of SYNCHRONOUS_EXECUTIVE_ENGINE we only use currentStackSize[NMF_SCHED_BACKGROUND] */
    if (eeState[coreId].executiveEngineId == SYNCHRONOUS_EXECUTIVE_ENGINE) {priority = NMF_SCHED_BACKGROUND;}
    eeState[coreId].currentStackSize[priority] = needMinStackSize;
    if (eeState[coreId].executiveEngineId == SYNCHRONOUS_EXECUTIVE_ENGINE) {*pNewStackValue = needMinStackSize;}
    else
    {
        *pNewStackValue = 0;
        for(i = NMF_SCHED_BACKGROUND; i < NMF_SCHED_URGENT + 1;i++)
        {
            *pNewStackValue += eeState[coreId].currentStackSize[i];
        }
    }

    /* try to increase size of stack by modifying xram allocator size */
    error = cm_DSP_setStackSize(coreId, *pNewStackValue);
    if (error != CM_OK) {
        eeState[coreId].currentStackSize[priority] = recoveryStackSize;
    } else {
        LOG_INTERNAL(1, "\n##### Stack update: size=%d, prio=%d on %s #####\n", *pNewStackValue, priority, cm_getDspName(coreId), 0, 0, 0);
    }

    return error;
}

/****************************************************************************/
/* NAME: t_nmf_executive_engine_id(                                         */
/*                          t_nmf_core_id id                                */
/*                         )                                                */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: return executive engine load on id core.                    */
/****************************************************************************/
PUBLIC t_ee_state * cm_EEM_getExecutiveEngine(t_nmf_core_id coreId)
{
    return &eeState[coreId];
}

/****************************************************************************/
/* NAME: cm_EEM_setTraceMode(                                               */
/*                          t_nmf_core_id id,                               */
/*                          t_uint32 state                                  */
/*                         )                                                */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: activate/deactivate trace for ee running on id. In case ee  */
/*          is not yet load then information is store.                      */
/****************************************************************************/
PUBLIC void cm_EEM_setTraceMode(t_nmf_core_id coreId, t_uint32 state)
{
    eeState[coreId].traceState = state;
    if (eeState[coreId].instance)
    {
        if(cm_EEM_ForceWakeup(coreId) == CM_OK)
        {
            cm_writeAttribute(eeState[coreId].instance, "xti/traceActive", eeState[coreId].traceState);

            cm_EEM_AllowSleep(coreId);
        }
    }
}

/****************************************************************************/
/* NAME: cm_EEM_setPrintLevel(                                              */
/*                          t_nmf_core_id id,                               */
/*                          t_uint32 level                                  */
/*                         )                                                */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: set print level for ee running on id. In case ee            */
/*          is not yet load then information is store.                      */
/****************************************************************************/
PUBLIC void cm_EEM_setPrintLevel(t_nmf_core_id coreId, t_uint32 level)
{
    eeState[coreId].printLevel = level;
    if (eeState[coreId].instance)
    {
        if(cm_EEM_ForceWakeup(coreId) == CM_OK)
        {
            cm_writeAttribute(eeState[coreId].instance, "rtos/commonpart/printLevel", eeState[coreId].printLevel);

            cm_EEM_AllowSleep(coreId);
        }
    }
}

t_cm_error cm_EEM_ForceWakeup(t_nmf_core_id coreId)
{
    if(eeState[coreId].nbOfForceWakeup++ == 0)
    {
        t_cm_error error;

        LOG_INTERNAL(1, "ARM: Try to wake up on core id : %d\n", coreId, 0, 0, 0, 0, 0);

        if (cm_DSP_GetState(coreId)->state != MPC_STATE_BOOTED)
        {
            return CM_MPC_NOT_RESPONDING;
        }
        else if ((error = cm_COMP_ULPForceWakeup(coreId)) != CM_OK)
        {
            if (error == CM_MPC_NOT_RESPONDING) {
                if(cm_DSP_GetState(coreId)->state == MPC_STATE_PANIC)
                    /* Don't print error which has been done by Panic handling */;
                else
                {
                    ERROR("CM_MPC_NOT_RESPONDING: DSP %s can't be wakeup'ed\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);
                    cm_DSP_SetStatePanic(coreId);
                }
            }
            return error;
        }
    }
    else
        LOG_INTERNAL(1, "ARM: Not Try to wake up on core id : %d (nbOfForceWakeup = %d)\n", coreId, eeState[coreId].nbOfForceWakeup, 0, 0, 0, 0);

    return CM_OK;
}

void cm_EEM_AllowSleep(t_nmf_core_id coreId)
{
    if(--eeState[coreId].nbOfForceWakeup == 0)
    {
        LOG_INTERNAL(1, "ARM: Allow sleep on core id : %d\n", coreId, 0, 0, 0, 0, 0);

        if (cm_DSP_GetState(coreId)->state != MPC_STATE_BOOTED)
        {
        }
        else if (cm_COMP_ULPAllowSleep(coreId) != CM_OK)
        {
            ERROR("CM_MPC_NOT_RESPONDING: DSP %s can't be allow sleep'ed\n", cm_getDspName(coreId), 0, 0, 0, 0, 0);
        }
    }
    else
        LOG_INTERNAL(1, "ARM: Not Allow sleep on core id : %d (nbOfForceWakeup = %d)\n", coreId, eeState[coreId].nbOfForceWakeup, 0, 0, 0, 0);
}

/* internal api */
t_cm_error cm_EEM_allocPanicArea(t_nmf_core_id coreId, t_cm_domain_id domainId)
{
    t_cm_error error = CM_OK;

    eeState[coreId].panicArea.handle = cm_DM_Alloc(cm_DSP_GetState(coreId)->domainEE, SDRAM_EXT24, 45 /* 42 registers, pc, 2 magic words */,CM_MM_ALIGN_WORD, TRUE);
    if (eeState[coreId].panicArea.handle == INVALID_MEMORY_HANDLE)
        error = CM_NO_MORE_MEMORY;
    else {
        t_uint32 mmdspAddr;

        eeState[coreId].panicArea.addr = cm_DSP_GetHostLogicalAddress(eeState[coreId].panicArea.handle);
        cm_DSP_GetDspAddress(eeState[coreId].panicArea.handle, &mmdspAddr);

        cm_writeAttribute(eeState[coreId].instance, "rtos/commonpart/panicDataAddr", mmdspAddr);
    }

    return error;
}

void cm_EEM_freePanicArea(t_nmf_core_id coreId)
{
    eeState[coreId].panicArea.addr = 0;
    cm_DM_Free(eeState[coreId].panicArea.handle, TRUE);
}
