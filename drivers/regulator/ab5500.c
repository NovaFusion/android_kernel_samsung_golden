/*
 * Copyright (C) 2011 ST-Ericsson SA
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Based on ab3100.c.
 *
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/regulator/ab5500.h>

#define AB5500_LDO_VDIGMIC_ST	0x50

#define AB5500_LDO_G_ST		0x78
#define AB5500_LDO_G_PWR1	0x79
#define AB5500_LDO_G_PWR0	0x7a

#define AB5500_LDO_H_ST		0x7b
#define AB5500_LDO_H_PWR1	0x7c
#define AB5500_LDO_H_PWR0	0x7d

#define AB5500_LDO_K_ST		0x7e
#define AB5500_LDO_K_PWR1	0x7f
#define AB5500_LDO_K_PWR0	0x80

#define AB5500_LDO_L_ST		0x81
#define AB5500_LDO_L_PWR1	0x82
#define AB5500_LDO_L_PWR0	0x83

/* In SIM bank */
#define AB5500_SIM_SUP		0x14

#define AB5500_MBIAS1		0x00
#define AB5500_MBIAS2		0x01

#define AB5500_LDO_MODE_MASK		(0x3 << 4)
#define AB5500_LDO_MODE_FULLPOWER	(0x3 << 4)
#define AB5500_LDO_MODE_PWRCTRL		(0x2 << 4)
#define AB5500_LDO_MODE_LOWPOWER	(0x1 << 4)
#define AB5500_LDO_MODE_OFF		(0x0 << 4)
#define AB5500_LDO_VOLT_MASK		0x07

#define AB5500_MBIAS1_ENABLE		(0x1 << 1)
#define AB5500_MBIAS1_MODE_MASK		(0x1 << 1)
#define AB5500_MBIAS2_ENABLE		(0x1 << 1)
#define AB5500_MBIAS2_VOLT_MASK		(0x1 << 2)
#define AB5500_MBIAS2_MODE_MASK		(0x1 << 1)

struct ab5500_regulator {
	struct regulator_desc desc;
	const int *voltages;
	int num_holes;
	bool off_is_lowpower;
	bool enabled;
	int enable_time;
	int load_lp_uA;
	u8 bank;
	u8 reg;
	u8 mode;
	u8 update_mask;
	u8 update_val_idle;
	u8 update_val_normal;
	u8 voltage_mask;
};

struct ab5500_regulators {
	struct device *dev;
	struct ab5500_regulator *regulator[AB5500_NUM_REGULATORS];
	struct regulator_dev *rdev[AB5500_NUM_REGULATORS];
};

static int ab5500_regulator_enable_time(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];

	return r->enable_time; /* microseconds */
}

static int ab5500_regulator_enable(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	int ret;

	ret = abx500_mask_and_set(ab5500->dev, r->bank, r->reg,
				  r->update_mask, r->mode);
	if (ret < 0)
		return ret;

	r->enabled = true;

	return 0;
}

static int ab5500_regulator_disable(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	u8 regval;
	int ret;

	if (r->off_is_lowpower)
		regval = AB5500_LDO_MODE_LOWPOWER;
	else
		regval = AB5500_LDO_MODE_OFF;

	ret = abx500_mask_and_set(ab5500->dev, r->bank, r->reg,
				  r->update_mask, regval);
	if (ret < 0)
		return ret;

	r->enabled = false;

	return 0;
}

static unsigned int ab5500_regulator_get_mode(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];

	if (r->mode == r->update_val_idle)
		return REGULATOR_MODE_IDLE;

	return REGULATOR_MODE_NORMAL;
}

static unsigned int
ab5500_regulator_get_optimum_mode(struct regulator_dev *rdev,
				  int input_uV, int output_uV, int load_uA)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	unsigned int mode;

	if (load_uA <= r->load_lp_uA)
		mode = REGULATOR_MODE_IDLE;
	else
		mode = REGULATOR_MODE_NORMAL;

	return mode;
}

