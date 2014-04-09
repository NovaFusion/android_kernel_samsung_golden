/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE Samsung M1361A0 DSI display driver
 *
 * Author: Gareth Phillips <gareth.phillips@samsung.com>
 *
 * License terms: GNU General Public License (GPL), version 2.
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
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>

#include <video/mcde_display.h>
#include <video/mcde_display-sec-dsi.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define VID_MODE_REFRESH_RATE 60

#define M1316A0_DRIVER_NAME	"mcde_disp_m1316a0_dsi"

#define VMODE_XRES		480
#define VMODE_YRES		800

#define VFP 6
#define VBP 19
#define VSW 4
#define HFP 45
#define HBP 45
#define HSW 16

#define MAX_BRIGHTNESS		255
#define DEFAULT_BRIGHTNESS	160

#define DIM_BL 20
#define MIN_BL 30
#define MAX_BL 255

/* used to write DSI/DCS commands when video stream active */
#define MAX_DCS_CMD_ALLOWED		(DSILINK_MAX_DSI_DIRECT_CMD_WRITE - 1)
#define HX8369_MAX_CONT_DATA_LEN	(MAX_DCS_CMD_ALLOWED - 1)

#define DCS_CMD_DISPLAY_ON		0x29
#define DCS_CMD_EXIT_SLEEP_MODE		0x11
#define DCS_CMD_WDISP_BRIGHT		0x51
#define DCS_CMD_READ_ID1		0xDA
#define DCS_CMD_READ_ID2		0xDB
#define DCS_CMD_READ_ID3		0xDC
#define DCS_CMD_SETCNCD			0xFD
#define DCS_CMD_WMAC			0x36
#define DCS_CMD_SEQ_DELAY_MS		0xFE
#define DCS_CMD_SEQ_END			0xFF

#define DCS_CMD_SEQ_PARAM(x)		(2+(x))	/* idx param within a cmd seq */

#define ROTATE_0_SETTING	0x40
#define ROTATE_180_SETTING	0x00


static const u8 DCS_CMD_SEQ_STANDBY[] = {
/*	Length	Command 	Parameters */
	4,	0xB9,		0xFF,0x83,0x69,
	DCS_CMD_SEQ_DELAY_MS,	200,
	93,	0xD5,		0x00,0x00,0x0F,0x03,0x36,
				0x00,0x00,0x10,0x01,0x00,
				0x00,0x00,0x1A,0x50,0x45,
				0x00,0x00,0x13,0x50,0x45,
				0x47,0x00,0x00,0x02,0x04,
				0x00,0x00,0x00,0x00,0x00,
				0x00,0x00,0x03,0x00,0x00,
				0x00,0x88,0x88,0x37,0x5F,
				0x1E,0x18,0x88,0x88,0x85,
				0x88,0x88,0x40,0x2F,0x6E,
				0x48,0x88,0x88,0x80,0x88,
				0x88,0x26,0x4F,0x0E,0x08,
				0x88,0x88,0x84,0x88,0x88,
				0x51,0x3F,0x7E,0x58,0x88, 
				0x88,0x81,0x00,0x00,0x00,
				0x01,0x00,0x00,0x00,0x07,
				0xF8,0x0F,0xFF,0xFF,0x07,
				0xF8,0x0F,0xFF,0xFF,0x00,
				0x00,0x18,
	2,	0x3A,		0x70,
	2,	0x36,		0x00,	
	3,	0xB5,		0x12,0x12,
	12,	0xB1,		0x12,0x83,0x77,0x00,0x12,
				0x12,0x1E,0x1E,0x0C,0x1A,
				0x00,
	5,	0xB3,		0x83,0x00,0x3A,0x17,
	2,	0xB4,		0x02,
	4,	0xB6,		0xBB,0xBC,0x00,
	5,	0xE3,		0x03,0x03,0x03,0x03,
	7,	0xBA,		0x31,0x00,0x00,0x16,0xC5,0x30,
	7,	0xC0,		0x73,0x50,0x00,0x3F,0xC4,0x04,
	2,	0xC1,		0x00,
	2,	0xCC,		0x0C,
	2,	0xEA,		0x7A,
	36,	0xE0,		0x00,0x01,0x04,0x0E,0x11,
				0x3C,0x28,0x37,0x04,0x0D,
				0x11,0x15,0x17,0x15,0x15,
				0x13,0x18,0x00,0x01,0x04,
				0x0E,0x11,0x3C,0x28,0x37,
				0x04,0x0D,0x11,0x15,0x17,
				0x15,0x15,0x13,0x18,0x01,
	DCS_CMD_SEQ_END,
};

