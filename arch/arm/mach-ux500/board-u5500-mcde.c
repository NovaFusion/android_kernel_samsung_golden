/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/dispdev.h>
#include <video/av8100.h>
#include <asm/mach-types.h>
#include <video/mcde_display.h>
#include <video/mcde_display-generic_dsi.h>
#include <video/mcde_display-sony_acx424akp_dsi.h>
#include <video/mcde_display-av8100.h>
#include <video/mcde_fb.h>
#include <video/mcde_dss.h>

#define DSI_UNIT_INTERVAL_0	0xA
#define DSI_UNIT_INTERVAL_2	0x5

/* The initialization of hdmi disp driver must be delayed in order to
 * ensure that inputclk will be available (needed by hdmi hw) */
static struct delayed_work work_dispreg_hdmi;
#define DISPREG_HDMI_DELAY 6000

enum {
	PRIMARY_DISPLAY_ID,
	AV8100_DISPLAY_ID,
	MCDE_NR_OF_DISPLAYS
};

static int display_initialized_during_boot;

static int __init startup_graphics_setup(char *str)
{

	if (get_option(&str, &display_initialized_during_boot) != 1)
		display_initialized_during_boot = 0;

	switch (display_initialized_during_boot) {
	case 1:
		pr_info("Startup graphics support\n");
		break;
	case 0:
	default:
		pr_info("No startup graphics supported\n");
		break;
	};

	return 1;
}
__setup("startup_graphics=", startup_graphics_setup);

static struct mcde_col_transform rgb_2_yCbCr_transform = {
	.matrix = {
		{0x0042, 0x0081, 0x0019},
		{0xffda, 0xffb6, 0x0070},
		{0x0070, 0xffa2, 0xffee},
	},
	.offset = {0x10, 0x80, 0x80},
};

static struct mcde_port sony_port0 = {
	.link = 0,
	.sync_src = MCDE_SYNCSRC_BTA,
	.frame_trig = MCDE_TRIG_SW,
};

static struct mcde_display_sony_acx424akp_platform_data \
			sony_acx424akp_display0_pdata = {
	.reset_gpio = 226,
};

static struct mcde_display_device sony_acx424akp_display0 = {
	.name = "mcde_disp_sony_acx424akp",
	.id = PRIMARY_DISPLAY_ID,
	.port = &sony_port0,
	.chnl_id = MCDE_CHNL_A,
	.fifo = MCDE_FIFO_A,
	.default_pixel_format = MCDE_OVLYPIXFMT_RGBA8888,
	.dev = {
		.platform_data = &sony_acx424akp_display0_pdata,
	},
};

#if defined(CONFIG_AV8100_HWTRIG_INT)
	#define AV8100_SYNC_SRC MCDE_SYNCSRC_TE0
#elif defined(CONFIG_AV8100_HWTRIG_I2SDAT3)
	#define AV8100_SYNC_SRC MCDE_SYNCSRC_TE1
#elif defined(CONFIG_AV8100_HWTRIG_DSI_TE)
	#define AV8100_SYNC_SRC MCDE_SYNCSRC_TE_POLLING
#else
	#define AV8100_SYNC_SRC MCDE_SYNCSRC_OFF
#endif
static struct mcde_port av8100_port2 = {
	.type = MCDE_PORTTYPE_DSI,
	.mode = MCDE_PORTMODE_CMD,
	.pixel_format = MCDE_PORTPIXFMT_DSI_24BPP,
	.link = 1,
	.sync_src = AV8100_SYNC_SRC,
	.update_auto_trig = true,
	.phy = {
		.dsi = {
			.num_data_lanes = 2,
			.ui = DSI_UNIT_INTERVAL_2,
		},
	},
	.hdmi_sdtv_switch = HDMI_SWITCH,
};

static struct mcde_display_hdmi_platform_data av8100_hdmi_pdata = {
	.rgb_2_yCbCr_transform = &rgb_2_yCbCr_transform,
};

static struct mcde_display_device av8100_hdmi = {
	.name = "av8100_hdmi",
	.id = AV8100_DISPLAY_ID,
	.port = &av8100_port2,
	.chnl_id = MCDE_CHNL_B,
	.fifo = MCDE_FIFO_B,
	.default_pixel_format = MCDE_OVLYPIXFMT_RGB888,
	.native_x_res = 1280,
	.native_y_res = 720,
	.dev = {
		.platform_data = &av8100_hdmi_pdata,
	},
};

static void delayed_work_dispreg_hdmi(struct work_struct *ptr)
{
	if (mcde_display_device_register(&av8100_hdmi))
		pr_warning("Failed to register av8100_hdmi\n");
}

/*
* This function will create the framebuffer for the display that is registered.
*/
static int display_postregistered_callback(struct notifier_block *nb,
	unsigned long event, void *dev)
{
	struct mcde_display_device *ddev = dev;
	u16 width, height;
	u16 virtual_height;
	u32 rotate = FB_ROTATE_UR;
	struct fb_info *fbi;
#ifdef CONFIG_DISPDEV
	struct mcde_fb *mfb;
#endif

	if (event != MCDE_DSS_EVENT_DISPLAY_REGISTERED)
		return 0;

	if (ddev->id < PRIMARY_DISPLAY_ID || ddev->id >= MCDE_NR_OF_DISPLAYS)
		return 0;

	mcde_dss_get_native_resolution(ddev, &width, &height);

	virtual_height = height * 2;

#ifndef CONFIG_MCDE_DISPLAY_HDMI_FB_AUTO_CREATE
	if (ddev->id == AV8100_DISPLAY_ID)
		goto out;
#endif

	/* Create frame buffer */
	fbi = mcde_fb_create(ddev, width, height, width, virtual_height,
					ddev->default_pixel_format, rotate);
	if (IS_ERR(fbi)) {
		dev_warn(&ddev->dev,
			"Failed to create fb for display %s\n", ddev->name);
		goto display_postregistered_callback_err;
	} else {
		dev_info(&ddev->dev, "Framebuffer created (%s)\n", ddev->name);
	}

#ifdef CONFIG_DISPDEV
	mfb = to_mcde_fb(fbi);

	/* Create a dispdev overlay for this display */
	if (dispdev_create(ddev, true, mfb->ovlys[0]) < 0) {
		dev_warn(&ddev->dev,
			"Failed to create disp for display %s\n", ddev->name);
		goto display_postregistered_callback_err;
	} else {
		dev_info(&ddev->dev, "Disp dev created for (%s)\n", ddev->name);
	}
#endif

out:
	return 0;

display_postregistered_callback_err:
	return -1;
}

static struct notifier_block display_nb = {
	.notifier_call = display_postregistered_callback,
};

static int __init init_display_devices(void)
{
	if (!cpu_is_u5500())
		return 0;

	(void)mcde_dss_register_notifier(&display_nb);

	if (display_initialized_during_boot)
		sony_acx424akp_display0.power_mode = MCDE_DISPLAY_PM_STANDBY;

	(void)mcde_display_device_register(&sony_acx424akp_display0);

	INIT_DELAYED_WORK_DEFERRABLE(&work_dispreg_hdmi,
			delayed_work_dispreg_hdmi);
	schedule_delayed_work(&work_dispreg_hdmi,
			msecs_to_jiffies(DISPREG_HDMI_DELAY));

	return 0;
}
module_init(init_display_devices);
