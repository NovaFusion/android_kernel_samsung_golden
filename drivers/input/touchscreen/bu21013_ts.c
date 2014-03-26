/*
 * Copyright (C) ST-Ericsson SA 2009
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * License terms:GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/input/bu21013.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#define PEN_DOWN_INTR	0
#define RESET_DELAY	30
#define PENUP_TIMEOUT	2 /* 2msecs */
#define SCALE_FACTOR	1000
#define DELTA_MIN	16
#define MASK_BITS	0x03
#define SHIFT_8		8
#define SHIFT_2		2
#define LENGTH_OF_BUFFER	11
#define I2C_RETRY_COUNT	5

#define BU21013_SENSORS_BTN_0_7_REG	0x70
#define BU21013_SENSORS_BTN_8_15_REG	0x71
#define BU21013_SENSORS_BTN_16_23_REG	0x72
#define BU21013_X1_POS_MSB_REG		0x73
#define BU21013_X1_POS_LSB_REG		0x74
#define BU21013_Y1_POS_MSB_REG		0x75
#define BU21013_Y1_POS_LSB_REG		0x76
#define BU21013_X2_POS_MSB_REG		0x77
#define BU21013_X2_POS_LSB_REG		0x78
#define BU21013_Y2_POS_MSB_REG		0x79
#define BU21013_Y2_POS_LSB_REG		0x7A
#define BU21013_INT_CLR_REG		0xE8
#define BU21013_INT_MODE_REG		0xE9
#define BU21013_GAIN_REG		0xEA
#define BU21013_OFFSET_MODE_REG		0xEB
#define BU21013_XY_EDGE_REG		0xEC
#define BU21013_RESET_REG		0xED
#define BU21013_CALIB_REG		0xEE
#define BU21013_DONE_REG		0xEF
#define BU21013_SENSOR_0_7_REG		0xF0
#define BU21013_SENSOR_8_15_REG		0xF1
#define BU21013_SENSOR_16_23_REG	0xF2
#define BU21013_POS_MODE1_REG		0xF3
#define BU21013_POS_MODE2_REG		0xF4
#define BU21013_CLK_MODE_REG		0xF5
#define BU21013_IDLE_REG		0xFA
#define BU21013_FILTER_REG		0xFB
#define BU21013_TH_ON_REG		0xFC
#define BU21013_TH_OFF_REG		0xFD


#define BU21013_RESET_ENABLE		0x01

#define BU21013_SENSORS_EN_0_7		0x3F
#define BU21013_SENSORS_EN_8_15		0xFC
#define BU21013_SENSORS_EN_16_23	0x1F

#define BU21013_POS_MODE1_0		0x02
#define BU21013_POS_MODE1_1		0x04
#define BU21013_POS_MODE1_2		0x08

#define BU21013_POS_MODE2_ZERO		0x01
#define BU21013_POS_MODE2_AVG1		0x02
#define BU21013_POS_MODE2_AVG2		0x04
#define BU21013_POS_MODE2_EN_XY		0x08
#define BU21013_POS_MODE2_EN_RAW	0x10
#define BU21013_POS_MODE2_MULTI		0x80

#define BU21013_CLK_MODE_DIV		0x01
#define BU21013_CLK_MODE_EXT		0x02
#define BU21013_CLK_MODE_CALIB		0x80

#define BU21013_IDLET_0			0x01
#define BU21013_IDLET_1			0x02
#define BU21013_IDLET_2			0x04
#define BU21013_IDLET_3			0x08
#define BU21013_IDLE_INTERMIT_EN	0x10

#define BU21013_DELTA_0_6	0x7F
#define BU21013_FILTER_EN	0x80

#define BU21013_INT_MODE_LEVEL	0x00
#define BU21013_INT_MODE_EDGE	0x01

#define BU21013_GAIN_0		0x01
#define BU21013_GAIN_1		0x02
#define BU21013_GAIN_2		0x04

#define BU21013_OFFSET_MODE_DEFAULT	0x00
#define BU21013_OFFSET_MODE_MOVE	0x01
#define BU21013_OFFSET_MODE_DISABLE	0x02

