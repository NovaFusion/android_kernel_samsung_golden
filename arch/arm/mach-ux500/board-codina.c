/*
 * Copyright (C) 2009 ST-Ericsson SA
 * Copyright (C) 2011 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
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
#include <linux/spi/spi_gpio.h>
#include <linux/i2c-gpio.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/regulator/ab8500.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/input.h>
#ifdef CONFIG_SAMSUNG_JACK
#include <linux/sec_jack.h>
#include <linux/mfd/abx500.h>
#endif
#ifdef CONFIG_BATTERY_SAMSUNG
#include <linux/battery/sec_charging_common.h>
#include <linux/battery/charger/abb_sec_charger.h>
#endif
#include <linux/gpio_keys.h>
#include <linux/delay.h>
#include <linux/mfd/abx500/ab8500-denc.h>
#include <linux/spi/stm_msp.h>
#include <plat/gpio-nomadik.h>
#include <linux/input/bt404_ts.h>
#include <linux/leds.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>
#include <video/ktd259x_bl.h>
#include <../drivers/staging/android/timed_gpio.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/i2c.h>
#include <plat/ste_dma40.h>
#include <plat/pincfg.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <linux/input/ab8505_micro_usb_iddet.h>
#include <linux/cpuidle-dbx500.h>
#include <mach/irqs.h>
#include <mach/ste-dma40-db8500.h>
#include <linux/mfd/abx500/ab8500-pwmleds.h>
#ifdef CONFIG_PROXIMITY_GP2A
#include <mach/gp2a.h>
#endif
#ifdef CONFIG_PROXIMITY_TMD2672
#include <mach/tmd2672.h>
#endif
#include <mach/crypto-ux500.h>
#include <mach/pm.h>
#include <mach/reboot_reasons.h>
#include <linux/yas.h>


#include <video/mcde_display.h>

#ifdef CONFIG_DB8500_MLOADER
#include <mach/mloader-dbx500.h>
#endif

#include "devices-db8500.h"
#include "board-codina-regulators.h"
#include "pins.h"
#include "pins-db8500.h"
#include "cpu-db8500.h"
#include "board-mop500.h"	/* using some generic functions defined here */
#include "board-sec-bm.h"
#ifdef CONFIG_STE_WLAN
#include "board-mop500-wlan.h"
#else
#include "board-bluetooth-bcm4334.h"
#endif
#include <mach/board-sec-ux500.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
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

unsigned int board_id;

unsigned int sec_debug_settings;
int jig_smd = 1;
EXPORT_SYMBOL(jig_smd);
int is_cable_attached;
EXPORT_SYMBOL(is_cable_attached);

int use_ab8505_iddet;
EXPORT_SYMBOL(use_ab8505_iddet);

struct device *gps_dev;
EXPORT_SYMBOL(gps_dev);

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

	if ((ram_console_size > 0) && (*p == '@')) {
		ram_console_resource.start = memparse(p + 1, &p);
		ram_console_resource.end =
			ram_console_resource.start + ram_console_size - 1;
		ram_console_device.num_resources = 1;
	}
	return 1;
}

__setup("mem_ram_console=", ram_console_setup);

#endif /* CONFIG_ANDROID_RAM_CONSOLE */


#if defined(CONFIG_INPUT_YAS_MAGNETOMETER)
struct yas_platform_data yas_data = {
	.hw_rev = 0,	/* hw gpio value */
};
#endif


#if defined(CONFIG_PROXIMITY_GP2A)

/* -------------------------------------------------------------------------
 * GP2A PROXIMITY SENSOR PLATFORM-SPECIFIC CODE AND DATA
 * ------------------------------------------------------------------------- */
static int __init gp2a_setup(void);

static struct gp2a_platform_data gp2a_plat_data __initdata = {
	.ps_vout_gpio	= PS_INT_CODINA_R0_0,
	.hw_setup	= gp2a_setup,
	.alsout		= ADC_AUX2,
};

static int __init gp2a_setup(void)
{
	int err;

	/* Configure the GPIO for the interrupt */
	err = gpio_request(gp2a_plat_data.ps_vout_gpio, "PS_VOUT");
	if (err < 0) {
		pr_err("PS_VOUT: failed to request GPIO %d,"
			" err %d\n", gp2a_plat_data.ps_vout_gpio, err);

		goto err1;
	}

	err = gpio_direction_input(gp2a_plat_data.ps_vout_gpio);
	if (err < 0) {
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
#if defined(CONFIG_PROXIMITY_TMD2672)

/* -------------------------------------------------------------------------
 * TMD2672 PROXIMITY SENSOR PLATFORM-SPECIFIC CODE AND DATA
 * ------------------------------------------------------------------------- */
static int __init tmd2672_setup(void);

static struct tmd2672_platform_data tmd2672_plat_data __initdata = {
	.ps_vout_gpio	= PS_INT_CODINA_R0_0,
	.hw_setup	= tmd2672_setup,
	.alsout		= ADC_AUX2,
};

static int __init tmd2672_setup(void)
{
	int err;

	/* Configure the GPIO for the interrupt */
	err = gpio_request(tmd2672_plat_data.ps_vout_gpio, "PS_VOUT");
	if (err < 0) {
		pr_err("PS_VOUT: failed to request GPIO %d,"
			" err %d\n", tmd2672_plat_data.ps_vout_gpio, err);

		goto err1;
	}

	err = gpio_direction_input(tmd2672_plat_data.ps_vout_gpio);
	if (err < 0) {
		pr_err("PS_VOUT: failed to configure input"
			" direction for GPIO %d, err %d\n",
			tmd2672_plat_data.ps_vout_gpio, err);

		goto err2;
	}

	return 0;

err2:
	gpio_free(tmd2672_plat_data.ps_vout_gpio);
err1:
	return err;
}

#endif

#if defined(CONFIG_BATTERY_SAMSUNG)
static enum cable_type_t set_cable_status;
int abb_get_cable_status(void) {return (int)set_cable_status; }

void abb_battery_cb(void)
{
	union power_supply_propval value;
	int i, ret = 0;
	struct power_supply *psy;

	pr_info("abb_battery_cb called\n");
	set_cable_status = CABLE_TYPE_NONE;

	for (i = 0; i < 10; i++) {
		psy = power_supply_get_by_name("battery");
		if (psy)
			break;
	}
	if (i == 10) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return;
	}

	value.intval = POWER_SUPPLY_TYPE_BATTERY;

	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
		&value);
	if (ret) {
		pr_err("%s: fail to set power_suppy ONLINE property(%d)\n",
			__func__, ret);
	}
}

void abb_usb_cb(bool attached)
{
	union power_supply_propval value;
	int i, ret = 0;
	struct power_supply *psy;

	pr_info("abb_usb_cb attached %d\n", attached);
	set_cable_status = attached ? CABLE_TYPE_USB : CABLE_TYPE_NONE;

	for (i = 0; i < 10; i++) {
		psy = power_supply_get_by_name("battery");
		if (psy)
			break;
	}
	if (i == 10) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return;
	}

	switch (set_cable_status) {
	case CABLE_TYPE_USB:
		value.intval = POWER_SUPPLY_TYPE_USB;
		break;
	case CABLE_TYPE_NONE:
		value.intval = POWER_SUPPLY_TYPE_BATTERY;
		break;
	default:
		pr_err("%s: invalid cable :%d\n", __func__, set_cable_status);
		return;
	}

	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
		&value);
	if (ret) {
		pr_err("%s: fail to set power_suppy ONLINE property(%d)\n",
			__func__, ret);
	}
}

void abb_charger_cb(bool attached)
{
	union power_supply_propval value;
	int i, ret = 0;
	struct power_supply *psy;

	pr_info("abb_charger_cb attached %d\n", attached);
	set_cable_status = attached ? CABLE_TYPE_AC : CABLE_TYPE_NONE;

	for (i = 0; i < 10; i++) {
		psy = power_supply_get_by_name("battery");
		if (psy)
			break;
	}
	if (i == 10) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return;
	}

	switch (set_cable_status) {
	case CABLE_TYPE_AC:
		value.intval = POWER_SUPPLY_TYPE_MAINS;
		break;
	case CABLE_TYPE_NONE:
		value.intval = POWER_SUPPLY_TYPE_BATTERY;
		break;
	default:
		pr_err("invalid status:%d\n", attached);
		return;
	}

	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
		&value);
	if (ret) {
		pr_err("%s: fail to set power_suppy ONLINE property(%d)\n",
			__func__, ret);
	}
}

