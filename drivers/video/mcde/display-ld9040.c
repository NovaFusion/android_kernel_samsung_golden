/*
 * LD9040 AMOLED LCD panel driver.
 *
 * Author: Donghwa Lee  <dh09.lee@samsung.com>
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


#include <video/mcde_display.h>
#include <video/mcde_display_ssg_dpi.h>
#include "display-ld9040_gamma.h"

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define	DEFMASK			0xFF00
#define COMMAND_ONLY	0xFE
#define DATA_ONLY		0xFF

#define MIN_SUPP_BRIGHTNESS	0
#define MAX_SUPP_BRIGHTNESS	MAX_GAMMA_LEVEL
#define MAX_REQ_BRIGHTNESS	255
#define LCD_POWER_UP		1
#define LCD_POWER_DOWN		0
#define LDI_STATE_ON		1
#define LDI_STATE_OFF		0
/* Taken from the programmed value of the LCD clock in PRCMU */
#define PIX_CLK_FREQ	25000000
#define VMODE_XRES		480
#define VMODE_YRES		800
#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

/* to be removed when display works */
#define dev_dbg	dev_info

extern unsigned int system_rev;

struct ld9040 {
	struct device			*dev;
	struct spi_device		*spi;
	struct mutex		lock;
	unsigned int 			beforepower;
	unsigned int			power;
	unsigned int			current_brightness;
	unsigned int			gamma_mode;
	unsigned int			gamma_table_count;
	unsigned int 			ldi_state;
	struct mcde_display_device 	*mdd;
	struct backlight_device		*bd;
	struct ssg_dpi_display_platform_data		*pd;
};

struct ld9040_spi_driver {
		struct spi_driver	base;
		struct ld9040		* lcd;
};

