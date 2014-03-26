/*
 * projector.c  --  projector module driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Inbum Choi <inbum.choi@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/earlysuspend.h>
#include <linux/mutex.h>
#include <mach/gpio.h>
#include <mach/board-sec-u8500.h>
#include <linux/device.h>
#include "projector.h"
#include "PRJ_Data.h"

#ifndef	GPIO_LEVEL_LOW
#define GPIO_LEVEL_LOW		0
#define GPIO_LEVEL_HIGH		1
#endif

/*
#define LED_COMPENSATION
#define PROJECTOR_DEBUG
*/
#define R_compensation	0
#define G_compensation	0
#define B_compensation	0

#define DPPDATAFLASH(ARRAY) (dpp_flash(ARRAY, sizeof(ARRAY)/sizeof(ARRAY[0])))
#define GENRGBSETTING(array, data) (array[6] = (data))

/* MOTOR_DRV should be set before use it */
static unsigned int motor_drv1;
static unsigned int motor_drv2;
static unsigned int motor_drv3;
static unsigned int motor_drv4;

static int motor_step;
static int motor_abs_step;
static int once_motor_verified;

static int verify_value;

static char step_motor_cw[] = {0x0A, 0x06, 0x05, 0x09};

#define MAX_MOTOR_STEP 60
#define MOTOR_MAX_PHASE ((sizeof(step_motor_cw)/sizeof(char)) - 1)
#define MOTOR_PHASE_CW_OUT(x)	do { \
	gpio_direction_output(motor_drv1, (step_motor_cw[(x)]&0x08)>>3);\
	gpio_direction_output(motor_drv2, (step_motor_cw[(x)]&0x04)>>2);\
	gpio_direction_output(motor_drv3, (step_motor_cw[(x)]&0x02)>>1);\
	gpio_direction_output(motor_drv4, step_motor_cw[(x)]&0x01);\
	msleep(20);\
	gpio_direction_output(motor_drv1, GPIO_LEVEL_LOW);\
	gpio_direction_output(motor_drv2, GPIO_LEVEL_LOW);\
	gpio_direction_output(motor_drv3, GPIO_LEVEL_LOW);\
	gpio_direction_output(motor_drv4, GPIO_LEVEL_LOW);\
	usleep_range(4900, 5000);\
} while (0);

#define MOTOR_PHASE_CW_FIRST(x)	do { \
	gpio_direction_output(motor_drv1, (step_motor_cw[(x)]&0x08)>>3);\
	gpio_direction_output(motor_drv2, (step_motor_cw[(x)]&0x04)>>2);\
	gpio_direction_output(motor_drv3, (step_motor_cw[(x)]&0x02)>>1);\
	gpio_direction_output(motor_drv4, step_motor_cw[(x)]&0x01);\
	msleep(30);\
	gpio_direction_output(motor_drv1, GPIO_LEVEL_LOW);\
	gpio_direction_output(motor_drv2, GPIO_LEVEL_LOW);\
	gpio_direction_output(motor_drv3, GPIO_LEVEL_LOW);\
	gpio_direction_output(motor_drv4, GPIO_LEVEL_LOW);\
	usleep_range(4900, 5000);\
} while (0);

static int brightness = BRIGHT_HIGH;

struct workqueue_struct *projector_work_queue;
struct workqueue_struct *stepmotor_work_queue;
struct work_struct projector_work_power_on;
struct work_struct projector_work_power_off;
struct work_struct projector_work_motor_cw;
struct work_struct projector_work_motor_ccw;
struct work_struct projector_work_testmode_on;
struct work_struct projector_work_rotate_screen;

struct device *sec_projector;
extern struct class *sec_class;

static int screen_direction;

static int status;
static int saved_pattern = -1;

static unsigned int not_calibrated = 10;
static unsigned int old_fw = 20;
unsigned char RGB_BUF[MAX_LENGTH];
unsigned int max_dac[3];
static unsigned char seq_number;

volatile unsigned char flash_rgb_level_data[3][3][MAX_LENGTH] = {0,};

