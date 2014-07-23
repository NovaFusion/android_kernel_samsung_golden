/*
 * display-sec-HVA40WVA1_dis.c - Display driver for HVA40WVA1 panel
 *
 * Copyright (C) 2012 Samsung
 * Author: Robert Teather <robert.teather@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */


#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/backlight.h>

#include <linux/regulator/consumer.h>

#include <video/mcde_display.h>
#include <video/mcde_display-sec-dsi.h>

// #define USE_MTP is not set
// #define USE_ACL is not set

#define dev_dbg dev_info

#define VMODE_XRES		480
#define VMODE_YRES		800

#define MAX_BRIGHTNESS		255
#define DEFAULT_BRIGHTNESS	255

#define DCS_CMD_READ_PANEL_ID	0x04
#define DCS_CMD_COLMOD		0x3A	/* Set Pixel Format */
#define DCS_CMD_SET_BRIGHTNESS	0x51
#define DCS_CMD_READ_ID1	0xDA
#define DCS_CMD_READ_ID2	0xDB
#define DCS_CMD_READ_ID3	0xDC
#define DCS_CMD_MANU_CMD_SEL	0xF0

#define DCS_CMD_SEQ_DELAY_MS	0xFD
#define DCS_CMD_SEQ_END		0xFE

#define DCS_CMD_SEQ_PARAM(x)	(2+(x))	// Used to index parameters within a command sequence.



static const u8 DCS_CMD_SEQ_MTP_READ_PARAM_SETTING[] = {
/*	Length	Command 			Parameters */
	5,	0xFF,				0xAA, 0x55, 0x25, 0x01,
	13,	0xF8,				0x01, 0x02, 0x00, 0x20, 0x33,
						0x13, 0x00, 0x40, 0x00, 0x00,
						0x23, 0x02,
	DCS_CMD_SEQ_END
};

#define ROTATE_0_SETTING	0x40
#define ROTATE_180_SETTING	0x00

