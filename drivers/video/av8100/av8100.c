/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * AV8100 driver
 *
 * Author: Per Persson <per.xb.persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/regulator/consumer.h>
#include <linux/earlysuspend.h>
#include <linux/mfd/dbx500-prcmu.h>

#include "av8100_regs.h"
#include <video/av8100.h>
#include <video/hdmi.h>
#include <linux/firmware.h>

#define AV8100_FW_FILENAME "av8100.fw"
#define CUT_STR_0 "2.1"
#define CUT_STR_1 "2.2"
#define CUT_STR_3 "2.3"
#define CUT_STR_30 "3.0"
#define CUT_STR_UNKNOWN ""
#define AV8100_DEVNR_DEFAULT 0

/* Interrupts */
#define AV8100_INT_EVENT		0x1
#define AV8100_FWDL_EVENT1		0x2
#define AV8100_FWDL_EVENT2		0x4
#define AV8100_1S_TIMER_EVENT		0x8
#define AV8100_SCAN_TIMER_EVENT		0x10
#define AV8100_HPD_TIMER_EVENT		0x20
#define AV8100_POWERSCAN_TIMER_EVENT	0x40
#define AV8100_RESUME_TIMER_EVENT	0x80
#define AV8100_POWERUP_TIMER_EVENT	0x100
#define AV8100_POWERDOWN_TIMER_EVENT	0x200
#define AV8100_CHECK_HW_STATUS_EVENT	0x400
#define AV8100_POWER_CYCLE_EVENT	0x800
#define AV8100_EVENTS			0xFFE
#define CCI_MAX 50
#define AV8100_INTERNAL_REG_1 0x8488
/* Standby search time */
#define AV8100_ON_TIME 1	/* 9 ms step */
#define AV8100_DENC_OFF_TIME 0	/* 275 ms step if > V1. Not used if V1 */
#define AV8100_OFF_TIME_BURST 1
/* OFF_TIME: 140 ms step if V2. 80 ms step if V1 */
#define AV8100_DISP_ON_HDMI_OFF_TIME 0
#define AV8100_DISP_OFF_HDMI_OFF_TIME 2
#define ONI_READ_MAX 20
#define AV8100_SCAN_WHEN_DISPLAY_IS_POWERED_OFF false
#define UFLOW_CNT_MAX 15
#define UFLOW_TIME_MAX 1000

/* Command offsets */
#define AV8100_COMMAND_OFFSET		0x10
#define AV8100_CUTVER_OFFSET		0x11
#define AV8100_COMMAND_MAX_LENGTH	0x81
#define AV8100_CMD_BUF_OFFSET		(AV8100_COMMAND_OFFSET + 1)
#define AV8100_2ND_RET_BYTE_OFFSET	(AV8100_COMMAND_OFFSET + 1)
#define AV8100_CEC_RET_BUF_OFFSET	(AV8100_COMMAND_OFFSET + 4)
#define AV8100_HDCP_RET_BUF_OFFSET	(AV8100_COMMAND_OFFSET + 2)
#define AV8100_EDID_RET_BUF_OFFSET	(AV8100_COMMAND_OFFSET + 1)
#define AV8100_FUSE_CRC_OFFSET		(AV8100_COMMAND_OFFSET + 2)
#define AV8100_FUSE_PRGD_OFFSET		(AV8100_COMMAND_OFFSET + 3)
#define AV8100_CRC32_OFFSET		(AV8100_COMMAND_OFFSET + 2)
#define AV8100_CEC_ADDR_OFFSET		(AV8100_COMMAND_OFFSET + 3)

/* Internal commands */
#define AV8100_WRITE_ACCESS 0x60

/* Tearing effect line numbers */
#define AV8100_TE_LINE_NB_14	14
#define AV8100_TE_LINE_NB_17	17
#define AV8100_TE_LINE_NB_18	18
#define AV8100_TE_LINE_NB_21	21
#define AV8100_TE_LINE_NB_22	22
#define AV8100_TE_LINE_NB_24	24
#define AV8100_TE_LINE_NB_25	25
#define AV8100_TE_LINE_NB_26	26
#define AV8100_TE_LINE_NB_29	29
#define AV8100_TE_LINE_NB_30	30
#define AV8100_TE_LINE_NB_32	32
#define AV8100_TE_LINE_NB_38	38
#define AV8100_TE_LINE_NB_40	40
#define AV8100_UI_X4_DEFAULT	6

#define HDMI_REQUEST_FOR_REVOCATION_LIST_INPUT 2
#define HDMI_CONTINUE_AUTHENTICATION 3
#define HDMI_CEC_MESSAGE_WRITE_BUFFER_SIZE 16
#define HDMI_HDCP_SEND_KEY_SIZE 7
#define HDMI_INFOFRAME_DATA_SIZE 28
#define HDMI_FUSE_AES_KEY_SIZE 16
#define HDMI_FUSE_AES_KEY_RET_SIZE 2
#define HDMI_LOADAES_END_BLK_NR 145
#define HDMI_CRC32_SIZE 4
#define HDMI_HDCP_MGMT_BKSV_SIZE 5
#define HDMI_HDCP_MGMT_SHA_SIZE 20
#define HDMI_HDCP_MGMT_MAX_DEVICES_SIZE 20
#define HDMI_HDCP_MGMT_DEVICE_MASK 0x7F
#define HDMI_EDIDREAD_SIZE 0x7F
#define HDCP_STATE_ENCRYPTION_ONGOING 7
#define INT_CNT_MAX 100
#define INFOFRAME_TYPE_AUDIO 0x84

#define REG_16_8_LSB(p)		((u8)(p & 0xFF))
#define REG_16_8_MSB(p)		((u8)((p & 0xFF00)>>8))
#define REG_32_8_MSB(p)		((u8)((p & 0xFF000000)>>24))
#define REG_32_8_MMSB(p)	((u8)((p & 0x00FF0000)>>16))
#define REG_32_8_MLSB(p)	((u8)((p & 0x0000FF00)>>8))
#define REG_32_8_LSB(p)		((u8)(p & 0x000000FF))
#define REG_10_8_MSB(p)		((u8)((p & 0x300)>>8))
#define REG_12_8_MSB(p)		((u8)((p & 0xf00)>>8))

#define AV8100_WAITTIME_1MS 1000
#define AV8100_WAITTIME_1MS_MAX 1200
#define AV8100_WAITTIME_5MS 5000
#define AV8100_WAITTIME_5MS_MAX 6000
#define AV8100_WAITTIME_10MS 10000
#define AV8100_WAITTIME_10MS_MAX 11000
#define AV8100_WAITTIME_50MS 50000
#define AV8100_WAITTIME_50MS_MAX 55000
#define AV8100_WATTIME_100US 100

static DEFINE_MUTEX(av8100_hw_mutex);
#define LOCK_AV8100_HW mutex_lock(&av8100_hw_mutex)
#define UNLOCK_AV8100_HW mutex_unlock(&av8100_hw_mutex)
static DEFINE_MUTEX(av8100_fwdl_mutex);
#define LOCK_AV8100_FWDL mutex_lock(&av8100_fwdl_mutex)
#define UNLOCK_AV8100_FWDL mutex_unlock(&av8100_fwdl_mutex)
static DEFINE_MUTEX(av8100_usrcnt_mutex);
#define LOCK_AV8100_USRCNT mutex_lock(&av8100_usrcnt_mutex)
#define UNLOCK_AV8100_USRCNT mutex_unlock(&av8100_usrcnt_mutex)
static DEFINE_MUTEX(av8100_conf_mutex);
#define LOCK_AV8100_CONF mutex_lock(&av8100_conf_mutex)
#define UNLOCK_AV8100_CONF mutex_unlock(&av8100_conf_mutex)

enum av8100_timer_flag {
	TIMER_UNSET,
	TIMER_1S,
	TIMER_SCAN,
	TIMER_SCAN2,
	TIMER_HPD,
	TIMER_POWERSCAN,
	TIMER_RESUME,
	TIMER_POWERUP,
	TIMER_POWERDOWN
};

struct color_conversion_cmd {
	unsigned short	c0;
	unsigned short	c1;
	unsigned short	c2;
	unsigned short	c3;
	unsigned short	c4;
	unsigned short	c5;
	unsigned short	c6;
	unsigned short	c7;
	unsigned short	c8;
	unsigned short	aoffset;
	unsigned short	boffset;
	unsigned short	coffset;
	unsigned char	lmax;
	unsigned char	lmin;
	unsigned char	cmax;
	unsigned char	cmin;
};

struct av8100_config {
	struct i2c_client *client;
	struct i2c_device_id *id;
	struct av8100_video_input_format_cmd hdmi_video_input_cmd;
	struct av8100_audio_input_format_cmd hdmi_audio_input_cmd;
	struct av8100_video_output_format_cmd hdmi_video_output_cmd;
	struct av8100_video_scaling_format_cmd hdmi_video_scaling_cmd;
	enum av8100_color_transform color_transform;
	struct av8100_cec_message_write_format_cmd
		hdmi_cec_message_write_cmd;
	struct av8100_cec_message_read_back_format_cmd
		hdmi_cec_message_read_back_cmd;
	struct av8100_denc_format_cmd hdmi_denc_cmd;
	struct av8100_hdmi_cmd hdmi_cmd;
	struct av8100_hdcp_send_key_format_cmd hdmi_hdcp_send_key_cmd;
	struct av8100_hdcp_management_format_cmd
		hdmi_hdcp_management_format_cmd;
	struct av8100_infoframes_format_cmd	hdmi_infoframes_cmd;
	struct av8100_infoframes_format_cmd	hdmi_audio_infoframe;
	struct av8100_edid_section_readback_format_cmd
		hdmi_edid_section_readback_cmd;
	struct av8100_pattern_generator_format_cmd hdmi_pattern_generator_cmd;
	struct av8100_fuse_aes_key_format_cmd hdmi_fuse_aes_key_cmd;
};

enum av8100_plug_state {
	AV8100_UNPLUGGED,
	AV8100_PLUGGED
};

struct av8100_params {
	int denc_off_time;/* 5 volt time */
	int hdmi_off_time;/* 5 volt time */
	int on_time;/* 5 volt time */
	u8 ccm;/*stby_int_mask*/
	u8 cecm;/*gen_int_mask*/
	u8 hdcpm;/*gen_int_mask*/
	u8 uovbm;/*gen_int_mask*/
	void (*hdmi_ev_cb)(enum av8100_hdmi_event);
	enum av8100_plug_state plug_state;
	struct clk *inputclk;
	bool inputclk_requested;
	bool opp_requested;
	struct regulator *regulator_pwr;
	bool regulator_requested;
	bool pre_suspend_power;
	bool irq_requested;
	bool fw_loaded;
	u8 count_cci;
	bool pulsing_5V;
	bool powerdown_scan;
	bool suspended;
	bool disp_on;
	bool busy;
	u8 hdcp_state;
	bool pwr_recover;
	bool init_requested;
};

/**
 * struct av8100_cea - CEA(consumer electronic access) standard structure
 * @cea_id:
 * @cea_nb:
 * @vtotale:
 **/

struct av8100_cea {
	char cea_id[40];
	int cea_nb;
	int vtotale;
	int vactive;
	int vsbp;
	int vslen;
	int vsfp;
	char vpol[5];
	int htotale;
	int hactive;
	int hbp;
	int hslen;
	int hfp;
	int frequence;
	char hpol[5];
	int reg_line_duration;
	int blkoel_duration;
	int uix4;
	int pll_mult;
	int pll_div;
};

enum av8100_command_size {
	AV8100_COMMAND_VIDEO_INPUT_FORMAT_SIZE  = 0x17,
	AV8100_COMMAND_AUDIO_INPUT_FORMAT_SIZE  = 0x8,
	AV8100_COMMAND_VIDEO_OUTPUT_FORMAT_SIZE = 0x1E,
	AV8100_COMMAND_VIDEO_SCALING_FORMAT_SIZE = 0x11,
	AV8100_COMMAND_COLORSPACECONVERSION_SIZE = 0x1D,
	AV8100_COMMAND_CEC_MESSAGE_WRITE_SIZE = 0x12,
	AV8100_COMMAND_CEC_MESSAGE_READ_BACK_SIZE = 0x1,
	AV8100_COMMAND_DENC_SIZE = 0x6,
	AV8100_COMMAND_HDMI_SIZE = 0x4,
	AV8100_COMMAND_HDCP_SENDKEY_SIZE = 0xA,
	AV8100_COMMAND_HDCP_MANAGEMENT_SIZE = 0x3,
	AV8100_COMMAND_INFOFRAMES_SIZE = 0x21,
	AV8100_COMMAND_EDID_SECTION_READBACK_SIZE  = 0x3,
	AV8100_COMMAND_PATTERNGENERATOR_SIZE  = 0x4,
	AV8100_COMMAND_FUSE_AES_KEY_SIZE = 0x12,
	AV8100_COMMAND_FUSE_AES_CHK_SIZE = 0x2,
};

struct av8100_device {
	struct list_head	list;
	struct miscdevice	miscdev;
	struct device		*dev;
	struct av8100_config	config;
	struct av8100_status	status;
	struct hrtimer		hrtimer;
	enum av8100_timer_flag	timer_flag;
	wait_queue_head_t	event;
	int			flag;
	struct av8100_params	params;
	u8			chip_version;
	struct early_suspend	early_suspend;
	u32                     usr_cnt;
	bool			usr_audio;
	bool			usr_video;
};

static const unsigned int waittime_retry[10] =	{
	1000, 2000, 4000, 6000, 8000, 10000, 10000, 10000, 10000, 10000};

static int write_single_byte(struct i2c_client *client, u8 reg, u8 data);
static int write_multi_byte(struct i2c_client *client, u8 reg,
		u8 *buf, u8 nbytes);
static int get_command_return_first(struct i2c_client *i2c,
		enum av8100_command_type command_type);
static int av8100_5V_w(u8 denc_off, u8 hdmi_off, u8 on);
static void clr_plug_status(struct av8100_device *adev,
	enum av8100_plugin_status status);
static void set_plug_status(struct av8100_device *adev,
	enum av8100_plugin_status status);
static void cec_rx(struct av8100_device *adev);
static void cec_tx(struct av8100_device *adev);
static void cec_txerr(struct av8100_device *adev);
static void cci_err(struct av8100_device *adev);
static void av8100_set_state(struct av8100_device *adev,
			enum av8100_operating_mode state);
static void hdcp_changed(struct av8100_device *adev);
static const struct color_conversion_cmd *get_color_transform_cmd(
				struct av8100_device *adev,
				enum av8100_color_transform transform);
static int av8100_open(struct inode *inode, struct file *filp);
static int av8100_release(struct inode *inode, struct file *filp);
static long av8100_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg);
static int __devinit av8100_probe(struct i2c_client *i2c_client,
	const struct i2c_device_id *id);
static int __devexit av8100_remove(struct i2c_client *i2c_client);

static const struct file_operations av8100_fops = {
	.owner =    THIS_MODULE,
	.open =     av8100_open,
	.release =  av8100_release,
	.unlocked_ioctl = av8100_ioctl
};

/* List of devices */
static LIST_HEAD(av8100_device_list);

static const struct av8100_cea av8100_all_cea[29] = {
/* cea id
 *	cea_nr	vtot	vact	vsbpp	vslen
 *	vsfp	vpol	htot	hact	hbp	hslen	hfp	freq
 *	hpol	rld	bd	uix4	pm	pd */
{ "0  CUSTOM                            ",
	0,	0,	0,	0,	0,
	0,	"-",	800,	640,	16,	96,	10,	25200000,
	"-",	0,	0,	0,	0,	0},/*Settings to be defined*/
{ "1  CEA 1 VESA 4 640x480p @ 60 Hz     ",
	1,	525,	480,	33,	2,
	10,	"-",	800,	640,	49,	290,	146,	25200000,
	"-",	2438,	1270,	6,	32,	1},/*RGB888*/
{ "2  CEA 2 - 3    720x480p @ 60 Hz 4:3 ",
	2,	525,	480,	30,	6,
	9,	"-",	858,	720,	34,	130,	128,	27027000,
	"-",	1828,	0x3C0,	8,	24,	1},/*RGB565*/
{ "3  CEA 4        1280x720p @ 60 Hz    ",
	4,	750,	720,	20,	5,
	5,	"+",	1650,	1280,	114,	39,	228,	74250000,
	"+",	1706,	164,	6,	32,	1},/*RGB565*/
{ "4  CEA 5        1920x1080i @ 60 Hz   ",
	5,	1125,	540,	20,	5,
	0,	"+",	2200,	1920,	88,	44,	10,	74250000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "5  CEA 6-7      480i (NTSC)          ",
	6,	525,	240,	44,	5,
	0,	"-",	858,	720,	12,	64,	10,	13513513,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "6  CEA 14-15    480p @ 60 Hz         ",
	14,	525,	480,	44,	5,
	0,	"-",	858,	720,	12,	64,	10,	27027000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "7  CEA 16       1920x1080p @ 60 Hz   ",
	16,	1125,	1080,	36,	5,
	0,	"+",	1980,	1280,	440,	40,	10,	133650000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "8  CEA 17-18    720x576p @ 50 Hz     ",
	17,	625,	576,	44,	5,
	0,	"-",	864,	720,	12,	64,	10,	27000000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "9  CEA 19       1280x720p @ 50 Hz    ",
	19,	750,	720,	25,	5,
	0,	"+",	1980,	1280,	440,	40,	10,	74250000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "10 CEA 20       1920 x 1080i @ 50 Hz ",
	20,	1125,	540,	20,	5,
	0,	"+",	2640,	1920,	528,	44,	10,	74250000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "11 CEA 21-22    576i (PAL)           ",
	21,	625,	288,	44,	5,
	0,	"-",	1728,	1440,	12,	64,	10,	27000000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "12 CEA 29/30    576p                 ",
	29,	625,	576,	44,	5,
	0,	"-",	864,	720,	12,	64,	10,	27000000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "13 CEA 31       1080p 50Hz           ",
	31,	1125,	1080,	44,	5,
	0,	"-",	2640,	1920,	12,	64,	10,	148500000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "14 CEA 32       1920x1080p @ 24 Hz   ",
	32,	1125,	1080,	36,	5,
	4,	"+",	2750,	1920,	660,	44,	153,	74250000,
	"+",	2844,	0x530,	6,	32,	1},/*RGB565*/
{ "15 CEA 33       1920x1080p @ 25 Hz   ",
	33,	1125,	1080,	36,	5,
	4,	"+",	2640,	1920,	528,	44,	10,	74250000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "16 CEA 34       1920x1080p @ 30Hz    ",
	34,	1125,	1080,	36,	5,
	4,	"+",	2200,	1920,	91,	44,	153,	74250000,
	"+",	2275,	0xAB,	6,	32,	1},/*RGB565*/
{ "17 CEA 60       1280x720p @ 24 Hz    ",
	60,	750,	720,	20,	5,
	5,	"+",	3300,	1280,	284,	50,	2276,	59400000,
	"+",	4266,	0xAD0,	5,	32,	1},/*RGB565*/
{ "18 CEA 61       1280x720p @ 25 Hz    ",
	61,	750,	720,	20,	5,
	5,	"+",	3960,	1280,	228,	39,	2503,	74250000,
	"+",	4096,	0x500,	5,	32,	1},/*RGB565*/
{ "19 CEA 62       1280x720p @ 30 Hz    ",
	62,	750,	720,	20,	5,
	5,	"+",	3300,	1280,	228,	39,	1820,	74250000,
	"+",	3413,	0x770,	5,	32,	1},/*RGB565*/
{ "20 VESA 9       800x600 @ 60 Hz      ",
	109,	628,	600,	28,	4,
	0,	"+",	1056,	800,	40,	128,	10,	40000000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "21 VESA 14      848x480  @ 60 Hz     ",
	114,	517,	480,	20,	5,
	0,	"+",	1088,	848,	24,	80,	10,	33750000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "22 VESA 16      1024x768 @ 60 Hz     ",
	116,	806,	768,	38,	6,
	0,	"-",	1344,	1024,	24,	135,	10,	65000000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "23 VESA 22      1280x768 @ 60 Hz     ",
	122,	790,	768,	34,	4,
	0,	"+",	1440,	1280,	48,	160,	10,	68250000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "24 VESA 23      1280x768 @ 60 Hz     ",
	123,	798,	768,	30,	7,
	0,	"+",	1664,	1280,	64,	128,	10,	79500000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "25 VESA 27      1280x800 @ 60 Hz     ",
	127,	823,	800,	23,	6,
	0,	"+",	1440,	1280,	48,	32,	10,	71000000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "26 VESA 28      1280x800 @ 60 Hz     ",
	128,	831,	800,	31,	6,
	0,	"+",	1680,	1280,	72,	128,	10,	83500000,
	"-",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "27 VESA 39      1360x768 @ 60 Hz     ",
	139,	795,	768,	22,	5,
	0,	"-",	1792,	1360,	48,	32,	10,	85500000,
	"+",	0,	0,	0,	0,	0},/*Settings to be define*/
{ "28 VESA 81      1366x768 @ 60 Hz     ",
	181,	798,	768,	25,	5,
	0,	"+",	1776,	1360,	208,	208,	0,	85000000,
	"-",	0,	0,	0,	0,	0} /*Settings to be define*/
};

