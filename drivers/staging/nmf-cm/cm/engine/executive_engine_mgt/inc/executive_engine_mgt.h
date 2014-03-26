/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_EE_MGT_H
#define __INC_EE_MGT_H

#include <cm/engine/component/inc/instance.h>
#include <cm/engine/dsp/inc/dsp.h>

typedef struct {
    t_component_instance        *instance;
    t_nmf_executive_engine_id   executiveEngineId;
    t_uint32                    currentStackSize[NMF_SCHED_URGENT + 1];
    t_uint32                    voidAddr;
    t_uint32                    traceState;
    t_uint32                    printLevel;
    t_uint32                    nbOfForceWakeup;
    struct {
        t_memory_handle         handle;
        t_cm_logical_address    addr;
    } panicArea;

    // Trace Management
    t_uint32                    traceBufferSize;
    t_uint32                    writeTracePointer;
    t_uint32                    lastWrittenTraceRevision;
    t_memory_handle             traceDataHandle;
    struct t_nmf_trace          *traceDataAddr;
} t_ee_state;

extern t_ee_state eeState[NB_CORE_IDS];

/******************************************************************************/
/************************ FUNCTIONS PROTOTYPES ********************************/
/******************************************************************************/

PUBLIC t_cm_error cm_EEM_Init(t_nmf_core_id coreId, const char *eeName, t_nmf_executive_engine_id executiveEngineId);
PUBLIC void cm_EEM_Close(t_nmf_core_id coreId);
PUBLIC t_uint32 cm_EEM_isStackUpdateNeed(t_nmf_core_id coreId, t_nmf_ee_priority priority, t_uint32 isInstantiate, t_uint32 needMinStackSize);
PUBLIC t_cm_error cm_EEM_UpdateStack(t_nmf_core_id coreId, t_nmf_ee_priority priority, t_uint32 needMinStackSize, t_uint32 *pNewStackValue);
PUBLIC t_ee_state* cm_EEM_getExecutiveEngine(t_nmf_core_id coreId);
PUBLIC void cm_EEM_setTraceMode(t_nmf_core_id coreId, t_uint32 state);
PUBLIC void cm_EEM_setPrintLevel(t_nmf_core_id coreId, t_uint32 level);
t_cm_error cm_EEM_ForceWakeup(t_nmf_core_id coreId);
void cm_EEM_AllowSleep(t_nmf_core_id coreId);

#endif /* __INC_EE_MGT_H */
