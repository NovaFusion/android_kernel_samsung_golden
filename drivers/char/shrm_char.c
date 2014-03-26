/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghavi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/modem/shrm/shrm_driver.h>
#include <linux/modem/shrm/shrm_private.h>
#include <linux/modem/shrm/shrm_config.h>
#include <linux/modem/shrm/shrm.h>
#include <asm/atomic.h>

#include <mach/isa_ioctl.h>


#define NAME "IPC_ISA"
/* L2 header for common loopback device is 0xc0 and hence 0xdd+1 = 222*/
#define MAX_L2_HEADERS 222

#define SIZE_OF_FIFO (512*1024)

static u8 message_fifo[ISA_DEVICES][SIZE_OF_FIFO];

static u8 wr_rpc_msg[10*1024];
static u8 wr_sec_msg[10*1024];
static u8 wr_audio_msg[10*1024];
static u8 wr_rtc_cal_msg[100];

struct map_device {
	u8 l2_header;
	u8 idx;
	char *name;
};

static struct map_device map_dev[] = {
	{ISI_MESSAGING, 0, "isi"},
	{RPC_MESSAGING, 1, "rpc"},
	{AUDIO_MESSAGING, 2, "modemaudio"},
	{SECURITY_MESSAGING, 3, "sec"},
	{COMMON_LOOPBACK_MESSAGING, 4, "common_loopback"},
	{AUDIO_LOOPBACK_MESSAGING, 5, "audio_loopback"},
	{CIQ_MESSAGING, 6, "ciq"},
	{RTC_CAL_MESSAGING, 7, "rtc_calibration"},
	{IPCCTRL, 8, "ipcctr"},
	{IPCDATA, 9, "ipcdata"},
};

/*
 * int major:This variable is exported to user as module_param to specify
 * major number at load time
 */
static int major;
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");
/* global fops mutex */
static DEFINE_MUTEX(isa_lock);

/**
 * shrm_get_cdev_index() - return the index mapped to l2 header
 * @l2_header:	L2 header
 *
 * struct map_device maps the index(count) with the device L2 header.
 * This function returns the index for the provided L2 header in case
 * of success else -ve value.
 */
int shrm_get_cdev_index(u8 l2_header)
{
	u8 cnt;
	for (cnt = 0; cnt < ISA_DEVICES; cnt++) {
		if (map_dev[cnt].l2_header == l2_header)
			return map_dev[cnt].idx;
	}
	return -EINVAL;
}

/**
 * shrm_get_cdev_l2header() - return l2_header mapped to the index
 * @idx:	index
 *
 * struct map_device maps the index(count) with the device L2 header.
 * This function returns the L2 header for the given index in case
 * of success else -ve value.
 */
int shrm_get_cdev_l2header(u8 idx)
{
	u8 cnt;
	for (cnt = 0; cnt < ISA_DEVICES; cnt++) {
		if (map_dev[cnt].idx == idx)
			return map_dev[cnt].l2_header;
	}
	return -EINVAL;
}

void shrm_char_reset_queues(struct shrm_dev *shrm)
{
	struct isadev_context *isadev;
	struct isa_driver_context *isa_context;
	struct queue_element *cur_msg = NULL;
	struct list_head *cur_msg_ptr = NULL;
	struct list_head *msg_ptr;
	struct message_queue *q;
	int no_dev;

	dev_info(shrm->dev, "%s: Resetting char device queues\n", __func__);
	isa_context = shrm->isa_context;
	for (no_dev = 0 ; no_dev < ISA_DEVICES ; no_dev++) {
		isadev = &isa_context->isadev[no_dev];
		q = &isadev->dl_queue;

		spin_lock_bh(&q->update_lock);
		/* empty out the msg queue */
		list_for_each_safe(cur_msg_ptr, msg_ptr, &q->msg_list) {
			cur_msg = list_entry(cur_msg_ptr,
					struct queue_element, entry);
			list_del(cur_msg_ptr);
			kfree(cur_msg);
		}

		/* reset the msg queue pointers */
		q->size = SIZE_OF_FIFO;
		q->readptr = 0;
		q->writeptr = 0;
		q->no = 0;

		/* wake up the blocking read/select */
		atomic_set(&q->q_rp, 1);
		wake_up_interruptible(&q->wq_readable);

		spin_unlock_bh(&q->update_lock);
	}
}

