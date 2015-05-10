#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <mach/board-sec-ux500.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include "alps.h"

#define HSCD_DRIVER_NAME "hscd_i2c"

#define HSCD_STBA		0x0B
#define HSCD_STBB		0x0C
#define HSCD_XOUT		0x10
#define HSCD_YOUT		0x12
#define HSCD_ZOUT		0x14
#define HSCD_XOUT_H		0x11
#define HSCD_XOUT_L		0x10
#define HSCD_YOUT_H		0x13
#define HSCD_YOUT_L		0x12
#define HSCD_ZOUT_H		0x15
#define HSCD_ZOUT_L		0x14

#define HSCD_STATUS		0x18
#define HSCD_CTRL1		0x1b
#define HSCD_CTRL2		0x1c
#define HSCD_CTRL3		0x1d
#define HSCD_CTRL4		0x28

/* hscd chip id */
#define DEVICE_ID	0x49
/* hscd magnetic registers */
#define WHO_AM_I	0x0F

struct hscd_power_data {
	struct regulator *regulator_vdd;
	struct regulator *regulator_vio;
};

static struct i2c_client *this_client;
static struct hscd_power_data hscd_power;
static atomic_t flgEna;
static atomic_t delay;

static int hscd_i2c_readm(char *rxData, int length)
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

static int hscd_i2c_writem(char *txData, int length)
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
		dev_err(&this_client->adapter->dev, "write transfer error\n");
		err = -EIO;
	} else
		err = 0;

	return err;
}

static int hscd_power_on(void)
{
	int err = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (hscd_power.regulator_vdd) {
		err = regulator_enable(hscd_power.regulator_vdd);
		if (err) {
			pr_err("%s: Couldn't enable VDD %d\n", __func__, err);
			return err;
		}
	}

	if (hscd_power.regulator_vio) {
		err = regulator_enable(hscd_power.regulator_vio);
		if (err) {
			pr_err("%s: Couldn't enable VIO %d\n", __func__, err);
			return err;
		}
	}

	msleep(60);
	return err;
}

static int hscd_power_off(void)
{
	int err = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (hscd_power.regulator_vdd) {
		err = regulator_disable(hscd_power.regulator_vdd);
		if (err) {
			pr_err("%s: Couldn't disable VDD %d\n", __func__, err);
			return err;
		}
	}

	if (hscd_power.regulator_vio) {
		err = regulator_disable(hscd_power.regulator_vio);
		if (err) {
			pr_err("%s: Couldn't disable VIO %d\n", __func__, err);
			return err;
		}
	}

	return err;
}
static int hscd_self_test_A(void)
{
	u8 buf[2], cr1[1];

	/* Control resister1 backup  */
	cr1[0] = HSCD_CTRL1;
	if (hscd_i2c_readm(cr1, 1))
		return 1;
	mdelay(1);

	/* Stndby Mode  */
	if (cr1[0] & 0x80) {
		buf[0] = HSCD_CTRL1;
		buf[1] = 0x60;

		if (hscd_i2c_writem(buf, 2))
			return 1;
	}

	/* Get inital value of self-test-A register  */
	buf[0] = HSCD_STBA;
	if (hscd_i2c_readm(buf, 1))
		return 1;

	if (buf[0] != 0x55) {
		pr_err("%s: self-test-A, initial value is 0x%x\n",
			__func__, buf[0]);
		return 2;
	}

    /* do self-test-A  */
	buf[0] = HSCD_CTRL3;
	buf[1] = 0x20;
	if (hscd_i2c_writem(buf, 2))
		return 1;
	mdelay(1);

	/* Get 1st value of self-test-A register  */
	buf[0] = HSCD_STBA;
	if (hscd_i2c_readm(buf, 1))
		return 1;

	if (buf[0] != 0xAA) {
		pr_err("%s: self-test-A, 1st value is 0x%x\n",
			__func__, buf[0]);
		return 3;
	}
	mdelay(1);

	/* Get 2nd value of self-test-A register  */
	buf[0] = HSCD_STBA;
	if (hscd_i2c_readm(buf, 1))
		return 1;

	if (buf[0] != 0x55) {
		pr_err("%s: self-test-A, 2nd value is 0x%x\n",
			__func__, buf[0]);
		return 4;
	}

	/* Active Mode  */
	if (cr1[0] & 0x80) {
		buf[0] = HSCD_CTRL1;
		buf[1] = cr1[0];
		if (hscd_i2c_writem(buf, 2))
			return 1;
	}

	return 0;
}

