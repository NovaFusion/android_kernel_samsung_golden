/*
 * Copyright (C) 2008-2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/amba/serial.h>
#include <linux/spi/spi.h>
#include <linux/hsi/hsi.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/regulator/ab8500.h>
#include <linux/mfd/tc3589x.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/regulator/fixed.h>
#include <linux/leds-lp5521.h>
#include <linux/input.h>
#include <linux/smsc911x.h>
#include <linux/gpio_keys.h>
#include <linux/delay.h>
#include <linux/mfd/abx500/ab8500-denc.h>
#include <linux/spi/stm_msp.h>
#include <linux/leds_pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/leds.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>
#include <linux/input/abx500-accdet.h>
#include <linux/input/ab8505_micro_usb_iddet.h>
#include <linux/cpuidle-dbx500.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/gpio-nomadik.h>
#include <plat/i2c.h>
#include <plat/ste_dma40.h>
#include <plat/pincfg.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/irqs.h>
#include <mach/ste-dma40-db8500.h>
#ifdef CONFIG_U8500_SIM_DETECT
#include <mach/sim_detect.h>
#endif
#include <mach/crypto-ux500.h>
#include <mach/pm.h>
#include <mach/reboot_reasons.h>

#include <video/av8100.h>

#ifdef CONFIG_KEYBOARD_NOMADIK_SKE
#include <plat/ske.h>
#endif

#include "pins.h"
#include "cpu-db8500.h"
#include "pins-db8500.h"
#include "devices-db8500.h"
#include "board-mop500.h"
#include "board-mop500-regulators.h"
#include "board-mop500-bm.h"
#include "board-mop500-wlan.h"

#define PRCM_DEBUG_NOPWRDOWN_VAL	0x194
#define ARM_DEBUG_NOPOWER_DOWN_REQ	1

#ifdef CONFIG_AB8500_DENC
static struct ab8500_denc_platform_data ab8500_denc_pdata = {
	.ddr_enable = true,
	.ddr_little_endian = false,
};
#endif

static struct gpio_led snowball_led_array[] = {
	{
		.name = "user_led",
		.default_trigger = "none",
		.gpio = 142,
	},
};

static struct gpio_led_platform_data snowball_led_data = {
	.leds = snowball_led_array,
	.num_leds = ARRAY_SIZE(snowball_led_array),
};

static struct platform_device snowball_led_dev = {
	.name = "leds-gpio",
	.dev = {
		.platform_data = &snowball_led_data,
	},
};

static struct ab8500_gpio_platform_data ab8500_gpio_pdata = {
	.gpio_base		= AB8500_PIN_GPIO(1),
	.irq_base		= MOP500_AB8500_VIR_GPIO_IRQ_BASE,
	/*
	 * config_reg is the initial configuration of ab8500 pins.
	 * The pins can be configured as GPIO or alt functions based
	 * on value present in GpioSel1 to GpioSel6 and AlternatFunction
	 * register. This is the array of 7 configuration settings.
	 * One has to compile time decide these settings. Below is the
	 * explanation of these setting
	 * GpioSel1 = 0x0F => Pin GPIO1 (SysClkReq2)
	 *                    Pin GPIO2 (SysClkReq3)
	 *                    Pin GPIO3 (SysClkReq4)
	 *                    Pin GPIO4 (SysClkReq6) are configured as GPIO
	 * GpioSel2 = 0x9E => Pins GPIO10 to GPIO13 are configured as GPIO
	 * GpioSel3 = 0x80 => Pin GPIO24 (SysClkReq7) is configured as GPIO
	 * GpioSel4 = 0x01 => Pin GPIO25 (SysClkReq8) is configured as GPIO
	 * GpioSel5 = 0x78 => Pin GPIO36 (ApeSpiClk)
	 *		      Pin GPIO37 (ApeSpiCSn)
	 *		      Pin GPIO38 (ApeSpiDout)
	 *		      Pin GPIO39 (ApeSpiDin) are configured as GPIO
	 * GpioSel6 = 0x02 => Pin GPIO42 (SysClkReq5) is configured as GPIO
	 * AlternaFunction = 0x00 => If Pins GPIO10 to 13 are not configured
	 * as GPIO then this register selectes the alternate functions
	 */
	.config_reg     = {0x0F, 0x9E, 0x80, 0x01, 0x78, 0x02, 0x00},

	/*
	 * config_direction allows for the initial GPIO direction to
	 * be set. For Snowball we set GPIO26 to output.
	 */
	.config_direction  = {0x00, 0x00, 0x00, 0x02, 0x00, 0x00},

	/*
	 * config_pullups allows for the intial configuration of the
	 * GPIO pullup/pulldown configuration.
	 */
	.config_pullups    = {0xE0, 0x01, 0x00, 0x00, 0x00, 0x00},
};