const struct color_conversion_cmd col_trans_identity = {
	.c0      = 0x0100, .c1      = 0x0000, .c2      = 0x0000,
	.c3      = 0x0000, .c4      = 0x0100, .c5      = 0x0000,
	.c6      = 0x0000, .c7      = 0x0000, .c8      = 0x0100,
	.aoffset = 0x0000, .boffset = 0x0000, .coffset = 0x0000,
	.lmax    = 0xff,
	.lmin    = 0x00,
	.cmax    = 0xff,
	.cmin    = 0x00,
};

const struct color_conversion_cmd col_trans_identity_clamp_yuv = {
	.c0      = 0x0100, .c1      = 0x0000, .c2      = 0x0000,
	.c3      = 0x0000, .c4      = 0x0100, .c5      = 0x0000,
	.c6      = 0x0000, .c7      = 0x0000, .c8      = 0x0100,
	.aoffset = 0x0000, .boffset = 0x0000, .coffset = 0x0000,
	.lmax    = 0xeb,
	.lmin    = 0x10,
	.cmax    = 0xf0,
	.cmin    = 0x10,
};

const struct color_conversion_cmd col_trans_yuv_to_rgb_v1 = {
	.c0      = 0x0087, .c1      = 0x0000, .c2      = 0x00ba,
	.c3      = 0x0087, .c4      = 0xffd3, .c5      = 0xffa1,
	.c6      = 0x0087, .c7      = 0x00eb, .c8      = 0x0000,
	.aoffset = 0xffab, .boffset = 0x004e, .coffset = 0xff92,
	.lmax    = 0xff,
	.lmin    = 0x00,
	.cmax    = 0xff,
	.cmin    = 0x00,
};

const struct color_conversion_cmd col_trans_yuv_to_rgb_v2 = {
	.c0      = 0x0198, .c1      = 0x012a, .c2      = 0x0000,
	.c3      = 0xff30, .c4      = 0x012a, .c5      = 0xff9c,
	.c6      = 0x0000, .c7      = 0x012a, .c8      = 0x0204,
	.aoffset = 0xff21, .boffset = 0x0088, .coffset = 0xfeeb,
	.lmax    = 0xff,
	.lmin    = 0x00,
	.cmax    = 0xff,
	.cmin    = 0x00,
};

const struct color_conversion_cmd col_trans_yuv_to_denc = {
	.c0      = 0x0100, .c1      = 0x0000, .c2      = 0x0000,
	.c3      = 0x0000, .c4      = 0x0100, .c5      = 0x0000,
	.c6      = 0x0000, .c7      = 0x0000, .c8      = 0x0100,
	.aoffset = 0x0000, .boffset = 0x0000, .coffset = 0x0000,
	.lmax    = 0xeb,
	.lmin    = 0x10,
	.cmax    = 0xf0,
	.cmin    = 0x10,
};

const struct color_conversion_cmd col_trans_rgb_to_denc = {
	.c0      = 0x0070, .c1      = 0xffb6, .c2      = 0xffda,
	.c3      = 0x0042, .c4      = 0x0081, .c5      = 0x0019,
	.c6      = 0xffee, .c7      = 0xffa2, .c8      = 0x0070,
	.aoffset = 0x007f, .boffset = 0x0010, .coffset = 0x007f,
	.lmax    = 0xff,
	.lmin    = 0x00,
	.cmax    = 0xff,
	.cmin    = 0x00,
};

static const struct i2c_device_id av8100_id[] = {
	{ "av8100", 0 },
	{ }
};

static struct av8100_device *devnr_to_adev(int devnr)
{
	/* Get device from list of devices */
	struct list_head *element;
	struct av8100_device *av8100_dev;
	int cnt = 0;

	list_for_each(element, &av8100_device_list) {
		av8100_dev = list_entry(element, struct av8100_device, list);
		if (cnt == devnr)
			return av8100_dev;
		cnt++;
	}

	return NULL;
}

static struct av8100_device *dev_to_adev(struct device *dev)
{
	/* Get device from list of devices */
	struct list_head *element;
	struct av8100_device *av8100_dev;
	int cnt = 0;

	list_for_each(element, &av8100_device_list) {
		av8100_dev = list_entry(element, struct av8100_device, list);
		if (av8100_dev->dev == dev)
			return av8100_dev;
		cnt++;
	}

	return NULL;
}

static void set_hrtimer(struct av8100_device *adev, enum av8100_timer_flag flag)
{
	ktime_t time;
	ktime_t delta;

	/* Don't override powerdown */
	if (adev->timer_flag == TIMER_POWERDOWN)
		return;

	adev->timer_flag = flag;

	switch (flag) {
	case TIMER_1S:
		time = ktime_set(1, 0); /* 1 sec */
		break;
	case TIMER_SCAN:
		time = ktime_set(4, 0); /* 4 sec */
		break;
	case TIMER_SCAN2:
		time = ktime_set(10, 0); /* 10 sec */
		break;
	case TIMER_HPD:
		time = ktime_set(0, 800000000); /* 0.8 sec */
		break;
	case TIMER_POWERSCAN:
		time = ktime_set(1, 500000000); /* 1.5 sec */
		break;
	case TIMER_RESUME:
		time = ktime_set(0, 1); /* 0.1 sec */
		break;
	case TIMER_POWERUP:
		time = ktime_set(0, 1); /* 0.1 sec */
		break;
	case TIMER_POWERDOWN:
		time = ktime_set(1, 0); /* 1.0 sec */
		break;
	default:
		return;
		break;
	}

	delta = ktime_set(0, 100000000); /* 0.1 sec */
	hrtimer_set_expires_range(&adev->hrtimer, time, delta);
	hrtimer_start_expires(&adev->hrtimer, HRTIMER_MODE_REL);
}

#ifdef CONFIG_PM
static int av8100_suspend(struct device *dev)
{
	int ret = 0;
	struct av8100_device *adev;

	adev = dev_to_adev(dev);
	if (!adev)
		return -EFAULT;

	if (adev->params.busy)
		return -EBUSY;

	dev_dbg(dev, "%s\n", __func__);

	adev->flag &= ~AV8100_EVENTS;
	hrtimer_cancel(&adev->hrtimer);
	adev->params.pre_suspend_power =
		(av8100_status_get().av8100_state > AV8100_OPMODE_SHUTDOWN);

	if (adev->params.pre_suspend_power) {
		if (adev->usr_audio) {
			ret = -EBUSY;
		} else {
			ret = av8100_powerdown();
			if (ret)
				dev_err(dev, "av8100_powerdown failed\n");
		}
	}

	return ret;
}

static int av8100_resume(struct device *dev)
{
	struct av8100_device *adev;

	adev = dev_to_adev(dev);
	if (!adev)
		return -EFAULT;

	dev_dbg(dev, "%s\n", __func__);
	if (adev->params.pre_suspend_power)
		set_hrtimer(adev, TIMER_RESUME);

	return 0;
}

static const struct dev_pm_ops av8100_dev_pm_ops = {
	.suspend = av8100_suspend,
	.resume = av8100_resume,
};
#endif

static struct i2c_driver av8100_driver = {
	.probe	= av8100_probe,
	.remove = av8100_remove,
	.driver = {
		.name	= "av8100",
#ifdef CONFIG_PM
		.pm	= &av8100_dev_pm_ops,
#endif
	},
	.id_table	= av8100_id,
};

static int power_cycle(struct av8100_device *adev)
{
	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return 0;
	dev_dbg(adev->dev, "%s %d\n", __func__, adev->usr_audio);
	if (!adev->usr_audio) {
		av8100_powerdown();
		set_hrtimer(adev, TIMER_POWERUP);
	} else {
		/* Simulate unplug in order to recover */
		clr_plug_status(adev, AV8100_HDMI_PLUGIN);
		adev->params.pwr_recover = true;
	}
	return 0;
}

/* 5V state machine timer */
static enum hrtimer_restart hrtimer_func(struct hrtimer *timer)
{
	struct av8100_device *adev = container_of(timer, struct av8100_device,
								hrtimer);

	switch (adev->timer_flag) {
	case TIMER_1S:
		adev->flag |= AV8100_1S_TIMER_EVENT;
		break;
	case TIMER_SCAN:
	case TIMER_SCAN2:
		adev->flag |= AV8100_SCAN_TIMER_EVENT;
		break;
	case TIMER_HPD:
		adev->flag |= AV8100_HPD_TIMER_EVENT;
		break;
	case TIMER_POWERSCAN:
		adev->flag |= AV8100_POWERSCAN_TIMER_EVENT;
		break;
	case TIMER_RESUME:
		adev->flag |= AV8100_RESUME_TIMER_EVENT;
		break;
	case TIMER_POWERUP:
		adev->flag |= AV8100_POWERUP_TIMER_EVENT;
		break;
	case TIMER_POWERDOWN:
		adev->flag |= AV8100_POWERDOWN_TIMER_EVENT;
		break;
	default:
		return -EINVAL;
		break;
	}
	wake_up_interruptible(&adev->event);

	return HRTIMER_NORESTART;
}

static void to_scan_mode_2(struct av8100_device *adev)
{
	if (adev->params.fw_loaded)
		(void)av8100_reg_gen_int_mask_w(
				AV8100_GENERAL_INTERRUPT_MASK_EOCM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_VSIM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_VSOM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_CECM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_HDCPM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_UOVBM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_TEM_LOW);

	/* Remove APE OPP requirement */
	if (adev->params.opp_requested) {
		prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
				adev->miscdev.name);
		prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP,
				adev->miscdev.name);
		adev->params.opp_requested = false;
	}

	/* Clock disable */
	if (adev->params.inputclk &&
			adev->params.inputclk_requested) {
		clk_disable(adev->params.inputclk);
		adev->params.inputclk_requested = false;
	}

	adev->params.fw_loaded = false;
	adev->params.count_cci = 0;
	if (adev->params.pulsing_5V == false)
		set_hrtimer(adev, TIMER_1S);

	if (av8100_status_get().av8100_state > AV8100_OPMODE_SCAN)
		av8100_set_state(adev, AV8100_OPMODE_SCAN);
}

static int fix_cec(struct av8100_device *adev)
{
	/* msg1: ana config, 0x88: disable 2V3 + pull down connected */
	u8 msg1[] = {0x38, 0x10, 0x86, 0x88, 0x84, 0x8C};
	u8 msg2[] = {0x00, 0x00, 0x00, 0x83, 0x84, 0xC4};
	u8 msg3[] = {0x40, 0x00, 0x00, 0x00, 0x84, 0x90};
	struct i2c_client *i2c = adev->config.client;
	int ret = 0;

	/* Set Device in Hold */
	if (av8100_reg_gen_ctrl_w(
			AV8100_GENERAL_CONTROL_FDL_LOW,
			AV8100_GENERAL_CONTROL_HLD_HIGH,
			AV8100_GENERAL_CONTROL_WA_LOW,
			AV8100_GENERAL_CONTROL_RA_LOW)) {
		ret = -EFAULT;
		goto fix_cec_end;
	}

	LOCK_AV8100_HW;

	/* Message 1 */
	if (write_multi_byte(i2c, AV8100_REGISTER_9, msg1, sizeof(msg1))) {
		ret = -EFAULT;
		goto fix_cec_end;
	}
	/* Set Write access */
	if (write_single_byte(i2c, AV8100_REGISTER_8, AV8100_WRITE_ACCESS)) {
		ret = -EFAULT;
		goto fix_cec_end;
	}

	/* Message 2 */
	if (write_multi_byte(i2c, AV8100_REGISTER_9, msg2, sizeof(msg2))) {
		ret = -EFAULT;
		goto fix_cec_end;
	}
	/* Set Write access */
	if (write_single_byte(i2c, AV8100_REGISTER_9, AV8100_WRITE_ACCESS)) {
		ret = -EFAULT;
		goto fix_cec_end;
	}

	/* Message 3 */
	if (write_multi_byte(i2c, AV8100_REGISTER_9, msg3, sizeof(msg3))) {
		ret = -EFAULT;
		goto fix_cec_end;
	}
	/* Set Write access */
	if (write_single_byte(i2c, AV8100_REGISTER_9, AV8100_WRITE_ACCESS)) {
		ret = -EFAULT;
		goto fix_cec_end;
	}

fix_cec_end:
	UNLOCK_AV8100_HW;
	return ret;
}

static void to_scan_mode_1(struct av8100_device *adev)
{
	struct av8100_platform_data *pdata = adev->dev->platform_data;

	adev->params.fw_loaded = false;
	adev->flag &= ~(AV8100_EVENTS | AV8100_INT_EVENT);
	if (fix_cec(adev))
		dev_err(adev->dev, "%s %d err\n", __func__, __LINE__);

	/* Wait 5 ms */
	usleep_range(5000, 5500);

	/* Power down */
	gpio_set_value_cansleep(pdata->reset, 0);

	/* Wait 10 ms */
	usleep_range(10000, 11000);

	/* Power up */
	gpio_set_value_cansleep(pdata->reset, 1);

	/* Wait 5 ms */
	usleep_range(5000, 5500);

	/* Clear pending interrupts */
	if (av8100_reg_stby_pend_int_w(
			AV8100_STANDBY_PENDING_INTERRUPT_HPDI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CPDI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_ONI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CCI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW))
		dev_dbg(adev->dev, "%s %d err\n", __func__, __LINE__);

	/* Init Master Clock */
	if (av8100_reg_stby_w(AV8100_STANDBY_CPD_LOW,
			AV8100_STANDBY_STBY_LOW, pdata->mclk_freq))
		dev_dbg(adev->dev, "%s %d err\n", __func__, __LINE__);

	/* Start 5V in always On mode */
	if (av8100_5V_w(0, 0, AV8100_ON_TIME))
		dev_dbg(adev->dev, "%s %d err\n", __func__, __LINE__);

	/* Mask CCI interrupt */
	if (av8100_reg_stby_int_mask_w(
			AV8100_STANDBY_INTERRUPT_MASK_HPDM_HIGH,
			AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CCM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
			AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW))
		dev_dbg(adev->dev, "%s %d err\n", __func__, __LINE__);

	to_scan_mode_2(adev);
}

/* 5V state machine timer event handlers */
static void av8100_1s_timer_event_handle(struct av8100_device *adev)
{
	u8 cci;

	dev_dbg(adev->dev, "T1s\n");

	if (av8100_status_get().av8100_state > AV8100_OPMODE_SCAN) {
		dev_dbg(adev->dev, "state > SCAN\n");
		goto av8100_1s_timer_event_handle_end;
	}

	/*
	 * Start 5V in burst mode to check CCI.
	 * CCI is not generated in scan mode if 5V is always on.
	 */
	if (av8100_5V_w(0, AV8100_OFF_TIME_BURST, AV8100_ON_TIME))
		dev_dbg(adev->dev, "%s %d err\n", __func__, __LINE__);

	/* Wait 10 ms to allow CCI interrupt */
	usleep_range(10000, 11000);

	/* Restart 5V in always ON mode */
	if (av8100_5V_w(0, 0, AV8100_ON_TIME))
		dev_dbg(adev->dev, "%s %d err\n", __func__, __LINE__);

	/* Read CCI */
	if (av8100_reg_stby_pend_int_r(NULL, NULL, NULL, &cci, NULL))
		goto av8100_1s_timer_event_handle_err;

	if (cci) {
		dev_dbg(adev->dev, "T1sCCI\n");

		/* Clear CCI */
		if (av8100_reg_stby_pend_int_w(
				AV8100_STANDBY_PENDING_INTERRUPT_HPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCI_HIGH,
				AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW))
			goto av8100_1s_timer_event_handle_err;

		adev->flag |= AV8100_FWDL_EVENT2;
		wake_up_interruptible(&adev->event);
	} else {
		to_scan_mode_2(adev);
	}
av8100_1s_timer_event_handle_end:
	return;

av8100_1s_timer_event_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return;
}

static void av8100_scan_timer_event_handle(struct av8100_device *adev)
{
	dev_dbg(adev->dev, "Tscan\n");
	to_scan_mode_1(adev);
}

static void av8100_hpd_timer_event_handle(struct av8100_device *adev)
{
	u8 hpds;

	dev_dbg(adev->dev, "Thpd\n");

	hrtimer_cancel(&adev->hrtimer);

	/* Clear hpdi and cci interrupts */
	if (av8100_reg_stby_pend_int_w(
			AV8100_STANDBY_PENDING_INTERRUPT_HPDI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_CCI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW))
		goto av8100_hpd_timer_event_handle_err;

	/* Get hpds */
	if (av8100_reg_stby_r(NULL, NULL, &hpds, NULL, NULL))
		goto av8100_hpd_timer_event_handle_err;

	if (hpds) {
		/* HPDM=1, CCM=1 */
		if (av8100_reg_stby_int_mask_w(
				AV8100_STANDBY_INTERRUPT_MASK_HPDM_HIGH,
				AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
				AV8100_STANDBY_INTERRUPT_MASK_CCM_HIGH,
				AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
				AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW))
			goto av8100_hpd_timer_event_handle_err;

		/* Set plug status */
		set_plug_status(adev, AV8100_HDMI_PLUGIN);
	} else {
		to_scan_mode_1(adev);
	}
	return;

av8100_hpd_timer_event_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return;
}

static void av8100_powerscan_timer_event_handle(struct av8100_device *adev)
{
	u8 hpds;
	dev_dbg(adev->dev, "Tpwrsc\n");

	/* Clear hpdi and cci interrupts */
	if (av8100_reg_stby_pend_int_w(
			AV8100_STANDBY_PENDING_INTERRUPT_HPDI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_CCI_HIGH,
			AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
			AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW))
		goto av8100_powerscan_timer_event_handle_err;

	/* Check if plugged */
	/* Get hpds */
	if ((av8100_reg_stby_r(NULL, NULL, &hpds, NULL, NULL) == 0) &&
			(hpds == 1)) {
		/* Set plug status */
		set_plug_status(adev, AV8100_HDMI_PLUGIN);

	} else {
		if (!adev->usr_audio) {
			av8100_powerdown();
			usleep_range(AV8100_WAITTIME_1MS,
						AV8100_WAITTIME_1MS_MAX);
			av8100_powerup();
		}
		if (adev->params.pulsing_5V == false)
			set_hrtimer(adev, TIMER_1S);
		av8100_set_state(adev, AV8100_OPMODE_SCAN);
	}
	av8100_enable_interrupt();
	return;

av8100_powerscan_timer_event_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return;
}

static void av8100_resume_timer_event_handle(struct av8100_device *adev)
{
	dev_dbg(adev->dev, "Tres\n");

	if (av8100_powerup())
		dev_dbg(adev->dev, "%s err\n", __func__);
}

static void av8100_powerup_timer_event_handle(struct av8100_device *adev)
{
	dev_dbg(adev->dev, "Tpwrup\n");

	if (av8100_powerup())
		dev_dbg(adev->dev, "%s err\n", __func__);
}

