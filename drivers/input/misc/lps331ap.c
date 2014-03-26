/*
 * Copyright (C) 2011 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 *Community status:
 *The driver has already been released by Samsung into the community.
 *
 *Download URL:
 *http://opensource.samsung.com
 *Classification Mobile Phone
 *Model GT-N7000
 *S/W GT-N7000_OpenSource.zip
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input/lps331ap.h>

/* device id */
#define DEVICE_ID		0xBB

/* Register definitions */
#define REF_P_XL		0x08
#define REF_P_L			0x09
#define REF_P_H			0x0A
#define REF_T_L			0x0B
#define REF_T_H			0x0C
#define WHO_AM_I		0x0F
#define RES_CONF		0x10
#define CTRL_REG1		0x20
#define CTRL_REG2		0x21
#define CTRL_REG3		0x22
#define INT_CFG_REG		0x23
#define INT_SOURCE_REG		0x24
#define THS_P_LOW_REG		0x25
#define THS_P_HIGH_REG		0x26
#define STATUS_REG		0x27
#define PRESS_POUT_XL_REH	0x28
#define PRESS_OUT_L		0x29
#define PRESS_OUT_H		0x2A
#define TEMP_OUT_L		0x2B
#define TEMP_OUT_H		0x2C
#define LAST_REG		0x40
#define AUTO_INCREMENT		0x80

/* odr settings */
#define ODR_MASK		0x70
#define ODR250			0x70
#define ODR125			0x60
#define ODR70			0x50
#define ODR10			0x01

/* poll delays */
#define DELAY_LOWBOUND		(50 * NSEC_PER_MSEC)
#define DELAY_DEFAULT		(200 * NSEC_PER_MSEC)

/* pressure and temperature min, max ranges */
#define PRESSURE_MAX		(1260 * 4096)
#define PRESSURE_MIN		(260 * 4096)
#define TEMP_MAX		(300 * 480)
#define TEMP_MIN		(-300 * 480)
#define SEA_LEVEL_MAX		(999999)
#define SEA_LEVEL_MIN		(-1)

/*pressure and temperature data status*/
#define DATA_OVERRUN	0x30 /*data overrun*/
#define PRESSURE_AVAIL	0x20 /*pressure data is available*/
#define TEMP_AVAIL		0x01 /*Temperature data is available*/

static const struct odr_delay {
	u8 odr;			/* odr reg setting */
	u64 delay_ns;		/* odr in ns */
} odr_delay_table[] = {
	{ ODR250,   40000000LL }, /* 25.0Hz */
	{ ODR125,   80000000LL }, /* 12.5Hz */
	{  ODR70,  142857142LL }, /*  7.0Hz */
	{  ODR10, 1000000000LL }, /*  1.0Hz */
};

struct barometer_data {
	struct i2c_client *client;
	struct mutex lock;
	struct workqueue_struct *wq;
	struct work_struct work_pressure;
	struct input_dev *input_dev;
	struct hrtimer timer;
	struct class *class;
	struct device *dev;
	bool enabled;
	int temperature;
	int pressure;
	ktime_t poll_delay;
	u8 ctrl_reg;
};

static const char default_regs[] = {
	0x84, /* CTRL_REG1: power on, block data update */
	0x6A, /* RES_CONF : higher precision */
};

static int barometer_enable(struct barometer_data *barom)
{
	int err = 0;

	/* append power on and BDU mode to ctrl_reg */
	barom->ctrl_reg |= default_regs[0];

	/*. power on */
	err = i2c_smbus_write_byte_data(barom->client,
			CTRL_REG1, barom->ctrl_reg);

	if (err < 0) {
		dev_err(barom->dev, "%s: power on failed\n", __func__);
		goto done;
	}

	if (!barom->enabled) {
		barom->enabled = true;
		hrtimer_start(&barom->timer, barom->poll_delay,
			HRTIMER_MODE_REL);
	}

done:
	return err;
}

