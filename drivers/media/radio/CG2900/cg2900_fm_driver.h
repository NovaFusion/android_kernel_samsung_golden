/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Linux FM Driver for CG2900 FM Chip
 *
 * Author: Hemant Gupta <hemant.gupta@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#ifndef _FMDRIVER_H_
#define _FMDRIVER_H_

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include "cg2900_fm_api.h"

/* structure declared in cg2900_fm_driver.c */
extern struct timespec time_spec;

/* module_param declared in cg2900_fm_driver.c */
extern unsigned short cg2900_fm_debug_level;

/**
 * enum fmd_debug_levels - FM Driver Debug Levels.
 *
 * @FM_NO_LOGS: No Logs are displayed.
 * @FM_ERROR_LOGS: Only Error Logs are displayed.
 * @FM_INFO_LOGS: Function Entry logs are displayed.
 * @FM_DEBUG_LOGS: Full debugging support.
 * @FM_HCI_PACKET_LOGS: HCI Packet Sent/received to/by
 * FM Driver are displayed.
 *
 * Various debug levels for FM Driver.
 */
enum fmd_debug_levels {
	FM_NO_LOGS,
	FM_ERROR_LOGS,
	FM_INFO_LOGS,
	FM_DEBUG_LOGS,
	FM_HCI_PACKET_LOGS
};

