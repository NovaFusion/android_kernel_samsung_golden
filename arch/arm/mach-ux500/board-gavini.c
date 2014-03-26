/*
 * Copyright (C) 2009 ST-Ericsson SA
 * Copyright (C) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/sec_jack.h>

#include <linux/power_supply.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500.h>
#include <sound/ux500_ab8500_ext.h>
#include <linux/mmio.h>
#include <linux/spi/stm_msp.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif
#include <linux/gpio_keys.h>
#include <linux/cypress_touchkey.h>
#include <linux/projector.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <mach/irqs-board-mop500.h>
#include <mach/ste-dma40-db8500.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>

#include <plat/pincfg.h>
#include <plat/ske.h>
#include <plat/i2c.h>
#include <plat/ste_dma40.h>

#include <mach/hardware.h>
#include <mach/devices.h>
#include <mach/ste_audio_io.h>
#ifdef CONFIG_PROXIMITY_GP2A
#include <mach/gp2a.h>
#endif
#ifdef CONFIG_PROXIMITY_GP2A030
#include <mach/gp2ap020.h>
#endif
#include <linux/yas.h>
#include <linux/mpu.h>
#include <mach/mpu60x0.h>
#include <linux/cpuidle-dbx500.h>
#include <mach/setup.h>
#include <mach/isa1200.h>
#include <mach/crypto-ux500.h>
#include <mach/pm.h>
#include <mach/reboot_reasons.h>

#include <video/mcde_display.h>
#include <video/ktd259x_bl.h>

#ifdef CONFIG_DB8500_MLOADER
#include <mach/mloader-dbx500.h>
#endif

#include <sound/ux500_ab8500_ext.h>

#include "devices-db8500.h"
#include "board-gavini-regulators.h"
#include "pins-db8500.h"
#include "pins.h"
#include "cpu-db8500.h"
#include "board-mop500.h"	/* using some generic functions defined here */
#include "board-sec-bm.h"
#include <mach/board-sec-ux500.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>
#include <linux/usb_switcher.h>

#include <mach/sec_param.h>
#include <mach/sec_common.h>
#include <mach/sec_log_buf.h>

#ifdef CONFIG_USB_ANDROID
#define PUBLIC_ID_BACKUPRAM1 (U8500_BACKUPRAM1_BASE + 0x0FC0)
#define USB_SERIAL_NUMBER_LEN 31
#endif

#ifndef SSG_CAMERA_ENABLE
#define SSG_CAMERA_ENABLE
#endif

#ifdef CONFIG_TOUCHSCREEN_MMS136_TS
#include <linux/input/mms136_ts.h>
#endif

#include <linux/leds.h>

unsigned int board_id;

unsigned int sec_debug_settings;
int jig_smd = 1;
EXPORT_SYMBOL(jig_smd);
int is_cable_attached=0;
EXPORT_SYMBOL(is_cable_attached);

struct device *gps_dev = NULL;
EXPORT_SYMBOL(gps_dev);

#define GPIO_SET 1
#define GPIO_RESET 0


#ifdef CONFIG_ANDROID_RAM_CONSOLE

static struct resource ram_console_resource = {
	.name = "ram_console",
	.flags = IORESOURCE_MEM,
};

static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources = 0,
	.resource = &ram_console_resource,
};

static int __init ram_console_setup(char *p)
{
	resource_size_t ram_console_size = memparse(p, &p);

	if ((ram_console_size > 0) && (*p == '@')){
		ram_console_resource.start = memparse(p + 1, &p);
		ram_console_resource.end = ram_console_resource.start + ram_console_size - 1;
		ram_console_device.num_resources = 1;
	}

	return 1;
}

__setup("mem_ram_console=", ram_console_setup);

#endif // CONFIG_ANDROID_RAM_CONSOLE

#if defined(CONFIG_KEYBOARD_CYPRESS_TOUCH)
static struct cypress_touchkey_platform_data  cypress_touchkey_pdata = {
	.gpio_scl = TOUCHKEY_SCL_GAVINI_R0_0,
	.gpio_sda = TOUCHKEY_SDA_GAVINI_R0_0,
	.gpio_int = TOUCHKEY_INT_GAVINI_R0_0,
	.gpio_ldo_en = TSP_LDO_ON2_GAVINI_R0_0,
	.gpio_rst = TSP_RST_GAVINI_R0_0,
	.gpio_led_en = EN_LED_LDO_GAVINI_R0_0,
};
#endif

static struct projector_dpp2601_platform_data  projector_dpp2601_pdata = {
	.gpio_scl = MPR_SCL_GAVINI_R0_0,
	.gpio_sda = MPR_SDA_GAVINI_R0_0,
	.gpio_int = PROJECTOR_INTZ_GAVINI_R0_0,
	.gpio_mp_on = MP_ON_GAVINI_R0_0,
	.gpio_prj_en = PRJ_EN_GAVINI_R0_0,
	.gpio_parkz = PARKZ_GAVINI_R0_0,
	.gpio_prj_led_en = PRJ_LED_EN_GAVINI_R0_1,
	.gpio_en_lcd = EN_LCD_GAVINI_R0_0
};

#if defined(CONFIG_PROXIMITY_GP2A030)
static void gp2a_power_on(int onoff);
#endif

#if defined(CONFIG_INPUT_YAS_MAGNETOMETER)
struct yas_platform_data yas_data = {
	.hw_rev=0,	/* hw gpio value */
};
#endif

#if defined(CONFIG_MPU_SENSORS_MPU3050)

#define SENSOR_MPU_NAME "mpu3050"

static struct mpu3050_platform_data mpu_data = {
	.int_config  = 0x12,
	.orientation = {
		0,  -1,  0,
		1,  0,  0,
		0,  0,  1
	},
	/* accel */
	.accel = {
		.get_slave_descr = get_accel_slave_descr,
		.adapt_num   = 8,
		.bus         = EXT_SLAVE_BUS_SECONDARY,
		.address     = 0x18,
		.orientation = {
		   -1,  0,  0,
			0, -1,  0,
			0,  0,  1
		},
	},
	/* compass */
	.compass = {
		.get_slave_descr = get_compass_slave_descr,
		.adapt_num   = 4,
		.bus         = EXT_SLAVE_BUS_PRIMARY,
		.address     = 0x30,
		.orientation = {
			0, -1,  0,
			1,  0,  0,
			0,  0, -1
		},
	},
};

static struct mpu3050_platform_data mpu_data_gavini_r00 = {
	.int_config  = 0x12,
	.orientation = {
		0,  -1,  0,
		1,  0,  0,
		0,  0,  1
	},
	/* accel */
	.accel = {
		.get_slave_descr = get_accel_slave_descr,
		.adapt_num   = 0,
		.bus         = EXT_SLAVE_BUS_SECONDARY,
		.address     = 0x18,
		.orientation = {
		   -1,  0,  0,
			0, -1,  0,
			0,  0,  1
		},
	},
	/* compass */
	.compass = {
		.get_slave_descr = get_compass_slave_descr,
		.adapt_num   = 4,
		.bus         = EXT_SLAVE_BUS_PRIMARY,
		.address     = 0x30,
		.orientation = {
			0, -1,  0,
			1,  0,  0,
			0,  0, -1
		},
	},
};

static struct mpu3050_platform_data mpu_data_gavini_r01 = {
	.int_config  = 0x12,
	.orientation = {
		0,  -1,  0,
		1,  0,  0,
		0,  0,  1
	},
	/* accel */
	.accel = {
		.get_slave_descr = get_accel_slave_descr,
		.adapt_num   = 0,
		.bus         = EXT_SLAVE_BUS_SECONDARY,
		.address     = 0x18,
		.orientation = {
			0,  1,  0,
		   -1,  0,  0,
			0,  0,  1
		},
	},
	/* compass */
	.compass = {
		.get_slave_descr = get_compass_slave_descr,
		.adapt_num   = 4,
		.bus         = EXT_SLAVE_BUS_PRIMARY,
		.address     = 0x30,
		.orientation = {
			0, -1,  0,
			1,  0,  0,
			0,  0, -1
		},
	},
};
#endif

#if defined(CONFIG_PROXIMITY_GP2A030)
static void gp2a_power_on(int onoff)
{
		gpio_set_value(PS_VOUT_GAVINI_R0_0, onoff ? 1 : 0);
}
#endif

#if defined(CONFIG_PROXIMITY_GP2A)

/* -------------------------------------------------------------------------
 * GP2A PROXIMITY SENSOR PLATFORM-SPECIFIC CODE AND DATA
 * ------------------------------------------------------------------------- */
static int __init gp2a_setup(void);

static struct gp2a_platform_data gp2a_plat_data __initdata = {
	.ps_vout_gpio = PS_VOUT_GAVINI_R0_0,
	.hw_setup = gp2a_setup,
	.alsout = ADC_AUX2,
};

static int __init gp2a_setup(void)
{
	int err;

	/* Configure the GPIO for the interrupt */
	err = gpio_request(gp2a_plat_data.ps_vout_gpio, "PS_VOUT");
	if (err < 0)	{
		pr_err("PS_VOUT: failed to request GPIO %d,"
			" err %d\n", gp2a_plat_data.ps_vout_gpio, err);

		goto err1;
	}

	err = gpio_direction_input(gp2a_plat_data.ps_vout_gpio);
	if (err < 0)
	{
		pr_err("PS_VOUT: failed to configure input"
			" direction for GPIO %d, err %d\n",
			gp2a_plat_data.ps_vout_gpio, err);

		goto err2;
	}

	return 0;

err2:
	gpio_free(gp2a_plat_data.ps_vout_gpio);
err1:
	return err;
}

#endif

