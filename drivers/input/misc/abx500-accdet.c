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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/input/abx500-accdet.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/mfd/abx500.h>

#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/jack.h>

#ifdef CONFIG_SND_SOC_UX500_AB8500
#include <sound/ux500_ab8500.h>
#else
#define ux500_ab8500_jack_report(i)
#endif

/* Unique value used to identify Headset button input device */
#define BTN_INPUT_UNIQUE_VALUE	"AB8500HsBtn"
#define BTN_INPUT_DEV_NAME	"AB8500 Hs Button"

#define DEBOUNCE_PLUG_EVENT_MS		100
#define DEBOUNCE_PLUG_RETEST_MS		25
#define DEBOUNCE_UNPLUG_EVENT_MS	0

/* After being loaded, how fast the first check is to be made */
#define INIT_DELAY_MS			3000

/* Voltage limits (mV) for various types of AV Accessories */
#define ACCESSORY_DET_VOL_DONTCARE	-1
#define ACCESSORY_HEADPHONE_DET_VOL_MIN	0
#define ACCESSORY_HEADPHONE_DET_VOL_MAX	40
#define ACCESSORY_U_HEADSET_DET_VOL_MIN	47
#define ACCESSORY_U_HEADSET_DET_VOL_MAX	732
#define ACCESSORY_U_HEADSET_ALT_DET_VOL_MIN	25
#define ACCESSORY_U_HEADSET_ALT_DET_VOL_MAX	50
#define ACCESSORY_CARKIT_DET_VOL_MIN	1100
#define ACCESSORY_CARKIT_DET_VOL_MAX	1300
#define ACCESSORY_HEADSET_DET_VOL_MIN	1301
#define ACCESSORY_HEADSET_DET_VOL_MAX	2000
#define ACCESSORY_OPENCABLE_DET_VOL_MIN	2001
#define ACCESSORY_OPENCABLE_DET_VOL_MAX	2150


/* Macros */

/*
 * Conviniency macros to check jack characteristics.
 */
#define jack_supports_mic(type) \
	(type == JACK_TYPE_HEADSET || type == JACK_TYPE_CARKIT)
#define jack_supports_spkr(type) \
	((type != JACK_TYPE_DISCONNECTED) && (type != JACK_TYPE_CONNECTED))
#define jack_supports_buttons(type) \
	((type == JACK_TYPE_HEADSET) ||\
	(type == JACK_TYPE_CARKIT) ||\
	(type == JACK_TYPE_OPENCABLE) ||\
	(type == JACK_TYPE_CONNECTED))


/* Forward declarations */
static void config_accdetect(struct abx500_ad *dd);
static enum accessory_jack_type detect(struct abx500_ad *dd, int *required_det);

/* Static data initialization */
static struct accessory_detect_task detect_ops[] = {
	{
		.type = JACK_TYPE_DISCONNECTED,
		.typename = "DISCONNECTED",
		.meas_mv = 1,
		.req_det_count = 1,
		.minvol = ACCESSORY_DET_VOL_DONTCARE,
		.maxvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_minvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_maxvol = ACCESSORY_DET_VOL_DONTCARE
	},
	{
		.type = JACK_TYPE_HEADPHONE,
		.typename = "HEADPHONE",
		.meas_mv = 1,
		.req_det_count = 1,
		.minvol = ACCESSORY_HEADPHONE_DET_VOL_MIN,
		.maxvol = ACCESSORY_HEADPHONE_DET_VOL_MAX,
		.alt_minvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_maxvol = ACCESSORY_DET_VOL_DONTCARE
	},
	{
		.type = JACK_TYPE_UNSUPPORTED_HEADSET,
		.typename = "UNSUPPORTED HEADSET",
		.meas_mv = 1,
		.req_det_count = 2,
		.minvol = ACCESSORY_U_HEADSET_DET_VOL_MIN,
		.maxvol = ACCESSORY_U_HEADSET_DET_VOL_MAX,
		.alt_minvol = ACCESSORY_U_HEADSET_ALT_DET_VOL_MIN,
		.alt_maxvol = ACCESSORY_U_HEADSET_ALT_DET_VOL_MAX,
		.nahj_headset = true,
	},
	{
		.type = JACK_TYPE_OPENCABLE,
		.typename = "OPENCABLE",
		.meas_mv = 0,
		.req_det_count = 4,
		.minvol = ACCESSORY_OPENCABLE_DET_VOL_MIN,
		.maxvol = ACCESSORY_OPENCABLE_DET_VOL_MAX,
		.alt_minvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_maxvol = ACCESSORY_DET_VOL_DONTCARE
	},
	{
		.type = JACK_TYPE_CARKIT,
		.typename = "CARKIT",
		.meas_mv = 1,
		.req_det_count = 1,
		.minvol = ACCESSORY_CARKIT_DET_VOL_MIN,
		.maxvol = ACCESSORY_CARKIT_DET_VOL_MAX,
		.alt_minvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_maxvol = ACCESSORY_DET_VOL_DONTCARE
	},
	{
		.type = JACK_TYPE_HEADSET,
		.typename = "HEADSET",
		.meas_mv = 0,
		.req_det_count = 2,
		.minvol = ACCESSORY_HEADSET_DET_VOL_MIN,
		.maxvol = ACCESSORY_HEADSET_DET_VOL_MAX,
		.alt_minvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_maxvol = ACCESSORY_DET_VOL_DONTCARE
	},
	{
		.type = JACK_TYPE_CONNECTED,
		.typename = "CONNECTED",
		.meas_mv = 0,
		.req_det_count = 4,
		.minvol = ACCESSORY_DET_VOL_DONTCARE,
		.maxvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_minvol = ACCESSORY_DET_VOL_DONTCARE,
		.alt_maxvol = ACCESSORY_DET_VOL_DONTCARE
	}
};

