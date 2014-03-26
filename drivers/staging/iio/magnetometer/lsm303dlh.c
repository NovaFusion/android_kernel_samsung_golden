/*
 * Copyright (C) ST-Ericsson SA 2012
 * License Terms: GNU General Public License, version 2
 *
 * Mostly this magnetometer device is a copy of hmc5843 or
 * viceversa, so the code is mostly based on hmc5843 driver.
 *
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
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
#include <linux/lsm303dlh.h>

#include "../iio.h"
#include "magnet.h"

/* configuration register A */
#define CRA_REG_M	0x00
/* configuration register B */
#define CRB_REG_M	0x01
/* mode register */
#define MR_REG_M	0x02
/* data output X register */
#define OUT_X_M		0x03
/* data output Y register */
#define OUT_Y_M		0x05
/* data output Z register */
#define OUT_Z_M		0x07
/* status register */
#define SR_REG_M	0x09
/* identification registers */
#define IRA_REG_M	0x0A
#define IRB_REG_M	0x0B
#define IRC_REG_M	0x0C

/* X,Y and Z gain values for LSM303DLH */
/* XY gain at 1.3G */
#define XY_GAIN_1_3	1055
/* XY gain at 1.9G */
#define XY_GAIN_1_9	 795
/* XY gain at 2.5G */
#define XY_GAIN_2_5	 635
/* XY gain at 4.0G */
#define XY_GAIN_4_0	 430
/* XY gain at 4.7G */
#define XY_GAIN_4_7	 375
/* XY gain at 5.6G */
#define XY_GAIN_5_6	 320
/* XY gain at 8.1G */
#define XY_GAIN_8_1	 230

/* Z gain at 1.3G */
#define Z_GAIN_1_3	950
/* Z gain at 1.9G */
#define Z_GAIN_1_9	710
/* Z gain at 2.5G */
#define Z_GAIN_2_5	570
/* Z gain at 4.0G */
#define Z_GAIN_4_0	385
/* Z gain at 4.7G */
#define Z_GAIN_4_7	335
/* Z gain at 5.6G */
#define Z_GAIN_5_6	285
/* Z gain at 8.1G */
#define Z_GAIN_8_1	205

/* X,Y and Z gain values for LSM303DLHC */
/* XY gain at 1.3G */
#define XY_GAIN_1_3_HC	1100
/* XY gain at 1.9G */
#define XY_GAIN_1_9_HC	 855
/* XY gain at 2.5G */
#define XY_GAIN_2_5_HC	 670
/* XY gain at 4.0G */
#define XY_GAIN_4_0_HC	 450
/* XY gain at 4.7G */
#define XY_GAIN_4_7_HC	 400
/* XY gain at 5.6G */
#define XY_GAIN_5_6_HC	 330
/* XY gain at 8.1G */
#define XY_GAIN_8_1_HC	 230

/* Z gain at 1.3G */
#define Z_GAIN_1_3_HC	980
/* Z gain at 1.9G */
#define Z_GAIN_1_9_HC	760
/* Z gain at 2.5G */
#define Z_GAIN_2_5_HC	600
/* Z gain at 4.0G */
#define Z_GAIN_4_0_HC	400
/* Z gain at 4.7G */
#define Z_GAIN_4_7_HC	355
/* Z gain at 5.6G */
#define Z_GAIN_5_6_HC	295
/* Z gain at 8.1G */
#define Z_GAIN_8_1_HC	205

/* control register A, Data Output rate */
#define CRA_DO_BIT	2
#define CRA_DO_MASK	(0x7 << CRA_DO_BIT)
/* control register A, measurement configuration */
#define CRA_MS_BIT	0
#define CRA_MS_MASK	(0x3 << CRA_MS_BIT)
/* control register B, gain configuration */
#define CRB_GN_BIT	5
#define CRB_GN_MASK	(0x7 << CRB_GN_BIT)
/* mode register */
#define MR_MD_BIT	0
#define MR_MD_MASK	(0x3 << MR_MD_BIT)
/* status register, ready  */
#define SR_RDY_BIT	0
#define SR_RDY_MASK	(0x1 << SR_RDY_BIT)
/* status register, data output register lock */
#define SR_LOC_BIT	1
#define SR_LOC_MASK	(0x1 << SR_LOC_BIT)
/* status register, regulator enabled */
#define SR_REN_BIT	2
#define SR_REN_MASK	(0x1 << SR_REN_BIT)