#if defined(CONFIG_BACKLIGHT_KTD259)
/* The following table is used to convert brightness level to the LED
    Current Ratio expressed as (full current) /(n * 32).
    i.e. 1 = 1/32 full current. Zero indicates LED is powered off.
    The table is intended to allow the brightness level to be "tuned"
    to compensate for non-linearity of brightness relative to current.
*/
static const unsigned short ktd259CurrentRatioLookupTable[] = {
0,		/* (0/32)		KTD259_BACKLIGHT_OFF */
30,		/* (1/32)		KTD259_MIN_CURRENT_RATIO */
38,		/* (2/32) */
46,		/* (3/32) */
54,		/* (4/32) */
62,		/* (5/32) */
71,		/* (6/32) */
79,		/* (7/32) */
87,		/* (8/32) */
95,		/* (9/32) */
103,	/* (10/32) */
111,	/* (11/32) */
119,	/* (12/32) */
127,	/* (13/32) default(120,180CD)*/
138,	/* (14/32) */
148,	/* (15/32) */
159,	/* (16/32) */
170,	/* (17/32) */
177,	/* (18/32) */
184,	/* (19/32) */
191,	/* (20/32) */
198,	/* (21/32) */
205,	/* (22/32) */
212,	/* (23/32) */
219,	/* (24/32) */
226,	/* (25/32) KTD259_MAX_CURRENT_RATIO*/
233,	/* (26/32) */
240,	/* (27/32) */
255,	/* (28/32) */
300,	/* (29/32) */
300,	/* (30/32) */
300,	/* (31/32) */
300		/* (32/32) */
};

static struct ktd259x_bl_platform_data gavini_bl_platform_info = {
	.bl_name			= "pwm-backlight",
	.ctrl_gpio			= LCD_BL_CTRL_GAVINI_R0_0,
	.ctrl_high			= 1,
	.ctrl_low			= 0,
	.max_brightness			= 255,
	.brightness_to_current_ratio	= ktd259CurrentRatioLookupTable,
};

static struct platform_device gavini_backlight_device = {
	.name = BL_DRIVER_NAME_KTD259,
	.id = -1,
	.dev = {
		.platform_data = &gavini_bl_platform_info,
	},
};

static struct ktd259x_bl_platform_data gavini_bl_platform_info_r0_3 = {
	.bl_name			= "pwm-backlight",
	.ctrl_gpio			= LCD_BL_CTRL_GAVINI_R0_3,
	.ctrl_high			= 1,
	.ctrl_low			= 0,
	.max_brightness			= 255,
	.brightness_to_current_ratio	= ktd259CurrentRatioLookupTable,
};

static struct platform_device gavini_backlight_device_r0_3 = {
	.name = BL_DRIVER_NAME_KTD259,
	.id = -1,
	.dev = {
		.platform_data = &gavini_bl_platform_info_r0_3,
	},
};

#endif


#if defined(CONFIG_IMMERSION_TSPDRV)
/* --------------------------------------------------------------------
* Immersion tspdrv and ISA1200 platform specific data
* ------------------------------------------------------------------ */

static int __init immvibe_setup( void );

static struct isa1200_platform_data isa1200_plat_data = {
	.mot_hen_gpio = MOT_HEN_GAVINI_R0_0,
	.mot_len_gpio = MOT_LEN_GAVINI_R0_0,
	.mot_clk = NULL,
	.hw_setup = immvibe_setup,
};

static int __init immvibe_setup( void )
{
	int ret;

	ret = gpio_request(isa1200_plat_data.mot_hen_gpio, "MOT_HWEN_GPIO");
	if (ret < 0) {
		pr_err("failed to request MOT_HWEN GPIO, err %d\n", ret);
		goto hwen_gpio_req_failed;
	}
	ret = gpio_direction_output(isa1200_plat_data.mot_hen_gpio, 0);
	if (ret < 0) {
		pr_err("failed to configure output direction for GPIO %d, err %d\n",
			isa1200_plat_data.mot_hen_gpio, ret);
		goto hwen_gpio_failed;
	}

	ret = gpio_request(isa1200_plat_data.mot_len_gpio, "MOT_LDOEN_GPIO");
	if (ret < 0) {
		pr_err("failed to request MOT_LDOEN GPIO, err %d\n", ret);
		goto hwen_gpio_failed;
	}
	ret = gpio_direction_output(isa1200_plat_data.mot_len_gpio, 0);
	if (ret < 0) {
		pr_err("failed to configure output direction for GPIO %d, err %d\n",
			isa1200_plat_data.mot_len_gpio, ret);
		goto ldo_en_gpio_failed;
	}

	printk(KERN_ERR "%s(), Vibrator gpio's are configured.\n", __func__);
	return ret;

ldo_en_gpio_failed:
	gpio_free(isa1200_plat_data.mot_len_gpio);
hwen_gpio_failed:
	gpio_free(isa1200_plat_data.mot_hen_gpio);
	return ret;
hwen_gpio_req_failed:
	return ret;

}
#endif

static DEFINE_SPINLOCK(dock_gpio_lock);
static bool fsa880_dock_gpio;
static bool sec_jack_dock_gpio;
static void set_shared_dock_gpio(void)
{
	/* earjack is removed && dock is connected */
	if (!sec_jack_dock_gpio && fsa880_dock_gpio)
		gpio_direction_output(EARSPK_SEL_GAVINI_R0_0_C, 1);
	else
		gpio_direction_output(EARSPK_SEL_GAVINI_R0_0_C, 0);
}

void fsa880_set_dock_gpio(bool on)
{
	unsigned long flags;
	spin_lock_irqsave(&dock_gpio_lock, flags);
	fsa880_dock_gpio = on;
	set_shared_dock_gpio();
	spin_unlock_irqrestore(&dock_gpio_lock, flags);
}

#if defined(CONFIG_USB_SWITCHER)
static void tsu6111_reset(void)
{
	/* Hold SCL&SDA Low more than 30ms */
	gpio_direction_output(GAVINI_GPIO_MUS_SCL, 0);
	gpio_direction_output(GAVINI_GPIO_MUS_SDA, 0);

	mdelay(35);

	/*Make SCL&SDA High again*/
	gpio_direction_output(GAVINI_GPIO_MUS_SCL, 1);
	gpio_direction_output(GAVINI_GPIO_MUS_SDA, 1);

	/* GPIOs will be set back to normal state by
	I2C driver during next transfer */
}

static struct usb_switch fsa880_data =	{
		.name					=	"FSA880",
		.id					=	0x0	,
		.id_mask				=	0xff	,
		.control_register_default		=	0x05	,
		.control_register_inital_value		=	0x1e	,
		.smd_gpio				= 0xFFFF,	/* not for Gavini */
		.charger_detect_gpio			=	0xffff	, /*no charger detect gpio for this device*/
		.irq_gpio				= JACK_NINT_GAVINI_R0_0,
		.valid_device_register_1_bits		=	0x74	,
		.valid_device_register_2_bits		=	0x8F	,
		.valid_registers			=	{0,1,1,1,1,0,0,1,0,0,1,1,0,0,0,0, 0, 0, 0, 1, 1  },
		.set_dock_gpio			=	fsa880_set_dock_gpio,
		.reset_cb				= tsu6111_reset,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT224E)
/*atmel_mxt224E*/
static void mxt224_power_on(void)
{
	gpio_direction_output(TSP_LDO_ON1_GAVINI_R0_0, 1);
	mdelay(70);
	printk("mxt224_power_on is finished\n");
}

static void mxt224_power_off(void)
{
	gpio_direction_output(TSP_LDO_ON1_GAVINI_R0_0, 0);

	printk("mxt224_power_off is finished\n");
}

//static void mxt224_register_callback(void *function)
//{
//	printk("mxt224_register_callback\n");
//	charging_cbs.tsp_set_charging_cable = function;
//}

static void mxt224_read_ta_status(bool *ta_status)
{
	*ta_status = is_cable_attached;
}

#define MXT224_MAX_MT_FINGERS		10

/*
	Configuration for MXT224
*/
#define MXT224_THRESHOLD_BATT		40
#define MXT224_THRESHOLD_CHRG		70
#define MXT224_ATCHCALST		9
#define MXT224_ATCHCALTHR		30

static u8 t7_config[] = {GEN_POWERCONFIG_T7,
				64, 255, 15};
static u8 t8_config[] = {GEN_ACQUISITIONCONFIG_T8,
				10, 0, 5, 0, 0, 0, MXT224_ATCHCALST, MXT224_ATCHCALTHR};
static u8 t9_config[] = {TOUCH_MULTITOUCHSCREEN_T9,
				131, 0, 0, 19, 11, 0, 32,  MXT224_THRESHOLD_BATT, 2, 1, 0, 10, 1,
				11, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
				223, 1, 0, 0, 0, 0, 143, 55, 143, 90, 18};

static u8 t18_config[] = {SPT_COMCONFIG_T18,
				0, 1};
static u8 t20_config[] = {PROCI_GRIPFACESUPPRESSION_T20,
				7, 0, 0, 0, 0, 0, 0, 30, 20, 4, 15, 10};
static u8 t22_config[] = {PROCG_NOISESUPPRESSION_T22,
				13, 0, 0, 0, 0, 0, 0, 3, 30, 0, 0,  29, 34, 39,
				49, 58, 3};
static u8 t28_config[] = {SPT_CTECONFIG_T28,
				0, 0, 3, 16, 19, 60};
static u8 end_config[] = {RESERVED_T255};

static const u8 *mxt224_config[] = {
	t7_config,
	t8_config,
	t9_config,
	t18_config,
	t20_config,
	t22_config,
	t28_config,
	end_config,
};

/*
	Configuration for MXT224-E
*/
#define MXT224E_THRESHOLD_BATT		50
#define MXT224E_THRESHOLD_CHRG		40
#define MXT224E_CALCFG_BATT		64
#define MXT224E_CALCFG_CHRG		82
#define MXT224E_ATCHFRCCALTHR_NORMAL		40
#define MXT224E_ATCHFRCCALRATIO_NORMAL		55

