/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson CG2900 GPS/BT/FM controller.
 */
#define NAME					"cg2900_lib"
#define pr_fmt(fmt)				NAME ": " fmt "\n"

#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/skbuff.h>
#include <linux/types.h>

#include "cg2900.h"
#include "cg2900_chip.h"
#include "cg2900_core.h"
#include "cg2900_lib.h"

/*
 * Max length in bytes for line buffer used to parse settings and patch file.
 * Must be max length of name plus characters used to define chip version.
 */
#define LINE_BUFFER_LENGTH			(NAME_MAX + 30)
#define LOGGER_HEADER_SIZE			1
/**
 * cg2900_tx_to_chip() - Transmit buffer to the transport.
 * @user:	User data for BT command channel.
 * @logger:	User data for logger channel.
 * @skb:	Data packet.
 *
 * The transmit_skb_to_chip() function transmit buffer to the transport.
 * If enabled, copy the transmitted data to the HCI logger as well.
 */
void cg2900_tx_to_chip(struct cg2900_user_data *user,
		       struct cg2900_user_data *logger, struct sk_buff *skb)
{
	int err;
	struct cg2900_chip_dev *chip_dev;

	dev_dbg(user->dev, "cg2900_tx_to_chip %d bytes.\n", skb->len);

	if (logger)
		cg2900_send_to_hci_logger(logger, skb, LOGGER_DIRECTION_TX);

	chip_dev = cg2900_get_prv(user);
	err = chip_dev->t_cb.write(chip_dev, skb);
	if (err) {
		dev_err(user->dev, "cg2900_tx_to_chip: Transport write failed "
			"(%d)\n", err);
		kfree_skb(skb);
	}
}
EXPORT_SYMBOL_GPL(cg2900_tx_to_chip);

/**
 * cg2900_tx_no_user() - Transmit buffer to the transport.
 * @dev:	Current chip to transmit to.
 * @skb:	Data packet.
 *
 * This function transmits buffer to the transport when no user exist (system
 * startup for example).
 */
void cg2900_tx_no_user(struct cg2900_chip_dev *dev, struct sk_buff *skb)
{
	int err;

	dev_dbg(dev->dev, "cg2900_tx_no_user %d bytes.\n", skb->len);

	err = dev->t_cb.write(dev, skb);
	if (err) {
		dev_err(dev->dev, "cg2900_tx_no_user: Transport write failed "
			"(%d)\n", err);
		kfree_skb(skb);
	}
}
EXPORT_SYMBOL_GPL(cg2900_tx_no_user);

/**
 * create_and_send_bt_cmd() - Copy and send sk_buffer.
 * @user:	User data for current channel.
 * @logger:	User data for logger channel.
 * @data:	Data to send.
 * @length:	Length in bytes of data.
 *
 * The create_and_send_bt_cmd() function allocate sk_buffer, copy supplied data
 * to it, and send the sk_buffer to controller.
 */
void cg2900_send_bt_cmd(struct cg2900_user_data *user,
			struct cg2900_user_data *logger,
			void *data, int length,
			u8 h4_channel)
{
	struct sk_buff *skb;

	skb = user->alloc_skb(length, GFP_ATOMIC);
	if (!skb) {
		dev_err(user->dev, "cg2900_send_bt_cmd: Couldn't alloc "
			"sk_buff with length %d\n", length);
		return;
	}

	memcpy(skb_put(skb, length), data, length);
	skb_push(skb, HCI_H4_SIZE);
	skb->data[0] = h4_channel;

	cg2900_tx_to_chip(user, logger, skb);
}
EXPORT_SYMBOL_GPL(cg2900_send_bt_cmd);

/**
 * cg2900_send_bt_cmd_no_user() - Copy and send sk_buffer with no assigned user.
 * @dev:	Current chip to transmit to.
 * @data:	Data to send.
 * @length:	Length in bytes of data.
 *
 * The cg2900_send_bt_cmd_no_user() function allocate sk_buffer, copy supplied
 * data to it, and send the sk_buffer to controller.
 */
void cg2900_send_bt_cmd_no_user(struct cg2900_chip_dev *dev, void *data,
				int length)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + HCI_H4_SIZE, GFP_KERNEL);
	if (!skb) {
		dev_err(dev->dev, "cg2900_send_bt_cmd_no_user: Couldn't alloc "
			"sk_buff with length %d\n", length);
		return;
	}

	skb_reserve(skb, HCI_H4_SIZE);
	memcpy(skb_put(skb, length), data, length);
	skb_push(skb, HCI_H4_SIZE);
	skb->data[0] = HCI_BT_CMD_H4_CHANNEL;

	cg2900_tx_no_user(dev, skb);
}
EXPORT_SYMBOL_GPL(cg2900_send_bt_cmd_no_user);

