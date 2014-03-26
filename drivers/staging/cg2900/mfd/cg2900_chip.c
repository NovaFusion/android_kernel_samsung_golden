/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * Hemant Gupta (hemant.gupta@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson CG2900 GPS/BT/FM controller.
 */
#define NAME					"cg2900_chip"
#define pr_fmt(fmt)				NAME ": " fmt "\n"

#include <asm/byteorder.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/mfd/core.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

#include "cg2900.h"
#include "cg2900_chip.h"
#include "cg2900_core.h"
#include "cg2900_lib.h"

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

#define MAIN_DEV				(main_info->dev)
#define BOOT_DEV				(info->user_in_charge->dev)

#define WQ_NAME					"cg2900_chip_wq"

/*
 * After waiting the first 500 ms we should just try to get the selftest results
 * for another number of poll attempts
 */
#define MAX_NBR_OF_POLLS			50

#define LINE_TOGGLE_DETECT_TIMEOUT		100	/* ms */
#define CHIP_READY_TIMEOUT			100	/* ms */
#define CHIP_STARTUP_TIMEOUT			15000	/* ms */
#define CHIP_SHUTDOWN_TIMEOUT			15000	/* ms */
#define POWER_SW_OFF_WAIT			500	/* ms */
#define SELFTEST_INITIAL			40	/* ms */
#define SELFTEST_POLLING			20	/* ms */

/** CHANNEL_BT_CMD - Bluetooth HCI H:4 channel
 * for Bluetooth commands in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_BT_CMD				0x01

/** CHANNEL_BT_ACL - Bluetooth HCI H:4 channel
 * for Bluetooth ACL data in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_BT_ACL				0x02

/** CHANNEL_BT_EVT - Bluetooth HCI H:4 channel
 * for Bluetooth events in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_BT_EVT				0x04

/** CHANNEL_NFC - Bluetooth HCI H:4 channel
 * for NFC in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_NFC			0x05

/** CHANNEL_ANT_CMD - Bluetooth HCI H:4 channel
 * for ANT command in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_ANT_CMD			0x0C

/** CHANNEL_ANT_DAT - Bluetooth HCI H:4 channel
 * for ANT data in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_ANT_DAT			0x0E

/** CHANNEL_FM_RADIO - Bluetooth HCI H:4 channel
 * for FM radio in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_FM_RADIO			0x08

/** CHANNEL_GNSS - Bluetooth HCI H:4 channel
 * for GNSS in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_GNSS				0x09

/** CHANNEL_DEBUG - Bluetooth HCI H:4 channel
 * for internal debug data in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_DEBUG				0x0B

/** CHANNEL_STE_TOOLS - Bluetooth HCI H:4 channel
 * for development tools data in the ST-Ericsson connectivity controller.
 */
#define CHANNEL_STE_TOOLS			0x0D

/** CHANNEL_DEV_MGMT - Device Management HCI H:4 channel
 * for driver use only, to send device commands for initialization
 * and shutdown on the ST-Ericsson connectivity controller.
 */
#define CHANNEL_DEV_MGMT			0x80

/** CHANNEL_DEV_MGMT_AUDIO - Device Management HCI H:4 channel
 * for audio driver use only, to send device commands on
 * the ST-Ericsson connectivity controller.
 */
#define CHANNEL_DEV_MGMT_AUDIO		0x81

/** CHANNEL_HCI_LOGGER - Bluetooth HCI H:4 channel
 * for logging all transmitted H4 packets (on all channels).
 */
#define CHANNEL_HCI_LOGGER			0xFA

/** CHANNEL_CORE - Bluetooth HCI H:4 channel
 * for user space control of the ST-Ericsson connectivity controller.
 */
#define CHANNEL_CORE				0xFD

/** CHANNEL_HCI_RAW - Bluetooth HCI H:4 channel
 * for user space read/write on the ST-Ericsson connectivity controller.
 */
#define CHANNEL_HCI_RAW				0xFE

/** CG2900_BT_CMD - Bluetooth HCI H4 channel for Bluetooth commands.
 */
#define CG2900_BT_CMD				"cg2900_bt_cmd"

/** CG2900_BT_ACL - Bluetooth HCI H4 channel for Bluetooth ACL data.
 */
#define CG2900_BT_ACL				"cg2900_bt_acl"

/** CG2900_BT_EVT - Bluetooth HCI H4 channel for Bluetooth events.
 */
#define CG2900_BT_EVT				"cg2900_bt_evt"

/** CG2900_NFC - Bluetooth HCI H4 channel for NFC.
 */
#define CG2900_NFC				"cg2900_nfc"

/** CG2900_ANT_CMD - Bluetooth HCI H4 channel for ANT Command.
 */
#define CG2900_ANT_CMD			"cg2900_antradio_cmd"

/** CG2900_ANT_DAT - Bluetooth HCI H4 channel for ANT Data.
 */
#define CG2900_ANT_DAT			"cg2900_antradio_data"

/** CG2900_FM_RADIO - Bluetooth HCI H4 channel for FM radio.
 */
#define CG2900_FM_RADIO				"cg2900_fm_radio"

/** CG2900_GNSS - Bluetooth HCI H4 channel for GNSS.
 */
#define CG2900_GNSS				"cg2900_gnss"

/** CG2900_DEBUG - Bluetooth HCI H4 channel for internal debug data.
 */
#define CG2900_DEBUG				"cg2900_debug"

/** CG2900_STE_TOOLS - Bluetooth HCI H4 channel for development tools data.
 */
#define CG2900_STE_TOOLS			"cg2900_ste_tools"

/** CG2900_HCI_LOGGER - BT channel for logging all transmitted H4 packets.
 * Data read is copy of all data transferred on the other channels.
 * Only write allowed is configuration of the HCI Logger.
 */
#define CG2900_HCI_LOGGER			"cg2900_hci_logger"

/** CG2900_DEV_AUDIO - HCI channel for sending vendor specific commands
 * meant for device audio configuration commands.
 */
#define CG2900_VS_AUDIO				"cg2900_vs_audio"

/** CG2900_FM_AUDIO - HCI channel for FM audio configuration commands.
 * Maps to FM Radio channel.
 */
#define CG2900_FM_AUDIO				"cg2900_fm_audio"

/** CG2900_CORE- Channel for keeping ST-Ericsson CG2900 enabled.
 * Opening this channel forces the chip to stay powered.
 * No data can be written to or read from this channel.
 */
#define CG2900_CORE				"cg2900_core"

/** CG2900_HCI_RAW - Channel for HCI RAW data exchange.
 * Opening this channel will not allow any others HCI Channels
 * to be opened except Logger Channel.
 */
#define CG2900_HCI_RAW				"cg2900_hci_raw"

/**
 * enum main_state - Main-state for CG2900 driver.
 * @CG2900_INIT:	CG2900 initializing.
 * @CG2900_IDLE:	No user registered to CG2900 driver.
 * @CG2900_BOOTING:	CG2900 booting after first user is registered.
 * @CG2900_CLOSING:	CG2900 closing after last user has deregistered.
 * @CG2900_RESETING:	CG2900 reset requested.
 * @CG2900_ACTIVE:	CG2900 up and running with at least one user.
 * @CG2900_ACTIVE_BEFORE_SELFTEST: CG2900 up and running before
 *		BT self test is run.
 */
enum main_state {
	CG2900_INIT,
	CG2900_IDLE,
	CG2900_BOOTING,
	CG2900_CLOSING,
	CG2900_RESETING,
	CG2900_ACTIVE,
	CG2900_ACTIVE_BEFORE_SELFTEST
};

/**
 * enum boot_state - BOOT-state for CG2900 chip driver.
 * @BOOT_NOT_STARTED:			Boot has not yet started.
 * @BOOT_SEND_BD_ADDRESS:		VS Store In FS command with BD address
 *					has been sent.
 * @BOOT_GET_FILES_TO_LOAD:		CG2900 chip driver is retrieving file to
 *					load.
 * @BOOT_DOWNLOAD_PATCH:		CG2900 chip driver is downloading
 *					patches.
 * @BOOT_ACTIVATE_PATCHES_AND_SETTINGS:	CG2900 chip driver is activating patches
 *					and settings.
 * @BOOT_READ_SELFTEST_RESULT:		CG2900 is performing selftests that
 *					shall be read out.
 * @BOOT_READY:				CG2900 chip driver boot is ready.
 * @BOOT_FAILED:			CG2900 chip driver boot failed.
 */
enum boot_state {
	BOOT_NOT_STARTED,
	BOOT_SEND_BD_ADDRESS,
	BOOT_GET_FILES_TO_LOAD,
	BOOT_DOWNLOAD_PATCH,
	BOOT_ACTIVATE_PATCHES_AND_SETTINGS,
	BOOT_READ_SELFTEST_RESULT,
	BOOT_READY,
	BOOT_FAILED
};

/**
 * enum closing_state - CLOSING-state for CG2900 chip driver.
 * @CLOSING_RESET:		HCI RESET_CMD has been sent.
 * @CLOSING_POWER_SWITCH_OFF:	HCI VS_POWER_SWITCH_OFF command has been sent.
 * @CLOSING_SHUT_DOWN:		We have now shut down the chip.
 */
enum closing_state {
	CLOSING_RESET,
	CLOSING_POWER_SWITCH_OFF,
	CLOSING_SHUT_DOWN
};

/**
 * enum file_load_state - BOOT_FILE_LOAD-state for CG2900 chip driver.
 * @FILE_LOAD_GET_PATCH:		Loading patches.
 * @FILE_LOAD_GET_STATIC_SETTINGS:	Loading static settings.
 * @FILE_LOAD_NO_MORE_FILES:		No more files to load.
 * @FILE_LOAD_FAILED:			File loading failed.
 */
enum file_load_state {
	FILE_LOAD_GET_PATCH,
	FILE_LOAD_GET_STATIC_SETTINGS,
	FILE_LOAD_NO_MORE_FILES,
	FILE_LOAD_FAILED
};

/**
 * enum download_state - BOOT_DOWNLOAD state.
 * @DOWNLOAD_PENDING:	Download in progress.
 * @DOWNLOAD_SUCCESS:	Download successfully finished.
 * @DOWNLOAD_FAILED:	Downloading failed.
 */
enum download_state {
	DOWNLOAD_PENDING,
	DOWNLOAD_SUCCESS,
	DOWNLOAD_FAILED
};

/**
 * enum fm_radio_mode - FM Radio mode.
 * It's needed because some FM do-commands generate interrupts only when
 * the FM driver is in specific mode and we need to know if we should expect
 * the interrupt.
 * @FM_RADIO_MODE_IDLE:	Radio mode is Idle (default).
 * @FM_RADIO_MODE_FMT:	Radio mode is set to FMT (transmitter).
 * @FM_RADIO_MODE_FMR:	Radio mode is set to FMR (receiver).
 */
enum fm_radio_mode {
	FM_RADIO_MODE_IDLE = 0,
	FM_RADIO_MODE_FMT = 1,
	FM_RADIO_MODE_FMR = 2
};


/**
 * struct cg2900_channel_item - List object for channel.
 * @list:	list_head struct.
 * @user:	User for this channel.
 */
struct cg2900_channel_item {
	struct list_head	list;
	struct cg2900_user_data	*user;
};

/**
 * struct cg2900_delayed_work_struct - Work structure for CG2900 chip.
 * @delayed_work:	Work structure.
 * @data:		Pointer to private data.
 */
struct cg2900_delayed_work_struct {
	struct delayed_work	work;
	void			*data;
};

/**
 * struct cg2900_skb_data - Structure for storing private data in an sk_buffer.
 * @dev:	CG2900 device for this sk_buffer.
 */
struct cg2900_skb_data {
	struct cg2900_user_data *user;
};
#define cg2900_skb_data(__skb) ((struct cg2900_skb_data *)((__skb)->cb))

/**
 * struct cg2900_chip_info - Main info structure for CG2900 chip driver.
 * @dev:			Current device. Same as @chip_dev->dev.
 * @patch_file_name:		Stores patch file name.
 * @settings_file_name:		Stores settings file name.
 * @file_info:			Firmware file info (patch or settings).
 * @boot_state:			Current BOOT-state of CG2900 chip driver.
 * @closing_state:		Current CLOSING-state of CG2900 chip driver.
 * @file_load_state:		Current BOOT_FILE_LOAD-state of CG2900 chip
 *				driver.
 * @download_state:		Current BOOT_DOWNLOAD-state of CG2900 chip
 *				driver.
 * @wq:				CG2900 chip driver workqueue.
 * @chip_dev:			Chip handler info.
 * @tx_bt_lock:			Spinlock used to protect some global structures
 *				related to internal BT command flow control.
 * @tx_fm_lock:			Spinlock used to protect some global structures
 *				related to internal FM command flow control.
 * @tx_fm_audio_awaiting_irpt:	Indicates if an FM interrupt event related to
 *				audio driver command is expected.
 * @fm_radio_mode:		Current FM radio mode.
 * @tx_nr_pkts_allowed_bt:	Number of packets allowed to send on BT HCI CMD
 *				H4 channel.
 * @audio_bt_cmd_op:		Stores the OpCode of the last sent audio driver
 *				HCI BT CMD.
 * @audio_fm_cmd_id:		Stores the command id of the last sent
 *				HCI FM RADIO command by the fm audio user.
 * @hci_fm_cmd_func:		Stores the command function of the last sent
 *				HCI FM RADIO command by the fm radio user.
 * @tx_queue_bt:		TX queue for HCI BT commands when nr of commands
 *				allowed is 0 (CG2900 internal flow control).
 * @tx_queue_fm:		TX queue for HCI FM commands when nr of commands
 *				allowed is 0 (CG2900 internal flow control).
 * @user_in_charge:		User currently operating. Normally used at
 *				channel open and close.
 * @last_user:			Last user of this chip. To avoid complications
 *				this will never be set for bt_audio and
 *				fm_audio.
 * @logger:			Logger user of this chip.
 * @hci_raw:			HCI Raw user of this chip.
 * @selftest_work:		Delayed work for reading selftest results.
 * @nbr_of_polls:		Number of times we should poll for selftest
 *				results.
 * @startup:			True if system is starting up.
 * @mfd_size:			Number of MFD cells.
 * @mfd_char_size:		Number of MFD char device cells.
 * @h4_channel_for_device:	H4 channel number for sending device
 *				mangement commands.
 */
struct cg2900_chip_info {
	struct device			*dev;
	char				*patch_file_name;
	char				*settings_file_name;
	struct cg2900_file_info		file_info;
	enum main_state			main_state;
	enum boot_state			boot_state;
	enum closing_state		closing_state;
	enum file_load_state		file_load_state;
	enum download_state		download_state;
	struct workqueue_struct		*wq;
	struct cg2900_chip_dev		*chip_dev;
	spinlock_t			tx_bt_lock;
	spinlock_t			tx_fm_lock;
	spinlock_t			rw_lock;
	bool				tx_fm_audio_awaiting_irpt;
	enum fm_radio_mode		fm_radio_mode;
	int				tx_nr_pkts_allowed_bt;
	u16				audio_bt_cmd_op;
	u16				audio_fm_cmd_id;
	u16				hci_fm_cmd_func;
	struct sk_buff_head		tx_queue_bt;
	struct sk_buff_head		tx_queue_fm;
	struct list_head		open_channels;
	struct cg2900_user_data		*user_in_charge;
	struct cg2900_user_data		*last_user;
	struct cg2900_user_data		*logger;
	struct cg2900_user_data		*hci_raw;
	struct cg2900_user_data		*bt_audio;
	struct cg2900_user_data		*fm_audio;
	struct cg2900_delayed_work_struct	selftest_work;
	int				nbr_of_polls;
	bool				startup;
	int				mfd_size;
	int				mfd_extra_size;
	int				mfd_char_size;
	int				mfd_extra_char_size;
	u8				h4_channel_for_device;
};

/**
 * struct main_info - Main info structure for CG2900 chip driver.
 * @dev:			Device structure.
 * @cell_base_id:		Base ID for MFD cells.
 * @man_mutex:			Management mutex.
 */
struct main_info {
	struct device			*dev;
	int				cell_base_id;
	struct mutex			man_mutex;
};

static struct main_info *main_info;

/*
 * main_wait_queue - Main Wait Queue in CG2900 driver.
 */
static DECLARE_WAIT_QUEUE_HEAD(main_wait_queue);
/*
 * Clock enabler should be released earlier
 * before running read self test
 * as that is not required for clock enabler
 * as is the case for WLAN
 */
static DECLARE_WAIT_QUEUE_HEAD(clk_user_wait_queue);

static struct mfd_cell cg2900_devs[];
static struct mfd_cell cg2900_char_devs[];
static struct mfd_cell cg2905_extra_devs[];
static struct mfd_cell cg2905_extra_char_devs[];
static struct mfd_cell cg2910_extra_devs[];
static struct mfd_cell cg2910_extra_char_devs[];
static struct mfd_cell cg2910_extra_devs_pg2[];
static struct mfd_cell cg2910_extra_char_devs_pg2[];

static void chip_startup_finished(struct cg2900_chip_info *info, int err);
static void chip_shutdown(struct cg2900_user_data *user);

/**
 * bt_is_open() - Checks if any BT user is in open state.
 * @info:	CG2900 info.
 *
 * Returns:
 *   true if a BT channel is open.
 *   false if no BT channel is open.
 */
static bool bt_is_open(struct cg2900_chip_info *info)
{
	struct list_head *cursor;
	struct cg2900_channel_item *tmp;

	list_for_each(cursor, &info->open_channels) {
		tmp = list_entry(cursor, struct cg2900_channel_item, list);
		if (tmp->user->h4_channel == CHANNEL_BT_CMD)
			return true;
	}
	return false;
}

/**
 * fm_is_open() - Checks if any FM user is in open state.
 * @info:	CG2900 info.
 *
 * Returns:
 *   true if a FM channel is open.
 *   false if no FM channel is open.
 */
static bool fm_is_open(struct cg2900_chip_info *info)
{
	struct list_head *cursor;
	struct cg2900_channel_item *tmp;

	list_for_each(cursor, &info->open_channels) {
		tmp = list_entry(cursor, struct cg2900_channel_item, list);
		if (tmp->user->h4_channel == CHANNEL_FM_RADIO)
			return true;
	}
	return false;
}

/**
 * fm_irpt_expected() - check if this FM command will generate an interrupt.
 * @cmd_id:	command identifier.
 *
 * Returns:
 *   true if the command will generate an interrupt.
 *   false if it won't.
 */
static bool fm_irpt_expected(struct cg2900_chip_info *info, u16 cmd_id)
{
	bool retval = false;

	switch (cmd_id) {
	case CG2900_FM_DO_AIP_FADE_START:
		if (info->fm_radio_mode == FM_RADIO_MODE_FMT)
			retval = true;
		break;

	case CG2900_FM_DO_AUP_BT_FADE_START:
	case CG2900_FM_DO_AUP_EXT_FADE_START:
	case CG2900_FM_DO_AUP_FADE_START:
		if (info->fm_radio_mode == FM_RADIO_MODE_FMR)
			retval = true;
		break;

	case CG2900_FM_DO_FMR_SETANTENNA:
	case CG2900_FM_DO_FMR_SP_AFSWITCH_START:
	case CG2900_FM_DO_FMR_SP_AFUPDATE_START:
	case CG2900_FM_DO_FMR_SP_BLOCKSCAN_START:
	case CG2900_FM_DO_FMR_SP_PRESETPI_START:
	case CG2900_FM_DO_FMR_SP_SCAN_START:
	case CG2900_FM_DO_FMR_SP_SEARCH_START:
	case CG2900_FM_DO_FMR_SP_SEARCHPI_START:
	case CG2900_FM_DO_FMR_SP_TUNE_SETCHANNEL:
	case CG2900_FM_DO_FMR_SP_TUNE_STEPCHANNEL:
	case CG2900_FM_DO_FMT_PA_SETCTRL:
	case CG2900_FM_DO_FMT_PA_SETMODE:
	case CG2900_FM_DO_FMT_SP_TUNE_SETCHANNEL:
	case CG2900_FM_DO_GEN_ANTENNACHECK_START:
	case CG2900_FM_DO_GEN_GOTOMODE:
	case CG2900_FM_DO_GEN_POWERSUPPLY_SETMODE:
	case CG2900_FM_DO_GEN_SELECTREFERENCECLOCK:
	case CG2900_FM_DO_GEN_SETPROCESSINGCLOCK:
	case CG2900_FM_DO_GEN_SETREFERENCECLOCKPLL:
	case CG2900_FM_DO_TST_TX_RAMP_START:
		retval = true;
		break;

	default:
		break;
	}

	if (retval)
		dev_dbg(info->dev, "Following interrupt event expected for this"
			" Cmd complete evt: cmd_id = 0x%X\n",
			cmd_id);

	return retval;
}

/**
 * fm_is_do_cmd_irpt() - Check if irpt_val is one of the FM DO command related interrupts.
 * @irpt_val:	interrupt value.
 *
 * Returns:
 *   true if it's do-command related interrupt value.
 *   false if it's not.
 */
static bool fm_is_do_cmd_irpt(u16 irpt_val)
{
	if ((irpt_val & CG2900_FM_IRPT_OPERATION_SUCCEEDED) ||
	    (irpt_val & CG2900_FM_IRPT_OPERATION_FAILED)) {
		dev_dbg(MAIN_DEV, "Irpt evt for FM do-command found, "
			"irpt_val = 0x%X\n", irpt_val);
		return true;
	}

	return false;
}

/**
 * fm_reset_flow_ctrl - Clears up internal FM flow control.
 *
 * Resets outstanding commands and clear FM TX list and set CG2900 FM mode to
 * idle.
 */
static void fm_reset_flow_ctrl(struct cg2900_chip_info *info)
{
	dev_dbg(info->dev, "fm_reset_flow_ctrl\n");

	skb_queue_purge(&info->tx_queue_fm);

	/* Reset the fm_cmd_id. */
	info->audio_fm_cmd_id = CG2900_FM_CMD_NONE;
	info->hci_fm_cmd_func = CG2900_FM_CMD_PARAM_NONE;

	info->fm_radio_mode = FM_RADIO_MODE_IDLE;
}


/**
 * fm_parse_cmd - Parses a FM command packet.
 * @data:	FM command packet.
 * @cmd_func:	Out: FM legacy command function.
 * @cmd_id:	Out: FM legacy command ID.
 */
static void fm_parse_cmd(u8 *data, u8 *cmd_func, u16 *cmd_id)
{
	/* Move past H4-header to start of actual package */
	struct fm_leg_cmd *pkt = (struct fm_leg_cmd *)(data + HCI_H4_SIZE);

	*cmd_func = CG2900_FM_CMD_PARAM_NONE;
	*cmd_id   = CG2900_FM_CMD_NONE;

	if (pkt->opcode != CG2900_FM_GEN_ID_LEGACY) {
		dev_err(MAIN_DEV, "fm_parse_cmd: Not an FM legacy command "
			"0x%02X\n", pkt->opcode);
		return;
	}

	*cmd_func = pkt->fm_function;
	if (*cmd_func == CG2900_FM_CMD_PARAM_WRITECOMMAND)
		*cmd_id = cg2900_get_fm_cmd_id(le16_to_cpu(pkt->fm_cmd.head));
}


/**
 * fm_parse_event - Parses a FM event packet
 * @data:	FM event packet.
 * @event:	Out: FM event.
 * @cmd_func:	Out: FM legacy command function.
 * @cmd_id:	Out: FM legacy command ID.
 * @intr_val:	Out: FM interrupt value.
 */
static void fm_parse_event(u8 *data, u8 *event, u8 *cmd_func, u16 *cmd_id,
			   u16 *intr_val)
{
	/* Move past H4-header to start of actual package */
	union fm_leg_evt_or_irq *pkt = (union fm_leg_evt_or_irq *)data;

	*cmd_func = CG2900_FM_CMD_PARAM_NONE;
	*cmd_id = CG2900_FM_CMD_NONE;
	*intr_val = 0;
	*event = CG2900_FM_EVENT_UNKNOWN;

	if (pkt->evt.opcode == CG2900_FM_GEN_ID_LEGACY &&
	    pkt->evt.read_write == CG2900_FM_CMD_LEG_PARAM_WRITE) {
		/* Command complete */
		*event = CG2900_FM_EVENT_CMD_COMPLETE;
		*cmd_func = pkt->evt.fm_function;
		if (*cmd_func == CG2900_FM_CMD_PARAM_WRITECOMMAND)
			*cmd_id = cg2900_get_fm_cmd_id(
				le16_to_cpu(pkt->evt.response_head));
	} else if (pkt->irq_v2.opcode == CG2900_FM_GEN_ID_LEGACY &&
		   pkt->irq_v2.event_type == CG2900_FM_CMD_LEG_PARAM_IRQ) {
		/* Interrupt, PG2 style */
		*event = CG2900_FM_EVENT_INTERRUPT;
		*intr_val = le16_to_cpu(pkt->irq_v2.irq);
	} else if (pkt->irq_v1.opcode == CG2900_FM_GEN_ID_LEGACY) {
		/* Interrupt, PG1 style */
		*event = CG2900_FM_EVENT_INTERRUPT;
		*intr_val = le16_to_cpu(pkt->irq_v1.irq);
	} else
		dev_err(MAIN_DEV, "fm_parse_event: Not an FM legacy command "
			"0x%X %X %X %X\n", data[0], data[1], data[2], data[3]);
}

/**
 * fm_update_mode - Updates the FM mode state machine.
 * @data:	FM command packet.
 *
 * Parses a FM command packet and updates the FM mode state machine.
 */
static void fm_update_mode(struct cg2900_chip_info *info, u8 *data)
{
	u8 cmd_func;
	u16 cmd_id;

	fm_parse_cmd(data, &cmd_func, &cmd_id);

	if (cmd_func == CG2900_FM_CMD_PARAM_WRITECOMMAND &&
	    cmd_id == CG2900_FM_DO_GEN_GOTOMODE) {
		/* Move past H4-header to start of actual package */
		struct fm_leg_cmd *pkt =
			(struct fm_leg_cmd *)(data + HCI_H4_SIZE);

		info->fm_radio_mode = le16_to_cpu(pkt->fm_cmd.data[0]);
		dev_dbg(info->dev, "FM Radio mode changed to %d\n",
			info->fm_radio_mode);
	}
}


/**
 * transmit_skb_from_tx_queue_bt() - Check flow control info and transmit skb.
 *
 * The transmit_skb_from_tx_queue_bt() function checks if there are tickets
 * available and commands waiting in the TX queue and if so transmits them
 * to the controller.
 * It shall always be called within spinlock_bh.
 */
static void transmit_skb_from_tx_queue_bt(struct cg2900_chip_dev *dev)
{
	struct cg2900_user_data *user;
	struct cg2900_chip_info *info = dev->c_data;
	struct sk_buff *skb;

	dev_dbg(dev->dev, "transmit_skb_from_tx_queue_bt\n");

	/* Dequeue an skb from the head of the list */
	skb = skb_dequeue(&info->tx_queue_bt);
	while (skb) {
		if (info->tx_nr_pkts_allowed_bt <= 0) {
			/*
			 * If no more packets allowed just return, we'll get
			 * back here after next Command Complete/Status event.
			 * Put skb back at head of queue.
			 */
			skb_queue_head(&info->tx_queue_bt, skb);
			return;
		}

		(info->tx_nr_pkts_allowed_bt)--;
		dev_dbg(dev->dev, "tx_nr_pkts_allowed_bt = %d\n",
			info->tx_nr_pkts_allowed_bt);

		user = cg2900_skb_data(skb)->user; /* user is never NULL */

		/*
		 * If it's a command from audio application, store the OpCode,
		 * it'll be used later to decide where to dispatch
		 * the Command Complete event.
		 */
		if (info->bt_audio == user) {
			struct hci_command_hdr *hdr = (struct hci_command_hdr *)
				(skb->data + HCI_H4_SIZE);

			info->audio_bt_cmd_op = le16_to_cpu(hdr->opcode);
			dev_dbg(user->dev,
				"Sending cmd from audio driver, saving "
				"OpCode = 0x%04X\n", info->audio_bt_cmd_op);
		}

		cg2900_tx_to_chip(user, info->logger, skb);

		/* Dequeue an skb from the head of the list */
		skb = skb_dequeue(&info->tx_queue_bt);
	}
}

/**
 * transmit_skb_from_tx_queue_fm() - Check flow control info and transmit skb.
 *
 * The transmit_skb_from_tx_queue_fm() function checks if it possible to
 * transmit and commands waiting in the TX queue and if so transmits them
 * to the controller.
 * It shall always be called within spinlock_bh.
 */
static void transmit_skb_from_tx_queue_fm(struct cg2900_chip_dev *dev)
{
	struct cg2900_user_data *user;
	struct cg2900_chip_info *info = dev->c_data;
	struct sk_buff *skb;

	dev_dbg(dev->dev, "transmit_skb_from_tx_queue_fm\n");

	/* Dequeue an skb from the head of the list */
	skb = skb_dequeue(&info->tx_queue_fm);
	while (skb) {
		u16 cmd_id;
		u8 cmd_func;

		if (info->audio_fm_cmd_id != CG2900_FM_CMD_NONE ||
		    info->hci_fm_cmd_func != CG2900_FM_CMD_PARAM_NONE) {
			/*
			 * There are currently outstanding FM commands.
			 * Wait for them to finish. We will get back here later.
			 * Queue back the skb at head of list.
			 */
			skb_queue_head(&info->tx_queue_bt, skb);
			return;
		}

		user = cg2900_skb_data(skb)->user; /* user is never NULL */

		if (!user->opened) {
			/*
			 * Channel is not open. That means that the user that
			 * originally sent it has deregistered.
			 * Just throw it away and check the next skb in the
			 * queue.
			 */
			kfree_skb(skb);
			/* Dequeue an skb from the head of the list */
			skb = skb_dequeue(&info->tx_queue_fm);
			continue;
		}

		fm_parse_cmd(&(skb->data[0]), &cmd_func, &cmd_id);

		/*
		 * Store the FM command function , it'll be used later to decide
		 * where to dispatch the Command Complete event.
		 */
		if (info->fm_audio == user) {
			info->audio_fm_cmd_id = cmd_id;
			dev_dbg(user->dev, "Sending FM audio cmd 0x%04X\n",
				info->audio_fm_cmd_id);
		} else {
			/* FM radio command */
			info->hci_fm_cmd_func = cmd_func;
			fm_update_mode(info, &skb->data[0]);
			dev_dbg(user->dev, "Sending FM radio cmd 0x%04X\n",
				info->hci_fm_cmd_func);
		}

		/*
		 * We have only one ticket on FM. Just return after
		 * sending the skb.
		 */
		cg2900_tx_to_chip(user, info->logger, skb);
		return;
	}
}

/**
 * update_flow_ctrl_bt() - Update number of outstanding commands for BT CMD.
 * @dev:	Current chip device.
 * @skb:	skb with received packet.
 *
 * The update_flow_ctrl_bt() checks if incoming data packet is
 * BT Command Complete/Command Status Event and if so updates number of tickets
 * and number of outstanding commands. It also calls function to send queued
 * commands (if the list of queued commands is not empty).
 */
static void update_flow_ctrl_bt(struct cg2900_chip_dev *dev,
				const struct sk_buff * const skb)
{
	u8 *data = skb->data;
	struct hci_event_hdr *event;
	struct cg2900_chip_info *info = dev->c_data;

	event = (struct hci_event_hdr *)data;
	data += sizeof(*event);

	if (HCI_EV_CMD_COMPLETE == event->evt) {
		struct hci_ev_cmd_complete *complete;
		complete = (struct hci_ev_cmd_complete *)data;

		/*
		 * If it's HCI Command Complete Event then we might get some
		 * HCI tickets back. Also we can decrease the number outstanding
		 * HCI commands (if it's not NOP command or one of the commands
		 * that generate both Command Status Event and Command Complete
		 * Event).
		 * Check if we have any HCI commands waiting in the TX list and
		 * send them if there are tickets available.
		 */
		spin_lock_bh(&info->tx_bt_lock);
		info->tx_nr_pkts_allowed_bt = complete->ncmd;
		dev_dbg(dev->dev, "New tx_nr_pkts_allowed_bt = %d\n",
			info->tx_nr_pkts_allowed_bt);

		if (!skb_queue_empty(&info->tx_queue_bt))
			transmit_skb_from_tx_queue_bt(dev);
		spin_unlock_bh(&info->tx_bt_lock);
	} else if (HCI_EV_CMD_STATUS == event->evt) {
		struct hci_ev_cmd_status *status;
		status = (struct hci_ev_cmd_status *)data;

		/*
		 * If it's HCI Command Status Event then we might get some
		 * HCI tickets back. Also we can decrease the number outstanding
		 * HCI commands (if it's not NOP command).
		 * Check if we have any HCI commands waiting in the TX queue and
		 * send them if there are tickets available.
		 */
		spin_lock_bh(&info->tx_bt_lock);
		info->tx_nr_pkts_allowed_bt = status->ncmd;
		dev_dbg(dev->dev, "New tx_nr_pkts_allowed_bt = %d\n",
			info->tx_nr_pkts_allowed_bt);

		if (!skb_queue_empty(&info->tx_queue_bt))
			transmit_skb_from_tx_queue_bt(dev);
		spin_unlock_bh(&info->tx_bt_lock);
	}
}

/**
 * update_flow_ctrl_fm() - Update packets allowed for FM channel.
 * @dev:	Current chip device.
 * @skb:	skb with received packet.
 *
 * The update_flow_ctrl_fm() checks if incoming data packet is FM packet
 * indicating that the previous command has been handled and if so update
 * packets. It also calls function to send queued commands (if the list of
 * queued commands is not empty).
 */
static void update_flow_ctrl_fm(struct cg2900_chip_dev *dev,
				const struct sk_buff * const skb)
{
	u8 cmd_func = CG2900_FM_CMD_PARAM_NONE;
	u16 cmd_id = CG2900_FM_CMD_NONE;
	u16 irpt_val = 0;
	u8 event = CG2900_FM_EVENT_UNKNOWN;
	struct cg2900_chip_info *info = dev->c_data;

	fm_parse_event(&(skb->data[0]), &event, &cmd_func, &cmd_id, &irpt_val);

	if (event == CG2900_FM_EVENT_CMD_COMPLETE) {
		/* FM legacy command complete event */
		spin_lock_bh(&info->tx_fm_lock);
		/*
		 * Check if it's not an write command complete event, because
		 * then it cannot be a DO command.
		 * If it's a write command complete event check that is not a
		 * DO command complete event before setting the outstanding
		 * FM packets to none.
		 */
		if (cmd_func != CG2900_FM_CMD_PARAM_WRITECOMMAND ||
		    !fm_irpt_expected(info, cmd_id)) {
			info->hci_fm_cmd_func = CG2900_FM_CMD_PARAM_NONE;
			info->audio_fm_cmd_id = CG2900_FM_CMD_NONE;
			dev_dbg(dev->dev,
				"FM_Write: Outstanding FM commands:\n"
				"\tRadio: 0x%04X\n"
				"\tAudio: 0x%04X\n",
				info->hci_fm_cmd_func,
				info->audio_fm_cmd_id);
			transmit_skb_from_tx_queue_fm(dev);

		/*
		 * If there was a write do command complete event check if it is
		 * DO command previously sent by the FM audio user. If that's
		 * the case we need remember that in order to be able to
		 * dispatch the interrupt to the correct user.
		 */
		} else if (cmd_id == info->audio_fm_cmd_id) {
			info->tx_fm_audio_awaiting_irpt = true;
			dev_dbg(dev->dev,
				"FM Audio waiting for interrupt = true\n");
		}
		spin_unlock_bh(&info->tx_fm_lock);
	} else if (event == CG2900_FM_EVENT_INTERRUPT) {
		/* FM legacy interrupt */
		if (fm_is_do_cmd_irpt(irpt_val)) {
			/*
			 * If it is an interrupt related to a DO command update
			 * the outstanding flow control and transmit blocked
			 * FM commands.
			 */
			spin_lock_bh(&info->tx_fm_lock);
			info->hci_fm_cmd_func = CG2900_FM_CMD_PARAM_NONE;
			info->audio_fm_cmd_id = CG2900_FM_CMD_NONE;
			dev_dbg(dev->dev,
				"FM_INT: Outstanding FM commands:\n"
				"\tRadio: 0x%04X\n"
				"\tAudio: 0x%04X\n",
				info->hci_fm_cmd_func,
				info->audio_fm_cmd_id);
			info->tx_fm_audio_awaiting_irpt = false;
			dev_dbg(dev->dev,
				"FM Audio waiting for interrupt = false\n");
			transmit_skb_from_tx_queue_fm(dev);
			spin_unlock_bh(&info->tx_fm_lock);
		}
	}
}

/**
 * send_bd_address() - Send HCI VS command with BD address to the chip.
 */
static void send_bd_address(struct cg2900_chip_info *info)
{
	struct bt_vs_store_in_fs_cmd *cmd;
	u8 plen = sizeof(*cmd) + BT_BDADDR_SIZE;

	cmd = kmalloc(plen, GFP_KERNEL);
	if (!cmd) {
		dev_err(info->dev, "send_bd_address could not allocate cmd\n");
		return;
	}

	cmd->opcode = cpu_to_le16(CG2900_BT_OP_VS_STORE_IN_FS);
	cmd->plen = BT_PARAM_LEN(plen);
	cmd->user_id = CG2900_VS_STORE_IN_FS_USR_ID_BD_ADDR;
	cmd->len = BT_BDADDR_SIZE;
	/* Now copy the BD address received from user space control app. */
	memcpy(cmd->data, bd_address, BT_BDADDR_SIZE);

	dev_dbg(BOOT_DEV, "New boot_state: BOOT_SEND_BD_ADDRESS\n");
	info->boot_state = BOOT_SEND_BD_ADDRESS;

	cg2900_send_bt_cmd(info->user_in_charge, info->logger,
			cmd, plen, info->h4_channel_for_device);

	kfree(cmd);
}

/**
 * send_settings_file() - Transmit settings file.
 *
 * The send_settings_file() function transmit settings file.
 * The file is read in parts to fit in HCI packets. When finished,
 * close the settings file and send HCI reset to activate settings and patches.
 */
static void send_settings_file(struct cg2900_chip_info *info)
{
	int bytes_sent;

	bytes_sent = cg2900_read_and_send_file_part(
			info->user_in_charge,
			info->logger,
			&info->file_info,
			info->file_info.fw_file_ssf,
			info->h4_channel_for_device);

	if (bytes_sent > 0) {
		/* Data sent. Wait for CmdComplete */
		return;
	} else if (bytes_sent < 0) {
		dev_err(BOOT_DEV, "send_settings_file: Error %d occurred\n",
			bytes_sent);
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
		info->boot_state = BOOT_FAILED;
		chip_startup_finished(info, bytes_sent);
		return;
	}

	/* No data was sent. This file is finished */
	info->download_state = DOWNLOAD_SUCCESS;

	/* Settings file finished. Release used resources */
	dev_dbg(BOOT_DEV, "Settings file finished\n");

	dev_dbg(BOOT_DEV, "New file_load_state: FILE_LOAD_NO_MORE_FILES\n");
	info->file_load_state = FILE_LOAD_NO_MORE_FILES;

	/* Create and send HCI VS Store In FS command with bd address. */
	send_bd_address(info);
}

/**
 * send_patch_file - Transmit patch file.
 *
 * The send_patch_file() function transmit patch file.
 * The file is read in parts to fit in HCI packets. When the complete file is
 * transmitted, the file is closed.
 * When finished, continue with settings file.
 */
static void send_patch_file(struct cg2900_chip_dev *dev)
{
	int err;
	int bytes_sent;
	struct cg2900_chip_info *info = dev->c_data;

	bytes_sent = cg2900_read_and_send_file_part(
			info->user_in_charge,
			info->logger,
			&info->file_info,
			info->file_info.fw_file_ptc,
			info->h4_channel_for_device);

	if (bytes_sent > 0) {
		/* Data sent. Wait for CmdComplete */
		return;
	} else if (bytes_sent < 0) {
		dev_err(BOOT_DEV, "send_patch_file: Error %d occurred\n",
			bytes_sent);
		err = bytes_sent;
		goto error_handling;
	}

	/* No data was sent. This file is finished */
	info->download_state = DOWNLOAD_SUCCESS;

	dev_dbg(BOOT_DEV, "Patch file finished\n");

	/* Now send the settings file */
	dev_dbg(BOOT_DEV,
		"New file_load_state: FILE_LOAD_GET_STATIC_SETTINGS\n");
	info->file_load_state = FILE_LOAD_GET_STATIC_SETTINGS;
	dev_dbg(BOOT_DEV, "New download_state: DOWNLOAD_PENDING\n");
	info->download_state = DOWNLOAD_PENDING;
	send_settings_file(info);
	return;

error_handling:
	dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
	info->boot_state = BOOT_FAILED;
	chip_startup_finished(info, err);
}

/**
 * work_power_off_chip() - Work item to power off the chip.
 * @work:	Reference to work data.
 *
 * The work_power_off_chip() function handles transmission of the HCI command
 * vs_power_switch_off and then informs the CG2900 Core that this chip driver is
 * finished and the Core driver can now shut off the chip.
 */
static void work_power_off_chip(struct work_struct *work)
{
	struct sk_buff *skb = NULL;
	u8 *h4_header;
	struct cg2900_platform_data *pf_data;
	struct cg2900_work *my_work;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	if (!work) {
		dev_err(MAIN_DEV, "work_power_off_chip: work == NULL\n");
		return;
	}

	my_work = container_of(work, struct cg2900_work, work);
	dev = my_work->user_data;
	info = dev->c_data;

	/*
	 * Get the VS Power Switch Off command to use based on connected
	 * connectivity controller
	 */
	pf_data = dev_get_platdata(dev->dev);
	if (pf_data->get_power_switch_off_cmd)
		skb = pf_data->get_power_switch_off_cmd(dev, NULL);

	/*
	 * Transmit the received command.
	 * If no command found for the device, just continue
	 */
	if (!skb) {
		dev_err(dev->dev,
			"Could not retrieve PowerSwitchOff command\n");
		goto shut_down_chip;
	}

	dev_dbg(dev->dev,
		"Got power_switch_off command. Add H4 header and transmit\n");

	/*
	 * Move the data pointer to the H:4 header position and store
	 * the H4 header
	 */
	h4_header = skb_push(skb, CG2900_SKB_RESERVE);
	*h4_header = info->h4_channel_for_device;

	dev_dbg(dev->dev, "New closing_state: CLOSING_POWER_SWITCH_OFF\n");
	info->closing_state = CLOSING_POWER_SWITCH_OFF;

	if (info->user_in_charge)
		cg2900_tx_to_chip(info->user_in_charge, info->logger, skb);
	else
		cg2900_tx_no_user(dev, skb);

	/*
	 * Mandatory to wait 500ms after the power_switch_off command has been
	 * transmitted, in order to make sure that the controller is ready.
	 */
	schedule_timeout_killable(msecs_to_jiffies(POWER_SW_OFF_WAIT));

shut_down_chip:
	dev_dbg(dev->dev, "New closing_state: CLOSING_SHUT_DOWN\n");
	info->closing_state = CLOSING_SHUT_DOWN;

	/* Close the transport, which will power off the chip */
	if (dev->t_cb.close)
		dev->t_cb.close(dev);

	/* Chip shut-down finished, set correct state and wake up the chip. */
	dev_dbg(dev->dev, "New main_state: CG2900_IDLE\n");
	info->main_state = CG2900_IDLE;
	wake_up_all(&main_wait_queue);

	/* If this is called during system startup, register the devices. */
	if (info->startup) {
		int err;

		err = mfd_add_devices(dev->dev, main_info->cell_base_id,
				cg2900_devs, info->mfd_size, NULL, 0);
		if (err) {
			dev_err(dev->dev, "Failed to add cg2900_devs (%d)\n",
					err);
			goto finished;
		}

		err = mfd_add_devices(dev->dev, main_info->cell_base_id,
				cg2900_char_devs, info->mfd_char_size,
				NULL, 0);
		if (err) {
			dev_err(dev->dev, "Failed to add cg2900_char_devs (%d)"
					"\n", err);
			mfd_remove_devices(dev->dev);
			goto finished;
		}

		/*
		 * Increase base ID so next connected transport will not get the
		 * same device IDs.
		 */
		main_info->cell_base_id +=
				MAX(info->mfd_size, info->mfd_char_size);

		if (dev->chip.hci_revision == CG2905_PG2_REV) {
			err = mfd_add_devices(dev->dev, main_info->cell_base_id,
					cg2905_extra_devs,
					info->mfd_extra_size, NULL, 0);
			if (err) {
				dev_err(dev->dev, "Failed to add cg2905_extra_devs "
						"(%d)\n", err);
				goto finished;
			}

			err = mfd_add_devices(dev->dev, main_info->cell_base_id,
					cg2905_extra_char_devs,
					info->mfd_extra_char_size, NULL, 0);
			if (err) {
				dev_err(dev->dev, "Failed to add cg2905_extra_char_devs "
						"(%d)\n", err);
				mfd_remove_devices(dev->dev);
				goto finished;
			}

			/*
			 * Increase base ID so next connected transport
			 * will not get the same device IDs.
			 */
			main_info->cell_base_id +=
					MAX(info->mfd_extra_size,
						info->mfd_extra_char_size);
		} else if (dev->chip.hci_revision == CG2910_PG1_REV ||
				dev->chip.hci_revision == CG2910_PG1_05_REV) {
			err = mfd_add_devices(dev->dev, main_info->cell_base_id,
					cg2910_extra_devs,
					info->mfd_extra_size, NULL, 0);
			if (err) {
				dev_err(dev->dev, "Failed to add cg2910_extra_devs "
						"(%d)\n", err);
				goto finished;
			}

			err = mfd_add_devices(dev->dev, main_info->cell_base_id,
					cg2910_extra_char_devs,
					info->mfd_extra_char_size, NULL, 0);
			if (err) {
				dev_err(dev->dev, "Failed to add cg2910_extra_char_devs "
						"(%d)\n", err);
				mfd_remove_devices(dev->dev);
				goto finished;
			}

			/*
			 * Increase base ID so next connected transport
			 * will not get the same device IDs.
			 */
			main_info->cell_base_id +=
					MAX(info->mfd_extra_size,
						info->mfd_extra_char_size);
		} else if (dev->chip.hci_revision == CG2910_PG2_REV) {

			err = mfd_add_devices(dev->dev, main_info->cell_base_id,
					cg2910_extra_devs_pg2,
					info->mfd_extra_size, NULL, 0);
			if (err) {
				dev_err(dev->dev, "Failed to add cg2910_extra_devs_pg2 "
						"(%d)\n", err);
				goto finished;
			}

			err = mfd_add_devices(dev->dev, main_info->cell_base_id,
					cg2910_extra_char_devs_pg2,
					info->mfd_extra_char_size, NULL, 0);
			if (err) {
				dev_err(dev->dev, "Failed to add cg2910_extra_char_devs_pg2 "
						"(%d)\n", err);
				mfd_remove_devices(dev->dev);
				goto finished;
			}

			/*
			 * Increase base ID so next connected transport
			 * will not get the same device IDs.
			 */
			main_info->cell_base_id +=
					MAX(info->mfd_extra_size,
						info->mfd_extra_char_size);
		}

		info->startup = false;
	}

finished:
	kfree(my_work);
}

/**
 * work_chip_shutdown() - Shut down the chip.
 * @work:	Reference to work data.
 */
static void work_chip_shutdown(struct work_struct *work)
{
	struct cg2900_work *my_work;
	struct cg2900_user_data *user;

	if (!work) {
		dev_err(MAIN_DEV, "work_chip_shutdown: work == NULL\n");
		return;
	}

	my_work = container_of(work, struct cg2900_work, work);
	user = my_work->user_data;

	chip_shutdown(user);

	kfree(my_work);
}

/**
 * work_reset_after_error() - Handle reset.
 * @work:	Reference to work data.
 *
 * Handle a reset after received Command Complete event.
 */
static void work_reset_after_error(struct work_struct *work)
{
	struct cg2900_work *my_work;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	if (!work) {
		dev_err(MAIN_DEV, "work_reset_after_error: work == NULL\n");
		return;
	}

	my_work = container_of(work, struct cg2900_work, work);
	dev = my_work->user_data;
	info = dev->c_data;

	chip_startup_finished(info, -EIO);

	kfree(my_work);
}

/**
 * work_load_patch_and_settings() - Start loading patches and settings.
 * @work:	Reference to work data.
 */
static void work_load_patch_and_settings(struct work_struct *work)
{
	struct cg2900_work *my_work;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	if (!work) {
		dev_err(MAIN_DEV,
			"work_load_patch_and_settings: work == NULL\n");
		return;
	}

	my_work = container_of(work, struct cg2900_work, work);
	dev = my_work->user_data;
	info = dev->c_data;

	/* Check that we are in the right state */
	if (info->boot_state != BOOT_GET_FILES_TO_LOAD)
		goto finished;

	/* We now all info needed */
	dev_dbg(BOOT_DEV, "New boot_state: BOOT_DOWNLOAD_PATCH\n");
	info->boot_state = BOOT_DOWNLOAD_PATCH;
	dev_dbg(BOOT_DEV, "New download_state: DOWNLOAD_PENDING\n");
	info->download_state = DOWNLOAD_PENDING;
	dev_dbg(BOOT_DEV, "New file_load_state: FILE_LOAD_GET_PATCH\n");
	info->file_load_state = FILE_LOAD_GET_PATCH;
	info->file_info.chunk_id = 0;
	info->file_info.file_offset = 0;

	send_patch_file(dev);

	goto finished;

finished:
	kfree(my_work);
}

/**
 * work_cont_file_download() - A file block has been written.
 * @work:	Reference to work data.
 *
 * Handle a received HCI VS Write File Block Complete event.
 * Normally this means continue to send files to the controller.
 */
static void work_cont_file_download(struct work_struct *work)
{
	struct cg2900_work *my_work;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	if (!work) {
		dev_err(MAIN_DEV, "work_cont_file_download: work == NULL\n");
		return;
	}

	my_work = container_of(work, struct cg2900_work, work);
	dev = my_work->user_data;
	info = dev->c_data;

	/* Continue to send patches or settings to the controller */
	if (info->file_load_state == FILE_LOAD_GET_PATCH)
		send_patch_file(dev);
	else if (info->file_load_state == FILE_LOAD_GET_STATIC_SETTINGS)
		send_settings_file(info);
	else
		dev_dbg(BOOT_DEV, "No more files to load\n");

	kfree(my_work);
}

/**
 * work_send_read_selftest_cmd() - HCI VS Read_SelfTests_Result command shall be sent.
 * @work:	Reference to work data.
 */
static void work_send_read_selftest_cmd(struct work_struct *work)
{
	struct delayed_work *del_work;
	struct cg2900_delayed_work_struct *current_work;
	struct cg2900_chip_info *info;
	struct hci_command_hdr cmd;

	if (!work) {
		dev_err(MAIN_DEV,
			"work_send_read_selftest_cmd: work == NULL\n");
		return;
	}

	del_work = to_delayed_work(work);
	current_work = container_of(del_work,
				    struct cg2900_delayed_work_struct, work);
	info = current_work->data;

	if (info->boot_state != BOOT_READ_SELFTEST_RESULT)
		return;

	cmd.opcode = cpu_to_le16(CG2900_BT_OP_VS_READ_SELTESTS_RESULT);
	cmd.plen = 0; /* No parameters for Read Selftests Result */
	cg2900_send_bt_cmd(info->user_in_charge, info->logger,
			&cmd, sizeof(cmd), info->h4_channel_for_device);
}

/**
 * handle_reset_cmd_complete() - Handles HCI Reset Command Complete event.
 * @data:	Pointer to received HCI data packet.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_reset_cmd_complete(struct cg2900_chip_dev *dev, u8 *data)
{
	u8 status = data[0];
	struct cg2900_chip_info *info = dev->c_data;

	dev_dbg(BOOT_DEV, "Received Reset complete event with status 0x%X\n",
		status);

	if (CG2900_CLOSING != info->main_state &&
	    CLOSING_RESET != info->closing_state)
		return false;

	if (HCI_BT_ERROR_NO_ERROR != status) {
		/*
		 * Continue in case of error, the chip is going to be shut down
		 * anyway.
		 */
		dev_err(BOOT_DEV, "Command complete for HciReset received with "
			"error 0x%X\n", status);
	}

	cg2900_create_work_item(info->wq, work_power_off_chip, dev);

	return true;
}

