/*
 * S6E63M0 AMOLED LCD panel driver.
 *
 * Author: InKi Dae  <inki.dae@samsung.com>
 * Modified by Anirban Sarkar <anirban.sarkar@samsung.com>
 * to add MCDE support
 *
 * Derived from drivers/video/omap/lcd-apollon.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/mutex.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/mfd/dbx500-prcmu.h>
#include <video/mcde_display.h>
#include <video/mcde_display-dpi.h>
#include <video/mcde_display_ssg_dpi.h>
#include "display-s6e63m0_gamma.h"

#include <plat/pincfg.h>
#include <plat/gpio-nomadik.h>
#include <mach/board-sec-u8500.h>
#include <mach/../../pins-db8500.h>
#include <mach/../../pins.h>

#define SMART_DIMMING
#define SPI_3WIRE_IF
#define DYNAMIC_ELVSS

#ifdef SMART_DIMMING
#include "smart_mtp_s6e63m0.h"
#endif

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define DEFMASK			0xFF00
#define COMMAND_ONLY	0xFE
#define DATA_ONLY		0xFF

#define MIN_SUPP_BRIGHTNESS	0
#define MAX_SUPP_BRIGHTNESS	10
#define MAX_REQ_BRIGHTNESS	255
#define LCD_POWER_UP		1
#define LCD_POWER_DOWN		0
#define LDI_STATE_ON		1
#define LDI_STATE_OFF		0
/* Taken from the programmed value of the LCD clock in PRCMU */
#define PIX_CLK_FREQ		25000000
#define VMODE_XRES		480
#define VMODE_YRES		800
#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

#define DIM_BL 20
#define MIN_BL 30
#define MAX_BL 255
#define MAX_GAMMA_VALUE 25

#define DPI_DISP_TRACE	dev_dbg(&ddev->dev, "%s\n", __func__)

/* to be removed when display works */
//#define dev_dbg	dev_info

extern unsigned int system_rev;

struct s6e63m0 {
	struct device			*dev;
	struct spi_device		*spi;
	struct mutex			lock;
	unsigned int			beforepower;
	unsigned int			power;
	unsigned int			current_gamma_mode;
	unsigned int			current_brightness;
	unsigned int			gamma_mode;
	unsigned int			gamma_table_count;
	unsigned int                    bl;
	unsigned int			ldi_state;
	unsigned int			acl_enable;
	unsigned int			cur_acl;
	unsigned char				panel_id;
	enum mcde_display_rotation	rotation;	
	struct mcde_display_device	*mdd;
	struct lcd_device			*ld;
	struct backlight_device		*bd;
	struct ssg_dpi_display_platform_data	*pd;
	struct spi_driver		spi_drv;
	unsigned int			elvss_ref;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend			earlysuspend;
#endif

#ifdef SMART_DIMMING
	struct SMART_DIM smart;
#endif

};

#ifdef CONFIG_HAS_EARLYSUSPEND
struct ux500_pins *dpi_pins;
#endif

#define ELVSS_MAX    0x28
const int ELVSS_OFFSET[] = {0x0, 0x07, 0x09, 0x0D};
#define SMART_MTP_PANEL_ID 0xa4

#ifdef SMART_DIMMING
#define LDI_MTP_LENGTH 21
#define LDI_MTP_ADDR	0xd3

/* mtp_data_from_boot used console memory */
extern char mtp_data_from_boot[];

static unsigned int LCD_DB[] = {70, 71, 72, 73, 74, 75, 76, 77};

static int is_load_mtp_offset;

/* READ CLK */
#define LCD_MPU80_RDX 169
/* DATA or COMMAND */
#define LCD_MPU80_DCX 171
/* WRITE CLK */
#define LCD_MPU80_WRX 220
/* CHIP SELECT */
#define LCD_MPU80_CSX 223

static pin_cfg_t  janice_mpu80_pins_enable[] = {
	GPIO169_GPIO | PIN_OUTPUT_HIGH, /* LCD_MPU80_RDX */
	GPIO171_GPIO | PIN_OUTPUT_HIGH, /* LCD_MPU80_DCX */
	GPIO220_GPIO | PIN_OUTPUT_HIGH, /* LCD_MPU80_WRX */
	GPIO223_GPIO | PIN_OUTPUT_HIGH, /* LCD_MPU80_CSX */

	/*LCD_MPU80_D0 ~ LCD_MPU80_D7 */
	GPIO70_GPIO | PIN_OUTPUT_HIGH,
	GPIO71_GPIO | PIN_OUTPUT_HIGH,
	GPIO72_GPIO | PIN_OUTPUT_HIGH,
	GPIO73_GPIO | PIN_OUTPUT_HIGH,
	GPIO74_GPIO | PIN_OUTPUT_HIGH,
	GPIO75_GPIO | PIN_OUTPUT_HIGH,
	GPIO76_GPIO | PIN_OUTPUT_HIGH,
	GPIO77_GPIO | PIN_OUTPUT_HIGH,
};

static pin_cfg_t janice_mpu80_pins_disable[] = {
	GPIO169_LCDA_DE,
	GPIO171_LCDA_HSO,
	GPIO220_GPIO | PIN_OUTPUT_HIGH, /* LCD_MPU80_WRX */
	GPIO223_GPIO | PIN_OUTPUT_HIGH, /* LCD_MPU80_CSX */

	/*LCD_MPU80_D0 ~ LCD_MPU80_D7 */
	GPIO70_LCD_D0,
	GPIO71_LCD_D1,
	GPIO72_LCD_D2,
	GPIO73_LCD_D3,
	GPIO74_LCD_D4,
	GPIO75_LCD_D5,
	GPIO76_LCD_D6,
	GPIO77_LCD_D7,
};

static pin_cfg_t janice_mpu80_data_line_input[] = {
	/*LCD_MPU80_D0 ~ LCD_MPU80_D7 */
	GPIO70_GPIO | PIN_INPUT_NOPULL,
	GPIO71_GPIO | PIN_INPUT_NOPULL,
	GPIO72_GPIO | PIN_INPUT_NOPULL,
	GPIO73_GPIO | PIN_INPUT_NOPULL,
	GPIO74_GPIO | PIN_INPUT_NOPULL,
	GPIO75_GPIO | PIN_INPUT_NOPULL,
	GPIO76_GPIO | PIN_INPUT_NOPULL,
	GPIO77_GPIO | PIN_INPUT_NOPULL,
};

static pin_cfg_t  janice_mpu80_data_line_output[] = {
	/*LCD_MPU80_D0 ~ LCD_MPU80_D7 */
	GPIO70_GPIO | PIN_OUTPUT_HIGH,
	GPIO71_GPIO | PIN_OUTPUT_HIGH,
	GPIO72_GPIO | PIN_OUTPUT_HIGH,
	GPIO73_GPIO | PIN_OUTPUT_HIGH,
	GPIO74_GPIO | PIN_OUTPUT_HIGH,
	GPIO75_GPIO | PIN_OUTPUT_HIGH,
	GPIO76_GPIO | PIN_OUTPUT_HIGH,
	GPIO77_GPIO | PIN_OUTPUT_HIGH,
};


const unsigned short  prepare_mtp_read[] = {
	/* LV2, LV3, MTP lock release code */
	0xf0, 0x5a,
	DATA_ONLY, 0x5a,
	0xf1, 0x5a,
	DATA_ONLY, 0x5a,
	0xfc, 0x5a,
	DATA_ONLY, 0x5a,
	/* MTP cell enable */
	0xd1,0x80,

	ENDDEF, 0x0000,
};

