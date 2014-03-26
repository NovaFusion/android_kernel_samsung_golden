/*
 * Samsung GT-I9060 LCD panel driver.
 *
 * Author: Robert Teather  <robert.teather@samsung.com>
 *
 * Derived from drivers/video/mcde/display-godin.c
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

#include <video/mcde_display.h>
#include <video/mcde_display_ssg_dpi.h>

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define	DEFMASK			0xFF00
#define COMMAND_ONLY		0xFE
#define DATA_ONLY		0xFF

#define MIN_SUPP_BRIGHTNESS	0
#define MAX_SUPP_BRIGHTNESS	255
#define MAX_REQ_BRIGHTNESS	255
#define DEFAULT_BRIGHTNESS	108
#define LCD_POWER_UP		1
#define LCD_POWER_DOWN		0
#define LDI_STATE_ON		1
#define LDI_STATE_OFF		0
/* Taken from the programmed value of the LCD clock in PRCMU */
#define PIX_CLK_FREQ		25000000
#define VMODE_XRES		480
#define VMODE_YRES		800
#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

#define PANEL_ID_SMD		3
#define PANEL_ID_SONY_A_SI	4

/* to be removed when display works */
//#define dev_dbg	dev_info

extern unsigned int system_rev;

struct i9060_lcd {
	struct device				*dev;
	struct spi_device			*spi;
	struct mutex				lock;
	unsigned int 				beforepower;
	unsigned int				power;
	unsigned int				current_brightness;
	unsigned int 				ldi_state;
	unsigned char				panel_id;
	struct mcde_display_device 		*mdd;
	struct lcd_device			*ld;
	struct backlight_device			*bd;
	struct ssg_dpi_display_platform_data	*pd;
	struct spi_driver			spi_drv;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend    		earlysuspend;
#endif
};