/*
 *     Control register gain settings
 *---------------------------------------------
 *GN2 | GN1| GN0|sensor input| Gain X/Y | Gain Z|
 * 0  |  0 |  1 |     +/-1.3 |   1055   |   950 |
 * 0  |  1 |  0 |     +/-1.9 |   795    |   710 |
 * 0  |  1 |  1 |     +/-2.5 |   635    |   570 |
 * 1  |  0 |  0 |     +/-4.0 |   430    |   385 |
 * 1  |  0 |  1 |     +/-4.7 |   375    |   335 |
 * 1  |  1 |  0 |     +/-5.6 |   320    |   285 |
 * 1  |  1 |  1 |     +/-8.1 |   230    |   205 |
 *---------------------------------------------
 */
#define RANGE_1_3G	0x01
#define RANGE_1_9G	0x02
#define RANGE_2_5G	0x03
#define RANGE_4_0G	0x04
#define RANGE_4_7G	0x05
#define RANGE_5_6G	0x06
#define RANGE_8_1G	0x07

/*
 * CRA register data output rate settings
 *
 * DO2 DO1 DO0 Minimum data output rate (Hz)
 * 0    0   0		0.75
 * 0    0   1		1.5
 * 0    1   0		3.0
 * 0    1   1		7.5
 * 1    0   0		15
 * 1    0   1		30
 * 1    1   0		75
 * 1    1   1		Not used
 */
#define RATE_00_75	0x00
#define RATE_01_50	0x01
#define RATE_03_00	0x02
#define RATE_07_50	0x03
#define RATE_15_00	0x04
#define RATE_30_00	0x05
#define RATE_75_00	0x06
#define RATE_RESERVED	0x07

/* device status defines */
#define DEVICE_OFF 0
#define DEVICE_ON 1
#define DEVICE_SUSPENDED 2

#define	NORMAL_CFG		0x00
#define	POSITIVE_BIAS_CFG	0x01
#define	NEGATIVE_BIAS_CFG	0x02
#define	NOT_USED_CFG		0x03

/* Magnetic sensor operating mode */
#define CONTINUOUS_CONVERSION_MODE	0x00
#define SINGLE_CONVERSION_MODE		0x01
#define UNUSED_MODE			0x02
#define SLEEP_MODE			0x03

/* Multiple byte transfer enable */
#define MULTIPLE_I2C_TR 0x80
#define DATA_RDY		0x01

/* device CHIP ID defines */
#define LSM303DLHC_CHIP_ID 51

/**
 * struct lsm303dlh_m_data - data structure used by lsm303dlh_m driver
 * @client: i2c client
 * @indio_dev: iio device structure
 * attr: device attributes
 * @lock: mutex lock for sysfs operations
 * @regulator: regulator
 * @early_suspend: early suspend structure
 * @pdata: lsm303dlh platform data pointer
 * @gain: x, y and z axes gain
 * @data: Magnetic field values of x, y and z axes
 * @mode: current mode of operation
 * @rate: current sampling rate
 * @config: device configuration
 * @range: current range value of magnetometer
 * @device_status: device is ON, OFF or SUSPENDED
 */
struct lsm303dlh_m_data {
	struct i2c_client	*client;
	struct iio_dev		*indio_dev;
	struct attribute_group	attrs;
	struct mutex		lock;
	struct regulator	*regulator;
	struct early_suspend early_suspend;
	struct lsm303dlh_platform_data *pdata;

