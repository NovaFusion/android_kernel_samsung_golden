/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com> for ST-Ericsson.
 *         Bibek Basu <bibek.basu@stericsson.com>
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __INC_MBOX_CHANNELS_H
#define __INC_MBOX_CHANNELS_H

/* Maximum number of datawords which can be send in one PDU */
#define MAILBOX_NR_OF_DATAWORDS	3

/* Number of buffers */
#define NUM_DSP_BUFFER		32

/**
 * mbox_channel_cb_t - Definition of the mailbox channel callback.
 * @data:	Pointer to the data.
 * @length:	Length of the data.
 * @priv:	The client's private data.
 *
 * This function will be called upon reception of complete mbox channel PDU
 * or after completion of send operation.
 */
typedef void mbox_channel_cb_t (u32 *data, u32 length, void *priv);

/**
 * struct mbox_channel_msg - Definition of mbox channel message
 * @channel:	Channel number.
 * @data:	Pointer to data to be sent.
 * @length:	Length of data to be sent.
 * @cb:	Pointer to the callback function to be called when send
 *		operation will be finished.
 * @priv:	The client's private data.
 *
 * This structure describes mailbox channel message.
 */
struct mbox_channel_msg {
	u16			channel;
	u32			*data;
	u8			length;
	mbox_channel_cb_t	*cb;
	void			*priv;
};

/**
 * mbox_channel_register - Set up a given mailbox channel.
 * @channel:	Mailbox channel number.
 * @cb:	Pointer to the callback function to be called when a new message
 *		is received.
 * @priv:	Client user data which will be returned in the callback.
 *
 * Returns 0 on success or a negative error code on error.
 */
int mbox_channel_register(u16 channel, mbox_channel_cb_t *cb, void *priv);

/**
 * mbox_channel_send - Send data on given mailbox channel.
 * @msg:	Mailbox channel message to be sent.
 *
 * Returns 0 on success or a negative error code on error.
 */
int mbox_channel_send(struct mbox_channel_msg *msg);

/**
 * mbox_channel_revoke_messages - Revoke messages on given mailbox channel.
 * @channel:	Mailbox channel number.
 *
 * Returns 0 on success or a negative error code on error.
 */
int mbox_channel_revoke_messages(u16 channel);

/**
 * mbox_channel_deregister - de-register given mailbox channel.
 * @channel:    Mailbox channel number.
 *
 * Returns 0 on success or a negative error code on error.
 */
int mbox_channel_deregister(u16 channel);

#endif /*INC_STE_MBOX_H*/
