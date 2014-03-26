/*
 * cypress_touchkey.c - Platform data for cypress touchkey driver
 *
 * Copyright (C) 2011 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* #define TOUCHKEY_UPDATE_ONBOOT */
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <asm/gpio.h>
#include <linux/miscdevice.h>
/* #include <asm/uaccess.h> */
#include <linux/earlysuspend.h>
#include <linux/cypress_touchkey.h>
#ifdef CONFIG_LEDS_CLASS
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#endif
#include <linux/usb_switcher.h>

#include <mach/board-sec-u8500.h>

#define CYPRESS_GEN		0X00
#define CYPRESS_FW_VER		0X01
#define CYPRESS_MODULE_VER	0X02
#define CYPRESS_2ND_HOST	0X03
#define CYPRESS_THRESHOLD	0X04
#define CYPRESS_AUTO_CAL_FLG	0X05
#define CYPRESS_IDAC_MENU	0X06
#define CYPRESS_IDAC_BACK	0X07
#define CYPRESS_TA_STATUS	0X08
#define CYPRESS_DIFF_MENU	0X0A
#define CYPRESS_DIFF_BACK	0X0C
#define CYPRESS_RAW_DATA_MENU	0X0E
#define CYPRESS_RAW_DATA_BACK	0X10

#define CYPRESS_LED_ON		0X10
#define CYPRESS_LED_OFF		0X20
#define CYPRESS_DATA_UPDATE	0X40
#define CYPRESS_AUTO_CAL	0X50
#define CYPRESS_LED_CONTROL_ON	0X60
#define CYPRESS_LED_CONTROL_OFF	0X70
#define CYPRESS_SLEEP		0X80

#define JANICE_TOUCHKEY_HW02_MOD_VER	0X03
#define JANICE_TOUCHKEY_HW03_MOD_VER	0X08
#define JANICE_TOUCHKEY_M_04_FW_VER	0X0B
#define JANICE_TOUCHKEY_M_03_FW_VER	0X05

#ifdef CONFIG_LEDS_CLASS
#define TOUCHKEY_BACKLIGHT	"button-backlight"
#endif

/*sec_class sysfs*/
extern struct class *sec_class;
struct device *sec_touchkey;

static u16 raw_data0;
static u16 raw_data1;
static u8 idac0;
static u8 idac1;
static u8 menu_sensitivity;
static u8 back_sensitivity;
static u8 touchkey_threshold;

/* bit masks*/
#define PRESS_BIT_MASK		0X08
#define KEYCODE_BIT_MASK	0X07

#define TOUCHKEY_LOG(k, v) dev_notice(&info->client->dev, "key[%d] %d\n", k, v);
#define FUNC_CALLED dev_notice(&info->client->dev, "%s: called.\n", __func__);

#define NUM_OF_RETRY_UPDATE	5
#define NUM_OF_RETRY_I2C	2
#define NUM_OF_KEY		2
static unsigned char cypress_touchkey_keycode[] = {KEY_MENU, KEY_BACK};
static int touchkey_update_status;
extern bool vbus_state;

static struct workqueue_struct *touchkey_wq;
static struct work_struct update_work;

extern int touch_is_pressed;
struct cypress_touchkey_info {
	struct i2c_client			*client;
	struct cypress_touchkey_platform_data	*pdata;
	struct input_dev			*input_dev;
	struct early_suspend			early_suspend;
	struct workqueue_struct *key_wq;
	struct work_struct key_work;
	struct timeval start, end;
	char					phys[32];
	unsigned char				keycode[NUM_OF_KEY];
	int					irq;
	int					code, press;
	u8					keybuf;
	u8					fw_ver;
	u8					mod_ver;
#ifdef CONFIG_LEDS_CLASS
	struct led_classdev leds;
	enum led_brightness brightness;
	struct mutex touchkey_mutex;
	struct workqueue_struct *led_wq;
	struct work_struct led_work;
	int	current_status;
#endif
};

static struct cypress_touchkey_info *info;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cypress_touchkey_early_suspend(struct early_suspend *h);
static void cypress_touchkey_late_resume(struct early_suspend *h);
#endif

#ifdef TOUCHKEY_UPDATE_ONBOOT
static int touch_FW_update(void);
#endif
static void touch_FW_update_func(struct work_struct *work);