/**
 * create_queue() - To create FIFO for Tx and Rx message buffering.
 * @q:		message queue.
 * @devicetype:	device type 0-isi,1-rpc,2-audio,3-security,
 * 4-common_loopback, 5-audio_loopback.
 * @shrm:	pointer to the shrm device information structure
 *
 * This function creates a FIFO buffer of n_bytes size using
 * dma_alloc_coherent(). It also initializes all queue handling
 * locks, queue management pointers. It also initializes message list
 * which occupies this queue.
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

static void delete_queue(struct message_queue *q)
{
	q->size = 0;
	q->readptr = 0;
	q->writeptr = 0;
}

/**
 * add_msg_to_queue() - Add a message inside queue
 * @q:		message queue
 * @size:	size in bytes
 *
 * This function tries to allocate n_bytes of size in FIFO q.
 * It returns negative number when no memory can be allocated
 * currently.
 */
int add_msg_to_queue(struct message_queue *q, u32 size)
{
	struct queue_element *new_msg = NULL;
	struct shrm_dev *shrm = q->shrm;

	dev_dbg(shrm->dev, "%s IN q->writeptr=%d\n", __func__, q->writeptr);
	new_msg = kmalloc(sizeof(struct queue_element), GFP_ATOMIC);
	if (new_msg == NULL) {
		dev_err(shrm->dev, "unable to allocate memory\n");
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
 * @q:	message queue
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
	struct list_head *msg_ptr = NULL;
	struct list_head *old_msg_ptr = NULL;

	dev_dbg(shrm->dev, "%s IN q->readptr %d\n", __func__, q->readptr);

	list_for_each_safe(old_msg_ptr, msg_ptr, &q->msg_list) {
		old_msg = list_entry(old_msg_ptr, struct queue_element, entry);
		if (old_msg == NULL) {
			dev_err(shrm->dev, "no message found\n");
			return -EFAULT;
		}
		list_del(old_msg_ptr);
		q->readptr = (q->readptr + old_msg->size)%q->size;
		kfree(old_msg);
		break;
	}
	if (list_empty(&q->msg_list)) {
		dev_dbg(shrm->dev, "List is empty setting RP= 0\n");
		atomic_set(&q->q_rp, 0);
	}

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return 0;
}

/**
 * get_size_of_new_msg() - retrieve new message from message list
 * @q:	message queue
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
	int size = 0;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	spin_lock_bh(&q->update_lock);
	list_for_each(msg_list, &q->msg_list) {
		new_msg = list_entry(msg_list, struct queue_element, entry);
		if (new_msg == NULL) {
			spin_unlock_bh(&q->update_lock);
			dev_err(shrm->dev, "no message found\n");
			return -EFAULT;
		}
		size = new_msg->size;
		break;
	}
	spin_unlock_bh(&q->update_lock);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return size;
}

/**
 * isa_select() - shrm char interface driver select interface
 * @filp:	file descriptor pointer
 * @wait:	poll_table_struct pointer
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
	u8 idx = shrm_get_cdev_index(m);

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (shrm->msr_flag)
		return -ENODEV;

	if (isadev->device_id != idx)
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
 * @filp:	file descriptor
 * @buf:	user buffer pointer
 * @len:	size of requested data transfer
 * @ppos:	not used
 *
 * It reads a oldest message from queue and copies it into user buffer and
 * returns its size.
 * If there is no message present in queue, then it blocks until new data is
 * available.
 */
ssize_t isa_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	u32 size = 0;
	int ret;
	char *psrc;
	struct isadev_context *isadev = (struct isadev_context *)
							filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	struct message_queue *q;
	u32 msgsize;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (len <= 0)
		return -EFAULT;

	q = &isadev->dl_queue;

	if (shrm->msr_flag) {
		atomic_set(&q->q_rp, 0);
		return -ENODEV;
	}

	spin_lock_bh(&q->update_lock);
	if (list_empty(&q->msg_list)) {
		spin_unlock_bh(&q->update_lock);
		dev_dbg(shrm->dev, "Waiting for Data\n");
		if (wait_event_interruptible(q->wq_readable,
				atomic_read(&q->q_rp) == 1))
			return -ERESTARTSYS;
	} else
		spin_unlock_bh(&q->update_lock);

	if (shrm->msr_flag) {
		atomic_set(&q->q_rp, 0);
		return -ENODEV;
	}

	msgsize = get_size_of_new_msg(q);

	if (len < msgsize)
		return -EINVAL;

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
				(u8 *)(q->fifo_base + q->readptr),
				msgsize)) {
			dev_err(shrm->dev, "copy_to_user failed\n");
			return -EFAULT;
		}
	}
	spin_lock_bh(&q->update_lock);
	ret = remove_msg_from_queue(q);
	if (ret < 0) {
		dev_err(shrm->dev,
				"Remove msg from message queue failed\n");
		msgsize = ret;
	}
	spin_unlock_bh(&q->update_lock);
	/* if RPC, Security  msg read, then unlock the acquired lock */
	if (isadev->device_id == RPC_MESSAGING) {
		wake_unlock(&shrm->rpc_wake_lock);
	}
	if (isadev->device_id == SECURITY_MESSAGING) {
		wake_unlock(&shrm->sec_wake_lock);
	}
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return msgsize;
}