#define BU21013_TH_ON_0		0x01
#define BU21013_TH_ON_1		0x02
#define BU21013_TH_ON_2		0x04
#define BU21013_TH_ON_3		0x08
#define BU21013_TH_ON_4		0x10
#define BU21013_TH_ON_5		0x20
#define BU21013_TH_ON_6		0x40
#define BU21013_TH_ON_7		0x80
#define BU21013_TH_ON_MAX	0xFF

#define BU21013_TH_OFF_0	0x01
#define BU21013_TH_OFF_1	0x02
#define BU21013_TH_OFF_2	0x04
#define BU21013_TH_OFF_3	0x08
#define BU21013_TH_OFF_4	0x10
#define BU21013_TH_OFF_5	0x20
#define BU21013_TH_OFF_6	0x40
#define BU21013_TH_OFF_7	0x80
#define BU21013_TH_OFF_MAX	0xFF

#define BU21013_X_EDGE_0	0x01
#define BU21013_X_EDGE_1	0x02
#define BU21013_X_EDGE_2	0x04
#define BU21013_X_EDGE_3	0x08
#define BU21013_Y_EDGE_0	0x10
#define BU21013_Y_EDGE_1	0x20
#define BU21013_Y_EDGE_2	0x40
#define BU21013_Y_EDGE_3	0x80

#define BU21013_DONE	0x01
#define BU21013_NUMBER_OF_X_SENSORS	(6)
#define BU21013_NUMBER_OF_Y_SENSORS	(11)

#define DRIVER_TP	"bu21013_ts"

/**
 * struct bu21013_ts_data - touch panel data structure
 * @client: pointer to the i2c client
 * @wait: variable to wait_queue_head_t structure
 * @touch_stopped: touch stop flag
 * @chip: pointer to the touch panel controller
 * @in_dev: pointer to the input device structure
 * @intr_pin: interrupt pin value
 * @regulator: pointer to the Regulator used for touch screen
 * @enable: variable to indicate the enable/disable of touch screen
 * @ext_clk_enable: true if running on ext clk
 * @ext_clk_state: Saved state for suspend/resume of ext clk
 * @factor_x: x scale factor
 * @factor_y: y scale factor
 * @tpclk: pointer to clock structure
 * @early_suspend: early_suspend structure variable
 *
 * Touch panel device data structure
 */
struct bu21013_ts_data {
	struct i2c_client *client;
	wait_queue_head_t wait;
	bool touch_stopped;
	struct bu21013_platform_device *chip;
	struct input_dev *in_dev;
	unsigned int intr_pin;
	struct regulator *regulator;
	bool enable;
	bool ext_clk_enable;
	bool ext_clk_state;
	unsigned int factor_x;
	unsigned int factor_y;
	struct clk *tpclk;
	struct early_suspend early_suspend;
};

static int bu21013_init_chip(struct bu21013_ts_data *data, bool on_ext_clk);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bu21013_ts_early_suspend(struct early_suspend *data);
static void bu21013_ts_late_resume(struct early_suspend *data);
#endif

/**
 * bu21013_ext_clk() - enable/disable the external clock
 * @pdata: touch screen data
 * @enable: enable external clock
 * @reconfig: reconfigure chip upon external clock off.
 *
 * This function used to enable or disable the external clock and possible
 * reconfigure hw.
 */
static int bu21013_ext_clk(struct bu21013_ts_data *pdata, bool enable,
			   bool reconfig)
{
	int retval = 0;

	if (!pdata->tpclk || pdata->ext_clk_enable == enable)
		return retval;

	if (enable) {
		pdata->ext_clk_enable = true;
		clk_enable(pdata->tpclk);
		retval = bu21013_init_chip(pdata, true);
	} else {
		pdata->ext_clk_enable = false;
		if (reconfig)
			retval = bu21013_init_chip(pdata, false);
		clk_disable(pdata->tpclk);
	}
	return retval;
}

/**
 * bu21013_enable() - enable the touch driver event
 * @pdata: touch screen data
 *
 * This function used to enable the driver and returns integer
 */
