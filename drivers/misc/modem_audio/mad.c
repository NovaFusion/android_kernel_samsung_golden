/*
 * Copyright (C) ST-Ericsson AB 2011
 *
 * Modem Audio Driver
 *
 * Author:Rahul Venkatram <rahul.venkatram@stericsson.com> for ST-Ericsson
 *	 Haridhar KALVALA<haridhar.kalvala@stericsson.com> for ST-Ericsson
 *	Amaresh Mulage<amaresh.mulage@stericsson.com> for ST-Ericsson.
 *
 * License terms:GNU General Public License (GPLv2)version 2
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fcntl.h>
#include <linux/spinlock.h>
#include <mach/mbox_channels-db5500.h>

MODULE_DESCRIPTION("Modem Audio Driver");
MODULE_LICENSE("GPLv2");

/**
 * -----------------------------------------------------
 * |		|		|			|
 * | Data[0]	|Data[1]	|Data[2]		|===>Data word 32 bits
 * -----------------------------------------------------
 * | MESSAGE	|Data		| Index			|
 * | TYPE	|length		| number		|===>READ/WRITE message
 * -----------------------------------------------------
 * -----------------------------------------------------
 * | MESSAGE	| DSP SHM addr	| max_no_of_buffers	|===> READ
 * | TYPE	| to write data	| ||buffersize		|WRITE SETUP message
 * -----------------------------------------------------
 */


#define MAD_NAME "mad"
/* Bit mask  */
#define MASK_UPPER_WORD		0xFFFF

/* channel values for each direction */
#define CHANNEL_NUM_RX	0x500
#define CHANNEL_NUM_TX	0x900

/*
 * Maximum number of datawords which can be sent
 * in the mailbox each word is 32 bits
 */
#define MAX_NR_OF_DATAWORDS	MAILBOX_NR_OF_DATAWORDS
#define MAX_NUM_RX_BUFF		NUM_DSP_BUFFER
#define NR_OF_DATAWORDS_REQD_FOR_ACK	1

/**
 * Message types, must be identical in DSP Side
 * VCS_MBOX_MSG_WRITE_IF_SETUP : DSP -> ARM
 * VCS_MBOX_MSG_WRITE_IF_SETUP_ACK : ARM -> DSP
 * VCS_MBOX_MSG_READ_IF_SETUP :  DSP -> ARM
 * VCS_MBOX_MSG_READ_IF_SETUP_ACK : ARM -> DSP
 * VCS_MBOX_MSG_IF_ENC_DATA : ARM -> DSP
 * VCS_MBOX_MSG_IF_DEC_DATA : DSP -> ARM
 */
#define VCS_MBOX_MSG_WRITE_IF_SETUP		0x200
#define VCS_MBOX_MSG_WRITE_IF_SETUP_ACK		0x201
#define VCS_MBOX_MSG_READ_IF_SETUP		0x400
#define VCS_MBOX_MSG_READ_IF_SETUP_ACK		0x401
#define VCS_MBOX_MSG_IF_ENC_DATA		0x80
#define VCS_MBOX_MSG_IF_DEC_DATA		0x100

/**
 * struct mad_data - This structure holds the state of the Modem Audio Driver.
 *
 * @dsp_shm_write_ptr : Ptr to the first TX buffer in DSP
 * @dsp_shm_read_ptr : Ptr to the first RX buffer in DSP
 * @max_tx_buffs : No. of DSP buffers available to write
 * @max_rx_buffs : No. of DSP buffers available to read
 * @write_offset : Size of each buffer in the DSP
 * @read_offset : Size of each buffer in the DSP
 * @rx_buff : Buffer for incoming data
 * @tx_buff : Buffer for outgoing data
 * @tx_buffer_num : Buffer counter for writing to DSP
 * @rx_buffer_num : Buffer counter for reading  to DSP
 * @rx_buffer_read :  Buffer counter for reading from userspace
 * @data_written : RX data message arrival indicator
 * @read_setup_msg : flag for opening read data
 * @readq :  read queue of data message
 * @lock : lock for r/w  message queue
 */
struct mad_data {
	void __iomem	*dsp_shm_write_ptr;
	void __iomem	*dsp_shm_read_ptr;
	int	max_tx_buffs;
	int	max_rx_buffs;
	int	write_offset;
	int	read_offset;
	u32	*rx_buff;
	u32	*tx_buff;
	int	tx_buffer_num;
	int	rx_buffer_num;
	int	rx_buffer_read;
	u32	data_written;
	bool	read_setup_msg;
	bool	open_check;
	wait_queue_head_t  readq;
	spinlock_t	lock;
};

