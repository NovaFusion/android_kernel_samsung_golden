/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Derek Morton <derek.morton@stericsson.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Power state driver for M6718 MODEM
 */

/* define DEBUG to enable debug logging */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/gpio/nomadik.h>
#include <plat/pincfg.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "modem_state.h"

/*
 * To enable this driver add a struct platform_device in the board
 * configuration file (e.g. board-*.c) with name="modemstate"
 * optionally specify dev.initname="m6718" to define the driver
 * name as it will appear in the file system.
 * e.g.
 * static struct platform_device modem_state_device =
 * {
 *    .name = "modemstate",
 *    .dev =
 *    {
 *       .init_name = "m6718" // Name that will appear in FS
 *    },
 *    .num_resources = ARRAY_SIZE(modem_state_resources),
 *    .resource = modem_state_resources
 * };
 *
 * This driver uses gpio pins which should be specified as resources *
 * e.g.
 * static struct resource modem_state_resources[] =  .......
 * Output pins are specified as IORESOURCE_IO
 * Currently supported Output pins are:
 * onkey_pin
 * reset_pin
 * vbat_pin
 * Input pins are specified as IORESOURCE_IRQ
 * Currently supported input pins are:
 * rsthc_pin
 * rstext_pin
 * crash_pin
 * Currently only the start value is used as the gpio pin number but
 * end should also be specified as the gpio pin number in case gpio ranges
 * are used in the future.
 * e.g. if gpio 161 is used as the onkey pin
 * {
 *    .start = 161,
 *    .end = 161,
 *    .name = "onkey_pin",
 *    .flags = IORESOURCE_IO,
 * },
 */

struct modem_state_dev {
	int onkey_pin;
	int rsthc_pin;
	int rstext_pin;
	int crash_pin;
	int reset_pin;
	int vbat_pin;
	int power_state;
	int irq_state;
	int busy;
	struct timer_list onkey_timer;
	struct timer_list reset_timer;
	struct timer_list onkey_debounce_timer;
	struct timer_list vbat_off_timer;
	struct timer_list busy_timer;
	spinlock_t lock;
	struct device *dev;
	struct workqueue_struct *workqueue;
	struct work_struct wq_rsthc;
	struct work_struct wq_rstext;
	struct work_struct wq_crash;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfsdir;
	struct dentry *debugfs_debug;
#endif
};

struct callback_list {
	struct list_head node;
	int (*callback) (unsigned long);
	unsigned long data;
};
LIST_HEAD(callback_list);

static char *modem_state_str[] = {
	"off",
	"reset",
	"crash",
	"on",
	/*
	 * Add new states before error and update enum modem_states
	 * in modem_state.h
	 */
	"error"
};

static struct modem_state_dev *modem_state;

static void set_on_config(struct modem_state_dev *msdev)
{
	if (msdev->crash_pin)
		nmk_config_pin(PIN_CFG(msdev->crash_pin, GPIO) |
			       PIN_INPUT_PULLDOWN, false);
	if (msdev->rstext_pin)
		nmk_config_pin(PIN_CFG(msdev->rstext_pin, GPIO) |
			       PIN_INPUT_PULLDOWN, false);
	if (msdev->rsthc_pin)
		nmk_config_pin(PIN_CFG(msdev->rsthc_pin, GPIO) |
			       PIN_INPUT_PULLDOWN, false);
	if (msdev->reset_pin)
		nmk_config_pin(PIN_CFG(msdev->reset_pin, GPIO) |
			       PIN_OUTPUT_HIGH, false);
}

static void set_off_config(struct modem_state_dev *msdev)
{
	if (msdev->crash_pin)
		nmk_config_pin(PIN_CFG(msdev->crash_pin, GPIO) |
			       PIN_INPUT_PULLDOWN, false);
	if (msdev->rstext_pin)
		nmk_config_pin(PIN_CFG(msdev->rstext_pin, GPIO) |
			       PIN_OUTPUT_LOW, false);
	if (msdev->rsthc_pin)
		nmk_config_pin(PIN_CFG(msdev->rsthc_pin, GPIO) | PIN_OUTPUT_LOW,
			       false);
	if (msdev->reset_pin)
		nmk_config_pin(PIN_CFG(msdev->reset_pin, GPIO) |
			       PIN_OUTPUT_HIGH, false);
}

static void enable_irq_all(struct modem_state_dev *msdev)
{
	if (msdev->rsthc_pin) {
		enable_irq(GPIO_TO_IRQ(msdev->rsthc_pin));
		if ((0 > enable_irq_wake(GPIO_TO_IRQ(msdev->rsthc_pin))))
			dev_err(msdev->dev,
				"Request for wake on pin %d failed\n",
				msdev->rsthc_pin);
	}
	if (msdev->rstext_pin) {
		enable_irq(GPIO_TO_IRQ(msdev->rstext_pin));
		if ((0 > enable_irq_wake(GPIO_TO_IRQ(msdev->rstext_pin))))
			dev_err(msdev->dev,
				"Request for wake on pin %d failed\n",
				msdev->rstext_pin);
	}
	if (msdev->crash_pin) {
		enable_irq(GPIO_TO_IRQ(msdev->crash_pin));
		if ((0 > enable_irq_wake(GPIO_TO_IRQ(msdev->crash_pin))))
			dev_err(msdev->dev,
				"Request for wake on pin %d failed\n",
				msdev->crash_pin);
	}
}

