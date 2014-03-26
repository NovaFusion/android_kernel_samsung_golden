/*
 *
 * Zinitix touch driver
 *
 * Copyright (C) 2009 Zinitix, Inc.
 * Modified by robert.teather@samsung.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 */
/*
Version 2.0.0 : using reg data file (2010/11/05)
Version 2.0.1 : syntxt bug fix (2010/11/09)
Version 2.0.2 : Save status cmd delay bug (2010/11/10)
Version 2.0.3 : modify delay 10ms -> 50ms for clear hw calibration bit
	: modify ZINITIX_TOTAL_NUMBER_OF_Y register (read only -> read/write )
	: modify SUPPORTED FINGER NUM register (read only -> read/write )
Version 2.0.4 : [20101116]
	Modify Firmware Upgrade routine.
Version 2.0.5 : [20101118]
	add esd timer function & some bug fix.
	you can select request_threaded_irq or
	request_irq, setting USE_THREADED_IRQ.
Version 2.0.6 : [20101123]
	add ABS_MT_WIDTH_MAJOR Report
Version 2.0.7 : [20101201]
	Modify zinitix_early_suspend() / zinitix_late_resume() routine.
Version 2.0.8 : [20101216]
	add using spin_lock option
Version 2.0.9 : [20101216]
	Test Version
Version 2.0.10 : [20101217]
	add USE_THREAD_METHOD option.
	if USE_THREAD_METHOD = 0, you use workqueue
Version 2.0.11 : [20101229]
	add USE_UPDATE_SYSFS option for update firmware.
	&& TOUCH_MODE == 1 mode.
Version 2.0.13 : [20110125]
	modify esd timer routine
Version 2.0.14 : [20110217]
	esd timer bug fix. (kernel panic)
	sysfs bug fix.
Version 2.0.15 : [20110315]
	add power off delay ,250ms
Version 2.0.16 : [20110316]
	add upgrade method using isp
Version 2.0.17 : [20110406]
	change naming rule : sain -> zinitix
	(add) pending interrupt skip
	add isp upgrade mode
	remove warning message when complile
Version 3.0.2 : [20110711]
	support bt4x3 series
Version 3.0.3 : [20110720]
	add raw data monitoring func.
	add the h/w calibration skip option.
Version 3.0.4 : [20110728]
	fix using semaphore bug.
Version 3.0.5 : [20110801]
	fix some bugs.
Version 3.0.6 : [20110802]
	fix Bt4x3 isp upgrade bug.
	add USE_TS_MISC_DEVICE option for showing info & upgrade
	remove USE_UPDATE_SYSFS option
Version 3.0.7 : [201108016]
	merge USE_TS_MISC_DEVICE option and USE_TEST_RAW_TH_DATA_MODE
	fix work proceedure bug.
Version 3.0.8 / Version 3.0.9 : [201108017]
	add ioctl func.
Version 3.0.10 : [201108030]
	support REAL_SUPPORTED_FINGER_NUM
Version 3.0.11 : [201109014]
	support zinitix apps.
Version 3.0.12 : [201109015]
	add disable touch event func.
Version 3.0.13 : [201109015]
	add USING_CHIP_SETTING option :  interrupt mask/button num/finger num
Version 3.0.14 : [201109020]
	support apps. above kernel version 2.6.35
Version 3.0.15 : [201101004]
	modify timing in firmware upgrade retry when failt to upgrade firmware
Version 3.0.16 : [201101024]
	remove thread method.
	add resume/suspend function
	check code using checkpatch.pl
*/

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/ioctl.h>
#include <linux/earlysuspend.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/uaccess.h>

#include <linux/input/zinitix_touch.h>
#include "zinitix_touch.h"
#if	BT4x2_Series
#include "zinitix_touch_firmware.h"
#endif

#if	BT4x3_Above_Series
#include "zinitix_touch_bt4x3_firmware.h"
#endif


#define	ZINITIX_DEBUG		0

static	int	m_ts_debug_mode = ZINITIX_DEBUG;

static struct workqueue_struct *zinitix_workqueue;

#if	ZINITIX_ESD_TIMER_INTERVAL
static struct workqueue_struct *zinitix_tmr_workqueue;
#endif

#define	zinitix_debug_msg(fmt, args...)	\
	if (m_ts_debug_mode)	\
		printk(KERN_INFO "[%-18s:%5d]" fmt, \
		__func__, __LINE__, ## args);

struct _ts_zinitix_coord {
	u16	x;
	u16	y;
	u8	width;
	u8	sub_status;
};

struct _ts_zinitix_point_info {
	u16	status;
#if (TOUCH_MODE == 1)
	u16	event_flag;
#else
	u8	finger_cnt;
	u8	time_stamp;
#endif
	struct _ts_zinitix_coord	coord[MAX_SUPPORTED_FINGER_NUM];

};

struct _ts_capa_info {
	u16 chip_revision;
	u16 chip_firmware_version;
	u16 chip_reg_data_version;
	u16 x_resolution;
	u16 y_resolution;
	u32 chip_fw_size;
	u32 MaxX;
	u32 MaxY;
	u32 MinX;
	u32 MinY;
	u32 Orientation;
	u8 gesture_support;
	u16 multi_fingers;
	u16 button_num;
	u16 chip_int_mask;
#if	USE_TEST_RAW_TH_DATA_MODE
	u16 x_node_num;
	u16 y_node_num;
	u16 total_node_num;
#if	BT4x3_Above_Series
	u16 max_y_node;
	u16 total_cal_n;
#endif
#endif
};

enum _ts_work_proceedure {
	TS_NO_WORK = 0,
	TS_NORMAL_WORK,
	TS_ESD_TIMER_WORK,
	TS_IN_EARLY_SUSPEND,
	TS_IN_SUSPEND,
	TS_IN_RESUME,
	TS_IN_LATE_RESUME,
	TS_IN_UPGRADE,
	TS_REMOVE_WORK,
	TS_SET_MODE,
	TS_HW_CALIBRAION,
};

struct zinitix_touch_dev {
	struct input_dev *input_dev;
	struct task_struct *task;
	wait_queue_head_t	wait;
	struct work_struct	work;
	struct work_struct	tmr_work;
	struct i2c_client *client;
	struct semaphore update_lock;
	u32 i2c_dev_addr;
	struct _ts_capa_info	cap_info;
	char	phys[32];

	bool is_valid_event;
	struct _ts_zinitix_point_info touch_info;
	struct _ts_zinitix_point_info reported_touch_info;
	u16 icon_event_reg;
	u16 event_type;
	u32 int_gpio_num;
	u32 irq;
	u8 button[MAX_SUPPORTED_BUTTON_NUM];
	u8 work_proceedure;
	struct semaphore work_proceedure_lock;

	int reset_gpio_num;

	u8	use_esd_timer;
#if	ZINITIX_ESD_TIMER_INTERVAL
	bool in_esd_timer;
	struct timer_list esd_timeout_tmr;
	struct timer_list *p_esd_timeout_tmr;
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif

#if	USE_TEST_RAW_TH_DATA_MODE
	struct semaphore	raw_data_lock;
	u16 raw_mode_flag;
	s16 ref_data[MAX_TEST_RAW_DATA];
	s16 cur_data[MAX_RAW_DATA];
	u8 update;
#endif

	u32	tsp_ldo_en;		/* power enable GPIO */
	u32	num_buttons;		/* number of buttons supported */
		/* ordered mapping of buttons */
	u32	BUTTON_MAPPING_KEY[MAX_SUPPORTED_BUTTON_NUM];
	u32	num_regs;		/* number of registers on
					particular Zinitix controller */
		/* Pointer to table of register settings to configure
		controller for this product. */
	const struct _zinitix_reg_data *reg_data;
};


#if	TOUCH_USING_ISP_METHOD
static struct i2c_client *m_isp_client;
#endif


static struct i2c_device_id zinitix_idtable[] = {
#if	TOUCH_USING_ISP_METHOD
	{ZINITIX_ISP_NAME, 0},
#endif
	{ZINITIX_DRIVER_NAME, 0},
	{ }
};


/* define i2c sub functions*/
#if BT4x2_Series
inline s32 ts_write_cmd(struct i2c_client *client, u8 reg)
{
	s32 ret;
	ret = i2c_smbus_write_byte(client, reg);
	udelay(DELAY_FOR_POST_TRANSCATION);
	return ret;
}

inline s32 ts_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	s32 ret;
	ret = i2c_smbus_write_word_data(client, reg, value);
	udelay(DELAY_FOR_POST_TRANSCATION);
	return ret;
}

inline s32 ts_read_data(struct i2c_client *client,
	u8 reg, u8 *values, u16 length)
{
	s32 ret;
	/* select register*/
	ret = i2c_master_send(client , &reg , 1);
	if (ret < 0)
		return ret;
	/* for setup tx transaction.*/
	udelay(DELAY_FOR_TRANSCATION);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

inline s32 ts_write_data(struct i2c_client *client,
	u8 reg, u8 *values, u16 length)
{
	s32 ret;
	ret = i2c_smbus_write_i2c_block_data(client, reg, length, values);
	udelay(DELAY_FOR_POST_TRANSCATION);
	return ret;
}

inline s32 ts_read_raw_data(struct i2c_client *client,
	u8 reg, u8 *values, u16 length)
{
	s32 ret;
	/* select register*/
	ret = i2c_master_send(client , &reg , 1);
	if (ret < 0)
		return ret;
	/* for setup tx transaction */
	mdelay(5);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}
#endif

#if BT4x3_Above_Series
static s32 ts_read_data(struct i2c_client *client,
	u16 reg, u8 *values, u16 length)
{
	s32 ret;
	/* select register*/
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0)
		return ret;
	/* for setup tx transaction. */
	udelay(DELAY_FOR_TRANSCATION);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

static s32 ts_write_data(struct i2c_client *client,
	u16 reg, u8 *values, u16 length)
{
	s32 ret;
	u8	pkt[4];
	pkt[0] = (reg)&0xff;
	pkt[1] = (reg >> 8)&0xff;
	pkt[2] = values[0];
	pkt[3] = values[1];
	ret = i2c_master_send(client , pkt , length+2);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

static s32 ts_write_reg(struct i2c_client *client, u16 reg, u16 value)
{
	s32 ret;
	ret = ts_write_data(client, reg, (u8 *)&value, 2);
	if (ret < 0)
		return ret;
	return I2C_SUCCESS;
}

static s32 ts_write_cmd(struct i2c_client *client, u16 reg)
{
	s32 ret;
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return I2C_SUCCESS;
}

static s32 ts_read_raw_data(struct i2c_client *client,
		u16 reg, u8 *values, u16 length)
{
	s32 ret;
	/* select register */
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0)
		return ret;
	/* for setup tx transaction. */
	udelay(200);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}
#endif

#if	TOUCH_USING_ISP_METHOD
inline s32 ts_read_firmware_data(struct i2c_client *client,
	char *addr, u8 *values, u16 length)
{
	s32 ret;
	if (addr != NULL) {
		/* select register*/
		ret = i2c_master_send(client , addr , 2);
		if (ret < 0)
			return ret;
		/* for setup tx transaction. */
		mdelay(1);
	}
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

inline s32 ts_write_firmware_data(struct i2c_client *client,
	u8 *values, u16 length)
{
	s32 ret;
	ret = i2c_master_send(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}
#else
inline s32 ts_read_firmware_data(struct i2c_client *client,
	u8 reg, u8 *values, u16 length)
{
	s32 ret;
	/* select register */
	ret = i2c_master_send(client , &reg , 1);
	if (ret < 0)
		return ret;
	/* for setup tx transaction */
	mdelay(1);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}
#endif

static int zinitix_touch_probe(struct i2c_client *client,
	const struct i2c_device_id *i2c_id);
static int zinitix_touch_remove(struct i2c_client *client);
static bool ts_init_touch(struct zinitix_touch_dev *touch_dev);
static void zinitix_clear_report_data(struct zinitix_touch_dev *touch_dev);
static int zinitix_resume(struct i2c_client *client);
static int zinitix_suspend(struct i2c_client *client, pm_message_t message);

#if (TOUCH_MODE == 1)
static void zinitix_report_data(struct zinitix_touch_dev *touch_dev,
	int id);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void zinitix_early_suspend(struct early_suspend *h);
static void zinitix_late_resume(struct early_suspend *h);
#endif

#if	ZINITIX_ESD_TIMER_INTERVAL
static void ts_esd_timer_start(u16 sec, struct zinitix_touch_dev *touch_dev);
static void ts_esd_timer_stop(struct zinitix_touch_dev *touch_dev);
static void ts_esd_timer_init(struct zinitix_touch_dev *touch_dev);
static void ts_esd_timeout_handler(unsigned long data);
#endif


#if USE_TEST_RAW_TH_DATA_MODE
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static int ts_misc_fops_ioctl(struct inode *inode, struct file *filp,
	unsigned int cmd, unsigned long arg);
#else
static long ts_misc_fops_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg);
#endif
static int ts_misc_fops_open(struct inode *inode, struct file *filp);
static int ts_misc_fops_close(struct inode *inode, struct file *filp);

static const struct file_operations ts_misc_fops = {
	.owner = THIS_MODULE,
	.open = ts_misc_fops_open,
	.release = ts_misc_fops_close,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	.ioctl = ts_misc_fops_ioctl,
#else
	.unlocked_ioctl = ts_misc_fops_ioctl,
#endif
};

static struct miscdevice touch_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "zinitix_touch_misc",
	.fops = &ts_misc_fops,
};

#define TOUCH_IOCTL_BASE	0xbc
#define TOUCH_IOCTL_GET_DEBUGMSG_STATE		_IOW(TOUCH_IOCTL_BASE, 0, int)
#define TOUCH_IOCTL_SET_DEBUGMSG_STATE		_IOW(TOUCH_IOCTL_BASE, 1, int)
#define TOUCH_IOCTL_GET_CHIP_REVISION		_IOW(TOUCH_IOCTL_BASE, 2, int)
#define TOUCH_IOCTL_GET_FW_VERSION		_IOW(TOUCH_IOCTL_BASE, 3, int)
#define TOUCH_IOCTL_GET_REG_DATA_VERSION	_IOW(TOUCH_IOCTL_BASE, 4, int)
#define TOUCH_IOCTL_VARIFY_UPGRADE_SIZE		_IOW(TOUCH_IOCTL_BASE, 5, int)
#define TOUCH_IOCTL_VARIFY_UPGRADE_DATA		_IOW(TOUCH_IOCTL_BASE, 6, int)
#define TOUCH_IOCTL_START_UPGRADE		_IOW(TOUCH_IOCTL_BASE, 7, int)
#define TOUCH_IOCTL_GET_X_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 8, int)
#define TOUCH_IOCTL_GET_Y_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 9, int)
#define TOUCH_IOCTL_GET_TOTAL_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 10, int)
#define TOUCH_IOCTL_SET_RAW_DATA_MODE		_IOW(TOUCH_IOCTL_BASE, 11, int)
#define TOUCH_IOCTL_GET_RAW_DATA		_IOW(TOUCH_IOCTL_BASE, 12, int)
#define TOUCH_IOCTL_GET_X_RESOLUTION		_IOW(TOUCH_IOCTL_BASE, 13, int)
#define TOUCH_IOCTL_GET_Y_RESOLUTION		_IOW(TOUCH_IOCTL_BASE, 14, int)
#define TOUCH_IOCTL_HW_CALIBRAION		_IOW(TOUCH_IOCTL_BASE, 15, int)
#define TOUCH_IOCTL_GET_REG			_IOW(TOUCH_IOCTL_BASE, 16, int)
#define TOUCH_IOCTL_SET_REG			_IOW(TOUCH_IOCTL_BASE, 17, int)
#define TOUCH_IOCTL_SEND_SAVE_STATUS		_IOW(TOUCH_IOCTL_BASE, 18, int)
#define TOUCH_IOCTL_DONOT_TOUCH_EVENT		_IOW(TOUCH_IOCTL_BASE, 19, int)


struct zinitix_touch_dev *misc_touch_dev;

#endif /*USE_TEST_RAW_TH_DATA_MODE */



/* id -> include/linux/i2c-id.h	*/
static struct i2c_driver zinitix_touch_driver = {
	.probe	= zinitix_touch_probe,
	.remove	= zinitix_touch_remove,
	.suspend = zinitix_suspend,
	.resume	= zinitix_resume,
	.id_table	= zinitix_idtable,
	.driver		= {
		.name	= ZINITIX_DRIVER_NAME,
	},
};

#if	USE_TEST_RAW_TH_DATA_MODE
static bool ts_get_raw_data(struct zinitix_touch_dev *touch_dev)
{
	u32 total_node = touch_dev->cap_info.total_node_num;
	u32 sz;
	u16 udata;
	down(&touch_dev->raw_data_lock);
	if (touch_dev->raw_mode_flag == TOUCH_TEST_RAW_MODE) {
		sz = (total_node*2 + MAX_TEST_POINT_INFO*2);
#if	BT4x2_Series
		if (sz < 512) {
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_RAWDATA_REG,
				(char *)touch_dev->cur_data, sz) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
		} else {
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_RAWDATA_REG,
				(char *)&touch_dev->cur_data[0], 512) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
			udelay(50);
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_EXTRA_RAWDATA_REG,
				(char *)&touch_dev->cur_data[256],
				sz - 512) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
		}
