/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Chris Blair <chris.blair@stericsson.com>
 *   based on modem_u8500.c
 *
 * Platform driver implementing access mechanisms to the M6718 modem.
 */
#include <linux/modem/modem.h>
#include <linux/platform_device.h>
#include <linux/err.h>

static void modem_m6718_request(struct modem_dev *mdev)
{
	/* nothing to do - modem will wake when data is sent */
}

static void modem_m6718_release(struct modem_dev *mdev)
{
	/* nothing to do - modem does not need to be requested/released */
}

static int modem_m6718_is_requested(struct modem_dev *mdev)
{
	return 0;
}

static struct modem_ops modem_m6718_ops = {
	.request = modem_m6718_request,
	.release = modem_m6718_release,
	.is_requested = modem_m6718_is_requested,
};

static struct modem_desc modem_m6718_desc = {
	.name   = "m6718",
	.id     = 0,
	.ops    = &modem_m6718_ops,
	.owner  = THIS_MODULE,
};

static int __devinit modem_m6718_probe(struct platform_device *pdev)
{
	struct modem_dev *mdev;
	int err;

	mdev = modem_register(&modem_m6718_desc, &pdev->dev,
			NULL);
	if (IS_ERR(mdev)) {
		err = PTR_ERR(mdev);
		dev_err(&pdev->dev, "failed to register %s: err %i\n",
			modem_m6718_desc.name, err);
	}

	return 0;
}

static int __devexit modem_m6718_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver modem_m6718_driver = {
	.driver = {
		.name = "modem-m6718",
		.owner = THIS_MODULE,
	},
	.probe = modem_m6718_probe,
	.remove = __devexit_p(modem_m6718_remove),
};

static int __init modem_m6718_init(void)
{
	int ret;

	ret = platform_driver_register(&modem_m6718_driver);
	if (ret < 0) {
		printk(KERN_ERR "modem_m6718: platform driver reg failed\n");
		return ret;
	}

	return 0;
}

static void __exit modem_m6718_exit(void)
{
	platform_driver_unregister(&modem_m6718_driver);
}

module_init(modem_m6718_init);
module_exit(modem_m6718_exit);

MODULE_AUTHOR("Chris Blair <chris.blair@stericsson.com>");
MODULE_DESCRIPTION("M6718 modem access driver");
MODULE_LICENSE("GPL v2");
