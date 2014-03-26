/*
 * Copyright (C) ST-Ericsson SA 2010
 * camera flash: Flash driver to export camera flash to user space application.
 *               It supports two flashes, one for primary and one for secondary camera
 * Author: Pankaj Chauhan/pankaj.chauhan@stericsson.com for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include "flash_common.h"

#define DEBUG_LOG(...) printk(KERN_DEBUG "Camera Flash driver: " __VA_ARGS__)

#define PRIMARY_CAMERA		(0)
#define SECONDARY_CAMERA	(1)
static struct miscdevice misc_dev;
struct flash_chip *flash_chips[2];
struct fasync_struct * async_queue;
struct task_struct* ptaskStruct;
wait_queue_head_t waitQueue;
int waitCondition = 0;
struct flash_ioctl_args_t flashArg;

#define COPY_ARG_FROM_USER(_to,_from_usr)	do{												\
	memset((_to),0,sizeof(struct flash_ioctl_args_t));													\
	if (copy_from_user((_to), (struct flash_ioctl_args_t*) (_from_usr), sizeof(struct flash_ioctl_args_t))) {	\
		DEBUG_LOG("Could not copy data from userspace successfully\n");							\
		break;																					\
	}																							\
}while(0)

#define COPY_ARG_TO_USER(_to_usr,_from) do{											\
	if (copy_to_user((struct flash_ioctl_args_t *)(_to_usr), (_from), sizeof(struct flash_ioctl_args_t))) {	\
		DEBUG_LOG("Could not copy data from userspace successfully\n");						\
		break;																				\
	}																						\
}while(0)


static long flash_ioctl(struct file *file_p, unsigned int cmd, unsigned long arg)
{
	int err=0;
	struct flash_chip *flash_p = NULL;
	struct flash_chip_ops *ops = NULL;
	char *my_name=NULL;
	struct flash_ioctl_args_t flash_arg;

	if (_IOC_TYPE(cmd) != FLASH_MAGIC_NUMBER) {
                printk(KERN_ALERT "Flash driver: Not an ioctl for this module\n");
		err =  -EINVAL;
	}

	COPY_ARG_FROM_USER(&flash_arg,arg);

	if(flash_arg.cam == SECONDARY_CAMERA || flash_arg.cam == PRIMARY_CAMERA)
		flash_p = flash_chips[flash_arg.cam];
	else{
		DEBUG_LOG("unsupported cam %lu\n",flash_arg.cam);
		err = -ENODEV;
		goto out;
	}
	my_name = flash_arg.cam ?"Secondary":"Primary";

	if (flash_arg.cam == PRIMARY_CAMERA)
	{
		ops = flash_p->ops;
	}

	switch(cmd){
	case FLASH_GET_MODES:
	{
		if (flash_arg.cam == PRIMARY_CAMERA)
		{
			err  = ops->get_modes(flash_p->priv_data,&flash_arg.flash_mode);
			if(!err){
				DEBUG_LOG("Supported flash modes for %s camera: %lx\n",
					my_name,flash_arg.flash_mode);
				COPY_ARG_TO_USER(arg,&flash_arg);
			}else{
				DEBUG_LOG("unable to get supported modes for %s camera\n",my_name);
			}
		}
		else
		{
			flash_arg.flash_mode = FLASH_MODE_NONE;
			COPY_ARG_TO_USER(arg,&flash_arg);
		}
	}
		break;
	case FLASH_GET_MODE_DETAILS:
	{
		err = ops->get_mode_details(flash_p->priv_data,flash_arg.flash_mode,
				&flash_arg.mode_arg.details);
		if(!err){
			COPY_ARG_TO_USER(arg,&flash_arg);
		}else{
			DEBUG_LOG("Unable to get mode details for %s camera, flash mode %lx\n",
				my_name,flash_arg.flash_mode);
		}
	}
		break;
	case FLASH_ENABLE_MODE:
	case FLASH_DISABLE_MODE:
	{
		int enable=0;
		if(cmd == FLASH_ENABLE_MODE){
			enable = 1;
		}
		err = ops->enable_flash_mode(flash_p->priv_data,flash_arg.flash_mode,enable);
		if(err){
			DEBUG_LOG("Unable to %s:  %s camera, flash mode %lx\n",
				(enable ?"Enable":"Disable"), my_name,flash_arg.flash_mode);
		}
	}
		break;
	case FLASH_CONFIGURE_MODE:
		err = ops->configure_flash_mode(flash_p->priv_data,flash_arg.flash_mode,
				&flash_arg.mode_arg.params);
		if(err){
			DEBUG_LOG("Unable to configure  %s camera, flash mode %lx\n",
				my_name,flash_arg.flash_mode);
		}
		break;
	case FLASH_TRIGGER_STROBE:
		err = ops->trigger_strobe(flash_p->priv_data,flash_arg.mode_arg.strobe_enable);
		if(err){
			DEBUG_LOG("Unable to %s:  %s camera strobe trigger, mode %lx\n",
				(arg ?"Enable":"Disable"), my_name,flash_arg.flash_mode);
		}
		break;
	case FLASH_GET_STATUS:
		err = ops->get_status(flash_p->priv_data,&flash_arg.status);
		if(!err){
			COPY_ARG_TO_USER(arg,&flash_arg);
		}else{
			DEBUG_LOG("Unable to get status details for %s camera, flash mode %lx\n",
				my_name,flash_arg.flash_mode);
		}
		 break;
	case FLASH_GET_LIFE_COUNTER:
		 DEBUG_LOG("Not Implemented\n");
		 break;
        case FLASH_SELF_TEST:
		flashArg = flash_arg;
		if (0 != (flashArg.cam & (FLASH_SELFTEST_FLASH | FLASH_SELFTEST_FLASH_WITH_STROBE)))
		{
			err = ENODEV;
		}
		else
		{
			/* wake up worker thread */
			waitCondition = 1;
			wake_up_interruptible(&waitQueue);
		}
		break;
        case FLASH_GET_SELF_TEST_MODES:
	{
		if (flash_arg.cam == PRIMARY_CAMERA)
		{
			err  = ops->get_selftest_modes(flash_p->priv_data,&flash_arg.flash_mode);
			if(!err){
				COPY_ARG_TO_USER(arg,&flash_arg);
			}else{
				DEBUG_LOG("unable to get supported modes for %s camera\n",my_name);
			}
		}
		else
		{
			flash_arg.flash_mode = FLASH_SELFTEST_NONE;
			COPY_ARG_TO_USER(arg,&flash_arg);
		}
		break;
	}
        case FLASH_GET_FAULT_REGISTERS:
	{
		err  = ops->get_fault_registers(flash_p->priv_data,flash_arg.flash_mode,&flash_arg.status);
		if(!err){
			COPY_ARG_TO_USER(arg,&flash_arg);
		}else{
			DEBUG_LOG("unable to get supported modes for %s camera\n",my_name);
		}

		break;
	}
        case FLASH_GET_SELF_TEST_RESULT:
	{
		COPY_ARG_TO_USER(arg,&flashArg);
		DEBUG_LOG("FLASH_GET_SELF_TEST_RESULT arg : 0x%lx\n", flashArg.status);
		break;
	}
	default:
		DEBUG_LOG("Unknown command %x\n",cmd);

	}