static u8 t7_config_e[] = {GEN_POWERCONFIG_T7,
				48, 255, 25};

static u8 t8_config_e[] = {GEN_ACQUISITIONCONFIG_T8,
				27, 0, 5, 1, 0, 0, 4, 35, MXT224E_ATCHFRCCALTHR_NORMAL, MXT224E_ATCHFRCCALRATIO_NORMAL};

#if defined(CONFIG_MACH_T1_CHN)
static u8 t9_config_e[] = {TOUCH_MULTITOUCHSCREEN_T9,
				139, 0, 0, 19, 11, 0, 32, MXT224E_THRESHOLD_BATT, 2, 1, 10, 15, 1,
				46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
				223, 1, 10, 10, 10, 10, 143, 40, 143, 80, 18, 15, 50, 50, 0};
#else
static u8 t9_config_e[] = {TOUCH_MULTITOUCHSCREEN_T9,
				139, 0, 0, 19, 11, 0, 32, MXT224E_THRESHOLD_BATT, 2, 1, 10, 3, 1,
				46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
				223, 1, 10, 10, 10, 10, 143, 40, 143, 80, 18, 15, 50, 50, 0};
#endif

static u8 t15_config_e[] = {TOUCH_KEYARRAY_T15,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static u8 t18_config_e[] = {SPT_COMCONFIG_T18,
				0, 0};

static u8 t19_config_e[] = {SPT_GPIOPWM_T19,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static u8 t23_config_e[] = {TOUCH_PROXIMITY_T23,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static u8 t25_config_e[] = {SPT_SELFTEST_T25,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static u8 t40_config_e[] = {PROCI_GRIPSUPPRESSION_T40,
				0, 0, 0, 0, 0};

static u8 t42_config_e[] = {PROCI_TOUCHSUPPRESSION_T42,
				0, 0, 0, 0, 0, 0, 0, 0};

static u8 t46_config_e[] = {SPT_CTECONFIG_T46,
				0, 3, 24, 35, 0, 0, 1, 0};

static u8 t47_config_e[] = {PROCI_STYLUS_T47,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static u8 t48_config_e[] = {PROCG_NOISESUPPRESSION_T48,
				3, 132, MXT224E_CALCFG_BATT, 0, 0, 0, 0, 0, 10, 20, 0, 0, 0,
				6,	6, 0, 0, 64, 4, 64, 10, 0, 20, 5, 0, 38, 0, 5,
				0, 0, 0, 0, 0, 0, 32, MXT224E_THRESHOLD_BATT, 2, 3, 1, 46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 10,
				10, 10, 143, 40, 143, 80, 18, 15, 0 };

static u8 t48_config_chrg_e[] = {PROCG_NOISESUPPRESSION_T48,
				3, 132, MXT224E_CALCFG_CHRG, 0, 0, 0, 0, 0, 10, 20, 0, 0, 0,
				6,	6, 0, 0, 64, 4, 64, 10, 0, 20, 5, 0, 38, 0, 20,
				0, 0, 0, 0, 0, 0, 0, MXT224E_THRESHOLD_CHRG, 2, 5, 2, 47, MXT224_MAX_MT_FINGERS, 5, 40, 235, 235,
				10, 10, 170, 50, 143, 80, 18, 15, 0 };

static u8 end_config_e[] = {RESERVED_T255};

static const u8 *mxt224e_config[] = {
	t7_config_e,
	t8_config_e,
	t9_config_e,
	t15_config_e,
	t18_config_e,
	t19_config_e,
	t23_config_e,
	t25_config_e,
	t40_config_e,
	t42_config_e,
	t46_config_e,
	t47_config_e,
	t48_config_e,
	end_config_e,
};

static struct mxt224_platform_data mxt224_data = {
	.max_finger_touches = MXT224_MAX_MT_FINGERS,
	.gpio_read_done = TSP_INT_GAVINI_R0_0,
	.config = mxt224_config,
	.config_e = mxt224e_config,
	.min_x = 0,
	.max_x = 479,
	.min_y = 0,
	.max_y = 799,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.atchcalst = MXT224_ATCHCALST,
	.atchcalsthr = MXT224_ATCHCALTHR,
	.tchthr_batt = MXT224_THRESHOLD_BATT,
	.tchthr_charging = MXT224_THRESHOLD_CHRG,
	.tchthr_batt_e = MXT224E_THRESHOLD_BATT,
	.tchthr_charging_e = MXT224E_THRESHOLD_CHRG,
	.calcfg_batt_e = MXT224E_CALCFG_BATT,
	.calcfg_charging_e = MXT224E_CALCFG_CHRG,
	.atchfrccalthr_e = MXT224E_ATCHFRCCALTHR_NORMAL,
	.atchfrccalratio_e = MXT224E_ATCHFRCCALRATIO_NORMAL,
	.t48_config_batt_e = t48_config_e,
	.t48_config_chrg_e = t48_config_chrg_e,
	.power_on = mxt224_power_on,
	.power_off = mxt224_power_off,
	//.register_cb = mxt224_register_callback,
	.read_ta_status = mxt224_read_ta_status,
};
#endif

static struct i2c_board_info __initdata gavini_r0_0_i2c0_devices[] = {
#if defined(CONFIG_PROXIMITY_GP2A030)
	{
		I2C_BOARD_INFO("gp2a", 0x72>>1),
	},
#endif
};

static struct i2c_board_info __initdata gavini_r0_0_i2c0_devices_rev00c[] = {
#if defined(CONFIG_MPU_SENSORS_MPU3050)
	{
		I2C_BOARD_INFO(MPU_NAME, DEFAULT_MPU_SLAVEADDR),
		.irq = GPIO_TO_IRQ(SENSOR_INT_GAVINI_R0_0),
		.platform_data = &mpu_data_gavini_r00,
	},
#endif
};

static struct i2c_board_info __initdata gavini_r0_0_i2c0_devices_rev00d[] = {
#if defined(CONFIG_MPU_SENSORS_MPU3050)
	{
		I2C_BOARD_INFO(MPU_NAME, DEFAULT_MPU_SLAVEADDR),
		.irq = GPIO_TO_IRQ(SENSOR_INT_GAVINI_R0_0),
		.platform_data = &mpu_data_gavini_r01,
	},
#endif
};

#if defined(CONFIG_PROXIMITY_GP2A030)
static struct gp2a_platform_data opt_gp2a_data = {
	.power_on = gp2a_power_on,
	.p_out = PS_VOUT_GAVINI_R0_0,
	.adapt_num	= 0,
	.addr = 0x72>>1,
};

static struct platform_device opt_gp2a = {
	.name = "gp2a-opt",
	.id = -1,
	.dev        = {
		.platform_data  = &opt_gp2a_data,
	},
};
#endif

#if defined(CONFIG_PROXIMITY_GP2A030)
static struct gp2a_platform_data opt_gp2a_data_00C = {
	.power_on = gp2a_power_on,
	.p_out = PS_VOUT_GAVINI_R0_0,
	.adapt_num	= 9,
	.addr = 0x72>>1,
};

static struct platform_device opt_gp2a_00C = {
	.name = "gp2a-opt",
	.id = -1,
	.dev        = {
		.platform_data  = &opt_gp2a_data_00C,
	},
};
#endif

static struct i2c_board_info __initdata gavini_r0_0_i2c1_devices[] = {
#if defined(CONFIG_USB_SWITCHER)
	{
		I2C_BOARD_INFO("musb", 0x25),
		.platform_data = &fsa880_data ,
		.irq = GPIO_TO_IRQ(JACK_NINT_GAVINI_R0_0),
	},
#endif
};

static struct i2c_board_info __initdata gavini_r0_0_c_i2c2_devices[] = {
#if defined(CONFIG_USB_SWITCHER)
	{
		I2C_BOARD_INFO("musb", 0x25),
		.platform_data = &fsa880_data ,
		.irq = GPIO_TO_IRQ(JACK_NINT_GAVINI_R0_0),
	},
#endif
};

#if defined(CONFIG_TOUCHSCREEN_MMS136_TS)
static void mms_ts_pin_set_pull(int pin, bool to_up)
{
	int ret;
	int pull = (to_up) ? NMK_GPIO_PULL_UP : NMK_GPIO_PULL_DOWN;

	ret = nmk_gpio_set_pull(pin, pull);
	if (ret < 0)
		printk(KERN_ERR "%s: fail to set %3d pull %d\n",
							__func__, pin, to_up);
}

static int mms_ts_pin_configure(bool to_gpios)
{
	/* TOUCH_EN is always an output */
	if (to_gpios) {
		gpio_direction_output(TSP_INT_GAVINI_R0_0, 0);

		nmk_gpio_set_mode(TSP_SCL_GAVINI_R0_0, NMK_GPIO_ALT_GPIO);
		gpio_direction_output(TSP_SCL_GAVINI_R0_0, 0);

		nmk_gpio_set_mode(TSP_SDA_GAVINI_R0_0, NMK_GPIO_ALT_GPIO);
		gpio_direction_output(TSP_SDA_GAVINI_R0_0, 0);

	} else {
		gpio_direction_output(TSP_INT_GAVINI_R0_0, 1);
		gpio_direction_input(TSP_INT_GAVINI_R0_0);

		gpio_direction_output(TSP_SCL_GAVINI_R0_0, 1);
		nmk_gpio_set_mode(TSP_SCL_GAVINI_R0_0, NMK_GPIO_ALT_C);

		gpio_direction_output(TSP_SDA_GAVINI_R0_0, 1);
		nmk_gpio_set_mode(TSP_SDA_GAVINI_R0_0, NMK_GPIO_ALT_C);
	}
	return 0;
}

unsigned int mms_ts_key_map[] = {KEY_MENU, KEY_BACK};

struct mms_ts_platform_data mms_ts_pdata = {
	.max_x			= 480,
	.max_y			= 800,
	.gpio_sda		= TSP_SDA_GAVINI_R0_0,
	.gpio_scl		= TSP_SCL_GAVINI_R0_0,
	.gpio_int		= TSP_INT_GAVINI_R0_0,
	.gpio_vdd_en		= TSP_LDO_ON_GAVINI_R0_0,
	.pin_configure		= mms_ts_pin_configure,
	.fw_name_ums		= "/sdcard/firmware/melfas/mms136_ts.fw.bin",
	.key_map		= mms_ts_key_map,
	.key_nums		= ARRAY_SIZE(mms_ts_key_map),
	.pin_set_pull		= mms_ts_pin_set_pull,
};

void __init mms136_ts_init(void)
{
	gpio_request(TSP_INT_GAVINI_R0_0, "tsp_int_n");
	gpio_direction_input(TSP_INT_GAVINI_R0_0);

	gpio_request(TSP_LDO_ON_GAVINI_R0_0, "tsp_en");
	gpio_request(TSP_SCL_GAVINI_R0_0, "ap_i2c3_scl");
	gpio_request(TSP_SDA_GAVINI_R0_0, "ap_i2c3_sda");

	if (board_id >= 13)
		mms_ts_pdata.fw_name_builtin = "melfas/mms136_ts_l.fw";
	else
		mms_ts_pdata.fw_name_builtin = "melfas/mms136_ts.fw";
}

#endif

static struct i2c_board_info __initdata gavini_r0_0_i2c3_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_MMS136_TS)
	{
		I2C_BOARD_INFO("mms_ts", 0x48),
		.platform_data	= &mms_ts_pdata,
		.irq = GPIO_TO_IRQ(TSP_INT_GAVINI_R0_0),
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_MELFAS)
	{
		I2C_BOARD_INFO("melfas-ts", 0x48),
//		.platform_data	= &mxt224_data,
		.irq = GPIO_TO_IRQ(TSP_INT_GAVINI_R0_0),
	},
#endif
};

static struct i2c_gpio_platform_data gavini_gpio_i2c4_data = {
	.sda_pin = SUBPMU_SDA_GAVINI_R0_0,
	.scl_pin = SUBPMU_SCL_GAVINI_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device gavini_gpio_i2c4_pdata = {
	.name = "i2c-gpio",
	.id = 4,
	.dev = {
		.platform_data = &gavini_gpio_i2c4_data,
	},
};

static struct i2c_board_info __initdata gavini_r0_0_gpio_i2c4_devices[] = {
// TBD - SUBPMU NCP6914
#if defined(CONFIG_INPUT_YAS_MAGNETOMETER)
		 {
			I2C_BOARD_INFO("geomagnetic", 0x2e),
			.platform_data = &yas_data,
		 },
#endif
#if defined(CONFIG_SENSORS_MMC328X)
	{
		/* Compass */
		I2C_BOARD_INFO("mmc328x", 0x30),
	},
#endif
	{
		/* ncp6914 power management IC for the cameras */
		I2C_BOARD_INFO("ncp6914", 0x10),
		//.platform_data = &ncp6914_plat_data,
	},
};


static struct i2c_gpio_platform_data gavini_gpio_i2c5_data = {
	.sda_pin = MPR_SDA_GAVINI_R0_0,
	.scl_pin = MPR_SCL_GAVINI_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device gavini_gpio_i2c5_pdata = {
	.name = "i2c-gpio",
	.id = 5,
	.dev = {
		.platform_data = &gavini_gpio_i2c5_data,
	},
};

static struct i2c_board_info __initdata gavini_r0_0_gpio_i2c5_devices[] = {
	{
		I2C_BOARD_INFO("dpp2601", 0x36 >> 1),
		.irq = GPIO_TO_IRQ(PROJECTOR_INTZ_GAVINI_R0_0),
		.platform_data = &projector_dpp2601_pdata,
	},
};

#if defined(CONFIG_KEYBOARD_CYPRESS_TOUCH)
static struct i2c_board_info __initdata gavini_r0_1_gpio_i2c5_devices[] = {
{
		I2C_BOARD_INFO("cypress_touchkey", 0x20),
		.irq = GPIO_TO_IRQ(TOUCHKEY_INT_GAVINI_R0_0),
		.platform_data = &cypress_touchkey_pdata,
	},
};
#endif

static struct i2c_gpio_platform_data gavini_gpio_i2c6_data = {
	.sda_pin = MOT_SDA_GAVINI_R0_0,
	.scl_pin = MOT_SCL_GAVINI_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device gavini_gpio_i2c6_pdata = {
	.name = "i2c-gpio",
	.id = 6,
	.dev = {
		.platform_data = &gavini_gpio_i2c6_data,
	},
};

static struct i2c_board_info __initdata gavini_r0_0_gpio_i2c6_devices[] = {

	{
		I2C_BOARD_INFO("immvibe", 0x92 >> 1),		// 0x90 when SADD pin is LOW
		.platform_data = &isa1200_plat_data,
	},

};


static struct i2c_gpio_platform_data gavini_gpio_i2c7_data = {
	.sda_pin = NFC_SDA_GAVINI_R0_0,
	.scl_pin = NFC_SCL_GAVINI_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device gavini_gpio_i2c7_pdata = {
	.name = "i2c-gpio",
	.id = 7,
	.dev = {
		.platform_data = &gavini_gpio_i2c7_data,
	},
};

static struct i2c_board_info __initdata gavini_r0_0_gpio_i2c7_devices[] = {
// TBD - NFC
#if 0
	{
		I2C_BOARD_INFO("", 0x30),
	},
#endif
};

static struct i2c_gpio_platform_data gavini_gpio_i2c8_data = {
	.sda_pin = SENSOR_SDA_GAVINI_R0_0,
	.scl_pin = SENSOR_SCL_GAVINI_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device gavini_gpio_i2c8_pdata = {
	.name = "i2c-gpio",
	.id = 8,
	.dev = {
		.platform_data = &gavini_gpio_i2c8_data,
	},
};

static struct i2c_board_info __initdata gavini_r0_0_gpio_i2c8_devices[] = {
#if defined(CONFIG_MPU_SENSORS_MPU3050)
		{
			I2C_BOARD_INFO(MPU_NAME, DEFAULT_MPU_SLAVEADDR),
			.irq = GPIO_TO_IRQ(SENSOR_INT_GAVINI_R0_0),
			.platform_data = &mpu_data_gavini_r00,
		},
#endif

};

#if defined(CONFIG_PROXIMITY_GP2A030)
static struct i2c_gpio_platform_data gavini_gpio_i2c9_data= {
	.sda_pin = AGC_I2C_SDA_GAVINI_R0_0_00C,
	.scl_pin = AGC_I2C_SCL_GAVINI_R0_0_00C,
	.udelay = 5,
};

static struct platform_device gavini_gpio_i2c9_pdata = {
	.name = "i2c-gpio",
	.id = 9,
	.dev = {
		.platform_data = &gavini_gpio_i2c9_data,
	},
};
#endif

static struct i2c_board_info __initdata gavini_r0_0_c_gpio_i2c9_devices[] = {
#if defined(CONFIG_MPU_SENSORS_MPU3050)
		{
			I2C_BOARD_INFO("gp2a", 0x72>>1),
		},
#endif

};

#ifdef CONFIG_KEYBOARD_GPIO
struct gpio_keys_button gavini_r0_0_gpio_keys[] = {
	{
	.code = KEY_HOME,		/* input event code (KEY_*, SW_*) */
	.gpio = HOME_KEY_GAVINI_R0_0,
	.active_low = 1,
	.desc = "home_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 1,		/* configure the button as a wake-up source */
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
	{
	.code = KEY_VOLUMEUP,		/* input event code (KEY_*, SW_*) */
	.gpio = VOL_UP_GAVINI_R0_0,
	.active_low = 1,
	.desc = "volup_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 0,		/* configure the button as a wake-up source */
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
	{
	.code = KEY_VOLUMEDOWN,		/* input event code (KEY_*, SW_*) */
	.gpio = VOL_DOWN_GAVINI_R0_0,
	.active_low = 1,
	.desc = "voldown_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 0,		/* configure the button as a wake-up source */
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
	{
	.code = KEY_F24,		/* input event code (KEY_*, SW_*) */
	.gpio = PRJ_ON_HOT_KEY_GAVINI_R0_0,
	.active_low = 1,
	.desc = "projector_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 0,		/* configure the button as a wake-up source */
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
};

struct gpio_keys_platform_data gavini_r0_0_gpio_data = {
	.buttons = gavini_r0_0_gpio_keys,
	.nbuttons = ARRAY_SIZE(gavini_r0_0_gpio_keys),
};

struct platform_device gavini_gpio_keys_device = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &gavini_r0_0_gpio_data,
	},
};
#endif

#ifdef CONFIG_LEDS_CLASS
static struct gpio_led gavini_touchkey_leds[] = {
	{
		.name			= "button-backlight",
		.gpio			= EN_LED_LDO_GAVINI_R0_0,
		.active_low		= 0,
		.retain_state_suspended = 0,
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data gavini_gpio_leds_pdata = {
	.num_leds	= ARRAY_SIZE(gavini_touchkey_leds),
	.leds		= gavini_touchkey_leds,
	.gpio_blink_set	= NULL,
};

static struct platform_device gavini_gpio_leds_device = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &gavini_gpio_leds_pdata,
	},
};
#endif

#ifdef CONFIG_USB_ANDROID
/*
static char *usb_functions_adb[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef CONFIG_USB_ANDROID_ECM
	"cdc_ethernet",
#endif
};
*/

static char *usb_functions_ums[] = {
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
		"usb_mass_storage",
#endif
};

static char *usb_functions_rndis[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
};

static char *usb_functions_phonet[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_PHONET
	"phonet",
#endif
};

static char *usb_functions_ecm[] = {
#ifdef CONFIG_USB_ANDROID_ECM
	"cdc_ethernet",
#endif
};

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : Variables for samsung composite such as kies, mtp, ums, etc... */
/* kies mode */
static char *usb_functions_acm_mtp[] = {
	"mtp",
	"acm",
};

#ifdef CONFIG_USB_ANDROID_ECM /* Temp !! will  be deleted 2011.04.12*/
/*Temp debug mode */
static char *usb_functions_acm_mtp_adb[] = {
	"mtp",
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
	"cdc_ethernet",
};
#else
/* debug mode */
static char *usb_functions_acm_mtp_adb[] = {
	"mtp",
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
};
#endif

/* mtp only mode */
static char *usb_functions_mtp[] = {
	"mtp",
};

#else /* android original composite*/
static char *usb_functions_ums_adb[] = {
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
};
#endif

static char *usb_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : Every function driver for samsung composite.
 *		  Number of to enable function features have to be same as below.
 */
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_ECM
	"cdc_ethernet",
#endif
#ifdef CONFIG_USB_ANDROID_SAMSUNG_MTP
	"mtp",
#endif
#ifdef CONFIG_USB_ANDROID_PHONET
	"phonet",
#endif
#else /* original */
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */
};


static struct android_usb_product usb_products[] = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	/* soonyong.cho : Please modify below value correctly if you customize composite */
#ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE /* USE DEVGURU HOST DRIVER */
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_acm_mtp_adb),
		.functions	= usb_functions_acm_mtp_adb,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_DEBUG_CONFIG_STRING,
		.mode		= USBSTATUS_ADB,
	},
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_acm_mtp),
		.functions	= usb_functions_acm_mtp,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_KIES_CONFIG_STRING,
		.mode		= USBSTATUS_SAMSUNG_KIES,
	},
	{
		.product_id	= SAMSUNG_UMS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_UMS_CONFIG_STRING,
		.mode		= USBSTATUS_UMS,
	},
	{
		.product_id = SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
#ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
#else
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass	= USB_CLASS_WIRELESS_CONTROLLER,
#else
		.bDeviceClass	= USB_CLASS_COMM,
#endif
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
#endif
		.s		= ANDROID_RNDIS_CONFIG_STRING,
		.mode		= USBSTATUS_VTP,
	},
#ifdef CONFIG_USB_ANDROID_PHONET
	{
		.product_id = SAMSUNG_PHONET_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_phonet),
		.functions	= usb_functions_phonet,
		.bDeviceClass	= USB_CLASS_CDC_DATA,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_PHONET_CONFIG_STRING,
		.mode		= USBSTATUS_PHONET,
	},
#endif
	{
		.product_id = SAMSUNG_MTP_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp),
		.functions	= usb_functions_mtp,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_MTP_CONFIG_STRING,
		.mode		= USBSTATUS_MTPONLY,
	},

	/*
	{
		.product_id	= 0x685d,
		.num_functions	= ARRAY_SIZE(usb_functions_adb),
		.functions	= usb_functions_adb,
	},

	*/
#else /* USE MCCI HOST DRIVER */
	{
		.product_id = SAMSUNG_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_acm_mtp_adb),
		.functions	= usb_functions_acm_mtp_adb,
		.bDeviceClass	= USB_CLASS_COMM,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_DEBUG_CONFIG_STRING,
		.mode		= USBSTATUS_ADB,
	},
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID, /* change sequence */
		.num_functions	= ARRAY_SIZE(usb_functions_acm_mtp),
		.functions	= usb_functions_acm_mtp,
		.bDeviceClass	= USB_CLASS_COMM,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_KIES_CONFIG_STRING,
		.mode		= USBSTATUS_SAMSUNG_KIES,
	},
	{
		.product_id = SAMSUNG_UMS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_UMS_CONFIG_STRING,
		.mode		= USBSTATUS_UMS,
	},
	{
		.product_id = SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass	= USB_CLASS_WIRELESS_CONTROLLER,
#else
		.bDeviceClass	= USB_CLASS_COMM,
#endif
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_RNDIS_CONFIG_STRING,
		.mode		= USBSTATUS_VTP,
	},
	{
		.product_id = SAMSUNG_MTP_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp),
		.functions	= usb_functions_mtp,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_MTP_CONFIG_STRING,
		.mode		= USBSTATUS_MTPONLY,
	},