#endif

#if	BT4x3_Above_Series
		if (ts_read_raw_data(touch_dev->client,
			ZINITIX_RAWDATA_REG,
			(char *)touch_dev->cur_data, sz) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
		}
#endif
		/* no point, so update ref_data*/
		udata = touch_dev->cur_data[total_node];

		if (!zinitix_bit_test(udata, BIT_ICON_EVENT)
			&& !zinitix_bit_test(udata, BIT_PT_EXIST))
			memcpy((u8 *)touch_dev->ref_data,
				(u8 *)touch_dev->cur_data, total_node*2);

		touch_dev->update = 1;
		memcpy((u8 *)(&touch_dev->touch_info),
			(u8 *)&touch_dev->cur_data[total_node],
			sizeof(struct _ts_zinitix_point_info));
		up(&touch_dev->raw_data_lock);
		return true;
	}  else if (touch_dev->raw_mode_flag != TOUCH_NORMAL_MODE) {
		zinitix_debug_msg("read raw data\r\n");
		sz = total_node*2 + sizeof(struct _ts_zinitix_point_info);
#if	BT4x2_Series
		if (touch_dev->raw_mode_flag == TOUCH_ZINITIX_CAL_N_MODE) {
			sz = (161*2 + sizeof(struct _ts_zinitix_point_info));
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_RAWDATA_REG,
				(char *)touch_dev->cur_data, sz) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
			misc_touch_dev->update = 1;
			memcpy((u8 *)(&touch_dev->touch_info),
				(u8 *)&touch_dev->cur_data[161],
				sizeof(struct _ts_zinitix_point_info));
			up(&touch_dev->raw_data_lock);
			return true;
		}
		if (sz < 512) {
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_RAWDATA_REG,
				(char *)touch_dev->cur_data, sz) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
		} else {
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_RAWDATA_REG,
				(char *)&touch_dev->cur_data[0], 512) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
			udelay(50);
			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_EXTRA_RAWDATA_REG,
				(char *)&touch_dev->cur_data[256],
				sz - 512) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
		}
#endif
#if	BT4x3_Above_Series
		if (touch_dev->raw_mode_flag == TOUCH_ZINITIX_CAL_N_MODE) {
			int total_cal_n = touch_dev->cap_info.total_cal_n;
			if (total_cal_n == 0)
				total_cal_n = 160;

			sz =
			(total_cal_n*2 + sizeof(struct _ts_zinitix_point_info));

			if (ts_read_raw_data(touch_dev->client,
				ZINITIX_RAWDATA_REG,
				(char *)touch_dev->cur_data, sz) < 0) {
				printk(KERN_INFO "error : read zinitix tc raw data\n");
				up(&touch_dev->raw_data_lock);
				return false;
			}
			misc_touch_dev->update = 1;
			memcpy((u8 *)(&touch_dev->touch_info),
				(u8 *)&touch_dev->cur_data[total_cal_n],
				sizeof(struct _ts_zinitix_point_info));
			up(&touch_dev->raw_data_lock);
			return true;
		}

		if (ts_read_raw_data(touch_dev->client,
			ZINITIX_RAWDATA_REG,
			(char *)touch_dev->cur_data, sz) < 0) {
			printk(KERN_INFO "error : read zinitix tc raw data\n");
			up(&touch_dev->raw_data_lock);
			return false;
		}
#endif
		udata = touch_dev->cur_data[total_node];
		if (!zinitix_bit_test(udata, BIT_ICON_EVENT)
			&& !zinitix_bit_test(udata, BIT_PT_EXIST))
			memcpy((u8 *)touch_dev->ref_data,
				(u8 *)touch_dev->cur_data, total_node*2);
		touch_dev->update = 1;
		memcpy((u8 *)(&touch_dev->touch_info),
			(u8 *)&touch_dev->cur_data[total_node],
			sizeof(struct _ts_zinitix_point_info));
	}
	up(&touch_dev->raw_data_lock);
	return true;
}
#endif


static bool ts_get_samples(struct zinitix_touch_dev *touch_dev)
{
	int i;
#if (TOUCH_MODE == 1)
	u8	sub_status;
	u32 x, y;
#endif

	zinitix_debug_msg("ts_get_samples+\r\n");

	if (gpio_get_value(touch_dev->int_gpio_num)) {
				/*interrupt pin is high, not valid data.*/
		zinitix_debug_msg("woops... intrpt pin is high\r\n");
		return false;
	}

#if	USE_TEST_RAW_TH_DATA_MODE
	if (touch_dev->raw_mode_flag != TOUCH_NORMAL_MODE) {
		if (ts_get_raw_data(touch_dev) == false)
			return false;
#if (TOUCH_MODE == 0)
		goto continue_check_point_data;
#endif
	}
#endif

#if (TOUCH_MODE == 1)
	memset(&touch_dev->touch_info,
		0x0, sizeof(struct _ts_zinitix_point_info));

	if (ts_read_data(touch_dev->client,
		ZINITIX_POINT_STATUS_REG,
		(u8 *)(&touch_dev->touch_info), 4) < 0) {
		zinitix_debug_msg("error read point info using i2c.-\r\n");
				 return false;
	}
	zinitix_debug_msg("status reg = 0x%x , event_flag = 0x%04x\r\n",
		touch_dev->touch_info.status, touch_dev->touch_info.event_flag);

	if (touch_dev->touch_info.status == 0x0) {
		zinitix_debug_msg("periodical esd repeated int occured\r\n");
		return true;
	}

	if (zinitix_bit_test(touch_dev->touch_info.status, BIT_ICON_EVENT)) {
		udelay(20);
		if (ts_read_data(touch_dev->client,
			ZINITIX_ICON_STATUS_REG,
			(u8 *)(&touch_dev->icon_event_reg), 2) < 0) {
			printk(KERN_INFO "error read icon info using i2c.\n");
			return false;
		}
		return true;
	}

	if (!zinitix_bit_test(touch_dev->touch_info.status, BIT_PT_EXIST)) {
		for (i = 0; i < touch_dev->cap_info.multi_fingers; i++) {
			sub_status =
			touch_dev->reported_touch_info.coord[i].sub_status;
			x = touch_dev->reported_touch_info.coord[i].x;
			y = touch_dev->reported_touch_info.coord[i].y;
			if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
				input_report_abs(touch_dev->input_dev,
					ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_WIDTH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_X, x);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_Y, y);
				input_mt_sync(touch_dev->input_dev);
				memset(&touch_dev->reported_touch_info.coord[i],
					0x0,
					sizeof(struct _ts_zinitix_coord));
			}
		}
		input_sync(touch_dev->input_dev);
		return true;
	}


	for (i = 0; i < touch_dev->cap_info.multi_fingers; i++) {
		if (zinitix_bit_test(touch_dev->touch_info.event_flag, i)) {
			udelay(20);
			if (ts_read_data(touch_dev->client,
				ZINITIX_POINT_STATUS_REG+2+i,
				(u8 *)(&touch_dev->touch_info.coord[i]),
				sizeof(struct _ts_zinitix_coord)) < 0) {
				zinitix_debug_msg("error read point info\n");
				return false;
			}
			zinitix_bit_clr(touch_dev->touch_info.event_flag, i);
			if (touch_dev->touch_info.event_flag == 0) {
				zinitix_report_data(touch_dev, i);
				return true;
			} else
				zinitix_report_data(touch_dev, i);
		}
	}


#else
	if (ts_read_data(touch_dev->client,
		ZINITIX_POINT_STATUS_REG,
		(u8 *)(&touch_dev->touch_info),
		sizeof(struct _ts_zinitix_point_info)) < 0) {
		zinitix_debug_msg("error read point info using i2c.-\n");
				return false;
	}

continue_check_point_data:
	zinitix_debug_msg("status = 0x%x , pt cnt = %d, t stamp = %d\n",
		touch_dev->touch_info.status,
		touch_dev->touch_info.finger_cnt,
		touch_dev->touch_info.time_stamp);

	if (touch_dev->touch_info.status == 0x0
		&& touch_dev->touch_info.finger_cnt == 100) {
		zinitix_debug_msg("periodical esd repeated int occured\r\n");
		return true;
	}

	for (i = 0; i < MAX_SUPPORTED_BUTTON_NUM; i++)
		touch_dev->button[i] = ICON_BUTTON_UNCHANGE;

	if (zinitix_bit_test(touch_dev->touch_info.status, BIT_ICON_EVENT)) {
		udelay(20);
		if (ts_read_data(touch_dev->client,
			ZINITIX_ICON_STATUS_REG,
			(u8 *)(&touch_dev->icon_event_reg), 2) < 0) {
			printk(KERN_INFO "error read icon info using i2c.\n");
					return false;
		}
	}
#endif
	zinitix_debug_msg("ts_get_samples-\r\n");

	return true;
}


static irqreturn_t ts_int_handler(int irq, void *dev)
{
	struct zinitix_touch_dev *touch_dev = (struct zinitix_touch_dev *)dev;
	/* zinitix_debug_msg("interrupt occured +\r\n"); */
	/* remove pending interrupt */
	if (gpio_get_value(touch_dev->int_gpio_num)) {
		zinitix_debug_msg("invalid interrupt occured +\r\n");
			 return IRQ_HANDLED;
	}

	disable_irq_nosync(irq);
	queue_work(zinitix_workqueue, &touch_dev->work);
	return IRQ_HANDLED;
}

static bool ts_read_coord(struct zinitix_touch_dev *hDevice)
{
	struct zinitix_touch_dev *touch_dev =
		(struct zinitix_touch_dev *)hDevice;
	if (ts_get_samples(touch_dev) == false)
		return false;

	ts_write_cmd(touch_dev->client, ZINITIX_CLEAR_INT_STATUS_CMD);
#if DELAY_FOR_SIGNAL_DELAY
	udelay(DELAY_FOR_SIGNAL_DELAY);
#endif
	return true;
}

static void ts_power_control(struct zinitix_touch_dev *touch_dev, u8 ctl)
{
	if (ctl == POWER_OFF) {
		if (gpio_is_valid(touch_dev->tsp_ldo_en)) {
			printk(KERN_INFO "Power pin low\r\n");
			gpio_set_value(touch_dev->tsp_ldo_en, 0);
		}
	} else if (ctl == POWER_ON) {
		if (gpio_is_valid(touch_dev->tsp_ldo_en)) {
			printk(KERN_INFO "Power pin high\r\n");
			gpio_set_value(touch_dev->tsp_ldo_en, 1);
		}
	} else if (ctl == RESET_LOW) {
		if (gpio_is_valid(touch_dev->reset_gpio_num)) {
			printk(KERN_INFO "reset pin low\r\n");
			gpio_set_value(touch_dev->reset_gpio_num, 0);
		}
	} else if (ctl == RESET_HIGH) {
		if (gpio_is_valid(touch_dev->reset_gpio_num)) {
			printk(KERN_INFO "reset pin high\r\n");
			gpio_set_value(touch_dev->reset_gpio_num, 1);
		}
	}


}

static bool ts_mini_init_touch(struct zinitix_touch_dev *touch_dev)
{
	if (touch_dev == NULL) {
		printk(KERN_INFO "ts_mini_init_touch : error (touch_dev == NULL?)\r\n");
		return false;
	}

	ts_init_touch(touch_dev);

#if	ZINITIX_ESD_TIMER_INTERVAL
	if (touch_dev->use_esd_timer) {
		ts_esd_timer_start(ZINITIX_CHECK_ESD_TIMER, touch_dev);
		zinitix_debug_msg("esd timer start\r\n");
	}
#endif

	return true;
}