static int barometer_disable(struct barometer_data *barom)
{
	int err = 0;

	if (barom->enabled) {
		barom->enabled = false;
		hrtimer_cancel(&barom->timer);
		cancel_work_sync(&barom->work_pressure);
	}

	/* set power-down mode */
	err = i2c_smbus_write_byte_data(barom->client, CTRL_REG1, 0x00);
	if (err < 0)
		dev_err(barom->dev, "%s: power-down failed\n", __func__);
	return err;
}

static int barometer_read_pressure_data(struct barometer_data *barom)
{
	int err = 0;
	int status;
	u8 temp_data[2];
	u8 press_data[3];

	/* prime the single shot mode to read data if it is cleared */
	err = i2c_smbus_write_byte_data(barom->client, CTRL_REG2, 0x01);

	/* check out if data is ready */
	status = i2c_smbus_read_byte_data(barom->client, STATUS_REG);
	if (status < 0) {
		dev_err(barom->dev, "%s: reading status reg failed\n",
			__func__);
		err = status;
		goto done;
	} else if (status & DATA_OVERRUN) {
		dev_dbg(barom->dev, "%s: data overrun %02x\n",
			__func__, (u8)status);
	}

	if (status & TEMP_AVAIL)  {
		/* read temperature data */
		err = i2c_smbus_read_i2c_block_data(barom->client,
				TEMP_OUT_L | AUTO_INCREMENT,
				sizeof(temp_data), temp_data);
		if (err < 0) {
			dev_err(barom->dev, "%s: TEMP_OUT i2c reading failed\n",
				__func__);
			goto done;
		} else {
			/* this is the raw register data */
			barom->temperature = (int)((s16)(temp_data[1] << 8) |
				temp_data[0]);
		}
	}

	if (status & PRESSURE_AVAIL)  {
		/* read pressure data */
		err = i2c_smbus_read_i2c_block_data(barom->client,
				PRESS_POUT_XL_REH | AUTO_INCREMENT,
				sizeof(press_data), press_data);
		if (err < 0) {
			dev_err(barom->dev, "%s: PRESS_OUT i2c reading failed\n",
				__func__);
			goto done;
		} else {
			/* this is the raw register data */
			barom->pressure = (press_data[2] << 16)	|
				(press_data[1] << 8) | press_data[0];
		}
	}

done:
	return err;
}

static void barometer_get_pressure_data(struct work_struct *work)
{
	struct barometer_data *barom =
		container_of(work, struct barometer_data, work_pressure);
	int err;

	err = barometer_read_pressure_data(barom);
	if (err < 0) {
		dev_err(barom->dev, "%s: read pressure data failed\n",
			__func__);
		return;
	}

	/* report pressure amd temperature values */
	input_report_abs(barom->input_dev, ABS_MISC, barom->temperature);
	input_report_abs(barom->input_dev, ABS_PRESSURE, barom->pressure);
	input_sync(barom->input_dev);
}

static enum hrtimer_restart barometer_timer_func(struct hrtimer *timer)
{
	struct barometer_data *barom =
		container_of(timer, struct barometer_data, timer);

	queue_work(barom->wq, &barom->work_pressure);
	hrtimer_forward_now(&barom->timer, barom->poll_delay);
	return HRTIMER_RESTART;
}

static ssize_t barometer_poll_delay_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct barometer_data *barom = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", ktime_to_ns(barom->poll_delay));
}

