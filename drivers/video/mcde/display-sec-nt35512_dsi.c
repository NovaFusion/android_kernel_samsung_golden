/*
 * display-sec-nt35512_dis.c - Display driver for BOE WVGA panel (NT35512)
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
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <video/mcde_display.h>
#include <video/mcde_display-sec-dsi.h>


// #define dev_dbg dev_info

#define READ_CRC_ERRORS_DEBUG

#define NT35512_DRIVER_NAME	"mcde_disp_nt35512"

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


static const u8 BOE_WVGA_DCS_CMD_SEQ_POWER_SETTING[] = {
/*	Length	Command 			Parameters */
	/* Enable CMD2 Page1 */
	6,	0xF0,				0x55, 0xAA, 0x52, 0x08, 0x01,
	4,	0xB0,				0x09, 0x09, 0x09,
	4,	0xB6,				0x34, 0x34, 0x34,
	4,	0xB1,				0x09, 0x09, 0x09,
	4,	0xB7,				0x24, 0x24, 0x24,
	4,	0xB3,				0x05, 0x05, 0x05,
	4,	0xB9,				0x24, 0x24, 0x24,
	2,	0xBF,				0x01,
	4,	0xB5,				0x0B, 0x0B, 0x0B,
	4,	0xBA,				0x24, 0x24, 0x24,
	2,	0xC2,				0x01,
	4,	0xBC,				0x00, 0x90, 0x00,
	4,	0xBD,				0x00, 0x90, 0x00,
	DCS_CMD_SEQ_DELAY_MS,			150,
	DCS_CMD_SEQ_END
};

#define BOE_WVGA_GAMMA_CTR1			0x00, 0x37, 0x00, 0x51, 0x00, \
						0x71, 0x00, 0x96, 0x00, 0xAA, \
						0x00, 0xD3, 0x00, 0xF0, 0x01, \
						0x1D, 0x01, 0x45, 0x01, 0x84, \
						0x01, 0xB5, 0x02, 0x02, 0x02, \
						0x46, 0x02, 0x48, 0x02, 0x80, \
						0x02, 0xC0, 0x02, 0xE8, 0x03, \
						0x14, 0x03, 0x32, 0x03, 0x5D, \
						0x03, 0x73, 0x03, 0x91, 0x03, \
						0xA0, 0x03, 0xBF, 0x03, 0xCF, \
						0x03, 0xEF

#define BOE_WVGA_GAMMA_CTR2			0x00, 0x37, 0x00, 0x51, 0x00, \
						0x71, 0x00, 0x96, 0x00, 0xAA, \
						0x00, 0xD3, 0x00, 0xF0, 0x01, \
						0x1D, 0x01, 0x45, 0x01, 0x84, \
						0x01, 0xB5, 0x02, 0x02, 0x02, \
						0x46, 0x02, 0x48, 0x02, 0x80, \
						0x02, 0xC0, 0x02, 0xE8, 0x03, \
						0x14, 0x03, 0x32, 0x03, 0x5D, \
						0x03, 0x73, 0x03, 0x91, 0x03, \
						0xA0, 0x03, 0xBF, 0x03, 0xCF, \
						0x03, 0xEF

static const u8 BOE_WVGA_DCS_CMD_SEQ_GAMMA_SETTING[] = {
/*	Length	Command 			Parameters */
	53,	0xD1, /* GMRCTR1  Gamma Correction for Red(positive) */
						BOE_WVGA_GAMMA_CTR1,
	53,	0xD2, /* GMGCTR1  Gamma Correction for Green(positive) */
						BOE_WVGA_GAMMA_CTR1,
	53,	0xD3, /* GMBCTR1  Gamma Correction for Blue(positive) */
						BOE_WVGA_GAMMA_CTR1,
	53,	0xD4, /* GMRCTR2  Gamma Correction for Red(negative) */
						BOE_WVGA_GAMMA_CTR2,
	53,	0xD5, /* GMGCTR2  Gamma Correction for Green(negative) */
						BOE_WVGA_GAMMA_CTR2,
	53,	0xD6, /* GMBCTR2  Gamma Correction for Blue(negative) */
						BOE_WVGA_GAMMA_CTR2,
	DCS_CMD_SEQ_END
};

