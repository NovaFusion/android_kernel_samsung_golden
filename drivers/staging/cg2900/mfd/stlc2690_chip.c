/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson STLC2690 BT/FM controller.
 */
#define NAME					"stlc2690_chip"
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
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/mfd/core.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

#include "cg2900.h"
#include "cg2900_core.h"
#include "cg2900_lib.h"
#include "stlc2690_chip.h"

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

#define MAIN_DEV				(main_info->dev)
#define BOOT_DEV				(info->user_in_charge->dev)

#define WQ_NAME					"stlc2690_chip_wq"

#define LINE_TOGGLE_DETECT_TIMEOUT		50	/* ms */
#define CHIP_READY_TIMEOUT			100	/* ms */
#define CHIP_STARTUP_TIMEOUT			15000	/* ms */
#define CHIP_SHUTDOWN_TIMEOUT			15000	/* ms */

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

/** CHANNEL_HCI_LOGGER - Bluetooth HCI H:4 channel
 * for logging all transmitted H4 packets (on all channels).
 */
#define CHANNEL_HCI_LOGGER			0xFA

/** CHANNEL_CORE - Bluetooth HCI H:4 channel
 * for user space control of the ST-Ericsson connectivity controller.
 */
#define CHANNEL_CORE				0xFD

/*
 * For the char dev names we keep the same names in order to be able to reuse
 * the users and to keep a consistent interface.
 */

/** STLC2690_BT_CMD - Bluetooth HCI H4 channel for Bluetooth commands.
 */
#define STLC2690_BT_CMD				"cg2900_bt_cmd"

/** STLC2690_BT_ACL - Bluetooth HCI H4 channel for Bluetooth ACL data.
 */
#define STLC2690_BT_ACL				"cg2900_bt_acl"

/** STLC2690_BT_EVT - Bluetooth HCI H4 channel for Bluetooth events.
 */
#define STLC2690_BT_EVT				"cg2900_bt_evt"

/** STLC2690_HCI_LOGGER - BT channel for logging all transmitted H4 packets.
 * Data read is copy of all data transferred on the other channels.
 * Only write allowed is configuration of the HCI Logger.
 */
#define STLC2690_HCI_LOGGER			"cg2900_hci_logger"

/** STLC2690_CORE- Channel for keeping ST-Ericsson STLC2690 enabled.
 * Opening this channel forces the chip to stay powered.
 * No data can be written to or read from this channel.
 */
#define STLC2690_CORE				"cg2900_core"

/**
 * enum main_state - Main-state for STLC2690 driver.
 * @STLC2690_INIT:	STLC2690 initializing.
 * @STLC2690_IDLE:	No user registered to STLC2690 driver.
 * @STLC2690_BOOTING:	STLC2690 booting after first user is registered.
 * @STLC2690_CLOSING:	STLC2690 closing after last user has deregistered.
 * @STLC2690_RESETING:	STLC2690 reset requested.
 * @STLC2690_ACTIVE:	STLC2690 up and running with at least one user.
 */
enum main_state {
	STLC2690_INIT,
	STLC2690_IDLE,
	STLC2690_BOOTING,
	STLC2690_CLOSING,
	STLC2690_RESETING,
	STLC2690_ACTIVE
};

/**
 * enum boot_state - BOOT-state for STLC2690 chip driver.
 * @BOOT_RESET:				HCI Reset has been sent.
 * @BOOT_SEND_BD_ADDRESS:		VS Store In FS command with BD address
 *					has been sent.
 * @BOOT_GET_FILES_TO_LOAD:		STLC2690 chip driver is retrieving file
 *					to load.
 * @BOOT_DOWNLOAD_PATCH:		STLC2690 chip driver is downloading
 *					patches.
 * @BOOT_ACTIVATE_PATCHES_AND_SETTINGS:	STLC2690 chip driver is activating
 *					patches and settings.
 * @BOOT_READY:				STLC2690 chip driver boot is ready.
 * @BOOT_FAILED:			STLC2690 chip driver boot failed.
 */
enum boot_state {
	BOOT_RESET,
	BOOT_SEND_BD_ADDRESS,
	BOOT_GET_FILES_TO_LOAD,
	BOOT_DOWNLOAD_PATCH,
	BOOT_ACTIVATE_PATCHES_AND_SETTINGS,
	BOOT_READY,
	BOOT_FAILED
};