/**
 * handle_vs_store_in_fs_cmd_complete() - Handles HCI VS StoreInFS Command Complete event.
 * @data:	Pointer to received HCI data packet.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_vs_store_in_fs_cmd_complete(struct cg2900_chip_dev *dev,
					       u8 *data)
{
	u8 status = data[0];
	struct cg2900_chip_info *info = dev->c_data;

	dev_dbg(BOOT_DEV,
		"Received Store_in_FS complete event with status 0x%X\n",
		status);

	if (info->boot_state != BOOT_SEND_BD_ADDRESS)
		return false;

	if (HCI_BT_ERROR_NO_ERROR == status) {
		struct hci_command_hdr cmd;

		/* Send HCI SystemReset command to activate patches */
		dev_dbg(BOOT_DEV,
			"New boot_state: BOOT_ACTIVATE_PATCHES_AND_SETTINGS\n");
		info->boot_state = BOOT_ACTIVATE_PATCHES_AND_SETTINGS;

		cmd.opcode = cpu_to_le16(CG2900_BT_OP_VS_SYSTEM_RESET);
		cmd.plen = 0; /* No parameters for System Reset */
		cg2900_send_bt_cmd(info->user_in_charge, info->logger,
				&cmd, sizeof(cmd), info->h4_channel_for_device);
	} else {
		dev_err(BOOT_DEV,
			"Command complete for StoreInFS received with error "
			"0x%X\n", status);
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
		info->boot_state = BOOT_FAILED;
		cg2900_create_work_item(info->wq, work_reset_after_error, dev);
	}

	return true;
}