static const u8 BOE_WVGA_DCS_CMD_SEQ_INIT[] = {
/*	Length	Command 			Parameters */
	6,	0xF0, /* Page 0 */		0x55, 0xAA, 0x52, 0x08, 0x00,
	2,	0xB6,				0x0A,
	3,	0xB7,				0x00, 0x00,
	5,	0xB8,				0x01, 0x05, 0x05, 0x05,
	2,	0xBA,				0x01,
	4,	0xBC,				0x00, 0x00, 0x00,
	6,	0xBD,				0x01, 0x4D, 0x07, 0x32, 0x00,
	6,	0xBE,				0x01, 0x84, 0x07, 0x31, 0x00,
	6,	0xBF,				0x01, 0x84, 0x07, 0x31, 0x00,
	4,	0xCC,				0x03, 0x00, 0x00,
	3,	0xB1,				0xF8, 0x06,
	2,	0x35,				0x00,
	DCS_CMD_SEQ_END
};

static const u8 BOE_WVGA_DCS_CMD_SEQ_EXIT_SLEEP[] = {
	1,	DCS_CMD_EXIT_SLEEP_MODE,
	DCS_CMD_SEQ_DELAY_MS,			150,
	1,	DCS_CMD_SET_DISPLAY_ON,
	DCS_CMD_SEQ_DELAY_MS,			10,

	DCS_CMD_SEQ_END
};

static const u8 BOE_WVGA_DCS_CMD_SEQ_ROTATE_0[] = {
	/* flip horizontal & vertical as display physically upside down */
	3,	0xB1,				0xF8, 0x06,
	DCS_CMD_SEQ_END
};

static const u8 BOE_WVGA_DCS_CMD_SEQ_ROTATE_180[] = {
	3,	0xB1,				0xF8, 0x00,
	DCS_CMD_SEQ_END
};

struct kyle_lcd_data {
	struct device			*dev;
	struct mutex			lock;
	struct mcde_display_device	*ddev;
	struct lcd_device		*ld;
	struct sec_dsi_platform_data	*pd;
	u8				lcd_id[3];
	bool 				opp_is_requested;
	bool				justStarted;
	enum mcde_display_rotation	rotation;
};

#define MAX_DCS_CMD_ALLOWED	(DSILINK_MAX_DSI_DIRECT_CMD_WRITE - 1)
static int dsi_dcs_write_command(struct mcde_display_device *ddev, u8 cmd, u8* p_data, int len)
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

static int dsi_dcs_write_sequence(struct mcde_display_device *ddev,
							const u8 *p_seq)
{
	int ret = 0;

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = dsi_dcs_write_command(ddev, p_seq[1],
				(u8 *)&p_seq[2], p_seq[0] - 1);
			p_seq += p_seq[0] + 1;
		}
	}

	return ret;
}

/* Reverse order of power on and channel update as compared with MCDE default display update */
static int dsi_display_update(struct mcde_display_device *ddev,
							bool tripple_buffer)
{
	int ret = 0;

	if (ddev->power_mode != MCDE_DISPLAY_PM_ON && ddev->set_power_mode) {
		ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_ON);
		if (ret < 0) {
			dev_warn(&ddev->dev,
				"%s:Failed to set power mode to on\n",
				__func__);
			return ret;
		}
	}

	ret = mcde_chnl_update(ddev->chnl_state, tripple_buffer);
	if (ret < 0) {
		dev_warn(&ddev->dev, "%s:Failed to update channel\n", __func__);
		return ret;
	}
	return 0;
}

static int dsi_read_panel_id(struct kyle_lcd_data *lcd)
{
	int readret = 0;
	int len = 1;

	if (lcd->lcd_id[0] == 0x00) {
		dev_dbg(&lcd->ddev->dev, "%s: Read device id of the display\n", __func__);

		readret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID1,
						(u32 *)&lcd->lcd_id[0], &len);
		readret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID2,
						(u32 *)&lcd->lcd_id[1], &len);
		readret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID3,
						(u32 *)&lcd->lcd_id[2], &len);
		if (readret)
			dev_info(&lcd->ddev->dev,
				"mcde_dsi_dcs_read failed to read display ID\n");
		else
			dev_info(&lcd->ddev->dev, "Panel id = 0x%x,0x%x,0x%x\n",
				lcd->lcd_id[0], lcd->lcd_id[1], lcd->lcd_id[2]);
	}
	return readret;
}

