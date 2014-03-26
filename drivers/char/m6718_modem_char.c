/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_char.c
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * M6718 modem char device interface.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/modem/m6718_spi/modem_char.h>
#include <linux/modem/m6718_spi/modem_driver.h>

#define NAME "IPC_ISA"

#define MAX_PDU_SIZE           (2000) /* largest frame we need to send */
#define MAX_RX_FIFO_ENTRIES    (10)
#define SIZE_OF_RX_FIFO        (MAX_PDU_SIZE * MAX_RX_FIFO_ENTRIES)
#define SIZE_OF_TX_COPY_BUFFER (MAX_PDU_SIZE) /* only need 1 at a time */

static u8 message_fifo[MODEM_M6718_SPI_MAX_CHANNELS][SIZE_OF_RX_FIFO];

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_LOOPBACK
static u8 wr_mlb_msg[SIZE_OF_TX_COPY_BUFFER];
#endif
static u8 wr_audio_msg[SIZE_OF_TX_COPY_BUFFER];

struct map_device {
	u8 l2_header;
	u8 idx;
	char *name;
};

static struct map_device map_dev[] = {
	{MODEM_M6718_SPI_CHN_ISI, 0, "isi"},
	{MODEM_M6718_SPI_CHN_AUDIO, 1, "modemaudio"},
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_LOOPBACK
	{MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0, 2, "master_loopback0"},
	{MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK0, 3, "slave_loopback0"},
	{MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1, 4, "master_loopback1"},
	{MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK1, 5, "slave_loopback1"},
#endif
};

/*
 * major - variable exported as module_param to specify major node number
 */
static int major;
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

/* global fops mutex */
static DEFINE_MUTEX(isa_lock);

/**
 * modem_get_cdev_index() - return the index mapped to l2 header
 * @l2_header:	L2 header
 *
 * struct map_device maps the index(count) with the device L2 header.
 * This function returns the index for the provided L2 header in case
 * of success else -ve value.
 */