static const u8 DCS_CMD_SEQ_ENTER_SLEEP[] = {
/*	Length	Command 			Parameters */
	1,	DCS_CMD_SET_DISPLAY_OFF,
	DCS_CMD_SEQ_DELAY_MS,			200,
	1,	DCS_CMD_ENTER_SLEEP_MODE,
	DCS_CMD_SEQ_DELAY_MS,			200,

	DCS_CMD_SEQ_END
};

static const u8 DCS_CMD_SEQ_EXIT_SLEEP[] = {
/*	Length	Command 			Parameters */
	1,	DCS_CMD_EXIT_SLEEP_MODE,
	DCS_CMD_SEQ_DELAY_MS,			200,
	1,	DCS_CMD_SET_DISPLAY_ON,
	DCS_CMD_SEQ_DELAY_MS,			120,

	DCS_CMD_SEQ_END
};

//#undef dev_dbg
//#define dev_dbg dev_info

struct m1316a0_dsi_lcd {
	struct device			*dev;
	struct mutex			lock;
	unsigned int			current_brightness;
	unsigned int			bl;
	struct mcde_display_device	*ddev;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct sec_dsi_platform_data	*pd;
	bool 				opp_is_requested;
	bool				justStarted;
	enum mcde_display_rotation	rotation;
	u8				lcd_id[3];
	bool				panel_awake;	
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend		earlysuspend;
#endif
};


static int m1316a0_dsi_display_sleep(struct m1316a0_dsi_lcd *lcd);
static int m1316a0_dsi_display_exit_sleep(struct m1316a0_dsi_lcd *lcd);
#ifdef CONFIG_HAS_EARLYSUSPEND
static void m1316a0_dsi_early_suspend(struct early_suspend *earlysuspend);
static void m1316a0_dsi_late_resume(struct early_suspend *earlysuspend);
#endif

static int m1316a0_dsi_dcs_write_command(struct mcde_display_device *ddev,
					u8 cmd, u8* p_data, int len)
{
	int retries = 1;
	int len_to_write;
	u8 *data;
	int write_len;
	int ret = 0;
	u8 cont_buf[DSILINK_MAX_DSI_DIRECT_CMD_WRITE] = {0,};

	do {
		len_to_write = len;
		data = p_data;
		write_len = len_to_write;
		if (write_len > MAX_DCS_CMD_ALLOWED)
			write_len = MAX_DCS_CMD_ALLOWED;

		ret = mcde_dsi_dcs_write(ddev->chnl_state, cmd, data,
					write_len);

		len_to_write -= write_len;
		data += write_len;

		while ((len_to_write > 0) && (ret == 0)) {
			write_len = len_to_write;
			if (write_len > HX8369_MAX_CONT_DATA_LEN)
				write_len = HX8369_MAX_CONT_DATA_LEN;

			cont_buf[0] = data[0];	/* dummy data */
			memcpy(cont_buf+1, data, write_len);

			ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SETCNCD,
						cont_buf, write_len+1);

			len_to_write-= write_len;
			data += write_len;
		}
		if (ret)
			usleep_range(8000,8000);
	}while (ret && --retries);

	if (ret) {
		dev_warn(&ddev->dev, "Failed to send DCS cmd %x, error %d\n",
			cmd, ret);
	}

	return ret;
}


