/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE HDMI display driver
 *
 * Author: Per Persson <per-xb-persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __DISPLAY_AV8100__H__
#define __DISPLAY_AV8100__H__

#include <linux/regulator/consumer.h>

#include "mcde_display.h"

#define GPIO_AV8100_RSTN	196
#define NATIVE_XRES_HDMI	1280
#define NATIVE_YRES_HDMI	720
#define NATIVE_XRES_SDTV	720
#define NATIVE_YRES_SDTV	576
#define DISPONOFF_SIZE		6
#define TIMING_SIZE		2
#define STAYALIVE_SIZE		1

struct mcde_display_hdmi_platform_data {
	/* Platform info */
	int reset_gpio;
	bool reset_high;
	const char *regulator_id;
	const char *cvbs_regulator_id;
	int reset_delay; /* ms */
	u32 ddb_id;
	struct mcde_col_transform *rgb_2_yCbCr_transform;

	/* Driver data */ /* TODO: move to driver data instead */
	bool hdmi_platform_enable;
	struct regulator *regulator;
};

struct display_driver_data {
	struct regulator *cvbs_regulator;
	bool cvbs_regulator_enabled;
	bool update_port_pixel_format;
	const char *fbdevname;
	struct mcde_video_mode *video_mode;
	u8 cea_nr;
};

void hdmi_fb_onoff(struct mcde_display_device *ddev, bool enable,
				u8 cea, u8 vesa_cea_nr);

#endif /* __DISPLAY_AV8100__H__ */