const unsigned short  start_mtp_read[] = {
	/* MPU  8bit read mode start */
	0xfc, 0x0c,
	DATA_ONLY, 0x00,
	ENDDEF, 0x0000,
};
#endif

static const unsigned short SEQ_PANEL_CONDITION_SET[] = {
	0xf8, 0x01,
	DATA_ONLY, 0x27,
	DATA_ONLY, 0x27,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x54,
	DATA_ONLY, 0x9f,
	DATA_ONLY, 0x63,
	DATA_ONLY, 0x8f,
//	DATA_ONLY, 0x86,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x0d,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_DISPLAY_CONDITION_SET[] = {
	0xf2, 0x02,
	DATA_ONLY, 0x03,
	DATA_ONLY, 0x1c,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x10,

	0xf7, 0x03,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_GAMMA_SETTING_160[] = {
	0xfa, 0x02,

	DATA_ONLY, 0x18,
	DATA_ONLY, 0x08,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x7F,
	DATA_ONLY, 0x6E,
	DATA_ONLY, 0x5F,
	DATA_ONLY, 0xC0,
	DATA_ONLY, 0xC6,
	DATA_ONLY, 0xB5,
	DATA_ONLY, 0xBA,
	DATA_ONLY, 0xBF,
	DATA_ONLY, 0xAD,
	DATA_ONLY, 0xCB,
	DATA_ONLY, 0xCF,
	DATA_ONLY, 0xC0,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x94,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x91,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xC8,

	0xfa, 0x03,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_ETC_CONDITION_SET[] = {
	0xf6, 0x00,
	DATA_ONLY, 0x8E,
	DATA_ONLY, 0x07,

	0xb3, 0x6C,

	0xb5, 0x2c,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x0c,
	DATA_ONLY, 0x0a,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0e,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x13,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x2a,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1b,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x17,

	DATA_ONLY, 0x2b,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x3a,
	DATA_ONLY, 0x34,
	DATA_ONLY, 0x30,
	DATA_ONLY, 0x2c,
	DATA_ONLY, 0x29,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x23,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x1e,
	DATA_ONLY, 0x1e,

	0xb6, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,

	DATA_ONLY, 0x55,
	DATA_ONLY, 0x55,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,

	0xb7, 0x2c,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x0c,
	DATA_ONLY, 0x0a,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0e,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x13,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x2a,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1b,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x17,

	DATA_ONLY, 0x2b,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x3a,
	DATA_ONLY, 0x34,
	DATA_ONLY, 0x30,
	DATA_ONLY, 0x2c,
	DATA_ONLY, 0x29,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x23,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x1e,
	DATA_ONLY, 0x1e,

	0xb8, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,

	DATA_ONLY, 0x55,
	DATA_ONLY, 0x55,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,

	0xb9, 0x2c,
	DATA_ONLY, 0x12,
	DATA_ONLY, 0x0c,
	DATA_ONLY, 0x0a,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x0e,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x13,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x2a,
	DATA_ONLY, 0x24,
	DATA_ONLY, 0x1f,
	DATA_ONLY, 0x1b,
	DATA_ONLY, 0x1a,
	DATA_ONLY, 0x17,

	DATA_ONLY, 0x2b,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x3a,
	DATA_ONLY, 0x34,
	DATA_ONLY, 0x30,
	DATA_ONLY, 0x2c,
	DATA_ONLY, 0x29,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x23,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x1e,
	DATA_ONLY, 0x1e,

	0xba, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x22,
	DATA_ONLY, 0x33,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x44,

	DATA_ONLY, 0x55,
	DATA_ONLY, 0x55,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,
	DATA_ONLY, 0x66,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_ACL_SETTING_40[] = {
	0xc1, 0x4D,
	DATA_ONLY, 0x96,
	DATA_ONLY, 0x1D,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x01,
	DATA_ONLY, 0xDF,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	DATA_ONLY, 0x1F,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x01,
	DATA_ONLY, 0x06,
	DATA_ONLY, 0x0C,
	DATA_ONLY, 0x11,
	DATA_ONLY, 0x16,
	DATA_ONLY, 0x1C,
	DATA_ONLY, 0x21,
	DATA_ONLY, 0x26,
	DATA_ONLY, 0x2B,
	DATA_ONLY, 0x31,
	DATA_ONLY, 0x36,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_ELVSS_SETTING[] = {
	0xb2, 0x17,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x17,
	DATA_ONLY, 0x17,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_ELVSS_ON[] = {
	/* ELVSS on */
	0xb1, 0x0b,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_ELVSS_OFF[] = {
	/* ELVSS off */
	0xb1, 0x0a,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_STAND_BY_OFF[] = {
	0x11, COMMAND_ONLY,
	SLEEPMSEC, 120,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_STAND_BY_ON[] = {
	0x10, COMMAND_ONLY,
	SLEEPMSEC, 120,

	ENDDEF, 0x0000
};

static const unsigned short SEQ_DISPLAY_ON[] = {
	0x29, COMMAND_ONLY,

	ENDDEF, 0x0000
};

/* Horizontal flip */
static const unsigned short DCS_CMD_SEQ_ORIENTATION_180[] = {
/*	Length	Command 			Parameters */
	0xF7,	0x00,

	ENDDEF, 0x0000
};

/* Default Orientation */
static const unsigned short DCS_CMD_SEQ_ORIENTATION_DEFAULT[] = {
/*	Length	Command 			Parameters */
	0xF7,	0x03,
	
	ENDDEF, 0x0000
};
static void print_vmode(struct device *dev, struct mcde_video_mode *vmode)
{
/*
	dev_dbg(dev, "resolution: %dx%d\n", vmode->xres, vmode->yres);
	dev_dbg(dev, "  pixclock: %d\n",    vmode->pixclock);
	dev_dbg(dev, "       hbp: %d\n",    vmode->hbp);
	dev_dbg(dev, "       hfp: %d\n",    vmode->hfp);
	dev_dbg(dev, "       hsw: %d\n",    vmode->hsw);
	dev_dbg(dev, "       vbp: %d\n",    vmode->vbp);
	dev_dbg(dev, "       vfp: %d\n",    vmode->vfp);
	dev_dbg(dev, "       vsw: %d\n",    vmode->vsw);
	dev_dbg(dev, "interlaced: %s\n",	vmode->interlaced ? "true" : "false");
*/
}

static int try_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode)
{
	int res = -EINVAL;

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		return res;
	}

	if ((video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) ||
	    (video_mode->xres == VMODE_YRES && video_mode->yres == VMODE_XRES)) {

		video_mode->hsw = 2;
		video_mode->hbp = 16; /* from end of hsync */
		video_mode->hfp = 16;
		video_mode->vsw = 2;
                video_mode->vbp = 1;//3  /* from end of vsync */
                video_mode->vfp = 28;//26;
		video_mode->interlaced = false;
		/* +445681 display padding */
		video_mode->xres_padding = 0;
		video_mode->yres_padding = 0;
		/* -445681 display padding */
		
		/*
		 * The pixclock setting is not used within MCDE. The clock is
		 * setup elsewhere. But the pixclock value is visible in user
		 * space.
		 */
		video_mode->pixclock =	(int) (1e+12 * (1.0 / PIX_CLK_FREQ));

		res = 0;
	}

	if (res == 0)
		print_vmode(&ddev->dev, video_mode);
	else
		dev_warn(&ddev->dev,
			"%s:Failed to find video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);

	return res;

}

static int set_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode)
{
	int res = -EINVAL;
	struct mcde_video_mode channel_video_mode;
	static int video_mode_apply_during_boot = 1;
	struct s6e63m0 *lcd = dev_get_drvdata(&ddev->dev);

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		goto out;
	}
	ddev->video_mode = *video_mode;
	print_vmode(&ddev->dev, video_mode);
	if ((video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) ||
	    (video_mode->xres == VMODE_YRES && video_mode->yres == VMODE_XRES)) {
		res = 0;
	}
	if (res < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		goto error;
	}

	channel_video_mode = ddev->video_mode;
	/* Dependant on if display should rotate or MCDE should rotate */
	if (ddev->rotation == MCDE_DISPLAY_ROT_90_CCW ||
		ddev->rotation == MCDE_DISPLAY_ROT_90_CW) {
		channel_video_mode.xres = ddev->native_x_res;
		channel_video_mode.yres = ddev->native_y_res;
	}
		channel_video_mode.xres_padding= 0;
		channel_video_mode.yres_padding= 0;
	
	res = mcde_chnl_set_video_mode(ddev->chnl_state, &channel_video_mode);
	if (res < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode on channel\n",
			__func__);

		goto error;
	}
	/* notify mcde display driver about updated video mode, excepted for
	 * the first update to preserve the splash screen and avoid a
	 * stop_flow() */
	if (video_mode_apply_during_boot && lcd->pd->platform_enabled) {
		ddev->update_flags |= UPDATE_FLAG_PIXEL_FORMAT;
		video_mode_apply_during_boot = 0;
	} else
		ddev->update_flags |= UPDATE_FLAG_VIDEO_MODE;
	return res;
out:
error:
	return res;
}

/* Reverse order of power on and channel update as compared with MCDE default display update */
static int s6e63m0_display_update(struct mcde_display_device *ddev,
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

static int s6e63m0_apply_config(struct mcde_display_device *ddev)
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

static int s6e63m0_spi_write_byte(struct s6e63m0 *lcd, int addr, int data)
{
	u16 buf[1];
	struct spi_message msg;

	struct spi_transfer xfer = {
		.len		= 2,
		.tx_buf		= buf,
	};

	buf[0] = (addr << 8) | data;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

		return spi_sync(lcd->spi, &msg);
	}

static int s6e63m0_spi_read_byte(struct s6e63m0 *lcd, int addr, u8 *data)
{
	u16 buf[2];
	u16 rbuf[2];
	int ret;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len		= 4,
		.tx_buf		= buf,
		.rx_buf		= rbuf,
	};

	buf[0] = addr;
	buf[1] = 0x100;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(lcd->spi, &msg);
	if (ret)
		return ret;

	*data = (rbuf[1] & 0x1FF) >> 1;

	return ret;
}

static int s6e63m0_spi_write(struct s6e63m0 *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	if (address != DATA_ONLY)
		ret = s6e63m0_spi_write_byte(lcd, 0x0, address);
	if (command != COMMAND_ONLY)
		ret = s6e63m0_spi_write_byte(lcd, 0x1, command);

	return ret;
}

static int s6e63m0_spi_write_words(struct s6e63m0 *lcd,
	const u16 *buf, int len)
{
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len		= 2 * len,
		.tx_buf		= buf,
	};

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	if(lcd->spi){
		return spi_sync(lcd->spi, &msg);
	}else{
		return -1;
	}
}


int s6e63m0_panel_send_sequence(struct s6e63m0 *lcd,
	const unsigned short *buf)
{
	int ret = 0, i = 0;

	const unsigned short *wbuf;

	mutex_lock(&lcd->lock);

	wbuf = buf;

	while ((wbuf[i] & DEFMASK) != ENDDEF) {
		if ((wbuf[i] & DEFMASK) != SLEEPMSEC) {
			ret = s6e63m0_spi_write(lcd, wbuf[i], wbuf[i + 1]);
			if (ret)
				break;
		} else
			udelay(wbuf[i + 1] * 1000);
		i += 2;
	}

	mutex_unlock(&lcd->lock);

	return ret;
}

#ifdef SPI_3WIRE_IF
static pin_cfg_t janice_spi_3wire_pins_enable[] = {
	GPIO220_GPIO | PIN_OUTPUT_HIGH, /* GPIO220_SPI0_CLK */
	GPIO223_GPIO | PIN_OUTPUT_HIGH, /* GPIO223_SPI0_CS */
	GPIO224_GPIO | PIN_OUTPUT_HIGH, /* GPIO224_SPI0_TXD */
};

static pin_cfg_t janice_spi_3wire_SDA = GPIO224_GPIO|PIN_OUTPUT_HIGH;
static pin_cfg_t janice_spi_3wire_SDI = GPIO224_GPIO|PIN_INPUT_NOPULL;

#define DEFAULT_SLEEP 1
#define SPI_3WIRE_CLK_HIGH do { \
	gpio_direction_output(LCD_CLK_JANICE_R0_0, 1); \
} while (0);

#define SPI_3WIRE_CLK_LOW do { \
	gpio_direction_output(LCD_CLK_JANICE_R0_0, 0); \
} while (0);

#define SPI_3WIRE_CS_HIGH do { \
	gpio_direction_output(LCD_CSX_JANICE_R0_0, 1); \
} while (0);