static int m1316a0_dsi_dcs_write_sequence(struct mcde_display_device *ddev,
							const u8 *p_seq)
{
	int ret = 0;

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = m1316a0_dsi_dcs_write_command(ddev, p_seq[1],
				(u8 *)&p_seq[2], p_seq[0] - 1);
			p_seq += p_seq[0] + 1;
		}
	}

	return ret;
}

static int m1316a0_write_dcs_vid_cmd(struct mcde_display_device *ddev,
					u8 cmd, u8* p_data, int len)
{
	int retries = 1;
	int len_to_write;
	u8 *data;
	int write_len;
	int ret = 0;
	u8 cont_buf[DSILINK_MAX_DSI_DIRECT_CMD_WRITE] = {0,};

	do {
		len_to_write = len;
		data = p_data;
		write_len = len_to_write;
		if (write_len > MAX_DCS_CMD_ALLOWED)
			write_len = MAX_DCS_CMD_ALLOWED;

		ret = mcde_dsi_dcs_write(ddev->chnl_state, cmd, data, write_len);

		dev_vdbg(&ddev->dev, "Send DCS cmd %x, len %d, ret=%d\n",
			cmd, write_len, ret);

		len_to_write -= write_len;
		data += write_len;

		while ((len_to_write > 0) && (ret == 0)) {
			write_len = len_to_write;
			if (write_len > HX8369_MAX_CONT_DATA_LEN)
				write_len = HX8369_MAX_CONT_DATA_LEN;

			cont_buf[0] = data[0];
			memcpy(cont_buf+1, data, write_len);

			ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_SETCNCD,
						cont_buf, write_len+1);

			len_to_write-= write_len;
			data += write_len;
		}
		if (ret)
			usleep_range(8000,8000);
	}while (ret && --retries);

	if (ret)
		dev_warn(&ddev->dev, "Failed to send DCS Vid Cmd 0x%x, Ret %d\n",
			cmd, ret);
	else
		dev_vdbg(&ddev->dev, "Wrote DCS Vid Cmd 0x%x\n", cmd);

	return ret;
}

static int m1316a0_write_dcs_vid_seq(struct mcde_display_device *ddev,
							const u8 *p_seq)
{
	int ret = 0;

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = m1316a0_write_dcs_vid_cmd(ddev, p_seq[1],
				(u8 *)&p_seq[2], p_seq[0] - 1);
			p_seq += p_seq[0] + 1;
		}
	}

	return ret;
}

static int m1316a0_dsi_read_panel_id(struct m1316a0_dsi_lcd *lcd)
{
	int ret = 0;
	int len = 1;

	mcde_dsi_set_max_pkt_size(lcd->ddev->chnl_state);

	if (lcd->lcd_id[0] != 0xFE) {
		dev_dbg(&lcd->ddev->dev, "%s: Read device id of the display\n",
						__func__);

		ret = mcde_dsi_dcs_read(lcd->ddev->chnl_state,
						DCS_CMD_READ_ID1,
						(u32 *)&lcd->lcd_id[0], &len);
		if (!ret)
			ret = mcde_dsi_dcs_read(lcd->ddev->chnl_state,
						DCS_CMD_READ_ID2,
						(u32 *)&lcd->lcd_id[1], &len);
		if (!ret)
			ret = mcde_dsi_dcs_read(lcd->ddev->chnl_state,
						DCS_CMD_READ_ID3,
						(u32 *)&lcd->lcd_id[2], &len);
		if (ret)
			dev_info(&lcd->ddev->dev,
				"mcde_dsi_dcs_read failed to read disp ID\n");

		pr_info("Panel ID = 0x%x:0x%x:0x%x\n", lcd->lcd_id[0],
							lcd->lcd_id[1],
							lcd->lcd_id[2]);
	}

	return ret;
}