/**
 * enum file_load_state - BOOT_FILE_LOAD-state for STLC2690 chip driver.
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
 * struct stlc2690_channel_item - List object for channel.
 * @list:	list_head struct.
 * @user:	User for this channel.
 */
struct stlc2690_channel_item {
	struct list_head	list;
	struct cg2900_user_data	*user;
};

/**
 * struct stlc2690_skb_data - Structure for storing private data in an sk_buffer.
 * @dev:	STLC2690 device for this sk_buffer.
 */
struct stlc2690_skb_data {
	struct cg2900_user_data *user;
};
#define stlc2690_skb_data(__skb) ((struct stlc2690_skb_data *)((__skb)->cb))

/**
 * struct stlc2690_chip_info - Main info structure for STLC2690 chip driver.
 * @patch_file_name:		Stores patch file name.
 * @settings_file_name:		Stores settings file name.
 * @file_info:			Firmware file info (patch or settings).
 * @main_state:			Current MAIN-state of STLC2690 chip driver.
 * @boot_state:			Current BOOT-state of STLC2690 chip driver.
 * @file_load_state:		Current BOOT_FILE_LOAD-state of STLC2690 chip
 *				driver.
 * @download_state:		Current BOOT_DOWNLOAD-state of STLC2690 chip
 *				driver.
 * @wq:				STLC2690 chip driver workqueue.
 * @chip_dev:			Chip handler info.
 * @user_in_charge:		User currently operating. Normally used at
 *				channel open and close.
 * @last_user:			Last user of this chip.
 * @logger:			Logger user of this chip.
 * @startup:			True if system is starting up.
 * @mfd_size:			Number of MFD cells.
 * @mfd_char_size:		Number of MFD char device cells.
 */
struct stlc2690_chip_info {
	char				*patch_file_name;
	char				*settings_file_name;
	struct cg2900_file_info		file_info;
	enum main_state			main_state;
	enum boot_state			boot_state;
	enum file_load_state		file_load_state;
	enum download_state		download_state;
	struct workqueue_struct		*wq;
	struct cg2900_chip_dev		*chip_dev;
	spinlock_t			rw_lock;
	struct list_head		open_channels;
	struct cg2900_user_data		*user_in_charge;
	struct cg2900_user_data		*last_user;
	struct cg2900_user_data		*logger;
	bool				startup;
	int				mfd_size;
	int				mfd_char_size;
};

/**
 * struct main_info - Main info structure for STLC2690 chip driver.
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
 * main_wait_queue - Main Wait Queue in STLC2690 driver.
 */
static DECLARE_WAIT_QUEUE_HEAD(main_wait_queue);

static struct mfd_cell stlc2690_devs[];
static struct mfd_cell stlc2690_char_devs[];

static void chip_startup_finished(struct stlc2690_chip_info *info, int err);

/**
 * send_bd_address() - Send HCI VS command with BD address to the chip.
 */
static void send_bd_address(struct stlc2690_chip_info *info)
{
	struct bt_vs_store_in_fs_cmd *cmd;
	u8 plen = sizeof(*cmd) + BT_BDADDR_SIZE;

	cmd = kmalloc(plen, GFP_KERNEL);
	if (!cmd)
		return;

	cmd->opcode = cpu_to_le16(STLC2690_BT_OP_VS_STORE_IN_FS);
	cmd->plen = BT_PARAM_LEN(plen);
	cmd->user_id = STLC2690_VS_STORE_IN_FS_USR_ID_BD_ADDR;
	cmd->len = BT_BDADDR_SIZE;
	/* Now copy the BD address received from user space control app. */
	memcpy(cmd->data, bd_address, BT_BDADDR_SIZE);

	dev_dbg(BOOT_DEV, "New boot_state: BOOT_SEND_BD_ADDRESS\n");
	info->boot_state = BOOT_SEND_BD_ADDRESS;

	cg2900_send_bt_cmd(info->user_in_charge, info->logger, cmd, plen,
			CHANNEL_BT_CMD);

	kfree(cmd);
}

/**
 * send_settings_file() - Transmit settings file.
 *
 * The send_settings_file() function transmit settings file.
 * The file is read in parts to fit in HCI packets. When finished,
 * close the settings file and send HCI reset to activate settings and patches.
 */
