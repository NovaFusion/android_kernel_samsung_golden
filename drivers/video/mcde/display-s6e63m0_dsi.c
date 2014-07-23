/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE Samsung S6E63M0 DSI display driver
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

#include "smart_mtp_s6e63m0_dsi.h"
#include "display-s6e63m0_dsi_param.h"
/* +452052 ESD recovery for DSI video */
#include <linux/fb.h>
#include <video/mcde_fb.h>
#include <video/mcde.h>
#include "mcde_struct.h"
#include <linux/compdev.h>
//#include <fcntl.h>
/* -452052 ESD recovery for DSI video */

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#define VID_MODE_REFRESH_RATE 60

//#undef dev_dbg
//#define dev_dbg dev_info

//#define ESD_TEST

struct s6e63m0_dsi_lcd {
	struct device			*dev;
	struct mutex			lock;
	unsigned int			current_brightness;
	unsigned int			current_gamma;
	unsigned int			bl;	
	unsigned int			acl_enable;
	unsigned int			cur_acl;	
	struct mcde_display_device	*ddev;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct sec_dsi_platform_data	*pd;
	bool 				opp_is_requested;
	bool				justStarted;
	enum mcde_display_rotation	rotation;
	u8				lcd_id[3];
	u8				elvss_pulse;
	const u8			*elvss_offsets;
	u8 				**gamma_seq;	
	enum elvss_brightness		elvss_brightness;
	bool				smartDimmingLoaded;
#ifdef SMART_DIMMING
	u8				mtpData[SEC_DSI_MTP_DATA_LEN];
	struct str_smart_dim		smart;
#endif
#ifdef ESD_OPERATION
	unsigned int			lcd_connected;
	unsigned int			esd_enable;
	unsigned int			esd_port;
	struct workqueue_struct		*esd_workqueue;
	struct work_struct		esd_work;
        bool                            esd_processing; /* +452052 ESD recovery for DSI video- */
	enum mcde_hw_rotation 		pre_rotation;	
#endif
	bool				panel_awake;	
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend	earlysuspend;
#endif

#ifdef ESD_TEST
        struct timer_list               esd_test_timer;
#endif
};

#ifdef ESD_TEST
struct s6e63m0_dsi_lcd *pdsi;
#endif


static int s6e63m0_dsi_display_sleep(struct s6e63m0_dsi_lcd *lcd);
static int s6e63m0_dsi_display_exit_sleep(struct s6e63m0_dsi_lcd *lcd);

/* +452052  ESD test 2 */
int mcde_rotate_to_fb(enum mcde_hw_rotation mcde_rot);
/* -452052  ESD test 2 */


