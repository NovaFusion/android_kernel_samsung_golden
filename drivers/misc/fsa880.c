/*
 * Copyright (C) Samsung 2011
 * Author: Andrew Roca  .
 * License Terms: GNU General Public License v2
 * Driver for FSA usb switches
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/irqs.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/usb_switcher.h>
#include <linux/semaphore.h>
#include <linux/reboot.h>
#include <linux/moduleparam.h>
#include <linux/switch.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include <mach/board-sec-u8500.h>
static struct class *usb_switch_class;
extern struct class *sec_class;

static struct device *micro_usb_switch;

static struct workqueue_struct *usb_switch_workqueue;
static BLOCKING_NOTIFIER_HEAD(usb_switch_notifier);

/* register addresses*/
#define FSA9490_DEVICE_ID_REGISTER 0x1
#define FSA9490_CONTROL_REGISTER 0x2
#define FSA9490_INTERRUPT_1_REGISTER 0x3
#define FSA9490_INTERRUPT_2_REGISTER 0x4
#define FSA9490_INTERRUPT_MASK_1_REGISTER 0x5
#define FSA9490_INTERRUPT_MASK_2_REGISTER 0x6
#define FSA9490_ADC_REGISTER 0x7
#define FSA9490_TIMING_1_REGISTER 0x8
#define FSA9490_TIMING_2_REGISTER 0x9
#define FSA9490_DEVICE_TYPE_1_REGISTER 0xa
#define FSA9490_DEVICE_TYPE_2_REGISTER 0xb
#define FSA9490_BUTTON_1_REGISTER 0xc
#define FSA9490_BUTTON_2_REGISTER 0xd
#define FSA9490_CAR_KIT_STATUS_REGISTER 0xe
#define FSA9490_CAR_KIT_INTERRUPT_1_REGISTER 0xf
#define FSA9490_CAR_KIT_INTERRUPT_2_REGISTER 0x10
#define FSA9490_CAR_KIT_INTERRUPT_1_MASK_REGISTER 0x11
#define FSA9490_CAR_KIT_INTERRUPT_2_MASK_REGISTER 0x12
#define FSA9490_MANUAL_SWITCH_1_REGISTER 0x13
#define FSA9490_MANUAL_SWITCH_2_REGISTER 0x14

/* Device Type 1 */
#define DEV_USB_OTG		(1 << 7)
#define DEV_DEDICATED_CHG	(1 << 6)
#define DEV_USB_CHG		(1 << 5)
#define DEV_CAR_KIT		(1 << 4)
#define DEV_UART		(1 << 3)
#define DEV_USB			(1 << 2)

#define DEV_T1_USB_MASK		(DEV_USB_OTG | DEV_USB)
#define DEV_T1_UART_MASK	(DEV_UART)
#define DEV_T1_CHARGER_MASK	(DEV_DEDICATED_CHG | DEV_USB_CHG | DEV_CAR_KIT)

/* Device Type 2 */
#define DEV_AV			(1 << 6)
#define DEV_TTY			(1 << 5)
#define DEV_PPD			(1 << 4)
#define DEV_JIG_UART_OFF	(1 << 3)
#define DEV_JIG_UART_ON		(1 << 2)
#define DEV_JIG_USB_OFF		(1 << 1)
#define DEV_JIG_USB_ON		(1 << 0)

#define DEV_T2_USB_MASK		(DEV_JIG_USB_OFF | DEV_JIG_USB_ON)
#define DEV_T2_UART_MASK	(DEV_JIG_UART_OFF | DEV_JIG_UART_ON)
#define DEV_T2_JIG_MASK		(DEV_JIG_USB_OFF | DEV_JIG_USB_ON | \
				DEV_JIG_UART_OFF)

#define SW_VAUDIO		((4 << 5) | (4 << 2) | (1 << 1) | (1 << 0))
#define SW_UART			((3 << 5) | (3 << 2))
#define SW_AUDIO		((2 << 5) | (2 << 2) | (1 << 1) | (1 << 0))
#define SW_DHOST		((1 << 5) | (1 << 2) | (1 << 1) | (1 << 0))
#define SW_AUTO			((0 << 5) | (0 << 2))
#define SW_USB_OPEN		(1 << 0)
#define SW_ALL_OPEN		(0)

#define CON_SWITCH_OPEN		(1 << 4)
#define CON_RAW_DATA		(1 << 3)
#define CON_MANUAL_SW		(1 << 2)
#define CON_WAIT		(1 << 1)
#define CON_INT_MASK		(1 << 0)
#define CON_MASK		(CON_SWITCH_OPEN | CON_RAW_DATA | \
				CON_MANUAL_SW | CON_WAIT)

/* interrupt control */
#define FSA9490_ATTACH_MASK_BIT (1<<0)
#define FSA9490_DETACH_MASK_BIT (1<<1)

