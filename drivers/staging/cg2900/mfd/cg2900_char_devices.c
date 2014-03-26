/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson connectivity controller.
 */
#define NAME					"cg2900_char_dev"
#define pr_fmt(fmt)				NAME ": " fmt "\n"

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/mfd/core.h>
#include <linux/clk.h>

#include "cg2900.h"
#include "cg2900_core.h"

#define MAIN_DEV				(dev->dev)

/**
 * struct char_dev_user - Stores device information.
 * @dev:		Current device.
 * @miscdev:		Registered device struct.
 * @filp:		Current file pointer.
 * @name:		Name of device.
 * @rx_queue:		Data queue.
 * @rx_wait_queue:	Wait queue.
 * @reset_wait_queue:	Reset Wait queue.
 * @read_mutex:		Read mutex.
 * @write_mutex:	Write mutex.
 * @list:		List header for inserting into device list.
 */
struct char_dev_user {
	struct device		*dev;
	struct miscdevice	miscdev;
	struct file		*filp;
	char			*name;
	struct sk_buff_head	rx_queue;
	wait_queue_head_t	rx_wait_queue;
	wait_queue_head_t	reset_wait_queue;
	struct mutex		read_mutex;
	struct mutex		write_mutex;
	struct list_head	list;
};

/**
 * struct char_info - Stores all current users.
 * @open_mutex:	Open mutex (used for both open and release).
 * @man_mutex:	Management mutex.
 * @dev_users:	List of char dev users.
 */
struct char_info {
	struct mutex		open_mutex;
	struct mutex		man_mutex;
	struct list_head	dev_users;
};

static struct char_info *char_info;

/**
 * char_dev_read_cb() - Handle data received from controller.
 * @dev:	Device receiving data.
 * @skb:	Buffer with data coming from controller.
 *
 * The char_dev_read_cb() function handles data received from the CG2900 driver.
 */
static void char_dev_read_cb(struct cg2900_user_data *dev, struct sk_buff *skb)
{
	struct char_dev_user *char_dev = dev_get_drvdata(dev->dev);

	dev_dbg(dev->dev, "char_dev_read_cb len %d\n", skb->len);

	skb_queue_tail(&char_dev->rx_queue, skb);

	wake_up_interruptible(&char_dev->rx_wait_queue);
}

/**
 * char_dev_reset_cb() - Handle reset from controller.
 * @dev:	Device resetting.
 *
 * The char_dev_reset_cb() function handles reset from the CG2900 driver.
 */
static void char_dev_reset_cb(struct cg2900_user_data *dev)
{
	struct char_dev_user *char_dev = dev_get_drvdata(dev->dev);

	dev_dbg(dev->dev, "char_dev_reset_cb\n");

	wake_up_interruptible(&char_dev->rx_wait_queue);
	wake_up_interruptible(&char_dev->reset_wait_queue);
}

/**
 * char_dev_open() - Open char device.
 * @inode:	Device driver information.
 * @filp:	Pointer to the file struct.
 *
 * The char_dev_open() function opens the char device.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL if device cannot be found in device list.
 *   Error codes from cg2900->open.
 */
static int char_dev_open(struct inode *inode, struct file *filp)
{
	int err;
	int minor;
	struct char_dev_user *dev = NULL;
	struct char_dev_user *tmp;
	struct list_head *cursor;
	struct cg2900_user_data *user;

	mutex_lock(&char_info->open_mutex);

	minor = iminor(inode);

	/* Find the device for this file */
	mutex_lock(&char_info->man_mutex);
	list_for_each(cursor, &char_info->dev_users) {
		tmp = list_entry(cursor, struct char_dev_user, list);
		if (tmp->miscdev.minor == minor) {
			dev = tmp;
			break;
		}
	}
	mutex_unlock(&char_info->man_mutex);
	if (!dev) {
		pr_err("Could not identify device in inode");
		err = -EINVAL;
		goto error_handling;
	}

	filp->private_data = dev;
	dev->filp = filp;
	user = dev_get_platdata(dev->dev);

	/* First initiate wait queues for this device. */
	init_waitqueue_head(&dev->rx_wait_queue);
	init_waitqueue_head(&dev->reset_wait_queue);

	/* Register to CG2900 Driver */
	err = user->open(user);
	if (err) {
		dev_err(MAIN_DEV,
			"Couldn't register to CG2900 for H:4 channel %s\n",
			dev->name);
		goto error_handling;
	}
	dev_info(MAIN_DEV, "char_dev %s opened\n", dev->name);

error_handling:
	mutex_unlock(&char_info->open_mutex);
	return err;
}