#ifdef ESD_TEST
static void est_test_timer_func(unsigned long data)
{
	pr_info("%s\n", __func__);

	if (list_empty(&(pdsi->esd_work.entry))) {
		disable_irq_nosync(GPIO_TO_IRQ(pdsi->esd_port));
		queue_work(pdsi->esd_workqueue, &(pdsi->esd_work));
		pr_info("%s invoked\n", __func__);
	}

	mod_timer(&pdsi->esd_test_timer,  jiffies + (10*HZ));
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void s6e63m0_dsi_early_suspend(
			struct early_suspend *earlysuspend);
static void s6e63m0_dsi_late_resume(
			struct early_suspend *earlysuspend);
#endif
extern unsigned int battpwroff_charging;


#ifdef SMART_DIMMING
static void s6e63m0_init_smart_dimming_table_22(struct s6e63m0_dsi_lcd *lcd)
{
	unsigned int i, j;
	u8 gamma_22[SEC_DSI_MTP_DATA_LEN] = {0,};

	for (i = 0; i < MAX_GAMMA_VALUE; i++) {
		calc_gamma_table_22(&lcd->smart, candela_table[i], gamma_22);
		for (j = 0; j < GAMMA_PARAM_LEN; j++)
			gamma_table_sm2[i][j+3] = (gamma_22[j]); // j+3 : for first value
	}
#if 0
	printk("++++++++++++++++++ !SMART DIMMING RESULT! +++++++++++++++++++\n");

	for (i = 0; i < MAX_GAMMA_VALUE; i++) {
		printk("SmartDimming Gamma Result=[%3d] : ",candela_table[i]);
		for (j = 0; j < GAMMA_PARAM_LEN; j++)
			printk("[0x%02x], ", gamma_table_sm2[i][j+3]);
		printk("\n");
	}
	printk("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");	
#endif
}
#endif
static int s6e63m0_dsi_dcs_write_command(struct mcde_display_device *ddev, u8 cmd, u8* p_data, int len)
{
	int retries = 1;
	int len_to_write;
	u8 *data;
	int write_len;
	int ret = 0;
	u8 globalPara = MCDE_MAX_DSI_DIRECT_CMD_WRITE;

	do {
		len_to_write = len;
		data = p_data;
		write_len = len_to_write;
		if (write_len > MCDE_MAX_DSI_DIRECT_CMD_WRITE)
			write_len = MCDE_MAX_DSI_DIRECT_CMD_WRITE;

		ret = mcde_dsi_dcs_write(ddev->chnl_state, cmd, data, write_len);

		len_to_write -= write_len;
		data += write_len;

		while ((len_to_write > 0) && (ret == 0)) {

			write_len = len_to_write;
			if (write_len > MCDE_MAX_DSI_DIRECT_CMD_WRITE)
				write_len = MCDE_MAX_DSI_DIRECT_CMD_WRITE;

			ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_GLOBAL_PARAM,
						&globalPara, 1);
			ret = mcde_dsi_dcs_write(ddev->chnl_state, cmd, data, write_len);

			len_to_write-= write_len;
			data += write_len;
			globalPara += write_len;
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


static int s6e63m0_dsi_dcs_write_sequence(struct mcde_display_device *ddev,
							const u8 *p_seq)
{
	int ret = 0;

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = s6e63m0_dsi_dcs_write_command(ddev, p_seq[1],
				(u8 *)&p_seq[2], p_seq[0] - 1);
			p_seq += p_seq[0] + 1;
		}
	}

	return ret;
}

/* used to write DSI/DCS commands when video stream active */
#define MAX_DCS_CMD_ALLOWED	(DSILINK_MAX_DSI_DIRECT_CMD_WRITE - 1)
static int s6e63m0_write_dcs_vid_cmd(struct mcde_display_device *ddev, u8 cmd, u8* p_data, int len)
{
	int retries = 1;
	int len_to_write;
	u8 *data;
	int write_len;
	int ret = 0;
	u8 globalPara = MAX_DCS_CMD_ALLOWED;

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
			if (write_len > MAX_DCS_CMD_ALLOWED)
				write_len = MAX_DCS_CMD_ALLOWED;

			ret = mcde_dsi_dcs_write(ddev->chnl_state,
						DCS_CMD_GLOBAL_PARAM,
						&globalPara, 1);
			dev_vdbg(&ddev->dev, "Send DCS GLOBAL_PARAM, data %d, ret=%d\n",
				globalPara, ret);
			ret = mcde_dsi_dcs_write(ddev->chnl_state, cmd, data, write_len);
			dev_vdbg(&ddev->dev, "Send DCS cmd %x, len %d, ret=%d\n",
				cmd, write_len, ret);

			len_to_write-= write_len;
			data += write_len;
			globalPara += write_len;
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

static int s6e63m0_write_dcs_vid_seq(struct mcde_display_device *ddev,
							const u8 *p_seq)
{
	int ret = 0;

	while ((p_seq[0] != DCS_CMD_SEQ_END) && !ret) {
		if (p_seq[0] == DCS_CMD_SEQ_DELAY_MS) {
			msleep(p_seq[1]);
			p_seq += 2;
		} else {
			ret = s6e63m0_write_dcs_vid_cmd(ddev, p_seq[1],
				(u8 *)&p_seq[2], p_seq[0] - 1);
			p_seq += p_seq[0] + 1;
		}
	}

	return ret;
}

static int s6e63m0_dsi_read_panel_id(struct s6e63m0_dsi_lcd *lcd)
{
	int readret = 0;
	int len = 1;

	if (lcd->lcd_id[0] != 0xFE) {
		dev_dbg(&lcd->ddev->dev, "%s: Read device id of the display\n", __func__);

		readret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID1,
						(u32 *)&lcd->lcd_id[0], &len);
		if (!readret)
			readret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID2,
						(u32 *)&lcd->lcd_id[1], &len);
		if (!readret)
			readret = mcde_dsi_dcs_read(lcd->ddev->chnl_state, DCS_CMD_READ_ID3,
						(u32 *)&lcd->lcd_id[2], &len);
		if (readret)
			dev_info(&lcd->ddev->dev,
				"mcde_dsi_dcs_read failed to read display ID\n");

		switch (lcd->lcd_id[1]) {

			case ID_VALUE_M2:
				dev_info(lcd->dev, "Panel is AMS397GE MIPI M2\n");
				lcd->elvss_pulse = lcd->lcd_id[2];
				lcd->elvss_offsets = stod13cm_elvss_offsets;
				break;

			case ID_VALUE_SM2:
			case ID_VALUE_SM2_1:
				dev_info(lcd->dev, "Panel is AMS397GE MIPI SM2\n");
				lcd->elvss_pulse = lcd->lcd_id[2];
				lcd->elvss_offsets = stod13cm_elvss_offsets;
				break;

			default:
				dev_err(lcd->dev, "panel type not recognised (panel_id = %x, %x, %x)\n",
					lcd->lcd_id[0], lcd->lcd_id[1], lcd->lcd_id[2]);
				lcd->elvss_pulse = 0x16;
				lcd->elvss_offsets = stod13cm_elvss_offsets;
				
//				readret = -EINVAL;
				break;
		}
	}
	return 0;
}


static int get_gamma_value_from_bl(int brightness)
{
	int backlightlevel;

	/* brightness setting from platform is from 0 to 255
	 * But in this driver, brightness is
	  only supported from 0 to 24 */

	switch (brightness) {
	case 0 ... 29:
		backlightlevel = GAMMA_30CD;
		break;
	case 30 ... 39:
		backlightlevel = GAMMA_40CD;
		break;
	case 40 ... 49:
		backlightlevel = GAMMA_50CD;
		break;
	case 50 ... 59:
		backlightlevel = GAMMA_60CD;
		break;
	case 60 ... 69:
		backlightlevel = GAMMA_70CD;
		break;
	case 70 ... 79:
		backlightlevel = GAMMA_80CD;
		break;
	case 80 ... 89:
		backlightlevel = GAMMA_90CD;
		break;
	case 90 ... 99:
		backlightlevel = GAMMA_100CD;
		break;
	case 100 ... 109:
		backlightlevel = GAMMA_110CD;
		break;
	case 110 ... 119:
		backlightlevel = GAMMA_120CD;
		break;
	case 120 ... 129:
		backlightlevel = GAMMA_130CD;
		break;
	case 130 ... 139:
		backlightlevel = GAMMA_140CD;
		break;
	case 140 ... 149:
		backlightlevel = GAMMA_150CD;
		break;
	case 150 ... 159:
		backlightlevel = GAMMA_160CD;
		break;
	case 160 ... 169:
		backlightlevel = GAMMA_170CD;
		break;
	case 170 ... 179:
		backlightlevel = GAMMA_180CD;
		break;
	case 180 ... 189:
		backlightlevel = GAMMA_190CD;
		break;
	case 190 ... 199:
		backlightlevel = GAMMA_200CD;
		break;
	case 200 ... 209:
		backlightlevel = GAMMA_210CD;
		break;
	case 210 ... 219:
		backlightlevel = GAMMA_220CD;
		break;
	case 220 ... 229:
		backlightlevel = GAMMA_230CD;
		break;
	case 230 ... 245:
		backlightlevel = GAMMA_240CD;
		break;
	case 246 ... 254:
		backlightlevel = GAMMA_250CD;
		break;
	case 255:
		backlightlevel = GAMMA_300CD;
		break;

	default:
		backlightlevel = DEFAULT_GAMMA_LEVEL;
		break;
        }
	return backlightlevel;

}

