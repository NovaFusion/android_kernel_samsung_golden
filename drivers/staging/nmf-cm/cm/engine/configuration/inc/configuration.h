/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_CONFIGURATION_H_
#define __INC_CONFIGURATION_H_

#include <cm/engine/api/control/configuration_engine.h>
#include <cm/engine/memory/inc/memory.h>
#include <inc/nmf-limits.h>
#include <cm/engine/dsp/inc/dsp.h>

/******************************************************************************/
/************************ FUNCTIONS PROTOTYPES ********************************/
/******************************************************************************/

PUBLIC t_cm_error cm_CFG_ConfigureMediaProcessorCore(t_nmf_core_id coreId,
        t_nmf_executive_engine_id executiveEngineId,
        t_nmf_semaphore_type_id semaphoreTypeId, t_uint8 nbYramBanks,
        const t_cm_system_address *mediaProcessorMappingBaseAddr,
        const t_cm_domain_id eeDomain,
        t_dsp_allocator_desc* sdramCodeAllocId,
        t_dsp_allocator_desc* sdramDataAllocId
        );

PUBLIC t_cm_error cm_CFG_AddMpcSdramSegment(const t_nmf_memory_segment *pDesc,
        const char *memoryname, t_dsp_allocator_desc **allocDesc);

PUBLIC t_cm_error cm_CFG_CheckMpcStatus(t_nmf_core_id coreId);

void cm_CFG_ReleaseMpc(t_nmf_core_id coreId);

#endif /* __INC_CONFIGURATION_H_ */