struct projector_dpp2601_info {
	struct i2c_client			*client;
	struct projector_dpp2601_platform_data	*pdata;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend			earlysuspend;
#endif
};

struct projector_dpp2601_info *info = NULL;


int dpp_flash(unsigned char *DataSetArray, int iNumArray)
{
	int i = 0;
	int CurrentDataIndex = 0;
	int Bytes = 0;
	
	for (i = 0; i < iNumArray; i = CurrentDataIndex + Bytes) {
		msleep(1);		//temp 091201 , to prevent from abnormal operation like nosie screen
		Bytes =  DataSetArray[i + OFFSET_I2C_NUM_BYTES];
		CurrentDataIndex = i + OFFSET_I2C_DATA_START;

		if (DataSetArray[i + OFFSET_I2C_DIRECTION] == PRJ_WRITE) {
			i2c_master_send(info->client, &DataSetArray[CurrentDataIndex], Bytes);
#ifdef PROJECTOR_DEBUG
			printk("[%s] WRITE addr:", __func__);
			for(i=0;i<Bytes;i++)
				printk("%x ", DataSetArray[CurrentDataIndex+i]);
			printk("\n");
#endif
		} else if (DataSetArray[i + OFFSET_I2C_DIRECTION] == PRJ_READ) {
			memset(RGB_BUF, 0x0, sizeof(RGB_BUF));
			i2c_master_recv(info->client, RGB_BUF, Bytes);
#ifdef PROJECTOR_DEBUG
			printk(KERN_INFO "[%s] READ value:%x %x %x %x\n", __func__,
				RGB_BUF[0], RGB_BUF[1], RGB_BUF[2], RGB_BUF[3]);
#endif
		} else {
			printk(KERN_INFO "[%s] data is invalid !!\n", __func__);
			return -EINVAL;
		}
	}
	return 0;
}

void set_proj_status(int enProjectorStatus)
{
	status = enProjectorStatus;
	printk(KERN_INFO "[%s] projector status : %d\n", __func__, status);
}

int get_proj_status(void)
{
	return status;
}
EXPORT_SYMBOL(get_proj_status);

static void projector_motor_cw_work(struct work_struct *work)
{
	motor_step++;
	if (motor_step > MOTOR_MAX_PHASE)
		motor_step = 0;
 
	MOTOR_PHASE_CW_OUT(motor_step);
	printk(KERN_INFO "[%s] CW:%d\n", __func__, motor_step);
	motor_abs_step--;
}

void projector_motor_cw(void)
{
	queue_work(stepmotor_work_queue, &projector_work_motor_cw);
}

static void projector_motor_ccw_work(struct work_struct *work)
{
	motor_step--;
	if (motor_step < 0)
		motor_step = MOTOR_MAX_PHASE;

	MOTOR_PHASE_CW_OUT(motor_step);
	printk(KERN_INFO "[%s] CCW:%d\n", __func__, motor_step);
	motor_abs_step++;
}

void projector_motor_ccw(void)
{
	queue_work(stepmotor_work_queue, &projector_work_motor_ccw);
}

void set_led_current(int level)
{
	printk(KERN_ERR "[%s] level:%d\n", __func__, level);
	
	int i;

	unsigned char RED_DAC[] = {
		PRJ_WRITE, 5, 0x12, 0x00, 0x00, 0x00, 0x00
	};

	unsigned char GREEN_DAC[] = {
		PRJ_WRITE, 5, 0x13, 0x00, 0x00, 0x00, 0x00
	};

	unsigned char BLUE_DAC[] = {
		PRJ_WRITE, 5, 0x14, 0x00, 0x00, 0x00, 0x00
	};

	unsigned char PLP[] = {
		PRJ_WRITE, 5, 0x38, 0x00, 0x00, 0x00, 0xD3
	};

	for (i = 0; i < MAX_LENGTH; i++) {
		RED_DAC[3 + i] = flash_rgb_level_data[level - 1][0][i];
		GREEN_DAC[3 + i] = flash_rgb_level_data[level - 1][1][i];
		BLUE_DAC[3 + i] = flash_rgb_level_data[level - 1][2][i];
	}

	DPPDATAFLASH(RED_DAC);
	DPPDATAFLASH(GREEN_DAC);
	DPPDATAFLASH(BLUE_DAC);
	DPPDATAFLASH(PLP);
}

