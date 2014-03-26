/*
 * Copyright (C) 2011 ST-Ericsson
 * Author: Joakim Axelsson <joakim.axelsson@stericsson.com> for ST-Ericsson
 * Author: Rajat Verma <rajat.verma@stericsson.com> for ST-Ericsson.
 * Author: Vincent Abriou <vincent.abriou@stericsson.com> for ST-Ericsson.
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
#include <plat/gpio-nomadik.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <plat/pincfg.h>
#include <mach/gpio.h>
#include <mach/devices.h>
#include <mach/hardware.h>

#include "pins-db8500.h"
#include "pins.h"
#include "board-mop500.h"
#include <linux/mmio.h>

/*
 ******************************************************************************
 * CUSTOMIZABLE PART
 ******************************************************************************
 */

/*
 * GLOBAL INFORMATION:
 * -------------------
 * If it exists different power sequencse for the different YUV camera, just
 * use the camera_slot field of mmio_platform_data to determine which camera
 * needs to be powered up.
 */

/*
 * Signal to control for secondary YUV camera on u8500
 *          VAUX ---- regulator named v-mmio-camera
 *          VANA ---- regulator named v-ana
 *      I2C2_SDA ---- mapped on GPIO8 (gpio_i2c[0])
 *      I2C2_SDL ---- mapped on GPIO9 (gpio_i2c[1])
 *       CAM_SD1 ---- mapped to GPIO142 active level low (gpio_camsd)
 *       bml clk ---- clkbml
 *     ipi2c clk ---- clkipi2c
 *       ext clk ---- clkout1
 */

/*
 * Signal to control for primary YUV camera on l9540
 * (under is machine_is_u9540() condition)
 *          VAUX ---- regulator named v-mmio-camera
 *          VANA ---- regulator named v-ana
 *      I2C2_SDA ---- mapped on GPIO8 (gpio_i2c[0])
 *      I2C2_SDL ---- mapped on GPIO9 (gpio_i2c[1])
 *      CAM0_RES ---- mapped to GPIO141 active level high (gpio_camsd)
 *	 CAM0_EN ---- mapped on EGPIO_PIN_3 (gpio_power_en)
 *       bml clk ---- clkbml
 *     ipi2c clk ---- clkipi2c
 *       ext clk ---- clkout0
 *
 *       i2c_mux ---- drived by i2c2 block mapped on gpio8 and gpio9
 */

/*
 * Signal to control for secondary YUV camera on u8500
 * (under is machine_is_u9540() condition)
 *          VAUX ---- regulator named v-mmio-camera
 *          VANA ---- regulator named v-ana
 *      I2C2_SDA ---- mapped on GPIO8 (gpio_i2c[0])
 *      I2C2_SDL ---- mapped on GPIO9 (gpio_i2c[1])
 *      CAM1_RES ---- mapped to GPIO142 active level high (gpio_camsd)
 *	 CAM1_EN ---- mapped on EGPIO_PIN_4 (gpio_power_en)
 *       bml clk ---- clkbml
 *     ipi2c clk ---- clkipi2c
 *       ext clk ---- clkout1
 *
 *       i2c_mux ---- drived by i2c2 block mapped on gpio8 and gpio9
 */

/*
 * This insternal structure is customizable in order to define and configure
 * the different regulator, gpio and clock use to drive the camera.
 */
struct mmio_board_data {
	/* Power config */
	struct mmio_regulator reg_vana;
	struct mmio_regulator reg_vmmiocamera;

	/* Pin config */
	struct mmio_gpio gpio_i2c[2];
	struct mmio_gpio gpio_power_en;
	struct mmio_gpio gpio_camsd;

	/* Clock config */
	struct mmio_clk clk_bml;
	struct mmio_clk clk_ipi2c;
	struct mmio_clk clk_ext;
};

/*
 * mmio_board_data_init() - Initialize board configuration.
 *
 * This function is customizable.
 * It allow to define and configure the different regulators,
 * gpios and clocks used to drive the camera.
 * Don't forget to update mmio_board_data structure accordingly.
 * It is called from mmio_platform_init function.
 */