/**
 * handle_vs_write_file_block_cmd_complete() - Handles HCI VS WriteFileBlock Command Complete event.
 * @data:	Pointer to received HCI data packet.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_vs_write_file_block_cmd_complete(struct cg2900_chip_dev *dev,
						    u8 *data)
{
	u8 status = data[0];
	struct cg2900_chip_info *info = dev->c_data;

	if (info->boot_state != BOOT_DOWNLOAD_PATCH ||
	    info->download_state != DOWNLOAD_PENDING)
		return false;

	if (HCI_BT_WRONG_SEQ_ERROR == status && info->file_info.chunk_id == 1 &&
			(CG2905_PG1_05_REV == dev->chip.hci_revision ||
			CG2910_PG1_REV == dev->chip.hci_revision ||
			CG2910_PG1_05_REV == dev->chip.hci_revision)) {
		/*
		 * Because of bug in CG2905/CG2910 PG1 H/W, the first chunk
		 * will return an error of wrong sequence number. As a
		 * workaround the first chunk needs to be sent again.
		 */
		info->file_info.chunk_id = 0;
		info->file_info.file_offset = 0;
		/*
		 * Set the status back to success so that it continues on the
		 * success path rather than failure.
		 */
		status = HCI_BT_ERROR_NO_ERROR;
	}

	if (HCI_BT_ERROR_NO_ERROR == status)
		cg2900_create_work_item(info->wq, work_cont_file_download, dev);
	else {
		dev_err(BOOT_DEV,
			"Command complete for WriteFileBlock received with"
			" error 0x%X\n", status);
		dev_dbg(BOOT_DEV, "New download_state: DOWNLOAD_FAILED\n");
		info->download_state = DOWNLOAD_FAILED;
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
		info->boot_state = BOOT_FAILED;
		cg2900_create_work_item(info->wq, work_reset_after_error, dev);
	}

	return true;
}

