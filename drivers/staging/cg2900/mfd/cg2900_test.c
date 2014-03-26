/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Driver for ST-Ericsson CG2900 test character device.
 */
#define NAME					"cg2900_test"
#define pr_fmt(fmt)				NAME ": " fmt "\n"

#include <asm/byteorder.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/wait.h>

#include "cg2900.h"
#include "cg2900_core.h"

#define MISC_DEV			(info->misc_dev.this_device)

/* Device names */
#define CG2900_CDEV_NAME		"cg2900_core_test"

/**
 * struct test_info - Main info structure for CG2900 test char device.
 * @misc_dev:	Registered Misc Device.
 * @rx_queue:	RX data queue.
 * @dev:	Device structure for STE Connectivity driver.
 * @pdev:	Platform device structure for STE Connectivity driver.
 */
struct test_info {
	struct miscdevice	misc_dev;
	struct sk_buff_head	rx_queue;
	struct device		*dev;
	struct platform_device	*pdev;
};

static struct test_info *test_info;

/*
 * main_wait_queue - Char device Wait Queue in CG2900 Core.
 */
static DECLARE_WAIT_QUEUE_HEAD(char_wait_queue);

/**
 * tx_to_char_dev() - Handle data received from CG2900 Core.
 * @dev:	Current chip device information.
 * @skb:	Buffer with data coming form device.
 */
static int tx_to_char_dev(struct cg2900_chip_dev *dev, struct sk_buff *skb)
{
	struct test_info *info = dev->t_data;
	skb_queue_tail(&info->rx_queue, skb);
	wake_up_interruptible_all(&char_wait_queue);
	return 0;
}

/**
 * cg2900_test_open() - User space char device has been opened.
 * @inode:	Device driver information.
 * @filp:	Pointer to the file struct.
 *
 * Returns:
 *   0 if there is no error.
 *   -EACCES if transport already exists.
 *   -ENOMEM if allocation fails.
 *   Errors from create_work_item.
 */
static int cg2900_test_open(struct inode *inode, struct file *filp)
{
	struct test_info *info = test_info;
	struct cg2900_chip_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(MISC_DEV, "Cannot allocate test_dev\n");
		return -ENOMEM;
	}
	dev->dev = info->dev;
	dev->pdev = info->pdev;
	dev->t_data = info;
	dev->t_cb.write = tx_to_char_dev;
	filp->private_data = dev;

	dev_info(MISC_DEV, "CG2900 test char dev opened\n");
	return cg2900_register_trans_driver(dev);
}

/**
 * cg2900_test_release() - User space char device has been closed.
 * @inode:	Device driver information.
 * @filp:	Pointer to the file struct.
 *
 * Returns:
 *   0 if there is no error.
 */
static int cg2900_test_release(struct inode *inode, struct file *filp)
{
	struct cg2900_chip_dev *dev = filp->private_data;
	struct test_info *info = dev->t_data;

	dev_info(MISC_DEV, "CG2900 test char dev closed\n");
	skb_queue_purge(&info->rx_queue);
	cg2900_deregister_trans_driver(dev);
	kfree(dev);

	return 0;
}

/**
 * cg2900_test_read() - Queue and copy buffer to user space char device.
 * @filp:	Pointer to the file struct.
 * @buf:	Received buffer.
 * @count:	Count of received data in bytes.
 * @f_pos:	Position in buffer.
 *
 * Returns:
 *   >= 0 is number of bytes read.
 *   -EFAULT if copy_to_user fails.
 */
static ssize_t cg2900_test_read(struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct sk_buff *skb;
	int bytes_to_copy;
	int err;
	struct cg2900_chip_dev *dev = filp->private_data;
	struct test_info *info = dev->t_data;
	struct sk_buff_head *rx_queue = &info->rx_queue;

	dev_dbg(MISC_DEV, "cg2900_test_read count %d\n", count);

	if (skb_queue_empty(rx_queue))
		wait_event_interruptible(char_wait_queue,
					 !(skb_queue_empty(rx_queue)));

	skb = skb_dequeue(rx_queue);
	if (!skb) {
		dev_dbg(MISC_DEV,
			"skb queue is empty - return with zero bytes\n");
		bytes_to_copy = 0;
		goto finished;
	}

	bytes_to_copy = min(count, skb->len);
	err = copy_to_user(buf, skb->data, bytes_to_copy);
	if (err) {
		skb_queue_head(rx_queue, skb);
		return -EFAULT;
	}

	skb_pull(skb, bytes_to_copy);

	if (skb->len > 0)
		skb_queue_head(rx_queue, skb);
	else
		kfree_skb(skb);

finished:
	return bytes_to_copy;
}

/**
 * cg2900_test_write() - Copy buffer from user and write to CG2900 Core.
 * @filp:	Pointer to the file struct.
 * @buf:	Read buffer.
 * @count:	Size of the buffer write.
 * @f_pos:	Position in buffer.
 *
 * Returns:
 *   >= 0 is number of bytes written.
 *   -EFAULT if copy_from_user fails.
 */
