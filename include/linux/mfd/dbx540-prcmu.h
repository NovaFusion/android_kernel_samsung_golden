/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Michel Jaouen <michel.jaouen@stericsson.com>
 *
 * PRCMU f/w APIs
 */
#ifndef __MFD_DBX540_PRCMU_H
#define __MFD_DBX540_PRCMU_H

#include <linux/interrupt.h>
#include <linux/bitops.h>



enum ap_pwrst_trans_status_9540 {
	EXECUTETODEEPIDLE	= 0xE8,
	EXECUTETODEEPSLEEP	= 0xF8
};

#define PRCMU_FW_PROJECT_U9540		6


#ifdef CONFIG_MFD_DBX540_PRCMU
struct prcmu_fops_register_data *dbx540_prcmu_early_init(void);
int prcmu_set_vsafe_opp(u8 opp);
int prcmu_get_vsafe_opp(void);

#ifdef CONFIG_C2C
void prcmu_c2c_request_notif_up(void);
void prcmu_c2c_request_reset(void);
#endif

int prcmu_reset_hva(void);
int prcmu_reset_hx170(void);

#else /* !CONFIG_MFD_DBX540_PRCMU */
static inline struct prcmu_fops_register_data *dbx540_prcmu_early_init(void)
{
	return NULL;
}

static inline int prcmu_set_vsafe_opp(u8 opp)
{
	return -EINVAL;
}

static inline int prcmu_get_vsafe_opp(void)
{
	return -EINVAL;
}

#ifdef CONFIG_C2C
static inline void prcmu_c2c_request_notif_up(void) {}
static inline void prcmu_c2c_request_reset(void) {}
#endif

static inline int prcmu_reset_hva(void)
{
	return -EINVAL;
}

static inline int prcmu_reset_hx170(void)
{
	return -EINVAL;
}

#endif /* !CONFIG_MFD_DBX540_PRCMU */

#endif /* __MFD_DBX540_PRCMU_H */
