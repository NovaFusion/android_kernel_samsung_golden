/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Mailbox Logical Driver
 *
 * Author: Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com> for ST-Ericsson.
 *         Bibek Basu ,bibek.basu@stericsson.com>
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <asm/mach-types.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <mach/mbox-db5500.h>
#include <mach/mbox_channels-db5500.h>
#include <linux/io.h>

/* Defines start sequence number for given mailbox channel */
#define CHANNEL_START_SEQUENCE_NUMBER	0x80

/* Defines number of channels per mailbox unit */
#define CHANNELS_PER_MBOX_UNIT		256

/*
 * This macro builds mbox channel PDU header with following format:
 * ---------------------------------------------------------------------------
 * |                |         |          |                                    |
 * | Sequence nmbr  |  Type   |  Length  | Destination logical channel number |
 * |                |         |          |                                    |
 * ---------------------------------------------------------------------------
 * 31               24        20         16                    0
 *
 */
#define BUILD_HEADER(chan, len, type, seq_no) \
			((chan) | (((len) & 0xf) << 16) | \
			(((type) & 0xf) << 20) | ((seq_no) << 24))

/* Returns type from mbox message header */
#define GET_TYPE(mbox_msg)			(((mbox_msg) >> 20) & 0xf)

/* Returns channel number from mbox message header */
#define GET_CHANNEL(mbox_msg)			((mbox_msg) & 0xffff)

/* Returns length of payload from mbox message header */
#define GET_LENGTH(mbox_msg)			(((mbox_msg) >> 16) & 0xf)

/* Returns sequence number from mbox message header */
#define GET_SEQ_NUMBER(mbox_msg)		(((mbox_msg) >> 24)

enum mbox_msg{
	MBOX_CLOSE,
	MBOX_OPEN,
	MBOX_SEND,
	MBOX_CAST,
	MBOX_ACK,
	MBOX_NAK,
};

enum mbox_dir {
	MBOX_TX,
	MBOX_RX,
};

struct mbox_channel_mapping {
	u16		chan_base;
	u8		mbox_id;
	enum mbox_dir	direction;
};

/* This table maps mbox logical channel to mbox id and direction */
static struct mbox_channel_mapping channel_mappings[] = {
	{0x500, 2, MBOX_RX}, /* channel 5 maps to mbox 0.1, dsp->app (unsec) */
	{0x900, 2, MBOX_TX}, /* channel 9 maps to mbox 0.0, app->dsp (unsec) */
};

/* This table specifies mailbox ids which mbox channels module will use */
static u8 mbox_ids[] = {
	2,		/* app <-> dsp (unsec) */
};

/**
 * struct mbox_unit_status - current status of mbox unit
 * @mbox_id :		holds mbox unit identification number
 * @mbox :		holds mbox pointer after mbox_register() call
 * @tx_chans :		holds list of open tx mbox channels
 * @tx_lock:		lock for tx channel
 * @rx_chans :		holds list of open rx mbox channels
 * @rx_lock:		lock for rx channel
 */
struct mbox_unit_status {
	u8			mbox_id;
	struct mbox		*mbox;
	struct list_head	tx_chans;
	spinlock_t		tx_lock;
	struct list_head	rx_chans;
	spinlock_t		rx_lock;
};

static struct {
	struct platform_device	*pdev;
	struct mbox_unit_status	mbox_unit[ARRAY_SIZE(mbox_ids)];
} channels;

/* This structure describes pending element for mbox tx channel */
struct pending_elem {
	struct list_head	list;
	u32			*data;
	u8			length;
};

struct rx_pending_elem {
	u32			buffer[MAILBOX_NR_OF_DATAWORDS];
	u8			length;
	void			*priv;
};

struct rx_pending_elem rx_pending[NUM_DSP_BUFFER];

/* This structure holds list of pending elements for mbox tx channel */
struct tx_channel {
	struct list_head	pending;
};

/* Specific status for mbox rx channel */
struct rx_channel {
	struct list_head	pending;
	spinlock_t		lock;
	u32			buffer[MAILBOX_NR_OF_DATAWORDS];
	u8			index;
	u8			length;
};

/**
 * struct channel_status - status of mbox channel - common for tx and rx
 * @list :		holds list of channels registered
 * @channel :		holds channel number
 * @state :		holds state of channel
 * @cb:			holds callback function forr rx channel
 * @with_ack :		holds if ack is needed
 * @rx:			holds pointer to rx_channel
 * @tx :		holds pointer to tx_channel
 * @receive_wq :	holds pointer to receive workqueue_struct
 * @cast_wq :		holds pointer to cast workqueue_struct
 * @open_msg:		holds work_struct for open msg
 * @receive_msg :	holds work_struct for receive msg
 * @cast_msg:		holds work_struct for cast msg
 * @lock:		holds lock for channel
 */
struct channel_status {
	atomic_t rcv_counter;
	struct list_head	list;
	u16			channel;
	int			state;
	mbox_channel_cb_t	*cb;
	void			*priv;
	u8			seq_number;
	bool			with_ack;
	struct rx_channel	rx;
	struct tx_channel	tx;
	struct workqueue_struct	*receive_wq;
	struct workqueue_struct	*cast_wq;
	struct work_struct	open_msg;
	struct work_struct	receive_msg;
	struct work_struct	cast_msg;
	struct mutex		lock;
};

