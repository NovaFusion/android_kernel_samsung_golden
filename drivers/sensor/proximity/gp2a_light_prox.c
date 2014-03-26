/*
 * Linux driver driver for proximity sensor GP2A002S00F
 * ----------------------------------------------------------------------------
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>		/* Kernel module header */
#include <linux/kernel.h>		/* snprintf() */
#include <linux/init.h>			/* __init, __exit, __devinit, __devexit */
#include <linux/slab.h>			/* kzalloc(), kfree() */
#include <linux/mutex.h>		/* DEFINE_MUTEX(), mutex_[un]lock() */
#include <linux/delay.h>		/* msleep() */
#include <linux/i2c.h>			/* struct i2c_client, i2c_*() */
#include <linux/pm.h>			/* struct dev_pm_ops */
#include <linux/miscdevice.h>		/* struct miscdevice, misc_[de]register() */
#include <linux/mod_devicetable.h>	/* MODULE_DEVICE_TABLE() */
#include <linux/sysfs.h>		/* sysfs stuff */
#include <linux/gpio.h>			/* GPIO generic functions */
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/input.h>		/* input_*() */
#include <mach/gp2a.h>			/* GP2A platform-specific stuff */
#if defined(CONFIG_AB5500_GPADC)
#include <linux/mfd/abx500/ab5500-gpadc.h>	/* GPADC0_V */
#elif defined(CONFIG_AB8500_GPADC)
#include <linux/mfd/abx500/ab8500-gpadc.h>	/* ADC_AUX2 */
#endif
#include <linux/sensors_core.h>
#include "gp2a_prox_ioctl.h"			/* GP2A ioctl interface */

/* -------------------------------------------------------------------------
 * MACROS, TYPES AND VARIABLES
 * ------------------------------------------------------------------------- */
/* Uncomment the next line to enable debug prints */
#undef	GP2A_DEBUG

#if !defined(GP2A_DEBUG) && defined(DEBUG)
#define GP2A_DEBUG
#endif