void abb_jig_cb(bool attached)
{
	pr_info("abb_jig_cb attached %d\n", attached);

	set_cable_status = attached ? CABLE_TYPE_JIG : CABLE_TYPE_NONE;
}

void abb_uart_cb(bool attached)
{
	pr_info("abb_uart_cb attached %d\n", attached);

	set_cable_status = attached ? CABLE_TYPE_UARTOFF : CABLE_TYPE_NONE;

}

void abb_dock_cb(bool attached)
{
	pr_info("abb_dock_cb attached %d\n", attached);
	set_cable_status = attached ? CABLE_TYPE_CARDOCK : CABLE_TYPE_NONE;
}
#endif

#if defined(CONFIG_USB_SWITCHER)
static struct usb_switch fsa880_data =	{
		.name					=	"FSA880",
		.id	 				=	0x0	,
		.id_mask				=	0xff	,
		.control_register_default		=	0x05	,
		.control_register_inital_value  	=	0x1e	,
		.connection_changed_interrupt_gpio	=	95	,
		.charger_detect_gpio			=	0xffff 	, /*no charger detect gpio for this device*/
		.valid_device_register_1_bits		=	0xEF	,
		.valid_device_register_2_bits		=	0xFF	,
		.valid_registers			=	{0,1,1,1,1,0,0,1,0,0,1,1,0,0,0,0, 0, 0, 0, 1, 1  },
};
#endif

