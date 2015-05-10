/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *
 * AB8500 Power-On Key handler
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>

/* Ponkey time control bits */
#define AB5500_MCB		0x2F
#define AB5500_PONKEY_10SEC	0x0
#define AB5500_PONKEY_5SEC	0x1
#define AB5500_PONKEY_DISABLE	0x2
#define AB5500_PONKEY_TMR_MASK	0x1
#define AB5500_PONKEY_TR_MASK	0x2

extern struct class *sec_class;

static int ab5500_ponkey_hw_init(struct platform_device *);

struct ab8500_ponkey_variant {
	const char *irq_falling;
	const char *irq_rising;
	int (*hw_init)(struct platform_device *);
};

static const struct ab8500_ponkey_variant ab5500_onswa = {
	.irq_falling	= "ONSWAn_falling",
	.irq_rising	= "ONSWAn_rising",
	.hw_init	= ab5500_ponkey_hw_init,
};

static const struct ab8500_ponkey_variant ab8500_ponkey = {
	.irq_falling	= "ONKEY_DBF",
	.irq_rising	= "ONKEY_DBR",
};

/**
 * struct ab8500_ponkey_info - ab8500 ponkey information
 * @input_dev: pointer to input device
 * @irq_dbf: irq number for falling transition
 * @irq_dbr: irq number for rising transition
 */
struct ab8500_ponkey_info {
	struct input_dev	*idev;
	struct device *sec_power_key;
	int			irq_dbf;
	int			irq_dbr;
	bool		key_state;
	bool		pcut_wa;
	struct delayed_work	pcut_work;
	u8			pcut_ctrl;
};

#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
extern bool gpio_keys_getstate(int keycode);
extern void gpio_keys_start_upload_modtimer(void);
extern int jack_is_detected;
#endif
extern void gpio_keys_setstate(int keycode, bool bState);

static int ab5500_ponkey_hw_init(struct platform_device *pdev)
{
	u8 val;
	struct ab5500_ponkey_platform_data *pdata;

	pdata = pdev->dev.platform_data;
	if (pdata) {
		switch (pdata->shutdown_secs) {
		case 0:
			val = AB5500_PONKEY_DISABLE;
			break;
		case 5:
			val = AB5500_PONKEY_5SEC;
			break;
		case 10:
			val = AB5500_PONKEY_10SEC;
			break;
		default:
			val = AB5500_PONKEY_10SEC;
		}
	} else {
		val = AB5500_PONKEY_10SEC;
	}
	return abx500_mask_and_set(
		&pdev->dev,
		AB5500_BANK_STARTUP,
		AB5500_MCB,
		AB5500_PONKEY_TMR_MASK | AB5500_PONKEY_TR_MASK,
		val);
}

static ssize_t power_key_pressed_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ab8500_ponkey_info *info = dev_get_drvdata(dev);
	int keystate = 0;

	keystate = info->key_state;

	if (keystate)
		sprintf(buf, "PRESS");
	else
		sprintf(buf, "RELEASE");

	return strlen(buf);
}

static DEVICE_ATTR(sec_power_key_pressed, 0664, power_key_pressed_show, NULL);

static struct attribute *sec_power_key_attrs[] = {
	&dev_attr_sec_power_key_pressed.attr,
	NULL,
};

static struct attribute_group sec_power_key_attr_group = {
	.attrs = sec_power_key_attrs,
};

#define PCUT_CTR_AND_STATUS 0x12

static void pcut_disable(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ab8500_ponkey_info *info = container_of(dwork,
				struct ab8500_ponkey_info, pcut_work);
	struct device *dev = info->idev->dev.parent;

	abx500_get(dev, AB8500_RTC, PCUT_CTR_AND_STATUS, &info->pcut_ctrl);
	abx500_set(dev, AB8500_RTC, PCUT_CTR_AND_STATUS, 0);
}

/* AB8500 gives us an interrupt when ONKEY is held */
static irqreturn_t ab8500_ponkey_handler(int irq, void *data)
{
	struct ab8500_ponkey_info *info = data;

	if (irq == info->irq_dbf) {
		if (info->pcut_wa)
			schedule_delayed_work(&info->pcut_work, HZ * 3);

#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
		if (gpio_keys_getstate(KEY_VOLUMEUP) && jack_is_detected)
			gpio_keys_start_upload_modtimer();
#endif
		gpio_keys_setstate(KEY_POWER, true);
		info->key_state = true;
		input_report_key(info->idev, KEY_POWER, true);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		dev_info(info->idev->dev.parent, "Power KEY pressed %d\n", KEY_POWER);
#endif
	} else if (irq == info->irq_dbr) {
		if (info->pcut_wa && !cancel_delayed_work_sync(&info->pcut_work))
			abx500_set(info->idev->dev.parent, AB8500_RTC,
				   PCUT_CTR_AND_STATUS, info->pcut_ctrl);

		gpio_keys_setstate(KEY_POWER, false);
		info->key_state = false;
		input_report_key(info->idev, KEY_POWER, false);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		dev_info(info->idev->dev.parent, "Power KEY released %d\n", KEY_POWER);
#endif

	}

	input_sync(info->idev);

	return IRQ_HANDLED;
}