static struct accessory_irq_descriptor *abx500_accdet_irq_desc;

/*
 * textual represenation of the accessory type
 */
static const char *accessory_str(enum accessory_jack_type type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(detect_ops); i++)
		if (type == detect_ops[i].type)
			return detect_ops[i].typename;

	return "UNKNOWN?";
}

/*
 * enables regulator but only if it has not been enabled earlier.
 */
void accessory_regulator_enable(struct abx500_ad *dd,
		enum accessory_regulator reg)
{
	int i;

	for (i = 0; i < dd->no_of_regu_desc; i++) {
		if (reg & dd->regu_desc[i].id) {
			if (!dd->regu_desc[i].enabled) {
				if (!regulator_enable(dd->regu_desc[i].handle))
					dd->regu_desc[i].enabled = 1;
			}
		}
	}
}

/*
 * disables regulator but only if it has been previously enabled.
 */
void accessory_regulator_disable(struct abx500_ad *dd,
		enum accessory_regulator reg)
{
	int i;

	for (i = 0; i < dd->no_of_regu_desc; i++) {
		if (reg & dd->regu_desc[i].id) {
			if (dd->regu_desc[i].enabled) {
				if (!regulator_disable(dd->regu_desc[i].handle))
					dd->regu_desc[i].enabled = 0;
			}
		}
	}
}

/*
 * frees previously retrieved regulators.
 */
static void free_regulators(struct abx500_ad *dd)
{
	int i;

	for (i = 0; i < dd->no_of_regu_desc; i++) {
		if (dd->regu_desc[i].handle) {
			regulator_put(dd->regu_desc[i].handle);
			dd->regu_desc[i].handle = NULL;
		}
	}
}

/*
 * gets required regulators.
 */
static int create_regulators(struct abx500_ad *dd)
{
	int i;
	int status = 0;

	for (i = 0; i < dd->no_of_regu_desc; i++) {
		struct regulator *regu =
			regulator_get(&dd->pdev->dev, dd->regu_desc[i].name);
		if (IS_ERR(regu)) {
			status = PTR_ERR(regu);
			dev_err(&dd->pdev->dev,
				"%s: Failed to get supply '%s' (%d).\n",
				__func__, dd->regu_desc[i].name, status);
			free_regulators(dd);
			goto out;
		} else {
			dd->regu_desc[i].handle = regu;
		}
	}

out:
	return status;
}

/*
 * create input device for button press reporting
 */
