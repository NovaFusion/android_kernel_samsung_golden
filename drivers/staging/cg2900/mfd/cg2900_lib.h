/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson CG2900 GPS/BT/FM controller.
 */

#ifndef _CG2900_LIB_H_
#define _CG2900_LIB_H_

#include <linux/firmware.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>

#include "cg2900.h"

/**
 * struct cg2900_work - Generic work structure.
 * @work:	Work structure.
 * @user_data:	Arbitrary data set by user.
 */
struct cg2900_work {
	struct work_struct work;
	void *user_data;
};

/**
 * struct cg2900_file_info - Info structure for file to download.
 * @fw_file:		Stores firmware file.
 * @file_offset:	Current read offset in firmware file.
 * @chunk_id:		Stores current chunk ID of write file
 *			operations.
 */
struct cg2900_file_info {
	const struct firmware	*fw_file_ptc;
	const struct firmware	*fw_file_ssf;
	int			file_offset;
	u8			chunk_id;
};

extern void cg2900_tx_to_chip(struct cg2900_user_data *user,
			      struct cg2900_user_data *logger,
			      struct sk_buff *skb);
extern void cg2900_tx_no_user(struct cg2900_chip_dev *dev, struct sk_buff *skb);
extern void cg2900_send_bt_cmd(struct cg2900_user_data *user,
			       struct cg2900_user_data *logger,
			       void *data, int length,
			       u8 h4_channel);
extern void cg2900_send_bt_cmd_no_user(struct cg2900_chip_dev *dev, void *data,
				       int length);
extern void cg2900_create_work_item(struct workqueue_struct *wq,
				    work_func_t work_func,
				    void *user_data);
extern int cg2900_read_and_send_file_part(struct cg2900_user_data *user,
					  struct cg2900_user_data *logger,
					  struct cg2900_file_info *info,
					  const struct firmware *fw_file,
					  u8 h4_channel);
extern void cg2900_send_to_hci_logger(struct cg2900_user_data *logger,
							struct sk_buff *skb,
							u8 direction);

#endif /* _CG2900_LIB_H_ */
