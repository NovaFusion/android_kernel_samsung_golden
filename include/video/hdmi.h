/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * HDMI driver
 *
 * Author: Per Persson <per.xb.persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __HDMI__H__
#define __HDMI__H__

#define HDMI_RESULT_OK			0
#define HDMI_RESULT_NOT_OK		1
#define HDMI_AES_NOT_FUSED		2
#define HDMI_RESULT_CRC_MISMATCH	3

#define HDMI_CEC_READ_MAXSIZE		16
#define HDMI_CEC_WRITE_MAXSIZE		15
#define HDMI_INFOFRAME_MAX_SIZE		27
#define HDMI_HDCP_FUSEAES_KEYSIZE	16
#define HDMI_HDCP_AES_BLOCK_START	128
#define HDMI_HDCP_KSV_BLOCK		40
#define HDMI_HDCP_AES_NR_OF_BLOCKS	18
#define HDMI_HDCP_AES_KEYSIZE		16
#define HDMI_HDCP_AES_KSVSIZE		5
#define HDMI_HDCP_AES_KSVZEROESSIZE	3
#define HDMI_EDID_DATA_SIZE		128
#define HDMI_CEC_SIZE			15
#define HDMI_INFOFR_SIZE		27
#define HDMI_FUSE_KEYSIZE		16
#define HDMI_AES_KSVSIZE		5
#define HDMI_AES_KEYSIZE		288
#define HDMI_CRC32_SIZE			4
#define HDMI_HDCPAUTHRESP_SIZE		126

#define HDMI_STOREASTEXT_TEXT_SIZE	2
#define HDMI_STOREASTEXT_BIN_SIZE	1
#define HDMI_PLUGDETEN_TEXT_SIZE	6
#define HDMI_PLUGDETEN_BIN_SIZE		3
#define HDMI_EDIDREAD_TEXT_SIZE		4
#define HDMI_EDIDREAD_BIN_SIZE		2
#define HDMI_CECEVEN_TEXT_SIZE		2
#define HDMI_CECEVEN_BIN_SIZE		1
#define HDMI_CECSEND_TEXT_SIZE_MAX	37
#define HDMI_CECSEND_TEXT_SIZE_MIN	6
#define HDMI_CECSEND_BIN_SIZE_MAX	18
#define HDMI_CECSEND_BIN_SIZE_MIN	3
#define HDMI_INFOFRSEND_TEXT_SIZE_MIN	8
#define HDMI_INFOFRSEND_TEXT_SIZE_MAX	63
#define HDMI_INFOFRSEND_BIN_SIZE_MIN	4
#define HDMI_INFOFRSEND_BIN_SIZE_MAX	31
#define HDMI_HDCPEVEN_TEXT_SIZE		2
#define HDMI_HDCPEVEN_BIN_SIZE		1
#define HDMI_HDCP_FUSEAES_TEXT_SIZE	34
#define HDMI_HDCP_FUSEAES_BIN_SIZE	17
#define HDMI_HDCP_LOADAES_TEXT_SIZE	594
#define HDMI_HDCP_LOADAES_BIN_SIZE	297
#define HDMI_HDCPAUTHENCR_TEXT_SIZE	4
#define HDMI_HDCPAUTHENCR_BIN_SIZE	2
#define HDMI_EVCLR_TEXT_SIZE		2
#define HDMI_EVCLR_BIN_SIZE		1
#define HDMI_AUDIOCFG_TEXT_SIZE		14
#define HDMI_AUDIOCFG_BIN_SIZE		7
#define HDMI_POWERONOFF_TEXT_SIZE	2
#define HDMI_POWERONOFF_BIN_SIZE	1

#define HDMI_IOC_MAGIC 0xcc

/** IOCTL Operations */
#define IOC_PLUG_DETECT_ENABLE		_IOWR(HDMI_IOC_MAGIC, 1, int)
#define IOC_EDID_READ			_IOWR(HDMI_IOC_MAGIC, 2, int)
#define IOC_CEC_EVENT_ENABLE		_IOWR(HDMI_IOC_MAGIC, 3, int)
#define IOC_CEC_READ			_IOWR(HDMI_IOC_MAGIC, 4, int)
#define IOC_CEC_SEND			_IOWR(HDMI_IOC_MAGIC, 5, int)
#define IOC_INFOFRAME_SEND		_IOWR(HDMI_IOC_MAGIC, 6, int)
#define IOC_HDCP_EVENT_ENABLE		_IOWR(HDMI_IOC_MAGIC, 7, int)
#define IOC_HDCP_CHKAESOTP		_IOWR(HDMI_IOC_MAGIC, 8, int)
#define IOC_HDCP_FUSEAES		_IOWR(HDMI_IOC_MAGIC, 9, int)
#define IOC_HDCP_LOADAES		_IOWR(HDMI_IOC_MAGIC, 10, int)
#define IOC_HDCP_AUTHENCR_REQ		_IOWR(HDMI_IOC_MAGIC, 11, int)
#define IOC_HDCP_STATE_GET		_IOWR(HDMI_IOC_MAGIC, 12, int)
#define IOC_EVENTS_READ			_IOWR(HDMI_IOC_MAGIC, 13, int)
#define IOC_EVENTS_CLEAR		_IOWR(HDMI_IOC_MAGIC, 14, int)
#define IOC_AUDIO_CFG			_IOWR(HDMI_IOC_MAGIC, 15, int)
#define IOC_PLUG_STATUS			_IOWR(HDMI_IOC_MAGIC, 16, int)
#define IOC_POWERONOFF			_IOWR(HDMI_IOC_MAGIC, 17, int)
#define IOC_EVENT_WAKEUP		_IOWR(HDMI_IOC_MAGIC, 18, int)
#define IOC_POWERSTATE			_IOWR(HDMI_IOC_MAGIC, 19, int)


