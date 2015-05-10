#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/input.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/regulator/consumer.h>

#define STOP_INPUT_EVENT	0
#define SENSOR_NAME			    "bma254"
#define I2C_RETRIES			    5

#define BMA254_CHIP_ID			0xFA
#define SOFT_RESET              0xB6

/*
 *      register definitions
 */
#define BMA254_CHIP_ID_REG                      0x00
#define BMA254_X_AXIS_LSB_REG                   0x02
#define BMA254_X_AXIS_MSB_REG                   0x03
#define BMA254_Y_AXIS_LSB_REG                   0x04
#define BMA254_Y_AXIS_MSB_REG                   0x05
#define BMA254_Z_AXIS_LSB_REG                   0x06
#define BMA254_Z_AXIS_MSB_REG                   0x07
#define BMA254_RANGE_SEL_REG                    0x0F
#define BMA254_BW_SEL_REG                       0x10
#define BMA254_MODE_CTRL_REG                    0x11
#define BMA254_RESET_REG                        0x14
#define BMA254_THETA_BLOCK_REG                  0x2D
#define BMA254_THETA_FLAT_REG                   0x2E
#define BMA254_FLAT_HOLD_TIME_REG               0x2F
#define BMA254_SELF_TEST_REG                    0x32
#define BMA254_OFFSET_CTRL_REG                  0x36
#define BMA254_OFFSET_PARAMS_REG                0x37

#define BMA254_ACC_X_LSB__POS           4
#define BMA254_ACC_X_LSB__LEN           4
#define BMA254_ACC_X_LSB__MSK           0xF0
#define BMA254_ACC_X_LSB__REG           BMA254_X_AXIS_LSB_REG

#define BMA254_ACC_X_MSB__POS           0
#define BMA254_ACC_X_MSB__LEN           8
#define BMA254_ACC_X_MSB__MSK           0xFF
#define BMA254_ACC_X_MSB__REG           BMA254_X_AXIS_MSB_REG

#define BMA254_ACC_Y_LSB__POS           4
#define BMA254_ACC_Y_LSB__LEN           4
#define BMA254_ACC_Y_LSB__MSK           0xF0
#define BMA254_ACC_Y_LSB__REG           BMA254_Y_AXIS_LSB_REG

#define BMA254_ACC_Y_MSB__POS           0
#define BMA254_ACC_Y_MSB__LEN           8
#define BMA254_ACC_Y_MSB__MSK           0xFF
#define BMA254_ACC_Y_MSB__REG           BMA254_Y_AXIS_MSB_REG

#define BMA254_ACC_Z_LSB__POS           4
#define BMA254_ACC_Z_LSB__LEN           4
#define BMA254_ACC_Z_LSB__MSK           0xF0
#define BMA254_ACC_Z_LSB__REG           BMA254_Z_AXIS_LSB_REG

#define BMA254_ACC_Z_MSB__POS           0
#define BMA254_ACC_Z_MSB__LEN           8
#define BMA254_ACC_Z_MSB__MSK           0xFF
#define BMA254_ACC_Z_MSB__REG           BMA254_Z_AXIS_MSB_REG

#define BMA254_RANGE_SEL__POS           0
#define BMA254_RANGE_SEL__LEN           4
#define BMA254_RANGE_SEL__MSK           0x0F
#define BMA254_RANGE_SEL__REG           BMA254_RANGE_SEL_REG

#define BMA254_BANDWIDTH__POS           0
#define BMA254_BANDWIDTH__LEN           5
#define BMA254_BANDWIDTH__MSK           0x1F
#define BMA254_BANDWIDTH__REG           BMA254_BW_SEL_REG

#define BMA254_EN_LOW_POWER__POS        6
#define BMA254_EN_LOW_POWER__LEN        1
#define BMA254_EN_LOW_POWER__MSK        0x40
#define BMA254_EN_LOW_POWER__REG        BMA254_MODE_CTRL_REG

#define BMA254_EN_SUSPEND__POS          7
#define BMA254_EN_SUSPEND__LEN          1
#define BMA254_EN_SUSPEND__MSK          0x80
#define BMA254_EN_SUSPEND__REG          BMA254_MODE_CTRL_REG