void pwron_seq_gpio(void)
{
	gpio_direction_output(info->pdata->gpio_mp_on, GPIO_LEVEL_LOW);
	msleep(120);
	
	gpio_direction_output(info->pdata->gpio_mp_on, GPIO_LEVEL_HIGH);
	msleep(200);

	gpio_direction_output(info->pdata->gpio_parkz, GPIO_LEVEL_HIGH);
	msleep(50);

	gpio_direction_output(info->pdata->gpio_prj_en, GPIO_LEVEL_HIGH);
	msleep(300);
}

void pwron_seq_direction(void)
{
	switch (get_proj_rotation()) {
	case PRJ_ROTATE_0:
		DPPDATAFLASH(Output_Rotate_0);
		break;
	case PRJ_ROTATE_90:
		DPPDATAFLASH(Output_Rotate_90);
		break;
	case PRJ_ROTATE_180:
		DPPDATAFLASH(Output_Rotate_180);
		break;
	case PRJ_ROTATE_270:
		DPPDATAFLASH(Output_Rotate_270);
		break;
	default:
		break;
	};
}

void pwron_seq_source_res(int value)
{
	if (value == LCD_VIEW) {
		DPPDATAFLASH(WVGA_RGB888);
	} else if (value == INTERNAL_PATTERN) {
		DPPDATAFLASH(nHD_RGB888);
	}
}

void pwron_seq_fdr(void)
{
	int cnt;
	unsigned int dac;

	DPPDATAFLASH(InitData_FlashDataLoading);
	msleep(3);

	for (cnt = 0; cnt < 10; cnt++) {
		DPPDATAFLASH(InitData_ReadFlashData);
		if (cnt < 9) {
			dac = 0;
			dac |= RGB_BUF[0] << 24 | RGB_BUF[1] << 16
				| RGB_BUF[2] << 8 |  RGB_BUF[3];

			not_calibrated = (dac < 2 || dac > 999) ? 1 : 0;

			if (dac >= max_dac[cnt / 3])
				max_dac[cnt / 3] = dac;
			memcpy(flash_rgb_level_data[cnt/3][cnt%3], RGB_BUF, MAX_LENGTH);
		}
	}
	seq_number = RGB_BUF[MAX_LENGTH-1];
	printk(KERN_ERR "[%s] seq_number %x\n", __func__, seq_number);
	printk(KERN_INFO "[%s] max_dac : %d, %d, %d\n", __func__,
					max_dac[0], max_dac[1], max_dac[2]);

	DPPDATAFLASH(InitData_TransferCtrlToI2C);
}

void pwron_seq_fw_ver(void)
{
	unsigned int ver = 0;

	DPPDATAFLASH(FW_VER_STEP1);
	usleep_range(3000, 3100);
	DPPDATAFLASH(FW_VER_STEP2);

	ver |= RGB_BUF[0] << 24 | RGB_BUF[1] << 16
		| RGB_BUF[2] << 8 |  RGB_BUF[3];

	if (ver == 0x040000)
		old_fw = 2;
	else if (ver == 0x0295028D)
		old_fw = 0;
	else
		old_fw = 40;
}