static int m1316a0_dsi_update_brightness(struct mcde_display_device *ddev,
						int brightness)
{
	struct m1316a0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
	int ret = 0;
	u8 data = 0;

	/* Defensive - to prevent table overrun. */
	if (brightness > MAX_BRIGHTNESS)
		brightness = MAX_BRIGHTNESS;

	lcd->bl = brightness;
	data = brightness;

	ret = m1316a0_write_dcs_vid_cmd(ddev, DCS_CMD_WDISP_BRIGHT, &data, 1);

	dev_dbg(&ddev->dev,"backlight level = [%d]...brightness=[%d]\n",lcd->bl,brightness);

	return ret;
}

static int m1316a0_dsi_get_brightness(struct backlight_device *bd)
{
	dev_dbg(&bd->dev, "lcd get brightness returns %d\n", bd->props.brightness);

	return bd->props.brightness;
}

static int m1316a0_dsi_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	int brightness = bd->props.brightness;

	struct m1316a0_dsi_lcd *lcd = bl_get_data(bd);

	if ((brightness < 0) ||	(brightness > bd->props.max_brightness)) {
		dev_err(&bd->dev, "lcd brightness should be 0 to %d.\n",
			bd->props.max_brightness);
		return -EINVAL;
	}

	mutex_lock(&lcd->lock);

	if (lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) {

		if ((brightness == 0) && (lcd->current_brightness != 0)) {
			ret = m1316a0_dsi_display_sleep(lcd);
		}

		if ((brightness != 0) && (lcd->current_brightness == 0)) {
			ret = m1316a0_dsi_display_exit_sleep(lcd);
		}

		if (!ret)
		ret = m1316a0_dsi_update_brightness(lcd->ddev, brightness);

		if (ret) {
			dev_info(&bd->dev, "lcd brightness setting failed.\n");
			ret = 0;
		}
	}

	lcd->current_brightness = brightness;

	mutex_unlock(&lcd->lock);

	return ret;
}


static struct backlight_ops m1316a0_dsi_backlight_ops  = {
	.get_brightness = m1316a0_dsi_get_brightness,
	.update_status = m1316a0_dsi_set_brightness,
};

struct backlight_properties m1316a0_dsi_backlight_props = {
	.brightness = DEFAULT_BRIGHTNESS,
	.max_brightness = MAX_BRIGHTNESS,
	.type = BACKLIGHT_RAW,
};

static int m1316a0_dsi_power_on(struct m1316a0_dsi_lcd *lcd)
{
	struct sec_dsi_platform_data *pd = lcd->pd;

	dev_info(lcd->dev, "%s: Power on M1316A0 display\n", __func__);

	if (pd->lcd_pwr_onoff) {
		pd->lcd_pwr_onoff(false);
		msleep(100);
		pd->lcd_pwr_onoff(true);
		msleep(25);
	}

	if (pd->reset_gpio) {
		gpio_set_value(pd->reset_gpio, 0);
		mdelay(5);
		gpio_set_value(pd->reset_gpio, 1);
		msleep(10);
	}

	/* temporary : backlight always max */
	if (pd->bl_en_gpio) {
		gpio_set_value(pd->bl_en_gpio, 1);
	}

	return 0;
}

static int m1316a0_dsi_power_off(struct m1316a0_dsi_lcd *lcd)
{
	struct sec_dsi_platform_data *pd = lcd->pd;
	int ret = 0;

	dev_info(lcd->dev, "%s: Power off display\n", __func__);

	/* temporary : backlight always max */
	if (pd->bl_en_gpio) {
		gpio_set_value(pd->bl_en_gpio, 0);
	}

	if (pd->reset_gpio)
		gpio_set_value(pd->reset_gpio, 0);

	if (pd->lcd_pwr_onoff)
		pd->lcd_pwr_onoff(false);

	lcd->panel_awake = false;

	return ret;
}