/* Power ON Seq */
/* ---------------- */
static const u8 DCS_CMD_SEQ_PWR_ON_INIT[] = {

/* Display Param */
/*	Length	Command			Parameters */
	9,	0xF3, /* */			0x00, 0x32, 0x00, 0x38, 0x31,
						0x08, 0x11, 0x00,
	6,	DCS_CMD_MANU_CMD_SEL,		0x55, 0xAA, 0x52, 0x08, 0x00,	/* Page 0 */
	3,	0xB1, /* DOPCTR */		0x4C, 0x04,
	2,	DCS_CMD_SET_ADDRESS_MODE,	ROTATE_0_SETTING,
	2,	DCS_CMD_COLMOD,			0x77,		/* 24 bits per pixel */
	2,	0xB5, /* DPRSLCTR */		0x50,		/* 480x800 */
	2,	0xB6, /* SDHDTCTR */		0x03,		/* Source data hold time */
	3,	0xB7, /* GSEQCTR */		0x70, 0x70,	/* Gate EQ */
	5,	0xB8, /* SDEQCTR */		0x00, 0x06, 0x06, 0x06,	/* Source EQ */
	4,	0xBC, /* INVCTR */		0x00, 0x00, 0x00,	/* Inversion Type */
	6,	0xBD, /* DPFRCTR1 */		0x01, 0x84, 0x06, 0x50, 0x00, /* Disp Timing */
	4,	0xCC, /* DPTMCTR12 */		0x03, 0x2A, 0x06,	/* aRD(Gateless) Setting */
	DCS_CMD_SEQ_DELAY_MS,			10,

/* POWER SET */
/*	Length	Command			Parameters */
	6,	DCS_CMD_MANU_CMD_SEL,		0x55, 0xAA, 0x52, 0x08, 0x01,	/* Page 1 */
	4,	0xB0, /* SETAVDD */		0x05, 0x05, 0x05,
	4,	0xB1, /* SETAVEE */		0x05, 0x05, 0x05,
	4,	0xB2, /* SETVCL */		0x03, 0x03, 0x03,	/* VCL Setting for LVGL */
	4,	0xB8, /* BT3CTR */		0x24, 0x24, 0x24,	/* Ctrl for VCL */
	4,	0xB3, /* SETVGH */		0x0A, 0x0A, 0x0A,
	4,	0xB9, /* BT4CTR */		0x24, 0x24, 0x24,	/* VGH boosting times/freq */
	2,	0xBF, /* VGHCTR */		0x01,			/* VGH output ctrl */
	4,	0xB5, /* SETVGL_REG */		0x08, 0x08, 0x08,
	4,	0xB4, /* SETVRGH */		0x2D, 0x2D, 0x2D,	/* VRGH voltage (VBIAS) */
	4,	0xBC, /* SETVPG */		0x00, 0x50, 0x00,	/* VGMP/VGSN */
	4,	0xBD, /* SETVGN */		0x00, 0x60, 0x00,	/* VGMN/VGSN */
	8,	0xCE,				0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00,		/* PWM Commands */
	DCS_CMD_SEQ_DELAY_MS,			10,

/* GAMMA CONTROL */
/*	Length	Command 		Parameters */
	5,	0xD0, /* GMGRDCTR */		0x0D, 0x15, 0x08, 0x0C,	/* Gradient Ctrl for Gamma */
	53,	0xD1, /* GMRCTR1  Gamma Correction for Red(positive) */
						0x00, 0x37, 0x00, 0x71, 0x00,
						0xA2, 0x00, 0xC4, 0x00, 0xDB,
						0x01, 0x01, 0x01, 0x40, 0x01,
						0x84, 0x01, 0xA9, 0x01, 0xD8,
						0x02, 0x0A, 0x02, 0x44, 0x02,
						0x85, 0x02, 0x87, 0x02, 0xBF,
						0x02, 0xE5, 0x03, 0x0F, 0x03,
						0x34, 0x03, 0x4F, 0x03, 0x73,
						0x03, 0x77, 0x03, 0x94, 0x03,
						0x9E, 0x03, 0xAC, 0x03,	0xBD,
						0x03, 0xF1,
	53,	0xD2, /* GMGCTR1  Gamma Correction for Green(positive) */
						0x00, 0x37, 0x00, 0x71, 0x00,
						0xA2, 0x00, 0xC4, 0x00, 0xDB,
						0x01, 0x01, 0x01, 0x40, 0x01,
						0x84, 0x01, 0xA9, 0x01, 0xD8,
						0x02, 0x0A, 0x02, 0x44, 0x02,
						0x85, 0x02, 0x87, 0x02, 0xBF,
						0x02, 0xE5, 0x03, 0x0F, 0x03,
						0x34, 0x03, 0x4F, 0x03, 0x73,
						0x03, 0x77, 0x03, 0x94, 0x03,
						0x9E, 0x03, 0xAC, 0x03,	0xBD,
						0x03, 0xF1,
	53,	0xD3, /* GMBCTR1  Gamma Correction for Blue(positive) */
						0x00, 0x37, 0x00, 0x71, 0x00,
						0xA2, 0x00, 0xC4, 0x00, 0xDB,
						0x01, 0x01, 0x01, 0x40, 0x01,
						0x84, 0x01, 0xA9, 0x01, 0xD8,
						0x02, 0x0A, 0x02, 0x44, 0x02,
						0x85, 0x02, 0x87, 0x02, 0xBF,
						0x02, 0xE5, 0x03, 0x0F, 0x03,
						0x34, 0x03, 0x4F, 0x03, 0x73,
						0x03, 0x77, 0x03, 0x94, 0x03,
						0x9E, 0x03, 0xAC, 0x03,	0xBD,
						0x03, 0xF1,
	53,	0xD4, /* GMRCTR2  Gamma Correction for Red(negative) */
						0x00, 0x37, 0x00, 0x46, 0x00,
						0x7E, 0x00, 0x9E, 0x00, 0xC2,
						0x01, 0x01, 0x01, 0x14, 0x01,
						0x4A, 0x01, 0x73, 0x01, 0xB8,
						0x01, 0xDF, 0x02, 0x2F, 0x02,
						0x68, 0x02, 0x6A, 0x02, 0xA3,
						0x02, 0xE0, 0x02, 0xF9, 0x03,
						0x25, 0x03, 0x43, 0x03, 0x6E,
						0x03, 0x77, 0x03, 0x94, 0x03,
						0x9E, 0x03, 0xAC, 0x03, 0xBD,
						0x03, 0xF1,
	53,	0xD5, /* GMGCTR2  Gamma Correction for Green(negative) */
						0x00, 0x37, 0x00, 0x46, 0x00,
						0x7E, 0x00, 0x9E, 0x00, 0xC2,
						0x01, 0x01, 0x01, 0x14, 0x01,
						0x4A, 0x01, 0x73, 0x01, 0xB8,
						0x01, 0xDF, 0x02, 0x2F, 0x02,
						0x68, 0x02, 0x6A, 0x02, 0xA3,
						0x02, 0xE0, 0x02, 0xF9, 0x03,
						0x25, 0x03, 0x43, 0x03, 0x6E,
						0x03, 0x77, 0x03, 0x94, 0x03,
						0x9E, 0x03, 0xAC, 0x03, 0xBD,
						0x03, 0xF1,
	53,	0xD6, /* GMBCTR2  Gamma Correction for Blue(negative) */
						0x00, 0x37, 0x00, 0x46, 0x00,
						0x7E, 0x00, 0x9E, 0x00, 0xC2,
						0x01, 0x01, 0x01, 0x14, 0x01,
						0x4A, 0x01, 0x73, 0x01, 0xB8,
						0x01, 0xDF, 0x02, 0x2F, 0x02,
						0x68, 0x02, 0x6A, 0x02, 0xA3,
						0x02, 0xE0, 0x02, 0xF9, 0x03,
						0x25, 0x03, 0x43, 0x03, 0x6E,
						0x03, 0x77, 0x03, 0x94, 0x03,
						0x9E, 0x03, 0xAC, 0x03, 0xBD,
						0x03, 0xF1,

	2,	0x51, /* WRDISBV */		0x6C,	/* PWM backlight brightness */
	2,	0x53, /* WRCTRLLD */		0x2C,	/* Backlight Ctrl */
	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_EXIT_SLEEP[] = {
	1,	DCS_CMD_EXIT_SLEEP_MODE,
	DCS_CMD_SEQ_DELAY_MS,			120,
	1,	DCS_CMD_SET_DISPLAY_ON,
	DCS_CMD_SEQ_DELAY_MS,			10,

	DCS_CMD_SEQ_END
};


struct hva40wv1_lcd {
	struct device			*dev;
	struct mutex			lock;
	unsigned int			current_brightness;
	struct mcde_display_device	*ddev;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct sec_dsi_platform_data	*pd;
	u8				lcd_id[3];
};

#define MAX_DCS_CMD_ALLOWED	(DSILINK_MAX_DSI_DIRECT_CMD_WRITE - 1)
static int hva40wv1_dsi_dcs_write_command(struct mcde_display_device *ddev, u8 cmd, u8* p_data, int len)
{
	int write_len;
	int ret = 0;

	write_len = len;
	if (write_len > MAX_DCS_CMD_ALLOWED)
		write_len = MAX_DCS_CMD_ALLOWED;

	ret = mcde_dsi_dcs_write(ddev->chnl_state, cmd, p_data, write_len);

	len -= write_len;
	p_data += write_len;

	while ((len > 0) && (ret == 0)) {

		write_len = len;
		if (write_len > MAX_DCS_CMD_ALLOWED)
			write_len = MAX_DCS_CMD_ALLOWED;

		ret = mcde_dsi_generic_write(ddev->chnl_state, p_data, write_len);

		len -= write_len;
		p_data += write_len;
	}

	if (ret != 0)
		dev_err(&ddev->dev, "failed to send DCS command (0x%x)\n", cmd);
	else
		dev_vdbg(&ddev->dev, "Sent DCS cmd (0x%x)\n", cmd);

	return ret;
}


static int hva40wv1_dsi_dcs_write_sequence(struct mcde_display_device *ddev,
							const u8 *p_seq)
{
	int ret = 0;

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = hva40wv1_dsi_dcs_write_command(ddev, p_seq[1],
				(u8 *)&p_seq[2], p_seq[0] - 1);
			p_seq += p_seq[0] + 1;
		}
	}

	return ret;
}

