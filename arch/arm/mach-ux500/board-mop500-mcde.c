/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/mfd/abx500/ab8500-denc.h>
#include <linux/workqueue.h>
#include <linux/dispdev.h>
#include <linux/compdev.h>
#include <asm/mach-types.h>
#include <linux/clk.h>
#include <mach/devices.h>
#include <video/av8100.h>
#include <video/mcde_display.h>
#include <video/mcde_display-vuib500-dpi.h>
#include <video/mcde_display-av8100.h>
#include <video/mcde_fb.h>
#include <video/mcde_dss.h>
#include <plat/pincfg.h>
#include "pins-db8500.h"
#include "pins.h"
#include "board-mop500.h"

#define DSI_UNIT_INTERVAL_0	0x9
#define DSI_UNIT_INTERVAL_1	0x9
#define DSI_UNIT_INTERVAL_2	0x5

#define DSI_PLL_FREQ_HZ_VID	330000000
#define DSI_PLL_FREQ_HZ_CMD	840320000
/* Based on PLL DDR Freq at 798,72 MHz */
#define HDMI_FREQ_HZ		33280000
#define TV_FREQ_HZ		38400000

#ifdef CONFIG_U8500_TV_OUTPUT_AV8100
/* The initialization of hdmi disp driver must be delayed in order to
 * ensure that inputclk will be available (needed by hdmi hw) */
static struct delayed_work work_dispreg_hdmi;
#define DISPREG_HDMI_DELAY 6000
#endif

enum {
	PRIMARY_DISPLAY_ID,
	SECONDARY_DISPLAY_ID,
	FICTIVE_DISPLAY_ID,
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

#if defined(CONFIG_U8500_TV_OUTPUT_AV8100)
static struct mcde_col_transform rgb_2_yCbCr_transform = {
	.matrix = {
		{0x0042, 0x0081, 0x0019},
		{0xffda, 0xffb6, 0x0070},
		{0x0070, 0xffa2, 0xffee},
	},
	.offset = {0x10, 0x80, 0x80},
};
#endif

static struct mcde_port samsung_s6d16d0_port0 = {
	.sync_src = MCDE_SYNCSRC_BTA,
	.frame_trig = MCDE_TRIG_SW,
};

static struct mcde_display_dsi_platform_data samsung_s6d16d0_pdata0 = {
	.link = 0,
	.num_data_lanes = 2,
};

static struct mcde_display_device samsung_s6d16d0_display0 = {
	.name = "samsung_s6d16d0",
	.id = PRIMARY_DISPLAY_ID,
	.port = &samsung_s6d16d0_port0,
	.chnl_id = MCDE_CHNL_A,
	.fifo = MCDE_FIFO_A,
	.default_pixel_format = MCDE_OVLYPIXFMT_RGBA8888,
	.dev = {
		.platform_data = &samsung_s6d16d0_pdata0,
	},
};

static struct mcde_port sony_port0 = {
	.mode = MCDE_PORTMODE_CMD, /* Change to _VID for video mode */
	.sync_src = MCDE_SYNCSRC_BTA,
};

static struct mcde_display_dsi_platform_data sony_acx424akp_display0_pdata;

static struct mcde_display_device sony_acx424akp_display0 = {
	.name = "mcde_disp_sony_acx424akp",
	.id = PRIMARY_DISPLAY_ID,
	.port = &sony_port0,
	.dev = {
		.platform_data = &sony_acx424akp_display0_pdata,
	},
};

static struct mcde_port samsung_s6d16d0_port1 = {
	.sync_src = MCDE_SYNCSRC_BTA,
	.frame_trig = MCDE_TRIG_SW,
};

static struct mcde_display_dsi_platform_data samsung_s6d16d0_pdata1 = {
	.link = 1,
	.num_data_lanes = 2,
};

static struct mcde_display_device samsung_s6d16d0_display1 = {
	.name = "samsung_s6d16d0",
	.id = SECONDARY_DISPLAY_ID,
	.port = &samsung_s6d16d0_port1,
	.chnl_id = MCDE_CHNL_C1,
	.fifo = MCDE_FIFO_C1,
	.orientation = MCDE_DISPLAY_ROT_90_CCW,
	.default_pixel_format = MCDE_OVLYPIXFMT_RGB565,
	.dev = {
		.platform_data = &samsung_s6d16d0_pdata1,
	},
};

#ifdef CONFIG_U8500_TV_OUTPUT_AV8100

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
	.ifc = 1,
	.link = 2,
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
	.cvbs_regulator_id = "vcc-N2158",
	.rgb_2_yCbCr_transform = &rgb_2_yCbCr_transform,
};

