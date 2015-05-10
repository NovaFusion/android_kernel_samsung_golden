/* K3DM_i2c.c
 *
 * Accelerometer device driver for I2C
 *
 * Copyright (C) 2011 ALPS ELECTRIC CO., LTD. All Rights Reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include "alps.h"

#define ACC_DRIVER_NAME			"accsns_i2c"
#define CALIBRATION_FILE_PATH	"/efs/calibration_data"
#define CALIBRATION_DATA_AMOUNT	100

/* k3g chip id */
#define DEVICE_ID	0x33
/* k3g gyroscope registers */
#define WHO_AM_I	0x0F

/* Register Name for accsns */
#define ACC_XOUT	0xA9
#define ACC_YOUT	0x2B
#define ACC_ZOUT	0x2D
#define ACC_CTR1	0x20

struct accsns_power_data {
	struct regulator *regulator_vdd;
	struct regulator *regulator_vio;
};

struct acc_data {
	int x;
	int y;
	int z;
};

static struct i2c_client *this_client;
static struct accsns_power_data accsns_power;
static struct acc_data caldata;
static atomic_t flgEna;
static atomic_t delay;

static int accel_open_calibration(void);

static int accsns_i2c_readm(u8 *rxData, int length)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
			.addr = this_client->addr,
			.flags = 0,
			.len = 1,
			.buf = rxData,
		},
		{
			.addr = this_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rxData,
		},
	};

	do {
		err = i2c_transfer(this_client->adapter, msgs, 2);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&this_client->adapter->dev, "read transfer error\n");
		err = -EIO;
	} else
		err = 0;

	return err;
}

static int accsns_i2c_writem(u8 *txData, int length)
{
	int err;
	int tries = 0;
	struct i2c_msg msg[] = {
		{
			.addr = this_client->addr,
			.flags = 0,
			.len = length,
			.buf = txData,
		},
	};

	do {
		err = i2c_transfer(this_client->adapter, msg, 1);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&this_client->adapter->dev,
					"write transfer error [%d]\n", err);
		err = -EIO;
	} else
		err = 0;

	return err;
}

static int accsns_power_on(void)
{
	int err = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (accsns_power.regulator_vdd) {
		err = regulator_enable(accsns_power.regulator_vdd);
		if (err) {
			pr_err("%s: Couldn't enable VDD %d\n", __func__, err);
			return err;
		}
	}

	if (accsns_power.regulator_vio) {
		err = regulator_enable(accsns_power.regulator_vio);
		if (err) {
			pr_err("%s: Couldn't enable VIO %d\n", __func__, err);
			return err;
		}
	}

	msleep(20);
	return err;
}

static int accsns_power_off(void)
{
	int err = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (accsns_power.regulator_vdd) {
		err = regulator_disable(accsns_power.regulator_vdd);
		if (err) {
			pr_err("%s: Couldn't disable VDD %d\n", __func__, err);
			return err;
		}
	}

	if (accsns_power.regulator_vio) {
		err = regulator_disable(accsns_power.regulator_vio);
		if (err) {
			pr_err("%s: Couldn't disable VIO %d\n", __func__, err);
			return err;
		}
	}

	return err;
}

int accsns_get_acceleration_data(int *xyz)
{
	u8 buf[5];
	int err, idx;

	if (this_client == NULL) {
		xyz[0] = xyz[1] = xyz[2] = 0;
		return -ENODEV;
	}

	buf[0] = ACC_XOUT;
	err = accsns_i2c_readm(buf, 5);
	if (err < 0)
		return err;

	for (idx = 0; idx < 3; idx++)
		xyz[idx] = (int)((s8)buf[2*idx] * 4);

	xyz[0] -= (caldata.x << 2);
	xyz[1] -= (caldata.y << 2);
	xyz[2] -= (caldata.z << 2);

	return err;
}

static int accsns_get_acceleration_rawdata(int *xyz)
{
	u8 buf[5];
	int err, idx;

	buf[0] = ACC_XOUT;
	err = accsns_i2c_readm(buf, 5);
	if (err < 0)
		return err;

	for (idx = 0; idx < 3; idx++)
		xyz[idx] = (int)((s8)buf[2*idx]);

	return err;
}

