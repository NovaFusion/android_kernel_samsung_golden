/*  drivers/misc/sec_jack.c
 *
 *  Copyright (C) 2011 Samsung Electronics Co.Ltd
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/switch.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/sec_jack.h>
#include <linux/mfd/ab8500.h>
#ifdef CONFIG_MACH_SEC_GOLDEN
#include <linux/mfd/abx500.h>
#endif

#define MAX_ZONE_LIMIT		10
#define DET_CHECK_TIME_MS	250		/* 250ms */
#define BUTTONS_CHECK_TIME_MS	60		/* 60ms */
#define WAKE_LOCK_TIME		(HZ * 5)	/* 5 sec */

struct sec_jack_info {
	struct sec_jack_platform_data *pdata;
	struct work_struct det_work;
	struct work_struct buttons_work;
	struct wake_lock det_wake_lock;
	struct regulator *regulator_mic;
	struct timer_list det_timer;
	struct timer_list buttons_timer;
	struct input_dev *input;
	struct timespec tp;  /* Get Current time for KSND */
	struct timespec tp_after; /* Get Current time After Event */
	struct device *pmdev; /* Device */
	int det_r_irq;
	int det_f_irq;
	int buttons_r_irq;
	int buttons_f_irq;
	int dev_id;
	int pressed_code;
	bool mic_status;
	bool det_status;
	unsigned int cur_jack_type;
	bool send_key_pressed;
};

#ifdef CONFIG_MACH_SEC_GOLDEN
static bool recheck_jack;
#endif

int jack_is_detected;
EXPORT_SYMBOL(jack_is_detected);

/* with some modifications like moving all the gpio structs inside
 * the platform data and getting the name for the switch and
 * gpio_event from the platform data, the driver could support more than
 * one headset jack, but currently user space is looking only for
 * one key file and switch for a headset so it'd be overkill and
 * untestable so we limit to one instantiation for now.
 */
static atomic_t instantiated = ATOMIC_INIT(0);

/* sysfs name HeadsetObserver.java looks for to track headset state
 */
struct switch_dev switch_jack_detection = {
	.name = "h2w",
};

/* to support AT+FCESTEST=1 */
struct switch_dev switch_sendend = {
		.name = "send_end",
};
static void set_micbias(struct sec_jack_info *hi, bool on)
{

	pr_info("%s:old state=%d, new state=%d\n", __func__,
		hi->mic_status, on);

	if (on && !hi->mic_status) {
		regulator_enable(hi->regulator_mic);
		hi->mic_status = true;
		pr_info("%s: enable mic bias\n", __func__);
	} else if (!on && hi->mic_status) {
		regulator_disable(hi->regulator_mic);
		hi->mic_status = false;
		pr_info("%s: disable mic bias\n", __func__);
	}

}

//KSND added
#ifdef CONFIG_MACH_SEC_GOLDEN
static void set_Accdetection( struct device *dev, bool on )
{
  int ret = 0;

	pr_info("%s: ACC Set : %d \n", __func__, on );

	if( on ){
		ret = abx500_set_register_interruptible(dev, AB8500_ECI_AV_ACC, 0x82, 0x33);
	} else {
		ret = abx500_set_register_interruptible(dev, AB8500_ECI_AV_ACC, 0x82, 0x1);
  }

	if (ret < 0){
		pr_err("%s: ab8500 write failed!! switch is %d \n", __func__, on);
	}
}
#endif

static void sec_jack_set_type(struct sec_jack_info *hi, int jack_type)
{
	if (jack_type == hi->cur_jack_type) {
		if (jack_type != SEC_HEADSET_4POLE)
			set_micbias(hi, false);
		return;
	}

	/* micbias is left enabled for 4pole and disabled otherwise */
	if (jack_type != SEC_HEADSET_4POLE)
		set_micbias(hi, false);

//KSND Added
#if 0 //def CONFIG_MACH_SEC_GOLDEN
	if( jack_type != SEC_JACK_NO_DEVICE )
		set_Accdetection(hi->pmdev, true );
	else
		set_Accdetection(hi->pmdev, false );
#endif
		
	if (hi->cur_jack_type == SEC_UNKNOWN_DEVICE &&
	    jack_type != SEC_JACK_NO_DEVICE)
		hi->det_status = true;

	hi->cur_jack_type = jack_type;
	jack_is_detected = hi->cur_jack_type;
	pr_info("%s : jack_type = %d\n", __func__, jack_type);

	switch_set_state(&switch_jack_detection, jack_type);
}

