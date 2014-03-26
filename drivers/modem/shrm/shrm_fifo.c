/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghavi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/modem/shrm/shrm.h>
#include <linux/modem/shrm/shrm_driver.h>
#include <linux/modem/shrm/shrm_private.h>
#include <linux/modem/shrm/shrm_net.h>
#include <linux/mfd/dbx500-prcmu.h>

#define L1_BOOT_INFO_REQ	1
#define L1_BOOT_INFO_RESP	2
#define L1_NORMAL_MSG		3
#define L1_HEADER_MASK		28
#define L1_MAPID_MASK		0xF0000000
#define CONFIG_OFFSET		8
#define COUNTER_OFFSET		20
#define L2_HEADER_SIZE		4
#define L2_HEADER_OFFSET	24
#define MASK_0_15_BIT		0xFF
#define MASK_16_31_BIT		0xFF00
#define MASK_16_27_BIT		0xFFF0000
#define MASK_0_39_BIT		0xFFFFF
#define MASK_40_55_BIT		0xFF00000
#define MASK_8_16_BIT           0x0000FF00
#define MSG_LEN_OFFSET          16
#define SHRM_VER                2
#define ca_ist_inactivity_timer 25 /*25ms */
#define ca_csc_inactivity_timer 25 /*25ms */

static u8 msg_audio_counter;
static u8 msg_common_counter;

struct fifo_write_params ape_shm_fifo_0;
struct fifo_write_params ape_shm_fifo_1;
struct fifo_read_params cmt_shm_fifo_0;
struct fifo_read_params cmt_shm_fifo_1;


static u8 cmt_read_notif_0_send;
static u8 cmt_read_notif_1_send;

void shm_fifo_init(struct shrm_dev *shrm)
{
	ape_shm_fifo_0.writer_local_wptr	= 0;
	ape_shm_fifo_0.writer_local_rptr	= 0;
	*((u32 *)shrm->ac_common_shared_wptr) = 0;
	*((u32 *)shrm->ac_common_shared_rptr) = 0;
	ape_shm_fifo_0.shared_wptr		= 0;
	ape_shm_fifo_0.shared_rptr		= 0;
	ape_shm_fifo_0.availablesize = shrm->ape_common_fifo_size;
	ape_shm_fifo_0.end_addr_fifo    = shrm->ape_common_fifo_size;
	ape_shm_fifo_0.fifo_virtual_addr = shrm->ape_common_fifo_base;
	spin_lock_init(&ape_shm_fifo_0.fifo_update_lock);


	cmt_shm_fifo_0.reader_local_rptr	= 0;
	cmt_shm_fifo_0.reader_local_wptr	= 0;
	cmt_shm_fifo_0.shared_wptr	=
			*((u32 *)shrm->ca_common_shared_wptr);
	cmt_shm_fifo_0.shared_rptr	=
			*((u32 *)shrm->ca_common_shared_rptr);
	cmt_shm_fifo_0.availablesize	= shrm->cmt_common_fifo_size;
	cmt_shm_fifo_0.end_addr_fifo	= shrm->cmt_common_fifo_size;
	cmt_shm_fifo_0.fifo_virtual_addr = shrm->cmt_common_fifo_base;

	ape_shm_fifo_1.writer_local_wptr	= 0;
	ape_shm_fifo_1.writer_local_rptr	= 0;
	ape_shm_fifo_1.shared_wptr		= 0;
	ape_shm_fifo_1.shared_rptr		= 0;
	*((u32 *)shrm->ac_audio_shared_wptr) = 0;
	*((u32 *)shrm->ac_audio_shared_rptr) = 0;
	ape_shm_fifo_1.availablesize = shrm->ape_audio_fifo_size;
	ape_shm_fifo_1.end_addr_fifo    = shrm->ape_audio_fifo_size;
	ape_shm_fifo_1.fifo_virtual_addr = shrm->ape_audio_fifo_base;
	spin_lock_init(&ape_shm_fifo_1.fifo_update_lock);

	cmt_shm_fifo_1.reader_local_rptr	= 0;
	cmt_shm_fifo_1.reader_local_wptr	= 0;
	cmt_shm_fifo_1.shared_wptr		=
			*((u32 *)shrm->ca_audio_shared_wptr);
	cmt_shm_fifo_1.shared_rptr		=
			*((u32 *)shrm->ca_audio_shared_rptr);
	cmt_shm_fifo_1.availablesize	= shrm->cmt_audio_fifo_size;
	cmt_shm_fifo_1.end_addr_fifo	= shrm->cmt_audio_fifo_size;
	cmt_shm_fifo_1.fifo_virtual_addr = shrm->cmt_audio_fifo_base;
	msg_audio_counter = 0;
	msg_common_counter = 0;
}