static void disable_irq_all(struct modem_state_dev *msdev)
{
	if (msdev->rsthc_pin) {
		disable_irq_wake(GPIO_TO_IRQ(msdev->rsthc_pin));
		disable_irq(GPIO_TO_IRQ(msdev->rsthc_pin));
	}
	if (msdev->rstext_pin) {
		disable_irq_wake(GPIO_TO_IRQ(msdev->rstext_pin));
		disable_irq(GPIO_TO_IRQ(msdev->rstext_pin));
	}
	if (msdev->crash_pin) {
		disable_irq_wake(GPIO_TO_IRQ(msdev->crash_pin));
		disable_irq(GPIO_TO_IRQ(msdev->crash_pin));
	}
}

/*
 * These functions which access GPIO must only be called
 * with spinlock enabled.
 */

/*
 * Toggle ONKEY pin high then low to turn modem on or off. Modem expects
 * ONKEY line to be pulled low then high. GPIO needs to be driven high then
 * low as logic is inverted through a transistor.
 */
static void toggle_modem_power(struct modem_state_dev *msdev)
{
	dev_info(msdev->dev, "Modem power toggle\n");
	msdev->busy = 1;
	gpio_set_value(msdev->onkey_pin, 1);
	msdev->onkey_timer.data = (unsigned long)msdev;
	/* Timeout of at least 1 second */
	mod_timer(&msdev->onkey_timer, jiffies + (1 * HZ) + 1);
}

/* Modem is forced into reset when its reset line is pulled low */
/* Drive GPIO low then high to reset modem */
static void modem_reset(struct modem_state_dev *msdev)
{
	dev_info(msdev->dev, "Modem reset\n");
	msdev->busy = 1;
	gpio_set_value(msdev->reset_pin, 0);
	msdev->reset_timer.data = (unsigned long)msdev;
	/* Wait a couple of Jiffies */
	mod_timer(&msdev->reset_timer, jiffies + 2);
}

static void modem_vbat_set_value(struct modem_state_dev *msdev, int vbat_val)
{
	switch (vbat_val) {
	case 0:
		msdev->power_state = 0;
		dev_info(msdev->dev, "Modem vbat off\n");
		gpio_set_value(msdev->vbat_pin, vbat_val);
		if (1 == msdev->irq_state) {
			msdev->irq_state = 0;
			disable_irq_all(msdev);
			set_off_config(msdev);
		}
		break;
	case 1:
		dev_info(msdev->dev, "Modem vbat on\n");
		if (0 == msdev->irq_state) {
			msdev->irq_state = 1;
			set_on_config(msdev);
			enable_irq_all(msdev);
		}
		gpio_set_value(msdev->vbat_pin, vbat_val);
		break;
	default:
		return;
		break;
	}
}

static void modem_power_on(struct modem_state_dev *msdev)
{
	int rsthc = gpio_get_value(msdev->rsthc_pin);
	msdev->power_state = 1;
	del_timer(&msdev->vbat_off_timer);
	if (rsthc == 0) {
		modem_vbat_set_value(msdev, 1);
		toggle_modem_power(msdev);
	}
}

static void modem_power_off(struct modem_state_dev *msdev)
{
	int rsthc = gpio_get_value(msdev->rsthc_pin);

	msdev->power_state = 0;
	if (rsthc == 1) {
		toggle_modem_power(msdev);
		/* Cut power to modem after 10 seconds */
		msdev->vbat_off_timer.data = (unsigned long)msdev;
		mod_timer(&msdev->vbat_off_timer, jiffies + (10 * HZ));
	}
}
/* End of functions requiring spinlock */

static void call_callbacks(void)
{
	struct callback_list *item;

	list_for_each_entry(item, &callback_list, node)
		item->callback(item->data);
}

static int get_modem_state(struct modem_state_dev *msdev)
{
	int state;
	unsigned long flags;

	spin_lock_irqsave(&msdev->lock, flags);
	if (0 == gpio_get_value(msdev->rsthc_pin))
		state = MODEM_STATE_OFF;
	else if (0 == gpio_get_value(msdev->rstext_pin))
		state = MODEM_STATE_RESET;
	else if (1 == gpio_get_value(msdev->crash_pin))
		state = MODEM_STATE_CRASH;
	else
		state = MODEM_STATE_ON;
	spin_unlock_irqrestore(&msdev->lock, flags);

	return state;
}

