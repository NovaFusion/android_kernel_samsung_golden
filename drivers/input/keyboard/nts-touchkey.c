/*
 * NETXTCHIP NTS series touchkey driver
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Author: Taeyoon Yoon <tyoony.yoon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/input/nts-touchkey.h>

#define NTS_KEY_DATA		0x40
#define NTS_VER			0x41
#define NTS_PCB_VER		0x42
#define NTS_SENSITIVITY		0x45
#define NTS_CMD			0x47
#define NTS_THR_BASE		0x4A
#define NTS_THR_STEP		1
#define NTS_RAW_DATA_BASE	0x50
#define NTS_RAW_DATA_STEP	2
#define NTS_BASELINE_BASE	0x56
#define NTS_BASELINE_STEP	2
#define NTS_DIFFERENCE_BASE	0x60
#define NTS_DIFFERENCE_STEP	2

#define NTS_KEY_INDEX_MASK	0x03
#define NTS_KEY_PRESS_MASK	0x08

#define NTS_POWERON_DELAY	100
#define NTS_INTER_I2C_DELAY	100

#define NTS_EEPROM_SLAVE_ADDR	0X70
#define	NTS_EEPROM_PAGE_SIZE	32

/* register map of specical function register */
#define NTS_SFR_BASE		0X4000
#define	NTS_SFR_PLLPROT		0x87
#define	NTS_SFR_MCLKSEL		0x8B
#define	NTS_SFR_EEPPROT		0x98
#define	NTS_SFR_PAG0		0x99
#define NTS_SFR_PAG1		0x9A
#define	NTS_SFR_CFG		0x9B
#define	NTS_SFR_MODE		0x9C
#define	NTS_SFR_CMD		0x9D

/*offset of special function register */
#define	NTS_SFR_WT_EN		0x80

/*the command register offsets in special function register */
#define	NTS_SFR_CMD_WT_EN	0x08
#define	NTS_SFR_CMD_PDOWN	0x00
#define	NTS_SFR_CMD_STNBY	0x01
#define	NTS_SFR_CMD_READ	0x02
#define	NTS_SFR_CMD_LOAD1	0x03
#define	NTS_SFR_CMD_LOAD2	0x04
#define	NTS_SFR_CMD_ERPRG	0x05

enum {
	NORMAL_MODE = 0,
	TEST_MODE,
};

struct nts_touchkey_data {
	struct i2c_client			*client;
	struct input_dev			*input_dev;
	char					phys[32];
	int					irq;
	struct nts_touchkey_platform_data	*pdata;
	struct early_suspend			early_suspend;
	struct mutex				lock;
	bool					enabled;
	int					num_key;
	int					*keycodes;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void nts_touchkey_early_suspend(struct early_suspend *h);
static void nts_touchkey_late_resume(struct early_suspend *h);
#endif

static irqreturn_t nts_touchkey_interrupt(int irq, void *dev_id)
{
	struct nts_touchkey_data *data = dev_id;
	struct i2c_client *client = data->client;
	u8 buf;
	u8 key_index;
	bool press;

	buf = i2c_smbus_read_byte_data(client, NTS_KEY_DATA);

	key_index = buf & NTS_KEY_INDEX_MASK;

	switch (key_index) {
	case 0:
		dev_err(&client->dev, "no button press interrupt(0x%2X)\n",
			buf);
		break;
	case 1 ... 2:
		press = !(buf & NTS_KEY_PRESS_MASK);
		dev_err(&client->dev, "key[%3d] is %s\n",
			data->keycodes[key_index - 1],
			(press) ? "pressed" : "releaseed");
		input_report_key(data->input_dev,
				data->keycodes[key_index - 1], press);
		input_sync(data->input_dev);
		break;
	default:
		dev_err(&client->dev, "wrong interrupt(0x%2X)\n", buf);
		break;
	}

	return IRQ_HANDLED;
}

int read_eeprom_byte(struct nts_touchkey_data *data, u16 addr, u8 *val)
{
	struct i2c_client *client = data->client;
	unsigned short addr_buf;
	u8 buf[] = {(addr >> 8) & 0xff, addr & 0xff};
	int ret;

	addr_buf = client->addr;
	client->addr = NTS_EEPROM_SLAVE_ADDR;

	dev_err(&client->dev, "arr size %d\n", ARRAY_SIZE(buf));
	
	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret != ARRAY_SIZE(buf)) {
		dev_err(&client->dev,
			"failed to read eeprom 0x%X (%d)\n", addr, ret);
		ret = -EIO;
		goto out;
	} 
	
