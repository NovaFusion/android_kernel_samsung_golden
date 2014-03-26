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

#ifndef _LINUX_DRIVERS_VIDEO_B2R2_TIMING_H_
#define _LINUX_DRIVERS_VIDEO_B2R2_TIMING_H_

/**
 * b2r2_get_curr_nsec() - Return the current nanosecond. Notice that the value
 *                        wraps when the u32 limit is reached.
 *
 */
u32 b2r2_get_curr_nsec(void);

#endif /* _LINUX_DRIVERS_VIDEO_B2R2_TIMING_H_ */
