/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_DSP_EVENT
#define __INC_DSP_EVENT

#include <cm/inc/cm_type.h>
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/memory/inc/memory.h>

/* value should be size of t_remote_event in mmdsp word */
#define DSP_REMOTE_EVENT_SIZE_IN_DSPWORD 5

t_cm_error dspevent_createDspEventFifo(
    const t_component_instance *pComp,
    const char* nameOfTOP,
    t_uint32 fifoNbElem,
    t_uint32 fifoElemSizeInWord,
    t_dsp_memory_type_id dspEventMemType,
    t_memory_handle *pHandle);
void dspevent_destroyDspEventFifo(t_memory_handle handle);

#endif /* __INC_DSP_EVENT */