static ssize_t barometer_poll_delay_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct barometer_data *barom = dev_get_drvdata(dev);
	int64_t new_delay;
	int err, i;
	u8 odr_value = ODR10;
	u8 ctrl;

	err = strict_strtoll(buf, 10, &new_delay);
	if (err < 0)
		return err;

	dev_dbg(barom->dev, "%s: new delay = %lldns, old delay = %lldns\n",
		__func__, new_delay, ktime_to_ns(barom->poll_delay));

	/*
	 * round to the nearest supported ODR that is less than
	 * the requested value
	 */
	mutex_lock(&barom->lock);
	for (i = 0; i < ARRAY_SIZE(odr_delay_table); i++)
		if (new_delay <= odr_delay_table[i].delay_ns)
			break;
	if (i > 0)
		i--;

	odr_value = odr_delay_table[i].odr;
	if (odr_value != (barom->ctrl_reg & ODR_MASK)) {
		ctrl = (barom->ctrl_reg & ~ODR_MASK);
		ctrl |= odr_value;
		barom->ctrl_reg = ctrl;
		err = i2c_smbus_write_byte_data(barom->client,
						CTRL_REG1, ctrl);
	}

	err = i2c_smbus_read_byte_data(barom->client, CTRL_REG1);

	if (new_delay < DELAY_LOWBOUND)
		new_delay = DELAY_LOWBOUND;

	if (new_delay != ktime_to_ns(barom->poll_delay)) {
		hrtimer_cancel(&barom->timer);
		barom->poll_delay = ns_to_ktime(new_delay);
		hrtimer_start(&barom->timer, barom->poll_delay,
			HRTIMER_MODE_REL);
	}
	mutex_unlock(&barom->lock);

	return size;
}

static ssize_t barometer_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct barometer_data *barom = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", barom->enabled);
}

static ssize_t barometer_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int err;
	bool new_value;
	struct barometer_data *barom = dev_get_drvdata(dev);

	dev_dbg(barom->dev, "%s: enable %s\n", __func__, buf);

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		dev_err(barom->dev, "%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	dev_dbg(barom->dev, "%s: new_value = %d, old state = %d\n",
		__func__, new_value, barom->enabled);

	mutex_lock(&barom->lock);
	if (new_value) {
		err = barometer_enable(barom);
		if (err < 0) {
			dev_err(barom->dev, "%s: barometer enable failed\n",
				__func__);
		}
	} else {
		err = barometer_disable(barom);
		if (err < 0) {
			dev_err(barom->dev, "%s: barometer disable failed\n",
				__func__);
		}
	}

	mutex_unlock(&barom->lock);
	if (err < 0)
		return err;
	return size;
}


static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		barometer_poll_delay_show, barometer_poll_delay_store);

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
		barometer_enable_show, barometer_enable_store);

static struct attribute *barometer_sysfs_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL
};

static struct attribute_group barometer_attribute_group = {
	.attrs = barometer_sysfs_attrs,
};

static int setup_input_device(struct barometer_data *barom)
{
	int err;

	barom->input_dev = input_allocate_device();
	if (!barom->input_dev) {
		dev_err(barom->dev, "%s: could not allocate input device\n",
			__func__);
		return -ENOMEM;
	}

	input_set_drvdata(barom->input_dev, barom);
	barom->input_dev->name = "barometer_sensor";

	/* temperature */
	input_set_capability(barom->input_dev, EV_ABS, ABS_MISC);
	input_set_abs_params(barom->input_dev, ABS_MISC,
				TEMP_MIN, TEMP_MAX, 0, 0);

	/* sea level pressure */
	input_set_capability(barom->input_dev, EV_ABS, ABS_VOLUME);
	input_set_abs_params(barom->input_dev, ABS_VOLUME,
				SEA_LEVEL_MIN, SEA_LEVEL_MAX, 0, 0);

	/* pressure */
	input_set_capability(barom->input_dev, EV_ABS, ABS_PRESSURE);
	input_set_abs_params(barom->input_dev, ABS_PRESSURE,
				PRESSURE_MIN, PRESSURE_MAX, 0, 0);

	err = input_register_device(barom->input_dev);
	if (err) {
		dev_err(barom->dev, "%s: unable to register input polled device %s\n",
			__func__, barom->input_dev->name);
		goto err_register_device;
	}

	goto done;

err_register_device:
	input_free_device(barom->input_dev);
done:
	return err;
}