/* modempower read handler */
static ssize_t modem_state_power_get(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int rsthc;
	int power_state;
	unsigned long flags;
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	spin_lock_irqsave(&msdev->lock, flags);
	rsthc = gpio_get_value(msdev->rsthc_pin);
	power_state = msdev->power_state;
	spin_unlock_irqrestore(&msdev->lock, flags);

	return sprintf(buf, "state=%d, expected=%d\n", rsthc, power_state);
}

/*
 * modempower write handler
 * Write '0' to /sys/devices/platform/modemstate/modempower to turn modem off
 * Write '1' to /sys/devices/platform/modemstate/modempower to turn modem on
 */
static ssize_t modem_state_power_set(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long flags;
	int ret = count;
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy) {
		ret = -EAGAIN;
	} else if (count > 0) {
		switch (buf[0]) {
		case '0':
			modem_power_off(msdev);
			break;
		case '1':
			modem_power_on(msdev);
			break;
		default:
			break;
		}
	}
	spin_unlock_irqrestore(&msdev->lock, flags);
	return ret;
}

/* reset read handler */
static ssize_t modem_state_reset_get(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int rstext;
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	/* No need for spinlocks here as there is only 1 value */
	rstext = gpio_get_value(msdev->rstext_pin);

	return sprintf(buf, "state=%d\n", rstext);
}

/* reset write handler */
/* Write '1' to /sys/devices/platform/modemstate/reset to reset modem */
static ssize_t modem_state_reset_set(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long flags;
	int ret = count;
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy) {
		ret = -EAGAIN;
	} else if (count > 0) {
		if (buf[0] == '1')
			modem_reset(msdev);
	}
	spin_unlock_irqrestore(&msdev->lock, flags);

	return ret;
}

/* crash read handler */
static ssize_t modem_state_crash_get(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int crash;
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	/* No need for spinlocks here as there is only 1 value */
	crash = gpio_get_value(msdev->crash_pin);

	return sprintf(buf, "state=%d\n", crash);
}

/* state read handler */
static ssize_t modem_state_state_get(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int state;
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	state = get_modem_state(msdev);
	if (state > MODEM_STATE_END_MARKER)
		state = MODEM_STATE_END_MARKER;

	return sprintf(buf, "%s\n", modem_state_str[state]);
}

#ifdef CONFIG_DEBUG_FS
static int modem_state_debug_get(struct seq_file *s, void *data)
{
	int onkey;
	int rsthc;
	int rstext;
	int reset;
	int crash;
	int vbat;
	unsigned long flags;
	struct modem_state_dev *msdev = s->private;

	spin_lock_irqsave(&msdev->lock, flags);
	onkey = gpio_get_value(msdev->onkey_pin);
	rsthc = gpio_get_value(msdev->rsthc_pin);
	rstext = gpio_get_value(msdev->rstext_pin);
	reset = gpio_get_value(msdev->reset_pin);
	crash = gpio_get_value(msdev->crash_pin);
	vbat = gpio_get_value(msdev->vbat_pin);
	spin_unlock_irqrestore(&msdev->lock, flags);

	seq_printf(s, "onkey=%d, rsthc=%d, rstext=%d, "
			"reset=%d, crash=%d, vbat=%d\n",
			onkey, rsthc, rstext, reset, crash, vbat);
	return 0;
}

/*
 * debug write handler
 * Write o['0'|'1'] to /sys/devices/platform/modemstate/debug to set
 * onkey line low or high.
 * Write r['0'|'1'] to /sys/devices/platform/modemstate/debug to set
 * reset line low or high.
 * Write v['0'|'1'] to /sys/devices/platform/modemstate/debug to set
 * vbat line low or high.
 */
static ssize_t modem_state_debug_set(struct file *file,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	unsigned long flags;
	int bufsize;
	char buf[128];

	bufsize = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, bufsize))
		return -EFAULT;
	buf[bufsize] = 0;

	spin_lock_irqsave(&modem_state->lock, flags);
	if (modem_state->busy) {
		return -EAGAIN;
	} else if (count > 1) {
		switch (buf[1]) {
		case '0':	/* fallthrough */
		case '1':
			switch (buf[0]) {
			case 'o':
				gpio_set_value(modem_state->onkey_pin,
					buf[1] - '0');
				break;
			case 'r':
				gpio_set_value(modem_state->reset_pin,
					buf[1] - '0');
				break;
			case 'v':
				gpio_set_value(modem_state->vbat_pin,
				buf[1] - '0');
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
	spin_unlock_irqrestore(&modem_state->lock, flags);

	return bufsize;
}

static int modem_state_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, modem_state_debug_get, inode->i_private);
}
#endif /* CONFIG_DEBUG_FS */

static DEVICE_ATTR(modempower, S_IRUSR | S_IWUSR,
		   modem_state_power_get, modem_state_power_set);