static int m1316a0_dsi_display_init(struct m1316a0_dsi_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_info(lcd->dev, "%s: Initialise M1316A0 display\n", __func__);

	mcde_formatter_enable(ddev->chnl_state);	/* ensure MCDE enabled */

	dev_info(lcd->dev, "%s : start to send lcd init seq!\n", __func__);

	ret = m1316a0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_STANDBY);
	ret |= m1316a0_dsi_read_panel_id(lcd);

	//ret |= m1316a0_dsi_update_brightness(ddev, lcd->bd->props.brightness);

	return 0;/*For PBA test on assembly line*/
}

static int m1316a0_dsi_display_sleep(struct m1316a0_dsi_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_dbg(lcd->dev, "%s: display sleep\n", __func__);

	if (lcd->panel_awake) {
		ret = m1316a0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_ENTER_SLEEP);

		if (!ret)
			lcd->panel_awake = false;
	}
	return ret;
}

static int m1316a0_dsi_display_exit_sleep(struct m1316a0_dsi_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_dbg(lcd->dev, "%s: M1316A0 display exit sleep\n", __func__);

	if (!lcd->panel_awake) {

		ret = m1316a0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_EXIT_SLEEP);

		if (!ret)
			lcd->panel_awake = true;
	}
	return ret;
}


static int m1316a0_dsi_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	static int notFirstTime;
	int ret = 0;
	enum mcde_display_rotation final;
	struct m1316a0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_hw_rotation final_hw_rot;
	u8 data = 0;

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
				data = ROTATE_180_SETTING;
				ret = m1316a0_dsi_dcs_write_command(ddev,
								DCS_CMD_WMAC,
								&data, 1);
				lcd->rotation = final;
			}
		} else if (final == MCDE_DISPLAY_ROT_0) {
			if (final != lcd->rotation) {
				data = ROTATE_0_SETTING;
				ret = m1316a0_dsi_dcs_write_command(ddev,
								DCS_CMD_WMAC,
								&data, 1);
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


static int m1316a0_dsi_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode)
{
	struct m1316a0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_display_power_mode orig_mode = ddev->power_mode;
	int ret = 0;

	mutex_lock(&lcd->lock);

	/* OFF -> STANDBY or OFF -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {
		ret = m1316a0_dsi_power_on(lcd);
		if (ret)
			goto err;

		ret = m1316a0_dsi_display_init(lcd);
		if (ret)
			goto err;

		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_ON) {

		if (lcd->justStarted) {
			lcd->justStarted = false;
			mcde_chnl_disable(ddev->chnl_state);
			if (lcd->pd->reset_gpio) {
				gpio_set_value(lcd->pd->reset_gpio, 0);
				msleep(2);
				gpio_set_value(lcd->pd->reset_gpio, 1);
				msleep(10);
			}
			ret = m1316a0_dsi_display_init(lcd);
		}
		
		ret = m1316a0_dsi_display_exit_sleep(lcd);
		if (ret)
			goto err;

		if ((!lcd->opp_is_requested) && (lcd->pd->min_ddr_opp > 0)) {
			if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
							M1316A0_DRIVER_NAME,
							lcd->pd->min_ddr_opp)) {
				dev_err(lcd->dev, "add DDR OPP %d failed\n",
					lcd->pd->min_ddr_opp);
			}
			dev_dbg(lcd->dev, "DDR OPP requested at %d%%\n",
							lcd->pd->min_ddr_opp);
			lcd->opp_is_requested = true;
		}

		ddev->power_mode = MCDE_DISPLAY_PM_ON;
	}
	/* ON -> STANDBY */
	else if (ddev->power_mode == MCDE_DISPLAY_PM_ON &&
					power_mode <= MCDE_DISPLAY_PM_STANDBY) {

		ret = m1316a0_dsi_display_sleep(lcd);
		if (ret && (power_mode != MCDE_DISPLAY_PM_OFF))
			goto err;

		if (lcd->opp_is_requested) {
			prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP,
							M1316A0_DRIVER_NAME);
			lcd->opp_is_requested = false;
			dev_dbg(lcd->dev, "DDR OPP removed\n");
		}
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_OFF) {
		ret = m1316a0_dsi_power_off(lcd);
		if (ret)
			goto err;
		ddev->power_mode = MCDE_DISPLAY_PM_OFF;
	}

	if (orig_mode != ddev->power_mode)
		dev_warn(&ddev->dev, "Power from mode %d to %d\n",
			orig_mode, ddev->power_mode);

	ret = mcde_chnl_set_power_mode(ddev->chnl_state, ddev->power_mode);
err:
	mutex_unlock(&lcd->lock);
	return ret;
}