/**
 * handle_vs_write_file_block_cmd_status() - Handles HCI VS WriteFileBlock Command Status event.
 * @status:	Returned status of WriteFileBlock command.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_vs_write_file_block_cmd_status(struct cg2900_chip_dev *dev,
						  u8 status)
{
	struct cg2900_chip_info *info = dev->c_data;

	if (info->boot_state != BOOT_DOWNLOAD_PATCH ||
	    info->download_state != DOWNLOAD_PENDING)
		return false;

	/*
	 * Only do something if there is an error. Otherwise we will wait for
	 * CmdComplete.
	 */
	if (HCI_BT_ERROR_NO_ERROR != status) {
		dev_err(BOOT_DEV,
			"Command status for WriteFileBlock received with"
			" error 0x%X\n", status);
		dev_dbg(BOOT_DEV, "New download_state: DOWNLOAD_FAILED\n");
		info->download_state = DOWNLOAD_FAILED;
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
		info->boot_state = BOOT_FAILED;
		cg2900_create_work_item(info->wq, work_reset_after_error, dev);
	}

	return true;
}

/**
 * handle_vs_power_switch_off_cmd_complete() - Handles HCI VS PowerSwitchOff Command Complete event.
 * @data:	Pointer to received HCI data packet.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_vs_power_switch_off_cmd_complete(struct cg2900_chip_dev *dev,
						    u8 *data)
{
	u8 status = data[0];
	struct cg2900_chip_info *info = dev->c_data;

	if (CLOSING_POWER_SWITCH_OFF != info->closing_state)
		return false;

	dev_dbg(BOOT_DEV,
		"handle_vs_power_switch_off_cmd_complete status %d\n", status);

	/*
	 * We were waiting for this but we don't need to do anything upon
	 * reception except warn for error status
	 */
	if (HCI_BT_ERROR_NO_ERROR != status)
		dev_err(BOOT_DEV,
			"Command Complete for PowerSwitchOff received with "
			"error 0x%X", status);

	return true;
}

/**
 * handle_vs_system_reset_cmd_complete() - Handle HCI VS SystemReset Command Complete event.
 * @data:	Pointer to received HCI data packet.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_vs_system_reset_cmd_complete(struct cg2900_chip_dev *dev,
						u8 *data)
{
	u8 status = data[0];
	struct cg2900_chip_info *info = dev->c_data;

	if (info->boot_state != BOOT_ACTIVATE_PATCHES_AND_SETTINGS)
		return false;

	dev_dbg(BOOT_DEV, "handle_vs_system_reset_cmd_complete status %d\n",
		status);

	if (HCI_BT_ERROR_NO_ERROR == status) {
		if (dev->chip.hci_revision == CG2900_PG2_REV) {
			/*
			 * We must now wait for the selftest results. They will
			 * take a certain amount of time to finish so start a
			 * delayed work that will then send the command.
			 */
			dev_dbg(BOOT_DEV,
				"New boot_state: BOOT_READ_SELFTEST_RESULT\n");
			info->boot_state = BOOT_READ_SELFTEST_RESULT;
			queue_delayed_work(info->wq, &info->selftest_work.work,
					   msecs_to_jiffies(SELFTEST_INITIAL));
			info->nbr_of_polls = 0;
		} else {
			/*
			 * We are now almost finished. Shut off BT Core. It will
			 * be re-enabled by the Bluetooth driver when needed.
			 */
			dev_dbg(BOOT_DEV, "New boot_state: BOOT_READY\n");
			info->boot_state = BOOT_READY;
			chip_startup_finished(info, 0);
		}
	} else {
		dev_err(BOOT_DEV,
			"Received Reset complete event with status 0x%X\n",
			status);
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
		info->boot_state = BOOT_FAILED;
		chip_startup_finished(info, -EIO);
	}

	return true;
}

/**
 * handle_vs_read_selftests_cmd_complete() - Handle HCI VS ReadSelfTestsResult Command Complete event.
 * @dev:	Current chip.
 * @data:	Pointer to received HCI data packet.
 *
 * Returns:
 *   true,  if packet was handled internally,
 *   false, otherwise.
 */
static bool handle_vs_read_selftests_cmd_complete(struct cg2900_chip_dev *dev,
						  u8 *data)
{
	struct bt_vs_read_selftests_result_evt *evt =
			(struct bt_vs_read_selftests_result_evt *)data;
	struct cg2900_chip_info *info = dev->c_data;

	if (info->boot_state != BOOT_READ_SELFTEST_RESULT)
		return false;

	dev_dbg(BOOT_DEV,
		"handle_vs_read_selftests_cmd_complete status %d result %d\n",
		evt->status, evt->result);

	if (HCI_BT_ERROR_NO_ERROR != evt->status)
		goto err_handling;

	if (CG2900_BT_SELFTEST_SUCCESSFUL == evt->result ||
	    CG2900_BT_SELFTEST_FAILED == evt->result) {
		if (CG2900_BT_SELFTEST_FAILED == evt->result)
			dev_err(BOOT_DEV, "CG2900 self test failed\n");

		/*
		 * We are now almost finished. Shut off BT Core. It will
		 * be re-enabled by the Bluetooth driver when needed.
		 */
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_READY\n");
		info->boot_state = BOOT_READY;
		chip_startup_finished(info, 0);
		return true;
	} else if (CG2900_BT_SELFTEST_NOT_COMPLETED == evt->result) {
		/*
		 * Self tests are not yet finished. Wait some more time
		 * before resending the command
		 */
		if (info->nbr_of_polls > MAX_NBR_OF_POLLS) {
			dev_err(BOOT_DEV, "Selftest results reached max"
				" number of polls\n");
			goto err_handling;
		}
		queue_delayed_work(info->wq, &info->selftest_work.work,
				   msecs_to_jiffies(SELFTEST_POLLING));
		info->nbr_of_polls++;
		return true;
	}

err_handling:
	dev_err(BOOT_DEV,
		"Received Read SelfTests Result complete event with "
		"status 0x%X and result 0x%X\n",
		evt->status, evt->result);
	dev_dbg(BOOT_DEV, "New boot_state: BOOT_FAILED\n");
	info->boot_state = BOOT_FAILED;
	chip_startup_finished(info, -EIO);
	return true;
}

/**
 * handle_rx_data_bt_evt() - Check if received data should be handled in CG2900 chip driver.
 * @skb:	Data packet
 *
 * The handle_rx_data_bt_evt() function checks if received data should be
 * handled in CG2900 chip driver. If so handle it correctly.
 * Received data is always HCI BT Event.
 *
 * Returns:
 *   True,  if packet was handled internally,
 *   False, otherwise.
 */
static bool handle_rx_data_bt_evt(struct cg2900_chip_dev *dev,
				  struct sk_buff *skb, int h4_channel)
{
	bool pkt_handled = false;
	/* skb cannot be NULL here so it is safe to de-reference */
	u8 *data = skb->data;
	struct hci_event_hdr *evt;
	u16 op_code;
	bool use_dev = use_device_channel_for_vs_cmd(dev->chip.hci_revision);
	evt = (struct hci_event_hdr *)data;
	data += sizeof(*evt);