static struct mcde_display_device av8100_hdmi = {
	.name = "av8100_hdmi",
	.id = AV8100_DISPLAY_ID,
	.port = &av8100_port2,
	.chnl_id = MCDE_CHNL_B,
	.fifo = MCDE_FIFO_B,
	.default_pixel_format = MCDE_OVLYPIXFMT_RGBA8888,
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
#endif /* CONFIG_U8500_TV_OUTPUT_AV8100 */

#ifdef CONFIG_FB_MCDE

/*
* This function will create the framebuffer for the display that is registered.
*/
static int display_postregistered_callback(struct notifier_block *nb,
	unsigned long event, void *dev)
{
	struct mcde_display_device *ddev = dev;
	u16 width, height;
	u16 virtual_height;
	struct fb_info *fbi;
#if defined(CONFIG_DISPDEV) || defined(CONFIG_COMPDEV)
	struct mcde_fb *mfb;
#endif

	if (event != MCDE_DSS_EVENT_DISPLAY_REGISTERED)
		return 0;

	if (ddev->id < 0 || ddev->id >= MCDE_NR_OF_DISPLAYS)
		return 0;

	mcde_dss_get_native_resolution(ddev, &width, &height);
	if (ddev->id == PRIMARY_DISPLAY_ID)
		virtual_height = height * 3;
	else
		virtual_height = height * 2;

#ifndef CONFIG_MCDE_DISPLAY_HDMI_FB_AUTO_CREATE
	if (ddev->id == AV8100_DISPLAY_ID)
		goto out;
#endif

	/* Create frame buffer */
	fbi = mcde_fb_create(ddev, width, height, width, virtual_height,
				ddev->default_pixel_format, FB_ROTATE_UR);
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

#ifdef CONFIG_COMPDEV
	/* Only create compdev for the main display */
	if (ddev->id == PRIMARY_DISPLAY_ID) {
		bool mcde_rotation = false;

		/* Use mcde rotation for U8500 only */
		if (cpu_is_u8500())
			mcde_rotation = true;

		mfb = to_mcde_fb(fbi);
		/* Create a compdev overlay for this display */
		if (compdev_create(ddev, mfb->ovlys[0], mcde_rotation,
					NULL) < 0) {
			dev_warn(&ddev->dev,
				"Failed to create compdev for display %s\n",
						ddev->name);
			goto display_postregistered_callback_err;
		} else {
			dev_info(&ddev->dev, "compdev created for (%s)\n",
						ddev->name);
		}
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
#endif /* CONFIG_FB_MCDE */

static int __init handle_display_devices(void)
{
	bool video_mode = false;
	struct mcde_platform_data *pdata = ux500_mcde_device.dev.platform_data;

	pr_debug("%s\n", __func__);

#ifdef CONFIG_FB_MCDE
	(void)mcde_dss_register_notifier(&display_nb);
#endif

	if (uib_is_u8500uibr3() && sony_port0.mode == MCDE_PORTMODE_VID)
		video_mode = true;

	/* Set powermode to STANDBY if startup graphics is executed */
	if (display_initialized_during_boot) {
		samsung_s6d16d0_display0.power_mode = MCDE_DISPLAY_PM_STANDBY;
		sony_acx424akp_display0.power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* Display reset GPIO is different depending on reference boards */
	if (machine_is_hrefv60() || machine_is_u8520()) {
		samsung_s6d16d0_pdata0.reset_gpio = HREFV60_DISP1_RST_GPIO;
		samsung_s6d16d0_pdata1.reset_gpio = HREFV60_DISP2_RST_GPIO;
	} else {
		samsung_s6d16d0_pdata0.reset_gpio = MOP500_DISP1_RST_GPIO;
		samsung_s6d16d0_pdata1.reset_gpio = MOP500_DISP2_RST_GPIO;
	}

	/* Initialize all needed clocks*/
	if (!display_initialized_during_boot) {
		struct clk *clk_dsi_pll;
		struct clk *clk_hdmi;
		struct clk *clk_tv;
		u32 freq;

		/*
		 * The TV CLK is used as parent for the
		 * DSI LP clock.
		 */
		clk_tv = clk_get(&ux500_mcde_device.dev, "tv");
		if (TV_FREQ_HZ != clk_round_rate(clk_tv, TV_FREQ_HZ))
			pr_warning("%s: TV_CLK freq differs %ld\n", __func__,
					clk_round_rate(clk_tv, TV_FREQ_HZ));
		clk_set_rate(clk_tv, TV_FREQ_HZ);
		clk_put(clk_tv);

		/*
		 * The HDMI CLK is used as parent for the
		 * DSI HS clock.
		 */
		clk_hdmi = clk_get(&ux500_mcde_device.dev, "hdmi");
		if (HDMI_FREQ_HZ != clk_round_rate(clk_hdmi, HDMI_FREQ_HZ))
			pr_warning("%s: HDMI freq differs %ld\n", __func__,
					clk_round_rate(clk_hdmi, HDMI_FREQ_HZ));
		clk_set_rate(clk_hdmi, HDMI_FREQ_HZ);
		clk_put(clk_hdmi);

		/*
		 * The DSI PLL CLK is used as DSI PLL for direct freq for
		 * link 2. Link 0/1 is then divided with 1/2/4 from this freq.
		 */
		freq = video_mode ? DSI_PLL_FREQ_HZ_VID : DSI_PLL_FREQ_HZ_CMD;
		clk_dsi_pll = clk_get(&ux500_mcde_device.dev, "dsipll");
		if (freq != clk_round_rate(clk_dsi_pll, freq))
			pr_warning("%s: DSI_PLL freq differs %ld\n", __func__,
					clk_round_rate(clk_dsi_pll, freq));
		clk_set_rate(clk_dsi_pll, freq);
		clk_put(clk_dsi_pll);
	}

	/* MCDE pixelfetchwtrmrk levels per overlay */
	pdata->pixelfetchwtrmrk[0] = video_mode ? 128 : 48;	/* LCD 32bpp */
	pdata->pixelfetchwtrmrk[1] = video_mode ? 128 : 64;	/* LCD 16bpp */
	pdata->pixelfetchwtrmrk[2] = 128;			/* HDMI 32bpp */
	pdata->pixelfetchwtrmrk[3] = 192;			/* HDMI 16bpp */

	/* Not all STUIBs supports VSYNC, disable vsync for STUIB */
	if (uib_is_stuib()) {
		/* Samsung display on STUIB */
		samsung_s6d16d0_display0.port->sync_src = MCDE_SYNCSRC_OFF;
		samsung_s6d16d0_display0.orientation = MCDE_DISPLAY_ROT_90_CCW;
		(void)mcde_display_device_register(&samsung_s6d16d0_display0);
	} else if (uib_is_u8500uib()) {
		/* Samsung display on U8500UIB */
		samsung_s6d16d0_display0.orientation = MCDE_DISPLAY_ROT_90_CW;
		(void)mcde_display_device_register(&samsung_s6d16d0_display0);
	} else if (uib_is_u8500uibr3()) {
		/* Sony display on U8500UIBV3 */
		sony_acx424akp_display0_pdata.reset_gpio = HREFV60_DISP1_RST_GPIO;
		(void)mcde_display_device_register(&sony_acx424akp_display0);
	} else {
		WARN_ON("Unknown UI board");
		samsung_s6d16d0_display0.port->sync_src = MCDE_SYNCSRC_OFF;
		samsung_s6d16d0_display0.orientation = MCDE_DISPLAY_ROT_90_CCW;
		(void)mcde_display_device_register(&samsung_s6d16d0_display0);
	}

	/* Display reset GPIO is different depending on reference boards */
	if (uib_is_stuib())
		(void)mcde_display_device_register(&samsung_s6d16d0_display1);

#if defined(CONFIG_U8500_TV_OUTPUT_AV8100)
	if (!video_mode) {
		INIT_DELAYED_WORK_DEFERRABLE(&work_dispreg_hdmi,
						delayed_work_dispreg_hdmi);
		schedule_delayed_work(&work_dispreg_hdmi,
					msecs_to_jiffies(DISPREG_HDMI_DELAY));
	}
#endif

	return 0;
}

static int __init init_display_devices(void)
{
	if (cpu_is_u8500())
		return handle_display_devices();
	else
		return 0;
}
module_init(init_display_devices);
