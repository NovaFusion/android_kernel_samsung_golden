/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_NMF_SEMAPHORE_H
#define __INC_NMF_SEMAPHORE_H

#include <cm/engine/api/control/configuration_engine.h>
#include <share/semaphores/inc/semaphores.h>
#include <cm/engine/semaphores/hw_semaphores/inc/hw_semaphores.h>

PUBLIC t_cm_error cm_SEM_Init(const t_cm_system_address *pSystemAddr);
PUBLIC t_cm_error cm_SEM_InitMpc(t_nmf_core_id coreId, t_nmf_semaphore_type_id semTypeId);
PUBLIC t_semaphore_id cm_SEM_Alloc(t_nmf_core_id fromCoreId, t_nmf_core_id toCoreId);

/* Semaphores management virtualized functions */
extern void (*cm_SEM_GenerateIrq[NB_CORE_IDS])(t_nmf_core_id coreId, t_semaphore_id semId);
extern t_cm_error (*cm_SEM_PowerOn[NB_CORE_IDS])(t_nmf_core_id coreId);
extern void (*cm_SEM_PowerOff[NB_CORE_IDS])(t_nmf_core_id coreId);

#endif /* __INC_NMF_SEMAPHORE_H */
