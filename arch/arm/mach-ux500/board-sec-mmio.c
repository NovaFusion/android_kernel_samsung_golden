/*
 * Copyright (C) 2011 ST-Ericsson
 * Author: Rajat Verma <rajat.verma@stericsson.com> for ST-Ericsson.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/mmio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <asm/mach-types.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include <mach/gpio.h>
#include <mach/devices.h>
#include <mach/hardware.h>

#include "pins-db8500.h"
#include "pins.h"
#include <mach/board-sec-u8500.h>
#include <linux/mmio.h>

#define HREFV60_MMIO_XENON_CHARGE 170
#define HREFV60_XSHUTDOWN_SECONDARY_SENSOR 140

#if defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_JANICE) || defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_GAVINI)
#define XSHUTDOWN_PRIMARY_SENSOR 142
#define XSHUTDOWN_SECONDARY_SENSOR 64
#define RESET_PRIMARY_SENSOR	149
#define RESET_SECONDARY_SENSOR	65
#define CAM_FLASH_EN	140
#define CAM_FLASH_MODE	      141
#elif defined(CONFIG_MACH_SEC_KYLE)
#define XSHUTDOWN_PRIMARY_SENSOR 142
#define XSHUTDOWN_SECONDARY_SENSOR 64
#define RESET_PRIMARY_SENSOR	149
#define RESET_SECONDARY_SENSOR	65
#else
#error "Unknown machine type"
#endif

static pin_cfg_t i2c2_pins[] = {
	GPIO8_I2C2_SDA,
	GPIO9_I2C2_SCL
};
static pin_cfg_t ipi2c_pins[] = {
	GPIO8_IPI2C_SDA,
	GPIO9_IPI2C_SCL
};
static pin_cfg_t i2c_disable_pins[] = {
	GPIO8_GPIO | PIN_OUTPUT_LOW,
	GPIO9_GPIO | PIN_OUTPUT_LOW,
};

#if 0
static pin_cfg_t xshutdown_host[] = {
	GPIO149_GPIO | PIN_OUTPUT_LOW, /* gpio disabled => set output low  */
	GPIO142_GPIO | PIN_OUTPUT_LOW, /* gpio disabled => set output low  */
	GPIO64_GPIO  | PIN_OUTPUT_LOW, /* gpio disabled => set output low  */
	GPIO65_GPIO  | PIN_OUTPUT_LOW,  /* gpio disabled => set output low  */
	GPIO140_GPIO | PIN_OUTPUT_LOW,  /*CAM_FLASH_EN*/
	GPIO141_GPIO | PIN_OUTPUT_LOW,  /*CAM_FLASH_MODE*/
};
static pin_cfg_t xshutdown_fw[] = {
	GPIO149_IP_GPIO0,
	GPIO142_IP_GPIO3,
	GPIO64_IP_GPIO4,
	GPIO65_IP_GPIO5
};
static pin_cfg_t xshutdown_disable[] = {
    GPIO149_GPIO | PIN_OUTPUT_LOW, /* gpio disabled => set output low  */
	GPIO142_GPIO | PIN_OUTPUT_LOW, /* gpio disabled => set output low  */
	GPIO64_GPIO  | PIN_OUTPUT_LOW, /* gpio disabled => set output low  */
	GPIO65_GPIO  | PIN_OUTPUT_LOW,  /* gpio disabled => set output low  */
	GPIO140_IP_GPIO7 | PIN_OUTPUT_LOW,
	GPIO141_IP_GPIO2 | PIN_OUTPUT_LOW,
};
#endif

struct mmio_board_data{
	int number_of_regulators;
	struct regulator **mmio_regulators;
	/* Pin configs  */
	int xenon_charge;
	struct mmio_gpio xshutdown_pins[CAMERA_SLOT_END];
	struct mmio_gpio reset_pins[CAMERA_SLOT_END];
	/* Internal clocks */
	struct clk *clk_ptr_bml;
	struct clk *clk_ptr_ipi2c;
	/* External clocks */
	struct clk *clk_ptr_ext[CAMERA_SLOT_END];
};