#ifdef CONFIG_LEDS_CLASS
static void cypress_touchkey_led_work(struct work_struct *work)
{
	u8 buf;
	int ret;

	/*never delete this line. Becaus of resume function sync. */
	msleep(100);

	if (info->brightness == LED_OFF)
		buf = CYPRESS_LED_OFF;
	else
		buf = CYPRESS_LED_ON;

	mutex_lock(&info->touchkey_mutex);

	ret = i2c_smbus_write_byte_data(info->client, CYPRESS_GEN, buf);
	if (ret < 0)
		dev_err(&info->client->dev, "[Touchkey] i2c write error [%d]\n",
			ret);

	mutex_unlock(&info->touchkey_mutex);
}

static void cypress_touchkey_brightness_set
		(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	/* Must not sleep, use a workqueue if needed */
	if (!info)
		return;

	info->brightness = brightness;

	if (info->current_status && !touchkey_update_status)
		queue_work(info->led_wq, &info->led_work);
	else
		dev_notice(&info->client->dev, "%s under suspend status or FW updating\n", __func__);
}
#endif

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_I2C_PERIPHS
void cypress_touchkey_panic_display(struct i2c_adapter *pAdap)
{
	u8 mod_ver;
	u8 fw_ver;
	u8 buf[2] = {0,};
	int ret = 0;

	/*
	 * Check driver has been started.
	*/
	if (!(info && info->client && info->client->adapter))
		return;

	/*
	 * If there is an associated LDO check to make sure it is powered, if
	 * not then we can exit as it wasn't powered when panic occurred.
	*/

	/*
	 * If pAdap is NULL then exit with message.
	*/
	if (!pAdap) {
		pr_emerg("\n\n%s Passed NULL pointer!\n", __func__);
		return;
	}

	/*
	 * If pAdap->algo_data is not NULL then this driver is using HW I2C,
	 *  then change adapter to use GPIO I2C panic driver.
	 * NB!Will "probably" not work on systems with dedicated I2C pins.
	*/
	if (pAdap->algo_data) {
		info->client->adapter = pAdap;
	} else {
		/*
		 * Otherwise use panic safe SW I2C algo,
		*/
		info->client->adapter->algo = pAdap->algo;
	}

	pr_emerg("\n\n[Display of Cypress Touchkey registers]\n");

	ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_FW_VER,
					ARRAY_SIZE(buf), buf);

	if (system_rev >= JANICE_R0_3) {
		mod_ver = JANICE_TOUCHKEY_HW03_MOD_VER;
		fw_ver = JANICE_TOUCHKEY_M_04_FW_VER;
	} else {
		mod_ver = JANICE_TOUCHKEY_HW02_MOD_VER;
		fw_ver = JANICE_TOUCHKEY_M_03_FW_VER;
	}

	if (ret != ARRAY_SIZE(buf))
		printk(KERN_ERR "failed to read FW ver.\n");

	else if ((buf[1] == mod_ver) &&	(buf[0] < fw_ver)) {
		printk(KERN_DEBUG "[TouchKey] %s : Mod Ver 0x%02x\n", __func__,
			buf[1]);
		printk(KERN_DEBUG "[TouchKey] FW mod 0x%02x, phone 0x%02x\n",
			buf[0], fw_ver);
	}
}
#endif