static int mmio_board_data_init(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	/* Power config */
	extra->reg_vana.name           = "vddcsi1v2";
	extra->reg_vmmiocamera.name    = "vaux12v5";

	/* Pin config */
	extra->gpio_i2c[0].name        = "I2C2_SDA";
	extra->gpio_i2c[0].gpio        = 8;
	extra->gpio_i2c[0].cfg_ena     = GPIO8_I2C2_SDA;
	extra->gpio_i2c[0].cfg_disa    = GPIO8_GPIO;

	extra->gpio_i2c[1].name        = "I2C2_SCL";
	extra->gpio_i2c[1].gpio        = 9;
	extra->gpio_i2c[1].cfg_ena     = GPIO9_I2C2_SCL;
	extra->gpio_i2c[1].cfg_disa    = GPIO9_GPIO;

	/* Update GPIO mappings according to board */
	if (machine_is_u9540()) {

		if (pdata->camera_slot == PRIMARY_CAMERA) {
			/* Primary sensor */
			extra->gpio_camsd.name  = "CAM0_RES";
			extra->gpio_camsd.gpio  = 141;

			extra->gpio_power_en.name = "CAM0_EN";
			extra->gpio_power_en.gpio = MOP500_EGPIO(3);
		} else {
			/* Secondary sensor */
			extra->gpio_camsd.name  = "CAM1_RES";
			extra->gpio_camsd.gpio  = 142;

			extra->gpio_power_en.name = "CAM1_EN";
			extra->gpio_power_en.gpio = MOP500_EGPIO(4);
		}
	} else if (machine_is_hrefv60()) {
		extra->gpio_camsd.name  = "CAM_SD1";
		extra->gpio_camsd.gpio  = 140;
	} else {
		extra->gpio_camsd.name  = "CAM_SD1";
		extra->gpio_camsd.gpio  = 142;
	}

	/* Clock config */
	extra->clk_bml.name              = "bml";
	extra->clk_ipi2c.name            = "ipi2";

	if (pdata->camera_slot == PRIMARY_CAMERA) {
		if (machine_is_u9540()) {
			extra->clk_ext.name    = "pri-yuv-cam";
		} else {
			dev_err(pdata->dev,
					"Yuv primary sensor not avalaible!\n");
			return -1;
		}
	} else {
		extra->clk_ext.name      = "sec-yuv-cam";
	}

	return 0;
}

/*
 * mmio_power_init() - Initialize power regulators/gpios.
 *
 * This function is customizable.
 * It allows to reserve regulators / gpios that will be used.
 * It is called from mmio_platform_init function.
 */
static int mmio_power_init(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	/* Request regulator v-mmio-camera */
	dev_dbg(pdata->dev, "Request v-mmio-camera regulator.\n");
	extra->reg_vmmiocamera.reg_ptr =
		regulator_get(pdata->dev, extra->reg_vmmiocamera.name);
	if (IS_ERR(extra->reg_vmmiocamera.reg_ptr)) {
		err = PTR_ERR(extra->reg_vmmiocamera.reg_ptr);
		dev_err(pdata->dev,
				"Error %d while getting regulator 'v-mmio-camera'\n",
				err);
		goto err_reg_v_mmio_camera;
	}

	/* Request regulator v-ana */
	dev_dbg(pdata->dev, "Request v-ana regulator.\n");
	extra->reg_vana.reg_ptr =
		regulator_get(pdata->dev, extra->reg_vana.name);
	if (IS_ERR(extra->reg_vana.reg_ptr)) {
		err = PTR_ERR(extra->reg_vana.reg_ptr);
		dev_err(pdata->dev,
				"Error %d while getting regulator 'v-ana'\n",
				err);
		goto err_reg_v_ana;
	}

	if (machine_is_u9540()) {
		/* Initialize CAMERA_POWER_EN */
		dev_dbg(pdata->dev,
				"Request %s gpio %d.\n",
				extra->gpio_power_en.name,
				extra->gpio_power_en.gpio);
		err = gpio_request(
				extra->gpio_power_en.gpio,
				extra->gpio_power_en.name);
		if (err) {
			dev_err(pdata->dev,
					"Unable to get GPIO %d.\n",
					extra->gpio_power_en.gpio);
			goto err_power_en;
		}

		err = gpio_direction_output(extra->gpio_power_en.gpio, 0);
		if (err) {
			dev_err(pdata->dev,
					"Unable to set GPIO %d\n",
					extra->gpio_power_en.gpio);
		}

		mdelay(100);
	}

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);

	return err;

err_power_en:
	regulator_put(extra->reg_vana.reg_ptr);
