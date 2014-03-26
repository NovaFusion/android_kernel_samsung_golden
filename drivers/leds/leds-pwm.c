/*
 * linux/drivers/leds-pwm.c
 *
 * simple PWM based LED control
 *
 * Copyright 2009 Luotao Fu @ Pengutronix (l.fu@pengutronix.de)
 *
 * based on leds-gpio.c by Raphael Assenat <raph@8d.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/leds_pwm.h>
#include <linux/slab.h>

struct led_pwm_data {
	struct led_classdev	cdev;
	struct pwm_device	*pwm;
	unsigned int 		active_low;
	unsigned int		lth_brightness;
	unsigned int		period;
	unsigned int		dutycycle_steps;
	unsigned int		period_steps;
};

static int led_pwm_blink_set(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct led_pwm_data *led_dat =
		container_of(led_cdev, struct led_pwm_data, cdev);
	int dutycycle_ms, period_sec;
	int dutycycle, period;
	/*
	 * If both the delays are zero set some sensible delay
	 */
	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 500;
		*delay_off = 500;
	}
	/*
	 * calculate the duty cycle from on and off time
	 */
	dutycycle_ms = ((*delay_on * 1000)/(*delay_on + *delay_off));
	/*
	 * convert calculated value to write into the PWM out register
	 */
	if (led_dat->dutycycle_steps)
		dutycycle = ((dutycycle_ms * led_dat->dutycycle_steps)/1000);
	else
		dutycycle = (dutycycle_ms/1000);
	/*
	 * calculate period from on and off time(msec)
	 */
	period_sec = ((*delay_on + *delay_off)/1000);
	/*
	 * convert calculated value to write into the PWM out register
	 */
	if (led_dat->period_steps) {
		if ((*delay_on + *delay_off) == 500)
			period = led_dat->period_steps;
		else
			period = led_dat->period_steps - period_sec;
	}
	else
		period = period_sec;
	/*
	 * configure the PWM registers and enable blink functionality
	 */
	pwm_config_blink(led_dat->pwm, dutycycle, period);
	pwm_blink_ctrl(led_dat->pwm, 1);
	return 0;
}

static void led_pwm_set(struct led_classdev *led_cdev,
	enum led_brightness brightness)
{
	struct led_pwm_data *led_dat =
		container_of(led_cdev, struct led_pwm_data, cdev);
	unsigned int max = led_dat->cdev.max_brightness;
	unsigned int period =  led_dat->period;

	if (brightness == 0) {
		pwm_config(led_dat->pwm, 0, period);
		pwm_disable(led_dat->pwm);
	} else {
		brightness = led_dat->lth_brightness + (brightness *
			(led_dat->period - led_dat->lth_brightness) / max);
		pwm_config(led_dat->pwm, brightness, led_dat->period);

		pwm_enable(led_dat->pwm);
	}
}

static int led_pwm_probe(struct platform_device *pdev)
{
	struct led_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct led_pwm *cur_led;
	struct led_pwm_data *leds_data, *led_dat;
	int i, ret = 0;

	if (!pdata)
		return -EBUSY;

	leds_data = kzalloc(sizeof(struct led_pwm_data) * pdata->num_leds,
				GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		cur_led = &pdata->leds[i];
		led_dat = &leds_data[i];

		led_dat->pwm = pwm_request(cur_led->pwm_id,
				cur_led->name);
		if (IS_ERR(led_dat->pwm)) {
			ret = PTR_ERR(led_dat->pwm);
			dev_err(&pdev->dev, "unable to request PWM %d\n",
					cur_led->pwm_id);
			goto err;
		}

		led_dat->cdev.name = cur_led->name;
		led_dat->cdev.default_trigger = cur_led->default_trigger;
		led_dat->active_low = cur_led->active_low;
		led_dat->period = cur_led->pwm_period_ns;
		led_dat->lth_brightness = cur_led->lth_brightness *
		      (cur_led->pwm_period_ns / cur_led->max_brightness);
		led_dat->dutycycle_steps = cur_led->dutycycle_steps;
		led_dat->period_steps = cur_led->period_steps;
		led_dat->cdev.brightness_set = led_pwm_set;
		led_dat->cdev.brightness = LED_OFF;
		led_dat->cdev.blink_set = led_pwm_blink_set;
		led_dat->cdev.max_brightness = cur_led->max_brightness;
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;

		ret = led_classdev_register(&pdev->dev, &led_dat->cdev);
		if (ret < 0) {
			pwm_free(led_dat->pwm);
			goto err;
		}
	}

	platform_set_drvdata(pdev, leds_data);

	return 0;

err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			led_classdev_unregister(&leds_data[i].cdev);
			pwm_free(leds_data[i].pwm);
		}
	}

	kfree(leds_data);

	return ret;
}

static int __devexit led_pwm_remove(struct platform_device *pdev)
{
	int i;
	struct led_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct led_pwm_data *leds_data;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&leds_data[i].cdev);
		pwm_free(leds_data[i].pwm);
	}

	kfree(leds_data);

	return 0;
}

static struct platform_driver led_pwm_driver = {
	.probe		= led_pwm_probe,
	.remove		= __devexit_p(led_pwm_remove),
	.driver		= {
		.name	= "leds_pwm",
		.owner	= THIS_MODULE,
	},
};

static int __init led_pwm_init(void)
{
	return platform_driver_register(&led_pwm_driver);
}

static void __exit led_pwm_exit(void)
{
	platform_driver_unregister(&led_pwm_driver);
}

module_init(led_pwm_init);
module_exit(led_pwm_exit);

MODULE_AUTHOR("Luotao Fu <l.fu@pengutronix.de>");
MODULE_DESCRIPTION("PWM LED driver for PXA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-pwm");