#ifdef GP2A_DEBUG
#define gp2a_dbg(_fmt, ...)	\
	printk(KERN_INFO "GP2A_DEBUG: " _fmt "\n", ## __VA_ARGS__)
#else
#define gp2a_dbg(_fmt, ...)
#endif

#define gp2a_err(_fmt, ...)	\
	printk(KERN_ERR "GP2A_ERROR: " _fmt "\n", ## __VA_ARGS__)

#define ADC_BUFFER_NUM		6
#define LIGHT_BUFFER_NUM	18

/* input device for proximity sensor */
#define USE_INPUT_DEVICE	0	/* 0 : No Use  ,  1: Use  */
#define INT_CLEAR		1	/* 0 = polling, 1 = interrupt */

/* Register map */
#define GP2A_REG_PROX		0x00
#define GP2A_REG_GAIN		0x01
#define GP2A_REG_HYS		0x02
#define GP2A_REG_CYCLE		0x03
#define GP2A_REG_OPMOD		0x04
#define GP2A_REG_CON		0x06


/* Useful bit values and masks */
#define GP2A_MSK_PROX_VO		0x01
#define GP2A_BIT_PROX_VO_NO_DETECTION	0x00
#define GP2A_BIT_PROX_VO_DETECTION	0x01

#define GP2A_BIT_GAIN_HIGHER_MODE	0x0
#define GP2A_BIT_GAIN_NORMAL_MODE	0x08

#define GP2A_BIT_CYCLE_8MS	0x04
#define GP2A_BIT_CYCLE_16MS	0x0C
#define GP2A_BIT_CYCLE_32MS	0x14
#define GP2A_BIT_CYCLE_64MS	0x1C
#define GP2A_BIT_CYCLE_128MS	0x24
#define GP2A_BIT_CYCLE_256MS	0x2C
#define GP2A_BIT_CYCLE_512MS	0x34
#define GP2A_BIT_CYCLE_1024MS	0x3C

#define GP2A_BIT_OPMOD_SSD_SHUTDOWN_MODE	0x00
#define GP2A_BIT_OPMOD_SSD_OPERATING_MODE	0x01
#define GP2A_BIT_OPMOD_VCON_NORMAL_MODE		0x00
#define GP2A_BIT_OPMOD_VCON_INTERRUPT_MODE	0x02
#define GP2A_BIT_OPMOD_ASD_INEFFECTIVE		0x00
#define GP2A_BIT_OPMOD_ASD_EFFECTIVE		0x10

#define GP2A_BIT_CON_OCON_ENABLE_VOUT		0x00
#define GP2A_BIT_CON_OCON_FORCE_VOUT_HIGH	0x18
#define GP2A_BIT_CON_OCON_FORCE_VOUT_LOW	0x10


/* GPADC constants from AB8500 spec */
#define AB8500_ADC_RESOLUTION			1023
#define AB8500_ADC_AUX2_VBUS_MAX		1350

#define GP2A_LIGHT_SENSOR_TIMER_PERIOD_MS	200

#define GP2A_PROXIMITY_POWER_ON			0x01
#define GP2A_LIGHT_SENSOR_POWER_ON		0x02

#define GP2A_POWER_OFF		0
#define GP2A_POWER_ON		1

static bool proximity_enable = 0;

#if defined(CONFIG_AB5500_GPADC)
static const int adc_table[9] = {
	250,
	400,
	600,
	800,
	1200,
	1800,
	1900,
	2100,
};
#elif defined(CONFIG_AB8500_GPADC)
static const int adc_table[9] = {
	100,
	140,
	260,
	326,
	487,
	517,
	657,
	701,
};
#endif

static u8 gp2a_original_image[8] =
{
#if defined(CONFIG_GP2A_MODE_B)
	0x00,	// REGS_PROX
	0x08,	// REGS_GAIN
	0x40,	// REGS_HYS
	0x04,	// REGS_CYCLE
	0x03,	// REGS_OPMOD
#else
	0x00,
	0x08,
	0xC2,
	0x04,
	0x01,
#endif
};

/* Device structure, stored as i2c client data */
struct gp2a_info {
	int	power_state;
	int     proximity_state;
	int	irq;
	int	gpio;
	int	value;
	bool	als_supported;
	int	alsout;
	int	lux;
	int     adc;
	int	is_sysfs_proximity_created;
	int	is_sysfs_light_created;
	int	current_operation;
	int	(*hw_teardown)(void);
	void	(*hw_pwr)(bool);
	int	delay;
	int	light_buffer;
	int	light_count;
	ktime_t light_poll_delay;
	struct timer_list	timer;
	struct hrtimer		light_timer;
	struct workqueue_struct *light_wq;
	struct work_struct	work;
	struct work_struct	work_prox;  /* for proximity sensor */
	struct work_struct	light_work;
	struct i2c_client*	client;
	struct input_dev*	prox_input;
	struct input_dev*	light_input;
#if defined(CONFIG_AB5500_GPADC)
	struct ab5500_gpadc*	gpadc;
#elif defined(CONFIG_AB8500_GPADC)
	struct ab8500_gpadc*	gpadc;
#endif
};
int prox_irq;

/*
 * Ranges for the linux input sub-system.
 * NB: The GP2A only reports a binary value.
 */
#define GP2A_INPUT_RANGE_MIN	0
#define GP2A_INPUT_RANGE_MAX	1
#define GP2A_INPUT_RANGE_FUZZ	0
#define GP2A_INPUT_RANGE_FLAT	0

/* -------------------------------------------------------------------------
 * DRIVER DECLARATIONS
 * ------------------------------------------------------------------------- */

static struct gp2a_info*  gp2a = NULL;
//static struct wake_lock   prx_wake_lock;

#if defined(CONFIG_GP2A_MODE_B)
struct workqueue_struct *gp2a_wq_prox;
#endif

/*
 * All the gp2a_dev_*() functions are MT-safe and must be called with the
 * mutex unlocked (except gp2a_dev_irq_handler of course).
 */
static int gp2a_dev_create(struct i2c_client* client);
static void gp2a_dev_destroy(void);
static int gp2a_dev_poweron(void);
static int gp2a_dev_poweroff(void);
static int gp2a_proximity_poweroff(void);
static void gp2a_dev_work_func(struct work_struct* work_prox);
static void gp2a_dev_light_work_func(struct work_struct* work);
//static int gp2a_dev_timer_reset(void);
static void gp2a_dev_timer_func(unsigned long data);
static void gp2a_dev_timer_cancel(void);

/* IRQ handler */
static irqreturn_t gp2a_dev_irq_handler(int irq, void* cookie);

/* set sysfs for light&proximity sensor test mode */
//extern int    sensors_register(struct device *dev, void * drvdata, struct device_attribute *attributes[], char *name);
static struct device *light_sensor_device;
static struct device *proximity_sensor_device;

/* -------------------------------------------------------------------------
 * I2C DRIVER DECLARATIONS
 * ------------------------------------------------------------------------- */
static int gp2a_i2c_write(struct i2c_client* client, u8 reg, u8 val);
static int gp2a_i2c_read(struct i2c_client* client,
					u8 reg, u8 *val, unsigned int len );
static int gp2a_i2c_probe(struct i2c_client* client,
				const struct i2c_device_id* id) __devinit;
static int gp2a_i2c_remove(struct i2c_client* client) __devexit;

#if defined(CONFIG_PM)
static int gp2a_i2c_suspend(struct device* dev);
static int gp2a_i2c_resume(struct device* dev);

static const struct dev_pm_ops	gp2a_pm_ops = {
	.suspend = gp2a_i2c_suspend,
	.resume  = gp2a_i2c_resume,
};
#endif

static const struct i2c_device_id gp2a_i2c_idtable[] = {
	{ GP2A_I2C_DEVICE_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, gp2a_i2c_idtable);

static struct i2c_driver gp2a_i2c_driver = {
	.driver = {
		/* This should be the same as the module name */
		.name = GP2A_I2C_DEVICE_NAME,
		.owner = THIS_MODULE,
	},
	.id_table = gp2a_i2c_idtable,
	.probe    = gp2a_i2c_probe,
	.remove   = gp2a_i2c_remove,
};

/* -------------------------------------------------------------------------
 * SYSFS ENTRIES DECLARATIONS
 * ------------------------------------------------------------------------- */
static ssize_t gp2a_sysfs_show_proximity(struct device* dev,
				struct device_attribute* attr, char* buf);
static ssize_t gp2a_sysfs_show_light(struct device* dev,
				struct device_attribute* attr, char* buf);

static DEVICE_ATTR(proximity, S_IRUGO, gp2a_sysfs_show_proximity, NULL);
static DEVICE_ATTR(light, S_IRUGO, gp2a_sysfs_show_light, NULL);

/* -------------------------------------------------------------------------
 * DRIVER IMPLEMENTATION FOR SYSFS
 * ------------------------------------------------------------------------- */
/*
 * We use this function to get the current lux value from driver.
 * So we have to provide functionality to calculate lux from adc
 */
static ssize_t lightsensor_lux_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", gp2a ? gp2a->lux : 0);
}

/*
 * Raw data from the h/w.
 */
static ssize_t lightsensor_adc_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", gp2a ? gp2a->adc : 0);
}

/*
 * To get the current state of proximity.
 */
static ssize_t proximitysensor_state_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
					gp2a ? gp2a->proximity_state : 0);
}

