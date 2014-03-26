/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Board data for the U8500 UIB, also known as the New UIB
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c/adp1653_plat.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/lsm303dlh.h>
#include <linux/l3g4200d.h>
#include <linux/mfd/tc3589x.h>
#include <linux/input/matrix_keypad.h>
#include <linux/input/lps001wp.h>
#include <../drivers/staging/ste_rmi4/synaptics_i2c_rmi4.h>

#include <asm/mach-types.h>
#include <linux/gpio.h>
#include <mach/irqs.h>

#include "board-mop500.h"

/*
 * LSM303DLH accelerometer + magnetometer & L3G4200D Gyroscope sensors
 */
static struct lsm303dlh_platform_data lsm303dlh_pdata_u8500 = {
	.axis_map_x = 1,
	.axis_map_y = 0,
	.axis_map_z = 2,
	.negative_x = 0,
	.negative_y = 0,
	.negative_z = 1,
};

static struct l3g4200d_gyr_platform_data l3g4200d_pdata_u8500 = {
	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,
	.negative_x = 1,
	.negative_y = 0,
	.negative_z = 1,
};

/*
 * Platform data for pressure sensor,
 * poll interval and min interval in millseconds.
 */
static struct lps001wp_prs_platform_data lps001wp_pdata = {
       .poll_interval = 1000,
       .min_interval = 10,
};

static struct adp1653_platform_data __initdata adp1653_pdata_u8500_uib = {
	.irq_no = CAMERA_FLASH_INT_PIN
};

static struct i2c_board_info __initdata mop500_i2c2_devices_u8500[] = {
	{
		/* LSM303DLH Accelerometer */
		I2C_BOARD_INFO("lsm303dlh_a", 0x18),
		.platform_data = &lsm303dlh_pdata_u8500,
	},
	{
		/* LSM303DLH Magnetometer */
		I2C_BOARD_INFO("lsm303dlh_m", 0x1E),
		.platform_data = &lsm303dlh_pdata_u8500,
	},
	{
		/* L3G4200D Gyroscope */
		I2C_BOARD_INFO("l3g4200d", 0x68),
		.platform_data = &l3g4200d_pdata_u8500,
	},
	{
		I2C_BOARD_INFO("adp1653", 0x30),
		.platform_data = &adp1653_pdata_u8500_uib
	},
	{
		/* LSP001WM Barometer */
		I2C_BOARD_INFO("lps001wp_prs", 0x5C),
		.platform_data = &lps001wp_pdata,
	},
};

/*
 * Synaptics RMI4 touchscreen interface on the U8500 UIB
 */

/*
 * Descriptor structure.
 * Describes the number of i2c devices on the bus that speak RMI.
 */
static struct synaptics_rmi4_platform_data rmi4_i2c_dev_platformdata = {
	.irq_number     = NOMADIK_GPIO_TO_IRQ(84),
	.irq_type       = (IRQF_TRIGGER_FALLING | IRQF_SHARED),
	.x_flip		= true,
	.y_flip		= false,
	.regulator_en   = true,
};

static struct i2c_board_info __initdata mop500_i2c3_devices_u8500[] = {
	{
		I2C_BOARD_INFO("synaptics_rmi4_i2c", 0x4B),
		.platform_data = &rmi4_i2c_dev_platformdata,
	},
};

/*
 * TC35893
 */
static const unsigned int u8500_keymap[] = {
	KEY(3, 1, KEY_END),
	KEY(4, 1, KEY_HOME),
	KEY(6, 4, KEY_VOLUMEDOWN),
	KEY(4, 2, KEY_EMAIL),
	KEY(3, 3, KEY_RIGHT),
	KEY(2, 5, KEY_BACKSPACE),

	KEY(6, 7, KEY_MENU),
	KEY(5, 0, KEY_ENTER),
	KEY(4, 3, KEY_0),
	KEY(3, 4, KEY_DOT),
	KEY(5, 2, KEY_UP),
	KEY(3, 5, KEY_DOWN),

	KEY(4, 5, KEY_SEND),
	KEY(0, 5, KEY_BACK),
	KEY(6, 2, KEY_VOLUMEUP),
	KEY(1, 3, KEY_SPACE),
	KEY(7, 6, KEY_LEFT),
	KEY(5, 5, KEY_SEARCH),
};

static struct matrix_keymap_data u8500_keymap_data = {
	.keymap		= u8500_keymap,
	.keymap_size    = ARRAY_SIZE(u8500_keymap),
};

static struct tc3589x_keypad_platform_data tc35893_data = {
	.krow = TC_KPD_ROWS,
	.kcol = TC_KPD_COLUMNS,
	.debounce_period = TC_KPD_DEBOUNCE_PERIOD,
	.settle_time = TC_KPD_SETTLE_TIME,
	.irqtype = IRQF_TRIGGER_FALLING,
	.enable_wakeup = true,
	.keymap_data    = &u8500_keymap_data,
	.no_autorepeat  = true,
};

static struct tc3589x_platform_data tc3589x_keypad_data = {
	.block = TC3589x_BLOCK_KEYPAD,
	.keypad = &tc35893_data,
	.irq_base = MOP500_EGPIO_IRQ_BASE,
};

static struct i2c_board_info __initdata mop500_i2c0_devices_u8500[] = {
	{
		I2C_BOARD_INFO("tc3589x", 0x44),
		.platform_data = &tc3589x_keypad_data,
		.irq = NOMADIK_GPIO_TO_IRQ(218),
		.flags = I2C_CLIENT_WAKE,
	},
};

void __init mop500_u8500uib_init(void)
{
	int ret;

	mop500_uib_i2c_add(3, mop500_i2c3_devices_u8500,
			ARRAY_SIZE(mop500_i2c3_devices_u8500));

	mop500_uib_i2c_add(0, mop500_i2c0_devices_u8500,
			ARRAY_SIZE(mop500_i2c0_devices_u8500));

	if (machine_is_hrefv60() || machine_is_u8520()) {
		lsm303dlh_pdata_u8500.irq_a1 = HREFV60_ACCEL_INT1_GPIO;
		lsm303dlh_pdata_u8500.irq_a2 = HREFV60_ACCEL_INT2_GPIO;
		lsm303dlh_pdata_u8500.irq_m = HREFV60_MAGNET_DRDY_GPIO;
		adp1653_pdata_u8500_uib.enable_gpio =
					HREFV60_CAMERA_FLASH_ENABLE;
	} else {
		lsm303dlh_pdata_u8500.irq_a1 = GPIO_ACCEL_INT1;
		lsm303dlh_pdata_u8500.irq_a2 = GPIO_ACCEL_INT2;
		lsm303dlh_pdata_u8500.irq_m = GPIO_MAGNET_DRDY;
		adp1653_pdata_u8500_uib.enable_gpio =
					GPIO_CAMERA_FLASH_ENABLE;
	}
	ret = mop500_get_acc_id();
	if (ret < 0)
		printk(KERN_ERR " Failed to get Accelerometr chip ID\n");
	else
		lsm303dlh_pdata_u8500.chip_id = ret;
	mop500_uib_i2c_add(2, mop500_i2c2_devices_u8500,
			ARRAY_SIZE(mop500_i2c2_devices_u8500));
}