out:
	return err;
}

int worker_thread (void* data)
{
	int err = 0;
	struct flash_chip *flash_p=NULL;
	struct flash_chip_ops *ops=NULL;
	struct flash_mode_params params;
	struct flash_mode_details details;

	while (1)
	{
		/* waiting for some job to do */
		wait_event_interruptible(waitQueue, (waitCondition != 0));
		waitCondition = 0;

		DEBUG_LOG("worker_thread wakes up\n");
		/* do we need to stop ? */
		err = kthread_should_stop();
		if (0 != err)
		{
			DEBUG_LOG("worker_thread stops\n");
			break;
		}

		/* do the job */
		flash_p = flash_chips[flashArg.cam];
		ops = flash_p->ops;

		/* clear fault registers */
		err  = ops->get_fault_registers(flash_p->priv_data, FLASH_MODE_INDICATOR, &flashArg.status);
		if (0 != err)
		{
			flashArg.status = flashArg.flash_mode;
			flashArg.flash_mode = 0;
		}
		flashArg.status = 0;

		/* do all selftests */
		while (flashArg.flash_mode != FLASH_SELFTEST_NONE)
		{
			if (0 != (flashArg.flash_mode & FLASH_SELFTEST_CONNECTION))
			{
				err = ops->get_mode_details(flash_p->priv_data, FLASH_MODE_INDICATOR, &details);
				if (0 != err)
				{
					DEBUG_LOG("not able to get mode FLASH_MODE_INDICATOR details\n");
					flashArg.status |= FLASH_SELFTEST_CONNECTION;
				}
				flashArg.flash_mode &= ~FLASH_SELFTEST_CONNECTION;
			}
			else if (0 != (flashArg.flash_mode & (FLASH_SELFTEST_FLASH | FLASH_SELFTEST_FLASH_WITH_STROBE)))
			{
				if (0 != (flashArg.flash_mode & FLASH_SELFTEST_FLASH))
				{
					flashArg.status |= FLASH_SELFTEST_FLASH;
					flashArg.flash_mode &= ~FLASH_SELFTEST_FLASH;
				}
				else
				{
					flashArg.status |= FLASH_SELFTEST_FLASH_WITH_STROBE;
					flashArg.flash_mode &= ~FLASH_SELFTEST_FLASH_WITH_STROBE;
				}
			}
			/* FLASH_SELFTEST_VIDEO_LIGHT | FLASH_SELFTEST_AF_LIGHT | FLASH_SELFTEST_INDICATOR | FLASH_SELFTEST_TORCH_LIGHT */
			else
			{
				unsigned long currentSelftest = FLASH_SELFTEST_NONE;
				unsigned long currentFlashMode = FLASH_MODE_NONE;

				if (0 != (flashArg.flash_mode & FLASH_SELFTEST_VIDEO_LIGHT))
				{
					currentSelftest = FLASH_SELFTEST_VIDEO_LIGHT;
					currentFlashMode = FLASH_MODE_VIDEO_LED;
				}
				else if (0 != (flashArg.flash_mode & FLASH_SELFTEST_AF_LIGHT))
				{
					currentSelftest = FLASH_SELFTEST_AF_LIGHT;
					currentFlashMode = FLASH_MODE_AF_ASSISTANT;
				}
				else if (0 != (flashArg.flash_mode & FLASH_SELFTEST_INDICATOR))
				{
					currentSelftest = FLASH_SELFTEST_INDICATOR;
					currentFlashMode = FLASH_MODE_INDICATOR;
				}
				else
				{
					currentSelftest = FLASH_SELFTEST_TORCH_LIGHT;
					currentFlashMode = FLASH_MODE_VIDEO_LED;
				}

				err = ops->get_mode_details(flash_p->priv_data, currentFlashMode, &details);
				if (0 != err)
				{
					DEBUG_LOG("not able to get mode 0x%lx details\n",currentFlashMode);
					flashArg.status |= currentSelftest;
					flashArg.flash_mode &= ~currentSelftest;
					continue;
				}

				err = ops->enable_flash_mode(flash_p->priv_data, currentFlashMode, 1);
				if (0 != err)
				{
					DEBUG_LOG("not able to enable flash mode 0x%lx\n",currentFlashMode);
					flashArg.status |= currentSelftest;
					flashArg.flash_mode &= ~currentSelftest;
					continue;
				}

				params.duration_uSecs = 0;
				params.intensity_uAmp = details.max_intensity_uAmp;
				params.timeout_uSecs = 0;
				err = ops->configure_flash_mode(flash_p->priv_data, currentFlashMode, &params);
				if (0 != err)
				{
					DEBUG_LOG("not able to configure flash mode 0x%lx\n",currentFlashMode);
					flashArg.status |= currentSelftest;
					flashArg.flash_mode &= ~currentSelftest;
					continue;
				}

				err = ops->trigger_strobe(flash_p->priv_data,1);
				if (0 != err)
				{
					DEBUG_LOG("not able to strobe, mode : 0x%lx\n",currentFlashMode);
					flashArg.status |= currentSelftest;
					flashArg.flash_mode &= ~currentSelftest;
					continue;
				}

				wait_event_timeout(waitQueue, 0, msecs_to_jiffies(1000));

				err = ops->trigger_strobe(flash_p->priv_data,0);
				if (0 != err)
				{
					DEBUG_LOG("not able to strobe, mode : 0x%lx\n",currentFlashMode);
					flashArg.status |= currentSelftest;
					flashArg.flash_mode &= ~currentSelftest;
					continue;
				}
				flashArg.flash_mode &= ~currentSelftest;
			}
		}

		/* job's done ! */
		flash_async_notify();
	}
	return 0;
}

