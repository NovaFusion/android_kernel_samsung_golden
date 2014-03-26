/*
 * ST L3G4200D 3-Axis Gyroscope Driver
 *
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Chethan Krishna N <chethan.krishna@stericsson.com> for ST-Ericsson
 * Licence terms: GNU General Public Licence (GPL) version 2
 */

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <linux/l3g4200d.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

/* l3g4200d gyroscope registers */

#define WHO_AM_I				0x0F

#define CTRL_REG1				0x20    /* CTRL REG1 */
#define CTRL_REG2				0x21    /* CTRL REG2 */
#define CTRL_REG3				0x22    /* CTRL_REG3 */
#define CTRL_REG4				0x23    /* CTRL_REG4 */
#define CTRL_REG5				0x24    /* CTRL_REG5 */
#define OUT_TEMP				0x26    /* OUT_TEMP */

#define AXISDATA_REG			0x28

/** Registers Contents */

#define WHOAMI_L3G4200D			0x00D3	/* Expected content for WAI register*/

/* CTRL_REG1 */
#define PM_OFF					0x00
#define PM_ON					0x01
#define ENABLE_ALL_AXES			0x07
#define BW00					0x00
#define BW01					0x10
#define BW10					0x20
#define BW11					0x30
#define ODR00					0x00  /* ODR = 100Hz */
#define ODR01					0x40  /* ODR = 200Hz */
#define ODR10					0x80  /* ODR = 400Hz */
#define ODR11					0xC0  /* ODR = 800Hz */
#define L3G4200D_PM_BIT			3
#define L3G4200D_PM_MASK		(0x01 << L3G4200D_PM_BIT)
#define L3G4200D_ODR_BIT		4
#define L3G4200D_ODR_MASK		(0x0F << L3G4200D_ODR_BIT)
#define L3G4200D_ODR_MIN_VAL	0x00
#define L3G4200D_ODR_MAX_VAL	0x0F

/* CTRL_REG4 */
#define FS250					0x00
#define FS500					0x01
#define FS2000					0x03
#define BDU_ENABLE				0x80
#define L3G4200D_FS_BIT			4
#define L3G4200D_FS_MASK		(0x3 << L3G4200D_FS_BIT)

/* multiple byte transfer enable */
#define MULTIPLE_I2C_TR			0x80

/* device status defines */
#define DEVICE_OFF 0
#define DEVICE_ON 1
#define DEVICE_SUSPENDED 2

/*
 * L3G4200D gyroscope data
 * brief structure containing gyroscope values for yaw, pitch and roll in
 * signed short
 */

struct l3g4200d_gyro_values {
	short x;	/* x-axis angular rate data. */
	short y;	/* y-axis angluar rate data. */
	short z;	/* z-axis angular rate data. */
};

struct l3g4200d_data {
	struct i2c_client *client;
	struct mutex lock;
	struct l3g4200d_gyro_values data;
	struct l3g4200d_gyr_platform_data pdata;
	struct regulator *regulator;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	unsigned char powermode;
	unsigned char odr;
	unsigned char range;
	int device_status;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void l3g4200d_early_suspend(struct early_suspend *ddata);
static void l3g4200d_late_resume(struct early_suspend *ddata);
#endif

static int l3g4200d_write(struct l3g4200d_data *ddata, u8 reg,
				u8 val, char *msg)
{
	int ret = i2c_smbus_write_byte_data(ddata->client, reg, val);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"i2c_smbus_write_byte_data failed error %d\
				Register (%s)\n", ret, msg);
	return ret;
}

static int l3g4200d_read(struct l3g4200d_data *ddata, u8 reg, char *msg)
{
	int ret = i2c_smbus_read_byte_data(ddata->client, reg);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"i2c_smbus_read_byte_data failed error %d\
				Register (%s)\n", ret, msg);
	return ret;
}

static int l3g4200d_readdata(struct l3g4200d_data *ddata)
{
	unsigned char gyro_data[6];
	short data[3];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(ddata->client,
			AXISDATA_REG | MULTIPLE_I2C_TR, 6, gyro_data);
	if (ret < 0) {
		dev_err(&ddata->client->dev,
				"i2c_smbus_read_byte_data failed error %d\
				Register AXISDATA_REG\n", ret);
		return ret;
	}

	data[0] = (short) (((gyro_data[1]) << 8) | gyro_data[0]);
	data[1] = (short) (((gyro_data[3]) << 8) | gyro_data[2]);
	data[2] = (short) (((gyro_data[5]) << 8) | gyro_data[4]);

	data[ddata->pdata.axis_map_x] = ddata->pdata.negative_x ?
		-data[ddata->pdata.axis_map_x] : data[ddata->pdata.axis_map_x];
	data[ddata->pdata.axis_map_y] = ddata->pdata.negative_y ?
		-data[ddata->pdata.axis_map_y] : data[ddata->pdata.axis_map_y];
	data[ddata->pdata.axis_map_z] = ddata->pdata.negative_z ?
		-data[ddata->pdata.axis_map_z] : data[ddata->pdata.axis_map_z];

	ddata->data.x = data[ddata->pdata.axis_map_x];
	ddata->data.y = data[ddata->pdata.axis_map_y];
	ddata->data.z = data[ddata->pdata.axis_map_z];

	return ret;
}

