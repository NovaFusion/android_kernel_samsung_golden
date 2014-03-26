/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson himax hx8392 display driver
 *
 * Author: Jimmy Rubin <jimmy.rubin@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/regulator/consumer.h>

#include <video/mcde_display.h>

#define RESET_DELAY_US		11000
#define RESET_LOW_DELAY_US	20
#define SLEEP_OUT_DELAY_MS	150
#define SLEEP_IN_DELAY_MS	150
#define DELAY_US		11000
#define IO_REGU			"vddi"
#define IO_REGU_MIN		1600000
#define IO_REGU_MAX		3300000

#define DSI_HS_FREQ_HZ		570000000
#define DSI_LP_FREQ_HZ		19200000

#define DCS_CMD_SET_POWER	0xB1
#define DCS_CMD_SET_DISP	0xB2
#define DCS_CMD_SET_MPU_CYC	0xB4
#define DCS_CMD_SET_VGH		0xB5
#define DCS_CMD_SET_EXTC	0xB9
#define DCS_CMD_SET_MIPI	0xBA
#define DCS_CMD_SET_DSIMO	0xC2
#define DCS_CMD_SET_BLANK_2	0xC7
#define DCS_CMD_SET_PANEL	0xCC
#define DCS_CMD_SET_EQ		0xD4
#define DCS_CMD_SET_LTPS_CTRL	0xD5
#define DCS_CMD_SET_RGB_CYC	0xD8
#define DCS_CMD_SET_GAMMA	0xE0
#define DCS_CMD_SET_GGAMMA	0xE1
#define DCS_CMD_SET_BGAMMA	0xE2

#define MAX_PARAMETERS		0x34 /* Gamma table is the biggest one */

#define MODIFY_GAMMA		0

/* Default gamma table extracted from HX8392-A LCD IC datasheet.
 * Please update this table with your prefered gamma values.
 * Do not forget to set MODIFY_GAMMA accordingly... */
static u8 default_gamma[] = {
0x04, 0x0C, 0x0D, 0x0A, 0x15, 0x21, 0x0D, 0x19, 0x06, 0x0C,
0x0F, 0x13, 0x16, 0x14, 0x15, 0x0D, 0x13, 0x04, 0x0C, 0x0D,
0x0A, 0x15, 0x21, 0x0D, 0x19, 0x06, 0x0C, 0x0F, 0x13, 0x16,
0x14, 0x15, 0x0D, 0x13};


struct device_info {
	int reset_gpio;
	struct mcde_port port;
	struct regulator *regulator;
};

static inline struct device_info *get_drvdata(struct mcde_display_device *ddev)
{
	return (struct device_info *)dev_get_drvdata(&ddev->dev);
}

static int display_read_deviceid(struct mcde_display_device *dev, u16 *id)
{
	struct mcde_chnl_state *chnl;

	u8  id1, id2, id3;
	int len = 1;
	int ret = 0;
	int readret = 0;

	dev_dbg(&dev->dev, "%s: Read device id of the display\n", __func__);

	/* Acquire MCDE resources */
	chnl = mcde_chnl_get(dev->chnl_id, dev->fifo, dev->port);
	if (IS_ERR(chnl)) {
		ret = PTR_ERR(chnl);
		dev_warn(&dev->dev, "Failed to acquire MCDE channel\n");
		goto out;
	}

	/* plugnplay: use registers DA, DBh and DCh to detect display */
	readret = mcde_dsi_dcs_read(chnl, 0xDA, (u32 *)&id1, &len);
	if (!readret)
		readret = mcde_dsi_dcs_read(chnl, 0xDB, (u32 *)&id2, &len);
	if (!readret)
		readret = mcde_dsi_dcs_read(chnl, 0xDC, (u32 *)&id3, &len);

	if (readret) {
		dev_info(&dev->dev,
			"mcde_dsi_dcs_read failed to read display ID\n");
		goto read_fail;
	}

	*id = (id3 << 8) | id2;
read_fail:
	/* close  MCDE channel */
	mcde_chnl_put(chnl);
out:
	return 0;
}