/*
 * We do not user this function because GP2A could return only far and near.
 */
static ssize_t proximitysensor_adc_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t light_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t light_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t light_poll_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t light_poll_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t proximity_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t proximity_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);

static DEVICE_ATTR(lightsensor_lux,
		S_IRUGO | S_IWUSR | S_IWGRP, lightsensor_lux_show,NULL);
static DEVICE_ATTR(lightsensor_adc,
		S_IRUGO | S_IWUSR | S_IWGRP, lightsensor_adc_show,NULL);

static struct device_attribute *light_sensor_attrs[] = {
	&dev_attr_lightsensor_lux,
	&dev_attr_lightsensor_adc,
	NULL,
};

static DEVICE_ATTR(state,
		S_IRUGO | S_IWUSR | S_IWGRP, proximitysensor_state_show,NULL);
static DEVICE_ATTR(adc,
		S_IRUGO | S_IWUSR | S_IWGRP, proximitysensor_adc_show,NULL);

static struct device_attribute *proximity_sensor_attrs[] = {
	&dev_attr_state,
	&dev_attr_adc,
	NULL,
};

static DEVICE_ATTR(poll_delay,
	S_IRUGO | S_IWUGO, light_poll_delay_show, light_poll_delay_store);

static struct device_attribute dev_attr_light_enable =
	__ATTR(enable, S_IRUGO | S_IWUGO,
	light_enable_show, light_enable_store);

