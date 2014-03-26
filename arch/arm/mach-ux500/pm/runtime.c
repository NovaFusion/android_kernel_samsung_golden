/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson> for ST-Ericsson
 *
 * Based on:
 *  Runtime PM support code for SuperH Mobile ARM
 *  Copyright (C) 2009-2010 Magnus Damm
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/regulator/dbx500-prcmu.h>
#include <linux/clk.h>
#include <plat/pincfg.h>

#include "../pins.h"

#ifdef CONFIG_PM_RUNTIME
#define BIT_ONCE		0
#define BIT_ACTIVE		1
#define BIT_ENABLED	2

struct pm_runtime_data {
	unsigned long flags;
	struct ux500_regulator *regulator;
	struct ux500_pins *pins;
};

static void __devres_release(struct device *dev, void *res)
{
	struct pm_runtime_data *prd = res;

	dev_dbg(dev, "__devres_release()\n");

	if (test_bit(BIT_ENABLED, &prd->flags)) {
		if (prd->pins)
			ux500_pins_disable(prd->pins);
		if (prd->regulator)
			ux500_regulator_atomic_disable(prd->regulator);
	}

	if (test_bit(BIT_ACTIVE, &prd->flags)) {
		if (prd->pins)
			ux500_pins_put(prd->pins);
		if (prd->regulator)
			ux500_regulator_put(prd->regulator);
	}
}

static struct pm_runtime_data *__to_prd(struct device *dev)
{
	return devres_find(dev, __devres_release, NULL, NULL);
}

static void platform_pm_runtime_init(struct device *dev,
				     struct pm_runtime_data *prd)
{
	prd->pins = ux500_pins_get(dev_name(dev));

	prd->regulator = ux500_regulator_get(dev);
	if (IS_ERR(prd->regulator))
		prd->regulator = NULL;

	if (prd->pins || prd->regulator) {
		dev_info(dev, "managed by runtime pm: %s%s\n",
			 prd->pins ? "pins " : "",
			 prd->regulator ? "regulator " : "");

		set_bit(BIT_ACTIVE, &prd->flags);
	}
}

static void platform_pm_runtime_bug(struct device *dev,
				    struct pm_runtime_data *prd)
{
	if (prd && !test_and_set_bit(BIT_ONCE, &prd->flags))
		dev_err(dev, "runtime pm suspend before resume\n");
}

static void platform_pm_runtime_used(struct device *dev,
				       struct pm_runtime_data *prd)
{
	if (prd)
		set_bit(BIT_ONCE, &prd->flags);
}

static int ux500_pd_runtime_idle(struct device *dev)
{
	return pm_runtime_suspend(dev);
}

static void ux500_pd_disable(struct pm_runtime_data *prd)
{
	if (prd && test_bit(BIT_ACTIVE, &prd->flags)) {

		if (prd->pins)
			ux500_pins_disable(prd->pins);

		if (prd->regulator)
			ux500_regulator_atomic_disable(prd->regulator);

		clear_bit(BIT_ENABLED, &prd->flags);
	}
}

static int ux500_pd_runtime_suspend(struct device *dev)
{
	int ret;
	struct pm_runtime_data *prd = __to_prd(dev);

	dev_vdbg(dev, "%s()\n", __func__);

	platform_pm_runtime_bug(dev, prd);

	ret = pm_generic_runtime_suspend(dev);
	if (ret)
		return ret;

	ux500_pd_disable(prd);

	return 0;
}

static void ux500_pd_enable(struct pm_runtime_data *prd)
{
	if (prd && test_bit(BIT_ACTIVE, &prd->flags)) {
		if (prd->pins)
			ux500_pins_enable(prd->pins);

		if (prd->regulator)
			ux500_regulator_atomic_enable(prd->regulator);

		set_bit(BIT_ENABLED, &prd->flags);
	}
}

static int ux500_pd_runtime_resume(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);

	dev_vdbg(dev, "%s()\n", __func__);

	platform_pm_runtime_used(dev, prd);
	ux500_pd_enable(prd);

	return pm_generic_runtime_resume(dev);
}

static int ux500_pd_suspend_noirq(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);

	dev_vdbg(dev, "%s()\n", __func__);

	/* Only handle devices that use runtime pm */
	if (!prd || !test_bit(BIT_ONCE, &prd->flags))
		return 0;

	/* Already is runtime suspended?  Nothing to do. */
	if (pm_runtime_status_suspended(dev))
		return 0;

	/*
	 * We get here only if the device was not runtime suspended for some
	 * reason.  We still need to do the power save stuff when going into
	 * suspend, so force it here.
	 */
	return ux500_pd_runtime_suspend(dev);
}

static int ux500_pd_resume_noirq(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);

	dev_vdbg(dev, "%s()\n", __func__);

	/* Only handle devices that use runtime pm */
	if (!prd || !test_bit(BIT_ONCE, &prd->flags))
		return 0;

	/*
	 * Already was runtime suspended?  No need to resume here, runtime
	 * resume will take care of it.
	 */
	if (pm_runtime_status_suspended(dev))
		return 0;

	/*
	 * We get here only if the device was not runtime suspended,
	 * but we forced it down in suspend_noirq above.  Bring it
	 * up since pm-runtime thinks it is not suspended.
	 */
	return ux500_pd_runtime_resume(dev);
}

