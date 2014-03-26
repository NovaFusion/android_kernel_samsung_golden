/*
 * Copyright (C) ST-Ericsson AB 2010
 * Copyright (C) Samsung Electronics 2010
 *
 * Author: Anirban Sarkar <anirban.sarkar@samsung.com>
 * for Samsung Electronics.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/dispdev.h>
#include <linux/compdev.h>
#include <linux/clk.h>
#include <mach/devices.h>
#include <linux/delay.h>
#include <mach/board-sec-u8500.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <video/mcde_display.h>
#include <video/mcde_display_ssg_dpi.h>
#include <video/mcde_display-dpi.h>
#include <video/mcde_fb.h>
#include <video/mcde_dss.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include "pins-db8500.h"
#include "pins.h"
#include <mach/db8500-regs.h>
	 

#define PRCMU_DPI_CLK_FREQ	50000000
	 
#ifdef CONFIG_FB_MCDE

enum {
	PRIMARY_DISPLAY_ID,
	SECONDARY_DISPLAY_ID,
	AV8100_DISPLAY_ID,
	AB8500_DISPLAY_ID,
	MCDE_NR_OF_DISPLAYS
};

static int display_initialized_during_boot = (int)false;

#ifndef CONFIG_HAS_EARLYSUSPEND
static struct ux500_pins *dpi_pins;
#endif

static struct fb_info *primary_fbi;

static int __init startup_graphics_setup(char *str)
{
	if (get_option(&str, &display_initialized_during_boot) != 1)
		display_initialized_during_boot = false;

	if (display_initialized_during_boot) {
		pr_info("Startup graphics support\n");
	} else {
		pr_info("No startup graphics supported\n");
	};

	return 1;

}	 
__setup("startup_graphics=", startup_graphics_setup);
	 
static struct mcde_port port0 = {
	.type = MCDE_PORTTYPE_DPI,
	.pixel_format = MCDE_PORTPIXFMT_DPI_24BPP,
	.ifc = 0,
	.link = 0, 	 
	.sync_src = MCDE_SYNCSRC_OFF,	 /* sync from output formatter	*/
	.update_auto_trig = true,
	.phy = {
		.dpi = {
			.tv_mode = false,
			.clock_div = 2,
			//vsync/hsync/Enable:Low Active
			//PCLOCK : Rising edge
			.polarity = DPI_ACT_LOW_VSYNC | DPI_ACT_LOW_HSYNC | DPI_ACT_LOW_DATA_ENABLE
					| DPI_ACT_ON_FALLING_EDGE,
			.lcd_freq = PRCMU_DPI_CLK_FREQ
		},
	},
};

#ifndef CONFIG_HAS_EARLYSUSPEND
static int dpi_display_platform_enable(struct mcde_display_device *ddev)
{
	int res = 0;
	dev_info(&ddev->dev, "%s\n", __func__);
	res = ux500_pins_enable(dpi_pins);
	if (res)
		dev_warn(&ddev->dev, "Failure during %s\n", __func__);
	return res;
}
	 
static int dpi_display_platform_disable(struct mcde_display_device *ddev)
{
	int res = 0;
	dev_info(&ddev->dev, "%s\n", __func__);
	res = ux500_pins_disable(dpi_pins);	/* pins disabled to save power */
	if (res)
		dev_warn(&ddev->dev, "Failure during %s\n", __func__);
	return res;
}
#endif

static int pri_display_power_on(struct ssg_dpi_display_platform_data *pd, int enable);
static int pri_display_reset(struct ssg_dpi_display_platform_data *pd);
static int lcd_gpio_cfg_earlysuspend(void);
static int lcd_gpio_cfg_lateresume(void);


/* Taken from the programmed value of the LCD clock in PRCMU */
#define FRAME_PERIOD_MS 17	// rounded up to the nearest ms

struct ssg_dpi_display_platform_data janice_dpi_pri_display_info = {
	.platform_enabled	= false,
	.reset_high		= false,
	.reset_gpio 		= LCD_RESX_JANICE_R0_0,
	.pwr_gpio		= LCD_PWR_EN_JANICE_R0_0,
	.bl_en_gpio		= -1,

	.power_on_delay		= 25,
	.reset_delay 		= 10,
	.sleep_out_delay	= 120,

	.display_off_delay	= 25,
	.sleep_in_delay		= 120,