#define REFRESH_RATE 60
static int m1316a0_dsi_try_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = 0;
	int bpp;
	int freq_hs_clk;
	static u32 bit_hs_clk = 0;
	u32 pclk;
	
	dev_dbg(&ddev->dev, "hjoun %s \n",__func__);
	if (!ddev || !video_mode) {
		dev_warn(&ddev->dev,
			"%s: dev or video_mode equals NULL, aborting\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}


	 /*
	  * pixel clock (Hz) > (VACT+VBP+VFP+VSA) * (HACT+HBP+HFP+HSA) *
	  *                    framerate * bpp / num_data_lanes * 1.1
	  */
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
		video_mode->xres_padding = ddev->x_res_padding;
		video_mode->yres_padding = ddev->y_res_padding;		
		/* -445681 display padding */

		pclk = 1000000000 / VID_MODE_REFRESH_RATE;
		pclk /= video_mode->xres + video_mode->xres_padding +
			video_mode->hsw + video_mode->hbp + video_mode->hfp;
		pclk *= 1000;
		pclk /= video_mode->yres + video_mode->yres_padding +
			video_mode->vsw + video_mode->vbp + video_mode->vfp;

		/* for BURST_MODE, add 10% */
		switch (ddev->port->phy.dsi.vid_mode) {
		case BURST_MODE_WITH_SYNC_EVENT:
		case BURST_MODE_WITH_SYNC_PULSE:
			pclk += pclk / 10;
			break;
		default:
			break;
		}

		video_mode->pixclock = pclk;
	} else {
		dev_warn(&ddev->dev,
			"%s:Failed to find video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		ret = -EINVAL;
	}
out:
	return ret;
}


static int m1316a0_dsi_set_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = -EINVAL;
	struct mcde_video_mode channel_video_mode;
	static int alreadyCalled;

	if (!ddev || !video_mode) {
		dev_warn(&ddev->dev,
			"%s: dev or video_mode equals NULL, aborting\n",
			__func__);
		return ret;
	}

	ddev->video_mode = *video_mode;
	channel_video_mode = ddev->video_mode;

	dev_dbg(&ddev->dev, "hjoun %s rot=%d,xres=%d,yres=%d\n",
		__func__, ddev->rotation, video_mode->xres, video_mode->yres);

	/* Dependant on if display should rotate orup MCDE should rotate */
	if (ddev->rotation == MCDE_DISPLAY_ROT_90_CCW ||
				ddev->rotation == MCDE_DISPLAY_ROT_90_CW) {
		channel_video_mode.xres = ddev->native_x_res;
		channel_video_mode.yres = ddev->native_y_res;
	}

	/* +445681 display padding */
	channel_video_mode.xres_padding = ddev->x_res_padding;
	channel_video_mode.yres_padding = ddev->y_res_padding;	
	/* -445681 display padding */	

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
		alreadyCalled++;
	}

	return ret;
}


/* Reverse order of power on and channel update as compared with MCDE default display update */
static int m1316a0_dsi_display_update(struct mcde_display_device *ddev,
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
	dev_vdbg(&ddev->dev, "Overlay updated, chnl=%d\n", ddev->chnl_id);

	return 0;
}


#ifdef CONFIG_LCD_CLASS_DEVICE

