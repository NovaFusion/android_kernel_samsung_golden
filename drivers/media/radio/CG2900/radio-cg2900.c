/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Linux Wrapper for V4l2 FM Driver for CG2900.
 *
 * Author: Hemant Gupta <hemant.gupta@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include<linux/init.h>
#include<linux/videodev2.h>
#include<media/v4l2-ioctl.h>
#include<media/v4l2-common.h>
#include<linux/module.h>
#include <linux/platform_device.h>
#include<linux/string.h>
#include<linux/wait.h>
#include"cg2900.h"
#include"cg2900_fm_driver.h"

#define RADIO_CG2900_VERSION KERNEL_VERSION(1, 1, 0)
#define BANNER "ST-Ericsson FM Radio Card driver v1.1.0"

#define FMR_HZ_TO_MHZ_CONVERTER				1000000
#define FMR_EU_US_LOW_FREQ_IN_MHZ			87.5
#define FMR_EU_US_HIGH_FREQ_IN_MHZ			108
#define FMR_JAPAN_LOW_FREQ_IN_MHZ			76
#define FMR_JAPAN_HIGH_FREQ_IN_MHZ			90
#define FMR_CHINA_LOW_FREQ_IN_MHZ			70
#define FMR_CHINA_HIGH_FREQ_IN_MHZ			108
#define FMR_MAX_BLOCK_SCAN_CHANNELS			198
#define FMR_CHINA_GRID_IN_HZ				50000
#define FMR_EUROPE_GRID_IN_HZ				100000
#define FMR_USA_GRID_IN_HZ				200000
#define FMR_AF_SWITCH_DATA_SIZE				2
#define FMR_BLOCK_SCAN_DATA_SIZE			2
#define FMR_GET_INTERRUPT_DATA_SIZE			2
#define FMR_TEST_TONE_CONNECT_DATA_SIZE			2
#define FMR_TEST_TONE_SET_PARAMS_DATA_SIZE		6

/* freq in Hz to V4l2 freq (units of 62.5Hz) */
#define HZ_TO_V4L2(X)  (2*(X)/125)
/* V4l2 freq (units of 62.5Hz) to freq in Hz */
#define V4L2_TO_HZ(X)  (((X)*125)/(2))

extern struct cg2900_version_info version_info;

static int cg2900_open(
			struct file *file
			);
static int cg2900_release(
			struct file *file
			);
static ssize_t cg2900_read(
			struct file *file,
			char __user *data,
			size_t count,
			loff_t *pos
			);
static unsigned int cg2900_poll(
			struct file *file,
			struct poll_table_struct *wait
			);
static int vidioc_querycap(
			struct file *file,
			void *priv,
			struct v4l2_capability *query_caps
			);
static int vidioc_get_tuner(
			struct file *file,
			void *priv,
			struct v4l2_tuner *tuner
			);
static int vidioc_set_tuner(
			struct file *file,
			void *priv,
			struct v4l2_tuner *tuner
			);
static int vidioc_get_modulator(
			struct file *file,
			void *priv,
			struct v4l2_modulator *modulator
			);
static int vidioc_set_modulator(
			struct file *file,
			void *priv,
			struct v4l2_modulator *modulator
			);
static int vidioc_get_frequency(
			struct file *file,
			void *priv,
			struct v4l2_frequency *freq
			);
static int vidioc_set_frequency(
			struct file *file,
			void *priv,
			struct v4l2_frequency *freq
			);
static int vidioc_query_ctrl(
			struct file *file,
			void *priv,
			struct v4l2_queryctrl *query_ctrl
			);
static int vidioc_get_ctrl(
			struct file *file,
			void *priv,
			struct v4l2_control *ctrl
			);
static int vidioc_set_ctrl(
			struct file *file,
			void *priv,
			struct v4l2_control *ctrl
			);
static int vidioc_get_ext_ctrls(
			struct file *file,
			void *priv,
			struct v4l2_ext_controls *ext_ctrl
			);
static int vidioc_set_ext_ctrls(
			struct file *file,
			void *priv,
			struct v4l2_ext_controls *ext_ctrl
			);
static int vidioc_set_hw_freq_seek(
			struct file *file,
			void *priv,
			struct v4l2_hw_freq_seek *freq_seek
			);
static int vidioc_get_audio(
			struct file *file,
			void *priv,
			struct v4l2_audio *audio
			);
static int vidioc_set_audio(
			struct file *file,
			void *priv,
			struct v4l2_audio *audio
			);
static int vidioc_get_input(
			struct file *filp,
			void *priv,
			unsigned int *input
			);
static int vidioc_set_input(
			struct file *filp,
			void *priv,
			unsigned int input
			);
static void cg2900_convert_err_to_v4l2(
			char status_byte,
			char *out_byte
			);
static int cg2900_map_event_to_v4l2(
			u8 fm_event
			);

static u32 freq_low;
static u32 freq_high;

/* Module Parameters */
static int radio_nr = -1;
static int grid;
static int band;

/* cg2900_poll_queue - Main Wait Queue for polling (Scan/Seek) */
static wait_queue_head_t cg2900_poll_queue;

struct sk_buff_head		fm_interrupt_queue;

/**
 * enum fm_seek_status - Seek status of FM Radio.
 *
 * @FMR_SEEK_NONE: No seek in progress.
 * @FMR_SEEK_IN_PROGRESS: Seek is in progress.
 *
 * Seek status of FM Radio.
 */
enum fm_seek_status {
	FMR_SEEK_NONE,
	FMR_SEEK_IN_PROGRESS
};

/**
 * enum fm_power_state - Power states of FM Radio.
 *
 * @FMR_SWITCH_OFF: FM Radio is switched off.
 * @FMR_SWITCH_ON: FM Radio is switched on.
 * @FMR_STANDBY: FM Radio in standby state.
 *
 * Power states of FM Radio.
 */
enum fm_power_state {
	FMR_SWITCH_OFF,
	FMR_SWITCH_ON,
	FMR_STANDBY
};

/**
 * struct cg2900_device - Stores FM Device Info.
 *
 * @state: state of FM Radio
 * @muted: FM Radio Mute/Unmute status
 * @seekstatus: seek status
 * @rx_rds_enabled: Rds enable/disable status for FM Rx
 * @tx_rds_enabled: Rds enable/disable status for FM Tx
 * @rx_stereo_status: Stereo Mode status for FM Rx
 * @tx_stereo_status: Stereo Mode status for FM Tx
 * @volume: Analog Volume Gain of FM Radio
 * @rssi_threshold: rssi Thresold set on FM Radio
 * @frequency: Frequency tuned on FM Radio in V4L2 Format
 * @audiopath: Audio Balance
 * @wait_on_read_queue: Flag for waiting on read queue.
 * @fm_mode: Enum for storing the current FM Mode.
 *
 * FM Driver Information Structure.
 */
struct cg2900_device {
	u8 state;
	u8 muted;
	u8 seekstatus;
	bool rx_rds_enabled;
	bool tx_rds_enabled;
	bool rx_stereo_status;
	bool tx_stereo_status;
	int volume;
	u16 rssi_threshold;
	u32 frequency;
	u32 audiopath;
	bool wait_on_read_queue;
	enum cg2900_fm_mode fm_mode;
};

/* Global Structure to store the maintain FM Driver device info */
static struct cg2900_device cg2900_device;

/* V4L2 Video Device Structure pointer */
static struct video_device *cg2900_video_device;

/* V4l2 File Operation Structure */
static const struct v4l2_file_operations cg2900_fops = {
	.owner = THIS_MODULE,
	.open = cg2900_open,
	.release = cg2900_release,
	.read = cg2900_read,
	.poll = cg2900_poll,
	.ioctl = video_ioctl2,
};

/* V4L2 IOCTL Operation Structure */
static const struct v4l2_ioctl_ops cg2900_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_g_tuner = vidioc_get_tuner,
	.vidioc_s_tuner = vidioc_set_tuner,
	.vidioc_g_modulator = vidioc_get_modulator,
	.vidioc_s_modulator = vidioc_set_modulator,
	.vidioc_g_frequency = vidioc_get_frequency,
	.vidioc_s_frequency = vidioc_set_frequency,
	.vidioc_queryctrl = vidioc_query_ctrl,
	.vidioc_g_ctrl = vidioc_get_ctrl,
	.vidioc_s_ctrl = vidioc_set_ctrl,
	.vidioc_g_ext_ctrls = vidioc_get_ext_ctrls,
	.vidioc_s_ext_ctrls = vidioc_set_ext_ctrls,
	.vidioc_s_hw_freq_seek = vidioc_set_hw_freq_seek,
	.vidioc_g_audio = vidioc_get_audio,
	.vidioc_s_audio = vidioc_set_audio,
	.vidioc_g_input = vidioc_get_input,
	.vidioc_s_input = vidioc_set_input,
};

static u16 no_of_scan_freq;
static u16 no_of_block_scan_freq;
static u32 scanfreq_rssi_level[MAX_CHANNELS_TO_SCAN];
static u16 block_scan_rssi_level[MAX_CHANNELS_FOR_BLOCK_SCAN];
static u32 scanfreq[MAX_CHANNELS_TO_SCAN];
static struct mutex fm_mutex;
static spinlock_t fm_spinlock;
static int users;

/**
 * vidioc_querycap()- Query FM Driver Capabilities.
 *
 * This function is used to query the capabilities of the
 * FM Driver. This function is called when the application issues the IOCTL
 * VIDIOC_QUERYCAP.
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @query_caps: v4l2_capability structure.
 *
 * Returns: 0
 */
static int vidioc_querycap(
			struct file *file,
			void *priv,
			struct v4l2_capability *query_caps
			)
{
	FM_INFO_REPORT("vidioc_querycap");
	memset(
			query_caps,
			0,
			sizeof(*query_caps)
			);
	strlcpy(
			query_caps->driver,
			"CG2900 Driver",
			sizeof(query_caps->driver)
			);
	strlcpy(
			query_caps->card,
			"CG2900 FM Radio",
			sizeof(query_caps->card)
			);
	strcpy(
			query_caps->bus_info,
			"platform"
			);
	query_caps->version = RADIO_CG2900_VERSION;
	query_caps->capabilities =
			V4L2_CAP_TUNER |
			V4L2_CAP_MODULATOR |
			V4L2_CAP_RADIO  |
			V4L2_CAP_READWRITE |
			V4L2_CAP_RDS_CAPTURE |
			V4L2_CAP_HW_FREQ_SEEK |
			V4L2_CAP_RDS_OUTPUT;
	FM_DEBUG_REPORT("vidioc_querycap returning 0");
	return 0;
}