	/* First check the event code. */
	if (HCI_EV_CMD_COMPLETE == evt->evt) {
		struct hci_ev_cmd_complete *cmd_complete;

		cmd_complete = (struct hci_ev_cmd_complete *)data;
		op_code = le16_to_cpu(cmd_complete->opcode);

		dev_dbg(dev->dev,
			"Received Command Complete: op_code = 0x%04X\n",
			op_code);
		/* Move to first byte after OCF */
		data += sizeof(*cmd_complete);

		if (op_code == HCI_OP_RESET)
			pkt_handled = handle_reset_cmd_complete(dev, data);
		else {
			if (use_dev) {
				if (h4_channel != CHANNEL_DEV_MGMT)
					return false;
			} else if (h4_channel != CHANNEL_BT_EVT)
				return false;

			if (op_code == CG2900_BT_OP_VS_STORE_IN_FS)
				pkt_handled =
					handle_vs_store_in_fs_cmd_complete(
						dev, data);
			else if (op_code == CG2900_BT_OP_VS_WRITE_FILE_BLOCK)
				pkt_handled =
					handle_vs_write_file_block_cmd_complete(
								dev, data);
			else if (op_code == CG2900_BT_OP_VS_POWER_SWITCH_OFF)
				pkt_handled =
					handle_vs_power_switch_off_cmd_complete(
								dev, data);
			else if (op_code == CG2900_BT_OP_VS_SYSTEM_RESET) {
				if (dev->chip.hci_revision == CG2900_PG2_REV) {
					struct cg2900_chip_info *info =
							dev->c_data;
					/*
					 * Don't wait till READ_SELFTESTS_RESULT
					 * is complete for clock users
					 */
					dev_dbg(dev->dev,
							"New main_state: CG2900_ACTIVE_BEFORE_SELFTEST\n");
					info->main_state =
						CG2900_ACTIVE_BEFORE_SELFTEST;
					wake_up_all(&clk_user_wait_queue);
				}

				pkt_handled =
					handle_vs_system_reset_cmd_complete(
								dev, data);
			} else if (op_code ==
					CG2900_BT_OP_VS_READ_SELTESTS_RESULT)
				pkt_handled =
					handle_vs_read_selftests_cmd_complete(
								dev, data);
		}
	} else if (HCI_EV_CMD_STATUS == evt->evt) {
		struct hci_ev_cmd_status *cmd_status;

		if (use_dev) {
			if (h4_channel != CHANNEL_DEV_MGMT)
				return false;
		} else if (h4_channel != CHANNEL_BT_EVT)
			return false;

		cmd_status = (struct hci_ev_cmd_status *)data;

		op_code = le16_to_cpu(cmd_status->opcode);

		dev_dbg(dev->dev, "Received Command Status: op_code = 0x%04X\n",
			op_code);

		if (op_code == CG2900_BT_OP_VS_WRITE_FILE_BLOCK)
			pkt_handled = handle_vs_write_file_block_cmd_status
				(dev, cmd_status->status);
	} else if (HCI_EV_VENDOR_SPECIFIC == evt->evt) {
		/*
		 * In the new versions of CG29xx i.e. after CG2900 we
		 * will recieve HCI_Event_VS_Write_File_Block_Complete
		 * instead of Command Complete while downloading
		 * the patches/settings file
		 */
		struct bt_vs_evt *cmd_evt;
		if (use_dev) {
			if (h4_channel != CHANNEL_DEV_MGMT)
				return false;
		} else if (h4_channel != CHANNEL_BT_EVT)
			return false;

		cmd_evt = (struct bt_vs_evt *)data;

		if (cmd_evt->evt_id == CG2900_EV_VS_WRITE_FILE_BLOCK_COMPLETE) {
			struct bt_vs_write_file_block_evt *write_block_evt;
			write_block_evt =
				(struct bt_vs_write_file_block_evt *)
				cmd_evt->data;
			dev_dbg(dev->dev, "Received VS write file block complete\n");
			pkt_handled = handle_vs_write_file_block_cmd_complete(
				dev, &write_block_evt->status);
		} else {
			dev_err(dev->dev, "Vendor Specific Event 0x%x"
					"Not Supported\n", cmd_evt->evt_id);
			return false;
		}
	} else if (HCI_EV_HW_ERROR == evt->evt) {
		struct hci_ev_hw_error *hw_error;

		hw_error = (struct hci_ev_hw_error *)data;
		/*
		 * Only do a printout. There might be a receiving stack that can
		 * handle this event
		 */
		dev_err(dev->dev, "HW Error event received with error 0x%02X\n",
			hw_error->hw_code);
		return false;
	} else
		return false;

	if (pkt_handled)
		kfree_skb(skb);

	return pkt_handled;
}

/**
 * transmit_skb_with_flow_ctrl_bt() - Send the BT skb to the controller if it is allowed or queue it.
 * @user:	Current user.
 * @skb:	Data packet.
 *
 * The transmit_skb_with_flow_ctrl_bt() function checks if there are
 * tickets available and if so transmits buffer to controller. Otherwise the skb
 * and user name is stored in a list for later sending.
 * If enabled, copy the transmitted data to the HCI logger as well.
 */
static void transmit_skb_with_flow_ctrl_bt(struct cg2900_user_data *user,
					   struct sk_buff *skb)
{
	struct cg2900_chip_dev *dev = cg2900_get_prv(user);
	struct cg2900_chip_info *info = dev->c_data;

	/*
	 * Because there are more users of some H4 channels (currently audio
	 * application for BT command and FM channel) we need to have an
	 * internal HCI command flow control in CG2900 driver.
	 * So check here how many tickets we have and store skb in a queue if
	 * there are no tickets left. The skb will be sent later when we get
	 * more ticket(s).
	 */
	spin_lock_bh(&info->tx_bt_lock);

	if (info->tx_nr_pkts_allowed_bt > 0) {
		info->tx_nr_pkts_allowed_bt--;
		dev_dbg(user->dev, "New tx_nr_pkts_allowed_bt = %d\n",
			info->tx_nr_pkts_allowed_bt);

		/*
		 * If it's command from audio app store the OpCode,
		 * it'll be used later to decide where to dispatch Command
		 * Complete event.
		 */
		if (info->bt_audio == user) {
			struct hci_command_hdr *hdr = (struct hci_command_hdr *)
				(skb->data + HCI_H4_SIZE);

			info->audio_bt_cmd_op = le16_to_cpu(hdr->opcode);

			dev_dbg(user->dev,
				"Sending cmd from audio driver, saving "
				"OpCode = 0x%X\n",
				info->audio_bt_cmd_op);
		}

		cg2900_tx_to_chip(user, info->logger, skb);
	} else {
		dev_dbg(user->dev, "Not allowed to send cmd to controller, "
			"storing in TX queue\n");

		cg2900_skb_data(skb)->user = user;
		skb_queue_tail(&info->tx_queue_bt, skb);
	}
	spin_unlock_bh(&info->tx_bt_lock);
}

/**
 * transmit_skb_with_flow_ctrl_fm() - Send the FM skb to the controller if it is allowed or queue it.
 * @user:	Current user.
 * @skb:	Data packet.
 *
 * The transmit_skb_with_flow_ctrl_fm() function checks if chip is available and
 * if so transmits buffer to controller. Otherwise the skb and user name is
 * stored in a list for later sending.
 * Also it updates the FM radio mode if it's FM GOTOMODE command, this is needed
 * to know how to handle some FM DO commands complete events.
 * If enabled, copy the transmitted data to the HCI logger as well.
 */
static void transmit_skb_with_flow_ctrl_fm(struct cg2900_user_data *user,
					   struct sk_buff *skb)
{
	u8 cmd_func = CG2900_FM_CMD_PARAM_NONE;
	u16 cmd_id = CG2900_FM_CMD_NONE;
	struct cg2900_chip_dev *dev = cg2900_get_prv(user);
	struct cg2900_chip_info *info = dev->c_data;

	fm_parse_cmd(&(skb->data[0]), &cmd_func, &cmd_id);

	/*
	 * If this is an FM IP disable or reset send command and also reset
	 * the flow control and audio user.
	 */
	if (cmd_func == CG2900_FM_CMD_PARAM_DISABLE ||
	    cmd_func == CG2900_FM_CMD_PARAM_RESET) {
		spin_lock_bh(&info->tx_fm_lock);
		fm_reset_flow_ctrl(info);
		spin_unlock_bh(&info->tx_fm_lock);
		cg2900_tx_to_chip(user, info->logger, skb);
		return;
	}

	/*
	 * If this is a FM user and no FM audio user command pending just send
	 * FM command. It is up to the user of the FM channel to handle its own
	 * flow control.
	 */
	spin_lock_bh(&info->tx_fm_lock);
	if (info->fm_audio != user &&
	    info->audio_fm_cmd_id == CG2900_FM_CMD_NONE) {
		info->hci_fm_cmd_func = cmd_func;
		dev_dbg(user->dev, "Sending FM radio command 0x%04X\n",
			info->hci_fm_cmd_func);
		/* If a GotoMode command update FM mode */
		fm_update_mode(info, &skb->data[0]);
		cg2900_tx_to_chip(user, info->logger, skb);
	} else if (info->fm_audio == user &&
		   info->hci_fm_cmd_func == CG2900_FM_CMD_PARAM_NONE &&
		   info->audio_fm_cmd_id == CG2900_FM_CMD_NONE) {
		/*
		 * If it's command from fm audio user store the command id.
		 * It'll be used later to decide where to dispatch
		 * command complete event.
		 */
		info->audio_fm_cmd_id = cmd_id;
		dev_dbg(user->dev, "Sending FM audio command 0x%04X\n",
			info->audio_fm_cmd_id);
		cg2900_tx_to_chip(user, info->logger, skb);
	} else {
		dev_dbg(user->dev,
			"Not allowed to send FM cmd to controller, storing in "
			"TX queue\n");

		cg2900_skb_data(skb)->user = user;
		skb_queue_tail(&info->tx_queue_fm, skb);
	}
	spin_unlock_bh(&info->tx_fm_lock);
}

/**
 * is_bt_audio_user() - Checks if this packet is for the BT audio user.
 * @info:	CG2900 info.
 * @h4_channel:	H:4 channel for this packet.
 * @skb:	Packet to check.
 *
 * Returns:
 *   true if packet is for BT audio user.
 *   false otherwise.
 */
static bool is_bt_audio_user(struct cg2900_chip_info *info, int h4_channel,
			     const struct sk_buff * const skb)
{
	struct hci_event_hdr *hdr;
	u8 *payload;
	u16 opcode;

	if (use_device_channel_for_vs_cmd(info->chip_dev->chip.hci_revision)) {
		if (h4_channel == CHANNEL_DEV_MGMT_AUDIO)
			return true;
		else
			return false;
	}

	if (h4_channel != CHANNEL_BT_EVT)
		return false;

	hdr = (struct hci_event_hdr *)skb->data;
	payload = (u8 *)(hdr + 1); /* follows header */

	if (HCI_EV_CMD_COMPLETE == hdr->evt)
		opcode = le16_to_cpu(
			((struct hci_ev_cmd_complete *)payload)->opcode);
	else if (HCI_EV_CMD_STATUS == hdr->evt)
		opcode = le16_to_cpu(
			((struct hci_ev_cmd_status *)payload)->opcode);
	else
		return false;

	if (opcode != info->audio_bt_cmd_op)
		return false;

	dev_dbg(info->bt_audio->dev, "Audio BT OpCode match = 0x%04X\n",
		opcode);
	info->audio_bt_cmd_op = CG2900_BT_OPCODE_NONE;
	return true;
}

/**
 * is_fm_audio_user() - Checks if this packet is for the FM audio user.
 * @info:	CG2900 info.
 * @h4_channel:	H:4 channel for this packet.
 * @skb:	Packet to check.
 *
 * Returns:
 *   true if packet is for BT audio user.
 *   false otherwise.
 */
static bool is_fm_audio_user(struct cg2900_chip_info *info, int h4_channel,
			     const struct sk_buff * const skb)
{
	u8 cmd_func;
	u16 cmd_id;
	u16 irpt_val;
	u8 event;

	if (h4_channel != CHANNEL_FM_RADIO)
		return false;

	cmd_func = CG2900_FM_CMD_PARAM_NONE;
	cmd_id = CG2900_FM_CMD_NONE;
	irpt_val = 0;
	event = CG2900_FM_EVENT_UNKNOWN;

	fm_parse_event(&skb->data[0], &event, &cmd_func, &cmd_id,
		       &irpt_val);
	/* Check if command complete event FM legacy interface. */
	if ((event == CG2900_FM_EVENT_CMD_COMPLETE) &&
	    (cmd_func == CG2900_FM_CMD_PARAM_WRITECOMMAND) &&
	    (cmd_id == info->audio_fm_cmd_id)) {
		dev_dbg(info->fm_audio->dev,
			"FM Audio Function Code match = 0x%04X\n",
			cmd_id);
		return true;
	}

	/* Check if Interrupt legacy interface. */
	if ((event == CG2900_FM_EVENT_INTERRUPT) &&
	    (fm_is_do_cmd_irpt(irpt_val)) &&
	    (info->tx_fm_audio_awaiting_irpt))
		return true;

	return false;
}

/**
 * data_from_chip() - Called when data is received from the chip.
 * @dev:	Chip info.
 * @cg2900_dev:	CG2900 user for this packet.
 * @skb:	Packet received.
 *
 * The data_from_chip() function updates flow control and checks
 * if packet is a response for a packet it itself has transmitted. If not it
 * finds the correct user and sends the packet* to the user.
 */
static void data_from_chip(struct cg2900_chip_dev *dev,
			   struct sk_buff *skb)
{
	int h4_channel;
	struct list_head *cursor;
	struct cg2900_channel_item *tmp;
	struct cg2900_chip_info *info = dev->c_data;
	struct cg2900_user_data *user = NULL;

	spin_lock_bh(&info->rw_lock);
	/* Copy RX Data into logger.*/
	if (info->logger)
		cg2900_send_to_hci_logger(info->logger, skb,
						LOGGER_DIRECTION_RX);

	/*
	 * HCI Raw user can only have exclusive access to chip, there won't be
	 * other users once it's opened.
	 */
	if (info->hci_raw && info->hci_raw->opened) {
		info->hci_raw->read_cb(info->hci_raw, skb);
		spin_unlock_bh(&info->rw_lock);
		return;
	}

	h4_channel = skb->data[0];
	skb_pull(skb, HCI_H4_SIZE);

	/* First check if it is a BT or FM audio event */
	if (is_bt_audio_user(info, h4_channel, skb))
		user = info->bt_audio;
	else if (is_fm_audio_user(info, h4_channel, skb))
		user = info->fm_audio;
	spin_unlock_bh(&info->rw_lock);

	/* Now check if we should update flow control */
	if (h4_channel == CHANNEL_BT_EVT)
		update_flow_ctrl_bt(dev, skb);
	else if (h4_channel == CHANNEL_FM_RADIO)
		update_flow_ctrl_fm(dev, skb);

	/* Then check if this is a response to data we have sent */
	if (handle_rx_data_bt_evt(dev, skb, h4_channel))
		return;

	spin_lock_bh(&info->rw_lock);

	if (user)
		goto user_found;

	/* Let's see if it is the last user */
	if (info->last_user && info->last_user->h4_channel == h4_channel) {
		user = info->last_user;
		goto user_found;
	}

	/*
	 * Search through the list of all open channels to find the user.
	 * We skip the audio channels since they have already been checked
	 * earlier in this function.
	 */
	list_for_each(cursor, &info->open_channels) {
		tmp = list_entry(cursor, struct cg2900_channel_item, list);
		if (tmp->user->h4_channel == h4_channel &&
		    !tmp->user->is_audio) {
			user = tmp->user;
			goto user_found;
		}
	}

user_found:
	if (user != info->bt_audio && user != info->fm_audio)
		info->last_user = user;

	spin_unlock_bh(&info->rw_lock);

	if (user)
		user->read_cb(user, skb);
	else {
		dev_err(dev->dev,
			"Could not find corresponding user to h4_channel %d\n",
			h4_channel);
		kfree_skb(skb);
	}
}

/**
 * chip_removed() - Called when transport has been removed.
 * @dev:	Chip device.
 *
 * Removes registered MFD devices and frees internal resources.
 */
