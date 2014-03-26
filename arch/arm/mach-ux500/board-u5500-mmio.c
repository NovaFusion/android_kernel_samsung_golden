/*
 * Copyright (C) 2011 ST-Ericsson
 * Author: Joakim Axelsson <joakim.axelsson@stericsson.com> for ST-Ericsson
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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <plat/pincfg.h>
#include <mach/gpio.h>
#include <mach/devices.h>
#include "board-u5500.h"
#include <linux/mmio.h>

struct mmio_board_data {
	int number_of_regulators;
	struct regulator **mmio_regulators;
	/* * Pin configs */
	struct mmio_gpio xshutdown_pins[CAMERA_SLOT_END];
	/* * Internal clocks */
	struct clk *clk_ptr_bml;
	struct clk *clk_ptr_ipi2c;
	/* * External clocks */
	struct clk *clk_ptr_ext[CAMERA_SLOT_END];
};

/*
 * Fill names of regulators required for powering up the
 * camera sensor in below array
 */
static char *regulator_names[] = {"v-mmio-camera", "v-ana"};

static int mmio_clock_init(struct mmio_platform_data *pdata)
{
	int err;
	struct mmio_board_data *extra = pdata->extra;

	extra->clk_ptr_ext[PRIMARY_CAMERA] =
		clk_get(pdata->dev, "primary-cam");
	if (IS_ERR(extra->clk_ptr_ext[PRIMARY_CAMERA])) {
		err = PTR_ERR(extra->clk_ptr_ext[PRIMARY_CAMERA]);
		dev_err(pdata->dev,
				"Error %d clock 'primary-cam'\n", err);
		goto err_pri_ext_clk;
	}
	extra->clk_ptr_ext[SECONDARY_CAMERA] =
		clk_get(pdata->dev, "secondary-cam");
	if (IS_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA])) {
		err = PTR_ERR(extra->clk_ptr_ext[SECONDARY_CAMERA]);
		dev_err(pdata->dev,
				"Error %d clock 'secondary-cam'\n", err);
		goto err_sec_ext_clk;
	}

	return 0;
err_sec_ext_clk:
	clk_put(extra->clk_ptr_ext[PRIMARY_CAMERA]);
err_pri_ext_clk:
	return err;
}

static void mmio_clock_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	clk_put(extra->clk_ptr_ext[PRIMARY_CAMERA]);
	clk_put(extra->clk_ptr_ext[SECONDARY_CAMERA]);
}

static int mmio_pin_cfg_init(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	extra->xshutdown_pins[PRIMARY_CAMERA].gpio =
		GPIO_PRIMARY_CAM_XSHUTDOWN;
	extra->xshutdown_pins[PRIMARY_CAMERA].active_high = 0;
	extra->xshutdown_pins[PRIMARY_CAMERA].udelay = 250;

	extra->xshutdown_pins[SECONDARY_CAMERA].gpio =
		GPIO_SECONDARY_CAM_XSHUTDOWN;
	extra->xshutdown_pins[SECONDARY_CAMERA].active_high = 0;
	extra->xshutdown_pins[SECONDARY_CAMERA].udelay = 250;

	return 0;
}

static void mmio_pin_cfg_exit(struct mmio_platform_data *pdata)
{
}

/*
 * For now, both sensors on B5500/S5500 have some power up sequence. If
 * different sequences are needed for primary and secondary sensors, it can
 * be implemented easily. Just use camera_slot field of mmio_platform_data
 * to determine which camera needs to be powered up
 */
static int mmio_power_init(struct mmio_platform_data *pdata)
{
	int err = 0, i = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	extra->number_of_regulators = ARRAY_SIZE(regulator_names);
	extra->mmio_regulators =
	    kzalloc(sizeof(struct regulator *) * extra->number_of_regulators,
		    GFP_KERNEL);
	if (!extra->mmio_regulators) {
		dev_err(pdata->dev
			, "Error allocating memory for mmio regulators\n");
		err = -ENOMEM;
		goto err_no_mem_reg;
	}
	for (i = 0; i <
		extra->number_of_regulators; i++) {
		extra->mmio_regulators[i] =
			regulator_get(pdata->dev, regulator_names[i]);
		if (IS_ERR(extra->mmio_regulators[i])) {
			err = PTR_ERR(extra->mmio_regulators[i]);
			dev_err(pdata->dev
					, "Error %d getting regulator '%s'\n"
					, err, regulator_names[i]);
			goto err_regulator;
		}
	}
	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
err_regulator:
	/*
	 * Return regulators we have already requested
	 */
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

	for (i = 0; i < extra->number_of_regulators; i++)
		regulator_put(extra->mmio_regulators[i]);
	kfree(extra->mmio_regulators);
}