static int ab5500_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		r->mode = r->update_val_normal;
		break;
	case REGULATOR_MODE_IDLE:
		r->mode = r->update_val_idle;
		break;
	default:
		return -EINVAL;
	}

	if (r->enabled)
		return ab5500_regulator_enable(rdev);

	return 0;
}

static int ab5500_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	u8 regval;
	int err;

	err = abx500_get_register_interruptible(ab5500->dev,
						r->bank, r->reg, &regval);
	if (err) {
		dev_err(rdev_get_dev(rdev), "unable to get register 0x%x\n",
			r->reg);
		return err;
	}

	switch (regval & r->update_mask) {
	case AB5500_LDO_MODE_PWRCTRL:
	case AB5500_LDO_MODE_OFF:
		r->enabled = false;
		break;
	case AB5500_LDO_MODE_LOWPOWER:
		if (r->off_is_lowpower) {
			r->enabled = false;
			break;
		}
		/* fall through */
	default:
		r->enabled = true;
		break;
	}

	return r->enabled;
}

static int
ab5500_regulator_list_voltage(struct regulator_dev *rdev, unsigned selector)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	unsigned n_voltages = r->desc.n_voltages;
	int selindex;
	int i;

	for (i = 0, selindex = 0; selindex < n_voltages; i++) {
		int voltage = r->voltages[i];

		if (!voltage)
			continue;

		if (selindex == selector)
			return voltage;

		selindex++;
	}

	return -EINVAL;
}

static int ab5500_regulator_fixed_get_voltage(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];

	return r->voltages[0];
}

static int ab5500_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	u8 regval;
	int ret;

	ret = abx500_get_register_interruptible(ab5500->dev,
						r->bank, r->reg, &regval);
	if (ret) {
		dev_warn(rdev_get_dev(rdev),
			"failed to get regulator value in register "
			"%02x\n", r->reg);
		return ret;
	}

	regval &= r->voltage_mask;
	if (regval >= r->desc.n_voltages + r->num_holes)
	       return -EINVAL;

	if (!r->voltages[regval])
		return -EINVAL;

	return r->voltages[regval];
}

static int ab5500_get_best_voltage_index(struct ab5500_regulator *r,
					 int min_uV, int max_uV)
{
	unsigned n_voltages = r->desc.n_voltages;
	int bestmatch = INT_MAX;
	int bestindex = -EINVAL;
	int selindex;
	int i;

	/*
	 * Locate the minimum voltage fitting the criteria on
	 * this regulator. The switchable voltages are not
	 * in strict falling order so we need to check them
	 * all for the best match.
	 */
	for (i = 0, selindex = 0; selindex < n_voltages; i++) {
		int voltage = r->voltages[i];

		if (!voltage)
			continue;

		if (voltage <= max_uV &&
		    voltage >= min_uV &&
		    voltage < bestmatch) {
			bestmatch = voltage;
			bestindex = i;
		}

		selindex++;
	}

	return bestindex;
}

static int ab5500_regulator_set_voltage(struct regulator_dev *rdev,
					int min_uV, int max_uV,
					unsigned *selector)
{
	struct ab5500_regulators *ab5500 = rdev_get_drvdata(rdev);
	struct ab5500_regulator *r = ab5500->regulator[rdev_get_id(rdev)];
	int bestindex;

	bestindex = ab5500_get_best_voltage_index(r, min_uV, max_uV);
	if (bestindex < 0) {
		dev_warn(rdev_get_dev(rdev),
			 "requested %d<=x<=%d uV, out of range!\n",
			 min_uV, max_uV);
		return bestindex;
	}

	*selector = bestindex;

	return abx500_mask_and_set_register_interruptible(ab5500->dev,
			r->bank, r->reg, r->voltage_mask, bestindex);

}