static const unsigned short SMD_INIT_SEQ[] = {
	/* Initializing Sequence */
	0x36,0x09,

	0xB0,0x00,
	
#if 1
/* causes display to fail */
	0xC0,0x28,
	DATA_ONLY,0x08,	/* Backporch */

	0xC1,0x01,
	DATA_ONLY,0x30,
	DATA_ONLY,0x15,
	DATA_ONLY,0x05,
	DATA_ONLY,0x22,

	0xC4,0x10,
	DATA_ONLY,0x01,
	DATA_ONLY,0x00,

	0xC5,0x06,
	DATA_ONLY,0x55,
	DATA_ONLY,0x03,
	DATA_ONLY,0x07,
	DATA_ONLY,0x0B,
	DATA_ONLY,0x33,
	DATA_ONLY,0x00,
	DATA_ONLY,0x01,
	DATA_ONLY,0x03,

//	0xC6, 0x00,	// RGB Sync option
#endif

	0xC8, 0x00,	// gamma set RED, R_Posi
	DATA_ONLY,0x00,
	DATA_ONLY,0x0F,
	DATA_ONLY,0x29,
	DATA_ONLY,0x43,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x4F,
	DATA_ONLY,0x52,
	DATA_ONLY,0x50,
	DATA_ONLY,0x56,
	DATA_ONLY,0x5B,
	DATA_ONLY,0x5A,
	DATA_ONLY,0x57,
	DATA_ONLY,0x51,
	DATA_ONLY,0x55,
	DATA_ONLY,0x55,
	DATA_ONLY,0x5D,
	DATA_ONLY,0x5F,
	DATA_ONLY,0x1C,
	
	DATA_ONLY,0x00, // R_Nega
	DATA_ONLY,0x00,
	DATA_ONLY,0x0F,
	DATA_ONLY,0x29,
	DATA_ONLY,0x43,
	DATA_ONLY,0x4F,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x52,
	DATA_ONLY,0x50,
	DATA_ONLY,0x56,
	DATA_ONLY,0x5B,
	DATA_ONLY,0x5A,
	DATA_ONLY,0x57,
	DATA_ONLY,0x51,
	DATA_ONLY,0x55,
	DATA_ONLY,0x55,
	DATA_ONLY,0x5D,
	DATA_ONLY,0x5F,
	DATA_ONLY,0x1C,

	0xC9, 0x00,		// gamma set GREEN, G_Posi
	DATA_ONLY,0x00,
	DATA_ONLY,0x28,
	DATA_ONLY,0x36,
	DATA_ONLY,0x48,
	DATA_ONLY,0x50,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x52,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x53,
	DATA_ONLY,0x56,
	DATA_ONLY,0x55,
	DATA_ONLY,0x52,
	DATA_ONLY,0x4B,
	DATA_ONLY,0x4D,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x4F,
	DATA_ONLY,0x4F,
	DATA_ONLY,0x0C,
	
	DATA_ONLY,0x00, // G_Nega
	DATA_ONLY,0x00,
	DATA_ONLY,0x28,
	DATA_ONLY,0x36,
	DATA_ONLY,0x48,
	DATA_ONLY,0x50,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x52,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x53,
	DATA_ONLY,0x56,
	DATA_ONLY,0x55,
	DATA_ONLY,0x52,
	DATA_ONLY,0x4B,
	DATA_ONLY,0x4D,
	DATA_ONLY,0x4E,
	DATA_ONLY,0x4F,
	DATA_ONLY,0x4F,
	DATA_ONLY,0x0C,

	0xCA, 0x00, 	 // gamma set BLUE , B_Posi
	DATA_ONLY,0x00,
	DATA_ONLY,0x26,
	DATA_ONLY,0x35,
	DATA_ONLY,0x48,
	DATA_ONLY,0x52,
	DATA_ONLY,0x51,
	DATA_ONLY,0x57,
	DATA_ONLY,0x54,
	DATA_ONLY,0x5B,
	DATA_ONLY,0x60,
	DATA_ONLY,0x60,
	DATA_ONLY,0x5E,
	DATA_ONLY,0x59,
	DATA_ONLY,0x5E,
	DATA_ONLY,0x5A,
	DATA_ONLY,0x5D,
	DATA_ONLY,0x5C,
	DATA_ONLY,0x2A,
	
	DATA_ONLY,0x00, // B_Nega
	DATA_ONLY,0x00,
	DATA_ONLY,0x26,
	DATA_ONLY,0x35,
	DATA_ONLY,0x48,
	DATA_ONLY,0x52,
	DATA_ONLY,0x51,
	DATA_ONLY,0x57,
	DATA_ONLY,0x54,
	DATA_ONLY,0x5B,
	DATA_ONLY,0x60,
	DATA_ONLY,0x60,
	DATA_ONLY,0x5E,
	DATA_ONLY,0x59,
	DATA_ONLY,0x5E,
	DATA_ONLY,0x5A,
	DATA_ONLY,0x5D,
	DATA_ONLY,0x5C,
	DATA_ONLY,0x2A,

	0xD1,0x33,
	DATA_ONLY,0x13,

	0xD2,0x11,
	DATA_ONLY,0x00,
	DATA_ONLY,0x00,

	0xD3,0x50,
	DATA_ONLY,0x50,

	0xD5,0x2F,
	DATA_ONLY,0x11,
	DATA_ONLY,0x1E,
	DATA_ONLY,0x46,

	0xD6,0x11,
	DATA_ONLY,0x0A,

	/* Sleep Out */
	0x11,	COMMAND_ONLY,
	SLEEPMSEC,20,

	/* NVM Load Sequence */
	0xD4, 0x55,
	DATA_ONLY,0x55,

	0xF8,0x01,
	DATA_ONLY,0xF5,
	DATA_ONLY,0xF2,
	DATA_ONLY,0x71,
	DATA_ONLY,0x44,	

	0xFC,0x00,
	DATA_ONLY,0x08,
	SLEEPMSEC,150,

	/* Set PWM	for backlight */
	0xB4,0x0F,
	DATA_ONLY,0x00,
	DATA_ONLY,0x50,

	0xB7,0x24,

	0xB8,0x01,	/* CABC user Mode */

	ENDDEF, 0x00
};

static const unsigned short SET_DISPLAY_ON[] =
{
	0x29,	COMMAND_ONLY,
	ENDDEF,	0x00
};