err_reg_v_ana:
	regulator_put(extra->reg_vmmiocamera.reg_ptr);
err_reg_v_mmio_camera:
	return err;
}

/*
 * mmio_power_exit() - De-initialize power regulators/gpios.
 *
 * This function is customizable.
 * It allows to free regulators / gpios reserved during init.
 * It is called from mmio_platform_exit function.
 */
static void mmio_power_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	if (machine_is_u9540())
		gpio_free(extra->gpio_power_en.gpio);

	regulator_put(extra->reg_vana.reg_ptr);

	regulator_put(extra->reg_vmmiocamera.reg_ptr);

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
}

/*
 * mmio_clock_init() - Initialize clocks.
 *
 * This function is customizable.
 * It allows to get the clocks that will be used.
 * It is called from mmio_platform_init function.
 */
static int mmio_clock_init(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	extra->clk_bml.clk_ptr = clk_get_sys(extra->clk_bml.name, NULL);
	if (IS_ERR(extra->clk_bml.clk_ptr)) {
		err = PTR_ERR(extra->clk_bml.clk_ptr);
		dev_err(pdata->dev, "Error %d getting clock '%s'\n",
				err,
				extra->clk_bml.name);
		goto err_bml_clk;
	}

	extra->clk_ipi2c.clk_ptr = clk_get_sys(extra->clk_ipi2c.name, NULL);
	if (IS_ERR(extra->clk_ipi2c.clk_ptr)) {
		err = PTR_ERR(extra->clk_ipi2c.clk_ptr);
		dev_err(pdata->dev, "Error %d getting clock '%s'\n",
				err,
				extra->clk_ipi2c.name);
		goto err_ipi2c_clk;
	}

	extra->clk_ext.clk_ptr = clk_get_sys(extra->clk_ext.name, NULL);
	if (IS_ERR(extra->clk_ext.clk_ptr)) {
		err = PTR_ERR(extra->clk_ext.clk_ptr);
		dev_err(pdata->dev, "Error %d getting clock '%s'\n",
				err,
				extra->clk_ext.name);
		goto err_ext_clk;
	}
	dev_info(pdata->dev , "clock %s activated\n",
				extra->clk_ext.name);

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);

	return err;

err_ext_clk:
	clk_put(extra->clk_ipi2c.clk_ptr);
err_ipi2c_clk:
	clk_put(extra->clk_bml.clk_ptr);
err_bml_clk:
	return err;
}

/*
 * mmio_clock_exit() - De-initialize clocks.
 *
 * This function is customizable.
 * It allows to free clocks reserved during init.
 * It is called from mmio_platform_exit function.
 */
static void mmio_clock_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	clk_put(extra->clk_bml.clk_ptr);

	clk_put(extra->clk_ipi2c.clk_ptr);

	clk_put(extra->clk_ext.clk_ptr);

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
}

/*
 * mmio_i2c_transfer - write command to i2c
*/
static int mmio_i2c_transfer(u8 command)
{
	struct i2c_adapter     *i2c2;
	u8                     i2c2data[1];
	struct i2c_msg         i2c2msg;

	i2c2msg.addr     = 0x70;     /* i2c address */
	i2c2msg.buf      = i2c2data;
	i2c2msg.len      = 1;
	i2c2msg.flags = 0;
	/* Select I2C2 */
	i2c2 = i2c_get_adapter(2);
	i2c2data[0] = command;
	i2c_transfer(i2c2, &i2c2msg, 1);
	i2c_put_adapter(i2c2);

	return 0;
}

/*
 * mmio_config_i2c_pins() - Configure I2C.
 *
 * Not customizable function.
 * Activate or deactivate I2C.
 * It is called from mmio driver.
 */
static int mmio_config_i2c_pins(struct mmio_platform_data *pdata,
		enum mmio_select_i2c_t select)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	switch (select) {
	case MMIO_ACTIVATE_I2C:
		err = nmk_config_pin(extra->gpio_i2c[0].cfg_ena, 0);
		if (err) {
			dev_err(pdata->dev, "failed to set I2C2.\n");
			return err;
		}
		err = nmk_config_pin(extra->gpio_i2c[1].cfg_ena, 0);
		if (err) {
			dev_err(pdata->dev, "failed to set I2C2.\n");
			return err;
		}
		dev_info(pdata->dev , "MMIO_ACTIVATE_I2C\n");

		break;
	case MMIO_DEACTIVATE_I2C:
		err = nmk_config_pin(extra->gpio_i2c[0].cfg_disa, 0);
		err = nmk_config_pin(extra->gpio_i2c[1].cfg_disa, 0);
		dev_info(pdata->dev , "MMIO_DEACTIVATE_I2C\n");
		break;
	default:
		break;
	}

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);

	return err;
}