static int bu21013_enable(struct bu21013_ts_data *pdata)
{
	int retval;

	if (pdata->regulator)
		regulator_enable(pdata->regulator);

	if (pdata->chip->cs_en) {
		retval = pdata->chip->cs_en(pdata->chip->cs_pin);
		if (retval < 0) {
			dev_err(&pdata->client->dev, "enable hw failed\n");
			return retval;
		}
	}

	if (pdata->ext_clk_state)
		retval = bu21013_ext_clk(pdata, true, true);
	else
		retval = bu21013_init_chip(pdata, false);

	if (retval < 0) {
		dev_err(&pdata->client->dev, "enable hw failed\n");
		return retval;
	}
	pdata->touch_stopped = false;
	enable_irq(pdata->chip->irq);

	return 0;
}

/**
 * bu21013_disable() - disable the touch driver event
 * @pdata: touch screen data
 *
 * This function used to disable the driver and returns integer
 */
static void bu21013_disable(struct bu21013_ts_data *pdata)
{
	pdata->touch_stopped = true;

	pdata->ext_clk_state = pdata->ext_clk_enable;
	(void) bu21013_ext_clk(pdata, false, false);

	disable_irq(pdata->chip->irq);
	if (pdata->chip->cs_dis)
		pdata->chip->cs_dis(pdata->chip->cs_pin);
	if (pdata->regulator)
		regulator_disable(pdata->regulator);
}

/**
 * bu21013_show_attr_enable() - show the touch screen controller status
 * @dev: pointer to device structure
 * @attr: pointer to device attribute
 * @buf: parameter buffer
 *
 * This funtion is used to show whether the touch screen is enabled or
 * disabled
 */
static ssize_t bu21013_show_attr_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bu21013_ts_data *pdata = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", pdata->enable);
}

/**
 * bu21013_store_attr_enable() - Enable/Disable the touchscreen.
 * @dev: pointer to device structure
 * @attr: pointer to device attribute
 * @buf: parameter buffer
 * @count: number of parameters
 *
 * This funtion is used to enable or disable the touch screen controller.
 */
static ssize_t bu21013_store_attr_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	unsigned long val;

	struct bu21013_ts_data *pdata = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if ((val != 0) && (val != 1))
		return -EINVAL;

	if (pdata->enable != val) {
		pdata->enable = val ? true : false;
		if (pdata->enable) {
			ret = bu21013_enable(pdata);
			if (ret < 0)
				return ret;
		} else
			bu21013_disable(pdata);
	}
	return count;
}

/**
 * bu21013_show_attr_extclk() - shows the external clock status
 * @dev: pointer to device structure
 * @attr: pointer to device attribute
 * @buf: parameter buffer
 *
 * This funtion is used to show whether the external clock for the touch
 * screen is enabled or disabled.
 */
static ssize_t bu21013_show_attr_extclk(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bu21013_ts_data *pdata = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", pdata->ext_clk_enable);
}

/**
 * bu21013_store_attr_extclk() - Enable/Disable the external clock
 * for the tocuh screen controller.
 * @dev: pointer to device structure
 * @attr: pointer to device attribute
 * @buf: parameter buffer
 * @count: number of parameters
 *
 * This funtion is used enabled or disable the external clock for the touch
 * screen controller.
 */
static ssize_t bu21013_store_attr_extclk(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	struct bu21013_ts_data *pdata = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if ((val != 0) && (val != 1))
		return -EINVAL;

	if (pdata->chip->has_ext_clk) {
		if (pdata->enable)
			retval = bu21013_ext_clk(pdata, val, true);
		else
			pdata->ext_clk_state = val;
		if (retval < 0)
			return retval;
	}
	return count;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO,
		bu21013_show_attr_enable, bu21013_store_attr_enable);

static DEVICE_ATTR(ext_clk, S_IWUSR | S_IRUGO,
		bu21013_show_attr_extclk, bu21013_store_attr_extclk);


static struct attribute *bu21013_attribute[] = {
		&dev_attr_enable.attr,
		&dev_attr_ext_clk.attr,
		NULL,
};

static struct attribute_group bu21013_attr_group = {
		.attrs = bu21013_attribute,
};


/**
 * bu21013_read_block_data(): read the touch co-ordinates
 * @data: bu21013_ts_data structure pointer
 * @buf: byte pointer
 *
 * Read the touch co-ordinates using i2c read block into buffer
 * and returns integer.
 */
