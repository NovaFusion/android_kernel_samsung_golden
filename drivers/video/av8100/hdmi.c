/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson HDMI driver
 *
 * Author: Per Persson <per.xb.persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <video/av8100.h>
#include <video/hdmi.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include "hdmi_loc.h"
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/switch.h>

#define SYSFS_EVENT_FILENAME "evread"
#define HDMI_DEVNR_DEFAULT	0

DEFINE_MUTEX(hdmi_events_mutex);
#define LOCK_HDMI_EVENTS mutex_lock(&hdmi_events_mutex)
#define UNLOCK_HDMI_EVENTS mutex_unlock(&hdmi_events_mutex)
#define EVENTS_MASK 0xFF

struct hdmi_device {
	struct list_head	list;
	struct miscdevice	miscdev;
	struct device		*dev;
	struct hdmi_sysfs_data	sysfs_data;
	int			events;
	int			events_mask;
	wait_queue_head_t	event_wq;
	bool			events_received;
	int			devnr;
	struct switch_dev switch_hdmi_detection;
};

/* List of devices */
static LIST_HEAD(hdmi_device_list);

static ssize_t store_storeastext(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t store_plugdeten(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t store_edidread(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_edidread(struct device *dev, struct device_attribute *attr,
				char *buf);
static ssize_t store_ceceven(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_cecread(struct device *dev, struct device_attribute *attr,
				char *buf);
static ssize_t store_cecsend(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t store_infofrsend(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t store_hdcpeven(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_hdcpchkaesotp(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_hdcpfuseaes(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_hdcpfuseaes(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_hdcploadaes(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_hdcploadaes(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_hdcpauthencr(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_hdcpauthencr(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_hdcpstateget(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_evread(struct device *dev, struct device_attribute *attr,
				char *buf);
static ssize_t store_evclr(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t store_audiocfg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_plugstatus(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_poweronoff(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_poweronoff(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_evwakeup(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static const struct device_attribute hdmi_sysfs_attrs[] = {
	__ATTR(storeastext, S_IWUSR, NULL, store_storeastext),
	__ATTR(plugdeten, S_IWUSR, NULL, store_plugdeten),
	__ATTR(edidread, S_IRUGO | S_IWUSR, show_edidread, store_edidread),
	__ATTR(ceceven, S_IWUSR, NULL, store_ceceven),
	__ATTR(cecread, S_IRUGO, show_cecread, NULL),
	__ATTR(cecsend, S_IWUSR, NULL, store_cecsend),
	__ATTR(infofrsend, S_IWUSR, NULL, store_infofrsend),
	__ATTR(hdcpeven, S_IWUSR, NULL, store_hdcpeven),
	__ATTR(hdcpchkaesotp, S_IRUGO, show_hdcpchkaesotp, NULL),
	__ATTR(hdcpfuseaes, S_IRUGO | S_IWUSR, show_hdcpfuseaes,
			store_hdcpfuseaes),
	__ATTR(hdcploadaes, S_IRUGO | S_IWUSR, show_hdcploadaes,
			store_hdcploadaes),
	__ATTR(hdcpauthencr, S_IRUGO | S_IWUSR, show_hdcpauthencr,
			store_hdcpauthencr),
	__ATTR(hdcpstateget, S_IRUGO, show_hdcpstateget, NULL),
	__ATTR(evread, S_IRUGO, show_evread, NULL),
	__ATTR(evclr, S_IWUSR, NULL, store_evclr),
	__ATTR(audiocfg, S_IWUSR, NULL, store_audiocfg),
	__ATTR(plugstatus, S_IRUGO, show_plugstatus, NULL),
	__ATTR(poweronoff, S_IRUGO | S_IWUSR, show_poweronoff,
			store_poweronoff),
	__ATTR(evwakeup, S_IWUSR, NULL, store_evwakeup),
	__ATTR_NULL
};

/* Hex to int conversion */
static unsigned int htoi(const char *ptr)
{
	unsigned int value = 0;
	char ch;

	if (!ptr)
		return 0;

	ch = *ptr;
	if (isdigit(ch))
		value = ch - '0';
	else
		value = toupper(ch) - 'A' + 10;

	value <<= 4;
	ch = *(++ptr);

	if (isdigit(ch))
		value += ch - '0';
	else
		value += toupper(ch) - 'A' + 10;

	return value;
}

static struct hdmi_device *dev_to_hdev(struct device *dev)
{
	/* Get device from list of devices */
	struct list_head *element;
	struct hdmi_device *hdmi_dev;
	int cnt = 0;

	list_for_each(element, &hdmi_device_list) {
		hdmi_dev = list_entry(element, struct hdmi_device, list);
		if (hdmi_dev->dev == dev)
			return hdmi_dev;
		cnt++;
	}

	return NULL;
}

static struct hdmi_device *devnr_to_hdev(int devnr)
{
	/* Get device from list of devices */
	struct list_head *element;
	struct hdmi_device *hdmi_dev;
	int cnt = 0;

	list_for_each(element, &hdmi_device_list) {
		hdmi_dev = list_entry(element, struct hdmi_device, list);
		if (cnt == devnr)
			return hdmi_dev;
		cnt++;
	}

	return NULL;
}

static int event_enable(struct hdmi_device *hdev, bool enable,
		enum hdmi_event ev)
{
	struct kobject *kobj = &hdev->dev->kobj;

	dev_dbg(hdev->dev, "enable_event %d %02x\n", enable, ev);
	if (enable)
		hdev->events_mask |= ev;
	else
		hdev->events_mask &= ~ev;

	if (hdev->events & ev) {
		/* Report pending event */
		/* Wake up application waiting for event via call to poll() */
		sysfs_notify(kobj, NULL, SYSFS_EVENT_FILENAME);

		LOCK_HDMI_EVENTS;
		hdev->events_received = true;
		UNLOCK_HDMI_EVENTS;

		wake_up_interruptible(&hdev->event_wq);
	}

	return 0;
}

static int plugdeten(struct hdmi_device *hdev, struct plug_detect *pldet)
{
	struct av8100_status status;
	u8 denc_off_time = 0;
	int retval;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY) {
		if (av8100_powerup() != 0) {
			dev_err(hdev->dev, "av8100_powerup failed\n");
			return -EINVAL;
		}
	}

	event_enable(hdev, pldet->hdmi_detect_enable != 0,
		HDMI_EVENT_HDMI_PLUGIN);
	event_enable(hdev, pldet->hdmi_detect_enable != 0,
		HDMI_EVENT_HDMI_PLUGOUT);

	av8100_reg_hdmi_5_volt_time_r(&denc_off_time, NULL, NULL);

	retval = av8100_reg_hdmi_5_volt_time_w(
			denc_off_time,
			pldet->hdmi_off_time,
			pldet->on_time);

	if (retval) {
		dev_err(hdev->dev, "Failed to write the value to av8100 "
			"register\n");
		return -EFAULT;
	}

	return retval;
}

static int edidread(struct hdmi_device *hdev, struct edid_read *edidread,
			u8 *len, u8 *data)
{
	union av8100_configuration config;
	struct av8100_status status;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
			return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	config.edid_section_readback_format.address = edidread->address;
	config.edid_section_readback_format.block_number = edidread->block_nr;

	dev_dbg(hdev->dev, "addr:%0x blnr:%0x",
		config.edid_section_readback_format.address,
		config.edid_section_readback_format.block_number);

	if (av8100_conf_prep(AV8100_COMMAND_EDID_SECTION_READBACK,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	if (av8100_conf_w(AV8100_COMMAND_EDID_SECTION_READBACK,
		len, data, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}

	dev_dbg(hdev->dev, "len:%0x\n", *len);

	return 0;
}

static int cecread(struct hdmi_device *hdev, u8 *src, u8 *dest, u8 *data_len,
			u8 *data)
{
	union av8100_configuration config;
	struct av8100_status status;
	u8 buf_len;
	u8 buff[HDMI_CEC_READ_MAXSIZE];

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
		return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	if (av8100_conf_prep(AV8100_COMMAND_CEC_MESSAGE_READ_BACK,
			&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	if (av8100_conf_w(AV8100_COMMAND_CEC_MESSAGE_READ_BACK,
		&buf_len, buff, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}

	if (buf_len > 0) {
		*src = (buff[0] & 0xF0) >> 4;
		*dest = buff[0] & 0x0F;
		*data_len = buf_len - 1;
		memcpy(data, &buff[1], buf_len - 1);
	} else
		*data_len = 0;

	return 0;
}

/* CEC tx status can be set or read */
static bool cec_tx_status(struct hdmi_device *hdev,
				enum cec_tx_status_action action)
{
	static bool cec_tx_busy;

	switch (action) {
	case CEC_TX_SET_FREE:
		cec_tx_busy = false;
		dev_dbg(hdev->dev, "cec_tx_busy set:%d\n", cec_tx_busy);
		break;

	case CEC_TX_SET_BUSY:
		cec_tx_busy = true;
		dev_dbg(hdev->dev, "cec_tx_busy set:%d\n", cec_tx_busy);
		break;

	case CEC_TX_CHECK:
	default:
		dev_dbg(hdev->dev, "cec_tx_busy chk:%d\n", cec_tx_busy);
		break;
	}

	return cec_tx_busy;
}

static int cecsend(struct hdmi_device *hdev, u8 src, u8 dest, u8 data_len,
			u8 *data)
{
	union av8100_configuration config;
	struct av8100_status status;
	int cnt;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
		return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	config.cec_message_write_format.buffer[0] = ((src & 0x0F) << 4) +
			(dest & 0x0F);
	config.cec_message_write_format.buffer_length = data_len + 1;
	memcpy(&config.cec_message_write_format.buffer[1], data, data_len);

	if (av8100_conf_prep(AV8100_COMMAND_CEC_MESSAGE_WRITE,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	cnt = 0;
	while ((cnt < CECTX_TRY) && cec_tx_status(hdev, CEC_TX_CHECK)) {
		/* Wait for pending CEC to be finished */
		msleep(CECTX_WAITTIME);
		cnt++;
	}
	dev_dbg(hdev->dev, "cectxcnt:%d\n", cnt);

	if (av8100_conf_w(AV8100_COMMAND_CEC_MESSAGE_WRITE,
		NULL, NULL, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}
	cec_tx_status(hdev, CEC_TX_SET_BUSY);

	return 0;
}

static int infofrsend(struct hdmi_device *hdev, u8 type, u8 version, u8 crc,
			u8 data_len, u8 *data)
{
	union av8100_configuration config;
	struct av8100_status status;
	int ret = 0;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
		return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	if ((data_len < 1) || (data_len > HDMI_INFOFRAME_MAX_SIZE))
		return -EINVAL;

	config.infoframes_format.type = type;
	config.infoframes_format.version = version;
	config.infoframes_format.crc = crc;
	config.infoframes_format.length = data_len;
	memcpy(&config.infoframes_format.data, data, data_len);
	av8100_conf_lock();
	if (av8100_conf_prep(AV8100_COMMAND_INFOFRAMES,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		ret = -EINVAL;
		goto infofrsend_end;
	}

	if (av8100_conf_w(AV8100_COMMAND_INFOFRAMES,
		NULL, NULL, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		ret = -EINVAL;
		goto infofrsend_end;
	}

infofrsend_end:
	av8100_conf_unlock();
	return ret;
}

static int hdcpchkaesotp(struct hdmi_device *hdev, u8 *crc, u8 *progged)
{
	union av8100_configuration config;
	struct av8100_status status;
	u8 buf_len;
	u8 buf[2];

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
		return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	config.fuse_aes_key_format.fuse_operation = AV8100_FUSE_READ;
	memset(config.fuse_aes_key_format.key, 0, AV8100_FUSE_KEY_SIZE);
	if (av8100_conf_prep(AV8100_COMMAND_FUSE_AES_KEY,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	if (av8100_conf_w(AV8100_COMMAND_FUSE_AES_KEY,
		&buf_len, buf, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}

	if (buf_len == 2) {
		*crc = buf[0];
		*progged = buf[1];
	}

	return 0;
}

static int hdcpfuseaes(struct hdmi_device *hdev, u8 *key, u8 crc, u8 *result)
{
	union av8100_configuration config;
	struct av8100_status status;
	u8 buf_len;
	u8 buf[2];

	/* Default not OK */
	*result = HDMI_RESULT_NOT_OK;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY) {
		if (av8100_powerup() != 0) {
			dev_err(hdev->dev, "av8100_powerup failed\n");
			return -EINVAL;
		}
	}

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	config.fuse_aes_key_format.fuse_operation = AV8100_FUSE_WRITE;
	memcpy(config.fuse_aes_key_format.key, key, AV8100_FUSE_KEY_SIZE);
	if (av8100_conf_prep(AV8100_COMMAND_FUSE_AES_KEY,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	if (av8100_conf_w(AV8100_COMMAND_FUSE_AES_KEY,
		&buf_len, buf, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}

	if (buf_len == 2) {
		dev_dbg(hdev->dev, "buf[0]:%02x buf[1]:%02x\n", buf[0], buf[1]);
		if ((crc == buf[0]) && (buf[1] == 1))
			/* OK */
			*result = HDMI_RESULT_OK;
		else
			*result = HDMI_RESULT_CRC_MISMATCH;
	}

	return 0;
}

static int hdcploadaes(struct hdmi_device *hdev, u8 block, u8 key_len, u8 *key,
			u8 *result, u8 *crc32)
{
	union av8100_configuration config;
	struct av8100_status status;
	u8 buf_len;
	u8 buf[CRC32_SIZE];

	/* Default not OK */
	*result = HDMI_RESULT_NOT_OK;

	dev_dbg(hdev->dev, "%s block:%d\n", __func__, block);

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
		return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	config.hdcp_send_key_format.key_number = block;
	config.hdcp_send_key_format.data_len = key_len;
	memcpy(config.hdcp_send_key_format.data, key, key_len);
	if (av8100_conf_prep(AV8100_COMMAND_HDCP_SENDKEY, &config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	if (av8100_conf_w(AV8100_COMMAND_HDCP_SENDKEY,
		&buf_len, buf, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}

	if ((buf_len == CRC32_SIZE) && (crc32)) {
		memcpy(crc32, buf, CRC32_SIZE);
		dev_dbg(hdev->dev, "crc32:%02x%02x%02x%02x\n",
			crc32[0], crc32[1], crc32[2], crc32[3]);
	}

	*result = HDMI_RESULT_OK;

	return 0;
}

static int hdcpauthencr(struct hdmi_device *hdev, u8 auth_type, u8 encr_type,
			u8 *len, u8 *data)
{
	union av8100_configuration config;
	struct av8100_status status;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY)
		return -EINVAL;

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	switch (auth_type) {
	case HDMI_HDCP_AUTH_OFF:
	default:
		config.hdcp_management_format.req_type =
				AV8100_HDCP_AUTH_REQ_OFF;
		break;

	case HDMI_HDCP_AUTH_START:
		config.hdcp_management_format.req_type =
				AV8100_HDCP_AUTH_REQ_ON;
		break;

	case HDMI_HDCP_AUTH_REV_LIST_REQ:
		config.hdcp_management_format.req_type =
				AV8100_HDCP_REV_LIST_REQ;
		break;
	case HDMI_HDCP_AUTH_CONT:
		config.hdcp_management_format.req_type =
				AV8100_HDCP_AUTH_CONT;
		break;
	}

	switch (encr_type) {
	case HDMI_HDCP_ENCR_OESS:
	default:
		config.hdcp_management_format.encr_use =
				AV8100_HDCP_ENCR_USE_OESS;
		break;

	case HDMI_HDCP_ENCR_EESS:
		config.hdcp_management_format.encr_use =
				AV8100_HDCP_ENCR_USE_EESS;
		break;
	}

	if (av8100_conf_prep(AV8100_COMMAND_HDCP_MANAGEMENT,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		return -EINVAL;
	}

	if (av8100_conf_w(AV8100_COMMAND_HDCP_MANAGEMENT,
		len, data, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		return -EINVAL;
	}

	return 0;
}

static u8 events_read(struct hdmi_device *hdev)
{
	int ret;

	LOCK_HDMI_EVENTS;
	ret = hdev->events;
	dev_dbg(hdev->dev, "%s %02x\n", __func__, hdev->events);
	UNLOCK_HDMI_EVENTS;

	return ret;
}

static int events_clear(struct hdmi_device *hdev, u8 ev)
{
	dev_dbg(hdev->dev, "%s %02x\n", __func__, ev);

	LOCK_HDMI_EVENTS;
	hdev->events &= ~ev & EVENTS_MASK;
	UNLOCK_HDMI_EVENTS;

	return 0;
}

static int event_wakeup(struct hdmi_device *hdev)
{
	struct kobject *kobj = &hdev->dev->kobj;

	dev_dbg(hdev->dev, "%s", __func__);

	LOCK_HDMI_EVENTS;
	hdev->events |= HDMI_EVENT_WAKEUP;
	hdev->events_received = true;
	UNLOCK_HDMI_EVENTS;

	/* Wake up application waiting for event via call to poll() */
	sysfs_notify(kobj, NULL, SYSFS_EVENT_FILENAME);
	wake_up_interruptible(&hdev->event_wq);

	return 0;
}

static int audiocfg(struct hdmi_device *hdev, struct audio_cfg *cfg)
{
	union av8100_configuration config;
	struct av8100_status status;
	int ret = 0;

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY) {
		if (av8100_powerup() != 0) {
			dev_err(hdev->dev, "av8100_powerup failed\n");
			return -EINVAL;
		}
	}

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
	}

	config.audio_input_format.audio_input_if_format	= cfg->if_format;
	config.audio_input_format.i2s_input_nb		= cfg->i2s_entries;
	config.audio_input_format.sample_audio_freq	= cfg->freq;
	config.audio_input_format.audio_word_lg		= cfg->word_length;
	config.audio_input_format.audio_format		= cfg->format;
	config.audio_input_format.audio_if_mode		= cfg->if_mode;
	config.audio_input_format.audio_mute		= cfg->mute;

	av8100_conf_lock();
	if (av8100_conf_prep(AV8100_COMMAND_AUDIO_INPUT_FORMAT,
		&config) != 0) {
		dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
		ret = -EINVAL;
		goto audiocfg_end;
	}

	if (av8100_conf_w(AV8100_COMMAND_AUDIO_INPUT_FORMAT,
		NULL, NULL, I2C_INTERFACE) != 0) {
		dev_err(hdev->dev, "av8100_conf_w FAIL\n");
		ret = -EINVAL;
		goto audiocfg_end;
	}

audiocfg_end:
	av8100_conf_unlock();
	return ret;
}

/* sysfs */
static ssize_t store_storeastext(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if ((count != HDMI_STOREASTEXT_BIN_SIZE) &&
		(count != HDMI_STOREASTEXT_TEXT_SIZE) &&
		(count != HDMI_STOREASTEXT_TEXT_SIZE + 1))
		return -EINVAL;

	if ((count == HDMI_STOREASTEXT_BIN_SIZE) && (*buf == 0x1))
		hdev->sysfs_data.store_as_hextext = true;
	else if (((count == HDMI_STOREASTEXT_TEXT_SIZE) ||
		(count == HDMI_STOREASTEXT_TEXT_SIZE + 1)) && (*buf == '0') &&
			(*(buf + 1) == '1')) {
		hdev->sysfs_data.store_as_hextext = true;
	} else {
		hdev->sysfs_data.store_as_hextext = false;
	}

	dev_dbg(hdev->dev, "store_as_hextext:%0d\n",
		hdev->sysfs_data.store_as_hextext);

	return count;
}

static ssize_t store_plugdeten(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct plug_detect plug_detect;
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_PLUGDETEN_TEXT_SIZE) &&
			(count != HDMI_PLUGDETEN_TEXT_SIZE + 1))
			return -EINVAL;
		plug_detect.hdmi_detect_enable = htoi(buf + index);
		index += 2;
		plug_detect.on_time = htoi(buf + index);
		index += 2;
		plug_detect.hdmi_off_time = htoi(buf + index);
		index += 2;
	} else {
		if (count != HDMI_PLUGDETEN_BIN_SIZE)
			return -EINVAL;
		plug_detect.hdmi_detect_enable = *(buf + index++);
		plug_detect.on_time = *(buf + index++);
		plug_detect.hdmi_off_time = *(buf + index++);
	}

	if (plugdeten(hdev, &plug_detect))
		return -EINVAL;

	return count;
}

static ssize_t store_edidread(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct edid_read edid_read;
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);
	dev_dbg(hdev->dev, "count:%d\n", count);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_EDIDREAD_TEXT_SIZE) &&
			(count != HDMI_EDIDREAD_TEXT_SIZE + 1))
			return -EINVAL;
		edid_read.address = htoi(buf + index);
		index += 2;
		edid_read.block_nr = htoi(buf + index);
		index += 2;
	} else {
		if (count != HDMI_EDIDREAD_BIN_SIZE)
			return -EINVAL;
		edid_read.address = *(buf + index++);
		edid_read.block_nr = *(buf + index++);
	}

	if (edidread(hdev, &edid_read, &hdev->sysfs_data.edid_data.buf_len,
			hdev->sysfs_data.edid_data.buf))
		return -EINVAL;

	return count;
}

static ssize_t show_edidread(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int len;
	int index = 0;
	int cnt;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	len = hdev->sysfs_data.edid_data.buf_len;

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", len);
		index += 2;
	} else
		*(buf + index++) = len;

	dev_dbg(hdev->dev, "len:%02x\n", len);

	cnt = 0;
	while (cnt < len) {
		if (hdev->sysfs_data.store_as_hextext) {
			snprintf(buf + index, 3, "%02x",
				hdev->sysfs_data.edid_data.buf[cnt]);
			index += 2;
		} else
			*(buf + index++) =
				hdev->sysfs_data.edid_data.buf[cnt];

		cnt++;
	}

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t store_ceceven(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	bool enable = false;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_CECEVEN_TEXT_SIZE) &&
			(count != HDMI_CECEVEN_TEXT_SIZE + 1))
			return -EINVAL;
		if ((*buf == '0') && (*(buf + 1) == '1'))
			enable = true;
	} else {
		if (count != HDMI_CECEVEN_BIN_SIZE)
			return -EINVAL;
		if (*buf == 0x01)
			enable = true;
	}

	event_enable(hdev, enable, HDMI_EVENT_CEC | HDMI_EVENT_CECTXERR |
				HDMI_EVENT_CECTX | HDMI_EVENT_CCIERR);

	return count;
}

static ssize_t show_cecread(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct cec_rw cec_read;
	int index = 0;
	int cnt;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (cecread(hdev, &cec_read.src, &cec_read.dest, &cec_read.length,
		cec_read.data))
		return -EINVAL;

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", cec_read.src);
		index += 2;
		snprintf(buf + index, 3, "%02x", cec_read.dest);
		index += 2;
		snprintf(buf + index, 3, "%02x", cec_read.length);
		index += 2;
	} else {
		*(buf + index++) = cec_read.src;
		*(buf + index++) = cec_read.dest;
		*(buf + index++) = cec_read.length;
	}

	dev_dbg(hdev->dev, "len:%02x\n", cec_read.length);

	cnt = 0;
	while (cnt < cec_read.length) {
		if (hdev->sysfs_data.store_as_hextext) {
			snprintf(buf + index, 3, "%02x", cec_read.data[cnt]);
			index += 2;
		} else
			*(buf + index++) = cec_read.data[cnt];

		dev_dbg(hdev->dev, "%02x ", cec_read.data[cnt]);

		cnt++;
	}

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t store_cecsend(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct cec_rw cec_w;
	int index = 0;
	int cnt;
	int store_as_text;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if ((*buf == 'F') || (*buf == 'f'))
		/* To be able to override bin format for test purpose */
		store_as_text = 1;
	else
		store_as_text = hdev->sysfs_data.store_as_hextext;

	if (store_as_text) {
		if ((count < HDMI_CECSEND_TEXT_SIZE_MIN) ||
			(count > HDMI_CECSEND_TEXT_SIZE_MAX))
			return -EINVAL;

		cec_w.src = htoi(buf + index) & 0x0F;
		index += 2;
		cec_w.dest = htoi(buf + index);
		index += 2;
		cec_w.length = htoi(buf + index);
		index += 2;
		if (cec_w.length > HDMI_CEC_WRITE_MAXSIZE)
			return -EINVAL;
		cnt = 0;
		while (cnt < cec_w.length) {
			cec_w.data[cnt] = htoi(buf + index);
			index += 2;
			dev_dbg(hdev->dev, "%02x ", cec_w.data[cnt]);
			cnt++;
		}
	} else {
		if ((count < HDMI_CECSEND_BIN_SIZE_MIN) ||
			(count > HDMI_CECSEND_BIN_SIZE_MAX))
			return -EINVAL;

		cec_w.src = *(buf + index++);
		cec_w.dest = *(buf + index++);
		cec_w.length = *(buf + index++);
		if (cec_w.length > HDMI_CEC_WRITE_MAXSIZE)
			return -EINVAL;
		memcpy(cec_w.data, buf + index, cec_w.length);
	}

	if (cecsend(hdev, cec_w.src, cec_w.dest, cec_w.length, cec_w.data))
		return -EINVAL;

	return count;
}

static ssize_t store_infofrsend(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct info_fr info_fr;
	int index = 0;
	int cnt;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count < HDMI_INFOFRSEND_TEXT_SIZE_MIN) ||
			(count > HDMI_INFOFRSEND_TEXT_SIZE_MAX))
			return -EINVAL;

		info_fr.type = htoi(&buf[index]);
		index += 2;
		info_fr.ver = htoi(&buf[index]);
		index += 2;
		info_fr.crc = htoi(&buf[index]);
		index += 2;
		info_fr.length = htoi(&buf[index]);
		index += 2;

		if (info_fr.length > HDMI_INFOFRAME_MAX_SIZE)
			return -EINVAL;
		cnt = 0;
		while (cnt < info_fr.length) {
			info_fr.data[cnt] = htoi(buf + index);
			index += 2;
			dev_dbg(hdev->dev, "%02x ", info_fr.data[cnt]);
			cnt++;
		}
	} else {
		if ((count < HDMI_INFOFRSEND_BIN_SIZE_MIN) ||
			(count > HDMI_INFOFRSEND_BIN_SIZE_MAX))
			return -EINVAL;

		info_fr.type = *(buf + index++);
		info_fr.ver = *(buf + index++);
		info_fr.crc = *(buf + index++);
		info_fr.length = *(buf + index++);

		if (info_fr.length > HDMI_INFOFRAME_MAX_SIZE)
			return -EINVAL;
		memcpy(info_fr.data, buf + index, info_fr.length);
	}

	if (infofrsend(hdev, info_fr.type, info_fr.ver, info_fr.crc,
		info_fr.length, info_fr.data))
		return -EINVAL;

	return count;
}

static ssize_t store_hdcpeven(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	bool enable = false;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_HDCPEVEN_TEXT_SIZE) &&
			(count != HDMI_HDCPEVEN_TEXT_SIZE + 1))
			return -EINVAL;
		if ((*buf == '0') && (*(buf + 1) == '1'))
			enable = true;
	} else {
		if (count != HDMI_HDCPEVEN_BIN_SIZE)
			return -EINVAL;
		if (*buf == 0x01)
			enable = true;
	}

	event_enable(hdev, enable, HDMI_EVENT_HDCP);

	return count;
}

static ssize_t show_hdcpchkaesotp(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	u8 crc;
	u8 progged;
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdcpchkaesotp(hdev, &crc, &progged))
		return -EINVAL;

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", progged);
		index += 2;
	} else {
		*(buf + index++) = progged;
	}

	dev_dbg(hdev->dev, "progged:%02x\n", progged);

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t store_hdcpfuseaes(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct hdcp_fuseaes hdcp_fuseaes;
	int index = 0;
	int cnt;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	/* Default not OK */
	hdev->sysfs_data.fuse_result = HDMI_RESULT_NOT_OK;

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_HDCP_FUSEAES_TEXT_SIZE) &&
			(count != HDMI_HDCP_FUSEAES_TEXT_SIZE + 1))
			return -EINVAL;

		cnt = 0;
		while (cnt < HDMI_HDCP_FUSEAES_KEYSIZE) {
			hdcp_fuseaes.key[cnt] = htoi(buf + index);
			index += 2;
			dev_dbg(hdev->dev, "%02x ", hdcp_fuseaes.key[cnt]);
			cnt++;
		}
		hdcp_fuseaes.crc = htoi(&buf[index]);
		index += 2;
		dev_dbg(hdev->dev, "%02x ", hdcp_fuseaes.crc);
	} else {
		if (count != HDMI_HDCP_FUSEAES_BIN_SIZE)
			return -EINVAL;

		memcpy(hdcp_fuseaes.key, buf + index,
				HDMI_HDCP_FUSEAES_KEYSIZE);
		index += HDMI_HDCP_FUSEAES_KEYSIZE;
		hdcp_fuseaes.crc = *(buf + index++);
	}

	if (hdcpfuseaes(hdev, hdcp_fuseaes.key, hdcp_fuseaes.crc,
		&hdcp_fuseaes.result))
		return -EINVAL;

	dev_dbg(hdev->dev, "fuseresult:%02x ", hdcp_fuseaes.result);

	hdev->sysfs_data.fuse_result = hdcp_fuseaes.result;

	return count;
}

static ssize_t show_hdcpfuseaes(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", hdev->sysfs_data.fuse_result);
		index += 2;
	} else
		*(buf + index++) = hdev->sysfs_data.fuse_result;

	dev_dbg(hdev->dev, "status:%02x\n", hdev->sysfs_data.fuse_result);

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t store_hdcploadaes(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct hdcp_loadaesone hdcp_loadaes;
	int index = 0;
	int block_cnt;
	int cnt;
	u8 crc32_rcvd[CRC32_SIZE];
	u8 crc;
	u8 progged;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	/* Default not OK */
	hdev->sysfs_data.loadaes_result = HDMI_RESULT_NOT_OK;

	if (hdcpchkaesotp(hdev, &crc, &progged))
		return -EINVAL;

	if (!progged) {
		/* AES is not fused */
		hdcp_loadaes.result = HDMI_AES_NOT_FUSED;
		goto store_hdcploadaes_err;
	}

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_HDCP_LOADAES_TEXT_SIZE) &&
			(count != HDMI_HDCP_LOADAES_TEXT_SIZE + 1)) {
			dev_err(hdev->dev, "%s", "count mismatch\n");
			return -EINVAL;
		}

		/* AES */
		block_cnt = 0;
		while (block_cnt < HDMI_HDCP_AES_NR_OF_BLOCKS) {
			cnt = 0;
			while (cnt < HDMI_HDCP_AES_KEYSIZE) {
				hdcp_loadaes.key[cnt] =	htoi(buf + index);
				index += 2;
				dev_dbg(hdev->dev, "%02x ",
					hdcp_loadaes.key[cnt]);
				cnt++;
			}

			if (hdcploadaes(hdev,
					block_cnt + HDMI_HDCP_AES_BLOCK_START,
					HDMI_HDCP_AES_KEYSIZE,
					hdcp_loadaes.key,
					&hdcp_loadaes.result,
					crc32_rcvd)) {
				dev_err(hdev->dev, "%s %d\n",
					"hdcploadaes err aes block",
					block_cnt + HDMI_HDCP_AES_BLOCK_START);
				return -EINVAL;
			}

			if (hdcp_loadaes.result)
				goto store_hdcploadaes_err;

			block_cnt++;
		}

		/* KSV */
		memset(hdcp_loadaes.key, 0, HDMI_HDCP_AES_KSVZEROESSIZE);
		cnt = HDMI_HDCP_AES_KSVZEROESSIZE;
		while (cnt < HDMI_HDCP_AES_KSVSIZE +
				HDMI_HDCP_AES_KSVZEROESSIZE) {
			hdcp_loadaes.key[cnt] =
					htoi(&buf[index]);
			index += 2;
			dev_dbg(hdev->dev, "%02x ", hdcp_loadaes.key[cnt]);
			cnt++;
		}

		if (hdcploadaes(hdev, HDMI_HDCP_KSV_BLOCK,
				HDMI_HDCP_AES_KSVSIZE +
				HDMI_HDCP_AES_KSVZEROESSIZE,
				hdcp_loadaes.key,
				&hdcp_loadaes.result,
				NULL)) {
			dev_err(hdev->dev,
				"%s %d\n", "hdcploadaes err in ksv\n",
				block_cnt + HDMI_HDCP_AES_BLOCK_START);
			return -EINVAL;
		}

		if (hdcp_loadaes.result)
			goto store_hdcploadaes_err;

		/* CRC32 */
		for (cnt = 0; cnt < CRC32_SIZE; cnt++) {
			hdcp_loadaes.crc32[cnt] = htoi(buf + index);
			index += 2;
		}

		if (memcmp(hdcp_loadaes.crc32, crc32_rcvd, CRC32_SIZE)) {
			dev_dbg(hdev->dev, "crc32exp:%02x%02x%02x%02x\n",
				hdcp_loadaes.crc32[0],
				hdcp_loadaes.crc32[1],
				hdcp_loadaes.crc32[2],
				hdcp_loadaes.crc32[3]);
			hdcp_loadaes.result = HDMI_RESULT_CRC_MISMATCH;
			goto store_hdcploadaes_err;
		}
	} else {
		if (count != HDMI_HDCP_LOADAES_BIN_SIZE) {
			dev_err(hdev->dev, "%s", "count mismatch\n");
			return -EINVAL;
		}

		/* AES */
		block_cnt = 0;
		while (block_cnt < HDMI_HDCP_AES_NR_OF_BLOCKS) {
			memcpy(hdcp_loadaes.key, buf + index,
					HDMI_HDCP_AES_KEYSIZE);
			index += HDMI_HDCP_AES_KEYSIZE;

			if (hdcploadaes(hdev,
					block_cnt + HDMI_HDCP_AES_BLOCK_START,
					HDMI_HDCP_AES_KEYSIZE,
					hdcp_loadaes.key,
					&hdcp_loadaes.result,
					crc32_rcvd)) {
				dev_err(hdev->dev, "%s %d\n",
					"hdcploadaes err aes block",
					block_cnt + HDMI_HDCP_AES_BLOCK_START);
				return -EINVAL;
			}

			if (hdcp_loadaes.result)
				goto store_hdcploadaes_err;

			block_cnt++;
		}

		/* KSV */
		memset(hdcp_loadaes.key, 0, HDMI_HDCP_AES_KSVZEROESSIZE);
		memcpy(hdcp_loadaes.key + HDMI_HDCP_AES_KSVZEROESSIZE,
				buf + index,
				HDMI_HDCP_AES_KSVSIZE);
		index += HDMI_HDCP_AES_KSVSIZE;

		if (hdcploadaes(hdev, HDMI_HDCP_KSV_BLOCK,
				HDMI_HDCP_AES_KSVSIZE +
				HDMI_HDCP_AES_KSVZEROESSIZE,
				hdcp_loadaes.key,
				&hdcp_loadaes.result,
				NULL)) {
			dev_err(hdev->dev, "%s %d\n",
				"hdcploadaes err in ksv\n",
				block_cnt + HDMI_HDCP_AES_BLOCK_START);
			return -EINVAL;
		}

		memcpy(hdcp_loadaes.crc32, buf + index, CRC32_SIZE);
		index += CRC32_SIZE;

		/* CRC32 */
		if (memcmp(hdcp_loadaes.crc32, crc32_rcvd, CRC32_SIZE)) {
			dev_dbg(hdev->dev, "crc32exp:%02x%02x%02x%02x\n",
				hdcp_loadaes.crc32[0],
				hdcp_loadaes.crc32[1],
				hdcp_loadaes.crc32[2],
				hdcp_loadaes.crc32[3]);
			hdcp_loadaes.result = HDMI_RESULT_CRC_MISMATCH;
		}
	}

store_hdcploadaes_err:
	hdev->sysfs_data.loadaes_result = hdcp_loadaes.result;
	return count;
}

static ssize_t show_hdcploadaes(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x",
			hdev->sysfs_data.loadaes_result);
		index += 2;
	} else
		*(buf + index++) = hdev->sysfs_data.loadaes_result;

	dev_dbg(hdev->dev, "result:%02x\n", hdev->sysfs_data.loadaes_result);

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t store_hdcpauthencr(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct hdcp_authencr hdcp_authencr;
	int index = 0;
	u8 crc;
	u8 progged;
	int result = HDMI_RESULT_NOT_OK;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	/* Default */
	hdev->sysfs_data.authencr.buf_len = 0;

	if (hdcpchkaesotp(hdev, &crc, &progged)) {
		result = HDMI_AES_NOT_FUSED;
		goto store_hdcpauthencr_end;
	}

	if (!progged) {
		/* AES is not fused */
		result = HDMI_AES_NOT_FUSED;
		goto store_hdcpauthencr_end;
	}

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_HDCPAUTHENCR_TEXT_SIZE) &&
			(count != HDMI_HDCPAUTHENCR_TEXT_SIZE + 1))
			goto store_hdcpauthencr_end;

		hdcp_authencr.auth_type = htoi(buf + index);
		index += 2;
		hdcp_authencr.encr_type = htoi(buf + index);
		index += 2;
	} else {
		if (count != HDMI_HDCPAUTHENCR_BIN_SIZE)
			goto store_hdcpauthencr_end;

		hdcp_authencr.auth_type = *(buf + index++);
		hdcp_authencr.encr_type = *(buf + index++);
	}

	if (hdcpauthencr(hdev, hdcp_authencr.auth_type, hdcp_authencr.encr_type,
		 &hdev->sysfs_data.authencr.buf_len,
		 hdev->sysfs_data.authencr.buf))
		goto store_hdcpauthencr_end;

	result = HDMI_RESULT_OK;

store_hdcpauthencr_end:
	hdev->sysfs_data.authencr.result = result;
	return count;
}

static ssize_t show_hdcpauthencr(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int len;
	int index = 0;
	int cnt;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	/* result */
	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x",
			hdev->sysfs_data.authencr.result);
		index += 2;
	} else
		*(buf + index++) = hdev->sysfs_data.authencr.result;

	dev_dbg(hdev->dev, "result:%02x\n", hdev->sysfs_data.authencr.result);

	/* resp_size */
	len = hdev->sysfs_data.authencr.buf_len;
	if (len > AUTH_BUF_LEN)
		len = AUTH_BUF_LEN;
	dev_dbg(hdev->dev, "resp_size:%d\n", len);

	/* resp */
	cnt = 0;
	while (cnt < len) {
		if (hdev->sysfs_data.store_as_hextext) {
			snprintf(buf + index, 3, "%02x",
				hdev->sysfs_data.authencr.buf[cnt]);
			index += 2;

			dev_dbg(hdev->dev, "%02x ",
				hdev->sysfs_data.authencr.buf[cnt]);

		} else
			*(buf + index++) = hdev->sysfs_data.authencr.buf[cnt];

		cnt++;
	}

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t show_hdcpstateget(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	u8 hdcp_state;
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (av8100_reg_gen_status_r(NULL, NULL, NULL, NULL, NULL, &hdcp_state))
			return -EINVAL;

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", hdcp_state);
		index += 2;
	} else
		*(buf + index++) = hdcp_state;

	dev_dbg(hdev->dev, "status:%02x\n", hdcp_state);

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t show_evread(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int index = 0;
	u8 ev;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	ev = events_read(hdev);

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", ev);
		index += 2;
	} else
		*(buf + index++) = ev;

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	/* Events are read: clear events */
	events_clear(hdev, EVENTS_MASK);

	return index;
}

static ssize_t store_evclr(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	u8 ev;
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_EVCLR_TEXT_SIZE) &&
			(count != HDMI_EVCLR_TEXT_SIZE + 1))
			return -EINVAL;

		ev = htoi(&buf[index]);
		index += 2;
	} else {
		if (count != HDMI_EVCLR_BIN_SIZE)
			return -EINVAL;

		ev = *(buf + index++);
	}

	events_clear(hdev, ev);

	return count;
}

static ssize_t store_audiocfg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	struct audio_cfg audio_cfg;
	int index = 0;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_AUDIOCFG_TEXT_SIZE) &&
			(count != HDMI_AUDIOCFG_TEXT_SIZE + 1))
			return -EINVAL;

		audio_cfg.if_format = htoi(&buf[index]);
		index += 2;
		audio_cfg.i2s_entries = htoi(&buf[index]);
		index += 2;
		audio_cfg.freq = htoi(&buf[index]);
		index += 2;
		audio_cfg.word_length = htoi(&buf[index]);
		index += 2;
		audio_cfg.format = htoi(&buf[index]);
		index += 2;
		audio_cfg.if_mode = htoi(&buf[index]);
		index += 2;
		audio_cfg.mute = htoi(&buf[index]);
		index += 2;
	} else {
		if (count != HDMI_AUDIOCFG_BIN_SIZE)
			return -EINVAL;

		audio_cfg.if_format = *(buf + index++);
		audio_cfg.i2s_entries = *(buf + index++);
		audio_cfg.freq = *(buf + index++);
		audio_cfg.word_length = *(buf + index++);
		audio_cfg.format = *(buf + index++);
		audio_cfg.if_mode = *(buf + index++);
		audio_cfg.mute = *(buf + index++);
	}

	audiocfg(hdev, &audio_cfg);

	return count;
}

static ssize_t show_plugstatus(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int index = 0;
	struct av8100_status av8100_status;
	u8 plstat;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	av8100_status = av8100_status_get();
	plstat = av8100_status.av8100_plugin_status == AV8100_HDMI_PLUGIN;

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", plstat);
		index += 2;
	} else
		*(buf + index++) = plstat;

	if (hdev->sysfs_data.store_as_hextext)
		index++;

	return index;
}

