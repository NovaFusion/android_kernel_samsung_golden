

/*
 *
 * Zinitix bt404 touchscreen driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
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

#define TSP_DEBUG
/* #define TSP_VERBOSE_DEBUG */
#define TSP_FACTORY

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/timer.h>
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
#include <linux/input/mt.h>
#include <linux/regulator/consumer.h>
#ifdef TSP_FACTORY
#include <linux/list.h>
#endif
#include <linux/input/bt404_ts.h>
#include "zinitix_touch_bt4x3_firmware.h"

#define	TS_DRIVER_VERSION			"3.0.16"
#define	TOUCH_MODE				0
#define	USING_CHIP_SETTING			0
#define	MAX_SUPPORTED_FINGER_NUM		10
#define	REAL_SUPPORTED_FINGER_NUM		10

/* Upgrade Method */
#define	TOUCH_ONESHOT_UPGRADE			1
#define	TOUCH_FORCE_UPGRADE			1

/* Power Control */
#define	RESET_CONTROL				0
#define	USE_HW_CALIBRATION			1

/* resolution offset */
#define	ABS_PT_OFFSET				1

#define	CHIP_POWER_OFF_DELAY			250	/* ms */
#define	CHIP_POWER_OFF_AF_FZ_DELAY		1000	/* ms */
#define	CHIP_ON_DELAY				100	/* ms */
#define	CHIP_ON_AF_FZ_DELAY			250	/* ms */
#define	DELAY_FOR_SIGNAL_DELAY			30	/* us */
#define	DELAY_FOR_TRANSCATION			50
#define	DELAY_FOR_POST_TRANSCATION		10

#define	TOUCH_USING_ISP_METHOD			1

#define	FW_VER_OFFSET				0x6410

/*
 * ESD Protection
 * 0 : no use, when using 3 is recommended
 */
#define	BT404_ESD_TIMER_INTERVAL	0	/* second */

#define	BT404_SCAN_RATE_HZ			60
#define	BT404_CHECK_ESD_TIMER			2

/* Test Mode (Monitoring Raw Data) */
#define	USE_TEST_RAW_TH_DATA_MODE		0

#define	MAX_TEST_RAW_DATA			320	/* 20 x 16 */
	/* status register + x + y  */
#define	MAX_TEST_POINT_INFO			3
#define	MAX_RAW_DATA	(MAX_TEST_RAW_DATA + \
			MAX_TEST_POINT_INFO * MAX_SUPPORTED_FINGER_NUM + 2)

/* preriod raw data interval  */
#define	BT404_RAW_DATA_ESD_TIMER_INTERVAL	1
#define	TOUCH_TEST_RAW_MODE			51
#define	TOUCH_NORMAL_MODE			48
#define	BT404_BASELINED_DATA			3
#define	BT404_PROCESSED_DATA			4
#define	BT404_CAL_N_DATA			7
#define	BT404_CAL_N_COUNT			8

/* Other Things  */
#define	BT404_INIT_RETRY_CNT		3
#define	BT404_INIT_RETRY_LIMIT		5
#define	I2C_SUCCESS			0
#define	INIT_RETRY_COUNT		2

/* Register Map */
#define BT404_SWRESET_CMD		0x0000
#define BT404_WAKEUP_CMD		0x0001
#define BT404_IDLE_CMD			0x0004
#define BT404_SLEEP_CMD			0x0005
#define	BT404_CLEAR_INT_STATUS_CMD	0x0003
#define	BT404_CAL_CMD			0x0006
#define	BT404_SAVE_STATUS_CMD		0x0007
#define	BT404_SAVE_CAL_CMD		0x08
#define	BT404_RECALL_FACTORY_CMD	0x000f
#define BT404_TOUCH_MODE		0x0010
#define BT404_IC_REV			0x0011
#define BT404_FW_VER			0x0012
#define	BT404_REG_VER			0x0013
#define BT404_TSP_TYPE			0x0014
#define BT404_SUPPORTED_FINGER_NUM	0x0015
#define	BT404_MAX_Y_NUM			0x0016
#define BT404_EEPROM_INFO		0x0018
#define BT404_CAL_N_TOTAL_NUM		0x001B
#define BT404_THRESHOLD			0x0020
#define BT404_TOTAL_NUMBER_OF_X		0x0060
#define BT404_TOTAL_NUMBER_OF_Y		0x0061
#define	BT404_BUTTON_SUPPORTED_NUM	0xB0
#define	BT404_X_RESOLUTION		0x00C0
#define	BT404_Y_RESOLUTION		0x00C1
#define	BT404_POINT_STATUS_REG		0x0080
#define	BT404_POINT_REG			0x0082
#define	BT404_ICON_STATUS_REG		0x00A0
#define	BT404_FW_CHECKSUM		0x00AC
#define	BT404_RAWDATA_REG		0x0200
#define	BT404_EEPROM_INFO_REG		0x0018

/* 0xF0 */
#define	BT404_INT_ENABLE_FLAG			0x00f0
#define	BT404_PERIODICAL_INTERRUPT_INTERVAL	0x00f1

/* Interrupt & status register flag bit */
/*-------------------------------------------------*/
#define	BIT_PT_CNT_CHANGE			0
#define	BIT_DOWN				1
#define	BIT_MOVE				2
#define	BIT_UP					3
#define	BIT_HOLD				4
#define	BIT_LONG_HOLD				5
#define	RESERVED_0				6
#define	RESERVED_1				7
#define	BIT_WEIGHT_CHANGE			8
#define	BIT_PT_NO_CHANGE			9
#define	BIT_REJECT				10
#define	BIT_PT_EXIST				11	/* status reg only */
/*-------------------------------------------------*/
#define	RESERVED_2				12
#define	RESERVED_3				13
#define	RESERVED_4				14
#define	BIT_ICON_EVENT				15

/* 4 icon */
#define	BIT_O_ICON0_DOWN			0
#define	BIT_O_ICON1_DOWN			1
#define	BIT_O_ICON2_DOWN			2
#define	BIT_O_ICON3_DOWN			3
#define	BIT_O_ICON4_DOWN			4
#define	BIT_O_ICON5_DOWN			5
#define	BIT_O_ICON6_DOWN			6
#define	BIT_O_ICON7_DOWN			7

#define	BIT_O_ICON0_UP				8
#define	BIT_O_ICON1_UP				9
#define	BIT_O_ICON2_UP				10
#define	BIT_O_ICON3_UP				11
#define	BIT_O_ICON4_UP				12
#define	BIT_O_ICON5_UP				13
#define	BIT_O_ICON6_UP				14
#define	BIT_O_ICON7_UP				15

#define	SUB_BIT_EXIST			0		/* status reg only */
#define	SUB_BIT_DOWN			1
#define	SUB_BIT_MOVE			2
#define	SUB_BIT_UP			3
#define	SUB_BIT_UPDATE			4
#define	SUB_BIT_WAIT			5

#define	bt404_bit_set(val, n)		((val) &= ~(1 << (n)), \
					(val) |= (1 << (n)))
#define	bt404_bit_clr(val, n)		((val) &= ~(1 << (n)))
#define	bt404_bit_test(val, n)	((val) & (1 << (n)))
#define bt404_swap_v(a, b, t)	((t) = (a), (a) = (b), (b) = (t))
#define bt404_swap_16(s) (((((s) & 0xff) << 8) | (((s) >> 8) & 0xff)))

#define	TC_PAGE_SZ		64
#define	TC_SECTOR_SZ		8

#define	BT404_DEBUG		0
static int m_ts_debug_mode = BT404_DEBUG;
#define	debug_msg(fmt, args...)	\
	if (m_ts_debug_mode)	\
		printk(KERN_INFO "bt404:[%-18s:%5d]" fmt, \
		__func__, __LINE__, ## args);

#ifdef TSP_FACTORY
#define TSP_CMD_STR_LEN		32
#define TSP_CMD_RESULT_STR_LEN	512
#define TSP_CMD_PARAM_NUM	8
#define TSP_CMD_X_NUM		18 /* touch key channel is excluded */
#define TSP_CMD_Y_NUM		10
#define TSP_CMD_NODE_NUM	180 /* 18x10 */
#endif

enum power_control {
	POWER_OFF,
	POWER_ON,
};

/* Button Enum */
enum button_event {
	ICON_BUTTON_UNCHANGE,
	ICON_BUTTON_DOWN,
	ICON_BUTTON_UP,
};

struct _raw_ioctl {
	int	sz;
	u8	*buf;
};

struct _reg_ioctl {
	int	addr;
	int	*val;
};

struct _ts_zinitix_coord {
	u16	x;
	u16	y;
	u8	width;
	u8	sub_status;
};

struct _ts_zinitix_point_info {
	u16	status;
	u8	finger_cnt;
	u8	time_stamp;
	struct _ts_zinitix_coord	coord[MAX_SUPPORTED_FINGER_NUM];
};

struct _ts_capa_info {
	u16 ic_rev;
	u16 ic_fw_ver;
	u16 ic_reg_ver;
	u16 x_resolution;
	u16 y_resolution;
	u32 fw_len;
	u32 x_max;
	u32 y_max;
	u32 x_min;
	u32 y_min;
	u32 orientation;
	u8 gesture_support;
	u16 max_finger;
	u16 button_num;
	u16 chip_int_mask;
	u16 x_node_num;
	u16 y_node_num;
	u16 total_node_num;
	u16 max_y_node;
	u16 total_cal_n;
};

enum _ts_work_state {
	NOTHING = 0,
	NORMAL,
	ESD_TIMER,
	EARLY_SUSPEND,
	SUSPEND,
	RESUME,
	LATE_RESUME,
	UPGRADE,
	REMOVE,
	SET_MODE,
	HW_CAL,
};

struct bt404_ts_data {
	struct i2c_client		*client;
	struct i2c_client		*isp_client;
	struct input_dev		*input_dev_ts;
	struct input_dev		*input_dev_tk;
	struct bt404_ts_platform_data	*pdata;
	struct work_struct		work;
	struct semaphore		update_lock;
	struct _ts_capa_info		cap_info;
	struct _ts_zinitix_point_info	touch_info;
	struct _ts_zinitix_point_info	reported_touch_info;
	struct regulator		*reg_3v3;
	struct regulator		*reg_1v8;
	char				phys_ts[32];
	char				phys_tk[32];
	bool				enabled;
	u16				reported_key_val;
	u16				event_type;
	u32				irq;

	u8				button[MAX_SUPPORTED_BUTTON_NUM];
	u8				work_state;
	struct semaphore		work_lock;

	u8				use_esd_timer;
	u8				*fw_data;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend		early_suspend;
#endif

#if	USE_TEST_RAW_TH_DATA_MODE
	struct semaphore		raw_data_lock;
	u16				raw_mode_flag;
	s16				ref_data[MAX_TEST_RAW_DATA];
	s16				cur_data[MAX_RAW_DATA];
	u8				update;
#endif
#ifdef TSP_FACTORY
	struct list_head		cmd_list_head;
	u8				cmd_state;
	char				cmd[TSP_CMD_STR_LEN];
	int				cmd_param[TSP_CMD_PARAM_NUM];
	char				cmd_result[TSP_CMD_RESULT_STR_LEN];
	char				cmd_buff[TSP_CMD_RESULT_STR_LEN];
	struct mutex			cmd_lock;
	bool				cmd_is_running;
	u16				raw_cal_n_data[TSP_CMD_NODE_NUM];
	u16				raw_cal_n_count[TSP_CMD_NODE_NUM];
	u16				raw_processed_data[TSP_CMD_NODE_NUM];
#endif
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bt404_ts_early_suspend(struct early_suspend *h);
static void bt404_ts_late_resume(struct early_suspend *h);
#endif

#ifdef TSP_FACTORY

#define TSP_CMD(name, func) .cmd_name = name, .cmd_func = func
#define TOSTRING(x) #x

enum {
	WAITING = 0,
	RUNNING,
	OK,
	FAIL,
	NOT_APPLICABLE,
};

struct tsp_cmd {
	struct list_head	list;
	const char		*cmd_name;
	void			(*cmd_func)(void *device_data);
};

static void fw_update(void *device_data);
static void get_fw_ver_bin(void *device_data);
static void get_fw_ver_ic(void *device_data);
static void get_threshold(void *device_data);
static void module_off_master(void *device_data);
static void module_on_master(void *device_data);
static void get_chip_vendor(void *device_data);
static void get_chip_name(void *device_data);
static void get_x_num(void *device_data);
static void get_y_num(void *device_data);
static void run_cal_n_data_read(void *device_data);
static void run_cal_n_count_read(void *device_data);
static void run_processed_data_read(void *device_data);
static void get_cal_n_data(void *device_data);
static void get_cal_n_count(void *device_data);
static void get_processed_data(void *device_data);
static void not_support_cmd(void *device_data);

struct tsp_cmd tsp_cmds[] = {
	{TSP_CMD("fw_update", fw_update),},
	{TSP_CMD("get_fw_ver_bin", get_fw_ver_bin),},
	{TSP_CMD("get_fw_ver_ic", get_fw_ver_ic),},
	{TSP_CMD("get_config_ver", not_support_cmd),},
	{TSP_CMD("get_threshold", get_threshold),},
	{TSP_CMD("module_off_master", module_off_master),},
	{TSP_CMD("module_on_master", module_on_master),},
	{TSP_CMD("module_off_slave", not_support_cmd),},
	{TSP_CMD("module_on_slave", not_support_cmd),},
	{TSP_CMD("get_chip_vendor", get_chip_vendor),},
	{TSP_CMD("get_chip_name", get_chip_name),},
	{TSP_CMD("get_x_num", get_x_num),},
	{TSP_CMD("get_y_num", get_y_num),},
	{TSP_CMD("run_cal_n_data_read", run_cal_n_data_read),},
	{TSP_CMD("run_cal_n_count_read", run_cal_n_count_read),},
	{TSP_CMD("run_processed_data_read", run_processed_data_read),},
	{TSP_CMD("get_cal_n_data", get_cal_n_data),},
	{TSP_CMD("get_cal_n_count", get_cal_n_count),},
	{TSP_CMD("get_processed_data", get_processed_data),},
	{TSP_CMD("not_support_cmd", not_support_cmd),},
};
#endif

static int bt404_ts_resume_device(struct bt404_ts_data *data);
static bool bt404_ts_init_device(struct bt404_ts_data *data, bool force_update);
static void bt404_ts_report_touch_data(struct bt404_ts_data *data,
							bool force_clear);
/* define i2c sub functions*/
static s32 bt404_ts_read_data(struct i2c_client *client, u16 reg, u8 *val,
								u16 len)
{
	s32 ret;

	/* select register*/
	ret = i2c_master_send(client, (u8 *)&reg, 2);
	if (ret < 0)
		return ret;
	/* for setup tx transaction. */
	udelay(DELAY_FOR_TRANSCATION);

	ret = i2c_master_recv(client, val, len);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);

	return len;
}

static s32 bt404_ts_write_data(struct i2c_client *client, u16 reg, u8 *val,
								u16 len)
{
	s32 ret;
	u8 pkt[4];

	pkt[0] = (reg) & 0xff;
	pkt[1] = (reg >> 8) & 0xff;
	pkt[2] = val[0];
	pkt[3] = val[1];

	ret = i2c_master_send(client, pkt, len + 2);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);

	return len;
}

