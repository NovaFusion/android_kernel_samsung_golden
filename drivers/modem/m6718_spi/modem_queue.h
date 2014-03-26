/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header:
 *   queue functionality.
 */
#ifndef _MODEM_QUEUE_H_
#define _MODEM_QUEUE_H_

void ipc_queue_init(struct ipc_link_context *context);
void ipc_queue_delete_frame(struct ipc_tx_queue *frame);
struct ipc_tx_queue *ipc_queue_new_frame(struct ipc_link_context *link_context,
	u32 l2_length);
bool ipc_queue_is_empty(struct ipc_link_context *context);
int ipc_queue_push_frame(struct ipc_link_context *link_context, u8 l2_header,
	u32 l2_length, void *l2_data);
struct ipc_tx_queue *ipc_queue_get_frame(struct ipc_link_context *context);
void ipc_queue_reset(struct ipc_link_context *context);

#endif /* _MODEM_QUEUE_H_ */