static void av8100_powerdown_timer_event_handle(struct av8100_device *adev)
{
	dev_dbg(adev->dev, "Tpwrdown\n");

	if (!adev->usr_audio)
		(void)av8100_powerdown();
}

static int cci_handle(struct av8100_device *adev, u8 hpdi)
{
	u8 hpds = 0;

	dev_dbg(adev->dev, "CCIint cnt:%d\n", adev->params.count_cci);

	/* Short circuit event */
	adev->params.count_cci++;
	if (adev->params.count_cci >= CCI_MAX) {
		dev_info(adev->dev, "CCIint cntmax\n");
		dev_info(adev->dev, "5V stopped\n");
		/* Stop 5V; battery protection */
		if (av8100_5V_w(adev->params.denc_off_time,
				adev->params.hdmi_off_time, 0))
			goto cci_handle_err;

		/* Clear cci int */
		if (av8100_reg_stby_pend_int_w(
				AV8100_STANDBY_PENDING_INTERRUPT_HPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCI_HIGH,
				AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW))
			goto cci_handle_err;

		/* standby_mode, no scanning */
		av8100_set_state(adev, AV8100_OPMODE_STANDBY);

		adev->flag &= ~AV8100_EVENTS;
		hrtimer_cancel(&adev->hrtimer);
		adev->params.count_cci = 0;

		/* event to user space */
		cci_err(adev);
	} else if (hpdi) {
		/* Get hpds */
		if (av8100_reg_stby_r(NULL, NULL, &hpds, NULL, NULL))
			goto cci_handle_err;
		if (hpds) {
			dev_dbg(adev->dev, "CCIint hpds 1\n");
			set_hrtimer(adev, TIMER_HPD);

			/* HPDM=0, CCM=1 */
			if (av8100_reg_stby_int_mask_w(
				AV8100_STANDBY_INTERRUPT_MASK_HPDM_LOW,
				AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
				AV8100_STANDBY_INTERRUPT_MASK_CCM_HIGH,
				AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
				AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW))
				goto cci_handle_err;
		} else {
			dev_dbg(adev->dev, "CCIint hpds 0\n");
			to_scan_mode_1(adev);
		}
	} else {
		dev_dbg(adev->dev, "CCIint hpd 0\n");
		/* Get hpds */
		if (av8100_reg_stby_r(NULL, NULL, &hpds, NULL, NULL))
			goto cci_handle_err;
		if (hpds == 0) {
			/*
			 * No hpd event and unplugged: reset hpd timer
			 * to allow more cci events before timer expires.
			 */
			set_hrtimer(adev, TIMER_HPD);
		}
	}
	return 0;

cci_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return -EFAULT;
}

static int hpdi_handle(struct av8100_device *adev)
{
	u8 hpds = 0;

	/* Plug event */
	/* Get hpds */
	if (av8100_reg_stby_r(NULL, NULL, &hpds, NULL, NULL))
		goto hpdi_handle_err;

	if (hpds) {
		dev_dbg(adev->dev, "HPDint hpds 1\n");
		set_hrtimer(adev, TIMER_HPD);

		/* HPDM=0, CCM=1 */
		if (av8100_reg_stby_int_mask_w(
			AV8100_STANDBY_INTERRUPT_MASK_HPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CCM_HIGH,
			AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
			AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW))
			goto hpdi_handle_err;

		if (adev->params.fw_loaded) {
			dev_dbg(adev->dev, "fwdld 1\n");
		} else {
			dev_dbg(adev->dev, "fwdld 0\n");

			adev->flag |= AV8100_FWDL_EVENT1;
			adev->flag &= ~AV8100_FWDL_EVENT2;
			wake_up_interruptible(&adev->event);
		}
	} else {
		dev_dbg(adev->dev, "HPDint hpds 0\n");
		set_hrtimer(adev, TIMER_SCAN2);
		clr_plug_status(adev, AV8100_HDMI_PLUGIN);
	}
	return 0;

hpdi_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return -EFAULT;
}

static int check_hw_status(struct av8100_device *adev)
{
	int retval = 0;
	struct i2c_client *i2c = adev->config.client;
	int cnt = 0;
	int cnt_max;

	if (!adev->params.fw_loaded)
		return 0;
	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return 0;

	/*
	 * Detect bad hw state that requires power refresh
	 * by reading AES FUSE status
	 */
	/* Write the command buffer */
	LOCK_AV8100_HW;
	if (write_single_byte(i2c, AV8100_CMD_BUF_OFFSET,
					AV8100_FUSE_READ))
		goto check_hw_status_end;

	/* Write the command */
	if (write_single_byte(i2c, AV8100_COMMAND_OFFSET,
			AV8100_COMMAND_FUSE_AES_KEY))
		goto check_hw_status_end;

	/* Get the first return byte */
	usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS * 12 / 10);
	cnt = 0;
	cnt_max = sizeof(waittime_retry) / sizeof(waittime_retry[0]);
	retval = get_command_return_first(i2c, AV8100_COMMAND_FUSE_AES_KEY);
	while (retval && (cnt < cnt_max)) {
		usleep_range(waittime_retry[cnt],
					waittime_retry[cnt] * 12 / 10);
		retval = get_command_return_first(i2c,
					AV8100_COMMAND_FUSE_AES_KEY);
		cnt++;
	}
	UNLOCK_AV8100_HW;
	if (cnt == cnt_max) {
		dev_dbg(adev->dev, "%s bad\n", __func__);
		power_cycle(adev);
	} else {
		dev_dbg(adev->dev, "%s ok\n", __func__);
	}
	return 0;

check_hw_status_end:
	UNLOCK_AV8100_HW;
	dev_dbg(adev->dev, "%s not completed\n", __func__);
	return -EFAULT;
}

static int uovbi_handle(struct av8100_device *adev, u8 onuvb)
{
	static int uflow_cnt;
	ktime_t later;
	static ktime_t earlier;
	ktime_t diff;
	u32 uflow_time;

	dev_dbg(adev->dev, "uovbi %d\n", onuvb);

	/* Check for continous under/overflow */
	if (uflow_cnt == 0)
		earlier = ktime_get();

	uflow_cnt++;
	if (uflow_cnt > UFLOW_CNT_MAX) {
		later = ktime_get();

		diff = ktime_sub(later, earlier);
		uflow_time = (u32)ktime_to_ms(diff);
		dev_dbg(adev->dev, "uovbi 0 time: %d ms", uflow_time);

		/* Underflow error: force power cycle */
		if (uflow_time < UFLOW_TIME_MAX) {
			adev->flag |= AV8100_POWER_CYCLE_EVENT;
			wake_up_interruptible(&adev->event);
		}
		uflow_cnt = 0;
	}

	/* Trig check of hw status */
	if (!(adev->flag & AV8100_POWER_CYCLE_EVENT) &&
				(adev->params.fw_loaded)) {
		adev->flag |= AV8100_CHECK_HW_STATUS_EVENT;
		wake_up_interruptible(&adev->event);
	}

	return 0;
}

static int av8100_int_event_handle(struct av8100_device *adev)
{
	u8 hpdi = 0;
	u8 cci = 0;
	u8 uovbi = 0;
	u8 hdcpi = 0;
	u8 ceci = 0;
	u8 hdcps = 0;
	u8 onuvb = 0;
	u8 cectxerr = 0;
	u8 cecrx = 0;
	u8 cectx = 0;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return 0;

	/* Get hpdi and cci */
	if (av8100_reg_stby_pend_int_r(&hpdi, NULL, NULL, &cci, NULL))
		goto av8100_int_event_handle_err;
	/* Clear hpdi and cci interrupts */
	if (hpdi | cci)
		/* Clear hpdi and cci interrupts */
		if (av8100_reg_stby_pend_int_w(
				hpdi,
				AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
				cci,
				AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW))
			goto av8100_int_event_handle_err;

	if (cci)
		cci_handle(adev, hpdi);
	if (hpdi)
		hpdi_handle(adev);

	if (adev->params.fw_loaded == false)
		return 0;

	/* GENERAL_INTERRUPT reg */
	if (av8100_reg_gen_int_r(NULL, NULL, NULL, &ceci,
			&hdcpi, &uovbi, NULL))
		goto av8100_int_event_handle_err;

	/* CEC or HDCP event */
	if (ceci | hdcpi | uovbi) {
		/* Clear pending interrupts */
		if (av8100_reg_gen_int_w(0, 0, 0, ceci, hdcpi, uovbi))
			goto av8100_int_event_handle_err;

		/* GENERAL_STATUS reg */
		if (av8100_reg_gen_status_r(&cectxerr, &cecrx, &cectx, NULL,
				&onuvb, &hdcps))
			goto av8100_int_event_handle_err;
	}

	/* Underflow or overflow */
	if (uovbi)
		uovbi_handle(adev, onuvb);

	/* CEC received */
	if (ceci && cecrx) {
		u8 val;

		dev_dbg(adev->dev, "cecrx\n");

		/* Clear cecrx in status reg*/
		if (av8100_reg_r(AV8100_GENERAL_STATUS, &val) == 0) {
			if (av8100_reg_w(AV8100_GENERAL_STATUS,
				val & ~AV8100_GENERAL_STATUS_CECREC_MASK))
				goto av8100_int_event_handle_err;
		} else {
			goto av8100_int_event_handle_err;
		}

		/* Report CEC event */
		cec_rx(adev);
	}

	/* CEC tx error */
	if (ceci && cectx && cectxerr) {
		dev_dbg(adev->dev, "cectxerr\n");
		/* Report CEC tx error event */
		cec_txerr(adev);
	} else if (ceci && cectx) {
		dev_dbg(adev->dev, "cectx\n");
		/* Report CEC tx event */
		cec_tx(adev);
	}

	/* HDCP event */
	if (hdcpi) {
		dev_dbg(adev->dev, "hdcpch:%0x\n", hdcps);

		/* Save the status for later fast retrieve */
		adev->params.hdcp_state = hdcps;

		/* Report HDCP status change event */
		hdcp_changed(adev);
	}

	return 0;

av8100_int_event_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return -EFAULT;
}

static int av8100_fwdl_event_handle(struct av8100_device *adev)
{
	dev_dbg(adev->dev, "%s\n", __func__);

	usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);

	/* Download fw */
	if (av8100_download_firmware(I2C_INTERFACE))
		goto av8100_fwdl_event_handle_err;

	/* HPDM=1, CCM=1 */
	if (av8100_reg_stby_int_mask_w(
			AV8100_STANDBY_INTERRUPT_MASK_HPDM_HIGH,
			AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CCM_HIGH,
			AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
			AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW))
		goto av8100_fwdl_event_handle_err;

	return 0;

av8100_fwdl_event_handle_err:
	dev_dbg(adev->dev, "%s error\n", __func__);
	return -EFAULT;
}

static int av8100_thread(void *p)
{
	int				flags;
	struct av8100_device		*adev = p;
	int				cnt;
	struct av8100_platform_data	*pdata = adev->dev->platform_data;

	while (1) {
		wait_event_interruptible(adev->event, (adev->flag != 0));
		flags = adev->flag;
		adev->flag = 0;

		if (flags & AV8100_INT_EVENT) {
			cnt = 0;
			do {
				cnt++;
				(void)av8100_int_event_handle(adev);
			} while (gpio_get_value(IRQ_TO_GPIO(pdata->irq)) &&
							(cnt < INT_CNT_MAX));
			if (cnt > 1)
				dev_dbg(adev->dev, "%s %d\n", __func__, cnt);
		}

		if (flags & AV8100_FWDL_EVENT1)
			(void)av8100_fwdl_event_handle(adev);

		if (flags & AV8100_FWDL_EVENT2) {
			(void)av8100_fwdl_event_handle(adev);
			set_hrtimer(adev, TIMER_SCAN);
		}

		if (flags & AV8100_1S_TIMER_EVENT)
			(void)av8100_1s_timer_event_handle(adev);

		if (flags & AV8100_SCAN_TIMER_EVENT)
			(void)av8100_scan_timer_event_handle(adev);

		if (flags & AV8100_HPD_TIMER_EVENT)
			(void)av8100_hpd_timer_event_handle(adev);

		if (flags & AV8100_POWERSCAN_TIMER_EVENT)
			(void)av8100_powerscan_timer_event_handle(adev);

		if (flags & AV8100_RESUME_TIMER_EVENT)
			(void)av8100_resume_timer_event_handle(adev);

		if (flags & AV8100_POWERUP_TIMER_EVENT)
			(void)av8100_powerup_timer_event_handle(adev);

		if (flags & AV8100_POWERDOWN_TIMER_EVENT)
			(void)av8100_powerdown_timer_event_handle(adev);

		if (flags & AV8100_CHECK_HW_STATUS_EVENT)
			(void)check_hw_status(adev);

		if (flags & AV8100_POWER_CYCLE_EVENT)
			(void)power_cycle(adev);
	}

	return 0;
}

static irqreturn_t av8100_intr_handler(int irq, void *p)
{
	struct av8100_device *adev;

	adev = (struct av8100_device *) p;
	adev->flag |= AV8100_INT_EVENT;
	wake_up_interruptible(&adev->event);

	return IRQ_HANDLED;
}

static u16 av8100_get_te_line_nb(
	enum av8100_output_CEA_VESA output_video_format)
{
	u16 retval;

	switch (output_video_format) {
	case AV8100_CEA1_640X480P_59_94HZ:
	case AV8100_CEA2_3_720X480P_59_94HZ:
	case AV8100_VESA16_1024X768P_60HZ:
		retval = AV8100_TE_LINE_NB_30;
		break;

	case AV8100_CEA4_1280X720P_60HZ:
	case AV8100_CEA60_1280X720P_24HZ:
	case AV8100_CEA61_1280X720P_25HZ:
	case AV8100_CEA62_1280X720P_30HZ:
		retval = AV8100_TE_LINE_NB_21;
		break;

	case AV8100_CEA5_1920X1080I_60HZ:
	case AV8100_CEA6_7_NTSC_60HZ:
	case AV8100_CEA20_1920X1080I_50HZ:
	case AV8100_CEA21_22_576I_PAL_50HZ:
	case AV8100_VESA27_1280X800P_59_91HZ:
		retval = AV8100_TE_LINE_NB_18;
		break;

	case AV8100_CEA14_15_480p_60HZ:
		retval = AV8100_TE_LINE_NB_32;
		break;

	case AV8100_CEA17_18_720X576P_50HZ:
	case AV8100_CEA29_30_576P_50HZ:
		retval = AV8100_TE_LINE_NB_40;
		break;

	case AV8100_CEA19_1280X720P_50HZ:
	case AV8100_VESA39_1360X768P_60_02HZ:
		retval = AV8100_TE_LINE_NB_22;
		break;

	case AV8100_CEA32_1920X1080P_24HZ:
	case AV8100_CEA33_1920X1080P_25HZ:
	case AV8100_CEA34_1920X1080P_30HZ:
		retval = AV8100_TE_LINE_NB_38;
		break;

	case AV8100_VESA9_800X600P_60_32HZ:
		retval = AV8100_TE_LINE_NB_24;
		break;

	case AV8100_VESA14_848X480P_60HZ:
		retval = AV8100_TE_LINE_NB_29;
		break;

	case AV8100_VESA22_1280X768P_59_99HZ:
		retval = AV8100_TE_LINE_NB_17;
		break;

	case AV8100_VESA23_1280X768P_59_87HZ:
	case AV8100_VESA81_1366X768P_59_79HZ:
		retval = AV8100_TE_LINE_NB_25;
		break;

	case AV8100_VESA28_1280X800P_59_81HZ:
		retval = AV8100_TE_LINE_NB_26;
		break;

	case AV8100_CEA16_1920X1080P_60HZ:
	case AV8100_CEA31_1920x1080P_50Hz:
	default:
		/* TODO */
		retval = AV8100_TE_LINE_NB_38;
		break;
	}

	return retval;
}

static u16 av8100_get_ui_x4(
	enum av8100_output_CEA_VESA output_video_format)
{
	return AV8100_UI_X4_DEFAULT;
}

static int av8100_config_video_output_dep(
		enum av8100_output_CEA_VESA output_format)
{
	int retval;
	union av8100_configuration config;

	/* video input */
	config.video_input_format.dsi_input_mode =
		AV8100_HDMI_DSI_COMMAND_MODE;
	config.video_input_format.input_pixel_format = AV8100_INPUT_PIX_RGB565;
	config.video_input_format.total_horizontal_pixel =
		av8100_all_cea[output_format].htotale;
	config.video_input_format.total_horizontal_active_pixel =
		av8100_all_cea[output_format].hactive;
	config.video_input_format.total_vertical_lines =
		av8100_all_cea[output_format].vtotale;
	config.video_input_format.total_vertical_active_lines =
		av8100_all_cea[output_format].vactive;

	switch (output_format) {
	case AV8100_CEA5_1920X1080I_60HZ:
	case AV8100_CEA20_1920X1080I_50HZ:
	case AV8100_CEA21_22_576I_PAL_50HZ:
	case AV8100_CEA6_7_NTSC_60HZ:
		config.video_input_format.video_mode =
			AV8100_VIDEO_INTERLACE;
		break;

	default:
		config.video_input_format.video_mode =
			AV8100_VIDEO_PROGRESSIVE;
		break;
	}

	config.video_input_format.nb_data_lane =
		AV8100_DATA_LANES_USED_2;
	config.video_input_format.nb_virtual_ch_command_mode = 0;
	config.video_input_format.nb_virtual_ch_video_mode = 0;
	config.video_input_format.ui_x4 = av8100_get_ui_x4(output_format);
	config.video_input_format.TE_line_nb = av8100_get_te_line_nb(
		output_format);
	config.video_input_format.TE_config = AV8100_TE_OFF;
	config.video_input_format.master_clock_freq = 0;

	retval = av8100_conf_prep(
		AV8100_COMMAND_VIDEO_INPUT_FORMAT, &config);
	if (retval)
		return -EFAULT;

	/* DENC */
	switch (output_format) {
	case AV8100_CEA21_22_576I_PAL_50HZ:
		config.denc_format.cvbs_video_format = AV8100_CVBS_625;
		config.denc_format.standard_selection = AV8100_PAL_BDGHI;
		break;

	case AV8100_CEA6_7_NTSC_60HZ:
		config.denc_format.cvbs_video_format = AV8100_CVBS_525;
		config.denc_format.standard_selection = AV8100_NTSC_M;
		break;

	default:
		/* Not supported */
		break;
	}

	return 0;
}

static int av8100_config_init(struct av8100_device *adev)
{
	int retval;
	union av8100_configuration config;

	dev_dbg(adev->dev, "%s\n", __func__);

	memset(&config, 0, sizeof(union av8100_configuration));
	memset(&adev->config, 0, sizeof(struct av8100_config));

	/* Color conversion */
	config.color_transform = AV8100_COLOR_TRANSFORM_INDENTITY;
	retval = av8100_conf_prep(
		AV8100_COMMAND_COLORSPACECONVERSION, &config);
	if (retval)
		return -EFAULT;

	/* DENC */
	config.denc_format.cvbs_video_format = AV8100_CVBS_625;
	config.denc_format.standard_selection = AV8100_PAL_BDGHI;
	config.denc_format.enable = 0;
	config.denc_format.macrovision_enable = 0;
	config.denc_format.internal_generator = 0;
	retval = av8100_conf_prep(AV8100_COMMAND_DENC, &config);
	if (retval)
		return -EFAULT;

	/* Video output */
	config.video_output_format.video_output_cea_vesa =
		AV8100_CEA4_1280X720P_60HZ;

	retval = av8100_conf_prep(
		AV8100_COMMAND_VIDEO_OUTPUT_FORMAT, &config);
	if (retval)
		return -EFAULT;

	/* Video input */
	av8100_config_video_output_dep(
		config.video_output_format.video_output_cea_vesa);

	/* Pattern generator */
	config.pattern_generator_format.pattern_audio_mode =
		AV8100_PATTERN_AUDIO_OFF;
	config.pattern_generator_format.pattern_type =
		AV8100_PATTERN_GENERATOR;
	config.pattern_generator_format.pattern_video_format =
		AV8100_PATTERN_720P;
	retval = av8100_conf_prep(AV8100_COMMAND_PATTERNGENERATOR,
		&config);
	if (retval)
		return -EFAULT;

	/* Audio input */
	config.audio_input_format.audio_input_if_format	=
		AV8100_AUDIO_I2SDELAYED_MODE;
	config.audio_input_format.i2s_input_nb = 1;
	config.audio_input_format.sample_audio_freq = AV8100_AUDIO_FREQ_48KHZ;
	config.audio_input_format.audio_word_lg = AV8100_AUDIO_16BITS;
	config.audio_input_format.audio_format = AV8100_AUDIO_LPCM_MODE;
	config.audio_input_format.audio_if_mode = AV8100_AUDIO_MASTER;
	config.audio_input_format.audio_mute = AV8100_AUDIO_MUTE_DISABLE;
	retval = av8100_conf_prep(
		AV8100_COMMAND_AUDIO_INPUT_FORMAT, &config);
	if (retval)
		return -EFAULT;

	/* HDMI mode */
	config.hdmi_format.hdmi_mode	= AV8100_HDMI_ON;
	config.hdmi_format.hdmi_format	= AV8100_HDMI;
	config.hdmi_format.dvi_format	= AV8100_DVI_CTRL_CTL0;
	retval = av8100_conf_prep(AV8100_COMMAND_HDMI, &config);
	if (retval)
		return -EFAULT;

	/* EDID section readback */
	config.edid_section_readback_format.address = 0xA0;
	config.edid_section_readback_format.block_number = 0;
	retval = av8100_conf_prep(
		AV8100_COMMAND_EDID_SECTION_READBACK, &config);
	if (retval)
		return -EFAULT;

	return 0;
}