static s32 bt404_ts_write_reg(struct i2c_client *client, u16 reg, u16 val)
{
	s32 ret;

	ret = bt404_ts_write_data(client, reg, (u8 *)&val, 2);
	if (ret < 0)
		return ret;

	return I2C_SUCCESS;
}

static s32 bt404_ts_write_cmd(struct i2c_client *client, u16 reg)
{
	s32 ret;

	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);

	return I2C_SUCCESS;
}

static s32 bt404_ts_read_raw_data(struct i2c_client *client, u16 reg, u8 *val,
								u16 len)
{
	s32 ret;

	/* select register */
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0)
		return ret;

	/* for setup tx transaction. */
	udelay(200);

	ret = i2c_master_recv(client , val , len);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);

	return len;
}

inline s32 ts_read_firmware_data(struct i2c_client *client, char *addr,
							u8 *val, u16 len)
{
	s32 ret;

	if (addr) {
		/* select register*/
		ret = i2c_master_send(client, addr, 2);
		if (ret < 0)
			return ret;
		/* for setup tx transaction. */
		mdelay(1);
	}
	ret = i2c_master_recv(client, val, len);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);

	return len;
}

inline s32 ts_write_firmware_data(struct i2c_client *client, u8 *val, u16 len)
{
	s32 ret;

	ret = i2c_master_send(client, val, len);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);

	return len;
}

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

struct bt404_ts_data *misc_data;

#endif /*USE_TEST_RAW_TH_DATA_MODE */

#if	USE_TEST_RAW_TH_DATA_MODE
static bool ts_get_raw_data(struct bt404_ts_data *data)
{
	u32 total_node = data->cap_info.total_node_num;
	u32 sz;
	u16 udata;
	down(&data->raw_data_lock);
	if (data->raw_mode_flag == TOUCH_TEST_RAW_MODE) {
		sz = (total_node * 2 + MAX_TEST_POINT_INFO * 2);

		if (bt404_ts_read_raw_data(data->client, BT404_RAWDATA_REG,
					(char *)data->cur_data, sz) < 0) {
			printk(KERN_INFO "error : read zinitix tc raw data\n");
			up(&data->raw_data_lock);
			return false;
		}

		/* no point, so update ref_data*/
		udata = data->cur_data[total_node];

		if (!bt404_bit_test(udata, BIT_ICON_EVENT)
			&& !bt404_bit_test(udata, BIT_PT_EXIST))
			memcpy((u8 *)data->ref_data,
				(u8 *)data->cur_data, total_node * 2);

		data->update = 1;
		memcpy((u8 *)(&data->touch_info),
			(u8 *)&data->cur_data[total_node],
			sizeof(struct _ts_zinitix_point_info));
		up(&data->raw_data_lock);
		return true;

	}  else if (data->raw_mode_flag != TOUCH_NORMAL_MODE) {
		debug_msg("read raw data\n");
		sz = total_node * 2 + sizeof(struct _ts_zinitix_point_info);

		if (data->raw_mode_flag == BT404_CAL_N_COUNT) {
			int total_cal_n = data->cap_info.total_cal_n;
			if (total_cal_n == 0)
				total_cal_n = 160;

			sz = (total_cal_n * 2 +
					sizeof(struct _ts_zinitix_point_info));

			if (bt404_ts_read_raw_data(data->client,
					BT404_RAWDATA_REG,
					(char *)data->cur_data, sz) < 0) {
				printk(KERN_INFO
					"error : read zinitix tc raw data\n");
				up(&data->raw_data_lock);
				return false;
			}
			misc_data->update = 1;
			memcpy((u8 *)(&data->touch_info),
				(u8 *)&data->cur_data[total_cal_n],
				sizeof(struct _ts_zinitix_point_info));
			up(&data->raw_data_lock);
			return true;
		}

		if (bt404_ts_read_raw_data(data->client,
			BT404_RAWDATA_REG,
			(char *)data->cur_data, sz) < 0) {
			printk(KERN_INFO "error : read zinitix tc raw data\n");
			up(&data->raw_data_lock);
			return false;
		}

		udata = data->cur_data[total_node];
		if (!bt404_bit_test(udata, BIT_ICON_EVENT)
			&& !bt404_bit_test(udata, BIT_PT_EXIST))
			memcpy((u8 *)data->ref_data,
				(u8 *)data->cur_data, total_node*2);
		data->update = 1;
		memcpy((u8 *)(&data->touch_info),
			(u8 *)&data->cur_data[total_node],
			sizeof(struct _ts_zinitix_point_info));
	}
	up(&data->raw_data_lock);
	return true;
}
#endif



static void bt404_ts_power(struct bt404_ts_data *data, u8 ctl)
{

	if (ctl == POWER_OFF) {
		if (data->pdata->power_con == LDO_CON) {
			gpio_direction_output(data->pdata->gpio_ldo_en, 0);
		} else if (data->pdata->power_con == PMIC_CON) {
			regulator_disable(data->reg_1v8);
			regulator_disable(data->reg_3v3);
		}
	} else if (ctl == POWER_ON) {
		if (data->pdata->power_con == LDO_CON) {
			gpio_direction_output(data->pdata->gpio_ldo_en, 1);
		} else if (data->pdata->power_con == PMIC_CON) {
			regulator_enable(data->reg_3v3);
			regulator_enable(data->reg_1v8);
		}
	}

#ifdef VERBOSE_DEBUG
	dev_info(&data->client->dev, "power %s\n", (ctl) ? "on" : "off");
#endif
}


bool bt404_check_fw_update(struct bt404_ts_data *data)
{
	u16 ver_ic, ver_new;
	int ret;

	/* get chip firmware version */
	ret = bt404_ts_read_data(data->client, BT404_FW_VER, (u8 *)&ver_ic, 2);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s: err: rd (fw ver)\n",
								__func__);
		goto out;
	}

	ver_new = (u16) (data->fw_data[0] | (data->fw_data[1] << 8));

	dev_info(&data->client->dev,
		"fw ver: cur= 0x%X, new= 0x%X\n", ver_ic, ver_new);

	if (ver_ic != 0x86 && ver_ic != 0x88) {
		dev_err(&data->client->dev, "%s: invalid fw version."
						"force update.\n", __func__);
		return true;
	}

	if (ver_ic < ver_new)
		return true;
	else if (ver_ic > ver_new)
		return false;


	ver_ic = 0xffff;
	ret = bt404_ts_read_data(data->client, BT404_REG_VER,
					(u8 *)&ver_ic, 2);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s: err: rd (reg ver)\n",
								__func__);
		goto out;
	}

	ver_new = (u16)(data->fw_data[FW_VER_OFFSET + 2]
				| (data->fw_data[FW_VER_OFFSET + 3] << 8));
	if (ver_new == 0xFFFF) {
		dev_err(&data->client->dev, "invalid reg version\n");
		return false;
	}
	dev_info(&data->client->dev,
		"reg ver: cur= 0x%X, new= 0x%X\n", ver_ic, ver_new);

	if (ver_ic < ver_new)
		return true;

	return false;
out:
	return true;
}

u8 bt404_ts_fw_update(struct bt404_ts_data *data, const u8 *_fw_data)
{
	u8 i2c_buffer[TC_PAGE_SZ + 2];
	u8 *verify_data;
	int ret;
	int i;
	unsigned short slave_addr_backup;
	int retries = 3;
	u32 size = data->cap_info.fw_len;
	struct i2c_adapter *adapter = to_i2c_adapter(data->client->dev.parent);
	struct i2c_adapter *isp_adapter =
				to_i2c_adapter(data->isp_client->dev.parent);

	verify_data = kzalloc(size, GFP_KERNEL);
	if (!verify_data) {
		dev_err(&data->client->dev, "cannot alloc verify buffer\n");
		return false;
	}

	slave_addr_backup = data->client->addr;

retry_isp_firmware_upgrade:

	bt404_ts_power(data, POWER_OFF);
	msleep(1000);
	bt404_ts_power(data, POWER_ON);
	mdelay(1);	/* under 4ms*/

	dev_info(&data->client->dev, "flashing firmware (size=%d)\n", size);

	data->client->addr = data->isp_client->addr;

	for (i = 0; i < size; i += TC_PAGE_SZ) {
		i2c_buffer[0] = (i >> 8) & 0xff;	/*addr_h*/
		i2c_buffer[1] = (i) & 0xff;		/*addr_l*/

		memcpy(&i2c_buffer[2], &_fw_data[i], TC_PAGE_SZ);
		ret = ts_write_firmware_data(data->client, i2c_buffer,
				TC_PAGE_SZ + 2);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"fail to flash %dth page (%d)\n", i, ret);
			goto fail_upgrade;
		}
		msleep(20);
	}
	msleep(CHIP_POWER_OFF_AF_FZ_DELAY);

	data->client->addr = slave_addr_backup;

	dev_info(&data->client->dev, "verifing firmware\n");

	i2c_lock_adapter(adapter);
	i2c_unlock_adapter(isp_adapter);
	data->pdata->pin_configure(true);

	i2c_buffer[0] = i2c_buffer[1] = 0;

	ret = ts_read_firmware_data(data->isp_client, i2c_buffer,
						verify_data, size);
	if (ret < 0) {
		dev_err(&data->client->dev,
				"fail to read written data (%d)\n", ret);
		goto fail_upgrade;
	}
	dev_info(&data->client->dev, "read data size = %d\n", ret);
	msleep(CHIP_POWER_OFF_AF_FZ_DELAY);

	for (i = 0; i < size; i++) {
		if (_fw_data[i] != verify_data[i]) {
			dev_err(&data->client->dev, "mismatch at 0x%X\n", i);
			goto fail_upgrade;
		}
	}

	msleep(20);

	data->pdata->pin_configure(false);
	i2c_lock_adapter(isp_adapter);
	i2c_unlock_adapter(adapter);

	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	bt404_ts_power(data, POWER_OFF);
	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	bt404_ts_power(data, POWER_ON);
	mdelay(CHIP_ON_AF_FZ_DELAY);

	dev_info(&data->client->dev, "flashed firmware is verified!\n");

	kfree(verify_data);
	return true;

fail_upgrade:

	data->client->addr = slave_addr_backup;

	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	bt404_ts_power(data, POWER_OFF);
	mdelay(CHIP_POWER_OFF_AF_FZ_DELAY);
	bt404_ts_power(data, POWER_ON);
	mdelay(CHIP_ON_AF_FZ_DELAY);

	data->pdata->pin_configure(false);
	i2c_lock_adapter(isp_adapter);
	i2c_unlock_adapter(adapter);

	dev_err(&data->client->dev, "failure: %d retrial is left\n",
								--retries);
	if (retries)
		goto retry_isp_firmware_upgrade;

	kfree(verify_data);

	return false;
}

static bool bt404_ts_sw_reset(struct bt404_ts_data *data)
{
	int ret;
	int retries;

	for (retries = 10; retries > 0; retries--) {
		ret = bt404_ts_write_cmd(data->client, BT404_SWRESET_CMD);
		if (!ret)
			break;
		if (retries == 5) {
			/* reset IC */
			mdelay(CHIP_POWER_OFF_DELAY);
			bt404_ts_power(data, POWER_OFF);
			mdelay(CHIP_POWER_OFF_DELAY);
			bt404_ts_power(data, POWER_ON);
			mdelay(CHIP_ON_DELAY);
		} else {
			mdelay(10);
		}
	}

	if (retries == 0) {
		dev_err(&data->client->dev, "fail to sw reset\n");
		return false;
	}

	return true;
}

