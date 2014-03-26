/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pankaj Chauhan <pankaj.chauhan@stericsson.com> for ST-Ericsson.
 * Author: Vincent Abriou <vincent.abriou@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#include "st_mmio_common.h"

/*
 * The one and only private data holder. Default inited to NULL.
 * Declare it here so no code above can use it directly.
 */
static struct mmio_info *yuv_info;

static long mmio_yuv_ioctl(struct file *filp, u32 cmd,
		unsigned long arg)
{
	struct mmio_input_output_t data;
	int no_of_bytes;
	int ret = 0;
	struct mmio_info *yuv_info =
		(struct mmio_info *)filp->private_data;
	BUG_ON(yuv_info == NULL);

	switch (cmd) {
	case MMIO_CAM_INITBOARD:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user(&data, (struct mmio_input_output_t *)arg,
					no_of_bytes)) {
			dev_err(yuv_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		yuv_info->pdata->camera_slot = data.mmio_arg.camera_slot;

		ret = mmio_cam_initboard(yuv_info);
		break;
	case MMIO_CAM_DESINITBOARD:
		ret = mmio_cam_desinitboard(yuv_info);
		yuv_info->pdata->camera_slot = -1;
		break;
	case MMIO_CAM_PWR_SENSOR:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(yuv_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_cam_pwr_sensor(yuv_info, data.mmio_arg.power_on);
		break;
	case MMIO_CAM_INITMMDSPTIMER:
		ret = mmio_cam_init_mmdsp_timer(yuv_info);
		break;
	default:
		dev_err(yuv_info->dev, "Not an ioctl for YUV camera.\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mmio_yuv_release(struct inode *node, struct file *filp)
{
	/* If not already done... */
	if (yuv_info->pdata->camera_slot != -1) {
		/* ...force camera to power off */
		mmio_cam_pwr_sensor(yuv_info, false);
		/* ...force to desinit board */
		mmio_cam_desinitboard(yuv_info);
		yuv_info->pdata->camera_slot = -1;
	}
	return 0;
}

static int mmio_yuv_open(struct inode *node, struct file *filp)
{
	filp->private_data = yuv_info;	/* Hook our mmio yuv_info */
	return 0;
}

static const struct file_operations mmio_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mmio_yuv_ioctl,
	.open = mmio_yuv_open,
	.release = mmio_yuv_release,
};


/**
 * mmio_yuv_probe() - Initialize MMIO yuv Camera resources.
 * @pdev: Platform device.
 *
 * Initialize the module and register misc device.
 *
 * Returns:
 *	0 if there is no err.
 *	-ENOMEM if allocation fails.
 *	-EEXIST if device has already been started.
 *	Error codes from misc_register.
 */
static int __devinit mmio_yuv_probe(struct platform_device *pdev)
{
	int err;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	/* Initialize private data. */
	yuv_info = kzalloc(sizeof(struct mmio_info), GFP_KERNEL);

	if (!yuv_info) {
		dev_err(&pdev->dev, "Could not alloc yuv_info struct\n");
		err = -ENOMEM;
		goto err_alloc;
	}

	/* Fill in private data */
	yuv_info->pdata                    = pdev->dev.platform_data;
	yuv_info->dev                      = &pdev->dev;
	yuv_info->pdata->dev               = &pdev->dev;
	yuv_info->xshutdown_enabled        = 0;
	yuv_info->xshutdown_is_active_high = 0;
	yuv_info->trace_allowed            = 0;

	/* Register Misc character device */
	yuv_info->misc_dev.minor           = MISC_DYNAMIC_MINOR;
	yuv_info->misc_dev.name            = MMIO_YUV_NAME;
	yuv_info->misc_dev.fops            = &mmio_fops;
	yuv_info->misc_dev.parent          = pdev->dev.parent;
	err = misc_register(&(yuv_info->misc_dev));

	if (err) {
		dev_err(&pdev->dev, "Error %d registering misc dev!", err);
		goto err_miscreg;
	}

	/* Memory mapping */
	yuv_info->siabase = ioremap(yuv_info->pdata->sia_base,
			SIA_ISP_MCU_SYS_SIZE);

	if (!yuv_info->siabase) {
		dev_err(yuv_info->dev, "Could not ioremap SIA_BASE\n");
		err = -ENOMEM;
		goto err_ioremap_sia_base;
	}

	yuv_info->crbase = ioremap(yuv_info->pdata->cr_base, PAGE_SIZE);

	if (!yuv_info->crbase) {
		dev_err(yuv_info->dev, "Could not ioremap CR_BASE\n");
		err = -ENOMEM;
		goto err_ioremap_cr_base;
	}

	dev_info(yuv_info->dev, "mmio driver initialized with minor=%d\n",
			yuv_info->misc_dev.minor);

	return 0;

err_ioremap_cr_base:
	iounmap(yuv_info->siabase);
err_ioremap_sia_base:
	misc_deregister(&yuv_info->misc_dev);
err_miscreg:
	kfree(yuv_info);
	yuv_info = NULL;
err_alloc:
	return err;
}

/**
 * mmio_yuv_remove() - Release MMIO yuv Camera resources.
 * @pdev:	Platform device.
 *
 * Remove misc device and free resources.
 *
 * Returns:
 *	0 if success.
 *	Error codes from misc_deregister.
 */
static int __devexit mmio_yuv_remove(struct platform_device *pdev)
{
	int err;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	if (!yuv_info)
		return 0;

	err = misc_deregister(&yuv_info->misc_dev);

	if (err)
		dev_err(&pdev->dev, "Error %d deregistering misc dev", err);

	iounmap(yuv_info->siabase);
	iounmap(yuv_info->crbase);
	kfree(yuv_info);
	yuv_info = NULL;
	return 0;
}

/**
 * platform_driver definition:
 * mmio_yuv_driver
 */
static struct platform_driver mmio_yuv_driver = {
	.driver = {
		.name = MMIO_YUV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = mmio_yuv_probe,
	.remove = __devexit_p(mmio_yuv_remove)
};

/**
 * mmio_yuv_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init mmio_yuv_init(void)
{
	return platform_driver_register(&mmio_yuv_driver);
}

/**
 * mmio_yuv_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit mmio_yuv_exit(void)
{
	platform_driver_unregister(&mmio_yuv_driver);
}

module_init(mmio_yuv_init);
module_exit(mmio_yuv_exit);

MODULE_AUTHOR("Joakim Axelsson ST-Ericsson");
MODULE_AUTHOR("Pankaj Chauhan ST-Ericsson");
MODULE_AUTHOR("Vincent Abriou ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MMIO Camera driver");
