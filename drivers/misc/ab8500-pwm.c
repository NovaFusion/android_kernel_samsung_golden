/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/clk.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-pwmleds.h>

/*
 * PWM Out generators
 * Bank: 0x10
 */
#define AB8500_PWM_OUT_CTRL1_REG	0x60
#define AB8500_PWM_OUT_CTRL2_REG	0x61
#define AB8500_PWM_OUT_CTRL7_REG	0x66
#define AB8505_PWM_OUT_BLINK_CTRL1_REG  0x68
#define AB8505_PWM_OUT_BLINK_CTRL4_REG  0x6B
#define AB8505_PWM_OUT_BLINK_CTRL_DUTYBIT 4
#define AB8505_PWM_OUT_BLINK_DUTYMASK (0x0F << AB8505_PWM_OUT_BLINK_CTRL_DUTYBIT)


/* backlight driver constants */
#define ENABLE_PWM			1
#define DISABLE_PWM			0

struct pwm_device {
	struct device *dev;
	struct list_head node;
	struct clk *clk;
	const char *label;
	unsigned int pwm_id;
	unsigned int num_pwm;
	unsigned int blink_en;
	struct ab8500 *parent;
	bool clk_enabled;
};

static LIST_HEAD(pwm_list);

int pwm_config_blink(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	int ret;
	unsigned int value;
	u8 reg;
	if ((!is_ab8505(pwm->parent)) || (!pwm->blink_en)) {
		dev_err(pwm->dev, "setting blinking for this "
					"device not supported\n");
		return -EINVAL;
	}
	/*
	 * get the period value that is to be written to
	 * AB8500_PWM_OUT_BLINK_CTRL1 REGS[0:2]
	 */
	value = period_ns & 0x07;
	/*
	 * get blink duty value to be written to
	 * AB8500_PWM_OUT_BLINK_CTRL REGS[7:4]
	 */
	value |= ((duty_ns << AB8505_PWM_OUT_BLINK_CTRL_DUTYBIT) &
					AB8505_PWM_OUT_BLINK_DUTYMASK);

	reg = AB8505_PWM_OUT_BLINK_CTRL1_REG + (pwm->pwm_id - 1);

	ret = abx500_set_register_interruptible(pwm->dev, AB8500_MISC,
			reg, (u8)value);
	if (ret < 0)
		dev_err(pwm->dev, "%s: Failed to config PWM blink,Error %d\n",
							pwm->label, ret);
	return ret;
}
EXPORT_SYMBOL(pwm_config_blink);

int pwm_blink_ctrl(struct pwm_device *pwm , int enable)
{
	int ret;

	if ((!is_ab8505(pwm->parent)) || (!pwm->blink_en)) {
		dev_err(pwm->dev, "setting blinking for this "
					"device not supported\n");
		return -EINVAL;
	}
	/*
	 * Enable/disable blinking feature for corresponding PWMOUT
	 * channel depending on value of enable.
	 */
	ret = abx500_mask_and_set_register_interruptible(pwm->dev,
			AB8500_MISC, AB8505_PWM_OUT_BLINK_CTRL4_REG,
			1 << (pwm->pwm_id-1), enable << (pwm->pwm_id-1));
	if (ret < 0)
		dev_err(pwm->dev, "%s: Failed to control PWM blink,Error %d\n",
							pwm->label, ret);
	return ret;
}
EXPORT_SYMBOL(pwm_blink_ctrl);

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	int ret = 0;
	unsigned int higher_val, lower_val;
	u8 reg;

	/*
	 * get the first 8 bits that are be written to
	 * AB8500_PWM_OUT_CTRL1_REG[0:7]
	 */
	lower_val = duty_ns & 0x00FF;
	/*
	 * get bits [9:10] that are to be written to
	 * AB8500_PWM_OUT_CTRL2_REG[0:1]
	 */
	higher_val = ((duty_ns & 0x0300) >> 8);

	reg = AB8500_PWM_OUT_CTRL1_REG + ((pwm->pwm_id - 1) * 2);

	ret = abx500_set_register_interruptible(pwm->dev, AB8500_MISC,
			reg, (u8)lower_val);
	if (ret < 0)
		return ret;
	ret = abx500_set_register_interruptible(pwm->dev, AB8500_MISC,
			(reg + 1), (u8)higher_val);

	return ret;
}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	int ret;

	if (!pwm->clk_enabled) {
		ret = clk_enable(pwm->clk);
		if (ret < 0) {
			dev_err(pwm->dev, "failed to enable clock\n");
			return ret;
		}
		pwm->clk_enabled = true;
	}
	ret = abx500_mask_and_set_register_interruptible(pwm->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << (pwm->pwm_id-1), 1 << (pwm->pwm_id-1));
	if (ret < 0)
		dev_err(pwm->dev, "%s: Failed to enable PWM, Error %d\n",
							pwm->label, ret);
	return ret;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	int ret;

	ret = abx500_mask_and_set_register_interruptible(pwm->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << (pwm->pwm_id-1), DISABLE_PWM);
	/*
	 * Workaround to set PWM in disable.
	 * If enable bit is not toggled the PWM might output 50/50 duty cycle
	 * even though it should be disabled
	 */
	ret &= abx500_mask_and_set_register_interruptible(pwm->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << (pwm->pwm_id-1),
				ENABLE_PWM << (pwm->pwm_id-1));
	ret &= abx500_mask_and_set_register_interruptible(pwm->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << (pwm->pwm_id-1), DISABLE_PWM);

	if (ret < 0)
		dev_err(pwm->dev, "%s: Failed to disable PWM, Error %d\n",
							pwm->label, ret);
	if (pwm->clk_enabled) {
		clk_disable(pwm->clk);
		pwm->clk_enabled = false;
	}

	return;
}
EXPORT_SYMBOL(pwm_disable);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == pwm_id) {
			pwm->label = label;
			pwm->pwm_id = pwm_id;
			return pwm;
		}
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	pwm_disable(pwm);
}
EXPORT_SYMBOL(pwm_free);