int accsns_activate(int flgatm, int flg, int dtime)
{
	u8 buf[2];

	if (flg != 0)
		flg = 1;

	if (this_client == NULL)
		return -ENODEV;
	else if ((atomic_read(&delay) == dtime)
				&& (atomic_read(&flgEna) == flg)
				&& (flgatm == 1))
		return 0;

	if (flg == 1) {
		if (((!atomic_read(&flgEna)) && (flgatm == 1))
				|| (flgatm == 0)) {
			if (flgatm == 1)
				accel_open_calibration();
			accsns_power_on();
		}
		buf[0] = ACC_CTR1;
		buf[1] = 0x4f;
		/*
		 * 0x67 - 200Hz
		 * 0x57 - 100Hz
		 * 0x47 - 50Hz
		 * 0x37 - 25Hz
		 */
		accsns_i2c_writem(buf, 2);
		mdelay(2);
	} else {
		buf[0] = ACC_CTR1;
		buf[1] = 0x0f;
		accsns_i2c_writem(buf, 2);
		mdelay(2);

		if (((!atomic_read(&flgEna)) && (flgatm == 1))
				|| (flgatm == 0))
			accsns_power_off();
	}

	if (flgatm) {
		atomic_set(&flgEna, flg);
		atomic_set(&delay, dtime);
	}

	return 0;
}

static int accel_open_calibration(void)
{
	int err = 0;
	mm_segment_t old_fs;
	struct file *cal_filp = NULL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH, O_RDONLY, 0666);
	if (IS_ERR(cal_filp)) {
		set_fs(old_fs);
		err = PTR_ERR(cal_filp);

		caldata.x = 0;
		caldata.y = 0;
		caldata.z = 0;

		return err;
	}

	err = cal_filp->f_op->read(cal_filp,
			(char *)&caldata, 3 * sizeof(int), &cal_filp->f_pos);
	if (err != 3 * sizeof(int))
		err = -EIO;

	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	if ((caldata.x == 0xffff) && (caldata.y == 0xffff)
			&& (caldata.z == 0xffff)) {
		caldata.x = 0;
		caldata.y = 0;
		caldata.z = 0;

		return -1;
	}

	printk(KERN_INFO "%s: %d, %d, %d\n", __func__,
			caldata.x, caldata.y, caldata.z);
	return err;
}

static int accel_do_calibrate(int enable)
{
	int data[3] = { 0, };
	int sum[3] = { 0, };
	int err = 0, cnt;
	struct file *cal_filp = NULL;
	mm_segment_t old_fs;

	if (enable) {
		for (cnt = 0; cnt < CALIBRATION_DATA_AMOUNT; cnt++) {
			err = accsns_get_acceleration_rawdata(data);
			if (err < 0) {
				pr_err("%s: accel_read_accel_raw_xyz() "
						"failed in the %dth loop\n",
						__func__, cnt);
				return err;
			}

			sum[0] += data[0];
			sum[1] += data[1];
			sum[2] += (data[2] - 64);
		}

		caldata.x = (sum[0] / CALIBRATION_DATA_AMOUNT);
		caldata.y = (sum[1] / CALIBRATION_DATA_AMOUNT);
		caldata.z = (sum[2] / CALIBRATION_DATA_AMOUNT);
	} else {
		caldata.x = 0xffff;
		caldata.y = 0xffff;
		caldata.z = 0xffff;
	}

	printk(KERN_INFO "%s: cal data (%d,%d,%d)\n", __func__,
			caldata.x, caldata.y, caldata.z);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH,
			O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (IS_ERR(cal_filp)) {
		pr_err("%s: Can't open calibration file\n", __func__);
		set_fs(old_fs);
		err = PTR_ERR(cal_filp);
		return err;
	}

	err = cal_filp->f_op->write(cal_filp,
			(char *)&caldata, 3 * sizeof(int), &cal_filp->f_pos);
	if (err != 3 * sizeof(int)) {
		pr_err("%s: Can't write the cal data to file\n", __func__);
		err = -EIO;
	}

	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	return err;
}

static ssize_t accel_calibration_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int err;
	int count = 0;

	err = accel_open_calibration();

	if (err < 0)
		pr_err("%s: accel_open_calibration() failed\n", __func__);

	printk(KERN_INFO "%d %d %d %d\n",
			err, caldata.x, caldata.y, caldata.z);

	count = sprintf(buf, "%d %d %d %d\n",
					err, caldata.x, caldata.y, caldata.z);
	return count;
}

static ssize_t accel_calibration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	int err;
	int64_t enable;

	err = strict_strtoll(buf, 10, &enable);
	if (err < 0)
		return err;

	if (!atomic_read(&flgEna))
		accsns_power_on();

	err = accel_do_calibrate((int)enable);
	if (err < 0)
		pr_err("%s: accel_do_calibrate() failed\n", __func__);

	if (!enable) {
		caldata.x = 0;
		caldata.y = 0;
		caldata.z = 0;
	}

	if (!atomic_read(&flgEna))
		accsns_power_off();

	return size;
}

static ssize_t raw_data_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int xyz[3] = {0, };

	if (!atomic_read(&flgEna))
		accsns_power_on();

	accsns_get_acceleration_data(xyz);

	if (!atomic_read(&flgEna))
		accsns_power_off();

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
			-(xyz[1] >> 2), (xyz[0] >> 2), (xyz[2]) >> 2);
}