void pwron_seq_current_limit(void)
{
	unsigned int limit;
	unsigned char ireg = 0x07;
	unsigned char current_limit[] = {
		PRJ_WRITE, 2, 0x02, 0xFF
	};

	limit = (max_dac[0] * 13) / 10;

	if (limit < 260)
		ireg |= 0x0 << 3;
	else if (limit >= 260 && limit < 300)
		ireg |= 0x1 << 3;
	else if (limit >= 300 && limit < 345)
		ireg |= 0x2 << 3;
	else if (limit >= 345 && limit < 385)
		ireg |= 0x3 << 3;
	else if (limit >= 385 && limit < 440)
		ireg |= 0x4 << 3;
	else if (limit >= 440 && limit < 660)
		ireg |= 0x5 << 3;
	else if (limit >= 660 && limit < 880)
		ireg |= 0x6 << 3;
	else if (limit >= 880)
		ireg |= 0x7 << 3;

	current_limit[3] = ireg;

	DPPDATAFLASH(current_limit);
	printk(KERN_INFO "[%s] Current Limit : %u, %#x\n", __func__,
			limit, (ireg & (~0x7)) >> 3);
}

static void proj_pwron_seq_work(struct work_struct *work)
{
	if (get_proj_status() == PRJ_ON_INTERNAL) {
		DPPDATAFLASH(Output_Curtain_Enable);
		pwron_seq_direction();
		pwron_seq_source_res(LCD_VIEW);
		msleep(300);
		DPPDATAFLASH(External_source);
		DPPDATAFLASH(Free_run);
		DPPDATAFLASH(Output_Curtain_Disable);
	} else {
		pwron_seq_gpio();

		pwron_seq_direction();
		pwron_seq_source_res(LCD_VIEW);

		pwron_seq_fdr();
		pwron_seq_fw_ver();
		pwron_seq_current_limit();
		set_led_current(brightness);

		GENRGBSETTING(Dmd_seq, seq_number);
		DPPDATAFLASH(Dmd_seq);

		DPPDATAFLASH(External_source);
		DPPDATAFLASH(Free_run);
		DPPDATAFLASH(AGC_OFF);

		if (system_rev >= GAVINI_R0_1)
			gpio_direction_output(info->pdata->gpio_prj_led_en,
					GPIO_LEVEL_HIGH);
	}
	set_proj_status(PRJ_ON_RGB_LCD);
}

static void proj_testmode_pwron_seq_work(struct work_struct *work)
{
	pwron_seq_gpio();

	DPPDATAFLASH(Internal_pattern_direction);
	pwron_seq_source_res(INTERNAL_PATTERN);

	pwron_seq_fdr();
	pwron_seq_fw_ver();
	pwron_seq_current_limit();
	set_led_current(brightness);

	GENRGBSETTING(Dmd_seq, seq_number);
	DPPDATAFLASH(Dmd_seq);

	switch (saved_pattern) {
	case CHECKER:
		DPPDATAFLASH(I_4x4checker);
		verify_value = 20;
		break;
	case WHITE:
		DPPDATAFLASH(I_white);
		verify_value = 21;
		break;
	case BLACK:
		DPPDATAFLASH(I_black);
		verify_value = 22;
		break;
	case LEDOFF:
		DPPDATAFLASH(RGB_led_off);
		set_proj_status(RGB_LED_OFF);
		verify_value = 23;
		break;
	case RED:
		DPPDATAFLASH(I_red);
		verify_value = 24;
		break;
	case GREEN:
		DPPDATAFLASH(I_green);
		verify_value = 25;
		break;
	case BLUE:
		DPPDATAFLASH(I_blue);
		verify_value = 26;
		break;
	case STRIPE:
		DPPDATAFLASH(I_stripe);
		verify_value = 28;
		break;
	default:
		break;
	}
	saved_pattern = -1;

	DPPDATAFLASH(AGC_OFF);
	if (system_rev >= GAVINI_R0_1)
		gpio_direction_output(info->pdata->gpio_prj_led_en,
					GPIO_LEVEL_HIGH);
	set_proj_status(PRJ_ON_INTERNAL);
}

void proj_testmode_pwron_seq(void)
{
	queue_work(projector_work_queue, &projector_work_testmode_on);
}

void ProjectorPowerOnSequence(void)
{
	queue_work(projector_work_queue, &projector_work_power_on);
}