static ssize_t store_blink_status(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_device *pwm;
	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;
	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == val)
			break;
		else {
			/* check if PWM ID is valid*/
			if (val > pwm->num_pwm) {
				dev_err(pwm->dev, "Invalid PWM ID\n");
				return -EINVAL;
			}
		}
	}
	if ((!is_ab8505(pwm->parent)) || (!pwm->blink_en)) {
		dev_err(pwm->dev, "setting blinking for this "
					"device not supported\n");
		return -EINVAL;
	}
	/*Disable blink functionlity */
	pwm_blink_ctrl(pwm, 0);
	return count;
}

static DEVICE_ATTR(disable_blink, S_IWUGO, NULL, store_blink_status);

static struct attribute *pwmled_attributes[] = {
	&dev_attr_disable_blink.attr,
	NULL
};

static const struct attribute_group pwmled_attr_group = {
	.attrs = pwmled_attributes,
};

static int __devinit ab8500_pwm_probe(struct platform_device *pdev)
{
	struct ab8500 *parent = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_platform_data *plat = dev_get_platdata(parent->dev);
	struct ab8500_pwmled_platform_data *pdata;
	struct pwm_device *pwm;
	int ret = 0 , i;

	/* get pwmled specific platform data */
	if (!plat->pwmled) {
		dev_err(&pdev->dev, "no pwm platform data supplied\n");
		return -EINVAL;
	}
	pdata = plat->pwmled;
	/*
	 * Nothing to be done in probe, this is required to get the
	 * device which is required for ab8500 read and write
	 */
	pwm = kzalloc(((sizeof(struct pwm_device)) * pdata->num_pwm),
						 GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	for (i = 0; i < pdata->num_pwm; i++) {
		pwm[i].dev = &pdev->dev;
		pwm[i].parent = parent;
		pwm[i].blink_en = pdata->leds[i].blink_en;
		pwm[i].pwm_id = pdata->leds[i].pwm_id;
		pwm[i].num_pwm = pdata->num_pwm;
		list_add_tail(&pwm[i].node, &pwm_list);
	}
	for (i = 0; i < pdata->num_pwm; i++) {
		/*Implement sysfs only if blink is enabled*/
		if ((is_ab8505(pwm[i].parent)) && (pwm[i].blink_en)) {
			/* sysfs implementation to disable the blink */
			ret = sysfs_create_group(&pdev->dev.kobj,
							&pwmled_attr_group);
			if (ret) {
				dev_err(&pdev->dev, "failed to create"
						" sysfs entries\n");
				goto fail;
			}
			break;
		}
	}
	pwm->clk = clk_get(pwm->dev, NULL);
	if (IS_ERR(pwm->clk)) {
		dev_err(pwm->dev, "clock request failed\n");
		ret = PTR_ERR(pwm->clk);
		goto err_clk;
	}
	platform_set_drvdata(pdev, pwm);
	pwm->clk_enabled = false;
	dev_dbg(pwm->dev, "pwm probe successful\n");
	return ret;

err_clk:
	for (i = 0; i < pdata->num_pwm; i++) {
		if ((is_ab8505(pwm[i].parent)) && (pwm[i].blink_en)) {
			sysfs_remove_group(&pdev->dev.kobj,
				&pwmled_attr_group);
			break;
		}
	}
fail:
	list_del(&pwm->node);
	kfree(pwm);
	return ret;
}

static int __devexit ab8500_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < pwm->num_pwm; i++) {
		if ((is_ab8505(pwm[i].parent)) && (pwm[i].blink_en)) {
			sysfs_remove_group(&pdev->dev.kobj,
				&pwmled_attr_group);
			break;
		}
	}
	list_del(&pwm->node);
	clk_put(pwm->clk);
	dev_dbg(&pdev->dev, "pwm driver removed\n");
	kfree(pwm);
	return 0;
}

static struct platform_driver ab8500_pwm_driver = {
	.driver = {
		.name = "ab8500-pwm",
		.owner = THIS_MODULE,
	},
	.probe = ab8500_pwm_probe,
	.remove = __devexit_p(ab8500_pwm_remove),
};

static int __init ab8500_pwm_init(void)
{
	return platform_driver_register(&ab8500_pwm_driver);
}

static void __exit ab8500_pwm_exit(void)
{
	platform_driver_unregister(&ab8500_pwm_driver);
}

subsys_initcall(ab8500_pwm_init);
module_exit(ab8500_pwm_exit);
MODULE_AUTHOR("Arun MURTHY <arun.murthy@stericsson.com>");
MODULE_DESCRIPTION("AB8500 Pulse Width Modulation Driver");
MODULE_ALIAS("AB8500 PWM driver");
MODULE_LICENSE("GPL v2");