static int create_btn_input_dev(struct abx500_ad *dd)
{
	int err;

	dd->btn_input_dev = input_allocate_device();
	if (!dd->btn_input_dev) {
		dev_err(&dd->pdev->dev, "%s: Failed to allocate input dev.\n",
			__func__);
		err = -ENOMEM;
		goto out;
	}

	input_set_capability(dd->btn_input_dev,
				EV_KEY,
				dd->pdata->btn_keycode);

	dd->btn_input_dev->name = BTN_INPUT_DEV_NAME;
	dd->btn_input_dev->uniq = BTN_INPUT_UNIQUE_VALUE;
	dd->btn_input_dev->dev.parent = &dd->pdev->dev;

	err = input_register_device(dd->btn_input_dev);
	if (err) {
		dev_err(&dd->pdev->dev,
			"%s: register_input_device failed (%d).\n", __func__,
			err);
		input_free_device(dd->btn_input_dev);
		dd->btn_input_dev = NULL;
		goto out;
	}
out:
	return err;
}

/*
 * reports jack status
 */
void report_jack_status(struct abx500_ad *dd)
{
	int value = 0;

	/* Never report possible open cable */
	if (dd->jack_type == JACK_TYPE_OPENCABLE)
		goto out;

	/* Never report same state twice in a row */
	if (dd->jack_type == dd->reported_jack_type)
		goto out;
	dd->reported_jack_type = dd->jack_type;

	dev_dbg(&dd->pdev->dev, "Accessory: %s\n",
		accessory_str(dd->jack_type));

	/* Never report unsupported headset */
	if (dd->jack_type == JACK_TYPE_UNSUPPORTED_HEADSET)
		goto out;

	if (dd->jack_type != JACK_TYPE_DISCONNECTED &&
		dd->jack_type != JACK_TYPE_UNSPECIFIED)
		value |= SND_JACK_MECHANICAL;
	if (jack_supports_mic(dd->jack_type))
		value |= SND_JACK_MICROPHONE;
	if (jack_supports_spkr(dd->jack_type))
		value |= (SND_JACK_HEADPHONE | SND_JACK_LINEOUT);
	if (dd->jack_type == JACK_TYPE_DISCONNECTED)
		set_android_switch_state(0);
	else
		set_android_switch_state(1);

	ux500_ab8500_jack_report(value);

out: return;
}

/*
 * worker routine to handle accessory unplug case
 */
void unplug_irq_handler_work(struct work_struct *work)
{
	struct abx500_ad *dd = container_of(work,
		struct abx500_ad, unplug_irq_work.work);

	dev_dbg(&dd->pdev->dev, "%s: Enter\n", __func__);

	dd->jack_type = dd->jack_type_temp = JACK_TYPE_DISCONNECTED;
	dd->jack_det_count = dd->total_jack_det_count = 0;
	dd->btn_state = BUTTON_UNK;
	config_accdetect(dd);

	accessory_regulator_disable(dd, REGULATOR_ALL);

	report_jack_status(dd);
}

/*
 * interrupt service routine for accessory unplug.
 */
irqreturn_t unplug_irq_handler(int irq, void *_userdata)
{
	struct abx500_ad *dd = _userdata;

	dev_dbg(&dd->pdev->dev, "%s: Enter (irq=%d)\n", __func__, irq);

	queue_delayed_work(dd->irq_work_queue, &dd->unplug_irq_work,
			msecs_to_jiffies(DEBOUNCE_UNPLUG_EVENT_MS));

	return IRQ_HANDLED;
}

/*
 * interrupt service routine for accessory plug.
 */
irqreturn_t plug_irq_handler(int irq, void *_userdata)
{
	struct abx500_ad *dd = _userdata;

	dev_dbg(&dd->pdev->dev, "%s: Enter (irq=%d)\n",
		__func__, irq);

	switch (dd->jack_type) {
	case JACK_TYPE_DISCONNECTED:
	case JACK_TYPE_UNSPECIFIED:
		queue_delayed_work(dd->irq_work_queue, &dd->detect_work,
			msecs_to_jiffies(DEBOUNCE_PLUG_EVENT_MS));
		break;

	default:
		dev_err(&dd->pdev->dev, "%s: Unexpected plug IRQ\n", __func__);
		break;
	}

	return IRQ_HANDLED;
}

/*
 * worker routine to perform detection.
 */
