/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/inc/cm_type.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/communication/inc/communication.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/dsp/mmdsp/inc/mmdsp_hwp.h>
#include <cm/engine/trace/inc/trace.h>
#include "../inc/dspevent.h"


#define DSP_REMOTE_EVENT_SIZE_IN_BYTE (4*DSP_REMOTE_EVENT_SIZE_IN_DSPWORD)
#define DSP_REMOTE_EVENT_NEXT_FIELD_OFFSET      0
#define DSP_REMOTE_EVENT_REACTION_FIELD_OFFSET  1
#define DSP_REMOTE_EVENT_THIS_FIELD_OFFSET      2
#define DSP_REMOTE_EVENT_PRIORITY_FIELD_OFFSET  3
#define DSP_REMOTE_EVENT_DATA_FIELD_OFFSET      4

t_cm_error dspevent_createDspEventFifo(
    const t_component_instance *pComp,
    const char* nameOfTOP,
    t_uint32 fifoNbElem,
    t_uint32 fifoElemSizeInWord,
    t_dsp_memory_type_id dspEventMemType,
    t_memory_handle *pHandle)
{
    t_uint32 dspElementAddr;
    t_uint32 *elemAddr32;
    int i;

    // Allocate fifo
    *pHandle = cm_DM_Alloc(pComp->domainId, dspEventMemType, fifoNbElem*fifoElemSizeInWord, CM_MM_ALIGN_2WORDS, TRUE);
    if(*pHandle == INVALID_MEMORY_HANDLE) {
        ERROR("CM_NO_MORE_MEMORY: dspevent_createDspEventFifo()\n", 0, 0, 0, 0, 0, 0);
        return 	CM_NO_MORE_MEMORY;
    }

    cm_DSP_GetDspAddress(*pHandle, &dspElementAddr);

    elemAddr32 = (t_uint32*)cm_DSP_GetHostLogicalAddress(*pHandle);

    LOG_INTERNAL(2, "\n##### FIFO (dsp event): ARM=0x%x DSP=0x%x\n", elemAddr32, dspElementAddr, 0, 0, 0, 0);

    // Read attribute addr (we assume that variable in XRAM)
    cm_writeAttribute(pComp, nameOfTOP, dspElementAddr);

    // Initialise the linked list (next...)
    for (i = 0; i < fifoNbElem - 1; i++)
    {
        dspElementAddr += fifoElemSizeInWord;

        /* Write next field  */
        *elemAddr32 = dspElementAddr;
        /* Write THIS field & priority field */
        *(volatile t_uint64*)&elemAddr32[DSP_REMOTE_EVENT_THIS_FIELD_OFFSET] =
                ((t_uint64)pComp->thisAddress | (((t_uint64)pComp->priority) << 32));

        elemAddr32 += fifoElemSizeInWord;
    }

    /* Last element: Write next field */
    *elemAddr32 = 0x0 /* NULL */;
    /* Last element: Write THIS field & priority field */
    *(volatile t_uint64*)&elemAddr32[DSP_REMOTE_EVENT_THIS_FIELD_OFFSET] =
            ((t_uint64)pComp->thisAddress | (((t_uint64)pComp->priority) << 32));

    return CM_OK;
}



void dspevent_destroyDspEventFifo(t_memory_handle handle)
{
    (void)cm_DM_Free(handle, TRUE);
}
