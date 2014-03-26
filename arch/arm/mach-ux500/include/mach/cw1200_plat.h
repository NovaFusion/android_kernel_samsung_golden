/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CW1200_PLAT_H_INCLUDED
#define CW1200_PLAT_H_INCLUDED

#include <linux/ioport.h>

struct cw1200_platform_data {
	const char *mmc_id;
	const struct resource *irq;
	const struct resource *reset;
	int (*power_ctrl)(const struct cw1200_platform_data *pdata,
			  bool enable);
	int (*clk_ctrl)(const struct cw1200_platform_data *pdata,
			  bool enable);
	int (*prcmu_ctrl)(const struct cw1200_platform_data *pdata,
			  bool enable);
};

/* Declaration only. Should be implemented in arch/xxx/mach-yyy */
const struct cw1200_platform_data *cw1200_get_platform_data(void);

#endif /* CW1200_PLAT_H_INCLUDED */
