/*
 * panic_core.c  --  Voltage/Current Regulator framework.
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 * Copyright 2008 SlimLogic Ltd.
 * Copyright 2011 SERI
 * Author: paul.kennedy@samsung.com
 * Under Kernel Panic conditions allow control of regulators. 
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "dummy.h"

#define REGULATOR_VERSION "0.5"

/*
 * struct regulator_map
 *
 * Used to provide symbolic supply names to devices.
 */
struct regulator_map {
	struct list_head list;
	const char *dev_name;   /* The dev_name() for the consumer */
	const char *supply;
	struct regulator_dev *regulator;
};

/*
 * struct regulator
 *
 * One for each consumer device.
 */
struct regulator {
	struct device *dev;
	struct list_head list;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
	int use;
};

static int _regulator_panic_is_enabled(struct regulator_dev *rdev);
static int _regulator_panic_disable(struct regulator_dev *rdev,
		struct regulator_dev **supply_rdev_ptr);
static int _regulator_panic_enable(struct regulator_dev *rdev);

#ifdef CONFIG_REGULATOR_DUMMY
extern bool has_full_constraints;
#endif

#if defined(CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES) && defined(CONFIG_AB8500_CORE)
#include <linux/mfd/abx500.h>
#include <linux/regulator/ab8500.h>

static int ab8500_panic_regulator_enable(struct regulator_dev *rdev);
static int ab8500_panic_regulator_disable(struct regulator_dev *rdev);
static int ab8500_panic_regulator_is_enabled(struct regulator_dev *rdev);

static struct regulator_ops ab8500_panic_regulator_volt_mode_ops = {
	.enable			= ab8500_panic_regulator_enable,
	.disable		= ab8500_panic_regulator_disable,
	.is_enabled		= ab8500_panic_regulator_is_enabled,
};

static struct regulator_ops ab8500_panic_regulator_mode_ops = {
	.enable			= ab8500_panic_regulator_enable,
	.disable		= ab8500_panic_regulator_disable,
	.is_enabled		= ab8500_panic_regulator_is_enabled,
};

static struct regulator_ops ab8500_panic_regulator_ops = {
	.enable			= ab8500_panic_regulator_enable,
	.disable		= ab8500_panic_regulator_disable,
	.is_enabled		= ab8500_panic_regulator_is_enabled,
};

static int ab8500_panic_regulator_enable(struct regulator_dev *rdev)
{
	int ret = -EPERM;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		pr_emerg("regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, info->update_val_normal);
	if (ret < 0)
		pr_emerg("couldn't set regulator normal mode\n");

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, info->update_val);
	if (ret < 0)
		pr_emerg("couldn't set enable bits for regulator\n");

	info->is_enabled = true;

	return ret;
}

static int ab8500_panic_regulator_disable(struct regulator_dev *rdev)
{
	int ret = -EPERM;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		pr_emerg("regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, 0x0);
	if (ret < 0)
		pr_emerg("couldn't set disable bits for regulator\n");

	info->is_enabled = false;

	return ret;
}

static int ab8500_panic_regulator_is_enabled(struct regulator_dev *rdev)
{
	int ret = -EPERM;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		pr_emerg("regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
		info->update_bank, info->update_reg, &regval);
	if (ret < 0) {
		pr_emerg("couldn't read 0x%x register\n", info->update_reg);
		return ret;
	}

	if (regval & info->update_mask)
		info->is_enabled = true;
	else
		info->is_enabled = false;

	ret = info->is_enabled;

	return ret;
}

void ab8500_reroute_regulator_fp(void)
{
	ab8500_panic_regulator_volt_mode(&ab8500_panic_regulator_volt_mode_ops);
	ab8500_panic_regulator_mode(&ab8500_panic_regulator_mode_ops);
	ab8500_panic_regulator(&ab8500_panic_regulator_ops);
}
#endif

static const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

#define REG_STR_SIZE	32

static struct regulator *create_panic_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name)
{
	static struct regulator regulator;
	struct regulator *pRegulator = &regulator;

	pRegulator->rdev = rdev;
	list_add(&pRegulator->list, &rdev->consumer_list);

	return pRegulator;
}

static int _regulator_panic_get_enable_time(struct regulator_dev *rdev)
{
	return 0;
}