static int av8100_params_init(struct av8100_device *adev)
{
	dev_dbg(adev->dev, "%s\n", __func__);

	memset(&adev->params, 0, sizeof(struct av8100_params));

	adev->params.denc_off_time = AV8100_DENC_OFF_TIME;
	adev->params.hdmi_off_time = AV8100_DISP_ON_HDMI_OFF_TIME;
	adev->params.on_time = AV8100_ON_TIME;

	adev->params.hdcpm = AV8100_GENERAL_INTERRUPT_MASK_HDCPM_HIGH;
	adev->params.cecm = AV8100_GENERAL_INTERRUPT_MASK_CECM_HIGH;
	adev->params.uovbm = AV8100_GENERAL_INTERRUPT_MASK_UOVBM_HIGH;

	adev->params.powerdown_scan = AV8100_SCAN_WHEN_DISPLAY_IS_POWERED_OFF;

	return 0;
}

static void clr_plug_status(struct av8100_device *adev,
			enum av8100_plugin_status status)
{
	adev->status.av8100_plugin_status &= ~status;

	switch (status) {
	case AV8100_HDMI_PLUGIN:
		switch (adev->params.plug_state) {
		case AV8100_UNPLUGGED:
		default:
			break;

		case AV8100_PLUGGED:
			adev->params.plug_state = AV8100_UNPLUGGED;
			dev_dbg(adev->dev, "plug_state:0\n");

			if (adev->params.hdmi_ev_cb)
				adev->params.hdmi_ev_cb(
					AV8100_HDMI_EVENT_HDMI_PLUGOUT);
			break;
		}
		break;

	case AV8100_CVBS_PLUGIN:
		/* TODO */
		break;

	default:
		break;
	}
}

static void set_plug_status(struct av8100_device *adev,
			enum av8100_plugin_status status)
{
	adev->status.av8100_plugin_status |= status;

	switch (status) {
	case AV8100_HDMI_PLUGIN:
		switch (adev->params.plug_state) {
		case AV8100_UNPLUGGED:
			adev->params.plug_state = AV8100_PLUGGED;
			dev_dbg(adev->dev, "plug_state:1\n");

			if (adev->params.hdmi_ev_cb)
				adev->params.hdmi_ev_cb(
					AV8100_HDMI_EVENT_HDMI_PLUGIN);
			break;

		case AV8100_PLUGGED:
		default:
			break;
		}
		break;

	case AV8100_CVBS_PLUGIN:
		/* TODO */
		break;

	default:
		break;
	}
}

static void cec_rx(struct av8100_device *adev)
{
	if (adev->params.hdmi_ev_cb)
		adev->params.hdmi_ev_cb(AV8100_HDMI_EVENT_CEC);
}

static void cec_tx(struct av8100_device *adev)
{
	if (adev->params.hdmi_ev_cb)
		adev->params.hdmi_ev_cb(AV8100_HDMI_EVENT_CECTX);
}

static void cec_txerr(struct av8100_device *adev)
{
	if (adev->params.hdmi_ev_cb)
		adev->params.hdmi_ev_cb(AV8100_HDMI_EVENT_CECTXERR);
}

static void hdcp_changed(struct av8100_device *adev)
{
	if (adev->params.hdmi_ev_cb)
		adev->params.hdmi_ev_cb(AV8100_HDMI_EVENT_HDCP);
}

static void cci_err(struct av8100_device *adev)
{
	if (adev->params.hdmi_ev_cb)
		adev->params.hdmi_ev_cb(AV8100_HDMI_EVENT_CCIERR);
}

static void av8100_set_state(struct av8100_device *adev,
			enum av8100_operating_mode state)
{
	if (state != adev->status.av8100_state) {
		dev_dbg(adev->dev, "%s %d\n", __func__, state);
		adev->status.av8100_state = state;
	}
	if (state <= AV8100_OPMODE_STANDBY) {
		clr_plug_status(adev, AV8100_HDMI_PLUGIN);
		clr_plug_status(adev, AV8100_CVBS_PLUGIN);
		adev->status.hdmi_on = false;
	}
}

/**
 * write_single_byte() - Write a single byte to av8100
 * through i2c interface.
 * @client:	i2c client structure
 * @reg:	register offset
 * @data:	data byte to be written
 *
 * This funtion uses smbus byte write API to write a single byte to av8100
 **/
static int write_single_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;
	struct device *dev = &client->dev;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_dbg(dev, "i2c smbus write byte failed\n");

	return ret;
}

/**
 * read_single_byte() - read single byte from av8100
 * through i2c interface
 * @client:     i2c client structure
 * @reg:        register offset
 * @val:	register value
 *
 * This funtion uses smbus read block API to read single byte from the reg
 * offset.
 **/
static int read_single_byte(struct i2c_client *client, u8 reg, u8 *val)
{
	int value;
	struct device *dev = &client->dev;

	value = i2c_smbus_read_byte_data(client, reg);
	if (value < 0) {
		dev_dbg(dev, "i2c smbus read byte failed,read data = %x from offset:%x\n",
							value, reg);
		return -EFAULT;
	}

	*val = (u8) value;
	return 0;
}

/**
 * write_multi_byte() - Write a multiple bytes to av8100 through
 * i2c interface.
 * @client:	i2c client structure
 * @buf:	buffer to be written
 * @nbytes:	nunmber of bytes to be written
 *
 * This funtion uses smbus block write API's to write n number of bytes to the
 * av8100
 **/
static int write_multi_byte(struct i2c_client *client, u8 reg,
		u8 *buf, u8 nbytes)
{
	int ret;
	struct device *dev = &client->dev;

	ret = i2c_smbus_write_i2c_block_data(client, reg, nbytes, buf);
	if (ret < 0)
		dev_dbg(dev, "i2c smbus write multi byte error\n");

	return ret;
}

static int configuration_video_input_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_video_input_cmd.dsi_input_mode;
	buffer[1] = adev->config.hdmi_video_input_cmd.input_pixel_format;
	buffer[2] = REG_16_8_MSB(adev->config.hdmi_video_input_cmd.
		total_horizontal_pixel);
	buffer[3] = REG_16_8_LSB(adev->config.hdmi_video_input_cmd.
		total_horizontal_pixel);
	buffer[4] = REG_16_8_MSB(adev->config.hdmi_video_input_cmd.
		total_horizontal_active_pixel);
	buffer[5] = REG_16_8_LSB(adev->config.hdmi_video_input_cmd.
		total_horizontal_active_pixel);
	buffer[6] = REG_16_8_MSB(adev->config.hdmi_video_input_cmd.
		total_vertical_lines);
	buffer[7] = REG_16_8_LSB(adev->config.hdmi_video_input_cmd.
		total_vertical_lines);
	buffer[8] = REG_16_8_MSB(adev->config.hdmi_video_input_cmd.
		total_vertical_active_lines);
	buffer[9] = REG_16_8_LSB(adev->config.hdmi_video_input_cmd.
		total_vertical_active_lines);
	buffer[10] = adev->config.hdmi_video_input_cmd.video_mode;
	buffer[11] = adev->config.hdmi_video_input_cmd.nb_data_lane;
	buffer[12] = adev->config.hdmi_video_input_cmd.
		nb_virtual_ch_command_mode;
	buffer[13] = adev->config.hdmi_video_input_cmd.
		nb_virtual_ch_video_mode;
	buffer[14] = REG_16_8_MSB(adev->config.hdmi_video_input_cmd.
		TE_line_nb);
	buffer[15] = REG_16_8_LSB(adev->config.hdmi_video_input_cmd.
		TE_line_nb);
	buffer[16] = adev->config.hdmi_video_input_cmd.TE_config;
	buffer[17] = REG_32_8_MSB(adev->config.hdmi_video_input_cmd.
		master_clock_freq);
	buffer[18] = REG_32_8_MMSB(adev->config.hdmi_video_input_cmd.
		master_clock_freq);
	buffer[19] = REG_32_8_MLSB(adev->config.hdmi_video_input_cmd.
		master_clock_freq);
	buffer[20] = REG_32_8_LSB(adev->config.hdmi_video_input_cmd.
		master_clock_freq);
	buffer[21] = adev->config.hdmi_video_input_cmd.ui_x4;

	*length = AV8100_COMMAND_VIDEO_INPUT_FORMAT_SIZE - 1;
	return 0;

}

static int configuration_audio_input_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_audio_input_cmd.audio_input_if_format;
	buffer[1] = adev->config.hdmi_audio_input_cmd.i2s_input_nb;
	buffer[2] = adev->config.hdmi_audio_input_cmd.sample_audio_freq;
	buffer[3] = adev->config.hdmi_audio_input_cmd.audio_word_lg;
	buffer[4] = adev->config.hdmi_audio_input_cmd.audio_format;
	buffer[5] = adev->config.hdmi_audio_input_cmd.audio_if_mode;
	buffer[6] = adev->config.hdmi_audio_input_cmd.audio_mute;

	*length = AV8100_COMMAND_AUDIO_INPUT_FORMAT_SIZE - 1;
	return 0;
}

static int configuration_video_output_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_video_output_cmd.
		video_output_cea_vesa;

	if (buffer[0] == AV8100_CUSTOM) {
		buffer[1] = adev->config.hdmi_video_output_cmd.
			vsync_polarity;
		buffer[2] = adev->config.hdmi_video_output_cmd.
			hsync_polarity;
		buffer[3] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.total_horizontal_pixel);
		buffer[4] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.total_horizontal_pixel);
		buffer[5] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.total_horizontal_active_pixel);
		buffer[6] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.total_horizontal_active_pixel);
		buffer[7] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.total_vertical_in_half_lines);
		buffer[8] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.total_vertical_in_half_lines);
		buffer[9] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.
				total_vertical_active_in_half_lines);
		buffer[10] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.
				total_vertical_active_in_half_lines);
		buffer[11] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.hsync_start_in_pixel);
		buffer[12] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.hsync_start_in_pixel);
		buffer[13] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.hsync_length_in_pixel);
		buffer[14] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.hsync_length_in_pixel);
		buffer[15] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.vsync_start_in_half_line);
		buffer[16] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.vsync_start_in_half_line);
		buffer[17] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.vsync_length_in_half_line);
		buffer[18] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.vsync_length_in_half_line);
		buffer[19] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.hor_video_start_pixel);
		buffer[20] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.hor_video_start_pixel);
		buffer[21] = REG_16_8_MSB(adev->config.
			hdmi_video_output_cmd.vert_video_start_pixel);
		buffer[22] = REG_16_8_LSB(adev->config.
			hdmi_video_output_cmd.vert_video_start_pixel);
		buffer[23] = adev->config.hdmi_video_output_cmd.video_type;
		buffer[24] = adev->config.hdmi_video_output_cmd.pixel_repeat;
		buffer[25] = REG_32_8_MSB(adev->config.
			hdmi_video_output_cmd.pixel_clock_freq_Hz);
		buffer[26] = REG_32_8_MMSB(adev->config.
			hdmi_video_output_cmd.pixel_clock_freq_Hz);
		buffer[27] = REG_32_8_MLSB(adev->config.
			hdmi_video_output_cmd.pixel_clock_freq_Hz);
		buffer[28] = REG_32_8_LSB(adev->config.
			hdmi_video_output_cmd.pixel_clock_freq_Hz);

		*length = AV8100_COMMAND_VIDEO_OUTPUT_FORMAT_SIZE - 1;
	} else {
		*length = 1;
	}

	return 0;
}

static int configuration_video_scaling_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		h_start_in_pixel);
	buffer[1] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		h_start_in_pixel);
	buffer[2] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		h_stop_in_pixel);
	buffer[3] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		h_stop_in_pixel);
	buffer[4] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		v_start_in_line);
	buffer[5] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		v_start_in_line);
	buffer[6] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		v_stop_in_line);
	buffer[7] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		v_stop_in_line);
	buffer[8] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		h_start_out_pixel);
	buffer[9] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd
		.h_start_out_pixel);
	buffer[10] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		h_stop_out_pixel);
	buffer[11] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		h_stop_out_pixel);
	buffer[12] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		v_start_out_line);
	buffer[13] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		v_start_out_line);
	buffer[14] = REG_16_8_MSB(adev->config.hdmi_video_scaling_cmd.
		v_stop_out_line);
	buffer[15] = REG_16_8_LSB(adev->config.hdmi_video_scaling_cmd.
		v_stop_out_line);

	*length = AV8100_COMMAND_VIDEO_SCALING_FORMAT_SIZE - 1;
	return 0;
}

static int configuration_colorspace_conversion_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	const struct color_conversion_cmd *hdmi_color_space_conversion_cmd;

	hdmi_color_space_conversion_cmd =
		get_color_transform_cmd(adev, adev->config.color_transform);

	buffer[0] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c0);
	buffer[1] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c0);
	buffer[2] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c1);
	buffer[3] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c1);
	buffer[4] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c2);
	buffer[5] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c2);
	buffer[6] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c3);
	buffer[7] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c3);
	buffer[8] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c4);
	buffer[9] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c4);
	buffer[10] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c5);
	buffer[11] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c5);
	buffer[12] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c6);
	buffer[13] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c6);
	buffer[14] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c7);
	buffer[15] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c7);
	buffer[16] = REG_12_8_MSB(hdmi_color_space_conversion_cmd->c8);
	buffer[17] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->c8);
	buffer[18] = REG_10_8_MSB(hdmi_color_space_conversion_cmd->aoffset);
	buffer[19] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->aoffset);
	buffer[20] = REG_10_8_MSB(hdmi_color_space_conversion_cmd->boffset);
	buffer[21] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->boffset);
	buffer[22] = REG_10_8_MSB(hdmi_color_space_conversion_cmd->coffset);
	buffer[23] = REG_16_8_LSB(hdmi_color_space_conversion_cmd->coffset);
	buffer[24] = hdmi_color_space_conversion_cmd->lmax;
	buffer[25] = hdmi_color_space_conversion_cmd->lmin;
	buffer[26] = hdmi_color_space_conversion_cmd->cmax;
	buffer[27] = hdmi_color_space_conversion_cmd->cmin;

	*length = AV8100_COMMAND_COLORSPACECONVERSION_SIZE - 1;
	return 0;
}

static int configuration_cec_message_write_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_cec_message_write_cmd.buffer_length;
	memcpy(&buffer[1], adev->config.hdmi_cec_message_write_cmd.buffer,
			adev->config.hdmi_cec_message_write_cmd.buffer_length);

	*length = adev->config.hdmi_cec_message_write_cmd.buffer_length + 1;

	return 0;
}

static int configuration_cec_message_read_get(char *buffer,
	unsigned int *length)
{
	/* No buffer data */
	*length = AV8100_COMMAND_CEC_MESSAGE_READ_BACK_SIZE - 1;
	return 0;
}

static int configuration_denc_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_denc_cmd.cvbs_video_format;
	buffer[1] = adev->config.hdmi_denc_cmd.standard_selection;
	buffer[2] = adev->config.hdmi_denc_cmd.enable;
	buffer[3] = adev->config.hdmi_denc_cmd.macrovision_enable;
	buffer[4] = adev->config.hdmi_denc_cmd.internal_generator;

	*length = AV8100_COMMAND_DENC_SIZE - 1;
	return 0;
}

static int configuration_hdmi_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_cmd.hdmi_mode;
	buffer[1] = adev->config.hdmi_cmd.hdmi_format;
	buffer[2] = adev->config.hdmi_cmd.dvi_format;

	*length = AV8100_COMMAND_HDMI_SIZE - 1;
	return 0;
}

static int configuration_hdcp_sendkey_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_hdcp_send_key_cmd.key_number;
	memcpy(&buffer[1], adev->config.hdmi_hdcp_send_key_cmd.data,
			adev->config.hdmi_hdcp_send_key_cmd.data_len);

	*length = adev->config.hdmi_hdcp_send_key_cmd.data_len + 1;
	return 0;
}

static int configuration_hdcp_management_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_hdcp_management_format_cmd.req_type;
	buffer[1] = adev->config.hdmi_hdcp_management_format_cmd.encr_use;

	*length = AV8100_COMMAND_HDCP_MANAGEMENT_SIZE - 1;
	return 0;
}

static int configuration_infoframe_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_infoframes_cmd.type;
	buffer[1] = adev->config.hdmi_infoframes_cmd.version;
	buffer[2] = adev->config.hdmi_infoframes_cmd.length;
	buffer[3] = adev->config.hdmi_infoframes_cmd.crc;
	memcpy(&buffer[4], adev->config.hdmi_infoframes_cmd.data,
					HDMI_INFOFRAME_DATA_SIZE);

	*length = adev->config.hdmi_infoframes_cmd.length + 4;
	return 0;
}

static int configuration_audio_infoframe_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_audio_infoframe.type;
	if (buffer[0] != INFOFRAME_TYPE_AUDIO)
		return -EINVAL;

	buffer[1] = adev->config.hdmi_audio_infoframe.version;
	buffer[2] = adev->config.hdmi_audio_infoframe.length;
	buffer[3] = adev->config.hdmi_audio_infoframe.crc;
	memcpy(&buffer[4], adev->config.hdmi_audio_infoframe.data,
					HDMI_INFOFRAME_DATA_SIZE);

	*length = adev->config.hdmi_audio_infoframe.length + 4;
	return 0;
}

static int av8100_edid_section_readback_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_edid_section_readback_cmd.address;
	buffer[1] = adev->config.hdmi_edid_section_readback_cmd.
		block_number;

	*length = AV8100_COMMAND_EDID_SECTION_READBACK_SIZE - 1;
	return 0;
}

static int configuration_pattern_generator_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_pattern_generator_cmd.pattern_type;
	buffer[1] = adev->config.hdmi_pattern_generator_cmd.
							pattern_video_format;
	buffer[2] = adev->config.hdmi_pattern_generator_cmd.
							pattern_audio_mode;

	*length = AV8100_COMMAND_PATTERNGENERATOR_SIZE - 1;

	return 0;
}

static int configuration_fuse_aes_key_get(struct av8100_device *adev,
			char *buffer, unsigned int *length)
{
	buffer[0] = adev->config.hdmi_fuse_aes_key_cmd.fuse_operation;
	if (adev->config.hdmi_fuse_aes_key_cmd.fuse_operation) {
		/* Write key command */
		memcpy(&buffer[1], adev->config.hdmi_fuse_aes_key_cmd.key,
			HDMI_FUSE_AES_KEY_SIZE);

		*length = AV8100_COMMAND_FUSE_AES_KEY_SIZE - 1;
	} else {
		/* Check key command */
		*length = AV8100_COMMAND_FUSE_AES_CHK_SIZE - 1;
	}
	return 0;
}

