/*
 * lsm303dlh_a.c
 * ST 3-Axis Accelerometer Driver
 *
 * Copyright (C) 2010 STMicroelectronics
 * Author: Carmine Iascone (carmine.iascone@st.com)
 * Author: Matteo Dameno (matteo.dameno@st.com)
 *
 * Copyright (C) 2010 STEricsson
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 * Updated:Preetham Rao Kaskurthi <preetham.rao@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
#include <linux/input.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#endif

#include <linux/lsm303dlh.h>
#include <linux/earlysuspend.h>
#include <linux/regulator/consumer.h>

 /* lsm303dlh accelerometer registers */
 #define WHO_AM_I    0x0F

 /* ctrl 1: pm2 pm1 pm0 dr1 dr0 zenable yenable zenable */
 #define CTRL_REG1       0x20    /* power control reg */
 #define CTRL_REG2       0x21    /* power control reg */
 #define CTRL_REG3       0x22    /* power control reg */
 #define CTRL_REG4       0x23    /* interrupt control reg */
 #define CTRL_REG5       0x24    /* interrupt control reg */

 #define STATUS_REG      0x27    /* status register */

 #define AXISDATA_REG    0x28    /* axis data */

 #define INT1_CFG  0x30    /* interrupt 1 configuration */
 #define INT1_SRC  0x31    /* interrupt 1 source reg    */
 #define INT1_THS  0x32    /* interrupt 1 threshold */
 #define INT1_DURATION  0x33    /* interrupt 1 threshold */

 #define INT2_CFG  0x34    /* interrupt 2 configuration */
 #define INT2_SRC  0x35    /* interrupt 2 source reg    */
 #define INT2_THS  0x36    /* interrupt 2 threshold */
 #define INT2_DURATION  0x37    /* interrupt 2 threshold */

 /* Sensitivity adjustment */
 #define SHIFT_ADJ_2G 4 /*    1/16*/
 #define SHIFT_ADJ_4G 3 /*    2/16*/
 #define SHIFT_ADJ_8G 2 /* ~3.9/16*/

 /* Control register 1 */
 #define LSM303DLH_A_CR1_PM_BIT 5
 #define LSM303DLH_A_CR1_PM_MASK (0x7 << LSM303DLH_A_CR1_PM_BIT)
 #define LSM303DLH_A_CR1_DR_BIT 3
 #define LSM303DLH_A_CR1_DR_MASK (0x3 << LSM303DLH_A_CR1_DR_BIT)
 #define LSM303DLH_A_CR1_EN_BIT 0
 #define LSM303DLH_A_CR1_EN_MASK (0x7 << LSM303DLH_A_CR1_EN_BIT)
 #define LSM303DLH_A_CR1_AXIS_ENABLE 7

 /* Control register 2 */
 #define LSM303DLH_A_CR4_ST_BIT 1
 #define LSM303DLH_A_CR4_ST_MASK (0x1 << LSM303DLH_A_CR4_ST_BIT)
 #define LSM303DLH_A_CR4_STS_BIT 3
 #define LSM303DLH_A_CR4_STS_MASK (0x1 << LSM303DLH_A_CR4_STS_BIT)
 #define LSM303DLH_A_CR4_FS_BIT 4
 #define LSM303DLH_A_CR4_FS_MASK (0x3 << LSM303DLH_A_CR4_FS_BIT)
 #define LSM303DLH_A_CR4_BLE_BIT 6
 #define LSM303DLH_A_CR4_BLE_MASK (0x3 << LSM303DLH_A_CR4_BLE_BIT)
 #define LSM303DLH_A_CR4_BDU_BIT 7
 #define LSM303DLH_A_CR4_BDU_MASK (0x1 << LSM303DLH_A_CR4_BDU_BIT)

 /* Control register 3 */
 #define LSM303DLH_A_CR3_I1_BIT 0
 #define LSM303DLH_A_CR3_I1_MASK (0x3 << LSM303DLH_A_CR3_I1_BIT)
 #define LSM303DLH_A_CR3_LIR1_BIT 2
 #define LSM303DLH_A_CR3_LIR1_MASK (0x1 << LSM303DLH_A_CR3_LIR1_BIT)
 #define LSM303DLH_A_CR3_I2_BIT 3
 #define LSM303DLH_A_CR3_I2_MASK (0x3 << LSM303DLH_A_CR3_I2_BIT)
 #define LSM303DLH_A_CR3_LIR2_BIT 5
 #define LSM303DLH_A_CR3_LIR2_MASK (0x1 << LSM303DLH_A_CR3_LIR2_BIT)
 #define LSM303DLH_A_CR3_PPOD_BIT 6
 #define LSM303DLH_A_CR3_PPOD_MASK (0x1 << LSM303DLH_A_CR3_PPOD_BIT)
 #define LSM303DLH_A_CR3_IHL_BIT 7
 #define LSM303DLH_A_CR3_IHL_MASK (0x1 << LSM303DLH_A_CR3_IHL_BIT)

 #define LSM303DLH_A_CR3_I_SELF 0x0
 #define LSM303DLH_A_CR3_I_OR   0x1
 #define LSM303DLH_A_CR3_I_DATA 0x2
 #define LSM303DLH_A_CR3_I_BOOT 0x3

 #define LSM303DLH_A_CR3_LIR_LATCH 0x1

 /* Range */
 #define LSM303DLH_A_RANGE_2G 0x00
 #define LSM303DLH_A_RANGE_4G 0x01
 #define LSM303DLH_A_RANGE_8G 0x03

 /* Mode */
 #define LSM303DLH_A_MODE_OFF 0x00
 #define LSM303DLH_A_MODE_NORMAL 0x01
 #define LSM303DLH_A_MODE_LP_HALF 0x02
 #define LSM303DLH_A_MODE_LP_1 0x03
 #define LSM303DLH_A_MODE_LP_2 0x02
 #define LSM303DLH_A_MODE_LP_5 0x05
 #define LSM303DLH_A_MODE_LP_10 0x06

 /* Rate */
 #define LSM303DLH_A_RATE_50 0x00
 #define LSM303DLH_A_RATE_100 0x01
 #define LSM303DLH_A_RATE_400 0x02
 #define LSM303DLH_A_RATE_1000 0x03

 /* Sleep & Wake */
 #define LSM303DLH_A_SLEEPWAKE_DISABLE 0x00
 #define LSM303DLH_A_SLEEPWAKE_ENABLE 0x3

