/*
 * mms_ts.c - Touchscreen driver for Melfas MMS-series touch controllers
 *
 * Copyright (C) 2011 Google Inc.
 * Author: Dima Zavin <dima@android.com>
 *         Simon Wilson <simonwilson@google.com>
 *
 * ISP reflashing code based on original code from Melfas.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */
#define TSP_FACTORY
#define TSK_FACTORY

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/input/mms136_ts.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#ifdef TSP_FACTORY
#include <linux/list.h>
#endif

#define MAX_FINGERS		5
#define MAX_WIDTH		30
#define MAX_PRESSURE		255

/* Registers */
#define MMS_MODE_CONTROL	0x01
#define MMS_XYRES_HI		0x02
#define MMS_XRES_LO		0x03
#define MMS_YRES_LO		0x04
#define MMS_THRESHOLD		0x05

#define MMS_INPUT_EVENT_PKT_SZ	0x0F
#define MMS_INPUT_EVENT0	0x10
#define	FINGER_EVENT_SZ		6

#define MMS_TSP_REVISION	0xF0
#define MMS_HW_REVISION		0xF1
#define MMS_COMPAT_GROUP	0xF2
#define MMS_FW_VERSION		0xF3

enum {
	ISP_MODE_FLASH_ERASE	= 0x59F3,
	ISP_MODE_FLASH_WRITE	= 0x62CD,
	ISP_MODE_FLASH_READ	= 0x6AC9,
};

/* each address addresses 4-byte words */
#define ISP_MAX_FW_SIZE		(0x1F00 * 4)
#define ISP_IC_INFO_ADDR	0x1F00

static bool mms_force_reflash = false;
module_param_named(force_reflash, mms_force_reflash, bool, S_IWUSR | S_IRUGO);

static bool mms_flash_from_probe = true;
module_param_named(flash_from_probe, mms_flash_from_probe, bool,
		   S_IWUSR | S_IRUGO);

static bool mms_die_on_flash_fail = true;
module_param_named(die_on_flash_fail, mms_die_on_flash_fail, bool,
		   S_IWUSR | S_IRUGO);

#define MSS_WORD_SIZE			4
#define MMS_PKT_HDR_SIZE		3
#define MMS_PKT_SIZE			1024
#define MMS_PKT_TAIL_SIZE		2
#define MMS_PKT_NUMS			30
#define MMS_ENTER_ISC			0x5F
#define MMS_ISC_CMD			0xAE
#define MMS_ISC_SUB_CMD			0xF1
#define MMS_ISC_CLR			0x1E
#define MMS_ISC_STATUS			0xAF

/* VSC(Vender Specific Command)  */
#define MMS_VSC			0xB0	/* vendor specific command */
#define MMS_VSC_MODE		0x1A	/* mode of vendor */
enum {
	VSC_ENTER		= 0X01,
	VSC_INSPECTION,		/* same as cm delta*/
	VSC_CM_ABS,
	VSC_INTENSITY,
	VSC_EXIT,
	VSC_RAW,
	VSC_REFERENCE,
	VSC_INSPECTION_TK	= 0x12,
	VSC_CM_ABS_TK,
	VSC_INTENSITY_TK,
	VSC_RAW_TK		= 0x16,
	VSC_REFERENCE_TK,
};

/* SDP (Self Diagnostic Process) */
#define MMS_SDP			0xA0
#define MMS_SDP_PARAM		0xA1
#define	MMS_SDP_RESULT_SIZE	0xAE
#define	MMS_SDP_RESULT		0xAF
enum {
	SDP_ENTER_TEST_MODE = 0X40,
	SDP_CM_DELTA,
	SDP_CM_DELTA_ONE,
	SDP_CM_ABS,
	SDP_CM_ABS_ONE,
	SDP_CM_JITTER,
	SDP_CM_JITTER_ONE,
	SDP_CM_RATIO,
	SDP_CM_RATIO_ONE,
	MMS_SDP_EXIT_TEST_MODE = 0x4F,
};

#ifdef TSP_FACTORY
#define TSP_CMD_STR_LEN		32
#define TSP_CMD_RESULT_STR_LEN	512
#define TSP_CMD_PARAM_NUM	8
#define TSP_CMD_X_NUM		17	/* num of tx */
#define TSP_CMD_Y_NUM		11	/* num of rx */
#define TSP_CMD_NODE_NUM	187	/* 17x11 */
#endif

enum {
	ISP_FLASH = 0,
	ISC_FLASH,
};

enum {
	BUILT_IN = 0,
	UMS,
};

enum {
	None = 0,
	TOUCH_SCREEN,
	TOUCH_KEY
};

struct mms_fw_image {
	__le32 hdr_len;
	__le32 data_len;
/*	__le32 fw_ver; */
	u8 hw_ver;
	u8 core_ver;
	u8 private_ver;
	u8 public_ver;
	__le32 hdr_ver;
	u8 data[0];
} __attribute__ ((packed));


struct mms_ts_info {
	struct i2c_client		*client;
	struct input_dev		*input_dev_ts;
	struct input_dev		*input_dev_tk;
	char				phys_ts[32];
	char				phys_tk[32];
	struct mms_ts_platform_data	*pdata;
	struct early_suspend		early_suspend;
	int				irq;

	struct mms_fw_image		*fw_img;
	const struct firmware		*fw;

	struct completion		init_done;

	/* protects the enabled flag */
	struct mutex			lock;
	bool				enabled;
	bool 				finger_state[MAX_FINGERS];

#ifdef TSK_FACTORY
	struct device			*dev_tk;
	bool				*key_pressed;
#endif

#ifdef TSP_FACTORY
	struct device			*dev_ts;
	struct device			*dev_ts_temp;
	struct list_head		cmd_list_head;
	u8				cmd_state;
	char				cmd[TSP_CMD_STR_LEN];
	int				cmd_param[TSP_CMD_PARAM_NUM];
	char				cmd_result[TSP_CMD_RESULT_STR_LEN];
	char				cmd_buff[TSP_CMD_RESULT_STR_LEN];
	struct mutex			cmd_lock;
	bool				cmd_is_running;
	u16				raw_reference[TSP_CMD_NODE_NUM];
	u16				raw_cm_abs[TSP_CMD_NODE_NUM];
	u16				raw_inspection[TSP_CMD_NODE_NUM];
	u16				raw_intensity[TSP_CMD_NODE_NUM];
#endif
};

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
static void get_reference(void *device_data);
static void get_cm_abs(void *device_data);
static void get_inspection(void *device_data);
static void get_intensity(void *device_data);
static void run_reference_read(void *device_data);
static void run_cm_abs_read(void *device_data);
static void run_inspection_read(void *device_data);
static void run_intensity_read(void *device_data);
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
	{TSP_CMD("run_reference_read", run_reference_read),},
	{TSP_CMD("run_cm_abs_read", run_cm_abs_read),},
	{TSP_CMD("run_inspection_read", run_inspection_read),},
	{TSP_CMD("run_intensity_read", run_intensity_read),},
	{TSP_CMD("get_reference", get_reference),},
	{TSP_CMD("get_cm_abs", get_cm_abs),},
	{TSP_CMD("get_inspection", get_inspection),},
	{TSP_CMD("get_intensity", get_intensity),},
	{TSP_CMD("not_support_cmd", not_support_cmd),},
};
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mms_ts_early_suspend(struct early_suspend *h);
static void mms_ts_late_resume(struct early_suspend *h);
#endif

static void hw_reboot(struct mms_ts_info *info, bool bootloader)
{
	gpio_direction_output(info->pdata->gpio_vdd_en, 0);
	gpio_direction_output(info->pdata->gpio_sda, bootloader ? 0 : 1);
	gpio_direction_output(info->pdata->gpio_scl, bootloader ? 0 : 1);
	gpio_direction_output(info->pdata->gpio_int, 0);
	msleep(30);
	gpio_set_value(info->pdata->gpio_vdd_en, 1);
	msleep(30);

	if (bootloader) {
		gpio_set_value(info->pdata->gpio_scl, 0);
		gpio_set_value(info->pdata->gpio_sda, 1);
	} else {
		gpio_set_value(info->pdata->gpio_int, 1);
		gpio_direction_input(info->pdata->gpio_int);
		gpio_direction_input(info->pdata->gpio_scl);
		gpio_direction_input(info->pdata->gpio_sda);
	}
	msleep(40);
}

static inline void hw_reboot_bootloader(struct mms_ts_info *info)
{
	hw_reboot(info, true);
}

static inline void hw_reboot_normal(struct mms_ts_info *info)
{
	hw_reboot(info, false);
}

static irqreturn_t mms_ts_interrupt(int irq, void *dev_id)
{
	struct mms_ts_info *info = dev_id;
	struct i2c_client *client = info->client;
	u8 buf[MAX_FINGERS * FINGER_EVENT_SZ] = { 0 };
	int ret;
	int i;
	int sz;
	u8 reg = MMS_INPUT_EVENT0;
	struct i2c_msg msg[] = {
		{
			.addr   = client->addr,
			.flags  = 0,
			.buf    = &reg,
			.len    = 1,
		}, {
			.addr   = client->addr,
			.flags  = I2C_M_RD,
			.buf    = buf,
		},
	};

	/* checking real interrupt */
	udelay(10);
	if (gpio_get_value(info->pdata->gpio_int))
		goto out;

	sz = i2c_smbus_read_byte_data(client, MMS_INPUT_EVENT_PKT_SZ);
	if (sz < 0) {
		dev_err(&client->dev, "%s bytes=%d\n", __func__, sz);
		goto out;
	}
	if (sz % 6 || sz == 0) {
		dev_err(&client->dev, "wrong msg size (%d)\n", sz);
		goto out;
	}
	msg[1].len = sz;
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"failed to read %d bytes of touch data (%d)\n",
			sz, ret);
		goto out;
	}

	if (buf[0] == 0x0f) {
		dev_info(&client->dev, "ESD is detected."
						"Now, IC will be reset\n");
		hw_reboot_normal(info);
		goto out;
	}