static ssize_t l3g4200d_show_gyrodata(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->powermode == PM_OFF ||
			ddata->device_status == DEVICE_SUSPENDED) {
		mutex_unlock(&ddata->lock);
		return ret;
	}

	ret = l3g4200d_readdata(ddata);

	if (ret < 0) {
		mutex_unlock(&ddata->lock);
		return ret;
	}

	mutex_unlock(&ddata->lock);

	return sprintf(buf, "%8x:%8x:%8x\n", ddata->data.x, ddata->data.y,
			ddata->data.z);
}

static ssize_t l3g4200d_show_range(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->range);
}

static ssize_t l3g4200d_store_range(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);
	long received_value;
	unsigned char value;
	int error;

	error = strict_strtol(buf, 0, &received_value);
	if (error)
		return error;

	/* check if the received range is in valid range */
	if (received_value < FS250 || received_value > FS2000)
		return -EINVAL;

	mutex_lock(&ddata->lock);

	if (ddata->powermode == PM_OFF) {
		dev_info(&ddata->client->dev,
				"The device is switched off, turn it ON using powermode\n");
		mutex_unlock(&ddata->lock);
		return count;
	}

	/* enable the BDU bit */
	value = BDU_ENABLE;
	value |= ((received_value << L3G4200D_FS_BIT) & L3G4200D_FS_MASK);

	ddata->range = received_value;

	error = l3g4200d_write(ddata, CTRL_REG4, value, "CTRL_REG4");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);
	return count;
}

static ssize_t l3g4200d_show_datarate(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->odr >> L3G4200D_ODR_BIT);
}

static ssize_t l3g4200d_store_datarate(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);
	long received_value;
	unsigned char value;
	int error;

	error = strict_strtol(buf, 0, &received_value);
	if (error)
		return error;

	/* check if the received output datarate value is in valid range */
	if (received_value < L3G4200D_ODR_MIN_VAL ||
			received_value > L3G4200D_ODR_MAX_VAL)
		return -EINVAL;

	mutex_lock(&ddata->lock);

	if (ddata->powermode == PM_OFF) {
		dev_info(&ddata->client->dev,
				"The device is switched off, turn it ON using powermode\n");
		mutex_unlock(&ddata->lock);
		return count;
	}

	/*
	 * read the current contents of CTRL_REG1
	 * retain any bits set other than the odr bits
	 */
	error = l3g4200d_read(ddata, CTRL_REG1, "CTRL_REG1");

	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	} else
		value = error;

	value &= ~L3G4200D_ODR_MASK;
	value |= ((received_value << L3G4200D_ODR_BIT) & L3G4200D_ODR_MASK);

	ddata->odr = received_value << L3G4200D_ODR_BIT;

	error = l3g4200d_write(ddata, CTRL_REG1, value, "CTRL_REG1");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);
	return count;
}

static ssize_t l3g4200d_show_powermode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->powermode);
}

static ssize_t l3g4200d_store_powermode(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);
	long received_value;
	unsigned char value;
	int error;

	error = strict_strtol(buf, 0, &received_value);
	if (error)
		return error;

	/* check if the received power mode is either 0 or 1 */
	if (received_value < PM_OFF || received_value > PM_ON)
		return -EINVAL;

	mutex_lock(&ddata->lock);

	if (ddata->device_status == DEVICE_SUSPENDED &&
			received_value == PM_OFF) {
		ddata->powermode = received_value;
		mutex_unlock(&ddata->lock);
		return count;
	}

	/* if sent value is same as current value do nothing */
	if (ddata->powermode == received_value) {
		mutex_unlock(&ddata->lock);
		return count;
	}

	/* turn on the power suppliy if it was turned off previously */
	if (ddata->regulator && ddata->powermode == PM_OFF
			&& (ddata->device_status == DEVICE_OFF
				|| ddata->device_status == DEVICE_SUSPENDED)) {
		regulator_enable(ddata->regulator);
		ddata->device_status = DEVICE_ON;
	}

	/*
	 * read the current contents of CTRL_REG1
	 * retain any bits set other than the power bit
	 */
	error = l3g4200d_read(ddata, CTRL_REG1, "CTRL_REG1");

	if (error < 0) {
		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
		mutex_unlock(&ddata->lock);
		return error;
	} else
		value = error;

	value &= ~L3G4200D_PM_MASK;
	value |= ((received_value << L3G4200D_PM_BIT) & L3G4200D_PM_MASK);

	ddata->powermode = received_value;

	error = l3g4200d_write(ddata, CTRL_REG1, value, "CTRL_REG1");
	if (error < 0) {
		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
		mutex_unlock(&ddata->lock);
		return error;
	}

	if (received_value == PM_OFF) {
		/* set the other configuration values to defaults */
		ddata->odr = ODR00 | BW00;
		ddata->range = FS250;

		/* turn off the power supply */
		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
	}
	mutex_unlock(&ddata->lock);
	return count;
}