/* Multiple byte transfer enable */
#define MULTIPLE_I2C_TR 0x80

/* device status defines */
#define DEVICE_OFF 0
#define DEVICE_ON 1
#define DEVICE_SUSPENDED 2

/* Range -2048 to 2047 */
struct lsm303dlh_a_t {
	short	x;
	short	y;
	short	z;
};

/**
 * struct lsm303dlh_a_data - data structure used by lsm303dlh_a driver
 * @client: i2c client
 * @lock: mutex lock for sysfs operations
 * @data: lsm303dlh_a_t struct containing x, y and z values
 * @input_dev: input device
 * @input_dev2: input device
 * @pdata: lsm303dlh platform data
 * @regulator: regulator
 * @range: current range value of accelerometer
 * @mode: current mode of operation
 * @rate: current sampling rate
 * @sleep_wake: sleep wake setting
 * @shift_adjust: current shift adjust value set according to range
 * @interrupt_control: interrupt control settings
 * @interrupt_channel: interrupt channel 0 or 1
 * @interrupt_configure: interrupt configurations for two channels
 * @interrupt_duration: interrupt duration for two channels
 * @interrupt_threshold: interrupt threshold for two channels
 * @early_suspend: early suspend structure
 * @device_status: device is ON, OFF or SUSPENDED
 * @id: accelerometer device id
 */
struct lsm303dlh_a_data {
	struct i2c_client *client;
	/* lock for sysfs operations */
	struct mutex lock;
	struct lsm303dlh_a_t data;

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
	struct input_dev *input_dev;
	struct input_dev *input_dev2;
#endif

	struct lsm303dlh_platform_data pdata;
	struct regulator *regulator;

	unsigned char range;
	unsigned char mode;
	unsigned char rate;
	unsigned char sleep_wake;
	int shift_adjust;

	unsigned char interrupt_control;
	unsigned int  interrupt_channel;