u8 read_boot_info_req(struct shrm_dev *shrm,
				u32 *config,
				u32 *version)
{
	struct fifo_read_params *fifo = &cmt_shm_fifo_0;
	u32 *msg;
	u32 header = 0;
	u8 msgtype;

	/* Read L1 header read content of reader_local_rptr */
	msg = (u32 *)
		(fifo->reader_local_rptr + fifo->fifo_virtual_addr);
	header = *msg;
	msgtype = (header & L1_MAPID_MASK) >> L1_MSG_MAPID_OFFSET;
	if (msgtype != L1_BOOT_INFO_REQ) {
		dev_err(shrm->dev, "Read_Boot_Info_Req Fatal ERROR\n");
		dev_err(shrm->dev, "Received msgtype is %d\n", msgtype);
		dev_info(shrm->dev, "Initiating a modem reset\n");
		queue_kthread_work(&shrm->shm_ac_wake_kw,
				&shrm->shm_mod_reset_req);
		return 0;
	}
	*config = (header >> CONFIG_OFFSET) & MASK_0_15_BIT;
	*version = header & MASK_0_15_BIT;
	fifo->reader_local_rptr += 1;

	return 1;
}

void write_boot_info_resp(struct shrm_dev *shrm, u32 config,
							u32 version)
{
	struct fifo_write_params *fifo = &ape_shm_fifo_0;
	u32 *msg;
	u8 msg_length;
	version = SHRM_VER;

	spin_lock_bh(&fifo->fifo_update_lock);
	/* Read L1 header read content of reader_local_rptr */
	msg = (u32 *)
		(fifo->writer_local_wptr+fifo->fifo_virtual_addr);
	if (version < 1)	{
		*msg = ((L1_BOOT_INFO_RESP << L1_MSG_MAPID_OFFSET) |
				((config << CONFIG_OFFSET) & MASK_16_31_BIT)
				| (version & MASK_0_15_BIT));
		msg_length = 1;
	} else {
		*msg = ((L1_BOOT_INFO_RESP << L1_MSG_MAPID_OFFSET) |
			((0x8 << MSG_LEN_OFFSET) & MASK_16_27_BIT) |
			((config << CONFIG_OFFSET) & MASK_8_16_BIT)|
			version);
		msg++;
		*msg = ca_ist_inactivity_timer;
		msg++;
		*msg = ca_csc_inactivity_timer;
		msg_length = L1_NORMAL_MSG;
	}
	fifo->writer_local_wptr += msg_length;
	fifo->availablesize -= msg_length;
	spin_unlock_bh(&fifo->fifo_update_lock);
}

/**
 * shm_write_msg_to_fifo() - write message to FIFO
 * @shrm:	pointer to shrm device information structure
 * @channel:	audio or common channel
 * @l2header:	L2 header or device ID
 * @addr:	pointer to write buffer address
 * @length:	length of mst to write
 *
 * Function Which Writes the data into Fifo in IPC zone
 * It is called from shm_write_msg. This function will copy the msg
 * from the kernel buffer to FIFO. There are 4 kernel buffers from where
 * the data is to copied to FIFO one for each of the messages ISI, RPC,
 * AUDIO and SECURITY. ISI, RPC and SECURITY messages are pushed to FIFO
 * in commmon channel and AUDIO message is pushed onto audio channel FIFO.
 */
