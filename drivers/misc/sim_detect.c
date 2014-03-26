/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: BIBEK BASU <bibek.basu@stericsson.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/modem/modem_client.h>
#include <mach/sim_detect.h>
#include <linux/regulator/consumer.h>

/* time in millisec */
#define TIMER_DELAY	10

struct sim_detect{
	struct work_struct	timer_expired;
	struct device	*dev;
	struct modem *modem;
	struct hrtimer timer;
	struct mutex lock;
	int voltage;
	struct regulator *vinvsim_regulator;
	bool regulator_enabled;
};

static ssize_t show_voltage(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct sim_detect *data = dev_get_drvdata(dev);
	int ret, len;

	ret = mutex_lock_interruptible(&data->lock);
	if (ret < 0)
		return ret;

	len = sprintf(buf, "%i\n", data->voltage);

	mutex_unlock(&data->lock);

	return len;
}

static ssize_t write_voltage(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sim_detect *sim_detect = dev_get_drvdata(dev);
	long val;
	int ret;

	/* check input */
	if (strict_strtol(buf, 0, &val) != 0) {
		dev_err(dev, "Invalid voltage class configured.\n");
		return -EINVAL;
	}

	switch (val) {
	case -1:
	case 0:
	case 1800000:
	case 3000000:
		break;
	default:
		dev_err(dev, "Invalid voltage class configured.\n");
		return -EINVAL;
	}

	/* lock */
	ret = mutex_lock_interruptible(&sim_detect->lock);
	if (ret < 0)
		return ret;

	/* update state */
	sim_detect->voltage = val;

	/* call regulator */
	switch (sim_detect->voltage) {
	case 0:
		/* SIM voltage is unknown, turn on regulator for 3 V SIM */
	case 3000000:
		/* Vinvsim supply is used only for 3 V SIM */
		if (!sim_detect->regulator_enabled && sim_detect->vinvsim_regulator) {
			ret = regulator_enable(sim_detect->vinvsim_regulator);
			if (ret) {
				dev_err(dev, "Failed to enable regulator.\n");
				goto out_unlock;
			}
			sim_detect->regulator_enabled = true;
		}
		break;
	case 1800000:
	case -1:
		/* Vbatvsim is used otherwise */
		if (sim_detect->regulator_enabled && sim_detect->vinvsim_regulator) {
			regulator_disable(sim_detect->vinvsim_regulator);
			sim_detect->regulator_enabled = false;
		}
	}

out_unlock:
	/* unlock and return */
	mutex_unlock(&sim_detect->lock);

	return count;
}

static DEVICE_ATTR(voltage, S_IWUSR | S_IRUGO, show_voltage, write_voltage);

static struct attribute *sim_attributes[] = {
	&dev_attr_voltage.attr,
	NULL
};

static const struct attribute_group sim_attr_group = {
	.attrs	= sim_attributes,
};

static void inform_modem_release(struct work_struct *work)
{
	struct sim_detect *sim_detect =
		container_of(work, struct sim_detect, timer_expired);

	/* call Modem Access Framework api to release modem */
	modem_release(sim_detect->modem);
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	struct sim_detect *sim_detect =
		container_of(timer, struct sim_detect, timer);

	schedule_work(&sim_detect->timer_expired);
	return HRTIMER_NORESTART;
}