#define SPI_3WIRE_CS_LOW do { \
	gpio_direction_output(LCD_CSX_JANICE_R0_0, 0); \
} while (0);

#define SPI_3WIRE_SDA_HIGH do { \
	gpio_direction_output(LCD_SDI_JANICE_R0_0, 1); \
} while (0);

#define SPI_3WIRE_SDA_LOW do { \
	gpio_direction_output(LCD_SDI_JANICE_R0_0, 0); \
} while (0);

void spi_3wire_gpio_enable(unsigned char enable)
{
	if (enable) {
		nmk_config_pins(janice_spi_3wire_pins_enable,
			ARRAY_SIZE(janice_spi_3wire_pins_enable));
	}
}

void spi_3wire_write_byte(u8 cmd, u8 addr)
{
	int bit;
	int bnum;

	SPI_3WIRE_CLK_LOW

	if (cmd == COMMAND_ONLY)
		SPI_3WIRE_SDA_LOW
	else
		SPI_3WIRE_SDA_HIGH

	udelay(DEFAULT_SLEEP);
	SPI_3WIRE_CLK_HIGH
	udelay(DEFAULT_SLEEP);

	bnum = 8;
	bit = 0x80;
	while (bnum--) {
		SPI_3WIRE_CLK_LOW
		if (addr & bit)
			SPI_3WIRE_SDA_HIGH
		else
			SPI_3WIRE_SDA_LOW
		udelay(1);
		SPI_3WIRE_CLK_HIGH
		udelay(1);
		bit >>= 1;
	}
}