static void chip_removed(struct cg2900_chip_dev *dev)
{
	struct cg2900_chip_info *info = dev->c_data;

	cancel_delayed_work(&info->selftest_work.work);
	mfd_remove_devices(dev->dev);
	if (info->file_info.fw_file_ptc) {
		release_firmware(info->file_info.fw_file_ptc);
		info->file_info.fw_file_ptc = NULL;
	}

	if (info->file_info.fw_file_ssf) {
		release_firmware(info->file_info.fw_file_ssf);
		info->file_info.fw_file_ssf = NULL;
	}

	kfree(info->settings_file_name);
	kfree(info->patch_file_name);
	destroy_workqueue(info->wq);
	kfree(info);
	dev->c_data = NULL;
	dev->c_cb.chip_removed = NULL;
	dev->c_cb.data_from_chip = NULL;
}

/**
 * last_bt_user_removed() - Called when last BT user is removed.
 * @info:	Chip handler info.
 *
 * Clears out TX queue for BT.
 */
static void last_bt_user_removed(struct cg2900_chip_info *info)
{
	spin_lock_bh(&info->tx_bt_lock);
	skb_queue_purge(&info->tx_queue_bt);

	/*
	 * Reset number of packets allowed and number of outstanding
	 * BT commands.
	 */
	info->tx_nr_pkts_allowed_bt = 1;
	/* Reset the audio_bt_cmd_op. */
	info->audio_bt_cmd_op = CG2900_BT_OPCODE_NONE;
	spin_unlock_bh(&info->tx_bt_lock);
}

/**
 * last_fm_user_removed() - Called when last FM user is removed.
 * @info:	Chip handler info.
 *
 * Clears out TX queue for BT.
 */
static void last_fm_user_removed(struct cg2900_chip_info *info)
{
	spin_lock_bh(&info->tx_fm_lock);
	fm_reset_flow_ctrl(info);
	spin_unlock_bh(&info->tx_fm_lock);
}

/**
 * chip_shutdown() - Reset and power the chip off.
 * @user:	MFD device.
 */
static void chip_shutdown(struct cg2900_user_data *user)
{
	struct hci_command_hdr cmd;
	struct cg2900_chip_dev *dev = cg2900_get_prv(user);
	struct cg2900_chip_info *info = dev->c_data;

	dev_dbg(user->dev, "chip_shutdown\n");

	/* First do a quick power switch of the chip to assure a good state */
	if (dev->t_cb.set_chip_power)
		dev->t_cb.set_chip_power(dev, false);

	/*
	 * Wait 50ms before continuing to be sure that the chip detects
	 * chip power off.
	 */
	schedule_timeout_killable(
			msecs_to_jiffies(LINE_TOGGLE_DETECT_TIMEOUT));

	if (dev->t_cb.set_chip_power)
		dev->t_cb.set_chip_power(dev, true);

	/* Wait 100ms before continuing to be sure that the chip is ready */
	schedule_timeout_killable(msecs_to_jiffies(CHIP_READY_TIMEOUT));

	if (user != info->bt_audio && user != info->fm_audio)
		info->last_user = user;
	info->user_in_charge = user;

	/*
	 * Transmit HCI reset command to ensure the chip is using
	 * the correct transport and to put BT part in reset.
	 */
	dev_dbg(user->dev, "New closing_state: CLOSING_RESET\n");
	info->closing_state = CLOSING_RESET;
	cmd.opcode = cpu_to_le16(HCI_OP_RESET);
	cmd.plen = 0; /* No parameters for HCI reset */
	cg2900_send_bt_cmd(info->user_in_charge, info->logger, &cmd,
			   sizeof(cmd), CHANNEL_BT_CMD);
}

/**
 * chip_startup_finished() - Called when chip startup has finished.
 * @info:	Chip handler info.
 * @err:	Result of chip startup, 0 for no error.
 *
 * Shuts down the chip upon error, sets state to active, wakes waiting threads,
 * and informs transport that startup has finished.
 */
static void chip_startup_finished(struct cg2900_chip_info *info, int err)
{
	dev_dbg(BOOT_DEV, "chip_startup_finished (%d)\n", err);

	if (err)
		/* Shutdown the chip */
		cg2900_create_work_item(info->wq, work_chip_shutdown,
					info->user_in_charge);
	else {
		dev_dbg(BOOT_DEV, "New main_state: CG2900_ACTIVE\n");
		info->main_state = CG2900_ACTIVE;
	}

	wake_up_all(&main_wait_queue);

	if (err) {
		if (info->chip_dev->chip.hci_revision == CG2900_PG2_REV) {
			/*
			 * This will wakeup clock user too
			 * if it started the initialization process
			 */
			wake_up_all(&clk_user_wait_queue);
			return;
		}
	}

	if (!info->chip_dev->t_cb.chip_startup_finished)
		dev_dbg(BOOT_DEV, "chip_startup_finished callback not found\n");
	else
		info->chip_dev->t_cb.chip_startup_finished(info->chip_dev);
}

/**
 * cg2900_open() - Called when user wants to open an H4 channel.
 * @user:	MFD device to open.
 *
 * Checks that H4 channel is not already opened. If chip is not started, starts
 * up the chip. Sets channel as opened and adds user to active users.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user is NULL or read_cb is NULL.
 *   -EBUSY if chip is in transit state (being started or shutdown).
 *   -EACCES if H4 channel is already opened.
 *   -ENOMEM if allocation fails.
 *   -EIO if chip startup fails.
 *   Error codes generated by t_cb.open.
 */
static int cg2900_open(struct cg2900_user_data *user)
{
	int err;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;
	struct list_head *cursor;
	struct cg2900_channel_item *tmp;
	enum main_state state_to_check = CG2900_ACTIVE;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV, "cg2900_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	if (!user->read_cb) {
		dev_err(user->dev, "cg2900_open: read_cb missing\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "cg2900_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	/* HCI Raw channel shall have exclusive access to chip. */
	if (info->hci_raw && user->h4_channel != CHANNEL_HCI_RAW &&
		user->h4_channel != CHANNEL_HCI_LOGGER) {
		dev_err(user->dev, "cg2900_open: Cannot open %s "
			"channel while HCI Raw channel is opened\n",
			user->channel_data.char_dev_name);
		return -EACCES;
	}

	mutex_lock(&main_info->man_mutex);

	/*
	 * Add a minor wait in order to avoid CPU blocking, looping openings.
	 * Note there will of course be no wait if we are already in the right
	 * state.
	 */
	err = wait_event_timeout(main_wait_queue,
			(CG2900_IDLE == info->main_state ||
			 CG2900_ACTIVE == info->main_state),
			msecs_to_jiffies(CHIP_STARTUP_TIMEOUT));
	if (err <= 0) {
		if (CG2900_INIT == info->main_state)
			dev_err(user->dev, "Transport not opened\n");
		else
			dev_err(user->dev, "cg2900_open currently busy (0x%X). "
				"Try again\n", info->main_state);
		err = -EBUSY;
		goto err_free_mutex;
	}

	err = 0;

	list_for_each(cursor, &info->open_channels) {
		tmp = list_entry(cursor, struct cg2900_channel_item, list);
		if (tmp->user->h4_channel == user->h4_channel &&
		    tmp->user->is_audio == user->is_audio) {
			dev_err(user->dev, "Channel %d is already opened\n",
				user->h4_channel);
			err = -EACCES;
			goto err_free_mutex;
		}
	}

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp) {
		dev_err(user->dev, "Could not allocate tmp\n");
		err = -ENOMEM;
		goto err_free_mutex;
	}
	tmp->user = user;

	if (CG2900_ACTIVE != info->main_state &&
	    !user->chip_independent) {
		/* Open transport and start-up the chip */
		if (dev->t_cb.set_chip_power)
			dev->t_cb.set_chip_power(dev, true);

		/* Wait to be sure that the chip is ready */
		schedule_timeout_killable(
				msecs_to_jiffies(CHIP_READY_TIMEOUT));

		if (dev->t_cb.open) {
			err = dev->t_cb.open(dev);
			if (err) {
				if (dev->t_cb.set_chip_power)
					dev->t_cb.set_chip_power(dev, false);
				goto err_free_list_item;
			}
		}

		/* Start the boot sequence */
		info->user_in_charge = user;
		if (user != info->bt_audio && user != info->fm_audio)
			info->last_user = user;
		dev_dbg(user->dev, "New boot_state: BOOT_GET_FILES_TO_LOAD\n");
		info->boot_state = BOOT_GET_FILES_TO_LOAD;
		dev_dbg(user->dev, "New main_state: CG2900_BOOTING\n");
		info->main_state = CG2900_BOOTING;
		cg2900_create_work_item(info->wq, work_load_patch_and_settings,
					dev);

		dev_dbg(user->dev, "Wait 15sec for chip to start\n");
		if (dev->chip.hci_revision == CG2900_PG2_REV) {
			if (user->is_clk_user) {
				/*
				 * Special case only in case cg2900 PG2,
				 * as 500ms is taken in self test, to avoid
				 * this wait for wlan. Wlan is made to return
				 * earlier and not wait for read self test
				 * result.
				 */
				dev_dbg(user->dev, "Clock user is Waiting here\n");
				wait_event_timeout(clk_user_wait_queue,
					CG2900_ACTIVE_BEFORE_SELFTEST ==
					info->main_state,
					msecs_to_jiffies(CHIP_STARTUP_TIMEOUT));
				state_to_check = CG2900_ACTIVE_BEFORE_SELFTEST;
			} else {
				dev_dbg(user->dev, "Not a Clock user\n");
				wait_event_timeout(main_wait_queue,
					(CG2900_ACTIVE == info->main_state ||
					CG2900_IDLE == info->main_state),
					msecs_to_jiffies(CHIP_STARTUP_TIMEOUT));
			}
		} else {
			wait_event_timeout(main_wait_queue,
				(CG2900_ACTIVE == info->main_state ||
				CG2900_IDLE == info->main_state),
				msecs_to_jiffies(CHIP_STARTUP_TIMEOUT));
		}


		if (state_to_check != info->main_state) {
			dev_err(user->dev, "CG2900 init failed\n");

			if (dev->t_cb.close)
				dev->t_cb.close(dev);

			dev_dbg(user->dev, "main_state: CG2900_IDLE\n");
			info->main_state = CG2900_IDLE;
			err = -EIO;
			goto err_free_list_item;
		}

		if (CG2905_PG1_05_REV == dev->chip.hci_revision ||
				CG2910_PG1_REV == dev->chip.hci_revision ||
				CG2910_PG1_05_REV == dev->chip.hci_revision) {
			/*
			 * Switch to higher baud rate
			 * Because of bug in CG2905/CG2910 PG1 H/W,
			 * We have to download the ptc/ssf files
			 * at lower baud and then switch to Higher Baud
			 */
			if (info->chip_dev->t_cb.set_baud_rate)
				info->chip_dev->t_cb.set_baud_rate(
						info->chip_dev, false);
		}

	}

	list_add_tail(&tmp->list, &info->open_channels);

	user->opened = true;

	dev_dbg(user->dev, "H:4 channel opened\n");

	mutex_unlock(&main_info->man_mutex);
	return 0;
err_free_list_item:
	kfree(tmp);
err_free_mutex:
	mutex_unlock(&main_info->man_mutex);
	return err;
}

/**
 * cg2900_hci_log_open() - Called when user wants to open HCI logger channel.
 * @user:	MFD device to open.
 *
 * Registers user as hci_logger and calls @cg2900_open to open the channel.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user is NULL.
 *   -EEXIST if H4 channel is already opened.
 *   Error codes generated by cg2900_open.
 */
static int cg2900_hci_log_open(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;
	int err;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_hci_log_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "cg2900_hci_log_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (info->logger) {
		dev_err(user->dev, "HCI Logger already stored\n");
		return -EEXIST;
	}

	info->logger = user;
	err = cg2900_open(user);
	if (err)
		info->logger = NULL;
	return err;
}

/**
 * cg2900_hci_raw_open() - Called when user wants to open HCI Raw channel.
 * @user:	MFD device to open.
 *
 * Registers user as hci_raw and calls @cg2900_open to open the channel.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user is NULL.
 *   -EEXIST if H4 channel is already opened.
 *   -EACCES if H4 channel iother than HCI RAW is already opened.
 *   Error codes generated by cg2900_open.
 */
static int cg2900_hci_raw_open(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;
	struct list_head *cursor;
	struct cg2900_channel_item *tmp;
	int err;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_hci_raw_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "cg2900_hci_raw_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (info->hci_raw) {
		dev_err(user->dev, "HCI Raw Channel already stored\n");
		return -EEXIST;
	}

	if (!list_empty(&info->open_channels)) {
		/*
		 * Go through each open channel to check if it is logger
		 * channel or some other channel.
		 */
		list_for_each(cursor, &info->open_channels) {
			tmp = list_entry(cursor,
					struct cg2900_channel_item, list);
			if (tmp->user->h4_channel != CHANNEL_HCI_LOGGER) {
				dev_err(user->dev, "Other channels other than "
					"Logger is already opened. Cannot open "
					"HCI Raw Channel\n");
				return -EACCES;
			}
		}
	}

	info->hci_raw = user;
	err = cg2900_open(user);
	if (err)
		info->hci_raw = NULL;
	return err;
}

/**
 * cg2900_bt_audio_open() - Called when user wants to open BT audio channel.
 * @user:	MFD device to open.
 *
 * Registers user as bt_audio and calls @cg2900_open to open the channel.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user is NULL.
 *   -EEXIST if H4 channel is already opened.
 *   Error codes generated by cg2900_open.
 */
static int cg2900_bt_audio_open(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;
	int err;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_bt_audio_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "cg2900_bt_audio_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (info->bt_audio) {
		dev_err(user->dev, "BT Audio already stored\n");
		return -EEXIST;
	}

	info->bt_audio = user;
	err = cg2900_open(user);
	if (err)
		info->bt_audio = NULL;
	return err;
}

/**
 * cg2900_fm_audio_open() - Called when user wants to open FM audio channel.
 * @user:	MFD device to open.
 *
 * Registers user as fm_audio and calls @cg2900_open to open the channel.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user is NULL.
 *   -EEXIST if H4 channel is already opened.
 *   Error codes generated by cg2900_open.
 */
static int cg2900_fm_audio_open(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;
	int err;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_fm_audio_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "cg2900_fm_audio_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (info->fm_audio) {
		dev_err(user->dev, "FM Audio already stored\n");
		return -EEXIST;
	}

	info->fm_audio = user;
	err = cg2900_open(user);
	if (err)
		info->fm_audio = NULL;
	return err;
}

/**
 * cg2900_close() - Called when user wants to close an H4 channel.
 * @user:	MFD device to close.
 *
 * Clears up internal resources, sets channel as closed, and shuts down chip if
 * this was the last user.
 */
static void cg2900_close(struct cg2900_user_data *user)
{
	bool keep_powered = false;
	struct list_head *cursor, *next;
	struct cg2900_channel_item *tmp;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV, "cg2900_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "cg2900_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	mutex_lock(&main_info->man_mutex);

	/*
	 * Go through each open channel. Remove our channel and check if there
	 * is any other channel that want to keep the chip running
	 */
	list_for_each_safe(cursor, next, &info->open_channels) {
		tmp = list_entry(cursor, struct cg2900_channel_item, list);
		if (tmp->user == user) {
			list_del(cursor);
			kfree(tmp);
		} else if (!tmp->user->chip_independent)
			keep_powered = true;
	}

	if (user->h4_channel == CHANNEL_BT_CMD && !bt_is_open(info))
		last_bt_user_removed(info);
	else if (user->h4_channel == CHANNEL_FM_RADIO && !fm_is_open(info))
		last_fm_user_removed(info);

	if (keep_powered)
		/* This was not the last user, we're done. */
		goto finished;

	if (CG2900_IDLE == info->main_state)
		/* Chip has already been shut down. */
		goto finished;

	dev_dbg(user->dev, "New main_state: CG2900_CLOSING\n");
	info->main_state = CG2900_CLOSING;
	chip_shutdown(user);

	dev_dbg(user->dev, "Wait up to 15 seconds for chip to shut-down\n");
	wait_event_timeout(main_wait_queue,
				(CG2900_IDLE == info->main_state),
				msecs_to_jiffies(CHIP_SHUTDOWN_TIMEOUT));

	/* Force shutdown if we timed out */
	if (CG2900_IDLE != info->main_state) {
		dev_err(user->dev,
			"ST-Ericsson CG2900 Core Driver was shut-down with "
			"problems\n");

		if (dev->t_cb.close)
			dev->t_cb.close(dev);

		dev_dbg(user->dev, "New main_state: CG2900_IDLE\n");
		info->main_state = CG2900_IDLE;
	}

finished:
	mutex_unlock(&main_info->man_mutex);
	user->opened = false;
	dev_dbg(user->dev, "H:4 channel closed\n");
}

