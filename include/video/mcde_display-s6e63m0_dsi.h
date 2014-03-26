/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Samsung MCDE S6E8AA0 display driver
 *
 * Author: Gareth Phillips <gareth.phillips@samsung.com>
 * for Samsung Electronics.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __MCDE_DISPLAY_S6E8AA0__H__
#define __MCDE_DISPLAY_S6E8AA0__H__

#include <linux/regulator/consumer.h>
#include "mcde_display.h"


#define ACTIVE_HIGH	1
#define ACTIVE_LOW	0

#define PIN_ASSERT(x)	(x)
#define PIN_DEASSERT(x)	(1-(x))

#define S6E8AA0_NAME	"mcde_disp_s6e8aa0"

struct  s6e8aa0_platform_data {
	/* Platform info */
	int power_on_gpio;
	int power_on_active_level;	/* ACTIVE_HIGH or ACTIVE_LOW */
	int post_power_on_delay;

	int reset_gpio;
	int reset_active_level;		/* ACTIVE_HIGH or ACTIVE_LOW */
	int reset_active_delay; 	/* ms */
	int post_reset_delay; 		/* ms */

	int sleep_out_delay; 		/* ms */
	int sleep_in_delay; 		/* ms */

	/* Driver data */
	bool platform_enabled;
	char *regulator_id;
	struct regulator *regulator;
	int max_supply_voltage;
	int min_supply_voltage;
};

#endif /* __MCDE_DISPLAY_S6E8AA0__H__ */