static struct regulator_ops ab5500_regulator_variable_ops = {
	.enable		= ab5500_regulator_enable,
	.disable	= ab5500_regulator_disable,
	.is_enabled	= ab5500_regulator_is_enabled,
	.enable_time	= ab5500_regulator_enable_time,
	.get_voltage	= ab5500_regulator_get_voltage,
	.set_voltage	= ab5500_regulator_set_voltage,
	.list_voltage	= ab5500_regulator_list_voltage,
	.set_mode	= ab5500_regulator_set_mode,
	.get_mode	= ab5500_regulator_get_mode,
	.get_optimum_mode = ab5500_regulator_get_optimum_mode,
};

static struct regulator_ops ab5500_regulator_fixed_ops = {
	.enable		= ab5500_regulator_enable,
	.disable	= ab5500_regulator_disable,
	.is_enabled	= ab5500_regulator_is_enabled,
	.enable_time	= ab5500_regulator_enable_time,
	.get_voltage	= ab5500_regulator_fixed_get_voltage,
	.list_voltage	= ab5500_regulator_list_voltage,
	.set_mode	= ab5500_regulator_set_mode,
	.get_mode	= ab5500_regulator_get_mode,
	.get_optimum_mode = ab5500_regulator_get_optimum_mode,
};

static const int ab5500_ldo_lg_voltages[] = {
	[0x00] = 1200000,
	[0x01] = 0, /* not used */
	[0x02] = 1500000,
	[0x03] = 1800000,
	[0x04] = 0, /* not used */
	[0x05] = 2500000,
	[0x06] = 2730000,
	[0x07] = 2910000,
};

static const int ab5500_ldo_kh_voltages[] = {
	[0x00] = 1200000,
	[0x01] = 1500000,
	[0x02] = 1800000,
	[0x03] = 2100000,
	[0x04] = 2500000,
	[0x05] = 2750000,
	[0x06] = 2790000,
	[0x07] = 2910000,
};

static const int ab5500_ldo_vdigmic_voltages[] = {
	[0x00] = 2100000,
};

static const int ab5500_ldo_sim_voltages[] = {
	[0x00] = 1875000,
	[0x01] = 2800000,
	[0x02] = 2900000,
};

static const int ab5500_bias2_voltages[] = {
	[0x00] = 2000000,
	[0x01] = 2200000,
};

static const int ab5500_bias1_voltages[] = {
	[0x00] = 2000000,
};

