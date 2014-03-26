/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Modem Crash Detection Driver
 *
 * Author:Bibek Basu <bibek.basu@stericsson.com> for ST-Ericsson
 *
 * License terms:GNU General Public License (GPLv2)version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#define MCDD_INTERRUPT_CLEAR (1 << 13)
#define MODEM_CRASH_EVT  1

struct mcdd_data {
	bool	modem_event;
	u32   event_type;
	wait_queue_head_t  readq;
	spinlock_t	lock;
	void __iomem *remap_intcon;
	struct device *dev;
	struct miscdevice misc_dev;
};

static struct mcdd_data *mcdd;

static irqreturn_t mcdd_interrupt_cb(int irq, void *dev)
{
	writel(MCDD_INTERRUPT_CLEAR, (u32 *)mcdd->remap_intcon);
	spin_lock(&mcdd->lock);
	mcdd->modem_event = true;
	mcdd->event_type = MODEM_CRASH_EVT;
	spin_unlock(&mcdd->lock);
	wake_up_interruptible(&mcdd->readq);
	return IRQ_HANDLED;
}

static unsigned int mcdd_select(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	unsigned long flags;

	poll_wait(filp, &mcdd->readq,  wait);
	spin_lock_irqsave(&mcdd->lock, flags);

	if (mcdd->modem_event == true) {
		mask |= POLLPRI;
		mcdd->modem_event = false;
	}
	spin_unlock_irqrestore(&mcdd->lock, flags);

	return mask;
}

static int mcdd_open(struct inode *ino, struct file *filp)
{
	/* Do nothing */
	return 0;
}

ssize_t mcdd_read(struct file *filp, char __user *buff, size_t size, loff_t *t)
{
	if (copy_to_user(buff, &mcdd->event_type, size))
		return -EFAULT;
	return 0;
};

static const struct file_operations mcdd_fops = {
	.open    = mcdd_open,
	.poll    = mcdd_select,
	.read    = mcdd_read,
	.owner   = THIS_MODULE,
};

static int __devinit u5500_mcdd_probe(struct platform_device *pdev)
{
	struct resource *resource;
	int ret = 0;
	int irq;

	mcdd = kzalloc(sizeof(*mcdd), GFP_KERNEL);
	if (!mcdd) {
		dev_err(&pdev->dev, "Memory Allocation Failed");
		return -ENOMEM;
	}
	mcdd->dev = &pdev->dev;
	mcdd->misc_dev.minor = MISC_DYNAMIC_MINOR;
	mcdd->misc_dev.name = "mcdd";
	mcdd->misc_dev.fops = &mcdd_fops;
	spin_lock_init(&mcdd->lock);
	init_waitqueue_head(&(mcdd->readq));

	/* Get addr for mcdd crash interrupt reset register and ioremap it */
	resource = platform_get_resource_byname(pdev,
						IORESOURCE_MEM,
						"mcdd_intreset_addr");
	if (resource == NULL) {
		dev_err(&pdev->dev,
			"Unable to retrieve mcdd_intreset_addr resource\n");
		goto exit_free;
	}
	mcdd->remap_intcon = ioremap(resource->start, resource_size(resource));
	if (!mcdd->remap_intcon) {
		dev_err(&pdev->dev, "Unable to ioremap intcon mbox1\n");
		ret = -EINVAL;
		goto exit_free;
	}

	/* Get IRQ for mcdd mbox interrupt and allocate it */
	irq = platform_get_irq_byname(pdev, "mcdd_mbox_irq");
	if (irq < 0) {
		dev_err(&pdev->dev,
			"Unable to retrieve mcdd mbox irq resource\n");
		goto exit_unmap;
	}

	ret = request_threaded_irq(irq, NULL,
			mcdd_interrupt_cb, IRQF_NO_SUSPEND | IRQF_ONESHOT,
			"mcdd", &mcdd);
	if (ret < 0) {
		dev_err(&pdev->dev,
				"Could not allocate irq %d,error %d\n",
				 irq, ret);
		goto exit_unmap;
	}

	ret = misc_register(&mcdd->misc_dev);
	if (ret) {
		dev_err(&pdev->dev, "can't misc-register\n");
		goto exit_unmap;
	}
	dev_info(&pdev->dev, "mcdd driver registration done\n");
	return 0;

exit_unmap:
	iounmap(mcdd->remap_intcon);
exit_free:
	kfree(mcdd);
	return ret;
}

static int u5500_mcdd_remove(struct platform_device *pdev)
{
	int ret = 0;

	if (mcdd) {
		iounmap(mcdd->remap_intcon);
		ret = misc_deregister(&mcdd->misc_dev);
		kfree(mcdd);
	}
	return ret;
}

static struct platform_driver u5500_mcdd_driver = {
	.driver = {
		.name = "u5500-mcdd-modem",
		.owner = THIS_MODULE,
	},
	.probe = u5500_mcdd_probe,
	.remove = __devexit_p(u5500_mcdd_remove),
};

static int __init mcdd_init(void)
{
	return platform_driver_register(&u5500_mcdd_driver);
}
module_init(mcdd_init);

static void __exit mcdd_exit(void)
{
	platform_driver_unregister(&u5500_mcdd_driver);
}
module_exit(mcdd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BIBEK BASU <bibek.basu@stericsson.com>");
MODULE_DESCRIPTION("Modem Dump Detection Driver");
MODULE_ALIAS("mcdd driver");