#endif
#else  /* original android composite */
	{
		.product_id = ANDROID_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
	},
	{
		.product_id = ANDROID_ADB_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_adb),
		.functions	= usb_functions_ums_adb,
	},
#endif
};

static char android_usb_serial_num[USB_SERIAL_NUMBER_LEN] = "0123456789ABCDEF";

static struct android_usb_platform_data android_usb_pdata = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	.vendor_id	= SAMSUNG_VENDOR_ID,
	.product_id	= SAMSUNG_DEBUG_PRODUCT_ID,
#else
	.vendor_id	= ANDROID_VENDOR_ID,
	.product_id = ANDROID_PRODUCT_ID,
#endif
	.version	= 0x0100,
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	.product_name	= "SAMSUNG_Android",
	.manufacturer_name = "SAMSUNG",
#else
	.product_name	= "Android Phone",
	.manufacturer_name = "Android",
#endif
	.serial_number	= android_usb_serial_num,
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_all),
	.functions = usb_functions_all,
};

static struct platform_device android_usb_device = {
	.name	= "android_usb",
	.id		= -1,
	.dev		= {
	.platform_data = &android_usb_pdata,
	},
};

#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
static struct usb_mass_storage_platform_data mass_storage_pdata = {
	.nluns		= 2,
	.vendor		= "Samsung",
	.product	= "Android Phone",
	.release	= 0x0100,
};

