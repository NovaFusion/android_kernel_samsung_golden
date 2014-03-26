/*
 * Copyright (C) ST Ericsson SA 2010
 *
 * Sim Interface driver for AB5500
 *
 * License Terms: GNU General Public License v2
 * Author: Bibek Basu <bibek.basu@stericsson.com>
 */
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/io.h>
#include <linux/err.h>

#define USIM_SUP2_REG		0x13
#define USIM_SUP_REG		0x14
#define USIM_SIMCTRL_REG	0x17
#define USIM_SIMCTRL2_REG	0x18
#define USIM_USBUICC_REG	0x19
#define USIM_USBUICC2_REG	0x20
#define SIM_DAT_PULLUP_10K	0x0F
#define SIM_LDO_1_8V		1875000
#define SIM_LDO_2_8V		2800000
#define SIM_LDO_2_9V		2900000

enum shift {
	SHIFT0,
	SHIFT1,
	SHIFT2,
	SHIFT3,
	SHIFT4,
	SHIFT5,
	SHIFT6,
	SHIFT7,
};

enum mask {
	MASK1 = 1,
	MASK3 = 3,
	MASK7 = 7,
};

enum sim_mode {
	OFF_MODE,
	LOW_PWR,
	PWRCTRL,
	FULL_PWR,
};
/**
 * struct ab5500_sim - ab5500 Sim Interface device information
 * @dev:		pointer to the structure device
 * @lock:		mutex lock
 * @sim_int_status:	Sim presence status
 * @irq_base:	Base of the two irqs
 */
struct ab5500_sim {
	struct device *dev;
	struct mutex lock;
	bool sim_int_status;
	u8 irq_base;
};

/* Exposure to the sysfs interface */
int ab5500_sim_weak_pulldforce(struct device *dev,
		struct device_attribute *attr,
		const char *user_buf, size_t count)
{
	unsigned long user_val;
	int err;
	bool enable;

	err = strict_strtoul(user_buf, 0, &user_val);
	if (err)
		return -EINVAL;
	enable = user_val ? true : false;
	err = abx500_mask_and_set(dev, AB5500_BANK_SIM_USBSIM,
		USIM_USBUICC2_REG, MASK1 << SHIFT5, user_val << SHIFT5);
	if (err)
		return -EINVAL;
	return count;
}

int ab5500_sim_load_sel(struct device *dev,
		struct device_attribute *attr,
		const char *user_buf, size_t count)
{
	unsigned long user_val;
	int err;
	bool enable;

	err = strict_strtoul(user_buf, 0, &user_val);
	if (err)
		return -EINVAL;
	enable = user_val ? true : false;
	err = abx500_mask_and_set(dev, AB5500_BANK_SIM_USBSIM,
		USIM_USBUICC_REG, MASK1 << SHIFT1, user_val << SHIFT1);
	if (err)
		return -EINVAL;
	return count;
}

int ab5500_sim_mode_sel(struct device *dev,
		struct device_attribute *attr,
		const char *user_buf, size_t count)
{
	unsigned long user_val;
	int err;

	err = strict_strtoul(user_buf, 0, &user_val);
	if (err)
		return -EINVAL;
	err = abx500_mask_and_set(dev, AB5500_BANK_SIM_USBSIM,
		USIM_SIMCTRL2_REG, MASK3 << SHIFT4, user_val << SHIFT4);
	if (err)
		return -EINVAL;
	return count;
}

int ab5500_sim_dat_pullup(struct device *dev,
		struct device_attribute *attr,
		const char *user_buf, size_t count)
{
	unsigned long user_val;
	int err;

	err = strict_strtoul(user_buf, 0, &user_val);
	if (err)
		return -EINVAL;
	err = abx500_mask_and_set(dev, AB5500_BANK_SIM_USBSIM,
		USIM_SIMCTRL_REG, MASK7, user_val);
	if (err)
		return -EINVAL;
	return count;
}

int ab5500_sim_enable_pullup(struct device *dev,
		struct device_attribute *attr,
		const char *user_buf, size_t count)
{
	unsigned long user_val;
	int err;
	bool enable;

	err = strict_strtoul(user_buf, 0, &user_val);
	if (err)
		return -EINVAL;
	enable = user_val ? true : false;
	err = abx500_mask_and_set(dev, AB5500_BANK_SIM_USBSIM,
		USIM_SIMCTRL_REG, MASK1 << SHIFT3, enable << SHIFT3);
	if (err)
		return -EINVAL;
	return count;
}

static ssize_t ab5500_simoff_int(struct device *dev,
			       struct device_attribute *devattr, char *user_buf)
{
	struct ab5500_sim *di = dev_get_drvdata(dev);
	int len;