static bool bt404_ts_init_device(struct bt404_ts_data *data, bool force_update)
{
	const struct bt404_ts_reg_data *__reg_data;
	struct i2c_client *client = data->client;
	struct device *dev = &data->client->dev;
	u16 fw_state[] = {0xFFFF, 0x0000, 0xAAAA, 0x5555};
	u16 reg_val;
	int i;
	u16 ic_rev;
	u16 ic_fw_ver = 0;
	u16 ic_reg_ver;
	u16 ic_eeprom_info;
	s16 stmp;
	int retry_cnt = 0;
	int ret;

	reg_val = 0;
	reg_val |= 1 << BIT_PT_CNT_CHANGE;
	reg_val |= 1 << BIT_DOWN;
	reg_val |= 1 << BIT_MOVE;
	reg_val |= 1 << BIT_UP;
	if (data->pdata->num_buttons > 0)
		reg_val |= 1 << BIT_ICON_EVENT;

	data->cap_info.chip_int_mask = reg_val;

retry_init:
	ret = bt404_ts_sw_reset(data);
	if (!ret) {
		dev_err(dev, "fail to write interrupt register\n");
		goto fail_init;
	}

	ret = bt404_ts_read_data(client, BT404_FW_CHECKSUM, (u8 *)&fw_state,
				sizeof(fw_state[0]) *  ARRAY_SIZE(fw_state));
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (check sum)\n", __func__);
		goto fail_init;
	}
	if ((fw_state[0] != fw_state[2]) || (fw_state[1] != fw_state[3])) {
		dev_info(dev, "fw state check failure."
				"(0x%4x, 0x%4x, 0x%4x, 0x%4x)\n", fw_state[0],
				fw_state[1], fw_state[2], fw_state[3]);
		goto fail_init;
	}
	dev_info(dev, "fw state checked(0x%X, 0x%X)\n", fw_state[0],
								fw_state[1]);

	ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt reg (int enable)\n", __func__);
		goto fail_init;
	}

	dev_info(dev, "send reset command\n");

	ret = bt404_ts_write_cmd(client, BT404_SWRESET_CMD);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt cmd (sw reset)\n", __func__);
		goto fail_init;
	}

	/* get chip revision id */
	ret = bt404_ts_read_data(client, BT404_IC_REV,
						(u8 *)&ic_rev, 2);
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (ic rev)\n", __func__);
		goto fail_init;
	}
	dev_info(dev, "bt404 touch chip revision id = %x\n",
								ic_rev);

fw_update:
	data->cap_info.fw_len = 32 * 1024;

	dev_info(dev, "work_state = %d\n", data->work_state);
	ret = (int)bt404_check_fw_update(data);

	if (ret || (retry_cnt >= BT404_INIT_RETRY_CNT) || force_update) {
		bt404_ts_fw_update(data, &data->fw_data[2]);

		/* get chip revision id */
		ret = bt404_ts_read_data(client, BT404_IC_REV,
							(u8 *)&ic_rev, 2);
		if (ret < 0) {
			dev_err(dev, "%s: err: rd (ic rev)\n", __func__);
			goto fail_init;
		}
		dev_info(dev, "bt404 touch chip revision id = %x\n",
								ic_rev);
	}

	/* get chip firmware version */
	ret = bt404_ts_read_data(client, BT404_FW_VER, (u8 *)&ic_fw_ver, 2);
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (fw ver)\n", __func__);
		goto fail_init;
	}
	dev_info(dev, "bt404 touch chip firmware version = %x\n",
							ic_fw_ver);

	ic_reg_ver = 0xffff;
	ret = bt404_ts_read_data(client, BT404_REG_VER,
					(u8 *)&ic_reg_ver, 2);
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (reg ver)\n", __func__);
		goto fail_init;
	}
	dev_info(dev, "touch reg data version = %d\n",
						ic_reg_ver);

	if (ic_reg_ver < data->pdata->reg_data[BT404_REG_VER].val) {

		dev_info(dev, "write new reg data(%d < %d)\n",
			ic_reg_ver, data->pdata->reg_data[BT404_REG_VER].val);

		for (i = 0; i < data->pdata->num_regs; i++) {

			__reg_data = &data->pdata->reg_data[i];
			if (!__reg_data->valid)
				continue;

			ret = bt404_ts_write_reg(client, (u16)i,
					(u16)(__reg_data->val));
			if (ret < 0) {
				dev_err(dev, "%s: err: wt reg (reg[%d])\n",
								__func__, i);
				goto fail_init;
			}

			if (i == BT404_TOTAL_NUMBER_OF_X
				|| i == BT404_TOTAL_NUMBER_OF_Y)
				mdelay(50);

			ret = bt404_ts_read_data(client, (u16)i,
							(u8 *)&stmp, 2);
			if (ret < 0) {
				dev_err(dev, "%s: err: rd (stmp)\n", __func__);
				goto fail_init;
			}

			if (memcmp((char *)&__reg_data->val,
							(char *)&stmp, 2) < 0)
				dev_err(dev, "fail to write reg \
						(addr = 0x%02X , %d != %d)\n",
						i, __reg_data->val, stmp);

		}
		dev_info(dev, "done new reg data( %d < %d)\n",
			ic_reg_ver, data->pdata->reg_data[BT404_REG_VER].val);

		ret = bt404_ts_write_cmd(client, BT404_SAVE_STATUS_CMD);
		if (ret < 0)
			goto fail_init;
		mdelay(1000);	/* for fusing eeprom */
	}

	ret = bt404_ts_read_data(client, BT404_EEPROM_INFO_REG,
					(u8 *)&ic_eeprom_info, 2);
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (eeprom info)\n", __func__);
		goto fail_init;
	}
	dev_info(dev, "touch eeprom info = 0x%04X\n",
							ic_eeprom_info);

	if (bt404_bit_test(ic_eeprom_info, 0)) { /* hw calibration bit*/
		 /* h/w calibration */
		ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG,
						data->cap_info.chip_int_mask);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt reg (int en)\n", __func__);
			goto fail_init;
		}

		ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE, 0x07);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt reg (touch mode)\n",
								__func__);
			goto fail_init;
		}

		ret = bt404_ts_write_cmd(client, BT404_CAL_CMD);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt cmd (cal)\n", __func__);
			goto fail_init;
		}

		ret = bt404_ts_write_cmd(client, BT404_SWRESET_CMD);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt cmd (sw reset)\n", __func__);
			goto fail_init;
		}
		mdelay(100);

		for (i = 0; i < 3; i++) {
			ret = bt404_ts_write_cmd(client,
						BT404_CLEAR_INT_STATUS_CMD);
			if (ret < 0) {
				dev_err(dev, "%s: err: wt cmd (clr int)\n",
								__func__);
				goto fail_init;
			}
			mdelay(100);
		}

		/* wait for h/w calibration*/
		do {
			/* to do :
			 * check H/W calibration failure check
			 */
			mdelay(1000);
			ret = bt404_ts_read_data(client, BT404_EEPROM_INFO_REG,
						(u8 *)&ic_eeprom_info, 2);
			if (ret < 0) {
				dev_err(dev, "%s: err: rd (eeprom info)\n",
								__func__);
				goto fail_init;
			}
			dev_info(dev, "touch eeprom info = 0x%04X\n",
							ic_eeprom_info);
			if (!bt404_bit_test(ic_eeprom_info, 0))
				break;
		} while (1);

		ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE, TOUCH_MODE);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt reg (touch mode)\n",
								__func__);
			goto fail_init;
		}
/*
		if (data->cap_info.chip_int_mask != 0) {
			ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG,
						data->cap_info.chip_int_mask);
			if (ret < 0) {
				dev_err(dev, "%s: err: wt reg (int en)\n",
								__func__);
				goto fail_init;
			}
		}
		mdelay(10);
*/
		ret = bt404_ts_write_cmd(client, BT404_SWRESET_CMD);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt cmd (sw rst)\n", __func__);
			goto fail_init;
		}
		mdelay(10);

		ret = bt404_ts_write_cmd(client, BT404_SAVE_CAL_CMD);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt cmd (save cal)\n", __func__);
			goto fail_init;
		}
		mdelay(500);

		/* disable chip interrupt */
		ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG, 0);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt reg (int en)\n", __func__);
			goto fail_init;
		}

	}

	data->cap_info.ic_rev = (u16)ic_rev;
	data->cap_info.ic_fw_ver = (u16)ic_fw_ver;
	data->cap_info.ic_reg_ver = (u16)ic_reg_ver;

	/* initialize */
	ret = bt404_ts_write_reg(client, BT404_X_RESOLUTION,
					(u16)(data->cap_info.x_max));
	if (ret < 0) {
		dev_err(dev, "%s: err: wt reg (x resolution)\n", __func__);
		goto fail_init;
	}

	ret = bt404_ts_write_reg(client, BT404_Y_RESOLUTION,
					(u16)(data->cap_info.y_max));
	if (ret < 0) {
		dev_err(dev, "%s: err: wt reg (y resolution)\n", __func__);
		goto fail_init;
	}

	ret = bt404_ts_read_data(client, BT404_X_RESOLUTION,
				(u8 *)&data->cap_info.x_resolution, 2);
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (x resolution)\n", __func__);
		goto fail_init;
	}
	debug_msg("touch max x = %d\n", data->cap_info.x_resolution);

	ret = bt404_ts_read_data(client, BT404_Y_RESOLUTION,
				(u8 *)&data->cap_info.y_resolution, 2);
	if (ret < 0) {
		dev_err(dev, "%s: err: rd (y resolution)\n", __func__);
		goto fail_init;
	}
	debug_msg("touch max y = %d\n",	data->cap_info.y_resolution);

	data->cap_info.x_min = (u32)0;
	data->cap_info.y_min = (u32)0;
	data->cap_info.x_max = (u32)data->cap_info.x_resolution;
	data->cap_info.y_max = (u32)data->cap_info.y_resolution;

	data->cap_info.button_num = data->pdata->num_buttons;
	ret = bt404_ts_write_reg(client, BT404_SUPPORTED_FINGER_NUM,
						(u16)MAX_SUPPORTED_FINGER_NUM);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt reg (max supprot finger)\n",
								__func__);
		goto fail_init;
	}
	data->cap_info.max_finger = REAL_SUPPORTED_FINGER_NUM;

	debug_msg("max supported finger num = %d, \
		real supported finger num = %d\n",
		data->cap_info.max_finger, REAL_SUPPORTED_FINGER_NUM);
	data->cap_info.gesture_support = 0;
	debug_msg("set other configuration\n");

	ret = bt404_ts_read_data(client, BT404_TOTAL_NUMBER_OF_X,
					(u8 *)&data->cap_info.x_node_num, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (x node num)\n",
								__func__);
		goto fail_init;
	}

	ret = bt404_ts_read_data(client, BT404_TOTAL_NUMBER_OF_Y,
					(u8 *)&data->cap_info.y_node_num, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (t node num)\n",
								__func__);
		goto fail_init;
	}

	data->cap_info.total_node_num = (u16)(data->cap_info.x_node_num *
						data->cap_info.y_node_num);

	dev_info(dev, "the num of node=%d(%d,%d)\n",
		data->cap_info.total_node_num, data->cap_info.x_node_num,
		data->cap_info.y_node_num);

#if	USE_TEST_RAW_TH_DATA_MODE
	if (data->raw_mode_flag != TOUCH_NORMAL_MODE) {	/* Test Mode */
		ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE,
			data->raw_mode_flag);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt reg (touch mode)\n",
								__func__);
			goto fail_init;
		}
	} else
#endif
		{
		reg_val = TOUCH_MODE;
		ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE, reg_val);
		if (ret < 0) {
			dev_err(dev, "%s: err: wt reg (touch mode)\n",
								__func__);
			goto fail_init;
		}
	}

	/* soft calibration */
	ret = bt404_ts_write_cmd(client, BT404_CAL_CMD);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt cmd (cal)\n", __func__);
		goto fail_init;
	}

	ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG,
					data->cap_info.chip_int_mask);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt reg (int en)\n", __func__);
		goto fail_init;
	}

	/* read garbage data */
	for (i = 0; i < 10; i++) {
		bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		udelay(10);
	}

#if	USE_TEST_RAW_TH_DATA_MODE
	if (data->raw_mode_flag != TOUCH_NORMAL_MODE) { /* Test Mode */
		ret = bt404_ts_write_reg(client,
			BT404_PERIODICAL_INTERRUPT_INTERVAL,
			BT404_SCAN_RATE_HZ
			*BT404_RAW_DATA_ESD_TIMER_INTERVAL);
		if (ret < 0)
			dev_err(dev, "%s: err: wt reg \
				(BT404_PERIODICAL_INTERRUPT_INTERVAL)\n",
								__func__);
	} else
#endif
		{
	}
	dev_info(dev, "successfully initialized\n");
	return true;