static void send_settings_file(struct stlc2690_chip_info *info)
{
	int bytes_sent;

	bytes_sent = cg2900_read_and_send_file_part(info->user_in_charge,
						    info->logger,
						    &info->file_info,
						    info->file_info.fw_file_ssf,
						    CHANNEL_BT_CMD);
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
	struct stlc2690_chip_info *info = dev->c_data;

	bytes_sent = cg2900_read_and_send_file_part(info->user_in_charge,
						    info->logger,
						    &info->file_info,
						    info->file_info.fw_file_ptc,
						    CHANNEL_BT_CMD);
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
 * work_reset_after_error() - Handle reset.
 * @work:	Reference to work data.
 *
 * Handle a reset after received Command Complete event.
 */
static void work_reset_after_error(struct work_struct *work)
{
	struct cg2900_work *my_work;
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;

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
	struct stlc2690_chip_info *info;

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
	struct stlc2690_chip_info *info;

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
	struct stlc2690_chip_info *info = dev->c_data;

	dev_dbg(BOOT_DEV, "Received Reset complete event with status 0x%X\n",
		status);

	if (BOOT_RESET != info->boot_state &&
	    BOOT_ACTIVATE_PATCHES_AND_SETTINGS != info->boot_state)
		return false;

	if (HCI_BT_ERROR_NO_ERROR != status) {
		dev_err(BOOT_DEV, "Command complete for HciReset received with "
			"error 0x%X\n", status);
		cg2900_create_work_item(info->wq, work_reset_after_error, dev);
		return true;
	}

	if (BOOT_RESET == info->boot_state) {
		info->boot_state = BOOT_GET_FILES_TO_LOAD;
		cg2900_create_work_item(info->wq, work_load_patch_and_settings,
					dev);
	} else {
		/*
		 * The boot sequence is now finished successfully.
		 * Set states and signal to waiting thread.
		 */
		dev_dbg(BOOT_DEV, "New boot_state: BOOT_READY\n");
		info->boot_state = BOOT_READY;
		chip_startup_finished(info, 0);
	}

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
	struct stlc2690_chip_info *info = dev->c_data;

	dev_dbg(BOOT_DEV,
		"Received Store_in_FS complete event with status 0x%X\n",
		status);

	if (info->boot_state != BOOT_SEND_BD_ADDRESS)
		return false;

	if (HCI_BT_ERROR_NO_ERROR == status) {
		struct hci_command_hdr cmd;

		/* Send HCI Reset command to activate patches */
		dev_dbg(BOOT_DEV,
			"New boot_state: BOOT_ACTIVATE_PATCHES_AND_SETTINGS\n");
		info->boot_state = BOOT_ACTIVATE_PATCHES_AND_SETTINGS;

		cmd.opcode = cpu_to_le16(HCI_OP_RESET);
		cmd.plen = 0; /* No parameters for Reset */
		cg2900_send_bt_cmd(info->user_in_charge, info->logger, &cmd,
				   sizeof(cmd), CHANNEL_BT_CMD);
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
	struct stlc2690_chip_info *info = dev->c_data;

	if (info->boot_state != BOOT_DOWNLOAD_PATCH ||
	    info->download_state != DOWNLOAD_PENDING)
		return false;

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
	struct stlc2690_chip_info *info = dev->c_data;

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
 * handle_rx_data_bt_evt() - Check if received data should be handled in STLC2690 chip driver.
 * @skb:	Data packet
 *
 * The handle_rx_data_bt_evt() function checks if received data should be
 * handled in STLC2690 chip driver. If so handle it correctly.
 * Received data is always HCI BT Event.
 *
 * Returns:
 *   True,  if packet was handled internally,
 *   False, otherwise.
 */
static bool handle_rx_data_bt_evt(struct cg2900_chip_dev *dev,
				  struct sk_buff *skb)
{
	bool pkt_handled = false;
	/* skb cannot be NULL here so it is safe to de-reference */
	u8 *data = skb->data;
	struct hci_event_hdr *evt;
	u16 op_code;

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
		else if (op_code == STLC2690_BT_OP_VS_STORE_IN_FS)
			pkt_handled = handle_vs_store_in_fs_cmd_complete(dev,
									 data);
		else if (op_code == STLC2690_BT_OP_VS_WRITE_FILE_BLOCK)
			pkt_handled =
				handle_vs_write_file_block_cmd_complete(dev,
									data);
	} else if (HCI_EV_CMD_STATUS == evt->evt) {
		struct hci_ev_cmd_status *cmd_status;

		cmd_status = (struct hci_ev_cmd_status *)data;

		op_code = le16_to_cpu(cmd_status->opcode);

		dev_dbg(dev->dev, "Received Command Status: op_code = 0x%04X\n",
			op_code);

		if (op_code == STLC2690_BT_OP_VS_WRITE_FILE_BLOCK)
			pkt_handled = handle_vs_write_file_block_cmd_status
				(dev, cmd_status->status);
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
 * data_from_chip() - Called when data is received from the chip.
 * @dev:	Chip info.
 * @skb:	Packet received.
 *
 * The data_from_chip() function checks if packet is a response for a packet it
 * itself has transmitted. If not it finds the correct user and sends the packet
 * to the user.
 */
static void data_from_chip(struct cg2900_chip_dev *dev,
			   struct sk_buff *skb)
{
	int h4_channel;
	struct list_head *cursor;
	struct stlc2690_channel_item *tmp;
	struct stlc2690_chip_info *info = dev->c_data;
	struct cg2900_user_data *user = NULL;

	h4_channel = skb->data[0];
	skb_pull(skb, HCI_H4_SIZE);

	/* Then check if this is a response to data we have sent */
	if (h4_channel == CHANNEL_BT_EVT && handle_rx_data_bt_evt(dev, skb))
		return;

	spin_lock_bh(&info->rw_lock);

	/* Let's see if this packet has the same user as the last one */
	if (info->last_user && info->last_user->h4_channel == h4_channel) {
		user = info->last_user;
		goto user_found;
	}

	/* Search through the list of all open channels to find the user */
	list_for_each(cursor, &info->open_channels) {
		tmp = list_entry(cursor, struct stlc2690_channel_item, list);
		if (tmp->user->h4_channel == h4_channel) {
			user = tmp->user;
			goto user_found;
		}
	}

user_found:
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

static void chip_removed(struct cg2900_chip_dev *dev)
{
	struct stlc2690_chip_info *info = dev->c_data;

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
 * chip_shutdown() - Reset and power the chip off.
 */
static void chip_shutdown(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev = cg2900_get_prv(user);
	struct stlc2690_chip_info *info = dev->c_data;

	dev_dbg(user->dev, "chip_shutdown\n");

	/* Close the transport, which will power off the chip */
	if (dev->t_cb.close)
		dev->t_cb.close(dev);

	/* Chip shut-down finished, set correct state and wake up the chip. */
	dev_dbg(dev->dev, "New main_state: STLC2690_IDLE\n");
	info->main_state = STLC2690_IDLE;
	wake_up_all(&main_wait_queue);
}

static void chip_startup_finished(struct stlc2690_chip_info *info, int err)
{
	dev_dbg(BOOT_DEV, "chip_startup_finished (%d)\n", err);

	if (err)
		/* Shutdown the chip */
		chip_shutdown(info->user_in_charge);
	else {
		dev_dbg(BOOT_DEV, "New main_state: CORE_ACTIVE\n");
		info->main_state = STLC2690_ACTIVE;
	}

	wake_up_all(&main_wait_queue);

	if (err)
		return;

	if (!info->chip_dev->t_cb.chip_startup_finished)
		dev_err(BOOT_DEV, "chip_startup_finished callback not found\n");
	else
		info->chip_dev->t_cb.chip_startup_finished(info->chip_dev);
}

static int stlc2690_open(struct cg2900_user_data *user)
{
	int err;
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;
	struct list_head *cursor;
	struct stlc2690_channel_item *tmp;
	struct hci_command_hdr cmd;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV, "stlc2690_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "stlc2690_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	mutex_lock(&main_info->man_mutex);

	/* Add a minor wait in order to avoid CPU blocking, looping openings */
	err = wait_event_timeout(main_wait_queue,
				 (STLC2690_IDLE == info->main_state ||
				  STLC2690_ACTIVE == info->main_state),
				 msecs_to_jiffies(LINE_TOGGLE_DETECT_TIMEOUT));
	if (err <= 0) {
		if (STLC2690_INIT == info->main_state)
			dev_err(user->dev, "Transport not opened\n");
		else
			dev_err(user->dev, "stlc2690_open currently busy "
				"(0x%X). Try again\n", info->main_state);
		err = -EBUSY;
		goto err_free_mutex;
	}

	err = 0;

	list_for_each(cursor, &info->open_channels) {
		tmp = list_entry(cursor, struct stlc2690_channel_item, list);
		if (tmp->user->h4_channel == user->h4_channel) {
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

	if (STLC2690_ACTIVE != info->main_state &&
	    !user->chip_independent) {
		/* Open transport and start-up the chip */
		if (dev->t_cb.set_chip_power)
			dev->t_cb.set_chip_power(dev, true);

		/* Wait to be sure that the chip is ready */
		schedule_timeout_killable(
				msecs_to_jiffies(CHIP_READY_TIMEOUT));

		if (dev->t_cb.open)
			err = dev->t_cb.open(dev);
		if (err) {
			if (dev->t_cb.set_chip_power)
				dev->t_cb.set_chip_power(dev, false);
			goto err_free_list_item;
		}

		/* Start the boot sequence */
		info->user_in_charge = user;
		info->last_user = user;
		dev_dbg(user->dev, "New boot_state: BOOT_RESET\n");
		info->boot_state = BOOT_RESET;
		dev_dbg(user->dev, "New main_state: STLC2690_BOOTING\n");
		info->main_state = STLC2690_BOOTING;
		cmd.opcode = cpu_to_le16(HCI_OP_RESET);
		cmd.plen = 0; /* No parameters for HCI reset */
		cg2900_send_bt_cmd(user, info->logger, &cmd, sizeof(cmd),
				CHANNEL_BT_CMD);

		dev_dbg(user->dev, "Wait up to 15 seconds for chip to start\n");
		wait_event_timeout(main_wait_queue,
				   (STLC2690_ACTIVE == info->main_state ||
				    STLC2690_IDLE   == info->main_state),
				   msecs_to_jiffies(CHIP_STARTUP_TIMEOUT));
		if (STLC2690_ACTIVE != info->main_state) {
			dev_err(user->dev, "STLC2690 driver failed to start\n");

			if (dev->t_cb.close)
				dev->t_cb.close(dev);

			dev_dbg(user->dev, "New main_state: CORE_IDLE\n");
			info->main_state = STLC2690_IDLE;
			err = -EIO;
			goto err_free_list_item;
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

static int stlc2690_hci_log_open(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;
	int err;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"stlc2690_hci_log_open: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev_dbg(user->dev, "stlc2690_hci_log_open\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	if (info->logger) {
		dev_err(user->dev, "HCI Logger already stored\n");
		return -EACCES;
	}

	info->logger = user;
	err = stlc2690_open(user);
	if (err)
		info->logger = NULL;
	return err;
}

static void stlc2690_close(struct cg2900_user_data *user)
{
	bool keep_powered = false;
	struct list_head *cursor, *next;
	struct stlc2690_channel_item *tmp;
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"stlc2690_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "stlc2690_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	mutex_lock(&main_info->man_mutex);

	/*
	 * Go through each open channel. Remove our channel and check if there
	 * is any other channel that want to keep the chip running
	 */
	list_for_each_safe(cursor, next, &info->open_channels) {
		tmp = list_entry(cursor, struct stlc2690_channel_item, list);
		if (tmp->user == user) {
			list_del(cursor);
			kfree(tmp);
		} else if (!tmp->user->chip_independent)
			keep_powered = true;
	}

	if (keep_powered)
		/* This was not the last user, we're done. */
		goto finished;

	if (STLC2690_IDLE == info->main_state)
		/* Chip has already been shut down. */
		goto finished;

	dev_dbg(user->dev, "New main_state: CORE_CLOSING\n");
	info->main_state = STLC2690_CLOSING;
	chip_shutdown(user);

	dev_dbg(user->dev, "Wait up to 15 seconds for chip to shut-down\n");
	wait_event_timeout(main_wait_queue,
			   STLC2690_IDLE == info->main_state,
			   msecs_to_jiffies(CHIP_SHUTDOWN_TIMEOUT));

	/* Force shutdown if we timed out */
	if (STLC2690_IDLE != info->main_state) {
		dev_err(user->dev,
			"ST-Ericsson STLC2690 Core Driver was shut-down with "
			"problems\n");

		if (dev->t_cb.close)
			dev->t_cb.close(dev);

		dev_dbg(user->dev, "New main_state: CORE_IDLE\n");
		info->main_state = STLC2690_IDLE;
	}

finished:
	mutex_unlock(&main_info->man_mutex);
	user->opened = false;
	dev_dbg(user->dev, "H:4 channel closed\n");
}

static void stlc2690_hci_log_close(struct cg2900_user_data *user)
{
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"stlc2690_hci_log_close: Calling with NULL pointer\n");
		return;
	}

	dev_dbg(user->dev, "stlc2690_hci_log_close\n");

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	info->logger = NULL;
	stlc2690_close(user);
}

static int stlc2690_reset(struct cg2900_user_data *user)
{
	struct list_head *cursor, *next;
	struct stlc2690_channel_item *tmp;
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;

	if (!user) {
		dev_err(MAIN_DEV,
			"stlc2690_reset: Calling with NULL pointer\n");
		return -EINVAL;
	}

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	dev_info(user->dev, "stlc2690_reset\n");

	BUG_ON(!main_info);

	mutex_lock(&main_info->man_mutex);

	dev_dbg(user->dev, "New main_state: CORE_RESETING\n");
	info->main_state = STLC2690_RESETING;

	chip_shutdown(user);

	/*
	 * Inform all opened channels about the reset and free the user devices
	 */
	list_for_each_safe(cursor, next, &info->open_channels) {
		tmp = list_entry(cursor, struct stlc2690_channel_item, list);
		list_del(cursor);
		tmp->user->opened = false;
		tmp->user->reset_cb(tmp->user);
		kfree(tmp);
	}

	/* Reset finished. We are now idle until first channel is opened */
	dev_dbg(user->dev, "New main_state: STLC2690_IDLE\n");
	info->main_state = STLC2690_IDLE;

	mutex_unlock(&main_info->man_mutex);

	/*
	 * Send wake-up since this might have been called from a failed boot.
	 * No harm done if it is a STLC2690 chip user who called.
	 */
	wake_up_all(&main_wait_queue);

	return 0;
}

static struct sk_buff *stlc2690_alloc_skb(unsigned int size, gfp_t priority)
{
	struct sk_buff *skb;

	dev_dbg(MAIN_DEV, "stlc2690_alloc_skb size %d bytes\n", size);

	/* Allocate the SKB and reserve space for the header */
	skb = alloc_skb(size + CG2900_SKB_RESERVE, priority);
	if (skb)
		skb_reserve(skb, CG2900_SKB_RESERVE);

	return skb;
}

static int stlc2690_write(struct cg2900_user_data *user, struct sk_buff *skb)
{
	int err = 0;
	u8 *h4_header;
	struct cg2900_chip_dev *dev;
	struct stlc2690_chip_info *info;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV,
			"stlc2690_write: Calling with NULL pointer\n");
		return -EINVAL;
	}

	if (!skb) {
		dev_err(user->dev, "stlc2690_write with no sk_buffer\n");
		return -EINVAL;
	}

	dev = cg2900_get_prv(user);
	info = dev->c_data;

	dev_dbg(user->dev, "stlc2690_write length %d bytes\n", skb->len);

	if (!user->opened) {
		dev_err(user->dev,
			"Trying to transmit data on a closed channel\n");
		return -EACCES;
	}

	/*
	 * Move the data pointer to the H:4 header position and
	 * store the H4 header.
	 */
	h4_header = skb_push(skb, CG2900_SKB_RESERVE);
	*h4_header = (u8)user->h4_channel;
	cg2900_tx_to_chip(user, info->logger, skb);

	return err;
}

static int stlc2690_no_write(struct cg2900_user_data *user,
			     struct sk_buff *skb)
{
	dev_err(user->dev, "Not allowed to send on this channel\n");
	return -EPERM;
}

static bool stlc2690_get_local_revision(struct cg2900_user_data *user,
					struct cg2900_rev_data *rev_data)
{
	struct cg2900_chip_dev *dev;

	BUG_ON(!main_info);

	if (!user) {
		dev_err(MAIN_DEV, "stlc2690_get_local_revision: Calling with "
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
static struct cg2900_user_data hci_logger_data = {
	.h4_channel = CHANNEL_HCI_LOGGER,
	.chip_independent = true,
	.write = stlc2690_no_write,
	.open = stlc2690_hci_log_open,
	.close = stlc2690_hci_log_close,
};
static struct cg2900_user_data core_data = {
	.h4_channel = CHANNEL_CORE,
	.write = stlc2690_no_write,
};

static struct mfd_cell stlc2690_devs[] = {
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
		.name = "cg2900-hcilogger",
		.platform_data = &hci_logger_data,
		.pdata_size = sizeof(hci_logger_data),
	},
	{
		.name = "cg2900-core",
		.platform_data = &core_data,
		.pdata_size = sizeof(core_data),
	},
};

static struct cg2900_user_data char_btcmd_data = {
	.channel_data = {
		.char_dev_name = STLC2690_BT_CMD,
	},
	.h4_channel = CHANNEL_BT_CMD,
};
static struct cg2900_user_data char_btacl_data = {
	.channel_data = {
		.char_dev_name = STLC2690_BT_ACL,
	},
	.h4_channel = CHANNEL_BT_ACL,
};
static struct cg2900_user_data char_btevt_data = {
	.channel_data = {
		.char_dev_name = STLC2690_BT_EVT,
	},
	.h4_channel = CHANNEL_BT_EVT,
};
static struct cg2900_user_data char_hci_logger_data = {
	.channel_data = {
		.char_dev_name = STLC2690_HCI_LOGGER,
	},
	.h4_channel = CHANNEL_HCI_LOGGER,
	.chip_independent = true,
	.write = stlc2690_no_write,
	.open = stlc2690_hci_log_open,
	.close = stlc2690_hci_log_close,
};
static struct cg2900_user_data char_core_data = {
	.channel_data = {
		.char_dev_name = STLC2690_CORE,
	},
	.h4_channel = CHANNEL_CORE,
	.write = stlc2690_no_write,
};

static struct mfd_cell stlc2690_char_devs[] = {
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
	struct cg2900_user_data *user = cell->platform_data;

	if (!user->open)
		user->open = stlc2690_open;
	if (!user->close)
		user->close = stlc2690_close;
	if (!user->reset)
		user->reset = stlc2690_reset;
	if (!user->alloc_skb)
		user->alloc_skb = stlc2690_alloc_skb;
	if (!user->write)
		user->write = stlc2690_write;
	if (!user->get_local_revision)
		user->get_local_revision = stlc2690_get_local_revision;

	cg2900_set_prv(user, dev);
}

/**
 * fetch_firmware_files() - Do a request_firmware for ssf/ptc files.
 * @dev:	Chip info structure.
 *
 * Returns:
 *   system wide error.
 */
static int fetch_firmware_files(struct cg2900_chip_dev *dev,
		struct stlc2690_chip_info *info)
{
	int filename_size_ptc = strlen("STLC2690_XXXX_XXXX_patch.fw");
	int filename_size_ssf = strlen("STLC2690_XXXX_XXXX_settings.fw");
	int err;

	/*
	 * Create the patch file name from HCI revision and sub_version.
	 * filename_size_ptc does not include terminating NULL character
	 * so add 1.
	 */
	err = snprintf(info->patch_file_name, filename_size_ptc + 1,
			"STLC2690_%04X_%04X_patch.fw", dev->chip.hci_revision,
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
			"STLC2690_%04X_%04X_settings.fw",
			dev->chip.hci_revision, dev->chip.hci_sub_version);
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
			dev->dev);
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
 * If supported return true and fill in @callbacks.
 *
 * Returns:
 *   true if chip is handled by this driver.
 *   false otherwise.
 */
static bool check_chip_support(struct cg2900_chip_dev *dev)
{
	struct cg2900_platform_data *pf_data;
	struct stlc2690_chip_info *info;
	int i;
	int err;

	dev_dbg(dev->dev, "check_chip_support\n");

	/*
	 * Check if this is a STLC2690 revision.
	 * We do not care about the sub-version at the moment. Change this if
	 * necessary.
	 */
	if (dev->chip.manufacturer != STLC2690_SUPP_MANUFACTURER ||
	    dev->chip.hci_revision < STLC2690_SUPP_REVISION_MIN ||
	    dev->chip.hci_revision > STLC2690_SUPP_REVISION_MAX) {
		dev_dbg(dev->dev, "Chip not supported by STLC2690 driver\n"
			"\tMan: 0x%02X\n"
			"\tRev: 0x%04X\n"
			"\tSub: 0x%04X\n",
			dev->chip.manufacturer, dev->chip.hci_revision,
			dev->chip.hci_sub_version);
		return false;
	}

	/* Store needed data */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev->dev, "Couldn't allocate info struct\n");
		return false;
	}

	/* Initialize all variables */
	INIT_LIST_HEAD(&info->open_channels);
	spin_lock_init(&info->rw_lock);
	info->chip_dev = dev;

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

	dev->c_data = info;
	/* Set the callbacks */
	dev->c_cb.data_from_chip = data_from_chip;
	dev->c_cb.chip_removed = chip_removed,
	info->chip_dev = dev;

	mutex_lock(&main_info->man_mutex);

	pf_data = dev_get_platdata(dev->dev);
	btcmd_data.channel_data.bt_bus = pf_data->bus;
	btacl_data.channel_data.bt_bus = pf_data->bus;
	btevt_data.channel_data.bt_bus = pf_data->bus;

	for (i = 0; i < ARRAY_SIZE(stlc2690_devs); i++)
		set_plat_data(&stlc2690_devs[i], dev);
	for (i = 0; i < ARRAY_SIZE(stlc2690_char_devs); i++)
		set_plat_data(&stlc2690_char_devs[i], dev);
	mutex_unlock(&main_info->man_mutex);

	dev_info(dev->dev, "Chip supported by the STLC2690 chip driver\n");

	/* Close the transport, which will power off the chip */
	if (dev->t_cb.close)
		dev->t_cb.close(dev);

	dev_dbg(dev->dev, "New main_state: STLC2690_IDLE\n");
	info->main_state = STLC2690_IDLE;

	err = mfd_add_devices(dev->dev, main_info->cell_base_id, stlc2690_devs,
			      ARRAY_SIZE(stlc2690_devs), NULL, 0);
	if (err) {
		dev_err(dev->dev, "Failed to add stlc2690_devs (%d)\n", err);
		goto err_handling_free_settings_name;
	}

	err = mfd_add_devices(dev->dev, main_info->cell_base_id,
			      stlc2690_char_devs,
			      ARRAY_SIZE(stlc2690_char_devs), NULL, 0);
	if (err) {
		dev_err(dev->dev, "Failed to add stlc2690_char_devs (%d)\n",
			err);
		goto err_handling_remove_devs;
	}

	/*
	 * Increase base ID so next connected transport will not get the
	 * same device IDs.
	 */
	main_info->cell_base_id += MAX(ARRAY_SIZE(stlc2690_devs),
				       ARRAY_SIZE(stlc2690_char_devs));

	return true;

err_handling_remove_devs:
	mfd_remove_devices(dev->dev);
err_handling_free_settings_name:
	kfree(info->settings_file_name);
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
 * stlc2690_chip_probe() - Initialize STLC2690 chip handler resources.
 * @pdev:	Platform device.
 *
 * This function initializes the STLC2690 driver, then registers to
 * the CG2900 Core.
 *
 * Returns:
 *   0 if success.
 *   -ENOMEM for failed alloc or structure creation.
 *   Error codes generated by cg2900_register_chip_driver.
 */
static int __devinit stlc2690_chip_probe(struct platform_device *pdev)
{
	int err;

	dev_dbg(&pdev->dev, "stlc2690_chip_probe\n");

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

	dev_info(&pdev->dev, "STLC2690 chip driver started\n");

	return 0;

error_handling:
	mutex_destroy(&main_info->man_mutex);
	kfree(main_info);
	main_info = NULL;
	return err;
}

/**
 * stlc2690_chip_remove() - Release STLC2690 chip handler resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success (always success).
 */
static int __devexit stlc2690_chip_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "STLC2690 chip driver removed\n");

	cg2900_deregister_chip_driver(&chip_support_callbacks);

	if (!main_info)
		return 0;

	mutex_destroy(&main_info->man_mutex);
	kfree(main_info);
	main_info = NULL;
	return 0;
}

static struct platform_driver stlc2690_chip_driver = {
	.driver = {
		.name	= "stlc2690-chip",
		.owner	= THIS_MODULE,
	},
	.probe	= stlc2690_chip_probe,
	.remove	= __devexit_p(stlc2690_chip_remove),
};

/**
 * stlc2690_chip_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init stlc2690_chip_init(void)
{
	pr_debug("stlc2690_chip_init");
	return platform_driver_register(&stlc2690_chip_driver);
}

/**
 * stlc2690_chip_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit stlc2690_chip_exit(void)
{
	pr_debug("stlc2690_chip_exit");
	platform_driver_unregister(&stlc2690_chip_driver);
}

module_init(stlc2690_chip_init);
module_exit(stlc2690_chip_exit);

MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Linux STLC2690 Connectivity Device Driver");
