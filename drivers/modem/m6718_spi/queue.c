/*
 * Copyright (C) ST-Ericsson SA 2010,2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * U9500 <-> M6718 IPC protocol implementation using SPI:
 *   TX queue functionality.
 */
#include <linux/modem/m6718_spi/modem_driver.h>
#include "modem_util.h"

#define FRAME_LENGTH_ALIGN (4)
#define MAX_FRAME_COUNTER (256)

void ipc_queue_init(struct ipc_link_context *context)
{
	spin_lock_init(&context->tx_q_update_lock);
	atomic_set(&context->tx_q_count, 0);
	context->tx_q_free = IPC_TX_QUEUE_MAX_SIZE;
	INIT_LIST_HEAD(&context->tx_q);
	context->tx_frame_counter = 0;
}

void ipc_queue_delete_frame(struct ipc_tx_queue *frame)
{
	kfree(frame);
}

struct ipc_tx_queue *ipc_queue_new_frame(struct ipc_link_context *link_context,
	u32 l2_length)
{
	struct ipc_tx_queue *frame;
	u32 padded_len = l2_length;

	/* frame length padded to alignment boundary */
	if (padded_len % FRAME_LENGTH_ALIGN)
		padded_len += (FRAME_LENGTH_ALIGN -
				(padded_len % FRAME_LENGTH_ALIGN));

	dev_dbg(&link_context->sdev->dev,
		"link %d: new frame: length %d, padded to %d\n",
		link_context->link->id, l2_length, padded_len);

	frame = kzalloc(sizeof(*frame) + padded_len, GFP_ATOMIC);
	if (frame == NULL) {
		dev_err(&link_context->sdev->dev,
			"link %d error: failed to allocate frame\n",
			link_context->link->id);
		return NULL;
	}

	frame->actual_len = l2_length;
	frame->len = padded_len;
	frame->data = frame + 1;
	return frame;
}

bool ipc_queue_is_empty(struct ipc_link_context *context)
{
	unsigned long flags;
	bool empty;

	spin_lock_irqsave(&context->tx_q_update_lock, flags);
	empty = list_empty(&context->tx_q);
	spin_unlock_irqrestore(&context->tx_q_update_lock, flags);

	return empty;
}

int ipc_queue_push_frame(struct ipc_link_context *context, u8 channel,
	u32 length, void *data)
{
	u32 l2_hdr;
	unsigned long flags;
	struct ipc_tx_queue *frame;
	int *tx_frame_counter = &context->tx_frame_counter;
	int qcount;

	/*
	 * Max queue size is only approximate so we allow it to go a few bytes
	 * over the limit
	 */
	if (context->tx_q_free < length) {
		dev_dbg(&context->sdev->dev,
			"link %d: tx queue full, wanted %d free %d\n",
			context->link->id,
			length,
			context->tx_q_free);
		return -EAGAIN;
	}

	frame = ipc_queue_new_frame(context, length + IPC_L2_HDR_SIZE);
	if (frame == NULL)
		return -ENOMEM;

	/* create l2 header and copy to pdu buffer */
	l2_hdr = ipc_util_make_l2_header(channel, length);
	*(u32 *)frame->data = l2_hdr;

	/* copy the l2 sdu into the pdu buffer after the header */
	memcpy(frame->data + IPC_L2_HDR_SIZE, data, length);

	spin_lock_irqsave(&context->tx_q_update_lock, flags);
	frame->counter = *tx_frame_counter;
	*tx_frame_counter = (*tx_frame_counter + 1) % MAX_FRAME_COUNTER;
	list_add_tail(&frame->node, &context->tx_q);
	qcount = atomic_add_return(1, &context->tx_q_count);
	/* tx_q_free could go negative here */
	context->tx_q_free -= frame->len;
#ifdef CONFIG_DEBUG_FS
	context->tx_q_min = min(context->tx_q_free, context->tx_q_min);
#endif
	spin_unlock_irqrestore(&context->tx_q_update_lock, flags);

	dev_dbg(&context->sdev->dev,
		"link %d: push tx frame %d: %08x (ch %d len %d), "
		"new count %d, new free %d\n",
		context->link->id,
		frame->counter,
		l2_hdr,
		ipc_util_get_l2_channel(l2_hdr),
		ipc_util_get_l2_length(l2_hdr),
		qcount,
		context->tx_q_free);
	return 0;
}

struct ipc_tx_queue *ipc_queue_get_frame(struct ipc_link_context *context)
{
	unsigned long flags;
	struct ipc_tx_queue *frame;
	int qcount;

	spin_lock_irqsave(&context->tx_q_update_lock, flags);
	frame = list_first_entry(&context->tx_q, struct ipc_tx_queue, node);
	list_del(&frame->node);
	qcount = atomic_sub_return(1, &context->tx_q_count);
	context->tx_q_free += frame->len;
	spin_unlock_irqrestore(&context->tx_q_update_lock, flags);

	dev_dbg(&context->sdev->dev,
		"link %d: get tx frame %d, new count %d, "
		"new free %d\n",
		context->link->id, frame->counter, qcount, context->tx_q_free);
	return frame;
}

void ipc_queue_reset(struct ipc_link_context *context)
{
	unsigned long flags;
	struct ipc_tx_queue *frame;
	int qcount;

	spin_lock_irqsave(&context->tx_q_update_lock, flags);
	qcount = atomic_read(&context->tx_q_count);
	while (qcount != 0) {
		frame = list_first_entry(&context->tx_q,
				struct ipc_tx_queue, node);
		list_del(&frame->node);
		ipc_queue_delete_frame(frame);
		qcount = atomic_sub_return(1, &context->tx_q_count);
	}
	spin_unlock_irqrestore(&context->tx_q_update_lock, flags);
}