/**
 * vidioc_get_tuner()- Get FM Tuner Features.
 *
 * This function is used to get the tuner features.
 * This function is called when the application issues the IOCTL
 * VIDIOC_G_TUNER
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @tuner: v4l2_tuner structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_get_tuner(
			struct file *file,
			void *priv,
			struct v4l2_tuner *tuner
			)
{
	int status = 0;
	u8 mode;
	bool rds_enabled;
	u16 rssi;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_get_tuner");

	if (tuner->index > 0) {
		FM_ERR_REPORT("vidioc_get_tuner: Only 1 tuner supported");
		goto error;
	}

	memset(tuner, 0, sizeof(*tuner));
	strcpy(tuner->name, "CG2900 FM Receiver");
	tuner->type = V4L2_TUNER_RADIO;
	tuner->rangelow = HZ_TO_V4L2(freq_low);
	tuner->rangehigh = HZ_TO_V4L2(freq_high);
	tuner->capability =
			V4L2_TUNER_CAP_LOW /* Frequency steps = 1/16 kHz */
			| V4L2_TUNER_CAP_STEREO	/* Can receive stereo */
			| V4L2_TUNER_CAP_RDS;	/* Supports RDS Capture  */

	if (cg2900_device.fm_mode == CG2900_FM_RX_MODE) {

		status = cg2900_fm_get_mode(&mode);

		FM_DEBUG_REPORT("vidioc_get_tuner: mode = %x, ", mode);

		if (0 != status) {
			/* Get mode API failed, set mode to mono */
			tuner->audmode = V4L2_TUNER_MODE_MONO;
			tuner->rxsubchans = V4L2_TUNER_SUB_MONO;
			goto error;
		}

		switch (mode) {
		case CG2900_MODE_STEREO:
			tuner->audmode = V4L2_TUNER_MODE_STEREO;
			tuner->rxsubchans = V4L2_TUNER_SUB_STEREO;
			break;
		case CG2900_MODE_MONO:
		default:
			tuner->audmode = V4L2_TUNER_MODE_MONO;
			tuner->rxsubchans = V4L2_TUNER_SUB_MONO;
			break;
		}

		status = cg2900_fm_get_rds_status(&rds_enabled);

		if (0 != status) {
			tuner->rxsubchans &= ~V4L2_TUNER_SUB_RDS;
			goto error;
		}

		if (rds_enabled)
			tuner->rxsubchans |= V4L2_TUNER_SUB_RDS;
		else
			tuner->rxsubchans &= ~V4L2_TUNER_SUB_RDS;
	} else {
		tuner->audmode = V4L2_TUNER_MODE_MONO;
		tuner->rxsubchans = V4L2_TUNER_SUB_MONO;
	}

	if (cg2900_device.fm_mode == CG2900_FM_RX_MODE) {
		status = cg2900_fm_get_signal_strength(&rssi);

		if (0 != status) {
			tuner->signal = 0;
			goto error;
		}
		tuner->signal = rssi;
	} else {
		tuner->signal = 0;
	}

	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_get_tuner: returning %d", ret_val);
	return ret_val;
}

/**
 * vidioc_set_tuner()- Set FM Tuner Features.
 *
 * This function is used to set the tuner features.
 * It also sets the default FM Rx settings.
 * This function is called when the application issues the IOCTL
 * VIDIOC_S_TUNER
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @tuner: v4l2_tuner structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_set_tuner(
			struct file *file,
			void *priv,
			struct v4l2_tuner *tuner
			)
{
	bool rds_status = false;
	bool stereo_status = false;
	int status = 0;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_set_tuner");
	if (tuner->index != 0) {
		FM_ERR_REPORT("vidioc_set_tuner: Only 1 tuner supported");
		goto error;
	}

	if (cg2900_device.fm_mode != CG2900_FM_RX_MODE) {
		/*
		 * FM Rx mode should be configured
		 * as earlier mode was not FM Rx
		 */
		if (CG2900_FM_BAND_US_EU == band) {
			freq_low = FMR_EU_US_LOW_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
			freq_high = FMR_EU_US_HIGH_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
		} else if (CG2900_FM_BAND_JAPAN == band) {
			freq_low = FMR_JAPAN_LOW_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
			freq_high = FMR_JAPAN_HIGH_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
		} else if (CG2900_FM_BAND_CHINA == band) {
			freq_low = FMR_CHINA_LOW_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
			freq_high = FMR_CHINA_HIGH_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
		}
		cg2900_device.fm_mode = CG2900_FM_RX_MODE;
		cg2900_device.rx_rds_enabled =
		    (tuner->rxsubchans & V4L2_TUNER_SUB_RDS) ?
		    true : false;
		if (tuner->rxsubchans & V4L2_TUNER_SUB_STEREO)
			stereo_status = true;
		else if (tuner->rxsubchans & V4L2_TUNER_SUB_MONO)
			stereo_status = false;
		cg2900_device.rx_stereo_status = stereo_status;
		status = cg2900_fm_set_rx_default_settings(freq_low,
				band,
				grid,
				cg2900_device.rx_rds_enabled,
				cg2900_device.rx_stereo_status);

		if (0 != status) {
			FM_ERR_REPORT("vidioc_set_tuner: "
				"cg2900_fm_set_rx_default_settings returned "
				" %d", status);
			goto error;
		}
		status = cg2900_fm_set_rssi_threshold(
				cg2900_device.rssi_threshold);
		if (0 != status) {
			FM_ERR_REPORT("vidioc_set_tuner: "
					"cg2900_fm_set_rssi_threshold returned "
					" %d", status);
			goto error;
		}
	} else {
		/*
		 * Mode was FM Rx only, change the RDS settings or stereo mode
		 * if they are changed by application
		 */
		rds_status = (tuner->rxsubchans & V4L2_TUNER_SUB_RDS) ?
		    true : false;
		if (tuner->rxsubchans & V4L2_TUNER_SUB_STEREO)
			stereo_status = true;
		else if (tuner->rxsubchans & V4L2_TUNER_SUB_MONO)
			stereo_status = false;
		if (stereo_status != cg2900_device.rx_stereo_status) {
			cg2900_device.rx_stereo_status = stereo_status;
			if (stereo_status)
				status =
				    cg2900_fm_set_mode(
						FMD_STEREOMODE_BLENDING);
			else
				status = cg2900_fm_set_mode(
						FMD_STEREOMODE_MONO);

			if (0 != status) {
				FM_ERR_REPORT("vidioc_set_tuner: "
						"cg2900_fm_set_mode returned "
						" %d", status);
				goto error;
			}
		}
		if (rds_status != cg2900_device.rx_rds_enabled) {
			cg2900_device.rx_rds_enabled = rds_status;
			if (rds_status)
				status = cg2900_fm_rds_on();
			else
				status = cg2900_fm_rds_off();

			if (0 != status) {
				FM_ERR_REPORT("vidioc_set_tuner: "
						"cg2900_fm_rds returned "
						" %d", status);
				goto error;
			}
		}
	}

	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_set_tuner: returning %d", ret_val);
	return ret_val;
}