	short			gain[3];
	short			data[3];
	u8			mode;
	u8			rate;
	u8			config;
	u8			range;
	int			device_status;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void lsm303dlh_m_early_suspend(struct early_suspend *data);
static void lsm303dlh_m_late_resume(struct early_suspend *data);
#endif

/**
 * To set x,y and z gain for magnetometer device(lsm303dlh)
 **/
static void set_gain_dlh(struct lsm303dlh_m_data *data)
{
	short xy_gain;
	short z_gain;

	switch (data->range) {
	case RANGE_1_3G:
		xy_gain = XY_GAIN_1_3;
		z_gain = Z_GAIN_1_3;
		break;
	case RANGE_1_9G:
		xy_gain = XY_GAIN_1_9;
		z_gain = Z_GAIN_1_9;
		break;
	case RANGE_2_5G:
		xy_gain = XY_GAIN_2_5;
		z_gain = Z_GAIN_2_5;
		break;
	case RANGE_4_0G:
		xy_gain = XY_GAIN_4_0;
		z_gain = Z_GAIN_4_0;
		break;
	case RANGE_4_7G:
		xy_gain = XY_GAIN_4_7;
		z_gain = Z_GAIN_4_7;
		break;
	case RANGE_5_6G:
		xy_gain = XY_GAIN_5_6;
		z_gain = Z_GAIN_5_6;
		break;
	case RANGE_8_1G:
		xy_gain = XY_GAIN_8_1;
		z_gain = Z_GAIN_8_1;
		break;
	default:
		return;
	}
	data->gain[data->pdata->axis_map_x] = xy_gain;
	data->gain[data->pdata->axis_map_y] = xy_gain;
	data->gain[data->pdata->axis_map_z] = z_gain;
}

/**
 * To set x,y and z gain for magnetometer device(lsm303dlhc)
 **/
static void set_gain_dlhc(struct lsm303dlh_m_data *data)
{
	short xy_gain;
	short z_gain;

	switch (data->range) {
	case RANGE_1_3G:
		xy_gain = XY_GAIN_1_3_HC;
		z_gain = Z_GAIN_1_3_HC;
		break;
	case RANGE_1_9G:
		xy_gain = XY_GAIN_1_9_HC;
		z_gain = Z_GAIN_1_9_HC;
		break;
	case RANGE_2_5G:
		xy_gain = XY_GAIN_2_5_HC;
		z_gain = Z_GAIN_2_5_HC;
		break;
	case RANGE_4_0G:
		xy_gain = XY_GAIN_4_0_HC;
		z_gain = Z_GAIN_4_0_HC;
		break;
	case RANGE_4_7G:
		xy_gain = XY_GAIN_4_7_HC;
		z_gain = Z_GAIN_4_7_HC;
		break;
	case RANGE_5_6G:
		xy_gain = XY_GAIN_5_6_HC;
		z_gain = Z_GAIN_5_6_HC;
		break;
	case RANGE_8_1G:
		xy_gain = XY_GAIN_8_1_HC;
		z_gain = Z_GAIN_8_1_HC;
		break;
	default:
		return;
	}
	data->gain[data->pdata->axis_map_x] = xy_gain;
	data->gain[data->pdata->axis_map_y] = xy_gain;
	data->gain[data->pdata->axis_map_z] = z_gain;
}

/**
 * To disable regulator and status
 **/
static int lsm303dlh_m_disable(struct lsm303dlh_m_data *data)
{
	data->device_status = DEVICE_OFF;
	if (data->regulator)
		regulator_disable(data->regulator);
	return 0;
}

/**
 * To enable regulator and status
 **/
static int lsm303dlh_m_enable(struct lsm303dlh_m_data *data)
{
	data->device_status = DEVICE_ON;
	if (data->regulator)
		regulator_enable(data->regulator);
	return 0;
}

/**
 * To read output x/y/z data register, in this case x,y and z are not
 * mapped w.r.t board orientation. Reading just raw data from device
 **/
static ssize_t lsm303dlh_m_xyz_read(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{

	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
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

	ret = i2c_smbus_read_byte_data(client, SR_REG_M);
	/* wait till data is written to all six registers */
	while (!(ret & DATA_RDY))
		ret = i2c_smbus_read_byte_data(client, SR_REG_M);

	ret = i2c_smbus_read_word_data(client, this_attr->address);

	if (ret < 0) {
		dev_err(&client->dev, "reading xyz failed\n");
		mutex_unlock(&data->lock);
		return -EINVAL;
	}

	mutex_unlock(&data->lock);

	val = (s16)swab16((u16)ret);

	return sprintf(buf, "%d:%lld\n", val, iio_get_time_ns());
}

/**
 * To read output x,y,z data register. After reading change x,y and z values
 * w.r.t the orientation of the device.
 **/
static ssize_t lsm303dlh_m_readdata(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;
	struct lsm303dlh_platform_data *pdata = data->pdata;
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

	ret = i2c_smbus_read_byte_data(client, SR_REG_M);

	/* wait till data is written to all six registers */
	while (!(ret & DATA_RDY))
		ret = i2c_smbus_read_byte_data(client, SR_REG_M);

	ret = i2c_smbus_read_i2c_block_data(client,
		   this_attr->address | MULTIPLE_I2C_TR, 6, magn_data);

	if (ret < 0) {
		dev_err(&client->dev, "reading xyz failed\n");
		mutex_unlock(&data->lock);
		return -EINVAL;
	}

	/* MSB is at lower address */
	val[0] = (s16)
		(((magn_data[0]) << 8) | magn_data[1]);
	val[1] = (s16)
		(((magn_data[2]) << 8) | magn_data[3]);
	val[2] = (s16)
		(((magn_data[4]) << 8) | magn_data[5]);
	/* check if chip is DHLC */
	if (data->pdata->chip_id == LSM303DLHC_CHIP_ID) {
		/*
		 * the out registers are in x, z and y order
		 * so swap y and z values
		 */
		short temp = val[1];
		val[1] = val[2];
		val[2] = temp;
	}

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

static ssize_t lsm303dlh_m_gain(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;

	return sprintf(buf, "%d:%d:%d\n",
			data->gain[data->pdata->axis_map_x],
			data->gain[data->pdata->axis_map_y],
			data->gain[data->pdata->axis_map_z]);
}

static ssize_t show_operating_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;

	return sprintf(buf, "%d\n", data->mode);
}

static ssize_t set_operating_mode(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int error;
	unsigned long mode = 0;

	mutex_lock(&data->lock);

	error = strict_strtoul(buf, 10, &mode);
	if (error) {
		count = error;
		goto exit;
	}

	if (mode > SLEEP_MODE) {
		dev_err(&client->dev, "trying to set invalid mode\n");
		count = -EINVAL;
		goto exit;
	}
	/*
	 * If device is drived to sleep mode in suspend, update mode
	 * and return
	 */
	if (data->device_status == DEVICE_SUSPENDED &&
			mode == SLEEP_MODE) {
		data->mode = mode;
		goto exit;
	}

	 /*  if same mode as existing, return */
	if (data->mode == mode)
		goto exit;

	/* Enable the regulator if it is not turned ON earlier*/
	if (data->device_status == DEVICE_OFF ||
		data->device_status == DEVICE_SUSPENDED)
		lsm303dlh_m_enable(data);

	dev_dbg(dev, "set operating mode to %lu\n", mode);
	error = i2c_smbus_write_byte_data(client, this_attr->address, mode);
	if (error < 0) {
		dev_err(&client->dev, "Error in setting the mode\n");
		count = -EINVAL;
		goto exit;
	}

	data->mode = mode;

	/* If mode is OFF then disable the regulator */
	if (data->mode == SLEEP_MODE) {
		data->rate = RATE_00_75;
		data->range = RANGE_1_3G;
		data->gain[data->pdata->axis_map_x] = XY_GAIN_1_3;
		data->gain[data->pdata->axis_map_y] = XY_GAIN_1_3;
		data->gain[data->pdata->axis_map_z] = Z_GAIN_1_3;
		lsm303dlh_m_disable(data);
	}
exit:
	mutex_unlock(&data->lock);
	return count;
}

static s32 lsm303dlh_set_config(struct i2c_client *client, u8 config)
{
	struct lsm303dlh_m_data *data = i2c_get_clientdata(client);
	u8 reg_val;

	reg_val = (config & CRA_MS_MASK) | (data->rate << CRA_DO_BIT);
	return i2c_smbus_write_byte_data(client, CRA_REG_M, reg_val);
}

static int set_configuration(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{

	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	unsigned long config = 0;

	int err = strict_strtoul(buf, 0, &config);
	if (err)
		return err;

	/*
	 * Perform read/write operation, only when device is active
	 */
	if (data->device_status != DEVICE_ON) {
		dev_info(&client->dev,
			"device is switched off,make it ON using MODE");
		return -EINVAL;
	}
	mutex_lock(&data->lock);

	dev_dbg(dev, "set measurement configuration to %lu\n", config);

	if (lsm303dlh_set_config(client, config)) {
		count = -EINVAL;
		dev_err(dev, "set configuration failed\n");
		goto exit;
	}

	data->config = config;
exit:
	mutex_unlock(&data->lock);
	return count;
}

static s32 show_configuration(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;

	return sprintf(buf, "%d\n", data->config);
}

static s32 lsm303dlh_m_set_rate(struct i2c_client *client, u8 rate)
{
	struct lsm303dlh_m_data *data = i2c_get_clientdata(client);
	u8 reg_val;

	reg_val =  (data->config) | (rate << CRA_DO_BIT);
	if (rate >= RATE_RESERVED) {
		dev_err(&client->dev, "given rate not supported\n");
		return -EINVAL;
	}

	return i2c_smbus_write_byte_data(client, CRA_REG_M, reg_val);
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("0.75 1.5 3.0 7.5 15 30 75");

static ssize_t set_sampling_frequency(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;
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
	if (strncmp(buf, "0.75" , 4) == 0)
		rate = RATE_00_75;

	else if (strncmp(buf, "1.5" , 3) == 0)
		rate = RATE_01_50;

	else if (strncmp(buf, "3.0" , 3) == 0)
		rate = RATE_03_00;

	else if (strncmp(buf, "7.5" , 3) == 0)
		rate = RATE_07_50;

	else if (strncmp(buf, "15" , 2) == 0)
		rate = RATE_15_00;

	else if (strncmp(buf, "30" , 2) == 0)
		rate = RATE_30_00;

	else if (strncmp(buf, "75" , 2) == 0)
		rate = RATE_75_00;
	else
		return -EINVAL;

	mutex_lock(&data->lock);

	if (lsm303dlh_m_set_rate(client, rate)) {
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
	"0.75",
	"1.5",
	"3.0",
	"7.5",
	"15",
	"30",
	"75",
	"res",
};

static ssize_t show_sampling_frequency(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;

	return sprintf(buf, "%s\n", reg_to_rate[data->rate]);
}

static ssize_t set_range(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;
	struct i2c_client *client = data->client;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
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

	if (range > RANGE_8_1G || range < RANGE_1_3G) {
		dev_err(dev, "wrong range %lu\n", range);
		count = -EINVAL;
		goto exit;
	}

	data->range = range;
	range <<= CRB_GN_BIT;

	if (i2c_smbus_write_byte_data(client, this_attr->address, range)) {
		count = -EINVAL;
		goto exit;
	}

	/* check if chip is DLH/DLHC */
	if (data->pdata->chip_id == LSM303DLHC_CHIP_ID)
		set_gain_dlhc(data);
	else
		set_gain_dlh(data);
exit:
	mutex_unlock(&data->lock);
	return count;
}

/*
 * array of register bit values to mgauss first element is 0
 * as register definition starts from 1.
 */
static const int reg_to_gauss[] = {
		0,
		1300,
		1900,
		2500,
		4000,
		4700,
		5600,
		8100
};

static ssize_t show_range(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct lsm303dlh_m_data *data = indio_dev->dev_data;

	return sprintf(buf, "%d\n", reg_to_gauss[data->range]);
}

static IIO_DEV_ATTR_MAGN_X(lsm303dlh_m_xyz_read,
			OUT_X_M);
static IIO_DEV_ATTR_MAGN_Y(lsm303dlh_m_xyz_read,
			OUT_Y_M);
static IIO_DEV_ATTR_MAGN_Z(lsm303dlh_m_xyz_read,
			OUT_Z_M);

static IIO_DEVICE_ATTR(sampling_frequency,
			S_IWUSR | S_IRUGO,
			show_sampling_frequency,
			set_sampling_frequency,
			CRA_REG_M);
static IIO_DEVICE_ATTR(magn_range,
			S_IWUSR | S_IRUGO,
			show_range,
			set_range,
			CRB_REG_M);
static IIO_DEVICE_ATTR(mode,
			S_IWUSR | S_IRUGO,
			show_operating_mode,
			set_operating_mode,
			MR_REG_M);
static IIO_DEVICE_ATTR(config, S_IWUSR | S_IRUGO,
			show_configuration,
			set_configuration,
			CRA_REG_M);
static IIO_DEVICE_ATTR(data, S_IRUGO,
			lsm303dlh_m_readdata,
			NULL,
			OUT_X_M);
static IIO_DEVICE_ATTR(magn_gain, S_IRUGO,
			lsm303dlh_m_gain,
			NULL,
			0);

static struct attribute *lsm303dlh_m_attributes[] = {
	&iio_dev_attr_config.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_magn_range.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_magn_x_raw.dev_attr.attr,
	&iio_dev_attr_magn_y_raw.dev_attr.attr,
	&iio_dev_attr_magn_z_raw.dev_attr.attr,
	&iio_dev_attr_data.dev_attr.attr,
	&iio_dev_attr_magn_gain.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group lsmdlh303m_group = {
	.attrs = lsm303dlh_m_attributes,
};

static const struct iio_info lsmdlh303m_info = {
	.attrs = &lsmdlh303m_group,
	.driver_module = THIS_MODULE,
};

static s32 lsm303dlh_config(struct i2c_client *client,	u8 mode)
{
	/* the lower two bits indicates the magnetic sensor mode */
	return i2c_smbus_write_byte_data(client, MR_REG_M, mode & 0x03);
}

static void lsm303dlh_m_setup(struct i2c_client *client)
{
	struct lsm303dlh_m_data *data = i2c_get_clientdata(client);

	lsm303dlh_set_config(client, data->config);
	lsm303dlh_m_set_rate(client, data->rate);
	lsm303dlh_config(client, data->mode);
	/* set the range */
	i2c_smbus_write_byte_data(client, CRB_REG_M, data->range);
}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_PM)
static int lsm303dlh_m_do_suspend(struct lsm303dlh_m_data *data)
{
	int ret = 0;

	if (data->mode == SLEEP_MODE)
		return 0;

	mutex_lock(&data->lock);

	/* Set the device to sleep mode */
	lsm303dlh_config(data->client, SLEEP_MODE);

	/* Disable regulator */
	lsm303dlh_m_disable(data);

	data->device_status = DEVICE_SUSPENDED;

	mutex_unlock(&data->lock);

	return ret;
}

static int lsm303dlh_m_restore(struct lsm303dlh_m_data *data)
{
	int ret = 0;

	if (data->device_status == DEVICE_ON ||
		data->device_status == DEVICE_OFF) {
		return 0;
	}
	mutex_lock(&data->lock);

	/* Enable regulator */
	lsm303dlh_m_enable(data);

	/* Setup device parameters */
	lsm303dlh_m_setup(data->client);

	mutex_unlock(&data->lock);
	return ret;
}
#endif

#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
static int lsm303dlh_m_suspend(struct device *dev)
{
	struct lsm303dlh_m_data *data;
	int ret;

	data = dev_get_drvdata(dev);

	ret = lsm303dlh_m_do_suspend(data);
	if (ret < 0)
		dev_err(&data->client->dev,
				"Error while suspending the device");

	return ret;
}

static int lsm303dlh_m_resume(struct device *dev)
{
	struct lsm303dlh_m_data *data;
	int ret;

	data = dev_get_drvdata(dev);

	ret = lsm303dlh_m_restore(data);

	if (ret < 0)
		dev_err(&data->client->dev,
				"Error while resuming the device");

	return ret;
}
static const struct dev_pm_ops lsm303dlh_m_dev_pm_ops = {
	.suspend = lsm303dlh_m_suspend,
	.resume  = lsm303dlh_m_resume,
};
#endif
#else
static void lsm303dlh_m_early_suspend(struct early_suspend *data)
{
	struct lsm303dlh_m_data *ddata =
		container_of(data, struct lsm303dlh_m_data, early_suspend);
	int ret;

	ret = lsm303dlh_m_do_suspend(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while suspending the device");
}

static void lsm303dlh_m_late_resume(struct early_suspend *data)
{
	struct lsm303dlh_m_data *ddata =
		container_of(data, struct lsm303dlh_m_data, early_suspend);
	int ret;

	ret = lsm303dlh_m_restore(ddata);

	if (ret < 0)
		dev_err(&ddata->client->dev,
				"lsm303dlh_m late resume failed\n");
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

static int lsm303dlh_m_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct lsm303dlh_m_data *data;
	int err = 0;

	data = kzalloc(sizeof(struct lsm303dlh_m_data), GFP_KERNEL);
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

	data->mode = SLEEP_MODE;
	data->config = NORMAL_CFG;
	data->range = RANGE_1_3G;
	data->rate = RATE_00_75;
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
	lsm303dlh_m_enable(data);

	lsm303dlh_m_setup(client);

	mutex_init(&data->lock);

	data->indio_dev = iio_allocate_device(0);
	if (!data->indio_dev) {
		dev_err(&client->dev, "iio allocation failed\n");
		err = -ENOMEM;
		goto exit2;
	}
	data->indio_dev->info = &lsmdlh303m_info;
	data->indio_dev->dev.parent = &client->dev;
	data->indio_dev->dev_data = (void *)data;
	data->indio_dev->modes = INDIO_DIRECT_MODE;

	err = iio_device_register(data->indio_dev);
	if (err)
		goto exit3;

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level =
				EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = lsm303dlh_m_early_suspend;
	data->early_suspend.resume = lsm303dlh_m_late_resume;
	register_early_suspend(&data->early_suspend);
#endif
	/* Disable regulator */
	lsm303dlh_m_disable(data);

	return 0;

exit3:
	iio_free_device(data->indio_dev);
exit2:
	lsm303dlh_m_disable(data);
	regulator_put(data->regulator);
exit1:
	kfree(data);
exit:
	return err;
}

static int __devexit lsm303dlh_m_remove(struct i2c_client *client)
{
	struct lsm303dlh_m_data *data = i2c_get_clientdata(client);
	int ret;

	/* safer to make device off */
	if (data->mode != SLEEP_MODE) {
		/* set mode to off */
		ret = lsm303dlh_config(client, SLEEP_MODE);
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

static const struct i2c_device_id lsm303dlh_m_id[] = {
	{ "lsm303dlh_m", 0 },
	{ },
};

static struct i2c_driver lsm303dlh_m_driver = {
	.driver = {
		.name	= "lsm303dlh_m",
	#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
		.pm = &lsm303dlh_m_dev_pm_ops,
	#endif
	},
	.id_table	= lsm303dlh_m_id,
	.probe		= lsm303dlh_m_probe,
	.remove		= lsm303dlh_m_remove,
};

static int __init lsm303dlh_m_init(void)
{
	return i2c_add_driver(&lsm303dlh_m_driver);
}

static void __exit lsm303dlh_m_exit(void)
{
	i2c_del_driver(&lsm303dlh_m_driver);
}

module_init(lsm303dlh_m_init);
module_exit(lsm303dlh_m_exit);

MODULE_DESCRIPTION("lsm303dlh Magnetometer Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("srinidhi kasagar <srinidhi.kasagar@stericsson.com>");