static int setup_regs(struct barometer_data *barom)
{
	int err = 0;

	/* clear(power-down) device */
	err = i2c_smbus_write_byte_data(barom->client,
					CTRL_REG1, 0x00);
	if (err < 0)
		dev_err(barom->dev, "%s: power-down failed\n", __func__);

	/* set higher precision */
	err = i2c_smbus_write_byte_data(barom->client,
					RES_CONF, default_regs[1]);
	if (err < 0)
		dev_err(barom->dev, "%s: higher precision failed\n",
		__func__);

	return err;
}

static ssize_t eeprom_check_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	/* struct bmp180_data *barom = dev_get_drvdata(dev); */
	/* to be implemented */
	return sprintf(buf, "1\n");
}

static ssize_t sea_level_pressure_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct barometer_data *barom = dev_get_drvdata(dev);
	int new_sea_level_pressure;

	sscanf(buf, "%d", &new_sea_level_pressure);

	input_report_abs(barom->input_dev, ABS_VOLUME, new_sea_level_pressure);
	input_sync(barom->input_dev);

	return size;
}

static DEVICE_ATTR(eeprom_check, S_IRUSR | S_IRGRP | S_IWGRP,
		eeprom_check_show, NULL);

static DEVICE_ATTR(sea_level_pressure, S_IRUGO | S_IWUSR | S_IWGRP,
		NULL, sea_level_pressure_store);

static int __devinit barometer_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err, ret;
	struct barometer_data *barom;

	/* initialize barometer_data */
	barom = kzalloc(sizeof(*barom), GFP_KERNEL);
	if (!barom) {
		dev_err(&client->dev, "%s: failed to allocate memory for module\n",
			__func__);
		err = -ENOMEM;
		goto exit;
	}

	/* i2c function check */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: client not i2c capable\n", __func__);
		err = -EIO;
		goto err_i2c_check;
	}

	/* read chip id */
	ret = i2c_smbus_read_byte_data(client, WHO_AM_I);
	if (ret != DEVICE_ID) {
		if (ret < 0) {
			dev_err(&client->dev, "%s: i2c for reading chip id failed\n",
								__func__);
			err = ret;
		} else {
			dev_err(&client->dev, "%s : Device identification failed\n",
								__func__);
			err = -ENODEV;
		}
		goto err_device_id;
	}

	/* setup i2c client and mutex */
	barom->client = client;
	i2c_set_clientdata(client, barom);
	mutex_init(&barom->lock);

	/* setup timer and workqueue */
	hrtimer_init(&barom->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	barom->poll_delay = ns_to_ktime(DELAY_DEFAULT);
	barom->timer.function = barometer_timer_func;

	barom->wq = create_singlethread_workqueue("barometer_wq");
	if (!barom->wq) {
		err = -ENOMEM;
		dev_err(&client->dev, "%s: could not create workqueue\n",
			__func__);
		goto err_create_workqueue;
	}

	INIT_WORK(&barom->work_pressure, barometer_get_pressure_data);

	/* setup input device */
	err = setup_input_device(barom);
	if (err) {
		dev_err(&client->dev, "%s: setup input device failed\n",
			__func__);
		goto err_setup_input_device;
	}

	/* setup interrupt */
	err = setup_regs(barom);
	if (err < 0) {
		dev_err(&client->dev, "%s: setup regs failed\n", __func__);
		goto err_setup_regs;
	}

	/* create sysfs(enable, poll_delay) */
	err = sysfs_create_group(&barom->input_dev->dev.kobj,
				&barometer_attribute_group);
	if (err) {
		dev_err(&client->dev, "%s: could not create sysfs group\n",
			__func__);
		goto err_sysfs_create_group;
	}

	/* sysfs for factory test */
	barom->class = class_create(THIS_MODULE, "sensors");
	if (IS_ERR(barom->class)) {
		err = PTR_ERR(barom->class);
		dev_err(&client->dev, "%s: class_create failed\n", __func__);
		goto err_class_create;
	}

	barom->dev = device_create(barom->class, NULL, 0,
			barom, "barometer_sensor");
	if (IS_ERR(barom->dev)) {
		err = PTR_ERR(barom->dev);
		dev_err(&client->dev, "%s: device_create failed[%d]\n",
			__func__, err);
		goto err_device_create;
	}

	err = device_create_file(barom->dev, &dev_attr_sea_level_pressure);
	if (err < 0) {
		dev_err(&client->dev, "%s: device_create_fil(sea_level_pressure) failed\n",
			__func__);
		goto err_device_create_file1;
	}

	err = device_create_file(barom->dev, &dev_attr_eeprom_check);
	if (err < 0) {
		dev_err(&client->dev, "%s: device_create_file(eeprom_check) failed\n",
			__func__);
		goto err_device_create_file2;
	}

	return 0;

err_device_create_file2:
	device_remove_file(barom->dev, &dev_attr_sea_level_pressure);
err_device_create_file1:
	device_destroy(barom->class, 0);
err_device_create:
	class_destroy(barom->class);
err_class_create:
	sysfs_remove_group(&barom->input_dev->dev.kobj,
				&barometer_attribute_group);
err_sysfs_create_group:
err_setup_regs:
	input_unregister_device(barom->input_dev);
err_setup_input_device:
	destroy_workqueue(barom->wq);
err_create_workqueue:
	mutex_destroy(&barom->lock);
err_device_id:
err_i2c_check:
	kfree(barom);
exit:
	return err;
}

