/*
 * ST LSM303DLHC 3-Axis Accelerometer Driver
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

#include <linux/lsm303dlh.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define WHO_AM_I    0x0F

/* lsm303dlhc accelerometer registers */
#define CTRL_REG1				0x20
#define CTRL_REG2				0x21
#define CTRL_REG3				0x22
#define CTRL_REG4				0x23
#define CTRL_REG5				0x24
#define CTRL_REG6				0x25

/* lsm303dlhc accelerometer defines */
#define LSM303DLHC_A_MODE_OFF		0x00
#define LSM303DLHC_A_MODE_ON		0x04
#define LSM303DLHC_A_MODE_MAX		0x09
#define LSM303DLHC_A_CR1_MODE_BIT	4
#define LSM303DLHC_A_CR1_MODE_MASK	(0xF << LSM303DLHC_A_CR1_MODE_BIT)
 #define LSM303DLHC_A_CR1_AXIS_ENABLE 7

/* Range */
#define LSM303DLHC_A_RANGE_2G 0x00
#define LSM303DLHC_A_RANGE_4G 0x01
#define LSM303DLHC_A_RANGE_8G 0x02
#define LSM303DLHC_A_RANGE_16G 0x03
#define LSM303DLHC_A_CR4_FS_BIT 4

/* Sensitivity adjustment */
#define SHIFT_ADJ_2G 4 /*    1/16*/
#define SHIFT_ADJ_4G 3 /*    2/16*/
#define SHIFT_ADJ_8G 2 /* ~3.9/16*/
#define SHIFT_ADJ_16G 1 /* ~3.9/16*/

#define AXISDATA_REG    0x28    /* axis data */

/* lsm303dlh magnetometer registers */
#define IRA_REG_M	0x0A

/* multiple byte transfer enable */
#define MULTIPLE_I2C_TR			0x80

/* device status defines */
#define DEVICE_OFF 0
#define DEVICE_ON 1
#define DEVICE_SUSPENDED 2

struct lsm303dlhc_a_t {
	short	x;
	short	y;
	short	z;
};

/**
 * struct lsm303dlhc_a_data - data structure used by lsm303dlhc_a driver
 * @client: i2c client
 * @lock: mutex lock for sysfs operations
 * @data: lsm303dlhc_a_t struct containing x, y and z values
 * @pdata: lsm303dlh platform data
 * @regulator: regulator
 * @range: current range value of accelerometer
 * @mode: current mode of operation
 * @rate: current sampling rate
 * @shift_adjust: current shift adjust value set according to range
 * @early_suspend: early suspend structure
 * @device_status: device is ON, OFF or SUSPENDED
 * @id: accelerometer device id
 */
struct lsm303dlhc_a_data {
	struct i2c_client *client;
	/* lock for sysfs operations */
	struct mutex lock;
	struct lsm303dlhc_a_t data;
	struct lsm303dlh_platform_data pdata;
	struct regulator *regulator;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	unsigned char range;
	unsigned char mode;
	unsigned char rate;
	int shift_adjust;
	int device_status;
	int id;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void lsm303dlhc_a_early_suspend(struct early_suspend *data);
static void lsm303dlhc_a_late_resume(struct early_suspend *data);
#endif

static int lsm303dlhc_a_write(struct lsm303dlhc_a_data *ddata, u8 reg,
		u8 val, char *msg)
{
	int ret = i2c_smbus_write_byte_data(ddata->client, reg, val);
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"i2c_smbus_write_byte_data failed error %d\
			Register (%s)\n", ret, msg);
	return ret;
}

static int lsm303dlhc_a_read(struct lsm303dlhc_a_data *ddata, u8 reg, char *msg)
{
	int ret = i2c_smbus_read_byte_data(ddata->client, reg);
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"i2c_smbus_read_byte_data failed error %d\
			 Register (%s)\n", ret, msg);
	return ret;
}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_PM)
static int lsm303dlhc_a_do_suspend(struct lsm303dlhc_a_data *ddata)
{
	int ret;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLHC_A_MODE_OFF) {
		ret = 0;
		goto exit;
	}

	ret = lsm303dlhc_a_write(ddata, CTRL_REG1,
			LSM303DLHC_A_MODE_OFF, "CONTROL");

	if (ddata->regulator)
		regulator_disable(ddata->regulator);

	ddata->device_status = DEVICE_SUSPENDED;

exit:
	mutex_unlock(&ddata->lock);

	return ret;
}