static int s6e63m0_set_gamma(struct s6e63m0_dsi_lcd *lcd)
{
	int ret = 0;
	int i=0;
	struct mcde_display_device *ddev = lcd->ddev;
	
	ret = s6e63m0_write_dcs_vid_seq(ddev, lcd->gamma_seq[lcd->bl]);
#if 0
	for (i=3; i<24 ; i++)
	printk("gamma_seq[%d][%x]...current_gamma=[%d]\n",i,lcd->gamma_seq[lcd->bl][i],lcd->current_gamma);
#endif
	if (ret == 0)
		ret = s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_GAMMA_SET_UPDATE);
	
	return ret;
}

static int s6e63m0_set_acl(struct s6e63m0_dsi_lcd *lcd)
{
	int ret = 0;
	struct mcde_display_device *ddev = lcd->ddev;

	if (lcd->acl_enable) {
		#if 0
		if (lcd->cur_acl == 0) {
			if (lcd->bl == 0 || lcd->bl == 1 || lcd->bl == 2) {
				s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_OFF_DSI);
				dev_dbg(lcd->dev, "cur_acl=%d, acl_off..lcd->bl [%d]\n", lcd->cur_acl,lcd->bl);
			} else {
				s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_ON_DSI);
				dev_dbg(lcd->dev, "cur_acl=%d, acl_on ..lcd->bl [%d]\n",lcd->cur_acl,lcd->bl);
			}
		}
		#endif		
		dev_dbg(lcd->dev,"current lcd-> bl [%d]\n",lcd->bl);
		switch (lcd->bl) {
		case 0 ... 2: /* 30cd ~ 60cd */
			if (lcd->cur_acl != 0) {
				ret = s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_NULL_DSI);
				dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : off!!\n");
				lcd->cur_acl = 0;
			}
			break;
		case 3 ... 24: /* 70cd ~ 250 */
			if (lcd->cur_acl != 40) {
				ret |= s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_40P_DSI);
				dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : 40!!\n");
				lcd->cur_acl = 40;
			}
			break;
			
		default:
			if (lcd->cur_acl != 40) {
				ret |= s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_40P_DSI);
				dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : 40!!\n");
				lcd->cur_acl = 40;
			}
				dev_dbg(lcd->dev, " cur_acl=%d\n", lcd->cur_acl);
			break;
		}
	} else {
			ret = s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_NULL_DSI);
			lcd->cur_acl = 0;
			dev_dbg(lcd->dev, "ACL_cutoff_set Percentage : off!!\n");
	}

	if (ret) {
		dev_err(lcd->dev, "failed to initialize ldi.\n");
		return -EIO;
	}

	return ret;
	}

static int s6e63m0_set_elvss(struct s6e63m0_dsi_lcd *lcd)
{
	u8 elvss_val;
	int gamma_index;
	int ret = 0;

	enum elvss_brightness elvss_setting;
	struct mcde_display_device *ddev = lcd->ddev;

		if (lcd->current_gamma < 110)
			elvss_setting = elvss_30cd_to_100cd;
		else if (lcd->current_gamma < 170)
			elvss_setting = elvss_110cd_to_160cd;
		else if (lcd->current_gamma < 210)
			elvss_setting = elvss_170cd_to_200cd;
		else
			elvss_setting = elvss_210cd_to_300cd;

		if (elvss_setting != lcd->elvss_brightness) {

			if (lcd->elvss_offsets)
				elvss_val = lcd->elvss_pulse +
						lcd->elvss_offsets[elvss_setting];
			else
				elvss_val = 0x16;

			if (elvss_val > 0x1F)
				elvss_val = 0x1F;	/* max for STOD13CM */

			dev_dbg(&ddev->dev, "ELVSS setting to %x\n", elvss_val);
			lcd->elvss_brightness = elvss_setting;
			for (gamma_index = ELVSS_SET_START_IDX;
				gamma_index <= ELVSS_SET_END_IDX; gamma_index++)

				DCS_CMD_SEQ_ELVSS_SET[gamma_index] = elvss_val;

			ret = s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_ELVSS_SET);
		}
	return ret;
}
static int s6e63m0_dsi_update_brightness(struct mcde_display_device *ddev,
						int brightness)
{
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
	int ret = 0;
	int gamma = 0;
	int i =0;

	/* Defensive - to prevent table overrun. */
	if (brightness > MAX_BRIGHTNESS)
		brightness = MAX_BRIGHTNESS;

	lcd->bl = get_gamma_value_from_bl(brightness);
	gamma = candela_table[lcd->bl];
	#ifndef SMART_DIMMING
	lcd->gamma_seq = gamma_table_sm2;
	#endif
	dev_dbg(&ddev->dev,"backlight level = [%d]...brightness=[%d]\n",lcd->bl,brightness);

	if (gamma != lcd->current_gamma) {
		
		lcd->current_gamma = gamma;
		
		s6e63m0_set_gamma(lcd);
		s6e63m0_set_acl(lcd);
		s6e63m0_set_elvss(lcd);

		dev_dbg(&ddev->dev, "Update Brightness: gamma=%d\n", gamma);
	}

	return ret;
}

static int s6e63m0_dsi_get_brightness(struct backlight_device *bd)
{
	dev_dbg(&bd->dev, "lcd get brightness returns %d\n", bd->props.brightness);
	return bd->props.brightness;
}

static int s6e63m0_dsi_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	int brightness = bd->props.brightness;

	struct s6e63m0_dsi_lcd *lcd = bl_get_data(bd);

	/*Protection code for  power on /off test */
	if(lcd->ddev <= 0)
		return ret;

	if ((brightness < 0) ||	(brightness > bd->props.max_brightness)) {
		dev_err(&bd->dev, "lcd brightness should be 0 to %d.\n",
			bd->props.max_brightness);
		return -EINVAL;
	}


	if (lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) {

		mutex_lock(&lcd->lock);
		if ((brightness == 0) && (lcd->current_brightness != 0)) {
			ret = s6e63m0_dsi_display_sleep(lcd);
		}

		if ((brightness != 0) && (lcd->current_brightness == 0)) {
			ret = s6e63m0_dsi_display_exit_sleep(lcd);
		}

		if (!ret)
		ret = s6e63m0_dsi_update_brightness(lcd->ddev, brightness);

		if (ret) {
			dev_info(&bd->dev, "lcd brightness setting failed.\n");
			ret = 0;
		}
		mutex_unlock(&lcd->lock);		
	}

	lcd->current_brightness = brightness;


	return ret;
}