static int __devinit ab8500_ponkey_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	const struct ab8500_ponkey_variant *variant;
	struct ab8500_ponkey_info *info;
	int irq_dbf, irq_dbr, ret;

	variant = (const struct ab8500_ponkey_variant *)
		  pdev->id_entry->driver_data;

	if (variant->hw_init) {
		ret = variant->hw_init(pdev);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to init hw");
			return ret;
		}
	}

	irq_dbf = platform_get_irq_byname(pdev, variant->irq_falling);
	if (irq_dbf < 0) {
		dev_err(&pdev->dev, "No IRQ for %s: %d\n",
			variant->irq_falling, irq_dbf);
		return irq_dbf;
	}

	irq_dbr = platform_get_irq_byname(pdev, variant->irq_rising);
	if (irq_dbr < 0) {
		dev_err(&pdev->dev, "No IRQ for %s: %d\n",
			variant->irq_rising, irq_dbr);
		return irq_dbr;
	}

	info = kzalloc(sizeof(struct ab8500_ponkey_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if ((is_ab8505(ab8500) || is_ab9540(ab8500))
	     && abx500_get_chip_id(&pdev->dev) >= AB8500_CUT2P0)
		info->pcut_wa = true;

	INIT_DELAYED_WORK(&info->pcut_work, pcut_disable);

	info->irq_dbf = irq_dbf;
	info->irq_dbr = irq_dbr;

	info->idev = input_allocate_device();
	if (!info->idev) {
		dev_err(&pdev->dev, "Failed to allocate input dev\n");
		ret = -ENOMEM;
		goto out;
	}

	info->idev->name = "AB8500 POn(PowerOn) Key";
	info->idev->dev.parent = &pdev->dev;
	info->idev->evbit[0] = BIT_MASK(EV_KEY);
	info->idev->keybit[BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER);


	info->sec_power_key = device_create(sec_class, NULL, 0, info, "sec_power_key");
	if (IS_ERR(info->sec_power_key))
		dev_err(&pdev->dev, "Failed to create sec_power_key device\n");

	ret = sysfs_create_group(&info->sec_power_key->kobj, &sec_power_key_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Unable to export sec_power_key device: %d\n", ret);
		goto out;
	}

	ret = input_register_device(info->idev);
	if (ret) {
		dev_err(&pdev->dev, "Can't register input device: %d\n", ret);
		goto out_unfreedevice;
	}

	ret = request_threaded_irq(info->irq_dbf, NULL, ab8500_ponkey_handler,
					IRQF_NO_SUSPEND, "ab8500-ponkey-dbf",
					info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request dbf IRQ#%d: %d\n",
				info->irq_dbf, ret);
		goto out_unregisterdevice;
	}

	ret = request_threaded_irq(info->irq_dbr, NULL, ab8500_ponkey_handler,
					IRQF_NO_SUSPEND, "ab8500-ponkey-dbr",
					info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request dbr IRQ#%d: %d\n",
				info->irq_dbr, ret);
		goto out_irq_dbf;
	}

	platform_set_drvdata(pdev, info);

	return 0;

out_irq_dbf:
	free_irq(info->irq_dbf, info);
out_unregisterdevice:
	input_unregister_device(info->idev);
	info->idev = NULL;
out_unfreedevice:
	input_free_device(info->idev);
	sysfs_remove_group(&info->sec_power_key->kobj, &sec_power_key_attr_group);
out:
	kfree(info);
	return ret;
}

static int __devexit ab8500_ponkey_remove(struct platform_device *pdev)
{
	struct ab8500_ponkey_info *info = platform_get_drvdata(pdev);

	free_irq(info->irq_dbf, info);
	free_irq(info->irq_dbr, info);
	input_unregister_device(info->idev);
	kfree(info);
	return 0;
}

static struct platform_device_id ab8500_ponkey_id_table[] = {
	{ "ab5500-onswa", (kernel_ulong_t)&ab5500_onswa, },
	{ "ab8500-poweron-key", (kernel_ulong_t)&ab8500_ponkey, },
	{ },
};
MODULE_DEVICE_TABLE(platform, ab8500_ponkey_id_table);

static struct platform_driver ab8500_ponkey_driver = {
	.driver		= {
		.name	= "ab8500-poweron-key",
		.owner	= THIS_MODULE,
	},
	.id_table	= ab8500_ponkey_id_table,
	.probe		= ab8500_ponkey_probe,
	.remove		= __devexit_p(ab8500_ponkey_remove),
};

static int __init ab8500_ponkey_init(void)
{
	return platform_driver_register(&ab8500_ponkey_driver);
}
module_init(ab8500_ponkey_init);

static void __exit ab8500_ponkey_exit(void)
{
	platform_driver_unregister(&ab8500_ponkey_driver);
}
module_exit(ab8500_ponkey_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_DESCRIPTION("ST-Ericsson AB8500 Power-ON(Pon) Key driver");