void spi_3wire_read_byte(u8 addr, u8 *data)
{
	int bit;
	spi_3wire_gpio_enable(1);

	SPI_3WIRE_CS_LOW
	udelay(DEFAULT_SLEEP);

	spi_3wire_write_byte(COMMAND_ONLY, addr);

	SPI_3WIRE_SDA_LOW

	nmk_config_pin(janice_spi_3wire_SDI, 0);

	bit = 8;
	*data = 0;
	while (bit) {
		SPI_3WIRE_CLK_LOW
		udelay(DEFAULT_SLEEP);
		*data <<= 1;
		*data |= gpio_get_value(LCD_SDI_JANICE_R0_0) ? 1 : 0;
		SPI_3WIRE_CLK_HIGH
		udelay(DEFAULT_SLEEP);
		--bit;
	}
	nmk_config_pin(janice_spi_3wire_SDA, 0);

	SPI_3WIRE_CS_HIGH
	SPI_3WIRE_CLK_HIGH
	SPI_3WIRE_SDA_HIGH
}

#endif

static int s6e63m0_read_panel_id(struct s6e63m0 *lcd, u8 *idbuf)
{
#ifdef SPI_3WIRE_IF
	static int pre_id_read;
	static u8 lcd_panel_id[3];

	if (pre_id_read) {
		memcpy(idbuf, lcd_panel_id, 3);
	} else {
		spi_3wire_read_byte(0xDA, &idbuf[0]);
		spi_3wire_read_byte(0xDB, &idbuf[1]);
		spi_3wire_read_byte(0xDC, &idbuf[2]);
		memcpy(lcd_panel_id, idbuf, 3);
		lcd->panel_id = idbuf[1];
		lcd->elvss_ref = idbuf[2];
		pre_id_read = 1;
	}
	return 0;
#else
	int ret;

	ret = s6e63m0_spi_write(lcd,0xB0,0x00);
	ret |= s6e63m0_spi_write(lcd, 0xDE, COMMAND_ONLY);
	ret |= s6e63m0_spi_read_byte(lcd, 0xDA, &idbuf[0]);
	ret |= s6e63m0_spi_read_byte(lcd, 0xDB, &idbuf[1]);
	ret |= s6e63m0_spi_read_byte(lcd, 0xDC, &idbuf[2]);
	ret |= s6e63m0_spi_write(lcd, 0xDF, COMMAND_ONLY);

	return ret;
#endif
}


#ifdef SMART_DIMMING
#define gen_table_max 21
#if 0
int illumination_tabel[] =
{
30, 40, 50, 60, 70, 80, 90,
100, 105, 110, 120, 130, 140,
150, 160, 170, 180, 190, 200,
205, 210, 220, 230, 240, 250,
300,
};
#else
int illumination_tabel[] = {
30, 40, 50, 60, 70, 80, 90,
100, 105, 110, 120, 130, 140,
150, 160, 170, 173, 180, 193,
198, 203, 213, 223, 233, 243,
293,
};

#endif
unsigned short s6e63m0_22_gamma_table[] = {
	0xFA, 0x02,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,

	0xFA, 0x03,

	ENDDEF, 0x0000
};

unsigned short *Gen_gamma_table(struct s6e63m0 *lcd)
{
	int i;
	int j = 0;
	char gen_gamma[gen_table_max] ={0,};

	lcd->smart.brightness_level = illumination_tabel[lcd->bl];
	generate_gamma(&(lcd->smart),gen_gamma,gen_table_max);

	for(i=3;i<((gen_table_max*2)+3);i+=2) {
		s6e63m0_22_gamma_table[i] = gen_gamma[j++];
	}

	/* for debug */
	#if 1
		printk("%s lcd->bl : %d ",__func__,lcd->bl);
		for(i=3;i<((gen_table_max*2)+3);i+=2) {
			printk("0x%x ",s6e63m0_22_gamma_table[i]);
		}
		printk("\n");
	#endif

	return s6e63m0_22_gamma_table;
}

#endif

static int s6e63m0_gamma_ctl(struct s6e63m0 *lcd)
{
	int ret = 0;
	const unsigned short *gamma;


	if (lcd->gamma_mode)
		gamma = gamma_table.gamma_19_table[lcd->bl];
	else {
		#ifdef SMART_DIMMING
			if(is_load_mtp_offset)
				gamma = Gen_gamma_table(lcd);
			else
				gamma = gamma_table.gamma_22_table[lcd->bl];
		#else
			gamma = gamma_table.gamma_22_table[lcd->bl];
		#endif
	}

	ret = s6e63m0_panel_send_sequence(lcd, gamma);
	if (ret) {
		dev_err(lcd->dev, "failed to disable gamma table updating.\n");
		goto gamma_err;
	}

	lcd->current_brightness = lcd->bl;
	lcd->current_gamma_mode = lcd->gamma_mode;
gamma_err:
	return ret;
}


unsigned short SEQ_DYNAMIC_ELVSS[] = {
	0xB2, 0x0,
	DATA_ONLY, 0x0,
	DATA_ONLY, 0x0,
	DATA_ONLY, 0x0,

	0xB1, 0x0B,

	ENDDEF, 0x00
};

int dynamic_elvss_cal(struct s6e63m0 *lcd, int step)
{
	int data, cnt;

	data = lcd->elvss_ref + ELVSS_OFFSET[step];

	if (data > ELVSS_MAX)
		data = ELVSS_MAX;

	for (cnt = 1; cnt <= 7; cnt += 2)
		SEQ_DYNAMIC_ELVSS[cnt] = data;

	return s6e63m0_panel_send_sequence(lcd, SEQ_DYNAMIC_ELVSS);
}

static int s6e63m0_set_elvss(struct s6e63m0 *lcd)
{
	int ret = 0;

	if ((lcd->elvss_ref) && (lcd->panel_id >= SMART_MTP_PANEL_ID)) {
		switch (lcd->bl) {
		case 0 ... 7: /* 30cd ~ 100cd */
			ret = dynamic_elvss_cal(lcd, 3);
			break;
		case 8 ... 14: /* 110cd ~ 160cd */
			ret = dynamic_elvss_cal(lcd, 2);
			break;
		case 15 ... 18: /* 170cd ~ 200cd */
			ret = dynamic_elvss_cal(lcd, 1);
			break;
		case 19 ... 27: /* 210cd ~ 300cd */
			ret = dynamic_elvss_cal(lcd, 0);
			break;
		default:
			break;
		}
	} else {
		switch (lcd->bl) {
		case 0 ... 4: /* 30cd ~ 100cd */
		ret = s6e63m0_panel_send_sequence(lcd, SEQ_ELVSS_SET[3]);
		break;
		case 5 ... 10: /* 110cd ~ 160cd */
		ret = s6e63m0_panel_send_sequence(lcd, SEQ_ELVSS_SET[2]);
		break;
		case 11 ... 14: /* 170cd ~ 200cd */
		ret = s6e63m0_panel_send_sequence(lcd, SEQ_ELVSS_SET[1]);
		break;
		case 15 ... 27: /* 210cd ~ 300cd */
		ret = s6e63m0_panel_send_sequence(lcd, SEQ_ELVSS_SET[0]);
		break;
		default:
		break;
		}
	}

	dev_dbg(lcd->dev, "level  = %d\n", lcd->bl);

	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return -EIO;
	}

	return ret;
}

