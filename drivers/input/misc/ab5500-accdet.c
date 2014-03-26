/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Jarmo K. Kuronen <jarmo.kuronen@symbio.com>
 *         for ST-Ericsson.
 *
 * License terms: GPL V2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>
#include <linux/mfd/abx500.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input/abx500-accdet.h>

/*
 * Register definition for accessory detection.
 */
#define AB5500_REGU_CTRL1_SPARE_REG	0x84
#define AB5500_ACC_DET_DB1_REG		0x20
#define AB5500_ACC_DET_DB2_REG		0x21
#define AB5500_ACC_DET_CTRL_REG		0x23
#define AB5500_VDENC_CTRL0		0x80

/* REGISTER: AB8500_ACC_DET_CTRL_REG */
#define BITS_ACCDETCTRL2_ENA		(0x20 | 0x10 | 0x08)
#define BITS_ACCDETCTRL1_ENA		(0x02 | 0x01)

/* REGISTER: AB8500_REGU_CTRL1_SPARE_REG */
#define BIT_REGUCTRL1SPARE_VAMIC1_GROUND	0x01

/* REGISTER: AB8500_IT_SOURCE5_REG */
#define BIT_ITSOURCE5_ACCDET1		0x02

static struct accessory_irq_descriptor ab5500_irq_desc[] = {
	{
		.irq = PLUG_IRQ,
		.name = "acc_detedt1db_falling",
		.isr = plug_irq_handler,
	},
	{
		.irq = UNPLUG_IRQ,
		.name = "acc_detedt1db_rising",
		.isr = unplug_irq_handler,
	},
	{
		.irq = BUTTON_PRESS_IRQ,
		.name = "acc_detedt21db_falling",
		.isr = button_press_irq_handler,
	},
	{
		.irq = BUTTON_RELEASE_IRQ,
		.name = "acc_detedt21db_rising",
		.isr = button_release_irq_handler,
	},
};

static struct accessory_regu_descriptor ab5500_regu_desc[] = {
	{
		.id = REGULATOR_VAMIC1,
		.name = "v-amic",
	},
};


/*
 * configures accdet2 input on/off
 */
static void ab5500_config_accdetect2_hw(struct abx500_ad *dd, int enable)
{
	int ret = 0;

	if (!dd->accdet2_th_set) {
		/* Configure accdetect21+22 thresholds */
		ret = abx500_set_register_interruptible(&dd->pdev->dev,
				AB5500_BANK_FG_BATTCOM_ACC,
				AB5500_ACC_DET_DB2_REG,
				dd->pdata->accdet2122_th);
		if (ret < 0) {
			dev_err(&dd->pdev->dev,
				"%s: Failed to write reg (%d).\n", __func__,
				ret);
			goto out;
		} else {
			dd->accdet2_th_set = 1;
		}
	}

	/* Enable/Disable accdetect21 comparators + pullup */
	ret = abx500_mask_and_set_register_interruptible(
			&dd->pdev->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_ACC_DET_CTRL_REG,
			BITS_ACCDETCTRL2_ENA,
			enable ? BITS_ACCDETCTRL2_ENA : 0);

	if (ret < 0)
		dev_err(&dd->pdev->dev, "%s: Failed to update reg (%d).\n",
				__func__, ret);
out:
	return;
}

/*
 * configures accdet1 input on/off
 */
static void ab5500_config_accdetect1_hw(struct abx500_ad *dd, int enable)
{
	int ret;

	if (!dd->accdet1_th_set) {
		ret = abx500_set_register_interruptible(&dd->pdev->dev,
				AB5500_BANK_FG_BATTCOM_ACC,
				AB5500_ACC_DET_DB1_REG,
				dd->pdata->accdet1_dbth);
		if (ret < 0)
			dev_err(&dd->pdev->dev,
				"%s: Failed to write reg (%d).\n", __func__,
				ret);
		else
			dd->accdet1_th_set = 1;
	}

	/* enable accdetect1 comparator */
	ret = abx500_mask_and_set_register_interruptible(
				&dd->pdev->dev,
				AB5500_BANK_FG_BATTCOM_ACC,
				AB5500_ACC_DET_CTRL_REG,
				BITS_ACCDETCTRL1_ENA,
				enable ? BITS_ACCDETCTRL1_ENA : 0);

	if (ret < 0)
		dev_err(&dd->pdev->dev,
			"%s: Failed to update reg (%d).\n", __func__, ret);
}

/*
 * returns the high level status whether some accessory is connected (1|0).
 */