/**
 * char_dev_flush() - flushes read queue.
 * @filp:	Pointer to the file struct.
 *
 * The char_dev_flush() function purges read queue
 * and releases rx and reset wait queue
 *
 * Returns:
 *   0 if there is no error.
 *   -EBADF if NULL pointer was supplied in private data.
 */
static int char_dev_flush(struct file *filp, fl_owner_t id)
{
	int err = 0;
	struct char_dev_user *dev = filp->private_data;

	pr_debug("char_dev_flush");

	if (!dev) {
		pr_err("char_dev_flush: Calling with NULL pointer");
		return -EBADF;
	}

	mutex_lock(&char_info->open_mutex);
	mutex_lock(&dev->read_mutex);
	mutex_lock(&dev->write_mutex);

	/* Purge the queue */
	skb_queue_purge(&dev->rx_queue);

	wake_up_interruptible(&dev->rx_wait_queue);
	wake_up_interruptible(&dev->reset_wait_queue);

	mutex_unlock(&dev->write_mutex);
	mutex_unlock(&dev->read_mutex);
	mutex_unlock(&char_info->open_mutex);

	return err;
}

/**
 * char_dev_release() - Release char device.
 * @inode:	Device driver information.
 * @filp:	Pointer to the file struct.
 *
 * The char_dev_release() function release the char device.
 *
 * Returns:
 *   0 if there is no error.
 *   -EBADF if NULL pointer was supplied in private data.
 */
static int char_dev_release(struct inode *inode, struct file *filp)
{
	int err = 0;
	struct char_dev_user *dev = filp->private_data;
	struct cg2900_user_data *user;

	pr_debug("char_dev_release");

	if (!dev) {
		pr_err("char_dev_release: Calling with NULL pointer");
		return -EBADF;
	}

	mutex_lock(&char_info->open_mutex);
	mutex_lock(&dev->read_mutex);
	mutex_lock(&dev->write_mutex);

	user = dev_get_platdata(dev->dev);
	if (user->opened)
		user->close(user);

	dev_info(MAIN_DEV, "char_dev %s closed\n", dev->name);

	filp->private_data = NULL;
	dev->filp = NULL;
	wake_up_interruptible(&dev->rx_wait_queue);
	wake_up_interruptible(&dev->reset_wait_queue);

	/* Purge the queue since the device is closed now */
	skb_queue_purge(&dev->rx_queue);

	mutex_unlock(&dev->write_mutex);
	mutex_unlock(&dev->read_mutex);
	mutex_unlock(&char_info->open_mutex);

	return err;
}

/**
 * char_dev_read() - Queue and copy buffer to user.
 * @filp:	Pointer to the file struct.
 * @buf:	Received buffer.
 * @count:	Size of buffer.
 * @f_pos:	Position in buffer.
 *
 * The char_dev_read() function queues and copy the received buffer to
 * the user space char device. If no data is available this function will block.
 *
 * Returns:
 *   Bytes successfully read (could be 0).
 *   -EBADF if NULL pointer was supplied in private data.
 *   -EFAULT if copy_to_user fails.
 *   Error codes from wait_event_interruptible.
 */
