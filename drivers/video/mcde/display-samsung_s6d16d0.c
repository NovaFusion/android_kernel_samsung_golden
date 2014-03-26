/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE Samsung S6D16D0 display driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/err.h>

#include <video/mcde_display.h>

#define RESET_DURATION_US	10
#define RESET_DELAY_MS		120
#define SLEEP_OUT_DELAY_MS	120
#define IO_REGU			"vdd1"
#define IO_REGU_MIN		1650000
#define IO_REGU_MAX		3300000

#define DSI_HS_FREQ_HZ		420160000
#define DSI_LP_FREQ_HZ		19200000

struct device_info {
	int reset_gpio;
	struct mcde_port port;
	struct regulator *regulator;
};

static inline struct device_info *get_drvdata(struct mcde_display_device *ddev)
{
	return (struct device_info *)dev_get_drvdata(&ddev->dev);
}

static int power_on(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	dev_dbg(&ddev->dev, "Reset & power on s6d16d0 display\n");

	regulator_enable(di->regulator);
	gpio_set_value_cansleep(di->reset_gpio, 0);
	udelay(RESET_DURATION_US);
	gpio_set_value_cansleep(di->reset_gpio, 1);
	msleep(RESET_DELAY_MS);

	return 0;
}

static int power_off(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	dev_dbg(&ddev->dev, "Power off s6d16d0 display\n");

	regulator_disable(di->regulator);

	return 0;
}

static int display_on(struct mcde_display_device *ddev)
{
	int ret;
	u8 val = 0;

	dev_dbg(&ddev->dev, "Display on s6d16d0\n");

	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_TEAR_ON, &val, 1);
	if (ret)
		dev_warn(&ddev->dev,
			"%s:Failed to enable synchronized update\n", __func__);

	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_EXIT_SLEEP_MODE,
								NULL, 0);
	if (ret)
		return ret;
	msleep(SLEEP_OUT_DELAY_MS);
	return mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_DISPLAY_ON,
								NULL, 0);
}

static int display_off(struct mcde_display_device *ddev)
{
	int ret;

	dev_dbg(&ddev->dev, "Display off s6d16d0\n");

	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_DISPLAY_OFF,
								NULL, 0);
	if (ret)
		return ret;

	return mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_ENTER_SLEEP_MODE,
								NULL, 0);
}

static int set_power_mode(struct mcde_display_device *ddev,
					enum mcde_display_power_mode power_mode)
{
	int ret = 0;

	dev_dbg(&ddev->dev, "Set power mode %d\n", power_mode);

	/* OFF -> STANDBY */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {
		ret = power_on(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_ON) {

		ret = display_on(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_ON;
	}
	/* ON -> STANDBY */
	else if (ddev->power_mode == MCDE_DISPLAY_PM_ON &&
					power_mode <= MCDE_DISPLAY_PM_STANDBY) {

		ret = display_off(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_OFF) {
		ret = power_off(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_OFF;
	}

	return mcde_chnl_set_power_mode(ddev->chnl_state, ddev->power_mode);
}

static int __devinit samsung_s6d16d0_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct mcde_display_dsi_platform_data *pdata = ddev->dev.platform_data;
	struct device_info *di;

	if (pdata == NULL || !pdata->reset_gpio) {
		dev_err(&ddev->dev, "Invalid platform data\n");
		return -EINVAL;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;
	di->reset_gpio = pdata->reset_gpio;
	di->port.link = pdata->link;
	di->port.type = MCDE_PORTTYPE_DSI;
	di->port.mode = MCDE_PORTMODE_CMD;
	di->port.pixel_format = MCDE_PORTPIXFMT_DSI_24BPP;
	di->port.sync_src = ddev->port->sync_src;
	if (ddev->port->sync_src == MCDE_SYNCSRC_TE0 ||
				ddev->port->sync_src == MCDE_SYNCSRC_TE1) {
		di->port.vsync_polarity = VSYNC_ACTIVE_HIGH;
		di->port.vsync_clock_div = 0;
		di->port.vsync_min_duration = 0;
		di->port.vsync_max_duration = 0;
	}
	di->port.frame_trig = ddev->port->frame_trig;
	di->port.phy.dsi.num_data_lanes = pdata->num_data_lanes;
	di->port.phy.dsi.host_eot_gen = true;
	di->port.phy.dsi.hs_freq = DSI_HS_FREQ_HZ;
	di->port.phy.dsi.lp_freq = DSI_LP_FREQ_HZ;

	ret = gpio_request(di->reset_gpio, NULL);
	if (ret)
		goto gpio_request_failed;
	gpio_direction_output(di->reset_gpio, 1);
	di->regulator = regulator_get(&ddev->dev, IO_REGU);
	if (IS_ERR(di->regulator)) {
		di->regulator = NULL;
		goto regulator_get_failed;
	}
	ret = regulator_set_voltage(di->regulator, IO_REGU_MIN, IO_REGU_MAX);
	if (WARN_ON(ret))
		goto regulator_voltage_failed;

	/* Get in sync with u-boot */
	if (ddev->power_mode != MCDE_DISPLAY_PM_OFF)
		(void)regulator_enable(di->regulator);

	ddev->set_power_mode = set_power_mode;
	ddev->port = &di->port;
	ddev->native_x_res = 864;
	ddev->native_y_res = 480;
	dev_set_drvdata(&ddev->dev, di);

	dev_info(&ddev->dev, "Samsung s6d16d0 display probed\n");

	return 0;
regulator_voltage_failed:
	regulator_put(di->regulator);
regulator_get_failed:
	gpio_free(di->reset_gpio);
gpio_request_failed:
	kfree(di);
	return ret;
}

static struct mcde_display_driver samsung_s6d16d0_driver = {
	.probe	= samsung_s6d16d0_probe,
	.driver = {
		.name	= "samsung_s6d16d0",
	},
};

static int __init samsung_s6d16d0_init(void)
{
	return mcde_display_driver_register(&samsung_s6d16d0_driver);
}
module_init(samsung_s6d16d0_init);

MODULE_AUTHOR("Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson MCDE Samsung S6D16D0 display driver");