static int bu21013_read_block_data(struct bu21013_ts_data *data, u8 *buf)
{
	int ret, i;

	for (i = 0; i < I2C_RETRY_COUNT; i++) {
		ret = i2c_smbus_read_i2c_block_data
			(data->client, BU21013_SENSORS_BTN_0_7_REG,
				LENGTH_OF_BUFFER, buf);
		if (ret == LENGTH_OF_BUFFER)
			return 0;
	}
	return -EINVAL;
}

/**
 * bu21013_do_touch_report(): Get the touch co-ordinates
 * @data: bu21013_ts_data structure pointer
 *
 * Get the touch co-ordinates from touch sensor registers and writes
 * into device structure and returns integer.
 */
static int bu21013_do_touch_report(struct bu21013_ts_data *data)
{
	u8	buf[LENGTH_OF_BUFFER];
	unsigned int pos_x[2], pos_y[2];
	bool	has_x_sensors, has_y_sensors;
	int	finger_down_count = 0;
	int	i;

	if (data == NULL)
		return -EINVAL;

	if (bu21013_read_block_data(data, buf) < 0)
		return -EINVAL;

	has_x_sensors = hweight32(buf[0] & BU21013_SENSORS_EN_0_7);
	has_y_sensors = hweight32(((buf[1] & BU21013_SENSORS_EN_8_15) |
		((buf[2] & BU21013_SENSORS_EN_16_23) << SHIFT_8)) >> SHIFT_2);
	if (!has_x_sensors || !has_y_sensors)
		return 0;

	for (i = 0; i < 2; i++) {
		const u8 *p = &buf[4 * i + 3];
		unsigned int x = p[0] << SHIFT_2 | (p[1] & MASK_BITS);
		unsigned int y = p[2] << SHIFT_2 | (p[3] & MASK_BITS);
		if (x == 0 || y == 0)
			continue;
		x = x * data->factor_x / SCALE_FACTOR;
		y = y * data->factor_y / SCALE_FACTOR;
		pos_x[finger_down_count] = x;
		pos_y[finger_down_count] = y;
		finger_down_count++;
	}

	if (finger_down_count) {
		if (finger_down_count == 2 &&
				(abs(pos_x[0] - pos_x[1]) < DELTA_MIN ||
				abs(pos_y[0] - pos_y[1]) < DELTA_MIN))
			return 0;

		for (i = 0; i < finger_down_count; i++) {
			if (data->chip->portrait && data->chip->x_flip)
				pos_x[i] = data->chip->x_max_res - pos_x[i];
			if (data->chip->portrait && data->chip->y_flip)
				pos_y[i] = data->chip->y_max_res - pos_y[i];
			input_report_abs(data->in_dev, ABS_MT_TOUCH_MAJOR,
						max(pos_x[i], pos_y[i]));
			input_report_abs(data->in_dev, ABS_MT_POSITION_X,
								pos_x[i]);
			input_report_abs(data->in_dev, ABS_MT_POSITION_Y,
								pos_y[i]);
			input_mt_sync(data->in_dev);
		}
	} else
		input_mt_sync(data->in_dev);

	input_sync(data->in_dev);

	return 0;
}
/**
 * bu21013_gpio_irq() - gpio thread function for touch interrupt
 * @irq: irq value
 * @device_data: void pointer
 *
 * This gpio thread function for touch interrupt
 * and returns irqreturn_t.
 */
static irqreturn_t bu21013_gpio_irq(int irq, void *device_data)
{
	struct bu21013_ts_data *data = device_data;
	struct i2c_client *i2c = data->client;
	int retval;

	do {
		retval = bu21013_do_touch_report(data);
		if (retval < 0) {
			dev_err(&i2c->dev, "bu21013_do_touch_report failed\n");
			return IRQ_NONE;
		}
		data->intr_pin = data->chip->irq_read_val();
		if (data->intr_pin == PEN_DOWN_INTR)
			wait_event_timeout(data->wait, data->touch_stopped,
					msecs_to_jiffies(PENUP_TIMEOUT));
	} while (!data->intr_pin && !data->touch_stopped);
	return IRQ_HANDLED;
}

/**
 * bu21013_init_chip() - power on sequence for the bu21013 controller
 * @data: device structure pointer
 * @on_ext_clk: Run on external clock
 *
 * This function is used to power on
 * the bu21013 controller and returns integer.
 */
