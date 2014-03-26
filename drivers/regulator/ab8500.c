/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *          Daniel Willerud <daniel.willerud@stericsson.com> for ST-Ericsson
 *
 * AB8500 peripheral regulators
 *
 * AB8500 supports the following regulators:
 *   VAUX1/2/3, VINTCORE, VTVOUT, VUSB, VAUDIO, VAMIC1/2, VDMIC, VANA
 *
 * AB8505 supports the following regulators:
 *   VAUX1/2/3/4/5/6, VINTCORE, VADC, VUSB, VAUDIO, VAMIC1/2, VDMIC, VANA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>
#include <linux/mfd/abx500/ab8500-gpio.h> /* for sysclkreq pins */
#include <mach/id.h>

#ifndef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES
/**
 * struct ab8500_regulator_info - ab8500 regulator information
 * @dev: device pointer
 * @desc: regulator description
 * @regulator_dev: regulator device
 * @is_enabled: status of regulator (on/off)
 * @max_uV: maximum voltage (for variable voltage supplies)
 * @min_uV: minimum voltage (for variable voltage supplies)
 * @fixed_uV: typical voltage (for fixed voltage supplies)
 * @load_lp_uA: maximum load in idle (low power) mode
 * @update_bank: bank to control on/off
 * @update_reg: register to control on/off
 * @update_mask: mask to enable/disable and set mode of regulator
 * @update_val: bits holding the regulator current mode
 * @update_val_idle: bits to enable the regulator in idle (low power) mode
 * @update_val_normal: bits to enable the regulator in normal (high power) mode
 * @voltage_bank: bank to control regulator voltage
 * @voltage_reg: register to control regulator voltage
 * @voltage_mask: mask to control regulator voltage
 * @voltage_shift: shift to control regulator voltage
 * @voltages: supported voltage table
 * @voltages_len: number of supported voltages for the regulator
 * @delay: startup/set voltage delay in us
 * @gpio_pin: ab8500 gpio pin offset number (for sysclkreq regulator only)
 */
struct ab8500_regulator_info {
	struct device		*dev;
	struct regulator_desc	desc;
	struct regulator_dev	*regulator;
	bool is_enabled;
	int max_uV;
	int min_uV;
	int fixed_uV;
	int load_lp_uA;
	u8 update_bank;
	u8 update_reg;
	u8 update_mask;
	u8 update_val;
	u8 update_val_idle;
	u8 update_val_normal;
	u8 voltage_bank;
	u8 voltage_reg;
	u8 voltage_mask;
	u8 voltage_shift;
	int const *voltages;
	int voltages_len;
	unsigned int delay;
	int gpio_pin;
};

#endif //CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES


/* voltage tables for the vauxn/vintcore supplies */
static const int ldo_vauxn_voltages[] = {
	1100000,
	1200000,
	1300000,
	1400000,
	1500000,
	1800000,
	1850000,
	1900000,
	2500000,
	2650000,
	2700000,
	2750000,
	2800000,
	2900000,
	3000000,
	3300000,
};

static const int ldo_vaux3_voltages[] = {
	1200000,
	1500000,
	1800000,
	2100000,
	2500000,
	2750000,
	2790000,
	2910000,
};

static const int ldo_vaux56_voltages[] = {
	1800000,
	1050000,
	1100000,
	1200000,
	1500000,
	2200000,
	2500000,
	2790000,
};

static const int ldo_vintcore_voltages[] = {
	1200000,
	1225000,
	1250000,
	1275000,
	1300000,
	1325000,
	1350000,
};

static const int ldo_vana_voltages[] = {
	1050000,
	1075000,
	1100000,
	1125000,
	1150000,
	1175000,
	1200000,
	1225000,
};

static const int ldo_vaudio_voltages[] = {
	2000000,
	2100000,
	2200000,
	2300000,
	2400000,
	2500000,
	2600000,
	2600000,	/* Duplicated in Vaudio and IsoUicc Control register. */
};

static int ab8500_regulator_enable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, info->update_val);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
			"couldn't set enable bits for regulator\n");

	info->is_enabled = true;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-enable (bank, reg, mask, value): 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, info->update_val);

	return ret;
}

static int ab8500_regulator_disable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, 0x0);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
			"couldn't set disable bits for regulator\n");

	info->is_enabled = false;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-disable (bank, reg, mask, value): 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, 0x0);

	return ret;
}

static unsigned int ab8500_regulator_get_optimum_mode(
		struct regulator_dev *rdev, int input_uV,
		int output_uV, int load_uA)
{
	unsigned int mode;

	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	if (load_uA <= info->load_lp_uA)
		mode = REGULATOR_MODE_IDLE;
	else
		mode = REGULATOR_MODE_NORMAL;

	return mode;
}

static int ab8500_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	int ret = 0;

	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		info->update_val = info->update_val_normal;
		break;
	case REGULATOR_MODE_IDLE:
		info->update_val = info->update_val_idle;
		break;
	default:
		return -EINVAL;
	}

	if (info->is_enabled) {
		ret = abx500_mask_and_set_register_interruptible(info->dev,
			info->update_bank, info->update_reg,
			info->update_mask, info->update_val);
		if (ret < 0)
			dev_err(rdev_get_dev(rdev),
				"couldn't set regulator mode\n");

		dev_vdbg(rdev_get_dev(rdev),
			"%s-set_mode (bank, reg, mask, value): "
			"0x%x, 0x%x, 0x%x, 0x%x\n",
			info->desc.name, info->update_bank, info->update_reg,
			info->update_mask, info->update_val);
	}

	return ret;
}

static unsigned int ab8500_regulator_get_mode(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	if (info->update_val == info->update_val_normal)
		ret = REGULATOR_MODE_NORMAL;
	else if (info->update_val == info->update_val_idle)
		ret = REGULATOR_MODE_IDLE;
	else
		ret = -EINVAL;

	return ret;
}