#if defined(VERBOSE_DEBUG)
	print_hex_dump(KERN_DEBUG, "mms_ts raw: ",
		       DUMP_PREFIX_OFFSET, 32, 1, buf, sz, false);
#endif

	for (i = 0; i < sz; i += FINGER_EVENT_SZ) {
		u8 *tmp = &buf[i];
		u8 type = (tmp[0] >> 5) & 0x03;
		int id = (tmp[0] & 0xf) - 1;
		u8 action = tmp[0] >> 7;

		if (type == TOUCH_SCREEN) {
			int x = tmp[2] | ((tmp[1] & 0xf) << 8);
			int y = tmp[3] | (((tmp[1] >> 4) & 0xf) << 8);
			int w = tmp[4];
			int s = tmp[5];

			if (info->pdata->invert_x)
				x = (info->pdata->max_x - x >= 0) ?
						info->pdata->max_x - x : 0;

			if (info->pdata->invert_y)
				y = (info->pdata->max_y - y >= 0) ?
						info->pdata->max_y - y : 0;

			if (!action) {
				dev_info(&client->dev, 
					"%4s[%d]: %3d, %3d (%3d,%3d)\n",
					"up",id, x, y, w, s);

				input_mt_slot(info->input_dev_ts, id);
				input_mt_report_slot_state(info->input_dev_ts,
						   MT_TOOL_FINGER, false);
				info->finger_state[id] = 0;

				continue;
			}

			if (info->finger_state[id] == 0) {
				info->finger_state[id] = 1;
				dev_info(&client->dev,
					"%4s[%d]: %3d, %3d (%3d,%3d)\n",
					"down",id, x, y, w, s);
			}
			
			input_mt_slot(info->input_dev_ts, id);
			input_mt_report_slot_state(info->input_dev_ts,
						   MT_TOOL_FINGER, true);
			input_report_abs(info->input_dev_ts, ABS_MT_POSITION_X, x);
			input_report_abs(info->input_dev_ts, ABS_MT_POSITION_Y, y);
			input_report_abs(info->input_dev_ts, ABS_MT_TOUCH_MAJOR, w);
			input_report_abs(info->input_dev_ts, ABS_MT_PRESSURE, s);

		} else if (type == TOUCH_KEY) {
			dev_info(&client->dev, "key(%3d): %s\n",
					info->pdata->key_map[id],
					(action) ? "down" : "up");
#ifdef TSK_FACTORY
			info->key_pressed[id] = action;
#endif

			input_report_key(info->input_dev_tk,
					info->pdata->key_map[id], action);
			input_sync(info->input_dev_tk);
		} else {
			dev_err(&client->dev, "not proper input data.\n");
			goto out;
		}
	}
	
	input_sync(info->input_dev_ts);

out:
	return IRQ_HANDLED;
}

static inline void mms_pwr_on_reset(struct mms_ts_info *info)
{
	struct i2c_adapter *adapter = to_i2c_adapter(info->client->dev.parent);

	if (!info->pdata->pin_configure) {
		dev_info(&info->client->dev,
			 "missing platform data, can't do power-on-reset\n");
		return;
	}

	i2c_lock_adapter(adapter);
	info->pdata->pin_configure(true);

	gpio_direction_output(info->pdata->gpio_vdd_en, 0);
	gpio_direction_output(info->pdata->gpio_sda, 1);
	gpio_direction_output(info->pdata->gpio_scl, 1);
	gpio_direction_output(info->pdata->gpio_int, 1);
	msleep(50);
	gpio_direction_output(info->pdata->gpio_vdd_en, 1);
	msleep(50);

	info->pdata->pin_configure(false);
	i2c_unlock_adapter(adapter);

	/* TODO: Seems long enough for the firmware to boot.
	 * Find the right value */
	msleep(250);
}

static void isp_toggle_clk(struct mms_ts_info *info, int start_lvl, int end_lvl,
			   int hold_us)
{
	gpio_direction_output(info->pdata->gpio_scl, start_lvl);
	udelay(hold_us);
	gpio_direction_output(info->pdata->gpio_scl, end_lvl);
	udelay(hold_us);
}

/* 1 <= cnt <= 32 bits to write */
static void isp_send_bits(struct mms_ts_info *info, u32 data, int cnt)
{
	gpio_direction_output(info->pdata->gpio_int, 0);
	gpio_direction_output(info->pdata->gpio_scl, 0);
	gpio_direction_output(info->pdata->gpio_sda, 0);

	/* clock out the bits, msb first */
	while (cnt--) {
		gpio_set_value(info->pdata->gpio_sda, (data >> cnt) & 1);
		udelay(3);
		isp_toggle_clk(info, 1, 0, 3);
	}
}

/* 1 <= cnt <= 32 bits to read */
static u32 isp_recv_bits(struct mms_ts_info *info, int cnt)
{
	u32 data = 0;

	gpio_direction_output(info->pdata->gpio_int, 0);
	gpio_direction_output(info->pdata->gpio_scl, 0);
	gpio_set_value(info->pdata->gpio_sda, 0);
	gpio_direction_input(info->pdata->gpio_sda);

	/* clock in the bits, msb first */
	while (cnt--) {
		isp_toggle_clk(info, 0, 1, 1);
		data = (data << 1) | (!!gpio_get_value(info->pdata->gpio_sda));
	}

	gpio_direction_output(info->pdata->gpio_sda, 0);
	return data;
}

static void isp_enter_mode(struct mms_ts_info *info, u32 mode)
{
	int cnt;
	unsigned long flags;

	local_irq_save(flags);
	gpio_direction_output(info->pdata->gpio_int, 0);
	gpio_direction_output(info->pdata->gpio_scl, 0);
	gpio_direction_output(info->pdata->gpio_sda, 1);

	mode &= 0xffff;
	for (cnt = 15; cnt >= 0; cnt--) {
		gpio_set_value(info->pdata->gpio_int, (mode >> cnt) & 1);
		udelay(3);
		isp_toggle_clk(info, 1, 0, 3);
	}

	gpio_set_value(info->pdata->gpio_int, 0);
	local_irq_restore(flags);
}

static void isp_exit_mode(struct mms_ts_info *info)
{
	int i;
	unsigned long flags;

	local_irq_save(flags);
	gpio_direction_output(info->pdata->gpio_int, 0);
	udelay(3);

	for (i = 0; i < 10; i++)
		isp_toggle_clk(info, 1, 0, 3);
	local_irq_restore(flags);
}

static void flash_set_address(struct mms_ts_info *info, u16 addr)
{
	/* Only 13 bits of addr are valid.
	 * The addr is in bits 13:1 of cmd */
	isp_send_bits(info, (u32)(addr & 0x1fff) << 1, 18);
}

static void flash_erase(struct mms_ts_info *info)
{
	isp_enter_mode(info, ISP_MODE_FLASH_ERASE);

	gpio_direction_output(info->pdata->gpio_int, 0);
	gpio_direction_output(info->pdata->gpio_scl, 0);
	gpio_direction_output(info->pdata->gpio_sda, 1);

	/* 4 clock cycles with different timings for the erase to
	 * get processed, clk is already 0 from above */
	udelay(7);
	isp_toggle_clk(info, 1, 0, 3);
	udelay(7);
	isp_toggle_clk(info, 1, 0, 3);

	msleep(35); /* usleep_range(25000, 35000); */
	isp_toggle_clk(info, 1, 0, 3);
	udelay(200); /* usleep_range(150, 200); */
	isp_toggle_clk(info, 1, 0, 3);

	gpio_set_value(info->pdata->gpio_sda, 0);

	isp_exit_mode(info);
}

static u32 flash_readl(struct mms_ts_info *info, u16 addr)
{
	int i;
	u32 val;
	unsigned long flags;

	local_irq_save(flags);
	isp_enter_mode(info, ISP_MODE_FLASH_READ);
	flash_set_address(info, addr);

	gpio_direction_output(info->pdata->gpio_scl, 0);
	gpio_direction_output(info->pdata->gpio_sda, 0);
	udelay(40);

	/* data load cycle */
	for (i = 0; i < 6; i++)
		isp_toggle_clk(info, 1, 0, 10);

	val = isp_recv_bits(info, 32);
	isp_exit_mode(info);
	local_irq_restore(flags);

	return val;
}

static void flash_writel(struct mms_ts_info *info, u16 addr, u32 val)
{
	unsigned long flags;

	local_irq_save(flags);
	isp_enter_mode(info, ISP_MODE_FLASH_WRITE);
	flash_set_address(info, addr);
	isp_send_bits(info, val, 32);

	gpio_direction_output(info->pdata->gpio_sda, 1);
	/* 6 clock cycles with different timings for the data to get written
	 * into flash */
	isp_toggle_clk(info, 0, 1, 3);
	isp_toggle_clk(info, 0, 1, 3);
	isp_toggle_clk(info, 0, 1, 6);
	isp_toggle_clk(info, 0, 1, 12);
	isp_toggle_clk(info, 0, 1, 3);
	isp_toggle_clk(info, 0, 1, 3);

	isp_toggle_clk(info, 1, 0, 1);

	gpio_direction_output(info->pdata->gpio_sda, 0);
	isp_exit_mode(info);
	local_irq_restore(flags);
	udelay(400);	/* usleep_range(300, 400); */
}