static int ux500_pd_amba_suspend_noirq(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);
	int (*callback)(struct device *) = NULL;
	int ret = 0;
	bool is_suspended = pm_runtime_status_suspended(dev);

	dev_vdbg(dev, "%s()\n", __func__);

	/*
	 * Do not bypass AMBA bus pm functions by calling generic
	 * pm directly. A future fix could be to implement a
	 * "pm_bus_generic_*" API which we can use instead.
	 */
	if (dev->bus && dev->bus->pm)
		callback = dev->bus->pm->suspend_noirq;

	if (callback)
		ret = callback(dev);
	else
		ret = pm_generic_suspend_noirq(dev);

	if (!ret && !is_suspended)
		ux500_pd_disable(prd);

	return ret;
}

static int ux500_pd_amba_resume_noirq(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);
	int (*callback)(struct device *) = NULL;
	int ret = 0;
	bool is_suspended = pm_runtime_status_suspended(dev);

	dev_vdbg(dev, "%s()\n", __func__);

	/*
	 * Do not bypass AMBA bus pm functions by calling generic
	 * pm directly. A future fix could be to implement a
	 * "pm_bus_generic_*" API which we can use instead.
	 */
	if (dev->bus && dev->bus->pm)
		callback = dev->bus->pm->resume_noirq;

	if (callback)
		ret = callback(dev);
	else
		ret = pm_generic_resume_noirq(dev);

	if (!ret && !is_suspended)
		ux500_pd_enable(prd);

	return ret;
}

static int ux500_pd_amba_runtime_suspend(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);
	int (*callback)(struct device *) = NULL;
	int ret;

	dev_vdbg(dev, "%s()\n", __func__);

	/*
	 * Do this first, to make sure pins is not in undefined state after
	 * drivers has run their runtime suspend. This also means that drivers
	 * are not able to use their pins/regulators during runtime suspend.
	 */
	ux500_pd_disable(prd);

	/*
	 * Do not bypass AMBA bus pm functions by calling generic
	 * pm directly. A future fix could be to implement a
	 * "pm_bus_generic_*" API which we can use instead.
	 */
	if (dev->bus && dev->bus->pm)
		callback = dev->bus->pm->runtime_suspend;

	if (callback)
		ret = callback(dev);
	else
		ret = pm_generic_runtime_suspend(dev);

	if (ret)
		ux500_pd_enable(prd);

	return ret;
}

static int ux500_pd_amba_runtime_resume(struct device *dev)
{
	struct pm_runtime_data *prd = __to_prd(dev);
	int (*callback)(struct device *) = NULL;
	int ret;

	dev_vdbg(dev, "%s()\n", __func__);

	/*
	 * Do not bypass AMBA bus pm functions by calling generic
	 * pm directly. A future fix could be to implement a
	 * "pm_bus_generic_*" API which we can use instead.
	 */
	if (dev->bus && dev->bus->pm)
		callback = dev->bus->pm->runtime_resume;

	if (callback)
		ret = callback(dev);
	else
		ret = pm_generic_runtime_resume(dev);

	/*
	 * Restore pins/regulator after drivers has runtime resumed, due
	 * to that we must not have pins in undefined state. This also means
	 * that drivers are not able to use their pins/regulators during
	 * runtime resume.
	 */
	if (!ret)
		ux500_pd_enable(prd);

	return ret;
}

static int ux500_pd_amba_runtime_idle(struct device *dev)
{
	int (*callback)(struct device *) = NULL;
	int ret;

	dev_vdbg(dev, "%s()\n", __func__);

	/*
	 * Do not bypass AMBA bus runtime functions by calling generic runtime
	 * directly. A future fix could be to implement a
	 * "pm_bus_generic_runtime_*" API which we can use instead.
	 */
	if (dev->bus && dev->bus->pm)
		callback = dev->bus->pm->runtime_idle;

	if (callback)
		ret = callback(dev);
	else
		ret = pm_generic_runtime_idle(dev);

	return ret;
}

static int ux500_pd_bus_notify(struct notifier_block *nb,
				unsigned long action,
				void *data,
				bool enable)
{
	struct device *dev = data;
	struct pm_runtime_data *prd;

	dev_dbg(dev, "%s() %ld !\n", __func__, action);

	if (action == BUS_NOTIFY_BIND_DRIVER) {
		prd = devres_alloc(__devres_release, sizeof(*prd), GFP_KERNEL);
		if (prd) {
			devres_add(dev, prd);
			platform_pm_runtime_init(dev, prd);
			if (enable)
				ux500_pd_enable(prd);
		} else
			dev_err(dev, "unable to alloc memory for runtime pm\n");
	}

	return 0;
}