#ifdef CONFIG_LCD_CLASS_DEVICE
static int nt35512_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode);
static int nt35512_power(struct kyle_lcd_data *lcd, int power)
{
	int ret = 0;

	switch (power) {
	case FB_BLANK_POWERDOWN:
		dev_dbg(lcd->dev, "%s(): Powering Off, was %s\n",__func__,
			(lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) ? "ON" : "OFF");
		ret = nt35512_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_OFF);
		break;
	case FB_BLANK_NORMAL:
		dev_dbg(lcd->dev, "%s(): Into Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_ON) ? "ON" : "SLEEP/OFF");
		ret = nt35512_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_STANDBY);
		break;
	case FB_BLANK_UNBLANK:
		dev_dbg(lcd->dev, "%s(): Exit Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_STANDBY) ? "SLEEP" : "ON/OFF");
		ret = nt35512_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_ON);
		break;
	default:
		ret = -EINVAL;
		dev_info(lcd->dev, "Invalid power change request (%d)\n", power);
		break;
	}
	return ret;
}

static int nt35512_set_power(struct lcd_device *ld, int power)
{
	struct kyle_lcd_data *lcd = lcd_get_data(ld);

	dev_dbg(lcd->dev, "%s: power=%d\n", __func__, power);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return nt35512_power(lcd, power);
}

static int nt35512_get_power(struct lcd_device *ld)
{
	struct kyle_lcd_data *lcd = lcd_get_data(ld);
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

static struct lcd_ops kyle_lcd_data_ops = {
	.set_power = nt35512_set_power,
	.get_power = nt35512_get_power,
};
#endif // CONFIG_LCD_CLASS_DEVICE


static int nt35512_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	static int notFirstTime;
	int ret = 0;
	enum mcde_display_rotation final;
	struct kyle_lcd_data *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_hw_rotation final_hw_rot;

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

	if (rotation != ddev->rotation) {
		if (final == MCDE_DISPLAY_ROT_180) {
			if (final != lcd->rotation) {
				ret = dsi_dcs_write_sequence(ddev,
					BOE_WVGA_DCS_CMD_SEQ_ROTATE_180);
				lcd->rotation = final;
			}
		} else if (final == MCDE_DISPLAY_ROT_0) {
			if (final != lcd->rotation) {
				ret = dsi_dcs_write_sequence(ddev,
					BOE_WVGA_DCS_CMD_SEQ_ROTATE_0);
				lcd->rotation = final;
			}
			(void)mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		} else {
			ret = mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		}
		if (ret)
			return ret;
		dev_dbg(lcd->dev, "Display rotated %d\n", final);
	}
	ddev->rotation = rotation;
	/* avoid disrupting splash screen by changing update_flags */
	if (notFirstTime || (final != MCDE_DISPLAY_ROT_0)) {
		notFirstTime = 1;
		ddev->update_flags |= UPDATE_FLAG_ROTATION;
	}
	return 0;
}

