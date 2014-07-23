/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 *
 * Heavily based upon geodewdt.c
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/mfd/ux500_wdt.h>
#include <mach/id.h>
#include <linux/hrtimer.h>
#include <linux/mfd/dbx500-prcmu.h>

#define WATCHDOG_TIMEOUT 23
#define WDOG_REFRESH_TIME 13

#define WDT_FLAGS_OPEN 1
#define WDT_FLAGS_ORPHAN 2

#define DEBUG_LEVEL_LOW	(0x4f4c)

static unsigned long wdt_flags;

static int refresh_time = WDOG_REFRESH_TIME;
module_param(refresh_time, int, 0);
MODULE_PARM_DESC(refresh_time,
	"Watchdog refresh time in seconds. default="
		 __MODULE_STRING(WDOG_REFRESH_TIME) ".");

static int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
static u8 wdog_id;
static bool wdt_en;
static bool wdt_auto_off = false;
static bool safe_close;
static struct ux500_wdt_ops *ux500_wdt_ops;

static int ux500_wdt_open(struct inode *inode, struct file *file)
{
	if (!timeout)
		return -ENODEV;

	if (test_and_set_bit(WDT_FLAGS_OPEN, &wdt_flags))
		return -EBUSY;

	if (!test_and_clear_bit(WDT_FLAGS_ORPHAN, &wdt_flags))
		__module_get(THIS_MODULE);

	ux500_wdt_ops->enable(wdog_id);
	wdt_en = true;

	return nonseekable_open(inode, file);
}

static int ux500_wdt_release(struct inode *inode, struct file *file)
{
	if (safe_close) {
		ux500_wdt_ops->disable(wdog_id);
		module_put(THIS_MODULE);
	} else {
		pr_crit("Unexpected close - watchdog is not stopping.\n");
		ux500_wdt_ops->kick(wdog_id);

		set_bit(WDT_FLAGS_ORPHAN, &wdt_flags);
	}

	clear_bit(WDT_FLAGS_OPEN, &wdt_flags);
	safe_close = false;
	return 0;
}

static ssize_t ux500_wdt_write(struct file *file, const char __user *data,
			       size_t len, loff_t *ppos)
{
	if (!len)
		return len;

	if (!nowayout) {
		size_t i;
		safe_close = false;

		for (i = 0; i != len; i++) {
			char c;

			if (get_user(c, data + i))
				return -EFAULT;

			if (c == 'V')
				safe_close = true;
		}
	}

	ux500_wdt_ops->kick(wdog_id);

	return len;
}

static long ux500_wdt_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int interval;

	static const struct watchdog_info ident = {
		.options =	WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
		.firmware_version =     1,
		.identity	= "Ux500 WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident,
				    sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
	{
		int options;
		int ret = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			ux500_wdt_ops->disable(wdog_id);
			wdt_en = false;
			ret = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			ux500_wdt_ops->enable(wdog_id);
			wdt_en = true;
			ret = 0;
		}

		return ret;
	}
	case WDIOC_KEEPALIVE:
		return ux500_wdt_ops->kick(wdog_id);

	case WDIOC_SETTIMEOUT:
		if (get_user(interval, p))
			return -EFAULT;

		if (cpu_is_u8500()) {
			/* 28 bit resolution in ms, becomes 268435.455 s */
			if (interval > 268435 || interval < 0)
				return -EINVAL;
		} else if (cpu_is_u5500()) {
			/* 32 bit resolution in ms, becomes 4294967.295 s */
			if (interval > 4294967 || interval < 0)
				return -EINVAL;
		} else
			return -EINVAL;

		timeout = interval;
		ux500_wdt_ops->disable(wdog_id);
		ux500_wdt_ops->load(wdog_id, timeout * 1000);
		ux500_wdt_ops->enable(wdog_id);

	/* Fall through */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);

	default:
		return -ENOTTY;
	}

	return 0;
}

#ifdef CONFIG_SAMSUNG_LOG_BUF
void wdog_disable()
{
	ux500_wdt_ops->disable(wdog_id);
	wdt_en = false;
}
#endif
static const struct file_operations ux500_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= ux500_wdt_write,
	.unlocked_ioctl = ux500_wdt_ioctl,
	.open		= ux500_wdt_open,
	.release	= ux500_wdt_release,
};

static struct miscdevice ux500_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &ux500_wdt_fops,
};