static ssize_t char_dev_read(struct file *filp, char __user *buf, size_t count,
			     loff_t *f_pos)
{
	struct char_dev_user *dev = filp->private_data;
	struct cg2900_user_data *user;
	struct sk_buff *skb;
	int bytes_to_copy;
	int err = 0;

	pr_debug("char_dev_read");

	if (!dev) {
		pr_err("char_dev_read: Calling with NULL pointer");
		return -EBADF;
	}
	mutex_lock(&dev->read_mutex);

	user = dev_get_platdata(dev->dev);

	if (user->opened && skb_queue_empty(&dev->rx_queue)) {
		err = wait_event_interruptible(dev->rx_wait_queue,
				(!(skb_queue_empty(&dev->rx_queue))) ||
				!user->opened);
		if (err) {
			dev_err(MAIN_DEV, "Failed to wait for event\n");
			goto error_handling;
		}
	}

	if (!user->opened) {
		dev_err(MAIN_DEV, "Channel has been closed\n");
		err = -EBADF;
		goto error_handling;
	}

	skb = skb_dequeue(&dev->rx_queue);
	if (!skb) {
		dev_dbg(MAIN_DEV,
			"skb queue is empty - return with zero bytes\n");
		bytes_to_copy = 0;
		goto finished;
	}

	bytes_to_copy = min(count, skb->len);

	err = copy_to_user(buf, skb->data, bytes_to_copy);
	if (err) {
		dev_err(MAIN_DEV, "Error %d from copy_to_user\n", err);
		skb_queue_head(&dev->rx_queue, skb);
		err = -EFAULT;
		goto error_handling;
	}

	skb_pull(skb, bytes_to_copy);

	if (skb->len > 0)
		skb_queue_head(&dev->rx_queue, skb);
	else
		kfree_skb(skb);

	goto finished;

error_handling:
	mutex_unlock(&dev->read_mutex);
	return (ssize_t)err;
finished:
	mutex_unlock(&dev->read_mutex);
	return bytes_to_copy;
}

/**
 * char_dev_write() - Copy buffer from user and write to CG2900 driver.
 * @filp:	Pointer to the file struct.
 * @buf:	Write buffer.
 * @count:	Size of the buffer write.
 * @f_pos:	Position of buffer.
 *
 * Returns:
 *   Bytes successfully written (could be 0).
 *   -EBADF if NULL pointer was supplied in private data.
 *   -EFAULT if copy_from_user fails.
 */
static ssize_t char_dev_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *f_pos)
{
	struct sk_buff *skb;
	struct char_dev_user *dev = filp->private_data;
	struct cg2900_user_data *user;
	int err = 0;

	pr_debug("char_dev_write");

	if (!dev) {
		pr_err("char_dev_write: Calling with NULL pointer");
		return -EBADF;
	}

	user = dev_get_platdata(dev->dev);
	if (!user->opened) {
		dev_err(MAIN_DEV, "char_dev_write: Channel not opened\n");
		return -EACCES;
	}

	mutex_lock(&dev->write_mutex);

	skb = user->alloc_skb(count, GFP_ATOMIC);
	if (!skb) {
		dev_err(MAIN_DEV, "Couldn't allocate sk_buff with length %d\n",
			count);
		goto error_handling;
	}

	err = copy_from_user(skb_put(skb, count), buf, count);
	if (err) {
		dev_err(MAIN_DEV, "Error %d from copy_from_user\n", err);
		kfree_skb(skb);
		err = -EFAULT;
		goto error_handling;
	}

	err = user->write(user, skb);
	if (err) {
		dev_err(MAIN_DEV, "cg2900_write failed (%d)\n", err);
		kfree_skb(skb);
		goto error_handling;
	}

	mutex_unlock(&dev->write_mutex);
	return count;

error_handling:
	mutex_unlock(&dev->write_mutex);
	return err;
}