static DEVICE_ATTR(reset, S_IRUSR | S_IWUSR,
		   modem_state_reset_get, modem_state_reset_set);
static DEVICE_ATTR(crash, S_IRUSR, modem_state_crash_get, NULL);
static DEVICE_ATTR(state, S_IRUSR, modem_state_state_get, NULL);

static struct attribute *modemstate_attributes[] = {
	&dev_attr_modempower.attr,
	&dev_attr_reset.attr,
	&dev_attr_crash.attr,
	&dev_attr_state.attr,
	NULL
};

static struct attribute_group modemstate_attr_group = {
	.attrs = modemstate_attributes,
	.name = "modemstate"
};

#ifdef CONFIG_DEBUG_FS
static const struct file_operations debugfs_debug_fops = {
	.open = modem_state_debug_open,
	.read = seq_read,
	.write = modem_state_debug_set,
	.llseek = seq_lseek,
	.release = single_release
};
#endif

static void sysfs_notify_rsthc(struct modem_state_dev *msdev)
{
	sysfs_notify(&msdev->dev->kobj, NULL, dev_attr_modempower.attr.name);
	sysfs_notify(&msdev->dev->kobj, NULL, dev_attr_state.attr.name);
}

static void sysfs_notify_rstext(struct modem_state_dev *msdev)
{
	sysfs_notify(&msdev->dev->kobj, NULL, dev_attr_reset.attr.name);
	sysfs_notify(&msdev->dev->kobj, NULL, dev_attr_state.attr.name);
}

static void sysfs_notify_crash(struct modem_state_dev *msdev)
{
	sysfs_notify(&msdev->dev->kobj, NULL, dev_attr_crash.attr.name);
	sysfs_notify(&msdev->dev->kobj, NULL, dev_attr_state.attr.name);
}

static void wq_rsthc(struct work_struct *work)
{
	unsigned long flags;
	int rsthc;
	struct modem_state_dev *msdev =
		container_of(work, struct modem_state_dev, wq_rsthc);

	spin_lock_irqsave(&msdev->lock, flags);
	rsthc = gpio_get_value(msdev->rsthc_pin);
	dev_dbg(msdev->dev, "RSTHC interrupt detected, rsthc=%d\n", rsthc);
	if (msdev->power_state == rsthc) {
		if (!rsthc) {
			/* Modem has turned off, and we were expecting it to.
			   turn vbat to the modem off now */
			del_timer(&msdev->vbat_off_timer);
			modem_vbat_set_value(msdev, 0);
		}
	} else {
		dev_dbg(msdev->dev,
			 "Modem power state is %d, expected %d\n", rsthc,
			 msdev->power_state);
		dev_dbg(msdev->dev,
			"Attempting to change modem power state "
			"in 2 seconds\n");

		msdev->onkey_debounce_timer.data = (unsigned long)msdev;
		/* Wait > 2048ms due to debounce timer */
		mod_timer(&msdev->onkey_debounce_timer,
			jiffies + ((2050 * HZ) / 1000));
	}
	spin_unlock_irqrestore(&msdev->lock, flags);

	call_callbacks();
	sysfs_notify_rsthc(msdev);
}

static void wq_rstext(struct work_struct *work)
{
	struct modem_state_dev *msdev =
		container_of(work, struct modem_state_dev, wq_rstext);

	dev_dbg(msdev->dev, "RSTEXT interrupt detected, rstext=%d\n",
		 gpio_get_value(msdev->rstext_pin));

	call_callbacks();
	sysfs_notify_rstext(msdev);
}

static void wq_crash(struct work_struct *work)
{
	struct modem_state_dev *msdev =
		container_of(work, struct modem_state_dev, wq_rstext);

	dev_dbg(msdev->dev, "modem crash interrupt detected. crash=%d\n",
		 gpio_get_value(msdev->crash_pin));

	call_callbacks();
	sysfs_notify_crash(msdev);
}

/* Populate device structure used by the driver */
static int modem_state_dev_init(struct platform_device *pdev,
				struct modem_state_dev *msdev)
{
	int err = 0;
	struct resource *r;

	r = platform_get_resource_byname(pdev, IORESOURCE_IO, "onkey_pin");
	if (r == NULL) {
		err = -ENXIO;
		dev_err(&pdev->dev,
			"Could not get GPIO number for onkey pin\n");
		goto err_resource;
	}
	msdev->onkey_pin = r->start;

	r = platform_get_resource_byname(pdev, IORESOURCE_IO, "reset_pin");
	if (r == NULL) {
		err = -ENXIO;
		dev_err(&pdev->dev,
			"Could not get GPIO number for reset pin\n");
		goto err_resource;
	}
	msdev->reset_pin = r->start;

	r = platform_get_resource_byname(pdev, IORESOURCE_IO, "vbat_pin");
	if (r == NULL) {
		err = -ENXIO;
		dev_err(&pdev->dev, "Could not get GPIO number for vbat pin\n");
		goto err_resource;
	}
	msdev->vbat_pin = r->start;