static int hva40wv1_update_brightness(struct mcde_display_device *ddev, int brightness)
{
	int ret = 0;
	u8 param[1];

	dev_info(&ddev->dev, "%s: brightness=%d\n", __func__, brightness);

	param[0] = brightness;
	ret = hva40wv1_dsi_dcs_write_command(ddev, DCS_CMD_SET_BRIGHTNESS,
						param, 1);
	return ret;
}

static int hva40wv1_get_brightness(struct backlight_device *bd)
{
	dev_dbg(&bd->dev, "lcd get brightness returns %d\n", bd->props.brightness);
	return bd->props.brightness;
}

static int hva40wv1_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	int brightness = bd->props.brightness;

	struct hva40wv1_lcd *lcd = bl_get_data(bd);

	dev_dbg(&bd->dev, "lcd set brightness called with %d\n", brightness);

	if ((brightness < 0) ||	(brightness > bd->props.max_brightness)) {
		dev_err(&bd->dev, "lcd brightness should be 0 to %d.\n",
			bd->props.max_brightness);
		return -EINVAL;
	}

	mutex_lock(&lcd->lock);

	if (lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) {

		ret = hva40wv1_update_brightness(lcd->ddev, brightness);
		if (ret) {
			dev_err(&bd->dev, "lcd brightness setting failed.\n");
			return -EIO;
		}
		if (lcd->pd->bl_en_gpio != -1)
			gpio_set_value(lcd->pd->bl_en_gpio, 1);

	} else if (lcd->pd->bl_en_gpio != -1) {
		gpio_set_value(lcd->pd->bl_en_gpio, 0);
	}

	lcd->current_brightness = brightness;

	mutex_unlock(&lcd->lock);

	return ret;
}


