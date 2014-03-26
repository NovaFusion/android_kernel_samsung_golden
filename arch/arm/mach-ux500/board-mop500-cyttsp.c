/*
 * Copyright (C) 2011 ST-Ericsson SA
 * Author: Avinash A <avinash.a@stericsson.com> for ST-Ericsson
 * License terms:GNU General Public License (GPL) version 2
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/cyttsp.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c/adp1653_plat.h>
#include <linux/input/matrix_keypad.h>
#include <linux/input/lps001wp.h>
#include <linux/mfd/tc3589x.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/amba/pl022.h>
#include <linux/lsm303dlh.h>
#include <linux/l3g4200d.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/irqs-db8500.h>
#include <asm/mach-types.h>
#include "pins-db8500.h"
#include "board-mop500.h"
#include "devices-db8500.h"

#define NUM_SSP_CLIENTS 10

/*
 * LSM303DLH accelerometer + magnetometer & L3G4200D Gyroscope sensors
 */
static struct lsm303dlh_platform_data lsm303dlh_pdata_u8500_r3 = {
	.axis_map_x = 1,
	.axis_map_y = 0,
	.axis_map_z = 2,
	.negative_x = 0,
	.negative_y = 0,
	.negative_z = 1,
};

static struct l3g4200d_gyr_platform_data l3g4200d_pdata_u8500_r3 = {
	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,
	.negative_x = 1,
	.negative_y = 0,
	.negative_z = 1,
};

static struct adp1653_platform_data __initdata adp1653_pdata_u8500_uib = {
	.irq_no = CAMERA_FLASH_INT_PIN
};

/*
 * Platform data for pressure sensor,
 * poll interval and min interval in millseconds.
 */
static struct lps001wp_prs_platform_data lps001wp_pdata_r3 = {
	.poll_interval = 1000,
	.min_interval = 10,
};

static struct i2c_board_info __initdata mop500_i2c2_devices_u8500_r3[] = {
	{
		/* LSM303DLH Accelerometer */
		I2C_BOARD_INFO("lsm303dlhc_a", 0x19),
		.platform_data = &lsm303dlh_pdata_u8500_r3,
	},
	{
		/* LSM303DLH Magnetometer */
		I2C_BOARD_INFO("lsm303dlh_m", 0x1E),
		.platform_data = &lsm303dlh_pdata_u8500_r3,
	},
	{
		/* L3G4200D Gyroscope */
		I2C_BOARD_INFO("l3g4200d", 0x68),
		.platform_data = &l3g4200d_pdata_u8500_r3,
	},
	{
		/* LSP001WM Barometer */
		I2C_BOARD_INFO("lps001wp_prs", 0x5C),
		.platform_data = &lps001wp_pdata_r3,
	},
	{
		I2C_BOARD_INFO("adp1653", 0x30),
		.platform_data = &adp1653_pdata_u8500_uib
	}
};

/* cyttsp_gpio_board_init : configures the touch panel. */
static int cyttsp_plat_init(int on)
{
	int ret;

	ret = gpio_direction_output(CYPRESS_SLAVE_SELECT_GPIO, 1);
	if (ret < 0) {
		pr_err("slave select gpio direction failed\n");
		gpio_free(CYPRESS_SLAVE_SELECT_GPIO);
		return ret;
	}
	return 0;
}

static struct pl022_ssp_controller mop500_spi2_data = {
	.bus_id         = SPI023_2_CONTROLLER,
	.num_chipselect = NUM_SSP_CLIENTS,
};

static int cyttsp_wakeup(void)
{
	int ret;

	ret = gpio_request(CYPRESS_TOUCH_INT_PIN, "Wakeup_pin");
	if (ret < 0) {
		pr_err("touch gpio failed\n");
		return ret;
	}
	ret = gpio_direction_output(CYPRESS_TOUCH_INT_PIN, 1);
	if (ret < 0) {
		pr_err("touch gpio direction failed\n");
		goto out;
	}
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 0);
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 1);
	/*
	 * To wake up the controller from sleep
	 * state the interrupt pin needs to be
	 * pulsed twice with a delay greater
	 * than 2 micro seconds.
	 */
	udelay(3);
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 0);
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 1);
	ret = gpio_direction_input(CYPRESS_TOUCH_INT_PIN);
	if (ret < 0) {
		pr_err("touch gpio direction IN config failed\n");
		goto out;
	}
out:
	gpio_free(CYPRESS_TOUCH_INT_PIN);
	return 0;
}
struct cyttsp_platform_data cyttsp_platdata = {
	.maxx = 480,
	.maxy = 854,
	.flags = 0,
	.gen = CY_GEN3,
	.use_st = 0,
	.use_mt = 1,
	.use_trk_id = 0,
	.use_hndshk = 0,
	.use_sleep = 1,
	.use_gestures = 0,
	.use_load_file = 0,
	.use_force_fw_update = 0,
	.use_virtual_keys = 0,
	/* activate up to 4 groups and set active distance */
	.gest_set = CY_GEST_GRP_NONE | CY_ACT_DIST,
	/* change scn_type to enable finger and/or stylus detection */
	.scn_typ = 0xA5, /* autodetect finger+stylus; balanced mutual scan */
	.act_intrvl = CY_ACT_INTRVL_DFLT,  /* Active refresh interval; ms */
	.tch_tmout = CY_TCH_TMOUT_DFLT,   /* Active touch timeout; ms */
	.lp_intrvl = CY_LP_INTRVL_DFLT,   /* Low power refresh interval; ms */
	.init = cyttsp_plat_init,
	.mt_sync = input_mt_sync,
	.wakeup = cyttsp_wakeup,
	.name = CY_SPI_NAME,
	.irq_gpio = CYPRESS_TOUCH_INT_PIN,
	.rst_gpio = CYPRESS_TOUCH_RST_GPIO,
};