static struct backlight_ops s6e63m0_dsi_backlight_ops  = {
	.get_brightness = s6e63m0_dsi_get_brightness,
	.update_status = s6e63m0_dsi_set_brightness,
};

struct backlight_properties s6e63m0_dsi_backlight_props = {
	.brightness = DEFAULT_BRIGHTNESS,
	.max_brightness = MAX_BRIGHTNESS,
	.type = BACKLIGHT_RAW,
};

static int s6e63m0_dsi_power_on(struct s6e63m0_dsi_lcd *lcd)
{
	struct sec_dsi_platform_data *pd = lcd->pd;

	dev_info(lcd->dev, "%s: Power on S6E63M0 display\n", __func__);

	if (pd->lcd_pwr_onoff) {
		pd->lcd_pwr_onoff(true);
		msleep(25);
	}

	if (pd->reset_gpio) {
		gpio_set_value(pd->reset_gpio, 0);
		mdelay(5);
		gpio_set_value(pd->reset_gpio, 1);
		msleep(10);
	}
	return 0;
}

static int s6e63m0_dsi_power_off(struct s6e63m0_dsi_lcd *lcd)
{
	struct sec_dsi_platform_data *pd = lcd->pd;
	int ret = 0;

	dev_info(lcd->dev, "%s: Power off display\n", __func__);

	if (pd->reset_gpio)
		gpio_set_value(pd->reset_gpio, 0);

	if (pd->lcd_pwr_onoff)
		pd->lcd_pwr_onoff(false);

	lcd->panel_awake = false;

	return ret;
}

static int s6e63m0_dsi_display_init(struct s6e63m0_dsi_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_info(lcd->dev, "%s: Initialise S6E63M0 display\n", __func__);

	lcd->current_gamma = GAMMA_INDEX_NOT_SET;
	lcd->elvss_brightness = elvss_not_set;

	mcde_formatter_enable(ddev->chnl_state);	/* ensure MCDE enabled */
	ret |= s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_L2_MTP_KEY_ENABLE);
	ret |= s6e63m0_write_dcs_vid_seq(ddev, SEQ_ACL_ON_DSI);	
	ret |= s6e63m0_dsi_read_panel_id(lcd);
	ret |= s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_PANEL_COND_SET);
	ret |= s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_DISPLAY_COND_SET);
	ret |= s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_ETC_COND_SET);

	ret |= s6e63m0_dsi_update_brightness(ddev, lcd->bd->props.brightness);
	ret |= s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_ELVSS_ON);

	return 0;/*For PBA test on assembly line*/
}

static int s6e63m0_dsi_display_sleep(struct s6e63m0_dsi_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_dbg(lcd->dev, "%s: display sleep\n", __func__);

	if (lcd->panel_awake) {
		ret = s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_ENTER_SLEEP);

		if (!ret)
			lcd->panel_awake = false;
	}
	return ret;
}

static int s6e63m0_dsi_display_exit_sleep(struct s6e63m0_dsi_lcd *lcd)
{
	struct mcde_display_device *ddev = lcd->ddev;
	int ret = 0;

	dev_dbg(lcd->dev, "%s: S6E63M0 display exit sleep\n", __func__);

	if (!lcd->panel_awake) {

		ret = s6e63m0_write_dcs_vid_seq(ddev, DCS_CMD_SEQ_EXIT_SLEEP);

		if (!ret)
			lcd->panel_awake = true;
	}
	return ret;
}


static int s6e63m0_dsi_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	static int notFirstTime;
	int ret = 0;
	enum mcde_display_rotation final;
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
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
				ret = s6e63m0_dsi_dcs_write_sequence(ddev,
						DCS_CMD_SEQ_ORIENTATION_180);
				lcd->rotation = final;
			}
		} else if (final == MCDE_DISPLAY_ROT_0) {
			if (final != lcd->rotation) {
				ret = s6e63m0_dsi_dcs_write_sequence(ddev,
						DCS_CMD_SEQ_ORIENTATION_DEFAULT);
				lcd->rotation = final;
			}
			(void)mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		} else {
			ret = mcde_chnl_set_rotation(ddev->chnl_state, final_hw_rot);
		}
		if (ret)
			return ret;
		dev_dbg(lcd->dev, " %s Display rotated %d\n", __func__, final);
	}
        /* +452052 ESD test 2 */
	lcd->pre_rotation = final_hw_rot;
        /* -452052 ESD test 2 */

	ddev->rotation = rotation;
	/* avoid disrupting splash screen by changing update_flags */
	if (notFirstTime || (final != MCDE_DISPLAY_ROT_0)) {
		notFirstTime = 1;
		ddev->update_flags |= UPDATE_FLAG_ROTATION;
	}
	return 0;
}