static void detect_work(struct work_struct *work)
{
	int req_det_count = 1;
	enum accessory_jack_type new_type;
	struct abx500_ad *dd = container_of(work,
		struct abx500_ad, detect_work.work);

	dev_dbg(&dd->pdev->dev, "%s: Enter\n", __func__);

	if (dd->set_av_switch)
		dd->set_av_switch(dd, AUDIO_IN, false);

	new_type = detect(dd, &req_det_count);

	dd->total_jack_det_count++;
	if (dd->jack_type_temp == new_type) {
		dd->jack_det_count++;
	} else {
		dd->jack_det_count = 1;
		dd->jack_type_temp = new_type;
	}

	if (dd->total_jack_det_count >= MAX_DET_COUNT) {
		dev_err(&dd->pdev->dev,
			"%s: MAX_DET_COUNT(=%d) reached. Bailing out.\n",
					__func__, MAX_DET_COUNT);
		queue_delayed_work(dd->irq_work_queue, &dd->unplug_irq_work,
				msecs_to_jiffies(DEBOUNCE_UNPLUG_EVENT_MS));
	} else if (dd->jack_det_count >= req_det_count) {
		dd->total_jack_det_count = dd->jack_det_count = 0;
		dd->jack_type = new_type;
		dd->detect_jiffies = jiffies;
		report_jack_status(dd);
		config_accdetect(dd);
	} else {
		queue_delayed_work(dd->irq_work_queue,
				&dd->detect_work,
				msecs_to_jiffies(DEBOUNCE_PLUG_RETEST_MS));
	}
}

/*
 * reports a button event (pressed, released).
 */
static void report_btn_event(struct abx500_ad *dd, int down)
{
	input_report_key(dd->btn_input_dev, dd->pdata->btn_keycode, down);
	input_sync(dd->btn_input_dev);

	dev_dbg(&dd->pdev->dev, "HS-BTN: %s\n", down ? "PRESSED" : "RELEASED");
}

/*
 * interrupt service routine invoked when hs button is pressed down.
 */
irqreturn_t button_press_irq_handler(int irq, void *_userdata)
{
	struct abx500_ad *dd = _userdata;

	unsigned long accept_jiffies = dd->detect_jiffies +
			msecs_to_jiffies(1000);
	if (time_before(jiffies, accept_jiffies)) {
		dev_dbg(&dd->pdev->dev, "%s: Skipped spurious btn press.\n",
			__func__);
		return IRQ_HANDLED;
	}

	dev_dbg(&dd->pdev->dev, "%s: Enter (irq=%d)\n", __func__, irq);

	if (dd->jack_type == JACK_TYPE_OPENCABLE) {
		/* Someting got connected to open cable -> detect.. */
		dd->config_accdetect2_hw(dd, 0);
		queue_delayed_work(dd->irq_work_queue, &dd->detect_work,
			msecs_to_jiffies(DEBOUNCE_PLUG_EVENT_MS));
		return IRQ_HANDLED;
	}

	if (dd->btn_state == BUTTON_PRESSED)
		return IRQ_HANDLED;

	if (jack_supports_buttons(dd->jack_type)) {
		dd->btn_state = BUTTON_PRESSED;
		report_btn_event(dd, 1);
	} else {
		dd->btn_state = BUTTON_UNK;
	}

	return IRQ_HANDLED;
}

/*
 * interrupts service routine invoked when hs button is released.
 */
irqreturn_t button_release_irq_handler(int irq, void *_userdata)
{
	struct abx500_ad *dd = _userdata;

	dev_dbg(&dd->pdev->dev, "%s: Enter (irq=%d)\n", __func__, irq);

	if (dd->jack_type == JACK_TYPE_OPENCABLE)
		return IRQ_HANDLED;

	if (dd->btn_state != BUTTON_PRESSED)
		return IRQ_HANDLED;

	if (jack_supports_buttons(dd->jack_type)) {
		report_btn_event(dd, 0);
		dd->btn_state = BUTTON_RELEASED;
	} else {
		dd->btn_state = BUTTON_UNK;
	}

	return IRQ_HANDLED;
}

/*
 * checks whether measured voltage is in given range. depending on arguments,
 * voltage might be re-measured or previously measured voltage is reused.
 */
static int mic_vol_in_range(struct abx500_ad *dd,
			int lo, int hi, int alt_lo, int alt_hi, int force_read)
{
	static int mv = MIN_MIC_POWER;
	static int alt_mv = MIN_MIC_POWER;

	if (mv == MIN_MIC_POWER || force_read)
		mv = dd->meas_voltage_stable(dd);

	if (mv < lo || mv > hi)
		return 0;

	if (ACCESSORY_DET_VOL_DONTCARE == alt_lo &&
		ACCESSORY_DET_VOL_DONTCARE == alt_hi)
		return 1;

	if (alt_mv == MIN_MIC_POWER || force_read)
		alt_mv = dd->meas_alt_voltage_stable(dd);

	if (alt_mv < alt_lo || alt_mv > alt_hi)
		return 0;

	return 1;
}