static bool flash_is_erased(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	u32 val;
	u16 addr;

	for (addr = 0; addr < (ISP_MAX_FW_SIZE / 4); addr++) {
		udelay(40);
		val = flash_readl(info, addr);

		if (val != 0xffffffff) {
			dev_dbg(&client->dev,
				"addr 0x%x not erased: 0x%08x != 0xffffffff\n",
				addr, val);
			return false;
		}
	}
	return true;
}

static int fw_write_image(struct mms_ts_info *info, const u8 *data, size_t len)
{
	struct i2c_client *client = info->client;
	u16 addr = 0;

	for (addr = 0; addr < (len / 4); addr++, data += 4) {
		u32 val = get_unaligned_le32(data);
		u32 verify_val;
		int retries = 3;

		while (retries--) {
			flash_writel(info, addr, val);
			verify_val = flash_readl(info, addr);
			if (val == verify_val)
				break;
			dev_err(&client->dev,
				"mismatch @ addr 0x%x: 0x%x != 0x%x\n",
				addr, verify_val, val);
			hw_reboot_bootloader(info);
			continue;
		}
		if (retries < 0)
			return -ENXIO;
	}

	return 0;
}

static int fw_download_isp(struct mms_ts_info *info, const u8 *data, size_t len)
{
	struct i2c_client *client = info->client;
	u32 val;
	int ret = 0;

	if (len % 4) {
		dev_err(&client->dev,
			"fw image size (%d) must be a multiple of 4 bytes\n",
			len);
		return -EINVAL;
	} else if (len > ISP_MAX_FW_SIZE) {
		dev_err(&client->dev,
			"fw image is too big, %d > %d\n", len, ISP_MAX_FW_SIZE);
		return -EINVAL;
	}

	dev_info(&client->dev, "fw download start\n");

	gpio_direction_output(info->pdata->gpio_vdd_en, 0);
	gpio_direction_output(info->pdata->gpio_sda, 0);
	gpio_direction_output(info->pdata->gpio_scl, 0);
	gpio_direction_output(info->pdata->gpio_int, 0);

	hw_reboot_bootloader(info);

	val = flash_readl(info, ISP_IC_INFO_ADDR);
	dev_info(&client->dev, "IC info: 0x%02x (%x)\n", val & 0xff, val);

	dev_info(&client->dev, "fw erase...\n");
	flash_erase(info);
	if (!flash_is_erased(info)) {
		ret = -ENXIO;
		goto err;
	}

	dev_info(&client->dev, "fw write...\n");
	/* XXX: what does this do?! */
	flash_writel(info, ISP_IC_INFO_ADDR, 0xffffff00 | (val & 0xff));
	msleep(2);/*usleep_range(1000, 1500);*/
	ret = fw_write_image(info, data, len);
	if (ret)
		goto err;
	msleep(2);/*usleep_range(1000, 1500);*/

	hw_reboot_normal(info);
	msleep(2);/*usleep_range(1000, 1500);*/
	dev_info(&client->dev, "fw download done...\n");
	return 0;

err:
	dev_err(&client->dev, "fw download failed...\n");
	hw_reboot_normal(info);
	return ret;
}

static u16 gen_crc(u8 data, u16 pre_crc)
{
	u16 crc;
	u16 cur;
	u16 temp;
	u16 bit_1;
	u16 bit_2;
	int i;

	crc = pre_crc;
	for (i = 7; i >= 0; i--) {
		cur = ((data >> i) & 0x01) ^ (crc & 0x0001);
		bit_1 = cur ^ (crc >> 11 & 0x01);
		bit_2 = cur ^ (crc >> 4 & 0x01);
		temp = (cur << 4) | (crc >> 12 & 0x0F);
		temp = (temp << 7) | (bit_1 << 6) | (crc >> 5 & 0x3F);
		temp = (temp << 4) | (bit_2 << 3) | (crc >> 1 & 0x0007);
		crc = temp;
	}
	return crc;
}

static int fw_download_isc(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	u8 *buff;
	u16 crc;
	int src_idx;
	int dest_idx;
	int ret;
	int page;
	u16 i;
	u8 status;

	buff = kzalloc(MMS_PKT_HDR_SIZE + MMS_PKT_SIZE + MMS_PKT_TAIL_SIZE,
								GFP_KERNEL);
	if (!buff) {
		dev_err(&client->dev, "failed to alloc memory for isc"
							"packet data.\n");
		ret = -1;
		goto err_alloc;
	}

	*buff = MMS_ENTER_ISC;
	*(buff + 1) = 0x01;
	ret = i2c_master_send(client, buff, 2);
	if (ret < 0) {
		dev_err(&client->dev,
					"fail to enter isc mode(%d).\n", ret);
		goto err_enter_isc;
	}
	msleep(10);
	dev_info(&client->dev, "enter isc mode\n");

	*buff = MMS_ISC_CMD;
	*(buff + 1) = 0x55;
	memset(buff + 2, 0x00, 8);
	ret = i2c_master_send(client, buff, 10);
	if (ret < 0) {
		dev_err(&client->dev,
				"fail to enter isc update mode(%d).\n", ret);
		goto err_isc_download;
	}
	msleep(100);

	status = i2c_smbus_read_byte_data(client, MMS_ISC_STATUS);
	if (status != 0x01) {
		dev_err(&client->dev,
				"err isc update status (%d).\n", ret);
		goto err_isc_download;
	}
	dev_info(&client->dev, "enter isc update mode (status:0x%X)\n", status);
	msleep(100);

	*(buff + 0) = MMS_ISC_CMD;
	*(buff + 1) = MMS_ISC_SUB_CMD;
	*(buff + 2) = 30;
	memset(buff + MMS_PKT_HDR_SIZE, 0xFF, MMS_PKT_SIZE);
	*(buff + MMS_PKT_HDR_SIZE + MMS_PKT_SIZE) = 0xEB;
	*(buff + MMS_PKT_HDR_SIZE + MMS_PKT_SIZE + 1) = 0x99;
	ret = i2c_master_send(client, buff,
			MMS_PKT_HDR_SIZE + MMS_PKT_SIZE + MMS_PKT_TAIL_SIZE);


	crc = gen_crc(*(buff + 2), 0xFFFF);
	for (i = 0; i < MMS_PKT_SIZE; i++)
		crc = gen_crc(*(buff + MMS_PKT_HDR_SIZE + i), crc);

	dev_err(&client->dev, "crc = 0x%X.\n", crc);

	if (ret < 0) {
		dev_err(&client->dev, "fail to erase fw (%d).\n", ret);
		goto err_isc_download;
	}
	msleep(100);

	status = i2c_smbus_read_byte_data(client, MMS_ISC_STATUS);
	if (status != 0x03) {
		dev_err(&client->dev,
				"err isc erase status (%d).\n", ret);
		goto err_isc_download;
	}
	dev_info(&client->dev, "fw is erased (status:0x%X)\n", status);
	msleep(100);

	for (page = 1; page < 31; page++) {
		*(buff + 2) = page;
		crc = gen_crc(*(buff + 2), 0xFFFF);

		for (i = 0; i < MMS_PKT_SIZE; i++) {
			dest_idx = MMS_PKT_HDR_SIZE + i;
			/* swap word */
			src_idx =  page * MMS_PKT_SIZE +
				(i >> 2) * MSS_WORD_SIZE + ((i & 0x3) ^ 0x3);
			*(buff + dest_idx) = *(info->fw_img->data + src_idx);
			crc = gen_crc(*(buff + dest_idx), crc);
		}
		*(buff + MMS_PKT_HDR_SIZE + MMS_PKT_SIZE) = crc >> 8 & 0xFF;
		*(buff + MMS_PKT_HDR_SIZE + MMS_PKT_SIZE + 1) = crc & 0xFF;

		ret = i2c_master_send(client, buff, MMS_PKT_HDR_SIZE +
					MMS_PKT_SIZE + MMS_PKT_TAIL_SIZE);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to fw writing on packet %d.(%d)\n",
				page, ret);
			goto err_isc_download;
		}
		udelay(5);

		status = i2c_smbus_read_byte_data(client, MMS_ISC_STATUS);
		if (status != 0x03) {
			dev_err(&client->dev,
					"err isc write status (%d).\n", status);
			goto err_isc_download;
		}
		dev_info(&client->dev, "packet %d is written (status:0x%X)\n",
								page, status);
	}

	*buff = MMS_ISC_CMD;
	*(buff + 1) = 0x0F;
	*(buff + 2) = 0x0F;
	ret = i2c_master_send(client, buff, 3);
	msleep(20); /* usleep_range(10000, 20000); */

/*	if (status != 0x00 && status != 0xFF) { */
	if (status != 0x03) {
		dev_err(&client->dev, "err isc exit status (%d).\n", ret);
		goto err_isc_download;
	}
	dev_info(&client->dev, "exit from isc mode (status:0x%X)\n", status);

	ret = 0;
err_isc_download:

	hw_reboot_normal(info);
err_enter_isc:
	kfree(buff);
err_alloc:
	return ret;
}

static int mms_ts_fw_load_built_in(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	const struct firmware *_fw;
	struct mms_fw_image *_fw_img;
	int ret;

	dev_info(&client->dev,
		"load firmware %s\n", info->pdata->fw_name_builtin);

	ret = request_firmware(&_fw, info->pdata->fw_name_builtin,
								&client->dev);
	if (ret) {
		dev_err(&client->dev,
				"error requesting built-in fw (%d)\n", ret);
		ret = -1;
		goto err_request_fw;
	}

	_fw_img = (struct mms_fw_image *)_fw->data;
	if (_fw_img->hdr_len != sizeof(struct mms_fw_image) ||
		_fw_img->data_len + _fw_img->hdr_len != _fw->size ||
		_fw_img->hdr_ver != 0x1) {
		dev_err(&client->dev,
			"fw image '%s' invalid, may continue\n",
			info->pdata->fw_name_builtin);
		ret = -2;
		goto err_fw_size;
	}

	dev_info(&client->dev, "built-in fw is loaded (%d)\n", _fw->size);

	info->fw = _fw;
	info->fw_img = _fw_img;

	return 0;

err_fw_size:
	release_firmware(_fw);
err_request_fw:
	return ret;
}