static struct ab8500_gpio_platform_data ab8505_gpio_pdata = {
	.gpio_base		= AB8500_PIN_GPIO(1),
	.irq_base		= MOP500_AB8500_VIR_GPIO_IRQ_BASE,
	/*
	 * config_reg is the initial configuration of ab8500 pins.
	 * The pins can be configured as GPIO or alt functions based
	 * on value present in GpioSel1 to GpioSel6 and AlternatFunction
	 * register. This is the array of 7 configuration settings.
	 * One has to compile time decide these settings. Below is the
	 * explanation of these setting
	 * GpioSel1 = 0x0F => Pin GPIO1 (SysClkReq2)
	 *                    Pin GPIO2 (SysClkReq3)
	 *                    Pin GPIO3 (SysClkReq4)
	 *                    Pin GPIO4 (SysClkReq6) are configured as GPIO
	 * GpioSel2 = 0x9E => Pins GPIO10 to GPIO13 are configured as GPIO
	 * GpioSel3 = 0x80 => Pin GPIO24 (SysClkReq7) is configured as GPIO
	 * GpioSel4 = 0x01 => Pin GPIO25 (SysClkReq8) is configured as GPIO
	 * GpioSel5 = 0x78 => Pin GPIO36 (ApeSpiClk)
	 *		      Pin GPIO37 (ApeSpiCSn)
	 *		      Pin GPIO38 (ApeSpiDout)
	 *		      Pin GPIO39 (ApeSpiDin) are configured as GPIO
	 * GpioSel6 = 0x02 => Pin GPIO42 (SysClkReq5) is configured as GPIO
	 * AlternaFunction = 0x00 => If Pins GPIO10 to 13 are not configured
	 * as GPIO then this register selectes the alternate functions
	 * GpioSel7 = 0x22 => Pins GPIO50 to GPIO52 are configured as GPIO.
	 */
	.config_reg     = {0x0F, 0x9E, 0x80, 0x01, 0x7A, 0x02, 0x22},

	/*
	 * config_direction allows for the initial GPIO direction to
	 * be set. For Snowball we set GPIO26 to output.
	 */
	.config_direction  = {0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},

	/*
	 * config_pullups allows for the intial configuration of the
	 * GPIO pullup/pulldown configuration.
	 * GPIO13(GpioPud2) = 1 and GPIO50(GpioPud7) = 1.
	 */
	.config_pullups    = {0xE0, 0x11, 0x00, 0x00, 0x00, 0x00, 0x06},
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

#ifdef CONFIG_INPUT_AB8500_ACCDET
static struct abx500_accdet_platform_data ab8500_accdet_pdata = {
	.btn_keycode = KEY_MEDIA,
	.accdet1_dbth = ACCDET1_TH_1200mV | ACCDET1_DB_70ms,
	.accdet2122_th = ACCDET21_TH_1000mV | ACCDET22_TH_1000mV,
	.video_ctrl_gpio = AB8500_PIN_GPIO(35),
};
#endif

static struct gpio_keys_button snowball_key_array[] = {
	{
		.gpio		= 32,
		.type		= EV_KEY,
		.code		= KEY_1,
		.desc		= "userpb",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup		= 1,
	},
	{
		.gpio		= 151,
		.type		= EV_KEY,
		.code		= KEY_2,
		.desc		= "extkb1",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup		= 1,
	},
	{
		.gpio		= 152,
		.type		= EV_KEY,
		.code		= KEY_3,
		.desc		= "extkb2",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup		= 1,
	},
	{
		.gpio		= 161,
		.type		= EV_KEY,
		.code		= KEY_4,
		.desc		= "extkb3",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup		= 1,
	},
	{
		.gpio		= 162,
		.type		= EV_KEY,
		.code		= KEY_5,
		.desc		= "extkb4",
		.active_low     = 1,
		.debounce_interval = 50,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data snowball_key_data = {
	.buttons	= snowball_key_array,
	.nbuttons       = ARRAY_SIZE(snowball_key_array),
};

static struct platform_device snowball_key_dev = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data  = &snowball_key_data,
		.pwr_domain	= &ux500_dev_power_domain,
	}
};

static struct smsc911x_platform_config snowball_sbnet_cfg = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
	.irq_type = SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags = SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.shift = 1,
};