	mutex_lock(&di->lock);
	len = sprintf(user_buf, "%d\n", di->sim_int_status);
	mutex_unlock(&di->lock);
	return len;
}

static DEVICE_ATTR(enable_pullup, S_IWUSR, NULL, ab5500_sim_enable_pullup);
static DEVICE_ATTR(dat_pullup, S_IWUSR, NULL, ab5500_sim_dat_pullup);
static DEVICE_ATTR(mode_sel, S_IWUSR, NULL, ab5500_sim_mode_sel);
static DEVICE_ATTR(load_sel, S_IWUSR, NULL, ab5500_sim_load_sel);
static DEVICE_ATTR(weak_pulldforce, S_IWUSR, NULL, ab5500_sim_weak_pulldforce);
static DEVICE_ATTR(simoff_int, S_IRUGO, ab5500_simoff_int, NULL);

static struct attribute *ab5500_sim_attributes[] = {
	&dev_attr_enable_pullup.attr,
	&dev_attr_dat_pullup.attr,
	&dev_attr_mode_sel.attr,
	&dev_attr_load_sel.attr,
	&dev_attr_weak_pulldforce.attr,
	&dev_attr_simoff_int.attr,
	NULL
};

static const struct attribute_group ab5500sim_attr_grp = {
	.attrs = ab5500_sim_attributes,
};

static irqreturn_t ab5500_sim_irq_handler(int irq, void *irq_data)
{
	struct platform_device *pdev = irq_data;
	struct ab5500_sim *data = platform_get_drvdata(pdev);

	if (irq == data->irq_base)
		data->sim_int_status = true;
	else
		data->sim_int_status = false;
	sysfs_notify(&pdev->dev.kobj, NULL, "simoff_int");

	return IRQ_HANDLED;
}

static int __devexit ab5500_sim_remove(struct platform_device *pdev)
{
	struct ab5500_sim *di = platform_get_drvdata(pdev);
	int irq = platform_get_irq_byname(pdev, "SIMOFF");

	if (irq >= 0) {
		free_irq(irq, di);
		irq++;
		free_irq(irq, di);
	}
	sysfs_remove_group(&pdev->dev.kobj, &ab5500sim_attr_grp);
	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return 0;
}

static int __devinit ab5500_sim_probe(struct platform_device *pdev)
{
	int ret = 0;
	int irq;
	struct ab5500_sim *di =
		kzalloc(sizeof(struct ab5500_sim), GFP_KERNEL);
	if (!di) {
		ret = -ENOMEM;
		goto error_alloc;
	}
	dev_info(&pdev->dev, "ab5500_sim_driver PROBE\n");
	irq = platform_get_irq_byname(pdev, "SIMOFF");
	if (irq < 0) {
		dev_err(&pdev->dev, "Get irq by name failed\n");
		ret = irq;
		goto exit;
	}
	di->irq_base = irq;
	di->dev = &pdev->dev;
	mutex_init(&di->lock);
	platform_set_drvdata(pdev, di);
	/* sysfs interface to configure sim reg from user space */
	if (sysfs_create_group(&pdev->dev.kobj, &ab5500sim_attr_grp) < 0) {
		dev_err(&pdev->dev, " Failed creating sysfs group\n");
		ret = -ENOMEM;
		goto error_sysfs;
	}
	ret = request_threaded_irq(irq, NULL, ab5500_sim_irq_handler,
			IRQF_NO_SUSPEND , "ab5500-sim", pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request threaded irq failed (%d)\n", ret);
		goto error_irq;
	}
	/* this is the contiguous irq for sim removal,falling edge */
	irq = irq + 1;
	ret = request_threaded_irq(irq, NULL, ab5500_sim_irq_handler,
			IRQF_NO_SUSPEND , "ab5500-sim", pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request threaded irq failed (%d)\n", ret);
		free_irq(--irq, di);
		goto error_irq;
	}
	return ret;
error_irq:
	sysfs_remove_group(&pdev->dev.kobj, &ab5500sim_attr_grp);
error_sysfs:
	platform_set_drvdata(pdev, NULL);
exit:
	kfree(di);
error_alloc:
	return ret;
}

static struct platform_driver ab5500_sim_driver = {
	.probe = ab5500_sim_probe,
	.remove = __devexit_p(ab5500_sim_remove),
	.driver = {
		.name = "ab5500-sim",
		.owner = THIS_MODULE,
	},
};

static int __init ab5500_sim_init(void)
{
	return platform_driver_register(&ab5500_sim_driver);
}

static void __exit ab5500_sim_exit(void)
{
	platform_driver_unregister(&ab5500_sim_driver);
}

module_init(ab5500_sim_init);
module_exit(ab5500_sim_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bibek Basu");
MODULE_ALIAS("platform:ab5500-sim");
MODULE_DESCRIPTION("AB5500 sim interface driver");