#if defined(CONFIG_TOUCHSCREEN_ZINITIX_BT404)
/* Configuration settings for Zinitix controller (BT404). 0x1D0 registers. */
const struct bt404_ts_reg_data reg_data[] = {
	/*{value, valid} */
	{0, 0}, /* 00, RESERVED */
	{0, 0}, /* 01, RESERVED */
	{0, 0}, /* 02, RESERVED */
	{0, 0}, /* 03, RESERVED */
	{0, 0}, /* 04, RESERVED */
	{0, 0}, /* 05, RESERVED */
	{0, 0}, /* 06, RESERVED */
	{0, 0}, /* 07, RESERVED */
	{0, 0}, /* 08, RESERVED */
	{0, 0}, /* 09, RESERVED */
	{0, 0}, /* 0A, RESERVED */
	{0, 0}, /* 0B, RESERVED */
	{0, 0}, /* 0C, RESERVED */
	{0, 0}, /* 0D, RESERVED */
	{0, 0}, /* 0E, RESERVED */
	{0, 0}, /* 0F, RESERVED */
	{0, 1}, /* 10, TOUCH MODE */
	{0x0E0E, 0}, /* 11, CHIP REVISION */
	{0x0038, 0}, /* 12, FIRMWARE VERSION */
	{0, 1}, /* 13, REGISTER DATA VERSION */
	{0, 1}, /* 14, TSP TYPE */
	{10, 1}, /* 15, SUPPORTED FINGER NUM */
	{0, 0}, /* 16, RESERVED */
	{0x002C, 0}, /* 17, INTERNAL FLAG */
	{0xFFFA, 0}, /* 18, EEPROM INFO */
	{0, 0}, /* 19, RESERVED */
	{0, 0}, /* 1A, RESERVED */
	{0, 0}, /* 1B, RESERVED */
	{0, 0}, /* 1C, RESERVED */
	{200, 0}, /* 1D, CURRENT SENSITIVITY TH */
	{0, 0}, /* 1E, CURRENT CHARGER LIMIT CNT */
	{4, 0}, /* 1F, CURRENT RAW VARIATION */
	{200, 0}, /* 20, SENSITIVITY TH. */
	{200, 0}, /* 21, Y0 SENSITIVITY TH */
	{200, 0}, /* 22, LAST Y SENSITIVITY TH */
	{200, 0}, /* 23, X0 SENSITIVITY TH */
	{200, 0}, /* 24, LAST X SENSITIVITY TH */
	{8, 0}, /* 25, ACTIVE SENSITIVITY COEF */
	{5, 0}, /* 26, AUTO SENSITIVITY TH STEP */
	{50, 0}, /* 27, AUTO SENSITIVITY TH VALUE PER STEP */
	{5, 0}, /* 28, 1st BASELINE VARIATION */
	{20, 0}, /* 29, 2nd BASELINE VARIATION */
	{40, 0}, /* 2A, 1st BASELINE PERIOD */
	{10, 0}, /* 2B, 2nd BASELINE PERIOD */
	{1000, 0}, /* 2C, BASELINE FORCE PERIOD */
	{20, 0}, /* 2D, 1st BASELINE VARIATION ON CHARGER */
	{40, 0}, /* 2E, 2nd BASELINE VARIATION ON CHARGER */
	{40, 0}, /* 2F, BASELINE UPDATE PERIOD ON CHARGER */
	{2, 0}, /* 30, FIR COEFFICIENT */
	{2, 0}, /* 31, HW_STYLUS MOVING FIR */
	{2, 0}, /* 32, HW_FINGER MOVING FIR */
	{2, 0}, /* 33, SW_FIR COEFFICIENT */
	{2, 0}, /* 34, SW WIDTH FIR */
	{15, 0}, /* 35, WIDTH(WEIGHT) COEF */
	{0x1E05, 0}, /* 36, MVAVG_1_VELOCITY */
	{258, 0}, /* 37, MVAVG_1_SW_INC */
	{0, 0}, /* 38, RESERVED */
	{0, 0}, /* 39, RESERVED */
	{0, 0}, /* 3A, RESERVED */
	{2, 0}, /* 3B, REACTION COUNT */
	{160, 0}, /* 3C, PALM REJECT TRESHHOLD */
	{160, 0}, /* 3D, NOISE REJECT TRESHHOLD */
	{0x0203, 0}, /* 3E, NOISE REJECT HILO RATIO */
	{80, 0}, /* 3F, NOISE PALM LEVEL */
	{10, 0}, /* 40, NOISE PALM UP SKIP COUNT */
	{5, 0}, /* 41, SKIP REJECT COUNT AFTER DETECT */
	{0x0103, 0}, /* 42, CUTOFF NOISE PDATA RATIO */
	{0x0103, 0}, /* 43, CUTOFF NOISE WIDTH RATIO */
	{128, 0}, /* 44, REACTION THRESHHOLD */
	{0x0104, 0}, /* 45, CHECK NOISE PATTERN P */
	{3, 0}, /* 46, CHECK NOISE PATTERN P CENTER CNT */
	{2, 0}, /* 47, CHECK NOISE PATTERN P EDGE CNT */
	{1, 0}, /* 48, CHECK NOISE PATTERN P CORNER CNT */
	{0x010A, 0}, /* 49, CHECK NOISE PATTERN N */
	{2, 0}, /* 4A, CHECK NOISE PATTERN N CNT */
	{80, 0}, /* 4B, CHECK NOISE STYLUS RAW LIMIT VALUE */
	{0x0203, 0}, /* 4C, CHECK NOISE STYLUS PATTERN P */
	{4, 0}, /* 4D, CHECK NOISE STYLUS PATTERN P CNT */
	{0x0203, 0}, /* 4E, CHECK NOISE STYLUS PATTERN N */
	{2, 0}, /* 4F, CHECK NOISE STYLUS PATTERN N CNT */
	{0, 0}, /* 50, AUTO CHARGING DETECT USE */
	{0, 0}, /* 51, CHARGING MODE */
	{15, 0}, /* 52, CHARGING STEP LIMIT */
	{1000, 0}, /* 53, CHARGING MODE SENSITIVITY TH */
	{20, 0}, /* 54, AUTO CHARGING OUT VARIATION */
	{200, 0}, /* 55, AUTO CHARGING IN VARIATION */
	{0x0103, 0}, /* 56, AUTO CHARING STRENGTH RATIO */
	{80, 0}, /* 57, AUTO CHARING LIMIT VALUE */
	{10, 0}, /* 58, AUTO CHARING LIMIT CNT */
	{10, 0}, /* 59, AUTO CHARGING SKIP CNT */
	{0, 0}, /* 5A, AUTO CHARGING REJECT HILO RATIO */
	{80, 0}, /* 5B, AUTO CHARGING REJECT PALM CNT */
	{10, 0}, /* 5C, AUTO CHARGING REACTION COUNT */
	{0, 0}, /* 5D, RESERVED */
	{0, 0}, /* 5E, RESERVED */
	{0, 0}, /* 5F, RESERVED */
	{20, 0}, /* 60, TOTAL NUM OF X */
	{16, 0}, /* 61, TOTAL NUM OF Y */
	{0x0B0A, 0}, /* 62, X00_01_DRIVE_NUM */
	{0x0D0C, 0}, /* 63, X02_03_DRIVE_NUM */
	{0x0F0E, 0}, /* 64, X04_05_DRIVE_NUM */
	{0x1110, 0}, /* 65, X06_07_DRIVE_NUM */
	{0x1312, 0}, /* 66, X08_09_DRIVE_NUM */
	{0x0100, 0}, /* 67, X10_11_DRIVE_NUM */
	{0x0302, 0}, /* 68, X12_13_DRIVE_NUM */
	{0x0504, 0}, /* 69, X14_15_DRIVE_NUM */
	{0x0706, 0}, /* 6A, X16_17_DRIVE_NUM */
	{0x0908, 0}, /* 6B, X18_19_DRIVE_NUM */
	{0x1514, 0}, /* 6C, X20_21_DRIVE_NUM */
	{0x1716, 0}, /* 6D, X22_23_DRIVE_NUM */
	{0x1918, 0}, /* 6E, X24_25_DRIVE_NUM */
	{0x1B1A, 0}, /* 6F, X26_27_DRIVE_NUM */
	{0x1D1C, 0}, /* 70, X28_29_DRIVE_NUM */
	{0x1F1E, 0}, /* 71, X30_31_DRIVE_NUM */
	{0x2120, 0}, /* 72, X32_33_DRIVE_NUM */
	{0x2322, 0}, /* 73, X34_35_DRIVE_NUM */
	{0x2524, 0}, /* 74, X36_37_DRIVE_NUM */
	{0x2726, 0}, /* 75, X38_39_DRIVE_NUM */
	{1700, 0}, /* 76, CALIBRATION REFERENCE */
	{1, 0}, /* 77, CALIBRATION C MODE */
	{15, 0}, /* 78, CALIBRATION DEFAULT N COUNT */
	{15, 0}, /* 79, CALIBRATION DEFAULT C */
	{32, 0}, /* 7A, CALIBRATION ACCURACY */
	{20, 0}, /* 7B, SOFT CALIBRATION INIT COUNT */
	{0, 0}, /* 7C, RESERVED */
	{0, 0}, /* 7D, RESERVED */
	{0, 0}, /* 7E, RESERVED */
	{0, 0}, /* 7F, RESERVED */
	{0, 0}, /* 80, RESERVED */
	{0, 0}, /* 81, RESERVED */
	{0, 0}, /* 82, RESERVED */
	{0, 0}, /* 83, RESERVED */
	{0, 0}, /* 84, RESERVED */
	{0, 0}, /* 85, RESERVED */
	{0, 0}, /* 86, RESERVED */
	{0, 0}, /* 87, RESERVED */
	{0, 0}, /* 88, RESERVED */
	{0, 0}, /* 89, RESERVED */
	{0, 0}, /* 8A, RESERVED */
	{0, 0}, /* 8B, RESERVED */
	{0, 0}, /* 8C, RESERVED */
	{0, 0}, /* 8D, RESERVED */
	{0, 0}, /* 8E, RESERVED */
	{0, 0}, /* 8F, RESERVED */
	{0, 0}, /* 90, RESERVED */
	{0, 0}, /* 91, RESERVED */
	{0, 0}, /* 92, RESERVED */
	{0, 0}, /* 93, RESERVED */
	{0, 0}, /* 94, RESERVED */
	{0, 0}, /* 95, RESERVED */
	{0, 0}, /* 96, RESERVED */
	{0, 0}, /* 97, RESERVED */
	{0, 0}, /* 98, RESERVED */
	{0, 0}, /* 99, RESERVED */
	{0, 0}, /* 9A, RESERVED */
	{0, 0}, /* 9B, RESERVED */
	{0, 0}, /* 9C, RESERVED */
	{0, 0}, /* 9D, RESERVED */
	{0, 0}, /* 9E, RESERVED */
	{0, 0}, /* 9F, RESERVED */
	{0, 0}, /* A0, RESERVED */
	{0, 0}, /* A1, RESERVED */
	{0, 0}, /* A2, RESERVED */
	{0, 0}, /* A3, RESERVED */
	{0, 0}, /* A4, RESERVED */
	{0, 0}, /* A5, RESERVED */
	{0, 0}, /* A6, RESERVED */
	{0, 0}, /* A7, RESERVED */
	{0, 0}, /* A8, RESERVED */
	{0, 0}, /* A9, RESERVED */
	{0, 0}, /* AA, RESERVED */
	{0, 0}, /* AB, RESERVED */
	{0, 0}, /* AC, RESERVED */
	{0, 0}, /* AD, RESERVED */
	{0, 0}, /* AE, RESERVED */
	{0, 0}, /* AF, RESERVED */
	{2, 1}, /* B0, SUPPORTED BUTTON NUM */
	{0, 1}, /* B1, BUTTON REACTION CNT */
	{200, 1}, /* B2, BUTTON SENSITIVITY TH */
	{1, 1}, /* B3, BUTTON LINE TYPE */
	{790, 1}, /* B4, BUTTON LINE NUM */
	{10, 1}, /* B5, BUTTON RANGE */
	{70, 1}, /* B6, BUTTON_0 START NODE */
	{400, 1}, /* B7, BUTTON_1 START NODE */
	{0, 1}, /* B8, BUTTON_2 START NODE */
	{0, 1}, /* B9, BUTTON_3 START NODE */
	{0, 1}, /* BA, BUTTON_4 START NODE */
	{0, 1}, /* BB, BUTTON_5 START NODE */
	{0, 1}, /* BC, BUTTON_6 START NODE */
	{0, 1}, /* BD, BUTTON_7 START NODE */
	{0, 0}, /* BE, RESERVED */
	{0, 0}, /* BF, RESERVED */
	{2560, 0}, /* C0, RESOLUTION OF X */
	{2048, 0}, /* C1, RESOLUTION OF Y */
	{0x0001, 0}, /* C2, COORD ORIENTATION */
	{8, 0}, /* C3, HOLD POINT THRESHOLD */
	{4, 0}, /* C4, HOLD WIDTH THRESHOLD */
	{1000, 0}, /* C5, STYLUS HW THRESHHOLD */
	{10000, 0}, /* C6, ASSUME UP THRESHHOLD */
	{64, 0}, /* C7, ASSUME UP SKIP THRESHHOLD */
	{0, 0}, /* C8, X POINT SHIFT */
	{0, 0}, /* C9, Y POINT SHIFT */
	{0, 0}, /* CA, VIEW XF OFFSET */
	{0, 0}, /* CB, VIEW XL OFFSET */
	{0, 0}, /* CC, VIEW YF OFFSET */
	{0, 0}, /* CD, VIEW YL OFFSET */
	{0, 0}, /* CE, RESERVED */
	{0, 0}, /* CF, RESERVED */
	{69, 0}, /* D0, FINGER COEF X GAIN */
	{1000, 0}, /* D1, FINGER ATTACH VALUE */
	{400, 0}, /* D2, STYLUS ATTACH VALUE */
	{0, 0}, /* D3, RESERVED */
	{0, 0}, /* D4, RESERVED */
	{0x0005, 0}, /* D5, PDATA COEF1 */
	{0x0003, 0}, /* D6, PDATA COEF2 */
	{0x0003, 0}, /* D7, PDATA COEF3 */
	{0, 0}, /* D8, RESERVED */
	{0, 0}, /* D9, RESERVED */
	{10, 0}, /* DA, EDGE COEFFICIENT */
	{100, 0}, /* DB, OPT Q RESOLUTION */
	{0x7777, 0}, /* DC, PDATA EDGE COEF1 */
	{0x4444, 0}, /* DD, PDATA EDGE COEF2 */
	{0x3333, 0}, /* DE, PDATA EDGE COEF3 */
	{160, 0}, /* DF, EDGE Q BIAS1_1 */
	{160, 0}, /* E0, EDGE Q BIAS2_1 */
	{165, 0}, /* E1, EDGE Q BIAS3_1 */
	{165, 0}, /* E2, EDGE Q BIAS4_1 */
	{0, 0}, /* E3, RESERVED */
	{0, 0}, /* E4, RESERVED */
	{0x8888, 0}, /* E5, PDATA CORNER COEF1 */
	{0x3333, 0}, /* E6, PDATA CORNER COEF2 */
	{0x3333, 0}, /* E7, PDATA CORNER COEF3 */
	{130, 0}, /* E8, CORNER Q BIAS1_1 */
	{130, 0}, /* E9, CORNER Q BIAS2_1 */
	{130, 0}, /* EA, CORNER Q BIAS3_1 */
	{130, 0}, /* EB, CORNER Q BIAS4_1 */
	{0, 0}, /* EC, RESERVED */
	{0, 0}, /* ED, RESERVED */
	{0, 0}, /* EE, RESERVED */
	{0, 0}, /* EF, RESERVED */
	{0x080F, 0}, /* F0, INT ENABLE FLAG */
	{0, 0}, /* F1, PERIODICAL INTERRUPT INTERVAL */
	{0, 0}, /* F2, RESERVED */
	{0, 0}, /* F3, RESERVED */
	{0, 0}, /* F4, RESERVED */
	{0, 0}, /* F5, RESERVED */
	{0, 0}, /* F6, RESERVED */
	{0, 0}, /* F7, RESERVED */
	{0, 0}, /* F8, RESERVED */
	{0, 0}, /* F9, RESERVED */
	{0, 0}, /* FA, RESERVED */
	{0, 0}, /* FB, RESERVED */
	{0, 0}, /* FC, RESERVED */
	{0, 0}, /* FD, RESERVED */
	{0, 0}, /* FE, RESERVED */
	{0, 0}, /* FF, RESERVED */
	{40, 0}, /* 100, AFE FREQUENCY */
	{0x2828, 0}, /* 101, FREQ X NUM 0_1 */
	{0x2828, 0}, /* 102, FREQ X NUM 2_3 */
	{0x2828, 0}, /* 103, FREQ X NUM 4_5 */
	{0x2828, 0}, /* 104, FREQ X NUM 6_7 */
	{0x2828, 0}, /* 105, FREQ X NUM 8_9 */
	{0x2828, 0}, /* 106, FREQ X NUM 10_11 */
	{0x2828, 0}, /* 107, FREQ X NUM 12_13 */
	{0x2828, 0}, /* 108, FREQ X NUM 14_15 */
	{0x2828, 0}, /* 109, FREQ X NUM 16_17 */
	{0x2828, 0}, /* 10A, FREQ X NUM 18_19 */
	{0x2828, 0}, /* 10B, FREQ X NUM 20_21 */
	{0x2828, 0}, /* 10C, FREQ X NUM 22_23 */
	{0x2828, 0}, /* 10D, FREQ X NUM 24_25 */
	{0x2828, 0}, /* 10E, FREQ X NUM 26_27 */
	{0x2828, 0}, /* 10F, FREQ X NUM 28_29 */
	{0x2828, 0}, /* 110, FREQ X NUM 30_31 */
	{0x2828, 0}, /* 111, FREQ X NUM 32_33 */
	{0x2828, 0}, /* 112, FREQ X NUM 34_35 */
	{0x2828, 0}, /* 113, FREQ X NUM 36_37 */
	{0x2828, 0}, /* 114, FREQ X NUM 38_39 */
	{0, 0}, /* 115, RESERVED */
	{0, 0}, /* 116, RESERVED */
	{0, 0}, /* 117, RESERVED */
	{0, 0}, /* 118, RESERVED */
	{0, 0}, /* 119, RESERVED */
	{0, 0}, /* 11A, RESERVED */
	{0, 0}, /* 11B, RESERVED */
	{0, 0}, /* 11C, RESERVED */
	{0, 0}, /* 11D, RESERVED */
	{0, 0}, /* 11E, RESERVED */
	{0, 0}, /* 11F, RESERVED */
	{0, 0}, /* 120, AFE MODE */
	{0, 0}, /* 121, AFE C MODE */
	{10, 0}, /* 122, AFE DEFAULT N COUNT */
	{63, 0}, /* 123, AFE DEFAULT C */
	{0x0000, 0}, /* 124, ONE NODE SCAN DELAY */
	{0x0000, 0}, /* 125, CUR ONE NODE SCAN DELAY */
	{0x0000, 0}, /* 126, ALL NODE SCAN DELAY LSB */
	{0x0000, 0}, /* 127, ALL NODE SCAN DELAY MSB */
	{0x0000, 0}, /* 128, CUR ALL NODE SCAN DELAY LSB */
	{0x0000, 0}, /* 129, CUR ALL NODE SCAN DELAYMSB */
	{0, 0}, /* 12A, AFE SCAN NOISE C */
	{2, 0}, /* 12B, AFE R SHIFT VALUE */
	{0, 0}, /* 12C, AFE SCAN MODE */
	{0, 0}, /* 12D, RESERVED */
	{0, 0}, /* 12E, RESERVED */
	{0, 0}, /* 12F, RESERVED */
	{0x3333, 0}, /* 130, REG_AFE_X_VAL */
	{0xFFFF, 0}, /* 131, REG_AFE_XA_EN */
	{0xFFFF, 0}, /* 132, REG_AFE_XB_EN */
	{0x3305, 0}, /* 133, REG_AFE_X_NOVL */
	{0x0011, 0}, /* 134, REG_AFE_Y_NOVL */
	{0x0133, 0}, /* 135, REG_AFE_Y_VAL */
	{0x0001, 0}, /* 136, REG_RBG_EN */
	{0x00FF, 0}, /* 137, REG_INTAMP_EN */
	{0x0011, 0}, /* 138, REG_INTAMP_VREF_EN */
	{0x2000, 0}, /* 139, REG_INTAMP_VREF_NSEL_N */
	{0x0002, 0}, /* 13A, REG_INTAMP_VREF_CTRL */
	{0x007F, 0}, /* 13B, REG_INTAMP_TIME0 */
	{0x00FF, 0}, /* 13C, REG_INTAMP_TIME1 */
	{0x3F0F, 0}, /* 13D, REG_SAR_SAMPLE_TIME */
	{0x0001, 0}, /* 13E, REG_SAR_CTRL */
	{0x0000, 0}, /* 13F, REG_SAR_BUF_EN */
	{0x0000, 0}, /* 140, REG_ATEST_CTRL */
	{0x0000, 0}, /* 141, REG_ATEST_SEL0 */
	{0x0000, 0}, /* 142, REG_ATEST_SEL1 */
	{0x0004, 0}, /* 143, REG_MULTI_FRAME */
	{0, 0}, /* 144 - 1CF, RESERVED */
};