fail_init:
	retry_cnt++;
	if (retry_cnt < BT404_INIT_RETRY_CNT) {
		mdelay(CHIP_POWER_OFF_DELAY);
		bt404_ts_power(data, POWER_OFF);
		mdelay(CHIP_POWER_OFF_DELAY);
		bt404_ts_power(data, POWER_ON);
		mdelay(CHIP_ON_DELAY);
		dev_err(dev, "retry to initiallize(retry cnt = %d)\n",
								retry_cnt);
		goto retry_init;
	} else {
		dev_err(dev, "retry init (cnt = %d)\n", retry_cnt);

		if (retry_cnt > BT404_INIT_RETRY_LIMIT) {
			dev_err(dev, "failed to force upgrade (%d)\n",
								retry_cnt);
			return false;
		}
		goto fw_update;
	}

	dev_err(dev, "failed to initiallize\n");
	return false;
}
static void bt404_ts_report_touch_data(struct bt404_ts_data *data,
							bool force_clear)
{
	struct i2c_client *client = data->client;
	struct _ts_zinitix_point_info *prev = &data->reported_touch_info;
	struct _ts_zinitix_point_info *cur = &data->touch_info;
	int i;

	if (force_clear) {
		for (i = 0; i < data->cap_info.max_finger; i++) {
			if (prev->coord[i].sub_status & 0x1) {
				dev_info(&client->dev,
						"%4s[%1d]: %3d,%3d (%3d)\n",
						"up/f", i, cur->coord[i].x,
						cur->coord[i].x,
						cur->coord[i].width);
				prev->coord[i].sub_status &= ~(0x01);
			}

			input_mt_slot(data->input_dev_ts, i);
			input_mt_report_slot_state(data->input_dev_ts,
							MT_TOOL_FINGER, false);
		}
		input_sync(data->input_dev_ts);
	}
	
	for (i = 0; i < data->cap_info.max_finger; i++) {
		bool prev_exist = prev->coord[i].sub_status & 0x1;
		bool cur_exist = cur->coord[i].sub_status & 0x1;
		bool cur_up = (cur->coord[i].sub_status >> 3) & 0x1;
		bool cur_move = (cur->coord[i].sub_status >> 2) & 0x1;
		bool cur_down = (cur->coord[i].sub_status >> 1) & 0x1;

		if (!(prev_exist && cur_up) && !cur_move && !cur_down )
			continue;
		
		if (prev_exist && cur_up) {

#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			dev_info(&client->dev, "%4s[%1d]: %3d,%3d (%3d)\n",
					"up", i, cur->coord[i].x,
					cur->coord[i].x, cur->coord[i].width);
#endif
			prev->coord[i].sub_status &= ~(0x01);

			input_mt_slot(data->input_dev_ts, i);
			input_mt_report_slot_state(data->input_dev_ts,
						   MT_TOOL_FINGER, false);

			continue;
		} else if (cur_move || cur_down) {

			if (!cur->coord[i].width) {
				dev_err(&client->dev,
					"recover form wrong width.\n");
				cur->coord[i].width = 5;
			}
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			if (cur_down)
				dev_info(&client->dev,
						"%4s[%1d]: %3d,%3d (%3d)\n",
						"down", i,  cur->coord[i].x,
						cur->coord[i].y,
						cur->coord[i].width);
#endif
			input_mt_slot(data->input_dev_ts, i);
			input_mt_report_slot_state(data->input_dev_ts,
						   MT_TOOL_FINGER, true);
			input_report_abs(data->input_dev_ts, ABS_MT_POSITION_X,
							cur->coord[i].x);
			input_report_abs(data->input_dev_ts, ABS_MT_POSITION_Y,
							cur->coord[i].y);
			input_report_abs(data->input_dev_ts, ABS_MT_TOUCH_MAJOR,
							cur->coord[i].width);
			input_report_abs(data->input_dev_ts, ABS_MT_PRESSURE,
							cur->coord[i].width);
		}
	}
	input_sync(data->input_dev_ts);
}

static irqreturn_t bt404_ts_interrupt(int irq, void *dev_id)
{
	struct bt404_ts_data *data = dev_id;
	struct i2c_client *client = data->client;
	int ret;
#ifdef TSP_VERBOSE_DEBUG
	int i;
#endif
	u16 addr, val;
	u16 status;

	down(&data->work_lock);
	if (gpio_get_value(data->pdata->gpio_int)) {
		dev_err(&client->dev, "invalid interrupt\n");
		goto out;
	}

	if (data->work_state != NOTHING) {
		dev_err(&client->dev, "%s: invalid work proceedure (%d)\n",
						__func__, data->work_state);
		bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		udelay(DELAY_FOR_SIGNAL_DELAY);
		goto out;
	}

	data->work_state = NORMAL;

	ret = bt404_ts_read_data(client, BT404_POINT_STATUS_REG,
						(u8 *)(&data->touch_info), 4);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (point status) (%d)\n",
							__func__, ret);
		goto out;
	}

	status = data->touch_info.status;
#ifdef TSP_VERBOSE_DEBUG
	dev_info(&data->client->dev, "status:0x%4X, cnt:%3d, stamp:%3d\n",
			data->touch_info.status, data->touch_info.finger_cnt,
			data->touch_info.time_stamp);
#endif

	if (status == 0x0 && data->touch_info.finger_cnt == 100) {
		dev_info(&client->dev, "periodical esd repeated interrupt.\n");
		goto out;
	}

	if ((status >> 15) & 0x1) {
		/* touch key interrupt*/
		bool need_report = false;
		int action;
		int offset;

		addr = BT404_ICON_STATUS_REG;
		ret = bt404_ts_read_data(data->client, addr, (u8 *)&val, 2);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err: rd (button event)\n",
								__func__);
			goto out;
		}
		ret = bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		if (ret < 0)
			dev_err(&client->dev, "%s: err: cmd (clr int)\n",
								 __func__);

		offset = 0;
		if ((val >> (offset + 8)) & 0x1) {
			need_report = true;
			action = 0;
		} else if ((val >> offset) & 0x1 &&
				!((data->reported_key_val >> offset) & 0x1)) {
			need_report = true;
			action = 1;
		}
		if (need_report) {
			input_report_key(data->input_dev_tk,
				data->pdata->button_map[offset], action);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			dev_info(&client->dev, "key[%3d]:%s\n",
						data->pdata->button_map[offset],
						(action) ? "down" : "up");
#endif
		}

		need_report = false;
		offset = 1;
		if ((val >> (offset + 8)) & 0x1) {
			need_report = true;
			action = 0;
		} else if ((val >> offset) & 0x1 &&
				!((data->reported_key_val >> offset) & 0x1)) {
			need_report = true;
			action = 1;
		}

		if (need_report) {
			input_report_key(data->input_dev_tk,
				data->pdata->button_map[offset], action);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			dev_info(&client->dev, "key[%3d]:%s\n",
						data->pdata->button_map[offset],
						(action) ? "down" : "up");
#endif
		}

		data->reported_key_val = val;

		input_sync(data->input_dev_tk);

		goto out;

	} else if ((status >> 1) & 0b111) {
		/* touch screen interrupt*/

		ret = bt404_ts_read_data(client, BT404_POINT_REG,
					(u8 *)(&data->touch_info),
					sizeof(struct _ts_zinitix_point_info));
		if (ret < 0) {
			dev_err(&client->dev, "%s: err: rd (points)\n",
								__func__);
			goto out;
		}

		ret = bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		if (ret < 0)
			dev_err(&client->dev, "%s: err: cmd (clr int)\n",
								__func__);

#ifdef TSP_VERBOSE_DEBUG
		for (i = 0; i < data->cap_info.max_finger; i++) {
			u8 sub_status = data->touch_info.coord[i].sub_status;
			if (sub_status & 0x1 || (sub_status >> 3) & 0x1)
				dev_info(&client->dev,
				"%1d: 0x%2X|%3d,%3d(%3d)\n",
				i, sub_status, data->touch_info.coord[i].x,
				data->touch_info.coord[i].y,
				data->touch_info.coord[i].width);
		}

#endif

		bt404_ts_report_touch_data(data, false);
		memcpy((char *)&data->reported_touch_info,
					(char *)&data->touch_info,
					sizeof(struct _ts_zinitix_point_info));
	} else {
		dev_err(&client->dev, "%s: invalid status (0x%X)\n", __func__,
									status);
		goto out;
	}

	if (data->work_state == NORMAL)
		data->work_state = NOTHING;

	up(&data->work_lock);
	return IRQ_HANDLED;

out:
	ret = bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
	if (ret < 0)
		dev_err(&client->dev, "%s: err: cmd (clr int)\n", __func__);

	udelay(DELAY_FOR_SIGNAL_DELAY);

	if (data->work_state == NORMAL)
		data->work_state = NOTHING;

	up(&data->work_lock);
	return IRQ_HANDLED;
}

#if	USE_TEST_RAW_TH_DATA_MODE
static ssize_t bt404_ts_get_raw_data(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int total_n_num;
	if (misc_data == NULL) {
		debug_msg("device NULL : NULL\n");
		return 0;
	}
	down(&misc_data->raw_data_lock);
	total_n_num = misc_data->cap_info.total_node_num;
	if (bt404_bit_test(
		misc_data->cur_data[total_n_num], BIT_PT_EXIST)) {
		/*x_lsb*/
		buf[20] =
			(misc_data->cur_data[total_n_num + 1] & 0xff);
		/*x_msb*/
		buf[21] =
			((misc_data->cur_data[total_n_num + 1] >> 8) & 0xff);
		/*y_lsb*/
		buf[22] =
			(misc_data->cur_data[total_n_num + 2] & 0xff);
		/*y_msb*/
		buf[23] =
			((misc_data->cur_data[total_n_num + 2] >> 8) & 0xff);
	} else {
		buf[20] = 0; /*x_lsb*/
		buf[21] = 0; /*x_msb*/
		buf[22] = 0; /*y_lsb*/
		buf[23] = 0; /*y_msb*/
	}

	/*lsb*/
	buf[0] =
		(char)(misc_data->ref_data[22] & 0xff);
	/*msb*/
	buf[1] =
		(char)((misc_data->ref_data[22] >> 8) & 0xff);
	/*delta lsb*/
	buf[2] =
		(char)(((s16)(misc_data->cur_data[22]
					- misc_data->ref_data[22])) & 0xff);
	/*delta msb*/
	buf[3] =
		(char)((((s16)(misc_data->cur_data[22]
				- misc_data->ref_data[22])) >> 8) & 0xff);
	/*
	buf[4] =
		(char)(misc_data->ref_data[51]&0xff);
	buf[5] =
		(char)((misc_data->ref_data[51]>>8)&0xff);
	buf[6] =
		(char)(((s16)(misc_data->cur_data[51]
		-misc_data->ref_data[51]))&0xff);
	buf[7] =
		(char)((((s16)(misc_data->cur_data[51]
		-misc_data->ref_data[51]))>>8)&0xff);

	buf[8] =
		(char)(misc_data->ref_data[102]&0xff);
	buf[9] =
		(char)((misc_data->ref_data[102]>>8)&0xff);
	buf[10] =
		(char)(((s16)(misc_data->cur_data[102]
		-misc_data->ref_data[102]))&0xff);
	buf[11] =
		(char)((((s16)(misc_data->cur_data[102]
		-misc_data->ref_data[102]))>>8)&0xff);

	buf[12] =
		(char)(misc_data->ref_data[169]&0xff);
	buf[13] =
		(char)((misc_data->ref_data[169]>>8)&0xff);
	buf[14] =
		(char)(((s16)(misc_data->cur_data[169]
		-misc_data->ref_data[169]))&0xff);
	buf[15] =
		(char)((((s16)(misc_data->cur_data[169]
		-misc_data->ref_data[169]))>>8)&0xff);

	buf[16] =
		(char)(misc_data->ref_data[178]&0xff);
	buf[17] =
		(char)((misc_data->ref_data[178]>>8)&0xff);
	buf[18] =
		(char)(((s16)(misc_data->cur_data[178]
		-misc_data->ref_data[178]))&0xff);
	buf[19] =
		(char)((((s16)(misc_data->cur_data[178]
		- misc_data->ref_data[178]))>>8)&0xff);
	*/
	up(&misc_data->raw_data_lock);

	return 24;
}

ssize_t zinitix_set_testmode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned char value = 0;

	printk(KERN_INFO "[zinitix_touch] zinitix_set_testmode, buf = %d\n",
		*buf);

	if (misc_data == NULL) {
		debug_msg("device NULL : NULL\n");
		return 0;
	}

	sscanf(buf, "%c", &value);

	if (value != TOUCH_TEST_RAW_MODE && value != TOUCH_NORMAL_MODE) {
		printk(KERN_WARNING
				"[zinitix ts] test mode setting value error."
				"you must set %d[=normal] or %d[=raw mode]\n",
				TOUCH_NORMAL_MODE, TOUCH_TEST_RAW_MODE);
		return 1;
	}

	down(&misc_data->raw_data_lock);
	misc_data->raw_mode_flag = value;

	printk(KERN_INFO "[zinitix_touch] zinitix_set_testmode,"
			"touchkey_testmode = %d\n", misc_data->raw_mode_flag);

	if (misc_data->raw_mode_flag == TOUCH_NORMAL_MODE) {
		/* enter into normal mode */
		printk(KERN_INFO "[zinitix_touch] TEST Mode Exit\n");

		if (bt404_ts_write_reg(misc_data->client,
			BT404_PERIODICAL_INTERRUPT_INTERVAL, BT404_SCAN_RATE_HZ
			*BT404_ESD_TIMER_INTERVAL) != I2C_SUCCESS)
			printk(KERN_INFO "[zinitix_touch] Fail to set"
				"BT404_PERIODICAL_INTERRUPT_INTERVAL.\n");

		if (bt404_ts_write_reg(misc_data->client,
			BT404_TOUCH_MODE, TOUCH_MODE) != I2C_SUCCESS)
			printk(KERN_INFO
				"[zinitix_touch] Fail to set touch mode %d.\n",
				TOUCH_MODE);

		/* clear garbage data */
		bt404_ts_write_cmd(misc_data->client,
			BT404_CLEAR_INT_STATUS_CMD);
		mdelay(100);
		bt404_ts_write_cmd(misc_data->client,
			BT404_CLEAR_INT_STATUS_CMD);
	} else {
		/* enter into test mode */
		printk(KERN_INFO "[zinitix_touch] TEST Mode Enter\n");

		if (bt404_ts_write_reg(misc_data->client,
			BT404_PERIODICAL_INTERRUPT_INTERVAL,
			BT404_SCAN_RATE_HZ
			*BT404_RAW_DATA_ESD_TIMER_INTERVAL) != I2C_SUCCESS)
			printk(KERN_INFO
					"[zinitix_touch] Fail to set"
					"BT404_RAW_DATA_ESD_TIMER_INTERVAL.\n");

		if (bt404_ts_write_reg(misc_data->client,
			BT404_TOUCH_MODE, TOUCH_TEST_RAW_MODE) != I2C_SUCCESS)
			printk(KERN_INFO
				"[zinitix_touch] Fail to set touch mode %d.\n",
				TOUCH_TEST_RAW_MODE);
		bt404_ts_write_cmd(misc_data->client,
			BT404_CLEAR_INT_STATUS_CMD);
		/* clear garbage data */
		mdelay(100);
		bt404_ts_write_cmd(misc_data->client,
			BT404_CLEAR_INT_STATUS_CMD);
		memset(&misc_data->reported_touch_info,
			0x0, sizeof(struct _ts_zinitix_point_info));
		memset(&misc_data->touch_info,
			0x0, sizeof(struct _ts_zinitix_point_info));
	}
	up(&misc_data->raw_data_lock);
	return 1;

}