static void mms_ts_fw_unload_built_in(struct mms_ts_info *info)
{
	release_firmware(info->fw);
	info->fw = NULL;
}

static int mms_ts_fw_load_ums(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	struct mms_fw_image *_fw_img;
	mm_segment_t old_fs;
	struct file *fp;
	long fsize, nread;
	u8 *buff;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(info->pdata->fw_name_ums, O_RDONLY, S_IRUSR);
	if (!fp) {
		dev_err(&client->dev, "fail to open fw in ums (%s)\n",
				info->pdata->fw_name_ums);
		ret = -1;
		goto err_open;
	}
	fsize = fp->f_path.dentry->d_inode->i_size;

	buff = kzalloc((size_t)fsize, GFP_KERNEL);
	if (!buff) {
		dev_err(&client->dev, "fail to alloc buffer for fw\n");
		ret = -2;
		goto err_alloc;
	}
	nread = vfs_read(fp, (char __user *)buff, fsize, &fp->f_pos);

	_fw_img = (struct mms_fw_image *)buff;

	if (_fw_img->hdr_len != sizeof(struct mms_fw_image) ||
		_fw_img->data_len + _fw_img->hdr_len != nread ||
		_fw_img->hdr_ver != 0x1) {
		dev_err(&client->dev,
			"fw image '%s' invalid, may continue\n",
			info->pdata->fw_name_ums);
		goto err_fw_size;
	}

	info->fw_img = _fw_img;

	filp_close(fp, NULL);
	set_fs(old_fs);
	return 0;

err_fw_size:
	kfree(buff);
err_alloc:
	filp_close(fp, NULL);
err_open:
	set_fs(old_fs);
	return ret;
}

static void mms_ts_fw_unload_ums(struct mms_ts_info *info)
{
	kfree(info->fw_img);
	info->fw_img = NULL;
}

static int mms_ts_fw_flash_isp(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret;

	dev_info(&client->dev, "isp firmware update start\n");

	if (!info->pdata->pin_configure) {
		dev_err(&client->dev, "missing pin configure function.\n");
		ret = -1;
		goto err_pin_configure;
	}

	i2c_lock_adapter(adapter);
	info->pdata->pin_configure(true);

	ret = fw_download_isp(info, info->fw_img->data, info->fw_img->data_len);

	info->pdata->pin_configure(false);
	i2c_unlock_adapter(adapter);

	if (ret < 0) {
		dev_err(&client->dev,
			"error updating firmware to version 0x%02x\n",
			info->fw_img->core_ver);
	}

err_pin_configure:
	return ret;
}

static int mms_ts_fw_flash_isc(struct mms_ts_info *info)
{
	int ret;

	dev_info(&info->client->dev, "isc firmware update start\n");
	ret = fw_download_isc(info);
	return ret;
}

static bool need_update(u8 core_ic, u8 prv_ic, u8 pub_ic, u8 core_bin,
							u8 prv_bin, u8 pub_bin)
{
	if (core_ic < core_bin) {
		return true;
	} else if (core_ic > core_bin) {
		return false;
	} else {
		if (prv_ic < prv_bin) {
			return true;
		} else if (prv_ic > prv_bin) {
			return false;
		} else {
			if (pub_ic < pub_bin)
				return true;
			else if (pub_ic >= pub_bin)
				return false;
		}
	}
	return true;
}

static int mms_ts_fw_update(struct mms_ts_info *info, u8 fw_file_type,
						u8 fw_update_type, bool force)
{
	struct i2c_client *client = info->client;
	u8 fw_ver[3];
	int ret;
	int retries = 3;

	switch (fw_file_type) {
	case BUILT_IN:
		ret = mms_ts_fw_load_built_in(info);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to load built-in fw (%d)\n", ret);
			ret = -1;
			goto err_fw_load;
		}
		break;
	case UMS:
		ret = mms_ts_fw_load_ums(info);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to load fw in ums(%d)\n", ret);
			ret = -1;
			goto err_fw_load;
		}
		break;
	default:
		dev_err(&client->dev, "invalid fw file type.\n");
		ret = -1;
		goto err_fw_load;
	}

	dev_info(&client->dev,
			"loaded fw: core(0x%X), private(0x%X), public(0x%X)\n",
			info->fw_img->core_ver, info->fw_img->private_ver,
			info->fw_img->public_ver);

	ret = i2c_smbus_read_i2c_block_data(client, MMS_FW_VERSION, 3,
								(u8 *)fw_ver);
	if (ret < 0) {
		dev_err(&client->dev, "fail to read flashed fw ver(%d).\n",
									ret);
		goto flash_fw;
	}
	dev_info(&client->dev,
			"fw in IC: core(0x%X), private(0x%X), public(0x%X)\n",
			fw_ver[0], fw_ver[1], fw_ver[2]);

	ret = (int)need_update(fw_ver[0], fw_ver[1], fw_ver[2],
			info->fw_img->core_ver, info->fw_img->private_ver,
						info->fw_img->public_ver);
	if (!ret && !force) {
		ret = 0;
		goto do_not_need_update;
	}

flash_fw:
	switch (fw_update_type) {
	case ISP_FLASH:
		ret = mms_ts_fw_flash_isp(info);
		break;
	case ISC_FLASH:
		ret = mms_ts_fw_flash_isc(info);
		break;
	default:
		dev_err(&client->dev, "invalid fw flash method.\n");
		ret = -3;
		goto err_fw_flash;
	}

	retries--;

	if (ret < 0) {
		if (retries > 0) {
			dev_err(&client->dev, "retrying flashing(%d)\n",
								retries);
			goto flash_fw;
		} else {
			dev_err(&client->dev, "flashing failure\n");
			ret = -3;
			goto err_fw_flash;
		}
	}

	ret = i2c_smbus_read_i2c_block_data(client, MMS_FW_VERSION, 3,
								(u8 *)fw_ver);
	if (ret < 0) {
		dev_err(&client->dev, "fail to read flashed fw ver(%d).\n",
									ret);
		ret = -2;
		goto err_fw_ver_read;
	}

	dev_info(&client->dev,
			"flashed fw: core(0x%X), private(0x%X), public(0x%X)\n",
			fw_ver[0], fw_ver[1], fw_ver[2]);

	if (fw_ver[0] != info->fw_img->core_ver) {
		if (retries > 0) {
			dev_err(&client->dev, "flashed, but wrong ver."
					"retrying(%d)\n", retries);
			goto flash_fw;
		} else {
			dev_err(&client->dev, "flashing failure\n");
			ret = -3;
			goto err_fw_flash;
		}
	}

err_fw_flash:
do_not_need_update:
err_fw_ver_read:
	if (fw_file_type == BUILT_IN)
		mms_ts_fw_unload_built_in(info);
	else if (fw_file_type == UMS)
		mms_ts_fw_unload_ums(info);
err_fw_load:
	return ret;
}