/* command sequences for LD9040 display */
static const unsigned short SEQ_SWRESET[] = {
	0x01, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_USER_SETTING[] = {
	0xF0, 0x5A,

	DATA_ONLY, 0x5A,
	ENDDEF, 0x00
};

static const unsigned short SEQ_ELVSS_ON[] = {
	0xB1, 0x0D,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x16,
	ENDDEF, 0x00
};

static const unsigned short SEQ_GTCON[] = {
	0xF7, 0x09,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	ENDDEF, 0x00
};

static const unsigned short SEQ_PANEL_CONDITION[] = {
	0xF8, 0x05,

	DATA_ONLY, 0x65,
	DATA_ONLY, 0x96,
	DATA_ONLY, 0x71,
	DATA_ONLY, 0x7D,
	DATA_ONLY, 0x19,
	DATA_ONLY, 0x3B,
	DATA_ONLY, 0x0D,
	DATA_ONLY, 0x19,
	DATA_ONLY, 0x7E,
	DATA_ONLY, 0x0D,
	DATA_ONLY, 0xE2,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x7E,
	DATA_ONLY, 0x7D,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x20,
	DATA_ONLY, 0x02,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_GAMMA_SET1[] = {
	0xF9, 0x00,

	DATA_ONLY, 0xA7,
	DATA_ONLY, 0xB4,
	DATA_ONLY, 0xAE,
	DATA_ONLY, 0xBF,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x91,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xB2,
	DATA_ONLY, 0xB4,
	DATA_ONLY, 0xAA,
	DATA_ONLY, 0xBB,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xAC,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xB3,
	DATA_ONLY, 0xB1,
	DATA_ONLY, 0xAA,
	DATA_ONLY, 0xBC,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0xB3,
	ENDDEF, 0x00
};

static const unsigned short SEQ_GAMMA_CTRL[] = {
	0xFB, 0x02,

	DATA_ONLY, 0x5A,
	ENDDEF, 0x00
};

static const unsigned short SEQ_GAMMA_START[] = {
	0xF9, COMMAND_ONLY,

	ENDDEF, 0x00
};

static const unsigned short SEQ_APON[] = {
	0xF3, 0x00,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x0A,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_DISPCTL[] = {
	0xF2, 0x02,

	DATA_ONLY, 0x08,
	DATA_ONLY, 0x08,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x10,
	ENDDEF, 0x00
};

static const unsigned short SEQ_MANPWR[] = {
	0xB0, 0x04,
		
	ENDDEF, 0x00
};

static const unsigned short SEQ_PWR_CTRL[] = {
	0xF4, 0x0A,

	DATA_ONLY, 0x87,
	DATA_ONLY, 0x25,
	DATA_ONLY, 0x6A,
	DATA_ONLY, 0x44,
	DATA_ONLY, 0x02,
	DATA_ONLY, 0x88,
	ENDDEF, 0x00
};

static const unsigned short SEQ_SLPOUT[] = {
	0x11, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_SLPIN[] = {
	0x10, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_DISPON[] = {
	0x29, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_DISPOFF[] = {
	0x28, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VCI1_1ST_EN[] = {
	0xF3, 0x10,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VL1_EN[] = {
	0xF3, 0x11,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VL2_EN[] = {
	0xF3, 0x13,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VCI1_2ND_EN[] = {
	0xF3, 0x33,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VL3_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VREG1_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x01,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VGH_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x11,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VGL_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0x31,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x02,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VMOS_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xB1,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VINT_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xF1,
	/* DATA_ONLY, 0x71,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short SEQ_VBH_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xF9,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	ENDDEF, 0x00
};

static const unsigned short SEQ_VBL_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFD,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	ENDDEF, 0x00
};

static const unsigned short SEQ_GAM_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short SEQ_SD_AMP_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x80,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short SEQ_GLS_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x81,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short SEQ_ELS_EN[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x83,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static const unsigned short SEQ_EL_ON[] = {
	0xF3, 0x37,

	DATA_ONLY, 0xFF,
	/* DATA_ONLY, 0x73,	VMOS/VBL/VBH not used */
	DATA_ONLY, 0x87,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,
	/* DATA_ONLY, 0x02,	VMOS/VBL/VBH not used */
	ENDDEF, 0x00
};

static void print_vmode(struct mcde_video_mode *vmode)
{
	pr_debug("resolution: %dx%d\n", vmode->xres, vmode->yres);
	pr_debug("  pixclock: %d\n",    vmode->pixclock);
	pr_debug("       hbp: %d\n",    vmode->hbp);
	pr_debug("       hfp: %d\n",    vmode->hfp);
	pr_debug("       hsw: %d\n",    vmode->hsw);
	pr_debug("       vbp: %d\n",    vmode->vbp1);
	pr_debug("       vfp: %d\n",    vmode->vfp1);
	pr_debug("       vsw: %d\n",    vmode->vsw);
	pr_debug("interlaced: %s\n", vmode->interlaced ? "true" : "false");
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

	if (video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) {
		video_mode->hsw = 2;
		video_mode->hbp = 8 - video_mode->hsw; /* from end of hsync */
		video_mode->hfp = 8;
		video_mode->vsw = 2;
		video_mode->vbp1 = 16 - video_mode->vsw; /* from end of vsync */
		video_mode->vfp1 = 16;
		video_mode->vbp2 = 0;
		video_mode->vfp2 = 0;
		video_mode->interlaced = false;
		/*
		 * The pixclock setting is not used within MCDE. The clock is
		 * setup elsewhere. But the pixclock value is visible in user
		 * space.
		 */
		video_mode->pixclock =	(int) (1e+12 * (1.0 / PIX_CLK_FREQ));
		res = 0;
	} /* TODO: add more supported resolutions here */

	if (res == 0)
		print_vmode(video_mode);
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
	static int video_mode_apply_during_boot = 1;
	struct ld9040 *lcd = dev_get_drvdata(&ddev->dev);

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		goto out;
	}
	ddev->video_mode = *video_mode;
	print_vmode(video_mode);
	if (video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) {
		/* TODO: set resolution dependent driver data here */
		//driver_data->xxx = yyy;
		res = 0;
	}
	if (res < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		goto error;
	}

	/* TODO: set general driver data here */
	//driver_data->xxx = yyy;

	res = mcde_chnl_set_video_mode(ddev->chnl_state, &ddev->video_mode);
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


static int ld9040_spi_write_byte(struct ld9040 *lcd, int addr, int data)
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

static int ld9040_spi_write(struct ld9040 *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	if (address != DATA_ONLY)
		ret = ld9040_spi_write_byte(lcd, 0x0, address);
	if (command != COMMAND_ONLY)
		ret = ld9040_spi_write_byte(lcd, 0x1, command);

	return ret;
}

static int ld9040_spi_write_words(struct ld9040 *lcd,
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
static int ld9040_panel_send_sequence(struct ld9040 *lcd,
	const unsigned short *wbuf)
{
	int ret = 0, i = 0, j = 0;
	u16 temp[256];

	while ((wbuf[i] & DEFMASK) != ENDDEF) {
		if ((wbuf[i] & DEFMASK) != SLEEPMSEC) {
			if (wbuf[i] != DATA_ONLY)
				temp[j++] = wbuf[i];
			if (wbuf[i+1] != COMMAND_ONLY)
				temp[j++] = wbuf[i+1] | 0x100;
			i += 2;
		} else {
			if (j > 0 )
				ret = ld9040_spi_write_words(lcd, temp, j);
			udelay(wbuf[i+1]*1000);
			j = 0;
		}
	}

	ret = ld9040_spi_write_words(lcd, temp, j);

	return ret;
}

static int _ld9040_gamma_ctl(struct ld9040 *lcd, const unsigned int *gamma)
{
	unsigned int i = 0;
	int ret = 0;

	/* start gamma table updating. */
	ret = ld9040_panel_send_sequence(lcd, SEQ_GAMMA_START);
	if (ret) {
		dev_err(lcd->dev, "failed to disable gamma table updating.\n");
		goto gamma_err;
	}

	for (i = 0 ; i < GAMMA_TABLE_COUNT; i++) {
		ret = ld9040_spi_write(lcd, DATA_ONLY, gamma[i]);
		if (ret) {
			dev_err(lcd->dev, "failed to set gamma table.\n");
			goto gamma_err;
		}
	}

	/* update gamma table. */
	ret = ld9040_panel_send_sequence(lcd, SEQ_GAMMA_CTRL);
	if (ret)
		dev_err(lcd->dev, "failed to update gamma table.\n");

gamma_err:
	return ret;
}

static int ld9040_gamma_ctl(struct ld9040 *lcd, int gamma)
{
	int ret = 0;

	ret = _ld9040_gamma_ctl(lcd, gamma_table.gamma_22_table[gamma]);

	return ret;
}

static int ld9040_ldi_init(struct ld9040 *lcd)
{
	int ret, i;
	const unsigned short *init_seq[] = {
		SEQ_USER_SETTING,
		SEQ_PANEL_CONDITION,
		SEQ_DISPCTL,
		SEQ_MANPWR,
		SEQ_PWR_CTRL,
		SEQ_ELVSS_ON,
		SEQ_GTCON,
		SEQ_GAMMA_SET1,
	};

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = ld9040_panel_send_sequence(lcd, init_seq[i]);
		mdelay(5);
		if (ret)
			break;
	}
	lcd->current_brightness = MAX_SUPP_BRIGHTNESS;
	return ret;
}

static int ld9040_ldi_enable(struct ld9040 *lcd)
{
	int ret = 0;

	ret = ld9040_panel_send_sequence(lcd, SEQ_DISPON);

	return ret;
}

static int ld9040_ldi_disable(struct ld9040 *lcd)
{
	int ret;

	ret = ld9040_panel_send_sequence(lcd, SEQ_DISPOFF);
	ret = ld9040_panel_send_sequence(lcd, SEQ_SLPIN);

	return ret;
}

static int ld9040_power_on(struct ld9040 *lcd)
{
	int ret = 0;
	int brightness;
	struct ssg_dpi_display_platform_data *pd = NULL;
	pd = lcd->pd;

	if (!pd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else {
		pd->power_on(pd, LCD_POWER_UP);
		msleep(10 /*pd->power_on_delay*/);
	}

	if (!pd->reset) {
		dev_err(lcd->dev, "reset is NULL.\n");
		return -EFAULT;
	} else {
		pd->reset(pd);
		msleep(10/*pd->reset_delay*/);
		dev_dbg(lcd->dev, "Applied reset delay\n");
	}

	ret = ld9040_ldi_init(lcd);
	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return ret;
	}

	brightness = lcd->bd->props.brightness;
	if (brightness) {
		brightness = brightness / (MAX_REQ_BRIGHTNESS/MAX_SUPP_BRIGHTNESS);
		if (brightness >= MAX_SUPP_BRIGHTNESS)
			brightness = MAX_SUPP_BRIGHTNESS - 1;

		 ret = ld9040_gamma_ctl(lcd, brightness);
		 if (ret) {
			 dev_err(lcd->dev, "lcd brightness setting failed.\n");
			 return -EIO;
		 }
	}
	lcd->current_brightness = brightness;

	ret = ld9040_panel_send_sequence(lcd, SEQ_SLPOUT);
	if (ret) {
		dev_err(lcd->dev, "failed to sleep out ldi.\n");
		return ret;
	}
	msleep(120);	/* sleeep out delay */

	if (lcd->current_brightness) {
		ret = ld9040_panel_send_sequence(lcd, SEQ_DISPON);
		dev_err(lcd->dev, "failed to display on ldi.\n");
		return ret;
	}

	dev_dbg(lcd->dev, "ldi power on successful\n");

	return 0;
}

static int ld9040_power_off(struct ld9040 *lcd)
{
	int ret = 0;
	struct ssg_dpi_display_platform_data *pd = NULL;

	pd = lcd->pd;
	if (!pd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	ret = ld9040_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}

	msleep(120/*pd->power_off_delay*/);

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else
		pd->power_on(pd, LCD_POWER_DOWN);

	return 0;
}

static int ld9040_power(struct ld9040 *lcd, int power)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = ld9040_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = ld9040_power_off(lcd);

	if (!ret)
		lcd->power = power;

	mutex_unlock(&lcd->lock);
	return ret;
}

static int ld9040_set_power(struct lcd_device *ld, int power)
{
	struct ld9040 *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return ld9040_power(lcd, power);
}

static int ld9040_get_power(struct lcd_device *ld)
{
	struct ld9040 *lcd = lcd_get_data(ld);

	return lcd->power;
}

/* This structure defines all the properties of a backlight */
struct backlight_properties ld9040_backlight_props = {
	.brightness = MAX_REQ_BRIGHTNESS,
	.max_brightness = MAX_REQ_BRIGHTNESS,
};

static int ld9040_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int ld9040_set_brightness(struct backlight_device *bd)
{
	int ret = 0, brightness = bd->props.brightness;
	struct ld9040 *lcd = bl_get_data(bd);

	dev_dbg(&bd->dev, "lcd set brightness called with %d\n", brightness);
	if (brightness < MIN_SUPP_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d.\n",
			MIN_SUPP_BRIGHTNESS, bd->props.max_brightness);
		return -EINVAL;
	}

	mutex_lock(&lcd->lock);
	if (brightness) {
		brightness = brightness / (MAX_REQ_BRIGHTNESS/MAX_SUPP_BRIGHTNESS);
		if (brightness >= MAX_SUPP_BRIGHTNESS)
			brightness = MAX_SUPP_BRIGHTNESS - 1;

		if (POWER_IS_ON(lcd->power)) {
		 	if (!lcd->current_brightness)
			 	ld9040_panel_send_sequence(lcd, SEQ_DISPON);

			 ret = ld9040_gamma_ctl(lcd, brightness);
			 if (ret) {
				 dev_err(&bd->dev, "lcd brightness setting failed.\n");
				 return -EIO;
			 }
		}
	}
	else if (POWER_IS_ON(lcd->power)) {
		ld9040_panel_send_sequence(lcd, SEQ_DISPOFF);
	}
	lcd->current_brightness = brightness;
	mutex_unlock(&lcd->lock);
	return ret;
}

static const struct backlight_ops ld9040_backlight_ops  = {
	.get_brightness = ld9040_get_brightness,
	.update_status = ld9040_set_brightness,
};

static ssize_t ld9040_sysfs_show_gamma_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ld9040 *lcd = dev_get_drvdata(dev);
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
		dev_info(dev, "gamma mode could be 0:2.2 or 1:1.9\n");
		break;
	}

	return strlen(buf);
}

static ssize_t ld9040_sysfs_store_gamma_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct ld9040 *lcd = dev_get_drvdata(dev);
	struct backlight_device *bd = NULL;
	int brightness, rc, mode;

	rc = strict_strtoul(buf, 0, (unsigned long *)&mode);
	if (rc < 0)
		return rc;

	bd = lcd->bd;

	if (bd->props.brightness) {
		brightness = bd->props.brightness / (MAX_REQ_BRIGHTNESS/MAX_SUPP_BRIGHTNESS);
		if (brightness >= MAX_SUPP_BRIGHTNESS)
			brightness = MAX_SUPP_BRIGHTNESS - 1;
	}
	else
		brightness = bd->props.brightness;

	mutex_lock(&lcd->lock);
	if (POWER_IS_ON(lcd->power)) {
		switch (lcd->gamma_mode) {
		case 0:
			_ld9040_gamma_ctl(lcd, gamma_table.gamma_22_table[brightness]);
			break;
		case 1:
			_ld9040_gamma_ctl(lcd, gamma_table.gamma_19_table[brightness]);
			break;
		default:
			dev_info(dev, "gamma mode could be 0:2.2 or 1:1.9\n");
			_ld9040_gamma_ctl(lcd, gamma_table.gamma_22_table[brightness]);
			break;
		}
	}
	lcd->gamma_mode = mode;
	lcd->current_brightness = brightness;
	mutex_unlock(&lcd->lock);
	return len;
}

static DEVICE_ATTR(gamma_mode, 0644,
		ld9040_sysfs_show_gamma_mode, ld9040_sysfs_store_gamma_mode);

static ssize_t ld9040_sysfs_show_gamma_table(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ld9040 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd->gamma_table_count);
	strcpy(buf, temp);

	return strlen(buf);
}

static DEVICE_ATTR(gamma_table, 0644,
		ld9040_sysfs_show_gamma_table, NULL);

static int __init ld9040_spi_probe(struct spi_device *spi);
static struct ld9040_spi_driver ld9040_spi_drv = {
	.base = {
		.driver = {
			.name	= "pri_lcd_spi",
			.bus	= &spi_bus_type,
			.owner	= THIS_MODULE,
		},
		.probe		= ld9040_spi_probe,
	},
	.lcd = NULL,
};

static int __init ld9040_spi_probe(struct spi_device *spi)
{
	int ret = 0;
	struct ld9040 *lcd = ld9040_spi_drv.lcd;
	
	dev_dbg(&spi->dev, "panel ld9040 spi being probed\n");

	dev_set_drvdata(&spi->dev, lcd);

	/* ld9040 lcd panel uses 3-wire 9bits SPI Mode. */
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

		ld9040_power(lcd, FB_BLANK_UNBLANK);
	} else
		lcd->power = FB_BLANK_UNBLANK;


	dev_dbg(&spi->dev, "ld9040 spi has been probed.\n");

out:
	return ret;
}

static int __devinit ld9040_mcde_panel_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct ld9040 *lcd = NULL;
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

	ddev->prepare_for_update = NULL;
       	ddev->try_video_mode = try_video_mode;
	ddev->set_video_mode = set_video_mode;


	lcd = kzalloc(sizeof(struct ld9040), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;
	dev_set_drvdata(&ddev->dev, lcd);
	lcd->mdd = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;

	mutex_init(&lcd->lock);
	bd = backlight_device_register("pri_lcd_bl",
								&ddev->dev,
								lcd,
								&ld9040_backlight_ops,
								&ld9040_backlight_props);
	if (IS_ERR(bd)) {
		ret =  PTR_ERR(bd);
		goto out_backlight_unregister;
	}
	lcd->bd = bd;

	/*
	 * it gets gamma table count available so it lets user
	 * know that.
	 */
	lcd->gamma_table_count = sizeof(gamma_table) / (MAX_GAMMA_LEVEL * sizeof(int));

	ret = device_create_file(&(ddev->dev), &dev_attr_gamma_mode);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add sysfs entries\n");

	ret = device_create_file(&(ddev->dev), &dev_attr_gamma_table);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add sysfs entries\n");

	ld9040_spi_drv.lcd = lcd;
	ret = spi_register_driver((struct spi_driver *)& ld9040_spi_drv);
	if (ret < 0) {
		dev_err(&(ddev->dev), "Failed to register SPI driver");
		goto out_backlight_unregister;
	}
	
	dev_dbg(&ddev->dev, "DPI display probed\n");

	goto out;

out_backlight_unregister:
	backlight_device_unregister(bd);
	kfree(lcd);
invalid_port_type:
no_pdata:
out:
	return ret;
}

static int ld9040_mcde_panel_resume(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct ld9040 *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);

	/*
	 * after suspended, if lcd panel status is FB_BLANK_UNBLANK
	 * (at that time, power is FB_BLANK_UNBLANK) then
	 * it changes that status to FB_BLANK_POWERDOWN to get lcd on.
	 */
	if (lcd->beforepower == FB_BLANK_UNBLANK)
		lcd->power = FB_BLANK_POWERDOWN;

	dev_dbg(&ddev->dev, "power = %d\n", lcd->beforepower);

	ret = ld9040_power(lcd, lcd->beforepower);

	return ret;
}

static int ld9040_mcde_panel_suspend(struct mcde_display_device *ddev, pm_message_t state)
{
	int ret = 0;
	struct ld9040 *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	lcd->beforepower = lcd->power;
	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	ret = ld9040_power(lcd, FB_BLANK_POWERDOWN);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);

	return ret;
}

static int __devexit ld9040_mcde_panel_remove(struct mcde_display_device *ddev)
{
	struct ld9040 *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);
	ld9040_power(lcd, FB_BLANK_POWERDOWN);
	backlight_device_unregister(lcd->bd);
	spi_unregister_driver((struct spi_driver *)&ld9040_spi_drv);
	kfree(lcd);

	return 0;
}


static struct mcde_display_driver ld9040_mcde = {
	.probe          = ld9040_mcde_panel_probe,
	.remove         = ld9040_mcde_panel_remove,
	.suspend        = ld9040_mcde_panel_suspend,
	.resume         = ld9040_mcde_panel_resume,
	.driver		= {
		.name	= LCD_DRIVER_NAME_LD9040,
		.owner 	= THIS_MODULE,
	},
};


static int __init ld9040_init(void)
{
	int ret = 0;
	ret =  mcde_display_driver_register(&ld9040_mcde);
	return ret;
}

static void __exit ld9040_exit(void)
{
	mcde_display_driver_unregister(&ld9040_mcde);
}

module_init(ld9040_init);
module_exit(ld9040_exit);

MODULE_AUTHOR("Donghwa Lee <dh09.lee@samsung.com>");
MODULE_DESCRIPTION("ld9040 LCD Driver");
MODULE_LICENSE("GPL");

