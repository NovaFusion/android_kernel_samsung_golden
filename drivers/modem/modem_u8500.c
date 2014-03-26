/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 *
 * Platform driver implementing access mechanisms to modem
 * on U8500 which uses Shared Memroy as IPC between Application
 * Processor and Modem processor.
 */
#include <linux/modem/modem.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mfd/dbx500-prcmu.h>

static void u8500_modem_request(struct modem_dev *mdev)
{
	prcmu_ac_wake_req();
}

static void u8500_modem_release(struct modem_dev *mdev)
{
	prcmu_ac_sleep_req();
}

static int u8500_modem_is_requested(struct modem_dev *mdev)
{
	return prcmu_is_ac_wake_requested();
}

static struct modem_ops u8500_modem_ops = {
	.request = u8500_modem_request,
	.release = u8500_modem_release,
	.is_requested = u8500_modem_is_requested,
};

static struct modem_desc u8500_modem_desc = {
	.name   = "u8500-shrm-modem",
	.id     = 0,
	.ops    = &u8500_modem_ops,
	.owner  = THIS_MODULE,
};


static int __devinit u8500_modem_probe(struct platform_device *pdev)
{
	struct modem_dev *mdev;
	int err;

	mdev = modem_register(&u8500_modem_desc, &pdev->dev,
			NULL);
	if (IS_ERR(mdev)) {
		err = PTR_ERR(mdev);
		pr_err("failed to register %s: err %i\n",
				u8500_modem_desc.name, err);
	}

	return 0;
}

static int __devexit u8500_modem_remove(struct platform_device *pdev)
{

	return 0;
}

static struct platform_driver u8500_modem_driver = {
	.driver = {
		.name = "u8500-modem",
		.owner = THIS_MODULE,
	},
	.probe = u8500_modem_probe,
	.remove = __devexit_p(u8500_modem_remove),
};

static int __init u8500_modem_init(void)
{
	int ret;

	ret = platform_driver_register(&u8500_modem_driver);
	if (ret < 0) {
		printk(KERN_ERR "u8500_modem: platform driver reg failed\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit u8500_modem_exit(void)
{
	platform_driver_unregister(&u8500_modem_driver);
}

arch_initcall(u8500_modem_init);