static int ab8500_regulator_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
		info->update_bank, info->update_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read 0x%x register\n", info->update_reg);
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-is_enabled (bank, reg, mask, value): 0x%x, 0x%x, 0x%x,"
		" 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, regval);

	if (regval & info->update_mask)
		info->is_enabled = true;
	else
		info->is_enabled = false;

	return info->is_enabled;
}

static int ab8500_list_voltage(struct regulator_dev *rdev, unsigned selector)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	/* return the uV for the fixed regulators */
	if (info->fixed_uV)
		return info->fixed_uV;

	if (selector >= info->voltages_len)
		return -EINVAL;

	return info->voltages[selector];
}

static int ab8500_regulator_get_voltage(struct regulator_dev *rdev)
{
	int ret, val;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
			info->voltage_bank, info->voltage_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage reg for regulator\n");
		return ret;
	}

 	dev_vdbg(rdev_get_dev(rdev),
		"%s-get_voltage (bank, reg, mask, shift, value): "
		"0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->voltage_bank,
		info->voltage_reg, info->voltage_mask,
		info->voltage_shift, regval);

	/* vintcore has a different layout */
	val = regval & info->voltage_mask;
	ret = info->voltages[val >> info->voltage_shift];

	return ret;
}

static int ab8500_get_best_voltage_index(struct regulator_dev *rdev,
		int min_uV, int max_uV)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	int i;

	/* check the supported voltage */
	for (i = 0; i < info->voltages_len; i++) {
		if ((info->voltages[i] >= min_uV) &&
		    (info->voltages[i] <= max_uV))
			return i;
	}

	return -EINVAL;
}

static int ab8500_regulator_set_voltage(struct regulator_dev *rdev,
					int min_uV, int max_uV,
					unsigned *selector)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	/* get the appropriate voltages within the range */
	ret = ab8500_get_best_voltage_index(rdev, min_uV, max_uV);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
				"couldn't get best voltage for regulator\n");
		return ret;
	}

	*selector = ret;

	/* vintcore register has a different layout */
	regval = ((u8)ret) << info->voltage_shift;

	/* set the registers for the request */
	ret = abx500_mask_and_set_register_interruptible(info->dev,
			info->voltage_bank, info->voltage_reg,
			info->voltage_mask, regval);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
		"couldn't set voltage reg for regulator\n");

	dev_vdbg(rdev_get_dev(rdev),
		"%s-set_voltage (bank, reg, mask, shift, value): "
		" 0x%x, 0x%x, 0x%x, 0x%x 0x%x\n",
		info->desc.name, info->voltage_bank, info->voltage_reg,
		info->voltage_mask, info->voltage_shift, regval);

	return ret;
}

static int ab8500_regulator_enable_time(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	return info->delay;
}

static int ab8500_regulator_set_voltage_time_sel(struct regulator_dev *rdev,
					     unsigned int old_sel,
					     unsigned int new_sel)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	/* If the regulator isn't on, it won't take time here */
	ret = ab8500_regulator_is_enabled(rdev);
	if (ret < 0)
		return ret;
	if (!ret)
		return 0;
	return info->delay;
}

static struct regulator_ops ab8500_regulator_volt_mode_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode	= ab8500_regulator_set_mode,
	.get_mode	= ab8500_regulator_get_mode,
	.is_enabled	= ab8500_regulator_is_enabled,
	.get_voltage	= ab8500_regulator_get_voltage,
	.set_voltage	= ab8500_regulator_set_voltage,
	.list_voltage	= ab8500_list_voltage,
	.enable_time	= ab8500_regulator_enable_time,
	.set_voltage_time_sel = ab8500_regulator_set_voltage_time_sel,
};

static struct regulator_ops ab8500_regulator_volt_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.is_enabled	= ab8500_regulator_is_enabled,
	.get_voltage	= ab8500_regulator_get_voltage,
	.set_voltage	= ab8500_regulator_set_voltage,
	.list_voltage	= ab8500_list_voltage,
	.enable_time	= ab8500_regulator_enable_time,
	.set_voltage_time_sel = ab8500_regulator_set_voltage_time_sel,
};

static int ab8500_fixed_get_voltage(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	return info->fixed_uV;
}

static struct regulator_ops ab8500_regulator_mode_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.is_enabled	= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.get_voltage	= ab8500_fixed_get_voltage,
	.list_voltage	= ab8500_list_voltage,
	.enable_time	= ab8500_regulator_enable_time,
	.set_voltage_time_sel = ab8500_regulator_set_voltage_time_sel,
};

static struct regulator_ops ab8500_regulator_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_voltage		= ab8500_fixed_get_voltage,
	.list_voltage		= ab8500_list_voltage,
};

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES

void ab8500_panic_regulator_volt_mode(struct regulator_ops *ops)
{
	ab8500_regulator_volt_mode_ops = *ops;
}

void ab8500_panic_regulator_mode(struct regulator_ops *ops)
{
	ab8500_regulator_mode_ops = *ops;
}

void ab8500_panic_regulator(struct regulator_ops *ops)
{
	ab8500_regulator_ops = *ops;
}

#endif

static int ab8500_sysclkreq_enable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = ab8500_gpio_config_select(info->dev, info->gpio_pin, false);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set sysclkreq pin selection\n");
		return ret;
	}

	info->is_enabled = true;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-enable (gpio_pin, gpio_select): %i, false\n",
		info->desc.name, info->gpio_pin);

	return ret;
}

static int ab8500_sysclkreq_disable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = ab8500_gpio_config_select(info->dev, info->gpio_pin, true);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set gpio pin selection\n");
		return ret;
	}

	info->is_enabled = false;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-disable (gpio_pin, gpio_select): %i, true\n",
		info->desc.name, info->gpio_pin);

	return ret;
}