/* Checks if provided channel number is valid */
static bool check_channel(u16 channel, enum mbox_dir direction)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(channel_mappings); i++) {
		if ((channel >= channel_mappings[i].chan_base) &&
		    (channel < channel_mappings[i].chan_base +
		    CHANNELS_PER_MBOX_UNIT)) {
			/* Check if direction of given channel is correct*/
			if (channel_mappings[i].direction == direction)
				return true;
			else
				break;
		}
	}
	return false;
}

/* get the tx channel corresponding to the given rx channel */
static u16 get_tx_channel(u16 channel)
{
	int i;
	int relative_chan = 0;
	int mbox_id = 0xFF;
	u16 tx_channel = 0xFF;

	for (i = 0; i < ARRAY_SIZE(channel_mappings); i++) {
		if ((channel >= channel_mappings[i].chan_base) &&
				(channel < channel_mappings[i].chan_base +
				 CHANNELS_PER_MBOX_UNIT)) {
			/* Check if direction of given channel is correct*/
			relative_chan = channel - channel_mappings[i].chan_base;
			mbox_id = channel_mappings[i].mbox_id;

		}
	}

	for (i = 0; i < ARRAY_SIZE(channel_mappings); i++) {
		if ((mbox_id == channel_mappings[i].mbox_id) &&
				(channel_mappings[i].direction == MBOX_TX))
			tx_channel = channel_mappings[i].chan_base +
				relative_chan;
	}
	return tx_channel;
}

/* Returns mbox unit id for given mbox channel */
static int get_mbox_id(u16 channel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(channel_mappings); i++) {
		if ((channel >= channel_mappings[i].chan_base) &&
		    (channel < channel_mappings[i].chan_base +
		    CHANNELS_PER_MBOX_UNIT)) {
			return channel_mappings[i].mbox_id;
		}
	}
	/* There is no mbox unit registered for given channel */
	return -EINVAL;
}

/* Returns mbox structure saved after mbox_register() call */
static struct mbox *get_mbox(u16 channel)
{
	int i;
	int mbox_id = get_mbox_id(channel);

	if (mbox_id < 0) {
		dev_err(&channels.pdev->dev, "couldn't get mbox id\n");
		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(channels.mbox_unit); i++) {
		if (channels.mbox_unit[i].mbox_id == mbox_id)
			return channels.mbox_unit[i].mbox;
	}
	return NULL;
}

/* Returns pointer to rx mbox channels list for given mbox unit */
static struct list_head *get_rx_list(u8 mbox_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mbox_ids); i++) {
		if (channels.mbox_unit[i].mbox_id == mbox_id)
			return &channels.mbox_unit[i].rx_chans;
	}
	return NULL;
}

/* Returns pointer to tx mbox channels list for given mbox unit */
static struct list_head *get_tx_list(u8 mbox_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mbox_ids); i++) {
		if (channels.mbox_unit[i].mbox_id == mbox_id)
			return &channels.mbox_unit[i].tx_chans;
	}
	return NULL;
}

static int send_pdu(struct channel_status *chan_status, int command,
		u16 channel)
{
	struct mbox *mbox;
	u32 header = 0;
	int ret = 0;
	/* SEND PDU is not supported */
	if (command == MBOX_SEND) {
		dev_err(&channels.pdev->dev, "SEND command not implemented\n");
		ret = -EINVAL;
		goto exit;
	}
	mbox = get_mbox(chan_status->channel);
	if (mbox == NULL) {
		dev_err(&channels.pdev->dev, "couldn't get mailbox\n");
		ret = -ENOSYS;
		goto exit;
	}
	/* For CAST type send all pending messages */
	if (command == MBOX_CAST) {
		struct list_head *pos, *n;

		/* Send all pending messages from TX channel */
		list_for_each_safe(pos, n, &chan_status->tx.pending) {
			struct pending_elem *pending =
				list_entry(pos, struct pending_elem, list);
			int i;

			header = BUILD_HEADER(channel,
					pending->length,
					command,
					chan_status->seq_number);

			ret = mbox_send(mbox, header, true);
			if (ret < 0) {
				dev_err(&channels.pdev->dev,
					"failed to send header, err=%d\n", ret);
				goto exit;
			}

			for (i = 0; i < pending->length; i++) {
				ret = mbox_send(mbox, pending->data[i], true);
				if (ret < 0) {
					dev_err(&channels.pdev->dev,
					"failed to send header, err=%d\n", ret);
					goto exit;
				}
			}

			/* Call client's callback that data is already sent */
			if (chan_status->cb)
				chan_status->cb(pending->data, pending->length,
						chan_status->priv);
			else
				dev_err(&channels.pdev->dev,
					"%s no callback provided:header 0x%x\n",
					__func__, header);

			/* Increment sequence number */
			chan_status->seq_number++;

			/* Remove and free element from the list */
			list_del(&pending->list);
			kfree(pending);
		}
	} else {
		header = BUILD_HEADER(channel, 0,
				command, chan_status->seq_number);

		ret = mbox_send(mbox, header, true);
		if (ret < 0)
			dev_err(&channels.pdev->dev, "failed to send header\n");
		/* Increment sequence number */
		chan_status->seq_number++;
	}

exit:
	return ret;
}