/**
 * isa_write() - Write to shrm char device
 * @filp:	file descriptor
 * @buf:	user buffer pointer
 * @len:	size of requested data transfer
 * @ppos:	not used
 *
 * It checks if there is space available in queue, and copies the message
 * inside queue. If there is no space, it blocks until space becomes available.
 * It also schedules transfer thread to transmit the newly added message.
 */
ssize_t isa_write(struct file *filp, const char __user *buf,
				 size_t len, loff_t *ppos)
{
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	struct message_queue *q;
	void *addr = 0;
	int err, l2_header;
	int ret = 0;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (len <= 0 || buf == NULL)
		return -EFAULT;
	q = &isadev->dl_queue;
	l2_header = shrm_get_cdev_l2header(isadev->device_id);
	if (l2_header < 0) {
		dev_err(shrm->dev, "failed to get L2 header\n");
		return l2_header;
	}

	switch (l2_header) {
	case RPC_MESSAGING:
		dev_dbg(shrm->dev, "RPC\n");
		addr = (void *)wr_rpc_msg;
		break;
	case AUDIO_MESSAGING:
		dev_dbg(shrm->dev, "Audio\n");
		addr = (void *)wr_audio_msg;
		break;
	case SECURITY_MESSAGING:
		dev_dbg(shrm->dev, "Security\n");
		addr = (void *)wr_sec_msg;
		break;
	case COMMON_LOOPBACK_MESSAGING:
		dev_dbg(shrm->dev, "Common loopback\n");
		addr = isadev->addr;
		break;
	case AUDIO_LOOPBACK_MESSAGING:
		dev_dbg(shrm->dev, "Audio loopback\n");
		addr = isadev->addr;
		break;
	case CIQ_MESSAGING:
		dev_dbg(shrm->dev, "CIQ\n");
		addr = isadev->addr;
		break;
	case RTC_CAL_MESSAGING:
		dev_dbg(shrm->dev, "isa_write(): RTC Calibration\n");
		addr = (void *)wr_rtc_cal_msg;
		break;
	case IPCCTRL:
		dev_dbg(shrm->dev, "ipc-control\n");
		addr = isadev->addr;
		break;
	case IPCDATA:
		dev_dbg(shrm->dev, "ipc-data\n");
		addr = isadev->addr;
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
	if ((l2_header == AUDIO_MESSAGING) ||
			(l2_header == AUDIO_LOOPBACK_MESSAGING)) {
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
 * @inode:	structure is used by the kernel internally to represent files
 * @filp:	file descriptor pointer
 * @cmd:	ioctl command
 * @arg:	input param
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
static long isa_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long err = 0;
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;
	u32 m = iminor(filp->f_path.dentry->d_inode);

	isadev = (struct isadev_context *)filp->private_data;

	if (isadev->device_id != m)
		return -EINVAL;

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
		err = -EFAULT;
		break;
	}
	return err;
}
/**
 * isa_mmap() - Maps kernel queue memory to user space.
 * @filp:	file descriptor pointer
 * @vma:	virtual area memory structure.
 *
 * This function maps kernel FIFO into user space. This function
 * shall be called twice to map both uplink and downlink buffers.
 */
static int isa_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct isadev_context *isadev = filp->private_data;
	struct shrm_dev *shrm = isadev->dl_queue.shrm;

	u32 m = iminor(filp->f_path.dentry->d_inode);
	dev_dbg(shrm->dev, "%s %d\n", __func__, m);

	return 0;
}