/**
 * cg2900_hci_log_close() - Called when user wants to close HCI logger channel.
 * @user:	MFD device to close.
 *
 * Clears hci_logger user and calls @cg2900_close to close the channel.
 */
static void cg2900_hci_log_close(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_hci_log_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "cg2900_hci_log_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (user != info->logger) {
		dev_err(user->dev, "cg2900_hci_log_close: Trying to remove "
			"another user\n");
		return;
	}

	info->logger = NULL;
	cg2900_close(user);
}

/**
 * cg2900_hci_raw_close() - Called when user wants to close HCI Raw channel.
 * @user:	MFD device to close.
 *
 * Clears hci_raw user and calls @cg2900_close to close the channel.
 */
static void cg2900_hci_raw_close(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_hci_raw_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "cg2900_hci_raw_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (user != info->hci_raw) {
		dev_err(user->dev, "cg2900_hci_raw_close: Trying to remove "
			"another user\n");
		return;
	}

	info->hci_raw = NULL;
	cg2900_close(user);
}

/**
 * cg2900_bt_audio_close() - Called when user wants to close BT audio channel.
 * @user:	MFD device to close.
 *
 * Clears bt_audio user and calls @cg2900_close to close the channel.
 */
static void cg2900_bt_audio_close(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_bt_audio_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "cg2900_bt_audio_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (user != info->bt_audio) {
		dev_err(user->dev, "cg2900_bt_audio_close: Trying to remove "
			"another user\n");
		return;
	}

	info->bt_audio = NULL;
	cg2900_close(user);
}

/**
 * cg2900_fm_audio_close() - Called when user wants to close FM audio channel.
 * @user:	MFD device to close.
 *
 * Clears fm_audio user and calls @cg2900_close to close the channel.
 */
static void cg2900_fm_audio_close(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"cg2900_fm_audio_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "cg2900_fm_audio_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (user != info->fm_audio) {
		dev_err(user->dev, "cg2900_fm_audio_close: Trying to remove "
			"another user\n");
		return;
	}

	info->fm_audio = NULL;
	cg2900_close(user);
}

/**
 * cg2900_reset() - Called when user wants to reset the chip.
 * @user:	MFD device to reset.
 *
 * Closes down the chip and calls reset_cb for all open users.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user is NULL.
 */
static int cg2900_reset(struct cg2900_user_data *user)
{
	struct list_head *cursor, *next;
	struct cg2900_channel_item *tmp;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	if (!user) {
		dev_err(MAIN_DEV, "cg2900_reset: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	dev_info(user->dev, "cg2900_reset\n");

	BUG_ON(!main_info);

	mutex_lock(&main_info->man_mutex);

	dev_dbg(user->dev, "New main_state: CG2900_RESETING\n");
	info->main_state = CG2900_RESETING;

	chip_shutdown(user);

	/*
	 * Inform all opened channels about the reset and free the user devices
	 */
	list_for_each_safe(cursor, next, &info->open_channels) {
		tmp = list_entry(cursor, struct cg2900_channel_item, list);
		list_del(cursor);
		tmp->user->opened  = false;
		tmp->user->reset_cb(tmp->user);
		kfree(tmp);
	}

	/* Reset finished. We are now idle until first channel is opened */
	dev_dbg(user->dev, "New main_state: CG2900_IDLE\n");
	info->main_state = CG2900_IDLE;

	mutex_unlock(&main_info->man_mutex);

	/*
	 * Send wake-up since this might have been called from a failed boot.
	 * No harm done if it is a CG2900 chip user who called.
	 */
	wake_up_all(&main_wait_queue);

	return 0;
}

/**
 * cg2900_alloc_skb() - Allocates socket buffer.
 * @size:	Sk_buffer size in bytes.
 * @priority:	GFP priorit for allocation.
 *
 * Allocates a sk_buffer and reserves space for H4 header.
 *
 * Returns:
 *   sk_buffer if success.
 *   NULL if allocation fails.
 */
static struct sk_buff *cg2900_alloc_skb(unsigned int size, gfp_t priority)
{
	struct sk_buff *skb;

	dev_dbg(MAIN_DEV, "cg2900_alloc_skb size %d bytes\n", size);

	/* Allocate the SKB and reserve space for the header */
	skb = alloc_skb(size + CG2900_SKB_RESERVE, priority);
	if (skb)
		skb_reserve(skb, CG2900_SKB_RESERVE);

	return skb;
}

/**
 * cg2900_write() - Called when user wants to write to the chip.
 * @user:	MFD device representing H4 channel to write to.
 * @skb:	Sk_buffer to transmit.
 *
 * Transmits the sk_buffer to the chip. If it is a BT cmd or FM audio packet it
 * is checked that it is allowed to transmit the chip.
 * Note that if error is returned it is up to the user to free the skb.
 *
 * Returns:
 *   0 if success.
 *   -EINVAL if user or skb is NULL.
 *   -EACCES if channel is closed.
 */
static int cg2900_write(struct cg2900_user_data *user, struct sk_buff *skb)
{
	u8 *h4_header;
	struct cg2900_chip_dev *dev;
	struct cg2900_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV, "cg2900_write: Calling with NULL pointer\n");
		return -EINVAL;
	}

	if (!skb) {
		dev_err(user->dev, "cg2900_write with no sk_buffer\n");
		return -EINVAL;
	}

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	dev_dbg(user->dev, "cg2900_write length %d bytes\n", skb->len);

	if (!user->opened) {
		dev_err(user->dev,
			"Trying to transmit data on a closed channel\n");
		return -EACCES;
	}

	if (user->h4_channel == CHANNEL_HCI_RAW) {
		/*
		 * Since the data transmitted on HCI Raw channel
		 * can be byte by byte, flow control cannot be used.
		 * This should be handled by user space application
		 * of the HCI Raw channel, so just transmit the
		 * received data to chip.
		 */
		cg2900_tx_to_chip(user, info->logger, skb);
		return 0;
	}

	/*
	 * Move the data pointer to the H:4 header position and
	 * store the H4 header.
	 */
	h4_header = skb_push(skb, CG2900_SKB_RESERVE);
	*h4_header = (u8)user->h4_channel;

	if (user->h4_channel == CHANNEL_BT_CMD)
		transmit_skb_with_flow_ctrl_bt(user, skb);
	else if (user->h4_channel == CHANNEL_FM_RADIO)
		transmit_skb_with_flow_ctrl_fm(user, skb);
	else
		cg2900_tx_to_chip(user, info->logger, skb);

	return 0;
}

/**
 * cg2900_no_write() - Used for channels where it is not allowed to write.
 * @user:	MFD device representing H4 channel to write to.
 * @skb:	Sk_buffer to transmit.
 *
 * Returns:
 *   -EPERM.
 */
static int cg2900_no_write(struct cg2900_user_data *user,
			   __attribute__((unused)) struct sk_buff *skb)
{
	dev_err(user->dev, "Not allowed to send on this channel\n");
	return -EPERM;
}

/**
 * cg2900_get_local_revision() - Called to retrieve revision data for the chip.
 * @user:	MFD device to check.
 * @rev_data:	Revision data to fill in.
 *
 * Returns:
 *   true if success.
 *   false upon failure.
 */
static bool cg2900_get_local_revision(struct cg2900_user_data *user,
				      struct cg2900_rev_data *rev_data)
{
	struct cg2900_chip_dev *dev;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV, "cg2900_get_local_revision: Calling with "
			"NULL pointer\n");
		return false;
	}

	if (!rev_data) {
		dev_err(user->dev, "Calling with rev_data NULL\n");
		return false;
	}

	dev = cg2900_get_prv(user);

	rev_data->revision = dev->chip.hci_revision;
	rev_data->sub_version = dev->chip.hci_sub_version;

	return true;
}

static struct cg2900_user_data btcmd_data = {
	.h4_channel = CHANNEL_BT_CMD,
};
static struct cg2900_user_data btacl_data = {
	.h4_channel = CHANNEL_BT_ACL,
};
static struct cg2900_user_data btevt_data = {
	.h4_channel = CHANNEL_BT_EVT,
};
static struct cg2900_user_data nfc_data = {
	.h4_channel = CHANNEL_NFC,
};
static struct cg2900_user_data antcmd_data = {
	.h4_channel = CHANNEL_ANT_CMD,
};
static struct cg2900_user_data antdat_data = {
	.h4_channel = CHANNEL_ANT_DAT,
};
static struct cg2900_user_data fm_data = {
	.h4_channel = CHANNEL_FM_RADIO,
};
static struct cg2900_user_data gnss_data = {
	.h4_channel = CHANNEL_GNSS,
};
static struct cg2900_user_data debug_data = {
	.h4_channel = CHANNEL_DEBUG,
};
static struct cg2900_user_data ste_tools_data = {
	.h4_channel = CHANNEL_STE_TOOLS,
};
static struct cg2900_user_data hci_logger_data = {
	.h4_channel = CHANNEL_HCI_LOGGER,
	.chip_independent = true,
	.write = cg2900_no_write,
	.open = cg2900_hci_log_open,
	.close = cg2900_hci_log_close,
};
static struct cg2900_user_data core_data = {
	.h4_channel = CHANNEL_CORE,
	.write = cg2900_no_write,
};
static struct cg2900_user_data audio_vs_data = {
	.h4_channel = CHANNEL_BT_CMD,
	.is_audio = true,
	.open = cg2900_bt_audio_open,
	.close = cg2900_bt_audio_close,
};
static struct cg2900_user_data audio_fm_data = {
	.h4_channel = CHANNEL_FM_RADIO,
	.is_audio = true,
	.open = cg2900_fm_audio_open,
	.close = cg2900_fm_audio_close,
};
static struct cg2900_user_data hci_raw_data = {
	.h4_channel = CHANNEL_HCI_RAW,
	.open = cg2900_hci_raw_open,
	.close = cg2900_hci_raw_close,
};

static struct mfd_cell cg2900_devs[] = {
	{
		.name = "cg2900-btcmd",
		.platform_data = &btcmd_data,
		.pdata_size = sizeof(btcmd_data),
	},
	{
		.name = "cg2900-btacl",
		.platform_data = &btacl_data,
		.pdata_size = sizeof(btacl_data),
	},
	{
		.name = "cg2900-btevt",
		.platform_data = &btevt_data,
		.pdata_size = sizeof(btevt_data),
	},
	{
		.name = "cg2900-fm",
		.platform_data = &fm_data,
		.pdata_size = sizeof(fm_data),
	},
	{
		.name = "cg2900-gnss",
		.platform_data = &gnss_data,
		.pdata_size = sizeof(gnss_data),
	},
	{
		.name = "cg2900-debug",
		.platform_data = &debug_data,
		.pdata_size = sizeof(debug_data),
	},
	{
		.name = "cg2900-stetools",
		.platform_data = &ste_tools_data,
		.pdata_size = sizeof(ste_tools_data),
	},
	{
		.name = "cg2900-hcilogger",
		.platform_data = &hci_logger_data,
		.pdata_size = sizeof(hci_logger_data),
	},
	{
		.name = "cg2900-core",
		.platform_data = &core_data,
		.pdata_size = sizeof(core_data),
	},
	{
		.name = "cg2900-audiovs",
		.platform_data = &audio_vs_data,
		.pdata_size = sizeof(audio_vs_data),
	},
	{
		.name = "cg2900-audiofm",
		.platform_data = &audio_fm_data,
		.pdata_size = sizeof(audio_fm_data),
	},
	{
		.name = "cg2900-hciraw",
		.platform_data = &hci_raw_data,
		.pdata_size = sizeof(hci_raw_data),
	},
};

static struct mfd_cell cg2905_extra_devs[] = {
	{
		.name = "cg2900-antcmd",
		.platform_data = &antcmd_data,
		.pdata_size = sizeof(antcmd_data)
	},
	{
		.name = "cg2900-antdat",
		.platform_data = &antdat_data,
		.pdata_size = sizeof(antdat_data)
	},
};

static struct mfd_cell cg2910_extra_devs[] = {
	{
		.name = "cg2900-nfc",
		.platform_data = &nfc_data,
		.pdata_size = sizeof(nfc_data)
	},
};

static struct mfd_cell cg2910_extra_devs_pg2[] = {
	{
		.name = "cg2900-nfc",
		.platform_data = &nfc_data,
		.pdata_size = sizeof(nfc_data)
	},
	{
		.name = "cg2900-antcmd",
		.platform_data = &antcmd_data,
		.pdata_size = sizeof(antcmd_data)
	},
	{
		.name = "cg2900-antdat",
		.platform_data = &antdat_data,
		.pdata_size = sizeof(antdat_data)
	},
};

static struct cg2900_user_data char_btcmd_data = {
	.channel_data = {
		.char_dev_name = CG2900_BT_CMD,
	},
	.h4_channel = CHANNEL_BT_CMD,
};
static struct cg2900_user_data char_btacl_data = {
	.channel_data = {
		.char_dev_name = CG2900_BT_ACL,
	},
	.h4_channel = CHANNEL_BT_ACL,
};
static struct cg2900_user_data char_btevt_data = {
	.channel_data = {
		.char_dev_name = CG2900_BT_EVT,
	},
	.h4_channel = CHANNEL_BT_EVT,
};
static struct cg2900_user_data char_nfc_data = {
	.channel_data = {
		.char_dev_name = CG2900_NFC,
	},
	.h4_channel = CHANNEL_NFC,
};
static struct cg2900_user_data char_antcmd_data = {
	.channel_data = {
		.char_dev_name = CG2900_ANT_CMD,
	},
	.h4_channel = CHANNEL_ANT_CMD,
};
static struct cg2900_user_data char_antdat_data = {
	.channel_data = {
		.char_dev_name = CG2900_ANT_DAT,
	},
	.h4_channel = CHANNEL_ANT_DAT,
};
static struct cg2900_user_data char_fm_data = {
	.channel_data = {
		.char_dev_name = CG2900_FM_RADIO,
	},
	.h4_channel = CHANNEL_FM_RADIO,
};
static struct cg2900_user_data char_gnss_data = {
	.channel_data = {
		.char_dev_name = CG2900_GNSS,
	},
	.h4_channel = CHANNEL_GNSS,
};
static struct cg2900_user_data char_debug_data = {
	.channel_data = {
		.char_dev_name = CG2900_DEBUG,
	},
	.h4_channel = CHANNEL_DEBUG,
};
static struct cg2900_user_data char_ste_tools_data = {
	.channel_data = {
		.char_dev_name = CG2900_STE_TOOLS,
	},
	.h4_channel = CHANNEL_STE_TOOLS,
};
static struct cg2900_user_data char_hci_logger_data = {
	.channel_data = {
		.char_dev_name = CG2900_HCI_LOGGER,
	},
	.h4_channel = CHANNEL_HCI_LOGGER,
	.chip_independent = true,
	.write = cg2900_no_write,
	.open = cg2900_hci_log_open,
	.close = cg2900_hci_log_close,
};
static struct cg2900_user_data char_core_data = {
	.channel_data = {
		.char_dev_name = CG2900_CORE,
	},
	.h4_channel = CHANNEL_CORE,
	.write = cg2900_no_write,
};
static struct cg2900_user_data char_audio_vs_data = {
	.channel_data = {
		.char_dev_name = CG2900_VS_AUDIO,
	},
	.h4_channel = CHANNEL_BT_CMD,
	.is_audio = true,
};
static struct cg2900_user_data char_audio_fm_data = {
	.channel_data = {
		.char_dev_name = CG2900_FM_AUDIO,
	},
	.h4_channel = CHANNEL_FM_RADIO,
	.is_audio = true,
};
static struct cg2900_user_data char_hci_raw_data = {
	.channel_data = {
		.char_dev_name = CG2900_HCI_RAW,
	},
	.h4_channel = CHANNEL_HCI_RAW,
	.open = cg2900_hci_raw_open,
	.close = cg2900_hci_raw_close,
};