static const unsigned short SET_DISPLAY_OFF[] =
{
	0x28,	COMMAND_ONLY,
	ENDDEF,	0x00
};

static const unsigned short ENTER_SLEEP_MODE[] =
{
	0x10,	COMMAND_ONLY,
	ENDDEF,	0x00
};

static const unsigned short EXIT_SLEEP_MODE[] =
{
	0x11,	COMMAND_ONLY,
	ENDDEF,	0x00
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
	dev_dbg(dev, "interlaced: %s\n", 	vmode->interlaced ? "true" : "false");
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

		/* SMD Panel settings */
		video_mode->hsw = 4;
		video_mode->hbp = 40;// - video_mode->hsw; /* from end of hsync */
		video_mode->hfp = 10;
		video_mode->vsw = 1;
		video_mode->vbp = 7;// - video_mode->vsw; /* from end of vsync */
		video_mode->vfp = 6;
		video_mode->interlaced = false;
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
	struct i9060_lcd *lcd = dev_get_drvdata(&ddev->dev);

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


static int i9060_spi_write_byte(struct i9060_lcd *lcd, int addr, int data)
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

static int i9060_spi_read_byte(struct i9060_lcd *lcd, int addr, u8 *data)
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

static int i9060_spi_write(struct i9060_lcd *lcd, unsigned char address,
	unsigned char command)
{
	int ret = 0;

	if (address != DATA_ONLY)
		ret = i9060_spi_write_byte(lcd, 0x0, address);
	if (command != COMMAND_ONLY)
		ret = i9060_spi_write_byte(lcd, 0x1, command);

	return ret;
}

static int i9060_spi_write_words(struct i9060_lcd *lcd,
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
static int i9060_panel_send_sequence(struct i9060_lcd *lcd,
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
		} else {
			if (j > 0 )
				ret = i9060_spi_write_words(lcd, temp, j);
			msleep(wbuf[i+1]);
			j = 0;
		}
		i += 2;
	}

	if (j > 0)
		ret = i9060_spi_write_words(lcd, temp, j);

	return ret;
}

static int i9060_read_panel_id(struct i9060_lcd *lcd,
								u8 *idbuf)
{
	int ret;

	ret = i9060_spi_write(lcd,0xB0,0x00);
	ret |= i9060_spi_write(lcd, 0xDE, COMMAND_ONLY);
	ret |= i9060_spi_read_byte(lcd, 0xDA, &idbuf[0]);
	ret |= i9060_spi_read_byte(lcd, 0xDB, &idbuf[1]);
	ret |= i9060_spi_read_byte(lcd, 0xDC, &idbuf[2]);
	ret |= i9060_spi_write(lcd, 0xDF, COMMAND_ONLY);

	return ret;
}

static int i9060_ldi_set_brightness(struct i9060_lcd *lcd, unsigned short brightness)
{
	int ret;

	ret = i9060_spi_write(lcd, 0xB5, brightness);
	return ret;
}

static int i9060_ldi_disable(struct i9060_lcd *lcd)
{
	int ret;

	ret = i9060_panel_send_sequence(lcd, SET_DISPLAY_OFF);
	ret = i9060_panel_send_sequence(lcd, ENTER_SLEEP_MODE);

	return ret;
}