/**
 * vidioc_get_modulator()- Get FM Modulator Features.
 *
 * This function is used to get the modulator features.
 * This function is called when the application issues the IOCTL
 * VIDIOC_G_MODULATOR
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @modulator: v4l2_modulator structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_get_modulator(
			struct file *file,
			void *priv,
			struct v4l2_modulator *modulator
			)
{
	int status = 0;
	bool rds_enabled;
	u8 mode;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_get_modulator");

	if (modulator->index > 0) {
		FM_ERR_REPORT("vidioc_get_modulator: Only 1 "
			      "modulator supported");
		goto error;
	}

	memset(modulator, 0, sizeof(*modulator));
	strcpy(modulator->name, "CG2900 FM Transmitter");
	modulator->rangelow = freq_low;
	modulator->rangehigh = freq_high;
	modulator->capability = V4L2_TUNER_CAP_NORM /* Freq steps = 1/16 kHz */
	    | V4L2_TUNER_CAP_STEREO	/* Can receive stereo */
	    | V4L2_TUNER_CAP_RDS;	/* Supports RDS Capture  */

	if (cg2900_device.fm_mode == CG2900_FM_TX_MODE) {
		status = cg2900_fm_get_mode(&mode);
		FM_DEBUG_REPORT("vidioc_get_modulator: mode = %x", mode);
		if (0 != status) {
			/* Get mode API failed, set mode to mono */
			modulator->txsubchans = V4L2_TUNER_SUB_MONO;
			goto error;
		}
		switch (mode) {
			/* Stereo */
		case CG2900_MODE_STEREO:
			modulator->txsubchans = V4L2_TUNER_SUB_STEREO;
			break;
			/* Mono */
		case CG2900_MODE_MONO:
			modulator->txsubchans = V4L2_TUNER_SUB_MONO;
			break;
			/* Switching or Blending, set mode as Stereo */
		default:
			modulator->txsubchans = V4L2_TUNER_SUB_STEREO;
		}
		status = cg2900_fm_get_rds_status(&rds_enabled);
		if (0 != status) {
			modulator->txsubchans &= ~V4L2_TUNER_SUB_RDS;
			goto error;
		}
		if (rds_enabled)
			modulator->txsubchans |= V4L2_TUNER_SUB_RDS;
		else
			modulator->txsubchans &= ~V4L2_TUNER_SUB_RDS;
	} else
		modulator->txsubchans = V4L2_TUNER_SUB_MONO;

	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_get_modulator: returning %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_set_modulator()- Set FM Modulator Features.
 *
 * This function is used to set the Modulaotr features.
 * It also sets the default FM Tx settings.
 * This function is called when the application issues the IOCTL
 * VIDIOC_S_MODULATOR
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @modulator: v4l2_modulator structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_set_modulator(
			struct file *file,
			void *priv,
			struct v4l2_modulator *modulator
			)
{
	bool rds_status = false;
	bool stereo_status = false;
	int status = 0;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_set_modulator");
	if (modulator->index != 0) {
		FM_ERR_REPORT("vidioc_set_modulator: Only 1 "
			      "modulator supported");
		goto error;
	}

	if (cg2900_device.fm_mode != CG2900_FM_TX_MODE) {
		/*
		 * FM Tx mode should be configured as
		 * earlier mode was not FM Tx
		 */
		if (band == CG2900_FM_BAND_US_EU) {
			freq_low = FMR_EU_US_LOW_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
			freq_high = FMR_EU_US_HIGH_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
		} else if (band == CG2900_FM_BAND_JAPAN) {
			freq_low = FMR_JAPAN_LOW_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
			freq_high = FMR_JAPAN_HIGH_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
		} else if (band == CG2900_FM_BAND_CHINA) {
			freq_low = FMR_CHINA_LOW_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
			freq_high = FMR_CHINA_HIGH_FREQ_IN_MHZ *
			    FMR_HZ_TO_MHZ_CONVERTER;
		}
		cg2900_device.fm_mode = CG2900_FM_TX_MODE;
		cg2900_device.rx_rds_enabled = false;
		cg2900_device.tx_rds_enabled =
		    (modulator->txsubchans & V4L2_TUNER_SUB_RDS) ?
		    true : false;
		if (modulator->txsubchans & V4L2_TUNER_SUB_STEREO)
			stereo_status = true;
		else if (modulator->txsubchans & V4L2_TUNER_SUB_MONO)
			stereo_status = false;
		cg2900_device.tx_stereo_status = stereo_status;

		status = cg2900_fm_set_tx_default_settings(freq_low,
					       band,
					       grid,
					       cg2900_device.tx_rds_enabled,
					       cg2900_device.
					       tx_stereo_status);

		if (0 != status) {
			FM_ERR_REPORT("vidioc_set_modulator: "
				"cg2900_fm_set_tx_default_settings returned "
				" %d", status);
			goto error;
		}
	} else {
		/*
		 * Mode was FM Tx only, change the RDS settings or stereo mode
		 * if they are changed by application
		 */
		rds_status = (modulator->txsubchans & V4L2_TUNER_SUB_RDS) ?
		    true : false;
		if (modulator->txsubchans & V4L2_TUNER_SUB_STEREO)
			stereo_status = true;
		else if (modulator->txsubchans & V4L2_TUNER_SUB_MONO)
			stereo_status = false;
		if (stereo_status != cg2900_device.tx_stereo_status) {
			cg2900_device.tx_stereo_status = stereo_status;
			status = cg2900_fm_set_mode(stereo_status);
			if (0 != status) {
				FM_ERR_REPORT("vidioc_set_modulator: "
						"cg2900_fm_set_mode returned "
						" %d", status);
				goto error;
			}
		}
		if (rds_status != cg2900_device.tx_rds_enabled) {
			cg2900_device.tx_rds_enabled = rds_status;
			status = cg2900_fm_tx_rds(rds_status);
			if (0 != status) {
				FM_ERR_REPORT("vidioc_set_modulator: "
						"cg2900_fm_tx_rds returned "
						" %d", status);
				goto error;
			}
		}
	}

	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_set_modulator: returning %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_get_frequency()- Get the Current FM Frequnecy.
 *
 * This function is used to get the currently tuned
 * frequency on FM Radio. This function is called when the application
 * issues the IOCTL VIDIOC_G_FREQUENCY
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @freq: v4l2_frequency structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_get_frequency(
			struct file *file,
			void *priv,
			struct v4l2_frequency *freq
			)
{
	int status;
	u32 frequency;
	int ret_val = -EINVAL;
	struct sk_buff *skb;

	FM_INFO_REPORT("vidioc_get_frequency: Status = %d",
			cg2900_device.seekstatus);

	status = cg2900_fm_get_frequency(&frequency);

	if (0 != status) {
		freq->frequency = cg2900_device.frequency;
		goto error;
	}

	if (cg2900_device.seekstatus == FMR_SEEK_IN_PROGRESS) {
		if (skb_queue_empty(&fm_interrupt_queue)) {
			/* No Interrupt, bad case */
			FM_ERR_REPORT("vidioc_get_frequency: "
				"No Interrupt to read");
			fm_event = CG2900_EVENT_NO_EVENT;
			goto error;
		}
		spin_lock(&fm_spinlock);
		skb = skb_dequeue(&fm_interrupt_queue);
		spin_unlock(&fm_spinlock);
		if (!skb) {
			/* No Interrupt, bad case */
			FM_ERR_REPORT("vidioc_get_frequency: "
				"No Interrupt to read");
			fm_event = CG2900_EVENT_NO_EVENT;
			goto error;
		}
		fm_event = (u8)skb->data[0];
		FM_DEBUG_REPORT("vidioc_get_frequency: Interrupt = %x",
				fm_event);
		/*  Check if seek is finished or not */
		if (CG2900_EVENT_SEARCH_CHANNEL_FOUND == fm_event) {
			/* seek is finished */
			spin_lock(&fm_spinlock);
			cg2900_device.frequency = HZ_TO_V4L2(frequency);
			freq->frequency = cg2900_device.frequency;
			cg2900_device.seekstatus = FMR_SEEK_NONE;
			fm_event = CG2900_EVENT_NO_EVENT;
			kfree_skb(skb);
			spin_unlock(&fm_spinlock);
		} else {
			/* Some other interrupt, queue it back */
			spin_lock(&fm_spinlock);
			skb_queue_head(&fm_interrupt_queue, skb);
			spin_unlock(&fm_spinlock);
		}
	} else {
		spin_lock(&fm_spinlock);
		cg2900_device.frequency = HZ_TO_V4L2(frequency);
		freq->frequency = cg2900_device.frequency;
		spin_unlock(&fm_spinlock);
	}
	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_get_frequency: returning = %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_set_frequency()- Set the FM Frequnecy.
 *
 * This function is used to set the frequency
 * on FM Radio. This function is called when the application
 * issues the IOCTL VIDIOC_S_FREQUENCY
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @freq: v4l2_frequency structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_set_frequency(
			struct file *file,
			void *priv,
			struct v4l2_frequency *freq
			)
{
	u32 frequency = freq->frequency;
	u32 freq_low, freq_high;
	int status;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_set_frequency: Frequency = "
			"%d ", V4L2_TO_HZ(frequency));

	/*  Check which band is set currently */
	switch (band) {
	case CG2900_FM_BAND_US_EU:
		freq_low = FMR_EU_US_LOW_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		freq_high = FMR_EU_US_HIGH_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		break;

	case CG2900_FM_BAND_CHINA:
		freq_low = FMR_CHINA_LOW_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		freq_high = FMR_CHINA_HIGH_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		break;

	case CG2900_FM_BAND_JAPAN:
		freq_low = FMR_JAPAN_LOW_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		freq_high = FMR_JAPAN_HIGH_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		break;

	default:
		/* Set to US_MAX and CHINA_MIN band */
		freq_low = FMR_CHINA_LOW_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
		freq_high = FMR_EU_US_HIGH_FREQ_IN_MHZ *
			FMR_HZ_TO_MHZ_CONVERTER;
	}

	/* Check if the frequency set is out of current band */
	if ((V4L2_TO_HZ(frequency) < freq_low) ||
		(V4L2_TO_HZ(frequency) > freq_high))
		goto error;

	spin_lock(&fm_spinlock);
	fm_event = CG2900_EVENT_NO_EVENT;
	no_of_scan_freq = 0;
	spin_unlock(&fm_spinlock);

	cg2900_device.seekstatus = FMR_SEEK_NONE;
	cg2900_device.frequency = frequency;
	status = cg2900_fm_set_frequency(V4L2_TO_HZ(frequency));

	if (0 != status)
		goto error;

	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_set_frequency: returning = %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_query_ctrl()- Query the FM Driver control features.
 *
 * This function is used to query the control features on FM Radio.
 * This function is called when the application
 * issues the IOCTL VIDIOC_QUERYCTRL
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @query_ctrl: v4l2_queryctrl structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_query_ctrl(
			struct file *file,
			void *priv,
			struct v4l2_queryctrl *query_ctrl
			)
{
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_query_ctrl");
	/* Check which control is requested */
	switch (query_ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		FM_DEBUG_REPORT("vidioc_query_ctrl:  V4L2_CID_AUDIO_MUTE");
		query_ctrl->type = V4L2_CTRL_TYPE_BOOLEAN;
		query_ctrl->minimum = 0;
		query_ctrl->maximum = 1;
		query_ctrl->step = 1;
		query_ctrl->default_value = 0;
		query_ctrl->flags = 0;
		strncpy(query_ctrl->name, "CG2900 Mute", 32);
		ret_val = 0;
		break;

	case V4L2_CID_AUDIO_VOLUME:
		FM_DEBUG_REPORT("vidioc_query_ctrl: V4L2_CID_AUDIO_VOLUME");

		strncpy(query_ctrl->name, "CG2900 Volume", 32);
		query_ctrl->minimum = MIN_ANALOG_VOLUME;
		query_ctrl->maximum = MAX_ANALOG_VOLUME;
		query_ctrl->step = 1;
		query_ctrl->default_value = MAX_ANALOG_VOLUME;
		query_ctrl->flags = 0;
		query_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
		ret_val = 0;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		FM_DEBUG_REPORT("vidioc_query_ctrl:   V4L2_CID_AUDIO_BALANCE ");
		strncpy(query_ctrl->name, "CG2900 Audio Balance", 32);
		query_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
		query_ctrl->minimum = 0x0000;
		query_ctrl->maximum = 0xFFFF;
		query_ctrl->step = 0x0001;
		query_ctrl->default_value = 0x0000;
		query_ctrl->flags = 0;
		ret_val = 0;
		break;

	case V4L2_CID_AUDIO_BASS:
		FM_DEBUG_REPORT("vidioc_query_ctrl: "
				"V4L2_CID_AUDIO_BASS (unsupported)");
		break;

	case V4L2_CID_AUDIO_TREBLE:
		FM_DEBUG_REPORT("vidioc_query_ctrl: "
				"V4L2_CID_AUDIO_TREBLE (unsupported)");
		break;

	default:
		FM_DEBUG_REPORT("vidioc_query_ctrl: "
				"--> unsupported id = %x", query_ctrl->id);
		break;
	}

	FM_DEBUG_REPORT("vidioc_query_ctrl: returning = %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_get_ctrl()- Get the value of a particular Control.
 *
 * This function is used to get the value of a
 * particular control from the FM Driver. This function is called
 * when the application issues the IOCTL VIDIOC_G_CTRL
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @ctrl: v4l2_control structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_get_ctrl(
			struct file *file,
			void *priv,
			struct v4l2_control *ctrl
			)
{
	int status;
	u8 value;
	u16 rssi;
	u8 antenna;
	u16 conclusion;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_get_ctrl");

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		status = cg2900_fm_get_volume(&value);
		if (0 == status) {
			ctrl->value = value;
			cg2900_device.volume = value;
			ret_val = 0;
		}
		break;
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = cg2900_device.muted;
		ret_val = 0;
		break;
	case V4L2_CID_AUDIO_BALANCE:
		ctrl->value = cg2900_device.audiopath;
		ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_RSSI_THRESHOLD:
		ctrl->value = cg2900_device.rssi_threshold;
		ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_SELECT_ANTENNA:
		status = cg2900_fm_get_antenna(&antenna);
		FM_DEBUG_REPORT("vidioc_get_ctrl: Antenna = %x", antenna);
		if (0 == status) {
			ctrl->value = antenna;
			ret_val = 0;
		}
		break;
	case V4L2_CID_CG2900_RADIO_RDS_AF_UPDATE_GET_RESULT:
		status = cg2900_fm_af_update_get_result(&rssi);
		FM_DEBUG_REPORT("vidioc_get_ctrl: AF RSSI Level = %x", rssi);
		if (0 == status) {
			ctrl->value = rssi;
			ret_val = 0;
		}
		break;
	case V4L2_CID_CG2900_RADIO_RDS_AF_SWITCH_GET_RESULT:
		status = cg2900_fm_af_switch_get_result(&conclusion);
		FM_DEBUG_REPORT("vidioc_get_ctrl: AF Switch conclusion = %x",
				conclusion);
		if (0 != status)
			break;
		if (conclusion == 0) {
			ctrl->value = conclusion;
			FM_DEBUG_REPORT("vidioc_get_ctrl: "
					"AF Switch conclusion = %d",
					ctrl->value);
			ret_val = 0;
		} else {
			/*
			 * Convert positive error code returned by chip
			 * into negative error codes to be in line with linux.
			 */
			ctrl->value = -conclusion;
			FM_ERR_REPORT("vidioc_get_ctrl: "
			"AF-Switch failed with value %d", ctrl->value);
			ret_val = 0;
		}
		break;
	default:
		FM_DEBUG_REPORT("vidioc_get_ctrl: "
				"unsupported (id = %x)", (int)ctrl->id);
		ret_val = -EINVAL;
	}
	FM_DEBUG_REPORT("vidioc_get_ctrl: returning = %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_set_ctrl()- Set the value of a particular Control.
 *
 * This function is used to set the value of a
 * particular control from the FM Driver. This function is called when the
 * application issues the IOCTL VIDIOC_S_CTRL
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @ctrl: v4l2_control structure.
 *
 * Returns:
 *   0 when no error
 *   -ERANGE when the parameter is out of range.
 *   -EINVAL: otherwise
 */
static int vidioc_set_ctrl(
			struct file *file,
			void *priv,
			struct v4l2_control *ctrl
			)
{
	int status;
	int ret_val = -EINVAL;
	FM_INFO_REPORT("vidioc_set_ctrl");
	/* Check which control is requested */
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_AUDIO_MUTE, "
				"value = %d", ctrl->value);
		if (ctrl->value > 1 && ctrl->value < 0) {
			ret_val =  -ERANGE;
			break;
		}

		if (ctrl->value) {
			FM_DEBUG_REPORT("vidioc_set_ctrl: Ctrl_Id = "
					"V4L2_CID_AUDIO_MUTE, "
					"Muting the Radio");
			status = cg2900_fm_mute();
		} else {
			FM_DEBUG_REPORT("vidioc_set_ctrl: "
					"Ctrl_Id = V4L2_CID_AUDIO_MUTE, "
					"UnMuting the Radio");
			status = cg2900_fm_unmute();
		}
		if (0 == status) {
			cg2900_device.muted = ctrl->value;
			ret_val = 0;
		}
		break;
	case V4L2_CID_AUDIO_VOLUME:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_AUDIO_VOLUME, "
				"value = %d", ctrl->value);
		if(version_info.revision == CG2900_PG1_REV
				|| version_info.revision == CG2900_PG2_REV
				|| version_info.revision == CG2900_PG1_SPECIAL_REV) {
			if (ctrl->value > MAX_ANALOG_VOLUME &&
					ctrl->value < MIN_ANALOG_VOLUME) {
				ret_val = -ERANGE;
				break;
			}
			status = cg2900_fm_set_volume(ctrl->value);
			if (0 == status) {
				cg2900_device.volume = ctrl->value;
				ret_val = 0;
			}
		} else {
		    /* CG2905/10 - Use AUP_BT_SetVolume */
		    if (ctrl->value > MAX_ANALOG_VOLUME &&
		            ctrl->value < MIN_ANALOG_VOLUME) {
		        ret_val = -ERANGE;
		        break;
		    }
		    status = cg2900_fm_set_aup_bt_setvolume(ctrl->value);
		    if (!status) {
		        cg2900_device.volume = ctrl->value;
		        ret_val = 0;
		    }
		}
		break;
	case V4L2_CID_AUDIO_BALANCE:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_AUDIO_BALANCE, "
				"value = %d", ctrl->value);
		status = cg2900_fm_set_audio_balance(ctrl->value);
		if (0 == status) {
			cg2900_device.audiopath = ctrl->value;
			ret_val = 0;
		}
		break;
	case V4L2_CID_CG2900_RADIO_CHIP_STATE:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_CG2900_RADIO_CHIP_STATE, "
				"value = %d", ctrl->value);
		if (V4L2_CG2900_RADIO_STANDBY == ctrl->value)
			status = cg2900_fm_standby();
		else if (V4L2_CG2900_RADIO_POWERUP == ctrl->value)
			status = cg2900_fm_power_up_from_standby();
		else
			break;
		if (0 != status)
			break;
		if (V4L2_CG2900_RADIO_STANDBY == ctrl->value)
			cg2900_device.state = FMR_STANDBY;
		else if (V4L2_CG2900_RADIO_POWERUP == ctrl->value)
			cg2900_device.state = FMR_SWITCH_ON;
		ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_SELECT_ANTENNA:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_CG2900_RADIO_SELECT_ANTENNA, "
				"value = %d", ctrl->value);
		status = cg2900_fm_select_antenna(ctrl->value);
		if (0 == status)
			ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_BANDSCAN:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_CG2900_RADIO_BANDSCAN, "
				"value = %d", ctrl->value);
		if (V4L2_CG2900_RADIO_BANDSCAN_START == ctrl->value) {
			cg2900_device.seekstatus = FMR_SEEK_IN_PROGRESS;
			no_of_scan_freq = 0;
			status = cg2900_fm_start_band_scan();
		} else if (V4L2_CG2900_RADIO_BANDSCAN_STOP == ctrl->value) {
			status = cg2900_fm_stop_scan();
			cg2900_device.seekstatus = FMR_SEEK_NONE;
		} else
			break;
		if (0 == status)
			ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_RSSI_THRESHOLD:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_CG2900_RADIO_RSSI_THRESHOLD "
				"= %d", ctrl->value);
		status = cg2900_fm_set_rssi_threshold(ctrl->value);
		if (0 == status) {
			cg2900_device.rssi_threshold = ctrl->value;
			ret_val = 0;
		}
		break;
	case V4L2_CID_CG2900_RADIO_RDS_AF_UPDATE_START:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"V4L2_CID_CG2900_RADIO_RDS_AF_UPDATE_START "
				"freq = %d Hz", ctrl->value);
		status = cg2900_fm_af_update_start(ctrl->value);
		if (0 == status)
			ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_TEST_TONE_GENERATOR_SET_STATUS:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
			"V4L2_CID_CG2900_RADIO_TEST_TONE_GENERATOR_SET_STATUS "
			"state = %d ", ctrl->value);
		if (ctrl->value < V4L2_CG2900_RADIO_TEST_TONE_GEN_OFF ||
			ctrl->value >
			V4L2_CG2900_RADIO_TEST_TONE_GENERATOR_ON_WO_SRC) {
			FM_ERR_REPORT("Invalid parameter = %d", ctrl->value);
			break;
		}
		status = cg2900_fm_set_test_tone_generator(ctrl->value);
		if (0 == status)
			ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_TUNE_DEEMPHASIS:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
			"V4L2_CID_CG2900_RADIO_TUNE_DEEMPHASIS, "
			"Value = %d", ctrl->value);

		if ((V4L2_CG2900_RADIO_DEEMPHASIS_DISABLED >
			ctrl->value) ||
			(V4L2_CG2900_RADIO_DEEMPHASIS_75_uS <
			ctrl->value)) {
			FM_ERR_REPORT("Unsupported deemphasis = %d",
			ctrl->value);
			break;
		}

		switch (ctrl->value) {
		case V4L2_CG2900_RADIO_DEEMPHASIS_50_uS:
			ctrl->value = FMD_EMPHASIS_50US;
			break;
		case V4L2_CG2900_RADIO_DEEMPHASIS_75_uS:
			ctrl->value = FMD_EMPHASIS_75US;
			break;
		case V4L2_CG2900_RADIO_DEEMPHASIS_DISABLED:
			/* Drop Down */
		default:
			ctrl->value = FMD_EMPHASIS_NONE;
			break;
		}
		status = cg2900_fm_rx_set_deemphasis(ctrl->value);

		if (0 == status)
			ret_val = 0;
		break;
	default:
		FM_DEBUG_REPORT("vidioc_set_ctrl: "
				"unsupported (id = %x)", ctrl->id);
	}
	FM_DEBUG_REPORT("vidioc_set_ctrl: returning = %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_get_ext_ctrls()- Get the values of a particular control.
 *
 * This function is used to get the value of a
 * particular control from the FM Driver. This is used when the data to
 * be received is more than 1 paramter. This function is called when the
 * application issues the IOCTL VIDIOC_G_EXT_CTRLS
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @ext_ctrl: v4l2_ext_controls structure.
 *
 * Returns:
 *   0 when no error
 *  -ENOSPC: when there is no space to copy the data into the buffer provided
 * by application.
 *   -EINVAL: otherwise
 */
static int vidioc_get_ext_ctrls(
			struct file *file,
			void *priv,
			struct v4l2_ext_controls *ext_ctrl
			)
{
	u32 *dest_buffer;
	int index = 0;
	int count = 0;
	int ret_val = -EINVAL;
	int status;
	struct sk_buff *skb;
	u8 mode;
	s8 interrupt_success;
	int *fm_interrupt_buffer;

	FM_INFO_REPORT("vidioc_get_ext_ctrls: Id = %04x,"
			"ext_ctrl->ctrl_class = %04x",
			ext_ctrl->controls->id,
			ext_ctrl->ctrl_class);

	if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_FM_TX &&
	    ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
		FM_ERR_REPORT("vidioc_get_ext_ctrls: Unsupported "
			      "ctrl_class = %04x", ext_ctrl->ctrl_class);
		goto error;
	}

	switch (ext_ctrl->controls->id) {
	case V4L2_CID_CG2900_RADIO_BANDSCAN_GET_RESULTS:
		if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
			FM_ERR_REPORT("vidioc_get_ext_ctrls:  "
			"V4L2_CID_CG2900_RADIO_BANDSCAN_GET_RESULTS "
			"Unsupported ctrl_class = %04x",
			ext_ctrl->ctrl_class);
			break;
		}
		if (cg2900_device.seekstatus ==
			FMR_SEEK_IN_PROGRESS) {
			spin_lock(&fm_spinlock);
			skb = skb_dequeue(&fm_interrupt_queue);
			spin_unlock(&fm_spinlock);
			if (!skb) {
				/* No Interrupt, bad case */
				FM_ERR_REPORT("No Interrupt to read");
				fm_event = CG2900_EVENT_NO_EVENT;
				break;
			}
			fm_event = (u8)skb->data[0];
			FM_DEBUG_REPORT(
				"V4L2_CID_CG2900_RADIO"
				"_BANDSCAN_GET_RESULTS: "
				"fm_event = %x", fm_event);
			if (fm_event ==
				CG2900_EVENT_SCAN_CHANNELS_FOUND) {
				/* Check to get Scan Result */
				status =
					cg2900_fm_get_scan_result
					(&no_of_scan_freq, scanfreq,
					scanfreq_rssi_level);
				if (0 != status) {
					FM_ERR_REPORT
						("vidioc_get_ext_ctrls: "
						"cg2900_fm_get_scan_"
						"result: returned %d",
						status);
					kfree_skb(skb);
					break;
				}
				kfree_skb(skb);
			} else {
				/* Some other interrupt, Queue it back */
				spin_lock(&fm_spinlock);
				skb_queue_head(&fm_interrupt_queue, skb);
				spin_unlock(&fm_spinlock);
			}
		}
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"SeekStatus = %x, GlobalEvent = %x, "
				"numchannels = %x",
				cg2900_device.seekstatus,
				fm_event, no_of_scan_freq);

		if (ext_ctrl->controls->size == 0 &&
		    ext_ctrl->controls->string == NULL) {
			if (cg2900_device.seekstatus ==
			    FMR_SEEK_IN_PROGRESS &&
			    CG2900_EVENT_SCAN_CHANNELS_FOUND
			    == fm_event) {
				spin_lock(&fm_spinlock);
				ext_ctrl->controls->size =
				    no_of_scan_freq;
				cg2900_device.seekstatus
				    = FMR_SEEK_NONE;
				fm_event =
				    CG2900_EVENT_NO_EVENT;
				spin_unlock(&fm_spinlock);
				return -ENOSPC;
			}
		} else if (ext_ctrl->controls->string != NULL) {
			dest_buffer =
			    (u32 *) ext_ctrl->controls->string;
			while (index < no_of_scan_freq) {
				*(dest_buffer + count + 0) =
				    HZ_TO_V4L2(scanfreq[index]);
				*(dest_buffer + count + 1) =
				    scanfreq_rssi_level[index];
				count += 2;
				index++;
			}
			ret_val = 0;
		}
		break;
	case V4L2_CID_CG2900_RADIO_BLOCKSCAN_GET_RESULTS:
		if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
			FM_ERR_REPORT("vidioc_get_ext_ctrls:  "
			"V4L2_CID_CG2900_RADIO_BLOCKSCAN"
			"_GET_RESULTS "
			"Unsupported ctrl_class = %04x",
			ext_ctrl->ctrl_class);
			break;
		}
		if (cg2900_device.seekstatus == FMR_SEEK_IN_PROGRESS) {
			spin_lock(&fm_spinlock);
			skb = skb_dequeue(&fm_interrupt_queue);
			spin_unlock(&fm_spinlock);
			if (!skb) {
				/* No Interrupt, bad case */
				FM_ERR_REPORT("No Interrupt to read");
				fm_event = CG2900_EVENT_NO_EVENT;
				break;
			}
			fm_event = (u8)skb->data[0];
			FM_DEBUG_REPORT(
				"V4L2_CID_CG2900_RADIO_BLOCKSCAN"
				"GET_RESULTS: "
				"fm_event = %x", fm_event);
			if (fm_event ==
				CG2900_EVENT_BLOCK_SCAN_CHANNELS_FOUND) {
				/* Check for BlockScan Result */
				status =
					cg2900_fm_get_block_scan_result
					(&no_of_block_scan_freq,
					block_scan_rssi_level);
				if (0 != status) {
					FM_ERR_REPORT
						("vidioc_get_ext_ctrls: "
						"cg2900_fm_get_block_scan_"
						"result: returned %d",
						status);
					kfree_skb(skb);
					break;
				}
				kfree_skb(skb);
			} else {
				/* Some other interrupt,
				Queue it back */
				spin_lock(&fm_spinlock);
				skb_queue_head(&fm_interrupt_queue, skb);
				spin_unlock(&fm_spinlock);
			}
		}
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"SeekStatus = %x, GlobalEvent = %x, "
				"numchannels = %x",
				cg2900_device.seekstatus,
				fm_event, no_of_block_scan_freq);
		if (ext_ctrl->controls->size == 0 &&
		    ext_ctrl->controls->string == NULL) {
			if (cg2900_device.seekstatus ==
			    FMR_SEEK_IN_PROGRESS &&
			    CG2900_EVENT_BLOCK_SCAN_CHANNELS_FOUND
			    == fm_event) {
				spin_lock(&fm_spinlock);
				ext_ctrl->controls->size =
				    no_of_block_scan_freq;
				cg2900_device.seekstatus
				    = FMR_SEEK_NONE;
				fm_event =
				    CG2900_EVENT_NO_EVENT;
				spin_unlock(&fm_spinlock);
				return -ENOSPC;
			}
		} else if (ext_ctrl->controls->size >=
			   no_of_block_scan_freq &&
			   ext_ctrl->controls->string != NULL) {
			dest_buffer =
			    (u32 *) ext_ctrl->controls->string;
			while (index < no_of_block_scan_freq) {
				*(dest_buffer + index) =
				    block_scan_rssi_level
				    [index];
				index++;
			}
			ret_val = 0;
			return ret_val;
		}
		break;
	case V4L2_CID_RDS_TX_DEVIATION:
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"V4L2_CID_RDS_TX_DEVIATION");
		if (V4L2_CTRL_CLASS_FM_TX != ext_ctrl->ctrl_class) {
			FM_ERR_REPORT("Invalid Ctrl Class = %x",
				      ext_ctrl->ctrl_class);
			break;
		}
		status = cg2900_fm_tx_get_rds_deviation((u16 *) &
						     ext_ctrl->
						     controls->value);
		if (status == 0)
			ret_val = 0;
		break;
	case V4L2_CID_PILOT_TONE_ENABLED:
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"V4L2_CID_PILOT_TONE_ENABLED");
		if (V4L2_CTRL_CLASS_FM_TX != ext_ctrl->ctrl_class) {
			FM_ERR_REPORT("Invalid Ctrl Class = %x",
				      ext_ctrl->ctrl_class);
			break;
		}
		status = cg2900_fm_tx_get_pilot_tone_status(
				(bool *)&ext_ctrl->controls->value);
		if (status == 0)
			ret_val = 0;
		break;
	case V4L2_CID_PILOT_TONE_DEVIATION:
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"V4L2_CID_PILOT_TONE_DEVIATION");
		if (V4L2_CTRL_CLASS_FM_TX != ext_ctrl->ctrl_class) {
			FM_ERR_REPORT("Invalid Ctrl Class = %x",
				      ext_ctrl->ctrl_class);
			break;
		}
		status = cg2900_fm_tx_get_pilot_deviation(
				(u16 *)&ext_ctrl->controls->value);
		if (status == 0)
			ret_val = 0;
		break;
	case V4L2_CID_TUNE_PREEMPHASIS:
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"V4L2_CID_TUNE_PREEMPHASIS");
		if (V4L2_CTRL_CLASS_FM_TX != ext_ctrl->ctrl_class) {
			FM_ERR_REPORT("Invalid Ctrl Class = %x",
				      ext_ctrl->ctrl_class);
			break;
		}
		status = cg2900_fm_tx_get_preemphasis(
				(u8 *)&ext_ctrl->controls->value);
		if (status == 0)
			ret_val = 0;
		break;
	case V4L2_CID_TUNE_POWER_LEVEL:
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"V4L2_CID_TUNE_POWER_LEVEL");
		if (V4L2_CTRL_CLASS_FM_TX != ext_ctrl->ctrl_class) {
			FM_ERR_REPORT("Invalid Ctrl Class = %x",
				      ext_ctrl->ctrl_class);
			break;
		}
		status = cg2900_fm_tx_get_power_level(
				(u16 *)&ext_ctrl->controls->value);
		if (status == 0)
			ret_val = 0;
		break;
	case V4L2_CID_CG2900_RADIO_GET_INTERRUPT:
		if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
			FM_ERR_REPORT("vidioc_get_ext_ctrls:  "
			"V4L2_CID_CG2900_RADIO_GET_INTERRUPT "
			"Unsupported ctrl_class = %04x",
			ext_ctrl->ctrl_class);
			break;
		}
		if (ext_ctrl->controls->size != FMR_GET_INTERRUPT_DATA_SIZE ||
			ext_ctrl->controls->string == NULL) {
			FM_ERR_REPORT("vidioc_get_ext_ctrls:  "
			"V4L2_CID_CG2900_RADIO_GET_INTERRUPT "
			"Invalid parameters, ext_ctrl->controls->size = %x "
			"ext_ctrl->controls->string = %08x",
			ext_ctrl->controls->size,
			(unsigned int)ext_ctrl->controls->string);
			ret_val = -ENOSPC;
			break;
		}
		spin_lock(&fm_spinlock);
		skb = skb_dequeue(&fm_interrupt_queue);
		spin_unlock(&fm_spinlock);
		if (!skb) {
			/* No Interrupt, bad case */
			FM_ERR_REPORT("V4L2_CID_CG2900_RADIO_GET_INTERRUPT: "
				"No Interrupt to read");
			fm_event = CG2900_EVENT_NO_EVENT;
			break;
		}
		fm_event = (u8)skb->data[0];
		interrupt_success = (s8)skb->data[1];
		FM_DEBUG_REPORT("vidioc_get_ctrl: Interrupt = %x "
				"interrupt_success = %x",
				fm_event, interrupt_success);
		fm_interrupt_buffer =
		    (int *) ext_ctrl->controls->string;
		/* Interrupt that has occurred */
		*fm_interrupt_buffer = cg2900_map_event_to_v4l2(fm_event);

		/* Interrupt success or failed */
		if (interrupt_success) {
			/* Interrupt Success, return 0 */
			*(fm_interrupt_buffer + 1) = 0;
		} else {
			spin_lock(&fm_spinlock);
			no_of_scan_freq = 0;
			no_of_block_scan_freq = 0;
			spin_unlock(&fm_spinlock);
			cg2900_device.seekstatus = FMR_SEEK_NONE;
			/* Clear the Interrupt flag */
			fm_event = CG2900_EVENT_NO_EVENT;
			kfree_skb(skb);
			/* Interrupt Success, return negative error */
			*(fm_interrupt_buffer + 1) = -1;
			FM_ERR_REPORT("vidioc_get_ext_ctrls: Interrupt = %d "
				"failed with reason = %d",
				(*fm_interrupt_buffer),
				(*(fm_interrupt_buffer + 1)));
			/*
			 * Update return value, so that application
			 * can read the event failure reason.
			 */
			ret_val = 0;
			break;
		}

		if (CG2900_EVENT_MONO_STEREO_TRANSITION
			== fm_event) {
			/*
			 * In case of Mono/Stereo Interrupt,
			 * get the current value from chip
			 */
			status = cg2900_fm_get_mode(&mode);
			cg2900_device.rx_stereo_status = (bool)mode;
			/* Clear the Interrupt flag */
			fm_event = CG2900_EVENT_NO_EVENT;
			kfree_skb(skb);
		} else if (CG2900_EVENT_SCAN_CANCELLED ==
			fm_event) {
			/* Scan/Search cancelled by User */
			spin_lock(&fm_spinlock);
			no_of_scan_freq = 0;
			no_of_block_scan_freq = 0;
			spin_unlock(&fm_spinlock);
			cg2900_device.seekstatus = FMR_SEEK_NONE;
			/* Clear the Interrupt flag */
			fm_event = CG2900_EVENT_NO_EVENT;
			kfree_skb(skb);
		} else {
				/* Queue the interrupt back
					for later dequeuing */
				FM_DEBUG_REPORT("V4L2_CID_CG2900"
					"_RADIO_GET_INTERRUPT: "
					"Queuing the interrupt"
					"again to head of list");
				spin_lock(&fm_spinlock);
				skb_queue_head(&fm_interrupt_queue, skb);
				spin_unlock(&fm_spinlock);
		}
		ret_val = 0;
		break;
	default:
		FM_DEBUG_REPORT("vidioc_get_ext_ctrls: "
				"unsupported (id = %x)",
				ext_ctrl->controls->id);
	}

