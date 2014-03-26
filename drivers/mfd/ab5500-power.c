/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>

static struct device *dev;

/* STARTUP */
#define AB5500_SYSPOR_CONTROL	0x30

/* VINT IO I2C CLOCK */
#define AB5500_RTC_VINT		0x01

int ab5500_clock_rtc_enable(int num, bool enable)
{
	/* RTC_CLK{0,1,2} are bits {4,3,2}, active low */
	u8 mask = BIT(4 - num);
	u8 value = enable ? 0 : mask;

	/* Don't allow RTC_CLK0 to be controlled. */
	if (num < 1 || num > 2)
		return -EINVAL;

	if (!dev)
		return -EAGAIN;

	return abx500_mask_and_set(dev, AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
				   AB5500_RTC_VINT, mask, value);
}

static void ab5500_power_off(void)
{
	sigset_t old;
	sigset_t all;

	sigfillset(&all);

	if (!sigprocmask(SIG_BLOCK, &all, &old)) {
		/* Clear dbb_on */
		int ret = abx500_set(dev, AB5500_BANK_STARTUP,
				     AB5500_SYSPOR_CONTROL, 0);
		WARN_ON(ret);
	}
}

static int __devinit ab5500_power_probe(struct platform_device *pdev)
{
	struct ab5500_platform_data *plat = dev_get_platdata(pdev->dev.parent);

	dev = &pdev->dev;

	if (plat->pm_power_off)
		pm_power_off = ab5500_power_off;

	return 0;
}

static int __devexit ab5500_power_remove(struct platform_device *pdev)
{
	struct ab5500_platform_data *plat = dev_get_platdata(pdev->dev.parent);

	if (plat->pm_power_off)
		pm_power_off = NULL;
	dev = NULL;

	return 0;
}

static struct platform_driver ab5500_power_driver = {
	.driver = {
		.name = "ab5500-power",
		.owner = THIS_MODULE,
	},
	.probe = ab5500_power_probe,
	.remove = __devexit_p(ab5500_power_remove),
};

static int __init ab8500_sysctrl_init(void)
{
	return platform_driver_register(&ab5500_power_driver);
}

subsys_initcall(ab8500_sysctrl_init);

MODULE_DESCRIPTION("AB5500 power driver");
MODULE_LICENSE("GPL v2");