static int get_command_return_first(struct i2c_client *i2c,
		enum av8100_command_type command_type)
{
	int retval = 0;
	char val;
	struct device *dev = &i2c->dev;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;
	retval = read_single_byte(i2c, AV8100_COMMAND_OFFSET, &val);
	if (retval) {
		dev_dbg(dev, "%s 1st ret failed\n", __func__);
		return retval;
	}

	if (val != (0x80 | command_type)) {
		dev_dbg(dev, "%s 1st ret:%x\n", __func__, val);
		return -EFAULT;
	}

	return 0;
}

static int get_command_return_data(struct i2c_client *i2c,
	enum av8100_command_type command_type,
	u8 *command_buffer,
	u8 *buffer_length,
	u8 *buffer)
{
	int retval = 0;
	char val;
	int index = 0;
	struct device *dev = &i2c->dev;
	u8 edid_chksum = 0;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;
	if (buffer_length)
		*buffer_length = 0;

	switch (command_type) {
	case AV8100_COMMAND_VIDEO_INPUT_FORMAT:
	case AV8100_COMMAND_AUDIO_INPUT_FORMAT:
	case AV8100_COMMAND_VIDEO_OUTPUT_FORMAT:
	case AV8100_COMMAND_VIDEO_SCALING_FORMAT:
	case AV8100_COMMAND_COLORSPACECONVERSION:
	case AV8100_COMMAND_CEC_MESSAGE_WRITE:
	case AV8100_COMMAND_DENC:
	case AV8100_COMMAND_HDMI:
	case AV8100_COMMAND_INFOFRAMES:
	case AV8100_COMMAND_AUDIO_INFOFRAME:
	case AV8100_COMMAND_PATTERNGENERATOR:
		/* Get the second return byte */
		retval = read_single_byte(i2c,
			AV8100_2ND_RET_BYTE_OFFSET, &val);
		if (retval)
			goto get_command_return_data_fail2r;

		if (val) {
			retval = -EFAULT;
			goto get_command_return_data_fail2v;
		}
		break;

	case AV8100_COMMAND_CEC_MESSAGE_READ_BACK:
		if ((buffer == NULL) ||	(buffer_length == NULL)) {
			retval = -EINVAL;
			goto get_command_return_data_fail;
		}

		/* Get the return buffer length */
		retval = read_single_byte(i2c, AV8100_CEC_ADDR_OFFSET, &val);
		if (retval)
			goto get_command_return_data_fail;

		dev_dbg(dev, "cec buflen:%d\n", val);
		*buffer_length = val;

		if (*buffer_length >
			HDMI_CEC_READ_MAXSIZE) {
			dev_dbg(dev, "CEC size too large %d\n",
				*buffer_length);
			*buffer_length = HDMI_CEC_READ_MAXSIZE;
		}

		dev_dbg(dev, "return data: ");

		/* Get the return buffer */
		for (index = 0; index < *buffer_length; ++index) {
			retval = read_single_byte(i2c,
				AV8100_CEC_RET_BUF_OFFSET + index, &val);
			if (retval) {
				*buffer_length = 0;
				goto get_command_return_data_fail;
			} else {
				*(buffer + index) = val;
				dev_dbg(dev, "%02x ", *(buffer + index));
			}
		}

		dev_dbg(dev, "\n");
		break;

	case AV8100_COMMAND_HDCP_MANAGEMENT:
		{
		u8 nrdev;
		u8 devcnt;
		int cnt;

		/* Get the second return byte */
		retval = read_single_byte(i2c,
			AV8100_2ND_RET_BYTE_OFFSET, &val);
		if (retval) {
			goto get_command_return_data_fail2r;
		} else {
			/* Check the second return byte */
			if (val)
				goto get_command_return_data_fail2v;
		}

		if ((buffer == NULL) || (buffer_length == NULL))
			/* Ignore return data */
			break;

		dev_dbg(dev, "req_type:%02x ", command_buffer[0]);

		/* Check if revoc list data is requested */
		switch (command_buffer[0]) {
		case HDMI_REQUEST_FOR_REVOCATION_LIST_INPUT:
		case HDMI_CONTINUE_AUTHENTICATION:
			/* Return data to be read */
			break;
		default:
			/* No more return data */
			goto hdcp_management_end;
			break;
		}

		dev_dbg(dev, "return data: ");

		/* Get the return buffer */
		for (cnt = 0; cnt < HDMI_HDCP_MGMT_BKSV_SIZE; cnt++) {
			retval = read_single_byte(i2c,
				AV8100_HDCP_RET_BUF_OFFSET + index, &val);
			if (retval) {
				*buffer_length = 0;
				goto get_command_return_data_fail;
			} else {
				*(buffer + index) = val;
				dev_dbg(dev, "%02x ", *(buffer + index));
			}
			index++;
		}

		/* Get Device count */
		retval = read_single_byte(i2c,
			AV8100_HDCP_RET_BUF_OFFSET + index, &nrdev);
		if (retval) {
			*buffer_length = 0;
			goto get_command_return_data_fail;
		} else {
			*(buffer + index) = nrdev;
			dev_dbg(dev, "%02x ", *(buffer + index));
		}
		index++;

		/* Determine number of devices */
		nrdev &= HDMI_HDCP_MGMT_DEVICE_MASK;
		if (nrdev > HDMI_HDCP_MGMT_MAX_DEVICES_SIZE)
			nrdev = HDMI_HDCP_MGMT_MAX_DEVICES_SIZE;

		/* Get Bksv for each connected equipment */
		for (devcnt = 0; devcnt < nrdev; devcnt++)
			for (cnt = 0; cnt < HDMI_HDCP_MGMT_BKSV_SIZE; cnt++) {
				retval = read_single_byte(i2c,
					AV8100_HDCP_RET_BUF_OFFSET + index,
									&val);
				if (retval) {
					*buffer_length = 0;
					goto get_command_return_data_fail;
				} else {
					*(buffer + index) = val;
					dev_dbg(dev, "%02x ",
							*(buffer + index));
				}
				index++;
			}

		if (nrdev == 0)
			goto hdcp_management_end;

		/* Get SHA signature */
		for (cnt = 0; cnt < HDMI_HDCP_MGMT_SHA_SIZE - 1; cnt++) {
			retval = read_single_byte(i2c,
				AV8100_HDCP_RET_BUF_OFFSET + index, &val);
			if (retval) {
				*buffer_length = 0;
				goto get_command_return_data_fail;
			} else {
				*(buffer + index) = val;
				dev_dbg(dev, "%02x ", *(buffer + index));
			}
			index++;
		}

hdcp_management_end:
		*buffer_length = index;

		dev_dbg(dev, "\n");
		}
		break;

	case AV8100_COMMAND_EDID_SECTION_READBACK:
		if ((buffer == NULL) ||	(buffer_length == NULL)) {
			retval = -EINVAL;
			goto get_command_return_data_fail;
		}

		/* Return buffer length is fixed */
		*buffer_length = HDMI_EDIDREAD_SIZE;

		dev_dbg(dev, "return data: ");

		/* Get the return buffer */
		for (index = 0; index < *buffer_length; ++index) {
			retval = read_single_byte(i2c,
				AV8100_EDID_RET_BUF_OFFSET + index, &val);
			if (retval) {
				*buffer_length = 0;
				goto get_command_return_data_fail;
			} else {
				*(buffer + index) = val;
				edid_chksum += val;
				dev_dbg(dev, "%02x ", *(buffer + index));
			}
		}
		/* Get EDID byte 127; checksum */
		retval = read_single_byte(i2c,
			AV8100_EDID_RET_BUF_OFFSET + HDMI_EDIDREAD_SIZE, &val);
		if (retval) {
			goto get_command_return_data_fail;
		} else {
			edid_chksum += val;
			/* EDID checksum 0-127 should be 0 */
			if (edid_chksum == 0) {
				dev_dbg(dev, "checksum:%02x ok\n", val);
			} else {
				dev_dbg(dev, "checksum:%02x fail\n", val);
				retval = -EFAULT;
				goto get_command_return_data_fail;
			}
		}
		break;

	case AV8100_COMMAND_FUSE_AES_KEY:
		if ((buffer == NULL) ||	(buffer_length == NULL)) {
			retval = -EINVAL;
			goto get_command_return_data_fail;
		}

		/* Get the second return byte */
		retval = read_single_byte(i2c,
			AV8100_2ND_RET_BYTE_OFFSET, &val);

		if (retval)
			goto get_command_return_data_fail2r;

		/* Check the second return byte */
		if (val) {
			retval = -EFAULT;
			goto get_command_return_data_fail2v;
		}

		/* Return buffer length is fixed */
		*buffer_length = HDMI_FUSE_AES_KEY_RET_SIZE;

		/* Get CRC */
		retval = read_single_byte(i2c,
			AV8100_FUSE_CRC_OFFSET, &val);
		if (retval)
			goto get_command_return_data_fail;

		*buffer = val;
		dev_dbg(dev, "CRC:%02x ", val);

		/* Get programmed status */
		retval = read_single_byte(i2c,
			AV8100_FUSE_PRGD_OFFSET, &val);
		if (retval)
			goto get_command_return_data_fail;

		*(buffer + 1) = val;

		dev_dbg(dev, "programmed:%02x ", val);
		break;

	case AV8100_COMMAND_HDCP_SENDKEY:
		if ((command_buffer[0] == HDMI_LOADAES_END_BLK_NR) &&
			((buffer == NULL) || (buffer_length == NULL))) {
			retval = -EINVAL;
			goto get_command_return_data_fail;
		}

		/* Get the second return byte */
		retval = read_single_byte(i2c,
			AV8100_2ND_RET_BYTE_OFFSET, &val);
		if (retval)
			goto get_command_return_data_fail2r;

		if (val) {
			retval = -EFAULT;
			goto get_command_return_data_fail2v;
		}

		if (command_buffer[0] == HDMI_LOADAES_END_BLK_NR) {
			/* Return CRC32 if last AES block */
			int cnt;

			dev_dbg(dev, "CRC32:");
			for (cnt = 0; cnt < HDMI_CRC32_SIZE; cnt++) {
				if (read_single_byte(i2c,
					AV8100_CRC32_OFFSET + cnt, &val))
					goto get_command_return_data_fail;
				*(buffer + cnt) = val;
				dev_dbg(dev, "%02x", val);
			}

			*buffer_length = HDMI_CRC32_SIZE;
		}
		break;

	default:
		retval = -EFAULT;
		break;
	}

	return retval;
get_command_return_data_fail2r:
	dev_dbg(dev, "%s Reading 2nd return byte failed\n", __func__);
	return retval;
get_command_return_data_fail2v:
	dev_dbg(dev, "%s 2nd return byte is wrong:%x\n", __func__, val);
	return retval;
get_command_return_data_fail:
	dev_dbg(dev, "%s FAIL\n", __func__);
	return retval;
}

static int av8100_powerup1(struct av8100_device *adev)
{
	int retval;
	struct av8100_platform_data *pdata = adev->dev->platform_data;

	if (!adev->params.init_requested)
		if (pdata->init())
			return -EFAULT;

	adev->params.init_requested = true;

	/* Regulator enable */
	if ((adev->params.regulator_pwr) &&
			(adev->params.regulator_requested == false)) {
		retval = regulator_enable(adev->params.regulator_pwr);
		if (retval < 0) {
			dev_warn(adev->dev, "%s: regulator_enable failed\n",
			__func__);
			return retval;
		}
		dev_dbg(adev->dev, "regulator_enable ok\n");
		adev->params.regulator_requested = true;
	}

	/* Reset av8100 */
	gpio_set_value_cansleep(pdata->reset, 1);

	/* Need to wait before proceeding */
	usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);

	adev->params.fw_loaded = false;
	adev->params.count_cci = 0;

	av8100_set_state(adev, AV8100_OPMODE_STANDBY);

	if (pdata->alt_powerupseq) {
		dev_dbg(adev->dev, "powerup seq alt\n");
		retval = av8100_5V_w(0, 0, AV8100_ON_TIME);
		if (retval) {
			dev_err(adev->dev, "%s reg_wr err 1\n", __func__);
			goto av8100_powerup1_err;
		}

		udelay(AV8100_WATTIME_100US);

		retval = av8100_reg_stby_pend_int_w(
				AV8100_STANDBY_PENDING_INTERRUPT_HPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_HIGH);
		if (retval) {
			dev_err(adev->dev, "%s reg_wr err 2\n", __func__);
			goto av8100_powerup1_err;
		}

		udelay(AV8100_WATTIME_100US);

		retval = av8100_reg_stby_w(AV8100_STANDBY_CPD_LOW,
				AV8100_STANDBY_STBY_HIGH, pdata->mclk_freq);
		if (retval) {
			dev_err(adev->dev, "%s reg_wr err 3\n", __func__);
			goto av8100_powerup1_err;
		}

		usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);

		retval = av8100_reg_stby_w(AV8100_STANDBY_CPD_LOW,
				AV8100_STANDBY_STBY_LOW, pdata->mclk_freq);
		if (retval) {
			dev_err(adev->dev, "%s reg_wr err 4\n", __func__);
			goto av8100_powerup1_err;
		}

		usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);

		retval = av8100_reg_stby_pend_int_w(
				AV8100_STANDBY_PENDING_INTERRUPT_HPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_LOW);
		if (retval) {
			dev_err(adev->dev, "%s reg_wr err 5\n", __func__);
			goto av8100_powerup1_err;
		}

		usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);
	}

	retval = request_irq(pdata->irq, av8100_intr_handler,
			IRQF_TRIGGER_RISING, "av8100", adev);
	if (retval == 0)
		adev->params.irq_requested = true;
	else
		dev_err(adev->dev, "request_irq %d failed %d\n",
				pdata->irq, retval);

	return retval;

av8100_powerup1_err:
	av8100_powerdown();
	return -EFAULT;
}

static int av8100_powerup2(struct av8100_device *adev)
{
	int retval;

	/* ON time & OFF time on 5v HDMI plug detect */
	retval = av8100_5V_w(adev->params.denc_off_time,
				adev->params.hdmi_off_time,
				adev->params.on_time);
	if (retval) {
		dev_err(adev->dev,
			"Failed to write the value to av8100 register\n");
		return retval;
	}

	usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);

	if (adev->params.pulsing_5V == false && adev->params.fw_loaded == false)
		set_hrtimer(adev, TIMER_1S);

	if (adev->params.fw_loaded == false)
		av8100_set_state(adev, AV8100_OPMODE_SCAN);

	adev->params.suspended = false;

	return 0;
}

static int register_read_internal(u8 offset, u8 *value)
{
	int retval = 0;
	struct i2c_client *i2c;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	i2c = adev->config.client;

	/* Read from register */
	retval = read_single_byte(i2c, offset, value);
	if (retval)	{
		dev_dbg(adev->dev,
			"Failed to read the value from av8100 register\n");
		return -EFAULT;
	}

	return retval;
}

static int register_write_internal(u8 offset, u8 value)
{
	int retval;
	struct i2c_client *i2c;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	i2c = adev->config.client;

	/* Write to register */
	retval = write_single_byte(i2c, offset, value);
	if (retval) {
		dev_dbg(adev->dev,
			"Failed to write the value to av8100 register\n");
		return -EFAULT;
	}

	return 0;
}

int av8100_hdmi_get(enum av8100_hdmi_user user)
{
	struct av8100_device *adev;
	int usr_cnt;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return  -EFAULT;
	if (adev->timer_flag == TIMER_POWERDOWN) {
		dev_dbg(adev->dev, "%s busy %d\n", __func__, user);
		return  -EBUSY;
	}
	if (adev->params.suspended && (user == AV8100_HDMI_USER_AUDIO)) {
		dev_dbg(adev->dev, "%s suspended %d\n", __func__, user);
		return -EPERM;
	}

	LOCK_AV8100_USRCNT;
	switch (user) {
	case AV8100_HDMI_USER_AUDIO:
		if (!adev->usr_audio) {
			adev->usr_cnt++;
			adev->usr_audio = true;
		}
		break;
	case AV8100_HDMI_USER_VIDEO:
		if (!adev->usr_video) {
			adev->usr_cnt++;
			adev->usr_video = true;
		}
		break;
	default:
		break;
	}
	usr_cnt = adev->usr_cnt;
	UNLOCK_AV8100_USRCNT;

	dev_dbg(adev->dev, "%s %d %d\n", __func__, usr_cnt, user);

	return usr_cnt;
}
EXPORT_SYMBOL(av8100_hdmi_get);

int av8100_hdmi_put(enum av8100_hdmi_user user)
{
	struct av8100_device *adev;
	int usr_cnt;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	LOCK_AV8100_USRCNT;
	switch (user) {
	case AV8100_HDMI_USER_AUDIO:
		if (adev->usr_audio) {
			adev->usr_cnt--;
			adev->usr_audio = false;
		}
		break;
	case AV8100_HDMI_USER_VIDEO:
		if (adev->usr_video) {
			adev->usr_cnt--;
			adev->usr_video = false;
		}
		break;
	default:
		break;
	}
	usr_cnt = adev->usr_cnt;

	dev_dbg(adev->dev, "%s %d %d\n", __func__, usr_cnt, user);

	if (usr_cnt == 0) {
		if (adev->params.suspended) {
			adev->params.disp_on = false;
			av8100_powerdown();
		} else if (adev->params.plug_state == AV8100_UNPLUGGED) {
			if (adev->params.pwr_recover) {
				adev->params.pwr_recover = false;
				av8100_powerdown();
				av8100_powerup();
			}
			if (av8100_powerscan(true))
				dev_err(adev->dev, "av8100_powerscan failed\n");
		}
	}
	UNLOCK_AV8100_USRCNT;

	return usr_cnt;
}
EXPORT_SYMBOL(av8100_hdmi_put);

int av8100_hdmi_video_off(void)
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	dev_dbg(adev->dev, "%s\n", __func__);

	if (av8100_status_get().av8100_state < AV8100_OPMODE_VIDEO)
		return 0;

	/* Video is off: mask uovbi interrupts */
	if (av8100_reg_gen_int_mask_w(
			AV8100_GENERAL_INTERRUPT_MASK_EOCM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSIM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSOM_LOW,
			adev->params.cecm,
			adev->params.hdcpm,
			AV8100_GENERAL_INTERRUPT_MASK_UOVBM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_TEM_LOW))
		dev_dbg(adev->dev, "av8100_reg_gen_int_mask_w err\n");

	av8100_set_state(adev, AV8100_OPMODE_IDLE);

	return 0;
}
EXPORT_SYMBOL(av8100_hdmi_video_off);

int av8100_hdmi_video_on(void)
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	dev_dbg(adev->dev, "%s\n", __func__);

	if (av8100_status_get().av8100_state < AV8100_OPMODE_IDLE)
		return 0;

	/* Video is off: unmask uovbi interrupts */
	if (av8100_reg_gen_int_mask_w(
			AV8100_GENERAL_INTERRUPT_MASK_EOCM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSIM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSOM_LOW,
			adev->params.cecm,
			adev->params.hdcpm,
			adev->params.uovbm,
			AV8100_GENERAL_INTERRUPT_MASK_TEM_LOW))
		dev_dbg(adev->dev, "av8100_reg_gen_int_mask_w err\n");

	av8100_set_state(adev, AV8100_OPMODE_VIDEO);

	return 0;
}
EXPORT_SYMBOL(av8100_hdmi_video_on);

void av8100_conf_lock(void)
{
	LOCK_AV8100_CONF;
}
EXPORT_SYMBOL(av8100_conf_lock);

void av8100_conf_unlock(void)
{
	UNLOCK_AV8100_CONF;
}
EXPORT_SYMBOL(av8100_conf_unlock);

int av8100_powerwakeup(bool disp_user)
{
	struct av8100_device *adev;
	int ret = 0;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	dev_dbg(adev->dev, "%s %d\n", __func__, disp_user);

	if (adev->params.suspended) {
		dev_dbg(adev->dev, "suspended\n");
		goto av8100_powerwakeup_end;
	}

	if (disp_user)
		adev->params.disp_on = true;

	if (av8100_status_get().av8100_state < AV8100_OPMODE_STANDBY)
		ret = av8100_powerup();

av8100_powerwakeup_end:
	return ret;
}
EXPORT_SYMBOL(av8100_powerwakeup);