static ssize_t store_poweronoff(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	bool enable = false;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	if (hdev->sysfs_data.store_as_hextext) {
		if ((count != HDMI_POWERONOFF_TEXT_SIZE) &&
			(count != HDMI_POWERONOFF_TEXT_SIZE + 1))
			return -EINVAL;
		if ((*buf == '0') && (*(buf + 1) == '1'))
			enable = true;
	} else {
		if (count != HDMI_POWERONOFF_BIN_SIZE)
			return -EINVAL;
		if (*buf == 0x01)
			enable = true;
	}

	if (enable == 0) {
		if (av8100_powerdown() != 0) {
			dev_err(hdev->dev, "av8100_powerdown FAIL\n");
			return -EINVAL;
		}
	} else {
		if (av8100_powerup() != 0) {
			dev_err(hdev->dev, "av8100_powerup FAIL\n");
			return -EINVAL;
		}
	}

	return count;
}

static ssize_t show_poweronoff(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);
	int index = 0;
	struct av8100_status status;
	u8 power_state;

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_SCAN)
		power_state = 0;
	else
		power_state = 1;

	if (hdev->sysfs_data.store_as_hextext) {
		snprintf(buf + index, 3, "%02x", power_state);
		index += 3;
	} else {
		*(buf + index++) = power_state;
	}

	return index;
}