error:
	FM_DEBUG_REPORT("vidioc_get_ext_ctrls: returning = %d", ret_val);
	return ret_val;
}

/**
 * vidioc_set_ext_ctrls()- Set the values of a particular control.
 *
 * This function is used to set the value of a
 * particular control on the FM Driver. This is used when the data to
 * be set is more than 1 paramter. This function is called when the
 * application issues the IOCTL VIDIOC_S_EXT_CTRLS
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @ext_ctrl: v4l2_ext_controls structure.
 *
 * Returns:
 *   0 when no error
 *   -ENOSPC: when there is no space to copy the data into the buffer provided
 * by application.
 *   -EINVAL: otherwise
 */
static int vidioc_set_ext_ctrls(
			struct file *file,
			void *priv,
			struct v4l2_ext_controls *ext_ctrl
			)
{
	int ret_val = -EINVAL;
	int status;

	FM_INFO_REPORT("vidioc_set_ext_ctrls: Id = %04x, ctrl_class = %04x",
			ext_ctrl->controls->id, ext_ctrl->ctrl_class);

	if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_FM_TX &&
	    ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
		FM_ERR_REPORT("vidioc_set_ext_ctrls: Unsupported "
			      "ctrl_class = %04x", ext_ctrl->ctrl_class);
		goto error;
	}

	switch (ext_ctrl->controls->id) {
	case V4L2_CID_CG2900_RADIO_RDS_AF_SWITCH_START:
		{
			u32 af_switch_freq;
			u16 af_switch_pi;
			u32 *af_switch_buf;

			if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
				"V4L2_CID_CG2900_RADIO_RDS_AF_SWITCH_START "
				"Unsupported ctrl_class = %04x",
				ext_ctrl->ctrl_class);
				break;
			}

			if (ext_ctrl->controls->size !=
				FMR_AF_SWITCH_DATA_SIZE ||
				ext_ctrl->controls->string == NULL) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
				"V4L2_CID_CG2900_RADIO_RDS_AF_SWITCH_START "
				"Unsupported ctrl_class = %04x",
				ext_ctrl->ctrl_class);
				break;
			}

			af_switch_buf = (u32 *) ext_ctrl->controls->string;
			af_switch_freq = V4L2_TO_HZ(*af_switch_buf);
			af_switch_pi = *(af_switch_buf + 1);
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
				"V4L2_CID_CG2900_RADIO_RDS_AF_SWITCH_START: "
				"AF Switch Freq =%d Hz AF Switch PI = %04x",
				(int)af_switch_freq, af_switch_pi);

			if (af_switch_freq < (FMR_CHINA_LOW_FREQ_IN_MHZ
				* FMR_HZ_TO_MHZ_CONVERTER) ||
				af_switch_freq > (FMR_CHINA_HIGH_FREQ_IN_MHZ
				* FMR_HZ_TO_MHZ_CONVERTER)) {
				FM_ERR_REPORT("Invalid Freq = %04x",
					af_switch_freq);
				break;
			}

			status = cg2900_fm_af_switch_start(
					af_switch_freq,
					af_switch_pi);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_RDS_TX_DEVIATION:
		{
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_RDS_TX_DEVIATION, "
					"Value = %d",
					ext_ctrl->controls->value);

			if (ext_ctrl->controls->value <= MIN_RDS_DEVIATION &&
			    ext_ctrl->controls->value > MAX_RDS_DEVIATION) {
				FM_ERR_REPORT("Invalid RDS Deviation = %02x",
					ext_ctrl->controls->value);
				break;
			}

			status = cg2900_fm_tx_set_rds_deviation(
					ext_ctrl->controls->value);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_RDS_TX_PI:
		{
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_RDS_TX_PI, PI = %04x",
					ext_ctrl->controls->value);

			if (ext_ctrl->controls->value <= MIN_PI_VALUE &&
			    ext_ctrl->controls->value > MAX_PI_VALUE) {
				FM_ERR_REPORT("Invalid PI = %04x",
					ext_ctrl->controls->value);
				break;
			}

			status = cg2900_fm_tx_set_pi_code(
					ext_ctrl->controls->value);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_RDS_TX_PTY:
		{
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_RDS_TX_PTY, PTY = %d",
					ext_ctrl->controls->value);

			if (ext_ctrl->controls->value < MIN_PTY_VALUE &&
			    ext_ctrl->controls->value > MAX_PTY_VALUE) {
				FM_ERR_REPORT("Invalid PTY = %02x",
					      ext_ctrl->controls->value);
				break;
			}

			status = cg2900_fm_tx_set_pty_code(
					ext_ctrl->controls->value);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_RDS_TX_PS_NAME:
		{
			if (ext_ctrl->controls->size > MAX_PSN_SIZE
			    || ext_ctrl->controls->string == NULL) {
				FM_ERR_REPORT("Invalid PSN");
				break;
			}

			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_RDS_TX_PS_NAME, "
					"PSN = %s, Len = %x",
					ext_ctrl->controls->string,
					ext_ctrl->controls->size);

			status = cg2900_fm_tx_set_program_station_name(
					ext_ctrl->controls->string,
					ext_ctrl->controls->size);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_RDS_TX_RADIO_TEXT:
		{
			if (ext_ctrl->controls->size >= MAX_RT_SIZE
			    || ext_ctrl->controls->string == NULL) {
				FM_ERR_REPORT("Invalid RT");
				break;
			}

			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_RDS_TX_RADIO_TEXT, "
					"RT = %s, Len = %x",
					ext_ctrl->controls->string,
					ext_ctrl->controls->size);

			status = cg2900_fm_tx_set_radio_text(
					ext_ctrl->controls->string,
					ext_ctrl->controls->size);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_PILOT_TONE_ENABLED:
		{
			bool enable;
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_PILOT_TONE_ENABLED, "
					"Value = %d",
					ext_ctrl->controls->value);

			if (FMD_PILOT_TONE_ENABLED ==
				ext_ctrl->controls->value)
				enable = true;
			else if (FMD_PILOT_TONE_DISABLED ==
				ext_ctrl->controls->value)
				enable = false;
			else {
				FM_ERR_REPORT("Unsupported Value = %d",
					      ext_ctrl->controls->value);
				break;
			}
			status = cg2900_fm_tx_set_pilot_tone_status(enable);
			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_PILOT_TONE_DEVIATION:
		{
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_PILOT_TONE_DEVIATION, "
					"Value = %d",
					ext_ctrl->controls->value);

			if (ext_ctrl->controls->value <= MIN_PILOT_DEVIATION &&
			    ext_ctrl->controls->value > MAX_PILOT_DEVIATION) {
				FM_ERR_REPORT("Invalid Pilot Deviation = %02x",
					      ext_ctrl->controls->value);
				break;
			}

			status = cg2900_fm_tx_set_pilot_deviation(
					ext_ctrl->controls->value);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_TUNE_PREEMPHASIS:
		{
			u8 preemphasis;
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_TUNE_PREEMPHASIS, "
					"Value = %d",
					ext_ctrl->controls->value);

			if ((V4L2_PREEMPHASIS_50_uS >
					ext_ctrl->controls->value) ||
					(V4L2_PREEMPHASIS_75_uS <
					ext_ctrl->controls->value)) {
				FM_ERR_REPORT("Unsupported Preemphasis = %d",
					      ext_ctrl->controls->value);
				break;
			}

			if (V4L2_PREEMPHASIS_50_uS ==
				ext_ctrl->controls->value) {
				preemphasis = FMD_EMPHASIS_50US;
			} else if (V4L2_PREEMPHASIS_75_uS ==
				ext_ctrl->controls->value) {
				preemphasis = FMD_EMPHASIS_75US;
			}

			status = cg2900_fm_tx_set_preemphasis(preemphasis);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_TUNE_POWER_LEVEL:
		{
			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_TUNE_POWER_LEVEL, "
					"Value = %d",
					ext_ctrl->controls->value);
			if (ext_ctrl->controls->value < MIN_POWER_LEVEL &&
			    ext_ctrl->controls->value > MAX_POWER_LEVEL) {
				FM_ERR_REPORT("Invalid Power Level = %02x",
					      ext_ctrl->controls->value);
				break;
			}

			status = cg2900_fm_tx_set_power_level(
					ext_ctrl->controls->value);

			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_CG2900_RADIO_BLOCKSCAN_START:
		{
			u32 start_freq;
			u32 end_freq;
			u32 *block_scan_buf;
			u32 current_grid;
			u32 low_freq;
			u32 high_freq;
			u32 result_freq;
			u8  no_of_block_scan_channels;

			/* V4L2 Initial check */
			if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
				"V4L2_CID_CG2900_RADIO_BLOCKSCAN_START "
				"Unsupported ctrl_class = %04x",
				ext_ctrl->ctrl_class);
				break;
			}

			if (ext_ctrl->controls->size !=
				FMR_BLOCK_SCAN_DATA_SIZE ||
				ext_ctrl->controls->string == NULL) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
				"V4L2_CID_CG2900_RADIO_BLOCKSCAN_START "
				"Invalid Parameters");
				break;
			}

			/* Check for current grid */
			if (grid == CG2900_FM_GRID_50)
				current_grid = FMR_CHINA_GRID_IN_HZ;
			else if (grid == CG2900_FM_GRID_100)
				current_grid = FMR_EUROPE_GRID_IN_HZ;
			else
				current_grid = FMR_USA_GRID_IN_HZ;

			/* Check for current band */
			if (band == CG2900_FM_BAND_US_EU) {
				low_freq = FMR_EU_US_LOW_FREQ_IN_MHZ *
					FMR_HZ_TO_MHZ_CONVERTER;
				high_freq = FMR_EU_US_HIGH_FREQ_IN_MHZ *
					FMR_HZ_TO_MHZ_CONVERTER;

			} else if (band == CG2900_FM_BAND_JAPAN) {
				low_freq = FMR_JAPAN_LOW_FREQ_IN_MHZ *
					FMR_HZ_TO_MHZ_CONVERTER;
				high_freq = FMR_JAPAN_HIGH_FREQ_IN_MHZ *
					FMR_HZ_TO_MHZ_CONVERTER;

			} else {
				low_freq = FMR_CHINA_LOW_FREQ_IN_MHZ *
					FMR_HZ_TO_MHZ_CONVERTER;
				high_freq = FMR_CHINA_HIGH_FREQ_IN_MHZ *
					FMR_HZ_TO_MHZ_CONVERTER;
			}

			/* V4L2 Extended control */

			block_scan_buf = (u32 *)ext_ctrl->controls->string;
			start_freq = V4L2_TO_HZ(*block_scan_buf);
			end_freq = V4L2_TO_HZ(*(block_scan_buf + 1));

			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
					"V4L2_CID_CG2900_RADIO_"
					"BLOCKSCAN_START: "
					"Start Freq = %d Hz "
					"End Freq = %d Hz",
					(int)start_freq,
					(int)end_freq);

			result_freq = end_freq - start_freq;
			no_of_block_scan_channels =
				(u8)(result_freq / current_grid);

			/* Frequency Check */
			if (end_freq < start_freq) {
				FM_ERR_REPORT("Start Freq (%d Hz) "
					" > End Freq (%d Hz)",
					(int)start_freq,
					(int)end_freq);
				break;
			}

			if ((start_freq < low_freq) ||
				(start_freq > high_freq)) {
				FM_ERR_REPORT("Out of Band Freq: "
					"Start Freq = %d Hz",
					(int)start_freq);
				break;
			}

			if ((end_freq < low_freq) ||
				(end_freq > high_freq)) {
				FM_ERR_REPORT("Out of Band Freq: "
					"End Freq = %d Hz",
					(int)end_freq);
				break;
			}

			/* Maximum allowed block scan range */
			if (FMR_MAX_BLOCK_SCAN_CHANNELS <
				no_of_block_scan_channels) {
				FM_ERR_REPORT("No of channels (%x)"
					"exceeds Max Block Scan (%x)",
					no_of_block_scan_channels,
					FMR_MAX_BLOCK_SCAN_CHANNELS);
				break;
			}

			status = cg2900_fm_start_block_scan(
					start_freq,
					end_freq);
			if (0 == status) {
				cg2900_device.seekstatus =
					FMR_SEEK_IN_PROGRESS;
				ret_val = 0;
			}
			break;
		}
	case V4L2_CID_CG2900_RADIO_TEST_TONE_CONNECT:
		{
			u8 left_audio_mode;
			u8 right_audio_mode;
			u8 *test_tone_connect_buf;

			/* V4L2 Initial check */
			if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
					"V4L2_CID_CG2900_RADIO_"
					"TEST_TONE_CONNECT "
					"Unsupported ctrl_class = %04x",
					ext_ctrl->ctrl_class);
				break;
			}

			if (ext_ctrl->controls->size !=
				FMR_TEST_TONE_CONNECT_DATA_SIZE ||
				ext_ctrl->controls->string == NULL) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
					"V4L2_CID_CG2900_RADIO_TEST"
					"_TONE_CONNECT "
					"Invalid Parameters");
				break;
			}

			/* V4L2 Extended control */
			test_tone_connect_buf =
				(u8 *)ext_ctrl->controls->string;
			left_audio_mode = *test_tone_connect_buf;
			right_audio_mode = *(test_tone_connect_buf + 1);

			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
				"V4L2_CID_CG2900_RADIO_TEST_TONE_CONNECT"
				"left_audio_mode Freq = %02x"
				"right_audio_modeFreq = %02x",
				left_audio_mode,
				right_audio_mode);

			/* Range Check */
			if (left_audio_mode > \
			V4L2_CG2900_RADIO_TEST_TONE_TONE_SUM) {
				FM_ERR_REPORT("Invalid Value of "
					"left_audio_mode (%02x) ",
					left_audio_mode);
				break;
			}

			if (right_audio_mode > \
			V4L2_CG2900_RADIO_TEST_TONE_TONE_SUM) {
				FM_ERR_REPORT("Invalid Value of "
					"right_audio_mode (%02x) ",
					left_audio_mode);
				break;
			}

			status = cg2900_fm_test_tone_connect(
					left_audio_mode,
					right_audio_mode);
			if (0 == status)
				ret_val = 0;
			break;
		}
	case V4L2_CID_CG2900_RADIO_TEST_TONE_SET_PARAMS:
		{
			u8 tone_gen;
			u16 frequency;
			u16 volume;
			u16 phase_offset;
			u16 dc;
			u8 waveform;
			u16 *test_tone_set_params_buf;

			/* V4L2 Initial check */
			if (ext_ctrl->ctrl_class != V4L2_CTRL_CLASS_USER) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
				"V4L2_CID_CG2900_RADIO_TEST_TONE_SET_PARAMS "
				"Unsupported ctrl_class = %04x",
				ext_ctrl->ctrl_class);
				break;
			}

			if (ext_ctrl->controls->size !=
				FMR_TEST_TONE_SET_PARAMS_DATA_SIZE ||
				ext_ctrl->controls->string == NULL) {
				FM_ERR_REPORT("vidioc_set_ext_ctrls:  "
				"FMR_TEST_TONE_SET_PARAMS_DATA_SIZE "
				"Invalid Parameters");
				break;
			}

			/* V4L2 Extended control */
			test_tone_set_params_buf = \
			(u16 *)ext_ctrl->controls->string;

			tone_gen = (u8)(*test_tone_set_params_buf);
			frequency = *(test_tone_set_params_buf + 1);
			volume = *(test_tone_set_params_buf + 2);
			phase_offset = *(test_tone_set_params_buf + 3);
			dc = *(test_tone_set_params_buf + 4);
			waveform = (u8)(*(test_tone_set_params_buf + 5));

			FM_DEBUG_REPORT("vidioc_set_ext_ctrls: "
				"V4L2_CID_CG2900_RADIO_TEST_TONE_SET_PARAMS"
				"tone_gen = %02x frequency = %04x"
				"volume = %04x phase_offset = %04x"
				"dc = %04x waveform = %02x",
				tone_gen, frequency,
				volume, phase_offset,
				dc, waveform);

			/* Range Check */
			if (tone_gen > FMD_TST_TONE_2) {
				FM_ERR_REPORT("Invalid Value of "
					"tone_gen (%02x) ",
					tone_gen);
				break;
			}

			if (waveform > FMD_TST_TONE_PULSE) {
				FM_ERR_REPORT("Invalid Value of "
					"waveform (%02x) ",
					waveform);
				break;
			}

			if (frequency > 0x7FFF) {
				FM_ERR_REPORT("Invalid Value of "
					"frequency (%04x) ",
					frequency);
				break;
			}

			if (volume > 0x7FFF) {
				FM_ERR_REPORT("Invalid Value of "
					"volume (%04x) ",
					volume);
				break;
			}

			status = cg2900_fm_test_tone_set_params(
					tone_gen,
					frequency,
					volume,
					phase_offset,
					dc,
					waveform);

			if (0 == status)
				ret_val = 0;
			break;
		}
	default:
		{
			FM_ERR_REPORT("vidioc_set_ext_ctrls: "
				      "Unsupported Id = %04x",
				      ext_ctrl->controls->id);
		}
	}