static struct platform_device usb_mass_storage_device = {
	.name	= "usb_mass_storage",
	.id	= -1,
	.dev	= {
		.platform_data = &mass_storage_pdata,
	},
};
#endif /* CONFIG_USB_ANDROID_MASS_STORAGE */

#ifdef CONFIG_USB_ANDROID_ECM
static struct usb_ether_platform_data usb_ecm_pdata = {
	.ethaddr	= {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
	.vendorID	= 0x04e8,
	.vendorDescr = "Samsung",
};

struct platform_device usb_ecm_device = {
	.name	= "cdc_ethernet",
	.id	= -1,
	.dev	= {
		.platform_data = &usb_ecm_pdata,
	},
};
#endif /* CONFIG_USB_ANDROID_ECM */

#ifdef CONFIG_USB_ANDROID_RNDIS
static struct usb_ether_platform_data usb_rndis_pdata = {
	.ethaddr	= {0x01, 0x11, 0x22, 0x33, 0x44, 0x55},
	.vendorID	= SAMSUNG_VENDOR_ID,
	.vendorDescr = "Samsung",
};

struct platform_device usb_rndis_device = {
	.name = "rndis",
	.id = -1,
	.dev = {
		.platform_data = &usb_rndis_pdata,
	},
};
#endif /* CONFIG_USB_ANDROID_RNDIS */

#ifdef CONFIG_USB_ANDROID_PHONET
static struct usb_ether_platform_data usb_phonet_pdata = {
	.vendorID	= SAMSUNG_VENDOR_ID,//PHONET_VENDOR_ID,
	.vendorDescr = "Samsung",
};

struct platform_device usb_phonet_device = {
	.name = "phonet",
	.id = -1,
	.dev = {
		.platform_data = &usb_phonet_pdata,
	},
};
#endif /* CONFIG_USB_ANDROID_PHONET */

#endif /* CONFIG_USB_ANDROID */

#define U8500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, t_out, _sm) \
static struct nmk_i2c_controller gavini_i2c##id##_data = { \
	/*				\
	 * slave data setup time, which is	\
	 * 250 ns,100ns,10ns which is 14,6,2	\
	 * respectively for a 48 Mhz	\
	 * i2c clock			\
	 */				\
	.slsu		= _slsu,	\
	/* Tx FIFO threshold */		\
	.tft		= _tft,		\
	/* Rx FIFO threshold */		\
	.rft		= _rft,		\
	/* std. mode operation */	\
	.clk_freq	= clk,		\
	/* Slave response timeout(ms) */\
	.timeout	= t_out,	\
	.sm		= _sm,		\
}

/*
 * The board uses 4 i2c controllers, initialize all of
 * them with slave data setup time of 250 ns,
 * Tx & Rx FIFO threshold values as 1 and standard
 * mode of operation
 */