/* HDMI driver */
void hdmi_event(enum av8100_hdmi_event);
int hdmi_init(void);
void hdmi_exit(void);

enum hdmi_event {
	HDMI_EVENT_NONE =		0x0,
	HDMI_EVENT_HDMI_PLUGIN =	0x1,
	HDMI_EVENT_HDMI_PLUGOUT =	0x2,
	HDMI_EVENT_CEC =		0x4,
	HDMI_EVENT_HDCP =		0x8,
	HDMI_EVENT_CECTXERR =		0x10,
	HDMI_EVENT_WAKEUP =		0x20,
	HDMI_EVENT_CECTX =		0x40,
	HDMI_EVENT_CCIERR =		0x80, /* 5V short circuit */
};

enum hdmi_hdcp_auth_type {
	HDMI_HDCP_AUTH_OFF = 0,
	HDMI_HDCP_AUTH_START = 1,
	HDMI_HDCP_AUTH_REV_LIST_REQ = 2,
	HDMI_HDCP_AUTH_CONT = 3,
};

enum hdmi_hdcp_encr_type {
	HDMI_HDCP_ENCR_OESS = 0,
	HDMI_HDCP_ENCR_EESS = 1,
};

struct plug_detect {
	__u8 hdmi_detect_enable;
	__u8 on_time;
	__u8 hdmi_off_time;
};

struct edid_read {
	__u8 address;
	__u8 block_nr;
	__u8 data_length;
	__u8 data[HDMI_EDID_DATA_SIZE];
};

struct cec_rw  {
	__u8 src;
	__u8 dest;
	__u8 length;
	__u8 data[HDMI_CEC_SIZE];
};

struct info_fr {
	__u8 type;
	__u8 ver;
	__u8 crc;
	__u8 length;
	__u8 data[HDMI_INFOFR_SIZE];
};

struct hdcp_fuseaes {
	__u8 key[HDMI_FUSE_KEYSIZE];
	__u8 crc;
	__u8 result;
};

struct hdcp_loadaesall {
	__u8 key[HDMI_AES_KEYSIZE];
	__u8 ksv[HDMI_AES_KSVSIZE];
	__u8 crc32[HDMI_CRC32_SIZE];
	__u8 result;
};


/* hdcp_authencr resp coding
 *
 * When encr_type is 2 (request revoc list), the response is given by
 * resp_size is != 0 and resp containing the folllowing:
 *
 * __u8[5]		Bksv from sink (not belonging to revocation list)
 * __u8			Device count
 * Additional output if Nrofdevices > 0:
 * __u8[5 * Nrofdevices]	Bksv per connected equipment
 * __u8[20]			SHA signature
 *
 * Device count coding:
 *	0 = a simple receiver is connected
 *	0x80 = a repeater is connected without downstream equipment
 *	0x81 = a repeater is connected with one downstream equipment
 *	up to 0x94 = (0x80 + 0x14) a repeater is connected with downstream
 *	equipment (thus up to 20 connected equipments)
 *	1  = repeater without sink equipment connected
 *	>1 = number of connected equipment on the repeater
 * Nrofdevices = Device count & 0x7F (max 20)
 *
 * Max resp_size is 5 + 1 + 5 * 20 + 20 = 126 bytes
 *
 */
struct hdcp_authencr {
	__u8 auth_type;
	__u8 encr_type;
	__u8 result;
	__u8 resp_size;
	__u8 resp[HDMI_HDCPAUTHRESP_SIZE];
};

struct audio_cfg {
	__u8 if_format;
	__u8 i2s_entries;
	__u8 freq;
	__u8 word_length;
	__u8 format;
	__u8 if_mode;
	__u8 mute;
};

#endif /* __HDMI__H__ */