static int s6e63m0_set_acl(struct s6e63m0 *lcd)
{
	int ret = 0;

	if (lcd->acl_enable) {
		switch (lcd->bl) {
		case 0 ... 3: /* 30cd ~ 60cd */
			if (lcd->cur_acl != 0) {
				ret = s6e63m0_panel_send_sequence(lcd, ACL_cutoff_set[8]);
				dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : off!!\n");
				lcd->cur_acl = 0;
			}
			break;
		case 4 ... 24: /* 70cd ~ 250 */
			if (lcd->cur_acl != 40) {
				ret |= s6e63m0_panel_send_sequence(lcd, ACL_cutoff_set[1]);
				dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : 40!!\n");
				lcd->cur_acl = 40;
			}
			break;
		default: /* 300 */
			if (lcd->cur_acl != 50) {
				ret |= s6e63m0_panel_send_sequence(lcd, ACL_cutoff_set[6]);
				dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : 50!!\n");
				lcd->cur_acl = 50;
			}
			break;
		}
	} else {
			ret = s6e63m0_panel_send_sequence(lcd, ACL_cutoff_set[8]);
			lcd->cur_acl = 0;
			dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : off!!\n");
	}

	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return -EIO;
	}

	return ret;
}

/* s6e63m0_set_rotation */
static int s6e63m0_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	static int notFirstTime;
	int ret = 0;
	enum mcde_display_rotation final;
	struct s6e63m0 *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_hw_rotation final_hw_rot;

	final = (360 + rotation - ddev->orientation) % 360;