static void determine_jack_type(struct sec_jack_info *hi)
{
	struct sec_jack_platform_data *pdata = hi->pdata;
	struct sec_jack_zone *zones = pdata->zones;
	int size = pdata->num_zones;
	int count[MAX_ZONE_LIMIT] = {0};
	int adc;
	int i;

#ifdef CONFIG_MACH_SEC_GOLDEN
	int reselector_zone = hi->pdata->ear_reselector_zone;
#endif

	/* set mic bias to enable adc */
	set_micbias(hi, true);

	while (hi->det_status || hi->cur_jack_type == SEC_UNKNOWN_DEVICE) {

		adc = pdata->get_adc_value();
		pr_info("%s: adc = %d\n", __func__, adc);

		/* determine the type of headset based on the
		 * adc value.  An adc value can fall in various
		 * ranges or zones.  Within some ranges, the type
		 * can be returned immediately.  Within others, the
		 * value is considered unstable and we need to sample
		 * a few more types (up to the limit determined by
		 * the range) before we return the type for that range.
		 */
		for (i = 0; i < size; i++) {
			if (adc <= zones[i].adc_high) {
				if (++count[i] > zones[i].check_count) {
#ifdef CONFIG_MACH_SEC_GOLDEN
					if ((recheck_jack == true) && (i > 2) && (reselector_zone < adc)) {
						pr_debug("%s : something wrong connectoin!\n",__func__);
						sec_jack_set_type(hi, SEC_JACK_NO_DEVICE);
						recheck_jack = false;
						return;
					}
#endif

					/* Get Headset detection time (estimate KSND) */
					hi->tp_after = current_kernel_time();
					hi->tp.tv_nsec = hi->tp_after.tv_nsec - hi->tp.tv_nsec;
					pr_info("%s: detect time : %d \n", __func__, (int)hi->tp.tv_nsec/1000000 );
					hi->tp.tv_nsec = 0;

					sec_jack_set_type(hi, zones[i].jack_type);
					return;
				}

				if (zones[i].delay_ms > 0)
					msleep(zones[i].delay_ms);
				break;
			}
		}
	}
#ifdef CONFIG_MACH_SEC_GOLDEN
	recheck_jack = false;
#endif

	/* jack detection is failed */
	pr_debug("%s : detection is failed\n", __func__);
	sec_jack_set_type(hi, SEC_JACK_NO_DEVICE);
}