static ssize_t store_evwakeup(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hdmi_device *hdev = dev_to_hdev(dev);

	if (!hdev)
		return -EFAULT;

	dev_dbg(hdev->dev, "%s\n", __func__);

	event_wakeup(hdev);

	return count;
}

static int hdmi_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int hdmi_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* ioctl */
static long hdmi_ioctl(struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	u8 value = 0;
	struct hdmi_register reg;
	struct av8100_status status;
	u8 aes_status;
	struct hdmi_device *hdev = devnr_to_hdev(HDMI_DEVNR_DEFAULT);

	switch (cmd) {
	case IOC_PLUG_DETECT_ENABLE:
		{
		struct plug_detect plug_detect;

		if (copy_from_user(&plug_detect, (void *)arg,
			sizeof(struct plug_detect)))
			return -EINVAL;

		if (plugdeten(hdev, &plug_detect))
			return -EINVAL;
		}
		break;

	case IOC_EDID_READ:
		{
		struct edid_read edid_read;

		if (copy_from_user(&edid_read, (void *)arg,
				sizeof(struct edid_read)))
			return -EINVAL;

		if (edidread(hdev, &edid_read, &edid_read.data_length,
				edid_read.data))
			return -EINVAL;

		if (copy_to_user((void *)arg, (void *)&edid_read,
			sizeof(struct edid_read))) {
			return -EINVAL;
		}
		}
		break;

	case IOC_CEC_EVENT_ENABLE:
		if (copy_from_user(&value, (void *)arg, sizeof(u8)))
			return -EINVAL;

		event_enable(hdev, value != 0,
					HDMI_EVENT_CEC | HDMI_EVENT_CECTXERR |
					HDMI_EVENT_CECTX | HDMI_EVENT_CCIERR);
		break;

	case IOC_CEC_READ:
		{
		struct cec_rw cec_read;

		if (cecread(hdev, &cec_read.src, &cec_read.dest,
				&cec_read.length, cec_read.data))
			return -EINVAL;

		if (copy_to_user((void *)arg, (void *)&cec_read,
			sizeof(struct cec_rw))) {
			return -EINVAL;
		}
		}
		break;

	case IOC_CEC_SEND:
		{
		struct cec_rw cec_send;

		if (copy_from_user(&cec_send, (void *)arg,
				sizeof(struct cec_rw)))
			return -EINVAL;

		if (cecsend(hdev, cec_send.src, cec_send.dest, cec_send.length,
				cec_send.data))
			return -EINVAL;
		}
		break;

	case IOC_INFOFRAME_SEND:
		{
		struct info_fr info_fr;

		if (copy_from_user(&info_fr, (void *)arg,
				sizeof(struct info_fr)))
			return -EINVAL;

		if (infofrsend(hdev, info_fr.type, info_fr.ver, info_fr.crc,
			info_fr.length, info_fr.data))
			return -EINVAL;
		}
		break;

	case IOC_HDCP_EVENT_ENABLE:
		if (copy_from_user(&value, (void *)arg, sizeof(u8)))
			return -EINVAL;

		event_enable(hdev, value != 0, HDMI_EVENT_HDCP);
		break;

	case IOC_HDCP_CHKAESOTP:
		if (hdcpchkaesotp(hdev, &value, &aes_status))
			return -EINVAL;

		if (copy_to_user((void *)arg, (void *)&aes_status,
			sizeof(u8))) {
			return -EINVAL;
		}
		break;

	case IOC_HDCP_FUSEAES:
		{
		struct hdcp_fuseaes hdcp_fuseaes;

		if (copy_from_user(&hdcp_fuseaes, (void *)arg,
				sizeof(struct hdcp_fuseaes)))
			return -EINVAL;

		if (hdcpfuseaes(hdev, hdcp_fuseaes.key, hdcp_fuseaes.crc,
				&hdcp_fuseaes.result))
				return -EINVAL;

		if (copy_to_user((void *)arg, (void *)&hdcp_fuseaes,
			sizeof(struct hdcp_fuseaes))) {
			return -EINVAL;
		}
		}
		break;

	case IOC_HDCP_LOADAES:
		{
		int block_cnt;
		struct hdcp_loadaesone hdcp_loadaesone;
		struct hdcp_loadaesall hdcp_loadaesall;

		if (copy_from_user(&hdcp_loadaesall, (void *)arg,
				sizeof(struct hdcp_loadaesall)))
			return -EINVAL;

		if (hdcpchkaesotp(hdev, &value, &aes_status))
			return -EINVAL;

		if (!aes_status) {
			/* AES is not fused */
			hdcp_loadaesone.result = HDMI_AES_NOT_FUSED;
			goto ioc_hdcploadaes_err;
		}

		/* AES */
		block_cnt = 0;
		while (block_cnt < HDMI_HDCP_AES_NR_OF_BLOCKS) {
			memcpy(hdcp_loadaesone.key, hdcp_loadaesall.key +
					block_cnt * HDMI_HDCP_AES_KEYSIZE,
					HDMI_HDCP_AES_KEYSIZE);

			if (hdcploadaes(hdev,
					block_cnt + HDMI_HDCP_AES_BLOCK_START,
					HDMI_HDCP_AES_KEYSIZE,
					hdcp_loadaesone.key,
					&hdcp_loadaesone.result,
					hdcp_loadaesone.crc32))
				return -EINVAL;

			if (hdcp_loadaesone.result)
				return -EINVAL;

			block_cnt++;
		}

		/* KSV */
		memset(hdcp_loadaesone.key, 0, HDMI_HDCP_AES_KSVZEROESSIZE);
		memcpy(hdcp_loadaesone.key + HDMI_HDCP_AES_KSVZEROESSIZE,
				hdcp_loadaesall.ksv, HDMI_HDCP_AES_KSVSIZE);

		if (hdcploadaes(hdev, HDMI_HDCP_KSV_BLOCK,
				HDMI_HDCP_AES_KSVSIZE +
				HDMI_HDCP_AES_KSVZEROESSIZE,
				hdcp_loadaesone.key,
				&hdcp_loadaesone.result,
				NULL))
			return -EINVAL;

		if (hdcp_loadaesone.result)
			return -EINVAL;

		/* CRC32 */
		if (memcmp(hdcp_loadaesall.crc32, hdcp_loadaesone.crc32,
				CRC32_SIZE)) {
			dev_dbg(hdev->dev, "crc32exp:%02x%02x%02x%02x\n",
				hdcp_loadaesall.crc32[0],
				hdcp_loadaesall.crc32[1],
				hdcp_loadaesall.crc32[2],
				hdcp_loadaesall.crc32[3]);
			hdcp_loadaesone.result = HDMI_RESULT_CRC_MISMATCH;
			goto ioc_hdcploadaes_err;
		}

ioc_hdcploadaes_err:
		hdcp_loadaesall.result = hdcp_loadaesone.result;

		if (copy_to_user((void *)arg, (void *)&hdcp_loadaesall,
			sizeof(struct hdcp_loadaesall))) {
			return -EINVAL;
		}
		}
		break;

	case IOC_HDCP_AUTHENCR_REQ:
		{
		struct hdcp_authencr hdcp_authencr;
		int result = HDMI_RESULT_NOT_OK;

		u8 buf[AUTH_BUF_LEN];

		if (copy_from_user(&hdcp_authencr, (void *)arg,
				sizeof(struct hdcp_authencr)))
			return -EINVAL;

		/* Default not OK */
		hdcp_authencr.resp_size = 0;

		if (hdcpchkaesotp(hdev, &value, &aes_status)) {
			result = HDMI_AES_NOT_FUSED;
			goto hdcp_authencr_end;
		}

		if (!aes_status) {
			/* AES is not fused */
			result = HDMI_AES_NOT_FUSED;
			goto hdcp_authencr_end;
		}

		if (hdcpauthencr(hdev, hdcp_authencr.auth_type,
				hdcp_authencr.encr_type,
				&value,
				buf)) {
			result = HDMI_RESULT_NOT_OK;
			goto hdcp_authencr_end;
		}

		if (value > AUTH_BUF_LEN)
			value = AUTH_BUF_LEN;

		result = HDMI_RESULT_OK;
		hdcp_authencr.resp_size = value;
		memcpy(hdcp_authencr.resp, buf, value);

hdcp_authencr_end:
		hdcp_authencr.result = result;
		if (copy_to_user((void *)arg, (void *)&hdcp_authencr,
			sizeof(struct hdcp_authencr)))
			return -EINVAL;
		}
		break;

	case IOC_HDCP_STATE_GET:
		if (av8100_reg_gen_status_r(NULL, NULL, NULL, NULL, NULL,
				&value))
				return -EINVAL;

		if (copy_to_user((void *)arg, (void *)&value,
			sizeof(u8))) {
			return -EINVAL;
		}
		break;

	case IOC_EVENTS_READ:
		value = events_read(hdev);

		if (copy_to_user((void *)arg, (void *)&value,
			sizeof(u8))) {
			return -EINVAL;
		}

		/* Events are read: clear events */
		events_clear(hdev, EVENTS_MASK);
		break;

	case IOC_EVENTS_CLEAR:
		if (copy_from_user(&value, (void *)arg, sizeof(u8)))
			return -EINVAL;

		events_clear(hdev, value);
		break;

	case IOC_AUDIO_CFG:
		{
		struct audio_cfg audio_cfg;

		if (copy_from_user(&audio_cfg, (void *)arg,
				sizeof(struct audio_cfg)))
			return -EINVAL;

		audiocfg(hdev, &audio_cfg);
		}
		break;

	case IOC_PLUG_STATUS:
		status = av8100_status_get();
		value = status.av8100_plugin_status == AV8100_HDMI_PLUGIN;

		if (copy_to_user((void *)arg, (void *)&value,
			sizeof(u8))) {
			return -EINVAL;
		}
		break;

	case IOC_POWERONOFF:
		/* Get desired power state on or off */
		if (copy_from_user(&value, (void *)arg, sizeof(u8)))
			return -EINVAL;

		if (value == 0) {
			if (av8100_powerdown() != 0) {
				dev_err(hdev->dev, "av8100_powerdown FAIL\n");
				return -EINVAL;
			}
		} else {
			if (av8100_powerup() != 0) {
				dev_err(hdev->dev, "av8100_powerup FAIL\n");
				return -EINVAL;
			}
		}
		break;

	case IOC_EVENT_WAKEUP:
		/* Trigger event */
		event_wakeup(hdev);
		break;

	case IOC_POWERSTATE:
		status = av8100_status_get();
		value = status.av8100_state >= AV8100_OPMODE_SCAN;

		if (copy_to_user((void *)arg, (void *)&value,
						sizeof(u8))) {
			return -EINVAL;
		}
		break;

	/* Internal */
	case IOC_HDMI_ENABLE_INTERRUPTS:
		av8100_disable_interrupt();
		if (av8100_enable_interrupt() != 0) {
			dev_err(hdev->dev, "av8100_ei FAIL\n");
			return -EINVAL;
		}
		break;

	case IOC_HDMI_DOWNLOAD_FW:
		if (av8100_download_firmware(I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100 dl fw FAIL\n");
			return -EINVAL;
		}
		break;

	case IOC_HDMI_ONOFF:
		{
		union av8100_configuration config;

		/* Get desired HDMI mode on or off */
		if (copy_from_user(&value, (void *)arg, sizeof(u8)))
			return -EFAULT;

		if (av8100_conf_get(AV8100_COMMAND_HDMI, &config) != 0) {
			dev_err(hdev->dev, "av8100_conf_get FAIL\n");
			return -EINVAL;
		}
		if (value == 0)
			config.hdmi_format.hdmi_mode = AV8100_HDMI_OFF;
		else
			config.hdmi_format.hdmi_mode = AV8100_HDMI_ON;

		av8100_conf_lock();
		if (av8100_conf_prep(AV8100_COMMAND_HDMI, &config) != 0) {
			dev_err(hdev->dev, "av8100_conf_prep FAIL\n");
			av8100_conf_unlock();
			return -EINVAL;
		}
		if (av8100_conf_w(AV8100_COMMAND_HDMI, NULL, NULL,
			I2C_INTERFACE) != 0) {
			dev_err(hdev->dev, "av8100_conf_w FAIL\n");
			av8100_conf_unlock();
			return -EINVAL;
		}
		av8100_conf_unlock();
		}
		break;

	case IOC_HDMI_REGISTER_WRITE:
		if (copy_from_user(&reg, (void *)arg,
			sizeof(struct hdmi_register))) {
			return -EINVAL;
		}

		if (av8100_reg_w(reg.offset, reg.value) != 0) {
			dev_err(hdev->dev, "hdmi_register_write FAIL\n");
			return -EINVAL;
		}
		break;

	case IOC_HDMI_REGISTER_READ:
		if (copy_from_user(&reg, (void *)arg,
			sizeof(struct hdmi_register))) {
			return -EINVAL;
		}

		if (av8100_reg_r(reg.offset, &reg.value) != 0) {
			dev_err(hdev->dev, "hdmi_register_write FAIL\n");
			return -EINVAL;
		}

		if (copy_to_user((void *)arg, (void *)&reg,
			sizeof(struct hdmi_register))) {
			return -EINVAL;
		}
		break;

	case IOC_HDMI_STATUS_GET:
		status = av8100_status_get();

		if (copy_to_user((void *)arg, (void *)&status,
			sizeof(struct av8100_status))) {
			return -EINVAL;
		}
		break;

	case IOC_HDMI_CONFIGURATION_WRITE:
		{
		struct hdmi_command_register command_reg;

		if (copy_from_user(&command_reg, (void *)arg,
				sizeof(struct hdmi_command_register)) != 0) {
			dev_err(hdev->dev, "IOC_HDMI_CONFIGURATION_WRITE "
				"fail 1\n");
			command_reg.return_status = EINVAL;
		} else {
			command_reg.return_status = 0;
			if (av8100_conf_w_raw(command_reg.cmd_id,
					command_reg.buf_len,
					command_reg.buf,
					&(command_reg.buf_len),
					command_reg.buf) != 0) {
				dev_err(hdev->dev,
					"IOC_HDMI_CONFIGURATION_WRITE "
					"fail 2\n");
				command_reg.return_status = EINVAL;
			}
		}

		if (copy_to_user((void *)arg, (void *)&command_reg,
			sizeof(struct hdmi_command_register)) != 0) {
			return -EINVAL;
		}
		}
		break;

	default:
		break;
	}

	return 0;
}

static unsigned int
hdmi_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct hdmi_device *hdev;

	hdev = devnr_to_hdev(HDMI_DEVNR_DEFAULT);
	if (!hdev)
		return 0;

	dev_dbg(hdev->dev, "%s\n", __func__);

	poll_wait(filp, &hdev->event_wq , wait);

	LOCK_HDMI_EVENTS;
	if (hdev->events_received == true) {
		hdev->events_received = false;
		mask = POLLIN | POLLRDNORM;
	}
	UNLOCK_HDMI_EVENTS;

	return mask;
}

