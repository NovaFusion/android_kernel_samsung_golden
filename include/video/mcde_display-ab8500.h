/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * AB8500 tvout driver interface
 *
 * Author: Marcel Tunnissen <marcel.tuennissen@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __DISPLAY_AB8500__H__
#define __DISPLAY_AB8500__H__

#include <video/mcde.h>

struct ab8500_display_platform_data {
	/* Platform info */
	struct mcde_col_transform *rgb_2_yCbCr_transform;
	int nr_regulators;
	const char *regulator_id[];
};

#endif /* __DISPLAY_AB8500__H__*/