static struct mfd_cell cg2900_char_devs[] = {
	{
		.name = "cg2900-chardev",
		.id = 0,
		.platform_data = &char_btcmd_data,
		.pdata_size = sizeof(char_btcmd_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 1,
		.platform_data = &char_btacl_data,
		.pdata_size = sizeof(char_btacl_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 2,
		.platform_data = &char_btevt_data,
		.pdata_size = sizeof(char_btevt_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 3,
		.platform_data = &char_fm_data,
		.pdata_size = sizeof(char_fm_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 4,
		.platform_data = &char_gnss_data,
		.pdata_size = sizeof(char_gnss_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 5,
		.platform_data = &char_debug_data,
		.pdata_size = sizeof(char_debug_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 6,
		.platform_data = &char_ste_tools_data,
		.pdata_size = sizeof(char_ste_tools_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 7,
		.platform_data = &char_hci_logger_data,
		.pdata_size = sizeof(char_hci_logger_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 8,
		.platform_data = &char_core_data,
		.pdata_size = sizeof(char_core_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 9,
		.platform_data = &char_audio_vs_data,
		.pdata_size = sizeof(char_audio_vs_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 10,
		.platform_data = &char_audio_fm_data,
		.pdata_size = sizeof(char_audio_fm_data),
	},
	{
		.name = "cg2900-chardev",
		.id = 11,
		.platform_data = &char_hci_raw_data,
		.pdata_size = sizeof(char_hci_raw_data),
	},
};

static struct mfd_cell cg2905_extra_char_devs[] = {
	{
		.name = "cg2900-chardev",
		.id = 12,
		.platform_data = &char_antcmd_data,
		.pdata_size = sizeof(char_antcmd_data)
	},
	{
		.name = "cg2900-chardev",
		.id = 13,
		.platform_data = &char_antdat_data,
		.pdata_size = sizeof(char_antdat_data)
	},
};

static struct mfd_cell cg2910_extra_char_devs[] = {
	{
		.name = "cg2900-chardev",
		.id = 12,
		.platform_data = &char_nfc_data,
		.pdata_size = sizeof(char_nfc_data)
	},
};

static struct mfd_cell cg2910_extra_char_devs_pg2[] = {
	{
		.name = "cg2900-chardev",
		.id = 12,
		.platform_data = &char_nfc_data,
		.pdata_size = sizeof(char_nfc_data)
	},
	{
		.name = "cg2900-chardev",
		.id = 13,
		.platform_data = &char_antcmd_data,
		.pdata_size = sizeof(char_antcmd_data)
	},
	{
		.name = "cg2900-chardev",
		.id = 14,
		.platform_data = &char_antdat_data,
		.pdata_size = sizeof(char_antdat_data)
	},
};

/**
 * set_plat_data() - Initializes data for an MFD cell.
 * @cell:	MFD cell.
 * @dev:	Current chip.
 *
 * Sets each callback to default function unless already set.
 */
static void set_plat_data(struct mfd_cell *cell, struct cg2900_chip_dev *dev)
{
	struct cg2900_user_data *pf_data = cell->platform_data;

	if (!pf_data->open)
		pf_data->open = cg2900_open;
	if (!pf_data->close)
		pf_data->close = cg2900_close;
	if (!pf_data->reset)
		pf_data->reset = cg2900_reset;
	if (!pf_data->alloc_skb)
		pf_data->alloc_skb = cg2900_alloc_skb;
	if (!pf_data->write)
		pf_data->write = cg2900_write;
	if (!pf_data->get_local_revision)
		pf_data->get_local_revision = cg2900_get_local_revision;

	cg2900_set_prv(pf_data, dev);
}

/**
 * fetch_firmware_files() - Do a request_firmware for ssf/ptc files.
 * @dev:	Chip info structure.
 *
 * Returns:
 *   system wide error.
 */
static int fetch_firmware_files(struct cg2900_chip_dev *dev,
		struct cg2900_chip_info *info)
{
	int filename_size_ptc = strlen("CG29XX_XXXX_XXXX_patch.fw");
	int filename_size_ssf = strlen("CG29XX_XXXX_XXXX_settings.fw");
	int err;

	/*
	 * Create the patch file name from HCI revision and sub_version.
	 * filename_size_ptc does not include terminating NULL character
	 * so add 1.
	 */
	err = snprintf(info->patch_file_name, filename_size_ptc + 1,
			"CG29XX_%04X_%04X_patch.fw", dev->chip.hci_revision,
			dev->chip.hci_sub_version);
	if (err == filename_size_ptc) {
		dev_dbg(BOOT_DEV, "Downloading patch file %s\n",
				info->patch_file_name);
	} else {
		dev_err(BOOT_DEV, "Patch file name failed! err=%d\n", err);
		goto error_handling;
	}

	/* OK. Now it is time to download the patches */
	err = request_firmware(&(info->file_info.fw_file_ptc),
			info->patch_file_name,
			dev->dev);
	if (err < 0) {
		dev_err(BOOT_DEV, "Couldn't get patch file "
				"(%d)\n", err);
		goto error_handling;
	}

	/*
	 * Create the settings file name from HCI revision and sub_version.
	 * filename_size_ssf does not include terminating NULL character
	 * so add 1.
	 */
	err = snprintf(info->settings_file_name, filename_size_ssf + 1,
			"CG29XX_%04X_%04X_settings.fw", dev->chip.hci_revision,
			dev->chip.hci_sub_version);
	if (err == filename_size_ssf) {
		dev_dbg(BOOT_DEV, "Downloading settings file %s\n",
				info->settings_file_name);
	} else {
		dev_err(BOOT_DEV, "Settings file name failed! err=%d\n", err);
		goto error_handling;
	}

	/* Retrieve the settings file */
	err = request_firmware(&info->file_info.fw_file_ssf,
			info->settings_file_name,
			info->dev);
	if (err) {
		dev_err(BOOT_DEV, "Couldn't get settings file "
				"(%d)\n", err);
		goto error_handling;
	}

	return 0;

error_handling:
	if (info->file_info.fw_file_ptc) {
		release_firmware(info->file_info.fw_file_ptc);
		info->file_info.fw_file_ptc = NULL;
	}

	if (info->file_info.fw_file_ssf) {
		release_firmware(info->file_info.fw_file_ssf);
		info->file_info.fw_file_ssf = NULL;
	}

	return err;
}

/**
 * check_chip_support() - Checks if connected chip is handled by this driver.
 * @dev:	Chip info structure.
 *
 * First check if chip is supported by this driver. If that is the case fill in
 * the callbacks in @dev and initiate internal variables. Finally create MFD
 * devices for all supported H4 channels. When finished power off the chip.
 *
 * Returns:
 *   true if chip is handled by this driver.
 *   false otherwise.
 */
static bool check_chip_support(struct cg2900_chip_dev *dev)
{
	struct cg2900_platform_data *pf_data;
	struct cg2900_chip_info *info;
	int i;
	int err;

	dev_dbg(dev->dev, "check_chip_support\n");

	/*
	 * Check if this is a CG29XX revision.
	 * We do not care about the sub-version at the moment. Change this if
	 * necessary.
	 */
	if (dev->chip.manufacturer != CG2900_SUPP_MANUFACTURER
		|| !(check_chip_revision_support(dev->chip.hci_revision))) {
		dev_err(dev->dev, "Unsupported Chip revision:0x%x\n",
				dev->chip.hci_revision);
		return false;
	}

	/* Store needed data */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev->dev, "Couldn't allocate info struct\n");
		return false;
	}

	/* Initialize all variables */
	skb_queue_head_init(&info->tx_queue_bt);
	skb_queue_head_init(&info->tx_queue_fm);

	INIT_LIST_HEAD(&info->open_channels);

	spin_lock_init(&info->tx_bt_lock);
	spin_lock_init(&info->tx_fm_lock);
	spin_lock_init(&info->rw_lock);

	info->tx_nr_pkts_allowed_bt = 1;
	info->audio_bt_cmd_op = CG2900_BT_OPCODE_NONE;
	info->audio_fm_cmd_id = CG2900_FM_CMD_NONE;
	info->hci_fm_cmd_func = CG2900_FM_CMD_PARAM_NONE;
	info->fm_radio_mode = FM_RADIO_MODE_IDLE;
	info->chip_dev = dev;
	info->dev = dev->dev;

	info->wq = create_singlethread_workqueue(WQ_NAME);
	if (!info->wq) {
		dev_err(dev->dev, "Could not create workqueue\n");
		goto err_handling_free_info;
	}

	info->patch_file_name = kzalloc(NAME_MAX + 1, GFP_ATOMIC);
	if (!info->patch_file_name) {
		dev_err(dev->dev,
			"Couldn't allocate name buffer for patch file\n");
		goto err_handling_destroy_wq;
	}

	info->settings_file_name = kzalloc(NAME_MAX + 1, GFP_ATOMIC);
	if (!info->settings_file_name) {
		dev_err(dev->dev,
			"Couldn't allocate name buffers settings file\n");
		goto err_handling_free_patch_name;
	}

	err = fetch_firmware_files(dev, info);
	if (err) {
		dev_err(dev->dev,
				"Couldn't fetch firmware files\n");
		goto err_handling_free_patch_name;
	}

	info->selftest_work.data = info;
	INIT_DELAYED_WORK(&info->selftest_work.work,
			  work_send_read_selftest_cmd);

	if (use_device_channel_for_vs_cmd(dev->chip.hci_revision))
		info->h4_channel_for_device = CHANNEL_DEV_MGMT;
	else
		info->h4_channel_for_device = CHANNEL_BT_CMD;

	dev->c_data = info;
	/* Set the callbacks */
	dev->c_cb.data_from_chip = data_from_chip;
	dev->c_cb.chip_removed = chip_removed;

	mutex_lock(&main_info->man_mutex);

	pf_data = dev_get_platdata(dev->dev);
	btcmd_data.channel_data.bt_bus = pf_data->bus;
	btacl_data.channel_data.bt_bus = pf_data->bus;
	btevt_data.channel_data.bt_bus = pf_data->bus;

	if (use_device_channel_for_vs_cmd(dev->chip.hci_revision)) {
		audio_vs_data.h4_channel = CHANNEL_DEV_MGMT_AUDIO;
		char_audio_vs_data.h4_channel = CHANNEL_DEV_MGMT_AUDIO;
	}

	/* Set the Platform data based on supported chip */
	for (i = 0; i < ARRAY_SIZE(cg2900_devs); i++)
		set_plat_data(&cg2900_devs[i], dev);
	for (i = 0; i < ARRAY_SIZE(cg2900_char_devs); i++)
		set_plat_data(&cg2900_char_devs[i], dev);
	info->mfd_size = ARRAY_SIZE(cg2900_devs);
	info->mfd_char_size = ARRAY_SIZE(cg2900_char_devs);

	if (dev->chip.hci_revision == CG2905_PG2_REV) {
		for (i = 0; i < ARRAY_SIZE(cg2905_extra_devs); i++)
			set_plat_data(&cg2905_extra_devs[i], dev);
		for (i = 0; i < ARRAY_SIZE(cg2905_extra_char_devs); i++)
			set_plat_data(&cg2905_extra_char_devs[i], dev);
		info->mfd_extra_size = ARRAY_SIZE(cg2905_extra_devs);
		info->mfd_extra_char_size = ARRAY_SIZE(cg2905_extra_char_devs);
	} else if (dev->chip.hci_revision == CG2910_PG1_REV ||
			dev->chip.hci_revision == CG2910_PG1_05_REV) {
		for (i = 0; i < ARRAY_SIZE(cg2910_extra_devs); i++)
			set_plat_data(&cg2910_extra_devs[i], dev);
		for (i = 0; i < ARRAY_SIZE(cg2910_extra_char_devs); i++)
			set_plat_data(&cg2910_extra_char_devs[i], dev);
		info->mfd_extra_size = ARRAY_SIZE(cg2910_extra_devs);
		info->mfd_extra_char_size = ARRAY_SIZE(cg2910_extra_char_devs);
	} else if (dev->chip.hci_revision == CG2910_PG2_REV) {
		for (i = 0; i < ARRAY_SIZE(cg2910_extra_devs_pg2); i++)
			set_plat_data(&cg2910_extra_devs_pg2[i], dev);
		for (i = 0; i < ARRAY_SIZE(cg2910_extra_char_devs_pg2); i++)
			set_plat_data(&cg2910_extra_char_devs_pg2[i], dev);
		info->mfd_extra_size = ARRAY_SIZE(cg2910_extra_devs_pg2);
		info->mfd_extra_char_size =
				ARRAY_SIZE(cg2910_extra_char_devs_pg2);
	}


	info->startup = true;

	/*
	 * The devices will be registered when chip has been powered down, i.e.
	 * when the system startup is ready.
	 */

	mutex_unlock(&main_info->man_mutex);

	dev_info(dev->dev, "Chip supported by the CG2900 driver\n");

	/* Finish by turning off the chip */
	cg2900_create_work_item(info->wq, work_power_off_chip, dev);

	return true;

err_handling_free_patch_name:
	kfree(info->patch_file_name);
err_handling_destroy_wq:
	destroy_workqueue(info->wq);
err_handling_free_info:
	kfree(info);
	return false;
}

static struct cg2900_id_callbacks chip_support_callbacks = {
	.check_chip_support = check_chip_support,
};

/**
 * cg2900_chip_probe() - Initialize CG2900 chip handler resources.
 * @pdev:	Platform device.
 *
 * This function initializes the CG2900 driver, then registers to
 * the CG2900 Core.
 *
 * Returns:
 *   0 if success.
 *   -ENOMEM for failed alloc or structure creation.
 *   Error codes generated by cg2900_register_chip_driver.
 */
static int __devinit cg2900_chip_probe(struct platform_device *pdev)
{
	int err;

	dev_dbg(&pdev->dev, "cg2900_chip_probe\n");

	main_info = kzalloc(sizeof(*main_info), GFP_ATOMIC);
	if (!main_info) {
		dev_err(&pdev->dev, "Couldn't allocate main_info\n");
		return -ENOMEM;
	}

	main_info->dev = &pdev->dev;
	mutex_init(&main_info->man_mutex);

	err = cg2900_register_chip_driver(&chip_support_callbacks);
	if (err) {
		dev_err(&pdev->dev,
			"Couldn't register chip driver (%d)\n", err);
		goto error_handling;
	}

	dev_info(&pdev->dev, "CG2900 chip driver started\n");

	return 0;

error_handling:
	mutex_destroy(&main_info->man_mutex);
	kfree(main_info);
	main_info = NULL;
	return err;
}

/**
 * cg2900_chip_remove() - Release CG2900 chip handler resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success (always success).
 */
static int __devexit cg2900_chip_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "CG2900 chip driver removed\n");

	cg2900_deregister_chip_driver(&chip_support_callbacks);

	if (!main_info)
		return 0;
	mutex_destroy(&main_info->man_mutex);
	kfree(main_info);
	main_info = NULL;
	return 0;
}

static struct platform_driver cg2900_chip_driver = {
	.driver = {
		.name	= "cg2900-chip",
		.owner	= THIS_MODULE,
	},
	.probe	= cg2900_chip_probe,
	.remove	= __devexit_p(cg2900_chip_remove),
};

/**
 * cg2900_chip_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init cg2900_chip_init(void)
{
	pr_debug("cg2900_chip_init");
	return platform_driver_register(&cg2900_chip_driver);
}

/**
 * cg2900_chip_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit cg2900_chip_exit(void)
{
	pr_debug("cg2900_chip_exit");
	platform_driver_unregister(&cg2900_chip_driver);
}

module_init(cg2900_chip_init);
module_exit(cg2900_chip_exit);

MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Linux CG2900 Connectivity Device Driver");