#if	ZINITIX_ESD_TIMER_INTERVAL

static void zinitix_touch_tmr_work(struct work_struct *work)
{
	struct zinitix_touch_dev *touch_dev =
		container_of(work, struct zinitix_touch_dev, tmr_work);

	printk(KERN_INFO "tmr queue work ++\r\n");
	if (touch_dev == NULL) {
		printk(KERN_INFO "touch dev == NULL ?\r\n");
		goto fail_time_out_init;
	}
	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_NO_WORK) {
		printk(KERN_INFO "other process occupied (%d)\r\n",
			touch_dev->work_proceedure);
		up(&touch_dev->work_proceedure_lock);
		return;
	}
	touch_dev->work_proceedure = TS_ESD_TIMER_WORK;

	disable_irq(touch_dev->irq);
	printk(KERN_INFO "error. timeout occured. maybe ts device dead. so reset & reinit.\r\n");
	mdelay(CHIP_POWER_OFF_DELAY);
	ts_power_control(touch_dev, RESET_LOW);
	ts_power_control(touch_dev, POWER_OFF);
	mdelay(CHIP_POWER_OFF_DELAY);
	ts_power_control(touch_dev, POWER_ON);
	ts_power_control(touch_dev, RESET_HIGH);
	mdelay(CHIP_ON_DELAY);

	zinitix_debug_msg("clear all reported points\r\n");
	zinitix_clear_report_data(touch_dev);
	if (ts_mini_init_touch(touch_dev) == false)
		goto fail_time_out_init;

	touch_dev->work_proceedure = TS_NO_WORK;
	enable_irq(touch_dev->irq);
	up(&touch_dev->work_proceedure_lock);
	printk(KERN_INFO "tmr queue work ----\r\n");
	return;
fail_time_out_init:
	printk(KERN_INFO "tmr work : restart error\r\n");
	ts_esd_timer_start(ZINITIX_CHECK_ESD_TIMER, touch_dev);
	touch_dev->work_proceedure = TS_NO_WORK;
	enable_irq(touch_dev->irq);
	up(&touch_dev->work_proceedure_lock);
}

static void ts_esd_timer_start(u16 sec, struct zinitix_touch_dev *touch_dev)
{
	if (touch_dev->p_esd_timeout_tmr != NULL)
		del_timer(touch_dev->p_esd_timeout_tmr);
	touch_dev->p_esd_timeout_tmr = NULL;

	init_timer(&(touch_dev->esd_timeout_tmr));
	touch_dev->esd_timeout_tmr.data = (unsigned long)(touch_dev);
	touch_dev->esd_timeout_tmr.function = ts_esd_timeout_handler;
	touch_dev->esd_timeout_tmr.expires = jiffies + HZ*sec;
	touch_dev->p_esd_timeout_tmr = &touch_dev->esd_timeout_tmr;
	add_timer(&touch_dev->esd_timeout_tmr);
}

static void ts_esd_timer_stop(struct zinitix_touch_dev *touch_dev)
{
	if (touch_dev->p_esd_timeout_tmr)
		del_timer(touch_dev->p_esd_timeout_tmr);
	touch_dev->p_esd_timeout_tmr = NULL;
}

static void ts_esd_timer_init(struct zinitix_touch_dev *touch_dev)
{
	init_timer(&(touch_dev->esd_timeout_tmr));
	touch_dev->esd_timeout_tmr.data = (unsigned long)(touch_dev);
	touch_dev->esd_timeout_tmr.function = ts_esd_timeout_handler;
	touch_dev->p_esd_timeout_tmr = NULL;
}

static void ts_esd_timeout_handler(unsigned long data)
{
	struct zinitix_touch_dev *touch_dev = (struct zinitix_touch_dev *)data;
	touch_dev->p_esd_timeout_tmr = NULL;
	queue_work(zinitix_tmr_workqueue, &touch_dev->tmr_work);
}
#endif


bool ts_check_need_upgrade(u16 curVersion, u16 curRegVersion)
{
	u16	newVersion;
	newVersion = (u16) (m_firmware_data[0] | (m_firmware_data[1]<<8));

	printk(KERN_INFO "cur Version = 0x%x, new Version = 0x%x\n",
		curVersion, newVersion);

	if (curVersion < newVersion)
		return true;
	else if (curVersion > newVersion)
		return false;

#if BT4x2_Series
	if (m_firmware_data[0x3FFE] == 0xff
		&& m_firmware_data[0x3FFF] == 0xff)
		return false;
	newVersion =
		(u16)(m_firmware_data[0x3FFE] | (m_firmware_data[0x3FFF]<<8));
#endif

#if BT4x3_Above_Series
	if (m_firmware_data[FIRMWARE_VERSION_POS+2] == 0xff
		&& m_firmware_data[FIRMWARE_VERSION_POS+3] == 0xff)
		return false;
	newVersion = (u16)(m_firmware_data[FIRMWARE_VERSION_POS+2]
		| (m_firmware_data[FIRMWARE_VERSION_POS+3]<<8));
#endif

	if (curRegVersion < newVersion) {
		printk(KERN_INFO, "cur reg ver = %d, new ver = %d\n",
			curRegVersion, newVersion);
		return true;
	}

	return false;
}


#define	TC_PAGE_SZ		64
#define	TC_SECTOR_SZ		8

u8 ts_upgrade_firmware(struct zinitix_touch_dev *touch_dev,
	const u8 *firmware_data, u32 size)
{
	u16 flash_addr;
	u8 *verify_data;
	int retry_cnt = 0;
#if	(TOUCH_USING_ISP_METHOD == 0 && BT4x2_Series == 1)
	u32 i;
#else
	u8 i2c_buffer[TC_PAGE_SZ+2];
#endif

	verify_data = kzalloc(size, GFP_KERNEL);
	if (verify_data == NULL) {
		printk(KERN_ERR "cannot alloc verify buffer\n");
		return false;
	}

#if	(TOUCH_USING_ISP_METHOD == 0 && BT4x2_Series == 1)
	do {
		printk(KERN_INFO "reset command\n");
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS) {
			printk(KERN_INFO "failed to reset\n");
			goto fail_upgrade;
		}

#if USE_HW_CALIBRATION
		printk(KERN_INFO "Erase Flash\n");
		if (ts_write_reg(touch_dev->client,
			ZINITIX_ERASE_FLASH, 0xaaaa) != I2C_SUCCESS) {
			printk(KERN_INFO "failed to erase flash\n");
			goto fail_upgrade;
		}

		mdelay(500);
#else
		if (ts_write_reg(touch_dev->client,
			ZINITIX_TOUCH_MODE, 0x06) != I2C_SUCCESS) {
			printk(KERN_INFO "failed to erase flash\n");
			goto fail_upgrade;
		}
		ts_write_cmd(touch_dev->client, ZINITIX_CLEAR_INT_STATUS_CMD);
		mdelay(10);
		ts_write_cmd(touch_dev->client, ZINITIX_CLEAR_INT_STATUS_CMD);
		mdelay(10);
#endif

		printk(KERN_INFO "writing firmware data\n");

		for (flash_addr = 0; flash_addr < size; ) {
			for (i = 0; i < TC_PAGE_SZ/TC_SECTOR_SZ; i++) {
				zinitix_debug_msg("write :addr=%04x, len=%d\n",
					flash_addr, TC_SECTOR_SZ);
				if (ts_write_data(touch_dev->client,
					ZINITIX_WRITE_FLASH,
					(u8 *)&firmware_data[flash_addr],
					TC_SECTOR_SZ) < 0) {
					printk(KERN_INFO"error : write zinitix tc firmare\n");
					goto fail_upgrade;
				}
				flash_addr += TC_SECTOR_SZ;
				udelay(100);
			}
			mdelay(20);
#if !USE_HW_CALIBRATION
			if (flash_addr >= CALIBRATION_AREA)
				break;
#endif
		}

		printk(KERN_INFO "read firmware data\n");
		for (flash_addr = 0; flash_addr < size; ) {
			for (i = 0; i < TC_PAGE_SZ/TC_SECTOR_SZ; i++) {
				zinitix_debug_msg("read :addr=%04x, len=%d\n",
					flash_addr, TC_SECTOR_SZ);
				if (ts_read_firmware_data(touch_dev->client,
					ZINITIX_READ_FLASH,
					&verify_data[flash_addr],
					TC_SECTOR_SZ) < 0) {
					printk(KERN_INFO "error : read zinitix tc firmare\n");
					goto fail_upgrade;
				}
				flash_addr += TC_SECTOR_SZ;
			}
#if !USE_HW_CALIBRATION
			if (flash_addr >= CALIBRATION_AREA) {
				memcpy((u8 *)&verify_data[CALIBRATION_AREA],
					(u8 *)&firmware_data[CALIBRATION_AREA],
					size - CALIBRATION_AREA);
				break;
			}
#endif
		}
		/* verify */
		printk(KERN_INFO "verify firmware data\n");
		if (memcmp((u8 *)&firmware_data[0],
			(u8 *)&verify_data[0], size) == 0) {
			printk(KERN_INFO "upgrade finished\n");
			kfree(verify_data);
			ts_power_control(touch_dev, RESET_LOW);
			ts_power_control(touch_dev, POWER_OFF);
			mdelay(CHIP_POWER_OFF_DELAY);
			ts_power_control(touch_dev, POWER_ON);
			ts_power_control(touch_dev, RESET_HIGH);
			mdelay(CHIP_ON_DELAY);
			return true;
		}
		printk(KERN_INFO "upgrade fail : so retry... (%d)\n",
			++retry_cnt);

		if (retry_cnt >= ZINITIX_INIT_RETRY_CNT)
			goto fail_upgrade;
	} while (1);

#elif	(TOUCH_USING_ISP_METHOD == 1)	/* isp*/

	if (m_isp_client == NULL) {
		printk(KERN_ERR "i2c client for isp is not register \r\n");
		return false;
	}

retry_isp_firmware_upgrade:

#if BT4x2_Series
	/*must be reset pin low*/
	ts_power_control(touch_dev, RESET_LOW);
	mdelay(100);
#endif

#if BT4x3_Above_Series
	ts_power_control(touch_dev, RESET_LOW);
	ts_power_control(touch_dev, POWER_OFF);
	mdelay(1000);
	ts_power_control(touch_dev, POWER_ON);
	ts_power_control(touch_dev, RESET_HIGH);
	mdelay(1);		/* under 4ms*/
#endif
	printk(KERN_INFO "write firmware data\n");

	for (flash_addr = 0; flash_addr < size; flash_addr += TC_PAGE_SZ) {

#if !USE_HW_CALIBRATION
		if (flash_addr >= CALIBRATION_AREA*2)
			break;
#endif
		printk(KERN_INFO ".");
		zinitix_debug_msg("firmware write : addr = %04x, len = %d\n",
			flash_addr, TC_PAGE_SZ);

		i2c_buffer[0] = (flash_addr>>8)&0xff;	/*addr_h*/
		i2c_buffer[1] = (flash_addr)&0xff;	/*addr_l*/
		memcpy(&i2c_buffer[2], &firmware_data[flash_addr], TC_PAGE_SZ);

		if (ts_write_firmware_data(m_isp_client,
			i2c_buffer, TC_PAGE_SZ+2) < 0) {
			printk(KERN_INFO"error : write zinitix tc firmare\n");
			goto fail_upgrade;
		}
		mdelay(20);
	}
	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);

	printk(KERN_INFO "\r\nread firmware data\n");

#if BT4x3_Above_Series
	flash_addr = 0;
	i2c_buffer[0] = (flash_addr>>8)&0xff;	/*addr_h*/
	i2c_buffer[1] = (flash_addr)&0xff;	/*addr_l*/

#if !USE_HW_CALIBRATION
	size = CALIBRATION_AREA*2;
#endif
	if (ts_read_firmware_data(m_isp_client,
		i2c_buffer, &verify_data[flash_addr], size) < 0) {
		printk(KERN_INFO "error : read zinitix tc firmare: addr = %04x, len = %d\n",
			flash_addr, size);
		goto fail_upgrade;
	}
	if (memcmp((u8 *)&firmware_data[flash_addr],
		(u8 *)&verify_data[flash_addr], size) != 0) {
		printk(KERN_INFO "error : verify error : addr = %04x, len = %d\n",
			flash_addr, size);
		goto fail_upgrade;
	}
#else	/*bt4x2*/

	for (flash_addr = 0; flash_addr < size; flash_addr += TC_PAGE_SZ) {
#if !USE_HW_CALIBRATION
		if (flash_addr >= CALIBRATION_AREA*2)
			break;
#endif
		i2c_buffer[0] = (flash_addr>>8)&0xff;	/*addr_h*/
		i2c_buffer[1] = (flash_addr)&0xff;	/*addr_l*/
		zinitix_debug_msg("firmware read : addr = %04x, len = %d\n",
			flash_addr, TC_PAGE_SZ);
		if (ts_read_firmware_data(m_isp_client,
			i2c_buffer, &verify_data[flash_addr], TC_PAGE_SZ) < 0) {
			printk(KERN_INFO "error : read zinitix tc firmare: addr = %04x, len = %d\n",
				flash_addr, TC_PAGE_SZ);
			goto fail_upgrade;
		}
		if (memcmp((u8 *)&firmware_data[flash_addr],
			(u8 *)&verify_data[flash_addr], TC_PAGE_SZ) != 0) {
			printk(KERN_INFO "error : verify error : addr = %04x, len = %d\n",
				flash_addr, TC_PAGE_SZ);
			goto fail_upgrade;
		}
		udelay(500);
	}
#endif
	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	ts_power_control(touch_dev, RESET_LOW);
	ts_power_control(touch_dev, POWER_OFF);
	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	ts_power_control(touch_dev, POWER_ON);
	ts_power_control(touch_dev, RESET_HIGH);
	mdelay(CHIP_ON_AF_FZ_DELAY);
	printk(KERN_INFO "upgrade finishpowered\n");
	kfree(verify_data);
	return true;
#endif
fail_upgrade:

	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	ts_power_control(touch_dev, RESET_LOW);
	ts_power_control(touch_dev, POWER_OFF);
	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	ts_power_control(touch_dev, POWER_ON);
	ts_power_control(touch_dev, RESET_HIGH);
	mdelay(CHIP_ON_AF_FZ_DELAY);