static struct resource sbnet_res[] = {
	{
		.name = "smsc911x-memory",
		.start = (0x5000 << 16),
		.end  =  (0x5000 << 16) + 0xffff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(140),
		.end = NOMADIK_GPIO_TO_IRQ(140),
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device snowball_sbnet_dev = {
	.name		= "smsc911x",
	.num_resources  = ARRAY_SIZE(sbnet_res),
	.resource       = sbnet_res,
	.dev		= {
		.platform_data = &snowball_sbnet_cfg,
	},
};

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

static struct ab8500_platform_data ab8500_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
	.regulator	= &ab8500_regulator_plat_data,
#ifdef CONFIG_AB8500_DENC
	.denc		= &ab8500_denc_pdata,
#endif
	.battery	= &ab8500_bm_data,
	.charger	= &ab8500_charger_plat_data,
	.btemp		= &ab8500_btemp_plat_data,
	.fg		= &ab8500_fg_plat_data,
	.chargalg	= &ab8500_chargalg_plat_data,
	.gpio		= &ab8500_gpio_pdata,
	.sysctrl	= &ab8500_sysctrl_pdata,
	.pwmled		= &ab8500_pwmled_plat_data,
#ifdef CONFIG_INPUT_AB8500_ACCDET
	.accdet = &ab8500_accdet_pdata,
#endif
#ifdef CONFIG_PM
	.pm_power_off = true,
#endif
	.thermal_time_out = 20, /* seconds */
#ifdef CONFIG_INPUT_AB8505_MICRO_USB_DETECT
	.iddet = &iddet_adc_val_list,
#endif
};

static struct resource ab8500_resources[] = {
	[0] = {
		.start	= IRQ_DB8500_AB8500,
		.end	= IRQ_DB8500_AB8500,
		.flags	= IORESOURCE_IRQ
	}
};

struct platform_device ab8500_device = {
	.name = "ab8500-i2c",
	.id = 0,
	.dev = {
		.platform_data = &ab8500_platdata,
	},
	.num_resources = 1,
	.resource = ab8500_resources,
};

struct platform_device ab8505_device = {
	.name = "ab8505-i2c",
	.id = 0,
	.dev = {
		.platform_data = &ab8500_platdata,
	},
	.num_resources = 1,
	.resource = ab8500_resources,
};

#ifdef CONFIG_KEYBOARD_NOMADIK_SKE

/*
 * Nomadik SKE keypad
 */
#define ROW_PIN_I0      164
#define ROW_PIN_I1      163
#define ROW_PIN_I2      162
#define ROW_PIN_I3      161
#define ROW_PIN_I4      156
#define ROW_PIN_I5      155
#define ROW_PIN_I6      154
#define ROW_PIN_I7      153
#define COL_PIN_O0      168
#define COL_PIN_O1      167
#define COL_PIN_O2      166
#define COL_PIN_O3      165
#define COL_PIN_O4      160
#define COL_PIN_O5      159
#define COL_PIN_O6      158
#define COL_PIN_O7      157

static int ske_kp_rows[] = {
	ROW_PIN_I0, ROW_PIN_I1, ROW_PIN_I2, ROW_PIN_I3,
	ROW_PIN_I4, ROW_PIN_I5, ROW_PIN_I6, ROW_PIN_I7,
};
static int ske_kp_cols[] = {
	COL_PIN_O0, COL_PIN_O1, COL_PIN_O2, COL_PIN_O3,
	COL_PIN_O4, COL_PIN_O5, COL_PIN_O6, COL_PIN_O7,
};

/*
 * ske_set_gpio_row: request and set gpio rows
 */
static int ske_set_gpio_row(int gpio)
{
	int ret;
	ret = gpio_request(gpio, "ske-kp");
	if (ret < 0) {
		pr_err("ske_set_gpio_row: gpio request failed\n");
		return ret;
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret < 0) {
		pr_err("ske_set_gpio_row: gpio direction failed\n");
		gpio_free(gpio);
	}

	return ret;
}

/*
 * ske_kp_init - enable the gpio configuration
 */
static int ske_kp_init(void)
{
	struct ux500_pins *pins;
	int ret, i;

	pins = ux500_pins_get("ske");
	if (pins)
		ux500_pins_enable(pins);

	for (i = 0; i < SKE_KPD_MAX_ROWS; i++) {
		ret = ske_set_gpio_row(ske_kp_rows[i]);
		if (ret < 0) {
			pr_err("ske_kp_init: failed init\n");
			return ret;
		}
	}

	return 0;
}

static int ske_kp_exit(void)
{
	struct ux500_pins *pins;
	int i;

	pins = ux500_pins_get("ske");
	if (pins)
		ux500_pins_disable(pins);

	for (i = 0; i < SKE_KPD_MAX_ROWS; i++)
		gpio_free(ske_kp_rows[i]);

	return 0;
}

static const unsigned int mop500_ske_keymap[] = {
#if defined(CONFIG_KEYLAYOUT_LAYOUT1)
	KEY(2, 5, KEY_END),
	KEY(4, 1, KEY_HOME),
	KEY(3, 5, KEY_VOLUMEDOWN),
	KEY(1, 3, KEY_EMAIL),
	KEY(5, 2, KEY_RIGHT),
	KEY(5, 0, KEY_BACKSPACE),

	KEY(0, 5, KEY_MENU),
	KEY(7, 6, KEY_ENTER),
	KEY(4, 5, KEY_0),
	KEY(6, 7, KEY_DOT),
	KEY(3, 4, KEY_UP),
	KEY(3, 3, KEY_DOWN),

	KEY(6, 4, KEY_SEND),
	KEY(6, 2, KEY_BACK),
	KEY(4, 2, KEY_VOLUMEUP),
	KEY(5, 5, KEY_SPACE),
	KEY(4, 3, KEY_LEFT),
	KEY(3, 2, KEY_SEARCH),
#elif defined(CONFIG_KEYLAYOUT_LAYOUT2)
	KEY(2, 5, KEY_RIGHT),
	KEY(4, 1, KEY_ENTER),
	KEY(3, 5, KEY_MENU),
	KEY(1, 3, KEY_3),
	KEY(5, 2, KEY_6),
	KEY(5, 0, KEY_9),

	KEY(0, 5, KEY_UP),
	KEY(7, 6, KEY_DOWN),
	KEY(4, 5, KEY_0),
	KEY(6, 7, KEY_2),
	KEY(3, 4, KEY_5),
	KEY(3, 3, KEY_8),

	KEY(6, 4, KEY_LEFT),
	KEY(6, 2, KEY_BACK),
	KEY(4, 2, KEY_KPDOT),
	KEY(5, 5, KEY_1),
	KEY(4, 3, KEY_4),
	KEY(3, 2, KEY_7),
#else
#warning "No keypad layout defined."
#endif
};

static struct matrix_keymap_data mop500_ske_keymap_data = {
	.keymap		= mop500_ske_keymap,
	.keymap_size    = ARRAY_SIZE(mop500_ske_keymap),
};



static struct ske_keypad_platform_data mop500_ske_keypad_data = {
	.init		= ske_kp_init,
	.exit		= ske_kp_exit,
	.gpio_input_pins = ske_kp_rows,
	.gpio_output_pins = ske_kp_cols,
	.keymap_data    = &mop500_ske_keymap_data,
	.no_autorepeat  = true,
	.krow		= SKE_KPD_MAX_ROWS,     /* 8x8 matrix */
	.kcol		= SKE_KPD_MAX_COLS,
	.debounce_ms    = 20,			/* in timeout period */
	.switch_delay	= 200,			/* in jiffies */
};

#endif

static int av8100_plat_init(void)
{
	struct ux500_pins *pins;
	int res;

	pins = ux500_pins_get("av8100-hdmi");
	if (!pins) {
		res = -EINVAL;
		goto failed;
	}

	res = ux500_pins_enable(pins);
	if (res != 0)
		goto failed;

	return res;

failed:
	pr_err("%s failed\n", __func__);
	return res;
}

static int av8100_plat_exit(void)
{
	struct ux500_pins *pins;
	int res;

	pins = ux500_pins_get("av8100-hdmi");
	if (!pins) {
		res = -EINVAL;
		goto failed;
	}
	res = ux500_pins_disable(pins);
	if (res != 0)
		goto failed;
	return res;

failed:
	pr_err("%s failed\n", __func__);
	return res;
}

/*
 * TC35892
 */

static void mop500_tc35892_init(struct tc3589x *tc3589x, unsigned int base)
{
	mop500_sdi_tc35892_init();
}

static struct tc3589x_gpio_platform_data mop500_tc35892_gpio_data = {
	.gpio_base	= MOP500_EGPIO(0),
	.setup		= mop500_tc35892_init,
};

static struct tc3589x_platform_data mop500_tc35892_data = {
	.block		= TC3589x_BLOCK_GPIO,
	.gpio		= &mop500_tc35892_gpio_data,
	.irq_base	= MOP500_EGPIO_IRQ_BASE,
};

static struct lp5521_led_config lp5521_pri_led[] = {
	[0] = {
		.chan_nr = 0,
		.led_current = 0x2f,
		.max_current = 0x5f,
	},
	[1] = {
		.chan_nr = 1,
		.led_current = 0x2f,
		.max_current = 0x5f,
	},
	[2] = {
		.chan_nr = 2,
		.led_current = 0x2f,
		.max_current = 0x5f,
	},
};

static struct av8100_platform_data av8100_plat_data = {
	.init			= av8100_plat_init,
	.exit			= av8100_plat_exit,
	.irq			= NOMADIK_GPIO_TO_IRQ(192),
	.reset			= MOP500_HDMI_RST_GPIO,
	.inputclk_id		= "sysclk2",
	.regulator_pwr_id	= "hdmi_1v8",
	.alt_powerupseq		= false,
	.mclk_freq		= 3, /* MCLK_RNG_31_38 */
};

static struct lp5521_platform_data __initdata lp5521_pri_data = {
	.label		= "lp5521_pri",
	.led_config	= &lp5521_pri_led[0],
	.num_channels	= 3,
	.clock_mode	= LP5521_CLOCK_EXT,
};

static struct lp5521_led_config lp5521_sec_led[] = {
	[0] = {
		.chan_nr = 0,
		.led_current = 0x2f,
		.max_current = 0x5f,
	},
	[1] = {
		.chan_nr = 1,
		.led_current = 0x2f,
		.max_current = 0x5f,
	},
	[2] = {
		.chan_nr = 2,
		.led_current = 0x2f,
		.max_current = 0x5f,
	},
};

static struct lp5521_platform_data __initdata lp5521_sec_data = {
	.label		= "lp5521_sec",
	.led_config	= &lp5521_sec_led[0],
	.num_channels	= 3,
	.clock_mode	= LP5521_CLOCK_EXT,
};

static struct i2c_board_info __initdata mop500_i2c0_devices[] = {
	{
		I2C_BOARD_INFO("tc3589x", 0x42),
		.irq		= NOMADIK_GPIO_TO_IRQ(217),
		.platform_data  = &mop500_tc35892_data,
	},
	{
		I2C_BOARD_INFO("av8100", 0x70),
		.platform_data = &av8100_plat_data,
	},
	/* I2C0 devices only available prior to HREFv60 */
};

#define NUM_PRE_V60_I2C0_DEVICES 1

static struct i2c_board_info __initdata mop500_i2c2_devices[] = {
	{
		/* lp5521 LED driver, 1st device */
		I2C_BOARD_INFO("lp5521", 0x33),
		.platform_data = &lp5521_pri_data,
	},
	{
		/* lp5521 LED driver, 2st device */
		I2C_BOARD_INFO("lp5521", 0x34),
		.platform_data = &lp5521_sec_data,
	},
	{
		/* Light sensor Rohm BH1780GLI */
		I2C_BOARD_INFO("bh1780", 0x29),
	},
};

#define U8500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, t_out, _sm)	\
static struct nmk_i2c_controller u8500_i2c##id##_data = { \
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
U8500_I2C_CONTROLLER(2,	0xe, 1, 8, 400000, 200, I2C_FREQ_MODE_FAST);
U8500_I2C_CONTROLLER(3,	0xe, 1, 8, 400000, 200, I2C_FREQ_MODE_FAST);

static void __init mop500_i2c_init(void)
{
	db8500_add_i2c0(&u8500_i2c0_data);
	db8500_add_i2c1(&u8500_i2c1_data);
	db8500_add_i2c2(&u8500_i2c2_data);
	db8500_add_i2c3(&u8500_i2c3_data);
}

#ifdef CONFIG_LEDS_PWM
static struct led_pwm pwm_leds_data[] = {
	[0] = {
		.name = "lcd-backlight",
		.pwm_id = 1,
		.max_brightness = 255,
		.lth_brightness = 90,
		.pwm_period_ns = 1023,
		.dutycycle_steps = 16,
		.period_steps = 4,
	},
	[1] = {
		.name = "sec-lcd-backlight",
		.pwm_id = 2,
		.max_brightness = 255,
		.lth_brightness = 90,
		.pwm_period_ns = 1023,
	},
};

static struct led_pwm_platform_data u8500_leds_data = {
	.num_leds = 1,
	.leds = pwm_leds_data,
};

static struct platform_device ux500_leds_device = {
	.name = "leds_pwm",
	.dev = {
		.platform_data = &u8500_leds_data,
	},
};
#endif

#ifdef CONFIG_BACKLIGHT_PWM
static struct platform_pwm_backlight_data u8500_backlight_data[] = {
	[0] = {
	.pwm_id = 1,
	.max_brightness = 255,
	.dft_brightness = 200,
	.lth_brightness = 90,
	.pwm_period_ns = 1023,
	},
	[1] = {
	.pwm_id = 2,
	.max_brightness = 255,
	.dft_brightness = 200,
	.lth_brightness = 90,
	.pwm_period_ns = 1023,
	},
};

static struct platform_device ux500_backlight_device[] = {
	[0] = {
		.name = "pwm-backlight",
		.id = 0,
		.dev = {
			.platform_data = &u8500_backlight_data[0],
		},
	},
	[1] = {
		.name = "pwm-backlight",
		.id = 1,
		.dev = {
			.platform_data = &u8500_backlight_data[1],
		},
	},
};
#endif

/* Force feedback vibrator device */
static struct platform_device ste_ff_vibra_device = {
	.name = "ste_ff_vibra"
};

#ifdef CONFIG_HSI
static struct hsi_board_info __initdata u8500_hsi_devices[] = {
	{
		.name = "hsi_char",
		.hsi_id = 0,
		.port = 0,
		.tx_cfg = {
			.mode = HSI_MODE_FRAME,
			.channels = 1,
			.speed = 200000,
			.ch_prio = {},
			{.arb_mode = HSI_ARB_RR},
		},
		.rx_cfg = {
			.mode = HSI_MODE_FRAME,
			.channels = 1,
			.speed = 200000,
			.ch_prio = {},
			{.flow = HSI_FLOW_SYNC},
		},
	},
	{
		.name = "hsi_test",
		.hsi_id = 0,
		.port = 0,
		.tx_cfg = {
			.mode = HSI_MODE_FRAME,
			.channels = 2,
			.speed = 100000,
			.ch_prio = {},
			{.arb_mode = HSI_ARB_RR},
		},
		.rx_cfg = {
			.mode = HSI_MODE_FRAME,
			.channels = 2,
			.speed = 200000,
			.ch_prio = {},
			{.flow = HSI_FLOW_SYNC},
		},
	},
	{
		.name = "cfhsi_v3_driver",
		.hsi_id = 0,
		.port = 0,
		.tx_cfg = {
			.mode = HSI_MODE_STREAM,
			.channels = 2,
			.speed = 20000,
			.ch_prio = {},
			{.arb_mode = HSI_ARB_RR},
		},
		.rx_cfg = {
			.mode = HSI_MODE_STREAM,
			.channels = 2,
			.speed = 200000,
			.ch_prio = {},
			{.flow = HSI_FLOW_SYNC},
		},
	},
};
#endif

#ifdef CONFIG_U8500_SIM_DETECT
static struct sim_detect_platform_data sim_detect_pdata = {
	.irq_num		= MOP500_AB8500_VIR_GPIO_IRQ(6),
};
struct platform_device u8500_sim_detect_device = {
	.name	= "sim-detect",
	.id	= 0,
	.dev	= {
			.platform_data          = &sim_detect_pdata,
	},
};
#endif

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

/* add any platform devices here - TODO */
static struct platform_device *mop500_platform_devs[] __initdata = {
#ifdef CONFIG_U8500_SIM_DETECT
	&u8500_sim_detect_device,
#endif
	&ste_ff_vibra_device,
#ifdef CONFIG_U8500_MMIO
	&ux500_mmio_device,
#endif
	&ux500_hwmem_device,
	&ux500_mcde_device,
#ifdef CONFIG_MCDE_DISPLAY_DSI
	&u8500_dsilink_device[0],
	&u8500_dsilink_device[1],
	&u8500_dsilink_device[2],
#endif
	&ux500_b2r2_device,
	&ux500_b2r2_blt_device,
#ifdef CONFIG_LEDS_PWM
	&ux500_leds_device,
#endif
#ifdef CONFIG_BACKLIGHT_PWM
	&ux500_backlight_device[0],
	&ux500_backlight_device[1],
#endif
#ifdef CONFIG_DB8500_MLOADER
	&mloader_fw_device,
#endif
#ifdef CONFIG_HSI
	&u8500_hsi_device,
#endif
};

/*
 * SSP
 */

#define NUM_SSP_CLIENTS 10

#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg ssp0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV8_SSP0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg ssp0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV8_SSP0_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};
#endif

static struct pl022_ssp_controller ssp0_platform_data = {
	.bus_id = 4,
#ifdef CONFIG_STE_DMA40
	.enable_dma = 1,
	.dma_filter = stedma40_filter,
	.dma_rx_param = &ssp0_dma_cfg_rx,
	.dma_tx_param = &ssp0_dma_cfg_tx,
#endif
	/* on this platform, gpio 31,142,144,214 &
	 * 224 are connected as chip selects
	 */
	.num_chipselect = NUM_SSP_CLIENTS,
};

static void __init mop500_spi_init(void)
{
	db8500_add_ssp0(&ssp0_platform_data);
}

#ifdef CONFIG_STE_DMA40_REMOVE
static struct stedma40_chan_cfg uart0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV13_UART0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV13_UART0_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart1_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV12_UART1_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart1_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV12_UART1_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart2_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV11_UART2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};