static int s6e63m0_dsi_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode)
{
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
	enum mcde_display_power_mode orig_mode = ddev->power_mode;
	int ret = 0;

	mutex_lock(&lcd->lock);

	/* OFF -> STANDBY or OFF -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {
#ifdef ESD_OPERATION
		if (lcd->lcd_connected)
			enable_irq(GPIO_TO_IRQ(lcd->esd_port));
#endif

		ret = s6e63m0_dsi_power_on(lcd);
		if (ret)
			goto err;

		ret = s6e63m0_dsi_display_init(lcd);
		if (ret)
			goto err;
#ifdef ESD_OPERATION
		if (lcd->lcd_connected) {
			irq_set_irq_type(GPIO_TO_IRQ(lcd->esd_port), IRQF_TRIGGER_RISING);
			lcd->esd_enable = 1;
			dev_dbg(lcd->dev, "change lcd->esd_enable :%d\n",lcd->esd_enable);
		} else
			dev_dbg(lcd->dev, "lcd_connected : %d\n", lcd->lcd_connected);
#endif

		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_ON) {

		if (lcd->justStarted) {
			lcd->justStarted = false;
			lcd->esd_enable = 1;			
			mcde_chnl_disable(ddev->chnl_state);
			if (lcd->pd->reset_gpio) {
				gpio_set_value(lcd->pd->reset_gpio, 0);
				msleep(2);
				gpio_set_value(lcd->pd->reset_gpio, 1);
				msleep(10);
			}
			ret = s6e63m0_dsi_display_init(lcd);
		}
		
		ret = s6e63m0_dsi_display_exit_sleep(lcd);
		if (ret)
			goto err;

		if ((!lcd->opp_is_requested) && (lcd->pd->min_ddr_opp > 0)) {
			if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
							S6E63M0_DRIVER_NAME,
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

		ret = s6e63m0_dsi_display_sleep(lcd);
		if (ret && (power_mode != MCDE_DISPLAY_PM_OFF))
			goto err;

		if (lcd->opp_is_requested) {
			prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP, S6E63M0_DRIVER_NAME);
			lcd->opp_is_requested = false;
			dev_dbg(lcd->dev, "DDR OPP removed\n");
		}
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_OFF) {
#ifdef ESD_OPERATION
		if (lcd->esd_enable) {
	                printk(KERN_INFO " %s esd_enable=1", __func__);
			lcd->esd_enable = 0;
			irq_set_irq_type(GPIO_TO_IRQ(lcd->esd_port), IRQF_TRIGGER_NONE);
			disable_irq_nosync(GPIO_TO_IRQ(lcd->esd_port));

			if (!list_empty(&(lcd->esd_work.entry))) {
				cancel_work_sync(&(lcd->esd_work));
				dev_dbg(lcd->dev," cancel_work_sync\n");
			}
	
                        printk(KERN_INFO " %s change esd_enable=0", __func__);
			dev_dbg(lcd->dev,"change lcd->esd_enable :%d\n", lcd->esd_enable);
		}
#endif		
		ret = s6e63m0_dsi_power_off(lcd);
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
static int s6e63m0_dsi_try_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = 0;
	int bpp;
	int freq_hs_clk;
	static u32 bit_hs_clk = 0;
	u32 pclk;
	
	dev_dbg(&ddev->dev, " %s \n",__func__);
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
		pclk /= video_mode->xres + video_mode->xres_padding + video_mode->hsw + video_mode->hbp +
								video_mode->hfp;
		pclk *= 1000;
		pclk /= video_mode->yres + video_mode->yres_padding + video_mode->vsw + video_mode->vbp +
								video_mode->vfp;
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


static int s6e63m0_dsi_set_video_mode(struct mcde_display_device *ddev,
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
	dev_dbg(&ddev->dev, " %s rot=%d,xres=%d,yres=%d\n", __func__, ddev->rotation, video_mode->xres, video_mode->yres);
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
static int s6e63m0_dsi_display_update(struct mcde_display_device *ddev,
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

static int s6e63m0_dsi_apply_config(struct mcde_display_device *ddev)
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


#ifdef CONFIG_LCD_CLASS_DEVICE

static int s6e63m0_dsi_power(struct s6e63m0_dsi_lcd *lcd, int power)
{
	int ret = 0;

	switch (power) {
	case FB_BLANK_POWERDOWN:
		dev_dbg(lcd->dev, "%s(): Powering Off, was %s\n",__func__,
			(lcd->ddev->power_mode != MCDE_DISPLAY_PM_OFF) ? "ON" : "OFF");
		ret = s6e63m0_dsi_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_OFF);
		break;
	case FB_BLANK_NORMAL:
		dev_dbg(lcd->dev, "%s(): Into Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_ON) ? "ON" : "SLEEP/OFF");
		ret = s6e63m0_dsi_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_STANDBY);
		break;
	case FB_BLANK_UNBLANK:
		dev_dbg(lcd->dev, "%s(): Exit Sleep, was %s\n",__func__,
			(lcd->ddev->power_mode == MCDE_DISPLAY_PM_STANDBY) ? "SLEEP" : "ON/OFF");
		ret = s6e63m0_dsi_set_power_mode(lcd->ddev, MCDE_DISPLAY_PM_ON);
		break;
	default:
		ret = -EINVAL;
		dev_info(lcd->dev, "Invalid power change request (%d)\n", power);
		break;
	}

	return ret;
}

static int s6e63m0_dsi_set_power(struct lcd_device *ld, int power)
{
	struct s6e63m0_dsi_lcd *lcd = lcd_get_data(ld);

	dev_dbg(lcd->dev, "%s: power=%d\n", __func__, power);

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
		power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	return s6e63m0_dsi_power(lcd, power);
}

static int s6e63m0_dsi_get_power(struct lcd_device *ld)
{
	struct s6e63m0_dsi_lcd *lcd = lcd_get_data(ld);
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

static struct lcd_ops s6e63m0_dsi_lcd_ops = {
	.set_power = s6e63m0_dsi_set_power,
	.get_power = s6e63m0_dsi_get_power,
};

#endif // CONFIG_LCD_CLASS_DEVICE

/* +452052  ESD test 2 */
int mcde_rotate_to_fb(enum mcde_hw_rotation mcde_rot)
{
    switch (mcde_rot) {
    case MCDE_HW_ROT_0:
        return FB_ROTATE_UR;
    case MCDE_HW_ROT_90_CCW:
        return FB_ROTATE_CCW;
    case MCDE_HW_ROT_90_CW:
        return FB_ROTATE_CW;
    case MCDE_HW_ROT_VERT_MIRROR:
        return FB_ROTATE_UD;
    default:
        pr_info("%s: Illegal degree supplied", __func__);
        return FB_ROTATE_UR;
    }
}
/* -452052  ESD test 2 */


#ifdef ESD_OPERATION
#if 0
static unsigned int fb_rotate_to_compdev(__u32 fb_rotate)
{
    switch (fb_rotate) {
    case FB_ROTATE_UR:
        return COMPDEV_TRANSFORM_ROT_0;
    case FB_ROTATE_CW:
        return COMPDEV_TRANSFORM_ROT_90_CW;
    case FB_ROTATE_UD:
        return COMPDEV_TRANSFORM_ROT_180;
    case FB_ROTATE_CCW:
        return COMPDEV_TRANSFORM_ROT_90_CCW;
    default:
        ALOGE("%s: Illegal fb rotation supplied", __func__);
        return 0;
    }
}

static void mcde_send_to_compdev(int compdev, struct fb_fix_screeninfo finfo, struct fb_var_screeninfo vinfo, int fb_rot)
{
    struct fb_fix_screeninfo *lcd_finfo = finfo;
    struct fb_var_screeninfo *lcd_vinfo = vinfo;
    int i;
    int ret;
    struct compdev_img img;

    memset(&img, 0, sizeof(img));

    /* lcd_vinfo->yoffset contains the LCD actual visible buffer */
        img.buf.offset = lcd_finfo->smem_start +
            lcd_finfo->line_length * lcd_vinfo->yoffset;

    /* Input */
    switch (lcd_vinfo->bits_per_pixel) {
    case 16:
        img.fmt = COMPDEV_FMT_RGB565;
        break;
    case 24:
        img.fmt = COMPDEV_FMT_RGB888;
        break;
    case 32:
    default:
        img.fmt = COMPDEV_FMT_RGBA8888;
        break;
    }
    
    img.width = lcd_vinfo->xres;
    img.height = lcd_vinfo->yres;
    img.pitch = lcd_finfo->line_length;
    img.buf.type = COMPDEV_PTR_PHYSICAL;
    img.buf.len = lcd_finfo->line_length * lcd_vinfo->yres;
    img.src_rect.x = 0;
    img.src_rect.y = 0;
    img.src_rect.width = lcd_vinfo->xres;
    img.src_rect.height = lcd_vinfo->yres;

    img.dst_rect.x = 0;
    img.dst_rect.y = 0;
    if (fb_rot == FB_ROTATE_CW ||
        fb_rot == FB_ROTATE_CCW) {
        img.dst_rect.width = lcd_vinfo->yres;
        img.dst_rect.height = lcd_vinfo->xres;
    } else {
        img.dst_rect.width = lcd_vinfo->xres;
        img.dst_rect.height = lcd_vinfo->yres;
    }

    img.z_position = 1; // HWC can decide to put a buffer either on top(0) or below(2).

    img.flags = (uint32_t)COMPDEV_FRAMEBUFFER_FLAG;

    img.transform = fb_rotate_to_compdev(fb_rot);

    ret = ioctl(ctx->compdev, COMPDEV_POST_BUFFER_IOC,
            (struct compdev_img*)&img);
    if (ret < 0)
        printk("%s: Failed to post buffers to compdev, %d", __func__, ret);

}
#endif

void mcde_recovery_fb(struct s6e63m0_dsi_lcd *lcd)
{
	/* +452052 ESD recovery for DSI video */
    
        int compdev = 0;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct fb_info *fbi;
	struct mcde_fb *mfb;
	extern struct fb_info* get_primary_display_fb_info(void);
        u32 rot;
        int i;

	fbi = get_primary_display_fb_info();
	mfb = to_mcde_fb(fbi);
        printk("mfb =[%x]\n");
	if (mfb->early_suspend.suspend) {
	pr_info("%s MCDE suspend \n", __func__);
	mfb->early_suspend.suspend(&mfb->early_suspend);
	}

	if (mfb->early_suspend.resume) {
	pr_info("%s MCDE resume \n", __func__);
	mfb->early_suspend.resume(&mfb->early_suspend);
	}
#endif
/*
	if (lcd->pre_rotation == MCDE_HW_ROT_90_CW)
        	lcd->ddev->fbi->var.rotate = MCDE_HW_ROT_90_CW-1;
	else if(lcd->pre_rotation == MCDE_HW_ROT_90_CCW)
		lcd->ddev->fbi->var.rotate = MCDE_HW_ROT_90_CCW+2;
*/
#if 0
        compdev = open("/dev/comp0", O_RDWR, 0);
        if (compdev == NULL) {
            printk(KERN_INFO " %s compdev open fail !!\n", __func__);
            return;
        }

#endif
//        rot = mcde_rotate_to_fb(((struct mcde_chnl_state *)lcd->ddev->chnl_state)->hw_rot);

        for(i = 0; i < mfb->num_ovlys; i++) {
            if (mfb->ovlys[i]) {
                int ret = 0; 
                struct mcde_overlay *ovly = mfb->ovlys[i];
                rot = ovly->ddev->get_rotation(ovly->ddev);

                printk(KERN_INFO "%s  ovly nb = %d\n", __func__, i);
                /* Set rotation */
                ret = mcde_dss_set_rotation(ovly->ddev, rot);
                if (ret != 0) {
                    printk(KERN_INFO "%s  mcde_dss_set_rotation failed\n", __func__);                
                }
                
                /* Apply */
                ret = mcde_dss_apply_channel(ovly->ddev);
                if (ret != 0) {
                    printk(KERN_INFO "%s  mcde_dss_apply_channel failed\n", __func__);                
                }               

                ret = mcde_dss_apply_overlay(ovly, NULL);                
                if (ret != 0) {
                    printk(KERN_INFO "%s  mcde_dss_apply_overlay failed\n", __func__);                
                }               

                ret = mcde_dss_update_overlay(ovly, true);
                if (ret != 0) {
                    printk(KERN_INFO "%s  mcde_dss_update_overlay failed\n", __func__);                
                }                
            }
        }

#if 0        
        lcd->ddev->update_flags |= UPDATE_FLAG_ROTATION;
        lcd->ddev->fbi->var.rotate = 0;
        lcd->ddev->fbi->var.activate = FB_ACTIVATE_VBL;
        mcde_fb_check_var(&lcd->ddev->fbi->var, lcd->ddev->fbi);
        mcde_fb_set_par(lcd->ddev->fbi);
#endif       

#if 0
        lcd->ddev->fbi->var.rotate = rot;

    printk(" %s chnl->hw_rot=%d, fb_rot=%d\n",__func__, lcd->ddev->chnl_state->hw_rot, lcd->ddev->fbi->var.rotate);

    lcd->ddev->update_flags |= UPDATE_FLAG_ROTATION;

#if 1    
    lcd->ddev->fbi->var.activate = FB_ACTIVATE_VBL;
    mcde_fb_check_var(&lcd->ddev->fbi->var, lcd->ddev->fbi);
    mcde_fb_set_par(lcd->ddev->fbi);
#endif

#endif

#if 0
    mcde_send_to_compdev(compdev, fbi->fix, fbi->var, rot);  
    close(compdev);
#endif
    printk(" %s Exit\n", __func__);
	/* -452052 ESD recovery for DSI video */

}
static void esd_work_func(struct work_struct *work)
{
	struct s6e63m0_dsi_lcd *lcd = container_of(work,
					struct s6e63m0_dsi_lcd, esd_work);
	struct mcde_display_device *ddev = lcd->ddev;
	
	pm_message_t dummy;
	
	//msleep(100);
        pr_info(" %s after sleep\n", __func__);
       
#ifndef ESD_TEST    
	//if (lcd->esd_enable && gpio_get_value(lcd->esd_port)) {
	if (lcd->esd_enable && !lcd->esd_processing && !battpwroff_charging) {
#endif

        pr_info(" %s lcd->esd_enable:%d start,ESD PORT =[%d]\n", __func__,
			lcd->esd_enable,gpio_get_value(lcd->esd_port));

	        /* +452052 ESD recovery for DSI video */
            lcd->esd_processing = true; 

	        /* -452052 ESD recovery for DSI video */
#if 0
		/*lcd off*/
		s6e63m0_dsi_display_sleep(lcd);		
		s6e63m0_dsi_power_off(lcd);
		
		/*lcd on*/
		s6e63m0_dsi_power_on(lcd);
		s6e63m0_dsi_display_init(lcd);
		s6e63m0_dsi_display_exit_sleep(lcd);
#endif
		mcde_recovery_fb(lcd);

		msleep(100);

                lcd->esd_processing = false; 

		/* low is normal. On PBA esd_port coule be HIGH */
		if (gpio_get_value(lcd->esd_port)) {
			pr_info(" %s esd_work_func re-armed\n", __func__);
//			queue_work(lcd->esd_workqueue, &(lcd->esd_work));
		}
		pr_info(" %s end\n", __func__);		

#ifndef ESD_TEST            
	}
#endif

}

static irqreturn_t esd_interrupt_handler(int irq, void *data)
{
	struct s6e63m0_dsi_lcd *lcd = data;

	dev_dbg(lcd->dev,"lcd->esd_enable :%d\n", lcd->esd_enable);

        /* +452052 ESD recovery for DSI video */
	if (lcd->esd_enable && !lcd->esd_processing && !battpwroff_charging) {
        /* -452052 ESD recovery for DSI video */
		if (list_empty(&(lcd->esd_work.entry)))
			queue_work(lcd->esd_workqueue, &(lcd->esd_work));
		else
			dev_dbg(lcd->dev,"esd_work_func is not empty\n" );
	}

	return IRQ_HANDLED;
}
#endif

static ssize_t power_reduce_show(struct device *dev, struct
device_attribute *attr, char *buf)
{
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(dev);
	char temp[3];
	sprintf(temp, "%d\n", lcd->acl_enable);
	strcpy(buf, temp);
	return strlen(buf);
}
static ssize_t power_reduce_store(struct device *dev, struct
device_attribute *attr, const char *buf, size_t size)
{
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(dev);
	int value;
	int rc;
	
	/*Protection code for  power on /off test */
	if(lcd->ddev <= 0)
		return size;
	
	rc = strict_strtoul(buf, (unsigned int) 0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else{
		dev_info(dev, "acl_set_store - %d, %d\n", lcd->acl_enable, value);
		if (lcd->acl_enable != value) {
			mutex_lock(&lcd->lock);
			lcd->acl_enable = value;
			if (lcd->panel_awake)
				s6e63m0_set_acl(lcd);
			mutex_unlock(&lcd->lock);			
		}
		return size;
	}
}
static DEVICE_ATTR(power_reduce, 0664,
		power_reduce_show, power_reduce_store);

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[20];
	sprintf(temp, "SMD_AMS397GEXX\n");
	strcat(buf, temp);
	return strlen(buf);
}
static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);