	unsigned char interrupt_configure[2];
	unsigned char interrupt_duration[2];
	unsigned char interrupt_threshold[2];
	struct early_suspend early_suspend;
	int device_status;
	int id;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void lsm303dlh_a_early_suspend(struct early_suspend *data);
static void lsm303dlh_a_late_resume(struct early_suspend *data);
#endif

static int lsm303dlh_a_write(struct lsm303dlh_a_data *ddata, u8 reg,
		u8 val, char *msg)
{
	int ret = i2c_smbus_write_byte_data(ddata->client, reg, val);
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"i2c_smbus_write_byte_data failed error %d\
			Register (%s)\n", ret, msg);
	return ret;
}

static int lsm303dlh_a_read(struct lsm303dlh_a_data *ddata, u8 reg, char *msg)
{
	int ret = i2c_smbus_read_byte_data(ddata->client, reg);
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"i2c_smbus_read_byte_data failed error %d\
			 Register (%s)\n", ret, msg);
	return ret;
}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_PM)
static int lsm303dlh_a_do_suspend(struct lsm303dlh_a_data *ddata)
{
	int ret;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		mutex_unlock(&ddata->lock);
		return 0;
	}

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
	disable_irq(gpio_to_irq(ddata->pdata.irq_a1));
	disable_irq(gpio_to_irq(ddata->pdata.irq_a2));
#endif

	ret = lsm303dlh_a_write(ddata, CTRL_REG1,
			LSM303DLH_A_MODE_OFF, "CONTROL");

	if (ddata->regulator)
		regulator_disable(ddata->regulator);

	ddata->device_status = DEVICE_SUSPENDED;

	mutex_unlock(&ddata->lock);

	return ret;
}

static int lsm303dlh_a_restore(struct lsm303dlh_a_data *ddata)
{
	unsigned char reg;
	unsigned char shifted_mode = (ddata->mode << LSM303DLH_A_CR1_PM_BIT);
	unsigned char shifted_rate = (ddata->rate << LSM303DLH_A_CR1_DR_BIT);
	unsigned char context = (shifted_mode | shifted_rate);
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->device_status == DEVICE_ON) {
		mutex_unlock(&ddata->lock);
		return 0;
	}

	/* in correct mode, no need to change it */
	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		ddata->device_status = DEVICE_OFF;
		mutex_unlock(&ddata->lock);
		return 0;
	} else
		ddata->device_status = DEVICE_ON;

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
	enable_irq(gpio_to_irq(ddata->pdata.irq_a1));
	enable_irq(gpio_to_irq(ddata->pdata.irq_a2));
#endif

	if (ddata->regulator)
		regulator_enable(ddata->regulator);

	/* BDU should be enabled by default/recommened */
	reg = ddata->range;
	reg |= LSM303DLH_A_CR4_BDU_MASK;
	context |= LSM303DLH_A_CR1_AXIS_ENABLE;

	ret = lsm303dlh_a_write(ddata, CTRL_REG1, context,
			"CTRL_REG1");
	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, CTRL_REG4, reg, "CTRL_REG4");

	if (ret < 0)
		goto fail;

	/* write to the boot bit to reboot memory content */
	ret = lsm303dlh_a_write(ddata, CTRL_REG2, 0x80, "CTRL_REG2");

	if (ret < 0)
		goto fail;

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
	ret = lsm303dlh_a_write(ddata, CTRL_REG3, ddata->interrupt_control,
			"CTRL_REG3");

	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, INT1_CFG, ddata->interrupt_configure[0],
			"INT1_CFG");

	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, INT2_CFG, ddata->interrupt_configure[1],
			"INT2_CFG");

	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, INT1_THS, ddata->interrupt_threshold[0],
			"INT1_THS");

	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, INT2_THS, ddata->interrupt_threshold[1],
			"INT2_THS");

	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, INT1_DURATION,
			ddata->interrupt_duration[0], "INT1_DURATION");

	if (ret < 0)
		goto fail;

	ret = lsm303dlh_a_write(ddata, INT1_DURATION,
			ddata->interrupt_duration[1], "INT1_DURATION");

	if (ret < 0)
		goto fail;
#endif

fail:
	if (ret < 0)
		dev_err(&ddata->client->dev, "could not restore the device %d\n", ret);
	mutex_unlock(&ddata->lock);
	return ret;
}
#endif

static int lsm303dlh_a_readdata(struct lsm303dlh_a_data *ddata)
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