error:
	return ret_val;
}

/**
 * vidioc_set_hw_freq_seek()- seek Up/Down Frequency.
 *
 * This function is used to start seek
 * on the FM Radio. Direction if seek is as inicated by the parameter
 * inside the v4l2_hw_freq_seek structure. This function is called when the
 * application issues the IOCTL VIDIOC_S_HW_FREQ_SEEK
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @freq_seek: v4l2_hw_freq_seek structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_set_hw_freq_seek(
			struct file *file,
			void *priv,
			struct v4l2_hw_freq_seek *freq_seek
			)
{
	int status;
	int ret_val = -EINVAL;

	FM_INFO_REPORT("vidioc_set_hw_freq_seek");

	FM_DEBUG_REPORT("vidioc_set_hw_freq_seek: Status = %x, "
			"Upwards = %x, Wrap Around = %x",
			cg2900_device.seekstatus,
			freq_seek->seek_upward, freq_seek->wrap_around);

	if (cg2900_device.seekstatus == FMR_SEEK_IN_PROGRESS) {
		FM_ERR_REPORT("vidioc_set_hw_freq_seek: "
				"VIDIOC_S_HW_FREQ_SEEK, "
				"freq_seek in progress");
		goto error;
	}

	spin_lock(&fm_spinlock);
	fm_event = CG2900_EVENT_NO_EVENT;
	no_of_scan_freq = 0;
	spin_unlock(&fm_spinlock);

	if (CG2900_DIR_UP == freq_seek->seek_upward)
		status = cg2900_fm_search_up_freq();
	else if (CG2900_DIR_DOWN == freq_seek->seek_upward)
		status = cg2900_fm_search_down_freq();
	else
		goto error;

	if (0 != status)
		goto error;

	cg2900_device.seekstatus = FMR_SEEK_IN_PROGRESS;
	ret_val = 0;

error:
	FM_DEBUG_REPORT("vidioc_set_hw_freq_seek: returning = %d",
			ret_val);
	return ret_val;
}

/**
 * vidioc_get_audio()- Get Audio features of FM Driver.
 *
 * This function is used to get the audio features of FM Driver.
 * This function is imlemented as a dumy function.
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @audio: (out) v4l2_audio structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_get_audio(
			struct file *file,
			void *priv,
			struct v4l2_audio *audio
			)
{
	FM_INFO_REPORT("vidioc_get_audio");
	strcpy(audio->name, "");
	audio->capability = 0;
	audio->mode = 0;
	return 0;
}

/**
 * vidioc_set_audio()- Set Audio features of FM Driver.
 *
 * This function is used to set the audio features of FM Driver.
 * This function is imlemented as a dumy function.
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @audio: v4l2_audio structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_set_audio(
			struct file *file,
			void *priv,
			struct v4l2_audio *audio
			)
{
	FM_INFO_REPORT("vidioc_set_audio");
	if (audio->index != 0)
		return -EINVAL;
	return 0;
}

/**
 * vidioc_get_input()- Get the Input Value
 *
 * This function is used to get the Input.
 * This function is imlemented as a dumy function.
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @input: (out) Value to be stored.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_get_input(
			struct file *file,
			void *priv,
			unsigned int *input
			)
{
	FM_INFO_REPORT("vidioc_get_input");
	*input = 0;
	return 0;
}

/**
 * vidioc_set_input()- Set the input value.
 *
 * This function is used to set input.
 * This function is imlemented as a dumy function.
 *
 * @file: File structure.
 * @priv: Previous data of file structure.
 * @input: Value to set
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int vidioc_set_input(
			struct file *file,
			void *priv,
			unsigned int input
			)
{
	FM_INFO_REPORT("vidioc_set_input");
	if (input != 0)
		return -EINVAL;
	return 0;
}

/**
 * cg2900_convert_err_to_v4l2()- Convert Error Bits to V4L2 RDS format.
 *
 * This function converts the error bits in RDS Block
 * as received from Chip into V4L2 RDS data specification.
 *
 * @status_byte: The status byte as received in RDS Group for
 * particular RDS Block
 * @out_byte: byte to store the modified byte with the err bits
 * alligned as per V4L2 RDS Specifications.
 */
