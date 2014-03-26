/*
 * ab5500-vibra.c - driver for vibrator in ST-Ericsson AB5500 chip
 *
 * Copyright (C) 2011 ST-Ericsson SA.
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Shreshtha Kumar SAHU <shreshthakumar.sahu@stericsson.com>
 */

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/err.h>
#include "timed_output.h"

#include <linux/mfd/abx500.h>		/* abx500_* */
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>
#include <linux/ab5500-vibra.h>

#define AB5500_VIBRA_DEV_NAME          "ab5500:vibra"
#define AB5500_VIBRA_DRV_NAME		"ab5500-vibrator"

/* Vibrator Register Address Offsets */
#define AB5500_VIB_CTRL		0x10
#define AB5500_VIB_VOLT		0x11
#define AB5500_VIB_FUND_FREQ	0x12	/* Linear vibra resonance freq. */
#define AB5500_VIB_FUND_DUTY	0x13
#define AB5500_KELVIN_ANA	0xB1
#define AB5500_VIBRA_KELVIN	0xFE

/* Vibrator Control */
#define AB5500_VIB_DISABLE	(0x80)
#define AB5500_VIB_PWR_ON	(0x40)
#define AB5500_VIB_FUND_EN	(0x20)
#define AB5500_VIB_FREQ_SHIFT	(0)
#define AB5500_VIB_DUTY_SHIFT	(3)
#define AB5500_VIB_VOLT_SHIFT	(0)
#define AB5500_VIB_PULSE_SHIFT	(4)
#define VIBRA_KELVIN_ENABLE	(0x90)
#define VIBRA_KELVIN_VOUT	(0x20)

/* Vibrator Freq. (in HZ) and Duty */
enum ab5500_vibra_freq {
	AB5500_VIB_FREQ_1HZ = 1,
	AB5500_VIB_FREQ_2HZ,
	AB5500_VIB_FREQ_4HZ,
	AB5500_VIB_FREQ_8HZ,
};

enum ab5500_vibra_duty {
	AB5500_VIB_DUTY_100 = 0,
	AB5500_VIB_DUTY_75 = 8,
	AB5500_VIB_DUTY_50 = 16,
	AB5500_VIB_DUTY_25 = 24,
};

/* Linear vibrator resonance freq. duty */
#define AB5500_VIB_RDUTY_50	(0x7F)

/* Vibration magnitudes */
#define AB5500_VIB_FREQ_MAX	(4)
#define AB5500_VIB_DUTY_MAX	(4)

static u8 vib_freq[AB5500_VIB_FREQ_MAX] = {
	AB5500_VIB_FREQ_1HZ,
	AB5500_VIB_FREQ_2HZ,
	AB5500_VIB_FREQ_4HZ,
	AB5500_VIB_FREQ_8HZ,
};

static u8 vib_duty[AB5500_VIB_DUTY_MAX] = {
	AB5500_VIB_DUTY_100,
	AB5500_VIB_DUTY_75,
	AB5500_VIB_DUTY_50,
	AB5500_VIB_DUTY_25,
};

/**
 * struct ab5500_vibra - Vibrator driver interal info.
 * @tdev:		Pointer to timed output device structure
 * @dev:		Reference to vibra device structure
 * @vibra_workqueue:    Pointer to vibrator workqueue structure
 * @vibra_work:		Vibrator work
 * @gpadc:		Gpadc instance
 * @vibra_wait:		Vibrator wait queue head
 * @vibra_lock:		Vibrator lock
 * @timeout_ms:		Indicates how long time the vibrator will be enabled
 * @timeout_start:	Start of vibrator in jiffies
 * @pdata:		Local pointer to platform data with vibrator parameters
 * @magnitude:		required vibration strength
 * @enable:		Vibrator running status
 * @eol:		Vibrator end of life(eol) status
 **/
struct ab5500_vibra {
	struct timed_output_dev		tdev;
	struct device			*dev;
	struct workqueue_struct		*vibra_workqueue;
	struct work_struct		vibra_work;
	struct ab5500_gpadc		*gpadc;
	wait_queue_head_t		vibra_wait;
	spinlock_t			vibra_lock;
	unsigned int			timeout_ms;
	unsigned long			timeout_start;
	struct ab5500_vibra_platform_data *pdata;
	u8				magnitude;
	bool				enable;
	bool				eol;
};