static ssize_t lsm303dlh_a_show_data(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF ||
			ddata->device_status == DEVICE_SUSPENDED) {
		mutex_unlock(&ddata->lock);
		return ret;
	}

	ret = lsm303dlh_a_readdata(ddata);

	if (ret < 0) {
		mutex_unlock(&ddata->lock);
		return ret;
	}

	mutex_unlock(&ddata->lock);

	return sprintf(buf, "%8x:%8x:%8x\n", ddata->data.x, ddata->data.y,
			ddata->data.z);
}

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
static irqreturn_t lsm303dlh_a_gpio_irq(int irq, void *device_data)
{

	struct lsm303dlh_a_data *ddata = device_data;
	int ret;
	unsigned char reg;
	struct input_dev *input;

	/* know your interrupt source */
	if (irq == gpio_to_irq(ddata->pdata.irq_a1)) {
		reg = INT1_SRC;
		input = ddata->input_dev;
	} else if (irq == gpio_to_irq(ddata->pdata.irq_a2)) {
		reg = INT2_SRC;
		input = ddata->input_dev2;
	} else {
		dev_err(&ddata->client->dev, "spurious interrupt");
		return IRQ_HANDLED;
	}

	/* read the axis */
	ret = lsm303dlh_a_readdata(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"reading data of xyz failed error %d\n", ret);

	input_report_abs(input, ABS_X, ddata->data.x);
	input_report_abs(input, ABS_Y, ddata->data.y);
	input_report_abs(input, ABS_Z, ddata->data.z);
	input_sync(input);

	/* clear the value by reading it */
	ret = lsm303dlh_a_read(ddata, reg, "INTTERUPT SOURCE");
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"clearing interrupt source failed error %d\n", ret);

	return IRQ_HANDLED;

}

static ssize_t lsm303dlh_a_show_interrupt_control(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->interrupt_control);
}

static ssize_t lsm303dlh_a_store_interrupt_control(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		mutex_unlock(&ddata->lock);
		return count;
	}

	ddata->interrupt_control = val;

	error = lsm303dlh_a_write(ddata, CTRL_REG3, val, "CTRL_REG3");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_interrupt_channel(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->interrupt_channel);
}

static ssize_t lsm303dlh_a_store_interrupt_channel(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	ddata->interrupt_channel = val;

	return count;
}

static ssize_t lsm303dlh_a_show_interrupt_configure(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n",
			ddata->interrupt_configure[ddata->interrupt_channel]);
}

static ssize_t lsm303dlh_a_store_interrupt_configure(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
	    mutex_unlock(&ddata->lock);
		return count;
	}

	ddata->interrupt_configure[ddata->interrupt_channel] = val;

	if (ddata->interrupt_channel == 0x0)
		error = lsm303dlh_a_write(ddata, INT1_CFG, val, "INT1_CFG");
	else
		error = lsm303dlh_a_write(ddata, INT2_CFG, val, "INT2_CFG");

	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_interrupt_duration(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n",
			ddata->interrupt_duration[ddata->interrupt_channel]);
}

static ssize_t lsm303dlh_a_store_interrupt_duration(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);


	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		mutex_unlock(&ddata->lock);
		return count;
	}

	ddata->interrupt_duration[ddata->interrupt_channel] = val;

	if (ddata->interrupt_channel == 0x0)
		error = lsm303dlh_a_write(ddata, INT1_DURATION, val,
				"INT1_DURATION");
	else
		error = lsm303dlh_a_write(ddata, INT2_DURATION, val,
				"INT2_DURATION");

	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_interrupt_threshold(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n",
			ddata->interrupt_threshold[ddata->interrupt_channel]);
}

static ssize_t lsm303dlh_a_store_interrupt_threshold(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		mutex_unlock(&ddata->lock);
		return count;
	}

	ddata->interrupt_threshold[ddata->interrupt_channel] = val;

	if (ddata->interrupt_channel == 0x0)
		error = lsm303dlh_a_write(ddata, INT1_THS, val, "INT1_THS");
	else
		error = lsm303dlh_a_write(ddata, INT2_THS, val, "INT2_THS");

	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);

	return count;
}
#endif

static ssize_t lsm303dlh_a_show_range(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->range >> LSM303DLH_A_CR4_FS_BIT);
}