int shm_write_msg_to_fifo(struct shrm_dev *shrm, u8 channel,
				u8 l2header, void *addr, u32 length)
{
	struct fifo_write_params *fifo = NULL;
	u32 l1_header = 0, l2_header = 0;
	u32 requiredsize;
	u32 size = 0;
	u32 *msg;
	u8 *src;

	if (channel == COMMON_CHANNEL)
		fifo = &ape_shm_fifo_0;
	else if (channel == AUDIO_CHANNEL)
		fifo = &ape_shm_fifo_1;
	else {
		dev_err(shrm->dev, "invalid channel\n");
		return -EINVAL;
	}

	/* L2 size in 32b */
	requiredsize = ((length + 3) / 4);
	/* Add size of L1 & L2 header */
	requiredsize += 2;

	/* if availablesize = or < requiredsize then error */
	if (fifo->availablesize <= requiredsize) {
		/* Fatal ERROR - should never happens */
		dev_dbg(shrm->dev, "wr_wptr= %x\n",
					fifo->writer_local_wptr);
		dev_dbg(shrm->dev, "wr_rptr= %x\n",
					fifo->writer_local_rptr);
		dev_dbg(shrm->dev, "shared_wptr= %x\n",
						fifo->shared_wptr);
		dev_dbg(shrm->dev, "shared_rptr= %x\n",
						fifo->shared_rptr);
		dev_dbg(shrm->dev, "availsize= %x\n",
						fifo->availablesize);
		dev_dbg(shrm->dev, "end__fifo= %x\n",
				fifo->end_addr_fifo);
		dev_warn(shrm->dev, "Modem is busy, please wait."
				" c_cnt = %d; a_cnt = %d\n", msg_common_counter,
				msg_audio_counter);
		if (channel == COMMON_CHANNEL) {
			dev_warn(shrm->dev,
					"Modem is lagging behind in reading."
					"Stopping n/w dev queue\n");
#ifdef CONFIG_U8500_SHRM_DEFAULT_NET
			shrm_stop_netdev(shrm->ndev);
#endif
		}

		return -EAGAIN;
	}

	if (channel == COMMON_CHANNEL) {
		/* build L1 header */
		l1_header = ((L1_NORMAL_MSG << L1_MSG_MAPID_OFFSET) |
				(((msg_common_counter++) << COUNTER_OFFSET)
				 & MASK_40_55_BIT) |
				((length + L2_HEADER_SIZE) & MASK_0_39_BIT));
	} else if (channel == AUDIO_CHANNEL) {
		/* build L1 header */
		l1_header = ((L1_NORMAL_MSG << L1_MSG_MAPID_OFFSET) |
				(((msg_audio_counter++) << COUNTER_OFFSET)
				 & MASK_40_55_BIT) |
				((length + L2_HEADER_SIZE) & MASK_0_39_BIT));
	}

	/*
	 * Need to take care race condition for fifo->availablesize
	 * & fifo->writer_local_rptr with Ac_Read_notification interrupt.
	 * One option could be use stack variable for LocalRptr and recompute
	 * fifo->availablesize,based on flag enabled in the
	 * Ac_read_notification
	 */
	l2_header = ((l2header << L2_HEADER_OFFSET) |
					((length) & MASK_0_39_BIT));
	spin_lock_bh(&fifo->fifo_update_lock);
	/* Check Local Rptr is less than or equal to Local WPtr */
	if (fifo->writer_local_rptr <= fifo->writer_local_wptr) {
		msg = (u32 *)
			(fifo->fifo_virtual_addr+fifo->writer_local_wptr);

		/* check enough place bewteen writer_local_wptr & end of FIFO */
		if ((fifo->end_addr_fifo-fifo->writer_local_wptr) >=
							requiredsize) {
			/* Add L1 header and L2 header */
			*msg = l1_header;
			msg++;
			*msg = l2_header;
			msg++;

			/* copy the l2 message in 1 memcpy */
			memcpy((void *)msg, addr, length);
			/* UpdateWptr */
			fifo->writer_local_wptr += requiredsize;
			fifo->availablesize -= requiredsize;
			fifo->writer_local_wptr %= fifo->end_addr_fifo;
		} else {
			/*
			 * message is split between and of FIFO and beg of FIFO
			 * copy first part from writer_local_wptr to end of FIFO
			 */
			size = fifo->end_addr_fifo-fifo->writer_local_wptr;

			if (size == 1) {
				/* Add L1 header */
				*msg = l1_header;
				msg++;
				/* UpdateWptr */
				fifo->writer_local_wptr = 0;
				fifo->availablesize -= size;
				/*
				 * copy second part from beg of FIFO
				 * with remaining part of msg
				 */
				msg =	(u32 *)
						fifo->fifo_virtual_addr;
				*msg = l2_header;
				msg++;

				/* copy the l3 message in 1 memcpy */
				memcpy((void *)msg, addr, length);
				/* UpdateWptr */
				fifo->writer_local_wptr +=
							requiredsize-size;
				fifo->availablesize -=
							(requiredsize-size);
			} else if (size == 2) {
				/* Add L1 header and L2 header */
				*msg = l1_header;
				msg++;
				*msg = l2_header;
				msg++;

				/* UpdateWptr */
				fifo->writer_local_wptr = 0;
				fifo->availablesize -= size;

				/*
				 * copy second part from beg of FIFO
				 * with remaining part of msg
				 */
				msg =	(u32 *)
						fifo->fifo_virtual_addr;
				/* copy the l3 message in 1 memcpy */
				memcpy((void *)msg, addr, length);

				/* UpdateWptr */
				fifo->writer_local_wptr +=
							requiredsize-size;
				fifo->availablesize -=
							(requiredsize-size);
			} else {
				/* Add L1 header and L2 header */
				*msg = l1_header;
				msg++;
				*msg = l2_header;
				msg++;

				/* copy the l2 message in 1 memcpy */
				memcpy((void *)msg, addr, (size-2)*4);


				/* UpdateWptr */
				fifo->writer_local_wptr = 0;
				fifo->availablesize -= size;

				/*
				 * copy second part from beg of FIFO
				 * with remaining part of msg
				 */
				msg = (u32 *)fifo->fifo_virtual_addr;
				src = (u8 *)addr+((size - 2) * 4);
				memcpy((void *)msg, src,
						(length-((size - 2) * 4)));

				/* UpdateWptr */
				fifo->writer_local_wptr +=
							requiredsize-size;
				fifo->availablesize -=
							(requiredsize-size);
			}

		}
	} else {
		/* writer_local_rptr > writer_local_wptr */
		msg = (u32 *)
			(fifo->fifo_virtual_addr+fifo->writer_local_wptr);
		/* Add L1 header and L2 header */
		*msg = l1_header;
		msg++;
		*msg = l2_header;
		msg++;
		/*
		 * copy message possbile between writer_local_wptr up
		 * to writer_local_rptr copy the l3 message in 1 memcpy
		 */
		memcpy((void *)msg, addr, length);

		/* UpdateWptr */
		fifo->writer_local_wptr += requiredsize;
		fifo->availablesize -= requiredsize;

	}
	spin_unlock_bh(&fifo->fifo_update_lock);
	return length;
}