static struct stedma40_chan_cfg uart2_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV11_UART2_TX,
	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
};
#endif

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

static struct amba_pl011_data uart0_plat = {
#ifdef CONFIG_STE_DMA40_REMOVE
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart0_dma_cfg_rx,
	.dma_tx_param = &uart0_dma_cfg_tx,
#endif
	.init = ux500_uart0_init,
	.exit = ux500_uart0_exit,
	.reset = u8500_uart0_reset,
};

static struct amba_pl011_data uart1_plat = {
#ifdef CONFIG_STE_DMA40_REMOVE
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart1_dma_cfg_rx,
	.dma_tx_param = &uart1_dma_cfg_tx,
#endif
	.reset = u8500_uart1_reset,
};

static struct amba_pl011_data uart2_plat = {
#ifdef CONFIG_STE_DMA40_REMOVE
	.dma_filter = stedma40_filter,
	.dma_rx_param = &uart2_dma_cfg_rx,
	.dma_tx_param = &uart2_dma_cfg_tx,
#endif
	.reset = u8500_uart2_reset,
};

static void __init mop500_uart_init(void)
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

static struct platform_device *snowball_platform_devs[] __initdata = {
	&ux500_hwmem_device,
	&snowball_led_dev,
	&snowball_key_dev,
	&snowball_sbnet_dev,
	&ux500_mcde_device,
	&u8500_dsilink_device[0],
	&u8500_dsilink_device[1],
	&u8500_dsilink_device[2],
	&ux500_b2r2_device,
	&ux500_b2r2_blt_device,
};