static int i9060_power_on(struct i9060_lcd *lcd)
{
	int ret;
	int brightness;
	struct ssg_dpi_display_platform_data *pd = lcd->pd;
	
	if (!pd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else {
		pd->power_on(pd, LCD_POWER_UP);
		msleep(50);
	}

	if (!pd->reset) {
		dev_err(lcd->dev, "reset is NULL.\n");
		return -EFAULT;
	} else {
		pd->reset(pd);
		msleep(10);
		dev_dbg(lcd->dev, "Applied reset delay\n");
	}

	ret = i9060_panel_send_sequence(lcd, SMD_INIT_SEQ);
	if (ret) {
		dev_err(lcd->dev, "lcd initialization settings failed.\n");
		return -EIO;
	}

	brightness = lcd->bd->props.brightness;
	if (brightness) {
		brightness = brightness / (MAX_REQ_BRIGHTNESS/MAX_SUPP_BRIGHTNESS);
		if (brightness >= MAX_SUPP_BRIGHTNESS)
			brightness = MAX_SUPP_BRIGHTNESS - 1;

		 ret = i9060_ldi_set_brightness(lcd, brightness);
		 if (ret) {
			dev_err(lcd->dev, "lcd brightness setting failed.\n");
			return -EIO;
		 }
	}
	lcd->current_brightness = brightness;

		if (gpio_is_valid(lcd->pd->bl_en_gpio)) {
			gpio_set_value(lcd->pd->bl_en_gpio,1);
		}
		ret = i9060_panel_send_sequence(lcd, SET_DISPLAY_ON);
		if (ret) {
			dev_err(lcd->dev, "failed to display on ldi.\n");
			return -EIO;
		}
	dev_dbg(lcd->dev, "ldi power on successful\n");

	return 0;
}

static int i9060_power_off(struct i9060_lcd *lcd)
{
	int ret = 0;
	struct ssg_dpi_display_platform_data *pd = NULL;

	pd = lcd->pd;
	if (!pd) {
		dev_err(lcd->dev, "platform data is NULL.\n");
		return -EFAULT;
	}

	if (gpio_is_valid(pd->bl_en_gpio)) {
		gpio_set_value(pd->bl_en_gpio,0);
	}

	ret = i9060_ldi_disable(lcd);
	if (ret) {
		dev_err(lcd->dev, "lcd setting failed.\n");
		return -EIO;
	}
	msleep(120);

	if (!pd->power_on) {
		dev_err(lcd->dev, "power_on is NULL.\n");
		return -EFAULT;
	} else
		pd->power_on(pd, LCD_POWER_DOWN);

	return 0;
}

static int i9060_power(struct i9060_lcd *lcd, int power)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	dev_dbg(lcd->dev, "%s(): old=%d (%s), new=%d (%s)\n", __func__,
		lcd->power, POWER_IS_ON(lcd->power)? "on": "off",
		power, POWER_IS_ON(power)? "on": "off"
		);

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power)) {
		ret = i9060_power_on(lcd);
	}
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power)) {
		ret = i9060_power_off(lcd);
	}
	if (!ret)
		lcd->power = power;

	mutex_unlock(&lcd->lock);
	return ret;
}

static int i9060_set_power(struct lcd_device *ld, int power)
{
	struct i9060_lcd *lcd = lcd_get_data(ld);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return i9060_power(lcd, power);
}

static int i9060_get_power(struct lcd_device *ld)
{
	struct i9060_lcd *lcd = lcd_get_data(ld);

	return lcd->power;
}

static struct lcd_ops i9060_lcd_ops = {
	.set_power = i9060_set_power,
	.get_power = i9060_get_power,
};


/* This structure defines all the properties of a backlight */
struct backlight_properties i9060_backlight_props = {
	.brightness = DEFAULT_BRIGHTNESS,
	.max_brightness = MAX_REQ_BRIGHTNESS,
};

static int i9060_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int i9060_set_brightness(struct backlight_device *bd)
{
	int ret = 0, brightness = bd->props.brightness;
	struct i9060_lcd *lcd = bl_get_data(bd);

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
			ret = i9060_ldi_set_brightness(lcd, brightness);
			if (ret) {
				dev_err(&bd->dev, "lcd brightness setting failed.\n");
				return -EIO;
			}
		}
	}

	lcd->current_brightness = brightness;
	mutex_unlock(&lcd->lock);
	return ret;
}

static const struct backlight_ops i9060_backlight_ops  = {
	.get_brightness = i9060_get_brightness,
	.update_status = i9060_set_brightness,
};


static ssize_t i9060_sysfs_store_lcd_power(struct device *dev,
                                       struct device_attribute *attr,
                                       const char *buf, size_t len)
{
        int rc;
        int lcd_enable;
	struct i9060_lcd *lcd = dev_get_drvdata(dev);

	dev_info(lcd->dev,"i9060 lcd_sysfs_store_lcd_power\n");

        rc = strict_strtoul(buf, 0, (unsigned long *)&lcd_enable);
        if (rc < 0)
                return rc;

        if(lcd_enable) {
		i9060_power(lcd, FB_BLANK_UNBLANK);
        }
        else {
		i9060_power(lcd, FB_BLANK_POWERDOWN);
        }

        return len;
}