/**
 * read_one_l2msg_common() - read message from common channel
 * @shrm:	pointer to shrm device information structure
 * @l2_msg:	pointer to the read L2 message buffer
 * @len:	message length
 *
 * This function read one message from the FIFO  and returns l2 header type
 */
u8 read_one_l2msg_common(struct shrm_dev *shrm,
			u8 *l2_msg, u32 *len)
{
	struct fifo_read_params *fifo = &cmt_shm_fifo_0;

	u32 *msg;
	u32 l1_header = 0;
	u32 l2_header = 0;
	u32 length;
	u8 msgtype;
	u32 msg_size;
	u32 size = 0;

	/* Read L1 header read content of reader_local_rptr */
	msg = (u32 *)
		(fifo->reader_local_rptr+fifo->fifo_virtual_addr);
	l1_header = *msg++;
	msgtype = (l1_header & 0xF0000000) >> L1_HEADER_MASK;

	if (msgtype != L1_NORMAL_MSG) {
		/* Fatal ERROR - should never happens */
		dev_info(shrm->dev, "wr_wptr= %x\n",
						fifo->reader_local_wptr);
		dev_info(shrm->dev, "wr_rptr= %x\n",
						fifo->reader_local_rptr);
		dev_info(shrm->dev, "shared_wptr= %x\n",
						fifo->shared_wptr);
		dev_info(shrm->dev, "shared_rptr= %x\n",
						fifo->shared_rptr);
		dev_info(shrm->dev, "availsize= %x\n",
						fifo->availablesize);
		dev_info(shrm->dev, "end_fifo= %x\n",
						fifo->end_addr_fifo);
		/* Fatal ERROR - should never happens */
		dev_crit(shrm->dev, "Fatal ERROR - should never happen\n");
		dev_info(shrm->dev, "Initiating a modem reset\n");
		queue_kthread_work(&shrm->shm_ac_wake_kw,
				&shrm->shm_mod_reset_req);
	 }
	if (fifo->reader_local_rptr == (fifo->end_addr_fifo-1)) {
		l2_header = (*((u32 *)fifo->fifo_virtual_addr));
		length = l2_header & MASK_0_39_BIT;
	} else {
		/* Read L2 header,Msg size & content of reader_local_rptr */
		l2_header = *msg;
		length = l2_header & MASK_0_39_BIT;
	}

	*len = length;
	msg_size = ((length + 3) / 4);
	msg_size += 2;

	if (fifo->reader_local_rptr + msg_size <=
						fifo->end_addr_fifo) {
		/* Skip L2 header */
		msg++;

		/* read msg between reader_local_rptr and end of FIFO */
		memcpy((void *)l2_msg, (void *)msg, length);
		/* UpdateLocalRptr */
		fifo->reader_local_rptr += msg_size;
		fifo->reader_local_rptr %= fifo->end_addr_fifo;
	} else {
		/*
		 * msg split between end of FIFO and beg copy first
		 * part of msg read msg between reader_local_rptr
		 * and end of FIFO
		 */
		size = fifo->end_addr_fifo-fifo->reader_local_rptr;
		if (size == 1) {
			msg = (u32 *)(fifo->fifo_virtual_addr);
			/* Skip L2 header */
			msg++;
			memcpy((void *)l2_msg, (void *)(msg), length);
		} else if (size == 2) {
			/* Skip L2 header */
			msg++;
			msg = (u32 *)(fifo->fifo_virtual_addr);
			memcpy((void *)l2_msg,
						(void *)(msg), length);
		} else {
			/* Skip L2 header */
			msg++;
			memcpy((void *)l2_msg, (void *)msg, ((size - 2) * 4));
			/* copy second part of msg */
			l2_msg += ((size - 2) * 4);
			msg = (u32 *)(fifo->fifo_virtual_addr);
			memcpy((void *)l2_msg, (void *)(msg),
						(length-((size - 2) * 4)));
		}
		fifo->reader_local_rptr =
			(fifo->reader_local_rptr+msg_size) %
				fifo->end_addr_fifo;
	}
	return (l2_header>>L2_HEADER_OFFSET) & MASK_0_15_BIT;
 }