//	printk("s6e63m0_set_rotation is FINAL =[%d]",final);
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
//	printk("rotation =[%d]...ddev->rotation =[%d]\n",rotation,ddev->rotation);

	if (rotation != ddev->rotation) {
//		printk("FINAL =[%d]\n",final);
		
		if (final == MCDE_DISPLAY_ROT_180) {
			if (final != lcd->rotation) {
				ret = s6e63m0_panel_send_sequence(lcd,
						DCS_CMD_SEQ_ORIENTATION_180);
				lcd->rotation = final;
			}
		} else if (final == MCDE_DISPLAY_ROT_0) {
			if (final != lcd->rotation) {
				ret = s6e63m0_panel_send_sequence(lcd,
						DCS_CMD_SEQ_ORIENTATION_DEFAULT);
				lcd->rotation = final;
			}
			(void)mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		} else {
			ret = mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		}
//		printk("SET ROTATION RETURN VALUE =[%d]\n",ret);
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

static int s6e63m0_ldi_init(struct s6e63m0 *lcd)
{
	int ret = 0, i;
	const unsigned short *init_seq[] = {
		SEQ_PANEL_CONDITION_SET,
		SEQ_DISPLAY_CONDITION_SET,
		SEQ_GAMMA_SETTING_160,
		SEQ_ETC_CONDITION_SET,
		SEQ_ACL_SETTING_40,
		SEQ_ACL_ON,
		SEQ_ELVSS_SETTING,
		SEQ_ELVSS_ON,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = s6e63m0_panel_send_sequence(lcd, init_seq[i]);
		if (ret)
			break;
	}

	return ret;
}

static int s6e63m0_ldi_enable(struct s6e63m0 *lcd)
{
	int ret = 0, i;
	const unsigned short *enable_seq[] = {
		SEQ_STAND_BY_OFF,
		SEQ_DISPLAY_ON,
	};

	dev_dbg(lcd->dev, "s6e63m0_ldi_enable\n");

	for (i = 0; i < ARRAY_SIZE(enable_seq); i++) {
		ret = s6e63m0_panel_send_sequence(lcd, enable_seq[i]);
		if (ret)
			break;
	}
	lcd->ldi_state = LDI_STATE_ON;

	return ret;
}

static int s6e63m0_ldi_disable(struct s6e63m0 *lcd)
{
	int ret;

	dev_dbg(lcd->dev, "s6e63m0_ldi_disable\n");
	ret = s6e63m0_panel_send_sequence(lcd, SEQ_STAND_BY_ON);
	lcd->ldi_state = LDI_STATE_OFF;

	return ret;
}

#ifdef SMART_DIMMING
void s6e63m0_parallel_read(struct s6e63m0 *lcd,u8 cmd, u8 *data, size_t len)
{
	int delay = 1;
	int i;

	gpio_direction_output(LCD_MPU80_DCX, 0);
	udelay(delay);
	gpio_direction_output(LCD_MPU80_WRX, 0);
	nmk_config_pins(janice_mpu80_data_line_output,
								ARRAY_SIZE(janice_mpu80_data_line_output));

	for (i = 0; i < 8; i++) {
		gpio_direction_output(LCD_DB[i], (cmd >> i) & 1);
	}
	udelay(delay);
	gpio_direction_output(LCD_MPU80_WRX, 1);
	udelay(delay);
	gpio_direction_output(LCD_MPU80_DCX, 1);

	for (i = 0; i < 8; i++) {
		gpio_direction_output(LCD_DB[i], 0);
	}

	nmk_config_pins(janice_mpu80_data_line_input,
								ARRAY_SIZE(janice_mpu80_data_line_input));
	/*1 byte dummy */
	udelay(delay);
	gpio_direction_output(LCD_MPU80_RDX, 0);
	udelay(delay);
	gpio_direction_output(LCD_MPU80_RDX, 1);

	while (len--) {
		char d = 0;
		gpio_direction_output(LCD_MPU80_RDX, 0);
		udelay(delay);
		for (i = 0; i < 8; i++)
			d |= gpio_get_value(LCD_DB[i]) << i;
		*data++ = d;
		gpio_direction_output(LCD_MPU80_RDX, 1);
		udelay(delay);
	}
	gpio_direction_output(LCD_MPU80_RDX, 1);

}

static void configure_mtp_gpios(bool enable)
{
	if (enable) {
		nmk_config_pins(janice_mpu80_pins_enable,
								ARRAY_SIZE(janice_mpu80_pins_enable));

		gpio_request(169, "LCD_MPU80_RDX");
		gpio_request(171, "LCD_MPU80_DCX");
		gpio_request(70, "LCD_MPU80_D0");
		gpio_request(71, "LCD_MPU80_D1");
		gpio_request(72, "LCD_MPU80_D2");
		gpio_request(73, "LCD_MPU80_D3");
		gpio_request(74, "LCD_MPU80_D4");
		gpio_request(75, "LCD_MPU80_D5");
		gpio_request(76, "LCD_MPU80_D6");
		gpio_request(77, "LCD_MPU80_D7");
	} else {
		nmk_config_pins(janice_mpu80_pins_disable,
								ARRAY_SIZE(janice_mpu80_pins_disable));

		gpio_free(169);
		gpio_free(171);
		gpio_free(70);
		gpio_free(71);
		gpio_free(72);
		gpio_free(73);
		gpio_free(74);
		gpio_free(75);
		gpio_free(76);
		gpio_free(77);
	}
}


static void s6e63m0_parallel_setup_gpios(bool init)
{
	if (init) {
		configure_mtp_gpios(true);
		gpio_set_value(LCD_MPU80_CSX, 0);
		gpio_set_value(LCD_MPU80_WRX, 1);
		gpio_set_value(LCD_MPU80_RDX, 1);
		gpio_set_value(LCD_MPU80_DCX, 0);

	} else {
		configure_mtp_gpios(false);
		gpio_set_value(LCD_MPU80_CSX, 1);
	}
}

static void s6e63mo_read_mtp_info(struct s6e63m0 *lcd)
{
	int i=0;

	s6e63m0_panel_send_sequence(lcd,prepare_mtp_read);
	s6e63m0_panel_send_sequence(lcd,start_mtp_read);

	s6e63m0_parallel_setup_gpios(true);

	s6e63m0_parallel_read(lcd, LDI_MTP_ADDR,
				(u8 *)(&(lcd->smart.MTP)), LDI_MTP_LENGTH);

	for(i=0;i<LDI_MTP_LENGTH;i++) {
		printk("%s main_mtp_data[%d] : %02x\n",__func__, i,
				((char*)&(lcd->smart.MTP))[i]);
	}

	Smart_dimming_init(&(lcd->smart));

	s6e63m0_parallel_setup_gpios(false);
}

static void s6e63mo_mtp_from_boot(struct s6e63m0 *lcd, char *mtp)
{
	int i;
	memcpy(&(lcd->smart.MTP), mtp, LDI_MTP_LENGTH);

	for (i = 0; i < LDI_MTP_LENGTH; i++) {
		printk("%s main_mtp_data[%d] : %02x\n", __func__, i,
				((char *)&(lcd->smart.MTP))[i]);
	}
	Smart_dimming_init(&(lcd->smart));
}
#endif

static int s6e63m0_set_brightness(struct backlight_device *bd);

static int update_brightness(struct s6e63m0 *lcd)
{
	int ret;

	ret = s6e63m0_set_elvss(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	ret = s6e63m0_set_acl(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	ret = s6e63m0_gamma_ctl(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	return 0;
}


static int s6e63m0_power_on(struct s6e63m0 *lcd)
{
	int ret = 0;
	struct ssg_dpi_display_platform_data *dpd = NULL;
	struct backlight_device *bd = NULL;

	dpd = lcd->pd;
	if (!dpd) {
		dev_err(lcd->dev, "s6e63m0 platform data is NULL.\n");
		return -EFAULT;
	}

	bd = lcd->bd;
	if (!bd) {
		dev_err(lcd->dev, "backlight device is NULL.\n");
		return -EFAULT;
	}

	dpd->power_on(dpd, LCD_POWER_UP);
	msleep(dpd->power_on_delay);

	if (!dpd->gpio_cfg_lateresume) {
		dev_err(lcd->dev, "gpio_cfg_lateresume is NULL.\n");
		return -EFAULT;
	} else
		dpd->gpio_cfg_lateresume();

	dpd->reset(dpd);
	msleep(dpd->reset_delay);

	ret = s6e63m0_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}
	dev_dbg(lcd->dev, "ldi init successful\n");

	ret = s6e63m0_ldi_enable(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to enable ldi.\n");
		return ret;
	}
	dev_dbg(lcd->dev, "ldi enable successful\n");

	/* force acl on */
	s6e63m0_panel_send_sequence(lcd, ACL_cutoff_set[7]);

	update_brightness(lcd);

	return 0;
}

static int s6e63m0_power_off(struct s6e63m0 *lcd)
{
	int ret = 0;
	struct ssg_dpi_display_platform_data *dpd = NULL;

	dev_dbg(lcd->dev, "s6e63m0_power_off\n");

	dpd = lcd->pd;
	if (!dpd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	ret = s6e63m0_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	msleep(dpd->display_off_delay);

	if (!dpd->gpio_cfg_earlysuspend) {
		dev_err(lcd->dev, "gpio_cfg_earlysuspend is NULL.\n");
		return -EFAULT;
	} else
		dpd->gpio_cfg_earlysuspend();

	if (!dpd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else
		dpd->power_on(dpd, LCD_POWER_DOWN);

	return 0;
}

static int s6e63m0_power(struct s6e63m0 *lcd, int power)
{
	int ret = 0;

	dev_dbg(lcd->dev, "%s(): old=%d (%s), new=%d (%s)\n", __func__,
		lcd->power, POWER_IS_ON(lcd->power)? "on": "off",
		power, POWER_IS_ON(power)? "on": "off"
		);

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = s6e63m0_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = s6e63m0_power_off(lcd);
	if (!ret)
		lcd->power = power;

	return ret;
}

static int s6e63m0_set_power(struct lcd_device *ld, int power)
{
	struct s6e63m0 *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return s6e63m0_power(lcd, power);
}

static int s6e63m0_get_power(struct lcd_device *ld)
{
	struct s6e63m0 *lcd = lcd_get_data(ld);

	return lcd->power;
}

static struct lcd_ops s6e63m0_lcd_ops = {
	.set_power = s6e63m0_set_power,
	.get_power = s6e63m0_get_power,
};


/* This structure defines all the properties of a backlight */
struct backlight_properties s6e63m0_backlight_props = {
	.brightness = MAX_REQ_BRIGHTNESS,
	.max_brightness = MAX_REQ_BRIGHTNESS,
	.type = BACKLIGHT_RAW,
};

static int s6e63m0_get_brightness(struct backlight_device *bd)
{
	dev_dbg(&bd->dev, "lcd get brightness returns %d\n", bd->props.brightness);
	return bd->props.brightness;
}

static int get_gamma_value_from_bl(int bl)
{
        int gamma_value =0;
        int gamma_val_x10 =0;

        if(bl >= MIN_BL){
                gamma_val_x10 = 10 *(MAX_GAMMA_VALUE-1)*bl/(MAX_BL-MIN_BL) + (10 - 10*(MAX_GAMMA_VALUE-1)*(MIN_BL)/(MAX_BL-MIN_BL));
                gamma_value=(gamma_val_x10 +5)/10;
        }else{
                gamma_value =0;
        }

        return gamma_value;
}

static int s6e63m0_set_brightness(struct backlight_device *bd)
{
	int ret = 0, bl = bd->props.brightness;
	struct s6e63m0 *lcd = bl_get_data(bd);

	if (bl < MIN_SUPP_BRIGHTNESS ||
		bl > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_SUPP_BRIGHTNESS, bd->props.max_brightness);
		return -EINVAL;
	}

        lcd->bl = get_gamma_value_from_bl(bl);

	if ((lcd->ldi_state) && (lcd->current_brightness != lcd->bl)) {
		ret = update_brightness(lcd);
		dev_info(lcd->dev, "brightness=%d, bl=%d\n", bd->props.brightness, lcd->bl);
		if (ret < 0)
			dev_err(&bd->dev, "update brightness failed.\n");
	}

	return ret;
}

static struct backlight_ops s6e63m0_backlight_ops  = {
	.get_brightness = s6e63m0_get_brightness,
	.update_status = s6e63m0_set_brightness,
};

static ssize_t s6e63m0_sysfs_store_lcd_power(struct device *dev,
                                       struct device_attribute *attr,
                                       const char *buf, size_t len)
{
        int rc;
        int lcd_enable;
	struct s6e63m0 *lcd = dev_get_drvdata(dev);

	dev_info(lcd->dev,"s6e63m0 lcd_sysfs_store_lcd_power\n");

        rc = strict_strtoul(buf, 0, (unsigned long *)&lcd_enable);
        if (rc < 0)
                return rc;

        if(lcd_enable) {
		s6e63m0_power(lcd, FB_BLANK_UNBLANK);
        }
        else {
		s6e63m0_power(lcd, FB_BLANK_POWERDOWN);
        }

        return len;
}

static DEVICE_ATTR(lcd_power, 0664,
                NULL, s6e63m0_sysfs_store_lcd_power);

static ssize_t panel_id_show(struct device *dev,
							struct device_attribute *attr,
							char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	u8 idbuf[3];

	if (s6e63m0_read_panel_id(lcd, idbuf)) {
		dev_err(lcd->dev,"Failed to read panel id\n");
		return sprintf(buf, "Failed to read panel id");
	} else {
		return sprintf(buf, "LCD Panel id = 0x%x, 0x%x, 0x%x\n", idbuf[0], idbuf[1], idbuf[2]);
	}
}
static DEVICE_ATTR(panel_id, 0444, panel_id_show, NULL);

static ssize_t panel_type_show(struct device *dev,
							struct device_attribute *attr,
							char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	u8 idbuf[3]= {0,0,0};

	if (s6e63m0_read_panel_id(lcd, idbuf)) {
		dev_err(lcd->dev,"Failed to read panel id\n");
		return sprintf(buf, "Failed to read panel id");
	} else {
		return sprintf(buf, "LCD Panel id = 0x%x, 0x%x, 0x%x\n", idbuf[0], idbuf[1], idbuf[2]);
	}
}
static DEVICE_ATTR(panel_type, 0444, panel_type_show, NULL);

static ssize_t acl_set_show(struct device *dev, struct
device_attribute *attr, char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd->acl_enable);
	strcpy(buf, temp);

	return strlen(buf);
}
static ssize_t acl_set_store(struct device *dev, struct
device_attribute *attr, const char *buf, size_t size)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = strict_strtoul(buf, (unsigned int) 0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else{
		dev_info(dev, "acl_set_store - %d, %d\n", lcd->acl_enable, value);
		if (lcd->acl_enable != value) {
			lcd->acl_enable = value;
			if (lcd->ldi_state)
				s6e63m0_set_acl(lcd);
		}
		return 0;
	}
}

static DEVICE_ATTR(acl_set, 0664,
		acl_set_show, acl_set_store);

static ssize_t s6e63m0_sysfs_show_gamma_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	char temp[10];

	switch (lcd->gamma_mode) {
	case 0:
		sprintf(temp, "2.2 mode\n");
		strcat(buf, temp);
		break;
	case 1:
		sprintf(temp, "1.9 mode\n");
		strcat(buf, temp);
		break;
	default:
		dev_dbg(dev, "gamma mode could be 2.2 or 1.9)n");
		break;
	}

	return strlen(buf);
}

static ssize_t s6e63m0_sysfs_store_gamma_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	int rc;

	rc = strict_strtoul(buf, 0, (unsigned long *)&lcd->gamma_mode);
	if (rc < 0)
		return rc;

	if (lcd->gamma_mode > 1)
	{
		lcd->gamma_mode = 0;
		dev_err(dev, "there are only 2 types of gamma mode(0:2.2, 1:1.9)\n");
	}
	else
		dev_info(dev, "%s :: gamma_mode=%d\n", __FUNCTION__, lcd->gamma_mode);

	if (lcd->ldi_state)
	{
		if((lcd->current_brightness == lcd->bl) && (lcd->current_gamma_mode == lcd->gamma_mode))
			printk("there is no gamma_mode & brightness changed\n");
		else
			s6e63m0_gamma_ctl(lcd);
	}
	return len;
}

static DEVICE_ATTR(gamma_mode, 0644,
		s6e63m0_sysfs_show_gamma_mode, s6e63m0_sysfs_store_gamma_mode);

static ssize_t s6e63m0_sysfs_show_gamma_table(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct s6e63m0 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd->gamma_table_count);
	strcpy(buf, temp);

	return strlen(buf);
}
static DEVICE_ATTR(gamma_table, 0644,
		s6e63m0_sysfs_show_gamma_table, NULL);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void s6e63m0_mcde_panel_early_suspend(struct early_suspend *earlysuspend);