static struct backlight_ops hva40wv1_backlight_ops  = {
	.get_brightness = hva40wv1_get_brightness,
	.update_status = hva40wv1_set_brightness,
};

struct backlight_properties hva40wv1_backlight_props = {
	.brightness = DEFAULT_BRIGHTNESS,
	.max_brightness = MAX_BRIGHTNESS,
	.type = BACKLIGHT_RAW,
};

static int hva40wv1_power_on(struct hva40wv1_lcd *lcd)
{
	struct sec_dsi_platform_data *pd = lcd->pd;

	dev_dbg(lcd->dev, "%s: Power on display\n", __func__);

	if (pd->reset_gpio)
		gpio_direction_output(pd->reset_gpio, 0);

	if (pd->lcd_pwr_onoff) {
		pd->lcd_pwr_onoff(true);
		msleep(10);
	}

	if (pd->reset_gpio) {
		gpio_direction_output(pd->reset_gpio, 0);
		msleep(2);
		gpio_set_value(pd->reset_gpio, 1);
		msleep(20);
	}

	return 0;
}


static int hva40wv1_power_off(struct hva40wv1_lcd *lcd)
{
	struct sec_dsi_platform_data *pd = lcd->pd;
	int ret = 0;

	dev_dbg(lcd->dev, "%s: Power off display\n", __func__);

	if (pd->lcd_pwr_onoff)
		pd->lcd_pwr_onoff(false);

	if (pd->reset_gpio)
		gpio_direction_output(pd->reset_gpio, 0);

	return ret;
}

static int hva40wv1_dsi_read_panel_id(struct hva40wv1_lcd *lcd)
{
	int ret = 0;
	int len = 1;

	if (lcd->lcd_id[0] == 0x00) {
		dev_dbg(&lcd->ddev->dev, "%s: Read device id of the display\n", __func__);

		ret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID1,
						(u32 *)&lcd->lcd_id[0], &len);
		ret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID2,
						(u32 *)&lcd->lcd_id[1], &len);
		ret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID3,
						(u32 *)&lcd->lcd_id[2], &len);
		if (ret)
			dev_info(&lcd->ddev->dev,
				"mcde_dsi_dcs_read failed (%d) to read display ID (len %d)\n",
				ret, len);
	}
	return ret;
}