static int ux500_pd_plat_bus_notify(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	return ux500_pd_bus_notify(nb, action, data, false);
}

static int ux500_pd_amba_bus_notify(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	return ux500_pd_bus_notify(nb, action, data, true);
}

#else /* CONFIG_PM_RUNTIME */

#define ux500_pd_suspend_noirq	NULL
#define ux500_pd_resume_noirq	NULL
#define ux500_pd_runtime_suspend	NULL
#define ux500_pd_runtime_resume	NULL

static int ux500_pd_bus_notify(struct notifier_block *nb,
			       unsigned long action, void *data)
{
	struct ux500_regulator *regulator = NULL;
	struct ux500_pins *pins = NULL;
	struct device *dev = data;
	const char *onoff = NULL;

	dev_dbg(dev, "%s() %ld !\n", __func__, action);

	switch (action) {
	case BUS_NOTIFY_BIND_DRIVER:
		pins = ux500_pins_get(dev_name(dev));
		if (pins) {
			ux500_pins_enable(pins);
			ux500_pins_put(pins);
		}

		regulator = ux500_regulator_get(dev);
		if (IS_ERR(regulator))
			regulator = NULL;
		else {
			ux500_regulator_atomic_enable(regulator);
			ux500_regulator_put(regulator);
		}

		onoff = "on";
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		pins = ux500_pins_get(dev_name(dev));
		if (pins) {
			ux500_pins_disable(pins);
			ux500_pins_put(pins);
		}

		regulator = ux500_regulator_get(dev);
		if (IS_ERR(regulator))
			regulator = NULL;
		else {
			ux500_regulator_atomic_disable(regulator);
			ux500_regulator_put(regulator);
		}

		onoff = "off";
		break;
	}

	if (pins || regulator) {
		dev_info(dev, "runtime pm disabled, forced %s: %s%s\n",
			 onoff,
			 pins ? "pins " : "",
			 regulator ? "regulator " : "");
	}

	return 0;
}

#endif /* CONFIG_PM_RUNTIME */

struct dev_power_domain ux500_amba_dev_power_domain = {
	.ops = {
		/* USE_AMBA_PM_SLEEP_OPS minus the two we replace */
		.prepare = amba_pm_prepare,
		.complete = amba_pm_complete,
		.suspend = amba_pm_suspend,
		.resume = amba_pm_resume,
		.freeze = amba_pm_freeze,
		.thaw = amba_pm_thaw,
		.poweroff = amba_pm_poweroff,
		.restore = amba_pm_restore,
		.freeze_noirq = amba_pm_freeze_noirq,
		.thaw_noirq = amba_pm_thaw_noirq,
		.poweroff_noirq = amba_pm_poweroff_noirq,
		.restore_noirq = amba_pm_restore_noirq,

		.suspend_noirq = ux500_pd_amba_suspend_noirq,
		.resume_noirq = ux500_pd_amba_resume_noirq,

		SET_RUNTIME_PM_OPS(ux500_pd_amba_runtime_suspend,
				   ux500_pd_amba_runtime_resume,
				   ux500_pd_amba_runtime_idle)
	},
};

struct dev_power_domain ux500_dev_power_domain = {
	.ops = {
		/* USE_PLATFORM_PM_SLEEP_OPS minus the two we replace */
		.prepare = platform_pm_prepare,
		.complete = platform_pm_complete,
		.suspend = platform_pm_suspend,
		.resume = platform_pm_resume,
		.freeze = platform_pm_freeze,
		.thaw = platform_pm_thaw,
		.poweroff = platform_pm_poweroff,
		.restore = platform_pm_restore,
		.freeze_noirq = platform_pm_freeze_noirq,
		.thaw_noirq = platform_pm_thaw_noirq,
		.poweroff_noirq = platform_pm_poweroff_noirq,
		.restore_noirq = platform_pm_restore_noirq,

		.suspend_noirq		= ux500_pd_suspend_noirq,
		.resume_noirq		= ux500_pd_resume_noirq,
		.runtime_idle		= ux500_pd_runtime_idle,
		.runtime_suspend	= ux500_pd_runtime_suspend,
		.runtime_resume		= ux500_pd_runtime_resume,
	},
};

static struct notifier_block ux500_pd_platform_notifier = {
	.notifier_call = ux500_pd_plat_bus_notify,
};

static struct notifier_block ux500_pd_amba_notifier = {
	.notifier_call = ux500_pd_amba_bus_notify,
};

static int __init ux500_pm_runtime_platform_init(void)
{
	bus_register_notifier(&platform_bus_type, &ux500_pd_platform_notifier);
	return 0;
}
core_initcall(ux500_pm_runtime_platform_init);

/*
 * The amba bus itself gets registered in a core_initcall, so we can't use
 * that.
 */
static int __init ux500_pm_runtime_amba_init(void)
{
	bus_register_notifier(&amba_bustype, &ux500_pd_amba_notifier);
	return 0;
}
arch_initcall(ux500_pm_runtime_amba_init);