static const struct file_operations hdmi_fops = {
	.owner =    THIS_MODULE,
	.open =     hdmi_open,
	.release =  hdmi_release,
	.unlocked_ioctl = hdmi_ioctl,
	.poll = hdmi_poll
};

/* Event callback function called by hw driver */
void hdmi_event(enum av8100_hdmi_event ev)
{
	int events_old;
	int events_new;
	struct hdmi_device *hdev;
	struct kobject *kobj;

	hdev = devnr_to_hdev(HDMI_DEVNR_DEFAULT);
	if (!hdev)
		return;

	dev_dbg(hdev->dev, "hdmi_event %02x\n", ev);

	kobj = &(hdev->dev->kobj);

	LOCK_HDMI_EVENTS;

	events_old = hdev->events;

	/* Set event */
	switch (ev) {
	case AV8100_HDMI_EVENT_HDMI_PLUGIN:
		hdev->events &= ~HDMI_EVENT_HDMI_PLUGOUT;
		hdev->events |= HDMI_EVENT_HDMI_PLUGIN;
		switch_set_state(&hdev->switch_hdmi_detection, 1);
		break;

	case AV8100_HDMI_EVENT_HDMI_PLUGOUT:
		hdev->events &= ~HDMI_EVENT_HDMI_PLUGIN;
		hdev->events |= HDMI_EVENT_HDMI_PLUGOUT;
		cec_tx_status(hdev, CEC_TX_SET_FREE);
		switch_set_state(&hdev->switch_hdmi_detection, 0);
		break;

	case AV8100_HDMI_EVENT_CEC:
		hdev->events |= HDMI_EVENT_CEC;
		break;

	case AV8100_HDMI_EVENT_HDCP:
		hdev->events |= HDMI_EVENT_HDCP;
		break;

	case AV8100_HDMI_EVENT_CECTXERR:
		hdev->events |= HDMI_EVENT_CECTXERR;
		cec_tx_status(hdev, CEC_TX_SET_FREE);
		break;

	case AV8100_HDMI_EVENT_CECTX:
		hdev->events |= HDMI_EVENT_CECTX;
		cec_tx_status(hdev, CEC_TX_SET_FREE);
		break;

	case AV8100_HDMI_EVENT_CCIERR:
		hdev->events |= HDMI_EVENT_CCIERR;
		break;

	default:
		break;
	}

	events_new = hdev->events_mask & hdev->events;

	UNLOCK_HDMI_EVENTS;

	dev_dbg(hdev->dev, "hdmi events:%02x, events_old:%02x mask:%02x\n",
			events_new, events_old, hdev->events_mask);

	if (events_new != events_old) {
		/* Wake up application waiting for event via call to poll() */
		sysfs_notify(kobj, NULL, SYSFS_EVENT_FILENAME);

		LOCK_HDMI_EVENTS;
		hdev->events_received = true;
		UNLOCK_HDMI_EVENTS;

		wake_up_interruptible(&hdev->event_wq);
	}
}
EXPORT_SYMBOL(hdmi_event);