/**
 * create_work_item() - Create work item and add it to the work queue.
 * @wq:		Work queue.
 * @work_func:	Work function.
 * @user_data:	Arbitrary data set by user.
 *
 * The create_work_item() function creates work item and add it to
 * the work queue.
 * Note that work is allocated by kmalloc and work must be freed when work
 * function is started.
 */
void cg2900_create_work_item(struct workqueue_struct *wq, work_func_t work_func,
			     void *user_data)
{
	struct cg2900_work *new_work;
	int err;

	new_work = kmalloc(sizeof(*new_work), GFP_ATOMIC);
	if (!new_work) {
		pr_err("Failed to alloc memory for new_work");
		return;
	}

	INIT_WORK(&new_work->work, work_func);
	new_work->user_data = user_data;

	err = queue_work(wq, &new_work->work);
	if (!err) {
		pr_err("Failed to queue work_struct because it's already "
		       "in the queue");
		kfree(new_work);
	}
}
EXPORT_SYMBOL_GPL(cg2900_create_work_item);

/**
 * read_and_send_file_part() - Transmit a part of the supplied file.
 * @user:	User data for current channel.
 * @logger:	User data for logger channel.
 * @info:	File information.
 *
 * The cg2900_read_and_send_file_part() function transmit a part of the supplied
 * file to the controller.
 *
 * Returns:
 *   0 if there is no more data in the file.
 *   >0 for number of bytes sent.
 *   -ENOMEM if skb allocation failed.
 */
int cg2900_read_and_send_file_part(struct cg2900_user_data *user,
				   struct cg2900_user_data *logger,
				   struct cg2900_file_info *info,
				   const struct firmware *fw_file,
					u8 h4_channel)
{
	int bytes_to_copy;
	struct sk_buff *skb;
	struct bt_vs_write_file_block_cmd *cmd;
	int plen;

	/*
	 * Calculate number of bytes to copy;
	 * either max bytes for HCI packet or number of bytes left in file
	 */
	bytes_to_copy = min((int)HCI_BT_SEND_FILE_MAX_CHUNK_SIZE,
			    (int)(fw_file->size - info->file_offset));

	if (bytes_to_copy <= 0) {
		/* Nothing more to read in file. */
		dev_dbg(user->dev, "File download finished\n");
		info->chunk_id = 0;
		info->file_offset = 0;
		return 0;
	}

	/* There is more data to send */
	plen = sizeof(*cmd) + bytes_to_copy;
	skb = user->alloc_skb(plen, GFP_KERNEL);
	if (!skb) {
		dev_err(user->dev, "Couldn't allocate sk_buffer\n");
		return -ENOMEM;
	}

	skb_put(skb, plen);

	cmd = (struct bt_vs_write_file_block_cmd *)skb->data;
	cmd->opcode = cpu_to_le16(CG2900_BT_OP_VS_WRITE_FILE_BLOCK);
	cmd->plen = BT_PARAM_LEN(plen);
	cmd->id = info->chunk_id;
	info->chunk_id++;

	/* Copy the data from offset position */
	memcpy(cmd->data,
	       &(fw_file->data[info->file_offset]),
	       bytes_to_copy);

	/* Increase offset with number of bytes copied */
	info->file_offset += bytes_to_copy;

	skb_push(skb, CG2900_SKB_RESERVE);
	skb->data[0] = h4_channel;

	cg2900_tx_to_chip(user, logger, skb);

	return bytes_to_copy;
}
EXPORT_SYMBOL_GPL(cg2900_read_and_send_file_part);

void cg2900_send_to_hci_logger(struct cg2900_user_data *logger,
							struct sk_buff *skb,
							u8 direction)
{
	struct sk_buff *skb_log;
	u8 *p;

	/*
	 * Alloc a new sk_buff and copy the data into it. Then send it to
	 * the HCI logger.
	 */
	skb_log = alloc_skb(skb->len + LOGGER_HEADER_SIZE, GFP_NOWAIT);
	if (!skb_log) {
		pr_err("cg2900_send_to_hci_logger:"
				"Couldn't allocate skb_log\n");
		return;
	}
	/* Reserve 1 byte for direction.*/
	skb_reserve(skb_log, LOGGER_HEADER_SIZE);

	memcpy(skb_put(skb_log, skb->len), skb->data, skb->len);
	p = skb_push(skb_log, LOGGER_HEADER_SIZE);
	*p = (u8) direction;

	if (logger->read_cb)
		logger->read_cb(logger, skb_log);
	else
		kfree_skb(skb_log);

	return;
}
EXPORT_SYMBOL_GPL(cg2900_send_to_hci_logger);

MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Linux CG2900 Library functions");