static void cypress_touchkey_work(struct work_struct *work)
{
	struct cypress_touchkey_info *info = container_of(work,
		struct cypress_touchkey_info, key_work);
	struct timeval diff;
	int code;
	int press;

	if (info->keybuf == 0xFF) {
		dev_err(&info->client->dev, "keybuf: 0x%2X\n", info->keybuf);
		goto out;
	}

	press = !(info->keybuf & PRESS_BIT_MASK);
	code = (int)(info->keybuf & KEYCODE_BIT_MASK) - 1;

	if (code < 0) {
		dev_err(&info->client->dev, "not proper interrupt 0x%2X.\n",
			info->keybuf);
		if (info->press == 0)
			goto out;
		dev_err(&info->client->dev, "forced key released.\n");
		code = info->code;
		press = 0;
	}

	/* ignore invalid keycode, keypress value */
	if (code > 1 || press > 1) {
		dev_err(&info->client->dev, "invalid keycode or keypress 0x%2X.\n", info->keybuf);
		goto out;
	}

#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	TOUCHKEY_LOG(info->keycode[code], press);
#endif

	if (touch_is_pressed && press) {
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		printk(KERN_DEBUG "[TouchKey]touchkey pressed but don't send event because touch is pressed.\n");
#endif
		goto out;
	}
	if (code == 0 && press == 1) {
		do_gettimeofday(&info->end);
		diff.tv_sec = info->end.tv_sec - info->start.tv_sec;
		diff.tv_usec = info->end.tv_usec - info->start.tv_usec;

		if (diff.tv_sec >= 0) {
			if (diff.tv_usec < 0) {
				(diff.tv_sec)--;
				diff.tv_usec = (info->end.tv_usec + 1000000L) - info->start.tv_usec;
			}

			/* If the interval of pressed menu-key is below 100msec */
			if (diff.tv_sec == 0 && diff.tv_usec < 100000) {
				dev_err(&info->client->dev, "Interval below 100msec:%ldusec\n", diff.tv_usec);
				info->start.tv_sec = info->end.tv_sec;
				info->start.tv_usec = info->end.tv_usec;
				goto out;
			}
		}
		/* refresh timeval */
		info->start.tv_sec = info->end.tv_sec;
		info->start.tv_usec = info->end.tv_usec;
	}

	info->code = code;
	info->press = press;
	input_report_key(info->input_dev, info->keycode[code], press);
	input_sync(info->input_dev);

out:
	enable_irq(info->irq);
	return;
}

static irqreturn_t cypress_touchkey_interrupt(int irq, void *dev_id)
{
	struct cypress_touchkey_info *info = dev_id;
	int ret;
	int cnt = 0;
	int code;
	u8 buf[1] = {0};

	disable_irq_nosync(info->irq);
	do {
		ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_GEN,
					ARRAY_SIZE(buf), buf);
		code = (int)(buf[0] & KEYCODE_BIT_MASK) - 1;
		cnt++;
	} while ((ret < 0 || code < 0) && cnt < NUM_OF_RETRY_I2C);
	info->keybuf = buf[0];

	if (ret < 0) {
		dev_err(&info->client->dev, "interrupt failed.\n");
		info->keybuf = 0xFF;
	}

	queue_work(info->key_wq, &info->key_work);
	return IRQ_HANDLED;
}

static void cypress_touchkey_con_hw(struct cypress_touchkey_info *info, bool flag)
{

	if (flag) {
		gpio_set_value(info->pdata->gpio_ldo_en, 1);
		gpio_set_value(info->pdata->gpio_led_en, 1);
	} else {
		gpio_set_value(info->pdata->gpio_led_en, 0);
		gpio_set_value(info->pdata->gpio_ldo_en, 0);
	}
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	dev_notice(&info->client->dev, "%s : called with flag %d.\n", __func__, flag);
#endif
}

static int cypress_touchkey_auto_cal(struct cypress_touchkey_info *info)
{
	u8 buf[4] = {0,};
	int ret, retry = 0;
	int count;

	while (retry < 3) {
		ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_GEN,
					ARRAY_SIZE(buf), buf);
		if (ret < 0) {
			printk(KERN_ERR"[TouchKey]i2c read fail.\n");
			return ret;
		}
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		printk(KERN_DEBUG "[TouchKey] touchkey_autocalibration : buf[0]=%x buf[1]=%x buf[2]=%x buf[3]=%x\n",
				buf[0], buf[1], buf[2], buf[3]);
#endif

		/* Send autocal Command */
		buf[0] = CYPRESS_AUTO_CAL;
		buf[3] = 0x01;

		count = i2c_smbus_write_i2c_block_data(info->client, CYPRESS_GEN, ARRAY_SIZE(buf), buf);
		msleep(100);

		/* Check autocal status*/
		buf[0] = i2c_smbus_read_byte_data(info->client, CYPRESS_AUTO_CAL_FLG);

		if ((buf[0] & 0x80)) {
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			printk(KERN_DEBUG "[Touchkey] autocal Enabled\n");
#endif
			break;
		} else
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			printk(KERN_DEBUG "[Touchkey] autocal disabled, retry %d\n", retry);
#endif

		retry += 1;
	}

	if (retry == 3)
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		printk(KERN_DEBUG "[Touchkey] autocal failed\n");
#endif

	return count;
}

