/*
 * Copyright (C) ST-Ericsson SA 2012
 * License Terms: GNU General Public License, version 2
 *
 * Mostly this accelerometer device is a copy of magnetometer
 * driver lsm303dlh or viceversa, so the code is mostly based
 * on lsm303dlh driver.
 *
 * Author: Naga Radhesh Y <naga.radheshy@stericsson.com>
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/earlysuspend.h>
#include <linux/l3g4200d.h>

#include "../iio.h"
#include "gyro.h"

/* Idenitification register */
#define WHO_AM_I	0x0F
/* control register1  */
#define CTRL_REG1	0x20
/* control register2 */
#define CTRL_REG2	0x21
/* control register3 */
#define CTRL_REG3	0x22
/* control register4 */
#define CTRL_REG4	0x23
/* control register5 */
#define CTRL_REG5	0x24
/* out temperature */
#define OUT_TEMP	0x26
/* data output X register */
#define OUT_X_L_A	0x28
/* data output Y register */
#define OUT_Y_L_A	0x2A
/* data output Z register */
#define OUT_Z_L_A	0x2C
/* status register */
#define STATUS_REG_A	0x27

/* control register 1, Mode selection */
#define CR1_PM_BIT 3
#define CR1_PM_MASK (0x01 << CR1_PM_BIT)
/* control register 1, Data Rate */
#define CR1_DR_BIT 4
#define CR1_DR_MASK (0x0F << CR1_DR_BIT)
/* control register 1, x,y,z enable bits */
#define CR1_EN_BIT 0
#define CR1_EN_MASK (0x7 << CR1_EN_BIT)
#define CR1_AXIS_ENABLE 7

/* control register 4, self test */
#define CR4_ST_BIT 1
#define CR4_ST_MASK (0x03 << CR4_ST_BIT)
/* control register 4, full scale */
#define CR4_FS_BIT 4
#define CR4_FS_MASK (0x3 << CR4_FS_BIT)
/* control register 4, endianness */
#define CR4_BLE_BIT 6
#define CR4_BLE_MASK (0x1 << CR4_BLE_BIT)
/* control register 4, Block data update */
#define CR4_BDU_BIT 7
#define CR4_BDU_MASK (0x1 << CR4_BDU_BIT)

/* Gyroscope operating mode */
#define MODE_OFF	0x00
#define MODE_NORMAL	0x01

/* Expected content for WAI register */
#define WHOAMI_L3G4200D		0x00D3
/*
 * CTRL_REG1 register rate settings
 *
 * DR1 DR0 BW1 BW0     Output data rate[Hz]
 * 0   0   0    0       100
 * 0   0   0    1       100
 * 0   0   1    0       100
 * 0   0   1    1       100
 * 0   1   0    0       200
 * 0   1   0    1       200
 * 0   1   1    0       200
 * 0   1   1    1       200
 * 1   0   0    0       400
 * 1   0   0    1       400
 * 1   0   1    0       400
 * 1   0   1    1       400
 * 1   1   0    0       800
 * 1   1   0    1       800
 * 1   1   1    0       800
 * 1   1   1    1       800
 */
#define ODR_MIN_VAL	0x00
#define ODR_MAX_VAL	0x0F
#define RATE_100	0x00
#define RATE_200	0x04
#define RATE_400	0x08
#define RATE_800	0x0C

/*
 * CTRL_REG4 register range settings
 *
 * FS1 FS0	FUll scale range
 * 0   0           250
 * 0   1           500
 * 1   0           2000
 * 1   1           2000
 */
#define RANGE_250	0x00
#define RANGE_500	0x01
#define RANGE_2000	0x03

/* device status defines */
#define DEVICE_OFF	0
#define DEVICE_ON	1
#define DEVICE_SUSPENDED	2

/* status register */
#define SR_REG_A	0x27
/* status register, ready  */
#define XYZ_DATA_RDY	0x80
#define XYZ_DATA_RDY_BIT	3
#define XYZ_DATA_RDY_MASK	(0x1 << XYZ_DATA_RDY_BIT)