#if	(TOUCH_USING_ISP_METHOD == 1)
	printk(KERN_INFO "upgrade fail : so retry... (%d)\n", ++retry_cnt);
	if (retry_cnt <= ZINITIX_INIT_RETRY_CNT)
		goto retry_isp_firmware_upgrade;
#endif
	if (verify_data != NULL)
		kfree(verify_data);
	printk(KERN_INFO "upgrade fail..\n");
	return false;

}


static bool ts_init_touch(struct zinitix_touch_dev *touch_dev)
{
	u16 reg_val;
	int i;
	u16 SetMaxX = touch_dev->cap_info.MaxX;
	u16 SetMaxY = touch_dev->cap_info.MaxY;
	u16 chip_revision;
	u16 chip_firmware_version;
	u16 chip_reg_data_version;
	u16 chip_eeprom_info;
#if	TOUCH_ONESHOT_UPGRADE
	s16 stmp;
#endif
	int retry_cnt = 0;

	if (touch_dev == NULL) {
		printk(KERN_ERR "error touch_dev == null?\r\n");
		return false;
	}


retry_init:

	zinitix_debug_msg("disable interrupt\r\n");

	for (i = 0; i < ZINITIX_INIT_RETRY_CNT; i++) {
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SWRESET_CMD) == I2C_SUCCESS)
			break;
		mdelay(10);
	}

	if (i == ZINITIX_INIT_RETRY_CNT) {
		printk(KERN_INFO "fail to write interrupt register\r\n");
		goto fail_init;
	}

#if USING_CHIP_SETTING
	if (ts_read_data(touch_dev->client,
		ZINITIX_INT_ENABLE_FLAG,
		(u8 *)&touch_dev->cap_info.chip_int_mask, 2) < 0)
		goto fail_init;
	printk(KERN_INFO "zinitix touch interrupt mask = %04x\r\n",
		touch_dev->cap_info.chip_int_mask);
	if (touch_dev->cap_info.chip_int_mask == 0
		|| touch_dev->cap_info.chip_int_mask == 0xffff)
		goto fail_init;
#else
		reg_val = 0;
		zinitix_bit_set(reg_val, BIT_PT_CNT_CHANGE);
		zinitix_bit_set(reg_val, BIT_DOWN);
		zinitix_bit_set(reg_val, BIT_MOVE);
		zinitix_bit_set(reg_val, BIT_UP);
		if (touch_dev->num_buttons > 0)
			zinitix_bit_set(reg_val, BIT_ICON_EVENT);
		touch_dev->cap_info.chip_int_mask = reg_val;
#endif


	if (ts_write_reg(touch_dev->client,
		ZINITIX_INT_ENABLE_FLAG, 0x0) != I2C_SUCCESS)
		goto fail_init;

	zinitix_debug_msg("send reset command\r\n");
	if (ts_write_cmd(touch_dev->client,
		ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
		goto fail_init;

	/* get chip revision id */
	if (ts_read_data(touch_dev->client,
		ZINITIX_CHIP_REVISION,
		(u8 *)&chip_revision, 2) < 0) {
		printk(KERN_INFO "fail to read chip revision\r\n");
		goto fail_init;
	}
	printk(KERN_INFO "zinitix touch chip revision id = %x\r\n",
		chip_revision);

	touch_dev->cap_info.chip_fw_size = 16*1024;

#if BT4x3_Above_Series
	touch_dev->cap_info.chip_fw_size = 32*1024;
#endif

#if	USE_TEST_RAW_TH_DATA_MODE
	if (ts_read_data(touch_dev->client,
		ZINITIX_TOTAL_NUMBER_OF_X,
		(u8 *)&touch_dev->cap_info.x_node_num, 2) < 0)
		goto fail_init;
	printk(KERN_INFO "zinitix touch chip x node num = %d\r\n",
		touch_dev->cap_info.x_node_num);
	if (ts_read_data(touch_dev->client,
		ZINITIX_TOTAL_NUMBER_OF_Y,
		(u8 *)&touch_dev->cap_info.y_node_num, 2) < 0)
		goto fail_init;
	printk(KERN_INFO "zinitix touch chip y node num = %d\r\n",
		touch_dev->cap_info.y_node_num);

	touch_dev->cap_info.total_node_num =
		touch_dev->cap_info.x_node_num*touch_dev->cap_info.y_node_num;
	printk(KERN_INFO "zinitix touch chip total node num = %d\r\n",
		touch_dev->cap_info.total_node_num);
#if BT4x3_Above_Series
	if (ts_read_data(touch_dev->client,
		ZINITIX_MAX_Y_NUM,
		(u8 *)&touch_dev->cap_info.max_y_node, 2) < 0)
		goto fail_init;

	printk(KERN_INFO "zinitix touch chip max y node num = %d\r\n",
		touch_dev->cap_info.max_y_node);

	if (ts_read_data(touch_dev->client,
		ZINITIX_CAL_N_TOTAL_NUM,
		(u8 *)&touch_dev->cap_info.total_cal_n, 2) < 0)
		goto fail_init;
	printk(KERN_INFO "zinitix touch chip total cal n data num = %d\r\n",
		touch_dev->cap_info.total_cal_n);

#endif
#endif


	/* get chip firmware version */
	if (ts_read_data(touch_dev->client,
		ZINITIX_FIRMWARE_VERSION,
		(u8 *)&chip_firmware_version, 2) < 0)
		goto fail_init;
	printk(KERN_INFO "zinitix touch chip firmware version = %x\r\n",
		chip_firmware_version);

#if	TOUCH_ONESHOT_UPGRADE
	chip_reg_data_version = 0xffff;

	if (ts_read_data(touch_dev->client,
		ZINITIX_DATA_VERSION_REG,
		(u8 *)&chip_reg_data_version, 2) < 0)
		goto fail_init;
	zinitix_debug_msg("touch reg data version = %d\r\n",
		chip_reg_data_version);


force_upgrade:
	printk(KERN_INFO "work_proceedure = %d\n", touch_dev->work_proceedure);

	if ((touch_dev->work_proceedure != TS_IN_RESUME) ||
		(touch_dev->work_proceedure != TS_IN_EARLY_SUSPEND)) {

		if ((ts_check_need_upgrade(chip_firmware_version,
			chip_reg_data_version) == true) || (retry_cnt > 10)) {
			printk(KERN_INFO "start upgrade firmware\n");

			ts_upgrade_firmware(touch_dev, &m_firmware_data[2],
				touch_dev->cap_info.chip_fw_size);

			/* get chip revision id */
			if (ts_read_data(touch_dev->client,
				ZINITIX_CHIP_REVISION,
				(u8 *)&chip_revision, 2) < 0) {
				printk(KERN_INFO "fail to read chip revision\r\n");
				goto fail_init;
			}
			printk(KERN_INFO "zinitix touch chip revision id = %x\r\n",
				chip_revision);

			/* get chip firmware version */
			if (ts_read_data(touch_dev->client,
				ZINITIX_FIRMWARE_VERSION,
				(u8 *)&chip_firmware_version, 2) < 0)
				goto fail_init;
			printk(KERN_INFO "zinitix touch chip renewed firmware version = %x\r\n",
				chip_firmware_version);

		}
	}
#endif

	if (ts_read_data(touch_dev->client,
		ZINITIX_DATA_VERSION_REG,
		(u8 *)&chip_reg_data_version, 2) < 0)
		goto fail_init;
	zinitix_debug_msg("touch reg data version = %d\r\n",
		chip_reg_data_version);

#if	TOUCH_ONESHOT_UPGRADE
	if (chip_reg_data_version
		< touch_dev->reg_data[ZINITIX_DATA_VERSION_REG].reg_val) {
		zinitix_debug_msg("write new reg data( %d < %d)\r\n",
			chip_reg_data_version,
			touch_dev->reg_data[ZINITIX_DATA_VERSION_REG].reg_val);
		for (i = 0; i < touch_dev->num_regs; i++) {
			struct _zinitix_reg_data *reg_data;
			reg_data = &touch_dev->reg_data[i];
			if (reg_data->valid == 1) {
				if (ts_write_reg(touch_dev->client,
					(u16)i,
					(u16)(reg_data->reg_val))
					!= I2C_SUCCESS)
					goto fail_init;
				if (i == ZINITIX_TOTAL_NUMBER_OF_X
					|| i == ZINITIX_TOTAL_NUMBER_OF_Y)
					mdelay(50);
				if (ts_read_data(touch_dev->client,
					(u16)i, (u8 *)&stmp, 2) < 0)
					goto fail_init;
				if (memcmp((char *)&reg_data->reg_val,
					(char *)&stmp, 2) < 0)
					printk(KERN_WARNING "reg data is different. (addr = 0x%02X , %d != %d)\r\n",
						i, reg_data->reg_val, stmp);
			}
		}
		zinitix_debug_msg("done new reg data( %d < %d)\r\n",
			chip_reg_data_version,
			touch_dev->reg_data[ZINITIX_DATA_VERSION_REG].reg_val);
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SAVE_STATUS_CMD) != I2C_SUCCESS)
			goto fail_init;
		mdelay(1000);	/* for fusing eeprom */
	}

#endif

	if (ts_read_data(touch_dev->client,
		ZINITIX_EEPROM_INFO_REG,
		(u8 *)&chip_eeprom_info, 2) < 0)
		goto fail_init;
	zinitix_debug_msg("touch eeprom info = 0x%04X\r\n", chip_eeprom_info);

#if	USE_HW_CALIBRATION
	if (zinitix_bit_test(chip_eeprom_info, 0)) { /* hw calibration bit*/
		 /* h/w calibration */
		if (ts_write_reg(touch_dev->client,
			ZINITIX_TOUCH_MODE, 0x07) != I2C_SUCCESS)
			goto fail_init;
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_CALIBRATE_CMD) != I2C_SUCCESS)
			goto fail_init;
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_init;
		mdelay(1);
		ts_write_cmd(touch_dev->client,
			ZINITIX_CLEAR_INT_STATUS_CMD);
		/* wait for h/w calibration*/
		do {
			mdelay(1000);
			if (ts_read_data(touch_dev->client,
				ZINITIX_EEPROM_INFO_REG,
				(u8 *)&chip_eeprom_info, 2) < 0)
				goto fail_init;
			zinitix_debug_msg("touch eeprom info = 0x%04X\r\n",
				chip_eeprom_info);
			if (!zinitix_bit_test(chip_eeprom_info, 0))
				break;
		} while (1);

		if (ts_write_reg(touch_dev->client,
			ZINITIX_TOUCH_MODE, TOUCH_MODE) != I2C_SUCCESS)
			goto fail_init;
		if (touch_dev->cap_info.chip_int_mask != 0)
			if (ts_write_reg(touch_dev->client,
				ZINITIX_INT_ENABLE_FLAG,
				touch_dev->cap_info.chip_int_mask)
				!= I2C_SUCCESS)
				goto fail_init;

		mdelay(10);
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_init;
#if BT4x3_Above_Series
		mdelay(10);
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SAVE_CALIBRATION_CMD) != I2C_SUCCESS)
			goto fail_init;
		mdelay(500);
#endif