static ssize_t raw_data_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	pr_info("raw_data_write is work");
	return size;
}

static DEVICE_ATTR(calibration, S_IRUGO | S_IWUSR | S_IWGRP,
	accel_calibration_show, accel_calibration_store);
static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR | S_IWGRP,
	raw_data_read, raw_data_write);

static struct device_attribute *acc_attrs[] = {
	&dev_attr_calibration,
	&dev_attr_raw_data,
	NULL,
};

static int accsns_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *acc_device = NULL;

	this_client = client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->adapter->dev, "client not i2c capable\n");
		ret = -ENOMEM;
		goto exit;
	}

	/* regulator output enable/disable control */
	accsns_power.regulator_vdd = accsns_power.regulator_vio = NULL;
	accsns_power.regulator_vdd = regulator_get(&client->dev, "vdd_acc");
	if (IS_ERR(accsns_power.regulator_vdd)) {
		ret = PTR_ERR(accsns_power.regulator_vdd);
		pr_err("%s: failed to get accsns_i2c_vdd %d\n", __func__, ret);
		goto err_setup_regulator;
	}

	accsns_power.regulator_vio = regulator_get(&client->dev, "vio_acc");
	if (IS_ERR(accsns_power.regulator_vio)) {
		ret = PTR_ERR(accsns_power.regulator_vio);
		pr_err("%s: failed to get accsns_i2c_vio %d\n", __func__, ret);
		goto err_setup_regulator;
	}

	accsns_power_on();
	/* read chip id */
	ret = i2c_smbus_read_byte_data(this_client, WHO_AM_I);
	pr_info("%s : device ID = 0x%x, reading ID = 0x%x\n", __func__,
		DEVICE_ID, ret);
	if (ret == DEVICE_ID) /* Normal Operation */
		ret = 0;
	else {
		if (ret < 0)
			pr_err("%s: i2c for reading chip id failed\n",
			       __func__);
		else {
			pr_err("%s : Device identification failed\n",
			       __func__);
			ret = -ENODEV;
		}
		goto err_setup_regulator;
	}
	accsns_power_off();

	sensors_register(acc_device, NULL, acc_attrs, "accelerometer_sensor");

	atomic_set(&flgEna, 0);
	atomic_set(&delay, 100);

	pr_info("%s: success.\n", __func__);
	return 0;

err_setup_regulator:
	if (accsns_power.regulator_vdd) {
		regulator_disable(accsns_power.regulator_vdd);
		regulator_put(accsns_power.regulator_vdd);
	}
	if (accsns_power.regulator_vio) {
		regulator_disable(accsns_power.regulator_vio);
		regulator_put(accsns_power.regulator_vio);
	}
exit:
	this_client = NULL;
	pr_err("%s: failed!\n", __func__);
	return ret;
}

static int accsns_remove(struct i2c_client *client)
{
	if (atomic_read(&flgEna))
		accsns_activate(0, 0, atomic_read(&delay));

	if (accsns_power.regulator_vdd) {
		regulator_disable(accsns_power.regulator_vdd);
		regulator_put(accsns_power.regulator_vdd);
	}

	if (accsns_power.regulator_vio) {
		regulator_disable(accsns_power.regulator_vio);
		regulator_put(accsns_power.regulator_vio);
	}

	this_client = NULL;

	return 0;
}

static int accsns_suspend(struct device *dev)
{
	if (atomic_read(&flgEna))
		accsns_activate(0, 0, atomic_read(&delay));

	return 0;
}

static int accsns_resume(struct device *dev)
{
	if (atomic_read(&flgEna))
		accsns_activate(0, 1, atomic_read(&delay));

	return 0;
}

static const struct i2c_device_id accsns_id[] = {
	{ ACC_DRIVER_NAME, 0 },
	{ }
};

static const struct dev_pm_ops accsns_pm_ops = {
	.suspend = accsns_suspend,
	.resume = accsns_resume
};

static struct i2c_driver accsns_driver = {
	.probe     = accsns_probe,
	.remove = __devexit_p(accsns_remove),
	.id_table  = accsns_id,
	.driver    = {
		.name	= ACC_DRIVER_NAME,
		.pm = &accsns_pm_ops,
	},
};

static int __init accsns_init(void)
{
	return i2c_add_driver(&accsns_driver);
}

static void __exit accsns_exit(void)
{
	i2c_del_driver(&accsns_driver);
}

module_init(accsns_init);
module_exit(accsns_exit);

MODULE_DESCRIPTION("Alps Accelerometer Device");
MODULE_AUTHOR("ALPS ELECTRIC CO., LTD.");
MODULE_LICENSE("GPL v2");