static int __devinit s6e63m0_dsi_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct sec_dsi_platform_data *pdata = ddev->dev.platform_data;
	struct s6e63m0_dsi_lcd *lcd = NULL;
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

	ddev->set_power_mode = s6e63m0_dsi_set_power_mode;
	ddev->set_rotation = s6e63m0_dsi_set_rotation;
	ddev->try_video_mode = s6e63m0_dsi_try_video_mode;
	ddev->set_video_mode = s6e63m0_dsi_set_video_mode;
	ddev->update = s6e63m0_dsi_display_update;
	ddev->apply_config = s6e63m0_dsi_apply_config;

	ddev->native_x_res = VMODE_XRES;
	ddev->native_y_res = VMODE_YRES;

	lcd = kzalloc(sizeof(struct s6e63m0_dsi_lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

#ifdef CONFIG_LCD_CLASS_DEVICE
	lcd->ld = lcd_device_register("panel", &ddev->dev,
					lcd, &s6e63m0_dsi_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		ret = PTR_ERR(lcd->ld);
		dev_err(&ddev->dev, "%s: Failed to register s6e63m0_dsi display device\n", __func__);
		goto out_free_lcd;
	}
#endif

	lcd->bd = backlight_device_register("panel",
					&ddev->dev,
					lcd,
					&s6e63m0_dsi_backlight_ops,
					&s6e63m0_dsi_backlight_props);
	if (IS_ERR(lcd->bd)) {
		ret =  PTR_ERR(lcd->bd);
		goto backlight_device_register_failed;
	}
	
	lcd->ddev = ddev;
	lcd->dev = &ddev->dev;
	lcd->pd = pdata;
	lcd->opp_is_requested = false;
	lcd->justStarted = true;
	lcd->rotation = MCDE_DISPLAY_ROT_0;
	lcd->bl = DEFAULT_GAMMA_LEVEL;
	lcd->acl_enable = true;
	lcd->cur_acl = 0;
	lcd->panel_awake = false;
	lcd->lcd_connected = 1;

	dev_set_drvdata(&ddev->dev, lcd);
	mutex_init(&lcd->lock);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_power_reduce);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n",
					__LINE__);

	ret = device_create_file(&lcd->ld->dev, &dev_attr_lcd_type);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add sysfs entries, %d\n",
					__LINE__);


	if (pdata->mtpAvail) {
		memcpy(lcd->mtpData, pdata->mtpData, SEC_DSI_MTP_DATA_LEN);

#ifdef SMART_DIMMING
		memcpy(lcd->smart.panelid, pdata->lcdId, 3);
		init_table_info_22(&lcd->smart);
		calc_voltage_table(&lcd->smart, pdata->mtpData);
		s6e63m0_init_smart_dimming_table_22(lcd);
		
		lcd->gamma_seq = (const u8 **)gamma_table_sm2;
#endif
		memcpy(lcd->lcd_id, pdata->lcdId, 3);
		switch (lcd->lcd_id[1]) {

			case ID_VALUE_M2:
				dev_info(lcd->dev, "Panel is AMS397GE MIPI M2\n");
				lcd->elvss_pulse = lcd->lcd_id[2];
				lcd->elvss_offsets = stod13cm_elvss_offsets;
				break;

			case ID_VALUE_SM2:
			case ID_VALUE_SM2_1:
				dev_info(lcd->dev, "Panel is AMS397GE MIPI SM2\n");
				lcd->elvss_pulse = lcd->lcd_id[2];
				lcd->elvss_offsets = stod13cm_elvss_offsets;
				break;

			default:
				dev_err(lcd->dev, "panel type not recognised (panel_id = %x, %x, %x)\n",
					lcd->lcd_id[0], lcd->lcd_id[1], lcd->lcd_id[2]);
				lcd->elvss_pulse = 0x16;
				lcd->elvss_offsets = stod13cm_elvss_offsets;				
				break;
		}
	}