struct i2c_client *bt404_i2c_client = NULL;

void put_isp_i2c_client(struct i2c_client *client)
{
	bt404_i2c_client = client;
}

struct i2c_client *get_isp_i2c_client(void)
{
	return bt404_i2c_client;
}

static void bt404_ts_int_set_pull(bool to_up)
{
	int ret;
	int pull = (to_up) ? NMK_GPIO_PULL_UP : NMK_GPIO_PULL_DOWN;

	ret = nmk_gpio_set_pull(TSP_INT_CODINA_R0_0, pull);
	if (ret < 0)
		printk(KERN_ERR "%s: fail to set pull xx on interrupt pin\n",
								__func__);
}

static int bt404_ts_pin_configure(bool to_gpios)
{
	if (to_gpios) {
		nmk_gpio_set_mode(TSP_SCL_CODINA_R0_0, NMK_GPIO_ALT_GPIO);
		gpio_direction_output(TSP_SCL_CODINA_R0_0, 0);

		nmk_gpio_set_mode(TSP_SDA_CODINA_R0_0, NMK_GPIO_ALT_GPIO);
		gpio_direction_output(TSP_SDA_CODINA_R0_0, 0);

	} else {
		gpio_direction_output(TSP_SCL_CODINA_R0_0, 1);
		nmk_gpio_set_mode(TSP_SCL_CODINA_R0_0, NMK_GPIO_ALT_C);

		gpio_direction_output(TSP_SDA_CODINA_R0_0, 1);
		nmk_gpio_set_mode(TSP_SDA_CODINA_R0_0, NMK_GPIO_ALT_C);
	}
	return 0;
}