static inline u8 vibra_magnitude(u8 mag)
{
	mag /= (AB5500_VIB_FREQ_MAX * AB5500_VIB_DUTY_MAX);
	mag = vib_freq[mag / AB5500_VIB_FREQ_MAX] << AB5500_VIB_FREQ_SHIFT;
	mag |= vib_duty[mag % AB5500_VIB_DUTY_MAX] << AB5500_VIB_DUTY_SHIFT;

	return mag;
}

static int ab5500_setup_vibra_kelvin(struct ab5500_vibra* vibra)
{
	int ret;

	/* Establish the kelvin IP connection to be measured */
	ret = abx500_set_register_interruptible(vibra->dev,
		AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		AB5500_KELVIN_ANA, VIBRA_KELVIN_ENABLE);
	if (ret < 0) {
		dev_err(vibra->dev, "failed to set kelvin network\n");
		return ret;
	}

	/* Select vibra parameter to be measured */
	ret = abx500_set_register_interruptible(vibra->dev, AB5500_BANK_VIBRA,
		AB5500_VIBRA_KELVIN, VIBRA_KELVIN_VOUT);
	if (ret < 0)
		dev_err(vibra->dev, "failed to select the kelvin param\n");

	return ret;
}

static int ab5500_vibra_start(struct ab5500_vibra* vibra)
{
	u8 ctrl = 0;

	ctrl = AB5500_VIB_PWR_ON |
		vibra_magnitude(vibra->magnitude);

	if (vibra->pdata->type == AB5500_VIB_LINEAR)
		ctrl |= AB5500_VIB_FUND_EN;

	return abx500_set_register_interruptible(vibra->dev,
			AB5500_BANK_VIBRA, AB5500_VIB_CTRL, ctrl);
}

static int ab5500_vibra_stop(struct ab5500_vibra* vibra)
{
	return abx500_mask_and_set_register_interruptible(vibra->dev,
			AB5500_BANK_VIBRA, AB5500_VIB_CTRL,
			AB5500_VIB_PWR_ON, 0);
}

static int ab5500_vibra_eol_check(struct ab5500_vibra* vibra)
{
	int ret, vout;

	ret = ab5500_setup_vibra_kelvin(vibra);
	if (ret < 0) {
		dev_err(vibra->dev, "failed to setup kelvin network\n");
		return ret;
	}

	/* Start vibra to measure voltage */
	ret = ab5500_vibra_start(vibra);
	if (ret < 0) {
		dev_err(vibra->dev, "failed to start vibra\n");
		return ret;
	}
	/* 20ms delay required for voltage rampup */
	wait_event_interruptible_timeout(vibra->vibra_wait,
			0, msecs_to_jiffies(20));

	vout  = ab5500_gpadc_convert(vibra->gpadc, VIBRA_KELVIN);
	if (vout < 0) {
		dev_err(vibra->dev, "failed to read gpadc vibra\n");
		return vout;
	}

	/* Stop vibra after measuring voltage */
	ret  = ab5500_vibra_stop(vibra);
	if (ret < 0) {
		dev_err(vibra->dev, "failed to stop vibra\n");
		return ret;
	}
	/* Check for vibra eol condition */
	if (vout < vibra->pdata->eol_voltage) {
		vibra->eol = true;
		dev_err(vibra->dev, "Vibra eol detected. Disabling vibra!\n");
	}

	return ret;
}

/**
 * ab5500_vibra_work() - Vibrator work, turns on/off vibrator
 * @work: Pointer to work structure
 *
 * This function is called from workqueue, turns on/off vibrator
 **/
static void ab5500_vibra_work(struct work_struct *work)
{
	struct ab5500_vibra *vibra = container_of(work,
			struct ab5500_vibra, vibra_work);
	unsigned long flags;
	int ret;

	ret = ab5500_vibra_start(vibra);
	if (ret < 0)
		dev_err(vibra->dev, "reg[%d] w failed: %d\n",
				AB5500_VIB_CTRL, ret);

	wait_event_interruptible_timeout(vibra->vibra_wait,
			0, msecs_to_jiffies(vibra->timeout_ms));

	ret = ab5500_vibra_stop(vibra);
	if (ret < 0)
		dev_err(vibra->dev, "reg[%d] w failed: %d\n",
				AB5500_VIB_CTRL, ret);

	spin_lock_irqsave(&vibra->vibra_lock, flags);

	vibra->timeout_start = 0;
	vibra->enable = false;

	spin_unlock_irqrestore(&vibra->vibra_lock, flags);
}