U8500_I2C_CONTROLLER(0, 0xe, 1, 8, 400000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(1, 0xe, 1, 8, 400000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(2, 0xe, 1, 8, 400000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(3, 0xe, 1, 8, 400000, 200, I2C_FREQ_MODE_FAST);

/* For Gavini GB : gavini_gpio_wq_kdebounce */
#if 0
/**
 * gavini_gpio_wq_kdebounce - work queue to debounce GPIO inputs.
 * @work:	pointer to gpio data
 *
 * Waits until GPIOI input are in steady state.
 */
#define DEBOUNCE_PERIOD		10	/* scan period in ms */
#define DEBOUNCE_CYCLES		6	/* Number of periods to check for bounce */

void gavini_gpio_wq_debounce(struct work_struct *work)
{
	int cnt;
	int input_state;
	struct gpio_debounce_t *pDb = container_of((struct work_struct *)work,
					   struct gpio_debounce_t, debounce_work);

	input_state = gpio_get_value( pDb->gpio );

	/* Debounce count */
	cnt = 0;

	/* Scan gpio input until it reachs a stable state for DEBOUNCE_CYCLES debounce periods */
	do
	{
		int value;

		msleep(DEBOUNCE_PERIOD);

		value = gpio_get_value( pDb->gpio );

		if ( input_state != value )
		{
			input_state = value;

			cnt = 0;
		}

		cnt++;
	}while ( cnt < DEBOUNCE_CYCLES );

	// Process action
	if ( pDb->pWork )
	{
		if ( pDb->wq )
		{
			queue_work(pDb->wq, pDb->pWork);
		}
		else
		{
			schedule_work( pDb->pWork );
		}
	}
	else if ( pDb->pFn )
	{
		pDb->pFn();

	}
	else
	{
		printk(KERN_ERR"Debounce: Action function not found\n");
	}

	if ( pDb->irq_disabled )
	{
		enable_irq( gpio_to_irq( pDb->gpio ) );
	}

}
#endif

/*
 * SSP
 */
#define NUM_SPI_CLIENTS 1
static struct pl022_ssp_controller gavini_spi0_data = {
	.bus_id		= SPI023_0_CONTROLLER,
	.num_chipselect	= NUM_SPI_CLIENTS,
};

#ifdef CONFIG_SPI_PL022
static void gavini_cs_control(u32 control)
{
	gpio_set_value(LCD_CSX_GAVINI_R0_0,control);
}

static struct pl022_config_chip gavini_spi0_controller = {
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	.hierarchy = SSP_MASTER,
	.slave_tx_disable = true,
	.clk_freq = {
		.cpsdvsr = 2,
		.scr = 100,
	},
	.com_mode = POLLING_TRANSFER, /* avoid interrupts */
	.cs_control = gavini_cs_control,
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias = "pri_lcd_spi",
		.controller_data = &gavini_spi0_controller,
		.max_speed_hz = 12000000,
		.bus_num = SPI023_0_CONTROLLER,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.irq = IRQ_DB8500_SPI0,
	},
};
#else
#define LCD_BUS_NUM     2

static struct spi_board_info spi_board_info[] __initdata = {
        {
		.modalias = "pri_lcd_spi",
                .max_speed_hz   = 1200000,
                .bus_num        = SPI023_0_CONTROLLER,
                .chip_select    = 0,
                .mode           = SPI_MODE_3,
                .controller_data = (void *)LCD_CSX_GAVINI_R0_0, //LCD_CS
        },
};

static struct spi_gpio_platform_data gavini_spi_gpio_data = {
        .sck    = LCD_CLK_GAVINI_R0_0,//LCD_CLK
        .mosi   = LCD_SDI_GAVINI_R0_0,//LCD_SDI
        .num_chipselect = 2,
};


static struct platform_device ux500_spi_gpio_device = {
        .name   = "spi_gpio",
        .id     = SPI023_0_CONTROLLER,
        .dev    = {
                .platform_data  = &gavini_spi_gpio_data,
        },
};

static struct spi_gpio_platform_data gavini_spi_gpio_data_rev8 = {
	/* scl->mosi, mosi->scl on rev08 */
	.sck    = LCD_SDI_GAVINI_R0_0,
	.mosi   = LCD_CLK_GAVINI_R0_0,
	.num_chipselect = 2,
};


static struct platform_device ux500_spi_gpio_device_rev8 = {
	.name   = "spi_gpio",
	.id     = SPI023_0_CONTROLLER,
	.dev    = {
		.platform_data  = &gavini_spi_gpio_data_rev8,
	},
};
#endif

static struct ab8500_gpio_platform_data ab8500_gpio_pdata = {
	.gpio_base		= AB8500_PIN_GPIO(1),
	.irq_base		= MOP500_AB8500_VIR_GPIO_IRQ_BASE,
	/* initial_pin_config is the initial configuration of ab8500 pins.
	 * The pins can be configured as GPIO or alt functions based
	 * on value present in GpioSel1 to GpioSel6 and AlternatFunction
	 * register. This is the array of 7 configuration settings.
	 * One has to compile time decide these settings. Below is the
	 * explaination of these setting
	 * GpioSel1 = 0xFF => 1-4 should be GPIO input, 5 Res,
	 *		      6-8 should be GPIO input
	 * GpioSel2 = 0xF1 => 9-13 GND, 14-16 NC. 9 is PD GND.
	 * GpioSel3 = 0x80 => Pin GPIO24 is configured as GPIO
	 * GpioSel4 = 0x75 => 25 is SYSCLKREQ8, but NC, should be GPIO
	 * GpioSel5 = 0x7A => Pins GPIO34, GPIO36 to GPIO39 are conf as GPIO
	 * GpioSel6 = 0x02 => 42 is NC
	 * AlternaFunction = 0x03 => If Pins GPIO10 to 13 are not configured
	 * as GPIO then this register selects the alternate fucntions
	 */
	.config_reg		= {0xFF, 0xFF, 0x81, 0xFD, 0x7A, 0x02, 0x03},

	/* initial_pin_direction allows for the initial GPIO direction to
	 * be set.
	 */
	.config_direction	= {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

	/*
	 * initial_pin_pullups allows for the intial configuration of the
	 * GPIO pullup/pulldown configuration.
	 */
	.config_pullups		= {0xE0, 0x1F, 0x00, 0x00, 0x80, 0x00},
};

static struct ab8500_sysctrl_platform_data ab8500_sysctrl_pdata = {
	/*
	 * SysClkReq1RfClkBuf - SysClkReq8RfClkBuf
	 * The initial values should not be changed because of the way
	 * the system works today
	 */
	.initial_req_buf_config
			= {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	.reboot_reason_code = reboot_reason_code,
};

#ifdef CONFIG_SAMSUNG_JACK
static struct sec_jack_zone sec_jack_zones[] = {
	{
		/* adc == 0, default to 3pole if it stays
		 * in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 0,
		.delay_ms = 20,
		.check_count = 2,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 0 < adc <= 660, unstable zone, default to 3pole if it stays
		 * in this range for a 600ms (30ms delays, 20 samples)
		 */
		.adc_high = 660,
		.delay_ms = 30,
		.check_count = 20,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 660 < adc <= 1000, unstable zone, default to 4pole if it
		 * stays in this range for 900ms (30ms delays, 30 samples)
		 */
		.adc_high = 1000,
		.delay_ms = 30,
		.check_count = 30,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* 1000 < adc <= 1825, default to 4 pole if it stays */
		/* in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 1825,
		.delay_ms = 20,
		.check_count = 2,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 1825, unstable zone, default to 3pole if it stays
		 * in this range for a second (10ms delays, 100 samples)
		 */
		.adc_high = 0x7fffffff,
		.delay_ms = 10,
		.check_count = 100,
		.jack_type = SEC_HEADSET_3POLE,
	},
};

/* to support 3-buttons earjack */
static struct sec_jack_buttons_zone sec_jack_buttons_zones[] = {
	{
		/* 0 <= adc <=82, stable zone */
		.code		= KEY_MEDIA,
		.adc_low	= 0,
		.adc_high	= 82,
	},
	{
		/* 83 <= adc <= 200, stable zone */
		.code		= KEY_VOLUMEUP,
		.adc_low	= 83,
		.adc_high	= 200,
	},
	{
		/* 201 <= adc <= 450, stable zone */
		.code		= KEY_VOLUMEDOWN,
		.adc_low	= 201,
		.adc_high	= 450,
	},
};

static int sec_jack_get_adc_value(void)
{
	return ab8500_gpadc_convert(ab8500_gpadc_get(), 5);

}

static void sec_jack_mach_init(struct platform_device *pdev)
{
	int ret = 0;
	/* initialise threshold for ACCDETECT1 comparator
	 * and the debounce for all ACCDETECT comparators */
	ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
		0x80, 0x31);
	if (ret < 0)
		pr_err("%s: ab8500 write failed\n", __func__);

	/* initialise threshold for ACCDETECT2 comparator1 and comparator2 */
	ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
		0x81, 0xB3);
	if (ret < 0)
		pr_err("%s: ab8500 write failed\n", __func__);

	ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
		0x82, 0x33);

	if (ret < 0)
		pr_err("%s: ab8500 write failed\n", __func__);

	/* set output polarity to Gnd when VAMIC1 is disabled */
	ret = abx500_set_register_interruptible(&pdev->dev, AB8500_REGU_CTRL1,
		0x84, 0x1);
	if (ret < 0)
		pr_err("%s: ab8500 write failed\n", __func__);
}

int sec_jack_get_det_level(struct platform_device *pdev)
{
	int value = 0;
	int ret = 0;

	abx500_get_register_interruptible(&pdev->dev, AB8500_INTERRUPT, 0x4,
		&value);
	ret = (value & 0x04) >> 2;
	pr_info("%s: ret=%x\n", __func__, ret);

	return ret;
}

void sec_jack_set_earspk_sel(bool on)
{
	unsigned long flags;
	spin_lock_irqsave(&dock_gpio_lock, flags);
	sec_jack_dock_gpio = on;
	set_shared_dock_gpio();
	spin_unlock_irqrestore(&dock_gpio_lock, flags);
}

struct sec_jack_platform_data sec_jack_pdata = {
	.get_adc_value = sec_jack_get_adc_value,
	.mach_init = sec_jack_mach_init,
	.get_det_level = sec_jack_get_det_level,
	.set_earspk_sel = sec_jack_set_earspk_sel,
	.zones = sec_jack_zones,
	.num_zones = ARRAY_SIZE(sec_jack_zones),
	.buttons_zones = sec_jack_buttons_zones,
	.num_buttons_zones = ARRAY_SIZE(sec_jack_buttons_zones),
	.det_r = "ACC_DETECT_1DB_R",
	.det_f = "ACC_DETECT_1DB_F",
	.buttons_r = "ACC_DETECT_21DB_R",
	.buttons_f = "ACC_DETECT_21DB_F",
	.regulator_mic_source = "v-amic1"
};
#endif

#ifdef CONFIG_MODEM_U8500
static struct platform_device u8500_modem_dev = {
	.name = "u8500-modem",
	.id   = 0,
	.dev  = {
		.platform_data = NULL,
	},
};
#endif


static struct dbx500_cpuidle_platform_data db8500_cpuidle_platform_data = {
	.wakeups = PRCMU_WAKEUP(ARM) | PRCMU_WAKEUP(RTC) | PRCMU_WAKEUP(ABB),
};

struct platform_device db8500_cpuidle_device = {
	.name	= "dbx500-cpuidle",
	.id	= -1,
	.dev	= {
		.platform_data = &db8500_cpuidle_platform_data,
	},
};

static struct dbx500_cpuidle_platform_data db9500_cpuidle_platform_data = {
	.wakeups = PRCMU_WAKEUP(ARM) | PRCMU_WAKEUP(RTC) | PRCMU_WAKEUP(ABB) \
		   | PRCMU_WAKEUP(HSI0),
};

struct platform_device db9500_cpuidle_device = {
	.name	= "dbx500-cpuidle",
	.id	= -1,
	.dev	= {
		.platform_data = &db9500_cpuidle_platform_data,
	},
};

/*
 * NOTE! The regulator configuration below must be in exactly the same order as
 * the regulator description in the driver, see drivers/regulator/ab8500.c
 */
static struct ab8500_platform_data ab8500_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
	.regulator	= &gavini_regulator_plat_data,
	.battery = &ab8500_bm_data,
	.charger = &ab8500_charger_plat_data,
	.btemp = &ab8500_btemp_plat_data,
	.fg = &ab8500_fg_plat_data,
	.chargalg = &ab8500_chargalg_plat_data,
	.gpio = &ab8500_gpio_pdata,
	.sysctrl	= &ab8500_sysctrl_pdata,
#ifdef CONFIG_SAMSUNG_JACK
       .accdet = &sec_jack_pdata,
#endif
#ifdef CONFIG_PM
	.pm_power_off = true,
#endif
	.thermal_time_out = 20, /* seconds */
};