/* control register */
#define FSA9490_SWITCH_AUTO_BIT (1<<4)
#define FSA9490_RAW_ENABLE_BIT (1<<3)
#define FSA9490_MANUAL_SWITCH_BIT (1<<2)
#define FSA9490_WAIT_ENABLE_BIT (1<<1)
#define FSA9490_INTERRUPT_ENABLE_BIT (1<<0)

#define FSA9490_CONTROL_MASK (FSA9490_RAW_ENABLE_BIT | FSA9490_WAIT_ENABLE_BIT)

#define FSA880_DELAY 20
#define FSA_DELAYED_WORK

/* Return value */
#define RETURN_OTHER_CABLE	1
#define RETURN_USB_CABLE	0

extern int jig_smd;
extern unsigned int system_rev;

struct register_bits {
	unsigned char mask;
	const char *name;
	unsigned long event;
} ;

static struct switch_dev switch_dock = {
	.name = "dock",
};

struct FSA9480_instance {
	struct usb_switch *current_switch;
	struct proc_dir_entry *proc_entry;
	struct i2c_client *client;
	unsigned charge_detect_gpio;
	unsigned connection_change_gpio;
	u32 irq_bit;
	struct device *dev;
	const char *name;
	unsigned long last_event;
	unsigned long prev_event;
#if defined(FSA_DELAYED_WORK)
	struct delayed_work notifier_queue;
#else
	struct work_struct notifier_queue;
#endif
	int started;
	int disabled;
	struct notifier_block  reboot_notifier;
	struct wake_lock vbus_wake_lock;
	int dev1;
	int dev2;
};

static struct register_bits device_1_register_bits[] = {
	{	(1<<0), "Audio_1", EXTERNAL_AUDIO_1},
	{	(1<<1), "Audio_2", EXTERNAL_AUDIO_2 },
	{	(1<<2), "USB", EXTERNAL_USB},
	{	(1<<3), "UART", EXTERNAL_UART},
	{	(1<<4), "CAR_KIT", EXTERNAL_CAR_KIT},
	{	(1<<5), "USB_Charger", EXTERNAL_USB_CHARGER},
	{	(1<<6), "Dedicated_Charger", EXTERNAL_DEDICATED_CHARGER},
	{	(1<<7), "USB_OTG", EXTERNAL_USB_OTG},

};

static struct register_bits device_2_register_bits[] = {
	{	(1<<0)	, "JIG_USB_ON", EXTERNAL_JIG_USB_ON},
	{	(1<<1)	, "JIG_USB_OFF", EXTERNAL_JIG_USB_OFF},
	{	(1<<2)	, "JIG_UART_ON", EXTERNAL_JIG_UART_ON},
	{	(1<<3)	, "JIG_UART_OFF", EXTERNAL_JIG_UART_OFF},
	{	(1<<4)	, "Phone_powered", EXTERNAL_PHONE_POWERED_DEVICE},
	{	(1<<5)	, "TTY", EXTERNAL_TTY},
	{	(1<<6)	, "AV_CABLE", EXTERNAL_AV_CABLE},
	{	(1<<7)	, "Device_Unknown", EXTERNAL_DEVICE_UNKNOWN},
};

static struct FSA9480_instance driver_instance = {0};

void usb_switch_register_notify(struct notifier_block *nb)
{
	if (driver_instance.started)
		nb->notifier_call(nb, USB_SWITCH_CONNECTION_EVENT, NULL);

	blocking_notifier_chain_register(&usb_switch_notifier, nb);
}
EXPORT_SYMBOL_GPL(usb_switch_register_notify);


/**
 * usb_unregister_notify - unregister a notifier callback
 * @nb: pointer to the notifier block for the callback events.
 *
 * usb_register_notify() must have been previously called for this function
 * to work properly.
 */
void usb_switch_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&usb_switch_notifier, nb);
}
EXPORT_SYMBOL_GPL(usb_switch_unregister_notify);

static int read_FSA9480_register(struct FSA9480_instance *instance,
				unsigned char reg_number, char *value_here)
{
	int ret;
	u8 data = reg_number;

	struct i2c_msg msg = {
		.addr   = instance->client->addr,
		.flags  = 0,
		.len    = 1,
		.buf    = &data,
	};

	ret = i2c_transfer(instance->client->adapter, &msg, 1);

	if (ret > 0) {
		msg.flags = I2C_M_RD;
		ret = i2c_transfer(instance->client->adapter, &msg, 1);
		if (ret > 0)
			ret = 0;
		*value_here = data;
	}

	return ret;
}

static int write_FSA9480_register(struct FSA9480_instance *instance,
			unsigned char reg_number, unsigned char  new_value)
{
	int ret;
	unsigned char data[2] = { reg_number, new_value };

	struct i2c_msg msg = {
		.addr   = instance->client->addr,
		.flags  = 0,
		.len    = 2,
		.buf    = data,
	};

	ret = i2c_transfer(instance->client->adapter, &msg, 1);

	if (ret > 0)
		ret = 0;

	return ret ;
}