/*
 * checks whether the currently connected HW is of given type.
 */
static int detect_hw(struct abx500_ad *dd,
			struct accessory_detect_task *task)
{
	int status;

	switch (task->type) {
	case JACK_TYPE_DISCONNECTED:
		dd->config_hw_test_plug_connected(dd, 1);
		status = !dd->detect_plugged_in(dd);
		break;
	case JACK_TYPE_CONNECTED:
		dd->config_hw_test_plug_connected(dd, 1);
		status = dd->detect_plugged_in(dd);
		break;
	case JACK_TYPE_CARKIT:
	case JACK_TYPE_HEADPHONE:
	case JACK_TYPE_HEADSET:
	case JACK_TYPE_UNSUPPORTED_HEADSET:
	case JACK_TYPE_OPENCABLE:
		status = mic_vol_in_range(dd,
					task->minvol,
					task->maxvol,
					task->alt_minvol,
					task->alt_maxvol,
					task->meas_mv);
		break;
	default:
		status = 0;
	}

	return status;
}

/*
 * Tries to detect the currently attached accessory
 */
static enum accessory_jack_type detect(struct abx500_ad *dd,
			int *req_det_count)
{
	enum accessory_jack_type type = JACK_TYPE_DISCONNECTED;
	int i;

	accessory_regulator_enable(dd, REGULATOR_VAUDIO | REGULATOR_AVSWITCH);
	/* enable the VAMIC1 regulator */
	dd->config_hw_test_basic_carkit(dd, 0);

	for (i = 0; i < ARRAY_SIZE(detect_ops); ++i) {
		if (detect_hw(dd, &detect_ops[i])) {
			type = detect_ops[i].type;
			*req_det_count = detect_ops[i].req_det_count;
			break;
		}
	}

	dd->config_hw_test_plug_connected(dd, 0);

	if (jack_supports_buttons(type))
		accessory_regulator_enable(dd, REGULATOR_VAMIC1);
	else
		accessory_regulator_disable(dd, REGULATOR_VAMIC1 |
						REGULATOR_AVSWITCH);

	accessory_regulator_disable(dd, REGULATOR_VAUDIO);

	return type;
}

/*
 * registers to specific interrupt
 */
static void claim_irq(struct abx500_ad *dd, enum accessory_irq irq_id)
{
	int ret;
	int irq;

	if (dd->pdata->is_detection_inverted)
		abx500_accdet_irq_desc = dd->irq_desc_inverted;
	else
		abx500_accdet_irq_desc = dd->irq_desc_norm;

	if (abx500_accdet_irq_desc[irq_id].registered)
		return;

	irq = platform_get_irq_byname(
			dd->pdev,
			abx500_accdet_irq_desc[irq_id].name);
	if (irq < 0) {
		dev_err(&dd->pdev->dev,
			"%s: Failed to get irq %s\n", __func__,
			abx500_accdet_irq_desc[irq_id].name);
		return;
	}

	ret = request_threaded_irq(irq,
				NULL,
				abx500_accdet_irq_desc[irq_id].isr,
				IRQF_NO_SUSPEND | IRQF_SHARED,
				abx500_accdet_irq_desc[irq_id].name,
				dd);
	if (ret != 0) {
		dev_err(&dd->pdev->dev,
			"%s: Failed to claim irq %s (%d)\n",
			__func__,
			abx500_accdet_irq_desc[irq_id].name,
			ret);
	} else {
		abx500_accdet_irq_desc[irq_id].registered = 1;
		dev_dbg(&dd->pdev->dev, "%s: %s\n",
			__func__, abx500_accdet_irq_desc[irq_id].name);
	}
}

/*
 * releases specific interrupt
 */