static int hscd_self_test_B(void)
{
	int rc = 0;
	u8 buf[2], cr1[1];

	/* Control resister1 backup  */
	cr1[0] = HSCD_CTRL1;
	if (hscd_i2c_readm(cr1, 1))
		return 1;
	mdelay(1);

	/* Get inital value of self-test-B register  */
	buf[0] = HSCD_STBB;
	if (hscd_i2c_readm(buf, 1))
		return 1;

	if (buf[0] != 0x55) {
		pr_err("%s: self-test-B, initial value is 0x%x\n",
			__func__, buf[0]);
		return 2;
	}

	/* Active mode (Force state)  */
	buf[0] = HSCD_CTRL1;
	buf[1] = 0xC2;
	if (hscd_i2c_writem(buf, 2))
		return 1;
	mdelay(1);

	do {
		/* do self-test-B  */
		buf[0] = HSCD_CTRL3;
		buf[1] = 0x10;
		if (hscd_i2c_writem(buf, 2)) {
			rc = 1;
			break;
		}
		mdelay(4);

		/* Get 1st value of self-test-A register  */
		buf[0] = HSCD_STBB;
		if (hscd_i2c_readm(buf, 1)) {
			rc = 1;
			break;
		}

		if (buf[0] != 0xAA) {
			if ((buf[0] < 0x01) || (buf[0] > 0x07)) {
				pr_err("%s: self-test-B, 1st value is 0x%x\n",
						__func__, buf[0]);
				rc = 3;
				break;
			} else {
				pr_err("%s: self-test-B, 1st value is 0x%x\n",
						__func__, buf[0]);
				rc = (int)(buf[0] | 0x10);
				break;
			}
		}
		mdelay(1);

		/* Get 2nd value of self-test-B register  */
		buf[0] = HSCD_STBB;
		if (hscd_i2c_readm(buf, 1)) {
			rc = 1;
			break;
		}

		if (buf[0] != 0x55) {
			pr_err("%s: self-test-B, 2nd value is 0x%x\n",
					__func__, buf[0]);
			rc = 4;
			break;
		}
	} while (0);

	/* Active Mode  */
	if (cr1[0] & 0x80) {
		buf[0] = HSCD_CTRL1;
		buf[1] = cr1[0];
		if (hscd_i2c_writem(buf, 2))
			return 1;
	}
	return rc;
}

int hscd_get_magnetic_field_data(int *xyz)
{
	int err;
	int idx;
	u8 buf[6];

	if (this_client == NULL) {
		xyz[0] = xyz[1] = xyz[2] = 0;
		return -ENODEV;
	}

	buf[0] = HSCD_XOUT;
	err = hscd_i2c_readm(buf, 6);
	if (err < 0)
		return err;

	for (idx = 0; idx < 3; idx++) {
		xyz[idx] = (int)((short)(((buf[(2 * idx) + 1] << 8))
						| (buf[2 * idx])));
	}

	return err;
}

int hscd_activate(int flgatm, int flg, int dtime)
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
#if 0
		if (((!atomic_read(&flgEna)) && (flgatm == 1))
				|| (flgatm == 0))
			hscd_power_on();
