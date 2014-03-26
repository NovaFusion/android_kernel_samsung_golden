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
#include <linux/interrupt.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/input/abx500-accdet.h>
#ifdef CONFIG_SND_SOC_UX500_AB8500
#include <sound/ux500_ab8500_ext.h>
#endif

#define MAX_DET_COUNT			10
#define MAX_VOLT_DIFF			30
#define MIN_MIC_POWER			-100

/* Unique value used to identify Headset button input device */
#define BTN_INPUT_UNIQUE_VALUE	"AB8500HsBtn"
#define BTN_INPUT_DEV_NAME	"AB8500 Hs Button"

#define DEBOUNCE_PLUG_EVENT_MS		100
#define DEBOUNCE_PLUG_RETEST_MS		25
#define DEBOUNCE_UNPLUG_EVENT_MS	0

/*
 * Register definition for accessory detection.
 */
#define AB8500_REGU_CTRL1_SPARE_REG	0x84
#define AB8500_ACC_DET_DB1_REG		0x80
#define AB8500_ACC_DET_DB2_REG		0x81
#define AB8500_ACC_DET_CTRL_REG		0x82
#define AB8500_IT_SOURCE5_REG		0x04

/* REGISTER: AB8500_ACC_DET_CTRL_REG */
#define BITS_ACCDETCTRL2_ENA		(0x20 | 0x10 | 0x08)
#define BITS_ACCDETCTRL1_ENA		(0x02 | 0x01)

/* REGISTER: AB8500_REGU_CTRL1_SPARE_REG */
#define BIT_REGUCTRL1SPARE_VAMIC1_GROUND	0x01

/* REGISTER: AB8500_IT_SOURCE5_REG */
#define BIT_ITSOURCE5_ACCDET1		0x04

/* After being loaded, how fast the first check is to be made */
#define INIT_DELAY_MS			3000

/* Voltage limits (mV) for various types of AV Accessories */
#define ACCESSORY_DET_VOL_DONTCARE	-1
#define ACCESSORY_HEADPHONE_DET_VOL_MIN	0
#define ACCESSORY_HEADPHONE_DET_VOL_MAX	40
#define ACCESSORY_CARKIT_DET_VOL_MIN	1100
#define ACCESSORY_CARKIT_DET_VOL_MAX	1300
#define ACCESSORY_HEADSET_DET_VOL_MIN	0
#define ACCESSORY_HEADSET_DET_VOL_MAX	200
#define ACCESSORY_OPENCABLE_DET_VOL_MIN	1730
#define ACCESSORY_OPENCABLE_DET_VOL_MAX	2150

/* Static data initialization */

static struct accessory_regu_descriptor ab8500_regu_desc[3] = {
	{
		.id = REGULATOR_VAUDIO,
		.name = "v-audio",
	},
	{
		.id = REGULATOR_VAMIC1,
		.name = "v-amic1",
	},
	{
		.id = REGULATOR_AVSWITCH,
		.name = "vcc-N2158",
	},
};

static struct accessory_irq_descriptor ab8500_irq_desc_norm[] = {
	{
		.irq = PLUG_IRQ,
		.name = "ACC_DETECT_1DB_F",
		.isr = plug_irq_handler,
	},
	{
		.irq = UNPLUG_IRQ,
		.name = "ACC_DETECT_1DB_R",
		.isr = unplug_irq_handler,
	},
	{
		.irq = BUTTON_PRESS_IRQ,
		.name = "ACC_DETECT_22DB_F",
		.isr = button_press_irq_handler,
	},
	{
		.irq = BUTTON_RELEASE_IRQ,
		.name = "ACC_DETECT_22DB_R",
		.isr = button_release_irq_handler,
	},
};

static struct accessory_irq_descriptor ab8500_irq_desc_inverted[] = {
	{
		.irq = PLUG_IRQ,
		.name = "ACC_DETECT_1DB_R",
		.isr = plug_irq_handler,
	},
	{
		.irq = UNPLUG_IRQ,
		.name = "ACC_DETECT_1DB_F",
		.isr = unplug_irq_handler,
	},
	{
		.irq = BUTTON_PRESS_IRQ,
		.name = "ACC_DETECT_22DB_R",
		.isr = button_press_irq_handler,
	},
	{
		.irq = BUTTON_RELEASE_IRQ,
		.name = "ACC_DETECT_22DB_F",
		.isr = button_release_irq_handler,
	},
};

/*
 * configures accdet2 input on/off
 */