/**
 * char_dev_unlocked_ioctl() - Handle IOCTL call to the interface.
 * @filp:	Pointer to the file struct.
 * @cmd:	IOCTL command.
 * @arg:	IOCTL argument.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL if supplied cmd is not supported.
 *   For cmd CG2900_CHAR_DEV_IOCTL_CHECK4RESET 0x01 is returned if device is
 *   reset and 0x02 is returned if device is closed.
 */
static long char_dev_unlocked_ioctl(struct file *filp, unsigned int cmd,
				    unsigned long arg)
{
	struct char_dev_user *dev = filp->private_data;
	struct cg2900_user_data *user;
	struct cg2900_rev_data rev_data;
	int err = 0;
	int ret_val;
	void __user *user_arg = (void __user *)arg;
	struct clk *clk = NULL;

	if (!dev) {
		pr_err("char_dev_unlocked_ioctl: Calling with NULL pointer");
		return -EBADF;
	}

	dev_dbg(dev->dev, "char_dev_unlocked_ioctl for %s\n"
		"\tDIR: %d\n"
		"\tTYPE: %d\n"
		"\tNR: %d\n"
		"\tSIZE: %d",
		dev->name, _IOC_DIR(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd),
		_IOC_SIZE(cmd));

	user = dev_get_platdata(dev->dev);

	switch (cmd) {
	case CG2900_CHAR_DEV_IOCTL_RESET:
		if (!user->opened)
			return -EACCES;
		dev_dbg(MAIN_DEV, "ioctl reset command for device %s\n",
			dev->name);
		err = user->reset(user);
		break;

	case CG2900_CHAR_DEV_IOCTL_CHECK4RESET:
		if (user->opened)
			ret_val = CG2900_CHAR_DEV_IOCTL_EVENT_IDLE;
		else
			ret_val = CG2900_CHAR_DEV_IOCTL_EVENT_RESET;

		dev_dbg(MAIN_DEV, "ioctl check for reset command for device %s",
			dev->name);

		err = copy_to_user(user_arg, &ret_val, sizeof(ret_val));
		if (err) {
			dev_err(MAIN_DEV,
				"Error %d from copy_to_user for reset\n", err);
			return -EFAULT;
		}
		break;

	case CG2900_CHAR_DEV_IOCTL_GET_REVISION:
		if (!user->get_local_revision(user, &rev_data)) {
			dev_err(MAIN_DEV, "No revision data available\n");
			return -EIO;
		}
		dev_dbg(MAIN_DEV, "ioctl check for local revision info\n"
			"\trevision 0x%04X\n"
			"\tsub_version 0x%04X\n",
			rev_data.revision, rev_data.sub_version);
		err = copy_to_user(user_arg, &rev_data, sizeof(rev_data));
		if (err) {
			dev_err(MAIN_DEV,
				"Error %d from copy_to_user for "
				"revision\n", err);
			return -EFAULT;
		}
		break;

	case CG2900_CHAR_DEV_IOCTL_EXT_CLK_ENABLE:
		if (!user->opened)
			return -EACCES;
		dev_dbg(MAIN_DEV, "ioctl clk_get enable "
				"command for device %s\n", dev->name);

		clk = clk_get(dev->dev, "sysclk3");
		if (IS_ERR(clk)) {
			dev_dbg(MAIN_DEV, "ioctl clk_get enable "
					"command for device %s failed\n",
					dev->name);
			return -EFAULT;
		} else {
			err = clk_enable(clk);
			if (err) {
				dev_dbg(MAIN_DEV, "clk_enable failed:%d "
						"for %s\n", err, dev->name);
				return err;
			}
		}
		break;

	case CG2900_CHAR_DEV_IOCTL_EXT_CLK_DISABLE:
		if (!user->opened)
			return -EACCES;
		dev_dbg(MAIN_DEV, "ioctl clk_get disable "
				"command for device %s\n", dev->name);

		clk = clk_get(dev->dev, "sysclk3");
		if (IS_ERR(clk)) {
			dev_dbg(MAIN_DEV, "ioctl clk_get disable "
					"command for device %s failed\n",
					dev->name);
			return -EFAULT;
		} else
			clk_disable(clk);

		break;

	default:
		dev_err(MAIN_DEV, "Unknown ioctl command %08X\n", cmd);
		err = -EINVAL;
		break;
	};

	return err;
}