	msdev->rsthc_pin = platform_get_irq_byname(pdev, "rsthc_pin");
	if (msdev->rsthc_pin < 0) {
		err = msdev->rsthc_pin;
		dev_err(&pdev->dev,
			"Could not get GPIO number for rsthc pin\n");
		goto err_resource;
	}

	msdev->rstext_pin = platform_get_irq_byname(pdev, "rstext_pin");
	if (msdev->rstext_pin < 0) {
		err = msdev->rstext_pin;
		dev_err(&pdev->dev,
			"Could not get GPIO number for retext pin\n");
		goto err_resource;
	}

	msdev->crash_pin = platform_get_irq_byname(pdev, "crash_pin");
	if (msdev->crash_pin < 0) {
		err = msdev->crash_pin;
		dev_err(&pdev->dev,
			"Could not get GPIO number for crash pin\n");
		goto err_resource;
	}
err_resource:
	return err;
}

/* IRQ handlers */

/* Handlers for rsthc (modem power off indication) IRQ */
static irqreturn_t rsthc_irq(int irq, void *dev)
{
	struct modem_state_dev *msdev = (struct modem_state_dev *)dev;

	/* check it's our interrupt */
	if (irq != GPIO_TO_IRQ(msdev->rsthc_pin)) {
		dev_err(msdev->dev, "Spurious RSTHC irq\n");
		return IRQ_NONE;
	}

	queue_work(msdev->workqueue, &msdev->wq_rsthc);
	return IRQ_HANDLED;
}

/* Handlers for rstext (modem reset indication) IRQ */
static irqreturn_t rstext_irq(int irq, void *dev)
{
	struct modem_state_dev *msdev = (struct modem_state_dev *)dev;

	/* check it's our interrupt */
	if (irq != GPIO_TO_IRQ(msdev->rstext_pin)) {
		dev_err(msdev->dev, "Spurious RSTEXT irq\n");
		return IRQ_NONE;
	}

	queue_work(msdev->workqueue, &msdev->wq_rstext);
	return IRQ_HANDLED;
}

/* Handlers for modem crash indication IRQ */
static irqreturn_t crash_irq(int irq, void *dev)
{
	struct modem_state_dev *msdev = (struct modem_state_dev *)dev;

	/* check it's our interrupt */
	if (irq != GPIO_TO_IRQ(msdev->crash_pin)) {
		dev_err(msdev->dev, "Spurious modem crash irq\n");
		return IRQ_NONE;
	}

	queue_work(msdev->workqueue, &msdev->wq_crash);
	return IRQ_HANDLED;
}

static int request_irq_pin(int pin, irq_handler_t handler, unsigned long flags,
			   struct modem_state_dev *msdev)
{
	int err = 0;
	if (pin) {
		err = request_irq(GPIO_TO_IRQ(pin), handler, flags,
				  dev_name(msdev->dev), msdev);
		if (err == 0) {
			err = enable_irq_wake(GPIO_TO_IRQ(pin));
			if (err < 0) {
				dev_err(msdev->dev,
					"Request for wake on pin %d failed\n",
					pin);
				free_irq(GPIO_TO_IRQ(pin), NULL);
			}
		} else {
			dev_err(msdev->dev,
				"Request for irq on pin %d failed\n", pin);
		}
	}
	return err;
}

static void free_irq_pin(int pin)
{
	disable_irq_wake(GPIO_TO_IRQ(pin));
	free_irq(GPIO_TO_IRQ(pin), NULL);
}

static int request_irq_all(struct modem_state_dev *msdev)
{
	int err;

	err = request_irq_pin(msdev->rsthc_pin, rsthc_irq,
				IRQF_TRIGGER_RISING |
				IRQF_TRIGGER_FALLING |
				IRQF_NO_SUSPEND, msdev);
	if (err < 0)
		goto err_rsthc_irq_req;

	err = request_irq_pin(msdev->rstext_pin, rstext_irq,
				IRQF_TRIGGER_RISING |
				IRQF_TRIGGER_FALLING |
				IRQF_NO_SUSPEND, msdev);
	if (err < 0)
		goto err_rstext_irq_req;

	err = request_irq_pin(msdev->crash_pin, crash_irq,
				IRQF_TRIGGER_RISING |
				IRQF_TRIGGER_FALLING |
				IRQF_NO_SUSPEND, msdev);
	if (err < 0)
		goto err_crash_irq_req;

	return 0;

err_crash_irq_req:
	free_irq_pin(msdev->rstext_pin);
err_rstext_irq_req:
	free_irq_pin(msdev->rsthc_pin);
err_rsthc_irq_req:
	return err;
}

/* Configure GPIO used by the driver */
static int modem_state_gpio_init(struct platform_device *pdev,
				 struct modem_state_dev *msdev)
{
	int err = 0;