static void cypress_thd_change(bool vbus_status)
{
	int ret;
	u8 data;

	if (!info)
		return;

	if (!info->fw_ver) {
		data = i2c_smbus_read_byte_data(info->client, CYPRESS_FW_VER);
		info->fw_ver = data;
	}
	if (info->fw_ver >= 0x09) {
		if (vbus_status == 1)
			ret = i2c_smbus_write_byte_data(info->client,
				CYPRESS_TA_STATUS, 0x01);
		else
			ret = i2c_smbus_write_byte_data(info->client,
				CYPRESS_TA_STATUS, 0x00);
		if (ret < 0)
			dev_err(&info->client->dev, "[Touchkey] i2c write error [%d]\n", ret);
	}
}

void cypress_touchkey_change_thd(bool vbus_status)
{
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	printk(KERN_DEBUG "[Touchkey] VBUS_STATUS %d\n", vbus_status);
	printk(KERN_DEBUG "[Touchkey] CYPRESS_VBUS_STATUS %d\n", vbus_status);
#endif
	if (info)
		cypress_thd_change(vbus_status);
}
EXPORT_SYMBOL(cypress_touchkey_change_thd);

/* Delete the old attribute file of Touchkey LED */
/*
static int cypress_touchkey_led_on(struct cypress_touchkey_info *info)
{
	u8 buf = CYPRESS_LED_ON;

	int ret;
	ret = i2c_smbus_write_byte_data(info->client, CYPRESS_GEN, buf);

	return ret;
}

static int cypress_touchkey_led_off(struct cypress_touchkey_info *info)
{
	u8 buf = CYPRESS_LED_OFF;

	int ret;
	ret = i2c_smbus_write_byte_data(info->client, CYPRESS_GEN, buf);

	return ret;
}
*/
static int __devinit cypress_touchkey_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct input_dev *input_dev;
	int ret = 0;
	int i;
	u8 buf[2] = {0,};
#ifdef TOUCHKEY_UPDATE_ONBOOT
	u8 mod_ver;
	u8 fw_ver;
#endif

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;

	info = kzalloc(sizeof(struct cypress_touchkey_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "fail to memory allocation.\n");
		goto err_mem_alloc;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "fail to allocate input device.\n");
		goto err_input_dev_alloc;
	}

	info->client = client;
	info->input_dev = input_dev;
	info->pdata = client->dev.platform_data;
	info->irq = client->irq;

	memcpy(info->keycode, cypress_touchkey_keycode, ARRAY_SIZE(cypress_touchkey_keycode));
	snprintf(info->phys, sizeof(info->phys), "%s/input0", dev_name(&client->dev));
	input_dev->name = "sec_touchkey";
	input_dev->phys = info->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_LED, input_dev->evbit);
	set_bit(LED_MISC, input_dev->ledbit);

	for (i = 0; i < ARRAY_SIZE(info->keycode); i++) {
		set_bit(info->keycode[i], input_dev->keybit);
	}
	input_set_drvdata(input_dev, info);

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&client->dev, "failed to register input dev (%d).\n",
			ret);
		goto err_reg_input_dev;
	}

	do_gettimeofday(&info->start);

	i2c_set_clientdata(client, info);
	cypress_touchkey_con_hw(info, true);

#ifdef TOUCHKEY_UPDATE_ONBOOT
	if (system_rev >= JANICE_R0_3) {
		mod_ver = JANICE_TOUCHKEY_HW03_MOD_VER;
		fw_ver = JANICE_TOUCHKEY_M_04_FW_VER;
	} else {
		mod_ver = JANICE_TOUCHKEY_HW02_MOD_VER;
		fw_ver = JANICE_TOUCHKEY_M_03_FW_VER;
	}
#endif

	ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_FW_VER,
					ARRAY_SIZE(buf), buf);

	if (ret != ARRAY_SIZE(buf))
		dev_err(&client->dev, "failed to check FW ver.\n");

	else {
		info->fw_ver = buf[0];
		info->mod_ver = buf[1];
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		printk(KERN_DEBUG "[TouchKey] %s : Mod Ver 0x%02x\n", __func__,
			info->mod_ver);
		printk(KERN_DEBUG "[TouchKey] FW mod 0x%02x\n",
			info->fw_ver);
#endif
#ifdef TOUCHKEY_UPDATE_ONBOOT
		if ((info->mod_ver == mod_ver) && (info->fw_ver < fw_ver))
			touch_FW_update();
#endif
	}
	cypress_thd_change(vbus_state);
	cypress_touchkey_auto_cal(info);

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->early_suspend.suspend = cypress_touchkey_early_suspend;
	info->early_suspend.resume = cypress_touchkey_late_resume;
	register_early_suspend(&info->early_suspend);