static void s6e63m0_mcde_panel_late_resume(struct early_suspend *earlysuspend);
#endif



static int __init s6e63m0_spi_probe(struct spi_device *spi)
{
	int ret = 0;
	struct s6e63m0 *lcd = container_of(spi->dev.driver, struct s6e63m0, spi_drv.driver);
	#ifdef SMART_DIMMING
	u8 lcd_id[3];
	#endif

	dev_dbg(&spi->dev, "panel s6e63m0 spi being probed\n");

	dev_set_drvdata(&spi->dev, lcd);

	/* s6e63m0 lcd panel uses 3-wire 9bits SPI Mode. */
	spi->bits_per_word = 9;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed.\n");
		goto out;
	}

	lcd->spi = spi;

	/*
	 * if lcd panel was on from bootloader like u-boot then
	 * do not lcd on.
	 */
	if (!lcd->pd->platform_enabled) {
		/*
		 * if lcd panel was off from bootloader then
		 * current lcd status is powerdown and then
		 * it enables lcd panel.
		 */
		lcd->power = FB_BLANK_POWERDOWN;

		s6e63m0_power(lcd, FB_BLANK_UNBLANK);
	} else {
		lcd->power = FB_BLANK_UNBLANK;
		lcd->ldi_state = LDI_STATE_ON;
	}

	/* force acl on */
	s6e63m0_panel_send_sequence(lcd, ACL_cutoff_set[7]);
	dev_dbg(&spi->dev, "s6e63m0 spi has been probed.\n");

	#ifdef SMART_DIMMING
	s6e63m0_read_panel_id(lcd, lcd_id);

	if (lcd_id[1] >= SMART_MTP_PANEL_ID) {
		if (!is_load_mtp_offset) {
		#if 0
			s6e63mo_read_mtp_info(lcd);
		#else
			s6e63mo_mtp_from_boot(lcd, mtp_data_from_boot);
		#endif
			is_load_mtp_offset =  1;
		}
	}
	#endif

out:
	return ret;
}

static int __devinit s6e63m0_mcde_panel_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct s6e63m0 *lcd = NULL;
	struct backlight_device *bd = NULL;
	struct ssg_dpi_display_platform_data *pdata = ddev->dev.platform_data;

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	if (pdata == NULL) {
		dev_err(&ddev->dev, "%s:Platform data missing\n", __func__);
		ret = -EINVAL;
		goto no_pdata;
	}

	if (ddev->port->type != MCDE_PORTTYPE_DPI) {
		dev_err(&ddev->dev,
			"%s:Invalid port type %d\n",
			__func__, ddev->port->type);
		ret = -EINVAL;
		goto invalid_port_type;
	}

	ddev->try_video_mode = try_video_mode;
	ddev->set_video_mode = set_video_mode;
	ddev->set_rotation = s6e63m0_set_rotation;
	ddev->update = s6e63m0_display_update;
	ddev->apply_config = s6e63m0_apply_config;

	lcd = kzalloc(sizeof(struct s6e63m0), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	mutex_init(&lcd->lock);

	dev_set_drvdata(&ddev->dev, lcd);
	lcd->mdd = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("panel", &ddev->dev,
					lcd, &s6e63m0_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);
		goto out_free_lcd;
	}else {
		if(device_create_file(&(lcd->ld->dev), &dev_attr_panel_type) < 0) {
			dev_err(&(lcd->ld->dev), "failed to add panel_type sysfs entries\n");
		}
	}