	/* Reserve gpio pins */
	if (msdev->onkey_pin != 0) {
		err = gpio_request(msdev->onkey_pin, dev_name(msdev->dev));
		if (err < 0) {
			dev_err(&pdev->dev, "Request for onkey pin failed\n");
			goto err_onkey_req;
		}
	}
	if (msdev->reset_pin != 0) {
		err = gpio_request(msdev->reset_pin, dev_name(msdev->dev));
		if (err < 0) {
			dev_err(&pdev->dev, "Request for reset pin failed\n");
			goto err_reset_req;
		}
	}
	if (msdev->rsthc_pin != 0) {
		err = gpio_request(msdev->rsthc_pin, dev_name(msdev->dev));
		if (err < 0) {
			dev_err(&pdev->dev, "Request for rsthc pin failed\n");
			goto err_rsthc_req;
		}
	}
	if (msdev->rstext_pin != 0) {
		err = gpio_request(msdev->rstext_pin, dev_name(msdev->dev));
		if (err < 0) {
			dev_err(&pdev->dev, "Request for rstext pin failed\n");
			goto err_rstext_req;
		}
	}
	if (msdev->crash_pin != 0) {
		err = gpio_request(msdev->crash_pin, dev_name(msdev->dev));
		if (err < 0) {
			dev_err(&pdev->dev, "Request for crash pin failed\n");
			goto err_crash_req;
		}
	}
	if (msdev->vbat_pin != 0) {
		err = gpio_request(msdev->vbat_pin, dev_name(msdev->dev));
		if (err < 0) {
			dev_err(&pdev->dev, "Request for vbat pin failed\n");
			goto err_vbat_req;
		}
	}

	/* Set initial pin config */
	set_on_config(msdev);
	if (msdev->onkey_pin)
		nmk_config_pin(PIN_CFG(msdev->onkey_pin, GPIO) |
			       PIN_OUTPUT_LOW, false);
	if (msdev->vbat_pin)
		nmk_config_pin(PIN_CFG(msdev->vbat_pin, GPIO) | PIN_OUTPUT_HIGH,
			       false);

	/* Configure IRQs for GPIO pins */
	err = request_irq_all(msdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Request for irqs failed, err = %d\n", err);
		goto err_irq_req;
	}
	msdev->irq_state = 1;

	/* Save current modem state */
	msdev->power_state = gpio_get_value(msdev->rsthc_pin);

	return 0;

err_irq_req:
	gpio_free(msdev->vbat_pin);
err_vbat_req:
	gpio_free(msdev->crash_pin);
err_crash_req:
	gpio_free(msdev->rstext_pin);
err_rstext_req:
	gpio_free(msdev->rsthc_pin);
err_rsthc_req:
	gpio_free(msdev->reset_pin);
err_reset_req:
	gpio_free(msdev->onkey_pin);
err_onkey_req:
	return err;
}

/* Timer handlers */

static void modem_power_timeout(unsigned long data)
{
	unsigned long flags;
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy)
		msdev->busy = 0;
	else
		dev_err(msdev->dev,
			"onkey timer expired and busy flag not set\n");

	gpio_set_value(msdev->onkey_pin, 0);
	spin_unlock_irqrestore(&msdev->lock, flags);
}

static void modem_reset_timeout(unsigned long data)
{
	unsigned long flags;
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;

	spin_lock_irqsave(&modem_state->lock, flags);
	if (msdev->busy)
		msdev->busy = 0;
	else
		dev_err(msdev->dev,
			"reset timer expired and busy flag not set\n");

	gpio_set_value(msdev->reset_pin, 1);
	spin_unlock_irqrestore(&modem_state->lock, flags);
}

static void modem_onkey_debounce_timeout(unsigned long data)
{
	unsigned long flags;
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy) {
		dev_info(msdev->dev,
			 "Delayed onkey change aborted. "
			 "Another action in progress\n");
	} else {
		if (gpio_get_value(msdev->rsthc_pin) != msdev->power_state) {
			if (0 == msdev->power_state)
				modem_power_off(msdev);
			else
				modem_power_on(msdev);
		}
	}
	spin_unlock_irqrestore(&msdev->lock, flags);
}

static void modem_vbat_off_timeout(unsigned long data)
{
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;
	unsigned long flags;
	spin_lock_irqsave(&msdev->lock, flags);
	if (0 == msdev->power_state)
		modem_vbat_set_value(msdev, 0);
	spin_unlock_irqrestore(&msdev->lock, flags);
}

static void modem_busy_on_timeout(unsigned long data)
{
	unsigned long flags;
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy) {
		mod_timer(&msdev->busy_timer, jiffies + 1);
	} else {
		msdev->busy_timer.function = NULL;
		modem_power_on(msdev);
	}
	spin_unlock_irqrestore(&msdev->lock, flags);
}