static void cg2900_convert_err_to_v4l2(
			char status_byte,
			char *out_byte
			)
{
	if ((status_byte & RDS_ERROR_STATUS_MASK) == RDS_ERROR_STATUS_MASK) {
		/* Uncorrectable Block */
		*out_byte = (*out_byte | V4L2_RDS_BLOCK_ERROR);
	} else if (((status_byte & RDS_UPTO_TWO_BITS_CORRECTED)
		    == RDS_UPTO_TWO_BITS_CORRECTED) ||
		   ((status_byte & RDS_UPTO_FIVE_BITS_CORRECTED)
		    == RDS_UPTO_FIVE_BITS_CORRECTED)) {
		/* Corrected Bits in Block */
		*out_byte = (*out_byte | V4L2_RDS_BLOCK_CORRECTED);
	}
}

/**
 * cg2900_map_event_to_v4l2()- Maps cg2900 event to v4l2 events .
 *
 * This function maps cg2900 events to corresponding v4l2 events.
 *
 * @event: This contains the cg2900 event to be converted.
 *
 * Returns: Corresponding V4L2 events.
 */
static int cg2900_map_event_to_v4l2(
			u8 event
			)
{
	switch (event) {
	case CG2900_EVENT_MONO_STEREO_TRANSITION:
		return V4L2_CG2900_RADIO_INTERRUPT_MONO_STEREO_TRANSITION;
	case CG2900_EVENT_SEARCH_CHANNEL_FOUND:
		return V4L2_CG2900_RADIO_INTERRUPT_SEARCH_COMPLETED;
	case CG2900_EVENT_SCAN_CHANNELS_FOUND:
		return V4L2_CG2900_RADIO_INTERRUPT_BAND_SCAN_COMPLETED;
	case CG2900_EVENT_BLOCK_SCAN_CHANNELS_FOUND:
		return V4L2_CG2900_RADIO_INTERRUPT_BLOCK_SCAN_COMPLETED;
	case CG2900_EVENT_SCAN_CANCELLED:
		return V4L2_CG2900_RADIO_INTERRUPT_SCAN_CANCELLED;
	case CG2900_EVENT_DEVICE_RESET:
		return V4L2_CG2900_RADIO_INTERRUPT_DEVICE_RESET;
	case CG2900_EVENT_RDS_EVENT:
		return V4L2_CG2900_RADIO_INTERRUPT_RDS_RECEIVED;
	default:
		return V4L2_CG2900_RADIO_INTERRUPT_UNKNOWN;
	}
}