/**
 * vibra_enable() - Enables vibrator
 * @tdev:      Pointer to timed output device structure
 * @timeout:	Time indicating how long vibrator will be enabled
 *
 * This function enables vibrator
 **/
static void vibra_enable(struct timed_output_dev *tdev, int timeout)
{
	struct ab5500_vibra *vibra = dev_get_drvdata(tdev->dev);
	unsigned long flags;

	spin_lock_irqsave(&vibra->vibra_lock, flags);

	if ((!vibra->enable || timeout) && !vibra->eol) {
		vibra->enable = true;

		vibra->timeout_ms = timeout;
		vibra->timeout_start = jiffies;
		queue_work(vibra->vibra_workqueue, &vibra->vibra_work);
	}

	spin_unlock_irqrestore(&vibra->vibra_lock, flags);
}

/**
 * vibra_get_time() - Returns remaining time to disabling vibration
 * @tdev:      Pointer to timed output device structure
 *
 * This function returns time remaining to disabling vibration
 *
 * Returns:
 *	Returns remaining time to disabling vibration
 **/
static int vibra_get_time(struct timed_output_dev *tdev)
{
	struct ab5500_vibra *vibra = dev_get_drvdata(tdev->dev);
	unsigned int ms;
	unsigned long flags;

	spin_lock_irqsave(&vibra->vibra_lock, flags);

	if (vibra->enable)
		ms = jiffies_to_msecs(vibra->timeout_start +
				msecs_to_jiffies(vibra->timeout_ms) - jiffies);
	else
		ms = 0;

	spin_unlock_irqrestore(&vibra->vibra_lock, flags);

	return ms;
}

static int ab5500_vibra_reg_init(struct ab5500_vibra *vibra)
{
	int ret = 0;
	u8 ctrl = 0;
	u8 pulse = 0;

	ctrl = (AB5500_VIB_DUTY_50 << AB5500_VIB_DUTY_SHIFT) |
		(AB5500_VIB_FREQ_8HZ << AB5500_VIB_FREQ_SHIFT);

	if (vibra->pdata->type == AB5500_VIB_LINEAR) {
		ctrl |= AB5500_VIB_FUND_EN;

		if (vibra->pdata->voltage > AB5500_VIB_VOLT_MAX)
			vibra->pdata->voltage = AB5500_VIB_VOLT_MAX;

		pulse = (vibra->pdata->pulse << AB5500_VIB_PULSE_SHIFT) |
			(vibra->pdata->voltage << AB5500_VIB_VOLT_SHIFT);
		ret = abx500_set_register_interruptible(vibra->dev,
				AB5500_BANK_VIBRA, AB5500_VIB_VOLT,
				pulse);
		if (ret < 0) {
			dev_err(vibra->dev,
				"reg[%#x] w %#x failed: %d\n",
				AB5500_VIB_VOLT, vibra->pdata->voltage, ret);
			return ret;
		}

		ret = abx500_set_register_interruptible(vibra->dev,
				AB5500_BANK_VIBRA, AB5500_VIB_FUND_FREQ,
				vibra->pdata->res_freq);
		if (ret < 0) {
			dev_err(vibra->dev, "reg[%#x] w %#x failed: %d\n",
					AB5500_VIB_FUND_FREQ,
					vibra->pdata->res_freq, ret);
			return ret;
		}

		ret = abx500_set_register_interruptible(vibra->dev,
				AB5500_BANK_VIBRA, AB5500_VIB_FUND_DUTY,
				AB5500_VIB_RDUTY_50);
		if (ret < 0) {
			dev_err(vibra->dev, "reg[%#x] w %#x failed: %d\n",
					AB5500_VIB_FUND_DUTY,
					AB5500_VIB_RDUTY_50, ret);
			return ret;
		}
	}

	ret = abx500_set_register_interruptible(vibra->dev,
			AB5500_BANK_VIBRA, AB5500_VIB_CTRL, ctrl);
	if (ret < 0) {
		dev_err(vibra->dev, "reg[%#x] w %#x failed: %d\n",
				AB5500_VIB_CTRL, ctrl, ret);
		return ret;
	}

	return ret;
}