static int m1316a0_dsi_power(struct m1316a0_dsi_lcd *lcd, int power)
{
	int ret = 0;

	switch (power) {
	case FB_BLANK_POWERDOWN:
		dev_dbg(lcd->dev, "%s(): Powering Off, was %s\n",__func__,
			(lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) ? "ON" : "OFF");
		ret = m1316a0_dsi_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_OFF);
		break;
	case FB_BLANK_NORMAL:
		dev_dbg(lcd->dev, "%s(): Into Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_ON) ? "ON" : "SLEEP/OFF");
		ret = m1316a0_dsi_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_STANDBY);
		break;
	case FB_BLANK_UNBLANK:
		dev_dbg(lcd->dev, "%s(): Exit Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_STANDBY) ? "SLEEP" : "ON/OFF");
		ret = m1316a0_dsi_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_ON);
		break;
	default:
		ret = -EINVAL;
		dev_info(lcd->dev, "Invalid power change request (%d)\n", power);
		break;
	}

	return ret;
}

static int m1316a0_dsi_set_power(struct lcd_device *ld, int power)
{
	struct m1316a0_dsi_lcd *lcd = lcd_get_data(ld);

	dev_dbg(lcd->dev, "%s: power=%d\n", __func__, power);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return m1316a0_dsi_power(lcd, power);
}

static int m1316a0_dsi_get_power(struct lcd_device *ld)
{
	struct m1316a0_dsi_lcd *lcd = lcd_get_data(ld);
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

static struct lcd_ops m1316a0_dsi_lcd_ops = {
	.set_power = m1316a0_dsi_set_power,
	.get_power = m1316a0_dsi_get_power,
};

#endif // CONFIG_LCD_CLASS_DEVICE

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[20];
	sprintf(temp, "M1316A0\n");
	strcat(buf, temp);
	return strlen(buf);
}
static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);

static int __devinit m1316a0_dsi_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct sec_dsi_platform_data *pdata = ddev->dev.platform_data;
	struct m1316a0_dsi_lcd *lcd = NULL;
//	struct backlight_device *bd = NULL;

	dev_info(&ddev->dev, "function entered\n");

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
		gpio_direction_output(pdata->reset_gpio, 1);
	}

	if (pdata->lcd_pwr_setup)
		pdata->lcd_pwr_setup(&ddev->dev);

	ddev->set_power_mode = m1316a0_dsi_set_power_mode;
	ddev->set_rotation = m1316a0_dsi_set_rotation;
	ddev->try_video_mode = m1316a0_dsi_try_video_mode;
	ddev->set_video_mode = m1316a0_dsi_set_video_mode;
	ddev->update = m1316a0_dsi_display_update;

	ddev->native_x_res = VMODE_XRES;
	ddev->native_y_res = VMODE_YRES;

	lcd = kzalloc(sizeof(struct m1316a0_dsi_lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("panel", &ddev->dev,
					lcd, &m1316a0_dsi_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);
		dev_err(&ddev->dev, "%s: Failed to register m1316a0_dsi display device\n", __func__);
		goto out_free_lcd;
	}
#endif

	if (pdata->bl_ctrl) {
		if (pdata->bl_en_gpio != -1) {
			ret = gpio_request(pdata->bl_en_gpio, "LCD_BL_CTRL");
			if (ret) {
				dev_err(&ddev->dev,
					"%s: Failed to request gpio %d\n",
					__func__, pdata->bl_en_gpio);
				goto backlight_device_register_failed;
			}
		}

		lcd->bd = backlight_device_register("panel",
						&ddev->dev,
						lcd,
						&m1316a0_dsi_backlight_ops,
						&m1316a0_dsi_backlight_props);

		if (IS_ERR(lcd->bd)) {
			ret =  PTR_ERR(lcd->bd);
			goto backlight_device_register_failed;
		}
	}

	lcd->ddev = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;
	lcd->opp_is_requested = false;
	lcd->justStarted = true;
	lcd->rotation = MCDE_DISPLAY_ROT_0;
	lcd->bl = DEFAULT_BRIGHTNESS;
	lcd->panel_awake = false;

	dev_set_drvdata(&ddev->dev, lcd);
	mutex_init(&lcd->lock);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_lcd_type);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n",
					__LINE__);