static struct bt404_ts_platform_data bt404_ts_pdata = {
	.gpio_int		= TSP_INT_CODINA_R0_0,
	.gpio_scl		= TSP_SCL_CODINA_R0_0,
	.gpio_sda		= TSP_SDA_CODINA_R0_0,
	.gpio_ldo_en		= TSP_LDO_ON1_CODINA_R0_0,
	.gpio_reset		= -1,
	.orientation		= 0,
	.x_max			= 480,
	.y_max			= 800,
	.num_buttons		= 2,
	.button_map		= {KEY_MENU, KEY_BACK,},
	.num_regs		= ARRAY_SIZE(reg_data),
	.reg_data		= reg_data,
	.put_isp_i2c_client	= put_isp_i2c_client,
	.get_isp_i2c_client	= get_isp_i2c_client,
	.int_set_pull		= bt404_ts_int_set_pull,
	.pin_configure		= bt404_ts_pin_configure,
};

static int __init bt404_ts_init(void)
{
	int ret;

	if (system_rev != CODINA_TMO_R0_0_A) {
	ret = gpio_request(TSP_LDO_ON1_CODINA_R0_0, "bt404_ldo_en");
	if (ret < 0) {
			printk(KERN_ERR
				"bt404: could not obtain gpio for ldo pin\n");
		return -1;
	}
	gpio_direction_output(TSP_LDO_ON1_CODINA_R0_0, 0);
	}
	if (system_rev >= CODINA_TMO_R0_0_A)
		bt404_ts_pdata.power_con = PMIC_CON;
	else
		bt404_ts_pdata.power_con = LDO_CON;

	ret = gpio_request(TSP_INT_CODINA_R0_0, "bt404_int");
	if (ret < 0) {
		printk(KERN_ERR "bt404: could not obtain gpio for int\n");
		return -1;
	}
	gpio_direction_input(TSP_INT_CODINA_R0_0);

	bt404_ts_pdata.panel_type = (board_id >= 12) ?
						GFF_PANEL : EX_CLEAR_PANEL;

	printk(KERN_INFO "bt404: initialize pins\n");

	return 0;
}
#endif


static struct i2c_board_info __initdata codina_r0_0_i2c0_devices[] = {
#if defined(CONFIG_PROXIMITY_GP2A)
	{
		/* GP2A proximity sensor */
		I2C_BOARD_INFO(GP2A_I2C_DEVICE_NAME, 0x44),
		.platform_data = &gp2a_plat_data,
	},
#endif
#if defined(CONFIG_PROXIMITY_TMD2672)
	{
		/* TMD2672 proximity sensor */
		I2C_BOARD_INFO(TMD2672_I2C_DEVICE_NAME, 0x39),
		.platform_data = &tmd2672_plat_data,
	},
#endif

};

static struct i2c_board_info __initdata codina_r0_0_i2c1_devices[] = {
#if defined(CONFIG_USB_SWITCHER)
	{
		I2C_BOARD_INFO("musb", 0x25),
		.platform_data = &fsa880_data ,
		.irq = GPIO_TO_IRQ(JACK_NINT_CODINA_R0_0),
	},
#endif
};

static struct i2c_board_info __initdata codina_r0_0_i2c2_devices[] = {
#if 0
#if defined(CONFIG_ACCEL_BMA222)
	{
		/* BMA222 accelerometer driver */
		I2C_BOARD_INFO("accelerometer", 0x08),
	},
#endif
#endif
};

static struct i2c_gpio_platform_data codina_gpio_i2c7_data = {
	.sda_pin = TSP_SDA_CODINA_R0_0,
	.scl_pin = TSP_SCL_CODINA_R0_0,
	.udelay = 1, /* 500/udelay KHz  */
};

static struct platform_device codina_gpio_i2c7_pdata = {
	.name = "i2c-gpio",
	.id = 7,
	.dev = {
		.platform_data = &codina_gpio_i2c7_data,
	},
};

static struct i2c_board_info __initdata codina_r0_0_gpio_i2c7_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_ZINITIX_BT404)
	{
		I2C_BOARD_INFO(BT404_ISP_DEVICE, 0x50),
		.platform_data	= &bt404_ts_pdata,
	},
#endif
};

static struct i2c_board_info __initdata codina_r0_0_i2c3_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_ZINITIX_BT404)
	{
		I2C_BOARD_INFO(BT404_TS_DEVICE, 0x20),
		.platform_data	= &bt404_ts_pdata,
		.irq = GPIO_TO_IRQ(TSP_INT_CODINA_R0_0),
	},
#endif
};
static struct i2c_gpio_platform_data codina_gpio_i2c4_data = {
	.sda_pin = SUBPMU_SDA_CODINA_R0_0,
	.scl_pin = SUBPMU_SCL_CODINA_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device codina_gpio_i2c4_pdata = {
	.name = "i2c-gpio",
	.id = 4,
	.dev = {
		.platform_data = &codina_gpio_i2c4_data,
	},
};

static struct i2c_board_info __initdata codina_r0_0_gpio_i2c4_devices[] = {
    /* NCP6914 */
{
	/* ncp6914 power management IC for the cameras */
	I2C_BOARD_INFO("ncp6914", 0x10),
	/* .platform_data = &ncp6914_plat_data, */
},

    /* SM5103 */
    {
        /* sm5103 power management IC for the cameras */
        I2C_BOARD_INFO("sm5103", 0x7F),
        /* .platform_data = &sm5103_plat_data, */
    },
};

static struct i2c_board_info __initdata codina_r0_0_gpio_i2c4_devices_r0[] = {
#ifdef CONFIG_SENSORS_HSCD
	{
		/* ALPS Magnetometer driver */
		I2C_BOARD_INFO("hscd_i2c", 0x0c),

	},
#endif
};

/*static struct i2c_gpio_platform_data codina_gpio_i2c5_data = {
	.sda_pin = NFC_SDA_CODINA_R0_0,
	.scl_pin = NFC_SCL_CODINA_R0_0,
	.udelay = 3,*/	/* closest to 400KHz */
/*};*/

/*static struct platform_device codina_gpio_i2c5_pdata = {
	.name = "i2c-gpio",
	.id = 5,
	.dev = {
		.platform_data = &codina_gpio_i2c5_data,
	},
};*/

/*static struct i2c_board_info __initdata codina_r0_0_gpio_i2c5_devices[] = {*/
/* TBD - NFC */
/*#if 0
	{
		I2C_BOARD_INFO("", 0x30),
	},
#endif
};*/

static struct i2c_gpio_platform_data codina_gpio_i2c6_data = {
	.sda_pin = SENSOR_SDA_CODINA_R0_0,
	.scl_pin = SENSOR_SCL_CODINA_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device codina_gpio_i2c6_pdata = {
	.name = "i2c-gpio",
	.id = 6,
	.dev = {
	.platform_data = &codina_gpio_i2c6_data,
	},
};

static struct i2c_board_info __initdata codina_r0_0_gpio_i2c6_devices[] = {
#ifdef CONFIG_SENSORS_KXDM
		{
			/* STM K2DM/K3DM Accelerometer driver */
			I2C_BOARD_INFO("accsns_i2c", 0x19),
		},
#endif
};

static struct platform_device codina_gpio_i2c6_pdata_01 = {
	.name = "i2c-gpio",
	.id = 6,
	.dev = {
	.platform_data = &codina_gpio_i2c6_data,
	},
};

static struct i2c_board_info __initdata codina_r0_0_gpio_i2c6_devices_01[] = {
#ifdef CONFIG_SENSORS_HSCD
		{
			I2C_BOARD_INFO("bma222e", 0x18),
		},
#endif
#ifdef CONFIG_SENSORS_BMA254
		{
			I2C_BOARD_INFO("bma254", 0x18),
		},
#endif
};

static struct i2c_gpio_platform_data codina_gpio_i2c8_data = {
	.sda_pin = COMP_SDA_CODINA_R0_0,
	.scl_pin = COMP_SCL_CODINA_R0_0,
	.udelay = 3,	/* closest to 400KHz */
};

static struct platform_device codina_gpio_i2c8_pdata = {
	.name = "i2c-gpio",
	.id = 8,
	.dev = {
		.platform_data = &codina_gpio_i2c8_data,
	},
};

#if defined(CONFIG_SENSORS_HSCD) || defined(CONFIG_SENSORS_ACCEL) || defined(CONFIG_SENSORS_HSCDTD008A)
static struct platform_device alps_pdata = {
	.name = "alps-input",
	.id = -1,
};
#endif

static struct i2c_board_info __initdata codina_r0_0_gpio_i2c8_devices[] = {
#ifdef CONFIG_SENSORS_HSCD
		{
		/* ALPS Magnetometer driver */
		I2C_BOARD_INFO("hscd_i2c", 0x0c),

		},
#endif
#ifdef CONFIG_SENSORS_HSCDTD008A
	{
		/* ALPS magnetometer HSCDTD008A */
		I2C_BOARD_INFO("hscd_i2c", 0x0c),
	},
#endif
};

#ifdef CONFIG_KEYBOARD_GPIO
struct gpio_keys_button codina_r0_0_gpio_keys[] = {
	{
	.code = KEY_HOMEPAGE,		/* input event code (KEY_*, SW_*) */
	.gpio = HOME_KEY_CODINA_R0_0,
	.active_low = 0,
	.desc = "home_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 1,		/* configure the button as a wake-up source */
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
	{
	.code = KEY_VOLUMEUP,		/* input event code (KEY_*, SW_*) */
	.gpio = VOL_UP_CODINA_R0_0,
	.active_low = 1,
	.desc = "volup_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 0,
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
	{
	.code = KEY_VOLUMEDOWN,		/* input event code (KEY_*, SW_*) */
	.gpio = VOL_DOWN_CODINA_R0_0,
	.active_low = 1,
	.desc = "voldown_key",
	.type = EV_KEY,		/* input event type (EV_KEY, EV_SW) */
	.wakeup = 0,		/* configure the button as a wake-up source */
	.debounce_interval = 30,	/* debounce ticks interval in msecs */
	.can_disable = false,
	},
};

struct gpio_keys_platform_data codina_r0_0_gpio_data = {
	.buttons = codina_r0_0_gpio_keys,
	.nbuttons = ARRAY_SIZE(codina_r0_0_gpio_keys),
};

struct platform_device codina_gpio_keys_device = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &codina_r0_0_gpio_data,
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
/* soonyong.cho : Variables for samsung composite
		such as kies, mtp, ums, etc... */
/* kies mode */
static char *usb_functions_acm_mtp[] = {
	"mtp",
	"acm",
};

#ifdef CONFIG_USB_ANDROID_ECM /* Temp !! will  be deleted 2011.04.12 */
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
/* soonyong.cho :	Every function driver for samsung composite.
 *			Number of enable function features have to
 *			be same as below.
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
	/* soonyong.cho : Please modify below value correctly
	if you customize composite */
#ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE /* USE DEVGURU HOST DRIVER */
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_acm_mtp_adb),
		.functions	= usb_functions_acm_mtp_adb,
		.bDeviceClass		= 0xEF,
		.bDeviceSubClass	= 0x02,
		.bDeviceProtocol	= 0x01,
		.s			= ANDROID_DEBUG_CONFIG_STRING,
		.mode			= USBSTATUS_ADB,
	},
	{
		.product_id		= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_acm_mtp),
		.functions		= usb_functions_acm_mtp,
		.bDeviceClass		= 0xEF,
		.bDeviceSubClass	= 0x02,
		.bDeviceProtocol	= 0x01,
		.s			= ANDROID_KIES_CONFIG_STRING,
		.mode			= USBSTATUS_SAMSUNG_KIES,
	},
	{
		.product_id		= SAMSUNG_UMS_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_ums),
		.functions		= usb_functions_ums,
		.bDeviceClass		= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
		.s			= ANDROID_UMS_CONFIG_STRING,
		.mode			= USBSTATUS_UMS,
	},
	{
		.product_id		= SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_rndis),
		.functions		= usb_functions_rndis,
#ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
		.bDeviceClass		= 0xEF,
		.bDeviceSubClass	= 0x02,
		.bDeviceProtocol	= 0x01,
#else
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass		= USB_CLASS_WIRELESS_CONTROLLER,
#else
		.bDeviceClass		= USB_CLASS_COMM,
#endif
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
#endif
		.s			= ANDROID_RNDIS_CONFIG_STRING,
		.mode			= USBSTATUS_VTP,
	},
#ifdef CONFIG_USB_ANDROID_PHONET
	{
		.product_id		= SAMSUNG_PHONET_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_phonet),
		.functions		= usb_functions_phonet,
		.bDeviceClass		= USB_CLASS_CDC_DATA,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
		.s			= ANDROID_PHONET_CONFIG_STRING,
		.mode			= USBSTATUS_PHONET,
	},
#endif
	{
		.product_id		= SAMSUNG_MTP_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_mtp),
		.functions		= usb_functions_mtp,
		.bDeviceClass		= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0x01,
		.s			= ANDROID_MTP_CONFIG_STRING,
		.mode			= USBSTATUS_MTPONLY,
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
		.bDeviceClass		= USB_CLASS_COMM,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
		.s			= ANDROID_DEBUG_CONFIG_STRING,
		.mode			= USBSTATUS_ADB,
	},
	{
		.product_id		= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_acm_mtp),
		.functions		= usb_functions_acm_mtp,
		.bDeviceClass		= USB_CLASS_COMM,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
		.s			= ANDROID_KIES_CONFIG_STRING,
		.mode			= USBSTATUS_SAMSUNG_KIES,
	},
	{
		.product_id		= SAMSUNG_UMS_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_ums),
		.functions		= usb_functions_ums,
		.bDeviceClass		= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
		.s			= ANDROID_UMS_CONFIG_STRING,
		.mode			= USBSTATUS_UMS,
	},
	{
		.product_id		= SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_rndis),
		.functions		= usb_functions_rndis,
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass		= USB_CLASS_WIRELESS_CONTROLLER,
#else
		.bDeviceClass		= USB_CLASS_COMM,
#endif
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0,
		.s			= ANDROID_RNDIS_CONFIG_STRING,
		.mode			= USBSTATUS_VTP,
	},
	{
		.product_id		= SAMSUNG_MTP_PRODUCT_ID,
		.num_functions		= ARRAY_SIZE(usb_functions_mtp),
		.functions		= usb_functions_mtp,
		.bDeviceClass		= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass	= 0,
		.bDeviceProtocol	= 0x01,
		.s			= ANDROID_MTP_CONFIG_STRING,
		.mode			= USBSTATUS_MTPONLY,
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
	.vendorID	= SAMSUNG_VENDOR_ID,	/* PHONET_VENDOR_ID */
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
static struct nmk_i2c_controller codina_i2c##id##_data = { \
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

/*
 * SSP
 */
#define NUM_SPI_CLIENTS 1
static struct pl022_ssp_controller codina_spi0_data = {
	.bus_id		= SPI023_0_CONTROLLER,
	.num_chipselect	= NUM_SPI_CLIENTS,
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias		= "pri_lcd_spi",
		.max_speed_hz		= 1200000,
		.bus_num		= SPI023_0_CONTROLLER,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
		.controller_data	= (void *)LCD_CSX_CODINA_R0_0,
	},
};

static struct spi_gpio_platform_data codina_spi_gpio_data = {
	.sck		= LCD_CLK_CODINA_R0_0,	/* LCD_CLK */
	.mosi		= LCD_SDI_CODINA_R0_0,	/* LCD_SDI */
	.miso		= LCD_SDO_CODINA_R0_0,	/* LCD_SDO */
	.num_chipselect	= 2,
};


static struct platform_device ux500_spi_gpio_device = {
	.name	= "spi_gpio",
	.id	= SPI023_0_CONTROLLER,
	.dev	= {
		.platform_data = &codina_spi_gpio_data,
	},
};


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
	.config_reg = {0xFF, 0xFF, 0x81, 0xFD, 0x7A, 0x02, 0x03},

	/* initial_pin_direction allows for the initial GPIO direction to
	 * be set.
	 */
	.config_direction = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

	/*
	 * initial_pin_pullups allows for the intial configuration of the
	 * GPIO pullup/pulldown configuration.
	 */
	.config_pullups = {0xE0, 0x1F, 0x00, 0x00, 0x80, 0x00},
};