static void modem_busy_off_timeout(unsigned long data)
{
	unsigned long flags;
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy) {
		mod_timer(&msdev->busy_timer, jiffies + 1);
	} else {
		msdev->busy_timer.function = NULL;
		modem_power_off(msdev);
	}
	spin_unlock_irqrestore(&msdev->lock, flags);
}

static void modem_busy_reset_timeout(unsigned long data)
{
	unsigned long flags;
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;

	spin_lock_irqsave(&msdev->lock, flags);
	if (msdev->busy) {
		mod_timer(&msdev->busy_timer, jiffies + 1);
	} else {
		msdev->busy_timer.function = NULL;
		modem_reset(msdev);
	}
	spin_unlock_irqrestore(&msdev->lock, flags);
}

#ifdef DEBUG
static int callback_test(unsigned long data)
{
	struct modem_state_dev *msdev = (struct modem_state_dev *)data;
	dev_info(msdev->dev, "Test callback. Modem state is %s\n",
		 modem_state_to_str(modem_state_get_state()));
	return 0;
}
#endif

/* Exported functions */

void modem_state_power_on(void)
{
	unsigned long flags;
	spin_lock_irqsave(&modem_state->lock, flags);
	if (modem_state->busy) {
		/*
		 * Ignore on request if turning off is queued,
		 * cancel any queued reset request
		 */
		if (modem_busy_reset_timeout ==
		    modem_state->busy_timer.function) {
			del_timer_sync(&modem_state->busy_timer);
			modem_state->busy_timer.function = NULL;
		}
		if (NULL == modem_state->busy_timer.function) {
			modem_state->busy_timer.function =
			    modem_busy_on_timeout;
			modem_state->busy_timer.data =
			    (unsigned long)modem_state;
			mod_timer(&modem_state->busy_timer, jiffies + 1);
		}
	} else {
		modem_power_on(modem_state);
	}
	spin_unlock_irqrestore(&modem_state->lock, flags);
}

void modem_state_power_off(void)
{
	unsigned long flags;
	spin_lock_irqsave(&modem_state->lock, flags);
	if (modem_state->busy) {
		/*
		 * Prioritize off request if others are queued.
		 * Must turn modem off if system is shutting down
		 */
		if (NULL != modem_state->busy_timer.function)
			del_timer_sync(&modem_state->busy_timer);

		modem_state->busy_timer.function = modem_busy_off_timeout;
		modem_state->busy_timer.data = (unsigned long)modem_state;
		mod_timer(&modem_state->busy_timer, jiffies + 1);
	} else {
		modem_power_off(modem_state);
	}
	spin_unlock_irqrestore(&modem_state->lock, flags);
}

void modem_state_force_reset(void)
{
	unsigned long flags;
	spin_lock_irqsave(&modem_state->lock, flags);
	if (modem_state->busy) {
		/* Ignore reset request if turning on or off is queued */
		if (NULL == modem_state->busy_timer.function) {
			modem_state->busy_timer.function =
			    modem_busy_reset_timeout;
			modem_state->busy_timer.data =
			    (unsigned long)modem_state;
			mod_timer(&modem_state->busy_timer, jiffies + 1);
		}
	} else {
		modem_reset(modem_state);
	}
	spin_unlock_irqrestore(&modem_state->lock, flags);
}

int modem_state_get_state(void)
{
	return get_modem_state(modem_state);
}

char *modem_state_to_str(int state)
{
	if (state > MODEM_STATE_END_MARKER)
		state = MODEM_STATE_END_MARKER;

	return modem_state_str[state];
}

int modem_state_register_callback(int (*callback) (unsigned long),
				  unsigned long data)
{
	struct callback_list *item;
	unsigned long flags;

	if (NULL == modem_state)
		return -EAGAIN;

	if (NULL == callback)
		return -EINVAL;

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (NULL == item) {
		dev_err(modem_state->dev,
			"Could not allocate memory for struct callback_list\n");
		return -ENOMEM;
	}
	item->callback = callback;
	item->data = data;

	spin_lock_irqsave(&modem_state->lock, flags);
	list_add_tail(&item->node, &callback_list);
	spin_unlock_irqrestore(&modem_state->lock, flags);

	return 0;
}