static int hva40wv1_display_init(struct hva40wv1_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_dbg(lcd->dev, "%s: Initialise display\n", __func__);

	ret |= hva40wv1_dsi_dcs_write_sequence(ddev, DCS_CMD_SEQ_MTP_READ_PARAM_SETTING);
	ret |= hva40wv1_dsi_read_panel_id(lcd);
	ret |= hva40wv1_dsi_dcs_write_sequence(ddev, DCS_CMD_SEQ_PWR_ON_INIT);
	if (lcd->pd->bl_ctrl)
		ret |= hva40wv1_update_brightness(ddev, lcd->current_brightness);

	return 0; /* assume OK in case of manufacturing test */
}

static int hva40wv1_display_sleep(struct hva40wv1_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;
	int len = 1;

	dev_dbg(lcd->dev, "%s: display sleep\n", __func__);

	ret = hva40wv1_dsi_dcs_write_command(ddev, DCS_CMD_SET_DISPLAY_OFF, NULL, 0);

	if (ret == 0) {
		msleep(10);
		ret = hva40wv1_dsi_dcs_write_command(ddev, DCS_CMD_ENTER_SLEEP_MODE, NULL, 0);
		msleep(5);
	}
	return ret;
}

static int hva40wv1_display_exit_sleep(struct hva40wv1_lcd *lcd)
{
	int ret;

	dev_dbg(lcd->dev, "%s: display exit sleep\n", __func__);

	ret = hva40wv1_dsi_dcs_write_sequence(lcd->ddev, DCS_CMD_SEQ_EXIT_SLEEP);

	return ret;
}

#ifdef CONFIG_LCD_CLASS_DEVICE
static int hva40wv1_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode);
static int hva40wv1_power(struct hva40wv1_lcd *lcd, int power)
{
	int ret = 0;

	switch (power) {
	case FB_BLANK_POWERDOWN:
		dev_dbg(lcd->dev, "%s(): Powering Off, was %s\n",__func__,
			(lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) ? "ON" : "OFF");
		ret = hva40wv1_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_OFF);
		break;
	case FB_BLANK_NORMAL:
		dev_dbg(lcd->dev, "%s(): Into Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_ON) ? "ON" : "SLEEP/OFF");
		ret = hva40wv1_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_STANDBY);
		break;
	case FB_BLANK_UNBLANK:
		dev_dbg(lcd->dev, "%s(): Exit Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_STANDBY) ? "SLEEP" : "ON/OFF");
		ret = hva40wv1_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_ON);
		break;
	default:
		ret = -EINVAL;
		dev_info(lcd->dev, "Invalid power change request (%d)\n", power);
		break;
	}
	return ret;
}

static int hva40wv1_set_power(struct lcd_device *ld, int power)
{
	struct hva40wv1_lcd *lcd = lcd_get_data(ld);

	dev_dbg(lcd->dev, "%s: power=%d\n", __func__, power);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return hva40wv1_power(lcd, power);
}

static int hva40wv1_get_power(struct lcd_device *ld)
{
	struct hva40wv1_lcd *lcd = lcd_get_data(ld);
	int power;

	switch (lcd->ddev->power_mode) {
	case MCDE_DISPLAY_PM_OFF:
		power = FB_BLANK_POWERDOWN;
		break;
	case MCDE_DISPLAY_PM_STANDBY:
		power = FB_BLANK_NORMAL;
		break;
	case MCDE_DISPLAY_PM_ON:
		power = FB_BLANK_UNBLANK;
		break;
	default:
		power = -1;
		break;
	}
	return power;
}

static struct lcd_ops hva40wv1_lcd_ops = {
	.set_power = hva40wv1_set_power,
	.get_power = hva40wv1_get_power,
};
#endif // CONFIG_LCD_CLASS_DEVICE