void mbox_handle_receive_msg(struct work_struct *work)
{
	struct channel_status *rx_chan = container_of(work,
			struct channel_status,
			receive_msg);

	if (!atomic_read(&rx_chan->rcv_counter))
		return;
rcv_msg:
	/* Call client's callback and reset state */
	if (rx_chan->cb) {
		static int rx_pending_count;
		rx_chan->cb(rx_pending[rx_pending_count].buffer,
				rx_pending[rx_pending_count].length,
				rx_pending[rx_pending_count].priv);
		rx_pending_count++;
		if (rx_pending_count == NUM_DSP_BUFFER)
			rx_pending_count = 0;
	} else {
		dev_err(&channels.pdev->dev,
				"%s no callback provided\n", __func__);
	}
	if (atomic_dec_return(&rx_chan->rcv_counter) > 0)
		goto rcv_msg;

}

void mbox_handle_open_msg(struct work_struct *work)
{
	struct channel_status *tx_chan = container_of(work,
							struct channel_status,
							 open_msg);
	/* Change channel state to OPEN */
	tx_chan->state = MBOX_OPEN;
	/* If pending list not empty, start sending data */
	mutex_lock(&tx_chan->lock);
	if (!list_empty(&tx_chan->tx.pending))
		send_pdu(tx_chan, MBOX_CAST, tx_chan->channel);
	mutex_unlock(&tx_chan->lock);
}

void mbox_handle_cast_msg(struct work_struct *work)
{
	struct channel_status *rx_chan = container_of(work,
							struct channel_status,
							 cast_msg);
	/* Check if channel is opened */
	if (rx_chan->state == MBOX_CLOSE) {
		/* Peer sent message to closed channel */
		dev_err(&channels.pdev->dev,
			"channel in wrong state\n");
	}
}

static bool handle_receive_msg(u32 mbox_msg, struct channel_status *rx_chan)
{
	int i;
	static int rx_pending_count;

	if (rx_chan) {
		/* Store received data in RX channel buffer */
		rx_chan->rx.buffer[rx_chan->rx.index++] = mbox_msg;

		/* Check if it's last data of PDU */
		if (rx_chan->rx.index == rx_chan->rx.length) {
			for (i = 0; i < MAILBOX_NR_OF_DATAWORDS; i++) {
				rx_pending[rx_pending_count].buffer[i] =
					rx_chan->rx.buffer[i];
			}

			rx_pending[rx_pending_count].length =
				rx_chan->rx.length;
			rx_pending[rx_pending_count].priv = rx_chan->priv;
			rx_chan->rx.index = 0;
			rx_chan->rx.length = 0;
			rx_chan->state = MBOX_OPEN;
			rx_chan->seq_number++;
			rx_pending_count++;
			if (rx_pending_count == NUM_DSP_BUFFER)
				rx_pending_count = 0;
			atomic_inc(&rx_chan->rcv_counter);
			queue_work(rx_chan->receive_wq,
			&rx_chan->receive_msg);
		}
		dev_dbg(&channels.pdev->dev, "%s OK\n", __func__);

		return true;
	}
	return false;
}

static void handle_open_msg(u16 channel, u8 mbox_id)
{
	struct list_head *tx_list, *pos;
	struct channel_status *tmp;
	struct channel_status *tx_chan = NULL;
	struct mbox_unit_status *mbox_unit;
	channel = get_tx_channel(channel);
	dev_dbg(&channels.pdev->dev, "%s mbox_id %d\tchannel %x\n",
			__func__, mbox_id, channel);
	/* Get TX channel for given mbox unit */
	tx_list = get_tx_list(mbox_id);
	if (tx_list == NULL) {
		dev_err(&channels.pdev->dev, "given mbox id is not valid %d\n",
			mbox_id);
		return;
	}
	mbox_unit = container_of(tx_list, struct mbox_unit_status, tx_chans);
	/* Search for channel in tx list */
	spin_lock(&mbox_unit->tx_lock);
	list_for_each(pos, tx_list) {
		tmp = list_entry(pos, struct channel_status, list);
		dev_dbg(&channels.pdev->dev, "tmp->channel=%d\n",
				tmp->channel);
		if (tmp->channel == channel)
			tx_chan = tmp;
	}
	spin_unlock(&mbox_unit->tx_lock);
	if (tx_chan) {
		schedule_work(&tx_chan->open_msg);
	} else {
		/* No tx channel found on the list, allocate new element */
		tx_chan = kzalloc(sizeof(*tx_chan), GFP_ATOMIC);
		if (tx_chan == NULL) {
			dev_err(&channels.pdev->dev,
				"failed to allocate memory\n");
			return;
		}

		/* Fill initial data and add this element to tx list */
		tx_chan->channel = get_tx_channel(channel);
		tx_chan->state = MBOX_OPEN;
		tx_chan->seq_number = CHANNEL_START_SEQUENCE_NUMBER;
		INIT_LIST_HEAD(&tx_chan->tx.pending);
		INIT_WORK(&tx_chan->open_msg, mbox_handle_open_msg);
		INIT_WORK(&tx_chan->cast_msg, mbox_handle_cast_msg);
		INIT_WORK(&tx_chan->receive_msg, mbox_handle_receive_msg);
		mutex_init(&tx_chan->lock);
		spin_lock(&mbox_unit->tx_lock);
		list_add_tail(&tx_chan->list, tx_list);
		spin_unlock(&mbox_unit->tx_lock);
	}
}