static void proj_pwroff_seq_work(struct work_struct *work)
{
	if (system_rev >= GAVINI_R0_1)
		gpio_direction_output(info->pdata->gpio_prj_led_en,
				GPIO_LEVEL_LOW);

	gpio_direction_output(info->pdata->gpio_parkz, GPIO_LEVEL_LOW);
	msleep(50);

	gpio_direction_output(info->pdata->gpio_prj_en, GPIO_LEVEL_LOW);
	set_proj_status(PRJ_OFF);
}

void ProjectorPowerOffSequence(void)
{
	queue_work(projector_work_queue, &projector_work_power_off);
}

static void projector_rotate_screen_work(struct work_struct *work)
{
	if (status == PRJ_ON_RGB_LCD) {
		switch (screen_direction) {
		case PRJ_ROTATE_0:
			DPPDATAFLASH(Output_Curtain_Enable);
			DPPDATAFLASH(Output_Rotate_0);
			msleep(50);
			DPPDATAFLASH(Output_Curtain_Disable);
			break;
		case PRJ_ROTATE_90:
			DPPDATAFLASH(Output_Curtain_Enable);
			DPPDATAFLASH(Output_Rotate_90);
			msleep(50);
			DPPDATAFLASH(Output_Curtain_Disable);
			break;
		case PRJ_ROTATE_180:
			DPPDATAFLASH(Output_Curtain_Enable);
			DPPDATAFLASH(Output_Rotate_180);
			msleep(50);
			DPPDATAFLASH(Output_Curtain_Disable);
			break;
		case PRJ_ROTATE_270:
			DPPDATAFLASH(Output_Curtain_Enable);
			DPPDATAFLASH(Output_Rotate_270);
			msleep(50);
			DPPDATAFLASH(Output_Curtain_Disable);
			break;
		default:
			break;
		}
	}
}

int get_proj_rotation(void)
{
	return screen_direction;
}

int get_proj_brightness(void)
{
	return brightness;
}

int __devinit dpp2601_i2c_probe(struct i2c_client *client,
                const struct i2c_device_id *id)
{
	int ret = 0;

	if (system_rev < GAVINI_R0_0_B) {
		motor_drv1 = MOTDRV_IN1_GAVINI_R0_0;
		motor_drv2 = MOTDRV_IN2_GAVINI_R0_0;
		motor_drv3 = MOTDRV_IN3_GAVINI_R0_0;
		motor_drv4 = MOTDRV_IN4_GAVINI_R0_0;
	} else {
		motor_drv1 = MOTDRV_IN1_GAVINI_R0_0_B;
		motor_drv2 = MOTDRV_IN2_GAVINI_R0_0_B;
		motor_drv3 = MOTDRV_IN3_GAVINI_R0_0_B;
		motor_drv4 = MOTDRV_IN4_GAVINI_R0_0_B;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		printk(KERN_ERR "[%s] need I2C_FUNC_I2C\n", __func__);
		ret = -ENODEV;
		return ret;
	}
	
	info = kzalloc(sizeof(struct projector_dpp2601_info), GFP_KERNEL);
	if (!info) {
		printk(KERN_ERR "[%s] fail to memory allocation.\n", __func__);
		return -1;
	}

	info->client = client;
	info->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, info);

	gpio_request(info->pdata->gpio_parkz, "PARKZ");
	gpio_request(info->pdata->gpio_mp_on, "MP_ON");
	gpio_request(info->pdata->gpio_prj_en, "PRJ_EN");
	gpio_request(info->pdata->gpio_prj_led_en, "PRJ_LED_EN");
	gpio_request(info->pdata->gpio_en_lcd, "EN_LCD");

	projector_work_queue = create_singlethread_workqueue("projector_work_queue");
	if (!projector_work_queue) {
		printk(KERN_ERR "[%s] i2c_probe fail.\n", __func__);
		return -ENOMEM;
	}

	stepmotor_work_queue = create_singlethread_workqueue("stepmotor_work_queue");
	if (!stepmotor_work_queue) {
		printk(KERN_ERR "[%s] i2c_probe fail.\n", __func__);
		return -ENOMEM;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->earlysuspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->earlysuspend.suspend = projector_module_early_suspend;
	info->earlysuspend.resume  = projector_module_late_resume;
	register_early_suspend(&info->earlysuspend);
#endif

	INIT_WORK(&projector_work_power_on, proj_pwron_seq_work);
	INIT_WORK(&projector_work_power_off, proj_pwroff_seq_work);
	INIT_WORK(&projector_work_motor_cw, projector_motor_cw_work);
	INIT_WORK(&projector_work_motor_ccw, projector_motor_ccw_work);
	INIT_WORK(&projector_work_testmode_on, proj_testmode_pwron_seq_work);
	INIT_WORK(&projector_work_rotate_screen, projector_rotate_screen_work);
	
	printk(KERN_ERR "[%s] dpp2601_i2c_probe.\n", __func__);

	return 0;
}