static int power_on(struct mcde_display_device *dev)
{
	struct device_info *di = get_drvdata(dev);

	dev_dbg(&dev->dev,
		"%s: Reset & power on himax_hx8392 display\n", __func__);

	regulator_enable(di->regulator);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);
	gpio_set_value_cansleep(di->reset_gpio, 1);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);
	gpio_set_value_cansleep(di->reset_gpio, 0);
	udelay(RESET_LOW_DELAY_US);
	gpio_set_value_cansleep(di->reset_gpio, 1);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);

	return 0;
}

static int power_off(struct mcde_display_device *dev)
{
	struct device_info *di = get_drvdata(dev);

	dev_dbg(&dev->dev,
		"%s:Reset & power off himax_hx8392 display\n", __func__);

	gpio_set_value_cansleep(di->reset_gpio, 0);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);
	regulator_disable(di->regulator);

	return 0;
}

static int display_on(struct mcde_display_device *ddev)
{
	int ret;
	u8 wrbuf[MAX_PARAMETERS] = {0};

	dev_dbg(&ddev->dev, "Display on himax_hx8392 display\n");

	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_EXIT_SLEEP_MODE,
								NULL, 0);
	if (ret)
		return ret;

	msleep(SLEEP_OUT_DELAY_MS);

	/* Set Password */
	wrbuf[0] = 0xFF;
	wrbuf[1] = 0x83;
	wrbuf[2] = 0x92;
	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_EXTC, wrbuf, 3);
	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	wrbuf[0] = 0x0;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_TEAR_ON, wrbuf, 1);
	if (ret)
		dev_warn(&ddev->dev,
			"%s:Failed to enable synchronized update\n", __func__);

	usleep_range(DELAY_US, DELAY_US);

	wrbuf[0] = 0xA9;
	wrbuf[1] = 0x18;
	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_VGH, wrbuf, 2);
	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* Set Power */
	/* VSN_EN, VSP_EN, VGL_EN, VGH_EN, VDDD_N_HZ */
	wrbuf[0] = 0x7C;
	wrbuf[1] = 0x00;
	/* FS1 & AP */
	wrbuf[2] = 0x44;
	/* VGHS = 4 VGLS = 5 */
	wrbuf[3] = 0x45;
	wrbuf[4] = 0x00;
	/* BTP = 16 */
	wrbuf[5] = 0x10;
	/* BTN = 16 */
	wrbuf[6] = 0x10;
	/* VRHP = 18 */
	wrbuf[7] = 0x12;
	/* VRHN = 31 */
	wrbuf[8] = 0x1F;
	/* VRMP = 63 */
	wrbuf[9] = 0x3F;
	/* VRMN = 63 */
	wrbuf[10] = 0x3F;
	/* APF_EN, PCCS = 2 */
	wrbuf[11] = 0x42;
	/* DC86_DIV = 7, XDK0 */
	wrbuf[12] = 0x72;
	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_POWER,
								wrbuf, 13);
	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* Set Display related registers */
	/* D = Display source GON, DTE = EN */
	wrbuf[0] = 0x0F;
	wrbuf[1] = 0xC8;
	wrbuf[2] = 0x05;
	wrbuf[3] = 0x0F;
	wrbuf[4] = 0x08;
	wrbuf[5] = 0x84;
	wrbuf[6] = 0x00;
	wrbuf[7] = 0xFF;
	wrbuf[8] = 0x05;
	wrbuf[9] = 0x0F;
	wrbuf[10] = 0x04;
	/* 720 x 1280 */
	wrbuf[11] = 0x20;
	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_DISP,
								wrbuf, 12);
	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* Set Command CYC */
	wrbuf[0]  = 0x00;
	wrbuf[1]  = 0x00;
	wrbuf[2]  = 0x05;
	wrbuf[3]  = 0x00;
	wrbuf[4]  = 0xA0;
	wrbuf[5]  = 0x05;
	wrbuf[6]  = 0x16;
	wrbuf[7]  = 0x9D;
	wrbuf[8]  = 0x30;
	wrbuf[9]  = 0x03;
	wrbuf[10] = 0x16;
	wrbuf[11] = 0x00;
	wrbuf[12] = 0x03;
	wrbuf[13] = 0x03;
	wrbuf[14] = 0x00;
	wrbuf[15] = 0x1B;
	wrbuf[16] = 0x06;
	wrbuf[17] = 0x07;
	wrbuf[18] = 0x07;
	wrbuf[19] = 0x00;

	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_MPU_CYC, wrbuf, 20);

	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* 3 datalanes, 12 Mhz low power clock */
	wrbuf[0] = 0x12;
	wrbuf[1] = 0x82;

	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_MIPI, wrbuf, 2);
	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* Use internal GRAM */
	wrbuf[0] = 0x08;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_DSIMO, wrbuf, 1);

	usleep_range(DELAY_US, DELAY_US);

	wrbuf[0] = 0x00;
	wrbuf[1] = 0x40;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_BLANK_2, wrbuf, 2);
	usleep_range(DELAY_US, DELAY_US);

	/* SS_PANEL */
	wrbuf[0] = 0x08;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_PANEL, wrbuf, 1);

	usleep_range(DELAY_US, DELAY_US);

	wrbuf[0] = 0x0C;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SET_EQ, wrbuf, 1);

	usleep_range(DELAY_US, DELAY_US);

	/* Set LTPS control output */
	wrbuf[0]  = 0x00;
	wrbuf[1]  = 0x08;
	wrbuf[2]  = 0x08;
	wrbuf[3]  = 0x00;
	wrbuf[4]  = 0x44;
	wrbuf[5]  = 0x55;
	wrbuf[6]  = 0x66;
	wrbuf[7]  = 0x77;
	wrbuf[8]  = 0xCC;
	wrbuf[9]  = 0xCC;
	wrbuf[10] = 0xCC;
	wrbuf[11] = 0xCC;
	wrbuf[12] = 0x00;
	wrbuf[13] = 0x77;
	wrbuf[14] = 0x66;
	wrbuf[15] = 0x55;
	wrbuf[16] = 0x44;
	wrbuf[17] = 0xCC;
	wrbuf[18] = 0xCC;
	wrbuf[19] = 0xCC;
	wrbuf[20] = 0xCC;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
					DCS_CMD_SET_LTPS_CTRL, wrbuf, 21);

	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* Set Video CYC */
	wrbuf[0]  = 0x00;
	wrbuf[1]  = 0x00;
	wrbuf[2]  = 0x04;
	wrbuf[3]  = 0x00;
	wrbuf[4]  = 0xA0;
	wrbuf[5]  = 0x04;
	wrbuf[6]  = 0x16;
	wrbuf[7]  = 0x9D;
	wrbuf[8]  = 0x30;
	wrbuf[9]  = 0x03;
	wrbuf[10] = 0x16;
	wrbuf[11] = 0x00;
	wrbuf[12] = 0x03;
	wrbuf[13] = 0x03;
	wrbuf[14] = 0x00;
	wrbuf[15] = 0x1B;
	wrbuf[16] = 0x06;
	wrbuf[17] = 0x07;
	wrbuf[18] = 0x07;
	wrbuf[19] = 0x00;
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
					DCS_CMD_SET_RGB_CYC, wrbuf, 20);

	if (ret)
		return ret;

	usleep_range(DELAY_US, DELAY_US);

	/* User gamma if any */
	if (MODIFY_GAMMA) {
		dev_dbg(&ddev->dev, "Set the gamma table\n");
		ret = mcde_dsi_dcs_write(ddev->chnl_state,
					DCS_CMD_SET_GAMMA, default_gamma, 34);
		ret += mcde_dsi_dcs_write(ddev->chnl_state,
					DCS_CMD_SET_GGAMMA, default_gamma, 34);
		ret += mcde_dsi_dcs_write(ddev->chnl_state,
					DCS_CMD_SET_BGAMMA, default_gamma, 34);

		usleep_range(DELAY_US, DELAY_US);
		if (ret)
			return ret;
	}

	/* Display on */
	ret = mcde_dsi_dcs_write(ddev->chnl_state,
					DCS_CMD_SET_DISPLAY_ON, NULL, 0);

	if (ret)
		return ret;

	msleep(SLEEP_OUT_DELAY_MS);

	return 0;
}