static void sec_jack_det_work(struct work_struct *work)
{
	struct sec_jack_info *hi =
		container_of(work, struct sec_jack_info, det_work);

	if (!hi->det_status)
		return;

	/* jack presence was detected the whole time, figure out which type */
	determine_jack_type(hi);

}
static void sec_jack_buttons_work(struct work_struct *work)
{
	struct sec_jack_info *hi =
		container_of(work, struct sec_jack_info, buttons_work);
	struct sec_jack_platform_data *pdata = hi->pdata;
	struct sec_jack_buttons_zone *btn_zones = pdata->buttons_zones;
	int adc, i;

	pr_debug("%s:\n", __func__);

	if (hi->cur_jack_type != SEC_HEADSET_4POLE || !hi->det_status) {
		hi->send_key_pressed = false;
		pr_debug("%s: key is ignored\n", __func__);
		return;
	}
	/* when button is pressed */
	adc = pdata->get_adc_value();

	for (i = 0; i < pdata->num_buttons_zones; i++)
		if (adc >= btn_zones[i].adc_low &&
		    adc <= btn_zones[i].adc_high) {
			hi->pressed_code = btn_zones[i].code;
			input_report_key(hi->input, btn_zones[i].code, 1);
			switch_set_state(&switch_sendend, 1);
			hi->send_key_pressed = true;
			input_sync(hi->input);
      
			/* Get Headset Key detection time (estimate KSND) */
			hi->tp_after = current_kernel_time();
			hi->tp.tv_nsec = hi->tp_after.tv_nsec - hi->tp.tv_nsec;
			pr_info("%s: pressed time : %d \n", __func__, (int)hi->tp.tv_nsec/1000000 );
			hi->tp.tv_nsec = 0;

			pr_info("%s: keycode=%d, is pressed, adc=%d\n",
				__func__, btn_zones[i].code, adc);
			return;
		}

	pr_warn("%s: key is skipped. ADC value is %d\n", __func__, adc);
}
static void sec_jack_det_timer(unsigned long param)
{
	struct sec_jack_info *hi = (struct sec_jack_info *)param;

	pr_debug("%s:\n", __func__);

	schedule_work(&hi->det_work);
}
static void sec_jack_buttons_timer(unsigned long param)
{
	struct sec_jack_info *hi = (struct sec_jack_info *)param;

	pr_debug("%s:\n", __func__);

	schedule_work(&hi->buttons_work);
}

/* thread run whenever the headset detect state changes (either insertion
 * or removal)
 */
static irqreturn_t sec_jack_detect_irq_thread_f(int irq, void *dev_id)
{
	struct sec_jack_info *hi = dev_id;

	pr_info("%s:\n", __func__);

	hi->det_status = true;
	/* prevent suspend to allow user space to respond to switch */
	wake_lock_timeout(&hi->det_wake_lock, WAKE_LOCK_TIME);

	/* debounce headset jack.  don't try to determine the type of
	 * headset until the detect state is true for a while.
	 */
	mod_timer(&hi->det_timer,
		jiffies + msecs_to_jiffies(DET_CHECK_TIME_MS));

  /* Get Current time */
	hi->tp = current_kernel_time();

	return IRQ_HANDLED;
}
static irqreturn_t sec_jack_detect_irq_thread_r(int irq, void *dev_id)
{
	struct sec_jack_info *hi = dev_id;

	pr_info("%s:\n", __func__);

	hi->det_status = false;
	del_timer(&hi->buttons_timer);
	del_timer(&hi->det_timer);

	/* update button status forcely when earjack is removed */
	input_report_key(hi->input, hi->pressed_code, 0);
	switch_set_state(&switch_sendend, 0);
	input_sync(hi->input);

	sec_jack_set_type(hi, SEC_JACK_NO_DEVICE);
	hi->send_key_pressed = false;

	return IRQ_HANDLED;
}
/* thread run whenever the headset buttons state changes (either pressed
 * or released)
 */
static irqreturn_t sec_jack_buttons_irq_thread_r(int irq, void *dev_id)
{
	struct sec_jack_info *hi = dev_id;
	pr_info("%s:\n", __func__);

	if (hi->cur_jack_type != SEC_HEADSET_4POLE || !hi->det_status)
		return IRQ_HANDLED;

	/* prevent suspend to allow user space to respond to switch */
	wake_lock_timeout(&hi->det_wake_lock, WAKE_LOCK_TIME);

	mod_timer(&hi->buttons_timer,
		jiffies + msecs_to_jiffies(BUTTONS_CHECK_TIME_MS));

	/* Get Current time */
	hi->tp = current_kernel_time();

	return IRQ_HANDLED;
}
static irqreturn_t sec_jack_buttons_irq_thread_f(int irq, void *dev_id)
{
	struct sec_jack_info *hi = dev_id;
	pr_debug("%s:\n", __func__);

	if (hi->cur_jack_type != SEC_HEADSET_4POLE || !hi->det_status)
		return IRQ_HANDLED;

	del_timer(&hi->buttons_timer);
	input_report_key(hi->input, hi->pressed_code, 0);
	switch_set_state(&switch_sendend, 0);
	hi->send_key_pressed = false;
	input_sync(hi->input);
	pr_info("%s: keycode=%d, is released\n", __func__,
		hi->pressed_code);
	return IRQ_HANDLED;
}

