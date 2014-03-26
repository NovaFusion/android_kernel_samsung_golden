/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#define DEBUG

#include <linux/err.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/modem/shrm/shrm_driver.h>
#include <linux/modem/shrm/shrm_private.h>
#include <linux/modem/shrm/shrm_config.h>
#include <linux/modem/shrm/shrm.h>

#include <mach/isa_ioctl.h>


#ifdef CONFIG_HIGH_RES_TIMERS
#include <linux/hrtimer.h>
static struct hrtimer timer;
#endif


#define NAME "IPC_ISA"
#define ISA_DEVICES 4
/**debug functionality*/
#define ISA_DEBUG 0

#define ISI_MESSAGING (0)
#define RPC_MESSAGING (1)
#define AUDIO_MESSAGING (2)
#define SECURITY_MESSAGING (3)

#define SIZE_OF_FIFO (512*1024)

static u8 message_fifo[4][SIZE_OF_FIFO];

static u8 wr_isi_msg[10*1024];
static u8 wr_rpc_msg[10*1024];
static u8 wr_sec_msg[10*1024];
static u8 wr_audio_msg[10*1024];

/* global data */
/*
 * int major:This variable is exported to user as module_param to specify
 * major number at load time
 */
static int major;
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");
/* global fops mutex */
static DEFINE_MUTEX(isa_lock);
rx_cb common_rx;
rx_cb audio_rx;


static int isi_receive(struct shrm_dev *shrm, void *data, u32 n_bytes);
static int rpc_receive(struct shrm_dev *shrm, void *data, u32 n_bytes);
static int audio_receive(struct shrm_dev *shrm, void *data, u32 n_bytes);
static int security_receive(struct shrm_dev *shrm,
						void *data, u32 n_bytes);

static void rx_common_l2msg_handler(u8 l2_header,
				 void *msg, u32 length,
				 struct shrm_dev *shrm)
{
	int ret = 0;
#ifdef CONFIG_U8500_SHRM_LOOP_BACK
	u8 *pdata;
#endif
     dev_dbg(shrm->dev, "%s IN\n", __func__);

	switch (l2_header) {
	case ISI_MESSAGING:
		ret = isi_receive(shrm, msg, length);
		if (ret < 0)
			dev_err(shrm->dev, "isi receive failed\n");
		break;
	case RPC_MESSAGING:
		ret  = rpc_receive(shrm, msg, length);
		if (ret < 0)
			dev_err(shrm->dev, "rpc receive failed\n");
		break;
	case SECURITY_MESSAGING:
		ret = security_receive(shrm, msg, length);
		if (ret < 0)
			dev_err(shrm->dev,
					"security receive failed\n");
		break;
#ifdef CONFIG_U8500_SHRM_LOOP_BACK
	case COMMMON_LOOPBACK_MESSAGING:
		pdata = (u8 *)msg;
		if ((*pdata == 0x50) || (*pdata == 0xAF)) {
			ret = isi_receive(shrm, msg, length);
			if (ret < 0)
				dev_err(shrm->dev, "isi receive failed\n");
		} else if ((*pdata == 0x0A) || (*pdata == 0xF5)) {
			ret = rpc_receive(shrm, msg, length);
			if (ret < 0)
				dev_err(shrm->dev, "rpc receive failed\n");
		} else if ((*pdata == 0xFF) || (*pdata == 0x00)) {
			ret = security_receive(shrm, msg, length);
			if (ret < 0)
				dev_err(shrm->dev,
						"security receive failed\n");
		}
		break;
#endif
	default:
		break;
	}
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

static void rx_audio_l2msg_handler(u8 l2_header,
				void *msg, u32 length,
				struct shrm_dev *shrm)
{
	int ret = 0;

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	audio_receive(shrm, msg, length);
	if (ret < 0)
		dev_err(shrm->dev, "audio receive failed\n");
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

static int __init shm_initialise_irq(struct shrm_dev *shrm)
{
	int err = 0;

	shrm_protocol_init(shrm,
			rx_common_l2msg_handler, rx_audio_l2msg_handler);

	err = request_irq(shrm->ca_wake_irq,
			ca_wake_irq_handler, IRQF_TRIGGER_RISING,
				 "ca_wake-up", shrm);
	if (err < 0) {
		dev_err(shrm->dev,
				"Unable to allocate shm tx interrupt line\n");
		return err;
	}

	err = request_irq(shrm->ac_read_notif_0_irq,
			ac_read_notif_0_irq_handler, 0,
			"ac_read_notif_0", shrm);
	if (err < 0) {
		dev_err(shrm->dev,
				"error ac_read_notif_0_irq interrupt line\n");
		goto irq_err1;
	}

	err = request_irq(shrm->ac_read_notif_1_irq,
			ac_read_notif_1_irq_handler, 0,
			"ac_read_notif_1", shrm);
	if (err < 0) {
		dev_err(shrm->dev,
				"error ac_read_notif_1_irq interrupt line\n");
		goto irq_err2;
	}

	err = request_irq(shrm->ca_msg_pending_notif_0_irq,
			 ca_msg_pending_notif_0_irq_handler, 0,
			"ca_msg_pending_notif_0", shrm);
	if (err < 0) {
		dev_err(shrm->dev,
				"error  ca_msg_pending_notif_0_irq line\n");
		goto irq_err3;
	}

	err = request_irq(shrm->ca_msg_pending_notif_1_irq,
			 ca_msg_pending_notif_1_irq_handler, 0,
			"ca_msg_pending_notif_1", shrm);
	if (err < 0) {
		dev_err(shrm->dev,
			"error ca_msg_pending_notif_1_irq interrupt line\n");
		goto irq_err4;
	}

	return err;

irq_err4:
	free_irq(shrm->ca_msg_pending_notif_0_irq, shrm);
irq_err3:
	free_irq(shrm->ac_read_notif_1_irq, shrm);
irq_err2:
	free_irq(shrm->ac_read_notif_0_irq, shrm);
irq_err1:
	free_irq(shrm->ca_wake_irq, shrm);
	return err;
}

static void free_shm_irq(struct shrm_dev *shrm)
{
	free_irq(shrm->ca_wake_irq, shrm);
	free_irq(shrm->ac_read_notif_0_irq, shrm);
	free_irq(shrm->ac_read_notif_1_irq, shrm);
	free_irq(shrm->ca_msg_pending_notif_0_irq, shrm);
	free_irq(shrm->ca_msg_pending_notif_1_irq, shrm);
}

/**
 * create_queue() - To create FIFO for Tx and Rx message buffering.
 * @q: message queue.
 * @devicetype: device type 0-isi,1-rpc,2-audio,3-security.
 *
 * This function creates a FIFO buffer of n_bytes size using
 * dma_alloc_coherent(). It also initializes all queue handling
 * locks, queue management pointers. It also initializes message list
 * which occupies this queue.
 *
 * It return -ENOMEM in case of no memory.
 */
static int create_queue(struct message_queue *q, u32 devicetype,
					struct shrm_dev *shrm)
{
	q->fifo_base = (u8 *)&message_fifo[devicetype];
	q->size = SIZE_OF_FIFO;
	q->readptr = 0;
	q->writeptr = 0;
	q->no = 0;
	q->shrm = shrm;
	spin_lock_init(&q->update_lock);
	INIT_LIST_HEAD(&q->msg_list);
	init_waitqueue_head(&q->wq_readable);
	atomic_set(&q->q_rp, 0);

