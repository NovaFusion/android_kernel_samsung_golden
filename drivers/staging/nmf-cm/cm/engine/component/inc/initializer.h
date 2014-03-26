/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_NMF_INITIALIZER
#define __INC_NMF_INITIALIZER

#include <cm/inc/cm_type.h>
#include <cm/engine/component/inc/instance.h>
#include <share/communication/inc/initializer.h>

PUBLIC t_cm_error cm_COMP_INIT_Init(t_nmf_core_id coreId);
PUBLIC t_cm_error cm_COMP_CallService(int serviceIndex, t_component_instance *pComp, t_uint32 methodAddress);
PUBLIC void cm_COMP_Flush(t_nmf_core_id coreId);
PUBLIC void cm_COMP_INIT_Close(t_nmf_core_id coreId);
PUBLIC t_cm_error cm_COMP_UpdateStack(t_nmf_core_id coreId, t_uint32 stackSize);
PUBLIC t_cm_error cm_COMP_ULPForceWakeup(t_nmf_core_id coreId);
PUBLIC t_cm_error cm_COMP_ULPAllowSleep(t_nmf_core_id coreId);
PUBLIC t_cm_error cm_COMP_InstructionCacheLock(t_nmf_core_id coreId, t_uint32 mmdspAddr, t_uint32 mmdspSize);
PUBLIC t_cm_error cm_COMP_InstructionCacheUnlock(t_nmf_core_id coreId, t_uint32 mmdspAddr, t_uint32 mmdspSize);


PUBLIC void processAsyncAcknowledge(t_nmf_core_id coreId, t_event_params_handle pParam);
PUBLIC void processSyncAcknowledge(t_nmf_core_id coreId, t_event_params_handle pParam);

#endif /* __INC_NMF_INITIALIZER */
