/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
  *Author: Bartosz Markowski <bartosz.markowski@tieto.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include "pins.h"
#include <mach/cw1200_plat.h>


static void cw1200_release(struct device *dev);
static int cw1200_prcmu_ctrl(const struct cw1200_platform_data *pdata,
		bool enable);

static struct resource cw1200_u5500_resources[] = {
	{
		.start = NOMADIK_GPIO_TO_IRQ(129),
		.end = NOMADIK_GPIO_TO_IRQ(129),
		.flags = IORESOURCE_IRQ,
		.name = "cw1200_irq",
	},
};

static struct cw1200_platform_data cw1200_platform_data = {
	.prcmu_ctrl = cw1200_prcmu_ctrl,
};

static struct platform_device cw1200_device = {
	.name = "cw1200_wlan",
	.dev = {
		.platform_data = &cw1200_platform_data,
		.release = cw1200_release,
		.init_name = "cw1200_wlan",
	},
};

const struct cw1200_platform_data *cw1200_get_platform_data(void)
{
	return &cw1200_platform_data;
}
EXPORT_SYMBOL_GPL(cw1200_get_platform_data);

static int cw1200_prcmu_ctrl(const struct cw1200_platform_data *pdata,
		bool enable)
{
	int ret;

	if (enable)
		ret = prcmu_resetout(2, 1);
	else
		ret = prcmu_resetout(2, 0);

	return ret;
}

int __init u5500_wlan_init(void)
{
	if (machine_is_u5500()) {
		cw1200_device.num_resources = ARRAY_SIZE(cw1200_u5500_resources);
		cw1200_device.resource = cw1200_u5500_resources;
	} else {
		dev_err(&cw1200_device.dev,
				"Unsupported mach type %d "
				"(check mach-types.h)\n",
				__machine_arch_type);
		return -ENOTSUPP;
	}

	cw1200_platform_data.mmc_id = "mmc2";
	cw1200_platform_data.irq = &cw1200_device.resource[0];

	cw1200_device.dev.release = cw1200_release;

	return platform_device_register(&cw1200_device);
}

static void cw1200_release(struct device *dev)
{

}