static DEVICE_ATTR(get_touch_raw_data,
	S_IRUGO | S_IWUSR | S_IWOTH | S_IXOTH,
	bt404_ts_get_raw_data,
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

	u8 div_node;
	int total_cal_n;

	u16 mode;
	u16 ic_eeprom_info;
	struct _reg_ioctl reg_ioctl;
	u16 val;
	int nval = 0;

	if (misc_data == NULL)
		return -1;

	/* debug_msg("cmd = %d, argp = 0x%x\n", cmd, (int)argp); */

	switch (cmd) {

	case TOUCH_IOCTL_GET_DEBUGMSG_STATE:
		ret = m_ts_debug_mode;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_SET_DEBUGMSG_STATE:
		if (copy_from_user(&nval, argp, 4)) {
			printk(KERN_INFO
				"[zinitix_touch] error : copy_from_user\n");
			return -1;
		}
		if (nval)
			printk(KERN_INFO "[zinitix_touch] on debug mode (%d)\n",
									nval);
		else
			printk(KERN_INFO
				"[zinitix_touch] off debug mode (%d)\n", nval);
		m_ts_debug_mode = nval;
		break;

	case TOUCH_IOCTL_GET_CHIP_REVISION:
		ret = misc_data->cap_info.ic_rev;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_FW_VERSION:
		ret = misc_data->cap_info.ic_fw_ver;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_REG_DATA_VERSION:
		ret = misc_data->cap_info.ic_reg_ver;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_SIZE:
		if (copy_from_user(&sz, argp, sizeof(size_t)))
			return -1;

		printk(KERN_INFO "firmware size = %d\n", sz);
		if (misc_data->cap_info.fw_len != sz) {
			printk(KERN_INFO "firmware size error\n");
			return -1;
		}
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_DATA:
		if (copy_from_user(&data->fw_data[2],
			argp, misc_data->cap_info.fw_len))
			return -1;

		version =
			(u16)(((u16)data->fw_data[FW_VER_OFFSET+1]<<8)
			|(u16)data->fw_data[FW_VER_OFFSET]);

		printk(KERN_INFO "firmware version = %x\n", version);

		if (copy_to_user(argp, &version, sizeof(version)))
			return -1;
		break;

	case TOUCH_IOCTL_START_UPGRADE:
		disable_irq(misc_data->irq);
		down(&misc_data->work_lock);
		misc_data->work_state = UPGRADE;

		debug_msg("clear all reported points\n");

		bt404_ts_report_touch_data(misc_data, true);
		/* zinitix_clear_report_data(misc_data); */

		printk(KERN_INFO "start upgrade firmware\n");
		if (bt404_ts_fw_update(misc_data,
			&data->fw_data[2]) == false) {
			enable_irq(misc_data->irq);
			misc_data->work_state = NOTHING;
			up(&misc_data->work_lock);
			return -1;
		}

		if (bt404_ts_init_device(misc_data, false) == false) {
			enable_irq(misc_data->irq);
			misc_data->work_state = NOTHING;
			up(&misc_data->work_lock);
			return -1;
		}

		enable_irq(misc_data->irq);
		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		break;

	case TOUCH_IOCTL_GET_X_RESOLUTION:
		ret = misc_data->cap_info.x_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_RESOLUTION:
		ret = misc_data->cap_info.y_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_X_NODE_NUM:
		ret = misc_data->cap_info.x_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_NODE_NUM:
		ret = misc_data->cap_info.y_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_TOTAL_NODE_NUM:
		ret = misc_data->cap_info.total_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_HW_CALIBRAION:
		ret = -1;
		disable_irq(misc_data->irq);
		down(&misc_data->work_lock);
		if (misc_data->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\n",
				misc_data->work_state);
			up(&misc_data->work_lock);
			return -1;
		}
		misc_data->work_state = HW_CAL;

		/* h/w calibration */
		if (bt404_ts_write_reg(misc_data->client,
			BT404_TOUCH_MODE, 0x07) != I2C_SUCCESS)
			goto fail_hw_cal;
		if (bt404_ts_write_cmd(misc_data->client,
			BT404_CAL_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;
		if (bt404_ts_write_cmd(misc_data->client,
			BT404_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;
		mdelay(1);
		bt404_ts_write_cmd(misc_data->client,
			BT404_CLEAR_INT_STATUS_CMD);
		/* wait for h/w calibration */
		do {
			mdelay(1000);
			if (bt404_ts_read_data(misc_data->client,
				BT404_EEPROM_INFO_REG,
				(u8 *)&ic_eeprom_info, 2) < 0)
				goto fail_hw_cal;
			debug_msg("touch eeprom info = 0x%04X\n",
				ic_eeprom_info);
			if (!bt404_bit_test(ic_eeprom_info, 0))
				break;
		} while (1);


		mdelay(10);
		if (bt404_ts_write_cmd(misc_data->client,
			BT404_SAVE_CAL_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;
		mdelay(500);

		ret = 0;
fail_hw_cal:
		if (misc_data->raw_mode_flag == TOUCH_NORMAL_MODE)
			mode = TOUCH_MODE;
		else
			mode = misc_data->raw_mode_flag;
		if (bt404_ts_write_reg(misc_data->client,
			BT404_TOUCH_MODE, mode) != I2C_SUCCESS) {
			printk(KERN_INFO "fail to set touch mode %d.\n",
				mode);
			goto fail_hw_cal2;
		}

		if (bt404_ts_write_cmd(misc_data->client,
			BT404_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal2;

		enable_irq(misc_data->irq);
		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return ret;
fail_hw_cal2:
		enable_irq(misc_data->irq);
		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return -1;

	case TOUCH_IOCTL_SET_RAW_DATA_MODE:
		if (misc_data == NULL) {
			debug_msg("misc device NULL?\n");
			return -1;
		}

		down(&misc_data->work_lock);
		if (misc_data->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\n",
				misc_data->work_state);
			up(&misc_data->work_lock);
			return -1;
		}
		misc_data->work_state = SET_MODE;

		if (copy_from_user(&nval, argp, 4)) {
			printk(KERN_INFO
				"[zinitix_touch] error : copy_from_user\n");
			misc_data->work_state = NOTHING;
			return -1;
		}

		debug_msg("[zinitix_touch] touchkey_testmode = %d\n",
			misc_data->raw_mode_flag);

		if (nval == TOUCH_NORMAL_MODE &&
			misc_data->raw_mode_flag != TOUCH_NORMAL_MODE) {
			/* enter into normal mode */
			misc_data->raw_mode_flag = nval;
			printk(KERN_INFO
					"[zinitix_touch] raw data mode exit\n");

			if (bt404_ts_write_reg(misc_data->client,
				BT404_PERIODICAL_INTERRUPT_INTERVAL,
				BT404_SCAN_RATE_HZ*BT404_ESD_TIMER_INTERVAL)
				!= I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] Fail to set"
				"BT404_PERIODICAL_INTERRUPT_INTERVAL.\n");

			if (bt404_ts_write_reg(misc_data->client,
				BT404_TOUCH_MODE, TOUCH_MODE) != I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] fail to set"
						"TOUCH_MODE.\n");

			/* clear garbage data */
			bt404_ts_write_cmd(misc_data->client,
				BT404_CLEAR_INT_STATUS_CMD);
			mdelay(100);
			bt404_ts_write_cmd(misc_data->client,
				BT404_CLEAR_INT_STATUS_CMD);
		} else if (nval != TOUCH_NORMAL_MODE) {
			/* enter into test mode*/
			misc_data->raw_mode_flag = nval;
			printk(KERN_INFO
				"[zinitix_touch] raw data mode enter\n");

			if (bt404_ts_write_reg(misc_data->client,
				BT404_PERIODICAL_INTERRUPT_INTERVAL,
				BT404_SCAN_RATE_HZ
				*BT404_RAW_DATA_ESD_TIMER_INTERVAL)
				!= I2C_SUCCESS)
				printk(KERN_INFO "[zinitix_touch] Fail to set"
					"BT404_RAW_DATA_ESD_TIMER_INTERVAL.\n");

			if (bt404_ts_write_reg(misc_data->client,
				BT404_TOUCH_MODE,
				misc_data->raw_mode_flag) != I2C_SUCCESS)
				printk(KERN_INFO
					"[zinitix_touch] raw data mode :"
					"Fail to set TOUCH_MODE %d.\n",
					misc_data->raw_mode_flag);

			bt404_ts_write_cmd(misc_data->client,
				BT404_CLEAR_INT_STATUS_CMD);
			/* clear garbage data */
			mdelay(100);
			bt404_ts_write_cmd(misc_data->client,
				BT404_CLEAR_INT_STATUS_CMD);
		}

		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return 0;

	case TOUCH_IOCTL_GET_REG:
		if (misc_data == NULL) {
			debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_data->work_lock);
		if (misc_data->work_state != NOTHING) {
			printk(KERN_INFO "other process occupied.. (%d)\n",
				misc_data->work_state);
			up(&misc_data->work_lock);
			return -1;
		}

		misc_data->work_state = SET_MODE;

		if (copy_from_user(&reg_ioctl,
			argp, sizeof(struct _reg_ioctl))) {
			misc_data->work_state = NOTHING;
			up(&misc_data->work_lock);
			printk(KERN_INFO
				"[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (bt404_ts_read_data(misc_data->client,
			reg_ioctl.addr, (u8 *)&val, 2) < 0)
			ret = -1;

		nval = (int)val;

		if (copy_to_user(reg_ioctl.val, (u8 *)&nval, 4)) {
			misc_data->work_state = NOTHING;
			up(&misc_data->work_lock);
			printk(KERN_INFO
				"[zinitix_touch] error : copy_to_user\n");
			return -1;
		}

		debug_msg("read : reg addr = 0x%x, val = 0x%x\n",
			reg_ioctl.addr, nval);

		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return ret;

	case TOUCH_IOCTL_SET_REG:

		if (misc_data == NULL) {
			debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_data->work_lock);
		if (misc_data->work_state != NOTHING) {
			printk(KERN_INFO "other process occupied.. (%d)\n",
				misc_data->work_state);
			up(&misc_data->work_lock);
			return -1;
		}

		misc_data->work_state = SET_MODE;
		if (copy_from_user(&reg_ioctl,
				argp, sizeof(struct _reg_ioctl))) {
			misc_data->work_state = NOTHING;
			up(&misc_data->work_lock);
			printk(KERN_INFO
				"[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (copy_from_user(&val, reg_ioctl.val, 4)) {
			misc_data->work_state = NOTHING;
			up(&misc_data->work_lock);
			printk(KERN_INFO
				"[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (bt404_ts_write_reg(misc_data->client,
			reg_ioctl.addr, val) != I2C_SUCCESS)
			ret = -1;

		debug_msg("write : reg addr = 0x%x, val = 0x%x\n",
			reg_ioctl.addr, val);
		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return ret;

	case TOUCH_IOCTL_DONOT_TOUCH_EVENT:

		if (misc_data == NULL) {
			debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_data->work_lock);
		if (misc_data->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\n",
				misc_data->work_state);
			up(&misc_data->work_lock);
			return -1;
		}

		misc_data->work_state = SET_MODE;
		if (bt404_ts_write_reg(misc_data->client,
			BT404_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
			ret = -1;
		debug_msg("write : reg addr = 0x%x, val = 0x0\n",
			BT404_INT_ENABLE_FLAG);

		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return ret;

	case TOUCH_IOCTL_SEND_SAVE_STATUS:
		if (misc_data == NULL) {
			debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_data->work_lock);
		if (misc_data->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\n",
				misc_data->work_state);
			up(&misc_data->work_lock);
			return -1;
		}
		misc_data->work_state = SET_MODE;
		ret = 0;
		if (bt404_ts_write_cmd(misc_data->client,
			BT404_SAVE_STATUS_CMD) != I2C_SUCCESS)
			ret = -1;
		mdelay(1000);	/* for fusing eeprom */

		misc_data->work_state = NOTHING;
		up(&misc_data->work_lock);
		return ret;

	case TOUCH_IOCTL_GET_RAW_DATA:
		if (misc_data == NULL) {
			debug_msg("misc device NULL?\n");
			return -1;
		}

		if (misc_data->raw_mode_flag == TOUCH_NORMAL_MODE)
			return -1;

		down(&misc_data->raw_data_lock);
		if (misc_data->update == 0) {
			up(&misc_data->raw_data_lock);
			return -2;
		}

		if (copy_from_user(&raw_ioctl,
			argp, sizeof(raw_ioctl))) {
			up(&misc_data->raw_data_lock);
			printk(KERN_INFO
				"[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		misc_data->update = 0;

		u8Data = (u8 *)&misc_data->cur_data[0];
		if (misc_data->raw_mode_flag == BT404_CAL_N_COUNT) {

			j = 0;
			total_cal_n =
				misc_data->cap_info.total_cal_n;
			if (total_cal_n == 0)
				total_cal_n = 160;
			div_node =
				(u8)misc_data->cap_info.max_y_node;
			if (div_node == 0)
				div_node = 16;
			u8Data = (u8 *)&misc_data->cur_data[0];
			for (i = 0; i < total_cal_n; i++) {
				if ((i*2)%div_node <
					misc_data->cap_info.y_node_num) {
					misc_data->ref_data[j*2] =
						(u16)u8Data[i*2];
					misc_data->ref_data[j*2+1]	=
						(u16)u8Data[i*2+1];
					j++;
				}
			}

			u8Data = (u8 *)&misc_data->ref_data[0];
		}

		if (copy_to_user(raw_ioctl.buf, (u8 *)u8Data,
			raw_ioctl.sz)) {
			up(&misc_data->raw_data_lock);
			return -1;
		}

		up(&misc_data->raw_data_lock);
		return 0;

	default:
		break;
	}
	return 0;
}
#endif /* USE_TEST_RAW_TH_DATA_MODE */

/*
 * funcitons for factory test
 */

static ssize_t back_key_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 addr = 0x00A9;
	u16 val = 0xffff;
	int ret;

	/* back key */
	ret = bt404_ts_read_data(client, addr, (u8 *)&val, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (key event)\n", __func__);
		return -1;
	}

	return snprintf(buf, 5, "%d", val);
}

static ssize_t menu_key_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 addr = 0x00A8;
	u16 val = 0xffff;
	int ret;

	/* menu key */
	ret = bt404_ts_read_data(client, addr, (u8 *)&val, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (key event)\n", __func__);
		return -1;
	}

	return snprintf(buf, 5, "%d", val);
}

#ifdef TSP_FACTORY

static void set_default_result(struct bt404_ts_data *data)
{
	char delim = ':';

	memset(data->cmd_result, 0x00, ARRAY_SIZE(data->cmd_result));
	memset(data->cmd_buff, 0x00, ARRAY_SIZE(data->cmd_buff));
	memcpy(data->cmd_result, data->cmd, strlen(data->cmd));
	strncat(data->cmd_result, &delim, 1);
}

static void set_cmd_result(struct bt404_ts_data *data, char *buff, int len)
{
	strncat(data->cmd_result, buff, len);
}

static ssize_t cmd_store(struct device *dev, struct device_attribute *devattr,
						const char *buf, size_t count)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	char *cur, *start, *end;
	char buff[TSP_CMD_STR_LEN] = {0, };
	int len, i;
	struct tsp_cmd *tsp_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;

	if (data->cmd_is_running == true) {
		dev_err(&client->dev, "%s: other cmd is running.\n", __func__);
		goto err_out;
	}

	/* check lock  */
	mutex_lock(&data->cmd_lock);
	data->cmd_is_running = true;
	mutex_unlock(&data->cmd_lock);

	data->cmd_state = 1;

	for (i = 0; i < ARRAY_SIZE(data->cmd_param); i++)
		data->cmd_param[i] = 0;

	len = (int)count;
	if (*(buf + len - 1) == '\n')
		len--;
	memset(data->cmd, 0x00, ARRAY_SIZE(data->cmd));
	memcpy(data->cmd, buf, len);

	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(buff, buf, cur - buf);
	else
		memcpy(buff, buf, len);

	/* find command */
	list_for_each_entry(tsp_cmd_ptr, &data->cmd_list_head, list) {
		if (!strcmp(buff, tsp_cmd_ptr->cmd_name)) {
			cmd_found = true;
			break;
		}
	}

	/* set not_support_cmd */
	if (!cmd_found) {
		list_for_each_entry(tsp_cmd_ptr, &data->cmd_list_head, list) {
			if (!strcmp("not_support_cmd", tsp_cmd_ptr->cmd_name))
				break;
		}
	}

	/* parsing TSP standard tset parameter */
	if (cur && cmd_found) {
		cur++;
		start = cur;
		do {
			if (*cur == delim || cur - buf == len) {
				end = cur;
				memcpy(buff, start, end - start);
				*(buff + strlen(buff)) = '\0';
				data->cmd_param[param_cnt] =
					(int)simple_strtol(buff, NULL, 10);
				start = cur + 1;
				memset(buff, 0x00, ARRAY_SIZE(buff));
				param_cnt++;
			}
			cur++;
		} while (cur - buf <= len);
	}

	dev_info(&client->dev, "cmd = %s\n", tsp_cmd_ptr->cmd_name);
	for (i = 0; i < param_cnt; i++)
		dev_info(&client->dev, "cmd param %d= %d\n", i,
							data->cmd_param[i]);

	tsp_cmd_ptr->cmd_func(data);

err_out:
	return count;
}

static ssize_t cmd_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	char buff[16];

	dev_info(&client->dev, "%s: check status:%d\n", __func__,
							data->cmd_state);

	switch (data->cmd_state) {
	case WAITING:
		sprintf(buff, "%s", TOSTRING(WAITING));
		break;
	case RUNNING:
		sprintf(buff, "%s", TOSTRING(RUNNING));
		break;
	case OK:
		sprintf(buff, "%s", TOSTRING(OK));
		break;
	case FAIL:
		sprintf(buff, "%s", TOSTRING(FAIL));
		break;
	case NOT_APPLICABLE:
		sprintf(buff, "%s", TOSTRING(NOT_APPLICABLE));
		break;
	default:
		sprintf(buff, "%s", TOSTRING(NOT_APPLICABLE));
		break;
	}

	return sprintf(buf, "%s\n", buff);
}

static ssize_t cmd_result_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_result, strlen(data->cmd_result));

	mutex_lock(&data->cmd_lock);
	data->cmd_is_running = false;
	mutex_unlock(&data->cmd_lock);

	data->cmd_state = 0;

	return sprintf(buf, "%s", data->cmd_result);
}

static void fw_update(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int ret;
	int fw_type = data->cmd_param[0];

	set_default_result(data);

	if (!data->enabled)
		goto out;

	if (fw_type != 0)
		goto out;

	/* to do :
	 * disable all interrupt in IC & clear interrupt buffer
	 */

	down(&data->work_lock);
	disable_irq(data->irq);

	ret = bt404_ts_init_device(data, true);

	enable_irq(data->irq);
	up(&data->work_lock);

	sprintf(data->cmd_buff, "%s", (ret) ? "OK" : "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));

	if (ret)
		data->cmd_state = 2;
	else
		data->cmd_state = 3;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read fw ver\n", __func__);
	return ;
}

static void get_fw_ver_bin(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	u16 buff;

	set_default_result(data);

	buff = (u16)(data->fw_data[FW_VER_OFFSET + 2]
				| (data->fw_data[FW_VER_OFFSET + 3] << 8));

	sprintf(data->cmd_buff, "0x%X", buff);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));
	return;
}