static struct ab5500_regulator ab5500_regulators[] = {
	[AB5500_LDO_L] = {
		.desc = {
			.name		= "LDO_L",
			.id		= AB5500_LDO_L,
			.ops		= &ab5500_regulator_variable_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ab5500_ldo_lg_voltages) -
				2,
		},
		.bank			= AB5500_BANK_STARTUP,
		.reg			= AB5500_LDO_L_ST,
		.voltages		= ab5500_ldo_lg_voltages,
		.num_holes		= 2, /* 2 register values unused */
		.enable_time		= 400,
		.load_lp_uA		= 20000,
		.mode			= AB5500_LDO_MODE_FULLPOWER,
		.update_mask		= AB5500_LDO_MODE_MASK,
		.update_val_normal	= AB5500_LDO_MODE_FULLPOWER,
		.update_val_idle	= AB5500_LDO_MODE_LOWPOWER,
		.voltage_mask		= AB5500_LDO_VOLT_MASK,
	},
	[AB5500_LDO_G] = {
		.desc = {
			.name		= "LDO_G",
			.id		= AB5500_LDO_G,
			.ops		= &ab5500_regulator_variable_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages 	= ARRAY_SIZE(ab5500_ldo_lg_voltages) -
				2,
		},
		.bank			= AB5500_BANK_STARTUP,
		.reg			= AB5500_LDO_G_ST,
		.voltages		= ab5500_ldo_lg_voltages,
		.num_holes		= 2, /* 2 register values unused */
		.enable_time		= 400,
		.load_lp_uA		= 20000,
		.mode			= AB5500_LDO_MODE_FULLPOWER,
		.update_mask		= AB5500_LDO_MODE_MASK,
		.update_val_normal	= AB5500_LDO_MODE_FULLPOWER,
		.update_val_idle	= AB5500_LDO_MODE_LOWPOWER,
		.voltage_mask		= AB5500_LDO_VOLT_MASK,
	},
	[AB5500_LDO_K] = {
		.desc = {
			.name		= "LDO_K",
			.id		= AB5500_LDO_K,
			.ops		= &ab5500_regulator_variable_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages 	= ARRAY_SIZE(ab5500_ldo_kh_voltages),
		},
		.bank			= AB5500_BANK_STARTUP,
		.reg			= AB5500_LDO_K_ST,
		.voltages		= ab5500_ldo_kh_voltages,
		.enable_time		= 400,
		.load_lp_uA		= 20000,
		.mode			= AB5500_LDO_MODE_FULLPOWER,
		.update_mask		= AB5500_LDO_MODE_MASK,
		.update_val_normal	= AB5500_LDO_MODE_FULLPOWER,
		.update_val_idle	= AB5500_LDO_MODE_LOWPOWER,
		.voltage_mask		= AB5500_LDO_VOLT_MASK,
	},
	[AB5500_LDO_H] = {
		.desc = {
			.name		= "LDO_H",
			.id		= AB5500_LDO_H,
			.ops		= &ab5500_regulator_variable_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ab5500_ldo_kh_voltages),
		},
		.bank			= AB5500_BANK_STARTUP,
		.reg			= AB5500_LDO_H_ST,
		.voltages		= ab5500_ldo_kh_voltages,
		.enable_time		= 400,
		.load_lp_uA		= 20000,
		.mode			= AB5500_LDO_MODE_FULLPOWER,
		.update_mask		= AB5500_LDO_MODE_MASK,
		.update_val_normal	= AB5500_LDO_MODE_FULLPOWER,
		.update_val_idle	= AB5500_LDO_MODE_LOWPOWER,
		.voltage_mask		= AB5500_LDO_VOLT_MASK,
	},
	[AB5500_LDO_VDIGMIC] = {
		.desc = {
			.name		= "LDO_VDIGMIC",
			.id		= AB5500_LDO_VDIGMIC,
			.ops		= &ab5500_regulator_fixed_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages	=
				ARRAY_SIZE(ab5500_ldo_vdigmic_voltages),
		},
		.bank			= AB5500_BANK_STARTUP,
		.reg			= AB5500_LDO_VDIGMIC_ST,
		.voltages		= ab5500_ldo_vdigmic_voltages,
		.enable_time		= 450,
		.mode			= AB5500_LDO_MODE_FULLPOWER,
		.update_mask		= AB5500_LDO_MODE_MASK,
		.update_val_normal	= AB5500_LDO_MODE_FULLPOWER,
		.update_val_idle	= AB5500_LDO_MODE_LOWPOWER,
		.voltage_mask		= AB5500_LDO_VOLT_MASK,
	},
	[AB5500_LDO_SIM] = {
		.desc = {
			.name		= "LDO_SIM",
			.id		= AB5500_LDO_SIM,
			.ops		= &ab5500_regulator_variable_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ab5500_ldo_sim_voltages),
		},
		.bank			= AB5500_BANK_SIM_USBSIM,
		.reg			= AB5500_SIM_SUP,
		.voltages		= ab5500_ldo_sim_voltages,
		.enable_time		= 1000,
		.mode			= AB5500_LDO_MODE_FULLPOWER,
		.update_mask		= AB5500_LDO_MODE_MASK,
		.update_val_normal	= AB5500_LDO_MODE_FULLPOWER,
		.update_val_idle	= AB5500_LDO_MODE_LOWPOWER,
		.voltage_mask		= AB5500_LDO_VOLT_MASK,
	},
	[AB5500_BIAS2] = {
		.desc = {
			.name		= "MBIAS2",
			.id		= AB5500_BIAS2,
			.ops		= &ab5500_regulator_variable_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ab5500_bias2_voltages),
		},
		.bank			= AB5500_BANK_AUDIO_HEADSETUSB,
		.reg			= AB5500_MBIAS2,
		.voltages		= ab5500_bias2_voltages,
		.enable_time		= 1000,
		.mode			= AB5500_MBIAS2_ENABLE,
		.update_mask		= AB5500_MBIAS2_MODE_MASK,
		.update_val_normal	= AB5500_MBIAS2_ENABLE,
		.update_val_idle	= AB5500_MBIAS2_ENABLE,
		.voltage_mask		= AB5500_MBIAS2_VOLT_MASK,
	},
	[AB5500_BIAS1] = {
		.desc = {
			.name		= "MBIAS1",
			.id		= AB5500_BIAS1,
			.ops		= &ab5500_regulator_fixed_ops,
			.type		= REGULATOR_VOLTAGE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ab5500_bias1_voltages),
		},
		.bank			= AB5500_BANK_AUDIO_HEADSETUSB,
		.reg			= AB5500_MBIAS1,
		.voltages		= ab5500_bias1_voltages,
		.enable_time		= 1000,
		.mode			= AB5500_MBIAS1_ENABLE,
		.update_mask		= AB5500_MBIAS1_MODE_MASK,
		.update_val_normal	= AB5500_MBIAS1_ENABLE,
		.update_val_idle	= AB5500_MBIAS1_ENABLE,
	},
};