static int nt35512_apply_config(struct mcde_display_device *ddev)
{
	int ret;

	if (!ddev->update_flags)
		return 0;

	if (ddev->update_flags & (UPDATE_FLAG_VIDEO_MODE |
			UPDATE_FLAG_ROTATION))
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

static void nt35512_display_init(struct kyle_lcd_data *lcd)
{
	if (lcd->pd->reset_gpio)
		gpio_direction_output(lcd->pd->reset_gpio, 0);

	if (lcd->pd->lcd_pwr_onoff) {
		lcd->pd->lcd_pwr_onoff(true);
		msleep(40);
	}

	if (lcd->pd->reset_gpio) {
		gpio_set_value(lcd->pd->reset_gpio, 1);
		msleep(10);
	}

	mcde_formatter_enable(lcd->ddev->chnl_state);	/* ensure MCDE enabled */
	(void)dsi_read_panel_id(lcd);
	(void)dsi_dcs_write_sequence(lcd->ddev, BOE_WVGA_DCS_CMD_SEQ_POWER_SETTING);
	(void)dsi_dcs_write_sequence(lcd->ddev, BOE_WVGA_DCS_CMD_SEQ_GAMMA_SETTING);
	(void)dsi_dcs_write_sequence(lcd->ddev, BOE_WVGA_DCS_CMD_SEQ_INIT);
}

static int nt35512_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode)
{
	struct kyle_lcd_data *lcd = dev_get_drvdata(&ddev->dev);
	int ret = 0;

	dev_dbg(&ddev->dev, "%s: power_mode = %d\n", __func__, power_mode);

	/* OFF -> STANDBY or OFF -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {

		lcd->justStarted = false;
		nt35512_display_init(lcd);
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_ON) {

		if (lcd->justStarted) {
			lcd->justStarted = false;
			mcde_chnl_disable(ddev->chnl_state);
			nt35512_display_init(lcd);
		}
		ret = dsi_dcs_write_sequence(lcd->ddev, BOE_WVGA_DCS_CMD_SEQ_EXIT_SLEEP);
		if (ret)
			return ret;

		if ((!lcd->opp_is_requested) && (lcd->pd->min_ddr_opp > 0)) {
			if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
							NT35512_DRIVER_NAME,
							lcd->pd->min_ddr_opp)) {
				dev_err(lcd->dev, "add DDR OPP %d failed\n",
					lcd->pd->min_ddr_opp);
			}
			dev_dbg(lcd->dev, "DDR OPP requested at %d%%\n",lcd->pd->min_ddr_opp);
			lcd->opp_is_requested = true;
		}

		ddev->power_mode = MCDE_DISPLAY_PM_ON;
	}
	/* ON -> STANDBY */
	else if (ddev->power_mode == MCDE_DISPLAY_PM_ON &&
					power_mode <= MCDE_DISPLAY_PM_STANDBY) {

		ret = dsi_dcs_write_command(ddev, DCS_CMD_SET_DISPLAY_OFF, NULL, 0);
		if (ret == 0) {
			msleep(10);
			ret = dsi_dcs_write_command(ddev, DCS_CMD_ENTER_SLEEP_MODE, NULL, 0);
			msleep(150);
		}
		if (ret && (power_mode != MCDE_DISPLAY_PM_OFF))
			return ret;

		if (lcd->opp_is_requested) {
			prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP, NT35512_DRIVER_NAME);
			lcd->opp_is_requested = false;
			dev_dbg(lcd->dev, "DDR OPP removed\n");
		}
#ifdef READ_CRC_ERRORS_DEBUG
		{
			int readret, len;
			u32 errors = 0;
			len = 1;
			readret = mcde_dsi_dcs_read(ddev->chnl_state, 0x05,
							&errors, &len);
			if (readret || len != 1)
				dev_warn(lcd->dev, "Failed to read DSI CRC errors (%d)\n",
					readret);
			else if (errors)
				dev_warn(lcd->dev, "DSI errors recorded = %x\n",
					errors);
		}
#endif
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_OFF) {

		if (lcd->pd->lcd_pwr_onoff)
			lcd->pd->lcd_pwr_onoff(false);

		if (lcd->pd->reset_gpio)
			gpio_direction_output(lcd->pd->reset_gpio, 0);

		ddev->power_mode = MCDE_DISPLAY_PM_OFF;
	}

	return mcde_chnl_set_power_mode(ddev->chnl_state, ddev->power_mode);
}

#define VFP 27	/* MCDE needs time to prepare frame when rotated 90/270 degrees */
#define VBP 8
#define VSW 2
#define HFP 3
#define HBP 8
#define HSW 2
#define REFRESH_RATE 60