__devexit int dpp2601_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id dpp2601_i2c_id[] = {
	{ "dpp2601", 0 },
	{ }
};
MODULE_DEVICE_TABLE(dpp2601_i2c, dpp2601_i2c_id);

static struct i2c_driver dpp2601_i2c_driver = {
	.driver = {
		.name = "dpp2601",
		.owner = THIS_MODULE,
	},
	.probe = dpp2601_i2c_probe,
	.remove = __devexit_p(dpp2601_i2c_remove),
	.id_table = dpp2601_i2c_id,
};

int projector_module_open(struct inode *inode, struct file *file)
{
		return 0;
}

int projector_module_release(struct inode *inode, struct file *file)
{
		return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
void projector_module_early_suspend(struct early_suspend *h)
{
	pr_info("%s\n", __func__);
	if (status == PRJ_OFF) {
		gpio_direction_output(info->pdata->gpio_parkz, GPIO_LEVEL_LOW);
		gpio_direction_output(info->pdata->gpio_prj_en, GPIO_LEVEL_LOW);
		gpio_direction_output(info->pdata->gpio_prj_led_en,
				GPIO_LEVEL_LOW);
		gpio_direction_output(info->pdata->gpio_en_lcd, GPIO_LEVEL_LOW);
	}
}

void projector_module_late_resume(struct early_suspend *h)
{
}

#endif


static struct file_operations projector_module_fops = {
	.owner = THIS_MODULE,
	.open = projector_module_open,
	.release = projector_module_release,
};

static struct miscdevice projector_module_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "secProjector",
	.fops = &projector_module_fops,
};

void project_internal(int pattern)
{
	flush_workqueue(projector_work_queue);
	if (get_proj_status() == PRJ_OFF) {
		proj_testmode_pwron_seq();
	} else {
		if (get_proj_status() == PRJ_ON_RGB_LCD) {
			DPPDATAFLASH(Internal_pattern_direction);
			pwron_seq_source_res(INTERNAL_PATTERN);
			set_proj_status(PRJ_ON_INTERNAL);
		}

		if (get_proj_status() == RGB_LED_OFF) {
			DPPDATAFLASH(RGB_led_on);
			set_proj_status(PRJ_ON_INTERNAL);
		}

		switch (pattern) {
		case CHECKER:
			DPPDATAFLASH(I_4x4checker);
			verify_value = 20;
			break;
		case WHITE:
			DPPDATAFLASH(I_white);
			verify_value = 21;
			break;
		case BLACK:
			DPPDATAFLASH(I_black);
			verify_value = 22;
			break;
		case LEDOFF:
			DPPDATAFLASH(RGB_led_off);
			set_proj_status(RGB_LED_OFF);
			verify_value = 23;
			break;
		case RED:
			DPPDATAFLASH(I_red);
			verify_value = 24;
			break;
		case GREEN:
			DPPDATAFLASH(I_green);
			verify_value = 25;
			break;
		case BLUE:
			DPPDATAFLASH(I_blue);
			verify_value = 26;
			break;
		case BEAUTY:
			verify_value = 27;
			break;
		case STRIPE:
			DPPDATAFLASH(I_stripe);
			verify_value = 28;
			break;
		default:
			break;
		}
	}
}