static void ab8500_config_accdetect2_hw(struct abx500_ad *dd, int enable)
{
	int ret = 0;

	if (!dd->accdet2_th_set) {
		/* Configure accdetect21+22 thresholds */
		ret = abx500_set_register_interruptible(&dd->pdev->dev,
				AB8500_ECI_AV_ACC,
				AB8500_ACC_DET_DB2_REG,
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
			AB8500_ECI_AV_ACC,
			AB8500_ACC_DET_CTRL_REG,
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
static void ab8500_config_accdetect1_hw(struct abx500_ad *dd, int enable)
{
	int ret;

	if (!dd->accdet1_th_set) {
		ret = abx500_set_register_interruptible(&dd->pdev->dev,
				AB8500_ECI_AV_ACC,
				AB8500_ACC_DET_DB1_REG,
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
				AB8500_ECI_AV_ACC,
				AB8500_ACC_DET_CTRL_REG,
				BITS_ACCDETCTRL1_ENA,
				enable ? BITS_ACCDETCTRL1_ENA : 0);

	if (ret < 0)
		dev_err(&dd->pdev->dev,
			"%s: Failed to update reg (%d).\n", __func__, ret);
}

/*
 * returns the high level status whether some accessory is connected (1|0).
 */
static int ab8500_detect_plugged_in(struct abx500_ad *dd)
{
	u8 value = 0;

	int status = abx500_get_register_interruptible(
				&dd->pdev->dev,
				AB8500_INTERRUPT,
				AB8500_IT_SOURCE5_REG,
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

#ifdef CONFIG_SND_SOC_UX500_AB8500

/*
 * meas_voltage_stable - measures relative stable voltage from spec. input
 */
static int ab8500_meas_voltage_stable(struct abx500_ad *dd)
{
	int ret, mv;

	ret = ux500_ab8500_audio_gpadc_measure((struct ab8500_gpadc *)dd->gpadc,
			ACC_DETECT2, false, &mv);

	return (ret < 0) ? ret : mv;
}

/*
 * meas_alt_voltage_stable - measures relative stable voltage from spec. input
 */
static int ab8500_meas_alt_voltage_stable(struct abx500_ad *dd)
{
	int ret, mv;

	ret = ux500_ab8500_audio_gpadc_measure((struct ab8500_gpadc *)dd->gpadc,
			ACC_DETECT2, true, &mv);

	return (ret < 0) ? ret : mv;
}

#else

/*
 * meas_voltage_stable - measures relative stable voltage from spec. input
 */
static int ab8500_meas_voltage_stable(struct abx500_ad *dd)
{
	int iterations = 2;
	int v1, v2, dv;

	v1 = ab8500_gpadc_convert((struct ab8500_gpadc *)dd->gpadc,
			ACC_DETECT2);
	do {
		msleep(1);
		--iterations;
		v2 = ab8500_gpadc_convert((struct ab8500_gpadc *)dd->gpadc,
				ACC_DETECT2);
		dv = abs(v2 - v1);
		v1 = v2;
	} while (iterations > 0 && dv > MAX_VOLT_DIFF);

	return v1;
}

/*
 * not implemented for non soc setups
 */
static int ab8500_meas_alt_voltage_stable(struct abx500_ad *dd)
{
	return -1;
}

#endif

/*
 * configures HW so that it is possible to make decision whether
 * accessory is connected or not.
 */
static void ab8500_config_hw_test_plug_connected(struct abx500_ad *dd,
		int enable)
{
	int ret;

	dev_dbg(&dd->pdev->dev, "%s:%d\n", __func__, enable);

	ret = ab8500_config_pulldown(&dd->pdev->dev,
					dd->pdata->video_ctrl_gpio, !enable);
	if (ret < 0) {
		dev_err(&dd->pdev->dev,
			"%s: Failed to update reg (%d).\n", __func__, ret);
		return;
	}

	if (enable)
		accessory_regulator_enable(dd, REGULATOR_VAMIC1);
}

/*
 * configures HW so that carkit/headset detection can be accomplished.
 */
static void ab8500_config_hw_test_basic_carkit(struct abx500_ad *dd, int enable)
{
	int ret;

	dev_dbg(&dd->pdev->dev, "%s:%d\n", __func__, enable);

	if (enable)
		accessory_regulator_disable(dd, REGULATOR_VAMIC1);

	/* Un-Ground the VAMic1 output when enabled */
	ret = abx500_mask_and_set_register_interruptible(
				&dd->pdev->dev,
				AB8500_REGU_CTRL1,
				AB8500_REGU_CTRL1_SPARE_REG,
				BIT_REGUCTRL1SPARE_VAMIC1_GROUND,
				enable ? BIT_REGUCTRL1SPARE_VAMIC1_GROUND : 0);
	if (ret < 0)
		dev_err(&dd->pdev->dev,
			"%s: Failed to update reg (%d).\n", __func__, ret);
}

/*
 * sets the av switch direction - audio-in vs video-out
 */
static void ab8500_set_av_switch(struct abx500_ad *dd,
		enum accessory_avcontrol_dir dir, bool nahj_headset)
{
	int ret = 0;

	dev_dbg(&dd->pdev->dev, "%s: Enter (%d)\n", __func__, dir);
	if (dir == NOT_SET) {
		if (dd->pdata->video_ctrl_gpio)
			ret = gpio_direction_output(dd->pdata->video_ctrl_gpio,
					0);
		if (dd->pdata->mic_ctrl)
			ret = gpio_direction_output(dd->pdata->mic_ctrl, 0);
		if (dd->pdata->nahj_ctrl)
			ret = gpio_direction_output(dd->pdata->nahj_ctrl, 0);
		if (ret < 0)
			dev_err(&dd->pdev->dev, "Direction set failed\n");
	} else if (dir == AUDIO_IN) {
		ret = gpio_direction_output(dd->pdata->video_ctrl_gpio,
				dd->pdata->video_ctrl_gpio_inverted ? 0 : 1);
		if (ret < 0) {
			dev_err(&dd->pdev->dev,
				"%s: video_ctrl pin output config failed (%d).\n",
								__func__, ret);
			return;
		}

		if (dd->pdata->mic_ctrl) {
			ret = gpio_direction_output(dd->pdata->mic_ctrl, 1);
			if (ret < 0) {
				dev_err(&dd->pdev->dev,
						"%s: mic_ctrl pin output"
						"config failed (%d).\n",
						__func__, ret);
				return;
			}
		}

		if (dd->pdata->nahj_ctrl) {
			ret = gpio_direction_output(dd->pdata->nahj_ctrl,
					nahj_headset ? 0 : 1);
			if (ret < 0) {
				dev_err(&dd->pdev->dev,
						"%s: nahj_ctrl pin output"
						"config failed (%d).\n",
						__func__, ret);
				return;
			}
		}

		dev_dbg(&dd->pdev->dev, "AV-SWITCH: %s\n",
			dir == AUDIO_IN ? "AUDIO_IN" : "VIDEO_OUT");
	} else {
		dev_err(&dd->pdev->dev, "Wrong option\n");
	}
}

static u8 acc_det_ctrl_suspend_val;

static void ab8500_turn_off_accdet_comparator(struct platform_device *pdev)
{
	struct abx500_ad *dd = platform_get_drvdata(pdev);

	/* Turn off AccDetect comparators and pull-up */
	(void) abx500_get_register_interruptible(
			&dd->pdev->dev,
			AB8500_ECI_AV_ACC,
			AB8500_ACC_DET_CTRL_REG,
			&acc_det_ctrl_suspend_val);
	(void) abx500_set_register_interruptible(
			&dd->pdev->dev,
			AB8500_ECI_AV_ACC,
			AB8500_ACC_DET_CTRL_REG,
			0);

}

static void ab8500_turn_on_accdet_comparator(struct platform_device *pdev)
{
	struct abx500_ad *dd = platform_get_drvdata(pdev);

	/* Turn on AccDetect comparators and pull-up */
	(void) abx500_set_register_interruptible(
			&dd->pdev->dev,
			AB8500_ECI_AV_ACC,
			AB8500_ACC_DET_CTRL_REG,
			acc_det_ctrl_suspend_val);

}

static void *ab8500_accdet_abx500_gpadc_get(void)
{
	return ab8500_gpadc_get();
}

struct abx500_accdet_platform_data *
	ab8500_get_platform_data(struct platform_device *pdev)
{
	struct ab8500_platform_data *plat;

	plat = dev_get_platdata(pdev->dev.parent);

	if (!plat || !plat->accdet) {
		dev_err(&pdev->dev, "%s: Failed to get accdet plat data.\n",
			__func__);
		return ERR_PTR(-ENODEV);
	}

	return plat->accdet;
}

struct abx500_ad ab8500_accessory_det_callbacks = {
	.irq_desc_norm			= ab8500_irq_desc_norm,
	.irq_desc_inverted		= ab8500_irq_desc_inverted,
	.no_irqs			= ARRAY_SIZE(ab8500_irq_desc_norm),
	.regu_desc			= ab8500_regu_desc,
	.no_of_regu_desc		= ARRAY_SIZE(ab8500_regu_desc),
	.config_accdetect2_hw		= ab8500_config_accdetect2_hw,
	.config_accdetect1_hw		= ab8500_config_accdetect1_hw,
	.detect_plugged_in		= ab8500_detect_plugged_in,
	.meas_voltage_stable		= ab8500_meas_voltage_stable,
	.meas_alt_voltage_stable	= ab8500_meas_alt_voltage_stable,
	.config_hw_test_basic_carkit	= ab8500_config_hw_test_basic_carkit,
	.turn_off_accdet_comparator	= ab8500_turn_off_accdet_comparator,
	.turn_on_accdet_comparator	= ab8500_turn_on_accdet_comparator,
	.accdet_abx500_gpadc_get	= ab8500_accdet_abx500_gpadc_get,
	.config_hw_test_plug_connected	= ab8500_config_hw_test_plug_connected,
	.set_av_switch			= ab8500_set_av_switch,
	.get_platform_data		= ab8500_get_platform_data,
};

MODULE_DESCRIPTION("AB8500 AV Accessory detection driver");
MODULE_ALIAS("platform:ab8500-acc-det");
MODULE_AUTHOR("ST-Ericsson");
MODULE_LICENSE("GPL v2");