static struct device_attribute dev_attr_proximity_enable =
	__ATTR(enable, S_IRUGO | S_IWUGO,
	proximity_enable_show, proximity_enable_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_light_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_proximity_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_I2C_PERIPHS
void gp2a_panic_display(struct i2c_adapter *pAdap)
{
	u8 value;
	int ret;

	/*
	 * Check driver has been started.
	*/
	if ( !(gp2a && gp2a->client && gp2a->client->adapter))
		return;

	/*
	 * If there is an associated LDO check to make sure it is powered, if
	 * not then we can exit as it wasn't powered when panic occurred.
	*/
	if (gp2a->power_state != GP2A_POWER_ON){
		pr_emerg("\n\n[GP2A Powered off at this time]\n");
		return;
	}

	/*
	 * If pAdap is NULL then exit with message.
	*/
	if ( !pAdap ){
		pr_emerg("\n\n%s Passed NULL pointer!\n",__func__);
		return;
	}

	/*
	 * If pAdap->algo_data is not NULL then this driver is using HW I2C,
	 *  then change adapter to use GPIO I2C panic driver.
	 * NB!Will "probably" not work on systems with dedicated I2C pins.
	*/
	if ( pAdap->algo_data ){
		gp2a->client->adapter = pAdap;
	} else {
		/*
		 * Otherwise use panic safe SW I2C algo,
		*/
		gp2a->client->adapter->algo = pAdap->algo;
	}

	pr_emerg("\n\n[Display of GP2A registers]\n");

	/* Can only read Proximity reg, all others are write only! */
	ret = gp2a_i2c_read(gp2a->client, GP2A_REG_PROX, &value, 1);

	if (ret < 0)
		pr_emerg("\t[%02d]: Failed to get value\n", GP2A_REG_PROX);
	else
		pr_emerg("\t[%02d]: 0x%02x\n", GP2A_REG_PROX, value);
}
#endif

static int _gp2a_input_create(void)
{
	int ret = 0;
	struct input_dev* input;

	input = input_allocate_device();
	if (!input) {
		gp2a_err("%s: Failed to allocate proximity sensor device.",
			__func__);
		return -ENOMEM;
	}

	input->name = "proximity_sensor";
	set_bit(EV_SYN, input->evbit);
	set_bit(EV_ABS, input->evbit);

	input_set_abs_params(input, ABS_DISTANCE,
			GP2A_INPUT_RANGE_MIN, GP2A_INPUT_RANGE_MAX,
			GP2A_INPUT_RANGE_FUZZ, GP2A_INPUT_RANGE_FLAT);

	ret = input_register_device(input);
	if (ret < 0) {
		gp2a_err("%s: Failed to register proximity sensor device(%d)",
			__func__, ret);
		input_free_device(input);
		return ret;
	}

	ret = sysfs_create_group(&input->dev.kobj, &proximity_attribute_group);
	if (ret)
		pr_err("%s: could not create sysfs group\n", __func__);

	gp2a->prox_input = input;

	input = input_allocate_device();
	if (!input) {
		gp2a_err("%s: Failed to allocate light sensor device.",
			__func__);
		return -ENOMEM;
	}

	input->name = "light_sensor";
	set_bit(EV_SYN, input->evbit);
	set_bit(EV_ABS, input->evbit);

	input_set_abs_params(input, ABS_MISC, 0, 1, 0, 0);

	ret = input_register_device(input);
	if (ret < 0) {
		gp2a_err("%s: Failed to register light sensor device(%d)",
			__func__, ret);
		input_free_device(input);
		return ret;
	}

	ret = sysfs_create_group(&input->dev.kobj, &light_attribute_group);
	if (ret)
		pr_err("%s: could not create sysfs group\n", __func__);

	gp2a->light_input = input;

	return 0;
}


static enum hrtimer_restart gp2a_light_timer_func(struct hrtimer *timer)
{
	struct gp2a_info *gp2a
			= container_of(timer, struct gp2a_info, light_timer);
	queue_work(gp2a->light_wq, &gp2a->light_work);
	hrtimer_forward_now(&gp2a->light_timer, gp2a->light_poll_delay);
	return HRTIMER_RESTART;
}

/* NB: The GP2A is in shutdown mode by default */
static int gp2a_dev_create(struct i2c_client* client)
{
	int ret = 0;
#if defined(CONFIG_GP2A_MODE_B)
	int i;
#endif
	struct gp2a_platform_data* platdata = client->dev.platform_data;

	/* 1 - Preliminary checks */
	if (gp2a) {
		pr_warn("%s: Device driver was already created!", __func__);
		goto out;
	}
	if (!platdata || !gpio_is_valid(platdata->ps_vout_gpio)) {
		gp2a_err("%s: No GPIO configured for device (%s)",
			__func__, client->name);
		ret = -ENODEV;
		goto out;
	}
	/* 2 - Allocate the structure  */
	gp2a = kzalloc(sizeof(*gp2a), GFP_KERNEL);
	if (!gp2a) {
		gp2a_err("%s: Failed to allocate %d bytes!",
			__func__, sizeof(*gp2a));
		ret = -ENOMEM;
		goto out;
	}

	gp2a->power_state = GP2A_POWER_OFF;
	gp2a->gpio = platdata->ps_vout_gpio;
	gp2a->alsout = platdata->alsout;
	gp2a->client = client;
	gp2a->hw_pwr = platdata->hw_pwr;
	gp2a->als_supported = platdata->als_supported;

#if defined(CONFIG_GP2A_MODE_B)
	gp2a_wq_prox = create_singlethread_workqueue("gp2a_wq_prox");
	if (!gp2a_wq_prox)
	    return -ENOMEM;

	INIT_WORK(&gp2a->work_prox, gp2a_dev_work_func);
#endif

	gp2a_dbg("Workqueue Settings complete\n");
	if (gp2a->als_supported) {
		/* We poll for light values using a timer. */
		hrtimer_init(&gp2a->light_timer,
				CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		gp2a->light_poll_delay = ns_to_ktime(200 * NSEC_PER_MSEC);
		gp2a->light_timer.function = gp2a_light_timer_func;

		/*
		 * the timer just fires off a work queue request.
		 * we need a thread
		 * to read the i2c (can be slow and blocking).
		 */
		gp2a->light_wq = create_singlethread_workqueue("gp2a_light_wq");
		if (!gp2a->light_wq) {
			ret = -ENOMEM;
			pr_err("%s: could not create light workqueue\n",
				__func__);
			goto err_create_light_workqueue;
		}

		INIT_WORK(&gp2a->light_work,gp2a_dev_light_work_func);
	}
	if (platdata->hw_setup) {
		ret = platdata->hw_setup(&client->dev);
		if (ret < 0) {
			pr_err("%s: Failed to setup HW (%d)", __func__, ret);
			goto out_kfree;
		}
	}

	/* 3 - Setup input device */
	ret = _gp2a_input_create();
	if (ret < 0)
		goto out_rm_input;

	/* 4 - Setup interrupt */
	/* GP2A Regs INIT SETTINGS */
	gp2a_dev_poweron();

#if defined(CONFIG_GP2A_MODE_B)
	gp2a_dbg("%s: CONFIG_GP2A_MODE_B INIT \n", __func__);
	for(i = 1; i < sizeof(gp2a_original_image)/sizeof(u8); i++) {
		gp2a_i2c_write(gp2a->client, i, gp2a_original_image[i]);
		gp2a_dbg("%s: gp2a_original_image:%x \n",
			gp2a_original_image[i]);
	}
#else
	u8 value;
	gp2a_dbg("%s: CONFIG_GP2A_MODE_A INIT \n", __func__);
	value = 0x00;
	gp2a_i2c_write(gp2a->client, (u8)(GP2A_REG_OPMOD), value);
#endif

	/* INT Settings */
	/* 4 - Setup interrupt */
	ret = gpio_to_irq(gp2a->gpio);
	if (ret < 0) {
		pr_warn("%s: Failed to convert GPIO %u to IRQ (%d)",
			__func__, gp2a->gpio, ret);
		goto out_rm_input;
	}

	gp2a_dbg("%s: gpio %d mapped to irq %d", __func__, gp2a->gpio, ret);
	gp2a->irq = ret;
	prox_irq = ret;
	ret = request_irq(gp2a->irq, gp2a_dev_irq_handler,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "GP2A_IRQ", &gp2a->work_prox);
	if (ret) {
		gp2a_err("%s: unable to request irq proximity_int (%d)\n",
			__func__, ret);
		return ret;
	}

#if !defined(CONFIG_GP2A_MODE_B)
	disable_irq(gp2a->irq);
	gp2a_dbg("%s: irq disabled, gp2a->irq = %d", __func__, gp2a->irq);
#endif
	gp2a_dbg("%s: INT Settings complete\n", __func__);

#if defined(CONFIG_GP2A_MODE_B)
	gp2a_proximity_poweroff();
#endif
	/* 5 - Setup sysfs entries */
	ret = device_create_file(&client->dev, &dev_attr_proximity);
	if (ret < 0)
		pr_warn("%s: Failed to create sysfs \"%s\" attribute (%d)",
				__func__, dev_attr_proximity.attr.name, ret);
	else
		gp2a->is_sysfs_proximity_created = 1;

	ret = device_create_file(&client->dev, &dev_attr_light);
	if (ret < 0)
		pr_warn("%s: Failed to create sysfs \"%s\" attribute (%d)",
				 __func__, dev_attr_light.attr.name, ret);
	else
		gp2a->is_sysfs_light_created = 1;

	if (gp2a->als_supported) {
		/* 6 - Get ABx500 gpadc */
#if defined(CONFIG_AB5500_GPADC)
		gp2a->gpadc = ab5500_gpadc_get("ab5500-adc.0");
		if (gp2a->gpadc < 0) {
			gp2a_err("%s(), Error acquiring ADC", __func__);
			goto out_free_irq;
		}
#elif defined(CONFIG_AB8500_GPADC)
		gp2a->gpadc = ab8500_gpadc_get();
		if (!gp2a->gpadc) {
			gp2a_err("%s(), Error acquiring ADC", __func__);
			goto out_free_irq;
		}
#endif
	}

	/* 7 - Done */
	gp2a->hw_teardown = platdata->hw_teardown;

	/* set sysfs for light sensor test mode and register with libsensors*/
	if (gp2a->als_supported) {
		ret = sensors_register(light_sensor_device, NULL,
					light_sensor_attrs, "light_sensor");
		if(ret)
			pr_err("%s: could not register proximity sensor device(%d).\n",
				__func__, ret);
	}
	ret = sensors_register(proximity_sensor_device, NULL,
				proximity_sensor_attrs, "proximity_sensor");
	if(ret)
		pr_err("%s: could not register proximity sensor device(%d).\n",
			__func__, ret);

	ret = 0;
	goto out;

out_free_irq:
	free_irq(gp2a->irq, (void*)(gp2a->gpio));

out_rm_input :
	input_unregister_device(gp2a->prox_input);
	input_unregister_device(gp2a->light_input);

out_kfree :
	destroy_workqueue(gp2a->light_wq);

err_create_light_workqueue:
	flush_work(&gp2a->work);
	kfree(gp2a);
	gp2a = NULL;
out :
	gp2a_dbg("<- %s() = %d", __func__, ret);
	return ret;
}

static void gp2a_dev_destroy(void)
{
	gp2a_dev_poweroff();

	if (!gp2a) {
		pr_warn("%s: Device driver was already destroyed!", __func__);
		goto out;
	}

	free_irq(gp2a->irq, (void*)(gp2a->gpio));
	flush_work(&gp2a->work);

	del_timer(&gp2a->timer);
	if (gp2a->als_supported)
		flush_work(&gp2a->light_work);

	if (gp2a->is_sysfs_proximity_created)
		device_remove_file(&gp2a->client->dev, &dev_attr_proximity);


	if (gp2a->is_sysfs_light_created)
		device_remove_file(&gp2a->client->dev, &dev_attr_light);


	if (gp2a->hw_teardown) {
		int ret = gp2a->hw_teardown();
		if (ret < 0)
			pr_warn("%s: Failed to teardown HW (%d)", __func__, ret);
	}

	if (gp2a->als_supported)
		destroy_workqueue(gp2a->light_wq);

	input_unregister_device(gp2a->prox_input);
	input_unregister_device(gp2a->light_input);
	kfree(gp2a);
	gp2a = NULL;

out :
	gp2a_dbg("<- %s()", __func__);
}

static int gp2a_proximity_poweron(void)
{
	int ret = 0;
#if defined(CONFIG_GP2A_MODE_B)
	u8 val;
#endif

	gp2a_dbg("%s: start!\n", __func__);

#if defined(CONFIG_GP2A_MODE_B)
	// ASD : Select switch for analog sleep function ( 0:ineffective, 1:effective )
	// OCON[1:0] : Select switches for enabling/disabling VOUT terminal
	//             ( 00:enable, 11:force to go High, 10:forcr to go Low )
	val = 0x18;	// 11:force to go High
	ret = gp2a_i2c_write(gp2a->client, GP2A_REG_CON, val);

	val = 0x40;	// HYSD enable
	ret = gp2a_i2c_write(gp2a->client, GP2A_REG_HYS, val);


	val = 0x03;	// VCON enable, SSD enable
	ret = gp2a_i2c_write(gp2a->client, GP2A_REG_OPMOD, val);
#else
	int i;
	for(i = 1; i < sizeof(gp2a_original_image)/sizeof(u8); i++)
		gp2a_i2c_write(gp2a->client,i, gp2a_original_image[i]);

#endif
	proximity_enable = 1;
	gp2a_dbg("%s: enabling irq %d", __func__, gp2a->irq);
	enable_irq(gp2a->irq);

#if defined(CONFIG_GP2A_MODE_B)
	val = 0x00;	// 00:enable
	ret = gp2a_i2c_write(gp2a->client, GP2A_REG_CON, val);
#endif

	input_report_abs(gp2a->prox_input, ABS_DISTANCE, 1);
	input_sync(gp2a->prox_input);

	gp2a_dbg("%s: end!\n", __func__);
	return ret;
}

static int gp2a_proximity_poweroff(void)
{
	u8 val = 0;
	int ret = 0;

	gp2a_dbg("%s: entry", __func__);
#if defined(CONFIG_GP2A_MODE_B)
	disable_irq_nosync(gp2a->irq);
#else
	disable_irq(gp2a->irq);
#endif

#if defined(CONFIG_GP2A_MODE_B)
	// SSD: Software shutdown function (0:shutdown mode, 1:opteration mode)
	val = 0x02;	// VCON enable, SSD disable
	ret = gp2a_i2c_write(gp2a->client, GP2A_REG_OPMOD, val);
#else
	val = 0x00;
	ret = gp2a_i2c_write(gp2a->client, GP2A_REG_OPMOD, val);
#endif
	proximity_enable = 0;

	return ret;
}

static int gp2a_light_sensor_poweron(void)
{
	gp2a_dbg("%s: start!\n", __func__);

	if (gp2a->als_supported)
		hrtimer_start(&gp2a->light_timer, gp2a->light_poll_delay,
				HRTIMER_MODE_REL);

	gp2a_dbg("%s: end!\n", __func__);
	return 0;
}

static void gp2a_light_sensor_poweroff(void)
{
	gp2a_dbg("%s: start!\n", __func__);

	if (gp2a->als_supported) {
		hrtimer_cancel(&gp2a->light_timer);
		cancel_work_sync(&gp2a->light_work);
	}
	gp2a_dbg("%s: end!\n", __func__);
}

static int gp2a_dev_poweron(void)
{
	int ret;

	gp2a_dbg("%s: start!\n", __func__);

	if (!gp2a) {
		ret = -ENODEV;
		goto out;
	}

	gp2a->light_buffer = 0;
	gp2a->light_count = 0;

	if (gp2a->hw_pwr)
		gp2a->hw_pwr(1);

	gp2a->power_state = GP2A_POWER_ON;
	gp2a_dbg("%s: gp2a power state = %d", gp2a->power_state);

out :
	gp2a_dbg("%s: end!\n", __func__);
	return ret;
}

static int gp2a_dev_poweroff(void)
{
	int ret;

	gp2a_dbg("%s: start!\n", __func__);

	if (!gp2a) {
		ret = -ENODEV;
		goto out;
	}

	gp2a->light_buffer = 0;
	gp2a->light_count = 0;

	if (gp2a->hw_pwr)
		gp2a->hw_pwr(0);

	gp2a->power_state = GP2A_POWER_OFF;
	gp2a_dbg("%s: gp2a power state = %d", gp2a->power_state);

out:
	gp2a_dbg("%s: end!\n", __func__);
	return ret;
}

static void gp2a_dev_work_func(struct work_struct* work_prox)
{
	unsigned char value;

	if (!gp2a)
		gp2a_err("%s: Pointer is NULL!\n", __func__);
	else if (gp2a->power_state == GP2A_POWER_ON) {
		int ret = 0;

		/* GP2A initialized and powered on => do the job */
		ret = gp2a_i2c_read(gp2a->client, GP2A_REG_PROX, &value, 1);
		if (ret < 0)
			pr_warn("%s: Failed to get proximity value (%d), ignored",
				__func__, ret);
		else {
			gp2a->proximity_state = (value & 0x01);

			pr_info("%s: proximity_state %d\n",
				__func__, !gp2a->proximity_state);
			input_report_abs(gp2a->prox_input, ABS_DISTANCE,
						!gp2a->proximity_state);
			input_sync(gp2a->prox_input);
		}
	}

	if(!gp2a->proximity_state)	// far
		value = 0x40;
	else				// near
		value = 0x20;

	/* reset hysteresis */
	gp2a_i2c_write(gp2a->client, (u8)(GP2A_REG_HYS), value);
	enable_irq(gp2a->irq);

	/* enabling VOUT terminal in nomal operation */
	value = 0x00;
	gp2a_i2c_write(gp2a->client, (u8)(GP2A_REG_CON), value);
}

static void gp2a_dev_light_work_func(struct work_struct* work)
{
	if (!gp2a)
		gp2a_err("%s: Pointer is NULL\n", __func__);
	else if (gp2a->power_state == GP2A_POWER_ON) {
		/* Light sensor values from AB5500 */
#if defined(CONFIG_AB5500_GPADC)
		gp2a->adc = ab5500_gpadc_convert(gp2a->gpadc, gp2a->alsout);
#elif defined(CONFIG_AB8500_GPADC)
		gp2a->adc = ab8500_gpadc_convert(gp2a->gpadc, gp2a->alsout);
#endif
		gp2a_dbg("%s: digital value of light %d\n",
			__func__,  gp2a->adc);
		if(gp2a->adc < 0)
			pr_warn("%s: Failed to get light value (%d); ignored",
				__func__, gp2a->adc);
		else {
			int i;
			/*
			 * 1350*x/1023 is the formula to convert from RAW GPADC data
			 * to voltage for AdcAux1 and AdcAux2 on the AB8500.
			 */
			for (i = 0; ARRAY_SIZE(adc_table); i++)
				if (gp2a->adc <= adc_table[i])
					break;
#if defined(CONFIG_AB5500_GPADC)
			gp2a->lux = (gp2a->adc) - 1;
#elif defined(CONFIG_AB8500_GPADC)
			gp2a->lux = gp2a->adc;
#endif
			if (gp2a->light_buffer == i) {
				if (gp2a->light_count++ == LIGHT_BUFFER_NUM) {
					input_report_abs(gp2a->light_input, ABS_MISC, gp2a->lux);
					input_sync(gp2a->light_input);
					gp2a->light_count = 0;
				}
			} else {
				gp2a->light_buffer = i;
				gp2a->light_count = 0;
			}

			goto out;
		}

out:
		return;
	}
}

static irqreturn_t gp2a_dev_irq_handler(int prox_irq, void *dev_id)
{
#if defined(CONFIG_GP2A_MODE_B)
	if(prox_irq !=-1) {
		disable_irq_nosync(prox_irq);
		schedule_work((struct work_struct*)dev_id);
	}
#endif
	return IRQ_HANDLED;
}

/* -------------------------------------------------------------------------
 * I2C DRIVER IMPLEMENTATION
 * ------------------------------------------------------------------------- */
static int gp2a_i2c_write(struct i2c_client* client, u8 reg, u8 val)
{
	int ret;
	u8 data[2];

	data[0] = reg;
	data[1] = val;
	ret = i2c_master_send(client, data, 2);
	if (ret < 0) {
		pr_err("%s: Failed to send data to GP2A (%d)\n", __func__, ret);
	} else if (ret != 2) {
		pr_err("%s: Failed to send exactly 2 bytes to GP2A (sent %d)\n", __func__, ret);
		ret = -EIO;
	} else {
		ret = 0;
	}

	return ret;
}

static int gp2a_i2c_read(struct i2c_client* client, u8 reg, u8 *val, unsigned int len )
{
	int err;
	u8 buf[2];
	struct i2c_msg msg[2];

	buf[0] = reg;
	msg[0].addr = client->addr;
	msg[0].flags = 1;
	msg[0].len = 2;
	msg[0].buf = buf;
	err = i2c_transfer(client->adapter, msg, 1);

	*val = buf[1];

	if (err < 0) {
		gp2a_err("%s: i2c transfer error\n", __func__, __LINE__);
		return err;
	}

	return 0;
}

static ssize_t light_enable_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%d\n",
		gp2a->current_operation & GP2A_LIGHT_SENSOR_POWER_ON ? 1 : 0);
}