static struct resource ab8500_resources[] = {
	[0] = {
		.start = IRQ_DB8500_AB8500,
		.end = IRQ_DB8500_AB8500,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device ab8500_device = {
	.name = "ab8500-i2c",
	.id = 0,
	.dev = {
		.platform_data = &ab8500_platdata,
	},
	.num_resources = 1,
	.resource = ab8500_resources,
};

static struct platform_device *gavini_platform_devices[] __initdata = {
	/*TODO - add platform devices here */

};

static struct platform_device sec_device_rfkill = {
	.name = "bt_rfkill",
	.id = -1,
};

static pin_cfg_t mop500_pins_uart0[] = {
	GPIO0_U0_CTSn   | PIN_INPUT_PULLUP,
	GPIO1_U0_RTSn   | PIN_OUTPUT_HIGH,
	GPIO2_U0_RXD    | PIN_INPUT_PULLUP,
	GPIO3_U0_TXD    | PIN_OUTPUT_HIGH,
};

static void ux500_uart0_init(void)
{
	int ret;

	ret = nmk_config_pins(mop500_pins_uart0,
			ARRAY_SIZE(mop500_pins_uart0));
	if (ret < 0)
		pr_err("pl011: uart pins_enable failed\n");
}

static void ux500_uart0_exit(void)
{
	int ret;

	ret = nmk_config_pins_sleep(mop500_pins_uart0,
			ARRAY_SIZE(mop500_pins_uart0));
	if (ret < 0)
		pr_err("pl011: uart pins_disable failed\n");
}

static void u8500_uart0_reset(void)
{
	/* UART0 lies in PER1 */
	return u8500_reset_ip(1, PRCC_K_SOFTRST_UART0_MASK);
}

static void u8500_uart1_reset(void)
{
	/* UART1 lies in PER1 */
	return u8500_reset_ip(1, PRCC_K_SOFTRST_UART1_MASK);
}

static void u8500_uart2_reset(void)
{
	/* UART2 lies in PER3 */
	return u8500_reset_ip(3, PRCC_K_SOFTRST_UART2_MASK);
}

static void bt_wake_peer(struct uart_port *port)
{
	printk("@@@@ BT WAKE_PEER\n");
	return;
}

static struct amba_pl011_data uart0_plat = {
#ifdef CONFIG_STE_DMA40_REMOVE
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart0_dma_cfg_rx,
	.dma_tx_param = &uart0_dma_cfg_tx,
#endif
	.init = ux500_uart0_init,
	.exit = ux500_uart0_exit,
    .reset = u8500_uart0_reset,
	.amba_pl011_wake_peer = bt_wake_peer,
};

static struct amba_pl011_data uart1_plat = {
#ifdef CONFIG_STE_DMA40_REMOVE
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart1_dma_cfg_rx,
	.dma_tx_param = &uart1_dma_cfg_tx,
#endif
	.reset = u8500_uart1_reset,
	.amba_pl011_wake_peer = NULL,
};

static struct amba_pl011_data uart2_plat = {
#ifdef CONFIG_STE_DMA40_REMOVE
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart2_dma_cfg_rx,
	.dma_tx_param = &uart2_dma_cfg_tx,
#endif
	.reset = u8500_uart2_reset,
	.amba_pl011_wake_peer = NULL,
};

static struct cryp_platform_data u8500_cryp1_platform_data = {
	.mem_to_engine = {
		.dir = STEDMA40_MEM_TO_PERIPH,
		.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
		.dst_dev_type = DB8500_DMA_DEV48_CAC1_TX,
		.src_info.data_width = STEDMA40_WORD_WIDTH,
		.dst_info.data_width = STEDMA40_WORD_WIDTH,
		.mode = STEDMA40_MODE_LOGICAL,
		.src_info.psize = STEDMA40_PSIZE_LOG_4,
		.dst_info.psize = STEDMA40_PSIZE_LOG_4,
	},
	.engine_to_mem = {
		.dir = STEDMA40_PERIPH_TO_MEM,
		.src_dev_type = DB8500_DMA_DEV48_CAC1_RX,
		.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
		.src_info.data_width = STEDMA40_WORD_WIDTH,
		.dst_info.data_width = STEDMA40_WORD_WIDTH,
		.mode = STEDMA40_MODE_LOGICAL,
		.src_info.psize = STEDMA40_PSIZE_LOG_4,
		.dst_info.psize = STEDMA40_PSIZE_LOG_4,
	}
};

static struct stedma40_chan_cfg u8500_hash_dma_cfg_tx = {
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV50_HAC1_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
	.mode = STEDMA40_MODE_LOGICAL,
	.src_info.psize = STEDMA40_PSIZE_LOG_16,
	.dst_info.psize = STEDMA40_PSIZE_LOG_16,
};

static struct hash_platform_data u8500_hash1_platform_data = {
	.mem_to_engine = &u8500_hash_dma_cfg_tx,
	.dma_filter = stedma40_filter,
};

static struct platform_device *platform_devs[] __initdata = {
	&u8500_shrm_device,
#ifdef SSG_CAMERA_ENABLE
	&ux500_mmio_device,
#endif
	&ux500_hwmem_device,
#ifdef CONFIG_SPI_GPIO
	&ux500_spi_gpio_device,
#endif
	&ux500_mcde_device,
	&ux500_b2r2_device,
	&ux500_b2r2_blt_device,
	&ab8500_device,
#ifdef CONFIG_STE_TRACE_MODEM
	&u8500_trace_modem,
#endif
	&db8500_mali_gpu_device,
#ifdef CONFIG_MODEM_U8500
	&u8500_modem_dev,
#endif
	&db8500_cpuidle_device,
#ifdef CONFIG_USB_ANDROID
	&android_usb_device,
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	&usb_mass_storage_device,
#endif
#ifdef CONFIG_USB_ANDROID_ECM
	&usb_ecm_device,
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
	&usb_rndis_device,
#endif
#ifdef CONFIG_USB_ANDROID_PHONET
	&usb_phonet_device,
#endif
#endif
#ifdef CONFIG_DB8500_MLOADER
	&mloader_fw_device,
#endif
#ifdef CONFIG_RFKILL
	&sec_device_rfkill,
#endif

#ifdef CONFIG_LEDS_CLASS
	&gavini_gpio_leds_device,
#endif
};

static struct platform_device *platform_devs_rev8[] __initdata = {
	&u8500_shrm_device,
#ifdef SSG_CAMERA_ENABLE
	&ux500_mmio_device,
#endif
	&ux500_hwmem_device,
#ifdef CONFIG_SPI_GPIO
	&ux500_spi_gpio_device_rev8,
#endif
	&ux500_mcde_device,
	&ux500_b2r2_device,
	&ux500_b2r2_blt_device,
	&ab8500_device,
#ifdef CONFIG_STE_TRACE_MODEM
	&u8500_trace_modem,
#endif
#ifdef CONFIG_MODEM_U8500
	&u8500_modem_dev,
#endif
	&db8500_cpuidle_device,
#ifdef CONFIG_USB_ANDROID
	&android_usb_device,
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	&usb_mass_storage_device,
#endif
#ifdef CONFIG_USB_ANDROID_ECM
	&usb_ecm_device,
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
	&usb_rndis_device,
#endif
#ifdef CONFIG_USB_ANDROID_PHONET
	&usb_phonet_device,
#endif
#endif
#ifdef CONFIG_DB8500_MLOADER
	&mloader_fw_device,
#endif
#ifdef CONFIG_RFKILL
	&sec_device_rfkill,
#endif
#ifdef CONFIG_PROXIMITY_GP2A030
	&opt_gp2a_00C,
#endif

#ifdef CONFIG_LEDS_CLASS
	&gavini_gpio_leds_device,
#endif
};


#if defined(CONFIG_MPU_SENSORS_MPU3050)
static void gavini_mpl_init(void)
{
	int intrpt_gpio = SENSOR_INT_GAVINI_R0_0;

	gpio_request(intrpt_gpio, "MPUIRQ");
	gpio_direction_input(intrpt_gpio);
}
#endif

static void __init gavini_i2c_init(void)
{
	db8500_add_i2c0(&gavini_i2c0_data);
	db8500_add_i2c1(&gavini_i2c1_data);
	db8500_add_i2c2(&gavini_i2c2_data);
	db8500_add_i2c3(&gavini_i2c3_data);

	if (system_rev < GAVINI_R0_0_C) {
#if defined(CONFIG_PROXIMITY_GP2A030)
		platform_device_register(&opt_gp2a);
#endif
		i2c_register_board_info(0,
			ARRAY_AND_SIZE(gavini_r0_0_i2c0_devices));
		i2c_register_board_info(1,
			ARRAY_AND_SIZE(gavini_r0_0_i2c1_devices));
	} else	if (system_rev == GAVINI_R0_0_C){
		i2c_register_board_info(0,
			ARRAY_AND_SIZE(gavini_r0_0_i2c0_devices_rev00c));
		i2c_register_board_info(2,
			ARRAY_AND_SIZE(gavini_r0_0_c_i2c2_devices));
	} else {
		i2c_register_board_info(0,
			ARRAY_AND_SIZE(gavini_r0_0_i2c0_devices_rev00d));
		i2c_register_board_info(2,
			ARRAY_AND_SIZE(gavini_r0_0_c_i2c2_devices));

	}

	i2c_register_board_info(3, ARRAY_AND_SIZE(gavini_r0_0_i2c3_devices));

	platform_device_register(&gavini_gpio_i2c4_pdata);
	i2c_register_board_info(4, ARRAY_AND_SIZE(gavini_r0_0_gpio_i2c4_devices));
	platform_device_register(&gavini_gpio_i2c5_pdata);
	i2c_register_board_info(5, ARRAY_AND_SIZE(gavini_r0_0_gpio_i2c5_devices));
	platform_device_register(&gavini_gpio_i2c6_pdata);
	i2c_register_board_info(6, ARRAY_AND_SIZE(gavini_r0_0_gpio_i2c6_devices));
	platform_device_register(&gavini_gpio_i2c7_pdata);
	i2c_register_board_info(7, ARRAY_AND_SIZE(gavini_r0_0_gpio_i2c7_devices));

	if (system_rev < GAVINI_R0_0_C)	{
		platform_device_register(&gavini_gpio_i2c8_pdata);
		i2c_register_board_info(8,
			ARRAY_AND_SIZE(gavini_r0_0_gpio_i2c8_devices));
	} else {
#if defined(CONFIG_PROXIMITY_GP2A030)
		platform_device_register(&gavini_gpio_i2c9_pdata);
#endif
		i2c_register_board_info(9,
			ARRAY_AND_SIZE(gavini_r0_0_c_gpio_i2c9_devices));
	}

}

#ifdef CONFIG_USB_ANDROID
/*
 * Public Id is a Unique number for each board and is stored
 * in Backup RAM at address 0x80151FC0, ..FC4, FC8, FCC and FD0.
 *
 * This function reads the Public Ids from this address and returns
 * a single string, which can be used as serial number for USB.
 * Input parameter - serial_number should be of 'len' bytes long
*/
static void fetch_usb_serial_no(int len)
{
	u32 buf[5];
	void __iomem *backup_ram = NULL;

	backup_ram = ioremap(PUBLIC_ID_BACKUPRAM1, 0x14);

	if (backup_ram) {
		buf[0] = readl(backup_ram);
		buf[1] = readl(backup_ram + 4);
		buf[2] = readl(backup_ram + 8);
		buf[3] = readl(backup_ram + 0x0c);
		buf[4] = readl(backup_ram + 0x10);

		snprintf(android_usb_pdata.serial_number, len+1, "%X%X%X%X%X",
					buf[0], buf[1], buf[2], buf[3], buf[4]);
		iounmap(backup_ram);
	} else {
		printk(KERN_ERR "$$ ioremap failed\n");
	}
}
#endif


static void __init gavini_spi_init(void)
{
	db8500_add_spi0(&gavini_spi0_data);
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}

static void __init gavini_uart_init(void)
{
	db8500_add_uart0(&uart0_plat);
	db8500_add_uart1(&uart1_plat);
	db8500_add_uart2(&uart2_plat);
}

static void __init u8500_cryp1_hash1_init(void)
{
	db8500_add_cryp1(&u8500_cryp1_platform_data);
	db8500_add_hash1(&u8500_hash1_platform_data);
}

static void __init gavini_projector_init(void)
{
#if 0
	gpio_request(PARKZ_GAVINI_R0_0, "PARKZ");
	gpio_request(MP_ON_GAVINI_R0_0, "MP_ON");
	gpio_request(PRJ_EN_GAVINI_R0_0, "PRJ_EN");
	gpio_request(MPR_SCL_GAVINI_R0_0, "MPR_SCL");
	gpio_request(MPR_SDA_GAVINI_R0_0, "MPR_SDA");
	gpio_request(PROJECTOR_INTZ_GAVINI_R0_0, "PROJECTOR_INTZ");
#endif

	if (system_rev < GAVINI_R0_0_B) {
		gpio_request(MOTDRV_IN1_GAVINI_R0_0, "MOTDRV_IN1");
		gpio_request(MOTDRV_IN2_GAVINI_R0_0, "MOTDRV_IN2");
		gpio_request(MOTDRV_IN3_GAVINI_R0_0, "MOTDRV_IN3");
		gpio_request(MOTDRV_IN4_GAVINI_R0_0, "MOTDRV_IN4");
	} else {
		gpio_request(MOTDRV_IN1_GAVINI_R0_0_B, "MOTDRV_IN1");
		gpio_request(MOTDRV_IN2_GAVINI_R0_0_B, "MOTDRV_IN2");
		gpio_request(MOTDRV_IN3_GAVINI_R0_0_B, "MOTDRV_IN3");
		gpio_request(MOTDRV_IN4_GAVINI_R0_0_B, "MOTDRV_IN4");
	}
}



void godin_cam_init(void);

static void __init gavini_init_machine(void)
{
	sec_common_init() ;

	sec_common_init_early() ;

#ifdef CONFIG_ANDROID_RAM_CONSOLE
	if (ram_console_device.num_resources == 1)
		platform_device_register(&ram_console_device);
#endif

	platform_device_register(&db8500_prcmu_device);
	platform_device_register(&u8500_usecase_gov_device);

	u8500_init_devices();

#ifdef CONFIG_USB_ANDROID
	fetch_usb_serial_no(USB_SERIAL_NUMBER_LEN);
#endif

	if (system_rev >= GAVINI_R0_0_A)
		platform_add_devices(platform_devs_rev8,
						ARRAY_SIZE(platform_devs_rev8));
	else
		platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	#if defined(CONFIG_BACKLIGHT_KTD259)
	if (system_rev >= GAVINI_R0_3)
		platform_device_register(&gavini_backlight_device_r0_3);
	else if (system_rev >= GAVINI_R0_0_C)
		platform_device_register(&gavini_backlight_device);
	#endif

	ssg_pins_init();

	u8500_cryp1_hash1_init();

/* TODO: This needs to be customised for Samsung Camera code
	mmio_config.xenon_charge.gpio = EGPIO_PIN_5; */


	gavini_i2c_init();
	gavini_spi_init();
	mop500_msp_init();		/* generic for now */
	gavini_uart_init();
	gavini_projector_init();

#if defined(CONFIG_MPU_SENSORS_MPU3050)
	gavini_mpl_init();
#endif

#ifdef CONFIG_KEYBOARD_GPIO
	platform_device_register(&gavini_gpio_keys_device);
#endif

	sec_cam_init();

	platform_add_devices(gavini_platform_devices,
			     ARRAY_SIZE(gavini_platform_devices));

	sec_common_init_post() ;

	mms136_ts_init();

}

static int __init  jig_smd_status(char *str)
{
	if (get_option(&str, &jig_smd) != 1)
		jig_smd = 0;

	return 1;

}
__setup("jig_smd=",jig_smd_status);

static int __init sec_debug_setup(char *str)
{
	if (get_option(&str, &sec_debug_settings) != 1)
		sec_debug_settings = 0;

	return 1;
}
__setup("debug=",sec_debug_setup);

/* we have equally similar boards with very minimal
 * changes, so we detect the platform during boot
 */
static int __init board_id_setup(char *str)
{
	if (get_option(&str, &board_id) != 1)
		board_id = 0;

	switch (board_id) {
	case 7:
		printk(KERN_INFO "GAVINI Board Rev 0.0\n");
		system_rev = GAVINI_R0_0;
		break;
	case 8:
		printk(KERN_INFO "GAVINI Board Rev 0.0A\n");
		system_rev = GAVINI_R0_0_A;
		break;
	case 9:
		printk(KERN_INFO "GAVINI Board Rev 0.0B\n");
		system_rev = GAVINI_R0_0_B;
		break;
	case 10:
		printk(KERN_INFO "GAVINI Board Rev 0.0A_EMUL\n");
		system_rev = GAVINI_R0_0_A_EMUL;
		break;
	case 11:
		printk(KERN_INFO "GAVINI Board Rev 0.0C\n");
		system_rev = GAVINI_R0_0_C;
		break;
	case 12:
		printk(KERN_INFO "GAVINI Board Rev 0.0D\n");
		system_rev = GAVINI_R0_0_D;
		break;
	case 13:
		printk(KERN_INFO "GAVINI Board Rev 0.1\n");
		system_rev = GAVINI_R0_1;
		break;
	case 14:
		printk(KERN_INFO "GAVINI Board Rev 0.3\n");
		system_rev = GAVINI_R0_3;
		break;

	default:
		printk(KERN_INFO "Unknown board_id=%c\n", *str);
		break;
	};

	return 1;
}
__setup("board_id=", board_id_setup);

MACHINE_START(GAVINI, "SAMSUNG GAVINI")
	/* Maintainer: SAMSUNG based on ST Ericsson */
	.boot_params	= 0x00000100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= gavini_init_machine,
MACHINE_END