/* Fill names of regulators required for powering up the
 * camera sensor in below array */
#if defined(CONFIG_MACH_SEC_KYLE)
static char *regulator_names[] = {"vddcsi1v2"};
#else
static char *regulator_names[] = {"vddcsi1v2", "v_sensor_3v" , "v_sensor_1v8"};
#endif


/* This function is used to translate the physical GPIO used for reset GPIO
 * to logical IPGPIO that needs to be communicated to Firmware. so that
 * firmware can control reset GPIO of a RAW Bayer sensor */
static int mmio_get_ipgpio(struct mmio_platform_data *pdata, int gpio,
			   int *ip_gpio)
{
	int err = 0;
	dev_dbg(pdata->dev, "%s() : IPGPIO requested for %d", __func__, gpio);
	switch (gpio) {
	case 67:
	case 140:
		*ip_gpio = 7;
		break;
	case 5:
	case 66:
		*ip_gpio = 6;
		break;
	case 81:
	case 65:
		*ip_gpio = 5;
		break;
	case 80:
	case 64:
		*ip_gpio = 4;
		break;
	case 10:
	case 79:
	case 142:
		*ip_gpio = 3;
		break;
	case 11:
	case 78:
	case 141:
		*ip_gpio = 2;
		break;
	case 7:
	case 150:
		*ip_gpio = 1;
		break;
	case 6:
	case 149:
		*ip_gpio = 0;
		break;
	default:
		*ip_gpio = -1;
		err = -1;
		break;
	}
	return err;
}

static int mmio_clock_init(struct mmio_platform_data *pdata)
{
	int err;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	extra->clk_ptr_bml = clk_get_sys("bml", NULL);
	if (IS_ERR(extra->clk_ptr_bml)) {
		err = PTR_ERR(extra->clk_ptr_bml);
		dev_err(pdata->dev, "Error %d getting clock 'bml'\n", err);
		goto err_bml_clk;
	}
	extra->clk_ptr_ipi2c = clk_get_sys("ipi2", NULL);
	if (IS_ERR(extra->clk_ptr_ipi2c)) {
		err = PTR_ERR(extra->clk_ptr_ipi2c);
		dev_err(pdata->dev, "Error %d getting clock 'ipi2'\n", err);
		goto err_ipi2c_clk;
	}

#ifdef CONFIG_MACH_JANICE
	if (system_rev < JANICE_R0_3) {
		extra->clk_ptr_ext[PRIMARY_CAMERA] =
			clk_get_sys("pri-cam", NULL);
		if (IS_ERR(extra->clk_ptr_ext[PRIMARY_CAMERA])) {
			err = PTR_ERR(extra->clk_ptr_ext[PRIMARY_CAMERA]);
			dev_err(pdata->dev,
			"Error %d getting clock 'pri-cam'\n", err);
			goto err_pri_ext_clk;
		}
	} else{
		extra->clk_ptr_ext[SECONDARY_CAMERA] =
			clk_get_sys("sec-cam", NULL);
		if (IS_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA])) {
			err = PTR_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA]);
			dev_err(pdata->dev,
			"Error %d getting clock 'sec-cam'\n", err);
			goto err_sec_ext_clk;
		}
	}
#elif defined(CONFIG_MACH_GAVINI)
	if (system_rev <= GAVINI_R0_0_B) {
		extra->clk_ptr_ext[PRIMARY_CAMERA] =
			clk_get_sys("pri-cam", NULL);
		if (IS_ERR(extra->clk_ptr_ext[PRIMARY_CAMERA])) {
			err = PTR_ERR(extra->clk_ptr_ext[PRIMARY_CAMERA]);
			dev_err(pdata->dev,
			"Error %d getting clock 'pri-cam'\n", err);
			goto err_pri_ext_clk;
		}
	} else{
		extra->clk_ptr_ext[SECONDARY_CAMERA] =
			clk_get_sys("sec-cam", NULL);
		if (IS_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA])) {
			err = PTR_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA]);
			dev_err(pdata->dev,
			"Error %d getting clock 'sec-cam'\n", err);
			goto err_sec_ext_clk;
		}
	}