static ssize_t light_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int enable = simple_strtoul(buf, NULL,10);

	gp2a_dbg("%s: enable %d, power state %d",
			__func__, enable, gp2a->power_state);

	if(enable) {
		if(gp2a->power_state == GP2A_POWER_OFF)
			gp2a_dev_poweron();

		if(!(gp2a->current_operation & GP2A_LIGHT_SENSOR_POWER_ON))
			gp2a_light_sensor_poweron();

		gp2a->current_operation |= GP2A_LIGHT_SENSOR_POWER_ON;
	} else {
		gp2a_light_sensor_poweroff();

		if(!(gp2a->current_operation & GP2A_PROXIMITY_POWER_ON))
			gp2a_dev_poweroff();

		gp2a->current_operation &= ~GP2A_LIGHT_SENSOR_POWER_ON;
	}

	return size;
}

static ssize_t light_poll_delay_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%lld\n", ktime_to_ns(gp2a->light_poll_delay));
}

static ssize_t light_poll_delay_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	int delay = simple_strtoul(buf, NULL,10);

	gp2a->light_poll_delay = ns_to_ktime(delay);
	return size;
}

static ssize_t proximity_enable_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%d\n",
		gp2a->current_operation & GP2A_PROXIMITY_POWER_ON ? 1 : 0);
}