/**
 * cg2900_open()- This function nitializes and switches on FM.
 *
 * This is called when the application opens the character device.
 *
 * @file: File structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int cg2900_open(
			struct file *file
			)
{
	int status;
	int ret_val = -EINVAL;
	struct video_device *vdev = video_devdata(file);

	mutex_lock(&fm_mutex);
	users++;
	FM_INFO_REPORT("cg2900_open: users = %d", users);

	if (users > 1) {
		FM_INFO_REPORT("cg2900_open: FM already switched on!!!");
		ret_val = 0;
		/*
		 * No need to perform the initialization and switch on FM
		 * since it is already done during the first open call to
		 * this driver.
		 */
		goto done;
	}

	status = cg2900_fm_init();
	if (0 != status)
		goto init_error;

	FM_DEBUG_REPORT("cg2900_open: Switching on FM");
	status = cg2900_fm_switch_on(&(vdev->dev));
	if (0 != status)
		goto switch_on_error;

	cg2900_device.state = FMR_SWITCH_ON;
	cg2900_device.frequency = HZ_TO_V4L2(freq_low);
	cg2900_device.rx_rds_enabled = false;
	cg2900_device.muted = false;
	cg2900_device.audiopath = 0;
	cg2900_device.seekstatus = FMR_SEEK_NONE;
	cg2900_device.rssi_threshold = CG2900_FM_DEFAULT_RSSI_THRESHOLD;
	fm_event = CG2900_EVENT_NO_EVENT;
	no_of_scan_freq = 0;
	cg2900_device.fm_mode = CG2900_FM_IDLE_MODE;
	ret_val = 0;
	goto done;

switch_on_error:
	cg2900_fm_deinit();
init_error:
	users--;
done:
	mutex_unlock(&fm_mutex);
	FM_DEBUG_REPORT("cg2900_open: returning %d", ret_val);
	return ret_val;
}

/**
 * cg2900_release()- This function switches off FM.
 *
 * This function switches off FM and releases the resources.
 * This is called when the application closes the character
 * device.
 *
 * @file: File structure.
 *
 * Returns:
 *   0 when no error
 *   -EINVAL: otherwise
 */
static int cg2900_release(
			struct file *file
			)
{
	int status;
	int ret_val = -EINVAL;

	mutex_lock(&fm_mutex);

	FM_INFO_REPORT("cg2900_release");
	if (users <= 0) {
		FM_ERR_REPORT("cg2900_release: No users registered "
			      "with FM Driver");
		goto done;
	}

	users--;
	FM_INFO_REPORT("cg2900_release: users = %d", users);

	if (0 == users) {
		FM_DEBUG_REPORT("cg2900_release: Switching Off FM");
		status = cg2900_fm_switch_off();
		status = cg2900_fm_deinit();
		if (0 != status)
			goto done;

		cg2900_device.state = FMR_SWITCH_OFF;
		cg2900_device.frequency = 0;
		cg2900_device.rx_rds_enabled = false;
		cg2900_device.muted = false;
		cg2900_device.seekstatus = FMR_SEEK_NONE;
		fm_event = CG2900_EVENT_NO_EVENT;
		no_of_scan_freq = 0;
	}
	ret_val = 0;

done:
	mutex_unlock(&fm_mutex);
	FM_DEBUG_REPORT("cg2900_release: returning %d", ret_val);
	return ret_val;
}

/**
 * cg2900_read()- This function is invoked when the application
 * calls read() to receive RDS Data.
 *
 * @file: File structure.
 * @data: buffer provided by application for receving the data.
 * @count: Number of bytes that application wants to read from driver
 * @pos: offset
 *
 * Returns:
 *   Number of bytes copied to the user buffer
 *   -EFAULT: If there is problem in copying data to buffer supplied
 *    by application
 *   -EIO: If the number of bytes to be read are not a multiple of
 *    struct  v4l2_rds_data.
 *   -EAGAIN: More than 22 blocks requested to be read or read
 *    was called in non blocking mode and no data was available for reading.
 *   -EINTR: If read was interrupted by a signal before data was avaialble.
 *   0 when no data available for reading.
 */