static int FSA9480_USB_charger_present(struct FSA9480_instance *instance)
{
	return gpio_get_value_cansleep(instance->charge_detect_gpio) ? 0 : 1;
}

static int FSA9480_connection_change_gpio(struct FSA9480_instance *instance)
{
	return gpio_get_value_cansleep(instance->connection_change_gpio) ? 1 : 0;
}

unsigned long usb_switch_get_previous_connection(void)
{
	unsigned long ret ;

	ret = driver_instance.prev_event ;

	return ret ;
}
EXPORT_SYMBOL_GPL(usb_switch_get_previous_connection);

unsigned long usb_switch_get_current_connection(void)
{
	unsigned long ret ;

	ret = driver_instance.last_event ;

	return ret ;
}
EXPORT_SYMBOL_GPL(usb_switch_get_current_connection);

#ifdef CONFIG_SAMSUNG_LOG_BUF
static ssize_t show_current_connection_log(unsigned long charger_event)
{
	int i ;
	const char *string = NULL ;

	for (i = 0; i < ARRAY_SIZE(device_1_register_bits); i++) {
		if ((charger_event & 0xFFFF) &
		    device_1_register_bits[i].event) {
			string = device_1_register_bits[i].name ;
			break ;
		}
	}

	if (string == NULL) {
		for (i = 0; i < ARRAY_SIZE(device_2_register_bits); i++) {
			if ((charger_event & 0xFFFF) &
			    device_2_register_bits[i].event) {
				string = device_2_register_bits[i].name ;
				break ;
			}
		}
	}

	if (string == NULL)
		string = "none";

	return printk(KERN_INFO "%s  is connnected to the device\n", string);
}
#endif

/* SW RESET for TI USB:To fix no USB recog problem after jig attach&detach*/
static void TI_SWreset(struct FSA9480_instance *instance)
{
	printk(KERN_INFO "[TSU6111] TI_SW reset ...Start\n");
	disable_irq(instance->irq_bit);


	/*Hold SCL&SDA Low more than 30ms*/
	nmk_config_pin(PIN_CFG(16, GPIO)| PIN_OUTPUT_LOW,false);
	nmk_config_pin(PIN_CFG(17, GPIO)| PIN_OUTPUT_LOW,false);
	mdelay(40);

	/*Make SCL&SDA High again*/
	nmk_config_pin(PIN_CFG(16, ALT_B)|PIN_OUTPUT_HIGH,false);
	nmk_config_pin(PIN_CFG(17, ALT_B)|PIN_OUTPUT_HIGH,false);

	/*Write SOME Init register value again*/
	write_FSA9480_register(instance, FSA9490_INTERRUPT_MASK_1_REGISTER, 0x00);
	write_FSA9480_register(instance, FSA9490_INTERRUPT_MASK_2_REGISTER, 0x27);
	write_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, 0x1E);

	mdelay(30);
	printk(KERN_INFO "[TSU6111] IRQ enable start\n");
	enable_irq(instance->irq_bit);
	printk(KERN_INFO "[TSU6111] TI_SWreset ...Done\n");
}

static ssize_t show_current_connection(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	const char *string = NULL;
	char c;
	struct FSA9480_instance *instance = dev_get_drvdata(dev);

	if (instance->disabled)
		return sprintf(buf, "disabled\n");

	read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_1_REGISTER, &c);

	for (i = 0; i < ARRAY_SIZE(device_1_register_bits); i++) {
		if (c & device_1_register_bits[i].mask) {
			string = device_1_register_bits[i].name ;
			break ;
		}
	}

	if (string == NULL) {
		read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_2_REGISTER, &c);

		for (i = 0; i < ARRAY_SIZE(device_2_register_bits); i++) {
			if (c & device_2_register_bits[i].mask) {
				string = device_2_register_bits[i].name ;
				break ;
			}
		}
	}

	if (string == NULL)
		string = "none";

	return sprintf(buf, "%s\n", string);
}

static ssize_t show_charger_connection(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct FSA9480_instance *instance = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", FSA9480_USB_charger_present(instance) ? "charger" : "none");
}

static ssize_t show_jig_smd(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (jig_smd) {
		return jig_smd;
	} else
		return -1;
}

static ssize_t store_jig_smd(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int data;

	sscanf(buf, "%d\n", &data);
	jig_smd = data;
	printk(KERN_DEBUG "jig_smd value : %d.\n", jig_smd);

	return size;
}

extern void uart_wakeunlock(void);

static ssize_t store_smd_wakelock(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int data;

	sscanf(buf, "%d\n", &data);
#if 0
	if (data == 0)
		uart_wakeunlock();
#endif
	return size;
}