static int mmio_platform_init(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = NULL;
	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	/*
	 * Alloc memory for our own extra data
	 */
	extra = kzalloc(sizeof(struct mmio_board_data), GFP_KERNEL);
	if (!extra) {
		dev_err(pdata->dev, "%s: memory alloc failed for "
		"mmio_board_data\n", __func__);
		err = -ENOMEM;
		goto err_no_mem_extra;
	}
	/*
	 * Hook the data for other callbacks to use
	 */
	pdata->extra = extra;

	pdata->camera_slot = -1;

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
	/*
	 * Enable the regulators
	 */
	for (i = 0; i < extra->number_of_regulators; i++) {
		err = regulator_enable(extra->mmio_regulators[i]);
		if (IS_ERR(extra->mmio_regulators[i])) {
			err = PTR_ERR(extra->mmio_regulators[i]);
			dev_err(pdata->dev , "Error %d enabling regulator '%s'"
			"\n", err, regulator_names[i]);
			goto err_regulator;
		}
	}

	err = gpio_request(GPIO_CAMERA_PMIC_EN, "Camera PMIC GPIO");
	if (err) {
		dev_err(pdata->dev, "Error %d while requesting"
				"Camera PMIC GPIO\n",
				err);
		return err;
	}

	err = gpio_direction_output(GPIO_CAMERA_PMIC_EN, 0);
	if (err) {
		dev_err(pdata->dev, "Error %d while setting"
				"Camera PMIC GPIO"
				"output mode\n", err);
		return err;
	}

	if (!(u5500_board_is_s5500()))
		gpio_set_value(GPIO_CAMERA_PMIC_EN, 1);
	else
		gpio_set_value(GPIO_CAMERA_PMIC_EN, 0);

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
	return 0;
err_regulator:
	/*
	 * Disable regulators we already enabled
	 */
	while (i--)
		regulator_disable(extra->mmio_regulators[i]);
	return err;
}

static void mmio_power_disable(struct mmio_platform_data *pdata)
{
	int i;
	struct mmio_board_data *extra = pdata->extra;
	/*
	 * Disable the regulators
	 */
	for (i = 0; i < extra->number_of_regulators; i++)
		regulator_disable(extra->mmio_regulators[i]);

	if (!(u5500_board_is_s5500()))
		gpio_set_value(GPIO_CAMERA_PMIC_EN, 0);
	else
		gpio_set_value(GPIO_CAMERA_PMIC_EN, 1);

	gpio_free(GPIO_CAMERA_PMIC_EN);
}

static int mmio_clock_enable(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	/*
	 * Enable appropriate external clock
	 */
	err = clk_enable(extra->clk_ptr_ext[pdata->camera_slot]);
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

	clk_disable(extra->clk_ptr_ext[pdata->camera_slot]);
}

static int mmio_config_xshutdown_pins(struct mmio_platform_data *pdata,
				      enum mmio_select_xshutdown_t select,
				      int is_active_high)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);
	switch (select) {
	case MMIO_ENABLE_XSHUTDOWN_HOST:
		extra->xshutdown_pins[pdata->camera_slot].active_high =
			is_active_high;
		dev_dbg(pdata->dev , "Enabling Xshutdown GPIO PIN = %d",
			extra->xshutdown_pins[pdata->camera_slot].gpio);

		err = gpio_request
			(extra->xshutdown_pins[pdata->camera_slot].gpio,
				"MMIO GPIO");
		if (err) {
			dev_err(pdata->dev, "Error %d while requesting"
					"Xshutdown MMIO GPIO\n",
					err);
			return err;
		}

		err = gpio_direction_output
			(extra->xshutdown_pins[pdata->camera_slot].gpio,
				0);
		if (err) {
			dev_err(pdata->dev, "Error %d while setting"
					"Xshutdown MMIO GPIO"
					"output mode\n", err);
			return err;
		}
		break;
	case MMIO_DISABLE_XSHUTDOWN:
		dev_dbg(pdata->dev , "Disabling Xshutdown GPIO PIN = %d",
			extra->xshutdown_pins[pdata->camera_slot].gpio);
		gpio_free(extra->xshutdown_pins[pdata->camera_slot].gpio);
		break;
	default:
		break;
	}
	if (err)
		dev_err(pdata->dev , "Error configuring xshutdown, err = %d\n",
		err);
	return err;
}

static void mmio_set_xshutdown(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	gpio_set_value(extra->xshutdown_pins[pdata->camera_slot].gpio ,
			(extra->xshutdown_pins[pdata->camera_slot].active_high
			? 1 : 0));
	udelay(extra->xshutdown_pins[pdata->camera_slot].udelay);
}

/*
 * TODO: This function would be removed in futute.
 * Since this function is called frequently
 * from HSM Camera code , it is kept for Legacy.
 */
static int mmio_config_i2c_pins(struct mmio_platform_data *pdata,
				enum mmio_select_i2c_t select)
{
	int err = 0;

	switch (select) {
	case MMIO_ACTIVATE_I2C_HOST:
		dev_dbg(pdata->dev , "Activate I2C from Host called\n");
		break;
	case MMIO_DEACTIVATE_I2C:
		dev_dbg(pdata->dev , "DeActivate I2C from Host called\n");
		break;
	default:
		break;
	}

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
	.set_xshutdown = mmio_set_xshutdown
};

struct platform_device u5500_mmio_device = {
	.name = MMIO_NAME,
	.id = -1,
	.dev = {
		.platform_data = &mmio_config,
	}
};