static int mms_ts_read_raw_data_all(struct mms_ts_info *info, u8 mode,
									u8 type)
{
	struct i2c_client *client = info->client;
	u16 raw_temp;
	u8 cmd_addr;
	u8 result_addr;
	int ret;
	int i, j;
	int idx;
	int rx_num = TSP_CMD_Y_NUM;
	int tx_num = TSP_CMD_X_NUM;
	u16 *raw_data;

	if (mode == MMS_VSC) {
		u8 buf[] = {MMS_VSC_MODE, 0, 0, 0, type};
		cmd_addr = mode;
		result_addr = 0xBF;

		switch (type) {
		case VSC_INSPECTION:
		case VSC_CM_ABS:

			buf[4] = VSC_ENTER;
			ret = i2c_smbus_write_i2c_block_data(client,
						cmd_addr, ARRAY_SIZE(buf), buf);
			if (ret < 0) {
				dev_err(&client->dev,
					"fail to enter VSC mode (%d)\n", type);
				goto out;
			}

			while (gpio_get_value(info->pdata->gpio_int))
				udelay(100);

			buf[4] = type;

			if (type == VSC_INSPECTION)
				raw_data = info->raw_inspection;
			else if (type == VSC_CM_ABS)
				raw_data = info->raw_cm_abs;

			break;

		case VSC_INTENSITY:
			raw_data = info->raw_intensity;
			break;

		case VSC_EXIT:
			dev_info(&client->dev, "exit VSC mode (%d)\n", type);
			ret = 0;
			goto out;

		case VSC_RAW:
			dev_err(&client->dev,
					"invalid VSC mode (%d)\n", type);
			ret = -EINVAL;
			goto out;

		case VSC_REFERENCE:
			raw_data = info->raw_reference;
			break;

		default:
			dev_err(&client->dev,
					"invalid VSC mode (%d)\n", type);
			ret = -EINVAL;
			goto out;
		}

		for (i = 0; i < rx_num; i++) {
			for (j = 0; j < tx_num; j++) {
				buf[1] = j;
				buf[2] = i;
				ret = i2c_smbus_write_i2c_block_data(client,
						cmd_addr, ARRAY_SIZE(buf), buf);
				if (ret < 0) {
					dev_err(&client->dev,
						"fail to read %d, %d node's"
						"raw data (%d)\n", i, j, type);
					goto out;
				}

				ret = i2c_smbus_read_i2c_block_data(client,
						result_addr, sizeof(raw_temp),
						(u8 *)&raw_temp);
				if (ret < 0) {
					dev_err(&client->dev,
						"fail to read %d, %d node's"
						"raw data (%d)\n", i, j, type);
					goto out;
				}

				raw_temp = (type == VSC_REFERENCE) ?
						raw_temp >> 4 : raw_temp;
				idx = j * rx_num + i;
				raw_data[idx] = raw_temp;
			}
		}

	} else if (mode == MMS_SDP) {
		u8 buf[] = {SDP_ENTER_TEST_MODE, 0, 0};
		cmd_addr = mode;
		result_addr = MMS_SDP_RESULT;

		switch (type) {
		case SDP_CM_DELTA_ONE:
			raw_data = info->raw_inspection;
			break;
		case SDP_CM_ABS_ONE:
			raw_data = info->raw_cm_abs;
			break;
		default:
			dev_err(&client->dev,
					"invalid SDP mode (%d)\n", type);
			ret = -EINVAL;
			goto out;
		}

		ret = i2c_smbus_write_byte_data(client, cmd_addr, buf[0]);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to sned enter sdp commnad.\n");
			goto out;
		}

		while (gpio_get_value(info->pdata->gpio_int))
			udelay(100);

		buf[0] = type;
		ret = i2c_smbus_write_byte_data(info->client, cmd_addr, buf[0]);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to sned test type of sdp (%d).\n", type);
			goto out;
		}

		while (gpio_get_value(info->pdata->gpio_int))
			udelay(100);

		for (i = 0; i < rx_num; i++) {
			for (j = 0; j < tx_num; j++) {
				buf[1] = j;
				buf[2] = i;

				ret = i2c_smbus_write_i2c_block_data(client,
						cmd_addr, ARRAY_SIZE(buf), buf);
				if (ret < 0) {
					dev_err(&client->dev,
						"fail to read %d, %d node's"
						"raw data (%d)\n", i, j, type);
					goto out;
				}
				ret = i2c_smbus_read_i2c_block_data(client,
						result_addr, sizeof(raw_temp),
						(u8 *)&raw_temp);
				if (ret < 0) {
					dev_err(&client->dev,
						"fail to read %d, %d node's"
						"raw data (%d)\n", i, j, type);
					goto out;
				}

				idx = j * rx_num + i;
				raw_data[idx] = raw_temp;
			}
		}

	} else {
		dev_err(&client->dev, "%s: invalid mode (%d)\n",
			__func__, mode);
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
out:
	hw_reboot_normal(info);
	return ret;
}

static int mms_ts_read_raw_data_one(struct mms_ts_info *info, u8 mode,
						u8 type, u8 rx_idx, u8 tx_idx)
{
	struct i2c_client *client = info->client;
	u16 raw_temp;
	u8 cmd_addr;
	u8 result_addr;
	int ret;

	if (mode == MMS_VSC) {
		u8 buf[] = {MMS_VSC_MODE, 0, 0, 0, type};
		cmd_addr = mode;
		result_addr = 0xBF;

		switch (type) {
		case VSC_INSPECTION:
		case VSC_CM_ABS:
			buf[4] = VSC_ENTER;
			ret = i2c_smbus_write_i2c_block_data(client,
						cmd_addr, ARRAY_SIZE(buf), buf);
			if (ret < 0) {
				dev_err(&client->dev,
					"fail to enter VSC mode (%d)\n", type);
				goto out;
			}

			while (gpio_get_value(info->pdata->gpio_int))
				udelay(100);

			buf[4] = type;

			break;

		case VSC_INTENSITY:
			break;

		case VSC_EXIT:
			dev_info(&client->dev, "exit VSC mode (%d)\n", type);
			ret = 0;
			goto out;

		case VSC_RAW:
			dev_err(&client->dev,
					"invalid VSC mode (%d)\n", type);
			ret = -EINVAL;
			goto out;

		case VSC_REFERENCE:
			break;
		case VSC_INSPECTION_TK:
		case VSC_CM_ABS_TK:
		case VSC_INTENSITY_TK:
		case VSC_RAW_TK:
		case VSC_REFERENCE_TK:
			break;

		default:
			dev_err(&client->dev,
					"invalid VSC mode (%d)\n", type);
			ret = -EINVAL;
			goto out;
		}

		buf[1] = tx_idx;
		buf[2] = rx_idx;

		ret = i2c_smbus_write_i2c_block_data(client, cmd_addr,
							ARRAY_SIZE(buf), buf);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to read %d, %d node's raw data (%d)\n",
							rx_idx, tx_idx, type);
			goto out;
		}

		ret = i2c_smbus_read_i2c_block_data(client, result_addr,
					sizeof(raw_temp), (u8 *)&raw_temp);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to read %d, %d node's raw data (%d)\n",
							rx_idx, tx_idx, type);
			goto out;
		}
		dev_err(&client->dev, "raw_temp=%d\n", raw_temp);
		raw_temp = (type == VSC_REFERENCE) ? raw_temp >> 4 : raw_temp;
		ret = raw_temp;
		goto out;

	} else if (mode == MMS_SDP) {
		u8 buf[] = {SDP_ENTER_TEST_MODE, 0, 0};
		cmd_addr = mode;
		result_addr = MMS_SDP_RESULT;

		switch (type) {
		case SDP_CM_DELTA_ONE:
		case SDP_CM_ABS_ONE:
			break;
		default:
			dev_err(&client->dev,
					"invalid SDP mode (%d)\n", type);
			ret = -EINVAL;
			goto out;
		}

		ret = i2c_smbus_write_byte_data(client, cmd_addr, buf[0]);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to sned enter sdp commnad.\n");
			goto out;
		}

		while (gpio_get_value(info->pdata->gpio_int))
			udelay(100);

		buf[0] = type;
		ret = i2c_smbus_write_byte_data(info->client, cmd_addr, buf[0]);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to sned test type of sdp (%d).\n", type);
			goto out;
		}

		while (gpio_get_value(info->pdata->gpio_int))
			udelay(100);


		buf[1] = tx_idx;
		buf[2] = rx_idx;

		ret = i2c_smbus_write_i2c_block_data(client, cmd_addr,
							ARRAY_SIZE(buf), buf);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to read %d, %d node's raw data (%d)\n",
							rx_idx, tx_idx, type);
			goto out;
		}

		ret = i2c_smbus_read_i2c_block_data(client, result_addr,
					sizeof(raw_temp), (u8 *)&raw_temp);
		if (ret < 0) {
			dev_err(&client->dev,
				"fail to read %d, %d node's raw data (%d)\n",
							rx_idx, tx_idx, type);
			goto out;
		}
		dev_err(&client->dev, "raw_temp=%d\n", raw_temp);
		ret = raw_temp;
		goto out;

	} else {
		dev_err(&client->dev, "%s: invalid mode (%d)\n",
			__func__, mode);
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
out:
	if (type != VSC_INSPECTION_TK && type != VSC_CM_ABS_TK &&
		type != VSC_INTENSITY_TK && type != VSC_RAW_TK &&
		type != VSC_REFERENCE_TK)
		hw_reboot_normal(info);

	return ret;
}


#ifdef TSP_FACTORY
static void set_cmd_result(struct mms_ts_info *info, char *buff, int len)
{
	strncat(info->cmd_result, buff, len);
}

static ssize_t cmd_store(struct device *dev, struct device_attribute *devattr,
						const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	char *cur, *start, *end;
	char buff[TSP_CMD_STR_LEN] = {0, };
	int len, i;
	struct tsp_cmd *tsp_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;

	if (info->cmd_is_running == true) {
		dev_err(&client->dev, "%s: other cmd is running.\n", __func__);
		goto err_out;
	}

	/* check lock  */
	mutex_lock(&info->cmd_lock);
	info->cmd_is_running = true;
	mutex_unlock(&info->cmd_lock);

	info->cmd_state = RUNNING;

	for (i = 0; i < ARRAY_SIZE(info->cmd_param); i++)
		info->cmd_param[i] = 0;

	len = (int)count;
	if (*(buf + len - 1) == '\n')
		len--;
	memset(info->cmd, 0x00, ARRAY_SIZE(info->cmd));
	memcpy(info->cmd, buf, len);

	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(buff, buf, cur - buf);
	else
		memcpy(buff, buf, len);

	/* find command */
	list_for_each_entry(tsp_cmd_ptr, &info->cmd_list_head, list) {
		if (!strcmp(buff, tsp_cmd_ptr->cmd_name)) {
			cmd_found = true;
			break;
		}
	}

	/* set not_support_cmd */
	if (!cmd_found) {
		list_for_each_entry(tsp_cmd_ptr, &info->cmd_list_head, list) {
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
				info->cmd_param[param_cnt] =
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
							info->cmd_param[i]);

	tsp_cmd_ptr->cmd_func(info);

err_out:
	return count;
}

static ssize_t cmd_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	char buff[16];

	dev_info(&client->dev, "%s: check status:%d\n", __func__,
							info->cmd_state);

	switch (info->cmd_state) {
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
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_result, strlen(info->cmd_result));

	mutex_lock(&info->cmd_lock);
	info->cmd_is_running = false;
	mutex_unlock(&info->cmd_lock);

	info->cmd_state = WAITING;

	return sprintf(buf, "%s", info->cmd_result);
}

static void set_default_result(struct mms_ts_info *info)
{
	char delim = ':';

	memset(info->cmd_result, 0x00, ARRAY_SIZE(info->cmd_result));
	memset(info->cmd_buff, 0x00, ARRAY_SIZE(info->cmd_buff));
	memcpy(info->cmd_result, info->cmd, strlen(info->cmd));
	strncat(info->cmd_result, &delim, 1);
}