static ssize_t show_usb_state(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	unsigned char c, device_type1, device_type2;

	read_FSA9480_register(&driver_instance, FSA9490_DEVICE_TYPE_1_REGISTER, &c);
	device_type1 = c;

	read_FSA9480_register(&driver_instance, FSA9490_DEVICE_TYPE_2_REGISTER, &c);
	device_type2 = c;

	if (device_type1 & (DEV_T1_USB_MASK | DEV_USB_CHG) ||
			device_type2 & DEV_T2_USB_MASK)
		return snprintf(buf, 22, "USB_STATE_CONFIGURED\n");

	return snprintf(buf, 25, "USB_STATE_NOTCONFIGURED\n");
}

static struct device_attribute FSA9480_device_attrs[] = {
	__ATTR(connection, 0444, show_current_connection, NULL),
	__ATTR(charger, 0444, show_charger_connection, NULL),
	__ATTR(jig_smd, 0644, show_jig_smd, store_jig_smd),
	__ATTR(smd_wakelock, 0664, NULL, store_smd_wakelock),
};

static DEVICE_ATTR(usb_state, 0444, show_usb_state, NULL);

static int FSA9480_readproc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	struct FSA9480_instance *instance = (struct FSA9480_instance *) data ;
	int i ;
	char c ;
	int len = 0;
	int ret = 0;

	for (i = 1; i <= 0x14; i++) {
		c = i;
		ret = read_FSA9480_register(instance, i, &c);
		if (ret >= 0)
			len += sprintf(page+len, "reg 0x%02x = 0x%02x\n", i, c);
		else
			len += sprintf(page+len, "reg 0x%02x failed return=%d\n", i, ret);

	}

	if (instance->charge_detect_gpio)
		len += sprintf(page + len, "charge detect= 0x%02x\n", FSA9480_USB_charger_present(instance));

	len += sprintf(page + len, "connection detect= 0x%02x\n", FSA9480_connection_change_gpio(instance));

	read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_1_REGISTER, &c);

	for (i = 0; i < ARRAY_SIZE(device_1_register_bits); i++) {
		if (c&device_1_register_bits[i].mask) {
			len += sprintf(page + len, "%s \n", device_1_register_bits[i].name);
		}
	}

	*eof = -1;

	return len;
}

#if !defined(FSA_DELAYED_WORK)
static irqreturn_t FSA9480_irq_handler(int irq, void *data)
{
	irqreturn_t r;
	r = IRQ_WAKE_THREAD;
	return r;
}
#endif

static unsigned long current_device_mask(struct FSA9480_instance *instance)
{
	char c;
	int i;
	unsigned long event = 0;

	if (instance->current_switch) {
		read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_1_REGISTER, &c);
		c &= instance->current_switch->valid_device_register_1_bits;

		for (i = 0; i < ARRAY_SIZE(device_1_register_bits); i++) {
			if (c & device_1_register_bits[i].mask) {
				event |= device_1_register_bits[i].event;
				break;
			}
		}

		if (instance->current_switch->valid_device_register_2_bits) {
			read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_2_REGISTER, &c);
			c &= instance->current_switch->valid_device_register_2_bits;

			for (i = 0; i < ARRAY_SIZE(device_2_register_bits); i++) {
				if (c & device_2_register_bits[i].mask) {
					event |= device_2_register_bits[i].event;
					break;
				}
			}
		}
	}

	return event;
}

static unsigned long current_connection_mask(struct FSA9480_instance *instance)
{
	char c;
	int i;
	unsigned long event = 0;

	if (instance->current_switch) {
		read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_1_REGISTER, &c);
		c &= instance->current_switch->valid_device_register_1_bits;

		for (i = 0; i < ARRAY_SIZE(device_1_register_bits); i++) {
			if (c & device_1_register_bits[i].mask) {
				event |= device_1_register_bits[i].event;
				break;
			}
		}

		if (instance->current_switch->valid_device_register_2_bits) {
			read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_2_REGISTER, &c);
			c &= instance->current_switch->valid_device_register_2_bits;

			for (i = 0; i < ARRAY_SIZE(device_2_register_bits); i++) {
				if (c & device_2_register_bits[i].mask) {
					event |= device_2_register_bits[i].event;
					break;
				}
			}
		}
	}

	if (event) {
		/* Interrupt 1, 2 REG Clear */
		read_FSA9480_register(instance, FSA9490_INTERRUPT_2_REGISTER, &c);
		read_FSA9480_register(instance, FSA9490_INTERRUPT_1_REGISTER, &c);
		event |= (c&FSA9490_ATTACH_MASK_BIT) ? USB_SWITCH_CONNECTION_EVENT : 0;
	}

	return event;
}