#ifdef ESD_OPERATION
		lcd->esd_workqueue = create_singlethread_workqueue("esd_workqueue");

		if (!lcd->esd_workqueue) {
			dev_info(lcd->dev, "esd_workqueue create fail\n");
			return 0;
		}
	
		INIT_WORK(&(lcd->esd_work), esd_work_func);

		lcd->esd_port = ESD_PORT_NUM;

		if (request_threaded_irq(GPIO_TO_IRQ(lcd->esd_port), NULL,
		esd_interrupt_handler, IRQF_TRIGGER_RISING, "esd_interrupt", lcd)) {
				dev_info(lcd->dev, "esd irq request fail\n");
				free_irq(GPIO_TO_IRQ(lcd->esd_port), NULL);
				lcd->lcd_connected = 0;
			}
        
                lcd->esd_processing = false; 
#ifdef ESD_TEST
            pdsi = lcd;
            setup_timer(&lcd->esd_test_timer, est_test_timer_func, 0);
            mod_timer(&lcd->esd_test_timer,  jiffies + (30*HZ));
#endif
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
        lcd->earlysuspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
        lcd->earlysuspend.suspend = s6e63m0_dsi_early_suspend;
        lcd->earlysuspend.resume  = s6e63m0_dsi_late_resume;
        register_early_suspend(&lcd->earlysuspend);