static ssize_t lsm303dlh_a_store_range(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	long val;
	unsigned long bdu_enabled_val;
	int error;


	error = strict_strtol(buf, 0, &val);
	if (error)
		return error;

	if (val < LSM303DLH_A_RANGE_2G || val > LSM303DLH_A_RANGE_8G)
		return -EINVAL;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		mutex_unlock(&ddata->lock);
		return count;
	}

	ddata->range = val;
	ddata->range <<= LSM303DLH_A_CR4_FS_BIT;

	/*
	 * Block mode update is recommended for not
	 * ending up reading different values
	 */
	bdu_enabled_val = ddata->range;
	bdu_enabled_val |= LSM303DLH_A_CR4_BDU_MASK;

	error = lsm303dlh_a_write(ddata, CTRL_REG4, bdu_enabled_val,
	"CTRL_REG4");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	switch (val) {
	case LSM303DLH_A_RANGE_2G:
		ddata->shift_adjust = SHIFT_ADJ_2G;
		break;
	case LSM303DLH_A_RANGE_4G:
		ddata->shift_adjust = SHIFT_ADJ_4G;
		break;
	case LSM303DLH_A_RANGE_8G:
		ddata->shift_adjust = SHIFT_ADJ_8G;
		break;
	default:
		mutex_unlock(&ddata->lock);
		return -EINVAL;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_mode(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->mode);
}

static ssize_t lsm303dlh_a_store_mode(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	long val;
	unsigned char data;
	int error;
	bool set_boot_bit = false;

	error = strict_strtol(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);

	/* not in correct range */

	if (val < LSM303DLH_A_MODE_OFF || val > LSM303DLH_A_MODE_LP_10) {
		mutex_unlock(&ddata->lock);
		return -EINVAL;
	}

	if (ddata->device_status == DEVICE_SUSPENDED) {
		if (val == LSM303DLH_A_MODE_OFF) {
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
	if (ddata->regulator && ddata->mode == LSM303DLH_A_MODE_OFF
			&& (ddata->device_status == DEVICE_OFF
				|| ddata->device_status == DEVICE_SUSPENDED)) {
		regulator_enable(ddata->regulator);
		ddata->device_status = DEVICE_ON;
#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
		enable_irq(gpio_to_irq(ddata->pdata.irq_a1));
		enable_irq(gpio_to_irq(ddata->pdata.irq_a2));
#endif
	}

	data = lsm303dlh_a_read(ddata, CTRL_REG1, "CTRL_REG1");

	/*
	 * If chip doesn't get reset during suspend/resume,
	 * x,y and z axis bits are getting cleared,so set
	 * these bits to get x,y,z axis data.
	 */
	data |= LSM303DLH_A_CR1_AXIS_ENABLE;
	data &= ~LSM303DLH_A_CR1_PM_MASK;

	ddata->mode = val;
	data |= ((val << LSM303DLH_A_CR1_PM_BIT) & LSM303DLH_A_CR1_PM_MASK);

	error = lsm303dlh_a_write(ddata, CTRL_REG1, data, "CTRL_REG1");
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
		error = lsm303dlh_a_write(ddata, CTRL_REG2, 0x80, "CTRL_REG2");
		if (error < 0) {
			if (ddata->regulator && ddata->device_status == DEVICE_ON) {
				regulator_disable(ddata->regulator);
				ddata->device_status = DEVICE_OFF;
			}
			mutex_unlock(&ddata->lock);
			return error;
		}
	}

	if (val == LSM303DLH_A_MODE_OFF) {
#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
		disable_irq(gpio_to_irq(ddata->pdata.irq_a1));
		disable_irq(gpio_to_irq(ddata->pdata.irq_a2));
#endif
		/*
		 * No need to store context here
		 * it is not like suspend/resume
		 * but fall back to default values
		 */
		ddata->rate = LSM303DLH_A_RATE_50;
		ddata->range = LSM303DLH_A_RANGE_2G;
		ddata->shift_adjust = SHIFT_ADJ_2G;

		if (ddata->regulator && ddata->device_status == DEVICE_ON) {
			regulator_disable(ddata->regulator);
			ddata->device_status = DEVICE_OFF;
		}
	}
	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_rate(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->rate);
}

static ssize_t lsm303dlh_a_store_rate(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	long val;
	unsigned char data;
	int error;

	error = strict_strtol(buf, 0, &val);
	if (error)
		return error;

	if (val < LSM303DLH_A_RATE_50 || val > LSM303DLH_A_RATE_1000)
		return -EINVAL;

	mutex_lock(&ddata->lock);

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		mutex_unlock(&ddata->lock);
		return count;
	}

	data = lsm303dlh_a_read(ddata, CTRL_REG1, "CTRL_REG1");

	data &= ~LSM303DLH_A_CR1_DR_MASK;

	ddata->rate = val;

	data |= ((val << LSM303DLH_A_CR1_DR_BIT) & LSM303DLH_A_CR1_DR_MASK);

	error = lsm303dlh_a_write(ddata, CTRL_REG1, data, "CTRL_REG1");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_sleepwake(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->sleep_wake);
}