u8 read_remaining_messages_common()
{
	struct fifo_read_params *fifo = &cmt_shm_fifo_0;
	/*
	 * There won't be any Race condition reader_local_rptr &
	 * fifo->reader_local_wptr with CaMsgpending Notification Interrupt
	 */
	return ((fifo->reader_local_rptr != fifo->reader_local_wptr) ? 1 : 0);
}

u8 read_one_l2msg_audio(struct shrm_dev *shrm,
				u8 *l2_msg, u32 *len)
{
	struct fifo_read_params *fifo = &cmt_shm_fifo_1;

	u32 *msg;
	u32 l1_header = 0;
	u32 l2_header = 0;
	u32 length;
	u8 msgtype;
	u32 msg_size;
	u32 size = 0;

	/* Read L1 header read content of reader_local_rptr */
	 msg = (u32 *)
			(fifo->reader_local_rptr+fifo->fifo_virtual_addr);
	 l1_header = *msg++;
	 msgtype = (l1_header & 0xF0000000) >> L1_HEADER_MASK;

	if (msgtype != L1_NORMAL_MSG) {
		/* Fatal ERROR - should never happens */
		dev_info(shrm->dev, "wr_local_wptr= %x\n",
						fifo->reader_local_wptr);
		dev_info(shrm->dev, "wr_local_rptr= %x\n",
						fifo->reader_local_rptr);
		dev_info(shrm->dev, "shared_wptr= %x\n",
						fifo->shared_wptr);
		dev_info(shrm->dev, "shared_rptr= %x\n",
						fifo->shared_rptr);
		dev_info(shrm->dev, "availsize=%x\n",
						fifo->availablesize);
		dev_info(shrm->dev, "end_fifo= %x\n",
						fifo->end_addr_fifo);
		dev_info(shrm->dev, "Received msgtype is %d\n", msgtype);
		/* Fatal ERROR - should never happens */
		dev_crit(shrm->dev, "Fatal ERROR - should never happen\n");
		dev_info(shrm->dev, "Initiating a modem reset\n");
		queue_kthread_work(&shrm->shm_ac_wake_kw,
				&shrm->shm_mod_reset_req);
	 }
	if (fifo->reader_local_rptr == (fifo->end_addr_fifo-1)) {
		l2_header = (*((u32 *)fifo->fifo_virtual_addr));
		length = l2_header & MASK_0_39_BIT;
	} else {
		/* Read L2 header,Msg size & content of reader_local_rptr */
		l2_header = *msg;
		length = l2_header & MASK_0_39_BIT;
	}

	*len = length;
	msg_size = ((length + 3) / 4);
	msg_size += 2;

	if (fifo->reader_local_rptr + msg_size <=
						fifo->end_addr_fifo) {
		/* Skip L2 header */
		msg++;
		/* read msg between reader_local_rptr and end of FIFO */
		memcpy((void *)l2_msg, (void *)msg, length);
		/* UpdateLocalRptr */
		fifo->reader_local_rptr += msg_size;
		fifo->reader_local_rptr %= fifo->end_addr_fifo;
	} else {

		/*
		 * msg split between end of FIFO and beg
		 * copy first part of msg
		 * read msg between reader_local_rptr and end of FIFO
		 */
		size = fifo->end_addr_fifo-fifo->reader_local_rptr;
		if (size == 1) {
			msg = (u32 *)(fifo->fifo_virtual_addr);
			/* Skip L2 header */
			msg++;
			memcpy((void *)l2_msg, (void *)(msg), length);
		} else if (size == 2) {
			/* Skip L2 header */
			msg++;
			msg = (u32 *)(fifo->fifo_virtual_addr);
			memcpy((void *)l2_msg, (void *)(msg), length);
		} else {
			/* Skip L2 header */
			msg++;
			memcpy((void *)l2_msg, (void *)msg, ((size - 2) * 4));
			/* copy second part of msg */
			l2_msg += ((size - 2) * 4);
			msg = (u32 *)(fifo->fifo_virtual_addr);
			memcpy((void *)l2_msg, (void *)(msg),
						(length-((size - 2) * 4)));
		}
		fifo->reader_local_rptr =
			(fifo->reader_local_rptr+msg_size) %
			fifo->end_addr_fifo;

	}
	return (l2_header>>L2_HEADER_OFFSET) & MASK_0_15_BIT;
 }