static unsigned long get_current_connection_mask(struct FSA9480_instance *instance)
{
	char c;
	int i;
	unsigned long event = 0;
	int event_found = 0;

	printk(KERN_INFO "%s entered !!!\n", __func__);
	read_FSA9480_register(instance, FSA9490_INTERRUPT_1_REGISTER, &c);
	printk(KERN_INFO "fsa_detect_dev: intr reg: 0x%x\n", c);

	event |= (c&FSA9490_ATTACH_MASK_BIT) ? USB_SWITCH_CONNECTION_EVENT : 0;
	event |= (c&FSA9490_DETACH_MASK_BIT) ? USB_SWITCH_DISCONNECTION_EVENT : 0;

	if (instance->current_switch) {
		if (instance->current_switch->valid_registers[FSA9490_INTERRUPT_2_REGISTER])
			read_FSA9480_register(instance, FSA9490_INTERRUPT_2_REGISTER, &c);

		read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_1_REGISTER, &c);
		c &= instance->current_switch->valid_device_register_1_bits;
		for (i = 0; i < ARRAY_SIZE(device_1_register_bits); i++) {
			if (c & device_1_register_bits[i].mask) {
				event |= device_1_register_bits[i].event;
				event_found = 1;
				break;
			}
		}

		if (!event_found) {
			read_FSA9480_register(instance, FSA9490_DEVICE_TYPE_2_REGISTER, &c);
			c &= instance->current_switch->valid_device_register_2_bits;
			for (i = 0; i < ARRAY_SIZE(device_2_register_bits); i++) {
				if (c & device_2_register_bits[i].mask) {
					event |= device_2_register_bits[i].event;
					break;
				}
			}
		}
	}

	return event ;
}

static void switch_dock_init(void)
{
	int ret;

	ret = switch_dev_register(&switch_dock);
	if (ret < 0)
		printk(KERN_INFO "Failed to register dock switch\n");
}

static void usb_switch_notify_clients(struct work_struct *work)
{
	char adc;

#if defined(FSA_DELAYED_WORK)
	struct FSA9480_instance *instance = container_of(work, struct FSA9480_instance, notifier_queue.work);
#else
	struct FSA9480_instance *instance = container_of(work, struct FSA9480_instance, notifier_queue);
#endif

	instance->prev_event = instance->last_event;
	instance->last_event = get_current_connection_mask(instance);

	if (instance->prev_event & (EXTERNAL_JIG_UART_ON | EXTERNAL_JIG_UART_OFF)
		&& instance->last_event & USB_SWITCH_DISCONNECTION_EVENT) {
			TI_SWreset(instance);
			printk(KERN_INFO "[FSA880] S/W reset called.\n");
	}

	if (instance->last_event == USB_SWITCH_CONNECTION_EVENT) {
		printk(KERN_INFO "%s: only insert event occured but no devices\n", __func__);
		read_FSA9480_register(instance, FSA9490_ADC_REGISTER, &adc);
		msleep(50);
		instance->last_event = current_device_mask(instance);
		instance->last_event |= USB_SWITCH_CONNECTION_EVENT;

		if (instance->last_event == USB_SWITCH_CONNECTION_EVENT)
			TI_SWreset(instance);
	}

	printk(KERN_INFO "usb_switch_notify_clients event = 0x%08lx\n", instance->last_event);

#ifdef CONFIG_SAMSUNG_LOG_BUF
	show_current_connection_log(instance->last_event);
#endif

	blocking_notifier_call_chain(&usb_switch_notifier, instance->last_event, NULL);
}