#define BMA254_GET_BITSLICE(regvar, bitname)\
	((regvar & bitname##__MSK) >> bitname##__POS)

#define BMA254_SET_BITSLICE(regvar, bitname, val)\
	((regvar & ~bitname##__MSK) | ((val<<bitname##__POS)&bitname##__MSK))

/* range registers */
#define BMA254_RANGE_2G                 3
#define BMA254_RANGE_4G                 5
#define BMA254_RANGE_8G                 8
#define BMA254_RANGE_16G               12

/* bandwidth registers */
#define BMA254_BW_7DOT81HZ          0x08
#define BMA254_BW_15DOT63HZ         0x09
#define BMA254_BW_31DOT25HZ         0x0A
#define BMA254_BW_62DOT50HZ         0x0B
#define BMA254_BW_125HZ         	0x0C
#define BMA254_BW_250HZ         	0x0D
#define BMA254_BW_500HZ         	0x0E
#define BMA254_BW_1000HZ        	0x0F

#define BMA254_RANGE_SET		BMA254_RANGE_2G
#define BMA254_BW_SET			BMA254_BW_15DOT63HZ

/* mode settings */
#define BMA254_MODE_NORMAL          0
#define BMA254_MODE_LOWPOWER        1
#define BMA254_MODE_SUSPEND         2

#define BMA254_EN_SELF_TEST__POS                0
#define BMA254_EN_SELF_TEST__LEN                2
#define BMA254_EN_SELF_TEST__MSK                0x03
#define BMA254_EN_SELF_TEST__REG                BMA254_SELF_TEST_REG

#define BMA254_NEG_SELF_TEST__POS               2
#define BMA254_NEG_SELF_TEST__LEN               1
#define BMA254_NEG_SELF_TEST__MSK               0x04
#define BMA254_NEG_SELF_TEST__REG               BMA254_SELF_TEST_REG

#define BMA254_EN_FAST_COMP__POS                5
#define BMA254_EN_FAST_COMP__LEN                2
#define BMA254_EN_FAST_COMP__MSK                0x60
#define BMA254_EN_FAST_COMP__REG                BMA254_OFFSET_CTRL_REG

#define BMA254_FAST_COMP_RDY_S__POS             4
#define BMA254_FAST_COMP_RDY_S__LEN             1
#define BMA254_FAST_COMP_RDY_S__MSK             0x10
#define BMA254_FAST_COMP_RDY_S__REG             BMA254_OFFSET_CTRL_REG

#define BMA254_COMP_TARGET_OFFSET_X__POS        1
#define BMA254_COMP_TARGET_OFFSET_X__LEN        2
#define BMA254_COMP_TARGET_OFFSET_X__MSK        0x06
#define BMA254_COMP_TARGET_OFFSET_X__REG        BMA254_OFFSET_PARAMS_REG

#define BMA254_COMP_TARGET_OFFSET_Y__POS        3
#define BMA254_COMP_TARGET_OFFSET_Y__LEN        2
#define BMA254_COMP_TARGET_OFFSET_Y__MSK        0x18
#define BMA254_COMP_TARGET_OFFSET_Y__REG        BMA254_OFFSET_PARAMS_REG

#define BMA254_COMP_TARGET_OFFSET_Z__POS        5
#define BMA254_COMP_TARGET_OFFSET_Z__LEN        2
#define BMA254_COMP_TARGET_OFFSET_Z__MSK        0x60
#define BMA254_COMP_TARGET_OFFSET_Z__REG        BMA254_OFFSET_PARAMS_REG

#define CALIBRATION_FILE_PATH	"/efs/calibration_data"
#define CALIBRATION_DATA_AMOUNT	100

#define WHO_AM_I		0x00

struct acc_data {
	int x;
	int y;
	int z;
};

struct bma254_power_data {
	struct regulator *regulator_vdd;
	struct regulator *regulator_vio;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend bma254_early_suspend_handler;
#endif

static void bma254_early_suspend(struct early_suspend *handler);
static void bma254_early_resume(struct early_suspend *handler);
int accsns_get_acceleration_data(int *xyz);
int accsns_activate(int flgatm, int flg, int dtime);
extern int sensors_register(struct device *dev, void * drvdata,
                    		struct device_attribute *attributes[], char *name);

struct acc_data caldata;
struct i2c_client *this_client;
static struct bma254_power_data bma254_power;

static struct i2c_driver bma254_driver;

static atomic_t flgEna;
static atomic_t delay;

static int bma254_i2c_writem(u8 *txData, int length)
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

static int bma254_i2c_readm(u8 *rxData, int length)
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

static int bma254_power_on(void)
{
	int err = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (bma254_power.regulator_vdd) {
		err = regulator_enable(bma254_power.regulator_vdd);
		if (err) {
			pr_err("%s: Couldn't enable VDD %d\n", __func__, err);
			return err;
		}
	}

	if (bma254_power.regulator_vio) {
		err = regulator_enable(bma254_power.regulator_vio);
		if (err) {
			pr_err("%s: Couldn't enable VIO %d\n", __func__, err);
			return err;
		}
	}

	msleep(20);
	return err;
}

static int bma254_power_off(void)
{
	int err = 0;

	printk(KERN_INFO "%s\n", __func__);

	if (bma254_power.regulator_vdd) {
		err = regulator_disable(bma254_power.regulator_vdd);
		if (err) {
			pr_err("%s: Couldn't disable VDD %d\n", __func__, err);
			return err;
		}
	}

	if (bma254_power.regulator_vio) {
		err = regulator_disable(bma254_power.regulator_vio);
		if (err) {
			pr_err("%s: Couldn't disable VIO %d\n", __func__, err);
			return err;
		}
	}

	return err;
}

static int bma254_smbus_read_byte_block(unsigned char reg_addr, 
										unsigned char *data, 
										unsigned char len)
{
	s32 dummy;
	dummy = i2c_smbus_read_i2c_block_data(this_client, reg_addr, len, data);
	if (dummy < 0)
		return -1;

	return 0;
}

int accsns_get_acceleration_data(int *xyz)
{
	u8 buf[6];
	int err;

	if (this_client == NULL) {
		xyz[0] = xyz[1] = xyz[2] = 0;
		return -ENODEV;
	}

	err = bma254_smbus_read_byte_block(BMA254_ACC_X_LSB__REG, buf, 6);

	xyz[0] = BMA254_GET_BITSLICE(buf[0], BMA254_ACC_X_LSB)
		|(BMA254_GET_BITSLICE(buf[1], BMA254_ACC_X_MSB)
				<< BMA254_ACC_X_LSB__LEN);
	xyz[0] = xyz[0] << (sizeof(int) * 8 - (BMA254_ACC_X_LSB__LEN
				+ BMA254_ACC_X_MSB__LEN));
	xyz[0] = xyz[0] >> (sizeof(int) * 8 - (BMA254_ACC_X_LSB__LEN
				+ BMA254_ACC_X_MSB__LEN));

	xyz[1] = BMA254_GET_BITSLICE(buf[2], BMA254_ACC_Y_LSB)
		| (BMA254_GET_BITSLICE(buf[3], BMA254_ACC_Y_MSB)
				<<BMA254_ACC_Y_LSB__LEN);
	xyz[1] = xyz[1] << (sizeof(int) * 8 - (BMA254_ACC_Y_LSB__LEN
				+ BMA254_ACC_Y_MSB__LEN));
	xyz[1] = xyz[1] >> (sizeof(int) * 8 - (BMA254_ACC_Y_LSB__LEN
				+ BMA254_ACC_Y_MSB__LEN));

	xyz[2] = BMA254_GET_BITSLICE(buf[4], BMA254_ACC_Z_LSB)
		| (BMA254_GET_BITSLICE(buf[5], BMA254_ACC_Z_MSB)
				<<BMA254_ACC_Z_LSB__LEN);
	xyz[2] = xyz[2] << (sizeof(int) * 8 - (BMA254_ACC_Z_LSB__LEN
				+ BMA254_ACC_Z_MSB__LEN));
	xyz[2] = xyz[2] >> (sizeof(int) * 8 - (BMA254_ACC_Z_LSB__LEN
				+ BMA254_ACC_Z_MSB__LEN));

	xyz[0] -= caldata.x;
	xyz[1] -= caldata.y;
	xyz[2] -= caldata.z;

	return err;
}

int bma254_get_acceleration_rawdata(int *xyz)
{
	u8 buf[6];
	int err;

	err = bma254_smbus_read_byte_block(BMA254_ACC_X_LSB__REG, buf, 6);

	xyz[0] = BMA254_GET_BITSLICE(buf[0], BMA254_ACC_X_LSB)
		|(BMA254_GET_BITSLICE(buf[1], BMA254_ACC_X_MSB)
				<< BMA254_ACC_X_LSB__LEN);
	xyz[0] = xyz[0] << (sizeof(int) * 8 - (BMA254_ACC_X_LSB__LEN
				+ BMA254_ACC_X_MSB__LEN));
	xyz[0] = xyz[0] >> (sizeof(int) * 8 - (BMA254_ACC_X_LSB__LEN
				+ BMA254_ACC_X_MSB__LEN));

	xyz[1] = BMA254_GET_BITSLICE(buf[2], BMA254_ACC_Y_LSB)
		| (BMA254_GET_BITSLICE(buf[3], BMA254_ACC_Y_MSB)
				<<BMA254_ACC_Y_LSB__LEN);
	xyz[1] = xyz[1] << (sizeof(int) * 8 - (BMA254_ACC_Y_LSB__LEN
				+ BMA254_ACC_Y_MSB__LEN));
	xyz[1] = xyz[1] >> (sizeof(int) * 8 - (BMA254_ACC_Y_LSB__LEN
				+ BMA254_ACC_Y_MSB__LEN));

	xyz[2] = BMA254_GET_BITSLICE(buf[4], BMA254_ACC_Z_LSB)
		| (BMA254_GET_BITSLICE(buf[5], BMA254_ACC_Z_MSB)
				<<BMA254_ACC_Z_LSB__LEN);
	xyz[2] = xyz[2] << (sizeof(int) * 8 - (BMA254_ACC_Z_LSB__LEN
				+ BMA254_ACC_Z_MSB__LEN));
	xyz[2] = xyz[2] >> (sizeof(int) * 8 - (BMA254_ACC_Z_LSB__LEN
				+ BMA254_ACC_Z_MSB__LEN));

	return err;
}

int accsns_activate(int flgatm, int flg, int dtime)
{
	u8 buf[2];

	int reg = 0;

	if (flg != 0) {
		buf[0] = BMA254_RESET_REG;
		buf[1] = SOFT_RESET;
		bma254_i2c_writem(buf, 2);
		msleep(20);
	}

	buf[0] = BMA254_RANGE_SEL_REG;
	buf[1] = 0x03;		/*g-range +/-2g*/
	bma254_i2c_writem(buf, 2);

	buf[0] = BMA254_BW_SEL_REG;
	buf[1] = BMA254_BW_15DOT63HZ;
	bma254_i2c_writem(buf, 2);

	bma254_i2c_readm(buf, 1);
	reg = (int)((s8)buf[0]);

	if (flg == 0) {
		buf[1] = 0x80; /*sleep*/
	} else {
		buf[1] = 0x00;
	}
	bma254_i2c_writem(buf, 2);

	if (flgatm) {
		atomic_set(&flgEna, flg);
		atomic_set(&delay, dtime);
	}
	return 0;
}
EXPORT_SYMBOL(accsns_activate);

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
			err = bma254_get_acceleration_rawdata(data);
			if (err < 0) {
				pr_err("%s: accel_read_accel_raw_xyz() "
						"failed in the %dth loop\n",
						__func__, cnt);
				return err;
			}

			sum[0] += data[0];
			sum[1] += data[1];
			sum[2] += (data[2] - 1024);
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

	err = accel_do_calibrate((int)enable);
	if (err < 0)
		pr_err("%s: accel_do_calibrate() failed\n", __func__);

	if (!enable) {
		caldata.x = 0;
		caldata.y = 0;
		caldata.z = 0;
	}

	return size;
}

static ssize_t raw_data_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int xyz[3] = {0, };

	accsns_get_acceleration_data(xyz);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
			-(xyz[1] >> 2), (xyz[0] >> 2), (xyz[2] >> 2));

}

static ssize_t raw_data_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	pr_info("raw_data_write is work");
	return size;
}

static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR | S_IWGRP,
	raw_data_read, raw_data_write);