static void handle_cast_msg(u16 channel, struct channel_status *rx_chan,
						u32 mbox_msg, bool send)
{
	dev_dbg(&channels.pdev->dev, " %s\n", __func__);
	if (rx_chan) {
		rx_chan->rx.buffer[0] = mbox_msg;
		rx_chan->with_ack = send;
		rx_chan->rx.length = GET_LENGTH(rx_chan->rx.buffer[0]);
		if (rx_chan->rx.length <= MAILBOX_NR_OF_DATAWORDS &&
				rx_chan->rx.length > 0) {
			rx_chan->rx.index = 0;
			rx_chan->state = MBOX_CAST;
		}
		queue_work(rx_chan->cast_wq,
			&rx_chan->cast_msg);
	} else {
		/* Channel not found, peer sent wrong message */
		dev_err(&channels.pdev->dev, "channel %d doesn't exist\n",
			channel);
	}
}

/*
 * This callback is called whenever mbox unit receives data.
 * priv parameter holds mbox unit id.
 */
static void mbox_cb(u32 mbox_msg, void *priv)
{
	u8 mbox_id = *(u8 *)priv;
	struct list_head *rx_list;
	u8 type = GET_TYPE(mbox_msg);
	u16 channel = GET_CHANNEL(mbox_msg);
	struct mbox_unit_status *mbox_unit;
	struct list_head *pos;
	struct channel_status *tmp;
	struct channel_status *rx_chan = NULL;
	bool is_Payload = 0;

	dev_dbg(&channels.pdev->dev, "%s type %d\t, mbox_msg %x\n",
		       __func__, type, mbox_msg);

	/* Get RX channels list for given mbox unit */
	rx_list = get_rx_list(mbox_id);
	if (rx_list == NULL) {
		dev_err(&channels.pdev->dev, "given mbox id is not valid %d\n",
				mbox_id);
		return;
	}

	mbox_unit = container_of(rx_list, struct mbox_unit_status, rx_chans);
	/* Search for channel in rx list */
	spin_lock(&mbox_unit->rx_lock);
	list_for_each(pos, rx_list) {
		tmp = list_entry(pos, struct channel_status, list);
		if (tmp->state == MBOX_SEND ||
				tmp->state == MBOX_CAST) {
			/* Received message is payload */
			is_Payload = 1;
			rx_chan = tmp;
		} else
		if (tmp->channel == channel)
			rx_chan = tmp;
	}
	spin_unlock(&mbox_unit->rx_lock);
	/* if callback is present for that RX channel */
	if (rx_chan && rx_chan->cb) {
		/* If received message is payload this
		 * function will take care of it
		 */
		if ((is_Payload) && (handle_receive_msg(mbox_msg, rx_chan)))
			return;
	} else
		dev_err(&channels.pdev->dev, "callback not present:msg 0x%x "
				"rx_chan 0x%x\n", mbox_msg, (u32)rx_chan);

	/* Received message is header as no RX channel is in SEND/CAST state */
	switch (type) {
	case MBOX_CLOSE:
		/* Not implemented */
		break;
	case MBOX_OPEN:
		handle_open_msg(channel, mbox_id);
		break;
	case MBOX_SEND:
		/* if callback is present for that RX channel */
		if (rx_chan && rx_chan->cb)
			handle_cast_msg(channel, rx_chan, mbox_msg, true);
		break;
	case MBOX_CAST:
		/* if callback is present for that RX channel */
		if (rx_chan && rx_chan->cb)
			handle_cast_msg(channel, rx_chan, mbox_msg, false);
		break;
	case MBOX_ACK:
	case MBOX_NAK:
		/* Not implemented */
		break;
	}
}

/**
 * mbox_channel_register() - Registers for a channel
 * @channel:	Channel Number.
 * @cb:		Pointer to function pointer mbox_channel_cb_t
 * @priv:	Pointer to private data
 *
 * This routine is used to register for a logical channel.
 * It first does sanity check on the requested channel availability
 * and parameters. Then it prepares internal entry for the channel.
 * And send a OPEN request for that channel.
 */