static int bu21013_init_chip(struct bu21013_ts_data *data, bool on_ext_clk)
{
	int retval;
	struct i2c_client *i2c = data->client;

	retval = i2c_smbus_write_byte_data(i2c, BU21013_RESET_REG,
					BU21013_RESET_ENABLE);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_RESET reg write failed\n");
		return retval;
	}
	msleep(RESET_DELAY);

	retval = i2c_smbus_write_byte_data(i2c, BU21013_SENSOR_0_7_REG,
					BU21013_SENSORS_EN_0_7);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_SENSOR_0_7 reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_SENSOR_8_15_REG,
						BU21013_SENSORS_EN_8_15);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_SENSOR_8_15 reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_SENSOR_16_23_REG,
						BU21013_SENSORS_EN_16_23);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_SENSOR_16_23 reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_POS_MODE1_REG,
				(BU21013_POS_MODE1_0 | BU21013_POS_MODE1_1));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_POS_MODE1 reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_POS_MODE2_REG,
			(BU21013_POS_MODE2_ZERO | BU21013_POS_MODE2_AVG1 |
			BU21013_POS_MODE2_AVG2 | BU21013_POS_MODE2_EN_RAW |
			BU21013_POS_MODE2_MULTI));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_POS_MODE2 reg write failed\n");
		return retval;
	}
	if (on_ext_clk)
		retval = i2c_smbus_write_byte_data(i2c, BU21013_CLK_MODE_REG,
			(BU21013_CLK_MODE_EXT | BU21013_CLK_MODE_CALIB));
	else
		retval = i2c_smbus_write_byte_data(i2c, BU21013_CLK_MODE_REG,
			(BU21013_CLK_MODE_DIV | BU21013_CLK_MODE_CALIB));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_CLK_MODE reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_IDLE_REG,
				(BU21013_IDLET_0 | BU21013_IDLE_INTERMIT_EN));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_IDLE reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_INT_MODE_REG,
						BU21013_INT_MODE_LEVEL);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_INT_MODE reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_FILTER_REG,
						(BU21013_DELTA_0_6 |
							BU21013_FILTER_EN));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_FILTER reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_TH_ON_REG,
					BU21013_TH_ON_5);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_TH_ON reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_TH_OFF_REG,
				BU21013_TH_OFF_4 | BU21013_TH_OFF_3);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_TH_OFF reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_GAIN_REG,
					(BU21013_GAIN_0 | BU21013_GAIN_1));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_GAIN reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_OFFSET_MODE_REG,
					BU21013_OFFSET_MODE_DEFAULT);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_OFFSET_MODE reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_XY_EDGE_REG,
				(BU21013_X_EDGE_0 | BU21013_X_EDGE_2 |
				BU21013_Y_EDGE_1 | BU21013_Y_EDGE_3));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_XY_EDGE reg write failed\n");
		return retval;
	}
	retval = i2c_smbus_write_byte_data(i2c, BU21013_DONE_REG,
							BU21013_DONE);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_REG_DONE reg write failed\n");
		return retval;
	}

	data->factor_x = (data->chip->x_max_res * SCALE_FACTOR /
					data->chip->touch_x_max);
	data->factor_y = (data->chip->y_max_res * SCALE_FACTOR /
					data->chip->touch_y_max);
	return retval;
}

/**
 * bu21013_probe() - initialzes the i2c-client touchscreen driver
 * @client: i2c client structure pointer
 * @id: i2c device id pointer
 *
 * This function used to initializes the i2c-client touchscreen
 * driver and returns integer.
 */