static ssize_t l3g4200d_show_gyrotemp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct l3g4200d_data *ddata = platform_get_drvdata(pdev);
	int ret;

	if (ddata->powermode == PM_OFF ||
			ddata->device_status == DEVICE_SUSPENDED)
		return -EINVAL;

	ret = l3g4200d_read(ddata, OUT_TEMP, "OUT_TEMP");
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static DEVICE_ATTR(gyrodata, S_IRUGO, l3g4200d_show_gyrodata, NULL);

static DEVICE_ATTR(range, S_IRUGO | S_IWUSR,
		l3g4200d_show_range, l3g4200d_store_range);

static DEVICE_ATTR(datarate, S_IRUGO | S_IWUSR,
		l3g4200d_show_datarate, l3g4200d_store_datarate);

static DEVICE_ATTR(powermode, S_IRUGO | S_IWUSR,
		l3g4200d_show_powermode, l3g4200d_store_powermode);

static DEVICE_ATTR(gyrotemp, S_IRUGO, l3g4200d_show_gyrotemp, NULL);

static struct attribute *l3g4200d_attributes[] = {
	&dev_attr_gyrodata.attr,
	&dev_attr_range.attr,
	&dev_attr_datarate.attr,
	&dev_attr_powermode.attr,
	&dev_attr_gyrotemp.attr,
	NULL
};

static const struct attribute_group l3g4200d_attr_group = {
	.attrs = l3g4200d_attributes,
};

static int __devinit l3g4200d_probe(struct i2c_client *client,
		const struct i2c_device_id *devid)
{
	int ret = -1;
	struct l3g4200d_data *ddata = NULL;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		goto exit;

	ddata = kzalloc(sizeof(struct l3g4200d_data), GFP_KERNEL);
	if (ddata == NULL) {
		dev_err(&client->dev, "memory alocation failed\n");
		ret = -ENOMEM;
		goto exit;
	}

	ddata->client = client;
	i2c_set_clientdata(client, ddata);

	memcpy(&ddata->pdata, client->dev.platform_data, sizeof(ddata->pdata));
	/* store default values in the data structure */
	ddata->odr = ODR00 | BW00;
	ddata->range = FS250;
	ddata->powermode = PM_OFF;
	ddata->device_status = DEVICE_OFF;

	dev_set_name(&client->dev, ddata->pdata.name_gyr);

	ddata->regulator = regulator_get(&client->dev, "vdd");
	if (IS_ERR(ddata->regulator)) {
		dev_err(&client->dev, "failed to get regulator\n");
		ret = PTR_ERR(ddata->regulator);
		ddata->regulator = NULL;
		goto error_op_failed;
	}

	if (ddata->regulator) {
		regulator_enable(ddata->regulator);
		ddata->device_status = DEVICE_ON;
	}

	ret = l3g4200d_read(ddata, WHO_AM_I, "WHO_AM_I");
	if (ret < 0)
		goto exit_free_regulator;

	if (ret == WHOAMI_L3G4200D)
		dev_info(&client->dev, "3-Axis Gyroscope device identification: %d\n", ret);
	else
		dev_info(&client->dev, "Gyroscope identification did not match\n");

	mutex_init(&ddata->lock);

	ret = sysfs_create_group(&client->dev.kobj, &l3g4200d_attr_group);
	if (ret)
		goto exit_free_regulator;
#ifdef CONFIG_HAS_EARLYSUSPEND
	ddata->early_suspend.level =
		EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ddata->early_suspend.suspend = l3g4200d_early_suspend;
	ddata->early_suspend.resume = l3g4200d_late_resume;
	register_early_suspend(&ddata->early_suspend);
#endif

	/*
	 * turn off the supplies until somebody turns on the device
	 * using l3g4200d_store_powermode
	 */
	if (ddata->device_status == DEVICE_ON && ddata->regulator) {
		regulator_disable(ddata->regulator);
		ddata->device_status = DEVICE_OFF;
	}

	return ret;

exit_free_regulator:
	if (ddata->device_status == DEVICE_ON && ddata->regulator) {
		regulator_disable(ddata->regulator);
		regulator_put(ddata->regulator);
		ddata->device_status = DEVICE_OFF;
	}
error_op_failed:
	kfree(ddata);
exit:
	dev_err(&client->dev, "probe function failed %x\n", ret);
	return ret;
}

