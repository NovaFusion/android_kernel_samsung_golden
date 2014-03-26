/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 
#ifndef __INC_NMF_FIFO_DESC
#define __INC_NMF_FIFO_DESC

#include <inc/typedef.h>
#include <share/semaphores/inc/semaphores.h>

/*
 * SHOULD be mapped onto a AHB burst (16 bytes=8x16-bit)
 */
typedef struct {
    t_semaphore_id semId;

    t_uint16 elemSize;
    t_uint16 fifoFullValue;
    t_uint16 readIndex;
    t_uint16 writeIndex;
    t_uint16 wrappingValue;

    t_uint32 extendedField; /* in DSP 24 memory when to MPC in Logical Host when to ARM */
} t_nmf_fifo_desc;

#define EXTENDED_FIELD_BCTHIS_OR_TOP        0       //<! This field will be used:
                                                    //<! - as hostBCThis for DSP->HOST binding
                                                    //<! - as TOP else
#define EXTENDED_FIELD_BCDESC               1       //<! This field will be used for:
                                                    //<! - interface method address for ->MPC binding
                                                    //<! - for params size for ->Host binding (today only [0] is used as max size)

#endif /* __INC_NMF_FIFO */