/* Internal regulator request function */
static struct regulator *_regulator_panic_get(struct device *dev, const char *id,
					int exclusive)
{
	struct regulator_dev *rdev;
	struct regulator_map *map;
	struct regulator *regulator = ERR_PTR(-ENODEV);
	struct list_head *pRegulator_panic_map_list;

	const char *devname = NULL;
	int ret;

	if (id == NULL) {
		printk(KERN_ERR "regulator: get() with no identifier\n");
		return regulator;
	}

	if (dev)
		devname = dev_name(dev);

	pRegulator_panic_map_list = regulator_regulator_map_list();

	list_for_each_entry(map, pRegulator_panic_map_list, list) {
		/* If the mapping has a device set up it must match */
		if (map->dev_name &&
		    (!devname || strcmp(map->dev_name, devname)))
			continue;

		if (strcmp(map->supply, id) == 0) {
			rdev = map->regulator;
			goto found;
		}
	}

#ifdef CONFIG_REGULATOR_DUMMY
	if (!devname)
		devname = "deviceless";

	/* If the board didn't flag that it was fully constrained then
	 * substitute in a dummy regulator so consumers can continue.
	 */
	if (!has_full_constraints) {
		pr_warning("%s supply %s not found, using dummy regulator\n",
			   devname, id);
		rdev = dummy_regulator_rdev;
		goto found;
	}
#endif

	return regulator;

found:
	if (rdev->exclusive) {
		regulator = ERR_PTR(-EPERM);
		goto out;
	}

	if (exclusive && rdev->open_count) {
		regulator = ERR_PTR(-EBUSY);
		goto out;
	}

	if (!try_module_get(rdev->owner))
		goto out;

	regulator = create_panic_regulator(rdev, dev, id);

	rdev->open_count++;
	if (exclusive) {
		rdev->exclusive = 1;

		ret = _regulator_panic_is_enabled(rdev);
		if (ret > 0)
			rdev->use_count = 1;
		else
			rdev->use_count = 0;
	}

out:
	return regulator;
}

/**
 * regulator_get - lookup and obtain a reference to a regulator.
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Returns a struct regulator corresponding to the regulator producer,
 * or IS_ERR() condition containing errno.
 *
 * Use of supply names configured via regulator_set_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_panic_get(struct device *dev, const char *id)
{
	return _regulator_panic_get(dev, id, 0);
}
EXPORT_SYMBOL_GPL(regulator_panic_get);

/**
 * regulator_get_exclusive - obtain exclusive access to a regulator.
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Returns a struct regulator corresponding to the regulator producer,
 * or IS_ERR() condition containing errno.  Other consumers will be
 * unable to obtain this reference is held and the use count for the
 * regulator will be initialised to reflect the current state of the
 * regulator.
 *
 * This is intended for use by consumers which cannot tolerate shared
 * use of the regulator such as those which need to force the
 * regulator off for correct operation of the hardware they are
 * controlling.
 *
 * Use of supply names configured via regulator_set_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_panic_get_exclusive(struct device *dev, const char *id)
{
	return _regulator_panic_get(dev, id, 1);
}
EXPORT_SYMBOL_GPL(regulator_panic_get_exclusive);

/**
 * regulator_put - "free" the regulator source
 * @regulator: regulator source
 *
 * Note: drivers must ensure that all regulator_enable calls made on this
 * regulator source are balanced by regulator_disable calls prior to calling
 * this function.
 */
void regulator_panic_put(struct regulator *regulator)
{
	struct regulator_dev *rdev;

	if (regulator == NULL || IS_ERR(regulator))
		return;

	rdev = regulator->rdev;

	list_del(&regulator->list);

	rdev->open_count--;
	rdev->exclusive = 0;
}
EXPORT_SYMBOL_GPL(regulator_panic_put);

static int _regulator_panic_can_change_status(struct regulator_dev *rdev)
{
	if (!rdev->constraints)
		return 0;

	if (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_STATUS)
		return 1;
	else
		return 0;
}

