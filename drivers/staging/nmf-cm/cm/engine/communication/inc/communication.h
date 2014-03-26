/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Components Management internal methods - Communication part.
 *
 */
#ifndef __INC_NMF_COM
#define __INC_NMF_COM

#include <cm/inc/cm_type.h>
#include <cm/engine/communication/fifo/inc/nmf_fifo_arm.h>
#include <cm/engine/memory/inc/memory.h>

#include <cm/engine/communication/inc/communication_type.h>

extern t_dsp_memory_type_id comsLocation;
extern t_dsp_memory_type_id paramsLocation;
extern t_dsp_memory_type_id extendedFieldLocation;

PUBLIC t_cm_error cm_COM_Init(t_nmf_coms_location comsLocation);
PUBLIC t_cm_error cm_COM_AllocateMpc(t_nmf_core_id coreId);
PUBLIC void cm_COM_InitMpc(t_nmf_core_id coreId);
PUBLIC void cm_COM_FreeMpc(t_nmf_core_id coreId);

PUBLIC t_cm_error cm_PushEventTrace(t_nmf_fifo_arm_desc*, t_event_params_handle h, t_uint32 methodIndex, t_uint32 isTrace);
PUBLIC t_cm_error cm_PushEvent(t_nmf_fifo_arm_desc *pArmFifo, t_event_params_handle h, t_uint32 methodIndex);
PUBLIC void cm_AcknowledgeEvent(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC t_event_params_handle cm_AllocEvent(t_nmf_fifo_arm_desc *pArmFifo);

/*!
 * \internal
 * \brief Definition of custom value for userTHIS parameter of PostDfc OSAL call
 *
 * This value is used as 1st parameter of a pPostDfc call to indicate that a given interrupt is linked to an internal Component Manager event
 */
#define NMF_INTERNAL_USERTHIS ((void*)MASK_ALL32)

typedef void (*t_callback_method)(t_nmf_core_id coreId, t_event_params_handle pParam);

#endif /* __INC_NMF_COM */
