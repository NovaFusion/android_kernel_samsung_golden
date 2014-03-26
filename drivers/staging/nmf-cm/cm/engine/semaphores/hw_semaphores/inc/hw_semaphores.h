/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_HW_SEMA_H_
#define __INC_HW_SEMA_H_

#include <cm/inc/cm_type.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <share/semaphores/inc/hwsem_hwp.h>


/******************************************************************************/
/************************ FUNCTIONS PROTOTYPES ********************************/
/******************************************************************************/

PUBLIC t_cm_error cm_HSEM_Init(const t_cm_system_address *pSystemAddr);
PUBLIC t_cm_error cm_HSEM_EnableSemIrq(t_semaphore_id semId, t_nmf_core_id toCoreId);
PUBLIC void cm_HSEM_Take(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC void cm_HSEM_Give(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC void cm_HSEM_GiveWithInterruptGeneration(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC void cm_HSEM_GenerateIrq(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC t_nmf_core_id cm_HSEM_GetCoreIdFromIrqSrc(void);

PUBLIC t_cm_error cm_HSEM_PowerOn(t_nmf_core_id coreId);
PUBLIC void cm_HSEM_PowerOff(t_nmf_core_id coreId);

#endif /* __INC_HW_SEMA_H_ */