static int ab8500_sysclkreq_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	bool gpio_select;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = ab8500_gpio_config_get_select(info->dev, info->gpio_pin,
		&gpio_select);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read gpio pin selection\n");
		return ret;
	}

	info->is_enabled = !gpio_select;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-is_enabled (gpio_pin, is_enabled): %i, %i\n",
		info->desc.name, info->gpio_pin, !gpio_select);

	return info->is_enabled;
}

static struct regulator_ops ab8500_sysclkreq_ops = {
	.enable		= ab8500_sysclkreq_enable,
	.disable	= ab8500_sysclkreq_disable,
	.is_enabled	= ab8500_sysclkreq_is_enabled,
	.get_voltage	= ab8500_fixed_get_voltage,
	.list_voltage	= ab8500_list_voltage,
};

/* AB8500 regulator information */
static struct ab8500_regulator_info
		ab8500_regulator_info[AB8500_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask, table, table length
	 */
	[AB8500_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8500_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8500_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux3_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux3_voltages),
	},
	[AB8500_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltage_shift		= 3,
		.voltages		= ldo_vintcore_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vintcore_voltages),
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8500_LDO_TVOUT] = {
		.desc = {
			.name		= "LDO-TVOUT",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_TVOUT,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.delay			= 500,
		.fixed_uV		= 2000000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8500_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2000000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB8500_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2050000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB8500_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2050000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB8500_LDO_DMIC] = {
		.desc = {
			.name		= "LDO-DMIC",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_DMIC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1800000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},

	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8500_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1200000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
	},

	/*
	 * SysClkReq regulators
	 */
	[AB8500_SYSCLKREQ_2] = {
		.desc = {
			.name		= "SYSCLKREQ-2",
			.ops		= &ab8500_sysclkreq_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_SYSCLKREQ_2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1, /* bogus value */
		.gpio_pin		= AB8500_PIN_GPIO(1),
	},
	[AB8500_SYSCLKREQ_4] = {
		.desc = {
			.name		= "SYSCLKREQ-4",
			.ops		= &ab8500_sysclkreq_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_SYSCLKREQ_4,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1, /* bogus value */
		.gpio_pin		= AB8500_PIN_GPIO(3),
	},
};

/* AB8505 regulator information */
static struct ab8500_regulator_info
		ab8505_regulator_info[AB8505_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask, table, table length
	 */
	[AB8505_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8505_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8505_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux3_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux3_voltages),
	},
	[AB8505_LDO_AUX4] = {
		.desc = {
			.name		= "LDO-AUX4",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX4,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		/* values for Vaux4Regu register */
		.update_bank		= 0x04,
		.update_reg		= 0x2e,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		/* values for Vaux4SEL register */
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x2f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8505_LDO_AUX5] = {
		.desc = {
			.name		= "LDO-AUX5",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX5,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux56_voltages),
		},
		.min_uV			= 1050000,
		.max_uV			= 2790000,
		.load_lp_uA		= 2000,
		/* values for CtrlVaux5 register */
		.update_bank		= 0x01,
		.update_reg		= 0x55,
		.update_mask		= 0x18,
		.update_val		= 0x10,
		.update_val_idle	= 0x18,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x55,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux56_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux56_voltages),
	},
	[AB8505_LDO_AUX6] = {
		.desc = {
			.name		= "LDO-AUX6",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX6,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux56_voltages),
		},
		.min_uV			= 1050000,
		.max_uV			= 2790000,
		.load_lp_uA		= 2000,
		/* values for CtrlVaux6 register */
		.update_bank		= 0x01,
		.update_reg		= 0x56,
		.update_mask		= 0x18,
		.update_val		= 0x10,
		.update_val_idle	= 0x18,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x56,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux56_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux56_voltages),
	},
	[AB8505_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltage_shift		= 3,
		.voltages		= ldo_vintcore_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vintcore_voltages),
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8505_LDO_ADC] = {
		.desc = {
			.name		= "LDO-ADC",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ADC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.delay			= 10000,
		.fixed_uV		= 2000000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8505_LDO_USB] = {
		.desc = {
			.name           = "LDO-USB",
			.ops            = &ab8500_regulator_ops,
			.type           = REGULATOR_VOLTAGE,
			.id             = AB8505_LDO_USB,
			.owner          = THIS_MODULE,
			.n_voltages     = 1,
		},
		.fixed_uV               = 3300000,
		.update_bank            = 0x03,
		.update_reg             = 0x82,
		.update_mask            = 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
	},
	[AB8505_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_volt_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaudio_voltages),
		},
		.min_uV			= 2000000,
		.max_uV			= 2600000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x57,
		.voltage_mask		= 0x7,
		.voltage_shift		= 4,
		.voltages		= ldo_vaudio_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaudio_voltages),
	},
	[AB8505_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2050000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB8505_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2050000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB8505_LDO_AUX8] = {
		.desc = {
			.name		= "LDO-AUX8",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX8,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1800000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},
	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8505_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vana_voltages),
		},
		.min_uV			= 1050000,
		.max_uV			= 1225000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x29,
		.voltage_mask		= 0x7,
		.voltages		= ldo_vana_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vana_voltages),
	},

	/*
	 * SysClkReq regulators
	 */
	[AB8505_SYSCLKREQ_2] = {
		.desc = {
			.name		= "SYSCLKREQ-2",
			.ops		= &ab8500_sysclkreq_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_SYSCLKREQ_2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1, /* bogus value */
		.gpio_pin		= AB8500_PIN_GPIO(1),
	},
	[AB8505_SYSCLKREQ_4] = {
		.desc = {
			.name		= "SYSCLKREQ-4",
			.ops		= &ab8500_sysclkreq_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_SYSCLKREQ_4,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1, /* bogus value */
		.gpio_pin		= AB8500_PIN_GPIO(3),
	},

};

/* AB9540 regulator information */
static struct ab8500_regulator_info
		ab9540_regulator_info[AB9540_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask, table, table length
	 */
	[AB9540_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB9540_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB9540_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux3_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux3_voltages),
	},
	[AB9540_LDO_AUX4] = {
		.desc = {
			.name		= "LDO-AUX4",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX4,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		/* values for Vaux4Regu register */
		.update_bank		= 0x04,
		.update_reg		= 0x2e,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		/* values for Vaux4SEL register */
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x2f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB9540_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
		},
		.min_uV			= 1100000,
		.max_uV			= 3300000,
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltage_shift		= 3,
		.voltages		= ldo_vintcore_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vintcore_voltages),
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB9540_LDO_TVOUT] = {
		.desc = {
			.name		= "LDO-TVOUT",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_TVOUT,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.delay			= 10000,
		.fixed_uV		= 2000000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB9540_LDO_USB] = {
		.desc = {
			.name           = "LDO-USB",
			.ops            = &ab8500_regulator_ops,
			.type           = REGULATOR_VOLTAGE,
			.id             = AB9540_LDO_USB,
			.owner          = THIS_MODULE,
			.n_voltages     = 1,
		},
		.fixed_uV               = 3300000,
		.update_bank            = 0x03,
		.update_reg             = 0x82,
		.update_mask            = 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
	},
	[AB9540_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2000000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB9540_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2050000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB9540_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 2050000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB9540_LDO_DMIC] = {
		.desc = {
			.name		= "LDO-DMIC",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_DMIC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1800000,
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},

	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB9540_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1200000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
	},

	/*
	 * SysClkReq regulators
	 */
	[AB9540_SYSCLKREQ_2] = {
		.desc = {
			.name		= "SYSCLKREQ-2",
			.ops		= &ab8500_sysclkreq_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_SYSCLKREQ_2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1, /* bogus value */
		.gpio_pin		= AB8500_PIN_GPIO(1),
	},
	[AB9540_SYSCLKREQ_4] = {
		.desc = {
			.name		= "SYSCLKREQ-4",
			.ops		= &ab8500_sysclkreq_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_SYSCLKREQ_4,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.fixed_uV		= 1, /* bogus value */
		.gpio_pin		= AB8500_PIN_GPIO(3),
	},
};

struct ab8500_reg_init {
	u8 bank;
	u8 addr;
	u8 mask;
};

#define REG_INIT(_id, _bank, _addr, _mask)	\
	[_id] = {				\
		.bank = _bank,			\
		.addr = _addr,			\
		.mask = _mask,			\
	}

/* AB8500 register init */
static struct ab8500_reg_init ab8500_reg_init[] = {
	/*
	 * 0x30, VanaRequestCtrl
	 * 0xc0, VextSupply1RequestCtrl
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL2,	0x03, 0x04, 0xf0),
	/*
	 * 0x03, VextSupply2RequestCtrl
	 * 0x0c, VextSupply3RequestCtrl
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL3,	0x03, 0x05, 0xff),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8500_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xe8),
	/*
	 * 0x10, VextSupply1SysClkReq1HPValid
	 * 0x20, VextSupply2SysClkReq1HPValid
	 * 0x40, VextSupply3SysClkReq1HPValid
	 */
	REG_INIT(AB8500_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x70),
	/*
	 * 0x08, VanaHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xe8),
	/*
	 * 0x01, VextSupply1HwHPReq1Valid
	 * 0x02, VextSupply2HwHPReq1Valid
	 * 0x04, VextSupply3HwHPReq1Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x07),
	/*
	 * 0x08, VanaHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xe8),
	/*
	 * 0x01, VextSupply1HwHPReq2Valid
	 * 0x02, VextSupply2HwHPReq2Valid
	 * 0x04, VextSupply3HwHPReq2Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x07),
	/*
	 * 0x20, VanaSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8500_REGUSWHPREQVALID1,	0x03, 0x0d, 0xa0),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x04, VextSupply1SwHPReqValid
	 * 0x08, VextSupply2SwHPReqValid
	 * 0x10, VextSupply3SwHPReqValid
	 */
	REG_INIT(AB8500_REGUSWHPREQVALID2,	0x03, 0x0e, 0x1f),
	/*
	 * 0x02, SysClkReq2Valid1
	 * 0x04, SysClkReq3Valid1
	 * 0x08, SysClkReq4Valid1
	 * 0x10, SysClkReq5Valid1
	 * 0x20, SysClkReq6Valid1
	 * 0x40, SysClkReq7Valid1
	 * 0x80, SysClkReq8Valid1
	 */
	REG_INIT(AB8500_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0xfe),
	/*
	 * 0x02, SysClkReq2Valid2
	 * 0x04, SysClkReq3Valid2
	 * 0x08, SysClkReq4Valid2
	 * 0x10, SysClkReq5Valid2
	 * 0x20, SysClkReq6Valid2
	 * 0x40, SysClkReq7Valid2
	 * 0x80, SysClkReq8Valid2
	 */
	REG_INIT(AB8500_REGUSYSCLKREQVALID2,	0x03, 0x10, 0xfe),
	/*
	 * 0x02, VTVoutEna
	 * 0x04, Vintcore12Ena
	 * 0x38, Vintcore12Sel
	 * 0x40, Vintcore12LP
	 * 0x80, VTVoutLP
	 */
	REG_INIT(AB8500_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB8500_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8500_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, VpllRegu (NOTE! PRCMU register bits)
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8500_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x01, VrefDDREna
	 * 0x02, VrefDDRSleepMode
	 */
	REG_INIT(AB8500_VREFDDR,		0x04, 0x07, 0x03),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8500_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8500_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x03, Vaux3Regu
	 */
	REG_INIT(AB8500_VRF1VAUX3REGU,		0x04, 0x0a, 0x03),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8500_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8500_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 */
	REG_INIT(AB8500_VRF1VAUX3SEL,		0x04, 0x21, 0x07),
	/*
	 * 0x01, VextSupply12LP
	 */
	REG_INIT(AB8500_REGUCTRL2SPARE,		0x04, 0x22, 0x01),
	/*
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8500_REGUCTRLDISCH,		0x04, 0x43, 0xfc),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8500_REGUCTRLDISCH2,		0x04, 0x44, 0x16),
};

/* Possibility to add debug */
int __attribute__((weak)) ab8500_regulator_debug_init(
	struct platform_device *pdev)
{
	return 0;
}

int __attribute__((weak)) ab8500_regulator_debug_exit(
	struct platform_device *pdev)
{
	return 0;
}

/* AB8505 register init */
static struct ab8500_reg_init ab8505_reg_init[] = {
	/*
	 * 0x03, VarmRequestCtrl
	 * 0x0c, VsmpsCRequestCtrl
	 * 0x30, VsmpsARequestCtrl
	 * 0xc0, VsmpsBRequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL1,	0x03, 0x03, 0xff),
	/*
	 * 0x03, VsafeRequestCtrl
	 * 0x0c, VpllRequestCtrl
	 * 0x30, VanaRequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL2,	0x03, 0x04, 0x3f),
	/*
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL3,	0x03, 0x05, 0xf0),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x01, VsmpsASysClkReq1HPValid
	 * 0x02, VsmpsBSysClkReq1HPValid
	 * 0x04, VsafeSysClkReq1HPValid
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x10, VpllSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xff),
	/*
	 * 0x01, VsmpsCSysClkReq1HPValid
	 * 0x02, VarmSysClkReq1HPValid
	 * 0x04, VbbSysClkReq1HPValid
	 * 0x08, VsmpsMSysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x0f),
	/*
	 * 0x01, VsmpsAHwHPReq1Valid
	 * 0x02, VsmpsBHwHPReq1Valid
	 * 0x04, VsafeHwHPReq1Valid
	 * 0x08, VanaHwHPReq1Valid
	 * 0x10, VpllHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xff),
	/*
	 * 0x08, VsmpsMHwHPReq1Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x08),
	/*
	 * 0x01, VsmpsAHwHPReq2Valid
	 * 0x02, VsmpsBHwHPReq2Valid
	 * 0x04, VsafeHwHPReq2Valid
	 * 0x08, VanaHwHPReq2Valid
	 * 0x10, VpllHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xff),
	/*
	 * 0x08, VsmpsMHwHPReq2Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x08),
	/*
	 * 0x01, VsmpsCSwHPReqValid
	 * 0x02, VarmSwHPReqValid
	 * 0x04, VsmpsASwHPReqValid
	 * 0x08, VsmpsBSwHPReqValid
	 * 0x10, VsafeSwHPReqValid
	 * 0x20, VanaSwHPReqValid
	 * 0x40, VpllSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8505_REGUSWHPREQVALID1,	0x03, 0x0d, 0xff),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x20, VsmpsMSwHPReqValid
	 */
	REG_INIT(AB8505_REGUSWHPREQVALID2,	0x03, 0x0e, 0x23),
	/*
	 * 0x02, SysClkReq2Valid1
	 * 0x04, SysClkReq3Valid1
	 * 0x08, SysClkReq4Valid1
	 */
	REG_INIT(AB8505_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0x0e),
	/*
	 * 0x02, SysClkReq2Valid2
	 * 0x04, SysClkReq3Valid2
	 * 0x08, SysClkReq4Valid2
	 */
	REG_INIT(AB8505_REGUSYSCLKREQVALID2,	0x03, 0x10, 0x0e),
	/*
	 * 0x01, Vaux4SwHPReqValid
	 * 0x02, Vaux4HwHPReq2Valid
	 * 0x04, Vaux4HwHPReq1Valid
	 * 0x08, Vaux4SysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUVAUX4REQVALID,	0x03, 0x11, 0x0f),
	/*
	 * 0x02, VadcEna
	 * 0x04, VintCore12Ena
	 * 0x38, VintCore12Sel
	 * 0x40, VintCore12LP
	 * 0x80, VadcLP
	 */
	REG_INIT(AB8505_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB8505_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8505_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, VsmpsARegu
	 * 0x0c, VsmpsASelCtrl
	 * 0x10, VsmpsAAutoMode
	 * 0x20, VsmpsAPWMMode
	 */
	REG_INIT(AB8505_VSMPSAREGU,		0x04, 0x03, 0x3f),
	/*
	 * 0x03, VsmpsBRegu
	 * 0x0c, VsmpsBSelCtrl
	 * 0x10, VsmpsBAutoMode
	 * 0x20, VsmpsBPWMMode
	 */
	REG_INIT(AB8505_VSMPSBREGU,		0x04, 0x04, 0x3f),
	/*
	 * 0x03, VsafeRegu
	 * 0x0c, VsafeSelCtrl
	 * 0x10, VsafeAutoMode
	 * 0x20, VsafePWMMode
	 */
	REG_INIT(AB8505_VSAFEREGU,		0x04, 0x05, 0x3f),
	/*
	 * 0x03, VpllRegu (NOTE! PRCMU register bits)
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8505_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8505_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8505_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x0f, Vaux3Regu
	 */
	REG_INIT(AB8505_VRF1VAUX3REGU,		0x04, 0x0a, 0x0f),
	/*
	 * 0x3f, VsmpsASel1
	 */
	REG_INIT(AB8505_VSMPSASEL1,		0x04, 0x13, 0x3f),
	/*
	 * 0x3f, VsmpsASel2
	 */
	REG_INIT(AB8505_VSMPSASEL2,		0x04, 0x14, 0x3f),
	/*
	 * 0x3f, VsmpsASel3
	 */
	REG_INIT(AB8505_VSMPSASEL3,		0x04, 0x15, 0x3f),
	/*
	 * 0x3f, VsmpsBSel1
	 */
	REG_INIT(AB8505_VSMPSBSEL1,		0x04, 0x17, 0x3f),
	/*
	 * 0x3f, VsmpsBSel2
	 */
	REG_INIT(AB8505_VSMPSBSEL2,		0x04, 0x18, 0x3f),
	/*
	 * 0x3f, VsmpsBSel3
	 */
	REG_INIT(AB8505_VSMPSBSEL3,		0x04, 0x19, 0x3f),
	/*
	 * 0x7f, VsafeSel1
	 */
	REG_INIT(AB8505_VSAFESEL1,		0x04, 0x1b, 0x7f),
	/*
	 * 0x3f, VsafeSel2
	 */
	REG_INIT(AB8505_VSAFESEL2,		0x04, 0x1c, 0x7f),
	/*
	 * 0x3f, VsafeSel3
	 */
	REG_INIT(AB8505_VSAFESEL3,		0x04, 0x1d, 0x7f),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8505_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8505_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 * 0x30, VRF1Sel
	 */
	REG_INIT(AB8505_VRF1VAUX3SEL,		0x04, 0x21, 0x37),
	/*
	 * 0x03, Vaux4RequestCtrl
	 */
	REG_INIT(AB8505_VAUX4REQCTRL,		0x04, 0x2d, 0x03),
	/*
	 * 0x03, Vaux4Regu
	 */
	REG_INIT(AB8505_VAUX4REGU,		0x04, 0x2e, 0x03),
	/*
	 * 0x0f, Vaux4Sel
	 */
	REG_INIT(AB8505_VAUX4SEL,		0x04, 0x2f, 0x0f),
	/*
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH,		0x04, 0x43, 0xfc),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH2,		0x04, 0x44, 0x16),
	/*
	 * 0x01, Vaux4Disch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH3,		0x04, 0x48, 0x01),
	/*
	 * 0x07, Vaux5Sel
	 * 0x08, Vaux5LP
	 * 0x10, Vaux5Ena
	 * 0x20, Vaux5Disch
	 * 0x40, Vaux5DisSfst
	 * 0x80, Vaux5DisPulld
	 */
	REG_INIT(AB8505_CTRLVAUX5,		0x01, 0x55, 0xff),
	/*
	 * 0x07, Vaux6Sel
	 * 0x08, Vaux6LP
	 * 0x10, Vaux6Ena
	 * 0x80, Vaux6DisPulld
	 */
	REG_INIT(AB8505_CTRLVAUX6,		0x01, 0x56, 0x9f),
};

/* AB9540 register init */
static struct ab8500_reg_init ab9540_reg_init[] = {
	/*
	 * 0x03, VarmRequestCtrl
	 * 0x0c, VapeRequestCtrl
	 * 0x30, Vsmps1RequestCtrl
	 * 0xc0, Vsmps2RequestCtrl
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL1,	0x03, 0x03, 0xff),
	/*
	 * 0x03, Vsmps3RequestCtrl
	 * 0x0c, VpllRequestCtrl
	 * 0x30, VanaRequestCtrl
	 * 0xc0, VextSupply1RequestCtrl
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL2,	0x03, 0x04, 0xff),
	/*
	 * 0x03, VextSupply2RequestCtrl
	 * 0x0c, VextSupply3RequestCtrl
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL3,	0x03, 0x05, 0xff),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x01, Vsmps1SysClkReq1HPValid
	 * 0x02, Vsmps2SysClkReq1HPValid
	 * 0x04, Vsmps3SysClkReq1HPValid
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x10, VpllSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB9540_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xff),
	/*
	 * 0x01, VapeSysClkReq1HPValid
	 * 0x02, VarmSysClkReq1HPValid
	 * 0x04, VbbSysClkReq1HPValid
	 * 0x08, VmodSysClkReq1HPValid
	 * 0x10, VextSupply1SysClkReq1HPValid
	 * 0x20, VextSupply2SysClkReq1HPValid
	 * 0x40, VextSupply3SysClkReq1HPValid
	 */
	REG_INIT(AB9540_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x7f),
	/*
	 * 0x01, Vsmps1HwHPReq1Valid
	 * 0x02, Vsmps2HwHPReq1Valid
	 * 0x04, Vsmps3HwHPReq1Valid
	 * 0x08, VanaHwHPReq1Valid
	 * 0x10, VpllHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xff),
	/*
	 * 0x01, VextSupply1HwHPReq1Valid
	 * 0x02, VextSupply2HwHPReq1Valid
	 * 0x04, VextSupply3HwHPReq1Valid
	 * 0x08, VmodHwHPReq1Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x0f),
	/*
	 * 0x01, Vsmps1HwHPReq2Valid
	 * 0x02, Vsmps2HwHPReq2Valid
	 * 0x03, Vsmps3HwHPReq2Valid
	 * 0x08, VanaHwHPReq2Valid
	 * 0x10, VpllHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xff),
	/*
	 * 0x01, VextSupply1HwHPReq2Valid
	 * 0x02, VextSupply2HwHPReq2Valid
	 * 0x04, VextSupply3HwHPReq2Valid
	 * 0x08, VmodHwHPReq2Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x0f),
	/*
	 * 0x01, VapeSwHPReqValid
	 * 0x02, VarmSwHPReqValid
	 * 0x04, Vsmps1SwHPReqValid
	 * 0x08, Vsmps2SwHPReqValid
	 * 0x10, Vsmps3SwHPReqValid
	 * 0x20, VanaSwHPReqValid
	 * 0x40, VpllSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB9540_REGUSWHPREQVALID1,	0x03, 0x0d, 0xff),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x04, VextSupply1SwHPReqValid
	 * 0x08, VextSupply2SwHPReqValid
	 * 0x10, VextSupply3SwHPReqValid
	 * 0x20, VmodSwHPReqValid
	 */
	REG_INIT(AB9540_REGUSWHPREQVALID2,	0x03, 0x0e, 0x3f),
	/*
	 * 0x02, SysClkReq2Valid1
	 * ...
	 * 0x80, SysClkReq8Valid1
	 */
	REG_INIT(AB9540_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0xfe),
	/*
	 * 0x02, SysClkReq2Valid2
	 * ...
	 * 0x80, SysClkReq8Valid2
	 */
	REG_INIT(AB9540_REGUSYSCLKREQVALID2,	0x03, 0x10, 0xfe),
	/*
	 * 0x01, Vaux4SwHPReqValid
	 * 0x02, Vaux4HwHPReq2Valid
	 * 0x04, Vaux4HwHPReq1Valid
	 * 0x08, Vaux4SysClkReq1HPValid
	 */
	REG_INIT(AB9540_REGUVAUX4REQVALID,	0x03, 0x11, 0x0f),
	/*
	 * 0x02, VTVoutEna
	 * 0x04, Vintcore12Ena
	 * 0x38, Vintcore12Sel
	 * 0x40, Vintcore12LP
	 * 0x80, VTVoutLP
	 */
	REG_INIT(AB9540_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB9540_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB9540_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, Vsmps1Regu
	 * 0x0c, Vsmps1SelCtrl
	 * 0x10, Vsmps1AutoMode
	 * 0x20, Vsmps1PWMMode
	 */
	REG_INIT(AB9540_VSMPS1REGU,		0x04, 0x03, 0x3f),
	/*
	 * 0x03, Vsmps2Regu
	 * 0x0c, Vsmps2SelCtrl
	 * 0x10, Vsmps2AutoMode
	 * 0x20, Vsmps2PWMMode
	 */
	REG_INIT(AB9540_VSMPS2REGU,		0x04, 0x04, 0x3f),
	/*
	 * 0x03, Vsmps3Regu
	 * 0x0c, Vsmps3SelCtrl
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB9540_VSMPS3REGU,		0x04, 0x05, 0x0f),
	/*
	 * 0x03, VpllRegu
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB9540_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB9540_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB9540_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x0c, Vrf1Regu
	 * 0x03, Vaux3Regu
	 */
	REG_INIT(AB9540_VRF1VAUX3REGU,		0x04, 0x0a, 0x0f),
	/*
	 * 0x3f, Vsmps1Sel1
	 */
	REG_INIT(AB9540_VSMPS1SEL1,		0x04, 0x13, 0x3f),
	/*
	 * 0x3f, Vsmps1Sel2
	 */
	REG_INIT(AB9540_VSMPS1SEL2,		0x04, 0x14, 0x3f),
	/*
	 * 0x3f, Vsmps1Sel3
	 */
	REG_INIT(AB9540_VSMPS1SEL3,		0x04, 0x15, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel1
	 */
	REG_INIT(AB9540_VSMPS2SEL1,		0x04, 0x17, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel2
	 */
	REG_INIT(AB9540_VSMPS2SEL2,		0x04, 0x18, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel3
	 */
	REG_INIT(AB9540_VSMPS2SEL3,		0x04, 0x19, 0x3f),
	/*
	 * 0x7f, Vsmps3Sel1
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB9540_VSMPS3SEL1,             0x04, 0x1b, 0x7f),
	/*
	 * 0x7f, Vsmps3Sel2
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB9540_VSMPS3SEL2,             0x04, 0x1c, 0x7f),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB9540_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB9540_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 * 0x30, Vrf1Sel
	 */
	REG_INIT(AB9540_VRF1VAUX3SEL,		0x04, 0x21, 0x37),
	/*
	 * 0x01, VextSupply12LP
	 */
	REG_INIT(AB9540_REGUCTRL2SPARE,		0x04, 0x22, 0x01),
	/*
	 * 0x03, Vaux4RequestCtrl
	 */
	REG_INIT(AB9540_VAUX4REQCTRL,		0x04, 0x2d, 0x03),
	/*
	 * 0x03, Vaux4Regu
	 */
	REG_INIT(AB9540_VAUX4REGU,		0x04, 0x2e, 0x03),
	/*
	 * 0x08, Vaux4Sel
	 */
	REG_INIT(AB9540_VAUX4SEL,		0x04, 0x2f, 0x0f),
	/*
	 * 0x00, CtrlVaux5
	 */
	REG_INIT(AB9540_VAUX5CTRL,		0x01, 0x55, 0xff),
	/*
	 * 0x00, CtrlVaux6
	 */
	REG_INIT(AB9540_VAUX6CTRL,		0x01, 0x56, 0xff),
	/*
	 * 0x01, VpllDisch
	 * 0x02, Vrf1Disch
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB9540_REGUCTRLDISCH,		0x04, 0x43, 0xff),
	/*
	 * 0x01, VsimDisch
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x08, VpllPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB9540_REGUCTRLDISCH2,		0x04, 0x44, 0x1f),
	/*
	 * 0x01, Vaux4Disch
	 */
	REG_INIT(AB9540_REGUCTRLDISCH3,		0x04, 0x48, 0x01),
};

static __devinit int ab8500_regulator_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_platform_data *ppdata;
	struct ab8500_regulator_platform_data *pdata;
	int i, err;
	struct ab8500_regulator_info *regulator_info;
	int regulator_info_size;
	struct ab8500_reg_init *reg_init;
	int reg_init_size;
	/* cache values needed repeatedly inside for-loops */

	if (!ab8500) {
		dev_err(&pdev->dev, "null mfd parent\n");
		return -EINVAL;
	}

	ppdata = dev_get_platdata(ab8500->dev);
	if (!ppdata) {
		dev_err(&pdev->dev, "null parent pdata\n");
		return -EINVAL;
	}

	pdata = ppdata->regulator;
	if (!pdata) {
		dev_err(&pdev->dev, "null pdata\n");
		return -EINVAL;
	}

	if (is_ab9540(ab8500)) {
		regulator_info = ab9540_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab9540_regulator_info);
		reg_init = ab9540_reg_init;
		reg_init_size = AB9540_NUM_REGULATOR_REGISTERS;
	} else if (is_ab8505(ab8500)) {
		regulator_info = ab8505_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8505_regulator_info);
		reg_init = ab8505_reg_init;
		reg_init_size = AB8505_NUM_REGULATOR_REGISTERS;
	} else {
		regulator_info = ab8500_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8500_regulator_info);
		reg_init = ab8500_reg_init;
		reg_init_size = AB8500_NUM_REGULATOR_REGISTERS;
	}

	/* make sure the platform data has the correct size */
	if (pdata->num_regulator != regulator_info_size) {
		dev_err(&pdev->dev, "Configuration error: size mismatch.\n");
		return -EINVAL;
	}

	/* initialize debug (initial state is recorded with this call) */
	err = ab8500_regulator_debug_init(pdev);
	if (err)
		return err;

	/* initialize registers */
	for (i = 0; i < pdata->num_reg_init; i++) {
		int id;
		u8 mask, value;

		id = pdata->reg_init[i].id;
		mask = pdata->reg_init[i].mask;
		value = pdata->reg_init[i].value;

		/* check for configuration errors */
		BUG_ON(id >= reg_init_size);
		BUG_ON(value & ~mask);
		BUG_ON(mask & ~reg_init[id].mask);

		/* initialize register */
		err = abx500_mask_and_set_register_interruptible(&pdev->dev,
			reg_init[id].bank,
			reg_init[id].addr,
			mask, value);
		if (err < 0) {
			dev_err(&pdev->dev,
				"Failed to initialize 0x%02x, 0x%02x.\n",
				reg_init[id].bank,
				reg_init[id].addr);
			return err;
		}
		dev_vdbg(&pdev->dev,
			"  init: 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
			reg_init[id].bank,
			reg_init[id].addr,
			mask, value);
	}

	/*
	 * This changes the default setting for VextSupply3Regu to low power.
	 * Active high or low is depending on OTP which is changed from ab8500v3.0.
	 * Remove this when ab8500v2.0 is no longer important.
	 * This only affects power consumption and it depends on the
	 * HREF OTP configurations.
	 */
	if (is_ab8500_2p0_or_earlier(ab8500)) {
		err = abx500_mask_and_set_register_interruptible(&pdev->dev,
			AB8500_REGU_CTRL2, 0x08, 0x30, 0x30);
		if (err < 0) {
			dev_err(&pdev->dev,
				"Failed to override 0x%02x, 0x%02x.\n",
				AB8500_REGU_CTRL2, 0x08);
			return err;
		}
	}

	if (!is_ab8505(ab8500)) {
		/* register external regulators (before Vaux1, 2 and 3) */
		err = ab8500_ext_regulator_init(pdev);
		if (err)
			return err;
	}

	/* register all regulators */
	for (i = 0; i < regulator_info_size; i++) {
		struct ab8500_regulator_info *info = NULL;

		/* assign per-regulator data */
		info = &regulator_info[i];
		info->dev = &pdev->dev;

		/* fix for hardware before ab8500v2.0 */
		if (is_ab8500_1p1_or_earlier(ab8500)) {
			if (info->desc.id == AB8500_LDO_AUX3) {
				info->desc.n_voltages =
					ARRAY_SIZE(ldo_vauxn_voltages);
				info->voltages = ldo_vauxn_voltages;
				info->voltages_len =
					ARRAY_SIZE(ldo_vauxn_voltages);
				info->voltage_mask = 0xf;
			}
		}

		/* register regulator with framework */
		info->regulator = regulator_register(&info->desc, &pdev->dev,
				&pdata->regulator[i], info);
		if (IS_ERR(info->regulator)) {
			err = PTR_ERR(info->regulator);
			dev_err(&pdev->dev, "failed to register regulator %s\n",
					info->desc.name);
			/* when we fail, un-register all earlier regulators */
			while (--i >= 0) {
				info = &regulator_info[i];
				regulator_unregister(info->regulator);
			}
			return err;
		}

		dev_vdbg(rdev_get_dev(info->regulator),
			"%s-probed\n", info->desc.name);
	}

	return 0;
}

static __devexit int ab8500_regulator_remove(struct platform_device *pdev)
{
	int i, err;
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_regulator_info *regulator_info;
	int regulator_info_size;


	if (is_ab9540(ab8500)) {
		regulator_info = ab9540_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab9540_regulator_info);
	} else if (is_ab8505(ab8500)) {
		regulator_info = ab8505_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8505_regulator_info);
	} else {
		regulator_info = ab8500_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8500_regulator_info);
	}

	for (i = 0; i < regulator_info_size; i++) {
		struct ab8500_regulator_info *info = NULL;
		info = &regulator_info[i];

		dev_vdbg(rdev_get_dev(info->regulator),
			"%s-remove\n", info->desc.name);

		regulator_unregister(info->regulator);
	}

	if (!is_ab8505(ab8500)) {
		/* remove external regulators (after Vaux1, 2 and 3) */
		err = ab8500_ext_regulator_exit(pdev);
		if (err)
			return err;
	}

	/* remove regulator debug */
	err = ab8500_regulator_debug_exit(pdev);
	if (err)
		return err;

	return 0;
}

static struct platform_driver ab8500_regulator_driver = {
	.probe = ab8500_regulator_probe,
	.remove = __devexit_p(ab8500_regulator_remove),
	.driver         = {
		.name   = "ab8500-regulator",
		.owner  = THIS_MODULE,
	},
};

static int __init ab8500_regulator_init(void)
{
	int ret;

	ret = platform_driver_register(&ab8500_regulator_driver);
	if (ret != 0)
		pr_err("Failed to register ab8500 regulator: %d\n", ret);

	return ret;
}
subsys_initcall(ab8500_regulator_init);

static void __exit ab8500_regulator_exit(void)
{
	platform_driver_unregister(&ab8500_regulator_driver);
}
module_exit(ab8500_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_AUTHOR("Bengt Jonsson <bengt.g.jonsson@stericsson.com>");
MODULE_AUTHOR("Daniel Willerud <daniel.willerud@stericsson.com>");
MODULE_DESCRIPTION("Regulator Driver for ST-Ericsson AB8500 Mixed-Sig PMIC");
MODULE_ALIAS("platform:ab8500-regulator");