#if BT4x2_Series
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SAVE_STATUS_CMD) != I2C_SUCCESS)
			goto fail_init;
		mdelay(1000);	/* for fusing eeprom*/
		if (ts_write_cmd(touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_init;
#endif
		/* disable chip interrupt */
		if (ts_write_reg(touch_dev->client,
			ZINITIX_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
			goto fail_init;

	}
#endif

	touch_dev->cap_info.chip_revision = (u16)chip_revision;
	touch_dev->cap_info.chip_firmware_version = (u16)chip_firmware_version;
	touch_dev->cap_info.chip_reg_data_version = (u16)chip_reg_data_version;


	/* initialize */
	if (ts_write_reg(touch_dev->client,
		ZINITIX_X_RESOLUTION,
		(u16)(SetMaxX)) != I2C_SUCCESS)
		goto fail_init;

	if (ts_write_reg(touch_dev->client,
		ZINITIX_Y_RESOLUTION,
		(u16)(SetMaxY)) != I2C_SUCCESS)
		goto fail_init;

	if (ts_read_data(touch_dev->client,
		ZINITIX_X_RESOLUTION,
		(u8 *)&touch_dev->cap_info.x_resolution, 2) < 0)
		goto fail_init;
	zinitix_debug_msg("touch max x = %d\r\n",
		touch_dev->cap_info.x_resolution);
	if (ts_read_data(touch_dev->client,
		ZINITIX_Y_RESOLUTION,
		(u8 *)&touch_dev->cap_info.y_resolution, 2) < 0)
		goto fail_init;
	zinitix_debug_msg("touch max y = %d\r\n",
		touch_dev->cap_info.y_resolution);

	touch_dev->cap_info.MinX = (u32)0;
	touch_dev->cap_info.MinY = (u32)0;
	touch_dev->cap_info.MaxX = (u32)touch_dev->cap_info.x_resolution;
	touch_dev->cap_info.MaxY = (u32)touch_dev->cap_info.y_resolution;

#if USING_CHIP_SETTING
		if (ts_read_data(touch_dev->client,
			ZINITIX_BUTTON_SUPPORTED_NUM,
			(u8 *)&touch_dev->cap_info.button_num, 2) < 0)
			goto fail_init;
		zinitix_debug_msg("supported button num = %d\r\n",
			touch_dev->cap_info.button_num);
		if (ts_read_data(touch_dev->client,
			ZINITIX_SUPPORTED_FINGER_NUM,
			(u8 *)&touch_dev->cap_info.multi_fingers, 2) < 0)
			goto fail_init;
		zinitix_debug_msg("supported finger num = %d\r\n",
			touch_dev->cap_info.multi_fingers);
#else
		touch_dev->cap_info.button_num = touch_dev->num_buttons;

		if (ts_write_reg(touch_dev->client,
			ZINITIX_SUPPORTED_FINGER_NUM,
			(u16)MAX_SUPPORTED_FINGER_NUM) != I2C_SUCCESS)
			goto fail_init;
		touch_dev->cap_info.multi_fingers = REAL_SUPPORTED_FINGER_NUM;
#endif

	zinitix_debug_msg("max supported finger num = %d, \
		real supported finger num = %d\r\n",
		touch_dev->cap_info.multi_fingers, REAL_SUPPORTED_FINGER_NUM);
	touch_dev->cap_info.gesture_support = 0;
	zinitix_debug_msg("set other configuration\r\n");

#if	USE_TEST_RAW_TH_DATA_MODE
	if (touch_dev->raw_mode_flag != TOUCH_NORMAL_MODE) {	/* Test Mode */
		if (ts_write_reg(touch_dev->client,
			ZINITIX_TOUCH_MODE,
			touch_dev->raw_mode_flag) != I2C_SUCCESS)
			goto fail_init;
	} else
#endif
{
	reg_val = TOUCH_MODE;
	if (ts_write_reg(touch_dev->client,
		ZINITIX_TOUCH_MODE,
		reg_val) != I2C_SUCCESS)
		goto fail_init;
}
	/* soft calibration */
	if (ts_write_cmd(touch_dev->client,
		ZINITIX_CALIBRATE_CMD) != I2C_SUCCESS)
		goto fail_init;
	if (ts_write_reg(touch_dev->client,
		ZINITIX_INT_ENABLE_FLAG,
		touch_dev->cap_info.chip_int_mask) != I2C_SUCCESS)
		goto fail_init;

	/* read garbage data */
	for (i = 0; i < 10; i++)	{
		ts_write_cmd(touch_dev->client, ZINITIX_CLEAR_INT_STATUS_CMD);
		udelay(10);
	}

#if	USE_TEST_RAW_TH_DATA_MODE
	if (touch_dev->raw_mode_flag != TOUCH_NORMAL_MODE) { /* Test Mode */
		if (ts_write_reg(touch_dev->client,
			ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
			ZINITIX_SCAN_RATE_HZ
			*ZINITIX_RAW_DATA_ESD_TIMER_INTERVAL) != I2C_SUCCESS)
			printk(KERN_ERR "[zinitix_touch] Fail to set ZINITIX_RAW_DATA_ESD_TIMER_INTERVAL.\r\n");
	} else
#endif
{
#if	ZINITIX_ESD_TIMER_INTERVAL
	if (ts_write_reg(touch_dev->client,
		ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
		ZINITIX_SCAN_RATE_HZ
		*ZINITIX_ESD_TIMER_INTERVAL) != I2C_SUCCESS)
		goto fail_init;
	if (ts_read_data(touch_dev->client,
		ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
		(u8 *)&reg_val, 2) < 0)
		goto fail_init;
	zinitix_debug_msg("esd timer register = %d\r\n", reg_val);
#endif
}

	zinitix_debug_msg("successfully initialized\r\n");
	return true;

fail_init:

	if (retry_cnt++ <= ZINITIX_INIT_RETRY_CNT) {
		mdelay(CHIP_POWER_OFF_DELAY);
		ts_power_control(touch_dev, RESET_LOW);
		ts_power_control(touch_dev, POWER_OFF);
		mdelay(CHIP_POWER_OFF_DELAY);
		ts_power_control(touch_dev, POWER_ON);
		ts_power_control(touch_dev, RESET_HIGH);
		mdelay(CHIP_ON_DELAY);
		zinitix_debug_msg("retry to initiallize(retry cnt = %d)\r\n",
			retry_cnt);
		goto	retry_init;
	} else {
	  mdelay(CHIP_POWER_OFF_DELAY);
	  ts_power_control(touch_dev, RESET_LOW);	/* reset pin low */
	  ts_power_control(touch_dev, POWER_OFF);	/* power off */
	  mdelay(CHIP_POWER_OFF_DELAY);
	  ts_power_control(touch_dev, POWER_ON);	/* power on */
	  ts_power_control(touch_dev, RESET_HIGH);	/* reset pin high */
	  mdelay(CHIP_ON_DELAY);
	  zinitix_debug_msg("retry init (cnt = %d)\r\n", retry_cnt);
#if TOUCH_FORCE_UPGRADE
	  if (retry_cnt > 15) {
		printk(KERN_ERR "[zinitix touch] failed to force upgrade\r\n");
		return false;
	  }
	  goto force_upgrade;
#endif
	 }

	printk(KERN_INFO "[zinitix touch] failed to initiallize\r\n");
	return false;

}


#if (TOUCH_MODE == 1)
static void	zinitix_report_data(struct zinitix_touch_dev *touch_dev, int id)
{
	int i;
	u32 tmp;
	u8 sub_status;
	u32 x, y, w;

	if (id >= touch_dev->cap_info.multi_fingers || id < 0)
		return;

	x = touch_dev->touch_info.coord[id].x;
	y = touch_dev->touch_info.coord[id].y;

	 /* transformation from touch to screen orientation */
	if (touch_dev->cap_info.Orientation & TOUCH_V_FLIP)
		y = touch_dev->cap_info.MaxY + touch_dev->cap_info.MinY - y;

	if (touch_dev->cap_info.Orientation & TOUCH_H_FLIP)
		x = touch_dev->cap_info.MaxX + touch_dev->cap_info.MinX - x;

	if (touch_dev->cap_info.Orientation & TOUCH_XY_SWAP)
		zinitix_swap_v(x, y, tmp);

	zinitix_debug_msg("x = %d, y = %d, w = %d\r\n",
		x, y, touch_dev->touch_info.coord[id].width);

	touch_dev->reported_touch_info.coord[id].x = x;
	touch_dev->reported_touch_info.coord[id].y = y;
	touch_dev->reported_touch_info.coord[id].width =
		touch_dev->touch_info.coord[id].width;
	touch_dev->reported_touch_info.coord[id].sub_status =
		touch_dev->touch_info.coord[id].sub_status;

	for (i = 0; i < touch_dev->cap_info.multi_fingers; i++) {
		sub_status =
		touch_dev->reported_touch_info.coord[i].sub_status;
		x = touch_dev->reported_touch_info.coord[i].x;
		y = touch_dev->reported_touch_info.coord[i].y;
		w = touch_dev->reported_touch_info.coord[i].width;

		if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)
			|| zinitix_bit_test(sub_status, SUB_BIT_DOWN)
			|| zinitix_bit_test(sub_status, SUB_BIT_MOVE)) {
			if (w == 0)
				w = 5;
			input_report_abs(touch_dev->input_dev,
				ABS_MT_TOUCH_MAJOR, (u32)w);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_WIDTH_MAJOR, (u32)w);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_X, x);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_Y, y);
			input_mt_sync(touch_dev->input_dev);
		} else if (zinitix_bit_test(sub_status, SUB_BIT_UP)) {
			input_report_abs(touch_dev->input_dev,
				ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_X, x);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_Y, y);
			input_mt_sync(touch_dev->input_dev);
			touch_dev->reported_touch_info.coord[i].sub_status = 0;
		} else
			touch_dev->reported_touch_info.coord[i].sub_status = 0;
	}

	input_sync(touch_dev->input_dev);
}
#endif	/* TOUCH_MODE == 1 */

static void	zinitix_clear_report_data(struct zinitix_touch_dev *touch_dev)
{
	int i;
	u8 reported = 0;
	u8 sub_status;

	for (i = 0; i < touch_dev->cap_info.multi_fingers; i++) {
		sub_status = touch_dev->reported_touch_info.coord[i].sub_status;
		if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
			input_report_abs(touch_dev->input_dev,
				ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_X,
				touch_dev->reported_touch_info.coord[i].x);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_Y,
				touch_dev->reported_touch_info.coord[i].y);
			input_mt_sync(touch_dev->input_dev);
			reported = 1;
		}
		touch_dev->reported_touch_info.coord[i].sub_status = 0;
	}
	if (reported)
		input_sync(touch_dev->input_dev);
}


static void zinitix_touch_work(struct work_struct *work)
{
	int i;
	u8 reported = false;
	u8 sub_status;
	u32 x, y;
#if (TOUCH_MODE == 0)
	u32 w;
	u32 tmp;
#endif
	struct zinitix_touch_dev *touch_dev =
		container_of(work, struct zinitix_touch_dev, work);

	zinitix_debug_msg("zinitix_touch_thread : semaphore signalled\r\n");

#if	ZINITIX_ESD_TIMER_INTERVAL
	if (touch_dev->use_esd_timer) {
		ts_esd_timer_stop(touch_dev);
		zinitix_debug_msg("esd timer stop\r\n");
	}
#endif
	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_NO_WORK) {
		zinitix_debug_msg("zinitix_touch_thread : \
			[warning] other process occupied..\r\n");
#if DELAY_FOR_SIGNAL_DELAY
		udelay(DELAY_FOR_SIGNAL_DELAY);
#endif
		if (!gpio_get_value(touch_dev->int_gpio_num)) {
			ts_write_cmd(touch_dev->client,
				ZINITIX_CLEAR_INT_STATUS_CMD);
#if DELAY_FOR_SIGNAL_DELAY
			udelay(DELAY_FOR_SIGNAL_DELAY);
#endif
		}
		goto continue_read_samples;
	}
	touch_dev->work_proceedure = TS_NORMAL_WORK;
	if (ts_read_coord(touch_dev) == false) {
		zinitix_debug_msg("couldn't read touch_dev sample\r\n");
		goto continue_read_samples;
	}

#if	USE_TEST_RAW_TH_DATA_MODE
	if (touch_dev->raw_mode_flag == TOUCH_TEST_RAW_MODE)
		goto continue_read_samples;
#endif

	/* invalid : maybe periodical repeated int. */
	if (touch_dev->touch_info.status == 0x0)
		goto continue_read_samples;

	reported = false;

	if (zinitix_bit_test(touch_dev->touch_info.status, BIT_ICON_EVENT)) {
		for (i = 0; i < touch_dev->cap_info.button_num; i++) {
			if (zinitix_bit_test(touch_dev->icon_event_reg,
				(BIT_O_ICON0_DOWN+i))) {
				touch_dev->button[i] = ICON_BUTTON_DOWN;
				input_report_key(touch_dev->input_dev,
					touch_dev->BUTTON_MAPPING_KEY[i], 1);
				reported = true;
				zinitix_debug_msg("button down = %d \r\n", i);
			}
		}

		for (i = 0; i < touch_dev->cap_info.button_num; i++) {
			if (zinitix_bit_test(touch_dev->icon_event_reg,
				(BIT_O_ICON0_UP+i))) {
				touch_dev->button[i] = ICON_BUTTON_UP;
				input_report_key(touch_dev->input_dev,
					touch_dev->BUTTON_MAPPING_KEY[i], 0);
				reported = true;
				zinitix_debug_msg("button up = %d \r\n", i);
			}
		}
	}

	/* if button press or up event occured... */
	if (reported == true) {
#if (TOUCH_MODE == 1)
		/* input_sync(touch_dev->input_dev); */
		for (i = 0; i < touch_dev->cap_info.multi_fingers; i++) {
			sub_status =
			touch_dev->reported_touch_info.coord[i].sub_status;
			x = touch_dev->reported_touch_info.coord[i].x;
			y = touch_dev->reported_touch_info.coord[i].y;
			if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
				input_report_abs(touch_dev->input_dev,
					ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_WIDTH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_X, x);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_Y, y);
				input_mt_sync(touch_dev->input_dev);
			}
			touch_dev->reported_touch_info.coord[i].sub_status = 0;
		}
		input_sync(touch_dev->input_dev);
		/*goto continue_read_samples;*/
	}
#else
		for (i = 0; i < touch_dev->cap_info.multi_fingers; i++)	{
			sub_status =
			touch_dev->reported_touch_info.coord[i].sub_status;
			x = touch_dev->reported_touch_info.coord[i].x;
			y = touch_dev->reported_touch_info.coord[i].y;
			if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
				/*input_report_abs(touch_dev->input_dev,
					ABS_MT_TRACKING_ID,i);*/
				input_report_abs(touch_dev->input_dev,
					ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_WIDTH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_X, x);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_Y, y);
				input_mt_sync(touch_dev->input_dev);
			}
		}
		memset(&touch_dev->reported_touch_info,
			0x0, sizeof(struct _ts_zinitix_point_info));
		input_sync(touch_dev->input_dev);
		udelay(100);
		goto continue_read_samples;
	}
	if (touch_dev->touch_info.finger_cnt > MAX_SUPPORTED_FINGER_NUM)
		touch_dev->touch_info.finger_cnt = MAX_SUPPORTED_FINGER_NUM;

	if (!zinitix_bit_test(touch_dev->touch_info.status, BIT_PT_EXIST)) {
		for (i = 0; i < touch_dev->cap_info.multi_fingers; i++) {
			sub_status =
			touch_dev->reported_touch_info.coord[i].sub_status;
			x = touch_dev->reported_touch_info.coord[i].x;
			y = touch_dev->reported_touch_info.coord[i].y;
			if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
				/*input_report_abs(touch_dev->input_dev,
					ABS_MT_TRACKING_ID,i);*/
				input_report_abs(touch_dev->input_dev,
				ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_WIDTH_MAJOR, 0);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_X, x);
				input_report_abs(touch_dev->input_dev,
					ABS_MT_POSITION_Y, y);
				input_mt_sync(touch_dev->input_dev);
			}
		}
		memset(&touch_dev->reported_touch_info,
			0x0, sizeof(struct _ts_zinitix_point_info));
		input_sync(touch_dev->input_dev);
		goto continue_read_samples;
	}

	for (i = 0; i < touch_dev->cap_info.multi_fingers; i++)	{
		sub_status = touch_dev->touch_info.coord[i].sub_status;

		if (zinitix_bit_test(sub_status, SUB_BIT_DOWN)
			|| zinitix_bit_test(sub_status, SUB_BIT_MOVE)
			|| zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {

			x = touch_dev->touch_info.coord[i].x;
			y = touch_dev->touch_info.coord[i].y;
			w = touch_dev->touch_info.coord[i].width;

			 /* transformation from touch to screen orientation */
			if (touch_dev->cap_info.Orientation & TOUCH_V_FLIP)
				y = touch_dev->cap_info.MaxY
					+ touch_dev->cap_info.MinY - y;
			if (touch_dev->cap_info.Orientation & TOUCH_H_FLIP)
				x = touch_dev->cap_info.MaxX
					+ touch_dev->cap_info.MinX - x;
			if (touch_dev->cap_info.Orientation & TOUCH_XY_SWAP)
				zinitix_swap_v(x, y, tmp);
			touch_dev->touch_info.coord[i].x = x;
			touch_dev->touch_info.coord[i].y = y;
			zinitix_debug_msg("finger [%02d] x = %d, y = %d \r\n",
				i, x, y);

			/*input_report_abs(touch_dev->input_dev,
				ABS_MT_TRACKING_ID,i); */
			if (w == 0)
				w = 5;
			input_report_abs(touch_dev->input_dev,
				ABS_MT_TOUCH_MAJOR, (u32)w);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_WIDTH_MAJOR, (u32)w);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_X, x);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_Y, y);
			input_mt_sync(touch_dev->input_dev);
		} else if (zinitix_bit_test(sub_status, SUB_BIT_UP)) {
			zinitix_debug_msg("finger [%02d] up \r\n", i);
			x = touch_dev->reported_touch_info.coord[i].x;
			y = touch_dev->reported_touch_info.coord[i].y;

			memset(&touch_dev->touch_info.coord[i],
				0x0, sizeof(struct _ts_zinitix_coord));
			/*input_report_abs(touch_dev->input_dev,
				ABS_MT_TRACKING_ID,i);*/
			input_report_abs(touch_dev->input_dev,
				ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_X, x);
			input_report_abs(touch_dev->input_dev,
				ABS_MT_POSITION_Y, y);
			input_mt_sync(touch_dev->input_dev);
		} else
			memset(&touch_dev->touch_info.coord[i],
				0x0, sizeof(struct _ts_zinitix_coord));

	}
	memcpy((char *)&touch_dev->reported_touch_info,
		(char *)&touch_dev->touch_info,
		sizeof(struct _ts_zinitix_point_info));
	input_sync(touch_dev->input_dev);