/*
 *  On boards hrefpv60 and later, the accessory insertion/removal,
 *  button press/release are inverted.
*/
static void accessory_detect_config(void)
{
#ifdef CONFIG_INPUT_AB8500_ACCDET
	if (machine_is_hrefv60() || machine_is_u8520() || machine_is_u9540())
		ab8500_accdet_pdata.is_detection_inverted = true;
	else
		ab8500_accdet_pdata.is_detection_inverted = false;

	if (machine_is_u8520()) {
		ab8500_accdet_pdata.video_ctrl_gpio = AB8500_PIN_GPIO(10);
		ab8500_accdet_pdata.mic_ctrl = AB8500_PIN_GPIO(34);
	}
#endif
}

static void fixup_ab8505_gpio(void)
{
	ab8500_gpio_pdata = ab8505_gpio_pdata;
}

static void __init mop500_init_machine(void)
{
	int i2c0_devs;

	accessory_detect_config();

	u8500_init_devices();
	platform_device_register(&db8500_prcmu_device);
	platform_device_register(&u8500_usecase_gov_device);

	mop500_pins_init();

	mop500_regulator_init();

	u8500_cryp1_hash1_init();

#ifdef CONFIG_HSI
	hsi_register_board_info(u8500_hsi_devices,
				ARRAY_SIZE(u8500_hsi_devices));
#endif
#ifdef CONFIG_LEDS_PWM
	if (uib_is_stuib())
		u8500_leds_data.num_leds = 2;
#endif
	if (machine_is_snowball()) {
		platform_add_devices(snowball_platform_devs,
					ARRAY_SIZE(snowball_platform_devs));
	} else {
		platform_add_devices(mop500_platform_devs,
					ARRAY_SIZE(mop500_platform_devs));
	}

	if (!cpu_is_u9500()) {
		platform_device_register(&u8500_shrm_device);
#ifdef CONFIG_STE_TRACE_MODEM
		platform_device_register(&u8500_trace_modem);
#endif
#ifdef CONFIG_MODEM_U8500
		platform_device_register(&u8500_modem_dev);
#endif
		platform_device_register(&db8500_cpuidle_device);
	} else
		platform_device_register(&db9500_cpuidle_device);

	mop500_i2c_init();
	mop500_sdi_init();
	mop500_msp_init();
	mop500_spi_init();
	mop500_uart_init();
	mop500_wlan_init();

#ifdef CONFIG_KEYBOARD_NOMADIK_SKE
	/*
	 * If a hw debugger is detected, do not load the ske driver
	 * since the gpio usage collides.
	 */
	if (!(prcmu_read(PRCM_DEBUG_NOPWRDOWN_VAL) &
	      ARM_DEBUG_NOPOWER_DOWN_REQ))
		db8500_add_ske_keypad(&mop500_ske_keypad_data);
#endif

#ifdef CONFIG_ANDROID_STE_TIMED_VIBRA
	mop500_vibra_init();
#endif
	if (machine_is_u8520()) {
		fixup_ab8505_gpio();
		ab8500_platdata.regulator = &ab8505_regulator_plat_data;
		platform_device_register(&ab8505_device);
	}
	else
		platform_device_register(&ab8500_device);

	i2c0_devs = ARRAY_SIZE(mop500_i2c0_devices);
	i2c_register_board_info(0, mop500_i2c0_devices, i2c0_devs);
	i2c_register_board_info(2, mop500_i2c2_devices,
				ARRAY_SIZE(mop500_i2c2_devices));

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
	.restart	= ab8500_restart,
MACHINE_END

/*
 * NOTE! 8520 machine reports as a HREFV60 until user space updates has been
 * done for 8520.
 */
MACHINE_START(U8520, "ST-Ericsson U8500 Platform HREFv60+")
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
	.restart	= ab8500_restart,
MACHINE_END

MACHINE_START(HREFV60, "ST-Ericsson U8500 Platform HREFv60+")
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
	.restart	= ab8500_restart,
MACHINE_END

MACHINE_START(SNOWBALL, "ST-Ericsson Snowball platform")
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.init_machine	= mop500_init_machine,
	.restart	= ux500_restart,
MACHINE_END