int modem_state_remove_callback(int (*callback) (unsigned long))
{
	struct callback_list *iterator;
	struct callback_list *item;
	unsigned long flags;
	int ret = -ENXIO;

	if (NULL == callback)
		return -EINVAL;

	spin_lock_irqsave(&modem_state->lock, flags);
	list_for_each_entry_safe(iterator, item, &callback_list, node) {
		if (callback == item->callback) {
			list_del(&item->node);
			kfree(item);
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&modem_state->lock, flags);

	return ret;
}

#ifdef CONFIG_PM
int modem_state_suspend(struct device *dev)
{
	struct modem_state_dev *msdev =
		platform_get_drvdata(to_platform_device(dev));

	if (msdev->busy) {
		dev_info(dev, "Driver is busy\n");
		return -EBUSY;
	} else {
		return 0;
	}
}

int modem_state_resume(struct device *dev)
{
	return 0;
}
#endif

static int __devinit modem_state_probe(struct platform_device *pdev)
{
	int err = 0;

	dev_info(&pdev->dev, "Starting probe\n");

	modem_state = kzalloc(sizeof(struct modem_state_dev), GFP_KERNEL);
	if (NULL == modem_state) {
		dev_err(&pdev->dev,
			"Could not allocate memory for modem_state_dev\n");
		return -ENOMEM;
	}
	modem_state->dev = &pdev->dev;

	spin_lock_init(&modem_state->lock);

	INIT_WORK(&modem_state->wq_rsthc, wq_rsthc);
	INIT_WORK(&modem_state->wq_rstext, wq_rstext);
	INIT_WORK(&modem_state->wq_crash, wq_crash);
	modem_state->workqueue =
		create_singlethread_workqueue(dev_name(&pdev->dev));
	if (modem_state->workqueue == NULL) {
		dev_err(&pdev->dev, "Failed to create workqueue\n");
		goto err_queue;
	}

	err = modem_state_dev_init(pdev, modem_state);
	if (err != 0) {
		dev_err(&pdev->dev, "Could not initialize device structure\n");
		goto err_dev;
	}

	init_timer(&modem_state->onkey_timer);
	init_timer(&modem_state->reset_timer);
	init_timer(&modem_state->onkey_debounce_timer);
	init_timer(&modem_state->vbat_off_timer);
	init_timer(&modem_state->busy_timer);
	modem_state->onkey_timer.function = modem_power_timeout;
	modem_state->reset_timer.function = modem_reset_timeout;
	modem_state->onkey_debounce_timer.function =
	    modem_onkey_debounce_timeout;
	modem_state->vbat_off_timer.function = modem_vbat_off_timeout;
	modem_state->busy_timer.function = NULL;

	platform_set_drvdata(pdev, modem_state);

	err = modem_state_gpio_init(pdev, modem_state);
	if (err != 0) {
		dev_err(&pdev->dev, "Could not initialize GPIO\n");
		goto err_gpio;
	}

	if (sysfs_create_group(&pdev->dev.kobj, &modemstate_attr_group) < 0) {
		dev_err(&pdev->dev, "failed to create sysfs nodes\n");
		goto err_sysfs;
	}

#ifdef CONFIG_DEBUG_FS
	modem_state->debugfsdir = debugfs_create_dir("modemstate", NULL);
	modem_state->debugfs_debug = debugfs_create_file("debug",
					S_IRUGO | S_IWUGO,
					modem_state->debugfsdir,
					modem_state,
					&debugfs_debug_fops);
#endif

#ifdef DEBUG
	modem_state_register_callback(callback_test,
				      (unsigned long)modem_state);
#endif
	return 0;

err_sysfs:
err_gpio:
err_dev:
	destroy_workqueue(modem_state->workqueue);
err_queue:
	kfree(modem_state);
	return err;
}

static int __devexit modem_state_remove(struct platform_device *pdev)
{
	struct modem_state_dev *msdev = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &modemstate_attr_group);
	destroy_workqueue(msdev->workqueue);
	kfree(msdev);
	return 0;
}

static void modem_state_shutdown(struct platform_device *pdev)
{
	/*
	 * Trigger software shutdown of the modem and then wait until
	 * modem-off state is detected. If the modem does not power off
	 * when requested power will be removed and we will detect the
	 * modem-off state that way.
	 */
	modem_state_power_off();
	if (MODEM_STATE_OFF != modem_state_get_state())
		dev_alert(&pdev->dev, "Waiting for modem to power down\n");
	while (MODEM_STATE_OFF != modem_state_get_state())
		cond_resched();
}

#ifdef CONFIG_PM
static const struct dev_pm_ops modem_state_dev_pm_ops = {
	.suspend_noirq = modem_state_suspend,
	.resume_noirq = modem_state_resume,
};
#endif

static struct platform_driver modem_state_driver = {
	.probe = modem_state_probe,
	.remove = __devexit_p(modem_state_remove),
	.shutdown = modem_state_shutdown,
	.driver = {
		   .name = "modemstate",
		   .owner = THIS_MODULE,
#ifdef CONFIG_PM
		   .pm = &modem_state_dev_pm_ops,
#endif
		   },
};

static int __init modem_state_init(void)
{
#ifdef DEBUG
	printk(KERN_ALERT "Modem state driver init\n");
#endif
	return platform_driver_probe(&modem_state_driver, modem_state_probe);
}

static void __exit modem_state_exit(void)
{
	platform_driver_unregister(&modem_state_driver);
}

module_init(modem_state_init);
module_exit(modem_state_exit);

MODULE_AUTHOR("Derek Morton");
MODULE_DESCRIPTION("M6718 modem power state driver");
MODULE_LICENSE("GPL v2");