#endif /* CONFIG_HAS_EARLYSUSPEND */

	info->key_wq = create_singlethread_workqueue("cypress_key_wq");
	INIT_WORK(&info->key_work, cypress_touchkey_work);
	touchkey_wq = create_singlethread_workqueue("cypress_tsk_update_wq");

#ifdef CONFIG_LEDS_CLASS
	mutex_init(&info->touchkey_mutex);
	info->led_wq = create_singlethread_workqueue("cypress_touchkey");
	INIT_WORK(&info->led_work, cypress_touchkey_led_work);

	info->leds.name = TOUCHKEY_BACKLIGHT;
	info->leds.brightness = LED_FULL;
	info->leds.max_brightness = LED_FULL;
	info->leds.brightness_set = cypress_touchkey_brightness_set;
	info->current_status = 1;
	ret = led_classdev_register(&client->dev, &info->leds);
	if (ret) {
		goto err_req_irq;
	}
#endif

	ret = request_threaded_irq(client->irq, NULL, cypress_touchkey_interrupt,
				   IRQF_TRIGGER_RISING, client->dev.driver->name, info);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to request IRQ %d (err: %d).\n", client->irq, ret);
		goto err_req_irq;
	}

	FUNC_CALLED;
	return 0;

err_req_irq:
#ifdef CONFIG_LEDS_CLASS
	destroy_workqueue(info->led_wq);
#endif
	destroy_workqueue(info->key_wq);
	destroy_workqueue(touchkey_wq);
	input_unregister_device(input_dev);
	input_dev = NULL;
err_reg_input_dev:
err_input_dev_alloc:
	input_free_device(input_dev);
	kfree(info);
err_mem_alloc:
	return ret;
}

static int __devexit cypress_touchkey_remove(struct i2c_client *client)
{
	struct cypress_touchkey_info *info = i2c_get_clientdata(client);
	if (info->irq >= 0)
		free_irq(info->irq, info);
	input_unregister_device(info->input_dev);
	kfree(info);
	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
static int cypress_touchkey_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cypress_touchkey_info *info = i2c_get_clientdata(client);
	int ret = 0;

	FUNC_CALLED;

	disable_irq(info->irq);
	cypress_touchkey_con_hw(info, false);
	return ret;
}

static int cypress_touchkey_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cypress_touchkey_info *info = i2c_get_clientdata(client);
	int ret = 0;

	cypress_touchkey_con_hw(info, true);
	msleep(110);
	cypress_thd_change(vbus_state);
	cypress_touchkey_auto_cal(info);

	FUNC_CALLED;
	enable_irq(info->irq);

	return ret;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cypress_touchkey_early_suspend(struct early_suspend *h)
{
	struct cypress_touchkey_info *info;
	info = container_of(h, struct cypress_touchkey_info, early_suspend);
	cypress_touchkey_suspend(&info->client->dev);

	#ifdef CONFIG_LEDS_CLASS
	info->current_status = 0;
	#endif
}

static void cypress_touchkey_late_resume(struct early_suspend *h)
{
	struct cypress_touchkey_info *info;
	info = container_of(h, struct cypress_touchkey_info, early_suspend);
	cypress_touchkey_resume(&info->client->dev);

	#ifdef CONFIG_LEDS_CLASS
	/*led sysfs write led value before resume process is not executed */
	info->current_status = 1;
	queue_work(info->led_wq, &info->led_work);
	#endif
}
#endif