int mbox_channel_register(u16 channel, mbox_channel_cb_t *cb, void *priv)
{
	struct channel_status *rx_chan;
	struct list_head *pos, *rx_list;
	int res = 0;
	struct mbox_unit_status *mbox_unit;

	dev_dbg(&channels.pdev->dev, " %s channel = %d\n", __func__, channel);
	/* Check for callback fcn */
	if (cb == NULL) {
		dev_err(&channels.pdev->dev,
			"channel callback missing:channel %d\n", channel);
		res = -EINVAL;
		goto exit;
	}

	/* Check if provided channel number is valid */
	if (!check_channel(channel, MBOX_RX)) {
		dev_err(&channels.pdev->dev, "wrong mbox channel number %d\n",
			channel);
		res = -EINVAL;
		goto exit;
	}

	rx_list = get_rx_list(get_mbox_id(channel));
	if (rx_list == NULL) {
		dev_err(&channels.pdev->dev, "given mbox id is not valid\n");
		res = -EINVAL;
		goto exit;
	}

	mbox_unit = container_of(rx_list, struct mbox_unit_status, rx_chans);

	/* Check if channel is already registered */
	spin_lock(&mbox_unit->rx_lock);
	list_for_each(pos, rx_list) {
		rx_chan = list_entry(pos, struct channel_status, list);

		if (rx_chan->channel == channel) {
			dev_dbg(&channels.pdev->dev,
				"channel already registered\n");
			rx_chan->cb = cb;
			rx_chan->priv = priv;
			spin_unlock(&mbox_unit->rx_lock);
			goto exit;
		}
	}
	spin_unlock(&mbox_unit->rx_lock);

	rx_chan = kzalloc(sizeof(*rx_chan), GFP_KERNEL);
	if (rx_chan == NULL) {
		dev_err(&channels.pdev->dev,
			"couldn't allocate channel status\n");
		res = -ENOMEM;
		goto exit;
	}

	atomic_set(&rx_chan->rcv_counter, 0);
	/* Fill out newly allocated element and add it to rx list */
	rx_chan->channel = channel;
	rx_chan->cb = cb;
	rx_chan->priv = priv;
	rx_chan->seq_number = CHANNEL_START_SEQUENCE_NUMBER;
	mutex_init(&rx_chan->lock);
	INIT_LIST_HEAD(&rx_chan->rx.pending);
	rx_chan->cast_wq = create_singlethread_workqueue("mbox_cast_msg");
	if (!rx_chan->cast_wq) {
		dev_err(&channels.pdev->dev, "failed to create work queue\n");
		res = -ENOMEM;
		goto error_cast_wq;
	}
	rx_chan->receive_wq = create_singlethread_workqueue("mbox_receive_msg");
	if (!rx_chan->receive_wq) {
		dev_err(&channels.pdev->dev, "failed to create work queue\n");
		res = -ENOMEM;
		goto error_recv_wq;
	}
	INIT_WORK(&rx_chan->open_msg, mbox_handle_open_msg);
	INIT_WORK(&rx_chan->cast_msg, mbox_handle_cast_msg);
	INIT_WORK(&rx_chan->receive_msg, mbox_handle_receive_msg);
	spin_lock(&mbox_unit->rx_lock);
	list_add_tail(&rx_chan->list, rx_list);
	spin_unlock(&mbox_unit->rx_lock);

	mutex_lock(&rx_chan->lock);
	res = send_pdu(rx_chan, MBOX_OPEN, get_tx_channel(rx_chan->channel));
	if (res) {
		dev_err(&channels.pdev->dev, "failed to send OPEN command\n");
		spin_lock(&mbox_unit->rx_lock);
		list_del(&rx_chan->list);
		spin_unlock(&mbox_unit->rx_lock);
		mutex_unlock(&rx_chan->lock);
		goto error_send_pdu;
	} else {
		rx_chan->seq_number++;
		rx_chan->state = MBOX_OPEN;
		mutex_unlock(&rx_chan->lock);
		return res;
	}
error_send_pdu:
	flush_workqueue(rx_chan->receive_wq);
error_recv_wq:
	flush_workqueue(rx_chan->cast_wq);
error_cast_wq:
	kfree(rx_chan);
exit:
	return res;
}
EXPORT_SYMBOL(mbox_channel_register);

/**
 * mbox_channel_deregister() - DeRegisters for a channel
 * @channel:	Channel Number.
 *
 * This routine is used to deregister for a logical channel.
 * It first does sanity check on the requested channel availability
 * and parameters. Then it deletes the channel
 */
int mbox_channel_deregister(u16 channel)
{
	struct channel_status *rx_chan = NULL;
	struct list_head *pos, *rx_list;
	int res = 0;
	struct mbox_unit_status *mbox_unit;

	dev_dbg(&channels.pdev->dev, " %s channel = %d\n", __func__, channel);
	/* Check if provided channel number is valid */
	if (!check_channel(channel, MBOX_RX)) {
		dev_err(&channels.pdev->dev, "wrong mbox channel number %d\n",
			channel);
		res = -EINVAL;
		goto exit;
	}

	rx_list = get_rx_list(get_mbox_id(channel));
	if (rx_list == NULL) {
		dev_err(&channels.pdev->dev, "given mbox id is not valid\n");
		res = -EINVAL;
		goto exit;
	}

	mbox_unit = container_of(rx_list, struct mbox_unit_status, rx_chans);

	/* Check if channel is already registered */
	spin_lock(&mbox_unit->rx_lock);
	list_for_each(pos, rx_list) {
		rx_chan = list_entry(pos, struct channel_status, list);

		if (rx_chan->channel == channel) {
			dev_dbg(&channels.pdev->dev,
				"channel found\n");
			rx_chan->cb = NULL;
		}
	}
	list_del(&rx_chan->list);
	spin_unlock(&mbox_unit->rx_lock);
	flush_workqueue(rx_chan->cast_wq);
	flush_workqueue(rx_chan->receive_wq);
	kfree(rx_chan);

exit:
	return res;
}
EXPORT_SYMBOL(mbox_channel_deregister);

/**
 * mbox_channel_send() - Send messages
 * @msg:	Pointer to mbox_channel_msg data structure.
 *
 * This routine is used to send messages over the registered logical
 * TX channel. It first does sanity check on the message paramenters.
 * It registered channel is not found then it just registers for that
 * channel. If channel found, it puts the message to the pending list.
 * If channel is OPEN, it then pushes the message to the mailbox in
 * FIFO manner from the pending list.
 */