#endif


	mutex_init(&lcd->lock);
	bd = backlight_device_register("panel",
					&ddev->dev,
					lcd,
					&s6e63m0_backlight_ops,
					&s6e63m0_backlight_props);
	if (IS_ERR(bd)) {
		ret =  PTR_ERR(bd);
		goto out_backlight_unregister;
	}
	lcd->bd = bd;
	lcd->bd->props.max_brightness = MAX_BRIGHTNESS;
	lcd->bd->props.brightness = DEFAULT_BRIGHTNESS;
	lcd->bl = DEFAULT_GAMMA_LEVEL;
	lcd->current_brightness = DEFAULT_GAMMA_LEVEL;
	lcd->rotation = MCDE_DISPLAY_ROT_0;	
	lcd->acl_enable = 0;
	lcd->cur_acl = 0;
	lcd->panel_id = 0;
	lcd->elvss_ref = 0;
	/*
	 * it gets gamma table count available so it lets user
	 * know that.
	 */
	lcd->gamma_table_count = sizeof(gamma_table) / (MAX_GAMMA_LEVEL * sizeof(int));

        ret = device_create_file(&(ddev->dev), &dev_attr_lcd_power);
        if (ret < 0)
                dev_err(&(ddev->dev), "failed to add lcd_power sysfs entries\n");

        ret = device_create_file(&(ddev->dev), &dev_attr_panel_id);
        if (ret < 0)
                dev_err(&(ddev->dev), "failed to add panel_id sysfs entries\n");

	ret = device_create_file(&(ddev->dev), &dev_attr_acl_set);
        if (ret < 0)
                dev_err(&(ddev->dev), "failed to add acl_set sysfs entries\n");

	ret = device_create_file(&(ddev->dev), &dev_attr_gamma_mode);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add sysfs entries\n");

	ret = device_create_file(&(ddev->dev), &dev_attr_gamma_table);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add sysfs entries\n");

	lcd->spi_drv.driver.name	= "pri_lcd_spi";
	lcd->spi_drv.driver.bus		= &spi_bus_type;
	lcd->spi_drv.driver.owner	= THIS_MODULE;
	lcd->spi_drv.probe		= s6e63m0_spi_probe;
	ret = spi_register_driver(&lcd->spi_drv);
	if (ret < 0) {
		dev_err(&(ddev->dev), "Failed to register SPI driver");
		goto out_backlight_unregister;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	lcd->earlysuspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB - 2;
	lcd->earlysuspend.suspend = s6e63m0_mcde_panel_early_suspend;
	lcd->earlysuspend.resume  = s6e63m0_mcde_panel_late_resume;
	register_early_suspend(&lcd->earlysuspend);
#endif

	#if 1
	if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
			"janice_lcd_dpi", 50)) {
		pr_info("pcrm_qos_add APE failed\n");
	}
	#endif

	dev_dbg(&ddev->dev, "DPI display probed\n");

	goto out;

out_backlight_unregister:
	backlight_device_unregister(bd);
out_free_lcd:
	mutex_destroy(&lcd->lock);
	kfree(lcd);
invalid_port_type:
no_pdata:
out:
	return ret;
}

static int __devexit s6e63m0_mcde_panel_remove(struct mcde_display_device *ddev)
{
	struct s6e63m0 *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);
	s6e63m0_power(lcd, FB_BLANK_POWERDOWN);
	backlight_device_unregister(lcd->bd);
	spi_unregister_driver(&lcd->spi_drv);
	kfree(lcd);

	return 0;
}

static void s6e63m0_mcde_panel_shutdown(struct mcde_display_device *ddev)
{
	struct s6e63m0 *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);
	s6e63m0_power(lcd, FB_BLANK_POWERDOWN);
	backlight_device_unregister(lcd->bd);
	spi_unregister_driver(&lcd->spi_drv);

	#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&lcd->earlysuspend);
	#endif

	kfree(lcd);
}

static int s6e63m0_mcde_panel_resume(struct mcde_display_device *ddev)
{
	int ret;
	struct s6e63m0 *lcd = dev_get_drvdata(&ddev->dev);
	DPI_DISP_TRACE;

	/* set_power_mode will handle call platform_enable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);

	s6e63m0_power(lcd, FB_BLANK_UNBLANK);

	return ret;
}

static int s6e63m0_mcde_panel_suspend(struct mcde_display_device *ddev, pm_message_t state)
{
	int ret = 0;
	struct s6e63m0 *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	lcd->beforepower = lcd->power;
	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	ret = s6e63m0_power(lcd, FB_BLANK_POWERDOWN);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);

	dev_dbg(&ddev->dev, "end %s\n", __func__);
	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND

static pin_cfg_t janice_sleep_pins[] = {
	GPIO169_GPIO | PIN_INPUT_PULLDOWN,
	GPIO171_GPIO | PIN_INPUT_PULLDOWN,
};

static pin_cfg_t janice_resume_pins[] = {
	GPIO169_LCDA_DE,
	GPIO171_LCDA_HSO,
};


static int dpi_display_platform_enable(struct s6e63m0 *lcd)
{
	int res = 0;
	dev_info(lcd->dev, "%s\n", __func__);
	nmk_config_pins(janice_resume_pins, ARRAY_SIZE(janice_resume_pins));
	res = ux500_pins_enable(dpi_pins);
	if (res)
		dev_warn(lcd->dev, "Failure during %s\n", __func__);
	return res;
}

static int dpi_display_platform_disable(struct s6e63m0 *lcd)
{
	int res = 0;
	dev_info(lcd->dev, "%s\n", __func__);
	nmk_config_pins(janice_sleep_pins, ARRAY_SIZE(janice_sleep_pins));

	/* pins disabled to save power */
	res = ux500_pins_disable(dpi_pins);
	if (res)
		dev_warn(lcd->dev, "Failure during %s\n", __func__);
	return res;
}

static void s6e63m0_mcde_panel_early_suspend(struct early_suspend *earlysuspend)
{
	struct s6e63m0 *lcd = container_of(earlysuspend, struct s6e63m0, earlysuspend);
	pm_message_t dummy;

	s6e63m0_mcde_panel_suspend(lcd->mdd, dummy);
	dpi_display_platform_disable(lcd);

	#if 1
	prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP,
				"janice_lcd_dpi");
	#endif
}

static void s6e63m0_mcde_panel_late_resume(struct early_suspend *earlysuspend)
{
	struct s6e63m0 *lcd = container_of(earlysuspend, struct s6e63m0, earlysuspend);

	#if 1
	if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
			"janice_lcd_dpi", 50)) {
		pr_info("pcrm_qos_add APE failed\n");
	}
	#endif

	dpi_display_platform_enable(lcd);
	s6e63m0_mcde_panel_resume(lcd->mdd);
}
#endif

static struct mcde_display_driver s6e63m0_mcde = {
	.probe          = s6e63m0_mcde_panel_probe,
	.remove         = s6e63m0_mcde_panel_remove,
	#if 0
	.shutdown	= s6e63m0_mcde_panel_shutdown,
	#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	.suspend        = NULL,
	.resume         = NULL,
#else
	.suspend        = s6e63m0_mcde_panel_suspend,
	.resume         = s6e63m0_mcde_panel_resume,
#endif
	.driver		= {
		.name	= LCD_DRIVER_NAME_S6E63M0,
		.owner	= THIS_MODULE,
	},
};


static int __init s6e63m0_init(void)
{
	int ret = 0;
	ret =  mcde_display_driver_register(&s6e63m0_mcde);

	#ifdef CONFIG_HAS_EARLYSUSPEND
	dpi_pins = ux500_pins_get("mcde-dpi");
	if (!dpi_pins)
		return -EINVAL;
	#endif

        return ret;
}

static void __exit s6e63m0_exit(void)
{
	mcde_display_driver_unregister(&s6e63m0_mcde);
}

module_init(s6e63m0_init);
module_exit(s6e63m0_exit);

MODULE_AUTHOR("InKi Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("S6E63M0 LCD Driver");
MODULE_LICENSE("GPL");