int flash_open(struct inode *node, struct file *file_p)
{
	// init sleep queue
	init_waitqueue_head(&waitQueue);

	// start worker thread
	ptaskStruct = kthread_run (&worker_thread, NULL, "flashDriverWorker");

	return 0;
}

int register_flash_chip(unsigned int cam, struct flash_chip *flash_p)
{
	int err =0;
	DEBUG_LOG("Registering cam %d\n", cam);
	DEBUG_LOG("flash_p: name=%s\n", flash_p->name);
	if(cam > 1 || !flash_p){
		DEBUG_LOG("Registration: something is wrong! cam %d, flash_p %x \n",cam,(int)flash_p);
		err = EINVAL;
		goto out;
	}
	if(!flash_chips[cam]){
		flash_chips[cam] = flash_p;
		DEBUG_LOG("Registered flash: id %lx, %s for camera %d\n",
			flash_p->id,flash_p->name,cam);
	}else{
		DEBUG_LOG("%s flash already registered for camera %d, ignore flash %s\n",
		flash_chips[cam]->name,cam, flash_p->name);
	}
out:
	return err;
}

int flash_async_notify ()
{
	kill_fasync(&async_queue, SIGIO, POLL_IN);
	return 0;
}

static int flash_fasync(int fd, struct file *filp, int mode)
{
        DEBUG_LOG("registered async notification on %d fd\n",fd);
        return fasync_helper(fd, filp, mode, &async_queue);
}

