/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 
#ifndef __INC_SHARED_SEMAPHORE_H
#define __INC_SHARED_SEMAPHORE_H

#include <share/inc/nmf.h>

typedef t_uint16 t_semaphore_id;

/*
 * HW semaphore allocation
 * -----------------------
 *  We want to optimize interrupt demultiplexing at dsp interrupt handler level
 *  so a good solution would be to have sequentially the semaphores for each neighbors
 *
 * STn8500 :
 * ---------
 * ARM <- SVA  COMS => 0
 * ARM <- SIA  COMS => 1
 * SVA <- ARM  COMS => 2
 * SVA <- SIA  COMS => 3
 * SIA <- ARM  COMS => 4
 * SIA <- SVA  COMS => 5

 * The first neighbor is always the ARM, then the other ones (SVA,SIA)
 */

/*
 * Local semaphore allocation
 * -----------------------
 * 0 : ARM <- DSP
 * 1 : DSP <- ARM
 */

#define NB_USED_HSEM_PER_CORE (NB_CORE_IDS - 1)
#define FIRST_NEIGHBOR_SEMID(coreId) ((coreId)*NB_USED_HSEM_PER_CORE)

#endif /* __INC_SHARED_SEMAPHORE_H */