static DEVICE_ATTR(lcd_power, 0664,
                NULL, i9060_sysfs_store_lcd_power);

static ssize_t panel_id_show(struct device *dev,
							struct device_attribute *attr,
							char *buf)
{
	struct i9060_lcd *lcd = dev_get_drvdata(dev);
	u8 idbuf[3];

	if (i9060_read_panel_id(lcd, idbuf)) {
		dev_err(lcd->dev,"Failed to read panel id\n");
		return sprintf(buf, "Failed to read panel id");
	} else {
		return sprintf(buf, "LCD Panel id = 0x%x, 0x%x, 0x%x\n", idbuf[0], idbuf[1], idbuf[2]);
	}
}
static DEVICE_ATTR(panel_id, 0444, panel_id_show, NULL);


static ssize_t lcdtype_show(struct device *dev, struct 
			device_attribute *attr, char *buf)
{

        char temp[16];
        sprintf(temp, "SMD_LMS369KF01\n");
        strcat(buf, temp);
        return strlen(buf);
}

static DEVICE_ATTR(lcdtype, 0664, lcdtype_show, NULL);


#ifdef CONFIG_HAS_EARLYSUSPEND
static void i9060_mcde_panel_early_suspend(struct early_suspend *earlysuspend);
static void i9060_mcde_panel_late_resume(struct early_suspend *earlysuspend);
#endif

static int i9060_spi_probe(struct spi_device *spi)
{
	int ret = 0;
	struct i9060_lcd *lcd = container_of(spi->dev.driver, struct i9060_lcd, spi_drv.driver);
	
	dev_dbg(&spi->dev, "panel i9060 spi being probed\n");

	dev_set_drvdata(&spi->dev, lcd);

	/* i9060 lcd panel uses 3-wire 9bits SPI Mode. */
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

		i9060_power(lcd, FB_BLANK_UNBLANK);
	} else
		lcd->power = FB_BLANK_UNBLANK;

	dev_dbg(&spi->dev, "i9060 spi has been probed.\n");

out:
	return ret;
}