void move_motor_step(int value)
{
	int i, difference;

	difference = value - motor_abs_step;

	if (!once_motor_verified) {
		for (i = 0; i < MAX_MOTOR_STEP; i++) {
			motor_step++;
			if (motor_step > MOTOR_MAX_PHASE)
				motor_step = 0;

			printk(KERN_INFO "[%s] CW:%d\n", __func__, motor_step);
			MOTOR_PHASE_CW_FIRST(motor_step);
		}

		motor_abs_step = 0;
		msleep(25);

		for (i = 0; i < value; i++) {
			motor_step--;
			if (motor_step < 0)
				motor_step = MOTOR_MAX_PHASE;

			printk(KERN_INFO "[%s] CCW:%d\n", __func__, motor_step);
			MOTOR_PHASE_CW_FIRST(motor_step);
			motor_abs_step++;
		}

		once_motor_verified = 1;
	} else {
		if (difference < 0) {
			for (i = 0; i < -1 * difference; i++) {
				motor_step++;
				if (motor_step > MOTOR_MAX_PHASE)
					motor_step = 0;

				printk(KERN_INFO "[%s] CW:%d\n",
						__func__, motor_step);
				MOTOR_PHASE_CW_OUT(motor_step);
				motor_abs_step--;
			}
		} else if (difference > 0) {
			for (i = 0; i < difference; i++) {
				motor_step--;
				if (motor_step < 0)
					motor_step = MOTOR_MAX_PHASE;

				printk(KERN_INFO "[%s] CCW:%d\n",
						__func__, motor_step);
				MOTOR_PHASE_CW_OUT(motor_step);
				motor_abs_step++;
			}
		}
	}

	printk(KERN_INFO "[%s] Projector Motor ABS Step : %d\n",
			__func__, motor_abs_step);
	verify_value = 300 + value;
}

static ssize_t store_motor_action(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int direction;

	sscanf(buf, "%d\n", &direction);
	flush_workqueue(stepmotor_work_queue);

	if (status != PRJ_OFF) {
		switch (direction) {
		case MOTOR_CW:
			projector_motor_cw();
			break;
		case MOTOR_CCW:
			projector_motor_ccw();
			break;
		default:
			break;
		}
	}

	return count;
}

static ssize_t store_brightness(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int value;

	sscanf(buf, "%d\n", &value);

	if (get_proj_status() == PRJ_OFF) {
		switch (value) {
		case 1:
			brightness = BRIGHT_HIGH;
			break;
		case 2:
			brightness = BRIGHT_MID;
			break;
		case 3:
			brightness = BRIGHT_LOW;
			break;
		default:
			break;
		}
	} else {
		brightness = value;
		set_led_current(value);
		printk(KERN_INFO "[%s] Proj Brightness Changed : %d\n",
					__func__, value);
	}
	return count;
}

static ssize_t store_proj_key(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int value;

	sscanf(buf, "%d\n", &value);

	flush_workqueue(projector_work_queue);
	switch (value) {
	case 0:
		ProjectorPowerOffSequence();
		verify_value = 0;
		break;
	case 1:
		if (get_proj_status() != PRJ_ON_RGB_LCD) {
			ProjectorPowerOnSequence();
			verify_value = 10;
		}
		break;
	default:
		break;
	};

	printk(KERN_INFO "[%s] -->  %d\n", __func__, value);
	return count;
}

static ssize_t show_cal_history(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int size, result = 0;

	result = not_calibrated + old_fw;

	size = sprintf(buf, "%d\n", result);

	return size;
}

static ssize_t show_screen_direction(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int size;

	size = sprintf(buf, "%d\n", screen_direction);

	return size;
}

static ssize_t show_retval(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int size;

	size = sprintf(buf, "%d\n", verify_value);

	return size;
}

static ssize_t store_screen_direction(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int  value;

	sscanf(buf, "%d\n", &value);

	if (value >= 0 && value <= 3) {
		screen_direction = value;
	}

	return count;
}

