/*
 * Header file for Kinetic KTD259 LED driver platform data.
 *
 * Copyright (c) 2011 Samsung Electronics (UK) Ltd.
 *
 * Author: Gareth Phillips  <gareth.phillips@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _KTD259X_BL_H_
#define _KTD259X_BL_H_

#define BL_DRIVER_NAME_KTD259	"pri_bl_ktd259"

struct ktd259x_bl_platform_data {
	const char *bl_name;

	int ctrl_gpio;
	int ctrl_high;
	int ctrl_low;

	/* The following table is used to convert between
	the requested brightness and the current ratio. */
	int max_brightness;
	const unsigned short *brightness_to_current_ratio;
};

#endif


/* to be removed when driver works */
/*#define dev_dbg	dev_info */


#define KTD259_BACKLIGHT_OFF		0
#define KTD259_MIN_CURRENT_RATIO	1	/* 1/32 full current */
#define KTD259_MAX_CURRENT_RATIO	32	/* 32/32 full current */


/* WARNING:
	If incrementing T_LOW_NS or T_HIGH_NS see WARNING
	in function ktd259_set_brightness().
*/
#define T_LOW_NS       (200 + 10) /* Additional 10 as safety factor */
#define T_HIGH_NS      (200 + 10) /* Additional 10 as safety factor */

#define T_STARTUP_MS   1
#define T_OFF_MS       3