#ifdef CONFIG_HAS_EARLYSUSPEND
        lcd->earlysuspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
        lcd->earlysuspend.suspend = m1316a0_dsi_early_suspend;
        lcd->earlysuspend.resume  = m1316a0_dsi_late_resume;
        register_early_suspend(&lcd->earlysuspend);
#endif
	dev_info(&ddev->dev, "function exit\n");

	return 0;

backlight_device_register_failed:
	lcd_device_unregister(lcd->ld);
	kfree(lcd);
	return ret;

out_free_lcd:

	kfree(lcd);
	return ret;

request_reset_gpio_failed:
	
	return ret;
}

static int __devexit m1316a0_dsi_remove(struct mcde_display_device *ddev)
{
	struct m1316a0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
	struct sec_dsi_platform_data *pdata = lcd->pd;

	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);

	if (pdata->reset_gpio) {
		gpio_direction_input(pdata->reset_gpio);
		gpio_free(pdata->reset_gpio);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
        unregister_early_suspend(&lcd->earlysuspend);
#endif
	kfree(lcd);

	return 0;
}


//#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
static int m1316a0_dsi_resume(struct mcde_display_device *ddev)
{
	int ret;

	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	/* set_power_mode will handle call platform_enable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);
#if 0    
	ddev->set_synchronized_update(ddev,
					ddev->get_synchronized_update(ddev));
#endif
	return ret;
}

static int m1316a0_dsi_suspend(struct mcde_display_device *ddev, \
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
#ifdef CONFIG_HAS_EARLYSUSPEND
static void m1316a0_dsi_early_suspend(
		struct early_suspend *earlysuspend)
{
    struct m1316a0_dsi_lcd *lcd = container_of(earlysuspend,
						struct m1316a0_dsi_lcd,
						earlysuspend);
    pm_message_t dummy;

    dev_dbg(&lcd->ddev->dev, "%s function entered\n", __func__);
    m1316a0_dsi_suspend(lcd->ddev, dummy);
}

static void m1316a0_dsi_late_resume(
		struct early_suspend *earlysuspend)
{
    struct m1316a0_dsi_lcd *lcd = container_of(earlysuspend,
						struct m1316a0_dsi_lcd,
						earlysuspend);

    dev_dbg(&lcd->ddev->dev, "%s function entered\n", __func__);
    m1316a0_dsi_resume(lcd->ddev);
}
#endif

/* Power down all displays on reboot, poweroff or halt. */
static void m1316a0_dsi_shutdown(struct mcde_display_device *ddev)
{
	struct m1316a0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);

	dev_info(&ddev->dev, "%s\n", __func__);

	m1316a0_dsi_power(lcd, FB_BLANK_POWERDOWN);
	
	kfree(lcd);
	dev_info(&ddev->dev, "end %s\n", __func__);
};

static struct mcde_display_driver m1316a0_dsi_driver = {
	.probe	= m1316a0_dsi_probe,
	.remove = m1316a0_dsi_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = m1316a0_dsi_suspend,
	.resume = m1316a0_dsi_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.shutdown = m1316a0_dsi_shutdown,

	.driver = {
		.name	= M1316A0_DRIVER_NAME,
	},
};

/* Module init */
static int __init mcde_display_m1316a0_dsi_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&m1316a0_dsi_driver);
}
module_init(mcde_display_m1316a0_dsi_init);

static void __exit mcde_display_m1316a0_dsi_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&m1316a0_dsi_driver);
}
module_exit(mcde_display_m1316a0_dsi_exit);

MODULE_AUTHOR("Gareth Phillips <gareth.phillips@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung MCDE M1316A0 DSI display driver");