static ssize_t cg2900_read(
			struct file *file,
			char __user *data,
			size_t count, loff_t *pos
			)
{
	int current_rds_grp;
	int index = 0;
	int blocks_to_read;
	struct v4l2_rds_data rdsbuf[MAX_RDS_GROUPS * NUM_OF_RDS_BLOCKS];
	struct v4l2_rds_data *rdslocalbuf = rdsbuf;
	struct sk_buff *skb;

	FM_INFO_REPORT("cg2900_read");

	blocks_to_read = (count / sizeof(struct v4l2_rds_data));

	if (!cg2900_device.rx_rds_enabled) {
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
		FM_INFO_REPORT("cg2900_read: returning 0");
		return 0;
	}

	if (count % sizeof(struct v4l2_rds_data) != 0) {
		FM_ERR_REPORT("cg2900_read: Invalid Number of bytes %x "
			      "requested to read", count);
		return -EIO;
	}

	if (blocks_to_read > MAX_RDS_GROUPS * NUM_OF_RDS_BLOCKS) {
		FM_ERR_REPORT("cg2900_read: Too many blocks(%d) "
			      "requested to be read", blocks_to_read);
		return -EAGAIN;
	}

	current_rds_grp = fm_rds_info.rds_group_sent;

	if ((fm_rds_info.rds_head == fm_rds_info.rds_tail) ||
	    (fm_rds_buf[fm_rds_info.rds_tail]
	     [current_rds_grp].block1 == 0x0000)) {
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
		FM_INFO_REPORT("cg2900_read: returning 0");
		return 0;
	}

	spin_lock(&fm_spinlock);
	while (index < blocks_to_read) {
		/* Check which Block needs to be transferred next */
		switch (fm_rds_info.rds_block_sent % NUM_OF_RDS_BLOCKS) {
		case 0:
			(rdslocalbuf + index)->lsb =
			fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block1;
			(rdslocalbuf + index)->msb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block1 >> 8;
			(rdslocalbuf + index)->block =
			    (fm_rds_buf[fm_rds_info.rds_tail]
			     [current_rds_grp].status1
			     & RDS_BLOCK_MASK) >> 2;
			cg2900_convert_err_to_v4l2(
				fm_rds_buf[fm_rds_info.rds_tail]
				[current_rds_grp].status1,
				&(rdslocalbuf + index)->block);
			break;
		case 1:
			(rdslocalbuf + index)->lsb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block2;
			(rdslocalbuf + index)->msb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block2 >> 8;
			(rdslocalbuf + index)->block =
			    (fm_rds_buf[fm_rds_info.rds_tail]
			     [current_rds_grp].status2
			     & RDS_BLOCK_MASK) >> 2;
			cg2900_convert_err_to_v4l2(
				fm_rds_buf[fm_rds_info.rds_tail]
				[current_rds_grp].status2,
				&(rdslocalbuf + index)->block);
			break;
		case 2:
			(rdslocalbuf + index)->lsb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block3;
			(rdslocalbuf + index)->msb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block3 >> 8;
			(rdslocalbuf + index)->block =
			    (fm_rds_buf[fm_rds_info.rds_tail]
			     [current_rds_grp].status3
			     & RDS_BLOCK_MASK) >> 2;
			cg2900_convert_err_to_v4l2(
				fm_rds_buf[fm_rds_info.rds_tail]
				[current_rds_grp].status3,
				&(rdslocalbuf + index)->block);
			break;
		case 3:
			(rdslocalbuf + index)->lsb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block4;
			(rdslocalbuf + index)->msb =
			    fm_rds_buf[fm_rds_info.rds_tail]
			    [current_rds_grp].block4 >> 8;
			(rdslocalbuf + index)->block =
			    (fm_rds_buf[fm_rds_info.rds_tail]
			     [current_rds_grp].status4
			     & RDS_BLOCK_MASK) >> 2;
			cg2900_convert_err_to_v4l2(
				fm_rds_buf[fm_rds_info.rds_tail]
				[current_rds_grp].status4,
				&(rdslocalbuf + index)->block);
			current_rds_grp++;
			if (current_rds_grp == MAX_RDS_GROUPS) {
				fm_rds_info.rds_tail++;
				current_rds_grp = 0;
				/* Dequeue Rds Interrupt here */
				skb = skb_dequeue(&fm_interrupt_queue);
				if (!skb) {
					/* No Interrupt, bad case */
					FM_ERR_REPORT("cg2900_read: "
					"skb is NULL. Major error");
					spin_unlock(&fm_spinlock);
					return 0;
				}
				fm_event = (u8)skb->data[0];
				if (fm_event != CG2900_EVENT_RDS_EVENT) {
					/* RDS interrupt not found */
					FM_ERR_REPORT("cg2900_read:"
					"RDS interrupt not found"
					"for de-queuing."
					"fm_event = %x", fm_event);
					/* Queue the event back */
					skb_queue_head(&fm_interrupt_queue,
						skb);
					spin_unlock(&fm_spinlock);
					return 0;
				}
				kfree_skb(skb);
			}
			break;
		default:
			FM_ERR_REPORT("Invalid RDS Group!!!");
			spin_unlock(&fm_spinlock);
			return 0;
		}
		index++;
		fm_rds_info.rds_block_sent++;
		if (fm_rds_info.rds_block_sent == NUM_OF_RDS_BLOCKS)
			fm_rds_info.rds_block_sent = 0;

		if (!cg2900_device.rx_rds_enabled) {
			/* Remove all Interrupts from the queue */
			skb_queue_purge(&fm_interrupt_queue);
			FM_INFO_REPORT("cg2900_read: returning 0");
			spin_unlock(&fm_spinlock);
			return 0;
		}
	}
	/* Update the RDS Group Count Sent to Application */
	fm_rds_info.rds_group_sent = current_rds_grp;
	if (fm_rds_info.rds_tail == MAX_RDS_BUFFER)
		fm_rds_info.rds_tail = 0;

	spin_unlock(&fm_spinlock);
	if (copy_to_user(data, rdslocalbuf, count)) {
		FM_ERR_REPORT("cg2900_read: Error "
			      "in copying, returning");
		return -EFAULT;
	}
	return count;
}

/**
 * cg2900_poll()- Check if the operation is complete or not.
 *
 * This function is invoked by application on calling poll() and is used to
 * wait till the any FM interrupt is received from the chip.
 * The application decides to read the corresponding data depending on FM
 * interrupt.
 *
 * @file: File structure.
 * @wait: poll table
 *
 * Returns:
 *   POLLRDNORM|POLLIN	whenever FM interrupt has occurred.
 *   0			whenever the call times out.
 */
static unsigned int cg2900_poll(
			struct file *file,
			struct poll_table_struct *wait
			)
{
	int ret_val = 0;

	FM_INFO_REPORT("cg2900_poll");

	/* Check if we have some data in queue already */
	if (skb_queue_empty(&fm_interrupt_queue)) {
		FM_DEBUG_REPORT("cg2900_poll: Interrupt Queue Empty, waiting");
		/* No Interrupt, wait for it to occur */
		poll_wait(file, &cg2900_poll_queue, wait);
	}
	/* Check if we now have interrupt to read in queue */
	if (skb_queue_empty(&fm_interrupt_queue))
		goto done;

	ret_val = POLLIN | POLLRDNORM;

done:
	FM_DEBUG_REPORT("poll_wait returning %d", ret_val);
	return ret_val;
}

/**
 * radio_cg2900_probe()- This function registers FM Driver with V4L2 Driver.
 *
 * This function is called whenever the driver is probed by the device system,
 * i.e. when a CG2900 controller has connected. It registers the FM Driver with
 * Video4Linux as a character device.
 *
 * @pdev: Platform device.
 *
 * Returns:
 *   0 on success
 * -EINVAL on error
 */
static int __devinit radio_cg2900_probe(
			struct platform_device *pdev
			)
{
	int err;

	FM_INFO_REPORT(BANNER);

	err = fmd_set_dev(&pdev->dev);
	if (err) {
		FM_ERR_REPORT("Could not set device %s", pdev->name);
		return err;
	}

	cg2900_video_device = video_device_alloc();
	if (cg2900_video_device == NULL) {
		FM_ERR_REPORT("Could not create video_device structure");
		return -ENOMEM;
	}

	strlcpy(cg2900_video_device->name, "STE CG2900 FM Rx/Tx Radio",
		sizeof(cg2900_video_device->name));
	cg2900_video_device->fops = &cg2900_fops;
	cg2900_video_device->ioctl_ops = &cg2900_ioctl_ops;
	cg2900_video_device->release = video_device_release;

	radio_nr = 0;
	grid = CG2900_FM_GRID_100;
	band = CG2900_FM_BAND_US_EU;
	FM_INFO_REPORT("radio_cg2900_probe: radio_nr= %d.", radio_nr);

	/* Initialize the parameters */
	if (video_register_device(
				cg2900_video_device,
				VFL_TYPE_RADIO,
				radio_nr) == -1) {
		FM_ERR_REPORT("radio_cg2900_probe: video_register_device err");
		video_device_release(cg2900_video_device);
		return -EINVAL;
	}
	mutex_init(&fm_mutex);
	spin_lock_init(&fm_spinlock);
	init_waitqueue_head(&cg2900_poll_queue);
	skb_queue_head_init(&fm_interrupt_queue);
	users = 0;
	return 0;
}

/**
 * radio_cg2900_remove()- This function removes the FM Driver.
 *
 * This function is called whenever the driver is removed by the device system,
 * i.e. when a CG2900 controller has disconnected. It unregisters the FM Driver
 * from Video4Linux.
 *
 * @pdev: Platform device.
 *
 * Returns: 0 on success
 */
static int __devexit radio_cg2900_remove(
			struct platform_device *pdev
			)
{
	FM_INFO_REPORT("radio_cg2900_remove");
	/* Wake up the poll queue since we are now exiting */
	wake_up_poll_queue();
	/* Give some time for application to exit the poll thread */
	schedule_timeout_interruptible(msecs_to_jiffies(500));

	/* Try to Switch Off FM in case it is still switched on */
	cg2900_fm_switch_off();
	cg2900_fm_deinit();
	skb_queue_purge(&fm_interrupt_queue);
	mutex_destroy(&fm_mutex);
	video_unregister_device(cg2900_video_device);
	fmd_set_dev(NULL);
	return 0;
}

static struct platform_driver radio_cg2900_driver = {
	.driver = {
		.name	= "cg2900-fm",
		.owner	= THIS_MODULE,
	},
	.probe	= radio_cg2900_probe,
	.remove	= __devexit_p(radio_cg2900_remove),
};

/**
 * radio_cg2900_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init radio_cg2900_init(void)
{
	FM_INFO_REPORT("radio_cg2900_init");
	return platform_driver_register(&radio_cg2900_driver);
}

/**
 * radio_cg2900_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit radio_cg2900_exit(void)
{
	FM_INFO_REPORT("radio_cg2900_exit");
	platform_driver_unregister(&radio_cg2900_driver);
}

void wake_up_poll_queue(void)
{
	FM_INFO_REPORT("wake_up_poll_queue");
	wake_up_interruptible(&cg2900_poll_queue);
}

void cg2900_handle_device_reset(void)
{
	struct sk_buff *skb;
	FM_INFO_REPORT("cg2900_handle_device_reset");
	skb = alloc_skb(SKB_FM_INTERRUPT_DATA, GFP_KERNEL);
	if (!skb) {
		FM_ERR_REPORT("cg2900_handle_device_reset: "
			"Unable to Allocate Memory");
		return;
	}
	skb->data[0] = CG2900_EVENT_DEVICE_RESET;
	skb->data[1] = true;
	skb_queue_tail(&fm_interrupt_queue, skb);
	wake_up_poll_queue();
}

module_init(radio_cg2900_init);
module_exit(radio_cg2900_exit);
MODULE_AUTHOR("Hemant Gupta");
MODULE_LICENSE("GPL v2");

module_param(radio_nr, int, S_IRUGO);

module_param(grid, int, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(grid, "Grid:"
			"0=50 kHz"
			"*1=100 kHz*"
			"2=200 kHz");

module_param(band, int, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(band, "Band:"
			"*0=87.5-108 MHz*"
			"1=76-90 MHz"
			"2=70-108 MHz");