#endif	/* TOUCH_MODE == 1 */

continue_read_samples:

	/* check_interrupt_pin, if high, enable int & wait signal */
	if (touch_dev->work_proceedure == TS_NORMAL_WORK) {
#if	ZINITIX_ESD_TIMER_INTERVAL
		if (touch_dev->use_esd_timer) {
			ts_esd_timer_start(ZINITIX_CHECK_ESD_TIMER, touch_dev);
			zinitix_debug_msg("esd tmr start\n");
		}
#endif
		touch_dev->work_proceedure = TS_NO_WORK;
	}
	up(&touch_dev->work_proceedure_lock);
	enable_irq(touch_dev->irq);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void zinitix_late_resume(struct early_suspend *h)
{

	struct zinitix_touch_dev *touch_dev;
	touch_dev = container_of(h, struct zinitix_touch_dev, early_suspend);

	if (touch_dev == NULL)
		return;

	zinitix_debug_msg("early_resume++\r\n");

	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_IN_RESUME
		&& touch_dev->work_proceedure != TS_IN_EARLY_SUSPEND) {
		printk(KERN_INFO"zinitix_late_resume : invalid work proceedure (%d)\r\n",
			touch_dev->work_proceedure);
		up(&touch_dev->work_proceedure_lock);
		return;
	}
	ts_write_cmd(touch_dev->client, ZINITIX_WAKEUP_CMD);
	mdelay(10);
	if (ts_mini_init_touch(touch_dev) == false)
		goto fail_resume;
	enable_irq(touch_dev->irq);
	touch_dev->work_proceedure = TS_NO_WORK;
	up(&touch_dev->work_proceedure_lock);
	zinitix_debug_msg("early_resume--\n");
	return;
fail_resume:
	printk(KERN_ERR "failed to resume\n");
	enable_irq(touch_dev->irq);
	touch_dev->work_proceedure = TS_NO_WORK;
	up(&touch_dev->work_proceedure_lock);
	return;
}


static void zinitix_early_suspend(struct early_suspend *h)
{
	struct zinitix_touch_dev *touch_dev =
		container_of(h, struct zinitix_touch_dev, early_suspend);

	if (touch_dev == NULL)
		return;

	zinitix_debug_msg("early suspend++\n");

#if	ZINITIX_ESD_TIMER_INTERVAL
	flush_work(&touch_dev->tmr_work);
#endif

	flush_work(&touch_dev->work);

	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_NO_WORK) {
		printk(KERN_INFO"zinitix_early_suspend : invalid work proceedure (%d)\r\n",
			touch_dev->work_proceedure);
		up(&touch_dev->work_proceedure_lock);
		return;
	}
	touch_dev->work_proceedure = TS_IN_EARLY_SUSPEND;

	zinitix_debug_msg("clear all reported points\r\n");
	zinitix_clear_report_data(touch_dev);

#if	ZINITIX_ESD_TIMER_INTERVAL
	if (touch_dev->use_esd_timer) {
		ts_write_reg(touch_dev->client,
			ZINITIX_PERIODICAL_INTERRUPT_INTERVAL, 0);
		ts_esd_timer_stop(touch_dev);
		printk(KERN_INFO " ts_esd_timer_stop\n");
	}
#endif

	disable_irq(touch_dev->irq);
#if !(USING_CHIP_SETTING)
	ts_write_reg(touch_dev->client, ZINITIX_INT_ENABLE_FLAG, 0x0);
#endif
	udelay(100);
	ts_write_cmd(touch_dev->client, ZINITIX_CLEAR_INT_STATUS_CMD);
	if (ts_write_cmd(touch_dev->client, ZINITIX_SLEEP_CMD) != I2C_SUCCESS) {
		printk(KERN_ERR "failed to enter into sleep mode\n");
		up(&touch_dev->work_proceedure_lock);
		return;
	}
	zinitix_debug_msg("early suspend--\n");
	up(&touch_dev->work_proceedure_lock);
	return;
}

#endif	/* CONFIG_HAS_EARLYSUSPEND */



static int zinitix_resume(struct i2c_client *client)
{
	struct zinitix_touch_dev *touch_dev = i2c_get_clientdata(client);

	if (!strcmp(client->name, ZINITIX_ISP_NAME))
		return 0;	/* handled by main driver */

	zinitix_debug_msg("resume++\n");
	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_IN_SUSPEND) {
		printk(KERN_INFO"zinitix_resume : invalid work proceedure (%d)\r\n",
			touch_dev->work_proceedure);
		up(&touch_dev->work_proceedure_lock);
		return 0;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	touch_dev->work_proceedure = TS_IN_RESUME;
#else
	touch_dev->work_proceedure = TS_NO_WORK;
#endif

/*
	ts_power_control(touch_dev, POWER_ON);
	ts_power_control(touch_dev, RESET_HIGH);
	mdelay(CHIP_ON_DELAY);
*/
	zinitix_debug_msg("resume--\n");
	up(&touch_dev->work_proceedure_lock);
	return 0;

}

static int zinitix_suspend(struct i2c_client *client, pm_message_t message)
{
	struct zinitix_touch_dev *touch_dev = i2c_get_clientdata(client);

	if (!strcmp(client->name, ZINITIX_ISP_NAME))
		return 0;	/* handled by main driver */

	zinitix_debug_msg("suspend++\n");
	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_NO_WORK
		&& touch_dev->work_proceedure != TS_IN_EARLY_SUSPEND) {
		printk(KERN_INFO"zinitix_suspend : invalid work proceedure (%d)\r\n",
			touch_dev->work_proceedure);
		up(&touch_dev->work_proceedure_lock);
		return 0;
	}
/*
	ts_power_control(touch_dev, RESET_LOW);
	ts_power_control(touch_dev, POWER_OFF);
	mdelay(CHIP_POWER_OFF_DELAY);
*/
	touch_dev->work_proceedure = TS_IN_SUSPEND;
	zinitix_debug_msg("suspend--\n");
	up(&touch_dev->work_proceedure_lock);
	return 0;
}

#if	USE_TEST_RAW_TH_DATA_MODE
static ssize_t zinitix_get_test_raw_data(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int total_n_num;
	if (misc_touch_dev == NULL) {
		zinitix_debug_msg("device NULL : NULL\n");
		return 0;
	}
	down(&misc_touch_dev->raw_data_lock);
	total_n_num = misc_touch_dev->cap_info.total_node_num;
	if (zinitix_bit_test(
		misc_touch_dev->cur_data[total_n_num], BIT_PT_EXIST)) {
		/*x_lsb*/
		buf[20] =
			(misc_touch_dev->cur_data[total_n_num+1]&0xff);
		/*x_msb*/
		buf[21] =
			((misc_touch_dev->cur_data[total_n_num+1]>>8)&0xff);
		/*y_lsb*/
		buf[22] =
			(misc_touch_dev->cur_data[total_n_num+2]&0xff);
		/*y_msb*/
		buf[23] =
			((misc_touch_dev->cur_data[total_n_num+2]
			>>8)&0xff);
	} else {
		buf[20] = 0; /*x_lsb*/
		buf[21] = 0; /*x_msb*/
		buf[22] = 0; /*y_lsb*/
		buf[23] = 0; /*y_msb*/
	}

	/*lsb*/
	buf[0] =
		(char)(misc_touch_dev->ref_data[22]&0xff);
	/*msb*/
	buf[1] =
		(char)((misc_touch_dev->ref_data[22]>>8)&0xff);
	/*delta lsb*/
	buf[2] =
		(char)(((s16)(misc_touch_dev->cur_data[22]
		- misc_touch_dev->ref_data[22]))&0xff);
	/*delta msb*/
	buf[3] =
		(char)((((s16)(misc_touch_dev->cur_data[22]
		- misc_touch_dev->ref_data[22]))>>8)&0xff);
	/*
	buf[4] =
		(char)(misc_touch_dev->ref_data[51]&0xff);
	buf[5] =
		(char)((misc_touch_dev->ref_data[51]>>8)&0xff);
	buf[6] =
		(char)(((s16)(misc_touch_dev->cur_data[51]
		-misc_touch_dev->ref_data[51]))&0xff);
	buf[7] =
		(char)((((s16)(misc_touch_dev->cur_data[51]
		-misc_touch_dev->ref_data[51]))>>8)&0xff);

	buf[8] =
		(char)(misc_touch_dev->ref_data[102]&0xff);
	buf[9] =
		(char)((misc_touch_dev->ref_data[102]>>8)&0xff);
	buf[10] =
		(char)(((s16)(misc_touch_dev->cur_data[102]
		-misc_touch_dev->ref_data[102]))&0xff);
	buf[11] =
		(char)((((s16)(misc_touch_dev->cur_data[102]
		-misc_touch_dev->ref_data[102]))>>8)&0xff);

	buf[12] =
		(char)(misc_touch_dev->ref_data[169]&0xff);
	buf[13] =
		(char)((misc_touch_dev->ref_data[169]>>8)&0xff);
	buf[14] =
		(char)(((s16)(misc_touch_dev->cur_data[169]
		-misc_touch_dev->ref_data[169]))&0xff);
	buf[15] =
		(char)((((s16)(misc_touch_dev->cur_data[169]
		-misc_touch_dev->ref_data[169]))>>8)&0xff);

	buf[16] =
		(char)(misc_touch_dev->ref_data[178]&0xff);
	buf[17] =
		(char)((misc_touch_dev->ref_data[178]>>8)&0xff);
	buf[18] =
		(char)(((s16)(misc_touch_dev->cur_data[178]
		-misc_touch_dev->ref_data[178]))&0xff);
	buf[19] =
		(char)((((s16)(misc_touch_dev->cur_data[178]
		- misc_touch_dev->ref_data[178]))>>8)&0xff);
	*/
	up(&misc_touch_dev->raw_data_lock);


	return 24;
}


ssize_t zinitix_set_testmode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned char value = 0;

	printk(KERN_INFO "[zinitix_touch] zinitix_set_testmode, buf = %d\r\n",
		*buf);

	if (misc_touch_dev == NULL) {
		zinitix_debug_msg("device NULL : NULL\n");
		return 0;
	}

	sscanf(buf, "%c", &value);

	if (value != TOUCH_TEST_RAW_MODE && value != TOUCH_NORMAL_MODE) {
		printk(KERN_WARNING "[zinitix ts] test mode setting value error. you must set %d[=normal] or %d[=raw mode]\r\n",
			TOUCH_NORMAL_MODE, TOUCH_TEST_RAW_MODE);
		return 1;
	}

	down(&misc_touch_dev->raw_data_lock);
	misc_touch_dev->raw_mode_flag = value;

	printk(KERN_INFO "[zinitix_touch] zinitix_set_testmode, touchkey_testmode = %d\r\n",
		misc_touch_dev->raw_mode_flag);

	if (misc_touch_dev->raw_mode_flag == TOUCH_NORMAL_MODE) {
		/* enter into normal mode */
		printk(KERN_INFO "[zinitix_touch] TEST Mode Exit\r\n");

		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
			ZINITIX_SCAN_RATE_HZ
			*ZINITIX_ESD_TIMER_INTERVAL) != I2C_SUCCESS)
			printk(KERN_INFO "[zinitix_touch] Fail to set ZINITIX_PERIODICAL_INTERRUPT_INTERVAL.\r\n");

		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_TOUCH_MODE, TOUCH_MODE) != I2C_SUCCESS)
			printk(KERN_INFO "[zinitix_touch] Fail to set touch mode %d.\r\n",
				TOUCH_MODE);

		/* clear garbage data */
		ts_write_cmd(misc_touch_dev->client,
			ZINITIX_CLEAR_INT_STATUS_CMD);
		mdelay(100);
		ts_write_cmd(misc_touch_dev->client,
			ZINITIX_CLEAR_INT_STATUS_CMD);
	} else {
		/* enter into test mode */
		printk(KERN_INFO "[zinitix_touch] TEST Mode Enter\r\n");

		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
			ZINITIX_SCAN_RATE_HZ
			*ZINITIX_RAW_DATA_ESD_TIMER_INTERVAL) != I2C_SUCCESS)
			printk(KERN_INFO "[zinitix_touch] Fail to set ZINITIX_RAW_DATA_ESD_TIMER_INTERVAL.\r\n");

		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_TOUCH_MODE, TOUCH_TEST_RAW_MODE) != I2C_SUCCESS)
			printk(KERN_INFO "[zinitix_touch] Fail to set touch mode %d.\r\n",
				TOUCH_TEST_RAW_MODE);
		ts_write_cmd(misc_touch_dev->client,
			ZINITIX_CLEAR_INT_STATUS_CMD);
		/* clear garbage data */
		mdelay(100);
		ts_write_cmd(misc_touch_dev->client,
			ZINITIX_CLEAR_INT_STATUS_CMD);
		memset(&misc_touch_dev->reported_touch_info,
			0x0, sizeof(struct _ts_zinitix_point_info));
		memset(&misc_touch_dev->touch_info,
			0x0, sizeof(struct _ts_zinitix_point_info));
	}
	up(&misc_touch_dev->raw_data_lock);
	return 1;

}