static int nt35512_dsi_try_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = 0;
	static u32 pixclock = 0;

	if (!ddev || !video_mode) {
		dev_warn(&ddev->dev,
			"%s: dev or video_mode equals NULL, aborting\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}


	if (video_mode->xres == ddev->native_x_res ||
			video_mode->xres == ddev->native_y_res) {
		video_mode->vfp = VFP;
		video_mode->vbp = VBP;
		video_mode->vsw = VSW;
		video_mode->hfp = HFP;
		video_mode->hbp = HBP;
		video_mode->hsw = HSW;
		video_mode->interlaced = false;
		/* +445681 display padding */
		video_mode->xres_padding = 0;
		video_mode->yres_padding = 0;
		/* -445681 display padding */

		if (pixclock == 0) {
			/* time between pixels in pico-seconds */
			pixclock = 1000000000 / REFRESH_RATE;
			pixclock /= video_mode->xres +
					video_mode->xres_padding +
					video_mode->hsw +
					video_mode->hbp +
					video_mode->hfp;
			pixclock *= 1000;
			pixclock /= video_mode->yres +
					video_mode->yres_padding +
					video_mode->vsw +
					video_mode->vbp +
					video_mode->vfp;
		}
		video_mode->pixclock = pixclock;

	} else {
		dev_warn(&ddev->dev,
			"%s:Failed to find video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		ret = -EINVAL;
	}
out:
	return ret;
}

static int nt35512_dsi_set_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = -EINVAL;
	struct mcde_video_mode channel_video_mode;
	static bool alreadyCalled;

	if (!ddev || !video_mode) {
		dev_warn(&ddev->dev,
			"%s: dev or video_mode equals NULL, aborting\n",
			__func__);
		return ret;
	}

	ddev->video_mode = *video_mode;
	channel_video_mode = ddev->video_mode;
	printk(KERN_INFO "hjoun %s rotation=%d, vid_xres=%d, vid_yres=%d\n", __func__, ddev->rotation, video_mode->xres, video_mode->yres);
	/* Dependant on if display should rotate or MCDE should rotate */
	if (ddev->rotation == MCDE_DISPLAY_ROT_90_CCW ||
				ddev->rotation == MCDE_DISPLAY_ROT_90_CW) {
		channel_video_mode.xres = ddev->native_x_res;
		channel_video_mode.yres = ddev->native_y_res;
	}
	ret = mcde_chnl_set_video_mode(ddev->chnl_state, &channel_video_mode);
	if (ret < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode\n", __func__);
		return ret;
	}

	/* notify mcde display driver about updated video mode, excepted for
	 * the first update to preserve the splash screen and avoid a
	 * stop_flow() */
	if (alreadyCalled)
		ddev->update_flags |= UPDATE_FLAG_VIDEO_MODE;
	else {
		ddev->update_flags |= UPDATE_FLAG_PIXEL_FORMAT;
		alreadyCalled = true;
	}

	return ret;
}

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[20];
	sprintf(temp, "HYDIS_NT35512\n");
	strcat(buf, temp);
	return strlen(buf);
}
static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);


static int __devinit nt35512_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct sec_dsi_platform_data *pdata = ddev->dev.platform_data;
	struct kyle_lcd_data *lcd = NULL;

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


	ddev->set_power_mode = nt35512_set_power_mode;
	ddev->set_rotation = nt35512_set_rotation;
	ddev->try_video_mode = nt35512_dsi_try_video_mode;
	ddev->set_video_mode = nt35512_dsi_set_video_mode;
	ddev->update = dsi_display_update;
	ddev->apply_config = nt35512_apply_config;

	ddev->native_x_res = VMODE_XRES;
	ddev->native_y_res = VMODE_YRES;

	lcd = kzalloc(sizeof(struct kyle_lcd_data), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	dev_set_drvdata(&ddev->dev, lcd);
	lcd->ddev = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;
	lcd->opp_is_requested = false;
	lcd->justStarted = true;
	lcd->rotation = MCDE_DISPLAY_ROT_0;
	memcpy(lcd->lcd_id, pdata->lcdId, 3);

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("panel", &ddev->dev,
					lcd, &kyle_lcd_data_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);

		dev_err(&ddev->dev, "%s: Failed to register nt35512 display device\n", __func__);

		goto out_free_lcd;
	}
#endif

	mutex_init(&lcd->lock);


	ret = device_create_file(&lcd->ld->dev, &dev_attr_lcd_type);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n",
	goto out;

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

static int __devexit nt35512_remove(struct mcde_display_device *ddev)
{
	struct kyle_lcd_data *lcd = dev_get_drvdata(&ddev->dev);
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
static int nt35512_resume(struct mcde_display_device *ddev)
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

static int nt35512_suspend(struct mcde_display_device *ddev, \
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

static struct mcde_display_driver nt35512_driver = {
	.probe	= nt35512_probe,
	.remove = nt35512_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = nt35512_suspend,
	.resume = nt35512_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.driver = {
		.name	= NT35512_DRIVER_NAME,
	},
};

/* Module init */
static int __init mcde_display_nt35512_init(void)
{
	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&nt35512_driver);
}
module_init(mcde_display_nt35512_init);

static void __exit mcde_display_nt35512_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&nt35512_driver);
}
module_exit(mcde_display_nt35512_exit);

MODULE_AUTHOR("Robert Teather <robert.teather@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung MCDE BOE WVGA panel display driver");