static struct mad_data *mad;

static void mad_receive_cb(u32 *data, u32 length, void *priv);
static int mad_read(struct file *filp, char __user *buff, size_t count,
		loff_t *offp);
static int mad_write(struct file *filp, const char __user *buff, size_t count,
		loff_t *offp);
static unsigned int mad_select(struct file *filp, poll_table *wait);
static void mad_send_cb(u32 *data, u32 len, void *arg);
static int mad_open(struct inode *ino, struct file *filp);
static int mad_close(struct inode *ino, struct file *filp);

static const struct file_operations mad_fops = {
	.release = mad_close,
	.open    = mad_open,
	.read    = mad_read,
	.write   = mad_write,
	.poll    = mad_select,
	.owner   = THIS_MODULE,
};

static struct miscdevice mad_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = MAD_NAME,
	.fops  = &mad_fops
};

/**
 *  mad_send_cb - This function is default callback for send.
 *  @data -Pointer to the data buffer
 *  @len  -Data buffer length
 *  @arg  -Private data pointer associated with test
 */
static void mad_send_cb(u32 *data, u32 len, void *arg)
{
	dev_dbg(mad_dev.this_device, "%s", __func__);
}

/**
 * mad_receive_cb - This callback function is for receiving data from mailbox
 * @data -Pointer to the data buffer
 * @len  -length of the Mailbox
 * @arg  -Private data pointer associated with test
 */
static void mad_receive_cb(u32 *data, u32 length, void *priv)
{
	struct mad_data *mad = priv;
	struct mbox_channel_msg msg;
	u32 ack_to_dsp;
	unsigned long flags;

	/* setup message for write address */
	if (*data == VCS_MBOX_MSG_WRITE_IF_SETUP) {

		ack_to_dsp = VCS_MBOX_MSG_WRITE_IF_SETUP_ACK;

		/* if setup message comes again.unmap  */
		if (mad->dsp_shm_write_ptr != NULL) {
			iounmap(mad->dsp_shm_write_ptr);
			mad->dsp_shm_write_ptr = NULL;
			mad->write_offset = 0;
			mad->max_tx_buffs = 0;
		}

		/* convert offset to uint size */
		mad->write_offset = (data[2] & MASK_UPPER_WORD);
		mad->max_tx_buffs = (data[2] >> 16);

		mad->dsp_shm_write_ptr =  ioremap(data[1],
			mad->max_tx_buffs * mad->write_offset);
		if (mad->dsp_shm_write_ptr == NULL)
			dev_err(mad_dev.this_device, "incrt write address");

		/* Initialize all buffer numbers */
		mad->tx_buffer_num = 0;

		/* Send ACK to the DSP */
		msg.channel = CHANNEL_NUM_TX;
		msg.data = &ack_to_dsp;
		msg.length = NR_OF_DATAWORDS_REQD_FOR_ACK;
		msg.cb = mad_send_cb;
		msg.priv = mad;

		if (mbox_channel_send(&msg))
			dev_err(mad_dev.this_device, "%s: can't send data\n",
					__func__);

	} /* setup message for reading SHM */
	else if (*data == VCS_MBOX_MSG_READ_IF_SETUP) {

		ack_to_dsp = VCS_MBOX_MSG_READ_IF_SETUP_ACK;

		/* if setup message comes again.unmap  */
		if (mad->dsp_shm_read_ptr != NULL) {
			iounmap(mad->dsp_shm_read_ptr);
			mad->dsp_shm_read_ptr = NULL;
			mad->read_offset = 0;
			mad->max_rx_buffs = 0;
		}

		/*convert offset to uint size*/
		mad->read_offset	= (data[2] & MASK_UPPER_WORD);
		mad->max_rx_buffs	= data[2] >> 16;

		mad->dsp_shm_read_ptr =  ioremap(data[1],
			mad->max_rx_buffs * mad->read_offset);

		/* Initialize all buffer numbers and flags */
		mad->rx_buffer_num  = 0;
		mad->rx_buffer_read = 0;
		mad->data_written   = 0;

		/* Send ACK to the DSP */
		msg.channel = CHANNEL_NUM_TX;
		msg.data = &ack_to_dsp;
		msg.length = NR_OF_DATAWORDS_REQD_FOR_ACK;
		msg.cb = mad_send_cb;
		msg.priv = mad;

		if (mbox_channel_send(&msg))
			dev_err(mad_dev.this_device, "%s: can't send data\n",
					__func__);

		/* allow read */
		spin_lock_irqsave(&mad->lock, flags);
		mad->read_setup_msg = true;
		spin_unlock_irqrestore(&mad->lock, flags);
		/* blocked in select() */
		wake_up_interruptible(&mad->readq);

	} else if (*data == VCS_MBOX_MSG_IF_DEC_DATA) {
		/*
		* Check if you have valid message with proper length in message
		* otherwise Dont care
		*/
		if ((data[1] <=  0) || (mad->rx_buff == NULL)
				|| (mad->dsp_shm_read_ptr == NULL)) {
			if (mad->rx_buff == NULL)
				dev_warn(mad_dev.this_device, "%s :MAD closed",
						__func__);
			else
				dev_warn(mad_dev.this_device, "%s :0-len msg",
						__func__);
		} else {
			mad->rx_buff[mad->rx_buffer_num] = data[1];
			mad->rx_buffer_num++;

			/* store the offset */
			mad->rx_buff[mad->rx_buffer_num] = data[2];

			if (mad->rx_buffer_num < ((MAX_NUM_RX_BUFF * 2)-1))
				mad->rx_buffer_num++;
			else
				mad->rx_buffer_num = 0;

			spin_lock_irqsave(&mad->lock, flags);
			mad->data_written++;

			if (mad->data_written > MAX_NUM_RX_BUFF) {
				dev_warn(mad_dev.this_device,
						"%s :Read msg overflow = %u\n",
						__func__ , mad->data_written);
				/*
				 * Donot exceed MAX_NUM_RX_BUFF size of buffer
				 * TO DO overflow control
				 */
				mad->data_written = MAX_NUM_RX_BUFF ;
			}
			spin_unlock_irqrestore(&mad->lock, flags);
			wake_up_interruptible(&mad->readq);
		}
	} else {
		/* received Invalid message */
		dev_err(mad_dev.this_device, "%s : Invalid Msg", __func__);
	}
}

