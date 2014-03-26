/*
 * Copyright (C) 2011 ST-Ericsson SA
 * Author: Avinash A <avinash.a@stericsson.com> for ST-Ericsson
 * License terms:GNU General Public License (GPL) version 2
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/lsm303dlh.h>
#include <linux/l3g4200d.h>
#include <linux/cyttsp.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c/adp1653_plat.h>
#include <linux/input/matrix_keypad.h>
#include <linux/input/lps331ap.h>
#include <linux/gpio_keys.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/amba/pl022.h>
#include <asm/mach-types.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include <plat/i2c.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/irqs-db8500.h>
#include "pins-db8500.h"
#include "board-mop500.h"
#include "devices-db8500.h"
#include "pins.h"

#define NUM_SSP_CLIENTS 10

/*
 * LSM303DLH accelerometer + magnetometer & L3G4200D Gyroscope sensors
 */
static struct lsm303dlh_platform_data __initdata lsm303dlh_pdata_u9540 = {
	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,
	.negative_x = 1,
	.negative_y = 1,
	.negative_z = 0,
};

static struct l3g4200d_gyr_platform_data  __initdata l3g4200d_pdata_u9540 = {
	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,
	.negative_x = 1,
	.negative_y = 0,
	.negative_z = 1,
};

static struct lps331ap_platform_data __initdata lps331ap_pdata = {
	.irq = 0,
};

static struct adp1653_platform_data __initdata adp1653_pdata_u9540_uib = {
	.irq_no = CCU9540_CAMERA_FLASH_READY
};

static struct i2c_board_info __initdata mop500_i2c2_devices_u9540[] = {
	{
		/* LSM303DLH Accelerometer */
		I2C_BOARD_INFO("lsm303dlh_a", 0x19),
		.platform_data = &lsm303dlh_pdata_u9540,
	},
	{
		/* LSM303DLH Magnetometer */
		I2C_BOARD_INFO("lsm303dlh_m", 0x1E),
		.platform_data = &lsm303dlh_pdata_u9540,
	},
	{
		/* L3G4200D Gyroscope */
		I2C_BOARD_INFO("l3g4200d", 0x68),
		.platform_data = &l3g4200d_pdata_u9540,
	},
	{
		/* LPS331AP Barometer */
		I2C_BOARD_INFO("lps331ap", 0x5C),
		.platform_data = &lps331ap_pdata,
	},
	{
		I2C_BOARD_INFO("adp1653", 0x30),
		.platform_data = &adp1653_pdata_u9540_uib
	}
};

static struct i2c_board_info __initdata mop500_i2c2_devices_tablet_v1[] = {
	{
		I2C_BOARD_INFO("dsi2lvds_i2c", 0x0F),
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