#endif
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

static int __devexit s6e63m0_dsi_remove(struct mcde_display_device *ddev)
{
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);
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
static int s6e63m0_dsi_resume(struct mcde_display_device *ddev)
{
	int ret;

        pr_info(" %s function entered\n", __func__);
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
        pr_info(" %s function exit\n", __func__);
	return ret;
}

static int s6e63m0_dsi_suspend(struct mcde_display_device *ddev, \
							pm_message_t state)
{
	int ret;

        pr_info(" %s function entered\n", __func__);
	dev_dbg(&ddev->dev, "%s function entered\n", __func__);

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);
        pr_info(" %s function exit\n", __func__);
	return ret;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void s6e63m0_dsi_early_suspend(
		struct early_suspend *earlysuspend)
{
    int ret;
    struct s6e63m0_dsi_lcd *lcd = container_of(earlysuspend,
						struct s6e63m0_dsi_lcd,
						earlysuspend);
    pm_message_t dummy;

    dev_dbg(&lcd->ddev->dev, "%s function entered\n", __func__);
    s6e63m0_dsi_suspend(lcd->ddev, dummy);
}

static void s6e63m0_dsi_late_resume(
		struct early_suspend *earlysuspend)
{
    struct s6e63m0_dsi_lcd *lcd = container_of(earlysuspend,
						struct s6e63m0_dsi_lcd,
						earlysuspend);

    dev_dbg(&lcd->ddev->dev, "%s function entered\n", __func__);
    s6e63m0_dsi_resume(lcd->ddev);
}
#endif

/* Power down all displays on reboot, poweroff or halt. */
static void s6e63m0_dsi_shutdown(struct mcde_display_device *ddev)
{
	struct s6e63m0_dsi_lcd *lcd = dev_get_drvdata(&ddev->dev);

	dev_info(&ddev->dev, "%s\n", __func__);
	mutex_lock(&ddev->display_lock);
	s6e63m0_dsi_power(lcd, FB_BLANK_POWERDOWN);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&lcd->earlysuspend);
#endif	
	kfree(lcd);
	mutex_unlock(&ddev->display_lock);
	dev_info(&ddev->dev, "end %s\n", __func__);
	
};

static struct mcde_display_driver s6e63m0_dsi_driver = {
	.probe	= s6e63m0_dsi_probe,
	.remove = s6e63m0_dsi_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = s6e63m0_dsi_suspend,
	.resume = s6e63m0_dsi_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.shutdown = s6e63m0_dsi_shutdown,

	.driver = {
		.name	= S6E63M0_DRIVER_NAME,
	},
};

/* Module init */
static int __init mcde_display_s6e63m0_dsi_init(void)
{
	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&s6e63m0_dsi_driver);
}
module_init(mcde_display_s6e63m0_dsi_init);

static void __exit mcde_display_s6e63m0_dsi_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&s6e63m0_dsi_driver);
}
module_exit(mcde_display_s6e63m0_dsi_exit);

MODULE_AUTHOR("Gareth Phillips <gareth.phillips@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung MCDE S6E63M0 DSI display driver");