#endif
		if (dtime <=  10)	/* 100Hz-10msec */
			buf[1] = (0x60 | (3 << 2));
		else if (dtime <=  20)	/* 50Hz-20msec */
			buf[1] = (0x60 | (2 << 2));
		else if (dtime <=  60)	/* 20Hz-50msec */
			buf[1] = (0x60 | (1 << 2));
		else	/* 10Hz-100msec */
			buf[1] = (0x60 | (0 << 2));

		buf[0] = HSCD_CTRL1;
		buf[1] |= (1 << 7);

		hscd_i2c_writem(buf, 2);
		mdelay(1);

		buf[0] = HSCD_CTRL3;
		buf[1] = 0x02;
		hscd_i2c_writem(buf, 2);
	} else {
		buf[0] = HSCD_CTRL1;
		buf[1] = 0;

		hscd_i2c_writem(buf, 2);
		mdelay(1);

#if 0
		if (((!atomic_read(&flgEna)) && (flgatm == 1))
				|| (flgatm == 0))
			hscd_power_off();
#endif
	}

	if (flgatm) {
		atomic_set(&flgEna, flg);
		atomic_set(&delay, dtime);
	}

	return 0;
}

static ssize_t selftest_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int result1, result2;
#if 0
	if (!atomic_read(&flgEna))
		hscd_power_on();
#endif
	result1 = hscd_self_test_A();
	result2 = hscd_self_test_B();
#if 0
	if (!atomic_read(&flgEna))
		hscd_power_off();
#endif
	if (result1 == 0)
		result1 = 1;
	else
		result1 = 0;

	if (result2 == 0)
		result2 = 1;
	else
		result2 = 0;

	pr_info("Selftest Result is %d, %d\n", result1, result2);
	return snprintf(buf, PAGE_SIZE, "%d, %d\n", result1, result2);
}

static ssize_t status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int result;
#if 0
	if (!atomic_read(&flgEna))
		hscd_power_on();
#endif
	result = hscd_self_test_B();
#if 0
	if (!atomic_read(&flgEna))
		hscd_power_off();
#endif
	if (result == 0)
		result = 1;
	else
		result = 0;
	return snprintf(buf, PAGE_SIZE, "%d,%d\n", result, 0);
}

static ssize_t dac_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n", 0, 0, 0);
}

static ssize_t adc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int data[3];

	if (!atomic_read(&flgEna))
		hscd_activate(0, 1, 100);

	msleep(20);
	hscd_get_magnetic_field_data(data);
	pr_info("[HSCD] x: %d y: %d z: %d\n", data[0], data[1], data[2]);

	if (!atomic_read(&flgEna))
		hscd_activate(0, 0, 100);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
				data[0], data[1], data[2]);
}

static ssize_t mag_raw_data_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int xyz[3] = {0, };
#if 0
	if (!atomic_read(&flgEna))
		hscd_power_on();
#endif
	hscd_get_magnetic_field_data(xyz);

#if 0
	if (!atomic_read(&flgEna))
		hscd_power_off();
#endif
	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
			xyz[0], xyz[1], xyz[2]);
}

static DEVICE_ATTR(selftest, S_IRUGO | S_IWUSR | S_IWGRP,
	selftest_show, NULL);
static DEVICE_ATTR(status, S_IRUGO | S_IWUSR | S_IWGRP,
	status_show, NULL);
static DEVICE_ATTR(dac, S_IRUGO | S_IWUSR | S_IWGRP,
	dac_show, NULL);
static DEVICE_ATTR(adc, S_IRUGO | S_IWUSR | S_IWGRP,
	adc_show, NULL);
static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR | S_IWGRP,
	mag_raw_data_read, NULL);

static struct device_attribute *magnetic_attrs[] = {
	&dev_attr_selftest,
	&dev_attr_status,
	&dev_attr_dac,
	&dev_attr_adc,
	&dev_attr_raw_data,
	NULL,
};