static const struct i2c_device_id cypress_touchkey_id[] = {
	{"cypress_touchkey", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cypress_touchkey_id);

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops cypress_touchkey_pm_ops = {
	.suspend	= cypress_touchkey_suspend,
	.resume		= cypress_touchkey_resume,
};
#endif

struct i2c_driver cypress_touchkey_driver = {
	.probe = cypress_touchkey_probe,
	.remove = cypress_touchkey_remove,
	.driver = {
		.name = "cypress_touchkey",
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm	= &cypress_touchkey_pm_ops,
#endif
		   },
	.id_table = cypress_touchkey_id,
};

static ssize_t touch_version_read(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	u8 data;
	int count;

	data = i2c_smbus_read_byte_data(info->client, CYPRESS_FW_VER);
	count = sprintf(buf, "0x%02x\n", data);

	info->fw_ver = data;
	printk(KERN_DEBUG "[TouchKey] %s : FW Ver 0x%02x\n", __func__, data);

	return count;
}

static ssize_t touch_version_write(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	printk(KERN_DEBUG "[TouchKey] %s : input data --> %s\n", __func__, buf);

	return size;
}

static ssize_t touch_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int count;
	int fw_ver;

	if (system_rev >= JANICE_R0_3)
		fw_ver = JANICE_TOUCHKEY_M_04_FW_VER;
	else
		fw_ver = JANICE_TOUCHKEY_M_03_FW_VER;

	count = sprintf(buf, "0x%02x\n", fw_ver);
	return count;
}

static ssize_t touch_update_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	printk(KERN_DEBUG "[TouchKey] touchkey firmware update\n");

/*
	if (*buf == 'S') {
		disable_irq(IRQ_TOUCH_INT);
		INIT_WORK(&touch_update_work, touchkey_update_func);
		queue_work(touchkey_wq, &touch_update_work);
	}
*/
	return size;
}

extern int ISSP_main(void);

static ssize_t touch_update_read(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int count = 0;

	printk(KERN_DEBUG "[TouchKey] touchkey firmware update \n");

	INIT_WORK(&update_work, touch_FW_update_func);
	queue_work(touchkey_wq, &update_work);

	msleep(500);
	count = sprintf(buf, "0x%02x\n", info->fw_ver);
	return count;
}

static void touch_FW_update_func(struct work_struct *work)
{
	int count = 0;
	int retry = NUM_OF_RETRY_UPDATE;
	u8 data;

	touchkey_update_status = 1;
	printk(KERN_DEBUG "[TouchKey] %s start... !\n", __func__);

	disable_irq(info->irq);
	while (retry--) {
		if (ISSP_main() == 0) {
			printk(KERN_DEBUG "[TOUCHKEY] Update success!\n");
			cypress_touchkey_con_hw(info, true);
			msleep(200);
			enable_irq(info->irq);

			data = i2c_smbus_read_byte_data(info->client, CYPRESS_FW_VER);
			info->fw_ver = data;
			printk(KERN_DEBUG "[TouchKey] %s : FW Ver 0x%02x\n", __func__,
			data);

			cypress_thd_change(vbus_state);
			cypress_touchkey_auto_cal(info);
			touchkey_update_status = 0;
			count = i2c_smbus_write_byte_data(info->client,
					CYPRESS_GEN, CYPRESS_DATA_UPDATE);
			return;
		}
		cypress_touchkey_con_hw(info, false);
		msleep(300);
		cypress_touchkey_con_hw(info, true);
		msleep(200);
	}

	touchkey_update_status = -1;
	printk(KERN_DEBUG "[TOUCHKEY]Touchkey_update fail\n");
	return;
}

#ifdef TOUCHKEY_UPDATE_ONBOOT
static int touch_FW_update(void)
{
	int count = 0;
	int retry = NUM_OF_RETRY_UPDATE;

	touchkey_update_status = 1;

	while (retry--) {
		printk(KERN_DEBUG "[TOUCHKEY] Updating... !\n");
		msleep(300);
		if (ISSP_main() == 0) {
			printk(KERN_DEBUG "[TOUCHKEY] Update success!\n");
			touchkey_update_status = 0;
			count = 1;
			break;
		}
		printk(KERN_DEBUG "[TOUCHKEY] Touchkey_update failed... retry...\n");
	}

	if (retry <= 0) {
		cypress_touchkey_con_hw(info, false);
		count = 0;
		printk(KERN_DEBUG "[TOUCHKEY]Touchkey_update fail\n");
		touchkey_update_status = -1;
		msleep(300);
	}

	msleep(500);
	return count;
}
#endif

static ssize_t touchkey_update_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int count;

	dev_info(&info->client->dev,
		"Enter firmware_status_show by Factory command\n");

	if (touchkey_update_status == 1)
		count = sprintf(buf, "DOWNLOADING\n");
	else if (touchkey_update_status == 0)
		count = sprintf(buf, "PASS\n");
	else if (touchkey_update_status == -1)
		count = sprintf(buf, "FAIL\n");
	else
		count = sprintf(buf, "PASS\n");

	return count;
}