/* Multiple byte transfer enable */
#define MULTIPLE_I2C_TR		0x80

/**
 * struct l3g4200d_data - data structure used by l3g4200d driver
 * @client: i2c client
 * @indio_dev: iio device structure
 * @attr: device attributes
 * @lock: mutex lock for sysfs operations
 * @regulator: regulator
 * @early_suspend: early suspend structure
 * @pdata: l3g4200d platform data
 * @data: Magnetic field values of x, y and z axes
 * @mode: current mode of operation
 * @rate: current sampling rate
 * @range: current range value of magnetometer
 * @device_status: device is ON, OFF or SUSPENDED
 */

struct l3g4200d_data {
	struct i2c_client	*client;
	struct iio_dev		*indio_dev;
	struct attribute_group	attrs;
	struct mutex		lock;
	struct regulator	*regulator;
	struct early_suspend early_suspend;
	struct l3g4200d_gyr_platform_data *pdata;

	short			data[3];
	u8			mode;
	u8			rate;
	u8			range;
	int			device_status;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void l3g4200d_early_suspend(struct early_suspend *data);
static void l3g4200d_late_resume(struct early_suspend *data);
#endif

/**
 * To disable regulator and status
 **/
static int l3g4200d_disable(struct l3g4200d_data *data)
{
	data->device_status = DEVICE_OFF;
	if (data->regulator)
		regulator_disable(data->regulator);
	return 0;
}

/**
 * To enable regulator and status
 **/
static int l3g4200d_enable(struct l3g4200d_data *data)
{
	data->device_status = DEVICE_ON;
	if (data->regulator)
		regulator_enable(data->regulator);
	return 0;
}

static s32 l3g4200d_set_mode(struct i2c_client *client,	u8 mode)
{
	int reg_val;

	if (mode > MODE_NORMAL) {
		dev_err(&client->dev, "given mode not supported\n");
		return -EINVAL;
	}

	reg_val = i2c_smbus_read_byte_data(client, CTRL_REG1);

	reg_val |= CR1_AXIS_ENABLE;
	reg_val &= ~CR1_PM_MASK;

	reg_val |= ((mode << CR1_PM_BIT) & CR1_PM_MASK);

	/* the 4th bit indicates the gyroscope sensor mode */
	return i2c_smbus_write_byte_data(client, CTRL_REG1, reg_val);
}

static s32 l3g4200d_set_rate(struct i2c_client *client, u8 rate)
{
	int reg_val;

	if (rate > ODR_MAX_VAL) {
		dev_err(&client->dev, "given rate not supported\n");
		return -EINVAL;
	}
	reg_val = i2c_smbus_read_byte_data(client, CTRL_REG1);

	reg_val &= ~CR1_DR_MASK;

	reg_val |= ((rate << CR1_DR_BIT) & CR1_DR_MASK);

	/* upper 4 bits indicate ODR of Gyroscope */
	return i2c_smbus_write_byte_data(client, CTRL_REG1, reg_val);
}

static s32 l3g4200d_set_range(struct i2c_client *client, u8 range)
{
	int reg_val;

	if (range > RANGE_2000) {
		dev_err(&client->dev, "given range not supported\n");
		return -EINVAL;
	}

	reg_val = (range << CR4_FS_BIT);
	/*
	 * If BDU is enabled, output registers are not updated until MSB
	 * and LSB reading completes.Otherwise we will end up reading
	 * wrong data.
	 */
	reg_val |= CR4_BDU_MASK;

	/* 5th and 6th bits indicate rate of gyroscope */
	return i2c_smbus_write_byte_data(client, CTRL_REG4, reg_val);
}

/**
 * To read output x/y/z data register, in this case x,y and z are not
 * mapped w.r.t board orientation. Reading just raw data from device
 **/
static ssize_t l3g4200d_xyz_read(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{

	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int lsb , msb;
	int ret;
	s16 val;

	/*
	 * Perform read/write operation, only when device is active
	 */
	if (data->device_status != DEVICE_ON) {
		dev_dbg(&client->dev,
			"device is switched off,make it ON using MODE");
		return -EINVAL;
	}
	mutex_lock(&data->lock);

	ret = i2c_smbus_read_byte_data(client, SR_REG_A);

	/* wait till data is written to all six registers */
	while (!(ret & XYZ_DATA_RDY_MASK))
		ret = i2c_smbus_read_byte_data(client, SR_REG_A);

	lsb = i2c_smbus_read_byte_data(client, this_attr->address);
	if (ret < 0) {
		dev_err(&client->dev, "reading xyz failed\n");
		mutex_unlock(&data->lock);
		return -EINVAL;
	}
	msb = i2c_smbus_read_byte_data(client, (this_attr->address + 1));
	if (ret < 0) {
		dev_err(&client->dev, "reading xyz failed\n");
		mutex_unlock(&data->lock);
		return -EINVAL;
	}
	val = ((msb << 8) | lsb);

	mutex_unlock(&data->lock);

	return sprintf(buf, "%d:%lld\n", val, iio_get_time_ns());
}

/**
 * To read output x,y,z data register. After reading change x,y and z values
 * w.r.t the orientation of the device.
 **/
static ssize_t l3g4200d_readdata(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{

	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;
	struct l3g4200d_gyr_platform_data *pdata = data->pdata;
	struct i2c_client *client = data->client;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u8 map_x = pdata->axis_map_x;
	u8 map_y = pdata->axis_map_y;
	u8 map_z = pdata->axis_map_z;
	int ret;
	unsigned char magn_data[6];
	s16 val[3];

	/*
	 * Perform read/write operation, only when device is active
	 */
	if (data->device_status != DEVICE_ON) {
		dev_dbg(&client->dev,
			"device is switched off,make it ON using MODE");
		return -EINVAL;
	}
	mutex_lock(&data->lock);

	ret = i2c_smbus_read_byte_data(client, SR_REG_A);
	/* wait till data is written to all six registers */
	while (!((ret & XYZ_DATA_RDY_MASK)))
		ret = i2c_smbus_read_byte_data(client, SR_REG_A);

	ret = i2c_smbus_read_i2c_block_data(client,
		   this_attr->address | MULTIPLE_I2C_TR, 6, magn_data);

	if (ret < 0) {
		dev_err(&client->dev, "reading xyz failed\n");
		mutex_unlock(&data->lock);
		return -EINVAL;
	}


	/* MSB is at lower address */
	val[0] = (s16)
		(((magn_data[1]) << 8) | magn_data[0]);
	val[1] = (s16)
		(((magn_data[3]) << 8) | magn_data[2]);
	val[2] = (s16)
		(((magn_data[5]) << 8) | magn_data[4]);

	/* modify the x,y and z values w.r.t orientation of device*/
	if (pdata->negative_x)
		val[map_x] = -val[map_x];
	if (pdata->negative_y)
		val[map_y] = -val[map_y];
	if (pdata->negative_z)
		val[map_z] = -val[map_z];

	mutex_unlock(&data->lock);

	return sprintf(buf, "%d:%d:%d:%lld\n", val[map_x], val[map_y],
		       val[map_z], iio_get_time_ns());
}

static ssize_t show_gyrotemp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;

	/*
	 * Perform read/write operation, only when device is active
	 */
	if (data->device_status != DEVICE_ON) {
		dev_info(&client->dev,
			"device is switched off,make it ON using MODE");
		return -EINVAL;
	}
	ret = i2c_smbus_read_byte_data(client, this_attr->address);
	if (ret < 0) {
		dev_err(&client->dev, "Error in reading Gyro temperature");
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t show_operating_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;

	return sprintf(buf, "%d\n", data->mode);
}

static ssize_t set_operating_mode(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	int error;
	unsigned long mode = 0;

	mutex_lock(&data->lock);

	error = strict_strtoul(buf, 10, &mode);
	if (error) {
		count = error;
		goto exit;
	}

	/* check if the received power mode is either 0 or 1 */
	if (mode < MODE_OFF ||  mode > MODE_NORMAL) {
		dev_err(&client->dev, "trying to set invalid mode\n");
		count = -EINVAL;
		goto exit;
	}
	/*
	 * If device is drived to sleep mode in suspend, update mode
	 * and return
	 */
	if (data->device_status == DEVICE_SUSPENDED &&
			mode == MODE_OFF) {
		data->mode = mode;
		goto exit;
	}

	 /*  if same mode as existing, return */
	if (data->mode == mode)
		goto exit;

	/* Enable the regulator if it is not turned ON earlier */
	if (data->device_status == DEVICE_OFF ||
		data->device_status == DEVICE_SUSPENDED)
		l3g4200d_enable(data);

	dev_dbg(dev, "set operating mode to %lu\n", mode);
	error = l3g4200d_set_mode(client, mode);
	if (error < 0) {
		dev_err(&client->dev, "Error in setting the mode\n");
		count = -EINVAL;
		goto exit;
	}

	data->mode = mode;

	/* If mode is OFF then disable the regulator */
	if (data->mode == MODE_OFF) {
		/* fall back to default values */
		data->rate = RATE_100;
		data->range = ODR_MIN_VAL;
		l3g4200d_disable(data);
	}
exit:
	mutex_unlock(&data->lock);
	return count;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("100 200 400 800");

static ssize_t set_sampling_frequency(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	unsigned long rate = 0;

	/*
	 * Perform read/write operation, only when device is active
	 */
	if (data->device_status != DEVICE_ON) {
		dev_info(&client->dev,
			"device is switched off,make it ON using MODE");
		return -EINVAL;
	}
	if (strncmp(buf, "100" , 3) == 0)
		rate = RATE_100;

	else if (strncmp(buf, "200" , 3) == 0)
		rate = RATE_200;

	else if (strncmp(buf, "400" , 3) == 0)
		rate = RATE_400;

	else if (strncmp(buf, "800" , 3) == 0)
		rate = RATE_800;
	else
		return -EINVAL;

	mutex_lock(&data->lock);

	if (l3g4200d_set_rate(client, rate)) {
		dev_err(&client->dev, "set rate failed\n");
		count = -EINVAL;
		goto exit;
	}
	data->rate = rate;

exit:
	mutex_unlock(&data->lock);
	return count;
}

/* sampling frequency - output rate in Hz */
static const char * const reg_to_rate[] = {
	"100",
	"100",
	"100",
	"100",
	"200",
	"200",
	"200",
	"200",
	"400",
	"400",
	"400",
	"400",
	"800",
	"800",
	"800",
	"800"
};

static ssize_t show_sampling_frequency(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;

	return sprintf(buf, "%s\n", reg_to_rate[data->rate]);
}

static ssize_t set_range(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	unsigned long range = 0;
	int error;

	/*
	 * Perform read/write operation, only when device is active
	 */
	if (data->device_status != DEVICE_ON) {
		dev_info(&client->dev,
			"device is switched off,make it ON using MODE");
		return -EINVAL;
	}
	mutex_lock(&data->lock);

	error = strict_strtoul(buf, 10, &range);
	if (error) {
		count = error;
		goto exit;
	}
	dev_dbg(dev, "setting range to %lu\n", range);

	if (range > RANGE_2000) {
		dev_err(dev, "wrong range %lu\n", range);
		count = -EINVAL;
		goto exit;
	}

	error = l3g4200d_set_range(client, range);
	if (error < 0) {
		dev_err(dev, "unable to set the range\n");
		count = -EINVAL;
		goto exit;
	}

	data->range = range;

exit:
	mutex_unlock(&data->lock);
	return count;
}


/* Fullscale range - range in gauss */
static const char * const reg_to_range[] = {
	"250",
	"500",
	"2000",
	"2000"
};
static ssize_t show_range(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct l3g4200d_data *data = indio_dev->dev_data;

	return sprintf(buf, "%s\n", reg_to_range[data->range]);
}

static IIO_DEV_ATTR_GYRO_X(l3g4200d_xyz_read,
			OUT_X_L_A);
static IIO_DEV_ATTR_GYRO_Y(l3g4200d_xyz_read,
			OUT_Y_L_A);
static IIO_DEV_ATTR_GYRO_Z(l3g4200d_xyz_read,
			OUT_Z_L_A);
static IIO_DEV_ATTR_GYRO(l3g4200d_readdata,
			OUT_X_L_A);

static IIO_DEVICE_ATTR(sampling_frequency,
			S_IWUSR | S_IRUGO,
			show_sampling_frequency,
			set_sampling_frequency,
			CTRL_REG1);
static IIO_DEVICE_ATTR(gyro_range,
			S_IWUSR | S_IRUGO,
			show_range,
			set_range,
			CTRL_REG4);
static IIO_DEVICE_ATTR(mode,
			S_IWUSR | S_IRUGO,
			show_operating_mode,
			set_operating_mode,
			CTRL_REG1);
static IIO_DEVICE_ATTR(gyro_temp,
			S_IRUGO,
			show_gyrotemp,
			NULL,
			OUT_TEMP);

static struct attribute *l3g4200d_attributes[] = {
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_gyro_range.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_gyro_x_raw.dev_attr.attr,
	&iio_dev_attr_gyro_y_raw.dev_attr.attr,
	&iio_dev_attr_gyro_z_raw.dev_attr.attr,
	&iio_dev_attr_gyro_raw.dev_attr.attr,
	&iio_dev_attr_gyro_temp.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group l3g4200d_group = {
	.attrs = l3g4200d_attributes,
};

static const struct iio_info l3g4200d_info = {
	.attrs = &l3g4200d_group,
	.driver_module = THIS_MODULE,
};

static void l3g4200d_setup(struct i2c_client *client)
{
	struct l3g4200d_data *data = i2c_get_clientdata(client);

	/* set mode */
	l3g4200d_set_mode(client, data->mode);
	/* set rate */
	l3g4200d_set_rate(client, data->rate);
	/* set range */
	l3g4200d_set_range(client, data->range);
}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_PM)
static int l3g4200d_do_suspend(struct l3g4200d_data *data)
{
	int ret = 0;

	if (data->mode == MODE_OFF)
		return 0;

	mutex_lock(&data->lock);

	/* Set the device to sleep mode */
	l3g4200d_set_mode(data->client, MODE_OFF);

	/* Disable regulator */
	l3g4200d_disable(data);

	data->device_status = DEVICE_SUSPENDED;

	mutex_unlock(&data->lock);

	return ret;
}

static int l3g4200d_restore(struct l3g4200d_data *data)
{
	int ret = 0;


	if (data->device_status == DEVICE_ON ||
			data->device_status == DEVICE_OFF) {
		return 0;
	}
	mutex_lock(&data->lock);

	/* Enable regulator */
	l3g4200d_enable(data);

	/* Set mode,rate and range */
	l3g4200d_setup(data->client);

	mutex_unlock(&data->lock);
	return ret;
}
#endif

#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
static int l3g4200d_suspend(struct device *dev)
{
	struct l3g4200d_data *data;
	int ret;

	data = dev_get_drvdata(dev);

	ret = l3g4200d_do_suspend(data);
	if (ret < 0)
		dev_err(&data->client->dev,
				"Error while suspending the device");

	return ret;
}

static int l3g4200d_resume(struct device *dev)
{
	struct l3g4200d_data *data;
	int ret;

	data = dev_get_drvdata(dev);

	ret = l3g4200d_restore(data);

	if (ret < 0)
		dev_err(&data->client->dev,
				"Error while resuming the device");

	return ret;
}
static const struct dev_pm_ops l3g4200d_dev_pm_ops = {
	.suspend = l3g4200d_suspend,
	.resume  = l3g4200d_resume,
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
				"Error while suspending the device");
}

static void l3g4200d_late_resume(struct early_suspend *data)
{
	struct l3g4200d_data *ddata =
		container_of(data, struct l3g4200d_data, early_suspend);
	int ret;

	ret = l3g4200d_restore(ddata);

	if (ret < 0)
		dev_err(&ddata->client->dev,
				"l3g4200d late resume failed\n");
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

static int l3g4200d_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct l3g4200d_data *data;
	int err = 0;

	data = kzalloc(sizeof(struct l3g4200d_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "memory allocation failed\n");
		err = -ENOMEM;
		goto exit;
	}
	/* check for valid platform data */
	if (!client->dev.platform_data) {
		dev_err(&client->dev, "Invalid platform data\n");
		err = -ENOMEM;
		goto exit1;
	}
	data->pdata = client->dev.platform_data;

	data->mode = MODE_OFF;
	data->range = RANGE_250;
	data->rate = ODR_MIN_VAL;
	data->device_status = DEVICE_OFF;
	data->client = client;

	i2c_set_clientdata(client, data);

	data->regulator = regulator_get(&client->dev, "vdd");
	if (IS_ERR(data->regulator)) {
		err = PTR_ERR(data->regulator);
		dev_err(&client->dev, "failed to get regulator = %d\n", err);
		goto exit1;
	}
	/* Enable regulator */
	l3g4200d_enable(data);

	err = i2c_smbus_read_byte_data(client, WHO_AM_I);
	if (err < 0) {
		dev_err(&client->dev, "failed to read of the chip\n");
		goto exit2;
	}
	if (err == WHOAMI_L3G4200D)
		dev_info(&client->dev, "3-Axis Gyroscope device"
					" identification: %d\n", err);
	else {
		dev_info(&client->dev, "Gyroscope identification"
						" did not match\n");
		goto exit2;
	}

	l3g4200d_setup(client);

	mutex_init(&data->lock);

	data->indio_dev = iio_allocate_device(0);
	if (!data->indio_dev) {
		dev_err(&client->dev, "iio allocation failed\n");
		err = -ENOMEM;
		goto exit2;
	}
	data->indio_dev->info = &l3g4200d_info;
	data->indio_dev->dev.parent = &client->dev;
	data->indio_dev->dev_data = (void *)data;
	data->indio_dev->modes = INDIO_DIRECT_MODE;

	err = iio_device_register(data->indio_dev);
	if (err)
		goto exit3;

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level =
				EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = l3g4200d_early_suspend;
	data->early_suspend.resume = l3g4200d_late_resume;
	register_early_suspend(&data->early_suspend);
#endif
	/* Disable regulator */
	l3g4200d_disable(data);

	return 0;

exit3:
	iio_free_device(data->indio_dev);
exit2:
	l3g4200d_disable(data);
	regulator_put(data->regulator);
exit1:
	kfree(data);
exit:
	return err;
}

static int __devexit l3g4200d_remove(struct i2c_client *client)
{
	struct l3g4200d_data *data = i2c_get_clientdata(client);
	int ret;

	/* safer to make device off */
	if (data->mode != MODE_OFF) {
		/* set mode to off */
		ret = l3g4200d_set_mode(client, MODE_OFF);
		if (ret < 0) {
			dev_err(&client->dev, "could not turn"
						"off the device %d", ret);
			return ret;
		}
		if (data->regulator && data->device_status == DEVICE_ON) {
			regulator_disable(data->regulator);
			data->device_status = DEVICE_OFF;
		}
	}
	regulator_put(data->regulator);
	iio_device_unregister(data->indio_dev);
	iio_free_device(data->indio_dev);
	kfree(data);
	return 0;
}

static const struct i2c_device_id l3g4200d_id[] = {
	{ "l3g4200d", 0 },
	{ },
};

static struct i2c_driver l3g4200d_driver = {
	.driver = {
		.name	= "l3g4200d",
	#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
		.pm = &l3g4200d_dev_pm_ops,
	#endif
	},
	.id_table	= l3g4200d_id,
	.probe		= l3g4200d_probe,
	.remove		= l3g4200d_remove,
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

MODULE_DESCRIPTION("l3g4200d Gyroscope Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Naga Radhesh Y <naga.radheshy@stericsson.com>");
