/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/inc/cm_type.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <cm/engine/semaphores/hw_semaphores/inc/hw_semaphores.h>
#include <cm/engine/dsp/inc/semaphores_dsp.h>
#include <cm/engine/trace/inc/trace.h>
#include <share/inc/nmf.h>

void (*cm_SEM_GenerateIrq[NB_CORE_IDS])(t_nmf_core_id coreId, t_semaphore_id semId);
t_cm_error (*cm_SEM_PowerOn[NB_CORE_IDS])(t_nmf_core_id coreId);
void (*cm_SEM_PowerOff[NB_CORE_IDS])(t_nmf_core_id coreId);

#define SEM_TYPE_ID_DEFAULT_VALUE ((t_nmf_semaphore_type_id)MASK_ALL32)
static t_nmf_semaphore_type_id semaphoreTypePerCoreId[NB_CORE_IDS];

static t_cm_error cm_LSEM_PowerOn(t_nmf_core_id coreId)
{
    return CM_OK;
}

static void cm_LSEM_PowerOff(t_nmf_core_id coreId)
{
}

PUBLIC t_cm_error cm_SEM_Init(const t_cm_system_address *pSystemAddr)
{
    t_nmf_core_id coreId;

    for (coreId = ARM_CORE_ID; coreId < NB_CORE_IDS; coreId++)
    {
        semaphoreTypePerCoreId[coreId] = SEM_TYPE_ID_DEFAULT_VALUE;

        /* By default, we suppose that we use a full feature NMF ;) */
        cm_SEM_GenerateIrq[coreId] = NULL;
        cm_SEM_PowerOn[coreId] = NULL;
        cm_SEM_PowerOff[coreId] = NULL;
   }

    cm_HSEM_Init(pSystemAddr);
    /* if needed local semaphore init will be done coreId per coreId */

    return CM_OK;
}

PUBLIC t_cm_error cm_SEM_InitMpc(t_nmf_core_id coreId, t_nmf_semaphore_type_id semTypeId)
{
    if (semaphoreTypePerCoreId[coreId] != SEM_TYPE_ID_DEFAULT_VALUE)
        return CM_MPC_ALREADY_INITIALIZED;

    if(semTypeId == SYSTEM_SEMAPHORES)
    {
        cm_SEM_GenerateIrq[coreId] = cm_HSEM_GenerateIrq;
        cm_SEM_PowerOn[coreId] = cm_HSEM_PowerOn;
        cm_SEM_PowerOff[coreId] = cm_HSEM_PowerOff;
    }
    else if (semTypeId == LOCAL_SEMAPHORES)
    {
        cm_SEM_GenerateIrq[coreId] = cm_DSP_SEM_GenerateIrq;
        cm_SEM_PowerOn[coreId] = cm_LSEM_PowerOn;
        cm_SEM_PowerOff[coreId] = cm_LSEM_PowerOff;
    }

    semaphoreTypePerCoreId[coreId] =  semTypeId;

    return CM_OK;
}

PUBLIC t_semaphore_id cm_SEM_Alloc(t_nmf_core_id fromCoreId, t_nmf_core_id toCoreId)
{
    t_semaphore_id semId;
    t_nmf_core_id corex;

    semId = FIRST_NEIGHBOR_SEMID(toCoreId);
    for (corex = FIRST_CORE_ID; corex < fromCoreId; corex++)
    {
        if (corex == toCoreId)
            continue;
        semId++;
    }

    if (
            (toCoreId == ARM_CORE_ID && semaphoreTypePerCoreId[fromCoreId] == SYSTEM_SEMAPHORES) ||
            (semaphoreTypePerCoreId[toCoreId] == SYSTEM_SEMAPHORES)
       )
    {
        cm_HSEM_EnableSemIrq(semId, toCoreId);
    }

    return semId;
}