static ssize_t lsm303dlh_a_store_sleepwake(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);
	long val;
	int error;

	if (ddata->mode == LSM303DLH_A_MODE_OFF) {
		dev_info(&ddata->client->dev,
				"device is switched off,make it ON using MODE");
		return count;
	}

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	mutex_lock(&ddata->lock);

	ddata->sleep_wake = val;

	error = lsm303dlh_a_write(ddata, CTRL_REG5, ddata->sleep_wake,
				"CTRL_REG5");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		return error;
	}

	mutex_unlock(&ddata->lock);

	return count;
}

static ssize_t lsm303dlh_a_show_id(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lsm303dlh_a_data *ddata = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", ddata->id);
}

static DEVICE_ATTR(id, S_IRUGO, lsm303dlh_a_show_id, NULL);

static DEVICE_ATTR(data, S_IRUGO, lsm303dlh_a_show_data, NULL);

static DEVICE_ATTR(range, S_IWUSR | S_IRUGO,
		lsm303dlh_a_show_range, lsm303dlh_a_store_range);

static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO,
		lsm303dlh_a_show_mode, lsm303dlh_a_store_mode);

static DEVICE_ATTR(rate, S_IWUSR | S_IRUGO,
		lsm303dlh_a_show_rate, lsm303dlh_a_store_rate);

static DEVICE_ATTR(sleep_wake, S_IWUSR | S_IRUGO,
		lsm303dlh_a_show_sleepwake, lsm303dlh_a_store_sleepwake);

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
static DEVICE_ATTR(interrupt_control, S_IWUGO | S_IRUGO,
		lsm303dlh_a_show_interrupt_control,
		lsm303dlh_a_store_interrupt_control);

static DEVICE_ATTR(interrupt_channel, S_IWUGO | S_IRUGO,
		lsm303dlh_a_show_interrupt_channel,
		lsm303dlh_a_store_interrupt_channel);

static DEVICE_ATTR(interrupt_configure, S_IWUGO | S_IRUGO,
		lsm303dlh_a_show_interrupt_configure,
		lsm303dlh_a_store_interrupt_configure);

static DEVICE_ATTR(interrupt_duration, S_IWUGO | S_IRUGO,
		lsm303dlh_a_show_interrupt_duration,
		lsm303dlh_a_store_interrupt_duration);

static DEVICE_ATTR(interrupt_threshold, S_IWUGO | S_IRUGO,
		lsm303dlh_a_show_interrupt_threshold,
		lsm303dlh_a_store_interrupt_threshold);
#endif

static struct attribute *lsm303dlh_a_attributes[] = {
	&dev_attr_id.attr,
	&dev_attr_data.attr,
	&dev_attr_range.attr,
	&dev_attr_mode.attr,
	&dev_attr_rate.attr,
	&dev_attr_sleep_wake.attr,
#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
	&dev_attr_interrupt_control.attr,
	&dev_attr_interrupt_channel.attr,
	&dev_attr_interrupt_configure.attr,
	&dev_attr_interrupt_duration.attr,
	&dev_attr_interrupt_threshold.attr,
#endif
	NULL
};

static const struct attribute_group lsm303dlh_a_attr_group = {
	.attrs = lsm303dlh_a_attributes,
};