#define FM_HEX_REPORT(fmt, arg...) \
	if (cg2900_fm_debug_level == FM_HCI_PACKET_LOGS) { \
		printk(KERN_INFO fmt "\r\n" , ## arg); \
	}

#define FM_DEBUG_REPORT(fmt, arg...) \
	if (cg2900_fm_debug_level > FM_INFO_LOGS && \
			cg2900_fm_debug_level < FM_HCI_PACKET_LOGS) { \
		getnstimeofday(&time_spec); \
		printk(KERN_INFO "\n[%08x:%08x] " \
		"CG2900_FM_Driver: " fmt "\r\n" , \
		(unsigned int)time_spec.tv_sec, \
		(unsigned int)time_spec.tv_nsec, ## arg); \
	}

#define FM_INFO_REPORT(fmt, arg...)    \
	if (cg2900_fm_debug_level > FM_ERROR_LOGS && \
			cg2900_fm_debug_level < FM_HCI_PACKET_LOGS) { \
		getnstimeofday(&time_spec); \
		printk(KERN_INFO "\n[%08x:%08x] " \
		"CG2900_FM_Driver: " fmt "\r\n" , \
		(unsigned int)time_spec.tv_sec, \
		(unsigned int)time_spec.tv_nsec, ## arg); \
	}

#define FM_ERR_REPORT(fmt, arg...)               \
	if (cg2900_fm_debug_level >= FM_ERROR_LOGS) { \
		getnstimeofday(&time_spec); \
		printk(KERN_ERR "\n[%08x:%08x] " \
		"CG2900_FM_Driver: " fmt "\r\n" , \
		(unsigned int)time_spec.tv_sec, \
		(unsigned int)time_spec.tv_nsec, ## arg); \
	}

#define MAX_COUNT_OF_IRQS				16
#define MAX_BUFFER_SIZE					512
#define MAX_NAME_SIZE					100
/* Maximum size of parsable data in bytes, received from CG2900 FM IP */
#define MAX_RESP_SIZE					20
/* Minimum Power level for CG2900. The value is in units of dBuV */
#define MIN_POWER_LEVEL					88
/* Maximum Power level for CG2900. The value is in units of dBuV */
#define MAX_POWER_LEVEL					123
/* Minimum RDS Deviation value for CG2900. The value is in units of 10 Hz */
#define MIN_RDS_DEVIATION				0
/* Default RDS Deviation value for CG2900. The value is in units of 10 Hz */
#define DEFAULT_RDS_DEVIATION				200
/* Maximum RDS Deviation value for CG2900. The value is in units of 10 Hz */
#define MAX_RDS_DEVIATION				750
#define FMD_EU_US_MIN_FREQ_IN_KHZ			87500
#define FMD_EU_US_MAX_FREQ_IN_KHZ			108000
#define FMD_JAPAN_MIN_FREQ_IN_KHZ			76000
#define FMD_JAPAN_MAX_FREQ_IN_KHZ			90000
#define FMD_CHINA_MIN_FREQ_IN_KHZ			70000
#define FMD_CHINA_MAX_FREQ_IN_KHZ			108000
#define FMD_MIN_CHANNEL_NUMBER				0
#define FMD_MAX_CHANNEL_NUMBER				760
/*
 * Maximum supported balance for CG2900. This is just a hexadecimal number
 * with no units.
 */
#define FMD_MAX_BALANCE					0x7FFF
/*
 * Maximum supported volume for CG2900. This is just a hexadecimal number
 * with no units.
 */
#define FMD_MAX_VOLUME					0x7FFF
/* Minimum Program Identification value as per RDS specification */
#define MIN_PI_VALUE					0x0000
/* Maximum Program Identification value as per RDS specification */
#define MAX_PI_VALUE					0xFFFF
/* Minimum Program Type code value as per RDS specification */
#define MIN_PTY_VALUE					0
/* Maximum Program Type code value as per RDS specification */
#define MAX_PTY_VALUE					31
/* Minimum Pilot Deviation value for CG2900. The value is in units of 10 Hz */
#define MIN_PILOT_DEVIATION				0
/* Default Pilot Deviation value for CG2900. The value is in units of 10 Hz */
#define DEFAULT_PILOT_DEVIATION				675
/* Maximum Pilot Deviation value for CG2900. The value is in units of 10 Hz */
#define MAX_PILOT_DEVIATION				1000
/*
 * Default RSSI Threshold for a channel to be considered valid for CG2900.
 * This is just a hexadecimal number with no units.
 */
#define DEFAULT_RSSI_THRESHOLD				0x0100
/*
 * Default Peak Noise level for a channel to be considered valid for CG2900.
 * This is just a hexadecimal number with no units.
 */
#define DEFAULT_PEAK_NOISE_VALUE			0x0035
/* Defines the RF level (at the antenna pin) at which the stereo blending
 * function will stop limiting the channel separation */
#define STEREO_BLENDING_MIN_RSSI			0x0005
/* Defines the RF level (at the antenna pin) at which the stereo blending
 * function will start limiting the channel separation */
#define STEREO_BLENDING_MAX_RSSI			0x0100
/*
 * Default Average Noise level for a channel to be considered valid for CG2900.
 * This is just a hexadecimal number with no units.
 */
#define DEFAULT_AVERAGE_NOISE_MAX_VALUE			0x0030
/*
 * Minimum Audio Deviation Level, as per CG2900 FM User Manual.
 * This is units of 10 Hz.
 */
#define MIN_AUDIO_DEVIATION						0x157C
/*
 * Maximum Audio Deviation Level, as per CG2900 FM UserManual.
 * This is units of 10 Hz.
 */
#define MAX_AUDIO_DEVIATION						0x3840
#define FREQUENCY_CONVERTOR_KHZ_HZ			1000
#define CHANNEL_FREQ_CONVERTER_MHZ			50
/* Interrupt(s) for CG2900 */
#define IRPT_INVALID					0x0000
#define IRPT_OPERATION_SUCCEEDED			0x0001
#define IRPT_OPERATION_FAILED				0x0002
#define IRPT_RX_BUFFERFULL_TX_BUFFEREMPTY		0x0008
#define IRPT_RX_SIGNAL_QUALITYLOW_MUTE_STATUS_CHANGED	0x0010
#define IRPT_RX_MONO_STEREO_TRANSITION			0x0020
#define IRPT_TX_OVERMODULATION				0x0030
#define IRPT_RX_RDS_SYNCFOUND_TX_OVERDRIVE		0x0040
#define IRPT_RDS_SYNC_LOST				0x0080
#define IRPT_PI_CODE_CHANGED				0x0100
#define IRPT_REQUESTED_BLOCK_AVAILABLE			0x0200
#define IRPT_BUFFER_CLEARED				0x2000
#define IRPT_WARM_BOOT_READY				0x4000
#define IRPT_COLD_BOOT_READY				0x8000
/* FM Commands Id */
#define CMD_ID_NONE					0x0000
#define CMD_AUP_EXT_SET_MUTE				0x01E2
#define CMD_AUP_SET_BALANCE				0x0042
#define CMD_AUP_SET_MUTE				0x0062
#define CMD_AUP_SET_VOLUME				0x0022
#define CMD_AUP_BT_SETVOLUME				0x0122
#define CMD_AUP_BT_SET_MUTE				0x01A2
#define CMD_FMR_DP_BUFFER_GET_GROUP			0x0303
#define CMD_FMR_DP_BUFFER_GET_GROUP_COUNT		0x0323
#define CMD_FMR_DP_BUFFER_SET_SIZE			0x0343
#define CMD_FMR_DP_BUFFER_SET_THRESHOLD			0x06C3
#define CMD_FMR_DP_SET_CONTROL				0x02A3
#define CMD_FMR_DP_SET_GROUP_REJECTION		0x0543
#define CMD_FMR_RP_GET_RSSI				0x0083
#define CMD_FMR_RP_GET_STATE				0x0063
#define CMD_FMR_RP_STEREO_SET_MODE			0x0123
#define CMD_FMR_RP_STEREO_SET_CONTROL_BLENDING_RSSI		0x0143
#define CMD_FMR_SET_ANTENNA				0x0663
#define CMD_FMR_SP_AF_SWITCH_GET_RESULT			0x0603
#define CMD_FMR_SP_AF_SWITCH_START			0x04A3
#define CMD_FMR_SP_AF_UPDATE_GET_RESULT			0x0483
#define CMD_FMR_SP_AF_UPDATE_START			0x0463
#define CMD_FMR_SP_BLOCK_SCAN_GET_RESULT		0x06A3
#define CMD_FMR_SP_BLOCK_SCAN_START			0x0683
#define CMD_FMR_SP_SCAN_GET_RESULT			0x0423
#define CMD_FMR_SP_SCAN_START				0x0403
#define CMD_FMR_SP_SEARCH_START				0x03E3
#define CMD_FMR_SP_STOP					0x0383
#define CMD_FMR_SP_TUNE_GET_CHANNEL			0x03A3
#define CMD_FMR_SP_TUNE_SET_CHANNEL			0x03C3
#define CMD_FMR_TN_SET_BAND				0x0023
#define CMD_FMR_TN_SET_GRID				0x0043
#define CMD_FMR_RP_SET_DEEMPHASIS			0x00C3
#define CMD_FMT_DP_BUFFER_GET_POSITION			0x0204
#define CMD_FMT_DP_BUFFER_SET_GROUP			0x0244
#define CMD_FMT_DP_BUFFER_SET_SIZE			0x0224
#define CMD_FMT_DP_BUFFER_SET_THRESHOLD			0x0284
#define CMD_FMT_DP_SET_CONTROL				0x0264
#define CMD_FMT_PA_SET_CONTROL				0x01A4
#define CMD_FMT_PA_SET_MODE				0x01E4
#define CMD_FMT_RP_SET_PILOT_DEVIATION			0x02A4
#define CMD_FMT_RP_SET_PREEMPHASIS			0x00C4
#define CMD_FMT_RP_SET_RDS_DEVIATION			0x0344
#define CMD_FMT_RP_STEREO_SET_MODE			0x0164
#define CMD_FMT_SP_TUNE_GET_CHANNEL			0x0184
#define CMD_FMT_SP_TUNE_SET_CHANNEL			0x0064
#define CMD_FMT_TN_SET_BAND				0x0024
#define CMD_FMT_TN_SET_GRID				0x0044
#define CMD_GEN_GET_MODE				0x0021
#define CMD_GEN_GET_REGISTER_VALUE			0x00E1
#define CMD_GEN_GET_VERSION				0x00C1
#define CMD_GEN_GOTO_MODE				0x0041
#define CMD_GEN_GOTO_POWERDOWN				0x0081
#define CMD_GEN_GOTO_STANDBY				0x0061
#define CMD_GEN_POWERUP					0x0141
#define CMD_GEN_SELECT_REFERENCE_CLOCK			0x0201
#define CMD_GEN_SET_REFERENCE_CLOCK			0x0161
#define CMD_GEN_SET_REFERENCE_CLOCK_PLL			0x01A1
#define CMD_GEN_SET_REGISTER_VALUE			0x0101
#define CMD_TST_TONE_ENABLE				0x0027
#define CMD_TST_TONE_CONNECT				0x0047
#define CMD_TST_TONE_SET_PARAMS				0x0067
#define CMD_FMT_RP_LIMITER_SETCONTROL				0x01C4

/* FM Command Id Parameter Length */
#define CMD_GET_VERSION_PARAM_LEN			0
#define CMD_GET_VERSION_RSP_PARAM_LEN			7
#define CMD_GOTO_MODE_PARAM_LEN				1
#define CMD_SET_ANTENNA_PARAM_LEN			1
#define CMD_TN_SET_BAND_PARAM_LEN			3
#define CMD_TN_SET_GRID_PARAM_LEN			1
#define CMD_SP_TUNE_SET_CHANNEL_PARAM_LEN		1
#define CMD_SP_TUNE_GET_CHANNEL_PARAM_LEN		0
#define CMD_SP_TUNE_GET_CHANNEL_RSP_PARAM_LEN		1
#define CMD_RP_STEREO_SET_MODE_PARAM_LEN		1
#define CMD_RP_STEREO_SET_CONTROL_BLENDING_RSSI_PARAM_LEN	2
#define CMD_RP_GET_RSSI_PARAM_LEN			0
#define CMD_RP_GET_RSSI_RSP_PARAM_LEN			1
#define CMD_RP_GET_STATE_PARAM_LEN			0
#define CMD_RP_GET_STATE_RSP_PARAM_LEN			2
#define CMD_SP_SEARCH_START_PARAM_LEN			4
#define CMD_SP_SCAN_START_PARAM_LEN			4
#define CMD_SP_SCAN_GET_RESULT_PARAM_LEN		1
#define CMD_SP_SCAN_GET_RESULT_RSP_PARAM_LEN		7
#define CMD_SP_BLOCK_SCAN_START_PARAM_LEN		3
#define CMD_SP_BLOCK_SCAN_GET_RESULT_PARAM_LEN		1
#define CMD_SP_BLOCK_SCAN_GET_RESULT_RSP_PARAM_LEN	7
#define CMD_SP_STOP_PARAM_LEN				0
#define CMD_SP_AF_UPDATE_START_PARAM_LEN		1
#define CMD_SP_AF_UPDATE_GET_RESULT_PARAM_LEN		0
#define CMD_SP_AF_UPDATE_GET_RESULT_RSP_PARAM_LEN	1
#define CMD_SP_AF_SWITCH_START_PARAM_LEN		5
#define CMD_SP_AF_SWITCH_GET_RESULT_PARAM_LEN		0
#define CMD_SP_AF_SWITCH_GET_RESULT_RWSP_PARAM_LEN	3
#define CMD_DP_BUFFER_SET_SIZE_PARAM_LEN		1
#define CMD_DP_BUFFER_SET_THRESHOLD_PARAM_LEN		1
#define CMD_DP_SET_CONTROL_PARAM_LEN			1
#define CMD_DP_SET_GROUP_REJECTION_PARAM_LEN		1
#define CMD_PA_SET_MODE_PARAM_LEN			1
#define CMD_PA_SET_CONTROL_PARAM_LEN			1
#define CMD_RP_SET_PREEMPHASIS_PARAM_LEN		1
#define CMD_RP_SET_DEEMPHASIS_PARAM_LEN			1
#define CMD_RP_SET_PILOT_DEVIATION_PARAM_LEN		1
#define CMD_RP_SET_RDS_DEVIATION_PARAM_LEN		1
#define CMD_DP_BUFFER_SET_GROUP_PARAM_LEN		5
#define CMD_SET_BALANCE_PARAM_LEN			1
#define CMD_SET_VOLUME_PARAM_LEN			1
#define CMD_SET_AUP_BT_SETVOLUME_PARAM_LEN		1
#define CMD_SET_MUTE_PARAM_LEN				2
#define CMD_EXT_SET_MUTE_PARAM_LEN			1
#define CMD_BT_SET_MUTE_PARAM_LEN			1
#define CMD_POWERUP_PARAM_LEN				0
#define CMD_GOTO_STANDBY_PARAM_LEN			0
#define CMD_GOTO_POWERDOWN_PARAM_LEN			0
#define CMD_SELECT_REFERENCE_CLOCK_PARAM_LEN		1
#define CMD_SET_REFERENCE_CLOCK_PLL_PARAM_LEN		1
#define CMD_DP_BUFFER_GET_GROUP_COUNT_PARAM_LEN		0
#define CMD_DP_BUFFER_GET_GROUP_PARAM_LEN		0
#define CMD_IP_ENABLE_CMD_LEN				4
#define CMD_IP_ENABLE_PARAM_LEN				3
#define CMD_IP_DISABLE_CMD_LEN				4
#define CMD_IP_DISABLE_PARAM_LEN			3
#define CMD_TST_TONE_ENABLE_PARAM_LEN			1
#define CMD_TST_TONE_CONNECT_PARAM_LEN			2
#define CMD_TST_TONE_SET_PARAMS_PARAM_LEN		6
#define CMD_FMT_RP_LIMITER_SETCONTROL_PARAM_LEN		2

/* FM HCI Command and event specific */
#define FM_WRITE					0x00
#define FM_READ						0x01
#define FM_CATENA_OPCODE				0xFE
#define HCI_CMD_FM					0xFD50
#define HCI_CMD_VS_WRITE_FILE_BLOCK			0xFC2E
#define FM_EVENT_ID					0x15
#define FM_SUCCESS_STATUS				0x00
#define FM_EVENT					0x01
#define HCI_COMMAND_COMPLETE_EVENT			0x0E
#define HCI_VS_DBG_EVENT				0xFF
#define ST_WRITE_FILE_BLK_SIZE				254
#define ST_MAX_NUMBER_OF_FILE_BLOCKS			256
#define FM_PG1_INTERRUPT_EVENT_LEN			0x04
#define FM_PG2_INTERRUPT_EVENT_LEN			0x06
#define FM_HCI_CMD_HEADER_LEN				6
#define FM_HCI_CMD_PARAM_LEN				5
#define FM_HCI_WRITE_FILE_BLK_HEADER_LEN		5
#define FM_HCI_WRITE_FILE_BLK_PARAM_LEN			4
#define HCI_PACKET_INDICATOR_CMD			0x01
#define HCI_PACKET_INDICATOR_EVENT			0x04
#define HCI_PACKET_INDICATOR_FM_CMD_EVT			0x08
/* FM Functions specific to CG2900 */
#define FM_FUNCTION_ENABLE				0x00
#define FM_FUNCTION_DISABLE				0x01
#define FM_FUNCTION_RESET				0x02
#define FM_FUNCTION_WRITE_COMMAND			0x10
#define FM_FUNCTION_SET_INT_MASK_ALL			0x20
#define FM_FUNCTION_GET_INT_MASK_ALL			0x21
#define FM_FUNCTION_SET_INT_MASK			0x22
#define FM_FUNCTION_GET_INT_MASK			0x23
#define FM_FUNCTION_FIRMWARE_DOWNLOAD			0x30
/* Command succeeded */
#define FM_CMD_STATUS_CMD_SUCCESS			0x00
/* HCI_ERR_HW_FAILURE when no response from the IP */
#define FM_CMD_STATUS_HCI_ERR_HW_FAILURE		0x03
/* HCI_ERR_INVALID_PARAMETERS. */
#define FM_CMD_STATUS_HCI_ERR_INVALID_PARAMETERS	0x12
/* When the host tries to send a command to an IP that hasn't been
 * initialized.
 */
#define FM_CMD_STATUS_IP_UNINIT				0x15
/* HCI_ERR_UNSPECIFIED_ERROR: any other error */
#define FM_CMD_STATUS_HCI_ERR_UNSPECIFIED_ERROR		0x1F
/* HCI_ERR_CMD_DISALLOWED when the host asks for an unauthorized operation
 * (FM state transition for instance)
 */
#define FM_CMD_STATUS_HCI_ERR_CMD_DISALLOWED		0x0C
/* Wrong sequence number for FM FW download command */
#define FM_CMD_STATUS_WRONG_SEQ_NUM			0xF1
/* Unknown file type for FM FW download command */
#define FM_CMD_STATUS_UNKNOWN_FILE_TYPE			0xF2
/* File version mismatch for FM FW download command */
#define FM_CMD_STATUS_FILE_VERSION_MISMATCH		0xF3


/**
 * enum fmd_event - Events received.
 *
 * @FMD_EVENT_OPERATION_COMPLETED: Previous operation has been completed.
 * @FMD_EVENT_ANTENNA_STATUS_CHANGED: Antenna has been changed.
 * @FMD_EVENT_FREQUENCY_CHANGED: Frequency has been changed.
 * @FMD_EVENT_SEEK_COMPLETED: Seek operation has completed.
 * @FMD_EVENT_SCAN_BAND_COMPLETED: Band Scan completed.
 * @FMD_EVENT_BLOCK_SCAN_COMPLETED: Block Scan completed.
 * @FMD_EVENT_AF_UPDATE_SWITCH_COMPLETE: Af Update or AF Switch is complete.
 * @FMD_EVENT_MONO_STEREO_TRANSITION_COMPLETE: Mono stereo transition is
 * completed.
 * @FMD_EVENT_SEEK_STOPPED: Previous Seek/Band Scan/ Block Scan operation is
 * stopped.
 * @FMD_EVENT_GEN_POWERUP: FM IP Powerup has been powered up.
 * @FMD_EVENT_RDSGROUP_RCVD: RDS Groups Full interrupt.
 * @FMD_EVENT_LAST_ELEMENT: Last event, used for keeping count of
 * number of events.
 *
 * Various events received from FM driver for Upper Layer(s) processing.
 */
enum fmd_event {
	FMD_EVENT_OPERATION_COMPLETED,
	FMD_EVENT_ANTENNA_STATUS_CHANGED,
	FMD_EVENT_FREQUENCY_CHANGED,
	FMD_EVENT_SEEK_COMPLETED,
	FMD_EVENT_SCAN_BAND_COMPLETED,
	FMD_EVENT_BLOCK_SCAN_COMPLETED,
	FMD_EVENT_AF_UPDATE_SWITCH_COMPLETE,
	FMD_EVENT_MONO_STEREO_TRANSITION_COMPLETE,
	FMD_EVENT_SEEK_STOPPED,
	FMD_EVENT_GEN_POWERUP,
	FMD_EVENT_RDSGROUP_RCVD,
	FMD_EVENT_LAST_ELEMENT
};

/**
 * enum fmd_mode - FM Driver Modes.
 *
 * @FMD_MODE_IDLE: FM Driver in Idle mode.
 * @FMD_MODE_RX: FM Driver in Rx mode.
 * @FMD_MODE_TX: FM Driver in Tx mode.
 *
 * Various Modes of FM Radio.
 */
enum fmd_mode {
	FMD_MODE_IDLE,
	FMD_MODE_RX,
	FMD_MODE_TX
};

/**
 * enum fmd_antenna - Antenna selection.
 *
 * @FMD_ANTENNA_EMBEDDED: Embedded Antenna.
 * @FMD_ANTENNA_WIRED: Wired Antenna.
 *
 * Antenna to be used for FM Radio.
 */
enum fmd_antenna {
	FMD_ANTENNA_EMBEDDED,
	FMD_ANTENNA_WIRED
};

/**
 * enum fmd_grid - Grid used on FM Radio.
 *
 * @FMD_GRID_50KHZ: 50  kHz grid spacing.
 * @FMD_GRID_100KHZ: 100  kHz grid spacing.
 * @FMD_GRID_200KHZ: 200  kHz grid spacing.
 *
 * Spacing used on FM Radio.
 */
enum fmd_grid {
	FMD_GRID_50KHZ,
	FMD_GRID_100KHZ,
	FMD_GRID_200KHZ
};

/**
 * enum fmd_emphasis - De-emphasis/Pre-emphasis level.
 *
 * @FMD_EMPHASIS_NONE: De-emphasis Disabled.
 * @FMD_EMPHASIS_50US: 50 us de-emphasis/pre-emphasis level.
 * @FMD_EMPHASIS_75US: 75 us de-emphasis/pre-emphasis level.
 *
 * De-emphasis/Pre-emphasis level used on FM Radio.
 */
enum fmd_emphasis {
	FMD_EMPHASIS_NONE = 0,
	FMD_EMPHASIS_50US = 1,
	FMD_EMPHASIS_75US = 2
};

/**
 * enum fmd_freq_range - Frequency range.
 *
 * @FMD_FREQRANGE_EUROAMERICA: EU/US Range (87.5 - 108 MHz).
 * @FMD_FREQRANGE_JAPAN: Japan Range (76 - 90 MHz).
 * @FMD_FREQRANGE_CHINA: China Range (70 - 108 MHz).
 *
 * Various Frequency range(s) supported by FM Radio.
 */
enum fmd_freq_range {
	FMD_FREQRANGE_EUROAMERICA,
	FMD_FREQRANGE_JAPAN,
	FMD_FREQRANGE_CHINA
};

/**
 * enum fmd_stereo_mode - FM Driver Stereo Modes.
 *
 * @FMD_STEREOMODE_OFF: Streo Blending Off.
 * @FMD_STEREOMODE_MONO: Mono Mode.
 * @FMD_STEREOMODE_BLENDING: Blending Mode.
 *
 * Various Stereo Modes of FM Radio.
 */
enum fmd_stereo_mode {
	FMD_STEREOMODE_OFF,
	FMD_STEREOMODE_MONO,
	FMD_STEREOMODE_BLENDING
};

/**
 * enum fmd_pilot_tone - Pilot Tone Selection
 *
 * @FMD_PILOT_TONE_DISABLED: Pilot Tone to be disabled.
 * @FMD_PILOT_TONE_ENABLED: Pilot Tone to be enabled.
 *
 * Pilot Tone to be enabled or disabled.
 */
enum fmd_pilot_tone {
	FMD_PILOT_TONE_DISABLED,
	FMD_PILOT_TONE_ENABLED
};

/**
 * enum fmd_output - Output of Sample Rate Converter.
 *
 * @FMD_OUTPUT_DISABLED: Sample Rate converter in disabled.
 * @FMD_OUTPUT_I2S: I2S Output from Sample rate converter.
 * @FMD_OUTPUT_PARALLEL: Parallel output from sample rate converter.
 *
 * Sample Rate Converter's output to be set on Connectivity Controller.
 */
enum fmd_output {
	FMD_OUTPUT_DISABLED,
	FMD_OUTPUT_I2S,
	FMD_OUTPUT_PARALLEL
};

/**
 * enum fmd_input - Audio Input to Sample Rate Converter.
 *
 * @FMD_INPUT_ANALOG: Selects the ADC's as audio source
 * @FMD_INPUT_DIGITAL: Selects Digital Input as audio source.
 *
 * Audio Input source for Sample Rate Converter.
 */
enum fmd_input {
	FMD_INPUT_ANALOG,
	FMD_INPUT_DIGITAL
};

/**
 * enum fmd_rds_mode - RDS Mode to be selected for FM Rx.
 *
 * @FMD_SWITCH_OFF_RDS: RDS Decoding disabled in FM Chip.
 * @FMD_SWITCH_ON_RDS: RDS Decoding enabled in FM Chip.
 * @FMD_SWITCH_ON_RDS_ENHANCED_MODE: Enhanced RDS Mode.
 * @FMD_SWITCH_ON_RDS_SIMULATOR: RDS Simulator switched on in FM Chip.
 *
 * RDS Mode to be selected for FM Rx.
 */
enum fmd_rds_mode {
	FMD_SWITCH_OFF_RDS,
	FMD_SWITCH_ON_RDS,
	FMD_SWITCH_ON_RDS_ENHANCED_MODE,
	FMD_SWITCH_ON_RDS_SIMULATOR
};

/**
 * enum fmd_rds_group_rejection_mode - RDS Group Rejection
 * to be selected for FM Rx.
 *
 * @FMD_RDS_GROUP_REJECTION_ON: Group rejection is enabled in FM Chip.
 * @FMD_RDS_GROUP_REJECTION_OFF: Group rejection is disabled in FM Chip.
 *
 * RDS Group rejection to be selected for FM Rx.
 */
enum fmd_rds_group_rejection_mode {
	FMD_RDS_GROUP_REJECTION_ON,
	FMD_RDS_GROUP_REJECTION_OFF
};

/**
 * enum fmd_tst_tone_status - Test Tone Generator Status.
 *
 * @FMD_TST_TONE_OFF: Test Tone Generator is off.
 * @FMD_TST_TONE_ON_W_SRC: Test Tone Gen. is on with Sample Rate Conversion.
 * @FMD_TST_TONE_ON_WO_SRC: Test Tone Gen. is on without Sample Rate Conversion.
 *
 * Test Tone Generator status to be set.
 */
enum fmd_tst_tone_status {
	FMD_TST_TONE_OFF,
	FMD_TST_TONE_ON_W_SRC,
	FMD_TST_TONE_ON_WO_SRC
};

/**
 * enum fmd_tst_tone_audio_mode - Test Tone Generator Audio Output/Input Mode.
 *
 * @FMD_TST_TONE_AUDIO_NORMAL: Normal Audio.
 * @FMD_TST_TONE_AUDIO_ZERO: Zero.
 * @FMD_TST_TONE_AUDIO_TONE_1: Tone 1.
 * @FMD_TST_TONE_AUDIO_TONE_2: Tone 2.
 * @FMD_TST_TONE_AUDIO_TONE_SUM: Sum of Tone 1 and Tone 2.
 *
 * Test Tone Generator Audio Output/Input Modes.
 */
enum fmd_tst_tone_audio_mode {
	FMD_TST_TONE_AUDIO_NORMAL,
	FMD_TST_TONE_AUDIO_ZERO,
	FMD_TST_TONE_AUDIO_TONE_1,
	FMD_TST_TONE_AUDIO_TONE_2,
	FMD_TST_TONE_AUDIO_TONE_SUM
};

/**
 * enum fmd_tst_tone - Test Tone of Internal Tone Generator.
 *
 * @FMD_TST_TONE_1: Test Tone 1
 * @FMD_TST_TONE_2: Test Tone 2
 *
 * Test Tone.
 */
enum fmd_tst_tone {
	FMD_TST_TONE_1,
	FMD_TST_TONE_2
};

/**
 * enum fmd_tst_tone_waveform - Test Tone Waveform of Internal Tone Generator.
 *
 * @FMD_TST_TONE_SINE: Sine wave
 * @FMD_TST_TONE_PULSE: Pulse wave
 *
 * Test Tone waveform.
 */
enum fmd_tst_tone_waveform {
	FMD_TST_TONE_SINE,
	FMD_TST_TONE_PULSE
};

/* Callback function to receive radio events. */
typedef void(*fmd_radio_cb)(
		u8 event,
		bool event_successful
		);

/**
 * fmd_init() - Initialize the FM Driver internal structures.
 *
 * Returns:
 *	 0,  if no error.
 *	 -EIO, if there is an error.
 */
int fmd_init(void);

/**
 * fmd_exit() - De-initialize the FM Driver.
 */
void fmd_exit(void);

/**
 * fmd_register_callback() - Function to register callback function.
 *
 * This function registers the callback function provided by upper layers.
 * @callback: Fmradio call back Function pointer
 *
 * Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 */
int fmd_register_callback(
			fmd_radio_cb callback
			);

/**
 * fmd_get_version() - Retrieves the FM HW and FW version.
 *
 * @version: (out) Version Array
 *
 *  Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if parameters are not valid.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_get_version(
			u16 *version
			);

/**
 * fmd_set_mode() - Starts a transition to the given mode.
 *
 * @mode: Transition mode
 *
 *  Returns:
 *	 0,  if set mode done successfully.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_set_mode(
			u8 mode
			);

/**
 * fmd_get_freq_range_properties() - Retrieves Freq Range Properties.
 *
 * @range: range of freq
 * @min_freq: (out) Minimum Frequency of the Band in kHz.
 * @max_freq: (out) Maximum Frequency of the Band in kHz
 *
 *  Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if parameter is invalid.
 */
int fmd_get_freq_range_properties(
			u8 range,
			u32  *min_freq,
			u32  *max_freq
			);

/**
 * fmd_set_antenna() - Selects the antenna to be used in receive mode.
 *
 * embedded - Selects the embedded antenna, wired- Selects the wired antenna.
 * @antenna: Antenna Type
 *
 *  Returns:
 *	 0,  if set antenna done successfully.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_set_antenna(
			u8 antenna
			);

/**
 * fmd_get_antenna() - Retrieves the currently used antenna type.
 *
 * @antenna: (out) Antenna Selected on FM Radio.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 */
int fmd_get_antenna(
			u8 *antenna
			);

/**
 * fmd_set_freq_range() - Sets the FM band.
 *
 * @range: freq range
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_set_freq_range(
			u8 range
			);

/**
 * fmd_get_freq_range() - Gets the FM band currently in use.
 *
 * @range: (out) Frequency Range set on FM Radio.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 */
int fmd_get_freq_range(
			u8 *range
			);

/**
 * fmd_rx_set_grid() - Sets the tuning grid.
 *
 * @grid: Tuning grid size
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_set_grid(
			u8 grid
			);

/**
 * fmd_rx_set_frequency() - Sets the FM Channel.
 *
 * @freq: Frequency to Set in Khz
 *
 *  Returns:
 *	 0,  if set frequency done successfully.
 *	 -EINVAL,  if parameters are invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_set_frequency(
			u32 freq
			);

/**
 * fmd_rx_get_frequency() - Gets the currently used FM Channel.
 *
 * @freq: (out) Current Frequency set on FM Radio.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL,  if parameters are invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_get_frequency(
			u32  *freq
			);

/**
 * fmd_rx_set_stereo_mode() - Sets the stereomode functionality.
 *
 * @mode: FMD_STEREOMODE_MONO, FMD_STEREOMODE_STEREO and
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_set_stereo_mode(
			u8 mode
			);

/**
 * fmd_rx_set_stereo_ctrl_blending_rssi() - Sets the stereo blending control setting.
 *
 * @min_rssi: Defines the RF level (at the antenna pin) at which the stereo blending
 *			function will stop limiting the channel separation
 * @max_rssi: Defines the RF level (at the antenna pin) at which the stereo blending
 *			function will start limiting the channel separation.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_set_stereo_ctrl_blending_rssi(
			u16 min_rssi,
			u16 max_rssi
			);

/**
 * fmd_rx_get_stereo_mode() - Gets the currently used FM mode.
 *
 * FMD_STEREOMODE_MONO, FMD_STEREOMODE_STEREO and
 * FMD_STEREOMODE_AUTO.
 * @mode: (out) Mode set on FM Radio, stereo or mono.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 */
int fmd_rx_get_stereo_mode(
			u8 *mode
			);

/**
 * fmd_rx_get_signal_strength() - Gets the RSSI level of current frequency.
 *
 * @strength: (out) RSSI level of current channel.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_get_signal_strength(
			u16  *strength
			);

/**
 * fmd_rx_set_stop_level() - Sets the FM Rx Seek stop level.
 *
 * @stoplevel: seek stop level
 *
 *  Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 */
int fmd_rx_set_stop_level(
			u16 stoplevel
			);

/**
 * fmd_rx_get_stop_level() - Gets the current FM Rx Seek stop level.
 *
 * @stoplevel: (out) RSSI Threshold set on FM Radio.
 *
 *  Returns:
 *	 0,  if no error.
 *	 -EINVAL, if parameter is invalid.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 */
int fmd_rx_get_stop_level(
			u16  *stoplevel
			);

/**
 * fmd_rx_seek() - Perform FM Seek.
 *
 * Starts searching relative to the actual channel with
 * a specific direction, stop.
 *  level and optional noise levels
 * @upwards: scan up
 *
 *  Returns:
 *	 0,  if seek started successfully.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EBUSY, if FM Driver is not in idle state.
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_seek(
			bool upwards
			);

/**
 * fmd_rx_stop_seeking() - Stops a currently active seek or scan band.
 *
 *  Returns:
 *	 0,  if stop seek done successfully.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -ENOEXEC, if FM Driver is
 * not currently in Seek or Scan State..
 *	 -EINVAL, if wrong response received from chip.
 */
int fmd_rx_stop_seeking(void);

/**
 * fmd_rx_af_update_start() - Perform AF update.
 *
 * This is used to switch to a shortly tune to a AF freq,
 * measure its RSSI and tune back to the original frequency.
 * @freq: Alternative frequncy in KHz to be set for AF updation.
 *
 *  Returns:
 * -EBUSY, if FM Driver is not in idle state.
 * 0,  if no error.
 * -ENOEXEC, if preconditions are violated.
 */
int fmd_rx_af_update_start(
			 u32 freq
			 );

/**
 * fmd_rx_get_af_update_result() - Retrive result of AF update.
 *
 * Retrive the RSSI level of the Alternative frequency.
 * @af_level: RSSI level of the Alternative frequency.
 *
 * Returns:
 * -EBUSY, if FM Driver is not in idle state.
 * 0,  if no error.
 * -EINVAL, if parameter is invalid.
 * -ENOEXEC, if preconditions are violated.
 */
int fmd_rx_get_af_update_result(
			 u16 *af_level
			 );

/**
 * fmd_af_switch_start() -Performs AF switch.
 *
 * @freq: Frequency to Set in Khz.
 * @picode:programable id,unique for each station.
 *
 *  Returns:
 * -EBUSY, if FM Driver is not in idle state.
 * 0,  if no error and if AF switch started successfully.
 * -ENOEXEC, if preconditions are violated.
 */
int fmd_rx_af_switch_start(
			u32 freq,
			u16  picode
			);

/**
 * fmd_rx_get_af_switch_results() -Retrieves the results of AF Switch.
 *
 * @afs_conclusion: Conclusion of AF switch.
 * @afs_level: RSSI level of the Alternative frequnecy.
 * @afs_pi: PI code of the alternative channel (if found).
 *
 * Returns:
 * -EBUSY, if FM Driver is not in idle state.
 * 0,  if no error.
 * -EINVAL, if parameter is invalid.
 * -ENOEXEC, if preconditions are violated.
 */
int fmd_rx_get_af_switch_results(
			 u16 *afs_conclusion,
			 u16 *afs_level,
			 u16 *afs_pi
			 );

/**
 * fmd_rx_scan_band() - Starts Band Scan.
 *
 * Starts scanning the active band for the strongest
 * channels above a threshold.
 * @max_channels_to_scan: Maximum number of channels to scan.
 *
 * Returns:
 *   0,  if scan band started successfully.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_rx_scan_band(
			u8 max_channels_to_scan
			);

/**
 * fmd_rx_get_max_channels_to_scan() - Retreives the maximum channels.
 *
 * Retrieves the maximum number of channels that can be found during
 * band scann.
 * @max_channels_to_scan: (out) Maximum number of channels to scan.
 *
 * Returns:
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if parameter is invalid.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_rx_get_max_channels_to_scan(
			u8 *max_channels_to_scan
			);

/**
 * fmd_rx_get_scan_band_info() - Retrieves Channels found during scanning.
 *
 * Retrieves the scanned active band
 * for the strongest channels above a threshold.
 * @index: (out) Index value to retrieve the channels.
 * @numchannels: (out) Number of channels found during Band Scan.
 * @channels: (out) Channels found during band scan.
 * @rssi: (out) Rssi of channels found during Band scan.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_rx_get_scan_band_info(
			u32 index,
			u16  *numchannels,
			u16  *channels,
			u16  *rssi
			);

/**
 * fmd_block_scan() - Starts Block Scan.
 *
 * Starts block scan for retriving the RSSI level of channels
 * in the given block.
 * @start_freq: Starting frequency of the block from where scanning has
 * to be started.
 * @stop_freq: End frequency of the block to be scanned.
 * @antenna: Antenna to be used during scanning.
 *
 * Returns:
 *   0,  if scan band started successfully.
 *   -EINVAL,  if parameters are invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_block_scan(
			u32 start_freq,
			u32 stop_freq,
			u8 antenna
			);

/**
 * fmd_get_block_scan_result() - Retrieves RSSI Level of channels.
 *
 * Retrieves the RSSI level of the channels in the block.
 * @index: (out) Index value to retrieve the channels.
 * @numchannels: (out) Number of channels found during Band Scan.
 * @rssi: (out) Rssi of channels found during Band scan.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_get_block_scan_result(
			u32 index,
			u16 *numchannels,
			u16 *rssi
			);

/**
 * fmd_rx_get_rds() - Gets the current status of RDS transmission.
 *
 * @on: (out) RDS status
 *
 *  Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_rx_get_rds(
			bool  *on
			);

/**
 * fmd_rx_buffer_set_size() - Sets the number of groups that the data buffer.
 * can contain and clears the buffer.
 *
 * @size: buffer size
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_rx_buffer_set_size(
			u8 size
			);

/**
 * fmd_rx_buffer_set_threshold() - RDS Buffer Threshold level in FM Chip.
 *
 * Sets the group number at which the RDS buffer full interrupt must be
 * generated. The interrupt will be set after reception of the group.
 * @threshold: threshold level.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_rx_buffer_set_threshold(
			u8 threshold
			);

/**
 * fmd_rx_set_rds() - Enables or disables demodulation of RDS data.
 *
 * @on_off_state : Rx Set ON/OFF control
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_rx_set_rds(
			u8 on_off_state
			);

/**
 * fmd_rx_set_rds_group_rejection() - Enables or disables group rejection
 * in case groups with erroneous blocks are received.
 *
 * @on_off_state : Rx Group Rejection ON /OFF control
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */

int fmd_rx_set_rds_group_rejection(
			u8 on_off_state
			);

/**
 * fmd_rx_get_low_level_rds_groups() - Gets Low level RDS group data.
 *
 * @index: RDS group index
 * @block1: (out) RDS Block 1
 * @block2: (out) RDS Block 2
 * @block3: (out) RDS Block 3
 * @block4: (out) RDS Block 4
 * @status1: (out) RDS data status 1
 * @status2: (out) RDS data status 2
 * @status3: (out) RDS data status 3
 * @status4: (out) RDS data status 4
 *
 *  Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_rx_get_low_level_rds_groups(
			u8 index,
			u16  *block1,
			u16  *block2,
			u16  *block3,
			u16  *block4,
			u8  *status1,
			u8  *status2,
			u8  *status3,
			u8  *status4
			);

/**
 * fmd_tx_set_pa() - Enables or disables the Power Amplifier.
 *
 * @on: Power Amplifier current state to set
 *
 * Returns:
 *   0,  if set Power Amplifier done successfully.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_pa(
			bool on
			);

/**
 * fmd_tx_set_signal_strength() - Sets the RF-level of the output FM signal.
 *
 * @strength: Signal strength to be set for FM Tx in dBuV.
 *
 * Returns:
 *   0,  if set RSSI Level done successfully.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_signal_strength(
			u16 strength
			);

/**
 * fmd_tx_get_signal_strength() - Retrieves current RSSI of FM Tx.
 *
 * @strength: (out) Strength of signal being transmitted in dBuV.
 *
 * Returns:
 *   0,  if no error.
 *   -EINVAL, if parameter is invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_signal_strength(
			u16  *strength
			);

/**
 * fmd_tx_set_freq_range() - Sets the FM band and specifies the custom band.
 *
 * @range: Freq range to set on FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -EINVAL, if parameter is invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_freq_range(
			u8 range
			);

/**
 * fmd_tx_get_freq_range() - Gets the FM band currently in use.
 *
 * @range: (out) Frequency Range set on Fm Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 */
int fmd_tx_get_freq_range(
			u8 *range
			);

/**
 * fmd_tx_set_grid() - Sets the tuning grid size.
 *
 * @grid: FM Grid (50 Khz, 100 Khz, 200 Khz) to be set for FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -EINVAL, if parameter is invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_grid(
			u8 grid
			);

/**
 * fmd_tx_get_grid() - Gets the current tuning grid size.
 *
 * @grid: (out) FM Grid (50 Khz, 100 Khz, 200 Khz) currently set on FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_grid(
			u8 *grid
			);

/**
 * fmd_tx_set_preemphasis() - Sets the Preemphasis characteristic of the Tx.
 *
 * @preemphasis: Pre-emphasis level to be set for FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_preemphasis(
			u8 preemphasis
			);

/**
 * fmd_tx_get_preemphasis() - Gets the currently used Preemphasis char of th FM Tx.
 *
 * @preemphasis: (out) Preemphasis Level used for FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_preemphasis(
			u8 *preemphasis
			);

/**
 * fmd_tx_set_frequency() - Sets the FM Channel for Tx.
 *
 * @freq: Freq to be set for transmission.
 *
 * Returns:
 *   0,  if set frequency done successfully.
 *   -EINVAL,  if parameters are invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_frequency(
			u32 freq
			);

/**
 * fmd_rx_get_frequency() - Gets the currently used Channel for Tx.
 *
 * @freq: (out) Frequency set on FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -EINVAL,  if parameters are invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_get_frequency(
			u32  *freq
			);

/**
 * fmd_tx_enable_stereo_mode() - Sets Stereo mode state for TX.
 *
 * @enable_stereo_mode: Flag indicating enabling or disabling Stereo mode.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_enable_stereo_mode(
			bool enable_stereo_mode
			);

/**
 * fmd_tx_get_stereo_mode() - Gets the currently used FM Tx stereo mode.
 *
 * @stereo_mode: (out) Stereo Mode state set on FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -EINVAL, if parameter is invalid.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_stereo_mode(
			bool  *stereo_mode
			);

/**
 * fmd_tx_set_pilot_deviation() - Sets pilot deviation in HZ
 *
 * @deviation: Pilot deviation in HZ to set on FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_pilot_deviation(
			u16 deviation
			);

/**
 * fmd_tx_get_pilot_deviation() - Retrieves the current pilot deviation.
 *
 * @deviation: (out) Pilot deviation set on FM Tx.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_pilot_deviation(
			u16  *deviation
			);

/**
 * fmd_tx_set_rds_deviation() - Sets Rds deviation in HZ.
 *
 * @deviation: RDS deviation in HZ.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_rds_deviation(
			u16 deviation
			);

/**
 * fmd_tx_get_rds_deviation() - Retrieves the current Rds deviation.
 *
 * @deviation: (out) RDS deviation currently set.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_rds_deviation(
			u16  *deviation
			);

/**
 * fmd_tx_set_rds() - Enables or disables RDS transmission for Tx.
 *
 * @on: Boolean - RDS ON
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_rds(
			bool on
			);

/**
 * fmd_rx_get_rds() - Gets the current status of RDS transmission for FM Tx.
 *
 * @on: (out) Rds enabled or disabled.
 *
 *Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_tx_get_rds(
			bool  *on
			);

/**
 * fmd_tx_set_group() - Programs a grp on a certain position in the RDS buffer.
 *
 * @position: RDS group position
 * @block1: Data to be transmitted in Block 1
 * @block2: Data to be transmitted in Block 2
 * @block3: Data to be transmitted in Block 3
 * @block4: Data to be transmitted in Block 4
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if parameters are invalid.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_set_group(
			u16 position,
			u8  *block1,
			u8  *block2,
			u8  *block3,
			u8  *block4
			);

/**
 * fmd_tx_buffer_set_size() - Controls the size of the RDS buffer in groups.
 *
 * @buffer_size: RDS buffer size.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_tx_buffer_set_size(
			u16 buffer_size
			);

/**
 * fmd_bt_set_volume() - Sets the receive audio volume.
 *
 * @volume: Audio volume level
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_bt_set_volume(
            u8 volume
            );

/**
 * fmd_set_volume() - Sets the receive audio volume.
 *
 * @volume: Audio volume level
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_set_volume(
			u8 volume
			);

/**
 * fmd_get_volume() - Retrives the current audio volume.
 *
 * @volume: Analog Volume level.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if parameter is invalid.
 *   -EBUSY, if FM Driver is not in idle state.
 */
int fmd_get_volume(
			u8 *volume
			);

/**
 * fmd_set_balance() - Controls the receiver audio balance.
 *
 * @balance: Audio balance level
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_set_balance(
			s8 balance
			);

/**
 * fmd_set_mute() - Enables or disables muting of the analog audio(DAC).
 *
 * @mute_on: bool of mute on
 *
 * Returns:
 *   0,  if mute done successfully.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_set_mute(
			bool mute_on
			);

/**
 * fmd_bt_set_mute() - Enables or disables muting of the audio channel
 * with Immediate effect for BT SRC.
 *
 * @mute_on: bool to Mute
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_bt_set_mute(
                   bool mute_on
                   );

/**
 * fmd_ext_set_mute() - Enables or disables muting of the audio channel.
 *
 * @mute_on: bool to Mute
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_ext_set_mute(
			bool mute_on
			);

/**
 * fmd_power_up() - Puts the system in Powerup state.
 *
 * Returns:
 *   0,  if power up command sent successfully to chip.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_power_up(void);

/**
 * fmd_goto_standby() - Puts the system in standby mode.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_goto_standby(void);

/**
 * fmd_goto_power_down() - Puts the system in Powerdown mode.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_goto_power_down(void);

/**
 * fmd_select_ref_clk() - Selects the FM reference clock.
 *
 * @ref_clk: Ref Clock.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_select_ref_clk(
			u16 ref_clk
			);

/**
 * fmd_set_ref_clk_pll() - Sets the freq of Referece Clock.
 *
 * Sets frequency and offset correction properties of the external
 * reference clock of the PLL
 * @freq: PLL Frequency/ 2 in kHz.
 *
 * Returns:
 *   0,  if no error.
 *   -ENOEXEC, if preconditions are violated.
 *   -EBUSY, if FM Driver is not in idle state.
 *   -EINVAL, if wrong response received from chip.
 */
int fmd_set_ref_clk_pll(
			u16 freq
			);

/**
 * fmd_send_fm_ip_enable()- Enables the FM IP.
 *
 * Returns:
 *	 0: If there is no error.
 *	 -ETIME: Otherwise
 */
int fmd_send_fm_ip_enable(void);

/**
 * fmd_send_fm_ip_disable()- Disables the FM IP.
 *
 * Returns:
 *	 0, If there is no error.
 *	 -ETIME: Otherwise
 */
int fmd_send_fm_ip_disable(void);

/**
 * fmd_send_fm_firmware() - Send the FM Firmware File to Device.
 *
 * @fw_buffer: Firmware to be downloaded.
 * @fw_size: Size of firmware to be downloaded.
 *
 * Returns:
 *	 0, If there is no error.
 *	 -ETIME: Otherwise
 */
int fmd_send_fm_firmware(
			u8 *fw_buffer,
			u16 fw_size
			);

/**
 * fmd_int_bufferfull() - RDS Groups availabe for reading by Host.
 *
 * Gets the number of groups that are available in the
 * buffer. This function is called in RX mode to read RDS groups.
 * @number_of_rds_groups: Number of RDS groups ready to
 * be read from the Host.
 *
 * Returns:
 *	 0, If there is no error.
 *	 corresponding error Otherwise
 */
int fmd_int_bufferfull(
			u16 *number_of_rds_groups
			);

/**
 * fmd_start_rds_thread() - Starts the RDS Thread for receiving RDS Data.
 *
 * This is started by Application when it wants to receive RDS Data.
 * @cb_func: Callback function for receiving RDS Data
 */
void fmd_start_rds_thread(
			cg2900_fm_rds_cb cb_func
			);
/**
 * fmd_stop_rds_thread() - Stops the RDS Thread when Application does not
 * want to receive RDS.
 */
void fmd_stop_rds_thread(void);

/**
 * fmd_get_rds_sem() - Block on RDS Semaphore.
 * Till irpt_BufferFull is received, RDS Task is blocked.
 */
void fmd_get_rds_sem(void);

/**
 * fmd_set_rds_sem() - Unblock on RDS Semaphore.
 * on receiving  irpt_BufferFull, RDS Task is un-blocked.
 */
void fmd_set_rds_sem(void);

/**
 * fmd_set_dev() - Set FM device.
 *
 * @dev: FM Device
 *
 * Returns:
 *	 0, If there is no error.
 *	 corresponding error Otherwise
 */
int fmd_set_dev(
			struct device *dev
			);

/**
 * fmd_set_test_tone_generator_status()- Sets the Test Tone Generator.
 *
 * This function is used to enable/disable the Internal Tone Generator of
 * CG2900.
 * @test_tone_status: Status of tone generator.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int fmd_set_test_tone_generator_status(
			u8 test_tone_status
			);

/**
 * fmd_test_tone_connect()- Connect Audio outputs/inputs.
 *
 * This function connects the audio outputs/inputs of the
 * Internal Tone Generator of CG2900.
 * @left_audio_mode: Left Audio Output Mode.
 * @right_audio_mode: Right Audio Output Mode.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int fmd_test_tone_connect(
			u8 left_audio_mode,
			u8 right_audio_mode
			);

/**
 * fmd_test_tone_set_params()- Sets the Test Tone Parameters.
 *
 * This function is used to set the parameters of
 * the Internal Tone Generator of CG2900.
 * @tone_gen: Tone to be configured (Tone 1 or Tone 2)
 * @frequency: Frequency of the tone.
 * @volume: Volume of the tone.
 * @phase_offset: Phase offset of the tone.
 * @dc: DC to add to tone.
 * @waveform: Waveform to generate, sine or pulse.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int fmd_test_tone_set_params(
			u8 tone_gen,
			u16 frequency,
			u16 volume,
			u16 phase_offset,
			u16 dc,
			u8 waveform
			);

/**
 * fmd_rx_set_deemphasis()- Connect Audio outputs/inputs.
 *
 * This function sets the de-emphasis filter to the
 * specified de-empahsis level.
 * @deemphasis: De-emphasis level to set.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int fmd_rx_set_deemphasis(
			u8 deemphasis
			);

/**
 * fmd_limiter_setcontrol()- Sets the Limiter Controls.
 *
 * This function sets the limiter control.
 * @audio_deviation: Limiting level of Audio Deviation.
 * @notification_hold_off_time: Minimum time between
 * two limiting interrupts.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int fmd_limiter_setcontrol(
			u16 audio_deviation,
			u16 notification_hold_off_time
			);

#endif /* _FMDRIVER_H_  */