static void get_fw_ver_ic(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	u16 buff;
	int ret;

	set_default_result(data);

	buff = 0xffff;
	ret = bt404_ts_read_data(client, BT404_REG_VER,
					(u8 *)&buff, 2);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s: err: rd (reg ver)\n",
								__func__);
		goto out;
	}

	sprintf(data->cmd_buff, "0x%X", buff);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read fw ver\n", __func__);
	return ;
}

static void get_threshold(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	u16 buff;
	int ret;

	set_default_result(data);

	buff = 0xffff;
	ret = bt404_ts_read_data(client, BT404_THRESHOLD, (u8 *)&buff, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (reg ver)\n",
								__func__);
		goto out;
	}

	sprintf(data->cmd_buff, "%d", buff);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read fw ver\n", __func__);
	return ;
}

static void module_off_master(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;

	set_default_result(data);

	down(&data->work_lock);

	if (!data->enabled) {
		sprintf(data->cmd_buff, "%s", "NG");
		set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
		data->cmd_state = 3;
		goto out;
	}

	data->enabled = false;
	disable_irq(data->irq);

	bt404_ts_power(data, POWER_OFF);
	mdelay(CHIP_POWER_OFF_DELAY);

	up(&data->work_lock);

	sprintf(data->cmd_buff, "%s", "OK");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

out:
	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;
}

static void module_on_master(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int ret;

	set_default_result(data);

	down(&data->work_lock);

	if (data->enabled) {
		sprintf(data->cmd_buff, "%s", "NG");
		set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
		data->cmd_state = 3;
		goto out;
	}

	bt404_ts_power(data, POWER_ON);
	mdelay(CHIP_ON_DELAY);

	ret = bt404_ts_resume_device(data);

	data->enabled = true;
	enable_irq(data->irq);

	up(&data->work_lock);

	sprintf(data->cmd_buff, "%s", (ret) ? "OK" : "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));

	if (ret)
		data->cmd_state = 2;
	else
		data->cmd_state = 3;

out:
	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;
}

static void get_chip_vendor(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	set_default_result(data);

	sprintf(data->cmd_buff, "%s", "zinitix");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));
}

static void get_chip_name(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	set_default_result(data);

	sprintf(data->cmd_buff, "%s", "bt404");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
					data->cmd_buff,	strlen(data->cmd_buff));
}

static void get_x_num(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	u16 buff;
	int ret;

	set_default_result(data);

	ret = bt404_ts_read_data(client, BT404_TOTAL_NUMBER_OF_X,
								(u8 *)&buff, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (reg ver)\n",
								__func__);
		goto out;
	}

	buff = buff - 1; /* the lastest tx channel is used for touchkey*/
	sprintf(data->cmd_buff, "%d", buff);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
					data->cmd_buff,	strlen(data->cmd_buff));

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read x num\n", __func__);
	return ;
}

static void get_y_num(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	u16 buff;
	int ret;

	set_default_result(data);

	ret = bt404_ts_read_data(client, BT404_TOTAL_NUMBER_OF_Y,
								(u8 *)&buff, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (reg ver)\n",
								__func__);
		goto out;
	}

	sprintf(data->cmd_buff, "%d", buff);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read y num\n", __func__);
	return ;
}

static int bt404_ts_read_raw_datas(struct bt404_ts_data *data, u16 type)
{
	struct i2c_client *client = data->client;
	int i, j;
	int ret;
	u16 old_mode;
	u8 *cur;
	int num;
	u16 buf[TSP_CMD_NODE_NUM] = {0, };

/* raw data type checking */

	ret = bt404_ts_read_data(client, BT404_TOUCH_MODE, (u8 *)&old_mode, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (touch mode)\n", __func__);
		goto out;
	}

	ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE,
						type);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (touch mode)\n",
							__func__);
		goto out;
	}

	for (i = 0; i < 3; i++) {
		bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		msleep(100);
	}

	while (true) {
		if (!gpio_get_value(data->pdata->gpio_int))
			break;
		msleep(1);
	}

	if (type == BT404_CAL_N_COUNT) {
		ret = bt404_ts_read_data(client, BT404_MAX_Y_NUM,
					(u8 *)&data->cap_info.max_y_node, 2);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err: rd (max y node)\n",
								__func__);
			goto out;
		}
		dev_info(&client->dev, "%s: max_y_node=%d\n", __func__,
						data->cap_info.max_y_node);

		ret = bt404_ts_read_data(client, BT404_CAL_N_TOTAL_NUM,
					(u8 *)&data->cap_info.total_cal_n, 2);
		if (ret < 0) {
			dev_err(&client->dev, "%s: err: rd (max y node)\n",
								__func__);
			goto out;
		}
		dev_info(&client->dev, "%s: total_cal_n=%d\n", __func__,
						data->cap_info.total_cal_n);
	}

	num = (type == BT404_CAL_N_COUNT) ?
				data->cap_info.total_cal_n : TSP_CMD_NODE_NUM;

	ret = bt404_ts_read_raw_data(data->client, BT404_RAWDATA_REG,
					(char *)buf, sizeof(buf[0]) * num);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: rd (raw data)\n", __func__);
		goto out;
	}

	ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE, old_mode);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (touch mode)\n",
								__func__);
		goto out;
	}

	for (i = 0; i < 3; i++) {
		bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		msleep(100);
	}

	if (type == BT404_CAL_N_COUNT) {
		j = 0;
		cur = (u8 *)&buf[0];
		for (i = 0; i < data->cap_info.total_cal_n; i++) {
			if ((i * 2) % data->cap_info.max_y_node <
							TSP_CMD_Y_NUM) {
				data->raw_cal_n_count[j++] = (u16)cur[i * 2];
				data->raw_cal_n_count[j++] =
							(u16)cur[i * 2 + 1];
				msleep(1);
				if (j >= TSP_CMD_NODE_NUM)
					break;
			}
		}
	} else {
		for (i = 0; i < num; i++) {
			switch (type) {
			case BT404_PROCESSED_DATA:
				data->raw_processed_data[i] = buf[i];
				break;
			case BT404_CAL_N_DATA:
				data->raw_cal_n_data[i] = buf[i];
				break;
			default:
				dev_err(&client->dev,
					"%s: not proper raw data type (%d)\n",
						__func__, type);
				ret = EINVAL;
				goto out;
			}
		}
	}

	ret = 0;

	dev_info(&client->dev, "%s: raw data (%d) is sucessfully readed\n",
						__func__, type);