static ssize_t cg2900_test_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *f_pos)
{
	struct sk_buff *skb;
	struct cg2900_chip_dev *dev = filp->private_data;
	struct test_info *info = dev->t_data;

	dev_dbg(MISC_DEV, "cg2900_test_write count %d\n", count);

	/* Allocate the SKB and reserve space for the header */
	skb = alloc_skb(count + RX_SKB_RESERVE, GFP_KERNEL);
	if (!skb) {
		dev_err(MISC_DEV, "cg2900_test_write: Failed to alloc skb\n");
		return -ENOMEM;
	}
	skb_reserve(skb, RX_SKB_RESERVE);

	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	dev->c_cb.data_from_chip(dev, skb);

	return count;
}

/**
 * cg2900_test_poll() - Handle POLL call to the interface.
 * @filp:	Pointer to the file struct.
 * @wait:	Poll table supplied to caller.
 *
 * Returns:
 *   Mask of current set POLL values (0 or (POLLIN | POLLRDNORM))
 */
static unsigned int cg2900_test_poll(struct file *filp, poll_table *wait)
{
	struct cg2900_chip_dev *dev = filp->private_data;
	struct test_info *info = dev->t_data;
	unsigned int mask = 0;

	poll_wait(filp, &char_wait_queue, wait);

	if (!(skb_queue_empty(&info->rx_queue)))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct file_operations test_char_dev_fops = {
	.open = cg2900_test_open,
	.release = cg2900_test_release,
	.read = cg2900_test_read,
	.write = cg2900_test_write,
	.poll = cg2900_test_poll
};

/**
 * test_char_dev_create() - Create a char device for testing.
 * @info:	Test device info.
 *
 * Creates a separate char device that will interact directly with userspace
 * test application.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from misc_register.
 */
static int test_char_dev_create(struct test_info *info)
{
	int err;

	/* Initialize the RX queue */
	skb_queue_head_init(&info->rx_queue);

	/* Prepare miscdevice struct before registering the device */
	info->misc_dev.minor = MISC_DYNAMIC_MINOR;
	info->misc_dev.name = CG2900_CDEV_NAME;
	info->misc_dev.fops = &test_char_dev_fops;
	info->misc_dev.parent = info->dev;
	info->misc_dev.mode = S_IRUGO | S_IWUGO;

	err = misc_register(&info->misc_dev);
	if (err) {
		dev_err(info->dev, "Error %d registering misc dev", err);
		return err;
	}

	return 0;
}

/**
 * test_char_dev_destroy() - Clean up after test_char_dev_create().
 * @info:	Test device info.
 */
static void test_char_dev_destroy(struct test_info *info)
{
	int err;

	err = misc_deregister(&info->misc_dev);
	if (err)
		dev_err(info->dev, "Error %d deregistering misc dev\n", err);

	/* Clean the message queue */
	skb_queue_purge(&info->rx_queue);
}

/**
 * cg2900_test_probe() - Initialize module.
 *
 * @pdev:	Platform device.
 *
 * This function initializes and registers the test misc char device.
 *
 * Returns:
 *   0 if success.
 *   -ENOMEM for failed alloc or structure creation.
 *   -EEXIST if device already exists.
 *   Error codes generated by test_char_dev_create.
 */
static int __devinit cg2900_test_probe(struct platform_device *pdev)
{
	int err;

	dev_dbg(&pdev->dev, "cg2900_test_probe\n");

	if (test_info) {
		dev_err(&pdev->dev, "test_info exists\n");
		return -EEXIST;
	}

	test_info = kzalloc(sizeof(*test_info), GFP_KERNEL);
	if (!test_info) {
		dev_err(&pdev->dev, "Couldn't allocate test_info\n");
		return -ENOMEM;
	}

	test_info->dev = &pdev->dev;
	test_info->pdev = pdev;

	/* Create and add test char device. */
	err = test_char_dev_create(test_info);
	if (err) {
		kfree(test_info);
		test_info = NULL;
		return err;
	}

	dev_set_drvdata(&pdev->dev, test_info);

	dev_info(&pdev->dev, "CG2900 test char device driver started\n");

	return 0;
}

/**
 * cg2900_test_remove() - Remove module.
 *
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success.
 *   -ENOMEM if core_info does not exist.
 *   -EINVAL if platform data does not exist in the device.
 */
static int __devexit cg2900_test_remove(struct platform_device *pdev)
{
	struct test_info *test_info;

	dev_dbg(&pdev->dev, "cg2900_test_remove\n");
	test_info = dev_get_drvdata(&pdev->dev);
	test_char_dev_destroy(test_info);
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(test_info);
	test_info = NULL;
	dev_info(&pdev->dev, "CG2900 Test char device driver removed\n");
	return 0;
}

static struct platform_driver cg2900_test_driver = {
	.driver = {
		.name	= "cg2900-test",
		.owner	= THIS_MODULE,
	},
	.probe	= cg2900_test_probe,
	.remove	= __devexit_p(cg2900_test_remove),
};

/**
 * cg2900_test_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init cg2900_test_init(void)
{
	pr_debug("cg2900_test_init");
	return platform_driver_register(&cg2900_test_driver);
}

/**
 * cg2900_test_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit cg2900_test_exit(void)
{
	pr_debug("cg2900_test_exit");
	platform_driver_unregister(&cg2900_test_driver);
}

module_init(cg2900_test_init);
module_exit(cg2900_test_exit);

MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Linux CG2900 Test Char Device Driver");
