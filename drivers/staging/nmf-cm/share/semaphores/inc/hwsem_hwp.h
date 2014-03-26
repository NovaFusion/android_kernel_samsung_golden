/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 
#ifndef __INC_HWSEM_HWP_H
#define __INC_HWSEM_HWP_H

#include <share/semaphores/inc/semaphores.h>

#define CORE_ID_2_HW_CORE_ID(coreId) (1U << (coreId))

/*
 * Definition of the number of hw semaphores into the Nomadik IP
 */
#define NUM_HW_SEMAPHORES       32


/*
 * Definition of how HSEM IP interrupts are interconnected with cores
 */
typedef enum {
    HSEM_FIRST_INTR = 0,
    HSEM_INTRA = HSEM_FIRST_INTR,
    HSEM_INTRB = 1,
    HSEM_INTRC = 2,
    HSEM_INTRD = 3,
    HSEM_INTRE = 4,
    HSEM_MAX_INTR
} t_hw_semaphore_irq_id;

/*
 * Description of the registers of the HW Sem IP
 */
#define HSEM_INTRA_MASK  (1<<(4+HSEM_INTRA))
#define HSEM_INTRB_MASK  (1<<(4+HSEM_INTRB))
#define HSEM_INTRC_MASK  (1<<(4+HSEM_INTRC))
#define HSEM_INTRD_MASK  (1<<(4+HSEM_INTRD))
#define HSEM_INTRE_MASK  (1<<(4+HSEM_INTRE))

typedef struct {
    t_shared_reg imsc;
    t_shared_reg ris;
    t_shared_reg mis;
    t_shared_reg icr;
} t_hsem_it_regs;

typedef volatile struct {
#if defined(__STN_8500)
    t_shared_reg cr;
    t_shared_reg dummy;
#endif
    t_shared_reg sem[NUM_HW_SEMAPHORES];
#if defined(__STN_8820)
    t_shared_reg RESERVED1[(0x90 - 0x80)>>2];
#elif defined(__STN_8500)
    t_shared_reg RESERVED1[(0x90 - 0x88)>>2];
#else /* __STN_8820 or __STN_8500 -> _STN_8815 */
    t_shared_reg RESERVED1[(0x90 - 0x40)>>2];
#endif /* __STN_8820 or __STN_8500 -> _STN_8815 */
    t_shared_reg icrall;
    t_shared_reg RESERVED2[(0xa0 - 0x94)>>2];
    t_hsem_it_regs it[HSEM_MAX_INTR];
#if defined(__STN_8820) || defined(__STN_8500)
    t_shared_reg RESERVED3[(0x100 - 0xf0)>>2];
#else /* __STN_8820 or __STN_8500 -> _STN_8815 */
    t_shared_reg RESERVED3[(0x100 - 0xe0)>>2];
#endif /* __STN_8820 or __STN_8500 -> _STN_8815 */
    t_shared_reg itcr;
    t_shared_reg RESERVED4;
    t_shared_reg itop;
    t_shared_reg RESERVED5[(0xfe0 - 0x10c)>>2];
    t_shared_reg pid0;
    t_shared_reg pid1;
    t_shared_reg pid2;
    t_shared_reg pid3;
    t_shared_reg pcid0;
    t_shared_reg pcid1;
    t_shared_reg pcid2;
    t_shared_reg pcid3;
} t_hw_semaphore_regs, *tp_hw_semaphore_regs;

#endif /* __INC_HWSEM_HWP_H */