int hdmi_device_register(struct hdmi_device *hdev)
{
	hdev->miscdev.minor = MISC_DYNAMIC_MINOR;
	hdev->miscdev.name = "hdmi";
	hdev->miscdev.fops = &hdmi_fops;

	if (misc_register(&hdev->miscdev)) {
		pr_err("hdmi misc_register failed\n");
		return -EFAULT;
	}

	hdev->dev = hdev->miscdev.this_device;

	return 0;
}

int __init hdmi_init(void)
{
	struct hdmi_device *hdev;
	int i;
	int ret;

	/* Allocate device data */
	hdev = kzalloc(sizeof(struct hdmi_device), GFP_KERNEL);
	if (!hdev) {
		pr_err("%s: Alloc failure\n", __func__);
		return -ENOMEM;
	}

	/* Add to list */
	list_add_tail(&hdev->list, &hdmi_device_list);

	if (hdmi_device_register(hdev)) {
		pr_err("%s: Alloc failure\n", __func__);
		return -EFAULT;
	}

	hdev->devnr = HDMI_DEVNR_DEFAULT;

	/* Default sysfs file format is hextext */
	hdev->sysfs_data.store_as_hextext = true;

	init_waitqueue_head(&hdev->event_wq);

	/* Create sysfs attrs */
	for (i = 0; attr_name(hdmi_sysfs_attrs[i]); i++) {
		ret = device_create_file(hdev->dev, &hdmi_sysfs_attrs[i]);
		if (ret)
			dev_err(hdev->dev,
				"Unable to create sysfs attr %s (%d)\n",
				hdmi_sysfs_attrs[i].attr.name, ret);
	}

	hdev->switch_hdmi_detection.name = "hdmi";
	switch_dev_register(&hdev->switch_hdmi_detection);

	/* Register event callback */
	av8100_hdmi_event_cb_set(hdmi_event);

	return 0;
}
late_initcall(hdmi_init);

void hdmi_exit(void)
{
	struct hdmi_device *hdev = NULL;
	int i;

	if (list_empty(&hdmi_device_list))
		return;
	else
		hdev = list_entry(hdmi_device_list.next,
				struct hdmi_device, list);

	/* Deregister event callback */
	av8100_hdmi_event_cb_set(NULL);

	switch_dev_unregister(&hdev->switch_hdmi_detection);

	/* Remove sysfs attrs */
	for (i = 0; attr_name(hdmi_sysfs_attrs[i]); i++)
		device_remove_file(hdev->dev, &hdmi_sysfs_attrs[i]);

	misc_deregister(&hdev->miscdev);

	/* Remove from list */
	list_del(&hdev->list);

	/* Free device data */
	kfree(hdev);
}
