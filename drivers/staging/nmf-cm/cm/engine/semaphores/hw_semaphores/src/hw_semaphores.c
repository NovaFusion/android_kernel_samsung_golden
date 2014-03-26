/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/******************************************************************* Includes
 ****************************************************************************/

#include "../inc/hw_semaphores.h"
#include <share/semaphores/inc/hwsem_hwp.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/power_mgt/inc/power.h>
static t_hw_semaphore_regs *pHwSemRegs = (t_hw_semaphore_regs *)0;

static t_uint32 semaphoreUseCounter = 0;
static t_uint32 imsc[HSEM_MAX_INTR];
PRIVATE void restoreMask(void);

/****************************************************************************/
/* NAME:   t_cm_error cm_HSEM_Init(const t_cm_system_address *pSystemAddr)     */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: Initialize the HW Semaphores module                         */
/*                                                                          */
/* PARAMETERS:                                                              */
/*       (in)   pSystemAddr: system base address of the HW semaphores IP    */
/*                                                                          */
/* RETURN:      CM_OK always                                                */
/*                                                                          */
/****************************************************************************/
PUBLIC t_cm_error cm_HSEM_Init(const t_cm_system_address *pSystemAddr)
{
    t_uint8 i;

    pHwSemRegs = (t_hw_semaphore_regs *)pSystemAddr->logical;

    for (i=HSEM_FIRST_INTR; i < HSEM_MAX_INTR; i++)
    {
        imsc[i] = 0; // Mask all interrupt
    }

    return CM_OK;
}

static void cm_HSEM_ReInit(void)
{
    t_uint8 i;

    pHwSemRegs->icrall = MASK_ALL16;

    for (i=HSEM_FIRST_INTR; i < HSEM_MAX_INTR; i++)
    {
        pHwSemRegs->it[i].imsc = imsc[i];
        pHwSemRegs->it[i].icr = MASK_ALL16;
    }

    for (i=0; i < NUM_HW_SEMAPHORES; i++)
    {
        pHwSemRegs->sem[i] = 0;
    }
}

/****************************************************************************/
/* NAME:   t_cm_error cm_HSEM_EnableSemIrq(                                */
/*                                          t_semaphore_id semId,           */
/*                                          t_nmf_core_id toCoreId           */
/*                                         )                                */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: Enable Irq for a given coreId (communication receiver)      */
/*                                                                          */
/* PARAMETERS:                                                              */
/*       (in)   semId: identifier of the semaphore                          */
/*       (in)   toCoreId: identifier of coreId destination of the coms      */
/*                                                                          */
/* RETURN:      CM_OK always                                                */
/*                                                                          */
/****************************************************************************/
PUBLIC t_cm_error cm_HSEM_EnableSemIrq(t_semaphore_id semId, t_nmf_core_id toCoreId)
{
    static t_uint32 CoreIdToIntr[NB_CORE_IDS] = {0, 2, 3};
    int i = CoreIdToIntr[toCoreId];

    imsc[i] |= (1UL << semId);

    // Allow cm_HSEM_EnableSemIrq to be called before real start in order to save power
    if(semaphoreUseCounter > 0)
    {
        pHwSemRegs->it[i].imsc = imsc[i];
    }

    return CM_OK;
}

/****************************************************************************/
/* NAME:  void cm_HSEM_GenerateIrq(t_semaphore_id semId)                    */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: Generate an irq toward correct core according to semId      */
/*                                                                          */
/* PARAMETERS:                                                              */
/*       (in)   semId: identifier of the semaphore to handle                */
/*                                                                          */
/* RETURN:      none                                                        */
/*                                                                          */
/****************************************************************************/
PUBLIC void cm_HSEM_GenerateIrq(t_nmf_core_id coreId, t_semaphore_id semId)
{
   // TODO Move restore in OS BSP or in PRCMU in order to to it only when wake-up, for now do it always !!!!!!!!!!!!
            restoreMask();

    pHwSemRegs->sem[semId] = CORE_ID_2_HW_CORE_ID(ARM_CORE_ID);
    pHwSemRegs->sem[semId] = (HSEM_INTRA_MASK|HSEM_INTRB_MASK|HSEM_INTRC_MASK|HSEM_INTRD_MASK);
}

/****************************************************************************/
/* NAME:  t_nmf_core_id cm_HSEM_GetCoreIdFromIrqSrc(void)                   */
/*--------------------------------------------------------------------------*/
/* DESCRIPTION: Check Masked Interrupt Status to know which semaphore(s)    */
/*        have pending interrupt and return the identifier of the given dsp */
/*                                                                          */
/* PARAMETERS:  none                                                        */
/*                                                                          */
/* RETURN:      none                                                        */
/*                                                                          */
/****************************************************************************/
PUBLIC t_nmf_core_id cm_HSEM_GetCoreIdFromIrqSrc(void)
{
    t_uword misValue = pHwSemRegs->it[ARM_CORE_ID].mis;
    t_uint32 mask = 1 << FIRST_NEIGHBOR_SEMID(ARM_CORE_ID) /* == 0 here */;
    t_nmf_core_id coreId = FIRST_MPC_ID;

    while ((misValue & mask) == 0)
    {
        mask <<= 1;

        coreId++;
        if(coreId > LAST_MPC_ID)
            return coreId;
    }

    /* Acknowledge Hsem interrupt */
    pHwSemRegs->it[ARM_CORE_ID].icr = mask;

    return coreId;
}

PUBLIC t_cm_error cm_HSEM_PowerOn(t_nmf_core_id coreId)
{
    if(semaphoreUseCounter++ == 0)
    {
        cm_PWR_EnableHSEM();

        cm_HSEM_ReInit();  // HSEM is called one time only when the HSEM is switched ON
    }

    return CM_OK;
}

PUBLIC void cm_HSEM_PowerOff(t_nmf_core_id coreId)
{
    if(--semaphoreUseCounter == 0)
    {
        cm_PWR_DisableHSEM();
    }
}

PRIVATE void restoreMask()
{
    t_uint8 i;
    
    for (i=HSEM_FIRST_INTR; i < HSEM_MAX_INTR; i++)
        pHwSemRegs->it[i].imsc = imsc[i];
}