	ret = i2c_master_recv(client, val, 1);
	if (ret != 1) {
		dev_err(&client->dev,
			"failed to read eeprom 0x%X (%d)\n", addr, ret);
		ret = -EIO;
		goto out;
	}

	ret = 0;

out:
	client->addr = addr_buf;
	return ret;
}

int write_eeprom_byte(struct nts_touchkey_data *data, u16 addr, u8 val)
{
	struct i2c_client *client = data->client;
	u8 buf[] = {(addr >> 8) & 0xff, addr & 0xff, val};
	unsigned short addr_buf;
	int ret;

	addr_buf = client->addr;
	client->addr = NTS_EEPROM_SLAVE_ADDR;

	dev_err(&client->dev, "arr size %d\n", ARRAY_SIZE(buf));

	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret != ARRAY_SIZE(buf)) {
		dev_err(&client->dev,
			"failed to write eeprom 0x%X (%d)\n", addr, ret);
		ret = -EIO;
		goto out;
	} 

	ret = 0;
out:
	client->addr = addr_buf;
	return ret;
}

static int __devinit nts_touchkey_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct nts_touchkey_data *data;
	struct input_dev *input_dev;
	int i;
	int ret;
	u8 buf;

	if (client->dev.platform_data) {
		struct nts_touchkey_platform_data *pdata = 
						client->dev.platform_data;
		if (!pdata->enable) {
			dev_info(&client->dev, "terminate driver\n");
			return -EINVAL;
		}
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;

	data = kzalloc(sizeof(struct nts_touchkey_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_data_alloc;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto err_input_devalloc;
	}

	data->client = client;
	data->pdata = client->dev.platform_data;
	data->input_dev = input_dev;

	i2c_set_clientdata(client, data);

	data->num_key = data->pdata->num_key;
	dev_info(&client->dev, "number of keys= %d\n", data->num_key);

	data->keycodes = data->pdata->keycodes;
	for (i = 0; i < data->num_key; i++)
		dev_info(&client->dev, "keycode[%d]= %3d\n", i, data->keycodes[i]);

	snprintf(data->phys, sizeof(data->phys), "%s/input0",
		dev_name(&client->dev));
	input_dev->name = "sec_touchkey";
	input_dev->phys = data->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->keycode = data->keycodes;
	input_dev->keycodesize = sizeof(data->keycodes[0]);
	input_dev->keycodemax = data->num_key;

	set_bit(EV_ABS, input_dev->evbit);
	for (i = 0; i < data->num_key; i++) {
		input_set_capability(input_dev, EV_KEY, data->keycodes[i]);
		set_bit(data->keycodes[i], input_dev->keybit);
	}

	i2c_set_clientdata(client, data);
	input_set_drvdata(input_dev, data);

	ret = input_register_device(data->input_dev);
	if (ret) {
		dev_err(&client->dev, "fail to register input_dev (%d).\n",
			ret);
		goto err_register_input_dev;
	}

	mutex_init(&data->lock);

	data->irq = client->irq;

	data->pdata->power(true);
	msleep(NTS_POWERON_DELAY);

/*	flash_firmware(data);	*/
	
	buf = i2c_smbus_read_byte_data(client, NTS_VER);
	dev_info(&client->dev, "chip ver: 0x%2X\n", buf);

	buf = i2c_smbus_read_byte_data(client, NTS_PCB_VER);
	dev_info(&client->dev, "pcb ver: 0x%2X\n", buf);

	if (data->irq) {
		ret = request_threaded_irq(data->irq, NULL,
					nts_touchkey_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					NTS_TOUCHKEY_DEVICE, data);
		if (ret) {
			dev_err(&client->dev, "fail to request irq (%d).\n",
				data->irq);
			goto err_request_irq;
		}
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = nts_touchkey_early_suspend;
	data->early_suspend.resume = nts_touchkey_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	data->enabled = true;

	dev_info(&client->dev, "successfully probed.\n");

	return 0;

err_request_irq:
	input_unregister_device(input_dev);
err_register_input_dev:
	input_free_device(input_dev);
err_input_devalloc:
	kfree(data);
err_data_alloc:
	return ret;
}

static int __devexit nts_touchkey_remove(struct i2c_client *client)
{
	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
static int nts_touchkey_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nts_touchkey_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->lock);
	if (!data->enabled) {
		dev_info(&client->dev, "%s, already disabled.\n", __func__);
		goto out;	
	}

	data->enabled = false;

	disable_irq(data->irq);
/*	data->pdata->power(false); */

	for (i = 0; i < data->num_key; i++)
		input_report_key(data->input_dev,
				data->keycodes[i], 0);

	input_sync(data->input_dev);

out:
	mutex_unlock(&data->lock);
	dev_info(&client->dev, "%s is finished.\n", __func__);
	return 0;
}

static int nts_touchkey_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nts_touchkey_data *data = i2c_get_clientdata(client);
	int i;
	int dbg_val[2];

	mutex_lock(&data->lock);
	if (data->enabled) {
		dev_info(&client->dev, "%s, already enabled.\n", __func__);
		goto out;	
	}
	dbg_val[0] = gpio_get_value(data->pdata->gpio_int);

	data->pdata->int_set_pull(true);

/*	data->pdata->power(true);
	msleep(NTS_POWERON_DELAY);
*/
	dbg_val[1] = gpio_get_value(data->pdata->gpio_int);

	enable_irq(data->irq);

	data->enabled = true;
out:
	mutex_unlock(&data->lock);
	dev_info(&client->dev, "%s is finished(%d,%d).\n", __func__,
		dbg_val[0], dbg_val[1]);
	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void nts_touchkey_early_suspend(struct early_suspend *h)
{
	struct nts_touchkey_data *data;
	data = container_of(h, struct nts_touchkey_data, early_suspend);
	nts_touchkey_suspend(&data->client->dev);
}

static void nts_touchkey_late_resume(struct early_suspend *h)
{
	struct nts_touchkey_data *data;
	data = container_of(h, struct nts_touchkey_data, early_suspend);
	nts_touchkey_resume(&data->client->dev);
}
#endif

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops nts_touchkey_pm_ops = {
	.suspend	= nts_touchkey_suspend,
	.resume		= nts_touchkey_resume,
};
#endif

static const struct i2c_device_id nts_touchkey_id[] = {
	{ NTS_TOUCHKEY_DEVICE, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nts_touchkey_id);

static struct i2c_driver nts_touchkey_driver = {
	.probe		= nts_touchkey_probe,
	.remove		= __devexit_p(nts_touchkey_remove),
	.driver = {
		.name	= NTS_TOUCHKEY_DEVICE,
		.owner	= THIS_MODULE,
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm	= &nts_touchkey_pm_ops,
#endif
	},
	.id_table	= nts_touchkey_id,
};

static int __devinit nts_touchkey_init(void)
{
	return i2c_add_driver(&nts_touchkey_driver);
}

static void __exit nts_touchkey_exit(void)
{
	i2c_del_driver(&nts_touchkey_driver);
}

module_init(nts_touchkey_init);
module_exit(nts_touchkey_exit);

MODULE_DESCRIPTION("NEXTCHIP NTS series touchkey driver");
MODULE_LICENSE("GPL");