static ssize_t  key_state_onoff_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_jack_info *hi = dev_get_drvdata(dev);
	int value = 0;
	if (hi->send_key_pressed != true)
		value = 0;
	else
		value = 1;
		
	return sprintf(buf, "%d\n", value);
}

static DEVICE_ATTR(key_state, 0664 , key_state_onoff_show, NULL);

static ssize_t  earjack_state_onoff_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_jack_info *hi = dev_get_drvdata(dev);
	int value = 0;
	if (hi->cur_jack_type == SEC_HEADSET_4POLE)
		value = 1;
	else
		value = 0;
	return sprintf(buf, "%d\n", value);
}
static DEVICE_ATTR(state, 0664 , earjack_state_onoff_show, NULL);

static ssize_t select_jack_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("%s : operate nothing\n", __func__);
	return 0;
}

static ssize_t select_jack_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct sec_jack_info *hi = dev_get_drvdata(dev);
	struct sec_jack_platform_data *pdata = hi->pdata;
	int value = 0;

	sscanf(buf, "%d", &value);
	pr_err("%s: User  selection : 0X%x", __func__, value);
	if (value == SEC_HEADSET_4POLE) {
		pdata->set_micbias_state(true);
		msleep(100);
	}

	sec_jack_set_type(hi, value);

	return size;
}

static DEVICE_ATTR(select_jack, 0664, select_jack_show,
		select_jack_store);

#ifdef CONFIG_MACH_SEC_GOLDEN
static ssize_t reselect_jack_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("%s : operate nothing\n", __func__);
	return 0;
}

static ssize_t reselect_jack_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct sec_jack_info *hi = dev_get_drvdata(dev);
	//struct sec_jack_platform_data *pdata = hi->pdata;
	int value = 0;


	sscanf(buf, "%d", &value);
	pr_err("%s: User  selection : 0X%x", __func__, value);

	if (value == 1) {
		recheck_jack = true;
		determine_jack_type(hi);
	}

	return size;
}

static DEVICE_ATTR(reselect_jack, 0664, reselect_jack_show,
		reselect_jack_store);
#endif

