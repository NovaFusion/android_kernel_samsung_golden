/* hscdtd004a_i2c.c
 *
 * GeoMagneticField device driver for I2C (HSCDTD004/HSCDTD006)
 *
 * Copyright (C) 2011-2012 ALPS ELECTRIC CO., LTD. All Rights Reserved.
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
#include <linux/regulator/consumer.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <mach/board-sec-ux500.h>

#define I2C_RETRY_DELAY  5
#define I2C_RETRIES      5

#define I2C_HSCD_ADDR    (0x0c)    /* 000 1100    */
#define I2C_BUS_NUMBER   8

#define HSCD_DRIVER_NAME "hscd_i2c"

#if 0
#define ALPS_DEBUG
#endif

#define HSCD_STBA        0x0B
#define HSCD_STBB        0x0C
#define HSCD_XOUT        0x10
#define HSCD_YOUT        0x12
#define HSCD_ZOUT        0x14
#define HSCD_XOUT_H      0x11
#define HSCD_XOUT_L      0x10
#define HSCD_YOUT_H      0x13
#define HSCD_YOUT_L      0x12
#define HSCD_ZOUT_H      0x15
#define HSCD_ZOUT_L      0x14

#define HSCD_STATUS      0x18
#define HSCD_CTRL1       0x1b
#define HSCD_CTRL2       0x1c
#define HSCD_CTRL3       0x1d

/* hscd chip id */
#define DEVICE_ID	0x49
/* hscd magnetic registers */
#define WHO_AM_I	0x0F

static struct i2c_driver hscd_driver;
static struct i2c_client *client_hscd = NULL;
static struct i2c_client *this_client;
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend hscd_early_suspend_handler;
#endif

struct hscd_power_data {
	struct regulator *regulator_vdd;
	struct regulator *regulator_vio;
};

static struct hscd_power_data hscd_power;

static atomic_t flgEna;
static atomic_t delay;
static atomic_t flgSuspend;

extern int sensors_register(struct device *dev, void * drvdata,
		struct device_attribute *attributes[], char *name);

static int hscd_i2c_readm(char *rxData, int length)
{
    int err;
    int tries = 0;

    struct i2c_msg msgs[] = {
        {
            .addr  = this_client->addr,
            .flags = 0,
            .len   = 1,
            .buf   = rxData,
        },
        {
            .addr  = this_client->addr,
            .flags = I2C_M_RD,
            .len   = length,
            .buf   = rxData,
         },
    };

    do {
        err = i2c_transfer(this_client->adapter, msgs, 2);
    } while ((err != 2) && (++tries < I2C_RETRIES));

    if (err != 2) {
        dev_err(&this_client->adapter->dev, "read transfer error\n");
        err = -EIO;
    } else {
        err = 0;
    }

    return err;
}