/*
 * mmio_pin_cfg_init() - Initialize gpios.
 *
 * This function is customizable.
 * It allows to reserve gpios and to set it in the right configuration (ready
 * to be used).
 * It is called from mmio_platform_init function.
 */
static int mmio_pin_cfg_init(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	/* Initialize I2C */
	dev_dbg(pdata->dev,
			"Request %s gpio %d.\n",
			extra->gpio_i2c[0].name,
			extra->gpio_i2c[0].gpio);
	err = gpio_request(
			extra->gpio_i2c[0].gpio,
			extra->gpio_i2c[0].name);
	if (err) {
		dev_err(pdata->dev,
				"Unable to get GPIO %d.\n",
				extra->gpio_i2c[0].gpio);
		goto err_i2c0;
	}

	dev_dbg(pdata->dev,
			"Request %s gpio %d.\n",
			extra->gpio_i2c[1].name,
			extra->gpio_i2c[1].gpio);
	err = gpio_request(
			extra->gpio_i2c[1].gpio,
			extra->gpio_i2c[1].name);
	if (err) {
		dev_err(pdata->dev,
				"Unable to get GPIO %d.\n",
				extra->gpio_i2c[1].gpio);
		goto err_i2c1;
	}

	/* Initialize gpio_camsd */
	dev_dbg(pdata->dev,
			"Request %s gpio %d.\n",
			extra->gpio_camsd.name,
			extra->gpio_camsd.gpio);
	err = gpio_request(
			extra->gpio_camsd.gpio,
			extra->gpio_camsd.name);
	if (err) {
		dev_err(pdata->dev,
				"Unable to get GPIO %d.\n",
				extra->gpio_camsd.gpio);
		goto err_cam_sd1;
	}

	if (machine_is_u9540()) {
		/* CAMx_RES -> low */
		err = gpio_direction_output(extra->gpio_camsd.gpio, 0);
		if (err) {
			dev_err(pdata->dev,
					"Unable to set GPIO %d\n",
					extra->gpio_camsd.gpio);
		}
	} else {
		/* CAM_SD1 -> high */
		err = gpio_direction_output(extra->gpio_camsd.gpio, 1);
		if (err) {
			dev_err(pdata->dev,
					"Unable to set GPIO %d\n",
					extra->gpio_camsd.gpio);
		}
	}

	/* On u9540 UIB we need to configure an I2C multiplexer IC using I2C2.
	 * It allow to route the I2C command the the selected camera/sensor.
	 * It should be done after VAUX1 activation */
	if (machine_is_u9540()) {
		struct mmio_board_data *extra = pdata->extra;
		/* Enable regulator v-mmio-camera */
		err = regulator_enable(extra->reg_vmmiocamera.reg_ptr);
		if (err) {
			dev_err(pdata->dev,
					"ERROR: Can't enable '%s' regulator.\n",
					extra->reg_vmmiocamera.name);
			return err;
		}
		/* Force IC2 gpio to be drived by host */
		mmio_config_i2c_pins(pdata, MMIO_ACTIVATE_I2C);
		if (pdata->camera_slot == PRIMARY_CAMERA) {
			/* Route I2C to the primary camera/sensor */
			dev_dbg(pdata->dev, "Primary camera I2C");
			mmio_i2c_transfer(0x04);
		} else {
			dev_dbg(pdata->dev, "Secondary camera I2C");
			/* Route I2C to the secondary camera/sensor */
			mmio_i2c_transfer(0x05);
		}

	}

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);

	return err;

err_cam_sd1:
	gpio_free(extra->gpio_i2c[1].gpio);
err_i2c1:
	gpio_free(extra->gpio_i2c[0].gpio);
err_i2c0:
	return err;
}

/*
 * mmio_pin_cfg_exit() - De-initialize gpios.
 *
 * This function is customizable.
 * It allows to free gpios reserved during init.
 * It is called from mmio_platform_exit function.
 */