static int hscd_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *magnetic_device = NULL;

	this_client = client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->adapter->dev, "client not i2c capable\n");
		ret = -ENOMEM;
		goto exit;
	}

	/* regulator output enable/disable control */
	if	(system_rev >= CODINA_TMO_R0_1)	{
		hscd_power.regulator_vdd = hscd_power.regulator_vio = NULL;
		hscd_power.regulator_vdd = regulator_get(&client->dev, "vdd_alps");
		if (IS_ERR(hscd_power.regulator_vdd)) {
			ret = PTR_ERR(hscd_power.regulator_vdd);
			hscd_power.regulator_vdd = NULL;
			pr_err("%s: failed to get hscd_i2c_vdd %d\n", __func__, ret);
			goto err_setup_regulator;
		}

		hscd_power.regulator_vio = regulator_get(&client->dev, "vio_alps");
		if (IS_ERR(hscd_power.regulator_vio)) {
			ret = PTR_ERR(hscd_power.regulator_vio);
			hscd_power.regulator_vio = NULL;
			pr_err("%s: failed to get hscd_i2c_vio %d\n", __func__, ret);
			goto err_setup_regulator;
		}
	}	else	{
		hscd_power.regulator_vdd = hscd_power.regulator_vio = NULL;
		hscd_power.regulator_vdd = regulator_get(&client->dev, "vdd_hscd");
		if (IS_ERR(hscd_power.regulator_vdd)) {
			ret = PTR_ERR(hscd_power.regulator_vdd);
			hscd_power.regulator_vdd = NULL;
			pr_err("%s: failed to get hscd_i2c_vdd %d\n", __func__, ret);
			goto err_setup_regulator;
		}

		hscd_power.regulator_vio = regulator_get(&client->dev, "vio_hscd");
		if (IS_ERR(hscd_power.regulator_vio)) {
			ret = PTR_ERR(hscd_power.regulator_vio);
			hscd_power.regulator_vio = NULL;
			pr_err("%s: failed to get hscd_i2c_vio %d\n", __func__, ret);
			goto err_setup_regulator;
		}
	}

	hscd_power_on();
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
#if 0
	hscd_power_off();
#endif
	sensors_register(magnetic_device, NULL, magnetic_attrs,
						"magnetic_sensor");

	atomic_set(&flgEna, 0);
	atomic_set(&delay, 100);

	pr_info("%s: success.\n", __func__);

	return 0;

err_setup_regulator:
	if (hscd_power.regulator_vdd) {
		regulator_disable(hscd_power.regulator_vdd);
		regulator_put(hscd_power.regulator_vdd);
	}
	if (hscd_power.regulator_vio) {
		regulator_disable(hscd_power.regulator_vio);
		regulator_put(hscd_power.regulator_vio);
	}
exit:
	this_client = NULL;
	pr_err("%s: failed!\n", __func__);
	return ret;
}

static int hscd_remove(struct i2c_client *client)
{
	if (atomic_read(&flgEna))
		hscd_activate(0, 0, atomic_read(&delay));

	if (hscd_power.regulator_vdd) {
		regulator_disable(hscd_power.regulator_vdd);
		regulator_put(hscd_power.regulator_vdd);
	}

	if (hscd_power.regulator_vio) {
		regulator_disable(hscd_power.regulator_vio);
		regulator_put(hscd_power.regulator_vio);
	}

	this_client = NULL;
	return 0;
}

static int hscd_suspend(struct device *dev)
{
	if (atomic_read(&flgEna))
		hscd_activate(0, 0, atomic_read(&delay));

	return 0;
}

static int hscd_resume(struct device *dev)
{
	if (atomic_read(&flgEna))
		hscd_activate(0, 1, atomic_read(&delay));

	return 0;
}

static const struct i2c_device_id ALPS_id[] = {
	{ HSCD_DRIVER_NAME, 0 },
	{ }
};

static const struct dev_pm_ops hscd_pm_ops = {
	.suspend = hscd_suspend,
	.resume = hscd_resume
};

static struct i2c_driver hscd_driver = {
	.probe		= hscd_probe,
	.remove = __devexit_p(hscd_remove),
	.id_table	= ALPS_id,
	.driver		= {
		.name	= HSCD_DRIVER_NAME,
		.pm = &hscd_pm_ops,
	},
};

static int __init hscd_init(void)
{
	return i2c_add_driver(&hscd_driver);
}

static void __exit hscd_exit(void)
{
	i2c_del_driver(&hscd_driver);
}

module_init(hscd_init);
module_exit(hscd_exit);

MODULE_DESCRIPTION("Alps hscd Device");
MODULE_AUTHOR("ALPS");
MODULE_LICENSE("GPL v2");