static void release_irq(struct abx500_ad *dd, enum accessory_irq irq_id)
{
	int irq;

	if (dd->pdata->is_detection_inverted)
		abx500_accdet_irq_desc = dd->irq_desc_inverted;
	else
		abx500_accdet_irq_desc = dd->irq_desc_norm;

	if (!abx500_accdet_irq_desc[irq_id].registered)
		return;

	irq = platform_get_irq_byname(
			dd->pdev,
			abx500_accdet_irq_desc[irq_id].name);
	if (irq < 0) {
		dev_err(&dd->pdev->dev,
			"%s: Failed to get irq %s (%d)\n",
			__func__,
			abx500_accdet_irq_desc[irq_id].name, irq);
	} else {
		free_irq(irq, dd);
		abx500_accdet_irq_desc[irq_id].registered = 0;
		dev_dbg(&dd->pdev->dev, "%s: %s\n",
			__func__, abx500_accdet_irq_desc[irq_id].name);
	}
}

/*
 * configures interrupts + detection hardware to meet the requirements
 * set by currently attached accessory type.
 */
static void config_accdetect(struct abx500_ad *dd)
{
	switch (dd->jack_type) {
	case JACK_TYPE_UNSPECIFIED:
		dd->config_accdetect1_hw(dd, 1);
		dd->config_accdetect2_hw(dd, 0);

		release_irq(dd, PLUG_IRQ);
		release_irq(dd, UNPLUG_IRQ);
		release_irq(dd, BUTTON_PRESS_IRQ);
		release_irq(dd, BUTTON_RELEASE_IRQ);
		if (dd->set_av_switch)
			dd->set_av_switch(dd, NOT_SET, false);
		break;

	case JACK_TYPE_DISCONNECTED:
	if (dd->set_av_switch)
		dd->set_av_switch(dd, NOT_SET, false);
	case JACK_TYPE_HEADPHONE:
		dd->config_accdetect1_hw(dd, 1);
		dd->config_accdetect2_hw(dd, 0);

		claim_irq(dd, PLUG_IRQ);
		claim_irq(dd, UNPLUG_IRQ);
		release_irq(dd, BUTTON_PRESS_IRQ);
		release_irq(dd, BUTTON_RELEASE_IRQ);
		break;

	case JACK_TYPE_UNSUPPORTED_HEADSET:
		dd->config_accdetect1_hw(dd, 1);
		dd->config_accdetect2_hw(dd, 1);

		release_irq(dd, PLUG_IRQ);
		claim_irq(dd, UNPLUG_IRQ);
		release_irq(dd, BUTTON_PRESS_IRQ);
		release_irq(dd, BUTTON_RELEASE_IRQ);
		if (dd->set_av_switch)
			dd->set_av_switch(dd, AUDIO_IN, true);
		break;

	case JACK_TYPE_CONNECTED:
	case JACK_TYPE_HEADSET:
	case JACK_TYPE_CARKIT:
	case JACK_TYPE_OPENCABLE:
		dd->config_accdetect1_hw(dd, 1);
		dd->config_accdetect2_hw(dd, 1);

		release_irq(dd, PLUG_IRQ);
		claim_irq(dd, UNPLUG_IRQ);
		claim_irq(dd, BUTTON_PRESS_IRQ);
		claim_irq(dd, BUTTON_RELEASE_IRQ);
		break;

	default:
		dev_err(&dd->pdev->dev, "%s: Unknown type: %d\n",
			__func__, dd->jack_type);
	}
}

/*
 * Deferred initialization of the work.
 */
static void init_work(struct work_struct *work)
{
	struct abx500_ad *dd = container_of(work,
		struct abx500_ad, init_work.work);

	dev_dbg(&dd->pdev->dev, "%s: Enter\n", __func__);

	dd->jack_type = dd->reported_jack_type = JACK_TYPE_UNSPECIFIED;
	config_accdetect(dd);
	queue_delayed_work(dd->irq_work_queue,
				&dd->detect_work,
				msecs_to_jiffies(0));
}

/*
 * performs platform device initialization
 */