/* locks held by regulator_enable() */
static int _regulator_panic_enable(struct regulator_dev *rdev)
{
	int ret;

	if (rdev->use_count == 0) {
		/* do we need to enable the supply regulator first */
		if (rdev->supply) {
			ret = _regulator_panic_enable(rdev->supply);
			if (ret < 0) {
				printk(KERN_ERR "%s: failed to enable %s: %d\n",
				       __func__, rdev_get_name(rdev), ret);
				return ret;
			}
		}
	}

	if (rdev->use_count == 0) {
		/* The regulator may on if it's not switchable or left on */
		ret = _regulator_panic_is_enabled(rdev);
		if (ret == -EINVAL || ret == 0) {
			if (!_regulator_panic_can_change_status(rdev))
				return -EPERM;

			if (!rdev->desc->ops->enable)
				return -EINVAL;

			/* Allow the regulator to ramp; it would be useful
			 * to extend this for bulk operations so that the
			 * regulators can ramp together.  */
			ret = rdev->desc->ops->enable(rdev);
			if (ret < 0)
				return ret;

		} else if (ret < 0) {
			printk(KERN_ERR "%s: is_enabled() failed for %s: %d\n",
			       __func__, rdev_get_name(rdev), ret);
			return ret;
		}
		/* Fallthrough on positive return values - already enabled */
	}

	rdev->use_count++;

	return 0;
}

/**
 * regulator_enable - enable regulator output
 * @regulator: regulator source
 *
 * Request that the regulator be enabled with the regulator output at
 * the predefined voltage or current value.  Calls to regulator_enable()
 * must be balanced with calls to regulator_disable().
 *
 * NOTE: the output value can be set by other drivers, boot loader or may be
 * hardwired in the regulator.
 */
int regulator_panic_enable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	ret = _regulator_panic_enable(rdev);
	if (ret == 0)
		regulator->use++;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_panic_enable);

static int _regulator_panic_disable(struct regulator_dev *rdev,
		struct regulator_dev **supply_rdev_ptr)
{
	int ret = 0;
	*supply_rdev_ptr = NULL;

	if (WARN(rdev->use_count <= 0,
			"unbalanced disables for %s\n",
			rdev_get_name(rdev)))
		return -EIO;

	/* are we the last user and permitted to disable ? */
	if (rdev->use_count == 1 &&
	    (rdev->constraints && !rdev->constraints->always_on)) {

		/* we are last user */
		if (_regulator_panic_can_change_status(rdev) &&
		    rdev->desc->ops->disable) {
			ret = rdev->desc->ops->disable(rdev);
			if (ret < 0) {
				printk(KERN_ERR "%s: failed to disable %s\n",
				       __func__, rdev_get_name(rdev));
				return ret;
			}

/* Notifier not used for Ux500 Regulators
			_notifier_call_chain(rdev, REGULATOR_EVENT_DISABLE,
					     NULL, 0);
*/
		}

		/* decrease our supplies ref count and disable if required */
		*supply_rdev_ptr = rdev->supply;

		rdev->use_count = 0;
	} else if (rdev->use_count > 1) {

		rdev->use_count--;
	}
	return ret;
}

/**
 * regulator_panic_disable - disable regulator output
 * @regulator: regulator source
 *
 * Disable the regulator output voltage or current.  Calls to
 * regulator_enable() must be balanced with calls to
 * regulator_disable().
 *
 * NOTE: this will only disable the regulator output if no other consumer
 * devices have it enabled, the regulator device supports disabling and
 * machine constraints permit this operation.
 */
int regulator_panic_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct regulator_dev *supply_rdev = NULL;
	int ret = 0;

	ret = _regulator_panic_disable(rdev, &supply_rdev);

	/* decrease our supplies ref count and disable if required */
	while (supply_rdev != NULL) {
		rdev = supply_rdev;

		_regulator_panic_disable(rdev, &supply_rdev);
	}

	if (ret == 0)
		regulator->use--;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_panic_disable);

static int _regulator_panic_is_enabled(struct regulator_dev *rdev)
{
	/* If we don't know then assume that the regulator is always on */
	if (!rdev->desc->ops->is_enabled)
		return 1;

	return rdev->desc->ops->is_enabled(rdev);
}

/**
 * regulator_panic_is_enabled - is the regulator output enabled
 * @regulator: regulator source
 *
 * Returns positive if the regulator driver backing the source/client
 * has requested that the device be enabled, zero if it hasn't, else a
 * negative errno code.
 *
 * Note that the device backing this regulator handle can have multiple
 * users, so it might be enabled even if regulator_enable() was never
 * called for this particular source.
 */
int regulator_panic_is_enabled(struct regulator *regulator)
{
	int ret;

	ret = _regulator_panic_is_enabled(regulator->rdev);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_panic_is_enabled);