out:
	return ret;
}

static void get_cal_n_data(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int idx;
	int x = data->cmd_param[0];
	int y = data->cmd_param[1];

	set_default_result(data);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM)
		goto out;

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(data->cmd_buff, "%d", data->raw_cal_n_data[idx]);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d),(%d,%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff), x, y);

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read raw_cal_n_data\n", __func__);
	return ;
}

static void get_cal_n_count(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int idx;
	int x = data->cmd_param[0];
	int y = data->cmd_param[1];

	set_default_result(data);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM)
		goto out;

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(data->cmd_buff, "%d", data->raw_cal_n_count[idx]);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d),(%d,%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff), x, y);

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read raw_cal_n_count\n", __func__);
	return ;
}

static void get_processed_data(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;

	int idx;
	int x = data->cmd_param[0];
	int y = data->cmd_param[1];

	set_default_result(data);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM)
		goto out;

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(data->cmd_buff, "%d", data->raw_processed_data[idx]);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 2;

	dev_info(&client->dev, "%s: \"%s\"(%d),(%d,%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff), x, y);

	return;

out:
	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 3;

	dev_info(&client->dev, "%s: fail to read raw_processed_data\n",
								__func__);
	return ;
}

static void run_cal_n_data_read(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int i, j;
	int ret;
	int raw_min, raw_max;
	u16 raw_data;

	set_default_result(data);

	disable_irq(data->irq);
	down(&data->work_lock);

	memset(data->raw_cal_n_data, 0x00, sizeof(data->raw_cal_n_data[0]) *
					ARRAY_SIZE(data->raw_cal_n_data));

	ret = bt404_ts_read_raw_datas(data, BT404_CAL_N_DATA);
	if (ret < 0)
		goto err;

	raw_min = raw_max = data->raw_cal_n_data[0];

	for (i = 0; i < TSP_CMD_X_NUM; i++) {
		for (j = 0; j < TSP_CMD_Y_NUM; j++) {

			raw_data = data->raw_cal_n_data[i * TSP_CMD_Y_NUM + j];
/*
			dev_info(&client->dev, "%2d,%2d: %d\n", i, j, raw_data);
			msleep(1);
*/
			if (i < TSP_CMD_X_NUM - 1) {
				if (raw_max < raw_data)
					raw_max = raw_data;

				if (raw_min > raw_data)
					raw_min = raw_data;
			}
		}
	}

	up(&data->work_lock);
	enable_irq(data->irq);

	sprintf(data->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;
err:
	up(&data->work_lock);
	enable_irq(data->irq);

	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = FAIL;

	dev_err(&client->dev, "%s: fail to read can_n_data\n", __func__);
	return ;
}

static void run_cal_n_count_read(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int i, j;
	int ret;
	int raw_min, raw_max;
	u16 raw_data;

	set_default_result(data);

	disable_irq(data->irq);
	down(&data->work_lock);

	memset(data->raw_cal_n_count, 0x00, sizeof(data->raw_cal_n_count[0]) *
					ARRAY_SIZE(data->raw_cal_n_count));

	ret = bt404_ts_read_raw_datas(data, BT404_CAL_N_COUNT);
	if (ret < 0)
		goto err;

	raw_min = raw_max = data->raw_cal_n_count[0];

	for (i = 0; i < TSP_CMD_X_NUM; i++) {
		for (j = 0; j < TSP_CMD_Y_NUM; j++) {

			raw_data = data->raw_cal_n_count[i * TSP_CMD_Y_NUM + j];
/*
			dev_info(&client->dev, "%2d,%2d: %d\n", i, j, raw_data);
			msleep(1);
*/
			if (i < TSP_CMD_X_NUM - 1) {
				if (raw_max < raw_data)
					raw_max = raw_data;

				if (raw_min > raw_data)
					raw_min = raw_data;
			}
		}
	}

	up(&data->work_lock);
	enable_irq(data->irq);

	sprintf(data->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;
err:
	up(&data->work_lock);
	enable_irq(data->irq);

	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = FAIL;

	dev_err(&client->dev, "%s: fail to read can_n_count\n", __func__);
	return ;
}

static void run_processed_data_read(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	int i, j;
	int ret;
	int raw_min, raw_max;
	u16 raw_data;

	set_default_result(data);

	disable_irq(data->irq);
	down(&data->work_lock);

	memset(data->raw_processed_data, 0x00,
					sizeof(data->raw_processed_data[0]) *
					ARRAY_SIZE(data->raw_cal_n_count));

	ret = bt404_ts_read_raw_datas(data, BT404_PROCESSED_DATA);
	if (ret < 0)
		goto err;

	raw_min = raw_max = data->raw_processed_data[0];

	for (i = 0; i < TSP_CMD_X_NUM; i++) {
		for (j = 0; j < TSP_CMD_Y_NUM; j++) {

			raw_data =
				data->raw_processed_data[i * TSP_CMD_Y_NUM + j];
/*
			dev_info(&client->dev, "%2d,%2d: %d\n", i, j, raw_data);
			msleep(1);
*/
			if (i < TSP_CMD_X_NUM - 1) {
				if (raw_max < raw_data)
					raw_max = raw_data;

				if (raw_min > raw_data)
					raw_min = raw_data;
			}
		}
	}

	up(&data->work_lock);
	enable_irq(data->irq);

	sprintf(data->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));

	return;
err:
	up(&data->work_lock);
	enable_irq(data->irq);

	sprintf(data->cmd_buff, "%s", "NG");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = FAIL;

	dev_err(&client->dev, "%s: fail to read raw_processed_data\n",
								__func__);
	return ;
}

static void not_support_cmd(void *device_data)
{
	struct bt404_ts_data *data = (struct bt404_ts_data *)device_data;
	struct i2c_client *client = data->client;
	set_default_result(data);
	sprintf(data->cmd_buff, "%s", "NA");
	set_cmd_result(data, data->cmd_buff, strlen(data->cmd_buff));
	data->cmd_state = 4;
	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				data->cmd_buff,	strlen(data->cmd_buff));
	return;
}

#endif

static ssize_t fw_ver_kernel_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u16 buff;

	buff = (u16)(data->fw_data[FW_VER_OFFSET + 2]
				| (data->fw_data[FW_VER_OFFSET + 3] << 8));
	dev_info(&client->dev, "%s: \"0x%X\"\n", __func__, buff);
	return sprintf(buf, "0x%X\n", buff);
}

static ssize_t fw_ver_ic_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bt404_ts_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u16 buff;
	int ret;

	buff = 0xffff;
	ret = bt404_ts_read_data(data->client, BT404_REG_VER,
					(u8 *)&buff, 2);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s: err: rd (reg ver)\n",
								__func__);
		buff = 0;
		goto out;
	}
	dev_info(&client->dev, "%s: \"0x%X\"\n", __func__, buff);

out:
	return sprintf(buf, "0x%X\n", buff);
}


static DEVICE_ATTR(touchkey_back, S_IRUGO, back_key_state_show, NULL);
static DEVICE_ATTR(touchkey_menu, S_IRUGO, menu_key_state_show, NULL);
static struct attribute *touchkey_attributes[] = {
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_menu.attr,
	NULL,
};
static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};
#ifdef TSP_FACTORY

static DEVICE_ATTR(cmd, S_IWUSR | S_IWGRP, NULL, cmd_store);
static DEVICE_ATTR(cmd_status, S_IRUGO, cmd_status_show, NULL);
static DEVICE_ATTR(cmd_result, S_IRUGO, cmd_result_show, NULL);

static struct attribute *touchscreen_attributes[] = {
	&dev_attr_cmd.attr,
	&dev_attr_cmd_status.attr,
	&dev_attr_cmd_result.attr,
	NULL,
};

static struct attribute_group touchscreen_attr_group = {
	.attrs = touchscreen_attributes,
};
#endif
static DEVICE_ATTR(tsp_firm_version_panel, S_IRUGO, fw_ver_ic_temp_show,
									NULL);
static DEVICE_ATTR(tsp_firm_version_phone, S_IRUGO, fw_ver_kernel_temp_show,
									NULL);

static struct attribute *touchscreen_temp_attributes[] = {
	&dev_attr_tsp_firm_version_panel.attr,
	&dev_attr_tsp_firm_version_phone.attr,
	NULL,
};

static struct attribute_group touchscreen_temp_attr_group = {
	.attrs = touchscreen_temp_attributes,
};

static int bt404_ts_probe(struct i2c_client *client,
					const struct i2c_device_id *i2c_id)
{
	struct bt404_ts_platform_data *pdata = client->dev.platform_data;
	struct bt404_ts_data *data;
	struct input_dev *input_dev_ts;
	struct input_dev *input_dev_tk;
	int ret;
	int i;
#ifdef TSP_FACTORY
	struct device *fac_dev_ts;
#endif
	struct device *fac_dev_tk;
	struct device *fac_dev_ts_temp;

	extern unsigned int lcd_type;

	if (!lcd_type) {
		dev_err(&client->dev, "touch screen is not connected.(%d)\n",
								lcd_type);
		return -ENODEV;
	}

	if (!pdata) {
		dev_err(&client->dev, "no platform data\n");
		ret = -EINVAL;
		goto err_platform_data;
	}

	if (!strcmp(client->name, BT404_ISP_DEVICE)) {
		struct i2c_adapter *isp_adapter =
					to_i2c_adapter(client->dev.parent);
		if (!pdata->put_isp_i2c_client) {
			dev_err(&client->dev, "can't set isp i2c client\n");
			ret = -EINVAL;
			goto err_put_isp_i2c_client;
		}
		pdata->put_isp_i2c_client(client);
		i2c_lock_adapter(isp_adapter);
		return 0;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "not compatible i2c function\n");
		ret = -ENODEV;
		goto err_check_functionality;
	}

	data = kzalloc(sizeof(struct bt404_ts_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "fail to alloc device data\n");
		ret = -ENOMEM;
		goto err_alloc_dev_data;
	}

	if (!strcmp(client->name, BT404_TS_DEVICE)) {
		if (!pdata->get_isp_i2c_client) {
			dev_err(&client->dev, "can't get isp i2c client\n");
			ret = -EINVAL;
			goto err_get_isp_i2c_client;
		}
		data->isp_client = pdata->get_isp_i2c_client();
	}

	data->pdata = pdata;
/*	data->cap_info.x_max = pdata->x_max - 1;
	data->cap_info.y_max = pdata->y_max - 1;
*/
	data->cap_info.x_max = pdata->x_max;
	data->cap_info.y_max = pdata->y_max;

	data->client = client;
	i2c_set_clientdata(client, data);

#if USE_TEST_RAW_TH_DATA_MODE
	/* not test mode */
	data->raw_mode_flag = TOUCH_NORMAL_MODE;