static void mmio_pin_cfg_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	/* On u9540 UIB we need to configure an I2C multiplexer IC using I2C2.
	 * It allow to route the I2C command the the selected camera/sensor.
	 * It should be done after VAUX1 activation */
	if (machine_is_u9540()) {
		int err = 0;
		struct mmio_board_data *extra = pdata->extra;

		/* Enable regulator v-mmio-camera */
		err = regulator_enable(extra->reg_vmmiocamera.reg_ptr);
		if (err) {
			dev_err(pdata->dev,
					"ERROR: Can't enable '%s' regulator.\n",
					extra->reg_vmmiocamera.name);
		}
		/* Force IC2 gpio to be drived by host */
		mmio_config_i2c_pins(pdata, MMIO_ACTIVATE_I2C);

		/* Deactivate I2C multiplexer */
		mmio_i2c_transfer(0x00);

		/* Deactivate I2C */
		mmio_config_i2c_pins(pdata, MMIO_DEACTIVATE_I2C);
	}

	gpio_free(extra->gpio_camsd.gpio);

	gpio_free(extra->gpio_i2c[1].gpio);

	gpio_free(extra->gpio_i2c[0].gpio);

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
}

/*
 * mmio_enable_clock() - clock enable sequence.
 *
 * This function is customizable.
 * It allows to define the clock enable sequence.
 * It is called from mmio_wakeup_sequence function.
 */
static int mmio_enable_clock(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	/* Enable internal clocks */
	err = clk_enable(extra->clk_bml.clk_ptr);
	if (err) {
		dev_err(pdata->dev, "Error activating '%s' clock %d\n",
				extra->clk_bml.name,
				err);
		goto err_bml_clk;
	}
	err = clk_enable(extra->clk_ipi2c.clk_ptr);
	if (err) {
		dev_err(pdata->dev, "Error activating '%s' clock %d\n",
				extra->clk_ipi2c.name,
				err);
		goto err_ipi2c_clk;
	}
	/* Enable appropriate external clock */
	err = clk_enable(extra->clk_ext.clk_ptr);
	if (err) {
		dev_err(pdata->dev, "Error activating '%s' clock %d\n",
				extra->clk_ext.name,
				err);
		goto err_ext_clk;
	}

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);

	return err;

err_ext_clk:
	clk_disable(extra->clk_ipi2c.clk_ptr);
err_ipi2c_clk:
	clk_disable(extra->clk_bml.clk_ptr);
err_bml_clk:
	return err;
}

/*
 * mmio_disable_clock() - clock disable sequence.
 *
 * This function is customizable.
 * It allows to define the clock disable sequence.
 * It is called from mmio_shutdown_sequence function.
 */
static void mmio_disable_clock(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	clk_disable(extra->clk_bml.clk_ptr);

	clk_disable(extra->clk_ipi2c.clk_ptr);

	clk_disable(extra->clk_ext.clk_ptr);

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
}

/*
 * mmio_wakeup_sequence() - wakeup sequence.
 *
 * This function is customizable.
 * It allows to define YUV camera wakeup sequence.
 * It is called from mmio driver.
 */