	ret = gpio_request(CYPRESS_TOUCH_9540_INT_PIN, "Wakeup_pin");
	if (ret < 0) {
		pr_err("touch gpio failed\n");
		return ret;
	}
	ret = gpio_direction_output(CYPRESS_TOUCH_9540_INT_PIN, 1);
	if (ret < 0) {
		pr_err("touch gpio direction failed\n");
		goto out;
	}
	gpio_set_value(CYPRESS_TOUCH_9540_INT_PIN, 0);
	gpio_set_value(CYPRESS_TOUCH_9540_INT_PIN, 1);
	/*
	 * To wake up the controller from sleep
	 * state the interrupt pin needs to be
	 * pulsed twice with a delay greater
	 * than 2 micro seconds.
	 */
	udelay(3);
	gpio_set_value(CYPRESS_TOUCH_9540_INT_PIN, 0);
	gpio_set_value(CYPRESS_TOUCH_9540_INT_PIN, 1);
	ret = gpio_direction_input(CYPRESS_TOUCH_9540_INT_PIN);
	if (ret < 0) {
		pr_err("touch gpio direction IN config failed\n");
		goto out;
	}
out:
	gpio_free(CYPRESS_TOUCH_9540_INT_PIN);
	return 0;
}

struct cyttsp_platform_data cyttsp_9540_platdata = {
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
	.irq_gpio = CYPRESS_TOUCH_9540_INT_PIN,
	.rst_gpio = CYPRESS_TOUCH_9540_RST_GPIO,
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

static struct spi_board_info cypress_spi_9540_devices[] = {
	{
		.modalias = CY_SPI_NAME,
		.controller_data = &cyttsp_ssp_config_chip,
		.platform_data = &cyttsp_9540_platdata,
		.max_speed_hz = 1000000,
		.bus_num = SPI023_2_CONTROLLER,
		.chip_select = 0,
		.mode = SPI_MODE_0,
	}
};

static UX500_PINS(ccu9540_pins_spi2,
	GPIO216_GPIO | PIN_OUTPUT_HIGH |
		PIN_SLPM_GPIO | PIN_SLPM_OUTPUT_HIGH,
	GPIO218_SPI2_RXD | PIN_INPUT_PULLDOWN |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_PULLDOWN,
	GPIO215_SPI2_TXD | PIN_OUTPUT_LOW |
		PIN_SLPM_GPIO | PIN_SLPM_OUTPUT_LOW,
	GPIO217_SPI2_CLK | PIN_OUTPUT_LOW |
		PIN_SLPM_GPIO | PIN_SLPM_OUTPUT_LOW,
);

static UX500_PINS(ccu9540_pins_i2c3,
	GPIO216_I2C3_SDA | PIN_INPUT_NOPULL,
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO218_I2C3_SCL | PIN_INPUT_NOPULL,
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
);

static struct ux500_pin_lookup pins_uibsv1[] = {
	PIN_LOOKUP("spi2", &ccu9540_pins_spi2),
};

static struct ux500_pin_lookup pins_uibsv2[] = {
	PIN_LOOKUP("nmk-i2c.3", &ccu9540_pins_i2c3),
};

static struct i2c_board_info __initdata i2c3_devices_uibsv2[] = {
	{
		/* STMT05 touchscreen */
		I2C_BOARD_INFO("ftk", 0x4b),
		.irq = NOMADIK_GPIO_TO_IRQ(146),
	},
};

static struct nmk_i2c_controller i2c3_data = {
	.slsu = 0xe,
	.tft = 1,
	.rft = 8,
	.clk_freq = 400000,
	.timeout = 200,
	.sm = I2C_FREQ_MODE_FAST,
};

void mop500_u9540_cyttsp_init(void)
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
	spi_register_board_info(cypress_spi_9540_devices,
			ARRAY_SIZE(cypress_spi_9540_devices));
}

#ifdef CONFIG_UX500_GPIO_KEYS
static struct gpio_keys_button gpio_keys[] = {
	{
		.desc = "SFH7741 Proximity Sensor",
		.type = EV_SW,
		.code = SW_FRONT_PROXIMITY,
		.active_low = 0,
		.can_disable = 1,
		.gpio = 229,
	},
};

static struct regulator *gpio_keys_regulator;
static int gpio_keys_activate(struct device *dev);
static void gpio_keys_deactivate(struct device *dev);

static struct gpio_keys_platform_data gpio_keys_data = {
	.buttons = gpio_keys,
	.nbuttons = ARRAY_SIZE(gpio_keys),
	.enable = gpio_keys_activate,
	.disable = gpio_keys_deactivate,
};

static struct platform_device gpio_keys_device = {
	.name = "gpio-keys",
	.id = 0,
	.dev = {
		.platform_data = &gpio_keys_data,
	},
};

static int gpio_keys_activate(struct device *dev)
{
	gpio_keys_regulator = regulator_get(&gpio_keys_device.dev, "vcc");
	if (IS_ERR(gpio_keys_regulator)) {
		dev_err(&gpio_keys_device.dev, "no regulator\n");
		return PTR_ERR(gpio_keys_regulator);
	}
	regulator_enable(gpio_keys_regulator);

	/*
	 * Please be aware that the start-up time of the SFH7741 is
	 * 120 ms and during that time the output is undefined.
	 */
	return 0;
}

static void gpio_keys_deactivate(struct device *dev)
{
	if (!IS_ERR(gpio_keys_regulator)) {
		regulator_disable(gpio_keys_regulator);
		regulator_put(gpio_keys_regulator);
	}
}

static __init void gpio_keys_init(void)
{
	struct ux500_pins *gpio_keys_pins = ux500_pins_get("gpio-keys.0");

	if (gpio_keys_pins == NULL) {
		pr_err("gpio_keys: Failed to get pins\n");
		return;
	}

	ux500_pins_enable(gpio_keys_pins);
}
#else
static inline void gpio_keys_init(void) { }
#endif

/* UIB specific platform devices */
static struct platform_device *uibs_v1_platform_devs[] __initdata = {
#ifdef CONFIG_UX500_GPIO_KEYS
	&gpio_keys_device,
#endif
};

void __init mop500_u9540uibs_v1_init(void)
{
	ux500_pins_add(pins_uibsv1, ARRAY_SIZE(pins_uibsv1));

	mop500_u9540_cyttsp_init();
	db8500_add_spi2(&mop500_spi2_data);

	lsm303dlh_pdata_u9540.irq_a1 = HREFV60_ACCEL_INT1_GPIO;
	lsm303dlh_pdata_u9540.irq_a2 = HREFV60_ACCEL_INT2_GPIO;
	/* lsm303dlh_pdata_u9540.irq_m = HREFV60_MAGNET_DRDY_GPIO;*/
	adp1653_pdata_u9540_uib.enable_gpio = CCU9540_CAMERA_FLASH_EN1;

	mop500_uib_i2c_add(2, mop500_i2c2_devices_u9540,
			ARRAY_SIZE(mop500_i2c2_devices_u9540));

	gpio_keys_init();
	platform_add_devices(uibs_v1_platform_devs,
				ARRAY_SIZE(uibs_v1_platform_devs));
}

void __init mop500_u9540uibs_v2_init(void)
{
	ux500_pins_add(pins_uibsv2, ARRAY_SIZE(pins_uibsv2));
	db8500_add_i2c3(&i2c3_data);

	lsm303dlh_pdata_u9540.irq_a1 = HREFV60_ACCEL_INT1_GPIO;
	lsm303dlh_pdata_u9540.irq_a2 = HREFV60_ACCEL_INT2_GPIO;
/*	lsm303dlh_pdata_u9540.irq_m = HREFV60_MAGNET_DRDY_GPIO;*/
	adp1653_pdata_u9540_uib.enable_gpio = CCU9540_CAMERA_FLASH_EN1;

	mop500_uib_i2c_add(2, mop500_i2c2_devices_u9540,
			ARRAY_SIZE(mop500_i2c2_devices_u9540));
	mop500_uib_i2c_add(3, i2c3_devices_uibsv2,
			ARRAY_SIZE(i2c3_devices_uibsv2));
}

void __init mop500_u9540uibt_v1_init(void)
{
	mop500_uib_i2c_add(2, mop500_i2c2_devices_tablet_v1,
				ARRAY_SIZE(mop500_i2c2_devices_tablet_v1));
}
