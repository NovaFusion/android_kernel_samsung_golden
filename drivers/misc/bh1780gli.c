/*
 * bh1780gli.c
 * ROHM Ambient Light Sensor Driver
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
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
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define BH1780_REG_CONTROL	0x80
#define BH1780_REG_PARTID	0x8A
#define BH1780_REG_MANFID	0x8B
#define BH1780_REG_DLOW	0x8C
#define BH1780_REG_DHIGH	0x8D

#define BH1780_REVMASK		(0xf)
#define BH1780_POWMASK		(0x3)
#define BH1780_POFF		(0x0)
#define BH1780_PON		(0x3)

/* power on settling time in ms */
#define BH1780_PON_DELAY	2

struct bh1780_data {
	struct i2c_client *client;
	struct regulator *regulator;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	int power_state;
	/* lock for sysfs operations */
	struct mutex lock;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bh1780_early_suspend(struct early_suspend *ddata);
static void bh1780_late_resume(struct early_suspend *ddata);
#endif

static int bh1780_write(struct bh1780_data *ddata, u8 reg, u8 val, char *msg)
{
	int ret = i2c_smbus_write_byte_data(ddata->client, reg, val);
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"i2c_smbus_write_byte_data failed error %d Register (%s)\n",
			ret, msg);
	return ret;
}

static int bh1780_read(struct bh1780_data *ddata, u8 reg, char *msg)
{
	int ret = i2c_smbus_read_byte_data(ddata->client, reg);
	if (ret < 0)
		dev_err(&ddata->client->dev,
			"i2c_smbus_read_byte_data failed error %d Register (%s)\n",
			ret, msg);
	return ret;
}

static ssize_t bh1780_show_lux(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bh1780_data *ddata = platform_get_drvdata(pdev);
	int lsb, msb;

	if (ddata->power_state == BH1780_POFF)
		return -EINVAL;

	lsb = bh1780_read(ddata, BH1780_REG_DLOW, "DLOW");
	if (lsb < 0)
		return lsb;

	msb = bh1780_read(ddata, BH1780_REG_DHIGH, "DHIGH");
	if (msb < 0)
		return msb;

	return sprintf(buf, "%d\n", (msb << 8) | lsb);
}

static ssize_t bh1780_show_power_state(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bh1780_data *ddata = platform_get_drvdata(pdev);

	/* we already maintain a sw state */
	return sprintf(buf, "%d\n", ddata->power_state);
}

static ssize_t bh1780_store_power_state(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bh1780_data *ddata = platform_get_drvdata(pdev);
	long val;
	int error;

	error = strict_strtoul(buf, 0, &val);
	if (error)
		return error;

	if (val < BH1780_POFF || val > BH1780_PON)
		return -EINVAL;

	if (ddata->power_state == val)
		return count;

	mutex_lock(&ddata->lock);

	if (ddata->power_state == BH1780_POFF)
		regulator_enable(ddata->regulator);

	error = bh1780_write(ddata, BH1780_REG_CONTROL, val, "CONTROL");
	if (error < 0) {
		mutex_unlock(&ddata->lock);
		regulator_disable(ddata->regulator);
		return error;
	}

	if (val == BH1780_POFF)
		regulator_disable(ddata->regulator);

	mdelay(BH1780_PON_DELAY);
	ddata->power_state = val;
	mutex_unlock(&ddata->lock);

	return count;
}

static DEVICE_ATTR(lux, S_IRUGO, bh1780_show_lux, NULL);

static DEVICE_ATTR(power_state, S_IWUSR | S_IRUGO,
		bh1780_show_power_state, bh1780_store_power_state);

static struct attribute *bh1780_attributes[] = {
	&dev_attr_power_state.attr,
	&dev_attr_lux.attr,
	NULL
};

static const struct attribute_group bh1780_attr_group = {
	.attrs = bh1780_attributes,
};