static DEVICE_ATTR(calibration, S_IRUGO | S_IWUSR | S_IWGRP,
	accel_calibration_show, accel_calibration_store);

static struct device_attribute *bma254_attrs[] = {
	&dev_attr_raw_data,
	&dev_attr_calibration,
	NULL,
};

static int bma254_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *bma_device = NULL;

	this_client = client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->adapter->dev, "client not i2c capable\n");
		ret = -ENOMEM;
		goto exit;
	}

	/* regulator output enable/disable control */
	bma254_power.regulator_vdd = bma254_power.regulator_vio = NULL;
	bma254_power.regulator_vdd = regulator_get(&client->dev, "vdd-acc");
	if (IS_ERR(bma254_power.regulator_vdd)) {
		ret = PTR_ERR(bma254_power.regulator_vdd);
		pr_err("%s: failed to get accsns_i2c_vdd %d\n", __func__, ret);
		goto err_setup_regulator;
	}

	bma254_power.regulator_vio = regulator_get(&client->dev, "vio-acc");
	if (IS_ERR(bma254_power.regulator_vio)) {
		ret = PTR_ERR(bma254_power.regulator_vio);
		pr_err("%s: failed to get accsns_i2c_vio %d\n", __func__, ret);
		goto err_setup_regulator;
	}

	bma254_power_on();

	/* read chip id */
	ret = i2c_smbus_read_byte_data(this_client, WHO_AM_I);
	pr_info("%s : device ID = 0x%x, reading ID = 0x%x\n", __func__,
		BMA254_CHIP_ID, ret);
	if (ret == BMA254_CHIP_ID) /* Normal Operation */
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

	sensors_register(bma_device, NULL, bma254_attrs,
		"accelerometer_sensor");

	atomic_set(&flgEna, 0);
	atomic_set(&delay, 100);

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&bma254_early_suspend_handler);
#endif

	pr_info("%s: success.\n", __func__);
	return 0;

