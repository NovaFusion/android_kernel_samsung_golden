/* alps-input.c
 *
 * Input device driver for alps sensor
 *
 * Copyright (C) 2011-2012 ALPS ELECTRIC CO., LTD. All Rights Reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include "alps_compass_io.h"

extern int hscd_get_magnetic_field_data(int *xyz);
extern void hscd_activate(int flgatm, int flg, int dtime);
extern void hscd_register_init(void);
extern int hscd_self_test_A(void);
extern int hscd_self_test_B(void);

static DEFINE_MUTEX(alps_lock);

static struct platform_device *pdev;
static struct input_polled_dev *alps_idev;
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend alps_early_suspend_handler;
#endif

#define EVENT_TYPE_MAGV_X           REL_X
#define EVENT_TYPE_MAGV_Y           REL_Y
#define EVENT_TYPE_MAGV_Z           REL_Z

#define ALPS_POLL_INTERVAL   200    /* msecs */
#define ALPS_INPUT_FUZZ        0    /* input event threshold */
#define ALPS_INPUT_FLAT        0

#define POLL_STOP_TIME       400    /* (msec) */

#undef ALPS_DEBUG
#define alps_dbgmsg(str, args...) pr_err("%s: " str, __func__, ##args)
#define alps_errmsg(str, args...) pr_err("%s: " str, __func__, ##args)
#define alps_info(str, args...) pr_info("%s: " str, __func__, ##args)


static int flgM = 0;
static int flgSuspend = 0;
static int delay = 200;
static int poll_stop_cnt = 0;

///////////////////////////////////////////////////////////////////////////////
// for I/O Control

static long alps_ioctl(struct file* filp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = -1, tmpval;

	switch (cmd) {
	case ALPSIO_SET_MAGACTIVATE:
		ret = copy_from_user(&tmpval, argp, sizeof(tmpval));
		if (ret) {
			alps_errmsg("Failed (cmd = ALPSIO_SET_MAGACTIVATE)\n" );
			return -EFAULT;
		}

		mutex_lock(&alps_lock);
		flgM = tmpval;
		if(flgM)	hscd_register_init();

		hscd_activate(1, tmpval, delay);
		mutex_unlock(&alps_lock);

		alps_info("ALPSIO_SET_MAGACTIVATE, flgM = %d\n", flgM);
		break;

	case ALPSIO_SET_DELAY:
		ret = copy_from_user(&tmpval, argp, sizeof(tmpval));
		if (ret) {
			alps_errmsg("Failed (cmd = ALPSIO_SET_DELAY)\n" );
			return -EFAULT;
		}

		mutex_lock(&alps_lock);
		if (tmpval <=  10)		tmpval =  10;
		else if (tmpval <=  20)	tmpval =  20;
		else if (tmpval <=  70)	tmpval =  70;
		else						tmpval = 200;

		delay = tmpval;
		hscd_activate(1, flgM, delay);
		mutex_unlock(&alps_lock);

		alps_info("ALPSIO_SET_DELAY, delay = %d\n", delay);
		break;

	case ALPSIO_ACT_SELF_TEST_A:

		mutex_lock(&alps_lock);
		ret = hscd_self_test_A();
		mutex_unlock(&alps_lock);

		alps_info("ALPSIO_ACT_SELF_TEST_A, result = %d\n", ret);

		if (copy_to_user(argp, &ret, sizeof(ret))) {
			alps_errmsg("Failed (cmd = ALPSIO_ACT_SELF_TEST_A)\n" );
			return -EFAULT;
		}
		break;

	case ALPSIO_ACT_SELF_TEST_B:

		mutex_lock(&alps_lock);
		ret = hscd_self_test_B();
		mutex_unlock(&alps_lock);

		alps_info("ALPSIO_ACT_SELF_TEST_B, result = %d\n", ret);

		if (copy_to_user(argp, &ret, sizeof(ret))) {
			alps_errmsg("Failed (cmd = ALPSIO_ACT_SELF_TEST_B)\n" );
			return -EFAULT;
		}
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}

static int
alps_io_open( struct inode* inode, struct file* filp )
{
	return 0;
}

static int
alps_io_release( struct inode* inode, struct file* filp )
{
	return 0;
}

static struct file_operations alps_fops = {
	.owner   = THIS_MODULE,
	.open    = alps_io_open,
	.release = alps_io_release,
	.unlocked_ioctl = alps_ioctl,
};

static struct miscdevice alps_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "alps_compass_io",
	.fops  = &alps_fops,
};


///////////////////////////////////////////////////////////////////////////////
// for input device

static int alps_probe(struct platform_device *dev)
{
	alps_info("is called\n", __func__);
	return 0;
}