static void not_support_cmd(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	set_default_result(info);
	sprintf(info->cmd_buff, "%s", "NA");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = NOT_APPLICABLE;
	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));
	return;
}

static void fw_update(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	int ret;
	int fw_type = info->cmd_param[0];

	set_default_result(info);

	if (!info->enabled)
		goto out;

	if (fw_type != BUILT_IN && fw_type != UMS) {
		dev_err(&client->dev, "%s: invalid update type(%d)\n", __func__,
							info->cmd_param[0]);
		goto out;
	}

	disable_irq(info->irq);

	ret = mms_ts_fw_update(info, fw_type, ISP_FLASH, true);

	enable_irq(info->irq);

	sprintf(info->cmd_buff, "%s", (ret) ? "OK" : "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));

	if (ret >= 0)
		info->cmd_state = OK;
	else
		info->cmd_state = FAIL;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

out:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_info(&client->dev, "%s: fail to read fw ver\n", __func__);
	return ;
}

static void get_fw_ver_bin(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u16 buff;
	int ret;

	set_default_result(info);

	ret = mms_ts_fw_load_built_in(info);
	if (ret < 0) {
		dev_err(&client->dev,
			"fail to load firmware (%d)\n", ret);
		goto out;
	}
	buff = info->fw_img->core_ver;
	mms_ts_fw_unload_built_in(info);

	sprintf(info->cmd_buff, "0x%X", buff);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

out:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_info(&client->dev, "%s: fail to read fw ver\n", __func__);
	return ;
}

static void get_fw_ver_ic(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u8 fw_ver[3];
	int ret;

	set_default_result(info);

	ret = i2c_smbus_read_i2c_block_data(client, MMS_FW_VERSION, 3,
								(u8 *)fw_ver);
	if (ret < 0) {
		dev_err(&client->dev,
			"fail to read fw ver (%d)\n", ret);
		goto out;
	}

	sprintf(info->cmd_buff, "0x%X", fw_ver[0]);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

out:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;
	return ;
}

static void get_threshold(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u8 buff;

	set_default_result(info);

	buff = i2c_smbus_read_byte_data(info->client, MMS_THRESHOLD);
	if (buff < 0) {
		dev_err(&client->dev,
			"fail to read threshold (%d)\n", buff);
		goto out;
	}

	sprintf(info->cmd_buff, "%d", buff);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

out:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;
	return ;
}

static void module_off_master(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;

	set_default_result(info);

	gpio_direction_output(info->pdata->gpio_vdd_en, 0);

	sprintf(info->cmd_buff, "%s", "OK");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;
}

static void module_on_master(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;

	set_default_result(info);

	mms_pwr_on_reset(info);

	sprintf(info->cmd_buff, "%s", "OK");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;
}

static void get_chip_vendor(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;

	set_default_result(info);

	sprintf(info->cmd_buff, "%s", "MELFAS");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;
}

static void get_chip_name(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;

	set_default_result(info);

	sprintf(info->cmd_buff, "%s", "MMS136");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;
}

static void get_x_num(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
/*
	u8 buff;
*/
	set_default_result(info);
/*
	buff = i2c_smbus_read_byte_data(info->client, 0xEE);
	if (buff < 0) {
		dev_err(&client->dev,
			"fail to read the number of x(tx) (%d)\n", buff);
		goto out;
	}
	sprintf(info->cmd_buff, "0x%X", buff);
*/
	sprintf(info->cmd_buff, "%d", TSP_CMD_X_NUM);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;
/*
out:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;
	return ;
*/
}

static void get_y_num(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
/*
	u8 buff;
*/
	set_default_result(info);
/*
	buff = i2c_smbus_read_byte_data(info->client, 0xEE);
	if (buff < 0) {
		dev_err(&client->dev,
			"fail to read the number of y(rx) (%d)\n", buff);
		goto out;
	}
	sprintf(info->cmd_buff, "0x%X", buff);
*/
	sprintf(info->cmd_buff, "%d", TSP_CMD_Y_NUM);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;
/*
out:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;
	return ;
*/
}

static void run_reference_read(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u16 raw_min, raw_max, raw_cur;
	int ret;
	int i, j;

	memset(info->raw_reference, 0x00,
					sizeof(info->raw_reference[0]) *
					ARRAY_SIZE(info->raw_reference));

	set_default_result(info);

	dev_info(&client->dev, "%s: read\n", __func__);

	disable_irq(info->irq);

	ret = mms_ts_read_raw_data_all(info, MMS_VSC, VSC_REFERENCE);
	if (ret < 0)
		goto err_read;

	enable_irq(info->irq);

	raw_min = raw_max = info->raw_reference[0];

	for (i = 0; i < TSP_CMD_Y_NUM; i++) {
		for (j = 0; j < TSP_CMD_X_NUM; j++) {
			raw_cur = info->raw_reference[j * TSP_CMD_Y_NUM + i];

			if (raw_cur < raw_min)
				raw_min = raw_cur;

			if (raw_cur > raw_max)
				raw_max = raw_cur;
/*
			dev_info(&client->dev, "%s: %3d,%3d=%d\n", __func__, i,
				j, raw_cur);
			msleep(1);
*/
		}
	}

	sprintf(info->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));
	return;

err_read:
	enable_irq(info->irq);

	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static void run_cm_abs_read(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u16 raw_min, raw_max, raw_cur;
	int ret;
	int i, j;

	memset(info->raw_cm_abs, 0x00,
		sizeof(info->raw_cm_abs[0]) * ARRAY_SIZE(info->raw_cm_abs));

	set_default_result(info);

	dev_info(&client->dev, "%s: read\n", __func__);

	disable_irq(info->irq);
/*
	ret = mms_ts_read_raw_data_all(info, MMS_VSC, VSC_CM_ABS);
*/
	ret = mms_ts_read_raw_data_all(info, MMS_SDP, SDP_CM_ABS_ONE);
	if (ret < 0)
		goto err_read;

	enable_irq(info->irq);

	raw_min = raw_max = info->raw_cm_abs[0];

	for (i = 0; i < TSP_CMD_Y_NUM; i++) {
		for (j = 0; j < TSP_CMD_X_NUM; j++) {
			raw_cur = info->raw_cm_abs[j * TSP_CMD_Y_NUM + i];

			if (raw_cur < raw_min)
				raw_min = raw_cur;

			if (raw_cur > raw_max)
				raw_max = raw_cur;
/*
			dev_info(&client->dev, "%s: %3d,%3d=%d\n", __func__, i,
				j, raw_cur);
			msleep(1);
*/
		}
	}

	sprintf(info->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));
	return;

err_read:
	enable_irq(info->irq);

	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static void run_inspection_read(void *device_data) /* same as cm_delta*/
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u16 raw_min, raw_max, raw_cur;
	int ret;
	int i, j;

	memset(info->raw_inspection, 0x00,
					sizeof(info->raw_inspection[0]) *
					ARRAY_SIZE(info->raw_inspection));

	set_default_result(info);

	dev_info(&client->dev, "%s: read\n", __func__);

	disable_irq(info->irq);
/*
	ret = mms_ts_read_raw_data_all(info, MMS_VSC, VSC_INSPECTION);
*/
	ret = mms_ts_read_raw_data_all(info, MMS_SDP, SDP_CM_DELTA_ONE);
	if (ret < 0)
		goto err_read;

	enable_irq(info->irq);

	raw_min = raw_max = info->raw_inspection[0];

	for (i = 0; i < TSP_CMD_Y_NUM; i++) {
		for (j = 0; j < TSP_CMD_X_NUM; j++) {
			raw_cur = info->raw_inspection[j * TSP_CMD_Y_NUM + i];

			if (raw_cur < raw_min)
				raw_min = raw_cur;

			if (raw_cur > raw_max)
				raw_max = raw_cur;
/*
			dev_info(&client->dev, "%s: %3d,%3d=%d\n", __func__, i,
				j, raw_cur);
			msleep(1);
*/
		}
	}

	sprintf(info->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));
	return;

err_read:
	enable_irq(info->irq);

	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static void run_intensity_read(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	u16 raw_min, raw_max, raw_cur;
	int ret;
	int i, j;

	memset(info->raw_intensity, 0x00,
					sizeof(info->raw_intensity[0]) *
					ARRAY_SIZE(info->raw_intensity));

	set_default_result(info);

	dev_info(&client->dev, "%s: read\n", __func__);

	disable_irq(info->irq);

	ret = mms_ts_read_raw_data_all(info, MMS_VSC, VSC_INTENSITY);
	if (ret < 0)
		goto err_read;

	enable_irq(info->irq);

	raw_min = raw_max = info->raw_intensity[0];

	for (i = 0; i < TSP_CMD_Y_NUM; i++) {
		for (j = 0; j < TSP_CMD_X_NUM; j++) {
			raw_cur = info->raw_intensity[j * TSP_CMD_Y_NUM + i];

			if (raw_cur < raw_min)
				raw_min = raw_cur;

			if (raw_cur > raw_max)
				raw_max = raw_cur;
/*
			dev_info(&client->dev, "%s: %3d,%3d=%d\n", __func__, i,
				j, raw_cur);
			msleep(1);
*/
		}
	}

	sprintf(info->cmd_buff, "%d,%d", raw_min, raw_max);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));
	return;

err_read:
	enable_irq(info->irq);

	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static void get_reference(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx;

	set_default_result(info);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM) {
		dev_err(&client->dev, "%s: invalid index.\n", __func__);
		goto err_idx;
	}

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(info->cmd_buff, "%d", info->raw_reference[idx]);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

err_idx:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;

}