static struct ab8500_gpio_platform_data ab8505_gpio_pdata = {
	.gpio_base		= AB8500_PIN_GPIO(1),
	.irq_base		= MOP500_AB8500_VIR_GPIO_IRQ_BASE,
	/*
	 * config_reg is the initial configuration of ab8505 pins.
	 * The pins can be configured as GPIO or alt functions based
	 * on value present in GpioSel1 to GpioSel6 and AlternatFunction
	 * register. This is the array of 8 configuration settings.
	 * One has to compile time decide these settings. Below is the
	 * explanation of these setting
	 * GpioSel1 = 0x0F => Pin GPIO1 (SysClkReq2)
	 *                    Pin GPIO2 (SysClkReq3)
	 *                    Pin GPIO3 (SysClkReq4)
	 *                    Pin GPIO4 (SysClkReq6) are configured as GPIO
	 * GpioSel2 = 0x26 => Pins GPIO10,11 HW_REV_MOD_0,1
	 * 		      Pin GPIO13, IF_TXD
	 *                    Pin GPIO14, NC
	 *                    Pins GPIO15,16 NotAvail
	 * GpioSel3 = 0x00 => Pins GPIO17-20 AD_Data2, DA_Data2, Fsync2, BitClk2
         *                    Pins GPIO21-24 NA
	 * GpioSel4 = 0x00 => Pins GPIO25, 27-32 NotAvail
	 * GpioSel5 = 0x02 => Pin GPIO34 (ExtCPEna) NC
	 *		      Pin GPIO40 (ModScl) I2C_MODEM_SCL
	 * GpioSel6 = 0x00 => Pin GPIO41 (ModSda) I2C_MODEM_SDA
         *                    Pin GPIO42 NotAvail
	 * GpioSel7 = 0x00 => Pin GPIO50, IF_RXD
         *                    Pins GPIO51 & 60 NotAvail
         *                    Pin GPIO52 (RestHW) RST_AB8505
         *                    Pin GPIO53 (Service) Service_AB8505
	 * AlternatFunction = 0x0C => GPIO13, 50 UartTX, RX
	 *
	 */
	.config_reg     = {0x0F, 0x26, 0x00, 0x00, 0x02, 0x00, 0x00, 0x0C},

	/*
	 * config_direction allows for the initial GPIO direction to
	 * be set.
	 */
	.config_direction  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

	/*
	 * config_pullups allows for the intial configuration of the
	 * GPIO pullup/pulldown configuration.
 	 * GPIO2/3(GpioPud1) = 1 and GPIO10/11(GpioPud2) = 1.
	 * GPIO13(GpioPud2) = 1 and GPIO50(GpioPud7) = 1.
	 */
	.config_pullups    = {0xE6, 0x17, 0x00, 0x00, 0x00, 0x00, 0x06},
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
		/* 1000 < adc <= 1850, default to 4 pole if it stays */
		/* in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 1850,
		.delay_ms = 20,
		.check_count = 2,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 1850, unstable zone, default to 3pole if it stays
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
		/* 83 <= adc <= 180, stable zone */
		.code		= KEY_VOLUMEUP,
		.adc_low	= 83,
		.adc_high	= 180,
	},
	{
		/* 181 <= adc <= 450, stable zone */
		.code		= KEY_VOLUMEDOWN,
		.adc_low	= 181,
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
	if (system_rev < CODINA_TMO_R0_4){ //KSND
		ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
						0x80, 0x38);
	} else {
		ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
						0x80, 0x37);
	}
	if (ret < 0)
		pr_err("%s: ab8500 write failed\n", __func__);

	/* initialise threshold for ACCDETECT2 comparator1 and comparator2 */
	ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
						0x81, 0xB3);
	if (ret < 0)
		pr_err("%s: ab8500 write failed\n", __func__);

	ret = abx500_set_register_interruptible(&pdev->dev, AB8500_ECI_AV_ACC,
						0x82, 0x33); //KSND

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
	u8 value = 0;
	int ret = 0;

	ret = abx500_get_register_interruptible(&pdev->dev, AB8500_INTERRUPT, 0x4,
		&value);
	if (ret < 0)
		return ret;

	ret = (value & 0x04) >> 2;
	pr_info("%s: ret=%x\n", __func__, ret);

	return ret;
}

struct sec_jack_platform_data sec_jack_pdata = {
	.get_adc_value = sec_jack_get_adc_value,
	.mach_init = sec_jack_mach_init,
	.get_det_level = sec_jack_get_det_level,
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

static struct ab8500_led_pwm leds_pwm_data[] = {

};


struct ab8500_pwmled_platform_data codina_pwmled_plat_data = {
	.num_pwm = 0,
	.leds = leds_pwm_data,
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
	.regulator	= &codina_ab8500_regulator_plat_data,
#ifdef CONFIG_BATTERY_SAMSUNG
	.sec_bat	= &sec_battery_pdata,
#else
	.battery	= &ab8500_bm_data,
	.charger	= &ab8500_charger_plat_data,
	.btemp		= &ab8500_btemp_plat_data,
	.fg		= &ab8500_fg_plat_data,
	.chargalg	= &ab8500_chargalg_plat_data,
#endif
	.gpio		= &ab8500_gpio_pdata,
	.sysctrl	= &ab8500_sysctrl_pdata,
//	.pwmled		= &codina_pwmled_plat_data,
#ifdef CONFIG_INPUT_AB8500_ACCDET
	.accdet = &ab8500_accdet_pdata,
#endif
#ifdef CONFIG_SAMSUNG_JACK
       .accdet = &sec_jack_pdata,
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

static struct ab8500_platform_data ab8505_platdata = {
	.irq_base	= MOP500_AB8500_IRQ_BASE,
	.regulator	= &codina_ab8505_regulator_plat_data,
#ifdef CONFIG_BATTERY_SAMSUNG
	.sec_bat = &sec_battery_pdata,
#else
	.battery	= &ab8500_bm_data,
	.charger	= &ab8500_charger_plat_data,
	.btemp		= &ab8500_btemp_plat_data,
	.fg		= &ab8500_fg_plat_data,
	.chargalg	= &ab8500_chargalg_plat_data,
#endif
	.gpio		= &ab8505_gpio_pdata,
	.sysctrl	= &ab8500_sysctrl_pdata,
//	.pwmled		= &codina_pwmled_plat_data,
#ifdef CONFIG_INPUT_AB8500_ACCDET
	.accdet = &ab8500_accdet_pdata,
#endif
#ifdef CONFIG_SAMSUNG_JACK
       .accdet = &sec_jack_pdata,
#endif
#ifdef CONFIG_PM
	.pm_power_off = true,
#endif
	.thermal_time_out = 20, /* seconds */
#ifdef CONFIG_INPUT_AB8505_MICRO_USB_DETECT
	.iddet = &iddet_adc_val_list,
#endif

};

struct platform_device ab8505_device = {
	.name = "ab8505-i2c",
	.id = 0,
	.dev = {
		.platform_data = &ab8505_platdata,
	},
	.num_resources = 1,
	.resource = ab8500_resources,
};

#ifdef CONFIG_BT_BCM4334
static struct platform_device bcm4334_bluetooth_device = {
	.name = "bcm4334_bluetooth",
	.id = -1,
};
#else
#ifndef CONFIG_BT_CG2900
static struct platform_device sec_device_rfkill = {
	.name = "bt_rfkill",
	.id = -1,
};
#endif
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
#ifdef CONFIG_BT_BCM4334
	.amba_pl011_wake_peer = bcm_bt_lpm_exit_lpm_locked,
#else
	.amba_pl011_wake_peer = NULL,
#endif
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
39,		/* (2/32) */
48,		/* (3/32) */
58,		/* (4/32) */
67,		/* (5/32) */
76,		/* (6/32) */
85,		/* (7/32) */
94,		/* (8/32) */
104,	/* (9/32) */
113,	/* (10/32) */
122,	/* (11/32) */
131,	/* (12/32) */
140,	/* (13/32) */
150,	/* (14/32) */
159,	/* (15/32) */
168,	/* (16/32) default(168,180CD)*/
178,	/* (17/32) */
183,	/* (18/32)  */
188,	/* (19/32) */
194,	/* (20/32) */
199,	/* (21/32) */
204,	/* (22/32) */
209,	/* (23/32) */
214,	/* (24/32) */
219,	/* (25/32) */
224,	/* (26/32) */
229,	/* (27/32) */
235,	/* (28/32) */
240,	/* (29/32) */
245,	/* (30/32) */
250,	/* (31/32) */
255		/* (32/32)	KTD259_MAX_CURRENT_RATIO */
};

static struct ktd259x_bl_platform_data codina_bl_platform_info = {
	.bl_name			= "pwm-backlight",
//	.ctrl_gpio				= LCD_BL_CTRL_CODINA_R0_0,	Setup moved to codina_init_machine()
	.ctrl_high			= 1,
	.ctrl_low			= 0,
	.max_brightness			= 255,
	.brightness_to_current_ratio	= ktd259CurrentRatioLookupTable,
};

static struct platform_device codina_backlight_device = {
	.name = BL_DRIVER_NAME_KTD259,
	.id = -1,
	.dev = {
		.platform_data = &codina_bl_platform_info,
	},
};
#endif

#ifdef CONFIG_LEDS_CLASS
static struct gpio_led codina_leds[] = {
	{
		.name			= "button-backlight",
		.gpio			= KEY_LED_EN_CODINA_R0_0,
		.active_low		= 0,
		.retain_state_suspended = 0,
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data codina_gpio_leds_pdata = {
	.num_leds	= ARRAY_SIZE(codina_leds),
	.leds		= codina_leds,
	.gpio_blink_set	= NULL,
};

static struct platform_device codina_gpio_leds_device = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &codina_gpio_leds_pdata,
	},
};
#endif

#ifdef CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio codina_timed_gpios[] = {
	{
		.name		= "vibrator",
		.gpio		= MOT_EN_CODINA_R0_0,
		.max_timeout	= 10000,
		.active_low	= 0,
	},
};