static int mad_read(struct file *filp, char __user *buff, size_t count,
		loff_t *offp)
{
	unsigned long flags;
	unsigned int size = 0;
	void __iomem *shm_ptr = NULL;

	dev_dbg(mad_dev.this_device, "%s", __func__);

	if (!(mad->data_written > 0)) {
		if (wait_event_interruptible(mad->readq,
			((mad->data_written > 0) &&
			(mad->dsp_shm_read_ptr != NULL))))
			return -ERESTARTSYS;
	}

	if (mad->dsp_shm_read_ptr == NULL) {
		dev_err(mad_dev.this_device, "%s :pointer err", __func__);
		return -EINVAL ;
	}

	if (mad->rx_buff[mad->rx_buffer_read] > count) {
		/*
		 * Size of message greater than buffer , this shouldnt happen
		 * It shouldnt come here : we ensured that message size
		 * smaller that buffer length
		 */
		dev_err(mad_dev.this_device, "%s : Incrct length", __func__);
		return -EFAULT;
	}
	size = mad->rx_buff[mad->rx_buffer_read];
	mad->rx_buff[mad->rx_buffer_read] = 0;
	mad->rx_buffer_read++;
	shm_ptr = (u8 *)(mad->dsp_shm_read_ptr +
			(mad->rx_buff[mad->rx_buffer_read] * mad->read_offset));
	if (copy_to_user(buff, shm_ptr, size) < 0) {
		dev_err(mad_dev.this_device, "%s :copy to user", __func__);
		return -EFAULT;
	}

	if (mad->rx_buffer_read < ((MAX_NUM_RX_BUFF*2)-1))
		mad->rx_buffer_read++;
	else
		mad->rx_buffer_read = 0;

	spin_lock_irqsave(&mad->lock, flags);
	mad->data_written--;
	if (mad->data_written < 0) {
		/* Means wrong read*/
		mad->data_written = 0;
		dev_err(mad_dev.this_device, "%s :data Rcev err", __func__);
	}
	spin_unlock_irqrestore(&mad->lock, flags);
	return size;
}