int mbox_channel_send(struct mbox_channel_msg *msg)
{
	struct list_head *pos, *tx_list;
	struct channel_status *tmp = NULL;
	struct channel_status *tx_chan = NULL;
	struct pending_elem *pending;
	struct mbox_unit_status *mbox_unit;
	int res = 0;

	if (msg->length > MAILBOX_NR_OF_DATAWORDS || msg->length == 0) {
		dev_err(&channels.pdev->dev, "data length incorrect\n");
		res = -EINVAL;
		goto exit;
	}

	if (!check_channel(msg->channel, MBOX_TX)) {
		dev_err(&channels.pdev->dev, "wrong channel number %d\n",
			msg->channel);
		res = -EINVAL;
		goto exit;
	}

	tx_list = get_tx_list(get_mbox_id(msg->channel));
	if (tx_list == NULL) {
		dev_err(&channels.pdev->dev, "given mbox id is not valid\n");
		res = -EINVAL;
		goto exit;
	}

	mbox_unit = container_of(tx_list, struct mbox_unit_status, tx_chans);

	spin_lock(&mbox_unit->tx_lock);
	dev_dbg(&channels.pdev->dev, "send:tx_list=%x\tmbox_unit=%x\n",
			(u32)tx_list, (u32)mbox_unit);
	list_for_each(pos, tx_list) {
		tmp = list_entry(pos, struct channel_status, list);
		if (tmp->channel == msg->channel)
			tx_chan = tmp;
	}
	spin_unlock(&mbox_unit->tx_lock);
	/* Allocate pending element and add it to the list */
	pending = kzalloc(sizeof(*pending), GFP_KERNEL);
	if (pending == NULL) {
		dev_err(&channels.pdev->dev,
			"couldn't allocate memory for pending\n");
		res = -ENOMEM;
		goto exit;
	}
	pending->data = msg->data;
	pending->length = msg->length;

	if (tx_chan) {
		mutex_lock(&tx_chan->lock);
		list_add_tail(&pending->list, &tx_chan->tx.pending);
		tx_chan->cb = msg->cb;
		tx_chan->priv = msg->priv;
		/* If channel is already opened start sending data */
		if (tx_chan->state == MBOX_OPEN)
			send_pdu(tx_chan, MBOX_CAST, tx_chan->channel);
		/* Stop processing here */
		mutex_unlock(&tx_chan->lock);
	} else {
		/* No channel found on the list, allocate new element */
		tx_chan = kzalloc(sizeof(*tx_chan), GFP_KERNEL);
		if (tx_chan == NULL) {
			dev_err(&channels.pdev->dev,
					"couldn't allocate memory for \
					tx_chan\n");
			res = -ENOMEM;
			goto exit;
		}
		tx_chan->channel = msg->channel;
		tx_chan->cb = msg->cb;
		tx_chan->priv = msg->priv;
		tx_chan->state = MBOX_CLOSE;
		tx_chan->seq_number = CHANNEL_START_SEQUENCE_NUMBER;
		INIT_LIST_HEAD(&tx_chan->tx.pending);
		INIT_WORK(&tx_chan->open_msg, mbox_handle_open_msg);
		INIT_WORK(&tx_chan->cast_msg, mbox_handle_cast_msg);
		INIT_WORK(&tx_chan->receive_msg, mbox_handle_receive_msg);
		mutex_init(&tx_chan->lock);
		spin_lock(&mbox_unit->tx_lock);
		list_add_tail(&tx_chan->list, tx_list);
		spin_unlock(&mbox_unit->tx_lock);
		mutex_lock(&tx_chan->lock);
		list_add_tail(&pending->list, &tx_chan->tx.pending);
		mutex_unlock(&tx_chan->lock);
	}
	return 0;

exit:
	return res;
}
EXPORT_SYMBOL(mbox_channel_send);

static void revoke_pending_msgs(struct channel_status *tx_chan)
{
	struct list_head *pos, *n;
	struct pending_elem *pending;

	list_for_each_safe(pos, n, &tx_chan->tx.pending) {
		pending = list_entry(pos, struct pending_elem, list);

		if (tx_chan->cb)
			tx_chan->cb(pending->data, pending->length,
							tx_chan->priv);
		else
			dev_err(&channels.pdev->dev,
				"%s no callback provided\n", __func__);
		list_del(&pending->list);
		kfree(pending);
	}
}

/**
 * mbox_channel_revoke_messages() - Revoke pending messages
 * @channel:	Channel on which action to be taken.
 *
 * This routine Clear all pending messages from TX channel
 * It searches for the channel.Checks if there is pending
 * messages.Calls if tehre is any registered function. And
 * deletes the messages for the pending list.
 */
int mbox_channel_revoke_messages(u16 channel)
{
	struct list_head *pos, *tx_list;
	struct channel_status *tmp;
	struct channel_status *tx_chan = NULL;
	struct mbox_unit_status *mbox_unit;
	int res = 0;

	if (!check_channel(channel, MBOX_TX)) {
		dev_err(&channels.pdev->dev,
			"wrong channel number %d\n", channel);
		return -EINVAL;
	}

	tx_list = get_tx_list(get_mbox_id(channel));
	if (tx_list == NULL) {
		dev_err(&channels.pdev->dev, "given mbox id is not valid\n");
		return -EINVAL;
	}

	mbox_unit = container_of(tx_list, struct mbox_unit_status, tx_chans);

	spin_lock(&mbox_unit->tx_lock);
	list_for_each(pos, tx_list) {
		tmp = list_entry(pos, struct channel_status, list);
		if (tmp->channel == channel)
			tx_chan = tmp;
	}
	spin_unlock(&mbox_unit->tx_lock);

	if (tx_chan) {
		mutex_lock(&tx_chan->lock);
		revoke_pending_msgs(tx_chan);
		mutex_unlock(&tx_chan->lock);
		dev_dbg(&channels.pdev->dev, "channel %d cleared\n",
			channel);
	} else {
		dev_err(&channels.pdev->dev, "no channel found\n");
		res = -EINVAL;
	}

	dev_dbg(&channels.pdev->dev, "%s exiting %d\n", __func__, res);
	return res;
}
EXPORT_SYMBOL(mbox_channel_revoke_messages);