err_setup_regulator:
	if (bma254_power.regulator_vdd) {
		regulator_disable(bma254_power.regulator_vdd);
		regulator_put(bma254_power.regulator_vdd);
	}
	if (bma254_power.regulator_vio) {
		regulator_disable(bma254_power.regulator_vio);
		regulator_put(bma254_power.regulator_vio);
	}
exit:
	this_client = NULL;
	pr_err("%s: failed!\n", __func__);
	return ret;
}

static int bma254_suspend(struct i2c_client *client, pm_message_t mesg)
{
	if (atomic_read(&flgEna))
		accsns_activate(0, 0, atomic_read(&delay));

	return 0;
}

static int bma254_resume(struct i2c_client *client)
{
	if (atomic_read(&flgEna))
		accsns_activate(0, 1, atomic_read(&delay));

	return 0;
}

static int __devexit bma254_remove(struct i2c_client *client)
{
    pr_info("%s\n", __func__);
    accsns_activate(0, 0, atomic_read(&delay));
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&bma254_early_suspend_handler);
#endif

	if (bma254_power.regulator_vdd) {
		regulator_disable(bma254_power.regulator_vdd);
		regulator_put(bma254_power.regulator_vdd);
	}
	if (bma254_power.regulator_vio) {
		regulator_disable(bma254_power.regulator_vio);
		regulator_put(bma254_power.regulator_vio);
	}

    this_client = NULL;

    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma254_early_suspend(struct early_suspend *handler)
{
	pr_info("%s\n", __func__);

	bma254_suspend(this_client, PMSG_SUSPEND);
}

static void bma254_early_resume(struct early_suspend *handler)
{
	pr_info("%s\n", __func__);

	bma254_resume(this_client);
}
#endif

static const struct i2c_device_id bma254_id[] = {
	{ "bma254", 0 },
	{ }
};

static struct i2c_driver bma254_driver = {
	.probe     = bma254_probe,
	.remove   = bma254_remove,
	.id_table  = bma254_id,
	.driver    = {
		.name	= "bma254",
	},
	.suspend = bma254_suspend,
	.resume = bma254_resume	
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend bma254_early_suspend_handler = {
    .suspend = bma254_early_suspend,
    .resume  = bma254_early_resume,
};
#endif

static int __init bma254_init(void)
{
	return i2c_add_driver(&bma254_driver);
}

static void __exit bma254_exit(void)
{
	i2c_del_driver(&bma254_driver);
}

module_init(bma254_init);
module_exit(bma254_exit);

MODULE_DESCRIPTION("Bosch BMA254 Accelerometer Sensor");
MODULE_AUTHOR("Samsung");
MODULE_LICENSE("GPL v2");