static int __devinit bu21013_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	int retval;
	struct bu21013_ts_data *bu21013_data;
	struct input_dev *in_dev;
	struct bu21013_platform_device *pdata =
					client->dev.platform_data;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c smbus byte data not supported\n");
		return -EIO;
	}

	if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		retval = -EINVAL;
		return retval;
	}

	bu21013_data = kzalloc(sizeof(struct bu21013_ts_data), GFP_KERNEL);
	if (!bu21013_data) {
		dev_err(&client->dev, "device memory alloc failed\n");
		retval = -ENOMEM;
		return retval;
	}
	/* allocate input device */
	in_dev = input_allocate_device();
	if (!in_dev) {
		dev_err(&client->dev, "input device memory alloc failed\n");
		retval = -ENOMEM;
		goto err_alloc;
	}

	bu21013_data->in_dev = in_dev;
	bu21013_data->chip = pdata;
	bu21013_data->client = client;

	bu21013_data->regulator = regulator_get(&client->dev, "avdd");
	if (IS_ERR(bu21013_data->regulator)) {
		dev_warn(&client->dev, "regulator_get failed\n");
		bu21013_data->regulator = NULL;
	}
	if (bu21013_data->regulator)
		regulator_enable(bu21013_data->regulator);

	/* configure the gpio pins */
	if (pdata->cs_en) {
		retval = pdata->cs_en(pdata->cs_pin);
		if (retval < 0) {
			dev_err(&client->dev, "chip init failed\n");
			goto err_init_cs;
		}
	}

	if (pdata->has_ext_clk) {
		bu21013_data->tpclk = clk_get(&client->dev, NULL);
		if (IS_ERR(bu21013_data->tpclk)) {
			dev_warn(&client->dev, "get extern clock failed\n");
			bu21013_data->tpclk = NULL;
		}
	}

	if (pdata->enable_ext_clk && bu21013_data->tpclk) {
		retval = clk_enable(bu21013_data->tpclk);
		if (retval < 0) {
			dev_err(&client->dev, "clock enable failed\n");
			goto err_ext_clk;
		}
		bu21013_data->ext_clk_enable = true;
	}

	/* configure the touch panel controller */
	retval = bu21013_init_chip(bu21013_data, bu21013_data->ext_clk_enable);
	if (retval < 0) {
		dev_err(&client->dev, "error in bu21013 config\n");
		goto err_init_config;
	}

	init_waitqueue_head(&bu21013_data->wait);
	bu21013_data->touch_stopped = false;

	/* register the device to input subsystem */
	in_dev->name = DRIVER_TP;
	in_dev->id.bustype = BUS_I2C;
	in_dev->dev.parent = &client->dev;

	__set_bit(EV_SYN, in_dev->evbit);
	__set_bit(EV_KEY, in_dev->evbit);
	__set_bit(EV_ABS, in_dev->evbit);

	input_set_abs_params(in_dev, ABS_MT_POSITION_X, 0,
						pdata->x_max_res, 0, 0);
	input_set_abs_params(in_dev, ABS_MT_POSITION_Y, 0,
						pdata->y_max_res, 0, 0);
	input_set_abs_params(in_dev, ABS_MT_TOUCH_MAJOR, 0,
			max(pdata->x_max_res , pdata->y_max_res), 0, 0);
	input_set_drvdata(in_dev, bu21013_data);
	retval = input_register_device(in_dev);
	if (retval)
		goto err_input_register;

	retval = request_threaded_irq(pdata->irq, NULL, bu21013_gpio_irq,
					(IRQF_TRIGGER_FALLING | IRQF_SHARED),
					DRIVER_TP, bu21013_data);
	if (retval) {
		dev_err(&client->dev, "request irq %d failed\n", pdata->irq);
		goto err_init_irq;
	}
	bu21013_data->enable = true;
	i2c_set_clientdata(client, bu21013_data);

	/* sysfs implementation for dynamic enable/disable the input event */
	retval = sysfs_create_group(&client->dev.kobj,	&bu21013_attr_group);
	if (retval) {
		dev_err(&client->dev, "failed to create sysfs entries\n");
		goto err_sysfs_create;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	bu21013_data->early_suspend.level =
				EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	bu21013_data->early_suspend.suspend = bu21013_ts_early_suspend;
	bu21013_data->early_suspend.resume = bu21013_ts_late_resume;
	register_early_suspend(&bu21013_data->early_suspend);
#endif
	return retval;

err_sysfs_create:
	free_irq(pdata->irq, bu21013_data);
	i2c_set_clientdata(client, NULL);
err_init_irq:
	input_unregister_device(bu21013_data->in_dev);
err_input_register:
	wake_up(&bu21013_data->wait);
err_init_config:
	if (bu21013_data->tpclk) {
		if (bu21013_data->ext_clk_enable)
			clk_disable(bu21013_data->tpclk);
		clk_put(bu21013_data->tpclk);
	}
err_ext_clk:
	if (pdata->cs_dis)
		pdata->cs_dis(pdata->cs_pin);
err_init_cs:
	if (bu21013_data->regulator) {
		regulator_disable(bu21013_data->regulator);
		regulator_put(bu21013_data->regulator);
	}
	input_free_device(bu21013_data->in_dev);
err_alloc:
	kfree(bu21013_data);

	return retval;
}