static DEVICE_ATTR(get_touch_raw_data,
	S_IRUGO | S_IWUSR | S_IWOTH | S_IXOTH,
	zinitix_get_test_raw_data,
	zinitix_set_testmode);


static int ts_misc_fops_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ts_misc_fops_close(struct inode *inode, struct file *filp)
{
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static int ts_misc_fops_ioctl(struct inode *inode,
	struct file *filp, unsigned int cmd,
	unsigned long arg)
#else
static long ts_misc_fops_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
#endif
{
	void __user *argp = (void __user *)arg;
	struct _raw_ioctl raw_ioctl;
	u8 *u8Data;
	int i, j;
	int ret = 0;
	size_t sz = 0;
	u16 version;
#if	BT4x2_Series
	unsigned int version_pos;
#else
	u8 div_node;
	int total_cal_n;
#endif
	u16 mode;
	u16 chip_eeprom_info;
	struct _reg_ioctl reg_ioctl;
	u16 val;
	int nval = 0;

	if (misc_touch_dev == NULL)
		return -1;

	/* zinitix_debug_msg("cmd = %d, argp = 0x%x\n", cmd, (int)argp); */

	switch (cmd) {

	case TOUCH_IOCTL_GET_DEBUGMSG_STATE:
		ret = m_ts_debug_mode;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_SET_DEBUGMSG_STATE:
		if (copy_from_user(&nval, argp, 4)) {
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}
		if (nval)
			printk(KERN_INFO "[zinitix_touch] on debug mode (%d)\n",
				nval);
		else
			printk(KERN_INFO "[zinitix_touch] off debug mode (%d)\n",
				nval);
		m_ts_debug_mode = nval;
		break;

	case TOUCH_IOCTL_GET_CHIP_REVISION:
		ret = misc_touch_dev->cap_info.chip_revision;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_FW_VERSION:
		ret = misc_touch_dev->cap_info.chip_firmware_version;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_REG_DATA_VERSION:
		ret = misc_touch_dev->cap_info.chip_reg_data_version;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_SIZE:
		if (copy_from_user(&sz, argp, sizeof(size_t)))
			return -1;

		printk(KERN_INFO "firmware size = %d\r\n", sz);
		if (misc_touch_dev->cap_info.chip_fw_size != sz) {
			printk(KERN_INFO "firmware size error\r\n");
			return -1;
		}
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_DATA:
		if (copy_from_user(&m_firmware_data[2],
			argp, misc_touch_dev->cap_info.chip_fw_size))
			return -1;
#if	BT4x2_Series
		version_pos =
			(unsigned int)((((unsigned int)m_firmware_data[4]<<8)
			&0xff00)|(m_firmware_data[5]&0xff));
		version =
			(u16)(((u16)m_firmware_data[version_pos+2+2]<<8)
			|(u16)m_firmware_data[version_pos+2+1]);
#endif
#if	BT4x3_Above_Series
		version =
			(u16)(((u16)m_firmware_data[FIRMWARE_VERSION_POS+1]<<8)
			|(u16)m_firmware_data[FIRMWARE_VERSION_POS]);
#endif
		printk(KERN_INFO "firmware version = %x\r\n", version);

		if (copy_to_user(argp, &version, sizeof(version)))
			return -1;
		break;

	case TOUCH_IOCTL_START_UPGRADE:
		disable_irq(misc_touch_dev->irq);
		down(&misc_touch_dev->work_proceedure_lock);
		misc_touch_dev->work_proceedure = TS_IN_UPGRADE;
#if	ZINITIX_ESD_TIMER_INTERVAL
		if (misc_touch_dev->use_esd_timer)
			ts_esd_timer_stop(misc_touch_dev);
#endif
		zinitix_debug_msg("clear all reported points\r\n");
		zinitix_clear_report_data(misc_touch_dev);

		printk(KERN_INFO "start upgrade firmware\n");
		if (ts_upgrade_firmware(misc_touch_dev,
			&m_firmware_data[2],
			misc_touch_dev->cap_info.chip_fw_size) == false) {
			enable_irq(misc_touch_dev->irq);
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}

		if (ts_init_touch(misc_touch_dev) == false) {
			enable_irq(misc_touch_dev->irq);
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}

#if	ZINITIX_ESD_TIMER_INTERVAL
		if (misc_touch_dev->use_esd_timer) {
			ts_esd_timer_start(ZINITIX_CHECK_ESD_TIMER,
				misc_touch_dev);
			zinitix_debug_msg("esd timer start\r\n");
		}
#endif
		enable_irq(misc_touch_dev->irq);
		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		break;

	case TOUCH_IOCTL_GET_X_RESOLUTION:
		ret = misc_touch_dev->cap_info.x_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_RESOLUTION:
		ret = misc_touch_dev->cap_info.y_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_X_NODE_NUM:
		ret = misc_touch_dev->cap_info.x_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_NODE_NUM:
		ret = misc_touch_dev->cap_info.y_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_TOTAL_NODE_NUM:
		ret = misc_touch_dev->cap_info.total_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_HW_CALIBRAION:
		ret = -1;
		disable_irq(misc_touch_dev->irq);
		down(&misc_touch_dev->work_proceedure_lock);
		if (misc_touch_dev->work_proceedure != TS_NO_WORK) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_touch_dev->work_proceedure);
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}
		misc_touch_dev->work_proceedure = TS_HW_CALIBRAION;

		/* h/w calibration */
		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_TOUCH_MODE, 0x07) != I2C_SUCCESS)
			goto fail_hw_cal;
		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_CALIBRATE_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;
		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;
		mdelay(1);
		ts_write_cmd(misc_touch_dev->client,
			ZINITIX_CLEAR_INT_STATUS_CMD);
		/* wait for h/w calibration */
		do {
			mdelay(1000);
			if (ts_read_data(misc_touch_dev->client,
				ZINITIX_EEPROM_INFO_REG,
				(u8 *)&chip_eeprom_info, 2) < 0)
				goto fail_hw_cal;
			zinitix_debug_msg("touch eeprom info = 0x%04X\r\n",
				chip_eeprom_info);
			if (!zinitix_bit_test(chip_eeprom_info, 0))
				break;
		} while (1);

	#if BT4x3_Above_Series
		mdelay(10);
		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_SAVE_CALIBRATION_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;
		mdelay(500);
	#endif
		ret = 0;
fail_hw_cal:
		if (misc_touch_dev->raw_mode_flag == TOUCH_NORMAL_MODE)
			mode = TOUCH_MODE;
		else
			mode = misc_touch_dev->raw_mode_flag;
		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_TOUCH_MODE, mode) != I2C_SUCCESS) {
			printk(KERN_INFO "fail to set touch mode %d.\n",
				mode);
			goto fail_hw_cal2;
		}

		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal2;

	#if BT4x2_Series
		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_SAVE_STATUS_CMD) != I2C_SUCCESS)
			goto fail_hw_cal2;
		mdelay(1000);	/* for fusing eeprom */
		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal2;
	#endif
		enable_irq(misc_touch_dev->irq);
		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return ret;