static ssize_t proximity_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	int enable = simple_strtoul(buf, NULL,10);
	gp2a_dbg("%s: enable %d, power state %d",
			__func__, enable, gp2a->power_state);

	if(enable) {
		if(gp2a->power_state == GP2A_POWER_OFF)
			gp2a_dev_poweron();

		if(!(gp2a->current_operation & GP2A_PROXIMITY_POWER_ON))
			gp2a_proximity_poweron();

		gp2a->current_operation |= GP2A_PROXIMITY_POWER_ON;
	} else {
		gp2a_proximity_poweroff();

		if(!(gp2a->current_operation & GP2A_LIGHT_SENSOR_POWER_ON))
			gp2a_dev_poweroff();

		gp2a->current_operation &= ~GP2A_PROXIMITY_POWER_ON;
	}

	return size;
}

static int __devinit gp2a_i2c_probe(struct i2c_client* client,
		const struct i2c_device_id* id)
{
	int ret;
	unsigned char value;

	pr_info("%s: probe start!\n", __func__);

	gp2a_dbg("-> %s(client=%s, id=%s), i2c addr %d",
			__func__, client->name, id->name, client->addr);

	ret = gp2a_i2c_read(client, GP2A_REG_PROX, &value, 1);
	if (ret < 0){
		pr_emerg("not the gp2a\n");
		return ret;
	} else {
		pr_emerg("\t[%02d]: 0x%02x\n", GP2A_REG_PROX, value);
	}

	dev_set_name(&client->dev, client->name);

	ret = gp2a_dev_create(client);
	if(ret)
		pr_err("%s: could not create device(%d).\n", __func__, ret);

	gp2a_dbg("<- %s(client=%s) = %d", __func__, client->name, ret);

	pr_info("%s: probe success!\n", __func__);

	return ret;
}