static int i9060_mcde_panel_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct i9060_lcd *lcd = NULL;
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

	lcd = kzalloc(sizeof(struct i9060_lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	dev_set_drvdata(&ddev->dev, lcd);
	lcd->mdd = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("i9060", &ddev->dev,
					lcd, &i9060_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);
		goto out_free_lcd;
	}
#endif

	mutex_init(&lcd->lock);

	if (gpio_is_valid(pdata->bl_en_gpio)) {
		gpio_request(pdata->bl_en_gpio,"LCD BL Ctrl");
		gpio_direction_output(pdata->bl_en_gpio,1);
	}

	bd = backlight_device_register("pwm-backlight",
					&ddev->dev,
					lcd,
					&i9060_backlight_ops,
					&i9060_backlight_props);
	if (IS_ERR(bd)) {
		ret =  PTR_ERR(bd);
		goto out_backlight_unregister;
	}
	lcd->bd = bd;

	ret = device_create_file(&(ddev->dev), &dev_attr_panel_id);
	if (ret < 0)
		dev_err(&(ddev->dev), "failed to add panel_id sysfs entries\n");
        ret = device_create_file(&(ddev->dev), &dev_attr_lcd_power);
        if (ret < 0)
                dev_err(&(ddev->dev), "failed to add lcd_power sysfs entries\n");

        ret = device_create_file(&(ddev->dev), &dev_attr_lcdtype);
        if (ret < 0)
                dev_err(&(ddev->dev), "failed to add lcd_power sysfs entries\n");


	lcd->spi_drv.driver.name	= "pri_lcd_spi",
	lcd->spi_drv.driver.bus		= &spi_bus_type,
	lcd->spi_drv.driver.owner	= THIS_MODULE,
	lcd->spi_drv.probe		= i9060_spi_probe,

	ret = spi_register_driver(&lcd->spi_drv);
	if (ret < 0) {
		dev_err(&(ddev->dev), "Failed to register SPI driver");
		goto out_backlight_unregister;
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	lcd->earlysuspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	lcd->earlysuspend.suspend = i9060_mcde_panel_early_suspend;
	lcd->earlysuspend.resume  = i9060_mcde_panel_late_resume;
	register_early_suspend(&lcd->earlysuspend);
#endif

	dev_dbg(&ddev->dev, "DPI display probed\n");

	goto out;

out_backlight_unregister:
	backlight_device_unregister(bd);
out_free_lcd:
	kfree(lcd);
invalid_port_type:
no_pdata:
out:
	return ret;
}

static int i9060_mcde_panel_resume(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct i9060_lcd *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	/* set_power_mode will handle call platform_disable */
	ret = lcd->mdd->set_power_mode(lcd->mdd, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&lcd->mdd->dev, "%s:Failed to resume display\n"
			, __func__);

	/*
	 * after suspended, if lcd panel status is FB_BLANK_UNBLANK
	 * (at that time, power is FB_BLANK_UNBLANK) then
	 * it changes that status to FB_BLANK_POWERDOWN to get lcd on.
	 */
	if (lcd->beforepower == FB_BLANK_UNBLANK)
		lcd->power = FB_BLANK_POWERDOWN;

	dev_dbg(&ddev->dev, "power = %d\n", lcd->beforepower);

	ret = i9060_power(lcd, lcd->beforepower);

	return ret;
}

static int i9060_mcde_panel_suspend(struct mcde_display_device *ddev, pm_message_t state)
{
	int ret = 0;
	struct i9060_lcd *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);

	lcd->beforepower = lcd->power;
	/*
	 * when lcd panel is suspend, lcd panel becomes off
	 * regardless of status.
	 */
	ret = i9060_power(lcd, FB_BLANK_POWERDOWN);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);

	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void i9060_mcde_panel_early_suspend(struct early_suspend *earlysuspend)
{
	struct i9060_lcd *lcd = container_of(earlysuspend, struct i9060_lcd, earlysuspend);
	pm_message_t dummy;
	
	i9060_mcde_panel_suspend(lcd->mdd, dummy);
}

static void i9060_mcde_panel_late_resume(struct early_suspend *earlysuspend)
{
	struct i9060_lcd *lcd = container_of(earlysuspend, struct i9060_lcd, earlysuspend);
	
	i9060_mcde_panel_resume(lcd->mdd);
}
#endif


static int i9060_mcde_panel_remove(struct mcde_display_device *ddev)
{
	struct i9060_lcd *lcd = dev_get_drvdata(&ddev->dev);

	dev_dbg(&ddev->dev, "Invoked %s\n", __func__);
	i9060_power(lcd, FB_BLANK_POWERDOWN);
	backlight_device_unregister(lcd->bd);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&lcd->earlysuspend);
#endif

	spi_unregister_driver(&lcd->spi_drv);
	kfree(lcd);

	return 0;
}


static struct mcde_display_driver i9060_mcde = {
	.probe          = i9060_mcde_panel_probe,
	.remove         = i9060_mcde_panel_remove,
#ifdef CONFIG_HAS_EARLYSUSPEND
	.suspend        = NULL,
	.resume         = NULL,
#else
	.suspend        = i9060_mcde_panel_suspend,
	.resume         = i9060_mcde_panel_resume,
#endif
	.driver		= {
		.name	= LCD_DRIVER_NAME_I9060,
		.owner 	= THIS_MODULE,
	},
};


static int __init i9060_init(void)
{
	int ret = 0;
	ret =  mcde_display_driver_register(&i9060_mcde);
	return ret;
}

static void __exit i9060_exit(void)
{
	mcde_display_driver_unregister(&i9060_mcde);
}

module_init(i9060_init);
module_exit(i9060_exit);

MODULE_AUTHOR("Robert Teather <robert.teather@samsung.com>");
MODULE_DESCRIPTION("i9060 LCD Driver");
MODULE_LICENSE("GPL");

