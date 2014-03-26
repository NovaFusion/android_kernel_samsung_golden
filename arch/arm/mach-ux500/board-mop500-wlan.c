/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include "pins.h"
#include <mach/cw1200_plat.h>
#include <linux/clk.h>

static void cw1200_release(struct device *dev);
static int cw1200_clk_ctrl(const struct cw1200_platform_data *pdata,
		bool enable);

static struct resource cw1200_href_resources[] = {
	{
		.start = 215,
		.end = 215,
		.flags = IORESOURCE_IO,
		.name = "cw1200_reset",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(216),
		.end = NOMADIK_GPIO_TO_IRQ(216),
		.flags = IORESOURCE_IRQ,
		.name = "cw1200_irq",
	},
};

static struct resource cw1200_href60_resources[] = {
	{
		.start = 85,
		.end = 85,
		.flags = IORESOURCE_IO,
		.name = "cw1200_reset",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(4),
		.end = NOMADIK_GPIO_TO_IRQ(4),
		.flags = IORESOURCE_IRQ,
		.name = "cw1200_irq",
	},
};

static struct resource cw1200_u9500_resources[] = {
	{
		.start = 85,
		.end = 85,
		.flags = IORESOURCE_IO,
		.name = "cw1200_reset",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(144),
		.end = NOMADIK_GPIO_TO_IRQ(144),
		.flags = IORESOURCE_IRQ,
		.name = "cw1200_irq",
	},
};

static struct cw1200_platform_data cw1200_platform_data = {
	.clk_ctrl = cw1200_clk_ctrl,
};

static struct platform_device cw1200_device = {
	.name = "cw1200_wlan",
	.dev = {
		.platform_data = &cw1200_platform_data,
		.release = cw1200_release,
		.init_name = "cw1200_wlan",
	},
};

static struct clk *clk_dev;

const struct cw1200_platform_data *cw1200_get_platform_data(void)
{
	return &cw1200_platform_data;
}
EXPORT_SYMBOL_GPL(cw1200_get_platform_data);

static int cw1200_clk_ctrl(const struct cw1200_platform_data *pdata,
		bool enable)
{
	static const char *clock_name = "sys_clk_out";
	int ret = 0;

	if (enable) {
		clk_dev = clk_get(&cw1200_device.dev, clock_name);
		if (IS_ERR(clk_dev)) {
			ret = PTR_ERR(clk_dev);
			dev_warn(&cw1200_device.dev,
				"%s: Failed to get clk '%s': %d\n",
				__func__, clock_name, ret);
		} else {
			ret = clk_enable(clk_dev);
			if (ret) {
				clk_put(clk_dev);
				dev_warn(&cw1200_device.dev,
					"%s: Failed to enable clk '%s': %d\n",
					__func__, clock_name, ret);
			}
		}
	} else {
		clk_disable(clk_dev);
		clk_put(clk_dev);
	}

	return ret;
}

int __init mop500_wlan_init(void)
{
#if 0
	if (pins_for_u9500()) {
		cw1200_device.num_resources = ARRAY_SIZE(cw1200_u9500_resources);
		cw1200_device.resource = cw1200_u9500_resources;
	} else if (machine_is_u8500() || machine_is_nomadik()) {
		cw1200_device.num_resources = ARRAY_SIZE(cw1200_href_resources);
		cw1200_device.resource = cw1200_href_resources;
	} else if (machine_is_hrefv60() || machine_is_u8520()
			 || machine_is_u9540()) {
		cw1200_device.num_resources =
				ARRAY_SIZE(cw1200_href60_resources);
		cw1200_device.resource = cw1200_href60_resources;
	} else {
		dev_err(&cw1200_device.dev,
				"Unsupported mach type %d "
				"(check mach-types.h)\n",
				__machine_arch_type);
		return -ENOTSUPP;
	}
#else
	cw1200_device.num_resources = ARRAY_SIZE(cw1200_href_resources);
	cw1200_device.resource = cw1200_href_resources;
#endif

	cw1200_platform_data.mmc_id = "mmc2";

	cw1200_platform_data.reset = &cw1200_device.resource[0];
	cw1200_platform_data.irq = &cw1200_device.resource[1];

	cw1200_device.dev.release = cw1200_release;

	return platform_device_register(&cw1200_device);
}

static void cw1200_release(struct device *dev)
{

}