fail_hw_cal2:
		enable_irq(misc_touch_dev->irq);
		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return -1;

	case TOUCH_IOCTL_SET_RAW_DATA_MODE:
		if (misc_touch_dev == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}

		down(&misc_touch_dev->work_proceedure_lock);
		if (misc_touch_dev->work_proceedure != TS_NO_WORK) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_touch_dev->work_proceedure);
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}
		misc_touch_dev->work_proceedure = TS_SET_MODE;

		if (copy_from_user(&nval, argp, 4)) {
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\r\n");
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			return -1;
		}

		zinitix_debug_msg("[zinitix_touch] touchkey_testmode = %d\r\n",
			misc_touch_dev->raw_mode_flag);

		if (nval == TOUCH_NORMAL_MODE &&
			misc_touch_dev->raw_mode_flag != TOUCH_NORMAL_MODE) {
			/* enter into normal mode */
			misc_touch_dev->raw_mode_flag = nval;
			printk(KERN_INFO "[zinitix_touch] raw data mode exit\r\n");

			if (ts_write_reg(misc_touch_dev->client,
				ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
				ZINITIX_SCAN_RATE_HZ*ZINITIX_ESD_TIMER_INTERVAL)
				!= I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] Fail to set ZINITIX_PERIODICAL_INTERRUPT_INTERVAL.\n");

			if (ts_write_reg(misc_touch_dev->client,
				ZINITIX_TOUCH_MODE, TOUCH_MODE) != I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] fail to set TOUCH_MODE.\r\n");

			/* clear garbage data */
			ts_write_cmd(misc_touch_dev->client,
				ZINITIX_CLEAR_INT_STATUS_CMD);
			mdelay(100);
			ts_write_cmd(misc_touch_dev->client,
				ZINITIX_CLEAR_INT_STATUS_CMD);
		} else if (nval != TOUCH_NORMAL_MODE) {
			/* enter into test mode*/
			misc_touch_dev->raw_mode_flag = nval;
			printk(KERN_INFO "[zinitix_touch] raw data mode enter\r\n");

			if (ts_write_reg(misc_touch_dev->client,
				ZINITIX_PERIODICAL_INTERRUPT_INTERVAL,
				ZINITIX_SCAN_RATE_HZ
				*ZINITIX_RAW_DATA_ESD_TIMER_INTERVAL)
				!= I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] Fail to set ZINITIX_RAW_DATA_ESD_TIMER_INTERVAL.\n");

			if (ts_write_reg(misc_touch_dev->client,
				ZINITIX_TOUCH_MODE,
				misc_touch_dev->raw_mode_flag) != I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] raw data mode : Fail to set TOUCH_MODE %d.\n",
					misc_touch_dev->raw_mode_flag);

			ts_write_cmd(misc_touch_dev->client,
				ZINITIX_CLEAR_INT_STATUS_CMD);
			/* clear garbage data */
			mdelay(100);
			ts_write_cmd(misc_touch_dev->client,
				ZINITIX_CLEAR_INT_STATUS_CMD);
		}

		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return 0;

	case TOUCH_IOCTL_GET_REG:
		if (misc_touch_dev == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_touch_dev->work_proceedure_lock);
		if (misc_touch_dev->work_proceedure != TS_NO_WORK) {
			printk(KERN_INFO "other process occupied.. (%d)\n",
				misc_touch_dev->work_proceedure);
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}

		misc_touch_dev->work_proceedure = TS_SET_MODE;

		if (copy_from_user(&reg_ioctl,
			argp, sizeof(struct _reg_ioctl))) {
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			up(&misc_touch_dev->work_proceedure_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (ts_read_data(misc_touch_dev->client,
			reg_ioctl.addr, (u8 *)&val, 2) < 0)
			ret = -1;

		nval = (int)val;

		if (copy_to_user(reg_ioctl.val, (u8 *)&nval, 4)) {
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			up(&misc_touch_dev->work_proceedure_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_to_user\n");
			return -1;
		}

		zinitix_debug_msg("read : reg addr = 0x%x, val = 0x%x\n",
			reg_ioctl.addr, nval);

		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return ret;

	case TOUCH_IOCTL_SET_REG:

		if (misc_touch_dev == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_touch_dev->work_proceedure_lock);
		if (misc_touch_dev->work_proceedure != TS_NO_WORK) {
			printk(KERN_INFO "other process occupied.. (%d)\n",
				misc_touch_dev->work_proceedure);
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}

		misc_touch_dev->work_proceedure = TS_SET_MODE;
		if (copy_from_user(&reg_ioctl,
				argp, sizeof(struct _reg_ioctl))) {
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			up(&misc_touch_dev->work_proceedure_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (copy_from_user(&val, reg_ioctl.val, 4)) {
			misc_touch_dev->work_proceedure = TS_NO_WORK;
			up(&misc_touch_dev->work_proceedure_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (ts_write_reg(misc_touch_dev->client,
			reg_ioctl.addr, val) != I2C_SUCCESS)
			ret = -1;

		zinitix_debug_msg("write : reg addr = 0x%x, val = 0x%x\r\n",
			reg_ioctl.addr, val);
		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return ret;

	case TOUCH_IOCTL_DONOT_TOUCH_EVENT:

		if (misc_touch_dev == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_touch_dev->work_proceedure_lock);
		if (misc_touch_dev->work_proceedure != TS_NO_WORK) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_touch_dev->work_proceedure);
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}

		misc_touch_dev->work_proceedure = TS_SET_MODE;
		if (ts_write_reg(misc_touch_dev->client,
			ZINITIX_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
			ret = -1;
		zinitix_debug_msg("write : reg addr = 0x%x, val = 0x0\r\n",
			ZINITIX_INT_ENABLE_FLAG);

		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return ret;

	case TOUCH_IOCTL_SEND_SAVE_STATUS:
		if (misc_touch_dev == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_touch_dev->work_proceedure_lock);
		if (misc_touch_dev->work_proceedure != TS_NO_WORK) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_touch_dev->work_proceedure);
			up(&misc_touch_dev->work_proceedure_lock);
			return -1;
		}
		misc_touch_dev->work_proceedure = TS_SET_MODE;
		ret = 0;
		if (ts_write_cmd(misc_touch_dev->client,
			ZINITIX_SAVE_STATUS_CMD) != I2C_SUCCESS)
			ret = -1;
		mdelay(1000);	/* for fusing eeprom */

		misc_touch_dev->work_proceedure = TS_NO_WORK;
		up(&misc_touch_dev->work_proceedure_lock);
		return ret;

	case TOUCH_IOCTL_GET_RAW_DATA:
		if (misc_touch_dev == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}

		if (misc_touch_dev->raw_mode_flag == TOUCH_NORMAL_MODE)
			return -1;

		down(&misc_touch_dev->raw_data_lock);
		if (misc_touch_dev->update == 0) {
			up(&misc_touch_dev->raw_data_lock);
			return -2;
		}

		if (copy_from_user(&raw_ioctl,
			argp, sizeof(raw_ioctl))) {
			up(&misc_touch_dev->raw_data_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\r\n");
			return -1;
		}

		misc_touch_dev->update = 0;

		u8Data = (u8 *)&misc_touch_dev->cur_data[0];
		if (misc_touch_dev->raw_mode_flag == TOUCH_ZINITIX_CAL_N_MODE) {
#if BT4x2_Series
			j = 0;
			u8Data = (u8 *)&misc_touch_dev->cur_data[1];
			for (i = 0; i < 160; i++) {
				if ((i*2)%16 <
					misc_touch_dev->cap_info.y_node_num) {
					misc_touch_dev->ref_data[j*2] =
						misc_touch_dev->cur_data[0]
						- (u16)u8Data[i*2];
					misc_touch_dev->ref_data[j*2+1] =
						misc_touch_dev->cur_data[0]
						- (u16)u8Data[i*2+1];
					j++;
				}
			}
#endif

#if BT4x3_Above_Series
			j = 0;
			total_cal_n =
				misc_touch_dev->cap_info.total_cal_n;
			if (total_cal_n == 0)
				total_cal_n = 160;
			div_node =
				(u8)misc_touch_dev->cap_info.max_y_node;
			if (div_node == 0)
				div_node = 16;
			u8Data = (u8 *)&misc_touch_dev->cur_data[0];
			for (i = 0; i < total_cal_n; i++) {
				if ((i*2)%div_node <
					misc_touch_dev->cap_info.y_node_num) {
					misc_touch_dev->ref_data[j*2] =
						(u16)u8Data[i*2];
					misc_touch_dev->ref_data[j*2+1]	=
						(u16)u8Data[i*2+1];
					j++;
				}
			}
#endif
			u8Data = (u8 *)&misc_touch_dev->ref_data[0];
		}

		if (copy_to_user(raw_ioctl.buf, (u8 *)u8Data,
			raw_ioctl.sz)) {
			up(&misc_touch_dev->raw_data_lock);
			return -1;
		}

		up(&misc_touch_dev->raw_data_lock);
		return 0;

	default:
		break;
	}
	return 0;
}

#endif /* USE_TEST_RAW_TH_DATA_MODE */


static int zinitix_touch_probe(struct i2c_client *client,
		const struct i2c_device_id *i2c_id)
{
	int ret;
	struct zinitix_touch_dev *touch_dev;
	struct zinitix_touch_platform_data *pdata = client->dev.platform_data;
	int i;

	zinitix_debug_msg("zinitix_touch_probe+\r\n");
#if	BT4x2_Series
	zinitix_debug_msg("BT4x2 Driver\r\n");
#endif

#if	BT4x3_Above_Series
	zinitix_debug_msg("Above BT4x3 Driver\r\n");
#endif
	printk(KERN_INFO "[zinitix touch] driver version = %s\r\n",
		TS_DRIVER_VERSION);

#if	TOUCH_USING_ISP_METHOD
	if (strcmp(client->name, ZINITIX_ISP_NAME) == 0) {
		printk(KERN_INFO "isp client probe \r\n");
		m_isp_client = client;
		return 0;
	}
#endif
	zinitix_debug_msg("i2c check function \r\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_ERR "error : not compatible i2c function \r\n");
		ret = -ENODEV;
		goto err_check_functionality;
	}

	zinitix_debug_msg("touch data alloc \r\n");
	touch_dev = kzalloc(sizeof(struct zinitix_touch_dev), GFP_KERNEL);
	if (!touch_dev) {
		printk(KERN_ERR "unabled to allocate touch data \r\n");
		ret = -ENOMEM;
		goto err_alloc_dev_data;
	}
	touch_dev->client = client;
	i2c_set_clientdata(client, touch_dev);

	/* copy required data across from platform data */
	touch_dev->cap_info.MaxX	= pdata->x_max - 1;
	touch_dev->cap_info.MaxY	= pdata->y_max - 1;
	touch_dev->num_regs		= pdata->num_regs;
	touch_dev->reg_data		= pdata->reg_data;
	touch_dev->num_buttons		= pdata->num_buttons;

	if (gpio_is_valid(pdata->reset_gpio)) {
		if (gpio_request(pdata->reset_gpio, "zinitix_reset_pin")) {
			printk(KERN_ERR "error : could not obtain gpio for reset pin\r\n");
			touch_dev->reset_gpio_num = -ENODEV;
		} else {
			gpio_direction_output(pdata->reset_gpio, 1);
			touch_dev->reset_gpio_num = pdata->reset_gpio;
			ts_power_control(touch_dev, RESET_HIGH);
		}

		mdelay(CHIP_ON_DELAY);
	}

	if (gpio_is_valid(pdata->tsp_ldo_en)) {
		if (gpio_request(pdata->tsp_ldo_en, "zinitix_ldo_pin")) {
			printk(KERN_ERR "error : could not obtain gpio for ldo pin\r\n");
			touch_dev->tsp_ldo_en = -ENODEV;
		} else {
			gpio_direction_output(pdata->tsp_ldo_en, 1);
			touch_dev->tsp_ldo_en = pdata->tsp_ldo_en;
		}
	}

	INIT_WORK(&touch_dev->work, zinitix_touch_work);

	zinitix_debug_msg("touch workqueue create \r\n");
	zinitix_workqueue = create_singlethread_workqueue("zinitix_workqueue");
	if (!zinitix_workqueue) {
		printk(KERN_ERR "unabled to create touch thread \r\n");
		ret = -1;
		goto err_kthread_create_failed;
	}


	zinitix_debug_msg("allocate input device \r\n");
	touch_dev->input_dev = input_allocate_device();
	if (touch_dev->input_dev == 0) {
		printk(KERN_ERR "unabled to allocate input device \r\n");
		ret = -ENOMEM;
		goto err_input_allocate_device;
	}

	/* initialize zinitix touch ic */
	memset(&touch_dev->reported_touch_info,
		0x0, sizeof(struct _ts_zinitix_point_info));

#if	USE_TEST_RAW_TH_DATA_MODE
	/*	not test mode */
	touch_dev->raw_mode_flag = TOUCH_NORMAL_MODE;
#endif

	ts_init_touch(touch_dev);

	touch_dev->use_esd_timer = 0;

#if	ZINITIX_ESD_TIMER_INTERVAL
	INIT_WORK(&touch_dev->tmr_work, zinitix_touch_tmr_work);
	zinitix_tmr_workqueue =
		create_singlethread_workqueue("zinitix_tmr_workqueue");
	if (!zinitix_tmr_workqueue) {
		printk(KERN_ERR "unabled to create touch tmr work queue \r\n");
		ret = -1;
		goto err_kthread_create_failed;
	}

	touch_dev->use_esd_timer = 1;
	ts_esd_timer_init(touch_dev);
	ts_esd_timer_start(ZINITIX_CHECK_ESD_TIMER, touch_dev);
	printk(KERN_INFO " ts_esd_timer_start\n");
#endif

	sprintf(touch_dev->phys, "input(ts)");
	touch_dev->input_dev->name = pdata->platform_name;
	touch_dev->input_dev->id.bustype = BUS_I2C;
	touch_dev->input_dev->id.vendor = 0x0001;
	touch_dev->input_dev->phys = touch_dev->phys;
	touch_dev->input_dev->id.product = 0x0002;
	touch_dev->input_dev->id.version = 0x0100;

	set_bit(EV_SYN, touch_dev->input_dev->evbit);
	set_bit(EV_KEY, touch_dev->input_dev->evbit);
	set_bit(BTN_TOUCH, touch_dev->input_dev->keybit);
	set_bit(EV_ABS, touch_dev->input_dev->evbit);

	for (i = 0; i < touch_dev->num_buttons; i++) {
		touch_dev->BUTTON_MAPPING_KEY[i] = pdata->BUTTON_MAPPING_KEY[i];
		set_bit(touch_dev->BUTTON_MAPPING_KEY[i],
			touch_dev->input_dev->keybit);
	}

	touch_dev->cap_info.Orientation = pdata->orientation;
	if (touch_dev->cap_info.Orientation & TOUCH_XY_SWAP) {
		input_set_abs_params(touch_dev->input_dev, ABS_MT_POSITION_Y,
			touch_dev->cap_info.MinX,
			touch_dev->cap_info.MaxX + ABS_PT_OFFSET,
			0, 0);
		input_set_abs_params(touch_dev->input_dev, ABS_MT_POSITION_X,
			touch_dev->cap_info.MinY,
			touch_dev->cap_info.MaxY + ABS_PT_OFFSET,
			0, 0);
	} else {
		input_set_abs_params(touch_dev->input_dev, ABS_MT_POSITION_X,
			touch_dev->cap_info.MinX,
			touch_dev->cap_info.MaxX + ABS_PT_OFFSET,
			0, 0);
		input_set_abs_params(touch_dev->input_dev, ABS_MT_POSITION_Y,
			touch_dev->cap_info.MinY,
			touch_dev->cap_info.MaxY + ABS_PT_OFFSET,
			0, 0);
	}

	input_set_abs_params(touch_dev->input_dev, ABS_TOOL_WIDTH,
		0, 255, 0, 0);
	input_set_abs_params(touch_dev->input_dev, ABS_MT_TOUCH_MAJOR,
		0, 255, 0, 0);
	input_set_abs_params(touch_dev->input_dev, ABS_MT_WIDTH_MAJOR,
		0, 255, 0, 0);

	zinitix_debug_msg("register %s input device \r\n",
		touch_dev->input_dev->name);
	ret = input_register_device(touch_dev->input_dev);
	if (ret) {
		printk(KERN_ERR "unable to register %s input device\r\n",
			touch_dev->input_dev->name);
		goto err_input_register_device;
	}

	/* configure touchscreen interrupt gpio */
	touch_dev->int_gpio_num = pdata->irq_gpio;	/*	for upgrade */
	ret = gpio_request(touch_dev->int_gpio_num, "zinitix_irq_gpio");
	if (ret) {
		printk(KERN_ERR "unable to request gpio.(%s)\r\n",
			touch_dev->input_dev->name);
		goto err_request_irq;
	}
	ret = gpio_direction_input(touch_dev->int_gpio_num);

	if (client->irq < 0) {
		touch_dev->irq = gpio_to_irq(touch_dev->int_gpio_num);
		if (touch_dev->irq < 0)
			printk(KERN_INFO "error. gpio_to_irq(..) not \
				supported? define I2C Client IRQ.\r\n");
	} else {
		touch_dev->irq = client->irq;
	}

	zinitix_debug_msg("request irq (irq = %d, pin = %d) \r\n",
		touch_dev->irq, touch_dev->int_gpio_num);

	touch_dev->work_proceedure = TS_NO_WORK;
	sema_init(&touch_dev->work_proceedure_lock, 1);

	if (touch_dev->irq) {
		ret = request_irq(touch_dev->irq, ts_int_handler,
			pdata->irq_trigger, ZINITIX_DRIVER_NAME, touch_dev);
		if (ret) {
			printk(KERN_ERR "unable to register irq.(%s)\r\n",
				touch_dev->input_dev->name);
			goto err_request_irq;
		}
	}
	dev_info(&client->dev, "zinitix touch probe.\r\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	touch_dev->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	touch_dev->early_suspend.suspend = zinitix_early_suspend;
	touch_dev->early_suspend.resume = zinitix_late_resume;
	register_early_suspend(&touch_dev->early_suspend);
#endif

#if	USE_TEST_RAW_TH_DATA_MODE
	sema_init(&touch_dev->raw_data_lock, 1);

	misc_touch_dev = touch_dev;

	ret = misc_register(&touch_misc_device);
	if (ret)
		zinitix_debug_msg("Fail to register touch misc device.\n");

	if (device_create_file(touch_misc_device.this_device,
		&dev_attr_get_touch_raw_data) < 0)
		printk(KERN_INFO "Failed to create device file(%s)!\n",
			dev_attr_get_touch_raw_data.attr.name);

#endif


	return 0;

err_request_irq:
	input_unregister_device(touch_dev->input_dev);
err_input_register_device:
	input_free_device(touch_dev->input_dev);
err_kthread_create_failed:
err_input_allocate_device:
	kfree(touch_dev);
err_alloc_dev_data:
err_check_functionality:

	return ret;
}


static int zinitix_touch_remove(struct i2c_client *client)
{
	struct zinitix_touch_dev *touch_dev = i2c_get_clientdata(client);

	zinitix_debug_msg("zinitix_touch_remove+ \r\n");
	down(&touch_dev->work_proceedure_lock);
	if (touch_dev->work_proceedure != TS_NO_WORK)
		flush_work(&touch_dev->work);

	touch_dev->work_proceedure = TS_REMOVE_WORK;

#if	ZINITIX_ESD_TIMER_INTERVAL
	if (touch_dev->use_esd_timer != 0) {
		flush_work(&touch_dev->tmr_work);
		ts_write_reg(touch_dev->client,
			ZINITIX_PERIODICAL_INTERRUPT_INTERVAL, 0);
		ts_esd_timer_stop(touch_dev);
		zinitix_debug_msg(KERN_INFO " ts_esd_timer_stop\n");
		destroy_workqueue(zinitix_tmr_workqueue);
	}
#endif
	if (touch_dev->irq)
		free_irq(touch_dev->irq, touch_dev);

#if USE_TEST_RAW_TH_DATA_MODE
	device_remove_file(touch_misc_device.this_device,
		&dev_attr_get_touch_raw_data);
	misc_deregister(&touch_misc_device);
#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&touch_dev->early_suspend);
#endif

	destroy_workqueue(zinitix_workqueue);

	if (gpio_is_valid(touch_dev->int_gpio_num) != 0)
		gpio_free(touch_dev->int_gpio_num);

	if (gpio_is_valid(touch_dev->reset_gpio_num))
		gpio_free(touch_dev->reset_gpio_num);

	if (gpio_is_valid(touch_dev->tsp_ldo_en))
		gpio_free(touch_dev->tsp_ldo_en);

	input_unregister_device(touch_dev->input_dev);
	input_free_device(touch_dev->input_dev);
	up(&touch_dev->work_proceedure_lock);
	kfree(touch_dev);

	return 0;
}

static int __devinit zinitix_touch_init(void)
{
#if	TOUCH_USING_ISP_METHOD
	m_isp_client = NULL;
#endif
	return i2c_add_driver(&zinitix_touch_driver);
}

static void __exit zinitix_touch_exit(void)
{
	i2c_del_driver(&zinitix_touch_driver);
}

module_init(zinitix_touch_init);
module_exit(zinitix_touch_exit);

MODULE_DESCRIPTION("touch-screen device driver using i2c interface");
MODULE_AUTHOR("sohnet <seonwoong.jang@zinitix.com>");
MODULE_LICENSE("GPL");


