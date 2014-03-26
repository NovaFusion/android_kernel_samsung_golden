/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson MCDE fictive display driver
 *
 * Author: Per Persson <per.xb.persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/err.h>

#include <video/mcde_display.h>

static int __devinit fictive_probe(struct mcde_display_device *dev)
{
	dev->platform_enable = NULL,
	dev->platform_disable = NULL,
	dev->set_power_mode = NULL;

	dev_info(&dev->dev, "Fictive display probed\n");

	return 0;
}

static int __devexit fictive_remove(struct mcde_display_device *dev)
{
	return 0;
}

static struct mcde_display_driver fictive_driver = {
	.probe	= fictive_probe,
	.remove = fictive_remove,
	.driver = {
		.name	= "mcde_disp_fictive",
	},
};

/* Module init */
static int __init mcde_display_fictive_init(void)
{
	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&fictive_driver);
}
module_init(mcde_display_fictive_init);

static void __exit mcde_display_fictive_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&fictive_driver);
}
module_exit(mcde_display_fictive_exit);

MODULE_AUTHOR("Per Persson <per.xb.persson@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson MCDE fictive display driver");