u8 read_remaining_messages_audio()
{
	struct fifo_read_params *fifo = &cmt_shm_fifo_1;

	return ((fifo->reader_local_rptr != fifo->reader_local_wptr) ?
									1 : 0);
}

u8 is_the_only_one_unread_message(struct shrm_dev *shrm,
						u8 channel, u32 length)
{
	struct fifo_write_params *fifo = NULL;
	u32 messagesize = 0;
	u8 is_only_one_unread_msg = 0;

	if (channel == COMMON_CHANNEL)
		fifo = &ape_shm_fifo_0;
	else /* channel = AUDIO_CHANNEL */
		fifo = &ape_shm_fifo_1;

	/* L3 size in 32b */
	messagesize = ((length + 3) / 4);
	/* Add size of L1 & L2 header */
	messagesize += 2;
	/*
	 * possibility of race condition with Ac Read notification interrupt.
	 * need to check ?
	 */
	if (fifo->writer_local_wptr > fifo->writer_local_rptr)
		is_only_one_unread_msg =
			((fifo->writer_local_rptr + messagesize) ==
			fifo->writer_local_wptr) ? 1 : 0;
	else
		/* Msg split between end of fifo and starting of Fifo */
		is_only_one_unread_msg =
			(((fifo->writer_local_rptr + messagesize) %
			fifo->end_addr_fifo) == fifo->writer_local_wptr) ?
									1 : 0;

	return is_only_one_unread_msg;
}