static int hva40wv1_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	int ret;
	enum mcde_display_rotation final;
	struct hva40wv1_lcd *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_hw_rotation final_hw_rot;
	u8 data = 0;
	int len = 1;

	final = (360 + rotation - ddev->orientation) % 360;

	switch (final) {
	case MCDE_DISPLAY_ROT_180:	/* handled by LDI */
	case MCDE_DISPLAY_ROT_0:
		final_hw_rot = MCDE_HW_ROT_0;
		break;
	case MCDE_DISPLAY_ROT_90_CW:	/* handled by MCDE */
		final_hw_rot = MCDE_HW_ROT_90_CW;
		break;
	case MCDE_DISPLAY_ROT_90_CCW:	/* handled by MCDE */
		final_hw_rot = MCDE_HW_ROT_90_CCW;
		break;
	default:
		return -EINVAL;
	}

	ret = mcde_dsi_dcs_read(ddev->chnl_state, 0x0B, (u32 *)&data, &len);
	if (!ret)
		dev_dbg(&ddev->dev, "MADCTL before = 0x%x\n", data);
	if (final == MCDE_DISPLAY_ROT_180) {
		data = ROTATE_180_SETTING;
		ret = hva40wv1_dsi_dcs_write_command(ddev,
						DCS_CMD_SET_ADDRESS_MODE,
						&data, 1);
		dev_dbg(lcd->dev, "%s: Display rotated 180\n", __func__);
	} else if (final == MCDE_DISPLAY_ROT_0) {
		data = ROTATE_0_SETTING;
		ret = hva40wv1_dsi_dcs_write_command(ddev,
						DCS_CMD_SET_ADDRESS_MODE,
						&data, 1);
		dev_dbg(lcd->dev, "%s: Display rotated 0\n", __func__);
		ret = mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
	} else {
		ret = mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
	}
	if (WARN_ON(ret))
		return ret;

	data = 0;
	ret = mcde_dsi_dcs_read(ddev->chnl_state, 0x0B, (u32 *)&data, &len);
	if (!ret)
		dev_dbg(&ddev->dev, "MADCTL after = 0x%x\n", data);

	ddev->rotation = rotation;
	ddev->update_flags |= UPDATE_FLAG_ROTATION;

	return 0;
}

static int hva40wv1_apply_config(struct mcde_display_device *ddev)
{
	int ret;

	if (!ddev->update_flags)
		return 0;

	if (ddev->update_flags & UPDATE_FLAG_ROTATION)
		mcde_chnl_stop_flow(ddev->chnl_state);

	ret = mcde_chnl_apply(ddev->chnl_state);
	if (ret < 0) {
		dev_warn(&ddev->dev, "%s:Failed to apply to channel\n",
							__func__);
		return ret;
	}

	ddev->update_flags = 0;
	ddev->first_update = true;

	return 0;
}

static int hva40wv1_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode)
{
	struct hva40wv1_lcd *lcd = dev_get_drvdata(&ddev->dev);
	int ret = 0;

	dev_dbg(&ddev->dev, "%s: power_mode = %d\n", __func__, power_mode);

	/* OFF -> STANDBY or OFF -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {
		ret = hva40wv1_power_on(lcd);
		if (ret)
			return ret;

		ret = hva40wv1_display_init(lcd);
		if (ret)
			return ret;

		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_ON) {

		ret = hva40wv1_display_exit_sleep(lcd);
		if (ret)
			return ret;

		ddev->power_mode = MCDE_DISPLAY_PM_ON;
	}
	/* ON -> STANDBY */
	else if (ddev->power_mode == MCDE_DISPLAY_PM_ON &&
					power_mode <= MCDE_DISPLAY_PM_STANDBY) {

		ret = hva40wv1_display_sleep(lcd);
		if (ret && (power_mode != MCDE_DISPLAY_PM_OFF))
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_OFF) {
		ret = hva40wv1_power_off(lcd);
		if (ret)
			return ret;

		ddev->power_mode = MCDE_DISPLAY_PM_OFF;
	}

	return mcde_chnl_set_power_mode(ddev->chnl_state, ddev->power_mode);
}


