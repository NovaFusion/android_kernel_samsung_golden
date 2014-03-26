/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#ifndef __INC_CM_XTITRACE_H
#define __INC_CM_XTITRACE_H

#include <cm/engine/component/inc/instance.h>

#include <inc/nmf-tracedescription.h>

extern t_bool cm_trace_enabled;
#define TRACE_BUFFER_SIZE                          128

/*************************/
/* Trace   related stuff */
/*************************/
void cm_TRC_Dump(void);

void cm_TRC_traceReset(void);

void cm_TRC_traceLoadMap(
        t_nmfTraceComponentCommandDescription cmd,
        const t_component_instance* component);

#define ARM_TRACE_COMPONENT ((const t_component_instance*)0xFFFFFFFF)

void cm_TRC_traceBinding(
        t_nmfTraceBindCommandDescription command,
        const t_component_instance* clientComponent, const t_component_instance* serverComponent,
        const char *requiredItfName, const char *providedItfName);

void cm_TRC_traceCommunication(
        t_nmfTraceCommunicationCommandDescription command,
        t_nmf_core_id coreId,
        t_nmf_core_id remoteCoreId);

void cm_TRC_traceMemAlloc(t_nmfTraceAllocatorCommandDescription command, t_uint8 allocId, t_uint32 memorySize, const char *allocname);

void cm_TRC_traceMem(t_nmfTraceAllocCommandDescription command, t_uint8 allocId, t_uint32 startAddress, t_uint32 memorySize);

/*************************/
/* MMDSP trace buffer    */
/*************************/
PUBLIC t_cm_error cm_SRV_allocateTraceBufferMemory(t_nmf_core_id coreId, t_cm_domain_id domainId);
PUBLIC t_cm_error cm_SRV_configureTraceBufferMemory(t_nmf_core_id coreId);
PUBLIC void cm_SRV_saveTraceBufferMemory(t_nmf_core_id coreId);
PUBLIC void cm_SRV_freeTraceBufferMemory(t_nmf_core_id coreId);



#endif /* __INC_CM_TRACE_H */