int av8100_powerscan(bool disp_user)
{
	struct av8100_device *adev;
	int ret = 0;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev) {
		ret = -EFAULT;
		goto av8100_powerscan_end;
	}

	dev_dbg(adev->dev, "%s %d\n", __func__, disp_user);

	if ((av8100_status_get().av8100_state < AV8100_OPMODE_STANDBY) ||
					(disp_user && !adev->params.disp_on))
		goto av8100_powerscan_end;

	if (disp_user)
		adev->params.disp_on = false;

	if (adev->params.powerdown_scan ||
				(disp_user && !adev->params.suspended)) {
		if (av8100_status_get().av8100_state > AV8100_OPMODE_SCAN) {
			dev_dbg(adev->dev, "set to scan mode\n");
			av8100_disable_interrupt();
			adev->flag |= AV8100_SCAN_TIMER_EVENT;
			wake_up_interruptible(&adev->event);
		}

		ret = av8100_5V_w(AV8100_DENC_OFF_TIME,
			adev->params.hdmi_off_time, adev->params.on_time);
		set_hrtimer(adev, TIMER_POWERSCAN);
	} else if (disp_user && adev->params.suspended) {
		/* Allow for a final CEC to be sent before powerdown */
		av8100_disable_interrupt();
		clr_plug_status(adev, AV8100_HDMI_PLUGIN);
		set_hrtimer(adev, TIMER_POWERDOWN);
		adev->params.busy = true;
	} else {
		if (!adev->usr_audio)
			ret = av8100_powerdown();
	}
av8100_powerscan_end:
	return ret;
}
EXPORT_SYMBOL(av8100_powerscan);

int av8100_powerup(void)
{
	int ret = 0;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	hrtimer_cancel(&adev->hrtimer);
	if (av8100_status_get().av8100_state == AV8100_OPMODE_UNDEFINED)
		return -EINVAL;

	if (adev->params.suspended) {
		dev_dbg(adev->dev, "suspended\n");
		goto av8100_powerup_end;
	}

	if (av8100_status_get().av8100_state < AV8100_OPMODE_STANDBY) {
		ret = av8100_powerup1(adev);
		if (ret) {
			dev_err(adev->dev, "av8100_powerup1 fail\n");
			return -EFAULT;
		}
	}

	if (av8100_status_get().av8100_state < AV8100_OPMODE_SCAN)
		ret = av8100_powerup2(adev);

	av8100_enable_interrupt();

av8100_powerup_end:
	return ret;
}
EXPORT_SYMBOL(av8100_powerup);

int av8100_powerdown(void)
{
	int retval = 0;
	struct av8100_device *adev;
	struct av8100_platform_data *pdata;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EFAULT;

	adev->params.busy = false;
	adev->flag = 0;
	adev->timer_flag = TIMER_UNSET;
	hrtimer_cancel(&adev->hrtimer);
	pdata = adev->dev->platform_data;
	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		goto av8100_powerdown_end;

	av8100_disable_interrupt();

	if (adev->params.irq_requested)
		free_irq(pdata->irq, adev);
	adev->params.irq_requested = false;

	if (pdata->alt_powerupseq) {
		retval = av8100_reg_stby_pend_int_w(
				AV8100_STANDBY_PENDING_INTERRUPT_HPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CPDI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_ONI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCI_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_CCRST_LOW,
				AV8100_STANDBY_PENDING_INTERRUPT_BPDIG_HIGH);

		if (retval)
			dev_err(adev->dev, "%s reg_wr err\n", __func__);
		usleep_range(AV8100_WAITTIME_50MS, AV8100_WAITTIME_50MS_MAX);
	}

	/* Remove APE OPP requirement */
	if (adev->params.opp_requested) {
		prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
				adev->miscdev.name);
		prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP,
				adev->miscdev.name);
		adev->params.opp_requested = false;
	}

	/* Clock disable */
	if (adev->params.inputclk && adev->params.inputclk_requested) {
		clk_disable(adev->params.inputclk);
		adev->params.inputclk_requested = false;
	}

	av8100_set_state(adev, AV8100_OPMODE_SHUTDOWN);

	gpio_set_value_cansleep(pdata->reset, 0);

	/* Regulator disable */
	if ((adev->params.regulator_pwr) &&
			(adev->params.regulator_requested)) {
		dev_dbg(adev->dev, "regulator_disable\n");
		regulator_disable(adev->params.regulator_pwr);
		adev->params.regulator_requested = false;
	}

	if (pdata->alt_powerupseq)
		usleep_range(AV8100_WAITTIME_5MS, AV8100_WAITTIME_5MS_MAX);

	if (adev->params.init_requested) {
		pdata->exit();
		adev->params.init_requested = false;
	}

av8100_powerdown_end:
	return retval;
}
EXPORT_SYMBOL(av8100_powerdown);

int av8100_download_firmware(enum interface_type if_type)
{
	int retval;
	int temp = 0x0;
	int increment = 15;
	int index = 0;
	int size = 0x0;
	char val = 0x0;
	char checksum = 0;
	int cnt;
	int cnt_max;
	struct i2c_client *i2c;
	u8 uc;
	u8 fdl;
	u8 hld;
	u8 wa;
	u8 ra;
	struct av8100_platform_data *pdata;
	const struct firmware *fw_file;
	u8 *fw_buff;
	int fw_bytes;
	struct av8100_device *adev;
	struct av8100_status status;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	LOCK_AV8100_FWDL;

	status = av8100_status_get();
	if (status.av8100_state <= AV8100_OPMODE_SHUTDOWN) {
		retval = -EINVAL;
		goto av8100_download_firmware_err2;
	}

	if (status.av8100_state >= AV8100_OPMODE_INIT) {
		dev_dbg(adev->dev, "FW already ok\n");
		goto av8100_download_firmware_end;
	}

	av8100_set_state(adev, AV8100_OPMODE_INIT);
	pdata = adev->dev->platform_data;

	/* Request firmware */
	if (request_firmware(&fw_file,
			       AV8100_FW_FILENAME,
			       adev->dev)) {
		dev_err(adev->dev, "fw request failed\n");
		retval = -EFAULT;
		goto av8100_download_firmware_err2;
	}

	/* Clock enable */
	if (adev->params.inputclk &&
			adev->params.inputclk_requested == false) {
		if (clk_enable(adev->params.inputclk)) {
			dev_err(adev->dev, "inputclk en failed\n");
			retval = -EFAULT;
			goto av8100_download_firmware_err;
		}

		adev->params.inputclk_requested = true;
	}

	usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);

	/* Master clock timing, running */
	retval = av8100_reg_stby_w(AV8100_STANDBY_CPD_LOW,
		AV8100_STANDBY_STBY_HIGH, pdata->mclk_freq);
	if (retval) {
		dev_err(adev->dev,
			"Failed to write the value to av8100 register\n");
		goto av8100_download_firmware_err;
	}

	/* Request 100% APE OPP */
	if (adev->params.opp_requested == false) {
		if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
				adev->miscdev.name, 100)) {
			dev_err(adev->dev, "APE OPP 100 failed\n");
			retval = -EFAULT;
			goto av8100_download_firmware_err;
		}
		if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
				adev->miscdev.name, 100)) {
			dev_err(adev->dev, "DDR OPP 100 failed\n");
			prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
					adev->miscdev.name);
			retval = -EFAULT;
			goto av8100_download_firmware_err;
		}

		adev->params.opp_requested = true;
	}

	usleep_range(AV8100_WAITTIME_10MS, AV8100_WAITTIME_10MS_MAX);

	/* Prepare firmware data */
	fw_bytes = fw_file->size;
	fw_buff = (u8 *)fw_file->data;
	dev_dbg(adev->dev, "fw size:%d\n", fw_bytes);

	i2c = adev->config.client;

	/* Enable firmware download */
	retval = av8100_reg_gen_ctrl_w(
		AV8100_GENERAL_CONTROL_FDL_HIGH,
		AV8100_GENERAL_CONTROL_HLD_HIGH,
		AV8100_GENERAL_CONTROL_WA_LOW,
		AV8100_GENERAL_CONTROL_RA_LOW);
	if (retval) {
		dev_err(adev->dev,
			"Failed to write the value to av8100 register\n");
		retval = -EFAULT;
		goto av8100_download_firmware_err;
	}

	retval = av8100_reg_gen_ctrl_r(&fdl, &hld, &wa, &ra);
	if (retval) {
		dev_err(adev->dev,
			"Failed to read the value from av8100 register\n");
		retval = -EFAULT;
		goto av8100_download_firmware_err;
	} else {
		dev_dbg(adev->dev, "GENERAL_CONTROL_REG register fdl:%d hld:%d wa:%d ra:%d\n",
							fdl, hld, wa, ra);
	}

	LOCK_AV8100_HW;

	temp = fw_bytes % increment;
	for (size = 0; size < (fw_bytes-temp); size = size + increment,
		index += increment) {
		if (if_type == I2C_INTERFACE) {
			retval = write_multi_byte(i2c,
				AV8100_FIRMWARE_DOWNLOAD_ENTRY, fw_buff + size,
				increment);
			if (retval) {
				dev_dbg(adev->dev, "Failed to download the av8100 firmware\n");
				UNLOCK_AV8100_HW;
				retval = -EFAULT;
				goto av8100_download_firmware_err;
			}
		} else if (if_type == DSI_INTERFACE) {
			dev_dbg(adev->dev,
				"DSI_INTERFACE is currently not supported\n");
			UNLOCK_AV8100_HW;
			retval = -EINVAL;
			goto av8100_download_firmware_err;
		} else {
			UNLOCK_AV8100_HW;
			retval = -EINVAL;
			goto av8100_download_firmware_err;
		}
	}

	/* Transfer last firmware bytes */
	if (if_type == I2C_INTERFACE) {
		retval = write_multi_byte(i2c,
			AV8100_FIRMWARE_DOWNLOAD_ENTRY, fw_buff + size, temp);
		if (retval) {
			dev_dbg(adev->dev,
				"Failed to download the av8100 firmware\n");
			UNLOCK_AV8100_HW;
			retval = -EFAULT;
			goto av8100_download_firmware_err;
		}
	} else if (if_type == DSI_INTERFACE) {
		/* TODO: Add support for DSI firmware download */
		UNLOCK_AV8100_HW;
		retval = -EINVAL;
		goto av8100_download_firmware_err;
	} else {
		UNLOCK_AV8100_HW;
		retval = -EINVAL;
		goto av8100_download_firmware_err;
	}

	/* check transfer*/
	for (size = 0; size < fw_bytes; size++)
		checksum = checksum ^ fw_buff[size];

	UNLOCK_AV8100_HW;

	retval = av8100_reg_fw_dl_entry_r(&val);
	if (retval) {
		dev_dbg(adev->dev,
			"Failed to read the value from the av8100 register\n");
		retval = -EFAULT;
		goto av8100_download_firmware_err;
	}

	dev_dbg(adev->dev, "checksum:%x,val:%x\n", checksum, val);

	if (checksum != val) {
		dev_dbg(adev->dev,
			">Fw downloading.... FAIL checksum issue\n");
		dev_dbg(adev->dev, "checksum = %d\n", checksum);
		dev_dbg(adev->dev, "checksum read: %d\n", val);
		retval = -EFAULT;
		goto av8100_download_firmware_err;
	} else {
		dev_dbg(adev->dev, ">Fw downloading.... success\n");
	}

	/* Set to idle mode */
	av8100_reg_gen_ctrl_w(AV8100_GENERAL_CONTROL_FDL_LOW,
		AV8100_GENERAL_CONTROL_HLD_LOW,	AV8100_GENERAL_CONTROL_WA_LOW,
		AV8100_GENERAL_CONTROL_RA_LOW);
	if (retval) {
		dev_dbg(adev->dev,
			"Failed to write the value to the av8100 register\n");
		retval = -EFAULT;
		goto av8100_download_firmware_err;
	}

	/* Wait Internal Micro controler ready */
	cnt = 0;
	cnt_max = sizeof(waittime_retry) / sizeof(waittime_retry[0]);
	retval = av8100_reg_gen_status_r(NULL, NULL, NULL, &uc,
		NULL, NULL);
	while ((retval == 0) && (uc != 0x1) && (cnt < cnt_max)) {
		usleep_range(waittime_retry[cnt],
					waittime_retry[cnt] * 12 / 10);
		retval = av8100_reg_gen_status_r(NULL, NULL, NULL,
			&uc, NULL, NULL);
		cnt++;
	}
	dev_dbg(adev->dev, "av8100 fwdl cnt:%d\n", cnt);

	if (retval)	{
		dev_dbg(adev->dev,
			"Failed to read the value from the av8100 register\n");
		retval = -EFAULT;
		goto av8100_download_firmware_err;
	}

	if (uc != 0x1)
		dev_dbg(adev->dev, "UC is not ready\n");

	release_firmware(fw_file);

	if (adev->chip_version != 1) {
		char *cut_str;

		/* Get cut version */
		retval = read_single_byte(i2c, AV8100_CUTVER_OFFSET, &val);
		if (retval) {
			dev_err(adev->dev, "Read cut ver failed\n");
			return retval;
		}

		switch (val) {
		case 0x00:
			cut_str = CUT_STR_0;
			break;
		case 0x01:
			cut_str = CUT_STR_1;
			break;
		case 0x03:
			cut_str = CUT_STR_3;
			break;
		case 0x30:
			cut_str = CUT_STR_30;
			break;
		default:
			cut_str = CUT_STR_UNKNOWN;
			break;
		}
		dev_dbg(adev->dev, "Cut ver %02x %s\n", val, cut_str);
	}

	adev->params.fw_loaded = true;

	/* Unmask gen ints */
	if (av8100_reg_gen_int_mask_w(
			AV8100_GENERAL_INTERRUPT_MASK_EOCM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSIM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSOM_LOW,
			adev->params.cecm,
			adev->params.hdcpm,
			adev->params.uovbm,
			AV8100_GENERAL_INTERRUPT_MASK_TEM_LOW))
		dev_dbg(adev->dev, "av8100_reg_gen_int_mask_w err\n");

	av8100_set_state(adev, AV8100_OPMODE_IDLE);

av8100_download_firmware_end:
	UNLOCK_AV8100_FWDL;
	return 0;

av8100_download_firmware_err:
	release_firmware(fw_file);

av8100_download_firmware_err2:
	UNLOCK_AV8100_FWDL;

	if (!adev->params.suspended) {
		adev->flag |= AV8100_SCAN_TIMER_EVENT;
		wake_up_interruptible(&adev->event);
	}

	return retval;
}
EXPORT_SYMBOL(av8100_download_firmware);

int av8100_disable_interrupt(void)
{
	int retval;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	if (adev->params.fw_loaded) {
		retval = av8100_reg_gen_int_mask_w(
				AV8100_GENERAL_INTERRUPT_MASK_EOCM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_VSIM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_VSOM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_CECM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_HDCPM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_UOVBM_LOW,
				AV8100_GENERAL_INTERRUPT_MASK_TEM_LOW);
		if (retval) {
			dev_dbg(adev->dev,
					"av8100_reg_gen_int_mask_w err\n");
			return -EFAULT;
		}
	}

	retval = av8100_reg_stby_int_mask_w(
			AV8100_STANDBY_INTERRUPT_MASK_HPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CCM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
			AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW);
	if (retval) {
		dev_dbg(adev->dev,
			"Failed to write the value to av8100 register\n");
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(av8100_disable_interrupt);

int av8100_enable_interrupt(void)
{
	int retval;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	if (adev->params.fw_loaded) {
		retval = av8100_reg_gen_int_mask_w(
			AV8100_GENERAL_INTERRUPT_MASK_EOCM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSIM_LOW,
			AV8100_GENERAL_INTERRUPT_MASK_VSOM_LOW,
			adev->params.cecm,
			adev->params.hdcpm,
			adev->params.uovbm,
			AV8100_GENERAL_INTERRUPT_MASK_TEM_LOW);
		if (retval) {
			dev_dbg(adev->dev,
				"%s gen_int_mask_w failed\n", __func__);
			return -EFAULT;
		}
	}

	retval = av8100_reg_stby_int_mask_w(
			AV8100_STANDBY_INTERRUPT_MASK_HPDM_HIGH,
			AV8100_STANDBY_INTERRUPT_MASK_CPDM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_CCM_LOW,
			AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_INPUT,
			AV8100_STANDBY_INTERRUPT_MASK_IPOL_LOW);
	if (retval) {
		dev_dbg(adev->dev,
			"%s stby_int_mask_w failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(av8100_enable_interrupt);

int av8100_reg_stby_w(
		u8 cpd, u8 stby, u8 mclkrng)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_STANDBY_CPD(cpd) | AV8100_STANDBY_STBY(stby) |
		AV8100_STANDBY_MCLKRNG(mclkrng);

	/* Write to register */
	retval = register_write_internal(AV8100_STANDBY, val);
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_stby_w);

static int av8100_5V_w(u8 denc_off, u8 hdmi_off, u8 on)
{
	u8 val;
	int retval;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value.
	 * chip_version == 1 have one common off time
	 * chip_version > 1 support different off time for hdmi and tvout. */
	if (adev->chip_version == 1)
		val = AV8100_HDMI_5_VOLT_TIME_OFF_TIME(hdmi_off) |
			AV8100_HDMI_5_VOLT_TIME_ON_TIME(on);
	else
		val = AV8100_HDMI_5_VOLT_TIME_DAC_OFF_TIME(denc_off) |
			AV8100_HDMI_5_VOLT_TIME_SU_OFF_TIME(hdmi_off) |
			AV8100_HDMI_5_VOLT_TIME_ON_TIME(on);

	/* Write to register */
	retval = register_write_internal(AV8100_HDMI_5_VOLT_TIME, val);

	if (on)
		adev->params.pulsing_5V = (hdmi_off != 0);

	UNLOCK_AV8100_HW;

	return retval;
}

int av8100_reg_hdmi_5_volt_time_w(u8 denc_off, u8 hdmi_off, u8 on)
{
	int retval;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	retval = av8100_5V_w(AV8100_DENC_OFF_TIME, hdmi_off, on);

	/* Set vars */
	adev->params.hdmi_off_time = hdmi_off;
	if (on)
		adev->params.on_time = on;

	return retval;
}
EXPORT_SYMBOL(av8100_reg_hdmi_5_volt_time_w);

int av8100_reg_stby_int_mask_w(
		u8 hpdm, u8 cpdm, u8 ccm, u8 stbygpiocfg, u8 ipol)
{
	int retval;
	u8 val;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_STANDBY_INTERRUPT_MASK_HPDM(hpdm) |
		AV8100_STANDBY_INTERRUPT_MASK_CPDM(cpdm) |
		AV8100_STANDBY_INTERRUPT_MASK_CCM(ccm) |
		AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG(stbygpiocfg) |
		AV8100_STANDBY_INTERRUPT_MASK_IPOL(ipol);

	/* Write to register */
	retval = register_write_internal(AV8100_STANDBY_INTERRUPT_MASK, val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_stby_int_mask_w);

int av8100_reg_stby_pend_int_w(
		u8 hpdi, u8 cpdi, u8 oni, u8 cci, u8 ccrst, u8 bpdig)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_STANDBY_PENDING_INTERRUPT_HPDI(hpdi) |
		AV8100_STANDBY_PENDING_INTERRUPT_CPDI(cpdi) |
		AV8100_STANDBY_PENDING_INTERRUPT_ONI(oni) |
		AV8100_STANDBY_PENDING_INTERRUPT_CCI(cci) |
		AV8100_STANDBY_PENDING_INTERRUPT_CCRST(ccrst) |
		AV8100_STANDBY_PENDING_INTERRUPT_BPDIG(bpdig);

	/* Write to register */
	retval = register_write_internal(AV8100_STANDBY_PENDING_INTERRUPT, val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_stby_pend_int_w);

int av8100_reg_gen_int_mask_w(
		u8 eocm, u8 vsim, u8 vsom, u8 cecm, u8 hdcpm, u8 uovbm, u8 tem)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_GENERAL_INTERRUPT_MASK_EOCM(eocm) |
		AV8100_GENERAL_INTERRUPT_MASK_VSIM(vsim) |
		AV8100_GENERAL_INTERRUPT_MASK_VSOM(vsom) |
		AV8100_GENERAL_INTERRUPT_MASK_CECM(cecm) |
		AV8100_GENERAL_INTERRUPT_MASK_HDCPM(hdcpm) |
		AV8100_GENERAL_INTERRUPT_MASK_UOVBM(uovbm) |
		AV8100_GENERAL_INTERRUPT_MASK_TEM(tem);

	/* Write to register */
	retval = register_write_internal(AV8100_GENERAL_INTERRUPT_MASK,	val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_int_mask_w);

int av8100_reg_gen_int_w(
		u8 eoci, u8 vsii, u8 vsoi, u8 ceci, u8 hdcpi, u8 uovbi)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_GENERAL_INTERRUPT_EOCI(eoci) |
		AV8100_GENERAL_INTERRUPT_VSII(vsii) |
		AV8100_GENERAL_INTERRUPT_VSOI(vsoi) |
		AV8100_GENERAL_INTERRUPT_CECI(ceci) |
		AV8100_GENERAL_INTERRUPT_HDCPI(hdcpi) |
		AV8100_GENERAL_INTERRUPT_UOVBI(uovbi);

	/* Write to register */
	retval = register_write_internal(AV8100_GENERAL_INTERRUPT, val);
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_int_w);

int av8100_reg_gpio_conf_w(
		u8 dat3dir, u8 dat3val, u8 dat2dir, u8 dat2val,	u8 dat1dir,
		u8 dat1val, u8 ucdbg)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_GPIO_CONFIGURATION_DAT3DIR(dat3dir) |
		AV8100_GPIO_CONFIGURATION_DAT3VAL(dat3val) |
		AV8100_GPIO_CONFIGURATION_DAT2DIR(dat2dir) |
		AV8100_GPIO_CONFIGURATION_DAT2VAL(dat2val) |
		AV8100_GPIO_CONFIGURATION_DAT1DIR(dat1dir) |
		AV8100_GPIO_CONFIGURATION_DAT1VAL(dat1val) |
		AV8100_GPIO_CONFIGURATION_UCDBG(ucdbg);

	/* Write to register */
	retval = register_write_internal(AV8100_GPIO_CONFIGURATION, val);
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gpio_conf_w);

int av8100_reg_gen_ctrl_w(
		u8 fdl, u8 hld, u8 wa, u8 ra)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_GENERAL_CONTROL_FDL(fdl) |
		AV8100_GENERAL_CONTROL_HLD(hld) |
		AV8100_GENERAL_CONTROL_WA(wa) |
		AV8100_GENERAL_CONTROL_RA(ra);

	/* Write to register */
	retval = register_write_internal(AV8100_GENERAL_CONTROL, val);
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_ctrl_w);

int av8100_reg_fw_dl_entry_w(
	u8 mbyte_code_entry)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Set register value */
	val = AV8100_FIRMWARE_DOWNLOAD_ENTRY_MBYTE_CODE_ENTRY(
		mbyte_code_entry);

	/* Write to register */
	retval = register_write_internal(AV8100_FIRMWARE_DOWNLOAD_ENTRY, val);
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_fw_dl_entry_w);

int av8100_reg_w(
		u8 offset, u8 value)
{
	int retval = 0;
	struct i2c_client *i2c;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	i2c = adev->config.client;

	/* Write to register */
	retval = write_single_byte(i2c, offset, value);
	if (retval) {
		dev_dbg(adev->dev,
			"Failed to write the value to av8100 register\n");
		UNLOCK_AV8100_HW;
		return -EFAULT;
	}

	UNLOCK_AV8100_HW;
	return 0;
}
EXPORT_SYMBOL(av8100_reg_w);

int av8100_reg_stby_r(
		u8 *cpd, u8 *stby, u8 *hpds, u8 *cpds, u8 *mclkrng)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_STANDBY, &val);

	/* Set return params */
	if (cpd)
		*cpd = AV8100_STANDBY_CPD_GET(val);
	if (stby)
		*stby = AV8100_STANDBY_STBY_GET(val);
	if (hpds)
		*hpds = AV8100_STANDBY_HPDS_GET(val);
	if (cpds)
		*cpds = AV8100_STANDBY_CPDS_GET(val);
	if (mclkrng)
		*mclkrng = AV8100_STANDBY_MCLKRNG_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_stby_r);