static int sec_jack_probe(struct platform_device *pdev)
{
	struct sec_jack_info *hi;
	struct ab8500_platform_data *plat;
	struct sec_jack_platform_data *pdata;
	int ret, irq, i;
	struct class *audio;
	struct device *earjack;

	pr_info("%s : Registering jack driver\n", __func__);

	plat = dev_get_platdata(pdev->dev.parent);
	if (!plat || !plat->accdet) {
		pr_err("%s : plat is NULL\n", __func__);
		return -ENODEV;
	}

	pdata = plat->accdet;

	if (!pdata->get_adc_value || !pdata->zones || !pdata->det_r ||
	    !pdata->det_f || !pdata->buttons_r || !pdata->buttons_f ||
	    !pdata->regulator_mic_source || !pdata->get_det_level ||
	    pdata->num_zones > MAX_ZONE_LIMIT) {
		pr_err("%s : need to check pdata\n", __func__);
		return -ENODEV;
	}

	if (atomic_xchg(&instantiated, 1)) {
		pr_err("%s : already instantiated, can only have one\n",
			__func__);
		return -ENODEV;
	}

	hi = kzalloc(sizeof(struct sec_jack_info), GFP_KERNEL);
	if (hi == NULL) {
		pr_err("%s : Failed to allocate memory.\n", __func__);
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	hi->pdata = pdata;

	/* make the id of our gpi_event device the same as our platform device,
	 * which makes it the responsiblity of the board file to make sure
	 * it is unique relative to other gpio_event devices
	 */
	hi->dev_id = pdev->id;
	ret = switch_dev_register(&switch_jack_detection);
	if (ret < 0) {
		pr_err("%s : Failed to register switch device\n", __func__);
		goto err_switch_dev_register;
	}
	ret = switch_dev_register(&switch_sendend);
	if (ret < 0) {
		printk(KERN_ERR "SEC JACK: Failed to register switch device\n");
		goto err_switch_dev_register_send_end;
	}

	wake_lock_init(&hi->det_wake_lock, WAKE_LOCK_SUSPEND, "sec_jack_det");

	hi->input = input_allocate_device();
	if (!hi->input)	{
		ret = -ENOMEM;
		pr_err("%s: failed to allocate input device\n", __func__);
		goto err_request_input_dev;
	}

	hi->input->name = "sec_jack";
	for (i = 0; i < pdata->num_buttons_zones; i++)
		input_set_capability(hi->input, EV_KEY,
			pdata->buttons_zones[i].code);

	ret = input_register_device(hi->input);
	if (ret < 0) {
		pr_err("%s: failed to register driver\n", __func__);
		input_free_device(hi->input);
		goto err_register_input_dev;
	}

	hi->regulator_mic = regulator_get(NULL, pdata->regulator_mic_source);

	if (hi->regulator_mic < 0) {
		pr_err("%s: failed to get v-amic1 LDO\n", __func__);
		goto err_regulator_get;
	}
	hi->mic_status = false;

	audio = class_create(THIS_MODULE, "audio");
	if (IS_ERR(audio))
		pr_err("Failed to create class(audio)!\n");

	earjack = device_create(audio, NULL, 0, NULL, "earjack");
	if (IS_ERR(earjack))
		pr_err("Failed to create device(earjack)!\n");

	ret = device_create_file(earjack, &dev_attr_key_state);
	if (ret)
		pr_err("Failed to create device file in sysfs entries!\n");

	ret = device_create_file(earjack, &dev_attr_state);
	if (ret)
		pr_err("Failed to create device file in sysfs entries!\n");
		
	ret = device_create_file(earjack, &dev_attr_select_jack);
	if (ret)
		pr_err("Failed to create device file in sysfs entries(%s)!\n",
				dev_attr_select_jack.attr.name);
#ifdef CONFIG_MACH_SEC_GOLDEN
	ret = device_create_file(earjack, &dev_attr_reselect_jack);
	if (ret)
		pr_err("Failed to create device file in sysfs entries(%s)!\n",
				dev_attr_reselect_jack.attr.name);
#endif
	INIT_WORK(&hi->det_work, sec_jack_det_work);
	INIT_WORK(&hi->buttons_work, sec_jack_buttons_work);

	init_timer(&hi->det_timer);
	hi->det_timer.function = sec_jack_det_timer;
	hi->det_timer.data = (unsigned long) hi;

	init_timer(&hi->buttons_timer);
	hi->buttons_timer.function = sec_jack_buttons_timer;
	hi->buttons_timer.data = (unsigned long) hi;

	if (pdata->mach_init)
		pdata->mach_init(pdev);

	hi->det_status = false;

	/* register interrupt handler */
	irq = platform_get_irq_byname(pdev, pdata->det_r);
	if (irq < 0) {
		pr_err("%s: failed to get platform irq-%d\n", __func__, irq);
		ret = irq;
		goto err_get_irq_byname_det_r;
	}


	ret = request_threaded_irq(irq, NULL,
				   sec_jack_detect_irq_thread_r, IRQF_NO_SUSPEND,
				   "sec_headset_detect_r", hi);
	if (ret) {
		pr_err("%s: failed to request_irq\n", __func__);
		goto err_request_detect_irq_r;
	}

	hi->det_r_irq = irq;

	irq = platform_get_irq_byname(pdev, pdata->det_f);
	if (irq < 0) {
		pr_err("%s: failed to get platform irq-%d\n", __func__, irq);
		ret = irq;
		goto err_get_irq_byname_det_f;
	}

	ret = request_threaded_irq(irq, NULL,
				   sec_jack_detect_irq_thread_f, IRQF_NO_SUSPEND,
				   "sec_headset_detect_f", hi);
	if (ret) {
		pr_err("%s: failed to request_irq\n", __func__);
		goto err_request_detect_irq_f;
	}

	hi->det_f_irq = irq;

	irq = platform_get_irq_byname(pdev, pdata->buttons_r);
	if (irq < 0) {
		pr_err("%s: failed to get platform irq-%d\n", __func__, irq);
		ret = irq;
		goto err_get_irq_byname_buttons_r;
	}

	ret = request_threaded_irq(irq, NULL,
				   sec_jack_buttons_irq_thread_r, IRQF_NO_SUSPEND,
				   "sec_button_r", hi);
	if (ret) {
		pr_err("%s: failed to request_irq\n", __func__);
		goto err_request_buttons_irq_r;
	}

	hi->buttons_r_irq = irq;

	irq = platform_get_irq_byname(pdev, pdata->buttons_f);
	if (irq < 0) {
		pr_err("%s: failed to get platform irq-%d\n", __func__, irq);
		ret = irq;
		goto err_get_irq_byname_buttons_f;
	}

	ret = request_threaded_irq(irq, NULL,
				   sec_jack_buttons_irq_thread_f, IRQF_NO_SUSPEND,
				   "sec_button_f", hi);
	if (ret) {
		pr_err("%s: failed to request_irq\n", __func__);
		goto err_request_buttons_f;
	}

	hi->buttons_f_irq = irq;

	dev_set_drvdata(&pdev->dev, hi);

//KSND Added
#ifdef CONFIG_MACH_SEC_GOLDEN
	hi->pmdev = &pdev->dev;
#endif

	hi->cur_jack_type = SEC_UNKNOWN_DEVICE;

	if (!pdata->get_det_level(pdev))
		determine_jack_type(hi);

	dev_set_drvdata(earjack, hi);

	return 0;

err_request_buttons_f:
err_get_irq_byname_buttons_f:
	free_irq(hi->buttons_r_irq, hi);
err_request_buttons_irq_r:
err_get_irq_byname_buttons_r:
	free_irq(hi->det_f_irq, hi);
err_request_detect_irq_f:
err_get_irq_byname_det_f:
	free_irq(hi->det_r_irq, hi);
err_request_detect_irq_r:
err_get_irq_byname_det_r:
err_regulator_get:
	input_unregister_device(hi->input);
err_register_input_dev:
err_request_input_dev:
	wake_lock_destroy(&hi->det_wake_lock);
	switch_dev_unregister(&switch_sendend);
err_switch_dev_register_send_end:
	switch_dev_unregister(&switch_jack_detection);
err_switch_dev_register:
	kfree(hi);
err_kzalloc:
	atomic_set(&instantiated, 0);
	return ret;
}

static int sec_jack_remove(struct platform_device *pdev)
{

	struct sec_jack_info *hi = dev_get_drvdata(&pdev->dev);

	pr_info("%s :\n", __func__);

	sec_jack_set_type(hi, SEC_JACK_NO_DEVICE);
	input_unregister_device(hi->input);
	free_irq(hi->det_r_irq, hi);
	free_irq(hi->det_f_irq, hi);
	free_irq(hi->buttons_f_irq, hi);
	free_irq(hi->buttons_r_irq, hi);
	wake_lock_destroy(&hi->det_wake_lock);
	switch_dev_unregister(&switch_sendend);
	switch_dev_unregister(&switch_jack_detection);
	kfree(hi);
	atomic_set(&instantiated, 0);

	return 0;
}

static struct platform_driver sec_jack_driver = {
	.probe = sec_jack_probe,
	.remove = sec_jack_remove,
	.driver = {
			.name = "sec_jack",
			.owner = THIS_MODULE,
		   },
};
static int __init sec_jack_init(void)
{
	return platform_driver_register(&sec_jack_driver);
}

static void __exit sec_jack_exit(void)
{
	platform_driver_unregister(&sec_jack_driver);
}

late_initcall(sec_jack_init);
module_exit(sec_jack_exit);

MODULE_AUTHOR("ms17.kim@samsung.com");
MODULE_DESCRIPTION("Samsung Electronics Corp Ear-Jack detection driver");
MODULE_LICENSE("GPL");