static int hscd_i2c_writem(char *txData, int length)
{
    int err;
    int tries = 0;
#ifdef ALPS_DEBUG
    int i;
#endif

    struct i2c_msg msg[] = {
        {
            .addr  = this_client->addr,
            .flags = 0,
            .len   = length,
            .buf   = txData,
         },
    };

#ifdef ALPS_DEBUG
    printk("[HSCD] i2c_writem : ");
    for (i=0; i<length;i++) printk("0X%02X, ", txData[i]);
    printk("\n");
#endif

    do {
        err = i2c_transfer(this_client->adapter, msg, 1);
    } while ((err != 1) && (++tries < I2C_RETRIES));

    if (err != 1) {
        dev_err(&this_client->adapter->dev, "write transfer error\n");
        err = -EIO;
    } else {
        err = 0;
    }

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

	mdelay(15);
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

int hscd_self_test_A(void)
{
    u8 sx[2], cr1[1];

    if (atomic_read(&flgSuspend) == 1) return -1;
    /* Control resister1 backup  */
    cr1[0] = HSCD_CTRL1;
    if (hscd_i2c_readm(cr1, 1)) return 1;
#ifdef ALPS_DEBUG
    else printk("[HSCD] Control resister1 value, %02X\n", cr1[0]);
#endif
    mdelay(1);

    /* Stndby Mode  */
    if (cr1[0] & 0x80) {
        sx[0] = HSCD_CTRL1;
        sx[1] = 0x60;
        if (hscd_i2c_writem(sx, 2)) return 1;
    }

    /* Get inital value of self-test-A register  */
    sx[0] = HSCD_STBA;
    hscd_i2c_readm(sx, 1);
    mdelay(1);
    sx[0] = HSCD_STBA;
    if (hscd_i2c_readm(sx, 1)) return 1;
#ifdef ALPS_DEBUG
    else printk("[HSCD] self test A register value, %02X\n", sx[0]);
#endif
    if (sx[0] != 0x55) {
        printk("error: self-test-A, initial value is %02X\n", sx[0]);
        return 2;
    }

    /* do self-test-A  */
    sx[0] = HSCD_CTRL3;
    sx[1] = 0x20;
    if (hscd_i2c_writem(sx, 2)) return 1;
    mdelay(3);

    /* Get 1st value of self-test-A register  */
    sx[0] = HSCD_STBA;
    if (hscd_i2c_readm(sx, 1)) return 1;
#ifdef ALPS_DEBUG
    else printk("[HSCD] self test A register value, %02X\n", sx[0]);
#endif
    if (sx[0] != 0xAA) {
        printk("error: self-test-A, 1st value is %02X\n", sx[0]);
        return 3;
    }
    mdelay(3);

    /* Get 2nd value of self-test-A register  */
    sx[0] = HSCD_STBA;
    if (hscd_i2c_readm(sx, 1)) return 1;
#ifdef ALPS_DEBUG
    else printk("[HSCD] self test A register value, %02X\n", sx[0]);
#endif
    if (sx[0] != 0x55) {
        printk("error: self-test-A, 2nd value is %02X\n", sx[0]);
        return 4;
    }

    /* Active Mode  */
    if (cr1[0] & 0x80) {
        sx[0] = HSCD_CTRL1;
        sx[1] = cr1[0];
        if (hscd_i2c_writem(sx, 2)) return 1;
    }

    return 0;
}

int hscd_self_test_B(void)
{
    int rc = 0;
    u8 sx[2], cr1[1];


    if (atomic_read(&flgSuspend) == 1) return -1;
    /* Control resister1 backup  */
    cr1[0] = HSCD_CTRL1;
    if (hscd_i2c_readm(cr1, 1)) return 1;
#ifdef ALPS_DEBUG
    else printk("[HSCD] Control resister1 value, %02X\n", cr1[0]);
#endif
    mdelay(1);

    /* Get inital value of self-test-B register  */
    sx[0] = HSCD_STBB;
    hscd_i2c_readm(sx, 1);
    mdelay(1);
    sx[0] = HSCD_STBB;
    hscd_i2c_readm(sx, 1);
    mdelay(1);
    sx[0] = HSCD_STBB;
    if (hscd_i2c_readm(sx, 1)) return 1;
#ifdef ALPS_DEBUG
    else printk("[HSCD] self test B register value, %02X\n", sx[0]);
#endif
    if (sx[0] != 0x55) {
        printk("error: self-test-B, initial value is %02X\n", sx[0]);
        return 2;
    }

    /* Active mode (Force state)  */
    sx[0] = HSCD_CTRL1;
    sx[1] = 0xC2;
    if (hscd_i2c_writem(sx, 2)) return 1;
    mdelay(1);

    do {
        /* do self-test-B  */
        sx[0] = HSCD_CTRL3;
        sx[1] = 0x10;
        if (hscd_i2c_writem(sx, 2)) {
            rc = 1;
            break;
        }
        mdelay(6);

        /* Get 1st value of self-test-A register  */
        sx[0] = HSCD_STBB;
        if (hscd_i2c_readm(sx, 1)) {
            rc = 1;
            break;
        }
#ifdef ALPS_DEBUG
        else printk("[HSCD] self test B register value, %02X\n", sx[0]);
#endif
        if (sx[0] != 0xAA) {
            if ((sx[0] < 0x01) || (sx[0] > 0x07)) {
                printk("error: self-test-B, 1st value is %02X\n", sx[0]);
                rc = 3;
                break;
            }
            else {
                printk("error: self-test-B, 1st value is %02X\n", sx[0]);
                rc = (int)(sx[0] | 0x10);
                break;
            }
        }
        mdelay(3);

        /* Get 2nd value of self-test-B register  */
        sx[0] = HSCD_STBB;
        if (hscd_i2c_readm(sx, 1)) {
            rc = 1;
            break;
        }
#ifdef ALPS_DEBUG
        else printk("[HSCD] self test B register value, %02X\n", sx[0]);
#endif
        if (sx[0] != 0x55) {
            printk("error: self-test-B, 2nd value is %02X\n", sx[0]);
            rc = 4;
            break;
        }
    } while(0);

    /* Active Mode  */
    if (cr1[0] & 0x80) {
        sx[0] = HSCD_CTRL1;
        sx[1] = cr1[0];
        if (hscd_i2c_writem(sx, 2)) return 1;
    }

    return rc;
}

int hscd_get_magnetic_field_data(int *xyz)
{
    int err = -1;
    int i;
    u8 sx[6];

    if (atomic_read(&flgSuspend) == 1) return err;
    sx[0] = HSCD_XOUT;
    err = hscd_i2c_readm(sx, 6);
    if (err < 0) return err;
    for (i=0; i<3; i++) {
        xyz[i] = (int) ((short)((sx[2*i+1] << 8) | (sx[2*i])));
    }

#ifdef ALPS_DEBUG
    /*** DEBUG OUTPUT - REMOVE ***/
    printk("Mag_I2C, x:%d, y:%d, z:%d\n",xyz[0], xyz[1], xyz[2]);
    /*** <end> DEBUG OUTPUT - REMOVE ***/
#endif

    return err;
}

void hscd_activate(int flgatm, int flg, int dtime)
{
    u8 buf[2];

    if (flg != 0) flg = 1;

    if      (dtime <=  10) buf[1] = (0x60 | 3<<2);        // 100Hz- 10msec
    else if (dtime <=  20) buf[1] = (0x60 | 2<<2);        //  50Hz- 20msec
    else if (dtime <=  70) buf[1] = (0x60 | 1<<2);        //  20Hz- 50msec
    else                   buf[1] = (0x60 | 0<<2);        //  10Hz-100msec
    buf[0]  = HSCD_CTRL1;
    buf[1] |= (flg<<7);
    hscd_i2c_writem(buf, 2);
    mdelay(1);

    if (flg) {
        buf[0] = HSCD_CTRL3;
        buf[1] = 0x02;
        hscd_i2c_writem(buf, 2);
    }
    mdelay(3);

    if (flgatm) {
        atomic_set(&flgEna, flg);
        atomic_set(&delay, dtime);
    }
}

static void hscd_register_init(void)
{
    int v[3];
    u8  buf[2];

#ifdef ALPS_DEBUG
    printk("[HSCD] register_init\n");
#endif

    buf[0] = HSCD_CTRL3;
    buf[1] = 0x80;
    hscd_i2c_writem(buf, 2);
    mdelay(5);

    atomic_set(&delay, 100);
    hscd_activate(0, 1, atomic_read(&delay));
    hscd_get_magnetic_field_data(v);
    printk("[HSCD] x:%d y:%d z:%d\n", v[0], v[1], v[2]);
    hscd_activate(0, 0, atomic_read(&delay));
}

static ssize_t selftest_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int result1, result2;

	if (!atomic_read(&flgEna))
		hscd_power_on();

	result1 = hscd_self_test_A();
	result2 = hscd_self_test_B();

	/*if (!atomic_read(&flgEna))
		hscd_power_off();*/

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

	if (!atomic_read(&flgEna))
		hscd_power_on();

	result = hscd_self_test_B();

	/*if (!atomic_read(&flgEna))
		hscd_power_off();*/

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
	printk("mag_raw_data_read~~~\n");
	int xyz[3] = {0, };

	if (!atomic_read(&flgEna))
		hscd_power_on();

	hscd_get_magnetic_field_data(xyz);

	/*if (!atomic_read(&flgEna))
		hscd_power_off();*/
	
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

static int hscd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

	int ret = 0;
	struct device *magnetic_device = NULL;

	this_client = client;
	
    printk("[HSCD] probe\n");
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->adapter->dev, "client not i2c capable\n");
        return -ENOMEM;
    }

    /*client_hscd = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
    if (!client_hscd) {
        dev_err(&client->adapter->dev, "failed to allocate memory for module data\n");
        return -ENOMEM;
    }*/

	hscd_power.regulator_vdd = hscd_power.regulator_vio = NULL;
	hscd_power.regulator_vdd = regulator_get(&client->dev, "vdd_alps");
	if (IS_ERR(hscd_power.regulator_vdd)) {
		ret = PTR_ERR(hscd_power.regulator_vdd);
		hscd_power.regulator_vdd = NULL;
		pr_err("%s: failed to get hscd_i2c_vdd %d\n", __func__, ret);
		goto err_setup_regulator;
		}
	if (system_rev >= GOLDEN_R0_2)	{
	hscd_power.regulator_vio = regulator_get(&client->dev, "vio_mag");
	if (IS_ERR(hscd_power.regulator_vio)) {
		ret = PTR_ERR(hscd_power.regulator_vio);
		hscd_power.regulator_vio = NULL;
		pr_err("%s: failed to get hscd_i2c_vio %d\n", __func__, ret);
		goto err_setup_regulator;
		}
	} else {
	hscd_power.regulator_vio = regulator_get(&client->dev, "vio_alps");
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
	
	/*printk("call hscd power off [%s] [%d]\n", __func__, __LINE__);
	hscd_power_off();*/

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

static int __devexit hscd_remove(struct i2c_client *client)
{
    printk("[HSCD] remove\n");
		hscd_activate(0, 0, atomic_read(&delay));
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&hscd_early_suspend_handler);
#endif
    kfree(client_hscd);
    return 0;
	}