static int __devexit gp2a_i2c_remove(struct i2c_client* client)
{
	gp2a_dbg("-> %s(client=%s)", __func__, client->name);

	sysfs_remove_group(&gp2a->light_input->dev.kobj, &light_attribute_group);
	input_unregister_device(gp2a->light_input);

	sysfs_remove_group(&gp2a->prox_input->dev.kobj, &proximity_attribute_group);
	input_unregister_device(gp2a->prox_input);

	gp2a_dev_destroy();

	gp2a_dbg("<- %s(client=%s) = 0", __func__, client->name);
	return 0;
}

#if defined(CONFIG_PM)

/* NB: The Android sensor service closes all open sensors before suspending and
 * re-opens them after resuming, so the next 2 functions are actually never
 * called; they are added just for compliance with kernel standards.
 */

static int gp2a_i2c_suspend(struct device* dev)
{
	int	ret = 0;

	gp2a_dbg("-> %s()", __func__);

//	gp2a_dev_poweroff();

	gp2a_dbg("<- %s() = %d", __func__, ret);
	return ret;
}

static int gp2a_i2c_resume(struct device* dev)
{
	int	ret = 0;

	gp2a_dbg("-> %s()", __func__);

//	gp2a_dev_poweron();

	gp2a_dbg("<- %s() = %d", __func__, ret);
	return ret;
}