static int __devinit hva40wv1_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct sec_dsi_platform_data *pdata = ddev->dev.platform_data;
	struct hva40wv1_lcd *lcd = NULL;
	struct backlight_device *bd = NULL;

	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	if (pdata == NULL) {
		dev_err(&ddev->dev, "%s: Platform data missing\n", __func__);
		return -EINVAL;
	}

	if (ddev->port->type != MCDE_PORTTYPE_DSI) {
		dev_err(&ddev->dev, "%s: Invalid port type %d\n", __func__,
							ddev->port->type);
		return -EINVAL;
	}

	if (pdata->reset_gpio) {
		ret = gpio_request(pdata->reset_gpio, "LCD Reset");
		if (ret) {
			dev_err(&ddev->dev, "%s: Failed to request gpio %d\n",
						__func__, pdata->reset_gpio);
			goto request_reset_gpio_failed;
		}
	}
	if (pdata->lcd_pwr_setup)
		pdata->lcd_pwr_setup(&ddev->dev);


	ddev->set_power_mode = hva40wv1_set_power_mode;
	ddev->set_rotation = hva40wv1_set_rotation;
	ddev->apply_config = hva40wv1_apply_config;

	ddev->native_x_res = VMODE_XRES;
	ddev->native_y_res = VMODE_YRES;

	lcd = kzalloc(sizeof(struct hva40wv1_lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	dev_set_drvdata(&ddev->dev, lcd);
	lcd->ddev = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;
	memcpy(lcd->lcd_id, pdata->lcdId, 3);

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("hva40wv1", &ddev->dev,
					lcd, &hva40wv1_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);

		dev_err(&ddev->dev, "%s: Failed to register hva40wv1 display device\n", __func__);

		goto out_free_lcd;
	}
#endif

	mutex_init(&lcd->lock);

	if (pdata->bl_ctrl) {
		if (pdata->bl_en_gpio != -1) {
			ret = gpio_request(pdata->bl_en_gpio, "LCD BL EN");
			if (ret) {
				dev_err(&ddev->dev,
					"%s: Failed to request gpio %d\n",
					__func__, pdata->bl_en_gpio);
				goto backlight_device_register_failed;
			}
		}
		bd = backlight_device_register("panel",
						&ddev->dev,
						lcd,
						&hva40wv1_backlight_ops,
						&hva40wv1_backlight_props);
		if (IS_ERR(bd)) {
			ret =  PTR_ERR(bd);
			goto backlight_device_register_failed;
		}
		lcd->bd = bd;
	}

	goto out;

backlight_device_register_failed:

#ifdef CONFIG_LCD_CLASS_DEVICE
out_free_lcd:
#endif
	kfree(lcd);

	if (pdata->reset_gpio)
		gpio_free(pdata->reset_gpio);

request_reset_gpio_failed:
out:
	return ret;
}

static int __devexit hva40wv1_remove(struct mcde_display_device *ddev)
{
	struct hva40wv1_lcd *lcd = dev_get_drvdata(&ddev->dev);
	struct sec_dsi_platform_data *pdata = lcd->pd;

	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);

	if (pdata->reset_gpio) {
		gpio_direction_input(pdata->reset_gpio);
		gpio_free(pdata->reset_gpio);
	}

	kfree(lcd);

	return 0;
}


#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
static int hva40wv1_resume(struct mcde_display_device *ddev)
{
	int ret;

	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	/* set_power_mode will handle call platform_enable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);
	ddev->set_synchronized_update(ddev,
					ddev->get_synchronized_update(ddev));
	return ret;
}

static int hva40wv1_suspend(struct mcde_display_device *ddev, \
							pm_message_t state)
{
	int ret;

	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);
	return ret;
}
#endif

static struct mcde_display_driver hva40wv1_driver = {
	.probe	= hva40wv1_probe,
	.remove = hva40wv1_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = hva40wv1_suspend,
	.resume = hva40wv1_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.driver = {
		.name	= "mcde_disp_hva40wv1",
	},
};

/* Module init */
static int __init mcde_display_hva40wv1_init(void)
{
	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&hva40wv1_driver);
}
module_init(mcde_display_hva40wv1_init);

static void __exit mcde_display_hva40wv1_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&hva40wv1_driver);
}
module_exit(mcde_display_hva40wv1_exit);

MODULE_AUTHOR("Robert Teather <robert.teather@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung MCDE HVA40WV1 panel display driver");