	.video_mode.xres	= 480, 
	.video_mode.yres	= 800,
	.video_mode.hsw		= 2,
	.video_mode.hbp		= 16 - 2,	/* -2 to allow for starting at end of hsw not start of hsw */
	.video_mode.hfp		= 16,
	.video_mode.vsw		= 2,
	.video_mode.vbp		= 3 - 2,	/* -2 to allow for starting at end of vsw not start of vsw */
	.video_mode.vfp		= 28,
	.video_mode.interlaced 	= false,

	/*
	 * The pixclock setting is not used within MCDE. The clock is
	 * setup elsewhere. But the pixclock value is visible in user
	 * space.
	 */
	.video_mode.pixclock = (int)(1e+12 * (1.0 / PRCMU_DPI_CLK_FREQ)),

	.reset 		= pri_display_reset,
	.power_on 	= pri_display_power_on,

	.gpio_cfg_earlysuspend = lcd_gpio_cfg_earlysuspend,
	.gpio_cfg_lateresume = lcd_gpio_cfg_lateresume,
};

static int pri_display_power_on(struct ssg_dpi_display_platform_data *pd, int enable)
{
	int res = 0;
	gpio_set_value(pd->pwr_gpio, enable);

	return res;
}

static int pri_display_reset(struct ssg_dpi_display_platform_data *pd)
{
	/* Active Reset */

	/* Reset LCD */
	gpio_set_value(pd->reset_gpio, 0);

	/* Hold for minimum of 1ms */
	msleep(1);

	/* Release LCD from reset */
	gpio_set_value(pd->reset_gpio, 1);

	return 0;
}

static pin_cfg_t janice_lcd_spi_pins_disable[] = {
	 /* LCD_RESX */
	GPIO139_GPIO | PIN_OUTPUT_LOW, 
	/* DPI LCD SPI I/F */
	GPIO220_GPIO | PIN_OUTPUT_LOW, /* LCD_SCL */
	GPIO223_GPIO | PIN_OUTPUT_LOW, /* LCD_CSX */
	GPIO224_GPIO | PIN_OUTPUT_LOW, /* LCD_SDA */
};

static pin_cfg_t janice_lcd_spi_pins_enable[] = {
	/* LCD_RESX */
	GPIO139_GPIO | PIN_OUTPUT_LOW,	
	/* DPI LCD SPI I/F */
	GPIO220_GPIO | PIN_OUTPUT_HIGH, /* LCD_SCL */
	GPIO223_GPIO | PIN_OUTPUT_HIGH, /* LCD_CSX */
	GPIO224_GPIO | PIN_OUTPUT_HIGH, /* LCD_SDA */
};

static int lcd_gpio_cfg_earlysuspend(void)
{
	int ret = 0;

	ret=nmk_config_pins(janice_lcd_spi_pins_disable,
		ARRAY_SIZE(janice_lcd_spi_pins_disable));

	return ret;
}

static int lcd_gpio_cfg_lateresume(void)
{
	int ret = 0;

	ret=nmk_config_pins(janice_lcd_spi_pins_enable,
		ARRAY_SIZE(janice_lcd_spi_pins_enable));

	return ret;
}

static struct mcde_display_device generic_display0 = {
	.name = LCD_DRIVER_NAME_S6E63M0,
	.id = PRIMARY_DISPLAY_ID,
	.port = &port0,
	.chnl_id = MCDE_CHNL_A,
	.fifo = MCDE_FIFO_A,
	.default_pixel_format = MCDE_OVLYPIXFMT_RGBA8888,//support RGBA888 for janice
	.x_res_padding = 0,
	.y_res_padding = 0,
	.native_x_res = 480,
	.native_y_res = 800,
	/* .synchronized_update: Don't care: port is set to update_auto_trig */
	.dev = {
		.platform_data = &janice_dpi_pri_display_info,
	},
	#ifndef CONFIG_HAS_EARLYSUSPEND
	.platform_enable = dpi_display_platform_enable,
	.platform_disable = dpi_display_platform_disable,
	#endif
};	 
	 	 

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

	if (ddev->id < PRIMARY_DISPLAY_ID || ddev->id >= MCDE_NR_OF_DISPLAYS)
		return 0;
	 
	mcde_dss_get_native_resolution(ddev, &width, &height);
	virtual_height = height * 3;

	/* Create frame buffer */
	fbi = mcde_fb_create(ddev, width, height, width, virtual_height,
				ddev->default_pixel_format, FB_ROTATE_UR);

	if (IS_ERR(fbi)) {
		dev_warn(&ddev->dev,
			"Failed to create fb for display %s\n", ddev->name);
		goto display_postregistered_callback_err;
	} else {
		dev_info(&ddev->dev, "Framebuffer created (%s)\n", ddev->name);

		if (ddev->id == PRIMARY_DISPLAY_ID)
			primary_fbi = fbi;
	}