/**
 * bu21013_remove() - removes the i2c-client touchscreen driver
 * @client: i2c client structure pointer
 *
 * This function uses to remove the i2c-client
 * touchscreen driver and returns integer.
 */
static int __devexit bu21013_remove(struct i2c_client *client)
{
	struct bu21013_ts_data *bu21013_data = i2c_get_clientdata(client);

	bu21013_data->touch_stopped = true;
	sysfs_remove_group(&client->dev.kobj, &bu21013_attr_group);
	wake_up(&bu21013_data->wait);
	free_irq(bu21013_data->chip->irq, bu21013_data);
	bu21013_data->chip->cs_dis(bu21013_data->chip->cs_pin);
	input_unregister_device(bu21013_data->in_dev);

	if (bu21013_data->tpclk) {
		if (bu21013_data->ext_clk_enable)
			clk_disable(bu21013_data->tpclk);
		clk_put(bu21013_data->tpclk);
	}
	if (bu21013_data->regulator) {
		regulator_disable(bu21013_data->regulator);
		regulator_put(bu21013_data->regulator);
	}
	kfree(bu21013_data);

	return 0;
}

#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
/**
 * bu21013_suspend() - suspend the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to suspend the
 * touch panel controller and returns integer
 */
static int bu21013_suspend(struct device *dev)
{
	struct bu21013_ts_data *bu21013_data = dev_get_drvdata(dev);

	bu21013_disable(bu21013_data);

	return 0;
}

/**
 * bu21013_resume() - resume the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to resume the touch panel
 * controller and returns integer.
 */
static int bu21013_resume(struct device *dev)
{
	struct bu21013_ts_data *bu21013_data = dev_get_drvdata(dev);

	return bu21013_enable(bu21013_data);
}

static const struct dev_pm_ops bu21013_dev_pm_ops = {
	.suspend = bu21013_suspend,
	.resume  = bu21013_resume,
};
#endif
#else
static void bu21013_ts_early_suspend(struct early_suspend *data)
{
	struct bu21013_ts_data *bu21013_data =
		container_of(data, struct bu21013_ts_data, early_suspend);
	bu21013_disable(bu21013_data);
}

static void bu21013_ts_late_resume(struct early_suspend *data)
{
	struct bu21013_ts_data *bu21013_data =
		container_of(data, struct bu21013_ts_data, early_suspend);
	struct i2c_client *client = bu21013_data->client;
	int retval;

	retval = bu21013_enable(bu21013_data);
	if (retval < 0)
		dev_err(&client->dev, "bu21013 enable failed\n");
}
#endif

static const struct i2c_device_id bu21013_id[] = {
	{ DRIVER_TP, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bu21013_id);

static struct i2c_driver bu21013_driver = {
	.driver	= {
		.name	=	DRIVER_TP,
		.owner	=	THIS_MODULE,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
		.pm	=	&bu21013_dev_pm_ops,
#endif
	},
	.probe		=	bu21013_probe,
	.remove		=	__devexit_p(bu21013_remove),
	.id_table	=	bu21013_id,
};

/**
 * bu21013_init() - initializes the bu21013 touchscreen driver
 *
 * This function used to initializes the bu21013
 * touchscreen driver and returns integer.
 */
static int __init bu21013_init(void)
{
	return i2c_add_driver(&bu21013_driver);
}

/**
 * bu21013_exit() - de-initializes the bu21013 touchscreen driver
 *
 * This function uses to de-initializes the bu21013
 * touchscreen driver and returns none.
 */
static void __exit bu21013_exit(void)
{
	i2c_del_driver(&bu21013_driver);
}

module_init(bu21013_init);
module_exit(bu21013_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Naveen Kumar G <naveen.gaddipati@stericsson.com>");
MODULE_DESCRIPTION("bu21013 touch screen controller driver");