#endif	/* defined(CONFIG_PM) */

/* -------------------------------------------------------------------------
 * SYSFS ENTRIES IMPLEMENTATION
 * ------------------------------------------------------------------------- */
static ssize_t gp2a_sysfs_show_proximity(struct device* dev,
		struct device_attribute* attr, char* buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", gp2a ? gp2a->proximity_state : 0);
}

static ssize_t gp2a_sysfs_show_light(struct device* dev,
		struct device_attribute* attr, char* buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", gp2a ? gp2a->lux : 0);
}

/* -------------------------------------------------------------------------
 * TIMER FUNCTIONS IMPLEMENTATION
 * ------------------------------------------------------------------------- */
 /*
static int gp2a_dev_timer_reset(void)
{
	int ret=0;

	if(!gp2a) {
		ret= -ENODEV;
		goto out;
	}

	gp2a->timer.expires = jiffies + msecs_to_jiffies(GP2A_LIGHT_SENSOR_TIMER_PERIOD_MS);
	gp2a->timer.function = gp2a_dev_timer_func;
	gp2a->timer.data=(unsigned long)&gp2a->light_work;

	add_timer(&gp2a->timer);

out:
	return ret;
}
*/

static void gp2a_dev_timer_func(unsigned long data)
{
	schedule_work((struct work_struct*)data);
}

static void gp2a_dev_timer_cancel(void)
{
	if(gp2a) {
		if (timer_pending(&gp2a->timer))
			del_timer_sync(&gp2a->timer);
        }
}

/* -------------------------------------------------------------------------
 * MODULE STUFF
 * ------------------------------------------------------------------------- */
static int __init gp2a_module_init(void)
{
	int	ret;

	ret = i2c_add_driver(&gp2a_i2c_driver);
	if (ret < 0)
		pr_err("%s: Failed to add i2c driver for GP2A (%d)", __func__, ret);

	return ret;
}

static void __exit gp2a_module_exit(void)
{
	i2c_del_driver(&gp2a_i2c_driver);
}

module_init(gp2a_module_init);
module_exit(gp2a_module_exit);

MODULE_AUTHOR("Fabrice Triboix <f.triboix@partner.samsung.com>");
MODULE_DESCRIPTION("Driver for the GP2A002S00F proximity sensor");
MODULE_LICENSE("GPL");