static int __devinit ab5500_regulator_probe(struct platform_device *pdev)
{
	struct ab5500_platform_data *ppdata = pdev->dev.parent->platform_data;
	struct ab5500_regulator_platform_data *pdata = ppdata->regulator;
	struct ab5500_regulator_data *regdata;
	struct ab5500_regulators *ab5500;
	int err = 0;
	int i;

	if (!pdata || !pdata->regulator)
		return -EINVAL;

	ab5500 = kzalloc(sizeof(*ab5500), GFP_KERNEL);
	if (!ab5500)
		return -ENOMEM;

	ab5500->dev = &pdev->dev;
	regdata = pdata->data;

	platform_set_drvdata(pdev, ab5500);

	for (i = 0; i < AB5500_NUM_REGULATORS; i++) {
		struct ab5500_regulator *regulator = &ab5500_regulators[i];
		struct regulator_dev *rdev;

		if (regdata)
			regulator->off_is_lowpower = regdata[i].off_is_lowpower;

		ab5500->regulator[i] = regulator;

		rdev = regulator_register(&regulator->desc, &pdev->dev,
					  &pdata->regulator[i], ab5500);
		if (IS_ERR(rdev)) {
			err = PTR_ERR(rdev);
			dev_err(&pdev->dev, "failed to register regulator %s err %d\n",
				regulator->desc.name, err);
			goto err_unregister;
		}

		ab5500->rdev[i] = rdev;
	}

	return 0;

err_unregister:
	/* remove the already registered regulators */
	while (--i >= 0)
		regulator_unregister(ab5500->rdev[i]);

	platform_set_drvdata(pdev, NULL);
	kfree(ab5500);

	return err;
}

static int __devexit ab5500_regulators_remove(struct platform_device *pdev)
{
	struct ab5500_regulators *ab5500 = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < AB5500_NUM_REGULATORS; i++)
		regulator_unregister(ab5500->rdev[i]);

	platform_set_drvdata(pdev, NULL);
	kfree(ab5500);

	return 0;
}

static struct platform_driver ab5500_regulator_driver = {
	.driver = {
		.name  = "ab5500-regulator",
		.owner = THIS_MODULE,
	},
	.probe = ab5500_regulator_probe,
	.remove = __devexit_p(ab5500_regulators_remove),
};

static __init int ab5500_regulator_init(void)
{
	return platform_driver_register(&ab5500_regulator_driver);
}

static __exit void ab5500_regulator_exit(void)
{
	platform_driver_unregister(&ab5500_regulator_driver);
}

subsys_initcall(ab5500_regulator_init);
module_exit(ab5500_regulator_exit);

MODULE_DESCRIPTION("AB5500 Regulator Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ab5500-regulator");