static int fsa_detect_dev(struct FSA9480_instance *instance)
{

	char adc, dtype1, dtype2, ret = RETURN_OTHER_CABLE;

	read_FSA9480_register(instance,
				FSA9490_DEVICE_TYPE_1_REGISTER, &dtype1);
	read_FSA9480_register(instance,
				FSA9490_DEVICE_TYPE_2_REGISTER,	&dtype2);
	read_FSA9480_register(instance, FSA9490_ADC_REGISTER, &adc);

	printk(KERN_INFO "%s: adc:0x%x dtype1:0x%x dtype2:0x%x\n",
				__func__, adc, dtype1, dtype2);

	/* Attached */
	if (dtype1 || dtype2) {
		/* USB */
		if (dtype1 & DEV_USB || dtype2 & DEV_T2_USB_MASK) {
			printk(KERN_INFO "%s: usb connect\n", __func__);
			ret = RETURN_USB_CABLE;
		/* UART */
		} else if (dtype1 & DEV_T1_UART_MASK ||
					dtype2 & DEV_T2_UART_MASK) {
			printk(KERN_INFO "%s: UART connect\n", __func__);
			ret = RETURN_OTHER_CABLE;
		/* CHARGER */
		} else if (dtype1 & DEV_T1_CHARGER_MASK) {
			printk(KERN_INFO "%s: Charger connect\n", __func__);
			ret = RETURN_OTHER_CABLE;
		/* Desk Dock */
		} else if (dtype2 & DEV_AV) {
			printk(KERN_INFO "%s: Deskdock connect\n", __func__);
			switch_set_state(&switch_dock, 1);
			gpio_direction_output(200, 1);

			write_FSA9480_register(instance,
				FSA9490_MANUAL_SWITCH_1_REGISTER, SW_AUDIO);

			read_FSA9480_register(instance,
				FSA9490_CONTROL_REGISTER, &ret);

			ret &= ~CON_MANUAL_SW & ~CON_RAW_DATA;

			write_FSA9480_register(instance,
				FSA9490_CONTROL_REGISTER, ret);

			ret = RETURN_OTHER_CABLE;
#if 0
		/* Car Dock */
		} else if (dtype2 & DEV_JIG_UART_ON) {
			printk(KERN_INFO "%s: Cardock connect\n", __func__);
			switch_set_state(&switch_dock, 2);
			ret = RETURN_OTHER_CABLE;
		} else{
			printk(KERN_INFO
				"%s: connect not defined cable!\n", __func__);
			ret = RETURN_OTHER_CABLE;
#endif
		}
	/* Detached */
	} else {
		/* USB */
		if (instance->dev1 & DEV_USB ||
				instance->dev2 & DEV_T2_USB_MASK) {
			printk(KERN_INFO "%s: usb disconnect\n", __func__);
			ret = RETURN_USB_CABLE;
		/* UART */
		} else if (instance->dev1 & DEV_T1_UART_MASK ||
				instance->dev2 & DEV_T2_UART_MASK) {
			printk(KERN_INFO "%s: UART disconnect\n", __func__);
			ret = RETURN_OTHER_CABLE;

		/* CHARGER */
		} else if (instance->dev1 & DEV_T1_CHARGER_MASK) {
			printk(KERN_INFO "%s: Charger disconnect\n", __func__);
			ret = RETURN_OTHER_CABLE;
		/* Desk Dock */
		} else if (instance->dev2 & DEV_AV) {
			printk(KERN_INFO "%s: Deskdock disconnect\n", __func__);
			switch_set_state(&switch_dock, 0);
			gpio_direction_output(200, 0);

			read_FSA9480_register(instance,
					FSA9490_CONTROL_REGISTER, &ret);

			ret |= CON_MANUAL_SW | CON_RAW_DATA;
			write_FSA9480_register(instance,
					FSA9490_CONTROL_REGISTER, ret);

			ret = RETURN_OTHER_CABLE;

#if 0
		/* Car Dock */
		} else if (instance->dev2 & DEV_JIG_UART_ON) {
			printk(KERN_INFO
				"%s: Cardock disconnect\n", __func__);
			switch_set_state(&switch_dock, 0);
			ret = RETURN_OTHER_CABLE;
			TI_SWreset(instance);
		} else{
			printk(KERN_INFO
				"%s: notdefined cable disconect\n", __func__);
			ret = RETURN_OTHER_CABLE;
			TI_SWreset(instance);
#endif
		}
	}
	instance->dev1 = dtype1;
	instance->dev2 = dtype2;

	return ret;

}

static irqreturn_t FSA9480_irq_thread_fn(int irq, void *data)
{
	struct FSA9480_instance *instance = (struct FSA9480_instance *) data;
	char dev_id;
	printk(KERN_INFO "FSA9480_irq_thread_fn\n");

	read_FSA9480_register(instance,
				FSA9490_DEVICE_ID_REGISTER, &dev_id);
	printk(KERN_INFO "%s: ID:%x = detect_dev run: FSA9485!\n",
					__func__, dev_id);
	if (dev_id == 0x00) {
		printk(KERN_INFO "%s: ID:%x = detect_dev run: FSA9485!\n",
					__func__, dev_id);
		fsa_detect_dev(instance);
	}

	printk(KERN_INFO "%s: get gpio_value:200 = %x\n",
				__func__, gpio_get_value(200));
	/* detect deskdock.
	fsa_detect_dev(instance);
	*/
#if defined(FSA_DELAYED_WORK)
	schedule_delayed_work(&instance->notifier_queue, FSA880_DELAY);
	printk(KERN_INFO "scheduled_delayed_work called, %s, Line no:%d\n", __func__, __LINE__);
#else
	queue_work(usb_switch_workqueue, &instance->notifier_queue);
#endif

	return IRQ_HANDLED;
}

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_I2C_PERIPHS
void usb_switch_panic_display(struct i2c_adapter *pAdap)
{
	struct FSA9480_instance *instance = &driver_instance;
	struct usb_switch *pSwitch = instance->current_switch;

	/*
	 * Check driver has been started.
	*/
	if (!(instance && instance->client && instance->client->adapter))
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
		instance->client->adapter = pAdap;
	} else {
		/*
		 * Otherwise use panic safe SW I2C algo,
		*/
		instance->client->adapter->algo = pAdap->algo;
	}

	if (pSwitch) {
		u32 i;
		int ret;
		char c;
		pr_emerg("\n\n[Display of Micro USB switch registers]\n");

		for (i = 0; i < ARRAY_SIZE(pSwitch->valid_registers); i++) {
			if (pSwitch->valid_registers[i]) {
				ret = read_FSA9480_register(instance, i, &c);

				if (ret < 0)
					pr_emerg("\t[%02d]: Failed to read value\n", i, c);
				else
					pr_emerg("\t[%02d]: 0x%02x\n", i, c);
			}
		}
		pr_emerg("\n");
	}
}
#endif