static void cyttsp_spi_cs_control(u32 command)
{
	if (command == SSP_CHIP_SELECT)
		gpio_set_value(CYPRESS_SLAVE_SELECT_GPIO, 0);
	else if (command == SSP_CHIP_DESELECT)
		gpio_set_value(CYPRESS_SLAVE_SELECT_GPIO, 1);
}

static struct pl022_config_chip cyttsp_ssp_config_chip = {
	.com_mode = INTERRUPT_TRANSFER,
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	/* we can act as master only */
	.hierarchy = SSP_MASTER,
	.slave_tx_disable = 0,
	.rx_lev_trig = SSP_RX_1_OR_MORE_ELEM,
	.tx_lev_trig = SSP_TX_16_OR_MORE_EMPTY_LOC,
	.ctrl_len = SSP_BITS_16,
	.wait_state = SSP_MWIRE_WAIT_ZERO,
	.duplex = SSP_MICROWIRE_CHANNEL_FULL_DUPLEX,
	.cs_control = cyttsp_spi_cs_control,
};

static struct spi_board_info cypress_spi_devices[] = {
	{
		.modalias = CY_SPI_NAME,
		.controller_data = &cyttsp_ssp_config_chip,
		.platform_data = &cyttsp_platdata,
		.max_speed_hz = 1000000,
		.bus_num = SPI023_2_CONTROLLER,
		.chip_select = 0,
		.mode = SPI_MODE_0,
	}
};

/*
 * TC35893
 */
static const unsigned int sony_keymap[] = {
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

static struct matrix_keymap_data sony_keymap_data = {
	.keymap		= sony_keymap,
	.keymap_size    = ARRAY_SIZE(sony_keymap),
};

static struct tc3589x_keypad_platform_data tc35893_data = {
	.krow = TC_KPD_ROWS,
	.kcol = TC_KPD_COLUMNS,
	.debounce_period = TC_KPD_DEBOUNCE_PERIOD,
	.settle_time = TC_KPD_SETTLE_TIME,
	.irqtype = IRQF_TRIGGER_FALLING,
	.enable_wakeup = true,
	.keymap_data    = &sony_keymap_data,
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
		.irq = NOMADIK_GPIO_TO_IRQ(64),
		.flags = I2C_CLIENT_WAKE,
	},
};

void mop500_cyttsp_init(void)
{
	int ret = 0;

	/*
	 * Enable the alternative C function
	 * in the PRCMU register
	 */
	prcmu_enable_spi2();
	ret = gpio_request(CYPRESS_SLAVE_SELECT_GPIO, "slave_select_gpio");
	if (ret < 0)
		pr_err("slave select gpio failed\n");
	spi_register_board_info(cypress_spi_devices,
			ARRAY_SIZE(cypress_spi_devices));
}

void __init mop500_u8500uib_r3_init(void)
{
	int ret;
	mop500_cyttsp_init();
	db8500_add_spi2(&mop500_spi2_data);
	nmk_config_pin((GPIO64_GPIO     | PIN_INPUT_PULLUP), false);
	if (machine_is_hrefv60() || machine_is_u8520()) {
		lsm303dlh_pdata_u8500_r3.irq_a1 = HREFV60_ACCEL_INT1_GPIO;
		lsm303dlh_pdata_u8500_r3.irq_a2 = HREFV60_ACCEL_INT2_GPIO;
		lsm303dlh_pdata_u8500_r3.irq_m = HREFV60_MAGNET_DRDY_GPIO;
		adp1653_pdata_u8500_uib.enable_gpio =
					HREFV60_CAMERA_FLASH_ENABLE;
	} else {
		lsm303dlh_pdata_u8500_r3.irq_a1 = GPIO_ACCEL_INT1;
		lsm303dlh_pdata_u8500_r3.irq_a2 = GPIO_ACCEL_INT2;
		lsm303dlh_pdata_u8500_r3.irq_m = GPIO_MAGNET_DRDY;
		adp1653_pdata_u8500_uib.enable_gpio =
					GPIO_CAMERA_FLASH_ENABLE;
	}
	mop500_uib_i2c_add(0, mop500_i2c0_devices_u8500,
			ARRAY_SIZE(mop500_i2c0_devices_u8500));
	mop500_uib_i2c_add(0, mop500_i2c0_devices_u8500,
			ARRAY_SIZE(mop500_i2c0_devices_u8500));

	ret = mop500_get_acc_id();
	if (ret < 0)
		printk(KERN_ERR " Failed to get Accelerometr chip ID\n");
	else
		lsm303dlh_pdata_u8500_r3.chip_id = ret;
	mop500_uib_i2c_add(2, mop500_i2c2_devices_u8500_r3,
			ARRAY_SIZE(mop500_i2c2_devices_u8500_r3));
}