#ifdef CONFIG_DISPDEV
	mfb = to_mcde_fb(fbi);

	/* Create a dispdev overlay for this display */
	if (dispdev_create(ddev, true, mfb->ovlys[0]) < 0) {
		dev_warn(&ddev->dev,
			"Failed to create disp for display %s\n",
					ddev->name);
		goto display_postregistered_callback_err;
	} else {
		dev_info(&ddev->dev, "Disp dev created for (%s)\n",
					ddev->name);
	}
#endif

#ifdef CONFIG_COMPDEV
	mfb = to_mcde_fb(fbi);
	/* Create a compdev overlay for this display */
	if (compdev_create(ddev, mfb->ovlys[0], true,	NULL) < 0) {
		dev_warn(&ddev->dev,
			"Failed to create compdev for display %s\n",
					ddev->name);
		goto display_postregistered_callback_err;
	} else {
		dev_info(&ddev->dev, "compdev created for (%s)\n",
					ddev->name);
	}
#endif

	return 0;

display_postregistered_callback_err:
	return -1;
}
	 
static struct notifier_block display_nb = {
	.notifier_call = display_postregistered_callback,
};

static void update_mcde_opp(struct device *dev,
					struct mcde_opp_requirements *reqs)
{
	s32 req_ape = PRCMU_QOS_DEFAULT_VALUE;

	if (reqs->num_rot_channels && reqs->num_overlays > 1)
		req_ape = PRCMU_QOS_MAX_VALUE;

	prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, dev_name(dev), req_ape);
}

int __init init_janice_display_devices(void)
{
	struct mcde_platform_data *pdata = ux500_mcde_device.dev.platform_data;

	int ret;

	ret = mcde_dss_register_notifier(&display_nb);
	if (ret)
		pr_warning("Failed to register dss notifier\n");
	 
	if (display_initialized_during_boot) {
		generic_display0.power_mode = MCDE_DISPLAY_PM_ON;
		janice_dpi_pri_display_info.platform_enabled = 1;
	}

	/*
	 * The pixclock setting is not used within MCDE. The clock is
	 * setup elsewhere. But the pixclock value is visible in user
	 * space.
	 */
	janice_dpi_pri_display_info.video_mode.pixclock /=
						port0.phy.dpi.clock_div;

	/* MCDE pixelfetchwtrmrk levels per overlay */
	{
#if 1 /* 16 bit overlay */
#define BITS_PER_WORD (4 * 64)
	u32 fifo = (1024*8 - 8 * BITS_PER_WORD) / 3;
	fifo &= ~(BITS_PER_WORD - 1);
	pdata->pixelfetchwtrmrk[0] = fifo * 2 / 32;	/* LCD 32bpp */
	pdata->pixelfetchwtrmrk[1] = fifo * 1 / 16;	/* LCD 16bpp */
#else /* 24 bit overlay */
	u32 fifo = (1024*8 - 8 * BITS_PER_WORD) / 7;
	fifo &= ~(BITS_PER_WORD - 1);
	pdata->pixelfetchwtrmrk[0] = fifo * 4 / 32;	/* LCD 32bpp */
	pdata->pixelfetchwtrmrk[1] = fifo * 3 / 24;	/* LCD 24bpp */
#endif
	}
	pdata->update_opp = update_mcde_opp;

	ret = mcde_display_device_register(&generic_display0);
	if (ret)
		pr_warning("Failed to register generic display device 0\n");

	#ifndef CONFIG_HAS_EARLYSUSPEND
	dpi_pins = ux500_pins_get("mcde-dpi");
	if (!dpi_pins)
		return -EINVAL;
	#endif

	return ret;
}


struct fb_info* get_primary_display_fb_info(void)
{
	return primary_fbi;
}
	
module_init(init_janice_display_devices);

#endif	/* CONFIG_FB_MCDE */