static int lsm303dlhc_a_restore(struct lsm303dlhc_a_data *ddata)
{
	unsigned char reg;
	unsigned char shifted_mode = (ddata->mode << LSM303DLHC_A_CR1_MODE_BIT);
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->device_status == DEVICE_ON) {
		mutex_unlock(&ddata->lock);
		return 0;
	}

	/* in correct mode, no need to change it */
	if (ddata->mode == LSM303DLHC_A_MODE_OFF) {
		ddata->device_status = DEVICE_OFF;
		goto fail;
	} else
		ddata->device_status = DEVICE_ON;

	if (ddata->regulator)
		regulator_enable(ddata->regulator);

	/* BDU should be enabled by default/recommened */
	reg = ddata->range;
	shifted_mode |= LSM303DLHC_A_CR1_AXIS_ENABLE;

	ret = lsm303dlhc_a_write(ddata, CTRL_REG1, shifted_mode,
			"CTRL_REG1");
	if (ret < 0)
		goto fail;

	ret = lsm303dlhc_a_write(ddata, CTRL_REG4, reg, "CTRL_REG4");

	if (ret < 0)
		goto fail;

	/* write to the boot bit to reboot memory content */
	ret = lsm303dlhc_a_write(ddata, CTRL_REG5, 0x80, "CTRL_REG5");

	if (ret < 0)
		goto fail;

fail:
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"could not restore the device %d\n", ret);
	mutex_unlock(&ddata->lock);
	return ret;
}
#endif

static int lsm303dlhc_a_readdata(struct lsm303dlhc_a_data *ddata)
{
	unsigned char acc_data[6];
	short data[3];

	int ret = i2c_smbus_read_i2c_block_data(ddata->client,
			AXISDATA_REG | MULTIPLE_I2C_TR, 6, acc_data);
	if (ret < 0) {
		dev_err(&ddata->client->dev,
				"i2c_smbus_read_byte_data failed error %d\
				Register AXISDATA_REG \n", ret);
		return ret;
	}

	data[0] = (short) (((acc_data[1]) << 8) | acc_data[0]);
	data[1] = (short) (((acc_data[3]) << 8) | acc_data[2]);
	data[2] = (short) (((acc_data[5]) << 8) | acc_data[4]);

	data[0] >>= ddata->shift_adjust;
	data[1] >>= ddata->shift_adjust;
	data[2] >>= ddata->shift_adjust;

	/* taking position and orientation of x,y,z axis into account*/

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

static ssize_t lsm303dlhc_a_show_data(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlhc_a_data *ddata = platform_get_drvdata(pdev);
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLHC_A_MODE_OFF ||
			ddata->device_status == DEVICE_SUSPENDED) {
		mutex_unlock(&ddata->lock);
		return ret;
	}

	ret = lsm303dlhc_a_readdata(ddata);

	if (ret < 0) {
		mutex_unlock(&ddata->lock);
		return ret;
	}

	mutex_unlock(&ddata->lock);

	return sprintf(buf, "%8x:%8x:%8x\n", ddata->data.x, ddata->data.y,
			ddata->data.z);
}

static ssize_t lsm303dlhc_a_show_range(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlhc_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->range >> LSM303DLHC_A_CR4_FS_BIT);
}

static ssize_t lsm303dlhc_a_store_range(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlhc_a_data *ddata = platform_get_drvdata(pdev);
	long val;
	int error;

	error = strict_strtol(buf, 0, &val);
	if (error)
		return error;

	if (val < LSM303DLHC_A_RANGE_2G || val > LSM303DLHC_A_RANGE_16G)
		return -EINVAL;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLHC_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		mutex_unlock(&ddata->lock);
		return count;
	}

	ddata->range = val;
	ddata->range <<= LSM303DLHC_A_CR4_FS_BIT;

	error = lsm303dlhc_a_write(ddata, CTRL_REG4, ddata->range,
	"CTRL_REG4");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	switch (val) {
	case LSM303DLHC_A_RANGE_2G:
		ddata->shift_adjust = SHIFT_ADJ_2G;
		break;
	case LSM303DLHC_A_RANGE_4G:
		ddata->shift_adjust = SHIFT_ADJ_4G;
		break;
	case LSM303DLHC_A_RANGE_8G:
		ddata->shift_adjust = SHIFT_ADJ_8G;
		break;
	case LSM303DLHC_A_RANGE_16G:
		ddata->shift_adjust = SHIFT_ADJ_16G;
		break;
	default:
		mutex_unlock(&ddata->lock);
		return -EINVAL;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlhc_a_show_mode(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlhc_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->mode);
}