#if defined(CONFIG_DEBUG_FS)
#define MBOXTEST_DEBUG 1
#ifdef MBOXTEST_DEBUG
#define DBG_TEST(x)  x
#else
#define DBG_TEST(x)
#endif

#define MBOX_TEST_MAX_WORDS	3
#define MBOX_RX_CHAN		0x500
#define MBOX_TX_RX_CHANNEL_DIFF 0x400
#define MBOX_MAX_NUM_TRANSFER	30000
static int registration_done;
/**
 * struct mboxtest_data - mbox test via debugfs information
 * @rx_buff:		Buffer for incomming data
 * @rx_pointer:		Ptr to actual RX data buff
 * @tx_buff:		Buffer for outgoing data
 * @tx_pointer:		Ptr to actual TX data buff
 * @tx_done:		TX Transfer done indicator
 * @rx_done:		RX Transfer done indicator
 * @received:		Received words
 * @xfer_words:		Num of bytes in actual trf
 * @xfers:		Number of transfers
 * @words:		Number of total words
 * @channel:		Channel test number
 */
struct mboxtest_data {
	unsigned int		*rx_buff;
	unsigned int		*rx_pointer;
	unsigned int		*tx_buff;
	unsigned int		*tx_pointer;
	struct completion	tx_done;
	struct completion	rx_done;
	int			received;
	int			xfer_words;
	int			xfers;
	int			words;
	int			channel;
};

static void mboxtest_receive_cb(u32 *data, u32 len, void *arg)
{
	struct mboxtest_data *mboxtest = (struct mboxtest_data *) arg;
	int i;

	printk(KERN_INFO "receive_cb.. data.= 0x%X, len = %d\n",
								*data, len);
	for (i = 0; i < len; i++)
		*(mboxtest->rx_pointer++) = *(data++);

	mboxtest->received += len;

	printk(KERN_INFO "received = %d, words = %d\n",
				mboxtest->received, mboxtest->words);
	if (mboxtest->received >= mboxtest->words)
		complete(&mboxtest->rx_done);
	dev_dbg(&channels.pdev->dev, "%s exiting\n", __func__);
}

static void mboxtest_send_cb(u32 *data, u32 len, void *arg)
{
	struct mboxtest_data *mboxtest = (struct mboxtest_data *) arg;

	printk(KERN_INFO "send_cb.. data.= 0x%X, len = %d\n",
							*data, len);

	complete(&mboxtest->tx_done);
	dev_dbg(&channels.pdev->dev, "kernel:mboxtest_send_cb exiting\n");
}

static int mboxtest_transmit(struct mboxtest_data *mboxtest)
{
	int status = 0;
	struct mbox_channel_msg msg;

	dev_dbg(&channels.pdev->dev, "%s entering\n", __func__);
	init_completion(&mboxtest->tx_done);

	msg.channel = mboxtest->channel;
	msg.data = mboxtest->tx_pointer;
	msg.length = mboxtest->words;
	msg.cb = mboxtest_send_cb;
	msg.priv = mboxtest;

	status = mbox_channel_send(&msg);
	if (!status) {
		mboxtest->tx_pointer += mboxtest->xfer_words;
		wait_for_completion(&mboxtest->tx_done);
	}

	dev_dbg(&channels.pdev->dev, "%s exiting %d\n",
			__func__, status);
	return status;
}

static int transfer_test(struct mboxtest_data *mboxtest)
{
	int status = 0;
	int len = 0;
	int i;

	len = mboxtest->words;

	dev_dbg(&channels.pdev->dev, "%s enterring\n", __func__);
	/* Allocate buffers */
	mboxtest->rx_buff = kzalloc(sizeof(unsigned int) * len, GFP_KERNEL);
	if (!mboxtest->rx_buff) {
		DBG_TEST(printk(KERN_INFO
			"Cannot allocate mbox rx memory\n"));
		status = -ENOMEM;
		goto err1;
	}
	memset(mboxtest->rx_buff, '\0', sizeof(unsigned int) * len);

	mboxtest->tx_buff = kzalloc(sizeof(unsigned int) * len, GFP_KERNEL);
	if (!mboxtest->tx_buff) {
		DBG_TEST(printk(KERN_INFO
			"Cannot allocate mbox tx memory\n"));
		status = -ENOMEM;
		goto err2;
	}
	memset(mboxtest->tx_buff, '\0', sizeof(unsigned int) * len);

	/* Generate data */
		get_random_bytes((unsigned char *)mboxtest->tx_buff,
				sizeof(unsigned int) * len);
	/* Set pointers */
	mboxtest->tx_pointer = mboxtest->tx_buff;
	mboxtest->rx_pointer = mboxtest->rx_buff;
	mboxtest->received = 0;
	init_completion(&mboxtest->rx_done);

	/* Start tx transfer test transfer */
	status = mboxtest_transmit(mboxtest);
	DBG_TEST(printk(KERN_INFO "xfer_words=%d\n",
				mboxtest->xfer_words));
	if (!status)
		wait_for_completion(&mboxtest->rx_done);
	for (i = 0; i < len; i++)
		DBG_TEST(printk(KERN_INFO "%d -> TX:0x%X, RX:0x%X\n", i,
			mboxtest->tx_buff[i], mboxtest->rx_buff[i]));

	dev_dbg(&channels.pdev->dev, "%s exiting %d\n", __func__, status);
	return status;
err2:
	kfree(mboxtest->rx_buff);
err1:
	return status;
}