int av8100_reg_hdmi_5_volt_time_r(
		u8 *denc_off_time, u8 *hdmi_off_time, u8 *on_time)
{
	int retval;
	u8 val;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_HDMI_5_VOLT_TIME, &val);

	/* Set return params */
	if (adev->chip_version == 1) {
		if (denc_off_time)
			*denc_off_time = 0;
		if (hdmi_off_time)
			*hdmi_off_time =
				AV8100_HDMI_5_VOLT_TIME_OFF_TIME_GET(val);
	} else {
		if (denc_off_time)
			*denc_off_time =
				AV8100_HDMI_5_VOLT_TIME_DAC_OFF_TIME_GET(val);
		if (hdmi_off_time)
			*hdmi_off_time =
				AV8100_HDMI_5_VOLT_TIME_SU_OFF_TIME_GET(val);
	}

	if (on_time)
		*on_time = AV8100_HDMI_5_VOLT_TIME_ON_TIME_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_hdmi_5_volt_time_r);

int av8100_reg_stby_int_mask_r(
		u8 *hpdm, u8 *cpdm, u8 *stbygpiocfg, u8 *ipol)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_STANDBY_INTERRUPT_MASK, &val);

	/* Set return params */
	if (hpdm)
		*hpdm = AV8100_STANDBY_INTERRUPT_MASK_HPDM_GET(val);
	if (cpdm)
		*cpdm = AV8100_STANDBY_INTERRUPT_MASK_CPDM_GET(val);
	if (stbygpiocfg)
		*stbygpiocfg =
			AV8100_STANDBY_INTERRUPT_MASK_STBYGPIOCFG_GET(val);
	if (ipol)
		*ipol = AV8100_STANDBY_INTERRUPT_MASK_IPOL_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_stby_int_mask_r);

int av8100_reg_stby_pend_int_r(
		u8 *hpdi, u8 *cpdi, u8 *oni, u8 *cci, u8 *sid)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_STANDBY_PENDING_INTERRUPT,
			&val);

	/* Set return params */
	if (hpdi)
		*hpdi = AV8100_STANDBY_PENDING_INTERRUPT_HPDI_GET(val);
	if (cpdi)
		*cpdi = AV8100_STANDBY_PENDING_INTERRUPT_CPDI_GET(val);
	if (oni)
		*oni = AV8100_STANDBY_PENDING_INTERRUPT_ONI_GET(val);
	if (cci)
		*cci = AV8100_STANDBY_PENDING_INTERRUPT_CCI_GET(val);
	if (sid)
		*sid = AV8100_STANDBY_PENDING_INTERRUPT_SID_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_stby_pend_int_r);

int av8100_reg_gen_int_mask_r(
		u8 *eocm,
		u8 *vsim,
		u8 *vsom,
		u8 *cecm,
		u8 *hdcpm,
		u8 *uovbm,
		u8 *tem)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_GENERAL_INTERRUPT_MASK, &val);

	/* Set return params */
	if (eocm)
		*eocm = AV8100_GENERAL_INTERRUPT_MASK_EOCM_GET(val);
	if (vsim)
		*vsim = AV8100_GENERAL_INTERRUPT_MASK_VSIM_GET(val);
	if (vsom)
		*vsom = AV8100_GENERAL_INTERRUPT_MASK_VSOM_GET(val);
	if (cecm)
		*cecm = AV8100_GENERAL_INTERRUPT_MASK_CECM_GET(val);
	if (hdcpm)
		*hdcpm = AV8100_GENERAL_INTERRUPT_MASK_HDCPM_GET(val);
	if (uovbm)
		*uovbm = AV8100_GENERAL_INTERRUPT_MASK_UOVBM_GET(val);
	if (tem)
		*tem = AV8100_GENERAL_INTERRUPT_MASK_TEM_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_int_mask_r);

int av8100_reg_gen_int_r(
		u8 *eoci,
		u8 *vsii,
		u8 *vsoi,
		u8 *ceci,
		u8 *hdcpi,
		u8 *uovbi,
		u8 *tei)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_GENERAL_INTERRUPT, &val);

	/* Set return params */
	if (eoci)
		*eoci = AV8100_GENERAL_INTERRUPT_EOCI_GET(val);
	if (vsii)
		*vsii = AV8100_GENERAL_INTERRUPT_VSII_GET(val);
	if (vsoi)
		*vsoi = AV8100_GENERAL_INTERRUPT_VSOI_GET(val);
	if (ceci)
		*ceci = AV8100_GENERAL_INTERRUPT_CECI_GET(val);
	if (hdcpi)
		*hdcpi = AV8100_GENERAL_INTERRUPT_HDCPI_GET(val);
	if (uovbi)
		*uovbi = AV8100_GENERAL_INTERRUPT_UOVBI_GET(val);
	if (tei)
		*tei = AV8100_GENERAL_INTERRUPT_TEI_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_int_r);

int av8100_reg_gen_status_r(
		u8 *cectxerr,
		u8 *cecrec,
		u8 *cectrx,
		u8 *uc,
		u8 *onuvb,
		u8 *hdcps)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_GENERAL_STATUS, &val);

	/* Set return params */
	if (cectxerr)
		*cectxerr = AV8100_GENERAL_STATUS_CECTXERR_GET(val);
	if (cecrec)
		*cecrec	= AV8100_GENERAL_STATUS_CECREC_GET(val);
	if (cectrx)
		*cectrx	= AV8100_GENERAL_STATUS_CECTRX_GET(val);
	if (uc)
		*uc = AV8100_GENERAL_STATUS_UC_GET(val);
	if (onuvb)
		*onuvb = AV8100_GENERAL_STATUS_ONUVB_GET(val);
	if (hdcps)
		*hdcps = AV8100_GENERAL_STATUS_HDCPS_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_status_r);

int av8100_reg_gpio_conf_r(
		u8 *dat3dir,
		u8 *dat3val,
		u8 *dat2dir,
		u8 *dat2val,
		u8 *dat1dir,
		u8 *dat1val,
		u8 *ucdbg)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_GPIO_CONFIGURATION, &val);

	/* Set return params */
	if (dat3dir)
		*dat3dir = AV8100_GPIO_CONFIGURATION_DAT3DIR_GET(val);
	if (dat3val)
		*dat3val = AV8100_GPIO_CONFIGURATION_DAT3VAL_GET(val);
	if (dat2dir)
		*dat2dir = AV8100_GPIO_CONFIGURATION_DAT2DIR_GET(val);
	if (dat2val)
		*dat2val = AV8100_GPIO_CONFIGURATION_DAT2VAL_GET(val);
	if (dat1dir)
		*dat1dir = AV8100_GPIO_CONFIGURATION_DAT1DIR_GET(val);
	if (dat1val)
		*dat1val = AV8100_GPIO_CONFIGURATION_DAT1VAL_GET(val);
	if (ucdbg)
		*ucdbg = AV8100_GPIO_CONFIGURATION_UCDBG_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gpio_conf_r);

int av8100_reg_gen_ctrl_r(
		u8 *fdl,
		u8 *hld,
		u8 *wa,
		u8 *ra)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_GENERAL_CONTROL, &val);
	/* Set return params */
	if (fdl)
		*fdl = AV8100_GENERAL_CONTROL_FDL_GET(val);
	if (hld)
		*hld = AV8100_GENERAL_CONTROL_HLD_GET(val);
	if (wa)
		*wa = AV8100_GENERAL_CONTROL_WA_GET(val);
	if (ra)
		*ra = AV8100_GENERAL_CONTROL_RA_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_gen_ctrl_r);

int av8100_reg_fw_dl_entry_r(
	u8 *mbyte_code_entry)
{
	int retval;
	u8 val;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	/* Read from register */
	retval = register_read_internal(AV8100_FIRMWARE_DOWNLOAD_ENTRY,	&val);

	/* Set return params */
	if (mbyte_code_entry)
		*mbyte_code_entry =
		AV8100_FIRMWARE_DOWNLOAD_ENTRY_MBYTE_CODE_ENTRY_GET(val);

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_fw_dl_entry_r);

int av8100_reg_r(
		u8 offset,
		u8 *value)
{
	int retval = 0;
	struct i2c_client *i2c;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	i2c = adev->config.client;

	/* Read from register */
	retval = read_single_byte(i2c, offset, value);
	if (retval)	{
		dev_dbg(adev->dev,
			"Failed to read the value from av8100 register\n");
		retval = -EFAULT;
		goto av8100_register_read_out;
	}

av8100_register_read_out:
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_reg_r);

int av8100_conf_get(enum av8100_command_type command_type,
	union av8100_configuration *config)
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state == AV8100_OPMODE_UNDEFINED)
		return -EINVAL;

	/* Put configuration data to the corresponding data struct depending
	 * on command type */
	switch (command_type) {
	case AV8100_COMMAND_VIDEO_INPUT_FORMAT:
		memcpy(&config->video_input_format,
			&adev->config.hdmi_video_input_cmd,
			sizeof(struct av8100_video_input_format_cmd));
		break;

	case AV8100_COMMAND_AUDIO_INPUT_FORMAT:
		memcpy(&config->audio_input_format,
			&adev->config.hdmi_audio_input_cmd,
			sizeof(struct av8100_audio_input_format_cmd));
		break;

	case AV8100_COMMAND_VIDEO_OUTPUT_FORMAT:
		memcpy(&config->video_output_format,
			&adev->config.hdmi_video_output_cmd,
			sizeof(struct av8100_video_output_format_cmd));
		break;

	case AV8100_COMMAND_VIDEO_SCALING_FORMAT:
		memcpy(&config->video_scaling_format,
			&adev->config.hdmi_video_scaling_cmd,
			sizeof(struct av8100_video_scaling_format_cmd));
		break;

	case AV8100_COMMAND_COLORSPACECONVERSION:
		config->color_transform = adev->config.color_transform;
		break;

	case AV8100_COMMAND_CEC_MESSAGE_WRITE:
		memcpy(&config->cec_message_write_format,
			&adev->config.hdmi_cec_message_write_cmd,
			sizeof(struct av8100_cec_message_write_format_cmd));
		break;

	case AV8100_COMMAND_CEC_MESSAGE_READ_BACK:
		memcpy(&config->cec_message_read_back_format,
			&adev->config.hdmi_cec_message_read_back_cmd,
			sizeof(struct av8100_cec_message_read_back_format_cmd));
		break;

	case AV8100_COMMAND_DENC:
		memcpy(&config->denc_format, &adev->config.hdmi_denc_cmd,
				sizeof(struct av8100_denc_format_cmd));
		break;

	case AV8100_COMMAND_HDMI:
		memcpy(&config->hdmi_format, &adev->config.hdmi_cmd,
				sizeof(struct av8100_hdmi_cmd));
		break;

	case AV8100_COMMAND_HDCP_SENDKEY:
		memcpy(&config->hdcp_send_key_format,
			&adev->config.hdmi_hdcp_send_key_cmd,
			sizeof(struct av8100_hdcp_send_key_format_cmd));
		break;

	case AV8100_COMMAND_HDCP_MANAGEMENT:
		memcpy(&config->hdcp_management_format,
			&adev->config.hdmi_hdcp_management_format_cmd,
			sizeof(struct av8100_hdcp_management_format_cmd));
		break;

	case AV8100_COMMAND_INFOFRAMES:
		memcpy(&config->infoframes_format,
			&adev->config.hdmi_infoframes_cmd,
			sizeof(struct av8100_infoframes_format_cmd));
		break;

	case AV8100_COMMAND_EDID_SECTION_READBACK:
		memcpy(&config->edid_section_readback_format,
			&adev->config.hdmi_edid_section_readback_cmd,
			sizeof(struct
				av8100_edid_section_readback_format_cmd));
		break;

	case AV8100_COMMAND_PATTERNGENERATOR:
		memcpy(&config->pattern_generator_format,
			&adev->config.hdmi_pattern_generator_cmd,
			sizeof(struct av8100_pattern_generator_format_cmd));
		break;

	case AV8100_COMMAND_FUSE_AES_KEY:
		memcpy(&config->fuse_aes_key_format,
			&adev->config.hdmi_fuse_aes_key_cmd,
			sizeof(struct av8100_fuse_aes_key_format_cmd));
		break;

	default:
		return -EINVAL;
		break;
	}

	return 0;
}
EXPORT_SYMBOL(av8100_conf_get);

int av8100_conf_prep(enum av8100_command_type command_type,
	union av8100_configuration *config)
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!config || !adev)
		return -EINVAL;

	/* Put configuration data to the corresponding data struct depending
	 * on command type */
	switch (command_type) {
	case AV8100_COMMAND_VIDEO_INPUT_FORMAT:
		memcpy(&adev->config.hdmi_video_input_cmd,
			&config->video_input_format,
			sizeof(struct av8100_video_input_format_cmd));
		break;

	case AV8100_COMMAND_AUDIO_INPUT_FORMAT:
		memcpy(&adev->config.hdmi_audio_input_cmd,
			&config->audio_input_format,
			sizeof(struct av8100_audio_input_format_cmd));
		break;

	case AV8100_COMMAND_VIDEO_OUTPUT_FORMAT:
		memcpy(&adev->config.hdmi_video_output_cmd,
			&config->video_output_format,
			sizeof(struct av8100_video_output_format_cmd));

		/* Set params that depend on video output */
		av8100_config_video_output_dep(adev->config.
			hdmi_video_output_cmd.video_output_cea_vesa);
		break;

	case AV8100_COMMAND_VIDEO_SCALING_FORMAT:
		memcpy(&adev->config.hdmi_video_scaling_cmd,
			&config->video_scaling_format,
			sizeof(struct av8100_video_scaling_format_cmd));
		break;

	case AV8100_COMMAND_COLORSPACECONVERSION:
		adev->config.color_transform = config->color_transform;
		break;

	case AV8100_COMMAND_CEC_MESSAGE_WRITE:
		memcpy(&adev->config.hdmi_cec_message_write_cmd,
			&config->cec_message_write_format,
			sizeof(struct av8100_cec_message_write_format_cmd));
		break;

	case AV8100_COMMAND_CEC_MESSAGE_READ_BACK:
		memcpy(&adev->config.hdmi_cec_message_read_back_cmd,
			&config->cec_message_read_back_format,
			sizeof(struct av8100_cec_message_read_back_format_cmd));
		break;

	case AV8100_COMMAND_DENC:
		memcpy(&adev->config.hdmi_denc_cmd, &config->denc_format,
				sizeof(struct av8100_denc_format_cmd));
		break;

	case AV8100_COMMAND_HDMI:
		memcpy(&adev->config.hdmi_cmd, &config->hdmi_format,
				sizeof(struct av8100_hdmi_cmd));
		break;

	case AV8100_COMMAND_HDCP_SENDKEY:
		memcpy(&adev->config.hdmi_hdcp_send_key_cmd,
			&config->hdcp_send_key_format,
			sizeof(struct av8100_hdcp_send_key_format_cmd));
		break;

	case AV8100_COMMAND_HDCP_MANAGEMENT:
		memcpy(&adev->config.hdmi_hdcp_management_format_cmd,
			&config->hdcp_management_format,
			sizeof(struct av8100_hdcp_management_format_cmd));
		break;

	case AV8100_COMMAND_INFOFRAMES:
		memcpy(&adev->config.hdmi_infoframes_cmd,
			&config->infoframes_format,
			sizeof(struct av8100_infoframes_format_cmd));
		/* If Audio Infoframe: save for later usage */
		if (adev->config.hdmi_infoframes_cmd.type ==
						INFOFRAME_TYPE_AUDIO) {
			memcpy(&adev->config.hdmi_audio_infoframe,
				&config->infoframes_format,
				sizeof(struct av8100_infoframes_format_cmd));
		}
		break;

	case AV8100_COMMAND_EDID_SECTION_READBACK:
		memcpy(&adev->config.hdmi_edid_section_readback_cmd,
			&config->edid_section_readback_format,
			sizeof(struct
				av8100_edid_section_readback_format_cmd));
		break;

	case AV8100_COMMAND_PATTERNGENERATOR:
		memcpy(&adev->config.hdmi_pattern_generator_cmd,
			&config->pattern_generator_format,
			sizeof(struct av8100_pattern_generator_format_cmd));
		break;

	case AV8100_COMMAND_FUSE_AES_KEY:
		memcpy(&adev->config.hdmi_fuse_aes_key_cmd,
			&config->fuse_aes_key_format,
			sizeof(struct av8100_fuse_aes_key_format_cmd));
		break;

	default:
		return -EINVAL;
		break;
	}

	return 0;
}
EXPORT_SYMBOL(av8100_conf_prep);