static int mmio_wakeup_sequence(struct mmio_platform_data *pdata)
{
	int err = 0;
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	if (machine_is_u9540()) {
		/* Wakeup sequence for l9540 primary/secondary camera */

		/* Enable regulator v-mmio-camera */
		err = regulator_enable(extra->reg_vmmiocamera.reg_ptr);
		if (err) {
			dev_err(pdata->dev,
					"ERROR: Can't enable '%s' regulator.\n",
					extra->reg_vmmiocamera.name);
			return err;
		}

		/* Enable regulator v-ana */
		err = regulator_enable(extra->reg_vana.reg_ptr);
		if (err) {
			dev_err(pdata->dev,
					"ERROR: Can't enable '%s' regulator.\n",
					extra->reg_vana.name);
			return err;
		}

		/* Camera power enable  */
		dev_dbg(pdata->dev , "Camera power enable");
		gpio_set_value_cansleep(extra->gpio_power_en.gpio, 1);

		/* Reset sequence */
		gpio_set_value(extra->gpio_camsd.gpio, 1);
		mdelay(110);
		gpio_set_value(extra->gpio_camsd.gpio, 0);
		mdelay(10);
		gpio_set_value(extra->gpio_camsd.gpio, 1);
		mdelay(10);

		/* Enable clock */
		err = mmio_enable_clock(pdata);
		if (err) {
			dev_err(pdata->dev, "Unable to enable clocks.\n");
			return err;
		}

	} else {
		/* Wakeup sequence for u8500 secondary camera */

		/* Enable regulator v-mmio-camera */
		err = regulator_enable(extra->reg_vmmiocamera.reg_ptr);
		if (err) {
			dev_err(pdata->dev,
					"ERROR: Can't enable '%s' regulator.\n",
					extra->reg_vmmiocamera.name);
			return err;
		}

		/* Enable regulator v-ana */
		err = regulator_enable(extra->reg_vana.reg_ptr);
		if (err) {
			dev_err(pdata->dev,
					"ERROR: Can't enable '%s' regulator.\n",
					extra->reg_vana.name);
			return err;
		}

		/* CAM_SD1 -> low */
		gpio_set_value(extra->gpio_camsd.gpio, 0);

		/* Enable clock */
		err = mmio_enable_clock(pdata);
		if (err) {
			dev_err(pdata->dev, "Unable to enable clocks.\n");
			return err;
		}
	}

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);

	return err;
}

/*
 * mmio_shutdown_sequence() - shutdown sequence.
 *
 * This function is customizable.
 * It allows to define YUV camera shutdown sequence.
 * It is called from mmio driver.
 */
static void mmio_shutdown_sequence(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	if (machine_is_u9540()) {
		/* CAM_SD1 -> high */
		gpio_set_value(extra->gpio_camsd.gpio, 0);

		/* Disable clock */
		mmio_disable_clock(pdata);

		/* Camera power disable */
		gpio_set_value_cansleep(extra->gpio_power_en.gpio, 0);

		/* Disable regulator v-ana */
		regulator_disable(extra->reg_vana.reg_ptr);

		/* Disable regulator v-mmio-camera */
		regulator_disable(extra->reg_vmmiocamera.reg_ptr);

	} else {
		/* CAM_SD1 -> high */
		gpio_set_value(extra->gpio_camsd.gpio, 1);

		/* Disable clock */
		mmio_disable_clock(pdata);

		/* Disable regulator v-ana */
		regulator_disable(extra->reg_vana.reg_ptr);

		/* Disable regulator v-mmio-camera */
		regulator_disable(extra->reg_vmmiocamera.reg_ptr);
	}

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
}

/*
 ******************************************************************************
 * NOT CUSTOMIZABLE PART
 ******************************************************************************
 */

/*
 * mmio_platform_init() - Initialize platform.
 *
 * Not customizable function.
 * Initialize power, clock and pins.
 * It is called from mmio driver.
 */
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

	err = mmio_board_data_init(pdata);
	if (err)
		goto err_board_data_init;

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

	return err;

err_pin_cfg:
	mmio_clock_exit(pdata);
err_clock:
	mmio_power_exit(pdata);
err_regulator:
err_board_data_init:
	kfree(extra);
err_no_mem_extra:
	return err;
}

/*
 * mmio_platform_exit() - Deinitialize platform.
 *
 * Not customizable function.
 * Deinitialize power, clock and pins.
 * It is called from mmio driver.
 */
static void mmio_platform_exit(struct mmio_platform_data *pdata)
{
	struct mmio_board_data *extra = pdata->extra;

	dev_dbg(pdata->dev , "Board %s() Enter\n", __func__);

	mmio_power_exit(pdata);
	mmio_clock_exit(pdata);
	mmio_pin_cfg_exit(pdata);
	kfree(extra);
	pdata->extra = NULL;

	dev_dbg(pdata->dev , "Board %s() Exit\n", __func__);
}


static struct mmio_platform_data mmio_config = {
	.platform_init   = mmio_platform_init,
	.platform_exit   = mmio_platform_exit,
	.power_enable    = mmio_wakeup_sequence,
	.power_disable   = mmio_shutdown_sequence,
	.config_i2c_pins = mmio_config_i2c_pins,
	.sia_base        = U8500_SIA_BASE,
	.cr_base         = U8500_CR_BASE
};

struct platform_device ux500_mmio_yuv_device = {
	.name = MMIO_YUV_NAME,
	.id = -1,
	.dev = {
		.platform_data = &mmio_config,
	}
};