void update_ca_common_local_wptr(struct shrm_dev *shrm)
{
	/*
	 * update CA common reader local write pointer with the
	 * shared write pointer
	 */
	struct fifo_read_params *fifo = &cmt_shm_fifo_0;

	fifo->shared_wptr =
		(*((u32 *)shrm->ca_common_shared_wptr));
	fifo->reader_local_wptr = fifo->shared_wptr;
}

void update_ca_audio_local_wptr(struct shrm_dev *shrm)
{
	/*
	 * update CA audio reader local write pointer with the
	 * shared write pointer
	 */
	struct fifo_read_params *fifo = &cmt_shm_fifo_1;

	fifo->shared_wptr =
		(*((u32 *)shrm->ca_audio_shared_wptr));
	fifo->reader_local_wptr = fifo->shared_wptr;
}

extern void log_this(u8 pc, char* a, u32 extra1, char* b, u32 extra2);

void update_ac_common_local_rptr(struct shrm_dev *shrm)
{
	/*
	 * update AC common writer local read pointer with the
	 * shared read pointer
	 */
	struct fifo_write_params *fifo;
	u32 free_space = 0;

	fifo = &ape_shm_fifo_0;

	spin_lock_bh(&fifo->fifo_update_lock);
	fifo->shared_rptr =
		(*((u32 *)shrm->ac_common_shared_rptr));

	if (fifo->shared_rptr >= fifo->writer_local_rptr)
		free_space =
			(fifo->shared_rptr-fifo->writer_local_rptr);
	else {
		free_space =
			(fifo->end_addr_fifo-fifo->writer_local_rptr);
		free_space += fifo->shared_rptr;
	}

	/* Chance of race condition of below variables with write_msg */
	fifo->availablesize += free_space;
	fifo->writer_local_rptr = fifo->shared_rptr;
	log_this(82, "rptr", fifo->shared_rptr, NULL, 0);
	trace_printk("rptr : 0x%04x\n", fifo->shared_rptr);
	spin_unlock_bh(&fifo->fifo_update_lock);
}

void update_ac_audio_local_rptr(struct shrm_dev *shrm)
{
	/*
	 * update AC audio writer local read pointer with the
	 * shared read pointer
	 */
	struct fifo_write_params *fifo;
	u32 free_space = 0;

	fifo = &ape_shm_fifo_1;
	spin_lock_bh(&fifo->fifo_update_lock);
	fifo->shared_rptr =
		(*((u32 *)shrm->ac_audio_shared_rptr));

	if (fifo->shared_rptr >= fifo->writer_local_rptr)
		free_space =
			(fifo->shared_rptr-fifo->writer_local_rptr);
	else {
		free_space =
			(fifo->end_addr_fifo-fifo->writer_local_rptr);
		free_space += fifo->shared_rptr;
	}

	/* Chance of race condition of below variables with write_msg */
	fifo->availablesize += free_space;
	fifo->writer_local_rptr = fifo->shared_rptr;
	log_this(80, "rptr", fifo->shared_rptr, NULL, 0);
	trace_printk("rptr : 0x%04x\n", fifo->shared_rptr);
	spin_unlock_bh(&fifo->fifo_update_lock);
}