#else  /* CONFIG_MACH_CODINA */
	extra->clk_ptr_ext[SECONDARY_CAMERA] = clk_get_sys("sec-cam", NULL);
	if (IS_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA])) {
		err = PTR_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA]);
		dev_err(pdata->dev,
			"Error %d getting clock 'sec-cam'\n", err);
		goto err_sec_ext_clk;
	}
#endif

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
err_sec_ext_clk:
	/*clk_put(extra->clk_ptr_ext[PRIMARY_CAMERA]);*/
err_pri_ext_clk:
	clk_put(extra->clk_ptr_ipi2c);
err_ipi2c_clk:
	clk_put(extra->clk_ptr_bml);
err_bml_clk:
	return err;
}
static void mmio_clock_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	clk_put(extra->clk_ptr_bml);
	clk_put(extra->clk_ptr_ipi2c);
#ifdef CONFIG_MACH_JANICE
	if (system_rev < JANICE_R0_3)
		clk_put(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	else
		clk_put(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#elif defined(CONFIG_MACH_GAVINI)
	if (system_rev <= GAVINI_R0_0_B)
		clk_put(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	else
		clk_put(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#else  /*CONFIG_MACH_CODINA */
	clk_put(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#endif
	/*clk_put(extra->clk_ptr_ext[pdata->camera_slot]);*/
}


static int mmio_pin_cfg_init(struct mmio_platform_data *pdata)
{
#if 0
	int err;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	extra->xshutdown_pins[PRIMARY_CAMERA].gpio = XSHUTDOWN_PRIMARY_SENSOR;
	extra->xshutdown_pins[PRIMARY_CAMERA].active_high = 1;
	extra->xshutdown_pins[PRIMARY_CAMERA].udelay = 20;

	extra->reset_pins[PRIMARY_CAMERA].gpio = RESET_PRIMARY_SENSOR;
	extra->reset_pins[PRIMARY_CAMERA].active_high = 1;
	extra->reset_pins[PRIMARY_CAMERA].udelay = 100;

	extra->xshutdown_pins[SECONDARY_CAMERA].gpio = XSHUTDOWN_SECONDARY_SENSOR;
	extra->xshutdown_pins[SECONDARY_CAMERA].active_high = 1;
	extra->xshutdown_pins[SECONDARY_CAMERA].udelay = 2;

	extra->reset_pins[SECONDARY_CAMERA].gpio = RESET_SECONDARY_SENSOR;
	extra->reset_pins[SECONDARY_CAMERA].active_high = 1;
	extra->reset_pins[SECONDARY_CAMERA].udelay = 100;

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
#endif
	return 0;

}

static void mmio_pin_cfg_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
#if 0
gpio_free(extra->xenon_charge);
#endif
}

/* For now, both sensors on HREF have some power up sequence. If different
 * sequences are needed for primary and secondary sensors, it can be
 * implemented easily. Just use camera_slot field of mmio_platform_data
 * to determine which camera needs to be powered up */
static int mmio_power_init(struct mmio_platform_data *pdata)
{
	int err = 0, i = 0;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	extra->number_of_regulators = sizeof(regulator_names)/
					sizeof(regulator_names[0]);
	extra->mmio_regulators =
	    kzalloc(sizeof(struct regulator *) * extra->number_of_regulators,
		    GFP_KERNEL);
	if (!extra->mmio_regulators) {
		dev_err(pdata->dev , "Error while allocating memory for mmio"
				"regulators\n");
		err = -ENOMEM;
		goto err_no_mem_reg;
	}
	for (i = 0; i <
		extra->number_of_regulators; i++) {
		extra->mmio_regulators[i] =
			regulator_get(pdata->dev, regulator_names[i]);
		if (IS_ERR(extra->mmio_regulators[i])) {
			err = PTR_ERR(extra->mmio_regulators[i]);
			dev_err(pdata->dev , "Error %d getting regulator '%s'"
			"\n", err, regulator_names[i]);
			goto err_regulator;
		}
	}
	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
err_regulator:
	/* Return regulators we have already requested */
	while (i--)
		regulator_put(extra->mmio_regulators[i]);
	kfree(extra->mmio_regulators);
err_no_mem_reg:
	return err;
}
static void mmio_power_exit(struct mmio_platform_data *pdata)
{
	int i = 0;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	for (i = 0; i < extra->number_of_regulators; i++)
		regulator_put(extra->mmio_regulators[i]);
	kfree(extra->mmio_regulators);
}

static int mmio_platform_init(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = NULL;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	/* Alloc memory for our own extra data */
	extra = kzalloc(sizeof(struct mmio_board_data), GFP_KERNEL);
	if (!extra) {
		dev_err(pdata->dev, "%s: memory alloc failed for "
		"mmio_board_data\n", __func__);
		err = -ENOMEM;
		goto err_no_mem_extra;
	}
	/* Hook the data for other callbacks to use */
	pdata->extra = extra;

	err = mmio_power_init(pdata);
	if (err)
		goto err_regulator;
	err = mmio_clock_init(pdata);
	if (err)
		goto err_clock;
	err = mmio_pin_cfg_init(pdata);
	if (err)
		goto err_pin_cfg;
	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;

err_pin_cfg:
	mmio_clock_exit(pdata);
err_clock:
	mmio_power_exit(pdata);
err_regulator:
	kfree(extra);
err_no_mem_extra:
	return err;
}
static void mmio_platform_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	mmio_power_exit(pdata);
	mmio_clock_exit(pdata);
	mmio_pin_cfg_exit(pdata);
	kfree(extra);
	pdata->extra = NULL;
}

static int mmio_power_enable(struct mmio_platform_data *pdata)
{
	int err = 0, i = 0;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	/* Enable the regulators */
	for (i = 0; i < extra->number_of_regulators; i++) {
		err = regulator_enable(extra->mmio_regulators[i]);
		if (IS_ERR(extra->mmio_regulators[i])) {
			err = PTR_ERR(extra->mmio_regulators[i]);
			dev_err(pdata->dev , "Error %d enabling regulator '%s'"
			"\n", err, regulator_names[i]);
			goto err_regulator;
		}
	}

	err = clk_enable(extra->clk_ptr_bml);
	if (err) {
		dev_err(pdata->dev, "Error activating bml clock %d\n", err);
		goto err_bml_clk;
	}

	err = clk_enable(extra->clk_ptr_ipi2c);
	if (err) {
		dev_err(pdata->dev, "Error activating i2c2 clock %d\n", err);
		goto err_ipi2c_clk;
	}
	
	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
	
err_ipi2c_clk:
	clk_disable(extra->clk_ptr_bml);
err_bml_clk:
	
err_regulator:
	/* Disable regulators we already enabled */
	while (i--)
		regulator_disable(extra->mmio_regulators[i]);
	return err;
}

static void mmio_power_disable(struct mmio_platform_data *pdata)
{
	int i;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	clk_disable(extra->clk_ptr_bml);
	clk_disable(extra->clk_ptr_ipi2c);
	
	/* Disable the regulators */
	for (i = 0; i < extra->number_of_regulators; i++)
		regulator_disable(extra->mmio_regulators[i]);
}

static int mmio_clock_enable(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

#ifdef CONFIG_MACH_JANICE
	/* Enable appropriate external clock */
	if (system_rev < JANICE_R0_3)
		err = clk_enable(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	else
		err = clk_enable(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#elif defined(CONFIG_MACH_GAVINI)
	/* Enable appropriate external clock */
	if (system_rev <= GAVINI_R0_0_B)
		err = clk_enable(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	else
		err = clk_enable(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#else  /*CONFIG_MACH_CODINA*/
	err = clk_enable(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#endif
	/*err = clk_enable(extra->clk_ptr_ext[pdata->camera_slot]);*/



	if (err) {
		dev_err(pdata->dev, "Error activating clock for sensor %d, err"
			"%d\n", pdata->camera_slot, err);
		goto err_ext_clk;
	}
	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
	
err_ext_clk:
	return err;
}

static void mmio_clock_disable(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

#ifdef CONFIG_MACH_JANICE
	if (system_rev < JANICE_R0_3)
		clk_disable(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	else
		clk_disable(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#elif defined(CONFIG_MACH_GAVINI)
	if (system_rev <= GAVINI_R0_0_B)
		clk_disable(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	else
		clk_disable(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#else  /*CONFIG_MACH_CODINA*/
	clk_disable(extra->clk_ptr_ext[SECONDARY_CAMERA]);
#endif
	/*clk_disable(extra->clk_ptr_ext[pdata->camera_slot]);*/

}


static int mmio_config_xshutdown_pins(struct mmio_platform_data *pdata,
				      enum mmio_select_xshutdown_t select,
				      int is_active_high)
{
	int err = 0;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
#if 0
	switch (select) {
	case MMIO_ENABLE_XSHUTDOWN_HOST:
		err = nmk_config_pins(xshutdown_host, ARRAY_SIZE(xshutdown_host));
		break;
	case MMIO_ENABLE_XSHUTDOWN_FW:
		err = nmk_config_pins(xshutdown_fw, ARRAY_SIZE(xshutdown_fw));
		break;
	case MMIO_DISABLE_XSHUTDOWN:
		err = nmk_config_pins(xshutdown_disable, ARRAY_SIZE(xshutdown_disable));
	default:
		break;
	}
	if (err)
		dev_dbg(pdata->dev , "Error configuring xshutdown, err = %d\n",
		err);
#endif
	return err;
}
static void mmio_set_xshutdown(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
#if 0
	gpio_set_value(extra->xshutdown_pins[pdata->camera_slot].gpio ,
		(extra->xshutdown_pins[pdata->camera_slot].active_high ? 1 :
		0));
	udelay(extra->xshutdown_pins[pdata->camera_slot].udelay);

	gpio_set_value(extra->reset_pins[pdata->camera_slot].gpio ,
		(extra->reset_pins[pdata->camera_slot].active_high ? 1 :
		0));
	udelay(extra->reset_pins[pdata->camera_slot].udelay);
#endif
}
static int mmio_config_i2c_pins(struct mmio_platform_data *pdata,
				enum mmio_select_i2c_t select)
{
	int err = 0;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
#if 0
	switch (select) {
	case MMIO_ACTIVATE_I2C_HOST:
		err = nmk_config_pins(i2c2_pins, ARRAY_SIZE(i2c2_pins));
		break;
	case MMIO_ACTIVATE_IPI2C2:
		err = nmk_config_pins(ipi2c_pins, ARRAY_SIZE(ipi2c_pins));
		break;
	case MMIO_DEACTIVATE_I2C:
		err = nmk_config_pins(i2c_disable_pins,
			ARRAY_SIZE(i2c_disable_pins));
		break;
	default:
		break;
	}
#endif
	return err;
}

static struct mmio_platform_data mmio_config = {
	.platform_init = mmio_platform_init,
	.platform_exit = mmio_platform_exit,
	.power_enable = mmio_power_enable,
	.power_disable = mmio_power_disable,
	.clock_enable = mmio_clock_enable,
	.clock_disable = mmio_clock_disable,
	.config_i2c_pins = mmio_config_i2c_pins,
	.config_xshutdown_pins = mmio_config_xshutdown_pins,
	.set_xshutdown = mmio_set_xshutdown,
	.sia_base = U8500_SIA_BASE,
	.cr_base = U8500_CR_BASE
};

struct platform_device ux500_mmio_device = {
	.name = MMIO_NAME,
	.id = -1,
	.dev = {
		.platform_data = &mmio_config,
	}
};