static int __devexit l3g4200d_remove(struct i2c_client *client)
{
	struct l3g4200d_data *ddata;
	ddata = i2c_get_clientdata(client);
	sysfs_remove_group(&client->dev.kobj, &l3g4200d_attr_group);

	/* safer to turn off the device */
	if (ddata->powermode != PM_OFF) {
		l3g4200d_write(ddata, CTRL_REG1, PM_OFF, "CONTROL");
		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			regulator_put(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
	}

	i2c_set_clientdata(client, NULL);
	kfree(ddata);

	return 0;
}
#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_PM)

static int l3g4200d_do_suspend(struct l3g4200d_data *ddata)
{
	int ret;

	mutex_lock(&ddata->lock);

	if (ddata->powermode == PM_OFF) {
		mutex_unlock(&ddata->lock);
		return 0;
	}

	ret = l3g4200d_write(ddata, CTRL_REG1, PM_OFF, "CONTROL");

	/* turn off the power when suspending the device */
	if (ddata->regulator)
		regulator_disable(ddata->regulator);

	ddata->device_status = DEVICE_SUSPENDED;

	mutex_unlock(&ddata->lock);
	return ret;
}

static int l3g4200d_do_resume(struct l3g4200d_data *ddata)
{
	unsigned char range_value;
	unsigned char shifted_powermode = (ddata->powermode << L3G4200D_PM_BIT);
	unsigned char shifted_odr = (ddata->odr << L3G4200D_ODR_BIT);
	unsigned context = ((shifted_powermode | shifted_odr) | ENABLE_ALL_AXES);
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->device_status == DEVICE_ON)
		goto fail;

	/* in correct mode, no need to change it */
	if (ddata->powermode == PM_OFF) {
		ddata->device_status = DEVICE_OFF;
		goto fail;
	} else {
		 ddata->device_status = DEVICE_ON;
	}

	/* turn on the power when resuming the device */
	if (ddata->regulator)
		regulator_enable(ddata->regulator);

	ret = l3g4200d_write(ddata, CTRL_REG1, context, "CONTROL");
	if (ret < 0)
		goto fail;

	range_value = ddata->range;
	range_value <<= L3G4200D_FS_BIT;
	range_value |= BDU_ENABLE;

	ret = l3g4200d_write(ddata, CTRL_REG4, range_value, "RANGE");

fail:
	mutex_unlock(&ddata->lock);
	return ret;
}
#endif

#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
static int l3g4200d_suspend(struct device *dev)
{
	struct l3g4200d_data *ddata;
	int ret;

	ddata = dev_get_drvdata(dev);

	ret = l3g4200d_do_suspend(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while suspending the device\n");

	return ret;
}

static int l3g4200d_resume(struct device *dev)
{
	struct l3g4200d_data *ddata;
	int ret;

	ddata = dev_get_drvdata(dev);

	ret = l3g4200d_do_resume(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while resuming the device\n");

	return ret;
}

static const struct dev_pm_ops l3g4200d_dev_pm_ops = {
	.suspend = l3g4200d_suspend,
	.resume = l3g4200d_resume,
};
#endif
#else
static void l3g4200d_early_suspend(struct early_suspend *data)
{
	struct l3g4200d_data *ddata =
		container_of(data, struct l3g4200d_data, early_suspend);
	int ret;

	ret = l3g4200d_do_suspend(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while suspending the device\n");
}

static void l3g4200d_late_resume(struct early_suspend *data)
{
	struct l3g4200d_data *ddata =
		container_of(data, struct l3g4200d_data, early_suspend);
	int ret;

	ret = l3g4200d_do_resume(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while resuming the device\n");
}
#endif

static const struct i2c_device_id  l3g4200d_id[] = {
	{"l3g4200d", 0 },
	{ },
};

static struct i2c_driver l3g4200d_driver = {
	.driver = {
		.name = "l3g4200d",
#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
		.pm = &l3g4200d_dev_pm_ops,
#endif
	},
	.probe = l3g4200d_probe,
	.remove = l3g4200d_remove,
	.id_table = l3g4200d_id,
};

static int __init l3g4200d_init(void)
{
	return i2c_add_driver(&l3g4200d_driver);
}

static void __exit l3g4200d_exit(void)
{
	i2c_del_driver(&l3g4200d_driver);
}

module_init(l3g4200d_init);
module_exit(l3g4200d_exit);

MODULE_DESCRIPTION("l3g4200d digital gyroscope driver");
MODULE_AUTHOR("Chethan Krishna N");
MODULE_LICENSE("GPL");