static int __devinit bh1780_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int ret;
	struct bh1780_data *ddata = NULL;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
		ret = -EIO;
		return ret;
	}

	ddata = kzalloc(sizeof(struct bh1780_data), GFP_KERNEL);
	if (ddata == NULL) {
		dev_err(&client->dev, "failed to alloc ddata\n");
		ret = -ENOMEM;
		return ret;
	}

	ddata->client = client;
	i2c_set_clientdata(client, ddata);

	ddata->regulator = regulator_get(&client->dev, "vcc");
	if (IS_ERR(ddata->regulator)) {
		dev_err(&client->dev, "failed to get regulator\n");
		ret = PTR_ERR(ddata->regulator);
		goto free_ddata;
	}

	regulator_enable(ddata->regulator);

	ret = bh1780_read(ddata, BH1780_REG_PARTID, "PART ID");
	if (ret < 0) {
		dev_err(&client->dev, "failed to read part ID\n");
		goto disable_regulator;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	ddata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ddata->early_suspend.suspend = bh1780_early_suspend;
	ddata->early_suspend.resume = bh1780_late_resume;
	register_early_suspend(&ddata->early_suspend);
#endif

	regulator_disable(ddata->regulator);
	ddata->power_state = BH1780_POFF;

	dev_info(&client->dev, "Ambient Light Sensor, Rev : %d\n",
			(ret & BH1780_REVMASK));

	mutex_init(&ddata->lock);

	ret = sysfs_create_group(&client->dev.kobj, &bh1780_attr_group);
	if (ret) {
		dev_err(&client->dev, "failed to create sysfs group\n");
		goto put_regulator;
	}

	return 0;
disable_regulator:
	regulator_disable(ddata->regulator);
put_regulator:
	regulator_put(ddata->regulator);
free_ddata:
	kfree(ddata);
	return ret;
}

static int __devexit bh1780_remove(struct i2c_client *client)
{
	struct bh1780_data *ddata;

	ddata = i2c_get_clientdata(client);
	sysfs_remove_group(&client->dev.kobj, &bh1780_attr_group);
	kfree(ddata);

	return 0;
}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_PM)
static int bh1780_do_suspend(struct bh1780_data *ddata)
{
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->power_state == BH1780_POFF)
		goto unlock;

	ret = bh1780_write(ddata, BH1780_REG_CONTROL, BH1780_POFF, "CONTROL");

	if (ret < 0)
		goto unlock;

	if (ddata->regulator)
		regulator_disable(ddata->regulator);
unlock:
	mutex_unlock(&ddata->lock);
	return ret;
}

static int bh1780_do_resume(struct bh1780_data *ddata)
{
	int ret = 0;

	mutex_lock(&ddata->lock);

	if (ddata->power_state == BH1780_POFF)
		goto unlock;

	if (ddata->regulator)
		regulator_enable(ddata->regulator);

	ret = bh1780_write(ddata, BH1780_REG_CONTROL,
					ddata->power_state, "CONTROL");

unlock:
	mutex_unlock(&ddata->lock);
	return ret;
}
#endif

#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
static int bh1780_suspend(struct device *dev)
{
	struct bh1780_data *ddata = dev_get_drvdata(dev);
	int ret = 0;

	ret = bh1780_do_suspend(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while suspending the device\n");

	return ret;
}

static int bh1780_resume(struct device *dev)
{
	struct bh1780_data *ddata = dev_get_drvdata(dev);
	int ret = 0;

	ret = bh1780_do_resume(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while resuming the device\n");

	return ret;
}

static SIMPLE_DEV_PM_OPS(bh1780_pm, bh1780_suspend, bh1780_resume);
#define BH1780_PMOPS (&bh1780_pm)
#endif /* CONFIG_PM */
#else
#define BH1780_PMOPS NULL
static void bh1780_early_suspend(struct early_suspend *data)
{
	struct bh1780_data *ddata =
		container_of(data, struct bh1780_data, early_suspend);
	int ret;

	ret = bh1780_do_suspend(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while suspending the device\n");
}

static void bh1780_late_resume(struct early_suspend *data)
{
	struct bh1780_data *ddata =
		container_of(data, struct bh1780_data, early_suspend);
	int ret;

	ret = bh1780_do_resume(ddata);
	if (ret < 0)
		dev_err(&ddata->client->dev,
				"Error while resuming the device\n");
}
#endif /*!CONFIG_HAS_EARLYSUSPEND */

static const struct i2c_device_id bh1780_id[] = {
	{ "bh1780", 0 },
	{ },
};

static struct i2c_driver bh1780_driver = {
	.probe		= bh1780_probe,
	.remove		= bh1780_remove,
	.id_table	= bh1780_id,
	.driver = {
		.name = "bh1780",
#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
		.pm	= BH1780_PMOPS,
#endif
	},
};

static int __init bh1780_init(void)
{
	return i2c_add_driver(&bh1780_driver);
}

static void __exit bh1780_exit(void)
{
	i2c_del_driver(&bh1780_driver);
}

module_init(bh1780_init)
module_exit(bh1780_exit)

MODULE_DESCRIPTION("BH1780GLI Ambient Light Sensor Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hemanth V <hemanthv@ti.com>");