	return 0;
}
/**
 * delete_queue() - To delete FIFO and assiciated memory.
 * @q: message queue
 *
 * This function deletes FIFO created using create_queue() function.
 * It resets queue management pointers.
 */
static void delete_queue(struct message_queue *q)
{
	q->size = 0;
	q->readptr = 0;
	q->writeptr = 0;
}

/**
 * add_msg_to_queue() - Add a message inside inside queue
 *
 * @q: message queue
 * @size: size in bytes
 *
 * This function tries to allocate n_bytes of size in FIFO q.
 * It returns negative number when no memory can be allocated
 * currently.
 */
int add_msg_to_queue(struct message_queue *q, u32 size)
{
	struct queue_element *new_msg = NULL;
	struct shrm_dev *shrm = q->shrm;

	dev_dbg(shrm->dev, "%s IN q->writeptr=%d\n",
						__func__, q->writeptr);
	new_msg = kmalloc(sizeof(struct queue_element),
						 GFP_KERNEL|GFP_ATOMIC);

	if (new_msg == NULL) {
		dev_err(shrm->dev, "memory overflow inside while(1)\n");
		return -ENOMEM;
	}
	new_msg->offset = q->writeptr;
	new_msg->size = size;
	new_msg->no = q->no++;

	/* check for overflow condition */
	if (q->readptr <= q->writeptr) {
		if (((q->writeptr-q->readptr) + size) >= q->size) {
			dev_err(shrm->dev, "Buffer overflow !!\n");
			BUG_ON(((q->writeptr-q->readptr) + size) >= q->size);
		}
	} else {
		if ((q->writeptr + size) >= q->readptr) {
			dev_err(shrm->dev, "Buffer overflow !!\n");
			BUG_ON((q->writeptr + size) >= q->readptr);
		}
	}
	q->writeptr = (q->writeptr + size) % q->size;
	if (list_empty(&q->msg_list)) {
		list_add_tail(&new_msg->entry, &q->msg_list);
		/* There can be 2 blocking calls read  and another select */

		atomic_set(&q->q_rp, 1);
		wake_up_interruptible(&q->wq_readable);
	} else
		list_add_tail(&new_msg->entry, &q->msg_list);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return 0;
}

/**
 * remove_msg_from_queue() - To remove a message from the msg queue.
 *
 * @q: message queue
 *
 * This function delets a message from the message list associated with message
 * queue q and also updates read ptr.
 * If the message list is empty, then, event is set to block the select and
 * read calls of the paricular queue.
 *
 * The message list is FIFO style and message is always added to tail and
 * removed from head.
 */

int remove_msg_from_queue(struct message_queue *q)
{
	struct queue_element *old_msg = NULL;
	struct shrm_dev *shrm = q->shrm;
	struct list_head *msg;

	dev_dbg(shrm->dev, "%s IN q->readptr %d\n",
					__func__, q->readptr);

	list_for_each(msg, &q->msg_list) {
		old_msg = list_entry(msg, struct queue_element, entry);
		if (old_msg == NULL) {
			dev_err(shrm->dev, ":no message found\n");
			return -EFAULT;
		}
		break;
	}
	list_del(msg);
	q->readptr = (q->readptr + old_msg->size) % q->size;
	if (list_empty(&q->msg_list)) {
		dev_dbg(shrm->dev, "List is empty setting RP= 0\n");
		atomic_set(&q->q_rp, 0);
	}
	kfree(old_msg);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return 0;
}

/**
 * get_size_of_new_msg() - retrieve new message from message list
 *
 * @q: message queue
 *
 * This function will retrieve most recent message from the corresponding
 * queue list. New message is always retrieved from head side.
 * It returns new message no, offset if FIFO and size.
 */
int get_size_of_new_msg(struct message_queue *q)
{
	struct queue_element *new_msg = NULL;
	struct list_head *msg_list;
	struct shrm_dev *shrm = q->shrm;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	spin_lock_bh(&q->update_lock);
	list_for_each(msg_list, &q->msg_list) {
		new_msg = list_entry(msg_list, struct queue_element, entry);
		if (new_msg == NULL) {
			spin_unlock_bh(&q->update_lock);
			dev_err(shrm->dev, "no message found\n");
			return -1;
		}
		break;
	}
	spin_unlock_bh(&q->update_lock);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return new_msg->size;
}

/**
 * isi_receive() - Rx Completion callback
 *
 * @data:message pointer
 * @n_bytes:message size
 *
 * This function is a callback to indicate ISI message reception is complete.
 * It updates Writeptr of the Fifo
 */
static int isi_receive(struct shrm_dev *shrm,
					void *data, u32 n_bytes)
{
	u32 size = 0;
	int ret = 0;
	u8 *psrc;
	struct message_queue *q;
	struct isadev_context *isidev = &shrm->isa_context->isadev[0];

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	q = &isidev->dl_queue;
	spin_lock(&q->update_lock);
	/* Memcopy RX data first */
	if ((q->writeptr+n_bytes) >= q->size) {
		dev_dbg(shrm->dev, "Inside Loop Back\n");
		psrc = (u8 *)data;
		size = (q->size-q->writeptr);
		/* Copy First Part of msg */
		memcpy((q->fifo_base+q->writeptr), psrc, size);
		psrc += size;
		/* Copy Second Part of msg at the top of fifo */
		memcpy(q->fifo_base, psrc, (n_bytes-size));
	} else {
		memcpy((q->fifo_base+q->writeptr), data, n_bytes);
	}
	ret = add_msg_to_queue(q, n_bytes);
	if (ret < 0)
		dev_err(shrm->dev, "Adding msg to message queue failed\n");
	spin_unlock(&q->update_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return ret;
}

/**
 * rpc_receive() - Rx Completion callback
 *
 * @data:message pointer
 * @n_bytes:message size
 *
 * This function is a callback to indicate RPC message reception is complete.
 * It updates Writeptr of the Fifo
 */
static int rpc_receive(struct shrm_dev *shrm,
					void *data, u32 n_bytes)
{
	u32 size = 0;
	int ret = 0;
	u8 *psrc;
	struct message_queue *q;
	struct isadev_context *rpcdev = &shrm->isa_context->isadev[1];

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	q = &rpcdev->dl_queue;
	spin_lock(&q->update_lock);
	/* Memcopy RX data first */
	if ((q->writeptr+n_bytes) >= q->size) {
		psrc = (u8 *)data;
		size = (q->size-q->writeptr);
		/* Copy First Part of msg */
		memcpy((q->fifo_base+q->writeptr), psrc, size);
		psrc += size;
		/* Copy Second Part of msg at the top of fifo */
		memcpy(q->fifo_base, psrc, (n_bytes-size));
	} else {
		memcpy((q->fifo_base+q->writeptr), data, n_bytes);
	}

	ret = add_msg_to_queue(q, n_bytes);
	if (ret < 0)
		dev_err(shrm->dev, "Adding msg to message queue failed\n");
	spin_unlock(&q->update_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return ret;
}

/**
 * audio_receive() - Rx Completion callback
 *
 * @data:message pointer
 * @n_bytes:message size
 *
 * This function is a callback to indicate audio message reception is complete.
 * It updates Writeptr of the Fifo
 */
static int audio_receive(struct shrm_dev *shrm,
					void *data, u32 n_bytes)
{
	u32 size = 0;
	int ret = 0;
	u8 *psrc;
	struct message_queue *q;
	struct isadev_context *audiodev = &shrm->isa_context->isadev[2];

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	q = &audiodev->dl_queue;
	spin_lock(&q->update_lock);
	/* Memcopy RX data first */
	if ((q->writeptr+n_bytes) >= q->size) {
		psrc = (u8 *)data;
		size = (q->size-q->writeptr);
		/* Copy First Part of msg */
		memcpy((q->fifo_base+q->writeptr), psrc, size);
		psrc += size;
		/* Copy Second Part of msg at the top of fifo */
		memcpy(q->fifo_base, psrc, (n_bytes-size));
	} else {
		memcpy((q->fifo_base+q->writeptr), data, n_bytes);
	}
	ret = add_msg_to_queue(q, n_bytes);
	if (ret < 0)
		dev_err(shrm->dev, "Adding msg to message queue failed\n");
	spin_unlock(&q->update_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return ret;
}

/**
 * security_receive() - Rx Completion callback
 *
 * @data:message pointer
 * @n_bytes: message size
 *
 * This function is a callback to indicate security message reception
 * is complete.It updates Writeptr of the Fifo
 */
static int security_receive(struct shrm_dev *shrm,
					void *data, u32 n_bytes)
{
	u32 size = 0;
	int ret = 0;
	u8 *psrc;
	struct message_queue *q;
	struct isadev_context *secdev = &shrm->isa_context->isadev[3];

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	q = &secdev->dl_queue;
	spin_lock(&q->update_lock);
	/* Memcopy RX data first */
	if ((q->writeptr+n_bytes) >= q->size) {
		psrc = (u8 *)data;
		size = (q->size-q->writeptr);
		/* Copy First Part of msg */
		memcpy((q->fifo_base+q->writeptr), psrc, size);
		psrc += size;
		/* Copy Second Part of msg at the top of fifo */
		memcpy(q->fifo_base, psrc, (n_bytes-size));
	} else {
		memcpy((q->fifo_base+q->writeptr), data, n_bytes);
	}
	ret = add_msg_to_queue(q, n_bytes);
	if (ret < 0)
		dev_err(shrm->dev, "Adding msg to message queue failed\n");
	spin_unlock(&q->update_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return ret;
}


/**
 * isa_select() - Select Interface
 *
 * @filp:file descriptor pointer
 * @wait:poll_table_struct pointer
 *
 * This function is used to perform non-blocking read operations. It allows
 * a process to determine whether it can read from one or more open files
 * without blocking. These calls can also block a process until any of a
 * given set of file descriptors becomes available for reading.
 * If a file is ready to read, POLLIN | POLLRDNORM bitmask is returned.
 * The driver method is called whenever the user-space program performs a select
 * system call involving a file descriptor associated with the driver.
 */
static u32 isa_select(struct file *filp,
				struct poll_table_struct *wait)
{
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	struct message_queue *q;
	u32 mask = 0;
	u32 m = iminor(filp->f_path.dentry->d_inode);

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (isadev->device_id != m)
			return -1;
	q = &isadev->dl_queue;
	poll_wait(filp, &q->wq_readable, wait);
	if (atomic_read(&q->q_rp) == 1)
		mask = POLLIN | POLLRDNORM;
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return mask;
}

/**
 * isa_read() - Read from device
 *
 * @filp:file descriptor
 * @buf:user buffer pointer
 * @len:size of requested data transfer
 * @ppos:not used
 *
 * This function is called whenever user calls read() system call.
 * It reads a oldest message from queue and copies it into user buffer and
 * returns its size.
 * If there is no message present in queue, then it blocks until new data is
 * available.
 */
ssize_t isa_read(struct file *filp, char __user *buf,
				size_t len, loff_t *ppos)
{
	struct isadev_context *isadev = (struct isadev_context *)
							filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	struct message_queue *q;
	char *psrc;
	u32 msgsize;
	u32 size = 0;
	int ret = 0;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (len <= 0)
		return -EFAULT;
	q = &isadev->dl_queue;

	spin_lock_bh(&q->update_lock);
	if (list_empty(&q->msg_list)) {
		spin_unlock_bh(&q->update_lock);
		if (wait_event_interruptible(q->wq_readable,
				atomic_read(&q->q_rp) == 1)) {
			return -ERESTARTSYS;
		}
	} else
		spin_unlock_bh(&q->update_lock);

	msgsize = get_size_of_new_msg(q);
	if ((q->readptr+msgsize) >= q->size) {
		dev_dbg(shrm->dev, "Inside Loop Back\n");
		psrc = (char *)buf;
		size = (q->size-q->readptr);
		/* Copy First Part of msg */
		if (copy_to_user(psrc,
				(u8 *)(q->fifo_base+q->readptr),
				size)) {
			dev_err(shrm->dev, "copy_to_user failed\n");
			return -EFAULT;
		}
		psrc += size;
		/* Copy Second Part of msg at the top of fifo */
		if (copy_to_user(psrc,
				(u8 *)(q->fifo_base),
				(msgsize-size))) {
			dev_err(shrm->dev, "copy_to_user failed\n");
			return -EFAULT;
		}
	} else {
		if (copy_to_user(buf,
				(u8 *)(q->fifo_base+q->readptr),
				msgsize)) {
			dev_err(shrm->dev, "copy_to_user failed\n");
			return -EFAULT;
		}
	}

	spin_lock_bh(&q->update_lock);
	ret = remove_msg_from_queue(q);
	if (ret < 0) {
		dev_err(shrm->dev,
				"Removing msg from message queue failed\n");
		msgsize = ret;
	}
	spin_unlock_bh(&q->update_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return msgsize;
}
/**
 * isa_write() - Write to device
 *
 * @filp:file descriptor
 * @buf:user buffer pointer
 * @len:size of requested data transfer
 * @ppos:not used
 *
 * This function is called whenever user calls write() system call.
 * It checks if there is space available in queue, and copies the message
 * inside queue. If there is no space, it blocks until space becomes available.
 * It also schedules transfer thread to transmit the newly added message.
 */
static ssize_t isa_write(struct file *filp, const char __user *buf,
				 size_t len, loff_t *ppos)
{
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	struct message_queue *q;
	int err, ret;
	void *addr = 0;
	u8 l2_header = 0;

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	if (len <= 0)
		return -EFAULT;
	q = &isadev->dl_queue;

	switch (isadev->device_id) {
	case ISI_MESSAGING:
		dev_dbg(shrm->dev, "ISI\n");
		addr = (void *)wr_isi_msg;
#ifdef CONFIG_U8500_SHRM_LOOP_BACK
		dev_dbg(shrm->dev, "Loopback\n");
		l2_header = COMMON_LOOPBACK_MESSAGING;
#else
		l2_header = isadev->device_id;
#endif
		break;
	case RPC_MESSAGING:
		dev_dbg(shrm->dev, "RPC\n");
		addr = (void *)wr_rpc_msg;
#ifdef CONFIG_U8500_SHRM_LOOP_BACK
		l2_header = COMMON_LOOPBACK_MESSAGING;
#else
		l2_header = isadev->device_id;
#endif
		break;
	case AUDIO_MESSAGING:
		dev_dbg(shrm->dev, "Audio\n");
		addr = (void *)wr_audio_msg;
#ifdef CONFIG_U8500_SHRM_LOOP_BACK
		l2_header = AUDIO_LOOPBACK_MESSAGING;
#else
		l2_header = isadev->device_id;
#endif

		break;
	case SECURITY_MESSAGING:
		dev_dbg(shrm->dev, "Security\n");
		addr = (void *)wr_sec_msg;
#ifdef CONFIG_U8500_SHRM_LOOP_BACK
		l2_header = COMMON_LOOPBACK_MESSAGING;
#else
		l2_header = isadev->device_id;
#endif
		break;
	default:
		dev_dbg(shrm->dev, "Wrong device\n");
		return -EFAULT;
	}

	if (copy_from_user(addr, buf, len)) {
		dev_err(shrm->dev, "copy_from_user failed\n");
		return -EFAULT;
	}

	/* Write msg to Fifo */
	if (isadev->device_id == 2) {
		mutex_lock(&shrm->isa_context->tx_audio_mutex);
		err = shm_write_msg(shrm, l2_header, addr, len);
		if (!err)
			ret = len;
		else
			ret = err;
		mutex_unlock(&shrm->isa_context->tx_audio_mutex);
	} else {
		spin_lock_bh(&shrm->isa_context->common_tx);
		err = shm_write_msg(shrm, l2_header, addr, len);
		if (!err)
			ret = len;
		else
			ret = err;
		spin_unlock_bh(&shrm->isa_context->common_tx);
	}

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return ret;
}

/**
 * isa_ioctl() - To handle different ioctl commands supported by driver.
 *
 * @inode: structure is used by the kernel internally to represent files
 * @filp:file descriptor pointer
 * @cmd:ioctl command
 * @arg:input param
 *
 * Following ioctls are supported by this driver.
 * DLP_IOCTL_ALLOCATE_BUFFER - To allocate buffer for new uplink message.
 * This ioctl is called with required message size. It returns offset for
 * the allocates space in the queue. DLP_IOCTL_PUT_MESSAGE -  To indicate
 * new uplink message available in queuq for  transmission. Message is copied
 * from offset location returned by previous ioctl before calling this ioctl.
 * DLP_IOCTL_GET_MESSAGE - To check if any downlink message is available in
 * queue. It returns  offset for new message inside queue.
 * DLP_IOCTL_DEALLOCATE_BUFFER - To deallocate any buffer allocate for
 * downlink message once the message is copied. Message is copied from offset
 * location returned by previous ioctl before calling this ioctl.
 */
static int isa_ioctl(struct inode *inode, struct file *filp,
				unsigned cmd, unsigned long arg)
{
	int err = 0;
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	u32 m = iminor(inode);

	if (isadev->device_id != m)
		return -1;

	switch (cmd) {
	case DLP_IOC_ALLOCATE_BUFFER:
		dev_dbg(shrm->dev, "DLP_IOC_ALLOCATE_BUFFER\n");
		break;
	case DLP_IOC_PUT_MESSAGE:
		dev_dbg(shrm->dev, "DLP_IOC_PUT_MESSAGE\n");
		break;
	case DLP_IOC_GET_MESSAGE:
		dev_dbg(shrm->dev, "DLP_IOC_GET_MESSAGE\n");
		break;
	case DLP_IOC_DEALLOCATE_BUFFER:
		dev_dbg(shrm->dev, "DLP_IOC_DEALLOCATE_BUFFER\n");
		break;
	default:
		dev_dbg(shrm->dev, "Unknown IOCTL\n");
		err = -1;
		break;
	}
	return err;
}
/**
 * isa_mmap() - Maps kernel queue memory to user space.
 *
 * @filp:file descriptor pointer
 * @vma:virtual area memory structure.
 *
 * This function maps kernel FIFO into user space. This function
 * shall be called twice to map both uplink and downlink buffers.
 */
static int isa_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;

	u32	m = iminor(filp->f_path.dentry->d_inode);
	dev_dbg(shrm->dev, "%s %dIN\n", __func__, m);

	isadev = (struct isadev_context *)filp->private_data;
	return 0;
}

/**
 * isa_close() - Close device file
 *
 * @inode:structure is used by the kernel internally to represent files
 * @filp:device file descriptor
 *
 * This function deletes structues associated with this file, deletes
 * queues, flushes and destroys workqueus and closes this file.
 * It also unregisters itself from l2mux driver.
 */
static int isa_close(struct inode *inode, struct file *filp)
{
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	struct isa_driver_context *isa_context = shrm->isa_context;
	u8 m;

	mutex_lock(&isa_lock);
	m = iminor(filp->f_path.dentry->d_inode);
	dev_dbg(shrm->dev, "%s IN %d", __func__, m);

	if (atomic_dec_and_test(&isa_context->is_open[m])) {
		atomic_inc(&isa_context->is_open[m]);
		dev_err(shrm->dev, "Device not opened yet\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}
	atomic_set(&isa_context->is_open[m], 1);

	dev_dbg(shrm->dev, "isadev->device_id %d", isadev->device_id);
	dev_dbg(shrm->dev, "Closed %d device\n", m);

	if (m == ISI_MESSAGING)
		dev_dbg(shrm->dev, "Closed ISI_MESSAGING Device\n");
	else if (m == RPC_MESSAGING)
		dev_dbg(shrm->dev, "Closed RPC_MESSAGING Device\n");
	else if (m == AUDIO_MESSAGING)
		dev_dbg(shrm->dev, "Closed AUDIO_MESSAGING Device\n");
	else if (m == SECURITY_MESSAGING)
		dev_dbg(shrm->dev, "Closed SECURITY_MESSAGING Device\n");
	else
		dev_dbg(shrm->dev, NAME ":No such device present\n");

	mutex_unlock(&isa_lock);
	return 0;
}
/**
 * isa_open() -  Open device file
 *
 * @inode: structure is used by the kernel internally to represent files
 * @filp: device file descriptor
 *
 * This function performs initialization tasks needed to open SHM channel.
 * Following tasks are performed.
 * -return if device is already opened
 * -create uplink FIFO
 * -create downlink FIFO
 * -init delayed workqueue thread
 * -register to l2mux driver
 */
static int isa_open(struct inode *inode, struct file *filp)
{
	int err = 0;
	u8 m;
	struct isadev_context *isadev;
	struct isa_driver_context *isa_context = container_of(
			inode->i_cdev,
			struct isa_driver_context,
			cdev);
	struct shrm_dev *shrm = isa_context->isadev->dl_queue.shrm;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (get_boot_state() != BOOT_DONE) {
		dev_err(shrm->dev, "Boot is not done\n");
		return -EBUSY;
	}
	mutex_lock(&isa_lock);
	m = iminor(inode);

	if ((m != ISI_MESSAGING) && (m != RPC_MESSAGING) &&
		(m != AUDIO_MESSAGING) && (m != SECURITY_MESSAGING)) {
		dev_err(shrm->dev, "No such device present\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}
	if (!atomic_dec_and_test(&isa_context->is_open[m])) {
		atomic_inc(&isa_context->is_open[m]);
		dev_err(shrm->dev, "Device already opened\n");
		mutex_unlock(&isa_lock);
		return -EBUSY;
	}

	if (m == ISI_MESSAGING)
		dev_dbg(shrm->dev, "Open ISI_MESSAGING Device\n");
	else if (m == RPC_MESSAGING)
		dev_dbg(shrm->dev, "Open RPC_MESSAGING Device\n");
	else if (m == AUDIO_MESSAGING)
		dev_dbg(shrm->dev, "Open AUDIO_MESSAGING Device\n");
	else if (m == SECURITY_MESSAGING)
		dev_dbg(shrm->dev, "Open SECURITY_MESSAGING Device\n");
	else
		dev_dbg(shrm->dev, ":No such device present\n");

	isadev = &isa_context->isadev[m];
	if (filp != NULL)
		filp->private_data = isadev;

	mutex_unlock(&isa_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return err;
}

const struct file_operations isa_fops = {
	.owner	 = THIS_MODULE,
	.open	 = isa_open,
	.release = isa_close,
	.ioctl	 = isa_ioctl,
	.mmap	 = isa_mmap,
	.read	 = isa_read,
	.write	 = isa_write,
	.poll	 = isa_select,
};

/**
 * isa_init() - module insertion function
 *
 * This function registers module as a character driver using
 * register_chrdev_region() or alloc_chrdev_region. It adds this
 * driver to system using cdev_add() call. Major number is dynamically
 * allocated using alloc_chrdev_region() by default or left to user to specify
 * it during load time. For this variable major is used as module_param
 * Nodes to be created using
 * mknod /dev/isi c $major 0
 * mknod /dev/rpc c $major 1
 * mknod /dev/audio c $major 2
 * mknod /dev/sec c $major 3
 */
int isa_init(struct shrm_dev *shrm)
{
	dev_t	dev_id;
	int	retval, no_dev;
	struct isadev_context *isadev;
	struct isa_driver_context *isa_context;

	isa_context = kzalloc(sizeof(struct isa_driver_context),
							GFP_KERNEL);
	shrm->isa_context = isa_context;
	if (isa_context == NULL) {
		dev_err(shrm->dev, "Failed to alloc memory\n");
		return -ENOMEM;
	}

	if (major) {
		dev_id = MKDEV(major, 0);
		retval = register_chrdev_region(dev_id, ISA_DEVICES, NAME);
	} else {
		retval = alloc_chrdev_region(&dev_id, 0, ISA_DEVICES, NAME);
		major = MAJOR(dev_id);
	}

	dev_dbg(shrm->dev, "major %d\n", major);

	cdev_init(&isa_context->cdev, &isa_fops);
	isa_context->cdev.owner = THIS_MODULE;
	retval = cdev_add(&isa_context->cdev, dev_id, ISA_DEVICES);
	if (retval) {
		dev_err(shrm->dev, "Failed to add char device\n");
		return retval;
	}

	for (no_dev = 0; no_dev < ISA_DEVICES; no_dev++)
		atomic_set(&isa_context->is_open[no_dev], 1);

	isa_context->isadev = kzalloc(sizeof
					(struct isadev_context)*ISA_DEVICES,
					GFP_KERNEL);
	if (isa_context->isadev == NULL) {
		dev_err(shrm->dev, "Failed to alloc memory\n");
		return -ENOMEM;
	}
	for (no_dev = 0; no_dev < ISA_DEVICES; no_dev++) {
		isadev = &isa_context->isadev[no_dev];
		isadev->device_id = no_dev;
		retval = create_queue(&isadev->dl_queue,
					isadev->device_id, shrm);
		if (retval < 0) {
			dev_err(shrm->dev, "create dl_queue failed\n");
			delete_queue(&isadev->dl_queue);
			kfree(isadev);
			return retval;
		}
	}
	mutex_init(&isa_context->tx_audio_mutex);
	spin_lock_init(&isa_context->common_tx);

	dev_err(shrm->dev, "SHRM char driver added\n");

	return retval;
}

void isa_exit(struct shrm_dev *shrm)
{
	int	no_dev;
	struct isadev_context *isadev;
	struct isa_driver_context *isa_context = shrm->isa_context;
	dev_t dev_id = MKDEV(major, 0);

	for (no_dev = 0; no_dev < ISA_DEVICES; no_dev++) {
		isadev = &isa_context->isadev[no_dev];
		delete_queue(&isadev->dl_queue);
		kfree(isadev);
	}

	cdev_del(&isa_context->cdev);
	unregister_chrdev_region(dev_id, ISA_DEVICES);
	kfree(isa_context);

	dev_err(shrm->dev, "SHRM char driver removed\n");
}

#ifdef CONFIG_HIGH_RES_TIMERS
static enum hrtimer_restart callback(struct hrtimer *timer)
{
	return HRTIMER_NORESTART;
}
#endif


static int __init shrm_probe(struct platform_device *pdev)
{
	int err = 0;
	struct resource *res;
	struct shrm_dev *shrm = NULL;

	if (pdev == NULL)  {
		dev_err(shrm->dev,
			"No device/platform_data found on shm device\n");
		return -ENODEV;
	}


	shrm = kzalloc(sizeof(struct shrm_dev), GFP_KERNEL);
	if (shrm == NULL) {
		dev_err(shrm->dev,
			"Could not allocate memory for struct shm_dev\n");
		return -ENOMEM;
	}
	shrm->dev = &pdev->dev;

	/* initialise the SHM */

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(shrm->dev, "Unable to map Ca Wake up interrupt\n");
		err = -EBUSY;
		goto rollback_intr;
	}
	shrm->ca_wake_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!res) {
		dev_err(shrm->dev,
			"Unable to map APE_Read_notif_common IRQ base\n");
		err = -EBUSY;
		goto rollback_intr;
	}
	shrm->ac_read_notif_0_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (!res) {
		dev_err(shrm->dev,
			"Unable to map APE_Read_notif_audio IRQ base\n");
		err = -EBUSY;
		goto rollback_intr;
	}
	shrm->ac_read_notif_1_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 3);
	if (!res) {
		dev_err(shrm->dev,
			"Unable to map Cmt_msg_pending_notif_common IRQbase\n");
		err = -EBUSY;
		goto rollback_intr;
	}
	shrm->ca_msg_pending_notif_0_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 4);
	if (!res) {
		dev_err(shrm->dev,
			"Unable to map Cmt_msg_pending_notif_audio IRQ base\n");
		err = -EBUSY;
		goto rollback_intr;
	}
	shrm->ca_msg_pending_notif_1_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(shrm->dev,
				"Could not get SHM IO memory information\n");
		err = -ENODEV;
		goto rollback_intr;
	}

	shrm->intr_base = (void __iomem *)ioremap_nocache(res->start,
					res->end - res->start + 1);

	if (!(shrm->intr_base)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_intr;
	}

	shrm->ape_common_fifo_base_phy =
			(u32 *)U8500_SHM_FIFO_APE_COMMON_BASE;
	shrm->ape_common_fifo_base =
		(void __iomem *)ioremap_nocache(
					U8500_SHM_FIFO_APE_COMMON_BASE,
					SHM_FIFO_0_SIZE);
	shrm->ape_common_fifo_size = (SHM_FIFO_0_SIZE)/4;

	if (!(shrm->ape_common_fifo_base)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_ape_common_fifo_base;
	}

	shrm->cmt_common_fifo_base_phy =
		(u32 *)U8500_SHM_FIFO_CMT_COMMON_BASE;

	shrm->cmt_common_fifo_base =
		(void __iomem *)ioremap_nocache(
			U8500_SHM_FIFO_CMT_COMMON_BASE, SHM_FIFO_0_SIZE);
	shrm->cmt_common_fifo_size = (SHM_FIFO_0_SIZE)/4;

	if (!(shrm->cmt_common_fifo_base)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_cmt_common_fifo_base;
	}

	shrm->ape_audio_fifo_base_phy =
			(u32 *)U8500_SHM_FIFO_APE_AUDIO_BASE;
	shrm->ape_audio_fifo_base =
		(void __iomem *)ioremap_nocache(U8500_SHM_FIFO_APE_AUDIO_BASE,
							SHM_FIFO_1_SIZE);
	shrm->ape_audio_fifo_size = (SHM_FIFO_1_SIZE)/4;

	if (!(shrm->ape_audio_fifo_base)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_ape_audio_fifo_base;
	}

	shrm->cmt_audio_fifo_base_phy =
			(u32 *)U8500_SHM_FIFO_CMT_AUDIO_BASE;
	shrm->cmt_audio_fifo_base =
		(void __iomem *)ioremap_nocache(U8500_SHM_FIFO_CMT_AUDIO_BASE,
							SHM_FIFO_1_SIZE);
	shrm->cmt_audio_fifo_size = (SHM_FIFO_1_SIZE)/4;

	if (!(shrm->cmt_audio_fifo_base)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_cmt_audio_fifo_base;
	}

	shrm->ac_common_shared_wptr =
		(void __iomem *)ioremap(SHM_ACFIFO_0_WRITE_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ac_common_shared_wptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_ac_common_shared_wptr;
	}

	shrm->ac_common_shared_rptr =
		(void __iomem *)ioremap(SHM_ACFIFO_0_READ_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ac_common_shared_rptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}


	shrm->ca_common_shared_wptr =
		(void __iomem *)ioremap(SHM_CAFIFO_0_WRITE_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ca_common_shared_wptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}

	shrm->ca_common_shared_rptr =
		(void __iomem *)ioremap(SHM_CAFIFO_0_READ_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ca_common_shared_rptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}


	shrm->ac_audio_shared_wptr =
		(void __iomem *)ioremap(SHM_ACFIFO_1_WRITE_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ac_audio_shared_wptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}


	shrm->ac_audio_shared_rptr =
		(void __iomem *)ioremap(SHM_ACFIFO_1_READ_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ac_audio_shared_rptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}


	shrm->ca_audio_shared_wptr =
		(void __iomem *)ioremap(SHM_CAFIFO_1_WRITE_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ca_audio_shared_wptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}


	shrm->ca_audio_shared_rptr =
		(void __iomem *)ioremap(SHM_CAFIFO_1_READ_AMCU, SHM_PTR_SIZE);

	if (!(shrm->ca_audio_shared_rptr)) {
		dev_err(shrm->dev, "Unable to map register base\n");
		err = -EBUSY;
		goto rollback_map;
	}


	if (isa_init(shrm) != 0) {
		dev_err(shrm->dev, "Driver Initialization Error\n");
		err = -EBUSY;
	}
	/* install handlers and tasklets */
	if (shm_initialise_irq(shrm)) {
		dev_err(shrm->dev, "shm error in interrupt registration\n");
		goto rollback_irq;
	}

#ifdef CONFIG_HIGH_RES_TIMERS
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.function = callback;

	hrtimer_start(&timer, ktime_set(0, 2*NSEC_PER_MSEC), HRTIMER_MODE_REL);
#endif

	return err;

rollback_irq:
	free_shm_irq(shrm);
rollback_map:
	iounmap(shrm->ac_common_shared_wptr);
	iounmap(shrm->ac_common_shared_rptr);
	iounmap(shrm->ca_common_shared_wptr);
	iounmap(shrm->ca_common_shared_rptr);
	iounmap(shrm->ac_audio_shared_wptr);
	iounmap(shrm->ac_audio_shared_rptr);
	iounmap(shrm->ca_audio_shared_wptr);
	iounmap(shrm->ca_audio_shared_rptr);
rollback_ac_common_shared_wptr:
	iounmap(shrm->cmt_audio_fifo_base);
rollback_cmt_audio_fifo_base:
	iounmap(shrm->ape_audio_fifo_base);
rollback_ape_audio_fifo_base:
	iounmap(shrm->cmt_common_fifo_base);
rollback_cmt_common_fifo_base:
	iounmap(shrm->ape_common_fifo_base);
rollback_ape_common_fifo_base:
	iounmap(shrm->intr_base);
rollback_intr:
	kfree(shrm);
	return err;
}

static int __exit shrm_remove(struct platform_device *pdev)
{
	struct shrm_dev *shrm = platform_get_drvdata(pdev);

	free_shm_irq(shrm);
	iounmap(shrm->intr_base);
	iounmap(shrm->ape_common_fifo_base);
	iounmap(shrm->cmt_common_fifo_base);
	iounmap(shrm->ape_audio_fifo_base);
	iounmap(shrm->cmt_audio_fifo_base);
	iounmap(shrm->ac_common_shared_wptr);
	iounmap(shrm->ac_common_shared_rptr);
	iounmap(shrm->ca_common_shared_wptr);
	iounmap(shrm->ca_common_shared_rptr);
	iounmap(shrm->ac_audio_shared_wptr);
	iounmap(shrm->ac_audio_shared_rptr);
	iounmap(shrm->ca_audio_shared_wptr);
	iounmap(shrm->ca_audio_shared_rptr);
	kfree(shrm);
	isa_exit(shrm);

	return 0;
}
#ifdef CONFIG_PM

/**
 * u8500_shrm_suspend() - This routine puts the SHRM in to sustend state.
 * @pdev: platform device.
 *
 * This routine checks the current ongoing communication with Modem by
 * examining the ca_wake state and prevents suspend if modem communication
 * is on-going.
 * If ca_wake = 1 (high), modem comm. is on-going; don't suspend
 * If ca_wake = 0 (low), no comm. with modem on-going.Allow suspend
 */
int u8500_shrm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct shrm_dev *shrm = platform_get_drvdata(pdev);

	dev_dbg(shrm->dev, "%s called...\n", __func__);
	dev_dbg(shrm->dev, "\n ca_wake_req_state = %x\n",
					get_ca_wake_req_state());
	/* if ca_wake_req is high, prevent system suspend */
	if (get_ca_wake_req_state())
		return -EBUSY;
	else
		return 0;
}

/**
 * u8500_shrm_resume() - This routine resumes the SHRM from sustend state.
 * @pdev: platform device.
 *
 * This routine restore back the current state of the SHRM
 */
int u8500_shrm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct shrm_dev *shrm = platform_get_drvdata(pdev);

	dev_dbg(shrm->dev, "%s called...\n", __func__);
	/* TODO:
	 * As of now, no state save takes place in suspend.
	 * So, nothing to restore in resume.
	 * Simply return as of now.
	 * State saved in suspend should be restored here.
	 */

	return 0;
}

static const struct dev_pm_ops shrm_dev_pm_ops = {
	.suspend = u8500_shrm_suspend,
	.resume = u8500_shrm_resume,
};
#endif

static struct platform_driver shrm_driver = {
	.remove = __exit_p(shrm_remove),
	.driver = {
		.name = "u8500_shrm",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &shrm_dev_pm_ops,
#endif
	},
};

static int __init shrm_driver_init(void)
{
	return platform_driver_probe(&shrm_driver, shrm_probe);
}

static void __exit shrm_driver_exit(void)
{
	platform_driver_unregister(&shrm_driver);
}

module_init(shrm_driver_init);
module_exit(shrm_driver_exit);

MODULE_AUTHOR("Biju Das");
MODULE_DESCRIPTION("Shared Memory Modem Driver Interface");
MODULE_LICENSE("GPL");