static ssize_t touchkey_menu_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u8 data[2] = {0,};
	int ret;

	if (!touchkey_update_status) {
		ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_DIFF_MENU,
			ARRAY_SIZE(data), data);
		if (ret != ARRAY_SIZE(data)) {
			dev_err(&info->client->dev, "[TouchKey] fail to read menu sensitivity.\n");
			return ret;
		}
		menu_sensitivity = ((0x00FF & data[0])<<8) | data[1];

		printk(KERN_INFO "called %s , data : %d %d\n", __func__,
				data[0], data[1]);
	}
	return sprintf(buf, "%d\n", menu_sensitivity);

}

static ssize_t touchkey_back_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 data[2] = {0,};
	int ret;

	if (!touchkey_update_status) {
		ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_DIFF_BACK,
			ARRAY_SIZE(data), data);
		if (ret != ARRAY_SIZE(data)) {
			dev_err(&info->client->dev, "[TouchKey] fail to read back sensitivity.\n");
			return ret;
		}

		back_sensitivity = ((0x00FF & data[0])<<8) | data[1];

		printk(KERN_INFO "called %s , data : %d %d\n", __func__,
				data[0], data[1]);
	}
	return sprintf(buf, "%d\n", back_sensitivity);

}

static ssize_t touchkey_raw_data0_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 data[2] = {0,};
	int ret;

	if (!touchkey_update_status) {
		ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_RAW_DATA_MENU,
			ARRAY_SIZE(data), data);
		if (ret != ARRAY_SIZE(data)) {
			dev_err(&info->client->dev, "[TouchKey] fail to read MENU raw data.\n");
			return ret;
		}

		raw_data0 = ((0x00FF & data[0])<<8) | data[1];

		printk(KERN_INFO "called %s , data : %d %d\n", __func__,
				data[0], data[1]);
	}
	return sprintf(buf, "%d\n", raw_data0);

}

static ssize_t touchkey_raw_data1_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 data[2] = {0,};
	int ret;

	if (!touchkey_update_status) {
		ret = i2c_smbus_read_i2c_block_data(info->client, CYPRESS_RAW_DATA_BACK,
			ARRAY_SIZE(data), data);
		if (ret != ARRAY_SIZE(data)) {
			dev_err(&info->client->dev, "[TouchKey] fail to read BACK raw data.\n");
			return ret;
		}

		raw_data1 = ((0x00FF & data[0])<<8) | data[1];

		printk(KERN_INFO "called %s , data : %d %d\n", __func__,
				data[0], data[1]);
	}
	return sprintf(buf, "%d\n", raw_data1);

}

static ssize_t touchkey_idac0_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 data = 0;

	if (!touchkey_update_status) {
		data = i2c_smbus_read_byte_data(info->client, CYPRESS_IDAC_MENU);

		printk(KERN_INFO "called %s , data : %d\n", __func__, data);
		idac0 = data;
	}
	return sprintf(buf, "%d\n", idac0);

}

static ssize_t touchkey_idac1_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 data = 0;

	if (!touchkey_update_status) {
		data = i2c_smbus_read_byte_data(info->client, CYPRESS_IDAC_BACK);

		printk(KERN_INFO "called %s , data : %d\n", __func__, data);
		idac1 = data;
	}
	return sprintf(buf, "%d\n", idac1);

}

static ssize_t touchkey_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 data = 0;

	data = i2c_smbus_read_byte_data(info->client, CYPRESS_THRESHOLD);

	printk(KERN_INFO "called %s , data : %d\n", __func__, data);
	touchkey_threshold = data;
	return sprintf(buf, "%d\n", touchkey_threshold);
}

static ssize_t touch_autocal_testmode(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	int count = 0;
	int on_off;

	if (sscanf(buf, "%d\n", &on_off) == 1) {
		printk(KERN_DEBUG "[TouchKey] Test Mode : %d \n", on_off);

		if (on_off == 1) {
			count = i2c_smbus_write_byte_data(info->client,
				CYPRESS_GEN, CYPRESS_DATA_UPDATE);
		} else {
			cypress_touchkey_con_hw(info, false);
			msleep(50);
			cypress_touchkey_con_hw(info, true);
			msleep(150);
			cypress_touchkey_auto_cal(info);
		}
	} else
		printk(KERN_DEBUG "[TouchKey] touch_led_brightness Error\n");

	return count;
}