static struct timed_gpio_platform_data codina_timed_gpio_pdata = {
	.num_gpios	= ARRAY_SIZE(codina_timed_gpios),
	.gpios		= codina_timed_gpios,
};

static struct platform_device codina_timed_gpios_device = {
	.name = TIMED_GPIO_NAME,
	.id = -1,
	.dev = {
		.platform_data = &codina_timed_gpio_pdata,
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
#ifdef CONFIG_BT_BCM4334
	&bcm4334_bluetooth_device,
#else
#if (defined CONFIG_RFKILL && !defined CONFIG_BT_CG2900)
	&sec_device_rfkill,
#endif
#endif
#if defined(CONFIG_BACKLIGHT_KTD259)
	&codina_backlight_device,
#endif
#ifdef CONFIG_LEDS_CLASS
	&codina_gpio_leds_device,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
	&codina_timed_gpios_device,
#endif
#if defined(CONFIG_SENSORS_HSCD) || defined(CONFIG_SENSORS_ACCEL) || defined(CONFIG_SENSORS_HSCDTD008A)
	&alps_pdata,
#endif
};

static void __init codina_i2c_init(void)
{
	db8500_add_i2c0(&codina_i2c0_data);
	db8500_add_i2c1(&codina_i2c1_data);
	db8500_add_i2c2(&codina_i2c2_data);
	db8500_add_i2c3(&codina_i2c3_data);

	i2c_register_board_info(0,
		ARRAY_AND_SIZE(codina_r0_0_i2c0_devices));
	if (!use_ab8505_iddet)
		i2c_register_board_info(1,
			ARRAY_AND_SIZE(codina_r0_0_i2c1_devices));
	i2c_register_board_info(2,
		ARRAY_AND_SIZE(codina_r0_0_i2c2_devices));
	i2c_register_board_info(3,
		ARRAY_AND_SIZE(codina_r0_0_i2c3_devices));
		platform_device_register(&codina_gpio_i2c4_pdata);
	i2c_register_board_info(4,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c4_devices));
	/*platform_device_register(&codina_gpio_i2c5_pdata);
	i2c_register_board_info(5,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c5_devices));*/
	if	(system_rev >= CODINA_TMO_R0_1)	{
		platform_device_register(&codina_gpio_i2c6_pdata_01);
		i2c_register_board_info(6,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c6_devices_01));
	}	else	{
	platform_device_register(&codina_gpio_i2c6_pdata);
	i2c_register_board_info(6,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c6_devices));
	}
	platform_device_register(&codina_gpio_i2c7_pdata);
	i2c_register_board_info(7,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c7_devices));
	if	(system_rev >= CODINA_TMO_R0_1)	{
		platform_device_register(&codina_gpio_i2c8_pdata);
	i2c_register_board_info(8,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c8_devices));
	}	else	{
	i2c_register_board_info(4,
		ARRAY_AND_SIZE(codina_r0_0_gpio_i2c4_devices_r0));
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


static void __init codina_spi_init(void)
{
	db8500_add_spi0(&codina_spi0_data);
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}

static void __init codina_uart_init(void)
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


static void __init codina_init_machine(void)
{
	sec_common_init();

	sec_common_init_early();

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

	if (system_rev < CODINA_TMO_R0_4){
		codina_bl_platform_info.ctrl_gpio = LCD_BL_CTRL_CODINA_R0_0;
	}
	else {
		codina_bl_platform_info.ctrl_gpio = LCD_BL_CTRL_CODINA_R0_4;
	}

	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	ssg_pins_init();

#ifdef CONFIG_TOUCHSCREEN_ZINITIX_BT404
	bt404_ts_init();
#endif

	u8500_cryp1_hash1_init();
	codina_i2c_init();
	codina_spi_init();
	mop500_msp_init();		/* generic for now */
	codina_uart_init();

#ifdef CONFIG_STE_WLAN
	mop500_wlan_init();
#endif

#ifdef CONFIG_KEYBOARD_GPIO
	platform_device_register(&codina_gpio_keys_device);
#endif

#ifdef CONFIG_BATTERY_SAMSUNG
	sec_init_battery();
#endif
	if (system_rev >= CODINA_TMO_R0_0)
		platform_device_register(&ab8505_device);
	else
		platform_device_register(&ab8500_device);

	sec_cam_init();

	sec_common_init_post() ;

	/* This board has full regulator constraints */
	regulator_has_full_constraints();
}

static int __init jig_smd_status(char *str)
{
	if (get_option(&str, &jig_smd) != 1)
		jig_smd = 0;

	return 1;

}
__setup("jig_smd=", jig_smd_status);

static int __init sec_debug_setup(char *str)
{
	if (get_option(&str, &sec_debug_settings) != 1)
		sec_debug_settings = 0;

	return 1;
}
__setup("debug=", sec_debug_setup);

/* we have equally similar boards with very minimal
 * changes, so we detect the platform during boot
 */
static int __init board_id_setup(char *str)
{
	if (get_option(&str, &board_id) != 1)
		board_id = 0;

	use_ab8505_iddet = (board_id >= CODINA_TMO_AB8505_IDDET_VER) ? 1 : 0;

	switch (board_id) {
	case 7:
		printk(KERN_INFO "GT-I8160 Board Rev 0.0\n");
		system_rev = CODINA_R0_0;
		break;
	case 8:
		printk(KERN_INFO "GT-I8160 Board Rev 0.1\n");
		system_rev = CODINA_R0_1;
		break;
	case 9:
		printk(KERN_INFO "GT-I8160 Board Rev 0.2\n");
		system_rev = CODINA_R0_2;
		break;
	case 10:
		printk(KERN_INFO "GT-I8160 Board Rev 0.3\n");
		system_rev = CODINA_R0_3;
		break;
	case 11:
		printk(KERN_INFO "GT-I8160 Board Rev 0.4\n");
		system_rev = CODINA_R0_4;
		break;
	case 12:
		printk(KERN_INFO "GT-I8160 Board Rev 0.5\n");
		system_rev = CODINA_R0_5;
		break;
	case 0x101:
		printk(KERN_INFO "SGH-T599 Board pre-Rev 0.0\n");
		system_rev = CODINA_TMO_R0_0;
		break;
	case 0x102:
		printk(KERN_INFO "SGH-T599 Board Rev 0.0\n");
		system_rev = CODINA_TMO_R0_0_A;
		break;
	case 0x103:
		printk(KERN_INFO "SGH-T599 Board Rev 0.1\n");
		system_rev = CODINA_TMO_R0_1;
		break;
	case 0x104:
		printk(KERN_INFO "SGH-T599 Board Rev 0.2\n");
		system_rev = CODINA_TMO_R0_2;
		break;
	case 0x105:
		printk(KERN_INFO "SGH-T599 Board Rev 0.4\n");
		system_rev = CODINA_TMO_R0_4;
		break;
	case 0x106:
		printk(KERN_INFO "SGH-T599 Board Rev 0.5\n");
		system_rev = CODINA_TMO_R0_5;
		break;
	case 0x107:
		printk(KERN_INFO "SGH-T599 Board Rev 0.6\n");
		system_rev = CODINA_TMO_R0_6;
		break;
	default:
		printk(KERN_INFO "Unknown board_id=%c\n", *str);
		break;
	};

	return 1;
}
__setup("board_id=", board_id_setup);

MACHINE_START(CODINA, "SAMSUNG CODINA")
	/* Maintainer: SAMSUNG based on ST Ericsson */
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= codina_init_machine,
	.restart	= ux500_restart,
MACHINE_END