static int __devexit barometer_remove(struct i2c_client *client)
{
	struct barometer_data *barom = i2c_get_clientdata(client);

	barometer_disable(barom);
	device_remove_file(barom->dev, &dev_attr_eeprom_check);
	device_remove_file(barom->dev, &dev_attr_sea_level_pressure);
	device_destroy(barom->class, 0);
	class_destroy(barom->class);
	sysfs_remove_group(&barom->input_dev->dev.kobj,
				&barometer_attribute_group);
	input_unregister_device(barom->input_dev);
	hrtimer_cancel(&barom->timer);
	cancel_work_sync(&barom->work_pressure);
	destroy_workqueue(barom->wq);
	mutex_destroy(&barom->lock);
	kfree(barom);

	return 0;
}

static int barometer_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct barometer_data *barom = i2c_get_clientdata(client);

	if (barom->enabled) {
		err = barometer_enable(barom);
		if (err < 0)
			dev_err(barom->dev, "%s: barometer enable failed\n",
			__func__);
	}

	return 0;
}

static int barometer_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct barometer_data *barom = i2c_get_clientdata(client);

	if (barom->enabled) {
		err = barometer_disable(barom);
		if (err < 0)
			dev_err(barom->dev, "%s: barometer disable failed\n",
			__func__);
	}

	return 0;
}

static const struct i2c_device_id barometer_id[] = {
	{"lps331ap", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, barometer_id);
static const struct dev_pm_ops barometer_pm_ops = {
	.suspend	= barometer_suspend,
	.resume		= barometer_resume,
};

static struct i2c_driver barometer_driver = {
	.driver = {
		.name	= "lps331ap",
		.owner	= THIS_MODULE,
		.pm	= &barometer_pm_ops,
	},
	.probe		= barometer_probe,
	.remove		= __devexit_p(barometer_remove),
	.id_table	= barometer_id,
};

static int __init barometer_init(void)
{
	return i2c_add_driver(&barometer_driver);
}

static void __exit barometer_exit(void)
{
	i2c_del_driver(&barometer_driver);
}
module_init(barometer_init);
module_exit(barometer_exit);

MODULE_AUTHOR("Seulki Lee <tim.sk.lee@samsung.com>");
MODULE_DESCRIPTION("LPS331AP digital output barometer");
MODULE_LICENSE("GPL");