#endif

	input_dev_ts = input_allocate_device();
	if (!input_dev_ts) {
		dev_err(&client->dev, "failed to allco input_ts\n");
		ret = -ENOMEM;
		goto err_input_ts_alloc;
	}
	data->input_dev_ts = input_dev_ts;

	snprintf(data->phys_ts, sizeof(data->phys_ts),
					"%s/input0", dev_name(&client->dev));
	input_dev_ts->name = "sec_touchscreen";
	input_dev_ts->id.bustype = BUS_I2C;
	input_dev_ts->phys = data->phys_ts;
	input_dev_ts->dev.parent = &client->dev;

	set_bit(EV_ABS, input_dev_ts->evbit);
	set_bit(INPUT_PROP_DIRECT, input_dev_ts->propbit);

	input_mt_init_slots(input_dev_ts, REAL_SUPPORTED_FINGER_NUM);

	data->cap_info.orientation = pdata->orientation;
	if (data->cap_info.orientation & TOUCH_XY_SWAP) {
		input_set_abs_params(input_dev_ts, ABS_MT_POSITION_Y,
				data->cap_info.x_min,
				data->cap_info.x_max + ABS_PT_OFFSET, 0, 0);
		input_set_abs_params(input_dev_ts, ABS_MT_POSITION_X,
				data->cap_info.y_min,
				data->cap_info.y_max + ABS_PT_OFFSET, 0, 0);
	} else {
		input_set_abs_params(input_dev_ts, ABS_MT_POSITION_X,
				data->cap_info.x_min,
				data->cap_info.x_max + ABS_PT_OFFSET, 0, 0);
		input_set_abs_params(input_dev_ts, ABS_MT_POSITION_Y,
				data->cap_info.y_min,
				data->cap_info.y_max + ABS_PT_OFFSET, 0, 0);
	}
	input_set_abs_params(input_dev_ts, ABS_MT_TOUCH_MAJOR, 0, 0xff, 0, 0);
	input_set_abs_params(input_dev_ts, ABS_MT_WIDTH_MAJOR, 0, 0xff, 0, 0);

	input_set_drvdata(input_dev_ts, data);

	ret = input_register_device(data->input_dev_ts);
	if (ret) {
		dev_err(&client->dev, "unable to register %s input device\n",
						data->input_dev_ts->name);
		goto err_input_ts_register;
	}

	input_dev_tk = input_allocate_device();
	if (!input_dev_tk) {
		dev_err(&client->dev, "failed to allco input_tk\n");
		ret = -ENOMEM;
		goto err_input_tk_alloc;
	}
	data->input_dev_tk = input_dev_tk;

	snprintf(data->phys_tk, sizeof(data->phys_tk),
					"%s/input0", dev_name(&client->dev));
	input_dev_tk->name = "sec_touchkey";
	input_dev_tk->id.bustype = BUS_I2C;
	input_dev_tk->phys = data->phys_ts;
	input_dev_tk->dev.parent = &client->dev;

	set_bit(EV_KEY, input_dev_tk->evbit);
	set_bit(EV_LED, input_dev_tk->evbit);
	set_bit(LED_MISC, input_dev_tk->ledbit);
	set_bit(EV_SYN, input_dev_tk->evbit);

	for (i = 0; i < pdata->num_buttons; i++)
		set_bit(pdata->button_map[i], input_dev_tk->keybit);

	input_set_drvdata(input_dev_tk, data);

	ret = input_register_device(data->input_dev_tk);
	if (ret) {
		dev_err(&client->dev, "unable to register %s input device\n",
						data->input_dev_tk->name);
		goto err_input_tk_register;
	}

	dev_info(&client->dev, "panel_type=%d\n", data->pdata->panel_type);
	if (data->pdata->panel_type == EX_CLEAR_PANEL)
		data->fw_data = fw_data_ex_clear;
	else if (data->pdata->panel_type == GFF_PANEL)
		data->fw_data = fw_data_gff;

	if (data->pdata->power_con == LDO_CON) {
		gpio_direction_output(pdata->gpio_ldo_en, 1);
		mdelay(CHIP_ON_DELAY);
		dev_info(&client->dev, "ldo power control(%d)\n",
						data->pdata->power_con);
	} else if (data->pdata->power_con == PMIC_CON) {
		data->reg_3v3 = regulator_get(&client->dev, "v-tsp-3.3");
		if (IS_ERR(data->reg_3v3)) {
			ret = PTR_ERR(data->reg_3v3);
			dev_err(&client->dev,
				"fail to get regulator v3.3 (%d)\n", ret);
			goto err_get_reg_3v3;
		}
		ret = regulator_set_voltage(data->reg_3v3, 3300000, 3300000);

		data->reg_1v8 = regulator_get(&client->dev, "v-tsp-1.8");
		if (IS_ERR(data->reg_1v8)) {
			ret = PTR_ERR(data->reg_1v8);
			dev_err(&client->dev,
				"fail to get regulator v1.8 (%d)\n", ret);
			goto err_get_reg_1v8;
		}
		regulator_set_voltage(data->reg_1v8, 1800000, 1800000);

		ret = regulator_enable(data->reg_3v3);
		if (ret){
			dev_err(&client->dev,
				"fail to enable regulator v3.3 (%d)\n", ret);
			goto err_reg_en_3v3;
		}

		ret = regulator_enable(data->reg_1v8);
		if (ret){
			dev_err(&client->dev,
				"fail to enable regulator v1.8 (%d)\n", ret);		
			goto err_reg_en_1v8;
		}

		dev_info(&client->dev, "pmic power control(%d)\n",
						data->pdata->power_con);
	}

	bt404_ts_init_device(data, false);

	bt404_ts_power(data, POWER_OFF);
	mdelay(CHIP_POWER_OFF_DELAY);
	bt404_ts_power(data, POWER_ON);
	mdelay(CHIP_ON_DELAY);
	bt404_ts_resume_device(data);

	data->work_state = NOTHING;
	sema_init(&data->work_lock, 1);

	data->irq = client->irq;

	if (data->irq) {
		ret = request_threaded_irq(data->irq, NULL, bt404_ts_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT, BT404_TS_DEVICE,
									data);
		if (ret) {
			dev_err(&client->dev, "fail to request irq (%d).\n",
								data->irq);
			goto err_request_irq;
		}
	}
	data->enabled = true;

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = bt404_ts_early_suspend;
	data->early_suspend.resume = bt404_ts_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	INIT_LIST_HEAD(&data->cmd_list_head);
	for (i = 0; i < ARRAY_SIZE(tsp_cmds); i++)
		list_add_tail(&tsp_cmds[i].list, &data->cmd_list_head);

#if	USE_TEST_RAW_TH_DATA_MODE
	sema_init(&data->raw_data_lock, 1);
	misc_data = data;
	ret = misc_register(&touch_misc_device);
	if (ret)
		debug_msg("Fail to register touch misc device.\n");

	if (device_create_file(touch_misc_device.this_device,
		&dev_attr_get_touch_raw_data) < 0)
		dev_err(&client->dev, "Failed to create device file(%s)!\n",
			dev_attr_get_touch_raw_data.attr.name);

#endif

	mutex_init(&data->cmd_lock);
	data->cmd_is_running = false;

	fac_dev_tk = device_create(sec_class, NULL, 0, data, "sec_touchkey");
	if (!fac_dev_tk) {
		dev_err(&client->dev, "Failed to create fac touchkey dev\n");
		ret = -ENODEV;
		goto err_create_sysfs;
	}

	ret = sysfs_create_group(&fac_dev_tk->kobj, &touchkey_attr_group);
	if (ret)
		dev_err(&client->dev,
			"Failed to create sysfs (touchkey_attr_group).\n");
#ifdef TSP_FACTORY
	fac_dev_ts = device_create(sec_class, NULL, 0, data, "tsp");
	if (!fac_dev_ts) {
		dev_err(&client->dev, "Failed to create fac tsp dev\n");
		ret = -ENODEV;
		goto err_create_sysfs;
	}

	ret = sysfs_create_group(&fac_dev_ts->kobj, &touchscreen_attr_group);
	if (ret)
		dev_err(&client->dev,
			"Failed to create sysfs (touchscreen_attr_group).\n");
#endif
	fac_dev_ts_temp = device_create(sec_class, NULL, 0, data,
							"sec_touchscreen");
	if (!fac_dev_ts_temp) {
		dev_err(&client->dev, "Failed to create fac tsp dev\n");
		ret = -ENODEV;
		goto err_create_sysfs;
	}

	ret = sysfs_create_group(&fac_dev_ts_temp->kobj,
				&touchscreen_temp_attr_group);
	if (ret)
		dev_err(&client->dev,
			"Failed to create sysfs (touchscreen_temp_attr_group)."
									"\n");
	dev_info(&client->dev, "successfully probed.\n");
	return 0;

err_create_sysfs:
err_request_irq:
	regulator_disable(data->reg_1v8);
err_reg_en_1v8:
	regulator_disable(data->reg_3v3);	
err_reg_en_3v3:
	regulator_put(data->reg_1v8);
err_get_reg_1v8:
	regulator_put(data->reg_3v3);
err_get_reg_3v3:
	input_unregister_device(data->input_dev_tk);
err_input_tk_register:
	input_free_device(data->input_dev_tk);
err_input_tk_alloc:
	input_unregister_device(data->input_dev_ts);
err_input_ts_register:
	input_free_device(data->input_dev_ts);
err_input_ts_alloc:
err_get_isp_i2c_client:
	kfree(data);
err_alloc_dev_data:
err_check_functionality:
err_put_isp_i2c_client:
	dev_err(&client->dev, "Failed to probe. power off & exit.\n");
	gpio_set_value(pdata->gpio_ldo_en, 0);
err_platform_data:
	return ret;
}

static int bt404_ts_remove(struct i2c_client *client)
{
	struct bt404_ts_data *data = i2c_get_clientdata(client);

	disable_irq(data->irq);
	down(&data->work_lock);

	data->work_state = REMOVE;

	if (data->irq)
		free_irq(data->irq, data);

#if USE_TEST_RAW_TH_DATA_MODE
	device_remove_file(touch_misc_device.this_device,
		&dev_attr_get_touch_raw_data);
	misc_deregister(&touch_misc_device);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif
	mutex_destroy(&data->cmd_lock);
	input_unregister_device(data->input_dev_ts);
	input_unregister_device(data->input_dev_tk);
	input_free_device(data->input_dev_ts);
	input_free_device(data->input_dev_tk);

	regulator_force_disable(data->reg_3v3);	
	regulator_put(data->reg_3v3);
	regulator_force_disable(data->reg_1v8);	
	regulator_put(data->reg_1v8);

	up(&data->work_lock);
	kfree(data);

	return 0;
}

static int bt404_ts_resume_device(struct bt404_ts_data *data)
{
	struct i2c_client *client = data->client;
	int ret;
	int i;

	bt404_ts_sw_reset(data);

	ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG, 0x0);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (int enable)\n",
								__func__);
		goto err_i2c;
	}
	ret = bt404_ts_write_reg(client, BT404_SUPPORTED_FINGER_NUM,
						(u16)MAX_SUPPORTED_FINGER_NUM);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (max supprot finger)\n",
								__func__);
		goto err_i2c;
	}
	ret = bt404_ts_write_reg(client, BT404_X_RESOLUTION,
						(u16)(data->cap_info.x_max));
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (x resolution)\n",
								__func__);
		goto err_i2c;
	}

	ret = bt404_ts_write_reg(client, BT404_Y_RESOLUTION,
						(u16)(data->cap_info.y_max));
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (y resolution)\n",
								__func__);
		goto err_i2c;
	}

	ret = bt404_ts_write_reg(client, BT404_TOUCH_MODE, TOUCH_MODE);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (touch mode)\n",
								__func__);
		goto err_i2c;
	}

	ret = bt404_ts_write_cmd(client, BT404_CAL_CMD);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt cmd (cal)\n", __func__);
		goto err_i2c;
	}

	ret = bt404_ts_write_reg(client, BT404_INT_ENABLE_FLAG,
				data->cap_info.chip_int_mask);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err: wt reg (int en)\n", __func__);
		goto err_i2c;
	}

	for (i = 0; i < 5; i++) {
		bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
		udelay(10);
	}

	ret = 1;
err_i2c:
	return ret;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
static int bt404_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bt404_ts_data *data = i2c_get_clientdata(client);
	int ret;

	if (!data->enabled) {
		dev_err(dev, "%s, already disabled\n", __func__);
		ret = -1;
		goto out;
	}

	disable_irq(data->irq);
	data->enabled = false;

	bt404_ts_report_touch_data(data, true);
	udelay(100);

	ret = bt404_ts_write_cmd(client, BT404_CLEAR_INT_STATUS_CMD);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt cmd (clear int)\n", __func__);
		ret = -1;
		goto out;
	}

	bt404_ts_power(data, POWER_OFF);
	/* The delay is moved resume function
	mdelay(CHIP_POWER_OFF_DELAY); */

	ret = 0;
out:
	dev_info(dev, "suspended.\n");
	return ret;
}

static int bt404_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bt404_ts_data *data = i2c_get_clientdata(client);
	int ret;

	if (data->enabled) {
		dev_err(dev, "%s, already enabled\n", __func__);
		ret = -1;
		goto out;
	}

	data->pdata->int_set_pull(true);
	data->enabled = true;

	mdelay(CHIP_POWER_OFF_DELAY);
	bt404_ts_power(data, POWER_ON);
	mdelay(CHIP_ON_DELAY);

/*
	ret = bt404_ts_write_cmd(data->client, BT404_WAKEUP_CMD);
	if (ret < 0) {
		dev_err(dev, "%s: err: wt cmd (wake-up)\n", __func__);
		ret = -1;
		goto out;
	}
	mdelay(10);

	if (ts_mini_init_touch(data) == false)
		goto out;
*/
	bt404_ts_resume_device(data);
	ret = 0;

	enable_irq(data->irq);
out:
	dev_info(dev, "resumed.\n");
	return ret;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bt404_ts_late_resume(struct early_suspend *h)
{
	struct bt404_ts_data *data =
			container_of(h, struct bt404_ts_data, early_suspend);
	bt404_ts_resume(&data->client->dev);
}

static void bt404_ts_early_suspend(struct early_suspend *h)
{
	struct bt404_ts_data *data =
			container_of(h, struct bt404_ts_data, early_suspend);
	bt404_ts_suspend(&data->client->dev);
}

#endif	/* CONFIG_HAS_EARLYSUSPEND */

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops bt404_ts_pm_ops = {
	.suspend	= bt404_ts_suspend,
	.resume		= bt404_ts_resume,
};
#endif

static struct i2c_device_id bt404_id[] = {
	{BT404_ISP_DEVICE, 0},
	{BT404_TS_DEVICE, 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, bt404_id);

static struct i2c_driver bt404_ts_driver = {
	.probe		= bt404_ts_probe,
	.remove		= bt404_ts_remove,
	.id_table	= bt404_id,
	.driver		= {
		.name	= BT404_TS_DEVICE,
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm	= &bt404_ts_pm_ops,
#endif
	},
};

static int __devinit bt404_ts_init(void)
{
	return i2c_add_driver(&bt404_ts_driver);
}

static void __exit bt404_ts_exit(void)
{
	i2c_del_driver(&bt404_ts_driver);
}

module_init(bt404_ts_init);
module_exit(bt404_ts_exit);

MODULE_DESCRIPTION("touch-screen device driver using i2c interface");
MODULE_AUTHOR("<tyoony.yoon@samsung.com>");
MODULE_LICENSE("GPL");