static int mad_write(struct file *filp, const char __user *buff, size_t count,
								loff_t *offp)
{
	int retval = 0;
	void __iomem  *dsp_write_address;
	struct mbox_channel_msg msg;

	dev_dbg(mad_dev.this_device, "%s", __func__);

	/* check for valid write pointer else skip writing*/
	if (mad->dsp_shm_write_ptr == NULL) {
		dev_err(mad_dev.this_device, "%s :Illegal memory", __func__);
		return -EFAULT;
	}

	dsp_write_address = (mad->dsp_shm_write_ptr +
				(mad->tx_buffer_num * mad->write_offset));

	if (copy_from_user(dsp_write_address, buff, count)) {
		dev_err(mad_dev.this_device, "%s:copy_from_user\n", __func__);
		return -EFAULT;
	}

	mad->tx_buff[0] = VCS_MBOX_MSG_IF_ENC_DATA;
	mad->tx_buff[1] = count;
	mad->tx_buff[2] = mad->tx_buffer_num;

	if (mad->tx_buffer_num < (mad->max_tx_buffs-1))
		mad->tx_buffer_num++;
	else
		mad->tx_buffer_num = 0;

	msg.channel = CHANNEL_NUM_TX;
	msg.data = mad->tx_buff;
	msg.length = MAX_NR_OF_DATAWORDS;
	msg.cb = mad_send_cb;
	msg.priv = mad;

	retval = mbox_channel_send(&msg);
	if (retval) {
		dev_err(mad_dev.this_device, "%s:can't send data", __func__);
		return retval;
	}
	return count;
}

static unsigned int mad_select(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	unsigned long flags;

	dev_dbg(mad_dev.this_device, "%s", __func__);

	poll_wait(filp, &mad->readq,  wait);
	spin_lock_irqsave(&mad->lock, flags);

	if ((true == mad->read_setup_msg) && (mad->data_written > 0))
		mask |= POLLIN | POLLRDNORM;    /* allow readable */
	spin_unlock_irqrestore(&mad->lock, flags);

	return mask;
}

static int mad_open(struct inode *ino, struct file *filp)
{
	int	err = 0;

	dev_dbg(mad_dev.this_device, "%s", __func__);

	if (mad->open_check == true) {
		dev_err(mad_dev.this_device, "%s :Already opened", __func__);
		return -EFAULT;
	}

	mad->rx_buff =  kzalloc((MAX_NUM_RX_BUFF*2 *
				sizeof(mad->rx_buff)), GFP_KERNEL);

	if (mad->rx_buff == NULL) {
		dev_err(mad_dev.this_device, "%s:RX memory\n", __func__);
		err = -ENOMEM;
		goto error;
	}

	mad->tx_buff =  kzalloc(MAX_NR_OF_DATAWORDS, GFP_KERNEL);
	if (mad->tx_buff == NULL) {
		dev_err(mad_dev.this_device, "%s:TX memory\n", __func__);
		err = -ENOMEM;
		goto error;
	}

	/* Init spinlock for critical section access*/
	spin_lock_init(&mad->lock);
	init_waitqueue_head(&(mad->readq));

	err = mbox_channel_register(CHANNEL_NUM_RX, mad_receive_cb, mad);
	if (err) {
		dev_err(mad_dev.this_device, "%s: register err", __func__);
		err = -EFAULT;
		goto error;
	}
	mad->open_check = true;

	return 0;
error:
	kfree(mad->rx_buff);
	kfree(mad->tx_buff);
	return err;
}

static int mad_close(struct inode *ino, struct file *filp)
{
	dev_dbg(mad_dev.this_device, "%s", __func__);

	if (mbox_channel_deregister(CHANNEL_NUM_RX)) {
		dev_err(mad_dev.this_device, "%s:deregister err", __func__);
		return -EFAULT;
	}
	kfree(mad->rx_buff);
	kfree(mad->tx_buff);
	mad->data_written = 0;
	mad->rx_buffer_num = 0;
	mad->rx_buffer_read = 0;
	mad->open_check = false;

	return 0;
}

static int __init mad_init(void)
{
	dev_dbg(mad_dev.this_device, "%s", __func__);

	mad = kzalloc(sizeof(*mad), GFP_KERNEL);
	if (mad == NULL) {
		dev_err(mad_dev.this_device, "%s :MAD failed", __func__);
		return -ENOMEM;
	}

	return misc_register(&mad_dev);
}
module_init(mad_init);

static void  __exit mad_exit(void)
{
	dev_dbg(mad_dev.this_device, "%s", __func__);

	 if (mad->dsp_shm_write_ptr != NULL) {
		 iounmap(mad->dsp_shm_write_ptr);
		 mad->dsp_shm_write_ptr = NULL;
	 }

	 if (mad->dsp_shm_read_ptr != NULL) {
		 iounmap(mad->dsp_shm_read_ptr);
		 mad->dsp_shm_read_ptr = NULL;
	 }

	 kfree(mad);
	 misc_deregister(&mad_dev);
}