static int mboxtest_prepare(struct mboxtest_data *mboxtest)
{
	int err = 0;

	mboxtest->xfers = MBOX_MAX_NUM_TRANSFER;
	/* Calculate number of bytes in each transfer */
	mboxtest->xfer_words = mboxtest->words / mboxtest->xfers;

	/* Trim to maxiumum data words per transfer */
	if (mboxtest->xfer_words > MBOX_TEST_MAX_WORDS) {
		DBG_TEST(printk(KERN_INFO "Recalculating xfers ...\n"));
		mboxtest->xfer_words = MBOX_TEST_MAX_WORDS;
		if (mboxtest->words % mboxtest->xfer_words)
			mboxtest->xfers = (mboxtest->words /
						mboxtest->xfer_words) + 1;
		else
			mboxtest->xfers = (mboxtest->words /
						mboxtest->xfer_words);
	}

	DBG_TEST(printk(KERN_INFO "Params: chan=0x%X words=%d, xfers=%d\n",
			mboxtest->channel, mboxtest->words,
						mboxtest->xfers));

	if (mbox_channel_register(mboxtest->channel,
			mboxtest_receive_cb, mboxtest)) {
		DBG_TEST(printk(KERN_INFO "Cannot register mbox channel\n"));
		err = -ENOMEM;
		goto err;
	}

	registration_done = true;
	return 0;
err:
	return err;
}

struct mboxtest_data mboxtest;
/*
 * Expected input: <nbr_channel> <nbr_word>
 * Example: "echo 500 2"
 */
static ssize_t mbox_write_channel(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	unsigned long nbr_channel;
	unsigned long nbr_word;
	char int_buf[16];
	char *token;
	char *val;

	strncpy((char *) &int_buf, buf, sizeof(int_buf));
	token = (char *) &int_buf;

	/* Parse message */
	val = strsep(&token, " ");
	if ((val == NULL) || (strict_strtoul(val, 16, &nbr_channel) != 0))
		nbr_channel = MBOX_RX_CHAN;

	val = strsep(&token, " ");
	if ((val == NULL) || (strict_strtoul(val, 16, &nbr_word) != 0))
		nbr_word = 2;

	dev_dbg(dev, "Will setup logical channel %ld\n", nbr_channel);
	mboxtest.channel = nbr_channel;
	mboxtest.words = nbr_word;

	if (!registration_done)
		mboxtest_prepare(&mboxtest);
	else
		dev_dbg(&channels.pdev->dev, "already registration done\n");

	return count;
}

static ssize_t mbox_read_channel(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{

	unsigned long i;
	static bool config_done;

	if (!config_done) {
		config_done = true;
		mboxtest.channel += MBOX_TX_RX_CHANNEL_DIFF;
	}
	dev_dbg(dev, "Will transfer %d words %d times at channel 0x%x\n",
			mboxtest.words, mboxtest.xfers, mboxtest.channel);
	for (i = 0; i < mboxtest.xfers; i++)
		transfer_test(&mboxtest);

	return 1;
}
static DEVICE_ATTR(channel, S_IWUGO | S_IRUGO, mbox_read_channel,
		mbox_write_channel);

#endif

static int __init mbox_channel_probe(struct platform_device *pdev)
{
	int i, ret = 0;
	struct mbox *mbox;

	dev_dbg(&(pdev->dev), "Probing mailbox (pdev = 0x%X)...\n", (u32)pdev);

	/* Register to given mailbox units (ids) */
	for (i = 0; i < ARRAY_SIZE(mbox_ids); i++) {
		mbox = mbox_setup(mbox_ids[i], mbox_cb, &mbox_ids[i]);
		if (mbox == NULL) {
			dev_err(&(pdev->dev), "Unable to setup mailbox %d\n",
				mbox_ids[i]);
			ret = -EBUSY;
			goto exit;
		}
		channels.mbox_unit[i].mbox_id = mbox_ids[i];
		channels.mbox_unit[i].mbox = mbox;
		INIT_LIST_HEAD(&channels.mbox_unit[i].rx_chans);
		INIT_LIST_HEAD(&channels.mbox_unit[i].tx_chans);
		spin_lock_init(&channels.mbox_unit[i].rx_lock);
		spin_lock_init(&channels.mbox_unit[i].tx_lock);
	}

	channels.pdev = pdev;

	dev_dbg(&(pdev->dev), "Mailbox channel driver loaded\n");
#if defined(CONFIG_DEBUG_FS)
	ret = device_create_file(&(pdev->dev), &dev_attr_channel);
	if (ret != 0)
		dev_warn(&(pdev->dev),
			 "Unable to create mbox_channel sysfs entry");


#endif
exit:
	return ret;
}

static struct platform_driver mbox_channel_driver = {
	.driver = {
		.name = "mbox_channel",
		.owner = THIS_MODULE,
	},
};

static int __init mbox_channel_init(void)
{
	if (!machine_is_u5500())
		return 0;

	platform_device_register_simple("mbox_channel", 0, NULL, 0);

	return platform_driver_probe(&mbox_channel_driver, mbox_channel_probe);
}
module_init(mbox_channel_init);

static void __exit mbox_channel_exit(void)
{
	platform_driver_unregister(&mbox_channel_driver);
}
module_exit(mbox_channel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MBOX channels driver");