static int abx500_accessory_init(struct platform_device *pdev)
{
	int ret;
	struct abx500_ad *dd = (struct abx500_ad *)pdev->id_entry->driver_data;

	dev_dbg(&pdev->dev, "Enter: %s\n", __func__);

	dd->pdev = pdev;
	dd->pdata = dd->get_platform_data(pdev);
	if (IS_ERR(dd->pdata))
		return PTR_ERR(dd->pdata);

	if (dd->pdata->video_ctrl_gpio) {
		ret = gpio_is_valid(dd->pdata->video_ctrl_gpio);
		if (!ret) {
			dev_err(&pdev->dev,
				"%s: Video ctrl GPIO invalid (%d).\n", __func__,
						dd->pdata->video_ctrl_gpio);

			return ret;
		}
		ret = gpio_request(dd->pdata->video_ctrl_gpio,
				"Video Control");
	       if (ret)	{
			dev_err(&pdev->dev, "%s: Get video ctrl GPIO"
					"failed.\n", __func__);
			return ret;
		}
	}

	if (dd->pdata->mic_ctrl) {
		ret = gpio_is_valid(dd->pdata->mic_ctrl);
		if (!ret) {
			dev_err(&pdev->dev,
				"%s: Mic ctrl GPIO invalid (%d).\n", __func__,
						dd->pdata->mic_ctrl);

			goto mic_ctrl_fail;
		}
		ret = gpio_request(dd->pdata->mic_ctrl,
				"Mic Control");
	       if (ret)	{
			dev_err(&pdev->dev, "%s: Get mic ctrl GPIO"
					"failed.\n", __func__);
			goto mic_ctrl_fail;
		}
	}

	if (dd->pdata->nahj_ctrl) {
		ret = gpio_is_valid(dd->pdata->nahj_ctrl);
		if (!ret) {
			dev_err(&pdev->dev,
				"%s: nahj ctrl GPIO invalid (%d).\n", __func__,
						dd->pdata->nahj_ctrl);

			goto nahj_fail;
		}
		ret = gpio_request(dd->pdata->nahj_ctrl,
				"nahj Control");
	       if (ret)	{
			dev_err(&pdev->dev, "%s: Get nahj GPIO"
					"failed.\n", __func__);
			goto nahj_fail;
		}
	}

	ret = create_btn_input_dev(dd);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: create_button_input_dev failed.\n",
			__func__);
		goto fail_no_btn_input_dev;
	}

	ret = create_regulators(dd);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: failed to create regulators\n",
			__func__);
		goto fail_no_regulators;
	}
	dd->btn_state = BUTTON_UNK;

	dd->irq_work_queue = create_singlethread_workqueue("abx500_accdet_wq");
	if (!dd->irq_work_queue) {
		dev_err(&pdev->dev, "%s: Failed to create wq\n", __func__);
		ret = -ENOMEM;
		goto fail_no_mem_for_wq;
	}

	dd->gpadc = dd->accdet_abx500_gpadc_get();

	INIT_DELAYED_WORK(&dd->detect_work, detect_work);
	INIT_DELAYED_WORK(&dd->unplug_irq_work, unplug_irq_handler_work);
	INIT_DELAYED_WORK(&dd->init_work, init_work);

	/* Deferred init/detect since no use for the info early in boot */
	queue_delayed_work(dd->irq_work_queue,
				&dd->init_work,
				msecs_to_jiffies(INIT_DELAY_MS));

	platform_set_drvdata(pdev, dd);


	return 0;

fail_no_mem_for_wq:
	free_regulators(dd);
fail_no_regulators:
	input_unregister_device(dd->btn_input_dev);
fail_no_btn_input_dev:
	if (dd->pdata->nahj_ctrl)
		gpio_free(dd->pdata->nahj_ctrl);
nahj_fail:
	if (dd->pdata->mic_ctrl)
		gpio_free(dd->pdata->mic_ctrl);
mic_ctrl_fail:
	if (dd->pdata->video_ctrl_gpio)
		gpio_free(dd->pdata->video_ctrl_gpio);
	return ret;
}

/*
 * Performs platform device cleanup
 */
static void abx500_accessory_cleanup(struct abx500_ad *dd)
{
	dev_dbg(&dd->pdev->dev, "Enter: %s\n", __func__);

	dd->jack_type = JACK_TYPE_UNSPECIFIED;
	config_accdetect(dd);

	if (dd->pdata->nahj_ctrl)
		gpio_free(dd->pdata->nahj_ctrl);

	if (dd->pdata->mic_ctrl)
		gpio_free(dd->pdata->mic_ctrl);

	if (dd->pdata->video_ctrl_gpio)
		gpio_free(dd->pdata->video_ctrl_gpio);

	input_unregister_device(dd->btn_input_dev);
	free_regulators(dd);

	cancel_delayed_work(&dd->detect_work);
	cancel_delayed_work(&dd->unplug_irq_work);
	cancel_delayed_work(&dd->init_work);
	flush_workqueue(dd->irq_work_queue);
	destroy_workqueue(dd->irq_work_queue);

}