void usb_switch_enable(void)
{
	struct FSA9480_instance *instance = &driver_instance;

	if (instance->started) {
		enable_irq(instance->irq_bit);
		instance->prev_event = instance->last_event;
		instance->last_event = get_current_connection_mask(instance);

#if defined(FSA_DELAYED_WORK)
		schedule_delayed_work(&instance->notifier_queue, FSA880_DELAY);
#else
		queue_work(usb_switch_workqueue, &instance->notifier_queue);
#endif
		driver_instance.disabled = 0;
	}
}
EXPORT_SYMBOL_GPL(usb_switch_enable);

void usb_switch_disable(void)
{
	if (driver_instance.started) {
		disable_irq(driver_instance.irq_bit);
		driver_instance.disabled = 1;
	}
}
EXPORT_SYMBOL_GPL(usb_switch_disable);

/*
	This function is called by the kernel reboot notifier .
	It allows the driver to selectively reset the control register to its
	default value.

	The USB switch isn't subject to a reset so we can use it to disingish between warm and
	cold boots.
	The assumption is that if the system is restarting then we don't want to go into charging mode
	so we don't reset the FSA control register.
	If the system is newly powered then the FSA reverts to its default settings. However if the
	processing cores are powered down then the battery keeps the FSA in its previous state.
	The reboot pending call resets the control register if the processor is powering down.

*/
static int reboot_pending(struct notifier_block *self, unsigned long type , void *arg)
{
	unsigned char c;
	struct FSA9480_instance *instance = container_of(self, struct FSA9480_instance, reboot_notifier);

	if (instance && instance->current_switch) {
		switch (type) {
		case	SYS_RESTART:
			break ;
		case	SYS_HALT:
		case	SYS_POWER_OFF:
			read_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, &c);		/*reset control register*/
			c |= (FSA9490_INTERRUPT_ENABLE_BIT);
			write_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, c);
			break ;
		}
	}

	return NOTIFY_DONE;
}

extern u32 sec_bootmode;

static int init_driver_instance(struct FSA9480_instance *instance, struct i2c_client *client)
{
	int ret;
	int i;
	char c;
	int vendor;

	memset(instance, 0, sizeof(struct FSA9480_instance));
	instance->client = client ;
	instance->reboot_notifier.notifier_call = reboot_pending;
	register_reboot_notifier(&instance->reboot_notifier);

	 /* detect model of switch */
	read_FSA9480_register(instance, FSA9490_DEVICE_ID_REGISTER, &c);
	vendor = c & 0x7;
	instance->current_switch = client->dev.platform_data;
	instance->name = instance->current_switch->name;
#if defined(FSA_DELAYED_WORK)
	INIT_DELAYED_WORK_DEFERRABLE(&instance->notifier_queue, usb_switch_notify_clients);
#else
	INIT_WORK(&instance->notifier_queue, usb_switch_notify_clients);
#endif
	instance->proc_entry = create_proc_read_entry("MUSB", 0444, NULL, FSA9480_readproc, instance);

	/*detect if control register is default*/
	read_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, &c);
	printk(KERN_INFO "FSA USB Switch control register is 0x%02x bootmode is %d\n", c, sec_bootmode);

	// Clear Interrupt Register1,2
//	read_FSA9480_register(instance, FSA9490_INTERRUPT_1_REGISTER, &c);
//	if (instance->current_switch->valid_registers[FSA9490_INTERRUPT_2_REGISTER])
//		read_FSA9480_register(instance, FSA9490_INTERRUPT_2_REGISTER, &c);

	gpio_request(instance->current_switch->connection_changed_interrupt_gpio, instance->current_switch->name);
	nmk_gpio_set_pull(instance->current_switch->connection_changed_interrupt_gpio, NMK_GPIO_PULL_UP);
	gpio_direction_input(instance->current_switch->connection_changed_interrupt_gpio);

	if (instance->current_switch->charger_detect_gpio != 0xFFFF) {
		if (gpio_request(instance->current_switch->charger_detect_gpio, "usb_charger_detect"))
			printk(KERN_INFO "\n %s:gpio_request failed\n ", __FUNCTION__);
		else
			instance->charge_detect_gpio = instance->current_switch->charger_detect_gpio;
	}
	write_FSA9480_register(instance, FSA9490_INTERRUPT_MASK_1_REGISTER, 0x00);
	write_FSA9480_register(instance, FSA9490_INTERRUPT_MASK_2_REGISTER, 0x27);