static int ab5500_detect_plugged_in(struct abx500_ad *dd)
{
	u8 value = 0;

	int status = abx500_get_register_interruptible(
				&dd->pdev->dev,
				AB5500_BANK_IT,
				AB5500_IT_SOURCE3_REG,
				&value);
	if (status < 0) {
		dev_err(&dd->pdev->dev, "%s: reg read failed (%d).\n",
			__func__, status);
		return 0;
	}

	if (dd->pdata->is_detection_inverted)
		return value & BIT_ITSOURCE5_ACCDET1 ? 1 : 0;
	else
		return value & BIT_ITSOURCE5_ACCDET1 ? 0 : 1;
}

/*
 * mic_line_voltage_stable - measures a relative stable voltage from spec. input
 */
static int ab5500_meas_voltage_stable(struct abx500_ad *dd)
{
	int iterations = 2;
	int v1, v2, dv;

	v1 = ab5500_gpadc_convert((struct ab5500_gpadc *)dd->gpadc,
			ACC_DETECT2);
	do {
		msleep(1);
		--iterations;
		v2 = ab5500_gpadc_convert((struct ab5500_gpadc *)dd->gpadc,
				ACC_DETECT2);
		dv = abs(v2 - v1);
		v1 = v2;
	} while (iterations > 0 && dv > MAX_VOLT_DIFF);

	return v1;
}

/*
 * not implemented
 */
static int ab5500_meas_alt_voltage_stable(struct abx500_ad *dd)
{
	return -1;
}

/*
 * configures HW so that it is possible to make decision whether
 * accessory is connected or not.
 */
static void ab5500_config_hw_test_plug_connected(struct abx500_ad *dd,
		int enable)
{
	dev_dbg(&dd->pdev->dev, "%s:%d\n", __func__, enable);

	/* enable mic BIAS2 */
	if (enable)
		accessory_regulator_enable(dd, REGULATOR_VAMIC1);
}

/*
 * configures HW so that carkit/headset detection can be accomplished.
 */
static void ab5500_config_hw_test_basic_carkit(struct abx500_ad *dd, int enable)
{
	/* enable mic BIAS2 */
	if (enable)
		accessory_regulator_disable(dd, REGULATOR_VAMIC1);
}

static u8 acc_det_ctrl_suspend_val;

static void ab5500_turn_off_accdet_comparator(struct platform_device *pdev)
{
	struct abx500_ad *dd = platform_get_drvdata(pdev);

	/* Turn off AccDetect comparators and pull-up */
	(void) abx500_get_register_interruptible(
			&dd->pdev->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_ACC_DET_CTRL_REG,
			&acc_det_ctrl_suspend_val);
	(void) abx500_set_register_interruptible(
			&dd->pdev->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_ACC_DET_CTRL_REG,
			0);
}

static void ab5500_turn_on_accdet_comparator(struct platform_device *pdev)
{
	struct abx500_ad *dd = platform_get_drvdata(pdev);

	/* Turn on AccDetect comparators and pull-up */
	(void) abx500_set_register_interruptible(
			&dd->pdev->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_ACC_DET_CTRL_REG,
			acc_det_ctrl_suspend_val);
}

static void *ab5500_accdet_abx500_gpadc_get(void)
{
	return ab5500_gpadc_get("ab5500-adc.0");
}

struct abx500_accdet_platform_data *
	ab5500_get_platform_data(struct platform_device *pdev)
{
	return pdev->dev.platform_data;
}

struct abx500_ad ab5500_accessory_det_callbacks = {
	.irq_desc_norm			= ab5500_irq_desc,
	.irq_desc_inverted		= NULL,
	.no_irqs			= ARRAY_SIZE(ab5500_irq_desc),
	.regu_desc			= ab5500_regu_desc,
	.no_of_regu_desc		= ARRAY_SIZE(ab5500_regu_desc),
	.config_accdetect2_hw		= ab5500_config_accdetect2_hw,
	.config_accdetect1_hw		= ab5500_config_accdetect1_hw,
	.detect_plugged_in		= ab5500_detect_plugged_in,
	.meas_voltage_stable		= ab5500_meas_voltage_stable,
	.meas_alt_voltage_stable	= ab5500_meas_alt_voltage_stable,
	.config_hw_test_basic_carkit	= ab5500_config_hw_test_basic_carkit,
	.turn_off_accdet_comparator	= ab5500_turn_off_accdet_comparator,
	.turn_on_accdet_comparator	= ab5500_turn_on_accdet_comparator,
	.accdet_abx500_gpadc_get	= ab5500_accdet_abx500_gpadc_get,
	.config_hw_test_plug_connected	= ab5500_config_hw_test_plug_connected,
	.set_av_switch			= NULL,
	.get_platform_data		= ab5500_get_platform_data,
};

MODULE_LICENSE("GPL v2");