static ssize_t store_rotate_screen(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int value;

	sscanf(buf, "%d\n", &value);

	if (value >= 0 && value <= 3) {
		flush_workqueue(projector_work_queue);
		screen_direction = value;
		queue_work(projector_work_queue, &projector_work_rotate_screen);

		printk(KERN_INFO "[%s] inputed rotate : %d\n", __func__, value);
	}

	return count;
}

static ssize_t store_projection_verify(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int value;

	sscanf(buf, "%d\n", &value);
	printk(KERN_INFO "[%s] selected internal pattern : %d\n",
				__func__, value);


	if (value == CURTAIN_ON) {
		if (status != PRJ_ON_INTERNAL)
			return count;
		DPPDATAFLASH(Output_Curtain_Enable);
	} else if (value == CURTAIN_OFF) {
		if (status != PRJ_ON_INTERNAL)
			return count;
		DPPDATAFLASH(Output_Curtain_Disable);
	} else {
		saved_pattern = value;
		project_internal(value);
	}
	return count;
}


static ssize_t store_motor_verify(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int value;

	sscanf(buf, "%d\n", &value);

	if (value >= 0 && value <= 60) {
		move_motor_step(value);
	}

	return count;
}

static DEVICE_ATTR(proj_motor, S_IRUGO | S_IWUSR, NULL, store_motor_action);
static DEVICE_ATTR(brightness, S_IRUGO | S_IWUSR, NULL, store_brightness);
static DEVICE_ATTR(proj_key, S_IRUGO | S_IWUSR, NULL, store_proj_key);
static DEVICE_ATTR(cal_history, S_IRUGO, show_cal_history, NULL);
static DEVICE_ATTR(rotate_screen, S_IRUGO | S_IWUSR,
				NULL, store_rotate_screen);
static DEVICE_ATTR(screen_direction, S_IRUGO | S_IWUSR,
				show_screen_direction, store_screen_direction);
static DEVICE_ATTR(projection_verify, S_IRUGO | S_IWUSR,
				NULL, store_projection_verify);
static DEVICE_ATTR(motor_verify, S_IRUGO | S_IWUSR,
				NULL, store_motor_verify);
static DEVICE_ATTR(retval, S_IRUGO, show_retval, NULL);

int __init projector_module_init(void)
{
	int ret;

	sec_projector = device_create(sec_class, NULL, 0, NULL, "sec_projector");
	if (IS_ERR(sec_projector)) {
		printk(KERN_ERR "Failed to create device(sec_projector)!\n");
	}

	if (device_create_file(sec_projector, &dev_attr_proj_motor) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_proj_motor.attr.name);

	if (device_create_file(sec_projector, &dev_attr_brightness) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_brightness.attr.name);

	if (device_create_file(sec_projector, &dev_attr_proj_key) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_proj_key.attr.name);

	if (device_create_file(sec_projector, &dev_attr_cal_history) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_cal_history.attr.name);

	if (device_create_file(sec_projector, &dev_attr_rotate_screen) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_rotate_screen.attr.name);

	if (device_create_file(sec_projector, &dev_attr_screen_direction) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_screen_direction.attr.name);

	if (device_create_file(sec_projector, &dev_attr_projection_verify) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_projection_verify.attr.name);

	if (device_create_file(sec_projector, &dev_attr_motor_verify) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_motor_verify.attr.name);

	if (device_create_file(sec_projector, &dev_attr_retval) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_retval.attr.name);

	ret = i2c_add_driver(&dpp2601_i2c_driver);
	ret |= misc_register(&projector_module_device);
	if (ret) {
		printk(KERN_ERR "Projector driver registration failed!\n");
	}
	return ret;
}

void __exit projector_module_exit(void)
{
	i2c_del_driver(&dpp2601_i2c_driver);
	misc_deregister(&projector_module_device);
}

late_initcall(projector_module_init);
module_exit(projector_module_exit);

MODULE_DESCRIPTION("Samsung projector module driver");
MODULE_AUTHOR("Inbum Choi <inbum.choi@samsung.com>");
MODULE_LICENSE("GPL");