static int ab5500_vibra_register_dev(struct ab5500_vibra *vibra,
		struct platform_device *pdev)
{
	int ret = 0;

	ret = timed_output_dev_register(&vibra->tdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register timed output device\n");
		goto err_out;
	}

	dev_set_drvdata(vibra->tdev.dev, vibra);


	/* Create workqueue just for timed output vibrator */
	vibra->vibra_workqueue =
		create_singlethread_workqueue("ste-timed-output-vibra");
	if (!vibra->vibra_workqueue) {
		dev_err(&pdev->dev, "failed to allocate workqueue\n");
		ret = -ENOMEM;
		goto exit_output_unregister;
	}

	init_waitqueue_head(&vibra->vibra_wait);
	INIT_WORK(&vibra->vibra_work, ab5500_vibra_work);
	spin_lock_init(&vibra->vibra_lock);

	platform_set_drvdata(pdev, vibra);

	return ret;

exit_output_unregister:
	timed_output_dev_unregister(&vibra->tdev);
err_out:
	return ret;
}

static int __devinit ab5500_vibra_probe(struct platform_device *pdev)
{
	struct ab5500_vibra_platform_data *pdata = pdev->dev.platform_data;
	struct ab5500_vibra *vibra = NULL;
	int ret = 0;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "platform data required. Quitting...\n");
		return -ENODEV;
	}

	vibra = kzalloc(sizeof(struct ab5500_vibra), GFP_KERNEL);
	if (vibra == NULL)
		return -ENOMEM;

	vibra->tdev.name = "vibrator";
	vibra->tdev.enable = vibra_enable;
	vibra->tdev.get_time = vibra_get_time;
	vibra->timeout_start = 0;
	vibra->enable = false;
	vibra->magnitude = pdata->magnitude;
	vibra->pdata = pdata;
	vibra->dev = &pdev->dev;

	if (vibra->pdata->eol_voltage) {
		vibra->gpadc = ab5500_gpadc_get("ab5500-adc.0");
		if (IS_ERR(vibra->gpadc))
			goto err_alloc;
	}

	if (vibra->pdata->type == AB5500_VIB_LINEAR)
		dev_info(&pdev->dev, "Linear Type Vibrators\n");
	else
		dev_info(&pdev->dev, "Rotary Type Vibrators\n");

	ret = ab5500_vibra_reg_init(vibra);
	if (ret < 0)
		goto err_alloc;

	ret = ab5500_vibra_register_dev(vibra, pdev);
	if (ret < 0)
		goto err_alloc;

	/* Perform vibra eol diagnostics if eol_voltage is set */
	if (vibra->pdata->eol_voltage) {
		ret = ab5500_vibra_eol_check(vibra);
		if (ret < 0)
			dev_warn(&pdev->dev, "EOL check failed\n");
	}

	dev_info(&pdev->dev, "initialization success\n");

	return ret;

err_alloc:
	kfree(vibra);

	return ret;
}

static int __devexit ab5500_vibra_remove(struct platform_device *pdev)
{
	struct ab5500_vibra *vibra = platform_get_drvdata(pdev);

	timed_output_dev_unregister(&vibra->tdev);
	destroy_workqueue(vibra->vibra_workqueue);
	kfree(vibra);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ab5500_vibra_driver = {
	.probe		= ab5500_vibra_probe,
	.remove		= __devexit_p(ab5500_vibra_remove),
	.driver		= {
		.name	= AB5500_VIBRA_DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init ab5500_vibra_module_init(void)
{
	return platform_driver_register(&ab5500_vibra_driver);
}

static void __exit ab5500_vibra_module_exit(void)
{
	platform_driver_unregister(&ab5500_vibra_driver);
}

module_init(ab5500_vibra_module_init);
module_exit(ab5500_vibra_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shreshtha Kumar SAHU <shreshthakumar.sahu@stericsson.com>");
MODULE_DESCRIPTION("Timed Output Driver for AB5500 Vibrator");