static irqreturn_t sim_activity_irq(int irq, void *dev)
{
	struct sim_detect *sim_detect = dev;

	/* call Modem Access Framework api to acquire modem */
	modem_request(sim_detect->modem);
	/* start the timer for 10ms */
	hrtimer_start(&sim_detect->timer,
			ktime_set(0, TIMER_DELAY*NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
/**
 * sim_detect_suspend() - This routine puts the Sim detect in to sustend state.
 * @dev:	pointer to device structure.
 *
 * This routine checks the current ongoing communication with Modem by
 * examining the modem_get_usage and work_pending state.
 * accordingly prevents suspend if modem communication
 * is on-going.
 */
int sim_detect_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sim_detect *sim_detect = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s called...\n", __func__);
	/* if modem is accessed, event system suspend */
	if (modem_get_usage(sim_detect->modem)
			|| work_pending(&sim_detect->timer_expired))
		return -EBUSY;
	else
		return 0;
}

static const struct dev_pm_ops sim_detect_dev_pm_ops = {
	.suspend = sim_detect_suspend,
};
#endif


static int __devinit sim_detect_probe(struct platform_device *pdev)
{
	struct sim_detect_platform_data *plat = dev_get_platdata(&pdev->dev);
	struct sim_detect *sim_detect;
	int ret;

	sim_detect = kzalloc(sizeof(struct sim_detect), GFP_KERNEL);
	if (sim_detect == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	/* initialize data */
	mutex_init(&sim_detect->lock);
	sim_detect->voltage = 0;

	sim_detect->dev = &pdev->dev;
	INIT_WORK(&sim_detect->timer_expired, inform_modem_release);
	hrtimer_init(&sim_detect->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sim_detect->timer.function = timer_callback;

	sim_detect->modem = modem_get(sim_detect->dev, "u8500-shrm-modem");
	if (IS_ERR(sim_detect->modem)) {
		ret = PTR_ERR(sim_detect->modem);
		dev_err(sim_detect->dev, "Could not retrieve the modem\n");
		goto out_free;
	}

	/* set drvdata */
	platform_set_drvdata(pdev, sim_detect);

	/* request irq */
	ret = request_threaded_irq(plat->irq_num,
		       NULL, sim_activity_irq,
		       IRQF_TRIGGER_FALLING |
		       IRQF_TRIGGER_RISING |
		       IRQF_NO_SUSPEND,
		       "sim activity", sim_detect);
	if (ret < 0)
		goto out_put_modem;

	/* get regulator */
	sim_detect->regulator_enabled = false;
	sim_detect->vinvsim_regulator = regulator_get(sim_detect->dev,
							 "vinvsim");
	if (IS_ERR(sim_detect->vinvsim_regulator)) {
		dev_err(&pdev->dev,
			"Failed to get vinvsim regulator, continuing without it (dev_name %s).\n",
			dev_name(sim_detect->dev));
		sim_detect->vinvsim_regulator = NULL;
	}

	/* register sysfs entry */
	ret = sysfs_create_group(&pdev->dev.kobj, &sim_attr_group);
	if (ret != 0) {
		dev_err(&pdev->dev,
			"Failed to create attribute group: %d\n", ret);
		goto out_free_regulator;
	}

	return 0;

out_free_regulator:
	if (sim_detect->vinvsim_regulator)
		regulator_put(sim_detect->vinvsim_regulator);
out_free_irq:
	free_irq(plat->irq_num, sim_detect);
out_put_modem:
	modem_put(sim_detect->modem);
	platform_set_drvdata(pdev, NULL);
out_free:
	kfree(sim_detect);
	return ret;
}

static int __devexit sim_detect_remove(struct platform_device *pdev)
{
	struct sim_detect *sim_detect = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &sim_attr_group);
	if (sim_detect->vinvsim_regulator)
		regulator_put(sim_detect->vinvsim_regulator);
	modem_put(sim_detect->modem);
	platform_set_drvdata(pdev, NULL);
	kfree(sim_detect);
	return 0;
}

static struct platform_driver sim_detect_driver = {
	.driver = {
		.name = "sim-detect",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &sim_detect_dev_pm_ops,
#endif
	},
	.probe = sim_detect_probe,
	.remove = __devexit_p(sim_detect_remove),
};

static int __init sim_detect_init(void)
{
	return platform_driver_register(&sim_detect_driver);
}
module_init(sim_detect_init);

static void __exit sim_detect_exit(void)
{
	platform_driver_unregister(&sim_detect_driver);
}
module_exit(sim_detect_exit);

MODULE_AUTHOR("BIBEK BASU <bibek.basu@stericsson.com>");
MODULE_DESCRIPTION("Detects SIM Hot Swap and wakes modem");
MODULE_ALIAS("platform:sim-detect");
MODULE_LICENSE("GPL v2");