/**
 * isa_close() - Close device file
 * @inode:	structure is used by the kernel internally to represent files
 * @filp:	device file descriptor
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
	int idx;

	mutex_lock(&isa_lock);
	m = iminor(filp->f_path.dentry->d_inode);
	idx = shrm_get_cdev_index(m);
	if (idx < 0) {
		dev_err(shrm->dev, "failed to get index\n");
		mutex_unlock(&isa_lock);
		return idx;
	}
	dev_dbg(shrm->dev, "isa_close %d", m);

	if (atomic_dec_and_test(&isa_context->is_open[idx])) {
		atomic_inc(&isa_context->is_open[idx]);
		dev_err(shrm->dev, "Device not opened yet\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}
	atomic_set(&isa_context->is_open[idx], 1);

	switch (m) {
	case RPC_MESSAGING:
		dev_info(shrm->dev, "Close RPC_MESSAGING Device\n");
		break;
	case AUDIO_MESSAGING:
		dev_info(shrm->dev, "Close AUDIO_MESSAGING Device\n");
		break;
	case SECURITY_MESSAGING:
		dev_info(shrm->dev, "CLose SECURITY_MESSAGING Device\n");
		break;
	case COMMON_LOOPBACK_MESSAGING:
		kfree(isadev->addr);
		dev_info(shrm->dev, "Close COMMON_LOOPBACK_MESSAGING Device\n");
		break;
	case AUDIO_LOOPBACK_MESSAGING:
		kfree(isadev->addr);
		dev_info(shrm->dev, "Close AUDIO_LOOPBACK_MESSAGING Device\n");
		break;
	case CIQ_MESSAGING:
		kfree(isadev->addr);
		dev_info(shrm->dev, "Close CIQ_MESSAGING Device\n");
		break;
	case RTC_CAL_MESSAGING:
		dev_info(shrm->dev, "Close RTC_CAL_MESSAGING Device\n");
		break;
	case IPCCTRL:
		kfree(isadev->addr);
		dev_info(shrm->dev, "Close ipc-ctrl\n");
		break;
	case IPCDATA:
		kfree(isadev->addr);
		dev_info(shrm->dev, "Close ipc-data\n");
		break;
	default:
		dev_info(shrm->dev, "No such device present\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	};
	mutex_unlock(&isa_lock);
	return 0;
}
/**
 * isa_open() -  Open device file
 * @inode:	structure is used by the kernel internally to represent files
 * @filp:	device file descriptor
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
	int idx;
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

	if ((m != RPC_MESSAGING) &&
				(m != AUDIO_LOOPBACK_MESSAGING) &&
				(m != COMMON_LOOPBACK_MESSAGING) &&
				(m != AUDIO_MESSAGING) &&
				(m != SECURITY_MESSAGING) &&
				(m != CIQ_MESSAGING) &&
				(m != RTC_CAL_MESSAGING) &&
				(m != IPCCTRL) &&
				(m != IPCDATA)) {
		dev_err(shrm->dev, "No such device present\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}
	idx = shrm_get_cdev_index(m);
	if (idx < 0) {
		dev_err(shrm->dev, "failed to get index\n");
		mutex_unlock(&isa_lock);
		return idx;
	}
	if (!atomic_dec_and_test(&isa_context->is_open[idx])) {
		atomic_inc(&isa_context->is_open[idx]);
		dev_err(shrm->dev, "Device already opened\n");
		mutex_unlock(&isa_lock);
		return -EBUSY;
	}
	isadev = &isa_context->isadev[idx];
	if (filp != NULL)
		filp->private_data = isadev;

	switch (m) {
	case RPC_MESSAGING:
		dev_info(shrm->dev, "Open RPC_MESSAGING Device\n");
		break;
	case AUDIO_MESSAGING:
		dev_info(shrm->dev, "Open AUDIO_MESSAGING Device\n");
		break;
	case SECURITY_MESSAGING:
		dev_info(shrm->dev, "Open SECURITY_MESSAGING Device\n");
		break;
	case COMMON_LOOPBACK_MESSAGING:
		isadev->addr = kzalloc(10 * 1024, GFP_KERNEL);
		if (!isadev->addr) {
			mutex_unlock(&isa_lock);
			return -ENOMEM;
		}
		dev_info(shrm->dev, "Open COMMON_LOOPBACK_MESSAGING Device\n");
		break;
	case AUDIO_LOOPBACK_MESSAGING:
		isadev->addr = kzalloc(10 * 1024, GFP_KERNEL);
		if (!isadev->addr) {
			mutex_unlock(&isa_lock);
			return -ENOMEM;
		}
		dev_info(shrm->dev, "Open AUDIO_LOOPBACK_MESSAGING Device\n");
		break;
	case CIQ_MESSAGING:
		isadev->addr = kzalloc(10 * 1024, GFP_KERNEL);
		if (!isadev->addr) {
			mutex_unlock(&isa_lock);
			return -ENOMEM;
		}
		dev_info(shrm->dev, "Open CIQ_MESSAGING Device\n");
		break;
	case RTC_CAL_MESSAGING:
		dev_info(shrm->dev, "Open RTC_CAL_MESSAGING Device\n");
		break;
	case IPCCTRL:
		isadev->addr = kzalloc(10 * 1024, GFP_KERNEL);
		if (!isadev->addr) {
                        mutex_unlock(&isa_lock);
                        return -ENOMEM;
                }
                dev_info(shrm->dev, "Open IPCCTRL Device\n");
                break;
	case IPCDATA:
		isadev->addr = kzalloc(10 * 1024, GFP_KERNEL);
		if (!isadev->addr) {
                        mutex_unlock(&isa_lock);
                        return -ENOMEM;
                }
                dev_info(shrm->dev, "Open IPCDATA Device\n");
                break;
	};

	mutex_unlock(&isa_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return err;
}

const struct file_operations isa_fops = {
	.owner = THIS_MODULE,
	.open = isa_open,
	.release = isa_close,
	.unlocked_ioctl = isa_ioctl,
	.mmap = isa_mmap,
	.read = isa_read,
	.write = isa_write,
	.poll = isa_select,
};

/**
 * isa_init() - module insertion function
 * @shrm:	pointer to the shrm device information structure
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
	if (isa_context == NULL) {
		dev_err(shrm->dev, "Failed to alloc memory\n");
		return -ENOMEM;
	}
	shrm->isa_context = isa_context;
	if (major) {
		dev_id = MKDEV(major, MAX_L2_HEADERS);
		retval = register_chrdev_region(dev_id, ISA_DEVICES, NAME);
	} else {
		/*
		 * L2 header of loopback device is 192(0xc0). As per the shrm
		 * protocol the minor id of the deivce is mapped to the
		 * L2 header.
		 */
		retval = alloc_chrdev_region(&dev_id, 0, MAX_L2_HEADERS, NAME);
		major = MAJOR(dev_id);
	}
	dev_dbg(shrm->dev, " major %d\n", major);

	cdev_init(&isa_context->cdev, &isa_fops);
	isa_context->cdev.owner = THIS_MODULE;
	retval = cdev_add(&isa_context->cdev, dev_id, MAX_L2_HEADERS);
	if (retval) {
		dev_err(shrm->dev, "Failed to add char device\n");
		return retval;
	}
	/* create class and device */
	isa_context->shm_class = class_create(THIS_MODULE, NAME);
	if (IS_ERR(isa_context->shm_class)) {
		dev_err(shrm->dev, "Error creating shrm class\n");
		cdev_del(&isa_context->cdev);
		retval = PTR_ERR(isa_context->shm_class);
		kfree(isa_context);
		return retval;
	}

	for (no_dev = 0; no_dev < ISA_DEVICES; no_dev++) {
		atomic_set(&isa_context->is_open[no_dev], 1);
		device_create(isa_context->shm_class, NULL,
				MKDEV(MAJOR(dev_id),
				map_dev[no_dev].l2_header), NULL,
				map_dev[no_dev].name);
	}

	isa_context->isadev = kzalloc(sizeof
				(struct isadev_context)*ISA_DEVICES,
				GFP_KERNEL);
	if (isa_context->isadev == NULL) {
		dev_err(shrm->dev, "Failed to alloc memory\n");
		return -ENOMEM;
	}
	for (no_dev = 0 ; no_dev < ISA_DEVICES ; no_dev++) {
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
	dev_dbg(shrm->dev, " SHM Char Driver added\n");
	return retval;
}

void isa_exit(struct shrm_dev *shrm)
{
	int no_dev;
	struct isadev_context *isadev;
	struct isa_driver_context *isa_context = shrm->isa_context;
	dev_t dev_id = MKDEV(major, 0);

	for (no_dev = 0 ; no_dev < ISA_DEVICES ; no_dev++) {
		device_destroy(isa_context->shm_class,
				MKDEV(MAJOR(dev_id),
				map_dev[no_dev].l2_header));
		isadev = &isa_context->isadev[no_dev];
		delete_queue(&isadev->dl_queue);
		kfree(isadev);
	}
	class_destroy(isa_context->shm_class);
	cdev_del(&isa_context->cdev);
	unregister_chrdev_region(dev_id, ISA_DEVICES);
	kfree(isa_context);
	dev_dbg(shrm->dev, " SHM Char Driver removed\n");
}