static ssize_t lsm303dlhc_a_store_mode(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlhc_a_data *ddata = platform_get_drvdata(pdev);
	long val;
	unsigned char data;
	int error;
	bool set_boot_bit = false;

	error = strict_strtol(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);

	/* not in correct range */

	if (val < LSM303DLHC_A_MODE_OFF || val > LSM303DLHC_A_MODE_MAX) {
		mutex_unlock(&ddata->lock);
		return -EINVAL;
	}

	if (ddata->device_status == DEVICE_SUSPENDED) {
		if (val == LSM303DLHC_A_MODE_OFF) {
			ddata->mode = val;
			mutex_unlock(&ddata->lock);
			return count;
		} else {
			/* device is turning on after suspend, reset memory */
			set_boot_bit = true;
		}
	}

	/*  if same mode as existing, return */
	if (ddata->mode == val) {
		mutex_unlock(&ddata->lock);
		return count;
	}

	/* turn on the supplies if already off */
	if (ddata->regulator && ddata->mode == LSM303DLHC_A_MODE_OFF
			&& (ddata->device_status == DEVICE_OFF
				|| ddata->device_status == DEVICE_SUSPENDED)) {
		regulator_enable(ddata->regulator);
		ddata->device_status = DEVICE_ON;
	}

	data = lsm303dlhc_a_read(ddata, CTRL_REG1, "CTRL_REG1");

	/*
	 * If chip doesn't get reset during suspend/resume,
	 * x,y and z axis bits are getting cleared,so set
	 * these bits to get x,y,z data.
	 */
	data |= LSM303DLHC_A_CR1_AXIS_ENABLE;

	data &= ~LSM303DLHC_A_CR1_MODE_MASK;

	ddata->mode = val;

	data |= ((val << LSM303DLHC_A_CR1_MODE_BIT)
			& LSM303DLHC_A_CR1_MODE_MASK);

	error = lsm303dlhc_a_write(ddata, CTRL_REG1, data, "CTRL_REG1");
	if (error < 0) {
		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
		mutex_unlock(&ddata->lock);
		return error;
	}

	/*
	 * Power on request when device is in suspended state
	 * write to the boot bit in CTRL_REG2 to reboot memory content
	 * and ensure correct device behavior after it resumes
	 */
	if (set_boot_bit) {
		error = lsm303dlhc_a_write(ddata, CTRL_REG5, 0x80, "CTRL_REG5");
		if (error < 0) {
			if (ddata->regulator &&
					ddata->device_status == DEVICE_ON) {
				regulator_disable(ddata->regulator);
				ddata->device_status = DEVICE_OFF;
			}
			mutex_unlock(&ddata->lock);
			return error;
		}
	}

	if (val == LSM303DLHC_A_MODE_OFF) {

		/*
		 * No need to store context here
		 * it is not like suspend/resume
		 * but fall back to default values
		 */
		ddata->range = LSM303DLHC_A_RANGE_2G;
		ddata->shift_adjust = SHIFT_ADJ_2G;

		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
	}
	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlhc_a_show_id(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlhc_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->id);
}

static DEVICE_ATTR(id, S_IRUGO, lsm303dlhc_a_show_id, NULL);

static DEVICE_ATTR(data, S_IRUGO, lsm303dlhc_a_show_data, NULL);

static DEVICE_ATTR(range, S_IWUSR | S_IRUGO,
		lsm303dlhc_a_show_range, lsm303dlhc_a_store_range);

static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO,
		lsm303dlhc_a_show_mode, lsm303dlhc_a_store_mode);

static struct attribute *lsm303dlhc_a_attributes[] = {
	&dev_attr_data.attr,
	&dev_attr_range.attr,
	&dev_attr_mode.attr,
	&dev_attr_id.attr,
	NULL
};

static const struct attribute_group lsm303dlhc_a_attr_group = {
	.attrs = lsm303dlhc_a_attributes,
};