static int alps_remove(struct platform_device *dev)
{
	alps_info("is called\n", __func__);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void alps_early_suspend(struct early_suspend *handler)
{
#ifdef ALPS_DEBUG
	alps_info("is called\n", __func__);
#endif
	mutex_lock(&alps_lock);
	flgSuspend = 1;
	mutex_unlock(&alps_lock);
}

static void alps_early_resume(struct early_suspend *handler)
{
#ifdef ALPS_DEBUG
	alps_info("is called\n", __func__);
#endif
	mutex_lock(&alps_lock);
	poll_stop_cnt = POLL_STOP_TIME / delay;
	flgSuspend = 0;
	mutex_unlock(&alps_lock);
}
#endif

static struct platform_driver alps_driver = {
	.driver    = {
		.name  = "alps-input",
		.owner = THIS_MODULE,
	},
	.probe     = alps_probe,
	.remove    = alps_remove,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend alps_early_suspend_handler = {
	.suspend = alps_early_suspend,
	.resume  = alps_early_resume,
};
#endif

static void hscd_poll(struct input_dev *idev)
{
	int xyz[3];

	if(hscd_get_magnetic_field_data(xyz) == 0) {
#ifdef ALPS_DEBUG
		alps_dbgmsg("%d, %d, %d\n", xyz[0], xyz[1], xyz[2]);
#endif
		input_report_rel(idev, EVENT_TYPE_MAGV_X, xyz[0]);
		input_report_rel(idev, EVENT_TYPE_MAGV_Y, xyz[1]);
		input_report_rel(idev, EVENT_TYPE_MAGV_Z, xyz[2]);
		input_sync(idev);
	}
}


static void alps_poll(struct input_polled_dev *dev)
{
	if (!flgSuspend) {
		mutex_lock(&alps_lock);
		dev->poll_interval = delay;
		if (flgM)	hscd_poll(alps_idev->input);
		mutex_unlock(&alps_lock);
	}
}

static int __init alps_init(void)
{
	struct input_dev *idev;
	int ret;

	alps_info("is called\n", __func__);

	ret = platform_driver_register(&alps_driver);
	if (ret)
	    goto out_region;
	alps_dbgmsg("platform_driver_register\n");

	pdev = platform_device_register_simple("alps_compass", -1, NULL, 0);
	if (IS_ERR(pdev)) {
	    ret = PTR_ERR(pdev);
	    goto out_driver;
	}
	alps_dbgmsg("platform_device_register_simple\n");

	alps_idev = input_allocate_polled_device();
	if (!alps_idev) {
		ret = -ENOMEM;
		goto out_device;
	}
	alps_dbgmsg("input_allocate_polled_device\n");

	alps_idev->poll = alps_poll;
	alps_idev->poll_interval = ALPS_POLL_INTERVAL;

	/* initialize the input class */
	idev = alps_idev->input;
	idev->name = "alps_compass";
	//idev->phys = "alps_compass/input0";
	idev->id.bustype = BUS_I2C;
	//idev->dev.parent = &pdev->dev;
	//idev->evbit[0] = BIT_MASK(EV_ABS);

	set_bit(EV_REL, idev->evbit);
	input_set_capability(idev, EV_REL, REL_X);
	input_set_capability(idev, EV_REL, REL_Y);
	input_set_capability(idev, EV_REL, REL_Z);

#if 0//defined(MAG_15BIT)
	input_set_abs_params(idev, EVENT_TYPE_MAGV_X,
			-16384, 16383, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_MAGV_Y,
			-16384, 16383, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_MAGV_Z,
			-16384, 16383, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	//#elif defined(MAG_13BIT)
	input_set_abs_params(idev, EVENT_TYPE_MAGV_X,
			-4096, 4095, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_MAGV_Y,
			-4096, 4095, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_MAGV_Z,
			-4096, 4095, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
#endif

	ret = input_register_polled_device(alps_idev);
	if (ret)
		goto out_alc_poll;
	alps_dbgmsg("input_register_polled_device\n");

	ret = misc_register(&alps_device);
	if (ret) {
		alps_info("alps_io_device register failed\n");
		goto out_reg_poll;
	}
	alps_dbgmsg("misc_register\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&alps_early_suspend_handler);
	alps_dbgmsg("register_early_suspend\n");
#endif
	mutex_lock(&alps_lock);
	flgSuspend = 0;
	mutex_unlock(&alps_lock);

	return 0;

out_reg_poll:
	input_unregister_polled_device(alps_idev);
	alps_info("input_unregister_polled_device\n");
out_alc_poll:
	input_free_polled_device(alps_idev);
	alps_info("input_free_polled_device\n");
out_device:
	platform_device_unregister(pdev);
	alps_info("platform_device_unregister\n");
out_driver:
	platform_driver_unregister(&alps_driver);
	alps_info("platform_driver_unregister\n");
out_region:
	return ret;
}

static void __exit alps_exit(void)
{
	alps_info("is called\n", __func__);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&alps_early_suspend_handler);
	alps_dbgmsg("early_suspend_unregister\n");
#endif
	misc_deregister(&alps_device);
	alps_dbgmsg("misc_deregister\n");
	input_unregister_polled_device(alps_idev);
	alps_dbgmsg("input_unregister_polled_device\n");
	input_free_polled_device(alps_idev);
	alps_dbgmsg("input_free_polled_device\n");
	platform_device_unregister(pdev);
	alps_dbgmsg("platform_device_unregister\n");
	platform_driver_unregister(&alps_driver);
	alps_dbgmsg("platform_driver_unregister\n");
}

module_init(alps_init);
module_exit(alps_exit);

MODULE_DESCRIPTION("Alps Input Device");
MODULE_AUTHOR("ALPS ELECTRIC CO., LTD.");
MODULE_LICENSE("GPL v2");