static int flash_release(struct inode *node, struct file *file_p)
{
	int err = 0;

	fasync_helper(-1, file_p, 0, &async_queue);

	// stop worker thread
	waitCondition = 1;
	err = kthread_stop(ptaskStruct);
	return err;
}

static struct file_operations flash_fops = {
	owner:THIS_MODULE,
	unlocked_ioctl:flash_ioctl,
	open:flash_open,
	release:flash_release,
        fasync:flash_fasync,
};

int  major_device_number;

/*Temporary here (adp_init)*/
extern int adp1653_init(void);
static int __init flash_init(void)
{
	int err = 0;
	err = adp1653_init();
	if(err){
		DEBUG_LOG("Unable to initialize adp1653, err %d\n",err);
		goto out;
	}
	/* Register misc device */
    	misc_dev.minor = MISC_DYNAMIC_MINOR;
    	misc_dev.name = "camera_flash";
    	misc_dev.fops = &flash_fops;
    	err = misc_register(&misc_dev);
	if (err < 0) {
                printk(KERN_INFO "camera_flash driver misc_register failed (%d)\n", err);
                return err;
	} else {
                major_device_number = err;
		printk(KERN_INFO "camera_flash driver initialized with minor=%d\n", misc_dev.minor);
	}
out:
	return err;
}

static void __exit flash_exit(void)
{
	misc_deregister(&misc_dev);
	printk(KERN_INFO"Camera flash driver unregistered\n");
}

module_init(flash_init);
module_exit(flash_exit);
MODULE_LICENSE("GPL");
EXPORT_SYMBOL(register_flash_chip);
EXPORT_SYMBOL(flash_async_notify);