static int __devinit lsm303dlhc_a_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int ret;
	struct lsm303dlhc_a_data *adata = NULL;

	adata = kzalloc(sizeof(struct lsm303dlhc_a_data), GFP_KERNEL);
	if (adata == NULL) {
		dev_err(&client->dev, "memory alocation failed\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	adata->client = client;
	i2c_set_clientdata(client, adata);

	/* copy platform specific data */
	memcpy(&adata->pdata, client->dev.platform_data, sizeof(adata->pdata));
	adata->mode = LSM303DLHC_A_MODE_OFF;
	adata->range = LSM303DLHC_A_RANGE_2G;
	adata->shift_adjust = SHIFT_ADJ_2G;
	adata->device_status = DEVICE_OFF;
	dev_set_name(&client->dev, adata->pdata.name_a);

	adata->regulator = regulator_get(&client->dev, "vdd");
	if (IS_ERR(adata->regulator)) {
		dev_err(&client->dev, "failed to get regulator\n");
		ret = PTR_ERR(adata->regulator);
		adata->regulator = NULL;
		goto err_op_failed;
	}

	if (adata->regulator) {
		/*
		 * 130 microamps typical with magnetic sensor setting ODR = 7.5
		 * Hz, Accelerometer sensor ODR = 50 Hz.  Double for safety.
		 */
		regulator_set_optimum_mode(adata->regulator, 130 * 2);
		regulator_enable(adata->regulator);
		adata->device_status = DEVICE_ON;
	}

	ret = lsm303dlhc_a_read(adata, WHO_AM_I, "WHO_AM_I");
	if (ret < 0)
		goto exit_free_regulator;

	dev_info(&client->dev, "3-Axis Accelerometer, ID : %d\n",
			 ret);
	adata->id = ret;

	mutex_init(&adata->lock);

	ret = sysfs_create_group(&client->dev.kobj, &lsm303dlhc_a_attr_group);
	if (ret)
		goto exit_free_regulator;

#ifdef CONFIG_HAS_EARLYSUSPEND
	adata->early_suspend.level =
			EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	adata->early_suspend.suspend = lsm303dlhc_a_early_suspend;
	adata->early_suspend.resume = lsm303dlhc_a_late_resume;
	register_early_suspend(&adata->early_suspend);
#endif

	if (adata->device_status == DEVICE_ON && adata->regulator) {
		regulator_disable(adata->regulator);
		adata->device_status = DEVICE_OFF;
	}

	return ret;

exit_free_regulator:
	if (adata->device_status == DEVICE_ON && adata->regulator) {
		regulator_disable(adata->regulator);
		regulator_put(adata->regulator);
		adata->device_status = DEVICE_OFF;
	}

err_op_failed:
	kfree(adata);
err_alloc:
	dev_err(&client->dev, "probe function fails %x", ret);
	return ret;
}

static int __devexit lsm303dlhc_a_remove(struct i2c_client *client)
{
	int ret;
	struct lsm303dlhc_a_data *adata;

	adata = i2c_get_clientdata(client);
	sysfs_remove_group(&client->dev.kobj, &lsm303dlhc_a_attr_group);

	/* safer to make device off */
	if (adata->mode != LSM303DLHC_A_MODE_OFF) {
		ret = lsm303dlhc_a_write(adata, CTRL_REG1, 0, "CONTROL");

		if (ret < 0) {
			dev_err(&client->dev,
					"could not turn off the device %d",
					ret);
			return ret;
		}

		if (adata->regulator && adata->device_status == DEVICE_ON) {
			regulator_disable(adata->regulator);
			regulator_put(adata->regulator);
			adata->device_status = DEVICE_OFF;
		}
	}

	i2c_set_clientdata(client, NULL);
	kfree(adata);

	return 0;
}

#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
static int lsm303dlhc_a_suspend(struct device *dev)
{
	struct lsm303dlhc_a_data *ddata;
	int ret;

	ddata = dev_get_drvdata(dev);

	ret = lsm303dlhc_a_do_suspend(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while suspending the device");

	return ret;
}

static int lsm303dlhc_a_resume(struct device *dev)
{
	struct lsm303dlhc_a_data *ddata;
	int ret;

	ddata = dev_get_drvdata(dev);

	ret	= lsm303dlhc_a_restore(ddata);

	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while resuming the device");

	return ret;
}
static const struct dev_pm_ops lsm303dlhc_a_dev_pm_ops = {
	.suspend = lsm303dlhc_a_suspend,
	.resume  = lsm303dlhc_a_resume,
};
#else
static void lsm303dlhc_a_early_suspend(struct early_suspend *data)
{
	struct lsm303dlhc_a_data *ddata =
		container_of(data, struct lsm303dlhc_a_data, early_suspend);
	int ret;

	ret = lsm303dlhc_a_do_suspend(ddata);
}

static void lsm303dlhc_a_late_resume(struct early_suspend *data)
{
	struct lsm303dlhc_a_data *ddata =
		container_of(data, struct lsm303dlhc_a_data, early_suspend);
	int ret;

	ret = lsm303dlhc_a_restore(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"lsm303dlhc_a late resume failed\n");
}
#endif /* CONFIG_PM */

static const struct i2c_device_id lsm303dlhc_a_id[] = {
	{ "lsm303dlhc_a", 0 },
	{ },
};

static struct i2c_driver lsm303dlhc_a_driver = {
	.probe		= lsm303dlhc_a_probe,
	.remove		= lsm303dlhc_a_remove,
	.id_table	= lsm303dlhc_a_id,
	.driver = {
		.name = "lsm303dlhc_a",
	#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
		.pm	=	&lsm303dlhc_a_dev_pm_ops,
	#endif
	},
};

static int __init lsm303dlhc_a_init(void)
{
	return i2c_add_driver(&lsm303dlhc_a_driver);
}

static void __exit lsm303dlhc_a_exit(void)
{
	i2c_del_driver(&lsm303dlhc_a_driver);
}

module_init(lsm303dlhc_a_init)
module_exit(lsm303dlhc_a_exit)

MODULE_DESCRIPTION("lSM303DLH 3-Axis Accelerometer Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("STMicroelectronics");