int modem_get_cdev_index(u8 l2_header)
{
	u8 cnt;
	for (cnt = 0; cnt < ARRAY_SIZE(map_dev); cnt++) {
		if (map_dev[cnt].l2_header == l2_header)
			return map_dev[cnt].idx;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(modem_get_cdev_index);

/**
 * modem_get_cdev_l2header() - return l2_header mapped to the index
 * @idx:	index
 *
 * struct map_device maps the index(count) with the device L2 header.
 * This function returns the L2 header for the given index in case
 * of success else -ve value.
 */
int modem_get_cdev_l2header(u8 idx)
{
	u8 cnt;
	for (cnt = 0; cnt < ARRAY_SIZE(map_dev); cnt++) {
		if (map_dev[cnt].idx == idx)
			return map_dev[cnt].l2_header;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(modem_get_cdev_l2header);

/**
 * modem_isa_reset() - reset device interfaces
 * @modem_spi_dev: pointer to modem driver information structure
 *
 * Emptys the queue for each L2 mux channel.
 */
void modem_isa_reset(struct modem_spi_dev *modem_spi_dev)
{
	struct isa_device_context *isadev;
	struct isa_driver_context *isa_context;
	struct queue_element *cur_msg = NULL;
	struct list_head *cur_msg_ptr = NULL;
	struct list_head *msg_ptr;
	struct message_queue *q;
	int devidx;

	dev_info(modem_spi_dev->dev, "resetting char device queues\n");

	isa_context = modem_spi_dev->isa_context;
	for (devidx = 0; devidx < ARRAY_SIZE(map_dev); devidx++) {
		isadev = &isa_context->isadev[devidx];
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
		q->size = SIZE_OF_RX_FIFO;
		q->readptr = 0;
		q->writeptr = 0;
		q->no = 0;

		/* wake up the blocking read/select */
		atomic_set(&q->q_rp, 1);
		wake_up_interruptible(&q->wq_readable);
		spin_unlock_bh(&q->update_lock);
	}
}
EXPORT_SYMBOL_GPL(modem_isa_reset);

static void create_queue(struct message_queue *q, u8 channel,
	struct modem_spi_dev *modem_spi_dev)
{
	q->channel = channel;
	q->fifo_base = (u8 *)&message_fifo[channel];
	q->size = SIZE_OF_RX_FIFO;
	q->free = q->size;
	q->readptr = 0;
	q->writeptr = 0;
	q->no = 0;
	spin_lock_init(&q->update_lock);
	atomic_set(&q->q_rp, 0);
	init_waitqueue_head(&q->wq_readable);
	INIT_LIST_HEAD(&q->msg_list);
	q->modem_spi_dev = modem_spi_dev;
}

static void delete_queue(struct message_queue *q)
{
	q->size = 0;
	q->readptr = 0;
	q->writeptr = 0;
}

/**
 * modem_isa_queue_msg() - Add a message to a queue queue
 * @q:		message queue
 * @size:	size in bytes
 *
 * This function tries to allocate size bytes in FIFO q.
 * It returns negative number when no memory can be allocated
 * currently.
 */
int modem_isa_queue_msg(struct message_queue *q, u32 size)
{
	struct queue_element *new_msg = NULL;
	struct modem_spi_dev *modem_spi_dev = q->modem_spi_dev;

	new_msg = kmalloc(sizeof(struct queue_element), GFP_ATOMIC);
	if (new_msg == NULL) {
		dev_err(modem_spi_dev->dev,
			"failed to allocate memory for queue item\n");
		return -ENOMEM;
	}
	new_msg->offset = q->writeptr;
	new_msg->size = size;
	new_msg->no = q->no++;

	/* check for overflow condition */
	if (q->readptr <= q->writeptr) {
		if (((q->writeptr - q->readptr) + size) >= q->size) {
			dev_err(modem_spi_dev->dev, "rx q++ ch %d %d (%d)\n",
				q->channel, size, q->free);
			dev_err(modem_spi_dev->dev,
				"ch%d buffer overflow, frame discarded\n",
				q->channel);
			return -ENOMEM;
		}
	} else {
		if ((q->writeptr + size) >= q->readptr) {
			dev_err(modem_spi_dev->dev, "rx q++ ch %d %d (%d)\n",
				q->channel, size, q->free);
			dev_err(modem_spi_dev->dev,
				"ch%d buffer overflow, frame discarded\n",
				q->channel);
			return -ENOMEM;
		}
	}
	q->free -= size;
	q->writeptr = (q->writeptr + size) % q->size;
	if (list_empty(&q->msg_list)) {
		list_add_tail(&new_msg->entry, &q->msg_list);
		/* There can be 2 blocking calls: read and another select */
		atomic_set(&q->q_rp, 1);
		wake_up_interruptible(&q->wq_readable);
	} else {
		list_add_tail(&new_msg->entry, &q->msg_list);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(modem_isa_queue_msg);

/**
 * modem_isa_unqueue_msg() - remove a message from the msg queue
 * @q:	message queue
 *
 * Deletes a message from the message list associated with message
 * queue q and also updates read ptr. If the message list is empty
 * then a flag is set to block the select and read calls of the paricular queue.
 *
 * The message list is FIFO style and message is always added to tail and
 * removed from head.
 */
int modem_isa_unqueue_msg(struct message_queue *q)
{
	struct queue_element *old_msg = NULL;
	struct list_head *msg_ptr = NULL;
	struct list_head *old_msg_ptr = NULL;

	list_for_each_safe(old_msg_ptr, msg_ptr, &q->msg_list) {
		old_msg = list_entry(old_msg_ptr, struct queue_element, entry);
		if (old_msg == NULL)
			return -EFAULT;
		list_del(old_msg_ptr);
		q->readptr = (q->readptr + old_msg->size) % q->size;
		q->free += old_msg->size;
		kfree(old_msg);
		break;
	}
	if (list_empty(&q->msg_list))
		atomic_set(&q->q_rp, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(modem_isa_unqueue_msg);

/**
 * modem_isa_msg_size() - retrieve the size of the most recent message
 * @q:	message queue
 */
int modem_isa_msg_size(struct message_queue *q)
{
	struct queue_element *new_msg = NULL;
	struct list_head *msg_list;
	unsigned long flags;
	int size = 0;

	spin_lock_irqsave(&q->update_lock, flags);
	list_for_each(msg_list, &q->msg_list) {
		new_msg = list_entry(msg_list, struct queue_element, entry);
		if (new_msg == NULL) {
			spin_unlock_irqrestore(&q->update_lock, flags);
			return -EFAULT;
		}
		size = new_msg->size;
		break;
	}
	spin_unlock_irqrestore(&q->update_lock, flags);
	return size;
}
EXPORT_SYMBOL_GPL(modem_isa_msg_size);

static u32 isa_select(struct file *filp, struct poll_table_struct *wait)
{
	struct isa_device_context *isadev = filp->private_data;
	struct modem_spi_dev *modem_spi_dev = isadev->dl_queue.modem_spi_dev;
	struct message_queue *q;
	u32 m = iminor(filp->f_path.dentry->d_inode);
	u8 idx = modem_get_cdev_index(m);

	if (modem_spi_dev->msr_flag)
		return -ENODEV;
	if (isadev->device_id != idx)
		return -1;

	q = &isadev->dl_queue;
	poll_wait(filp, &q->wq_readable, wait);
	if (atomic_read(&q->q_rp) == 1)
		return POLLIN | POLLRDNORM;
	return 0;
}

static ssize_t isa_read(struct file *filp, char __user *buf,
	size_t len, loff_t *ppos)
{
	u32 size = 0;
	int ret;
	char *psrc;
	struct isa_device_context *isadev =
		(struct isa_device_context *)filp->private_data;
	struct message_queue *q = &isadev->dl_queue;
	struct modem_spi_dev *modem_spi_dev = q->modem_spi_dev;
	u32 msgsize;
	unsigned long flags;

	if (len <= 0)
		return -EFAULT;

	if (modem_spi_dev->msr_flag) {
		atomic_set(&q->q_rp, 0);
		return -ENODEV;
	}

	spin_lock_irqsave(&q->update_lock, flags);
	if (list_empty(&q->msg_list)) {
		spin_unlock_irqrestore(&q->update_lock, flags);
		dev_dbg(modem_spi_dev->dev, "waiting for data on device %d\n",
			isadev->device_id);
		if (wait_event_interruptible(q->wq_readable,
				atomic_read(&q->q_rp) == 1))
			return -ERESTARTSYS;
	} else {
		spin_unlock_irqrestore(&q->update_lock, flags);
	}

	if (modem_spi_dev->msr_flag) {
		atomic_set(&q->q_rp, 0);
		return -ENODEV;
	}

	msgsize = modem_isa_msg_size(q);
	if (len < msgsize)
		return -EINVAL;

	if ((q->readptr + msgsize) >= q->size) {
		psrc = (char *)buf;
		size = (q->size - q->readptr);
		/* copy first part of msg */
		if (copy_to_user(psrc, (u8 *)(q->fifo_base + q->readptr),
				size))
			return -EFAULT;

		psrc += size;
		/* copy second part of msg at the top of fifo */
		if (copy_to_user(psrc, (u8 *)(q->fifo_base),
				(msgsize - size)))
			return -EFAULT;
	} else {
		if (copy_to_user(buf, (u8 *)(q->fifo_base + q->readptr),
				msgsize))
			return -EFAULT;
	}

	spin_lock_irqsave(&q->update_lock, flags);
	ret = modem_isa_unqueue_msg(q);
	if (ret < 0)
		msgsize = ret;
	spin_unlock_irqrestore(&q->update_lock, flags);
	return msgsize;
}

static ssize_t isa_write(struct file *filp, const char __user *buf,
	size_t len, loff_t *ppos)
{
	struct isa_device_context *isadev = filp->private_data;
	struct message_queue *q = &isadev->dl_queue;
	struct modem_spi_dev *modem_spi_dev = q->modem_spi_dev;
	struct isa_driver_context *isa_context = modem_spi_dev->isa_context;
	void *addr = 0;
	int err;
	int l2_header;
	int ret = 0;
	unsigned long flags;

	if (len <= 0 || buf == NULL)
		return -EFAULT;

	if (len > SIZE_OF_TX_COPY_BUFFER) {
		dev_err(modem_spi_dev->dev,
			"invalid message size %d! max is %d bytes\n",
			len, SIZE_OF_TX_COPY_BUFFER);
		return -EFAULT;
	}

	l2_header = modem_get_cdev_l2header(isadev->device_id);
	if (l2_header < 0) {
		dev_err(modem_spi_dev->dev, "invalid L2 channel!\n");
		return l2_header;
	}

	switch (l2_header) {
	case MODEM_M6718_SPI_CHN_AUDIO:
		addr = (void *)wr_audio_msg;
		break;
	case MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0:
	case MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK0:
	case MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1:
	case MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK1:
		addr = (void *)wr_mlb_msg;
		break;
	default:
		dev_dbg(modem_spi_dev->dev, "invalid device!\n");
		return -EFAULT;
	}

	if (copy_from_user(addr, buf, len))
		return -EFAULT;

	/*
	 * Special handling for audio channel:
	 *   uses a mutext instead of a spinlock
	 */
	if (l2_header == MODEM_M6718_SPI_CHN_AUDIO ||
		l2_header == MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1) {
		mutex_lock(&isa_context->audio_tx_mutex);
		err = modem_m6718_spi_send(modem_spi_dev, l2_header, len, addr);
		if (!err)
			ret = len;
		else
			ret = err;
		mutex_unlock(&modem_spi_dev->isa_context->audio_tx_mutex);
	} else {
		spin_lock_irqsave(&isa_context->common_tx_lock, flags);
		err = modem_m6718_spi_send(modem_spi_dev, l2_header, len, addr);
		if (!err)
			ret = len;
		else
			ret = err;
		spin_unlock_irqrestore(&isa_context->common_tx_lock, flags);
	}
	return ret;
}

static long isa_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int isa_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static int isa_close(struct inode *inode, struct file *filp)
{
	struct isa_device_context *isadev = filp->private_data;
	struct modem_spi_dev *modem_spi_dev = isadev->dl_queue.modem_spi_dev;
	struct isa_driver_context *isa_context = modem_spi_dev->isa_context;
	u8 m;
	int idx;

	mutex_lock(&isa_lock);
	m = iminor(filp->f_path.dentry->d_inode);
	idx = modem_get_cdev_index(m);
	if (idx < 0) {
		dev_err(modem_spi_dev->dev, "invalid L2 channel!\n");
		return idx;
	}

	if (atomic_dec_and_test(&isa_context->is_open[idx])) {
		atomic_inc(&isa_context->is_open[idx]);
		dev_err(modem_spi_dev->dev, "device is not open yet!\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}
	atomic_set(&isa_context->is_open[idx], 1);

	switch (m) {
	case MODEM_M6718_SPI_CHN_AUDIO:
		dev_dbg(modem_spi_dev->dev, "close channel AUDIO\n");
		break;
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_LOOPBACK
	case MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0:
		dev_dbg(modem_spi_dev->dev, "close channel MASTER_LOOPBACK0\n");
		break;
	case MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK0:
		dev_dbg(modem_spi_dev->dev, "close channel SLAVE_LOOPBACK0\n");
		break;
	case MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1:
		dev_dbg(modem_spi_dev->dev, "close channel MASTER_LOOPBACK1\n");
		break;
	case MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK1:
		dev_dbg(modem_spi_dev->dev, "close channel SLAVE_LOOPBACK1\n");
		break;
#endif
	default:
		dev_dbg(modem_spi_dev->dev, "invalid device\n");
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}
	mutex_unlock(&isa_lock);
	return 0;
}

static int isa_open(struct inode *inode, struct file *filp)
{
	int err = 0;
	u8 m;
	int idx;
	struct isa_device_context *isadev;
	struct isa_driver_context *isa_context =
		container_of(inode->i_cdev, struct isa_driver_context, cdev);
	struct modem_spi_dev *modem_spi_dev =
		isa_context->isadev->dl_queue.modem_spi_dev;

	if (!modem_m6718_spi_is_boot_done()) {
		dev_dbg(modem_spi_dev->dev,
			"failed to open device, boot is not complete\n");
		err = -EBUSY;
		goto out;
	}

	mutex_lock(&isa_lock);
	m = iminor(inode);
	idx = modem_get_cdev_index(m);
	if (idx < 0) {
		dev_err(modem_spi_dev->dev, "invalid device\n");
		err = -ENODEV;
		goto cleanup;
	}

	if (!atomic_dec_and_test(&isa_context->is_open[idx])) {
		atomic_inc(&isa_context->is_open[idx]);
		dev_err(modem_spi_dev->dev, "device is already open\n");
		err = -EBUSY;
		goto cleanup;
	}

	isadev = &isa_context->isadev[idx];
	if (filp != NULL)
		filp->private_data = isadev;

	switch (m) {
	case MODEM_M6718_SPI_CHN_AUDIO:
		dev_dbg(modem_spi_dev->dev, "open channel AUDIO\n");
		break;
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_LOOPBACK
	case MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0:
		dev_dbg(modem_spi_dev->dev, "open channel MASTER_LOOPBACK0\n");
		break;
	case MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK0:
		dev_dbg(modem_spi_dev->dev, "open channel SLAVE_LOOPBACK0\n");
		break;
	case MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1:
		dev_dbg(modem_spi_dev->dev, "open channel MASTER_LOOPBACK1\n");
		break;
	case MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK1:
		dev_dbg(modem_spi_dev->dev, "open channel SLAVE_LOOPBACK1\n");
		break;
#endif
	}

cleanup:
	mutex_unlock(&isa_lock);
out:
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
 * modem_isa_init() - initialise the modem char device interfaces
 * @modem_spi_dev: pointer to the modem driver information structure
 *
 * This function registers registers as a char device driver and creates the
 * char device nodes supported by the modem.
 */
int modem_isa_init(struct modem_spi_dev *modem_spi_dev)
{
	dev_t dev_id;
	int retval;
	int devidx;
	struct isa_device_context *isadev;
	struct isa_driver_context *isa_context;

	dev_dbg(modem_spi_dev->dev, "registering char device interfaces\n");

	isa_context = kzalloc(sizeof(struct isa_driver_context), GFP_KERNEL);
	if (isa_context == NULL) {
		dev_err(modem_spi_dev->dev, "failed to allocate context\n");
		retval = -ENOMEM;
		goto rollback;
	}

	modem_spi_dev->isa_context = isa_context;
	if (major) {
		/* major node specified at module load */
		dev_id = MKDEV(major, 0);
		retval = register_chrdev_region(dev_id,
			MODEM_M6718_SPI_MAX_CHANNELS, NAME);
	} else {
		retval = alloc_chrdev_region(&dev_id, 0,
			MODEM_M6718_SPI_MAX_CHANNELS, NAME);
		major = MAJOR(dev_id);
	}

	dev_dbg(modem_spi_dev->dev, "device major is %d\n", major);

	cdev_init(&isa_context->cdev, &isa_fops);
	isa_context->cdev.owner = THIS_MODULE;
	retval = cdev_add(&isa_context->cdev, dev_id,
		MODEM_M6718_SPI_MAX_CHANNELS);
	if (retval) {
		dev_err(modem_spi_dev->dev, "failed to add char device\n");
		goto rollback_register;
	}

	isa_context->modem_class = class_create(THIS_MODULE, NAME);
	if (IS_ERR(isa_context->modem_class)) {
		dev_err(modem_spi_dev->dev, "failed to create modem class\n");
		retval = PTR_ERR(isa_context->modem_class);
		goto rollback_add_dev;
	}
	isa_context->isadev = kzalloc(sizeof(struct isa_device_context) *
				MODEM_M6718_SPI_MAX_CHANNELS, GFP_KERNEL);
	if (isa_context->isadev == NULL) {
		dev_err(modem_spi_dev->dev,
			"failed to allocate device context\n");
		goto rollback_create_dev;
	}

	for (devidx = 0; devidx < ARRAY_SIZE(map_dev); devidx++) {
		atomic_set(&isa_context->is_open[devidx], 1);
		device_create(isa_context->modem_class,
			NULL,
			MKDEV(MAJOR(dev_id), map_dev[devidx].l2_header),
			NULL,
			map_dev[devidx].name);

		isadev = &isa_context->isadev[devidx];
		isadev->device_id = devidx;
		create_queue(&isadev->dl_queue,
			isadev->device_id, modem_spi_dev);

		dev_dbg(modem_spi_dev->dev, "created device %d (%s) (%d.%d)\n",
			devidx, map_dev[devidx].name, major,
			map_dev[devidx].l2_header);
	}

	mutex_init(&isa_context->audio_tx_mutex);
	spin_lock_init(&isa_context->common_tx_lock);

	dev_dbg(modem_spi_dev->dev, "registered modem char devices\n");
	return 0;

rollback_create_dev:
	for (devidx = 0; devidx < ARRAY_SIZE(map_dev); devidx++) {
		device_destroy(isa_context->modem_class,
			MKDEV(MAJOR(dev_id), map_dev[devidx].l2_header));
	}
	class_destroy(isa_context->modem_class);
rollback_add_dev:
	cdev_del(&isa_context->cdev);
rollback_register:
	unregister_chrdev_region(dev_id, MODEM_M6718_SPI_MAX_CHANNELS);
	kfree(isa_context);
	modem_spi_dev->isa_context = NULL;
rollback:
	return retval;
}
EXPORT_SYMBOL_GPL(modem_isa_init);

/**
 * modem_isa_exit() - remove the char device interfaces and clean up
 * @modem_spi_dev: pointer to the modem driver information structure
 */
void modem_isa_exit(struct modem_spi_dev *modem_spi_dev)
{
	int devidx;
	struct isa_device_context *isadev;
	struct isa_driver_context *isa_context = modem_spi_dev->isa_context;
	dev_t dev_id = MKDEV(major, 0);

	if (!modem_spi_dev || !modem_spi_dev->isa_context)
		return;

	for (devidx = 0; devidx < ARRAY_SIZE(map_dev); devidx++)
		device_destroy(isa_context->modem_class,
				MKDEV(MAJOR(dev_id),
				map_dev[devidx].l2_header));
	for (devidx = 0; devidx < MODEM_M6718_SPI_MAX_CHANNELS; devidx++) {
		isadev = &isa_context->isadev[devidx];
		delete_queue(&isadev->dl_queue);
		kfree(isadev);
	}
	class_destroy(isa_context->modem_class);
	cdev_del(&isa_context->cdev);
	unregister_chrdev_region(dev_id, MODEM_M6718_SPI_MAX_CHANNELS);
	kfree(isa_context);
	modem_spi_dev->isa_context = NULL;
	dev_dbg(modem_spi_dev->dev, "removed modem char devices\n");
}
EXPORT_SYMBOL_GPL(modem_isa_exit);

MODULE_AUTHOR("Chris Blair <chris.blair@stericsson.com>");
MODULE_DESCRIPTION("M6718 modem IPC char device interface");
MODULE_LICENSE("GPL v2");