int av8100_conf_w(enum av8100_command_type command_type,
	u8 *return_buffer_length,
	u8 *return_buffer, enum interface_type if_type)
{
	int retval = 0;
	u8 cmd_buffer[AV8100_COMMAND_MAX_LENGTH];
	u32 cmd_length = 0;
	struct i2c_client *i2c;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	if (return_buffer_length)
		*return_buffer_length = 0;

	i2c = adev->config.client;

	memset(&cmd_buffer, 0x00, AV8100_COMMAND_MAX_LENGTH);

#define PRNK_MODE(_m) dev_dbg(adev->dev, "cmd: " #_m "\n");

	/* Fill the command buffer with configuration data */
	switch (command_type) {
	case AV8100_COMMAND_VIDEO_INPUT_FORMAT:
		PRNK_MODE(AV8100_COMMAND_VIDEO_INPUT_FORMAT);
		configuration_video_input_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_AUDIO_INPUT_FORMAT:
		PRNK_MODE(AV8100_COMMAND_AUDIO_INPUT_FORMAT);
		configuration_audio_input_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_VIDEO_OUTPUT_FORMAT:
		PRNK_MODE(AV8100_COMMAND_VIDEO_OUTPUT_FORMAT);
		configuration_video_output_get(adev, cmd_buffer, &cmd_length);
		av8100_set_state(adev, AV8100_OPMODE_VIDEO);
		break;

	case AV8100_COMMAND_VIDEO_SCALING_FORMAT:
		PRNK_MODE(AV8100_COMMAND_VIDEO_SCALING_FORMAT);
		configuration_video_scaling_get(adev, cmd_buffer,
			&cmd_length);
		break;

	case AV8100_COMMAND_COLORSPACECONVERSION:
		PRNK_MODE(AV8100_COMMAND_COLORSPACECONVERSION);
		configuration_colorspace_conversion_get(adev, cmd_buffer,
			&cmd_length);
		break;

	case AV8100_COMMAND_CEC_MESSAGE_WRITE:
		PRNK_MODE(AV8100_COMMAND_CEC_MESSAGE_WRITE);
		configuration_cec_message_write_get(adev, cmd_buffer,
			&cmd_length);
		break;

	case AV8100_COMMAND_CEC_MESSAGE_READ_BACK:
		PRNK_MODE(AV8100_COMMAND_CEC_MESSAGE_READ_BACK);
		configuration_cec_message_read_get(cmd_buffer,
			&cmd_length);
		break;

	case AV8100_COMMAND_DENC:
		PRNK_MODE(AV8100_COMMAND_DENC);
		configuration_denc_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_HDMI:
		PRNK_MODE(AV8100_COMMAND_HDMI);
		configuration_hdmi_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_HDCP_SENDKEY:
		PRNK_MODE(AV8100_COMMAND_HDCP_SENDKEY);
		configuration_hdcp_sendkey_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_HDCP_MANAGEMENT:
		PRNK_MODE(AV8100_COMMAND_HDCP_MANAGEMENT);
		configuration_hdcp_management_get(adev, cmd_buffer,
			&cmd_length);
		break;

	case AV8100_COMMAND_INFOFRAMES:
		PRNK_MODE(AV8100_COMMAND_INFOFRAMES);
		configuration_infoframe_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_EDID_SECTION_READBACK:
		PRNK_MODE(AV8100_COMMAND_EDID_SECTION_READBACK);
		av8100_edid_section_readback_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_PATTERNGENERATOR:
		PRNK_MODE(AV8100_COMMAND_PATTERNGENERATOR);
		configuration_pattern_generator_get(adev, cmd_buffer,
			&cmd_length);
		break;

	case AV8100_COMMAND_FUSE_AES_KEY:
		PRNK_MODE(AV8100_COMMAND_FUSE_AES_KEY);
		configuration_fuse_aes_key_get(adev, cmd_buffer, &cmd_length);
		break;

	case AV8100_COMMAND_AUDIO_INFOFRAME:
		PRNK_MODE(AV8100_COMMAND_AUDIO_INFOFRAME);
		command_type = AV8100_COMMAND_INFOFRAMES;
		if (configuration_audio_infoframe_get(adev, cmd_buffer,
								&cmd_length))
			retval = -EINVAL;
		break;

	default:
		dev_dbg(adev->dev, "Invalid command type\n");
		retval = -EFAULT;
		break;
	}

	if (retval)
		return retval;

	LOCK_AV8100_HW;

	if (if_type == I2C_INTERFACE) {
		int cnt = 0;
		int cnt_max;

		dev_dbg(adev->dev, "av8100_conf_w cmd_type:%02x length:%02x ",
			command_type, cmd_length);
		dev_dbg(adev->dev, "buffer: ");
		while (cnt < cmd_length) {
			dev_dbg(adev->dev, "%02x ", cmd_buffer[cnt]);
			cnt++;
		}

		/* Write the command buffer */
		retval = write_multi_byte(i2c,
			AV8100_CMD_BUF_OFFSET, cmd_buffer, cmd_length);
		if (retval) {
			UNLOCK_AV8100_HW;
			return retval;
		}

		/* Write the command */
		retval = write_single_byte(i2c, AV8100_COMMAND_OFFSET,
			command_type);
		if (retval) {
			UNLOCK_AV8100_HW;
			return retval;
		}


		/* Get the first return byte */
		usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);
		cnt = 0;
		cnt_max = sizeof(waittime_retry) / sizeof(waittime_retry[0]);
		retval = get_command_return_first(i2c, command_type);
		while (retval && (cnt < cnt_max)) {
			usleep_range(waittime_retry[cnt],
						waittime_retry[cnt] * 12 / 10);
			retval = get_command_return_first(i2c, command_type);
			cnt++;
		}
		dev_dbg(adev->dev, "first return cnt:%d\n", cnt);
		if (cnt == cnt_max) {
			adev->flag |= AV8100_CHECK_HW_STATUS_EVENT;
			wake_up_interruptible(&adev->event);
		}

		if (retval) {
			UNLOCK_AV8100_HW;
			return retval;
		}

		retval = get_command_return_data(i2c, command_type, cmd_buffer,
			return_buffer_length, return_buffer);
	} else if (if_type == DSI_INTERFACE) {
		/* TODO */
	} else {
		retval = -EINVAL;
		dev_dbg(adev->dev, "Invalid command type\n");
	}

	switch (command_type) {
	case AV8100_COMMAND_HDMI:
		adev->status.hdmi_on = ((adev->config.hdmi_cmd.
			hdmi_mode == AV8100_HDMI_ON) &&
			(adev->config.hdmi_cmd.hdmi_format == AV8100_HDMI));
		break;
	case AV8100_COMMAND_CEC_MESSAGE_WRITE:
		/* Allow suspend */
		adev->params.busy = false;
		break;
	default:
		/* Do nothing */
		break;
	}

	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_conf_w);

int av8100_conf_w_raw(enum av8100_command_type command_type,
	u8 buffer_length,
	u8 *buffer,
	u8 *return_buffer_length,
	u8 *return_buffer)
{
	int retval = 0;
	struct i2c_client *i2c;
	int cnt;
	int cnt_max;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	if (av8100_status_get().av8100_state <= AV8100_OPMODE_SHUTDOWN)
		return -EINVAL;

	LOCK_AV8100_HW;

	if (return_buffer_length)
		*return_buffer_length = 0;

	i2c = adev->config.client;

	/* Write the command buffer */
	retval = write_multi_byte(i2c,
		AV8100_CMD_BUF_OFFSET, buffer, buffer_length);
	if (retval)
		goto av8100_conf_w_raw_out;

	/* Write the command */
	retval = write_single_byte(i2c, AV8100_COMMAND_OFFSET,
		command_type);
	if (retval)
		goto av8100_conf_w_raw_out;


	/* Get the first return byte */
	usleep_range(AV8100_WAITTIME_1MS, AV8100_WAITTIME_1MS_MAX);
	cnt = 0;
	cnt_max = sizeof(waittime_retry) / sizeof(waittime_retry[0]);
	retval = get_command_return_first(i2c, command_type);
	while (retval && (cnt < cnt_max)) {
		usleep_range(waittime_retry[cnt],
					waittime_retry[cnt] * 12 / 10);
		retval = get_command_return_first(i2c, command_type);
		cnt++;
	}
	dev_dbg(adev->dev, "first return cnt:%d\n", cnt);
	if (cnt == cnt_max) {
		adev->flag |= AV8100_CHECK_HW_STATUS_EVENT;
		wake_up_interruptible(&adev->event);
	}
	if (retval)
		goto av8100_conf_w_raw_out;

	retval = get_command_return_data(i2c, command_type, buffer,
		return_buffer_length, return_buffer);

av8100_conf_w_raw_out:
	UNLOCK_AV8100_HW;
	return retval;
}
EXPORT_SYMBOL(av8100_conf_w_raw);

struct av8100_status av8100_status_get(void)
{
	struct av8100_status status = {0};
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (adev)
		return adev->status;
	else
		return status;
}
EXPORT_SYMBOL(av8100_status_get);

enum av8100_output_CEA_VESA av8100_video_output_format_get(int xres,
	int yres,
	int htot,
	int vtot,
	int pixelclk,
	bool interlaced)
{
	enum av8100_output_CEA_VESA index = 1;
	int yres_div = !interlaced ? 1 : 2;
	int hres_div = 1;
	long freq1;
	long freq2;
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	/*
	* 720_576_I need a divider for hact and htot since
	* these params need to be twice as large as expected in av8100_all_cea,
	* which is used as input parameter to video input config.
	*/
	if ((xres == 720) && (yres == 576) && (interlaced == true))
		hres_div = 2;

	freq1 = 1000000 / htot * 1000000 / vtot / pixelclk + 1;
	while (index < sizeof(av8100_all_cea)/sizeof(struct av8100_cea)) {
		freq2 = av8100_all_cea[index].frequence /
			av8100_all_cea[index].htotale /
			av8100_all_cea[index].vtotale;

		dev_dbg(adev->dev, "freq1:%ld freq2:%ld\n", freq1, freq2);
		if ((xres == av8100_all_cea[index].hactive / hres_div) &&
			(yres == av8100_all_cea[index].vactive * yres_div) &&
			(htot == av8100_all_cea[index].htotale / hres_div) &&
			(vtot == av8100_all_cea[index].vtotale) &&
			(abs(freq1 - freq2) < 2)) {
			goto av8100_video_output_format_get_out;
		}
		index++;
	}

av8100_video_output_format_get_out:
	dev_dbg(adev->dev, "av8100_video_output_format_get %d %d %d %d %d\n",
		xres, yres, htot, vtot, index);
	return index;
}
EXPORT_SYMBOL(av8100_video_output_format_get);

void av8100_hdmi_event_cb_set(void (*hdmi_ev_cb)(enum av8100_hdmi_event))
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (adev)
		adev->params.hdmi_ev_cb = hdmi_ev_cb;
}
EXPORT_SYMBOL(av8100_hdmi_event_cb_set);

u8 av8100_ver_get(void)
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return -EINVAL;

	return adev->chip_version;
}
EXPORT_SYMBOL(av8100_ver_get);

bool av8100_encryption_ongoing(void)
{
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev)
		return false;

	return adev->params.hdcp_state == HDCP_STATE_ENCRYPTION_ONGOING;
}
EXPORT_SYMBOL(av8100_encryption_ongoing);

void av8100_video_mode_changed(void)
{
	/*
	 * Resolution has been changed. If audio is a user, Audio Infoframe
	 * shall be reconfigured, since HDMI has been off when changing
	 * video_mode.
	 */
	struct av8100_device *adev;

	adev = devnr_to_adev(AV8100_DEVNR_DEFAULT);
	if (!adev || !adev->usr_audio)
		return;

	/* Reconfigure Audio Infoframe with latest config data */
	if (av8100_conf_w(AV8100_COMMAND_AUDIO_INFOFRAME, NULL, NULL,
							I2C_INTERFACE))
		dev_info(adev->dev, "Failed to send Infoframe\n");
}
EXPORT_SYMBOL(av8100_video_mode_changed);

static const struct color_conversion_cmd *get_color_transform_cmd(
			struct av8100_device *adev,
			enum av8100_color_transform transform)
{
	const struct color_conversion_cmd *result;

	switch (transform) {
	case AV8100_COLOR_TRANSFORM_INDENTITY:
		result = &col_trans_identity;
		break;
	case AV8100_COLOR_TRANSFORM_INDENTITY_CLAMP_YUV:
		result = &col_trans_identity_clamp_yuv;
		break;
	case AV8100_COLOR_TRANSFORM_YUV_TO_RGB:
		if (adev->chip_version == AV8100_CHIPVER_1)
			result = &col_trans_yuv_to_rgb_v1;
		else
			result = &col_trans_yuv_to_rgb_v2;
		break;
	case AV8100_COLOR_TRANSFORM_YUV_TO_DENC:
		result = &col_trans_yuv_to_denc;
		break;
	case AV8100_COLOR_TRANSFORM_RGB_TO_DENC:
		result = &col_trans_rgb_to_denc;
		break;
	default:
		dev_warn(adev->dev, "Unknown color space transform\n");
		result = &col_trans_identity;
		break;
	}
	return result;
}

static void early_suspend(struct early_suspend *data)
{
	struct av8100_device *adev =
		container_of(data, struct av8100_device, early_suspend);

	if (!adev->params.disp_on)
		(void)av8100_powerscan(false);
	adev->params.suspended = true;
}

static void late_resume(struct early_suspend *data)
{
	struct av8100_device *adev =
		container_of(data, struct av8100_device, early_suspend);

	adev->params.suspended = false;
	if (!adev->params.disp_on) {
		adev->flag &= ~AV8100_EVENTS;
		hrtimer_cancel(&adev->hrtimer);
		(void)av8100_powerwakeup(false);
	}
}

static int av8100_open(struct inode *inode, struct file *filp)
{
	pr_debug("%s\n", __func__);
	return 0;
}

static int av8100_release(struct inode *inode, struct file *filp)
{
	pr_debug("%s\n", __func__);
	return 0;
}

static long av8100_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	return 0;
}

int av8100_device_register(struct av8100_device *adev)
{
	adev->miscdev.minor = MISC_DYNAMIC_MINOR;
	adev->miscdev.name = "av8100";
	adev->miscdev.fops = &av8100_fops;

	if (misc_register(&adev->miscdev)) {
		pr_err("av8100 misc_register failed\n");
		return -EFAULT;
	}
	return 0;
}

int av8100_init_device(struct av8100_device *adev, struct device *dev)
{
	adev->dev = dev;

	if (av8100_config_init(adev)) {
		dev_info(dev, "av8100_config_init failed\n");
		return -EFAULT;
	}

	if (av8100_params_init(adev)) {
		dev_info(dev, "av8100_params_init failed\n");
		return -EFAULT;
	}
	return 0;
}

static int __devinit av8100_probe(struct i2c_client *i2c_client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct av8100_platform_data *pdata = i2c_client->dev.platform_data;
	struct device *dev;
	struct av8100_device *adev;

	dev = &i2c_client->dev;

	dev_dbg(dev, "%s\n", __func__);

	/* Allocate device data */
	adev = kzalloc(sizeof(struct av8100_device), GFP_KERNEL);
	if (!adev) {
		dev_info(dev, "%s: Alloc failure\n", __func__);
		return -ENOMEM;
	}

	/* Add to list */
	list_add_tail(&adev->list, &av8100_device_list);

	av8100_device_register(adev);

	av8100_init_device(adev, dev);

	av8100_set_state(adev, AV8100_OPMODE_UNDEFINED);

	if (!i2c_check_functionality(i2c_client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA |
			I2C_FUNC_SMBUS_READ_WORD_DATA)) {
		ret = -ENODEV;
		dev_info(dev, "av8100 i2c_check_functionality failed\n");
		goto err1;
	}

	init_waitqueue_head(&adev->event);

	adev->config.client = i2c_client;
	adev->config.id = (struct i2c_device_id *) id;
	i2c_set_clientdata(i2c_client, &adev->config);

	kthread_run(av8100_thread, adev, "av8100_thread");

	/* Get regulator resource */
	if (pdata->regulator_pwr_id) {
		adev->params.regulator_pwr = regulator_get(dev,
				pdata->regulator_pwr_id);
		if (IS_ERR(adev->params.regulator_pwr)) {
			ret = PTR_ERR(adev->params.regulator_pwr);
			dev_warn(dev,
				"%s: Failed to get regulator '%s'\n",
				__func__, pdata->regulator_pwr_id);
			adev->params.regulator_pwr = NULL;
			goto err1;
		}
	}

	/* Get clock resource */
	if (pdata->inputclk_id) {
		adev->params.inputclk = clk_get(NULL, pdata->inputclk_id);
		if (IS_ERR(adev->params.inputclk)) {
			adev->params.inputclk = NULL;
			dev_warn(dev, "%s: Failed to get clock '%s'\n",
					__func__, pdata->inputclk_id);
		}
	}

	av8100_set_state(adev, AV8100_OPMODE_SHUTDOWN);

	if (av8100_powerup1(adev)) {
		dev_err(adev->dev, "av8100_powerup1 fail\n");
		ret = -EFAULT;
		goto err1;
	}

	/* Obtain the chip version */
	ret = av8100_reg_stby_pend_int_r(NULL, NULL, NULL, NULL,
					&adev->chip_version);
	if (ret) {
		dev_err(adev->dev, "Failed to read chip version\n");
		goto err2;
	}

	dev_info(adev->dev, "chip version:%d\n", adev->chip_version);

	switch (adev->chip_version) {
	case AV8100_CHIPVER_1:
	case AV8100_CHIPVER_2:
		break;

	default:
		dev_err(adev->dev, "Unsupported chip version:%d\n",
				adev->chip_version);
		ret = -EINVAL;
		goto err2;
		break;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	adev->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	adev->early_suspend.suspend = early_suspend;
	adev->early_suspend.resume = late_resume;
#endif
	register_early_suspend(&adev->early_suspend);

	/* Timer init */
	hrtimer_init(&adev->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	adev->hrtimer.function = hrtimer_func;

err2:
	(void) av8100_powerdown();
err1:
	return ret;
}

static int __devexit av8100_remove(struct i2c_client *i2c_client)
{
	struct av8100_device *adev;

	adev = dev_to_adev(&i2c_client->dev);
	if (!adev)
		return -EFAULT;

	dev_dbg(adev->dev, "%s\n", __func__);

	hrtimer_cancel(&adev->hrtimer);

	if (adev->params.inputclk)
		clk_put(adev->params.inputclk);

	/* Release regulator resource */
	if (adev->params.regulator_pwr)
		regulator_put(adev->params.regulator_pwr);

	unregister_early_suspend(&adev->early_suspend);

	misc_deregister(&adev->miscdev);

	/* Remove from list */
	list_del(&adev->list);

	/* Free device data */
	kfree(adev);

	return 0;
}

int av8100_init(void)
{
	pr_debug("%s\n", __func__);

	if (i2c_add_driver(&av8100_driver)) {
		pr_err("av8100 i2c_add_driver failed\n");
		return -EFAULT;
	}

	return 0;
}
module_init(av8100_init);

void av8100_exit(void)
{
	pr_debug("%s\n", __func__);

	i2c_del_driver(&av8100_driver);
}
module_exit(av8100_exit);

MODULE_AUTHOR("Per Persson <per.xb.persson@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson hdmi display driver");