static void get_cm_abs(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx;

	set_default_result(info);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM) {
		dev_err(&client->dev, "%s: invalid index.\n", __func__);
		goto err_idx;
	}

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(info->cmd_buff, "%d", info->raw_cm_abs[idx]);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

err_idx:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static void get_inspection(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx;

	set_default_result(info);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM) {
		dev_err(&client->dev, "%s: invalid index.\n", __func__);
		goto err_idx;
	}

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(info->cmd_buff, "%d", info->raw_inspection[idx]);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

err_idx:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static void get_intensity(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	struct i2c_client *client = info->client;
	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx;

	set_default_result(info);

	if (x < 0 || x >= TSP_CMD_X_NUM || y < 0 || y >= TSP_CMD_Y_NUM) {
		dev_err(&client->dev, "%s: invalid index.\n", __func__);
		goto err_idx;
	}

	idx = x * TSP_CMD_Y_NUM + y;

	sprintf(info->cmd_buff, "%d", info->raw_intensity[idx]);
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__,
				info->cmd_buff,	strlen(info->cmd_buff));

	return;

err_idx:
	sprintf(info->cmd_buff, "%s", "NG");
	set_cmd_result(info, info->cmd_buff, strlen(info->cmd_buff));
	info->cmd_state = FAIL;

	dev_err(&client->dev, "%s: failure.\n", __func__);
	return;
}

static ssize_t fw_ver_kernel_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 buff;

	mms_ts_fw_load_built_in(info);
	mms_ts_fw_unload_built_in(info);

	buff = info->fw_img->core_ver;

	dev_info(&client->dev, "%s: \"0x%X\"\n", __func__, buff);
	return sprintf(buf, "0x%X\n", buff);
}

static ssize_t fw_ver_ic_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 fw_ver[3];

	i2c_smbus_read_i2c_block_data(client, MMS_FW_VERSION, 3, (u8 *)fw_ver);

	dev_info(&client->dev, "%s: \"0x%X\"\n", __func__, fw_ver[0]);
	return sprintf(buf, "0x%X\n", fw_ver[0]);

}

static ssize_t threshold_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 buff;

	buff = i2c_smbus_read_byte_data(info->client, MMS_THRESHOLD);

	dev_info(&client->dev, "%s: \"%d\"\n", __func__, buff);
	return sprintf(buf, "%d\n", buff);
}
#endif

#ifdef TSK_FACTORY
static ssize_t back_key_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	int i;

	for (i = 0; i < info->pdata->key_nums; i++) {
		if (info->pdata->key_map[i] == KEY_BACK)
			break;
	}

	dev_info(&client->dev, "back key state: %d\n", info->key_pressed[i]);

	return snprintf(buf, sizeof(bool), "%d\n", info->key_pressed[i]);
}

static ssize_t menu_key_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	int i;

	for (i = 0; i < info->pdata->key_nums; i++) {
		if (info->pdata->key_map[i] == KEY_MENU)
			break;
	}

	dev_info(&client->dev, "back key state: %d\n", info->key_pressed[i]);

	return snprintf(buf, sizeof(bool), "%d\n", info->key_pressed[i]);
}

static ssize_t tkey_rawcounter_show0(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	int ret;
	u16 val;

	/* menu key*/
	disable_irq(info->irq);

	ret = mms_ts_read_raw_data_one(info, MMS_VSC, VSC_INTENSITY_TK, 0, 0);
	if (ret < 0)
		dev_err(&client->dev, "%s: fail to read (%d)\n", __func__, ret);

	enable_irq(info->irq);
	val = (u16)ret;

	dev_info(&client->dev, "%s: val=%d\n", __func__, val);

	return sprintf(buf, "%d\n", val);
}

static ssize_t tkey_rawcounter_show1(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	int ret;
	u16 val;

	/* menu key*/
	disable_irq(info->irq);

	ret = mms_ts_read_raw_data_one(info, MMS_VSC, VSC_INTENSITY_TK, 0, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s: fail to read (%d)\n", __func__, ret);

	enable_irq(info->irq);
	val = (u16)ret;

	dev_info(&client->dev, "%s: val=%d\n", __func__, val);

	return sprintf(buf, "%d\n", val);
}


#endif

#ifdef TSK_FACTORY
static DEVICE_ATTR(touchkey_back, S_IRUGO, back_key_state_show, NULL);
static DEVICE_ATTR(touchkey_menu, S_IRUGO, menu_key_state_show, NULL);
static DEVICE_ATTR(touchkey_raw_data0, S_IRUGO, tkey_rawcounter_show0, NULL) ;
static DEVICE_ATTR(touchkey_raw_data1, S_IRUGO, tkey_rawcounter_show1, NULL) ;

static struct attribute *touchkey_attributes[] = {
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_menu.attr,
	&dev_attr_touchkey_raw_data0.attr,
	&dev_attr_touchkey_raw_data1.attr,
	NULL,
};
static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};

static int factory_init_tk(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int ret;

	info->dev_tk = device_create(sec_class, NULL, (dev_t)NULL, info,
								"sec_touchkey");
	if (IS_ERR(info->dev_tk)) {
		dev_err(&client->dev, "Failed to create fac touchkey dev\n");
		ret = -ENODEV;
		info->dev_tk = NULL;
		goto err_create_dev_tk;
	}

	ret = sysfs_create_group(&info->dev_tk->kobj, &touchkey_attr_group);
	if (ret) {
		dev_err(&client->dev,
			"Failed to create sysfs (touchkey_attr_group).\n");
		ret = (ret > 0) ? -ret : ret;
		goto err_create_tk_sysfs;
	}

	info->key_pressed = kzalloc(sizeof(bool) * info->pdata->key_nums,
								GFP_KERNEL);
	if (!info->key_pressed) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	return 0;

err_alloc:
	sysfs_remove_group(&info->dev_tk->kobj, &touchkey_attr_group);
err_create_tk_sysfs:
err_create_dev_tk:
	return ret;
}
#endif

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

static DEVICE_ATTR(tsp_firm_version_panel, S_IRUGO, fw_ver_ic_temp_show, NULL);
static DEVICE_ATTR(tsp_firm_version_phone, S_IRUGO, fw_ver_kernel_temp_show,
									NULL);
static DEVICE_ATTR(tsp_threshold, S_IRUGO, threshold_temp_show, NULL);


static struct attribute *touchscreen_temp_attributes[] = {
	&dev_attr_tsp_firm_version_panel.attr,
	&dev_attr_tsp_firm_version_phone.attr,
	&dev_attr_tsp_threshold.attr,
	NULL,
};

static struct attribute_group touchscreen_temp_attr_group = {
	.attrs = touchscreen_temp_attributes,
};

static int factory_init_ts(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int i;
	int ret;

	mutex_init(&info->cmd_lock);
	info->cmd_is_running = false;

	INIT_LIST_HEAD(&info->cmd_list_head);
	for (i = 0; i < ARRAY_SIZE(tsp_cmds); i++)
		list_add_tail(&tsp_cmds[i].list, &info->cmd_list_head);

	info->dev_ts = device_create(sec_class, NULL, (dev_t)NULL, info, "tsp");
	if (IS_ERR(info->dev_ts)) {
		dev_err(&client->dev, "Failed to create fac tsp dev\n");
		ret = -ENODEV;
		info->dev_ts = NULL;
		goto err_create_dev_ts;
	}

	ret = sysfs_create_group(&info->dev_ts->kobj, &touchscreen_attr_group);
	if (ret) {
		dev_err(&client->dev,
			"Failed to create sysfs (touchscreen_attr_group).\n");
		ret = (ret > 0) ? -ret : ret;
		goto err_create_ts_sysfs;
	}

	info->dev_ts_temp = device_create(sec_class, NULL, (dev_t)NULL, info,
							"sec_touchscreen");
	if (IS_ERR(info->dev_ts_temp)) {
		dev_err(&client->dev, "Failed to create fac tsp temp dev\n");
		ret = -ENODEV;
		info->dev_ts_temp = NULL;
		goto err_create_dev_ts_temp;
	}
	ret = sysfs_create_group(&info->dev_ts_temp->kobj,
						&touchscreen_temp_attr_group);
	if (ret) {
		dev_err(&client->dev,
			"Failed to create sysfs (touchscreen_temp_attr_group)."
									"\n");
		ret = (ret > 0) ? -ret : ret;
		goto err_create_ts_temp_sysfs;
	}

	return 0;

err_create_ts_temp_sysfs:
err_create_dev_ts_temp:
	sysfs_remove_group(&info->dev_ts->kobj, &touchscreen_attr_group);
err_create_ts_sysfs:
err_create_dev_ts:
	mutex_destroy(&info->cmd_lock);
	return ret;
}
#endif