void update_ac_common_shared_wptr(struct shrm_dev *shrm)
{
	/*
	 * update AC common shared write pointer with the
	 * local write pointer
	 */
	struct fifo_write_params *fifo;

	fifo = &ape_shm_fifo_0;

	spin_lock_bh(&fifo->fifo_update_lock);
	/* Update shared pointer fifo offset of the IPC zone */
	(*((u32 *)shrm->ac_common_shared_wptr)) =
						fifo->writer_local_wptr;

	fifo->shared_wptr = fifo->writer_local_wptr;
	log_this(83, "wptr", fifo->shared_wptr, NULL, 0);
	trace_printk("wptr : 0x%04x\n", fifo->shared_wptr);
	spin_unlock_bh(&fifo->fifo_update_lock);
}

void update_ac_audio_shared_wptr(struct shrm_dev *shrm)
{
	/*
	 * update AC audio shared write pointer with the
	 * local write pointer
	 */
	struct fifo_write_params *fifo;

	fifo = &ape_shm_fifo_1;
	spin_lock_bh(&fifo->fifo_update_lock);
	/* Update shared pointer fifo offset of the IPC zone */
	(*((u32 *)shrm->ac_audio_shared_wptr)) =
						fifo->writer_local_wptr;
	fifo->shared_wptr = fifo->writer_local_wptr;
	log_this(81, "wptr", fifo->shared_wptr, NULL, 0);
	trace_printk("wptr : 0x%04x\n", fifo->shared_wptr);
	spin_unlock_bh(&fifo->fifo_update_lock);
}

void update_ca_common_shared_rptr(struct shrm_dev *shrm)
{
	/*
	 * update CA common shared read pointer with the
	 * local read pointer
	 */
	struct fifo_read_params *fifo;

	fifo = &cmt_shm_fifo_0;

	/* Update shared pointer fifo offset of the IPC zone */
	(*((u32 *)shrm->ca_common_shared_rptr)) =
						fifo->reader_local_rptr;
	fifo->shared_rptr = fifo->reader_local_rptr;
}

void update_ca_audio_shared_rptr(struct shrm_dev *shrm)
{
	/*
	 * update CA audio shared read pointer with the
	 * local read pointer
	 */
	struct fifo_read_params *fifo;

	fifo = &cmt_shm_fifo_1;

	/* Update shared pointer fifo offset of the IPC zone */
	(*((u32 *)shrm->ca_audio_shared_rptr)) =
						fifo->reader_local_rptr;
	fifo->shared_rptr = fifo->reader_local_rptr;
}

void get_reader_pointers(u8 channel_type, u32 *reader_local_rptr,
				u32 *reader_local_wptr, u32 *shared_rptr)
{
	struct fifo_read_params *fifo = NULL;

	if (channel_type == COMMON_CHANNEL)
		fifo = &cmt_shm_fifo_0;
	else /* channel_type = AUDIO_CHANNEL */
		fifo = &cmt_shm_fifo_1;

	*reader_local_rptr = fifo->reader_local_rptr;
	*reader_local_wptr = fifo->reader_local_wptr;
	*shared_rptr = fifo->shared_rptr;
}

void get_writer_pointers(u8 channel_type, u32 *writer_local_rptr,
			 u32 *writer_local_wptr, u32 *shared_wptr)
{
	struct fifo_write_params *fifo = NULL;

	if (channel_type == COMMON_CHANNEL)
		fifo = &ape_shm_fifo_0;
	else /* channel_type = AUDIO_CHANNEL */
		fifo = &ape_shm_fifo_1;

	spin_lock_bh(&fifo->fifo_update_lock);
	*writer_local_rptr = fifo->writer_local_rptr;
	*writer_local_wptr = fifo->writer_local_wptr;
	*shared_wptr = fifo->shared_wptr;
	spin_unlock_bh(&fifo->fifo_update_lock);
}

void set_ca_msg_0_read_notif_send(u8 val)
{
	cmt_read_notif_0_send = val;
}

u8 get_ca_msg_0_read_notif_send(void)
{
	return cmt_read_notif_0_send;
}

void set_ca_msg_1_read_notif_send(u8 val)
{
	cmt_read_notif_1_send = val;
}

u8 get_ca_msg_1_read_notif_send(void)
{
	return cmt_read_notif_1_send;
}