static int __devinit lsm303dlh_a_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int ret;
	struct lsm303dlh_a_data *ddata = NULL;

	ddata = kzalloc(sizeof(struct lsm303dlh_a_data), GFP_KERNEL);
	if (ddata == NULL) {
		dev_err(&client->dev, "memory alocation failed\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	ddata->client = client;
	i2c_set_clientdata(client, ddata);

	/* copy platform specific data */
	memcpy(&ddata->pdata, client->dev.platform_data, sizeof(ddata->pdata));
	ddata->mode = LSM303DLH_A_MODE_OFF;
	ddata->rate = LSM303DLH_A_RATE_50;
	ddata->range = LSM303DLH_A_RANGE_2G;
	ddata->sleep_wake = LSM303DLH_A_SLEEPWAKE_DISABLE;
	ddata->shift_adjust = SHIFT_ADJ_2G;
	ddata->device_status = DEVICE_OFF;
	dev_set_name(&client->dev, ddata->pdata.name_a);

	ddata->regulator = regulator_get(&client->dev, "vdd");
	if (IS_ERR(ddata->regulator)) {
		dev_err(&client->dev, "failed to get regulator\n");
		ret = PTR_ERR(ddata->regulator);
		ddata->regulator = NULL;
		goto err_op_failed;
	}

	if (ddata->regulator) {
		/*
		 * 0.83 milliamps typical with magnetic sensor setting ODR =
		 * 7.5 Hz, Accelerometer sensor ODR = 50 Hz.  Double for
		 * safety.
		 */
		regulator_set_optimum_mode(ddata->regulator, 830 * 2);
		regulator_enable(ddata->regulator);
		ddata->device_status = DEVICE_ON;
	}

	ret = lsm303dlh_a_read(ddata, WHO_AM_I, "WHO_AM_I");
	if (ret < 0)
		goto exit_free_regulator;

	dev_info(&client->dev, "3-Axis Accelerometer, ID : %d\n",
			 ret);
	ddata->id = ret;

	mutex_init(&ddata->lock);

	ret = sysfs_create_group(&client->dev.kobj, &lsm303dlh_a_attr_group);
	if (ret)
		goto exit_free_regulator;

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE

    /* accelerometer has two interrupts channels
       (thresholds,durations and sources)
       and can support two input devices */

	ddata->input_dev = input_allocate_device();
	if (!ddata->input_dev) {
		ret = -ENOMEM;
		dev_err(&client->dev, "Failed to allocate input device\n");
		goto exit_free_regulator;
	}

	ddata->input_dev2 = input_allocate_device();
	if (!ddata->input_dev2) {
		ret = -ENOMEM;
		dev_err(&client->dev, "Failed to allocate input device\n");
		goto err_input_alloc_failed;
	}

	set_bit(EV_ABS, ddata->input_dev->evbit);
	set_bit(EV_ABS, ddata->input_dev2->evbit);

	/* x-axis acceleration */
	input_set_abs_params(ddata->input_dev, ABS_X, -32768, 32767, 0, 0);
	input_set_abs_params(ddata->input_dev2, ABS_X, -32768, 32767, 0, 0);
	/* y-axis acceleration */
	input_set_abs_params(ddata->input_dev, ABS_Y, -32768, 32767, 0, 0);
	input_set_abs_params(ddata->input_dev2, ABS_Y, -32768, 32767, 0, 0);
	/* z-axis acceleration */
	input_set_abs_params(ddata->input_dev, ABS_Z, -32768, 32767, 0, 0);
	input_set_abs_params(ddata->input_dev2, ABS_Z, -32768, 32767, 0, 0);

	ddata->input_dev->name = "accelerometer";
	ddata->input_dev2->name = "motion";

	ret = input_register_device(ddata->input_dev);
	if (ret) {
		dev_err(&client->dev, "Unable to register input device: %s\n",
			ddata->input_dev->name);
		goto err_input_register_failed;
	}

	ret = input_register_device(ddata->input_dev2);
	if (ret) {
		dev_err(&client->dev, "Unable to register input device: %s\n",
			ddata->input_dev->name);
		goto err_input_register_failed2;
	}

	/* Register interrupt */
	ret = request_threaded_irq(gpio_to_irq(ddata->pdata.irq_a1), NULL,
			lsm303dlh_a_gpio_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"lsm303dlh_a", ddata);
	if (ret) {
		dev_err(&client->dev, "request irq1 failed\n");
		goto err_input_failed;
	}

	ret = request_threaded_irq(gpio_to_irq(ddata->pdata.irq_a2), NULL,
			lsm303dlh_a_gpio_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"lsm303dlh_a", ddata);
	if (ret) {
		dev_err(&client->dev, "request irq2 failed\n");
		goto err_input_failed;
	}

	/* only mode can enable it */
	disable_irq(gpio_to_irq(ddata->pdata.irq_a1));
	disable_irq(gpio_to_irq(ddata->pdata.irq_a2));

#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	ddata->early_suspend.level =
			EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ddata->early_suspend.suspend = lsm303dlh_a_early_suspend;
	ddata->early_suspend.resume = lsm303dlh_a_late_resume;
	register_early_suspend(&ddata->early_suspend);
#endif

	if (ddata->device_status == DEVICE_ON && ddata->regulator) {
		regulator_disable(ddata->regulator);
		ddata->device_status = DEVICE_OFF;
	}
	return ret;

#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
err_input_failed:
	input_unregister_device(ddata->input_dev2);
err_input_register_failed2:
	input_unregister_device(ddata->input_dev);
err_input_register_failed:
	input_free_device(ddata->input_dev2);
err_input_alloc_failed:
	input_free_device(ddata->input_dev);
#endif
exit_free_regulator:
	if (ddata->device_status == DEVICE_ON && ddata->regulator) {
		regulator_disable(ddata->regulator);
		regulator_put(ddata->regulator);
		ddata->device_status = DEVICE_OFF;
	}

err_op_failed:
	kfree(ddata);
err_alloc:
	dev_err(&client->dev, "probe function fails %x", ret);
	return ret;
}

static int __devexit lsm303dlh_a_remove(struct i2c_client *client)
{
	int ret;
	struct lsm303dlh_a_data *ddata;

	ddata = i2c_get_clientdata(client);
#ifdef CONFIG_SENSORS_LSM303DLH_INPUT_DEVICE
	input_unregister_device(ddata->input_dev);
	input_unregister_device(ddata->input_dev2);
	input_free_device(ddata->input_dev);
	input_free_device(ddata->input_dev2);
#endif
	sysfs_remove_group(&client->dev.kobj, &lsm303dlh_a_attr_group);

	/* safer to make device off */
	if (ddata->mode != LSM303DLH_A_MODE_OFF) {
		ret = lsm303dlh_a_write(ddata, CTRL_REG1, 0, "CONTROL");

		if (ret < 0) {
			dev_err(&client->dev, "could not turn off the device %d", ret);
			return ret;
		}

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

#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
static int lsm303dlh_a_suspend(struct device *dev)
{
	struct lsm303dlh_a_data *ddata;
	int ret;

	ddata = dev_get_drvdata(dev);

	ret = lsm303dlh_a_do_suspend(ddata);

	return ret;
}

static int lsm303dlh_a_resume(struct device *dev)
{
	struct lsm303dlh_a_data *ddata;
	int ret;

	ddata = dev_get_drvdata(dev);

	ret	= lsm303dlh_a_restore(ddata);

	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while resuming the device");

	return ret;
}
static const struct dev_pm_ops lsm303dlh_a_dev_pm_ops = {
	.suspend = lsm303dlh_a_suspend,
	.resume  = lsm303dlh_a_resume,
};
#endif
#else
static void lsm303dlh_a_early_suspend(struct early_suspend *data)
{
	struct lsm303dlh_a_data *ddata =
		container_of(data, struct lsm303dlh_a_data, early_suspend);
	int ret;

	ret = lsm303dlh_a_do_suspend(ddata);
}

static void lsm303dlh_a_late_resume(struct early_suspend *data)
{
	struct lsm303dlh_a_data *ddata =
		container_of(data, struct lsm303dlh_a_data, early_suspend);
	int ret;

	ret = lsm303dlh_a_restore(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"lsm303dlh_a late resume failed\n");
}
#endif /* CONFIG_PM */

static const struct i2c_device_id lsm303dlh_a_id[] = {
	{ "lsm303dlh_a", 0 },
	{ },
};

static struct i2c_driver lsm303dlh_a_driver = {
	.probe		= lsm303dlh_a_probe,
	.remove		= lsm303dlh_a_remove,
	.id_table	= lsm303dlh_a_id,
	.driver = {
		.name = "lsm303dlh_a",
	#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
		.pm	=	&lsm303dlh_a_dev_pm_ops,
	#endif
	},
};

static int __init lsm303dlh_a_init(void)
{
	return i2c_add_driver(&lsm303dlh_a_driver);
}

static void __exit lsm303dlh_a_exit(void)
{
	i2c_del_driver(&lsm303dlh_a_driver);
}

module_init(lsm303dlh_a_init)
module_exit(lsm303dlh_a_exit)

MODULE_DESCRIPTION("lSM303DLH 3-Axis Accelerometer Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("STMicroelectronics");