/**
 * char_dev_poll() - Handle POLL call to the interface.
 * @filp:	Pointer to the file struct.
 * @wait:	Poll table supplied to caller.
 *
 * Returns:
 *   Mask of current set POLL values
 */
static unsigned int char_dev_poll(struct file *filp, poll_table *wait)
{
	struct char_dev_user *dev = filp->private_data;
	struct cg2900_user_data *user;
	unsigned int mask = 0;

	if (!dev) {
		pr_debug("char_dev_poll: Device not open");
		return POLLERR | POLLRDHUP;
	}

	user = dev_get_platdata(dev->dev);

	poll_wait(filp, &dev->reset_wait_queue, wait);
	poll_wait(filp, &dev->rx_wait_queue, wait);

	if (!user->opened)
		mask |= POLLERR | POLLRDHUP | POLLPRI;
	else
		mask |= POLLOUT; /* We can TX unless there is an error */

	if (!(skb_queue_empty(&dev->rx_queue)))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

/*
 * struct char_dev_fops - Char devices file operations.
 * @read:		Function that reads from the char device.
 * @write:		Function that writes to the char device.
 * @unlocked_ioctl:	Function that performs IO operations with
 *			the char device.
 * @poll:		Function that checks if there are possible operations
 *			with the char device.
 * @open:		Function that opens the char device.
 * @flush:		Function that flushes rx queue and releases wait queues
 * @release:		Function that release the char device.
 */
static const struct file_operations char_dev_fops = {
	.read		= char_dev_read,
	.write		= char_dev_write,
	.unlocked_ioctl	= char_dev_unlocked_ioctl,
	.poll		= char_dev_poll,
	.open		= char_dev_open,
	.flush		= char_dev_flush,
	.release	= char_dev_release
};

/**
 * remove_dev() - Remove char device structure for device.
 * @dev_usr:	Char device user.
 *
 * The remove_dev() function releases the char_dev structure for this device.
 */
static void remove_dev(struct char_dev_user *dev_usr)
{
	if (!dev_usr)
		return;

	dev_dbg(dev_usr->dev,
		"Removing char device %s with major %d and minor %d\n",
		dev_usr->name,
		MAJOR(dev_usr->miscdev.this_device->devt),
		MINOR(dev_usr->miscdev.this_device->devt));

	skb_queue_purge(&dev_usr->rx_queue);

	mutex_destroy(&dev_usr->read_mutex);
	mutex_destroy(&dev_usr->write_mutex);

	dev_usr->dev = NULL;
	if (dev_usr->filp)
		dev_usr->filp->private_data = NULL;

	/* Remove device node in file system. */
	misc_deregister(&dev_usr->miscdev);
	kfree(dev_usr);
}

/**
 * cg2900_char_probe() - Initialize char device module.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success.
 *   -ENOMEM if allocation fails.
 *   -EACCES if device already have been initiated.
 */
static int __devinit cg2900_char_probe(struct platform_device *pdev)
{
	int err = 0;
	struct char_dev_user *dev_usr;
	struct cg2900_user_data *user;
	struct device *dev = &pdev->dev;

	dev_dbg(&pdev->dev, "cg2900_char_probe\n");

	user = dev_get_platdata(dev);
	user->dev = dev;
	user->read_cb = char_dev_read_cb;
	user->reset_cb = char_dev_reset_cb;

	dev_usr = kzalloc(sizeof(*dev_usr), GFP_KERNEL);
	if (!dev_usr) {
		dev_err(&pdev->dev, "Couldn't allocate dev_usr\n");
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, dev_usr);
	dev_usr->dev = &pdev->dev;

	/* Store device name */
	dev_usr->name = user->channel_data.char_dev_name;

	/* Prepare miscdevice struct before registering the device */
	dev_usr->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev_usr->miscdev.name = dev_usr->name;
	dev_usr->miscdev.nodename = dev_usr->name;
	dev_usr->miscdev.fops = &char_dev_fops;
	dev_usr->miscdev.parent = &pdev->dev;
	dev_usr->miscdev.mode = S_IRUGO | S_IWUGO;

	err = misc_register(&dev_usr->miscdev);
	if (err) {
		dev_err(&pdev->dev, "Error %d registering misc dev\n", err);
		goto err_free_usr;
	}

	dev_dbg(&pdev->dev, "Added char device %s with major %d and minor %d\n",
		dev_usr->name, MAJOR(dev_usr->miscdev.this_device->devt),
		MINOR(dev_usr->miscdev.this_device->devt));

	mutex_init(&dev_usr->read_mutex);
	mutex_init(&dev_usr->write_mutex);

	skb_queue_head_init(&dev_usr->rx_queue);

	mutex_lock(&char_info->man_mutex);
	list_add_tail(&dev_usr->list, &char_info->dev_users);
	mutex_unlock(&char_info->man_mutex);

	return 0;

err_free_usr:
	kfree(dev_usr);
	dev_set_drvdata(&pdev->dev, NULL);
	return err;
}

/**
 * cg2900_char_remove() - Release the char device module.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success (always success).
 */
static int __devexit cg2900_char_remove(struct platform_device *pdev)
{
	struct list_head *cursor, *next;
	struct char_dev_user *tmp;
	struct char_dev_user *user;

	dev_dbg(&pdev->dev, "cg2900_char_remove\n");

	user = dev_get_drvdata(&pdev->dev);

	mutex_lock(&char_info->man_mutex);
	list_for_each_safe(cursor, next, &char_info->dev_users) {
		tmp = list_entry(cursor, struct char_dev_user, list);
		if (tmp == user) {
			list_del(cursor);
			remove_dev(tmp);
			dev_set_drvdata(&pdev->dev, NULL);
			break;
		}
	}
	mutex_unlock(&char_info->man_mutex);
	return 0;
}

static struct platform_driver cg2900_char_driver = {
	.driver = {
		.name	= "cg2900-chardev",
		.owner	= THIS_MODULE,
	},
	.probe	= cg2900_char_probe,
	.remove	= __devexit_p(cg2900_char_remove),
};

/**
 * cg2900_char_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init cg2900_char_init(void)
{
	pr_debug("cg2900_char_init");

	/* Initialize private data. */
	char_info = kzalloc(sizeof(*char_info), GFP_ATOMIC);
	if (!char_info) {
		pr_err("Could not alloc char_info struct");
		return -ENOMEM;
	}

	mutex_init(&char_info->open_mutex);
	mutex_init(&char_info->man_mutex);
	INIT_LIST_HEAD(&char_info->dev_users);

	return platform_driver_register(&cg2900_char_driver);
}

/**
 * cg2900_char_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit cg2900_char_exit(void)
{
	struct list_head *cursor, *next;
	struct char_dev_user *tmp;

	pr_debug("cg2900_char_exit");

	platform_driver_unregister(&cg2900_char_driver);

	if (!char_info)
		return;

	list_for_each_safe(cursor, next, &char_info->dev_users) {
		tmp = list_entry(cursor, struct char_dev_user, list);
		list_del(cursor);
		remove_dev(tmp);
	}

	mutex_destroy(&char_info->open_mutex);
	mutex_destroy(&char_info->man_mutex);

	kfree(char_info);
	char_info = NULL;
}

module_init(cg2900_char_init);
module_exit(cg2900_char_exit);

MODULE_AUTHOR("Henrik Possung ST-Ericsson");
MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ST-Ericsson CG2900 Char Devices Driver");