static int __devinit abx500_acc_detect_probe(struct platform_device *pdev)
{

	return abx500_accessory_init(pdev);
}

static int __devexit abx500_acc_detect_remove(struct platform_device *pdev)
{
	abx500_accessory_cleanup(platform_get_drvdata(pdev));
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#if defined(CONFIG_PM)
static int abx500_acc_detect_suspend(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
		struct platform_device, dev);
	struct abx500_ad *dd = platform_get_drvdata(pdev);
	int irq_id, irq;

	dev_dbg(&dd->pdev->dev, "%s: Enter\n", __func__);

	cancel_delayed_work_sync(&dd->unplug_irq_work);
	cancel_delayed_work_sync(&dd->detect_work);
	cancel_delayed_work_sync(&dd->init_work);

	if (dd->pdata->is_detection_inverted)
		abx500_accdet_irq_desc = dd->irq_desc_inverted;
	else
		abx500_accdet_irq_desc = dd->irq_desc_norm;

	for (irq_id = 0; irq_id < dd->no_irqs; irq_id++) {
		if (abx500_accdet_irq_desc[irq_id].registered == 1) {
			irq = platform_get_irq_byname(
					dd->pdev,
					abx500_accdet_irq_desc[irq_id].name);

			disable_irq(irq);
		}
	}

	dd->turn_off_accdet_comparator(pdev);

	if (dd->jack_type == JACK_TYPE_HEADSET)
		accessory_regulator_disable(dd, REGULATOR_VAMIC1);

	return 0;
}

static int abx500_acc_detect_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
		struct platform_device, dev);
	struct abx500_ad *dd = platform_get_drvdata(pdev);
	int irq_id, irq;

	dev_dbg(&dd->pdev->dev, "%s: Enter\n", __func__);

	if (dd->jack_type == JACK_TYPE_HEADSET)
		accessory_regulator_enable(dd, REGULATOR_VAMIC1);

	dd->turn_on_accdet_comparator(pdev);

	if (dd->pdata->is_detection_inverted)
		abx500_accdet_irq_desc = dd->irq_desc_inverted;
	else
		abx500_accdet_irq_desc = dd->irq_desc_norm;

	for (irq_id = 0; irq_id < dd->no_irqs; irq_id++) {
		if (abx500_accdet_irq_desc[irq_id].registered == 1) {
			irq = platform_get_irq_byname(
					dd->pdev,
					abx500_accdet_irq_desc[irq_id].name);

			enable_irq(irq);

		}
	}

	/* After resume, reinitialize */
	dd->accdet1_th_set = dd->accdet2_th_set = 0;
	queue_delayed_work(dd->irq_work_queue, &dd->init_work, 0);

	return 0;
}
#else
#define abx500_acc_detect_suspend	NULL
#define abx500_acc_detect_resume	NULL
#endif

static struct platform_device_id abx500_accdet_ids[] = {
#ifdef CONFIG_INPUT_AB5500_ACCDET
	{ "ab5500-acc-det", (kernel_ulong_t)&ab5500_accessory_det_callbacks, },
#endif
#ifdef CONFIG_INPUT_AB8500_ACCDET
	{ "ab8500-acc-det", (kernel_ulong_t)&ab8500_accessory_det_callbacks, },
#endif
	{ },
};

static const struct dev_pm_ops abx_ops = {
	.suspend = abx500_acc_detect_suspend,
	.resume = abx500_acc_detect_resume,
};

static struct platform_driver abx500_acc_detect_platform_driver = {
	.driver = {
		.name = "abx500-acc-det",
		.owner = THIS_MODULE,
		.pm = &abx_ops,
	},
	.probe = abx500_acc_detect_probe,
	.id_table = abx500_accdet_ids,
	.remove	= __devexit_p(abx500_acc_detect_remove),
};

static int __init abx500_acc_detect_init(void)
{
	return platform_driver_register(&abx500_acc_detect_platform_driver);
}

static void __exit abx500_acc_detect_exit(void)
{
	platform_driver_unregister(&abx500_acc_detect_platform_driver);
}

module_init(abx500_acc_detect_init);
module_exit(abx500_acc_detect_exit);

MODULE_DESCRIPTION("ABx500 AV Accessory detection driver");
MODULE_ALIAS("platform:abx500-acc-det");
MODULE_AUTHOR("ST-Ericsson");
MODULE_LICENSE("GPL v2");