#ifdef CONFIG_UX500_WATCHDOG_DEBUG
enum wdog_dbg {
	WDOG_DBG_CONFIG,
	WDOG_DBG_LOAD,
	WDOG_DBG_KICK,
	WDOG_DBG_EN,
	WDOG_DBG_DIS,
};

static ssize_t wdog_dbg_write(struct file *file,
			      const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	unsigned long val;
	int err;
	enum wdog_dbg v = (enum wdog_dbg)((struct seq_file *)
					  (file->private_data))->private;

	switch(v) {
	case WDOG_DBG_CONFIG:
		err = kstrtoul_from_user(user_buf, count, 0, &val);

		if (!err) {
			wdt_auto_off = val != 0;
			(void) ux500_wdt_ops->config(1, wdt_auto_off);
		}
		else {
			pr_err("ux500_wdt:dbg: unknown value\n");
		}
		break;
	case WDOG_DBG_LOAD:
		err = kstrtoul_from_user(user_buf, count, 0, &val);

		if (!err) {
			timeout = val;
			/* Convert seconds to ms */
			ux500_wdt_ops->disable(wdog_id);
			ux500_wdt_ops->load(wdog_id, timeout * 1000);
			ux500_wdt_ops->enable(wdog_id);
		}
		else {
			pr_err("ux500_wdt:dbg: unknown value\n");
		}
		break;
	case WDOG_DBG_KICK:
		(void) ux500_wdt_ops->kick(wdog_id);
		break;
	case WDOG_DBG_EN:
		wdt_en = true;
		(void) ux500_wdt_ops->enable(wdog_id);
		break;
	case WDOG_DBG_DIS:
		wdt_en = false;
		(void) ux500_wdt_ops->disable(wdog_id);
		break;
	}

	return count;
}

static int wdog_dbg_read(struct seq_file *s, void *p)
{
	enum wdog_dbg v = (enum wdog_dbg)s->private;

	switch(v) {
	case WDOG_DBG_CONFIG:
		seq_printf(s,"wdog is on id %d, auto off on sleep: %s\n",
			   (int)wdog_id,
			   wdt_auto_off ? "enabled": "disabled");
		break;
	case WDOG_DBG_LOAD:
		/* In 1s */
		seq_printf(s, "wdog load is: %d s\n",
			   timeout);
		break;
	case WDOG_DBG_KICK:
		break;
	case WDOG_DBG_EN:
	case WDOG_DBG_DIS:
		seq_printf(s, "wdog is %sabled\n",
			       wdt_en ? "en" : "dis");
		break;
	}
	return 0;
}

static int wdog_dbg_open(struct inode *inode,
			struct file *file)
{
	return single_open(file, wdog_dbg_read, inode->i_private);
}

