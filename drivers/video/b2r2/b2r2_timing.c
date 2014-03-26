/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 timing
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/time.h>


u32 b2r2_get_curr_nsec(void)
{
	struct timespec ts;

	getrawmonotonic(&ts);

	return (u32)timespec_to_ns(&ts);
}