static int display_off(struct mcde_display_device *ddev)
{
	int ret;

	dev_dbg(&ddev->dev, "Display off himax_hx8392 display\n");

	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_SET_DISPLAY_OFF,
								NULL, 0);
	if (ret)
		return ret;

	msleep(SLEEP_IN_DELAY_MS);

	ret = mcde_dsi_dcs_write(ddev->chnl_state, DCS_CMD_ENTER_SLEEP_MODE,
								NULL, 0);
	msleep(SLEEP_IN_DELAY_MS);

	return ret;
}

static int himax_hx8392_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode)
{
	int ret = 0;

	dev_dbg(&ddev->dev, "%s:Set Power mode\n", __func__);

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

static int __devinit himax_hx8392_probe(struct mcde_display_device *dev)
{
	int ret = 0;
	u16 id = 0;
	struct device_info *di;
	struct mcde_port *port;
	struct mcde_display_dsi_platform_data *pdata = dev->dev.platform_data;

	if (pdata == NULL || !pdata->reset_gpio) {
		dev_err(&dev->dev, "Invalid platform data\n");
		return -EINVAL;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	port = dev->port;
	di->reset_gpio = pdata->reset_gpio;
	di->port.type = MCDE_PORTTYPE_DSI;
	di->port.mode = MCDE_PORTMODE_CMD;
	di->port.pixel_format = MCDE_PORTPIXFMT_DSI_24BPP;
	di->port.sync_src = dev->port->sync_src;
	di->port.frame_trig = dev->port->frame_trig;
	di->port.phy.dsi.num_data_lanes = pdata->num_data_lanes;
	di->port.link = pdata->link;
	di->port.phy.dsi.host_eot_gen = true;
	di->port.phy.dsi.hs_freq = DSI_HS_FREQ_HZ;
	di->port.phy.dsi.lp_freq = DSI_LP_FREQ_HZ;

	ret = gpio_request(di->reset_gpio, NULL);
	if (WARN_ON(ret))
		goto gpio_request_failed;

	gpio_direction_output(di->reset_gpio, 1);
	di->regulator = regulator_get(&dev->dev, IO_REGU);
	if (IS_ERR(di->regulator)) {
		ret = PTR_ERR(di->regulator);
		di->regulator = NULL;
		goto regulator_get_failed;
	}
	ret = regulator_set_voltage(di->regulator, IO_REGU_MIN, IO_REGU_MAX);
	if (WARN_ON(ret))
		goto regulator_voltage_failed;

	dev->set_power_mode = himax_hx8392_set_power_mode;

	dev->port = &di->port;
	dev->native_x_res = 720;
	dev->native_y_res = 1280;
	dev_set_drvdata(&dev->dev, di);

	/*
	* When u-boot has display a startup screen.
	* U-boot has turned on display power however the
	* regulator framework does not know about that
	* This is the case here, the display driver has to
	* enable the regulator for the display.
	*/
	if (dev->power_mode != MCDE_DISPLAY_PM_OFF) {
		(void) regulator_enable(di->regulator);
	} else {
		power_on(dev);
		dev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	ret = display_read_deviceid(dev, &id);
	if (ret)
		goto read_id_failed;

	dev_info(&dev->dev,
			"Chimei 990001267 display (ID 0x%.4X) probed\n", id);

	return 0;

read_id_failed:
regulator_voltage_failed:
	regulator_put(di->regulator);
regulator_get_failed:
	gpio_free(di->reset_gpio);
gpio_request_failed:
	kfree(di);
	return ret;
}

static int __devexit himax_hx8392_remove(struct mcde_display_device *dev)
{
	struct device_info *di = get_drvdata(dev);

	dev->set_power_mode(dev, MCDE_DISPLAY_PM_OFF);

	regulator_put(di->regulator);
	gpio_direction_input(di->reset_gpio);
	gpio_free(di->reset_gpio);

	kfree(di);

	return 0;
}

#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
static int himax_hx8392_resume(struct mcde_display_device *ddev)
{
	int ret;

	/* set_power_mode will handle call platform_enable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);
	return ret;
}

static int himax_hx8392_suspend(struct mcde_display_device *ddev, \
							pm_message_t state)
{
	int ret;

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);
	return ret;
}
#endif

static struct mcde_display_driver himax_hx8392_driver = {
	.probe	= himax_hx8392_probe,
	.remove = himax_hx8392_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = himax_hx8392_suspend,
	.resume = himax_hx8392_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.driver = {
		.name	= "himax_hx8392",
	},
};

/* Module init */
static int __init mcde_display_himax_hx8392_init(void)
{
	return mcde_display_driver_register(&himax_hx8392_driver);
}
module_init(mcde_display_himax_hx8392_init);

MODULE_AUTHOR("Jimmy Rubin <jimmy.rubin@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson MCDE himax_hx8392 display driver");