static const struct file_operations wdog_dbg_fops = {
	.open		= wdog_dbg_open,
	.write		= wdog_dbg_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int __init wdog_dbg_init(void)
{
	struct dentry *wdog_dir;

	wdog_dir = debugfs_create_dir("wdog", NULL);
	if (IS_ERR_OR_NULL(wdog_dir))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_u8("id",
					     S_IWUSR | S_IWGRP | S_IRUGO, wdog_dir,
					     &wdog_id)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("config",
					       S_IWUSR | S_IWGRP | S_IRUGO, wdog_dir,
					       (void *)WDOG_DBG_CONFIG,
					       &wdog_dbg_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("load",
					       S_IWUSR | S_IWGRP | S_IRUGO, wdog_dir,
					       (void *)WDOG_DBG_LOAD,
					       &wdog_dbg_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("kick",
					       S_IWUSR | S_IWGRP, wdog_dir,
					       (void *)WDOG_DBG_KICK,
					       &wdog_dbg_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("enable",
					       S_IWUSR | S_IWGRP | S_IRUGO, wdog_dir,
					       (void *)WDOG_DBG_EN,
					       &wdog_dbg_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("disable",
					       S_IWUSR | S_IWGRP | S_IRUGO, wdog_dir,
					       (void *)WDOG_DBG_DIS,
					       &wdog_dbg_fops)))
		goto fail;

	return 0;
fail:
	pr_err("ux500:wdog: Failed to initialize wdog dbg\n");
	debugfs_remove_recursive(wdog_dir);

	return -EFAULT;
}

#else
static inline int __init wdog_dbg_init(void)
{
	return 0;
}
#endif

#ifdef CONFIG_UX500_WATCHDOG_KERNEL_KICKER
static struct hrtimer wdog_auto_timer;
struct work_struct wdog_kick_work;
struct workqueue_struct *wdog_kick_wq;

static void wdog_kick_req_work(struct work_struct *work)
{
	ux500_wdt_ops->kick(wdog_id);
	pr_info("ux500_wdt : reloaded by kernel kicker\n");
}


static enum hrtimer_restart wdog_kick_timer(struct hrtimer *timer)
{
	queue_work(wdog_kick_wq, &wdog_kick_work);
	hrtimer_forward_now( timer, ktime_set(refresh_time, 0));
	return HRTIMER_RESTART;
}

static inline int wdog_init(struct platform_device *pdev)
{
	/*
	 * Ensure no user-space daemon access the watchdog
	 * if kernel kicker is enabled
	 */

	wdog_kick_wq = create_workqueue("ux500_wdog");
	if (!wdog_kick_wq) {
		dev_err(&pdev->dev, "%s: err create workqueue\n", __func__);
		return -EINVAL;
	}

	INIT_WORK(&wdog_kick_work, wdog_kick_req_work);

	hrtimer_init(&wdog_auto_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wdog_auto_timer.function = wdog_kick_timer;
	hrtimer_start(&wdog_auto_timer, ktime_set(refresh_time, 0),
		      HRTIMER_MODE_REL);

	ux500_wdt_ops->enable(wdog_id);
	wdt_en = true;
	dev_info(&pdev->dev, "%s: kernel kicker enabled\n", __func__);

	return 0;
}

#else
static inline int wdog_init(struct platform_device *pdev)
{
	int ret = misc_register(&ux500_wdt_miscdev);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to register misc.\n");

	return ret;
}
#endif


static int __init ux500_wdt_probe(struct platform_device *pdev)
{
	int ret;

	/* retrieve prcmu fops from plat data */
	ux500_wdt_ops = dev_get_platdata(&pdev->dev);

	if (!ux500_wdt_ops) {
		dev_err(&pdev->dev, "plat dat incorrect\n");
		return -EIO;
	}
	/* Number of watch dogs */
	ux500_wdt_ops->config(1, wdt_auto_off);
	/* convert to ms */
	ux500_wdt_ops->load(wdog_id, timeout * 1000);

	ret = wdog_init(pdev);
	if (ret < 0)
		return ret;

	/* no cause for alarm if dbg_init fails, just continue */
	wdog_dbg_init();

	dev_info(&pdev->dev, "initialized\n");

	return 0;
}

static int __exit ux500_wdt_remove(struct platform_device *dev)
{
	ux500_wdt_ops->disable(wdog_id);
	wdt_en = false;
	ux500_wdt_ops = NULL;
	misc_deregister(&ux500_wdt_miscdev);
	return 0;
}
#ifdef CONFIG_PM
static int ux500_wdt_suspend(struct platform_device *pdev,
			     pm_message_t state)
{
	if (wdt_en && cpu_is_u5500()) {
		ux500_wdt_ops->disable(wdog_id);
		return 0;
	}

	if (wdt_en && !wdt_auto_off) {
		ux500_wdt_ops->disable(wdog_id);
		ux500_wdt_ops->config(1, true);

		ux500_wdt_ops->load(wdog_id, timeout * 1000);
		ux500_wdt_ops->enable(wdog_id);
	}
	return 0;
}

static int ux500_wdt_resume(struct platform_device *pdev)
{
	if (wdt_en && cpu_is_u5500()) {
		ux500_wdt_ops->load(wdog_id, timeout * 1000);
		ux500_wdt_ops->enable(wdog_id);
		return 0;
	}

	if (wdt_en && !wdt_auto_off) {
		ux500_wdt_ops->disable(wdog_id);
		ux500_wdt_ops->config(1, wdt_auto_off);

		ux500_wdt_ops->load(wdog_id, timeout * 1000);
		ux500_wdt_ops->enable(wdog_id);
	}
	return 0;
}

#else
#define ux500_wdt_suspend NULL
#define ux500_wdt_resume NULL
#endif
static struct platform_driver ux500_wdt_driver = {
	.remove		= __exit_p(ux500_wdt_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "ux500_wdt",
	},
	.suspend	= ux500_wdt_suspend,
	.resume		= ux500_wdt_resume,
};

static int __init ux500_wdt_init(void)
{
	return platform_driver_probe(&ux500_wdt_driver, ux500_wdt_probe);
}
module_init(ux500_wdt_init);

MODULE_AUTHOR("Jonas Aaberg <jonas.aberg@stericsson.com>");
MODULE_DESCRIPTION("Ux500 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