static int hscd_suspend(struct i2c_client *client, pm_message_t mesg)
{
#ifdef ALPS_DEBUG
    printk("[HSCD] suspend\n");
#endif
    atomic_set(&flgSuspend, 1);
    hscd_activate(0, 0, atomic_read(&delay));
    return 0;
	}

static int hscd_resume(struct i2c_client *client)
{
#ifdef ALPS_DEBUG
    printk("[HSCD] resume\n");
#endif
    atomic_set(&flgSuspend, 0);
    hscd_activate(0, atomic_read(&flgEna), atomic_read(&delay));
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void hscd_early_suspend(struct early_suspend *handler)
{
#ifdef ALPS_DEBUG
    printk("[HSCD] early_suspend\n");
#endif
    hscd_suspend(client_hscd, PMSG_SUSPEND);
}

static void hscd_early_resume(struct early_suspend *handler)
{
#ifdef ALPS_DEBUG
    printk("[HSCD] early_resume\n");
#endif
    hscd_resume(client_hscd);
}
#endif

static const struct i2c_device_id ALPS_id[] = {
	{ HSCD_DRIVER_NAME, 0 },
	{ }
};

static struct i2c_driver hscd_driver = {
	.probe		= hscd_probe,
    .remove   = hscd_remove,
	.id_table	= ALPS_id,
	.driver		= {
		.name	= HSCD_DRIVER_NAME,
	},
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend  = hscd_suspend,
    .resume   = hscd_resume,
#endif
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend hscd_early_suspend_handler = {
    .suspend = hscd_early_suspend,
    .resume  = hscd_early_resume,
};
#endif

static int __init hscd_init(void)
{
	return i2c_add_driver(&hscd_driver);
}

static void __exit hscd_exit(void)
{
#ifdef ALPS_DEBUG
    printk("[HSCD] exit\n");
#endif
	i2c_del_driver(&hscd_driver);
}

module_init(hscd_init);
module_exit(hscd_exit);

EXPORT_SYMBOL(hscd_self_test_A);
EXPORT_SYMBOL(hscd_self_test_B);
EXPORT_SYMBOL(hscd_get_magnetic_field_data);
EXPORT_SYMBOL(hscd_activate);

MODULE_DESCRIPTION("Alps HSCDTD Device");
MODULE_AUTHOR("ALPS ELECTRIC CO., LTD.");
MODULE_LICENSE("GPL v2");