static ssize_t touch_sensitivity_control(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	u8 data[] = {CYPRESS_DATA_UPDATE};
	int ret;

	printk(KERN_INFO "called %s \n", __func__);

	ret = i2c_smbus_write_i2c_block_data(info->client, CYPRESS_GEN, ARRAY_SIZE(data), data);

	return ret;
}

static DEVICE_ATTR(touchkey_firm_version_panel, S_IRUGO | S_IWUSR | S_IWGRP, touch_version_read, touch_version_write);
static DEVICE_ATTR(touchkey_firm_version_phone, S_IRUGO | S_IWUSR | S_IWGRP, touch_version_show, NULL);
static DEVICE_ATTR(touchkey_firm_update, S_IRUGO | S_IWUSR | S_IWGRP, touch_update_read, touch_update_write);
static DEVICE_ATTR(touchkey_firm_update_status, S_IRUGO, touchkey_update_status_show, NULL);
/* Delete the old attribute file of Touchkey LED */
/*
static DEVICE_ATTR(touchkey_brightness, S_IRUGO | S_IWUSR | S_IWGRP, NULL, touch_led_control);
*/
static DEVICE_ATTR(touchkey_menu, S_IRUGO, touchkey_menu_show, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO, touchkey_back_show, NULL);
static DEVICE_ATTR(touchkey_raw_data0, S_IRUGO, touchkey_raw_data0_show, NULL);
static DEVICE_ATTR(touchkey_raw_data1, S_IRUGO, touchkey_raw_data1_show, NULL);
static DEVICE_ATTR(touchkey_idac0, S_IRUGO, touchkey_idac0_show, NULL);
static DEVICE_ATTR(touchkey_idac1, S_IRUGO, touchkey_idac1_show, NULL);
static DEVICE_ATTR(touchkey_threshold, S_IRUGO, touchkey_threshold_show, NULL);
static DEVICE_ATTR(touch_sensitivity, S_IRUGO | S_IWUSR | S_IWGRP, NULL, touch_sensitivity_control);
static DEVICE_ATTR(touchkey_autocal_start, S_IRUGO | S_IWUSR | S_IWGRP, NULL, touch_autocal_testmode);


static int __init cypress_touchkey_init(void)
{
	int ret = 0;

	sec_touchkey = device_create(sec_class, NULL, 0, NULL, "sec_touchkey");

	if (IS_ERR(sec_touchkey)) {
			printk(KERN_ERR "Failed to create device(sec_touchkey)!\n");
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_firm_update) < 0) {
		printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_update.attr.name);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_firm_update_status) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_update_status.attr.name);

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_firm_version_panel) < 0) {
		printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_version_panel.attr.name);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_firm_version_phone) < 0) {
		printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_version_phone.attr.name);
	}
	/* Delete the old attribute file of Touchkey LED */
/*	if (device_create_file(sec_touchkey, &dev_attr_touchkey_brightness) < 0) {
		printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_brightness.attr.name);
	}
*/
	if (device_create_file(sec_touchkey, &dev_attr_touchkey_menu) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_menu\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_back) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_back\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_raw_data0) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_raw_data0\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_raw_data1) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_raw_data1\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_idac0) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_idac0\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_idac1) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_idac1\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_threshold) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_threshold\n", __func__);
	}

	if (device_create_file(sec_touchkey, &dev_attr_touchkey_autocal_start) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_autocal_start\n", __func__);
	}


	if (device_create_file(sec_touchkey, &dev_attr_touch_sensitivity) < 0) {
		printk(KERN_ERR "%s device_create_file fail dev_attr_touch_sensitivity\n", __func__);
	}

	ret = i2c_add_driver(&cypress_touchkey_driver);
	if (ret) {
		printk(KERN_ERR "[TouchKey] cypress touch keypad registration failed, module not inserted.ret= %d\n", ret);
	}

	return ret;
}

static void __exit cypress_touchkey_exit(void)
{
	i2c_del_driver(&cypress_touchkey_driver);
}

late_initcall(cypress_touchkey_init);
module_exit(cypress_touchkey_exit);

MODULE_DESCRIPTION("Touchkey driver for Cypress touchkey controller ");
MODULE_LICENSE("GPL");