//	read_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, &c);	/*If control register has interrupt enabled*/
//	c &= (~FSA9490_INTERRUPT_ENABLE_BIT);				/* then we have just rebooted*/
//	write_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, c);	/* compared to the Fairchild	*/
	write_FSA9480_register(instance, FSA9490_CONTROL_REGISTER, 0x1E);

	instance->connection_change_gpio = instance->current_switch->connection_changed_interrupt_gpio;
	instance->irq_bit = gpio_to_irq(instance->current_switch->connection_changed_interrupt_gpio);
	instance->prev_event = instance->last_event = current_connection_mask(instance);
	printk(KERN_INFO "FSA initial event = 0x%08lx\n", instance->last_event);

	instance->dev = device_create(usb_switch_class, NULL, 0, instance, "%s", "FSA_SWITCH");
	for (i = 0; i < ARRAY_SIZE(FSA9480_device_attrs); i++) {
			ret = device_create_file(instance->dev, &FSA9480_device_attrs[i]);
			if (ret < 0)
				printk(KERN_INFO "device_create_file failed for file %s error =%d\n", FSA9480_device_attrs[i].attr.name, ret);
	}

	micro_usb_switch = device_create(sec_class, NULL, 0, NULL, "switch");
	if (IS_ERR(micro_usb_switch)) {
		printk(KERN_ERR "Failed to create device(sec_switch)!\n");
	}

	if (device_create_file(micro_usb_switch, &dev_attr_usb_state) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_usb_state.attr.name);

	blocking_notifier_call_chain(&usb_switch_notifier, USB_SWITCH_DRIVER_STARTED|instance->last_event, NULL);
	instance->started = 1;

	switch_dock_init();
	gpio_request(200, NULL);
	i2c_set_clientdata(client, instance);
#if defined(FSA_DELAYED_WORK)
	ret = request_threaded_irq(instance->irq_bit, NULL, FSA9480_irq_thread_fn,
			IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND | IRQF_ONESHOT,
			instance->current_switch->name, instance);
#else
	ret = request_threaded_irq(instance->irq_bit, FSA9480_irq_handler, FSA9480_irq_thread_fn,
			IRQF_NO_SUSPEND | IRQF_SHARED,
			instance->current_switch->name, instance);
#endif

	if (ret < 0)
		printk(KERN_INFO "Failed to request IRQ %d (err: %d).\n", instance->irq_bit, ret);

	return 0 ;
}

#if defined(CONFIG_PM)
static int FSA9480_i2c_suspend(struct device *dev)
{
	return 0;
}

static int FSA9480_i2c_resume(struct device *dev)
{
	return 0;
}
#endif

static int __devexit FSA9480_i2c_remove(struct i2c_client *client)
{
	struct FSA9480_instance *instance = i2c_get_clientdata(client);

	printk(KERN_INFO "\n -----------%s:..........\n ", __FUNCTION__);

	if (instance && instance->proc_entry)
		remove_proc_entry("MUSB", NULL);

	free_irq(instance->irq_bit, instance);
	device_unregister(instance->dev);

	if (instance->charge_detect_gpio)
		gpio_free(instance->charge_detect_gpio);

	return 0;
}

static int __devinit FSA9480_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	usb_switch_class = class_create(THIS_MODULE, "usb_switch");
	usb_switch_workqueue = create_singlethread_workqueue("usb_switch");

	init_driver_instance(&driver_instance, client);

	return 0;
}

#if defined(CONFIG_PM)
static const struct dev_pm_ops  FSA9480_pm_ops = {
	.suspend = FSA9480_i2c_suspend,
	.resume  = FSA9480_i2c_resume,
};
#endif

static struct i2c_device_id FSA9480_i2c_idtable[] = {
	{ "musb", 0 },
	{ }
};

#define FSA9480_I2C_DEVICE_NAME "FSA9480"

MODULE_DEVICE_TABLE(i2c, FSA9480_i2c_idtable);

static struct i2c_driver FSA9480_i2c_driver = {
	.driver = {
		/* This should be the same as the module name */
		.name = FSA9480_I2C_DEVICE_NAME,
		.owner = THIS_MODULE,
#if defined(CONFIG_PM)
		.pm = &FSA9480_pm_ops,
#endif
	},
	.id_table = FSA9480_i2c_idtable,
	.probe    = FSA9480_i2c_probe,
	.remove   = FSA9480_i2c_remove,
};

static int __init FSA9480_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&FSA9480_i2c_driver);
	if (ret < 0) {
		printk(KERN_INFO "%s  failed to register\n ", __FUNCTION__);
		return ret;
	}

	return 0;
}

static void __exit FSA9480_module_exit(void)
{
	class_destroy(usb_switch_class);
	i2c_del_driver(&FSA9480_i2c_driver);
}

module_init(FSA9480_module_init);
module_exit(FSA9480_module_exit);

MODULE_AUTHOR("Andrew Roca ");
MODULE_DESCRIPTION("Driver for FSA9480 USB source switch");
MODULE_LICENSE("GPL");