static int __devinit mms_ts_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mms_ts_info *info;
	struct input_dev *input_dev_ts;
	struct input_dev *input_dev_tk;
	int ret = 0;
	int i;

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "no platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"need i2c bus that supports protocol mangling\n");
		return -ENODEV;
	}

	info = kzalloc(sizeof(struct mms_ts_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	info->pdata = client->dev.platform_data;

	input_dev_ts = input_allocate_device();
	if (!input_dev_ts) {
		dev_err(&client->dev,
			"Failed to allocate input device (touchscreen)\n");
		goto err_alloc_input_dev_ts;
	}

	input_dev_tk = input_allocate_device();
	if (!input_dev_tk) {
		dev_err(&client->dev,
			"Failed to allocate input device (touch key)\n");
		goto err_alloc_input_dev_tk;
	}

	info->client = client;

	info->irq = -1;
	mutex_init(&info->lock);

	ret = input_mt_init_slots(input_dev_ts, MAX_FINGERS);
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to init slots (%d)\n", ret);
		goto err_init_slots;
	}

	info->input_dev_ts = input_dev_ts;
	snprintf(info->phys_ts, sizeof(info->phys_ts), "%s/input0",
						dev_name(&client->dev));
	input_dev_ts->name = "sec_touchscreen";
	input_dev_ts->phys = info->phys_ts;
	input_dev_ts->id.bustype = BUS_I2C;
	input_dev_ts->dev.parent = &client->dev;
	__set_bit(EV_ABS, input_dev_ts->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_dev_ts->propbit);

	input_set_abs_params(input_dev_ts, ABS_MT_POSITION_X, 0,
						info->pdata->max_x, 0, 0);
	input_set_abs_params(input_dev_ts, ABS_MT_POSITION_Y, 0,
						info->pdata->max_y, 0, 0);
	input_set_abs_params(input_dev_ts, ABS_MT_TOUCH_MAJOR, 0,
						MAX_WIDTH, 0, 0);
	input_set_abs_params(input_dev_ts, ABS_MT_PRESSURE, 0,
						MAX_WIDTH, 0, 0);
	input_set_drvdata(input_dev_ts, info);
	ret = input_register_device(input_dev_ts);
	if (ret) {
		dev_err(&client->dev,
			"failed to register input dev (touchscreen) (%d)\n",
									ret);
		goto err_reg_input_dev_ts;
	}

	info->input_dev_tk = input_dev_tk;
	snprintf(info->phys_tk, sizeof(info->phys_tk), "%s/input0",
						dev_name(&client->dev));
	input_dev_tk->name = "sec_touchkey";
	input_dev_tk->phys = info->phys_tk;
	input_dev_tk->id.bustype = BUS_I2C;
	input_dev_tk->dev.parent = &client->dev;

	set_bit(EV_KEY, input_dev_tk->evbit);
	set_bit(EV_LED, input_dev_tk->evbit);
	set_bit(LED_MISC, input_dev_tk->ledbit);
	set_bit(EV_SYN, input_dev_tk->evbit);

	for (i = 0; i < info->pdata->key_nums; i++)
		__set_bit(info->pdata->key_map[i], input_dev_tk->keybit);

	input_set_drvdata(input_dev_tk, info);

	ret = input_register_device(input_dev_tk);
	if (ret) {
		dev_err(&client->dev,
			"failed to register input dev (touchscreen) (%d)\n",
									ret);
		goto err_reg_input_dev_tk;
	}

	i2c_set_clientdata(client, info);

	mms_pwr_on_reset(info);

	ret = mms_ts_fw_update(info, BUILT_IN, ISP_FLASH, false);
	if (ret < 0) {
		dev_err(&client->dev,
			"failed to initialize device (%d)\n", ret);
		goto err_init_device;
	}

	ret = request_threaded_irq(client->irq, NULL, mms_ts_interrupt,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "mms_ts", info);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_req_irq;
	}

	info->irq = client->irq;
	info->enabled = true;

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->early_suspend.suspend = mms_ts_early_suspend;
	info->early_suspend.resume = mms_ts_late_resume;
	register_early_suspend(&info->early_suspend);
#endif

	ret = factory_init_ts(info);
	if (ret < 0) {
		dev_err(&client->dev, "failed to init factory init (ts)\n");
		goto err_factory_init;
	}

	ret = factory_init_tk(info);
	if (ret < 0) {
		dev_err(&client->dev, "failed to init factory init (tk)\n");
		goto err_factory_init;
	}

	dev_info(&client->dev, "%s: finish.\n", __func__);

	return 0;

err_factory_init:
	free_irq(info->irq, info);
err_req_irq:
err_init_device:
	input_unregister_device(input_dev_tk);
	input_dev_tk = NULL;
err_reg_input_dev_tk:
	input_unregister_device(input_dev_ts);
	input_dev_ts = NULL;
err_reg_input_dev_ts:
	input_mt_destroy_slots(input_dev_ts);
err_init_slots:
	input_free_device(input_dev_tk);
err_alloc_input_dev_tk:
	input_free_device(input_dev_ts);
err_alloc_input_dev_ts:
	gpio_set_value(info->pdata->gpio_vdd_en, 0);
	kfree(info);
err_alloc:
	return ret;
}

static int mms_ts_enable(struct mms_ts_info *info)
{
	mutex_lock(&info->lock);
	if (info->enabled)
		goto out;
	/* wake up the touch controller. */

	/* wake up command */
/*
	i2c_smbus_write_byte_data(info->client, 0, 0);
	msleep(5);
*/

	info->pdata->pin_set_pull(info->pdata->gpio_scl, true);
	info->pdata->pin_set_pull(info->pdata->gpio_sda, true);
	info->pdata->pin_set_pull(info->pdata->gpio_int, true);

	gpio_set_value(info->pdata->gpio_vdd_en, 1);
	msleep(100);

	info->enabled = true;
	enable_irq(info->irq);
out:
	mutex_unlock(&info->lock);
	return 0;
}

static int mms_ts_disable(struct mms_ts_info *info)
{
	mutex_lock(&info->lock);
	if (!info->enabled)
		goto out;
	disable_irq(info->irq);

	/* sleep command */
/*
	i2c_smbus_write_byte_data(info->client, MMS_MODE_CONTROL, 0);
	msleep(12);
*/
	gpio_set_value(info->pdata->gpio_vdd_en, 0);

	info->pdata->pin_set_pull(info->pdata->gpio_int, false);
	info->pdata->pin_set_pull(info->pdata->gpio_scl, false);
	info->pdata->pin_set_pull(info->pdata->gpio_sda, false);

	info->enabled = false;
out:
	mutex_unlock(&info->lock);
	return 0;
}

static int __devexit mms_ts_remove(struct i2c_client *client)
{
	struct mms_ts_info *info = i2c_get_clientdata(client);

	if (info->irq >= 0)
		free_irq(info->irq, info);

	input_mt_destroy_slots(info->input_dev_ts);
	input_unregister_device(info->input_dev_ts);
	input_unregister_device(info->input_dev_tk);
#ifdef TSK_FACTORY
	sysfs_remove_group(&info->dev_tk->kobj, &touchkey_attr_group);
	kfree(info->key_pressed);
#endif

#ifdef TSP_FACTORY
	sysfs_remove_group(&info->dev_ts->kobj, &touchscreen_attr_group);
	sysfs_remove_group(&info->dev_ts_temp->kobj,
						&touchscreen_temp_attr_group);
	mutex_destroy(&info->cmd_lock);
#endif
	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
static int mms_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms_ts_info *info = i2c_get_clientdata(client);
	int i;

	dev_info(&info->client->dev, "%s: ts users=%d, tk users=%d\n",
		__func__, info->input_dev_ts->users,
		info->input_dev_tk->users);

	mutex_lock(&info->input_dev_ts->mutex);
	if (!info->input_dev_ts->users)
		goto out;

	mms_ts_disable(info);

	for (i = 0; i < MAX_FINGERS; i++) {
		int mt_val;

		input_mt_slot(info->input_dev_ts, i);
		mt_val = input_mt_get_value(&info->input_dev_ts->
				mt[info->input_dev_ts->slot], ABS_MT_TRACKING_ID);

		if ((info->finger_state[i] == 1 && mt_val == -1) ||
				(info->finger_state[i] == 0 && mt_val != -1))
			dev_err(&client->dev, "mismatch occured. (%d)\n", i);

		if (info->finger_state[i] == 1) {
			info->finger_state[i] = 0;
			dev_info(&client->dev, "%4s[%d]: \n", "up/f", i);
		}

		input_mt_slot(info->input_dev_ts, i);
		input_mt_report_slot_state(info->input_dev_ts, MT_TOOL_FINGER,
					   false);
	}

	input_sync(info->input_dev_ts);
out:
	mutex_unlock(&info->input_dev_ts->mutex);
	return 0;
}

static int mms_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms_ts_info *info = i2c_get_clientdata(client);
	int ret = 0;

	dev_info(&info->client->dev, "%s: ts users=%d, tk users=%d\n",
		__func__, info->input_dev_ts->users,
		info->input_dev_tk->users);
	mutex_lock(&info->input_dev_ts->mutex);
	if (info->input_dev_ts->users)
		ret = mms_ts_enable(info);
	mutex_unlock(&info->input_dev_ts->mutex);

	return ret;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mms_ts_early_suspend(struct early_suspend *h)
{
	struct mms_ts_info *info;
	info = container_of(h, struct mms_ts_info, early_suspend);
	mms_ts_suspend(&info->client->dev);
}

static void mms_ts_late_resume(struct early_suspend *h)
{
	struct mms_ts_info *info;
	info = container_of(h, struct mms_ts_info, early_suspend);

	mms_ts_resume(&info->client->dev);
}
#endif

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops mms_ts_pm_ops = {
	.suspend	= mms_ts_suspend,
	.resume		= mms_ts_resume,
};
#endif

static const struct i2c_device_id mms_ts_id[] = {
	{ "mms_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mms_ts_id);

static struct i2c_driver mms_ts_driver = {
	.probe		= mms_ts_probe,
	.remove		= __devexit_p(mms_ts_remove),
	.driver = {
		.name = "mms_ts",
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm	= &mms_ts_pm_ops,
#endif
	},
	.id_table	= mms_ts_id,
};

static int __init mms_ts_init(void)
{
	return i2c_add_driver(&mms_ts_driver);
}

static void __exit mms_ts_exit(void)
{
	i2c_del_driver(&mms_ts_driver);
}

module_init(mms_ts_init);
module_exit(mms_ts_exit);

/* Module information */
MODULE_DESCRIPTION("Touchscreen driver for Melfas MMS-series controllers");
MODULE_LICENSE("GPL");
