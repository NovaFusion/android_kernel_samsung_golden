/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Etienne CARRIERE <etienne.carriere@stericsson.com>
 * Author: Virupax Sadashivpetimath <virupax.sadashivpetimath@stericsson.com>
 * Author: Naga Radhesh Y <naga.radheshy@stericsson.com>
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mfd/abx500.h>
#include <linux/hrtimer.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/input/ab8505_micro_usb_iddet.h>
#include <sound/jack.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/ab8500-otg.h>
#include <linux/switch.h>
#include <linux/mfd/ab8500.h>

/* should NOT be there... */
#include <linux/syscalls.h>

#define KEY_DEBOUCE_TIME_12MS		0x00
#define KEY_DEBOUCE_TIME_25MS		0x01
#define KEY_DEBOUCE_TIME_37MS		0x02
#define KEY_DEBOUCE_TIME_50MS		0x03
#define KEY_DEBOUCE_TIME_62MS		0x04
#define KEY_DEBOUCE_TIME_75MS		0x05
#define KEY_DEBOUCE_TIME_87MS		0x06
#define KEY_DEBOUCE_TIME_100MS		0x07
#define KEY_DEBOUCE_TIME_0MS		0x08

#define KEY_PRESS_TIME_100MS		0x00
#define KEY_PRESS_TIME_200MS		0x01
#define KEY_PRESS_TIME_300MS		0x02
#define KEY_PRESS_TIME_400MS		0x03
#define KEY_PRESS_TIME_500MS		0x04
#define KEY_PRESS_TIME_600MS		0x05
#define KEY_PRESS_TIME_700MS		0x06
#define KEY_PRESS_TIME_800MS		0x07
#define KEY_PRESS_TIME_900MS		0x08
#define KEY_PRESS_TIME_1000MS		0x09
#define KEY_PRESS_TIME_1100MS		0x0a
#define KEY_PRESS_TIME_1200MS		0x0b
#define KEY_PRESS_TIME_1300MS		0x0c
#define KEY_PRESS_TIME_1400MS		0x0d
#define KEY_PRESS_TIME_1500MS		0x0e

#define IDHOSTENA			0x02
#define IDDEVENA			0x01
#define IDDETSWCTRLENA			0x80
#define IDDETPU1ENA			0x20
#define PLUGDETCOMPENA			0x08
#define IDDETPLUGDETCOMP		0x08
#define IDDETPU200K18VENA		0x40
#define IDDETADCENA			0x01
#define ITSOURCE2			0x01
#define VUSBENA				0x01
#define VBUSDET				0x80
#define VBUSVALIDENA			0x20
#define USBSWCTRL			0x04
#define USBCPMICSUBCLKENA		0x08
#define USBHOSTMODEENA			0x01
#define REGIDDETKEYDEGLITCH		0xAF
#define REGIDDETKEYTIMER1		0xB0
#define IDDETKEYDEGLITCH		0x0F
#define IDDETKEYPRESSTIME		0xF0
#define IDDETKEYLONGTIME		0x0F
#define ENUHSR				0x01
#define ENUHSL				0x02
#define USBMICSEL			0x30
#define ENCKLOL				0x80
#define ENCKLOR				0x40
#define ENCKLOLDM			0x20
#define ENCKLOLDP			0x10
#define ROUTE_CARKIT_HEADSET		(ENCKLOL | ENCKLOR | ENCKLOLDM |\
		ENCKLOLDP)
#define VUSBCTRL			0x82
#define USBOTGCTRL			0x87
#define REGIDDETCTRL1			0xA0
#define REGIDDETCTRL2			0xA1
#define REGIDDETCTRL3			0xA2
#define REGIDDETSTATE			0xA7
#define REGIDDETKEYLEVEL		0xA4
#define MISSKEYPRESS			0xA5
#define REGIDDETKEYTIMER2		0xB1
#define REGIDDETKEYTIMER3		0xB2
#define USBLINK1STATUS			0x94
#define USBLINECTRL1			0x81
#define USBPHYCTRL			0x8A
#define USBDRVCTRL			0x74
#define USBGAINMICSEL			0x75
#define DAPATHCONF			0x09
#define ENDACHSL			0x20
#define ENDACHSR			0x10
#define ENABLE				0x01
#define DISABLE				0x00
#define AB8505_SUPPLY_CONTROL		0x03
#define LINK1_STATUS_MASK		0xF8
#define REGIDDETVTH			0xA6
#define IDDETFMDETCOMPHIENA		0x01
#define IDDETFMDETLOENA			0x02
#define IDDETFMDETREFPUENA		0x04
#define IDDETPU200KVISENA		0x80
#define FMCOMPENA			(IDDETFMDETCOMPHIENA | IDDETFMDETLOENA\
		| IDDETFMDETREFPUENA | IDDETPU200KVISENA)
#define IDDETVTHDET619KENA		0x40
#define IDDETVTHDET523KENA		0x20
#define IDDETVTHDET440KENA		0x10
#define IDDETVTHDET301KENA		0x08
#define IDDETVTHDET255KENA		0x04
#define IDDETVTHDET200KENA		0x02
#define IDDETFMDETCOMPHI		0x40
#define IDDETFMDETCOMPLO		0x20
#define IDDETFMCOMPSTAT			(IDDETFMDETCOMPHI | IDDETFMDETCOMPLO)
#define SERVICESWCTRLENA		0x80
#define SERVICEFORCEHZENA		0x40
#define USBHSGAIN			0x73
#define VUSBFLP				0x02

#define IDDETKEYCOMPPENA		0x01
#define KEYPADSINGLECOMPMODE		0x02
#define IDDETKEYRDREQ			0x08
#define REGIDDETKEYTIMER3_MASK		0x03
#define IDDETPU10K1V8ENA		0x08
#define IDDETPU10KVADCENA		0x10
#define IDDETPU1UENA			0x20
#define IDDETKEYCOMPNENA		0x02
#define IDDETKEYNLEVEL			0x0F
#define IDDETKEYPLEVEL			0xF0
#define PLEVEL_500MV			0x30
#define NLEVEL_200MV			0x01
#define KEYPADENAFLANK			0x01
#define IDDETKEYNEGPOSON		0x04
#define IDPU10KSWENA			0x08
#define USBLINECTRL2			0x82
#define USBCHARGDETENA			0x01
#define TURNONSTATUS			0x00

#define CTRLUSBUICCPUD			0x54

#define DETECTION_INTERVAL_MS		500
#define BTN_DETECTION_RESET_INTERVAL_MS	800
#define UART_BOOT_VAL			0x80
#define USB_BOOTOFF_VAL			0xB8
#define USB_BOOTON_VAL			0xC0
#define UART_BOOTOFF_VAL		0xC8
#define UART_BOOTON_VAL			0xD0
#define COUNT_GPADC_FAIL_TRY		5


#define GPIO_ENABLE 0
#define GPIO_DISABLE 1
#define USB_LINK_STD_HOST	1
#define USB_LINK_CDP	4
#define USB_LINK_ACA_RID	8
#define USB_LINK_HM_IDGND	13
#define CURRENT_LIMIT_CKT_TYPE1		650
#define CURRENT_LIMIT_CKT_TYPE2		950
#define CURRENT_LIMIT_DESKTOP_DOCK     950
#define CURRENT_LIMIT_LEGACY_CHARGER	1500
#define UART_BOOT_OFF_MIN	445
#define UART_BOOT_ON_MAX	800
#define HEADSET_MAX		1158

extern void set_android_switch_state(int state);

enum irq_type_e {
	CABLE_PLUG,
	CABLE_UNPLUG,
	LINK_STATUS,
	VBUS_R,
	VBUS_F,
};

struct irq_types {
	char *irq_name;
	irq_handler_t irq_fn;
	int irq;
	bool state;
};

struct key_interrupt_types {
	char *name;
	irqreturn_t (*function) (int, void*);
	int irq;
};
static int (*handle_gpio[])(void) = {
	[GPIO_ENABLE] = NULL,
	[GPIO_DISABLE] = NULL,
};

static int accessory_claim_irq(int irq_id,
			struct usb_accessory_state *accessory);
static int accessory_release_irq(int irq_id,
			struct usb_accessory_state *accessory);
static int (*acessory_func_list[])(struct usb_accessory_state *, bool);
static int init_button_press_detection(struct usb_accessory_state *,
							int connected);
static irqreturn_t micro_usb_accessory_plug(int irq, void *data);
static irqreturn_t micro_usb_accessory_unplug(int irq, void *data);
static irqreturn_t link_status_irq_handler(int irq, void *data);
static irqreturn_t vbus_irq_handler(int irq, void *data);
static irqreturn_t vbus_fall_irq_handler(int irq, void *data);

static struct key_interrupt_types key_press_interrupts[];
struct workqueue_struct *btn_detection_work_queue;
static struct input_dev *usb_button;

static BLOCKING_NOTIFIER_HEAD(micro_usb_notifier_list);

/* For notification to usb and charger driver */
struct blocking_notifier_head micro_usblinkstatus_notifier_usb =
BLOCKING_NOTIFIER_INIT(micro_usblinkstatus_notifier_usb);

static int key_glitch_time =  KEY_DEBOUCE_TIME_50MS;
static int key_press_time = KEY_PRESS_TIME_500MS;
static int key_log_press_time = KEY_PRESS_TIME_1000MS;

int micro_usb_register_usb_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&micro_usblinkstatus_notifier_usb, nb);
}
EXPORT_SYMBOL(micro_usb_register_usb_notifier);

int micro_usb_unregister_usb_notifier(struct notifier_block *nb)
{
		return blocking_notifier_chain_unregister(&micro_usblinkstatus_notifier_usb, nb);
}
EXPORT_SYMBOL(micro_usb_unregister_usb_notifier);

static struct irq_types plug_unplug_irqs[] = {
	{
		"ID_DET_PLUGR",
		micro_usb_accessory_plug,
	},
	{
		"ID_DET_PLUGF",
		micro_usb_accessory_unplug,
	},
	{
		"USB_LINK_STATUS",
		link_status_irq_handler,
	},
	{
		"VBUS_DET_R",
		vbus_irq_handler,
	},
	{
		"VBUS_DET_F",
		vbus_fall_irq_handler,
	},
};

/* Samsung Specific */
extern struct class *sec_class;
static struct class *usb_switch_class;

static struct device *micro_usb_switch;
static struct device *fsa_temp;

extern int jig_smd;

#if defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_SEC_KYLE) || defined(CONFIG_MACH_CODINA)
extern int use_ab8505_iddet;
#endif

static struct switch_dev switch_dock = {
	.name = "dock",
};

static void switch_dock_init(void)
{
	int ret;

	ret = switch_dev_register(&switch_dock);
	if (ret < 0)
		printk(KERN_INFO "Failed to register dock switch\n");
}

/* End of Samsung Specific */

static int set_pullup_comparator(struct usb_accessory_state *accessory,
				char enable)
{
	struct device *dev = accessory->dev;
	int ret;

	/* 1microAmp current source pull up enable/disable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA,
				enable ? IDDETPU1ENA : 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}
	/* Enable/Disable ID detect comparator */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL1, PLUGDETCOMPENA,
			enable ? PLUGDETCOMPENA : 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	if (enable)
		mdelay(2); /* temporary delay by jaehoon */

	return 0;
}

static int get_ctrl_from_fsmcharger(struct usb_accessory_state *accessory)
{
	struct device *dev = accessory->dev;
	int ret;

	/* Clear the IDHostEna, IDDevEna register content */
	ret = abx500_set(dev, AB8505_USB, USBOTGCTRL, 0x20);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__,
				__LINE__);
		return ret;
	}
	/* 1microAmp current source pull up and IDdet comp enable */
	ret = set_pullup_comparator(accessory, true);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}
	return 0;
}

static int give_ctrl_to_fsmcharger(struct usb_accessory_state *accessory)
{
	struct device *dev = accessory->dev;
	int ret;

	/* 1microAmp current source pull up and IDdet comp disable */
	ret = set_pullup_comparator(accessory, false);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Set iddet IP HW controllable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL3, IDDETSWCTRLENA, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Restore the register back */
	ret = abx500_mask_and_set(dev, AB8505_USB,
			USBOTGCTRL, IDHOSTENA | IDDEVENA |
			VBUSVALIDENA, VBUSVALIDENA | IDHOSTENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	return 0;
}

int micro_usb_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&micro_usb_notifier_list, nb);
}
EXPORT_SYMBOL(micro_usb_register_notifier);

int micro_usb_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&micro_usb_notifier_list, nb);
}
EXPORT_SYMBOL(micro_usb_unregister_notifier);

void get_button_adc_val(struct kthread_work *work)
{
	int ret;
	int id_voltage;
	struct usb_accessory_state *accessory
		= container_of(work, struct usb_accessory_state,
				detect_button_work);
	struct device *dev = accessory->dev;
	struct button_param_list *tmp = accessory->btn_param_list;

	/* IDPU10KSWENA Ena */
	ret = abx500_mask_and_set(dev, AB8505_SYS_CTRL, CTRLUSBUICCPUD,
			IDPU10KSWENA, IDPU10KSWENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}


	id_voltage = ab8500_gpadc_convert(accessory->gpadc, USB_ID);
	if (id_voltage < 0) {
		dev_err(dev, "GPADC read failed %d\n", id_voltage);
		return;
	}

	/* IDPU10KSWENA Dis */
	ret = abx500_mask_and_set(dev, AB8505_SYS_CTRL, CTRLUSBUICCPUD,
			IDPU10KSWENA, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}

	dev_info(dev, "GPADC USB_ID read %d\n", id_voltage);

	while (tmp->vmax != 0) {
		if ((id_voltage <= tmp->vmax) && (id_voltage >= tmp->vmin)) {
			dev_info(dev, "Button ID %d name %s\n", tmp->btn_id,
					tmp->btn_name);
			break;
		}
		tmp++;
	}
}

static irqreturn_t key_glitch_interrupt_handler(int irq, void *data)
{
	int ret;
	struct usb_accessory_state *accessory = data;
	struct device *dev = accessory->dev;

	input_report_key(usb_button, KEY_MEDIA, 1);
	input_sync(usb_button);
	input_report_key(usb_button, KEY_MEDIA, 0);
	input_sync(usb_button);

	/* IdDetKeyCompPEna Ena */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYCOMPPENA, IDDETKEYCOMPPENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto handled;
	}

	/* KeypadSingleCompMode Dis */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL3,
			KEYPADSINGLECOMPMODE, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto handled;
	}

	queue_kthread_work(&accessory->kworker, &accessory->detect_button_work);

handled:
	return IRQ_HANDLED;
}

static irqreturn_t key_press_interrupt_handler(int irq, void *data)
{
	int ret;
	struct usb_accessory_state *accessory = data;
	struct device *dev = accessory->dev;
	u8 tmp1, tmp2;
	u16 iddetkeytime;

	/* IdDetKeyRdReq Ena */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYRDREQ, IDDETKEYRDREQ);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto handled;
	}

	/* Read IdDetKeyTime[7:0] */
	ret = abx500_get(dev, AB8505_CHARGER, REGIDDETKEYTIMER2, &tmp1);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		goto handled;
	}

	/* Read IdDetKeyTime[9:8] */
	ret = abx500_get(dev, AB8505_CHARGER, REGIDDETKEYTIMER3, &tmp2);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		goto handled;
	}

	/* IdDetKeyRdReq Dis */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYRDREQ, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto handled;
	}

	/* IdDetKeyTime[7:0] | IdDetKeyTime[9:8] */
	iddetkeytime = tmp1 | ((tmp2 & REGIDDETKEYTIMER3_MASK) << 8);

	dev_info(dev, "Key Press Time read (%d * 12.5)ms\n", iddetkeytime);
handled:
	return IRQ_HANDLED;
}

/*TODO: Task needs to be defined for the below 3 handlers */
static irqreturn_t long_key_press_interrupt_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

static irqreturn_t ikr_interrupt_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

static irqreturn_t key_stuck_interrupt_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

static struct key_interrupt_types key_press_interrupts[] = {
	{"KeyDeglitch", &key_glitch_interrupt_handler},
	{"KP", &key_press_interrupt_handler},
	{"IKP", &long_key_press_interrupt_handler},
	{"IKR", &ikr_interrupt_handler},
	{"KeyStuck", &key_stuck_interrupt_handler},
};

static int key_press_interrupt(bool connected)
{
	int i;

	for (i = 0; i < sizeof(key_press_interrupts)/
			sizeof(key_press_interrupts[0]); i++) {
		if (connected)
			enable_irq(key_press_interrupts[i].irq);
		else
			disable_irq(key_press_interrupts[i].irq);
	}

	return 0;
};

static int phone_powered_device(struct usb_accessory_state *accessory, bool connected)
{
	struct device *dev = accessory->dev;
	int ret;
	struct otg_transceiver *otg_trxiver;
	int value;

	dev_info(dev, "PPD %s\n", connected ? "PLUGGED" : "UNPLUGGED");

	/* Run Routine required for UART unPlug event */
	if (!connected)
		acessory_func_list[USBSWITCH_UART](accessory, connected);

	/* Enable sysclk and per5clk */
	clk_enable(accessory->micusb_ph5clk);
	clk_enable(accessory->micusb_sysclk);

	/* Enable GPIO correspondingto ULPI to transfer the data */
	handle_gpio[GPIO_ENABLE]();

	/* Notify to usb driver to Enable PHY register*/
	blocking_notifier_call_chain(&micro_usb_switch_notifier,
			USB_PHY_ENABLE, NULL);

	/* Get the USB PHY transceiver to write to upli registers */
	otg_trxiver = otg_get_transceiver();

	/* Enable/Disable CarkitPwr bit in Carkit control register */
	value = otg_io_read(otg_trxiver, ULPI_CARCIT_CTRL);
	if (value < 0) {
		dev_err(dev, "%s USBCarkit read failed \n", __func__);
		ret = value;
		goto failed;
	}
	if (connected)
		value = value | 0x01;
	else
		value = value & 0xFE;

	ret = otg_io_write(otg_trxiver, ULPI_CARCIT_CTRL, value);
	if (ret < 0) {
		dev_err(dev, "%s USBCarkit write failed \n", __func__);
		goto failed;
	}

	/* Put the USB PHY handler */
	otg_put_transceiver(otg_trxiver);

	/* Notify to usb driver to Disable PHY register*/
	blocking_notifier_call_chain(&micro_usb_switch_notifier,
			USB_PHY_DISABLE, NULL);

	/* Run Routine required for UART Plug event */
	if (connected)
		acessory_func_list[USBSWITCH_UART](accessory, connected);
failed:
	/* Disable GPIO corresponding to ULPI */
	handle_gpio[GPIO_DISABLE]();

	/* Disable the clocks */
	clk_disable(accessory->micusb_sysclk);
	clk_disable(accessory->micusb_ph5clk);

	return ret;
}

static int uart_boot_off(struct usb_accessory_state *accessory, bool connected)
{
	struct device *dev = accessory->dev;

	dev_info(dev, "UART Boot-OFF %s\n",
			connected ? "PLUGGED" : "UNPLUGGED");
	acessory_func_list[USBSWITCH_UART](accessory, connected);

	return 0;
}

static int uart_boot_on(struct usb_accessory_state *accessory, bool connected)
{
	struct device *dev = accessory->dev;

	dev_info(dev, "UART Boot-ON %s\n", connected ? "PLUGGED" : "UNPLUGGED");
	acessory_func_list[USBSWITCH_UART](accessory, connected);

	return 0;
}

static int usb_boot_on(struct usb_accessory_state *accessory, bool connected)
{
	int ret;
	struct device *dev = accessory->dev;

	dev_info(dev, "USB Boot-ON %s\n", connected ? "PLUGGED" : "UNPLUGGED");

	/* Service controlled by i2c */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL4, SERVICESWCTRLENA,
				connected ? SERVICESWCTRLENA : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Service is forced Hig hZ */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL4, SERVICEFORCEHZENA,
				connected ? SERVICEFORCEHZENA : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	blocking_notifier_call_chain(&micro_usb_notifier_list,
			connected ? USB_BOOT_ON_PLUGGED :
			USB_BOOT_ON_UNPLUGGED, NULL);

	return ret;
}

static int usb_boot_off(struct usb_accessory_state *accessory, bool connected)
{
	int ret;
	struct device *dev = accessory->dev;

	dev_info(dev, "USB Boot-OFF %s\n", connected ? "PLUGGED" : "UNPLUGGED");

	/* Service controlled by i2c */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL4, SERVICESWCTRLENA,
				connected ? SERVICESWCTRLENA : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Service is forced Hig hZ */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL4, SERVICEFORCEHZENA,
				0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	blocking_notifier_call_chain(&micro_usb_notifier_list,
			connected ? USB_BOOT_OFF_PLUGGED :
			USB_BOOT_OFF_UNPLUGGED, NULL);

	return ret;
}

static int tty_converter(struct usb_accessory_state *accessory, bool connected)
{
	int ret;
	struct device *dev = accessory->dev;
	static unsigned char usbhsgain;
	static unsigned char eargainmicsel;

	dev_info(dev, "TTY Device %s\n",  connected ? "PLUGGED" : "UNPLUGGED");

	/* Vbus OVP external switch force opened */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL3, USBSWCTRL,
				connected ? USBSWCTRL : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	if (connected) {
		/* Take backup and Set Left,Right Usb Headset analog gain */
		ret = abx500_get(dev, AB8505_AUDIO, USBHSGAIN, &usbhsgain);
		if (ret < 0) {
			dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
			return ret;
		}

		ret = abx500_set(dev, AB8505_AUDIO, USBHSGAIN, 0x0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	} else {
		ret = abx500_set(dev, AB8505_AUDIO, USBHSGAIN, usbhsgain);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	}

	/* Enable Left and Right USB Headset */
	ret = abx500_set(dev, AB8505_AUDIO, USBDRVCTRL,
			connected ? ENUHSR | ENUHSL : 0xC);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	if (connected) {
		ret = abx500_get(dev, AB8505_AUDIO, USBGAINMICSEL,
				&eargainmicsel);
		if (ret < 0) {
			dev_err(dev, "%s get failed %d\n", __func__, __LINE__);
			return ret;
		}

		/* DP ball is selected as usb microphone input */
		ret = abx500_mask_and_set(dev, AB8505_AUDIO,
				USBGAINMICSEL, USBMICSEL, 0x20);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	} else {
		ret = abx500_set(dev, AB8505_AUDIO, USBGAINMICSEL,
				eargainmicsel);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	}

	/* Enable Vusb */
	ret = abx500_mask_and_set(dev, AB8505_SUPPLY_CONTROL,
				VUSBCTRL, VUSBENA | VUSBFLP,
				connected ? VUSBENA | VUSBFLP : 0x0);
	if (ret < 0)
		dev_err(dev, "%s write failed %d\n", __func__,
				__LINE__);

	return ret;
}

static int init_button_press_detection(struct usb_accessory_state *accessory,
								int connected)
{
	int ret;
	struct device *dev = accessory->dev;

	/* IDDetPu10k1v8Ena Ena & IDDetPu10kVadcEna Dis & IDDetPu1uEna Ena */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL2,
			IDDETPU10K1V8ENA | IDDETPU10KVADCENA | IDDETPU1UENA,
			connected ? IDDETPU1UENA | IDDETPU10K1V8ENA : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* IDPU10KSWENA Dis */
	ret = abx500_mask_and_set(dev, AB8505_SYS_CTRL, CTRLUSBUICCPUD,
			IDPU10KSWENA, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}
	/*
	 * REGIDDETCTRL1 should not be changed incase of cut2
	 * as same bit is used to enable the interrupt.
	 */
	if (is_ab8505_2p0_earlier(accessory->parent) ||
		is_ab9540_2p0_or_earlier(accessory->parent)) {
		/* IDDetPlugDetCompEna Ena */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL1,
				PLUGDETCOMPENA, connected ?
				PLUGDETCOMPENA : 0x0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
						__func__, __LINE__);
			return ret;
		}
	}
	/* IDDetKeyCompNEna Ena & IDDetKeyCompPEna Dis */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYCOMPNENA | IDDETKEYCOMPPENA, connected ?
			IDDETKEYCOMPNENA : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* IdDetKeyNlevel[3:0] &  IdDetKeyPlevel[7:4] */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETKEYLEVEL,
			IDDETKEYNLEVEL | IDDETKEYPLEVEL, connected ?
			PLEVEL_500MV | NLEVEL_200MV : 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* IdDetKeyPressTime[7:4] */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETKEYTIMER1, IDDETKEYPRESSTIME,
				key_press_time << 4);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* IdDetKeyLongTime[3:0] */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETKEYTIMER1, IDDETKEYLONGTIME,
				key_log_press_time);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* set key glitch time */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETKEYDEGLITCH, IDDETKEYDEGLITCH,
				key_glitch_time);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* KeypadSingleCompMode Ena & KeypadEnaFlank Dis */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL3,
			KEYPADSINGLECOMPMODE | KEYPADENAFLANK,
			KEYPADSINGLECOMPMODE);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* IdDetKeyNegPosOn Dis */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYNEGPOSON, 0x0);
	if (ret < 0)
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
	return ret;
}

static void button_detection_function(struct work_struct *work)
{
	int ret;
	struct usb_accessory_state *accessory = container_of(work,
		struct usb_accessory_state, detect_button.work);
	struct device *dev = accessory->dev;

	/* IdDetKeyNegPosOn Dis */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYNEGPOSON, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto btn_det_restart;
	}

	/* IDDetPu10k1v8Ena Dis Here */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL2,
			IDDETPU10K1V8ENA, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto btn_det_restart;
	}

	/*
	 * Incase of cut2 headset plug/unplug is handled through interrupts
	 * so no need of following lines.They are required only for cut1.
	 */
	if (is_ab8505_2p0_earlier(accessory->parent) ||
		is_ab9540_2p0_or_earlier(accessory->parent)) {
		u8 id_voltage;
		/* read Iddet comparator state */
		ret = abx500_get(dev, AB8505_CHARGER,
					REGIDDETSTATE, &id_voltage);
		if (ret < 0) {
			dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
			goto btn_det_restart;
		}
		/* Check if the cable was UnPluged */
		if (!(id_voltage & IDDETPLUGDETCOMP)) {
			mutex_lock(&accessory->usb_otg_ctrl_lock);
			if (accessory->usb_otg_ctrl) {
				/* Restore the register back */
				ret = abx500_mask_and_set(dev, AB8505_USB,
					USBOTGCTRL, IDHOSTENA | IDDEVENA |
					VBUSVALIDENA, accessory->usb_otg_ctrl);
				if (ret < 0) {
					dev_err(dev, "%s write failed %d\n",
							__func__, __LINE__);
				}
				accessory->usb_otg_ctrl = 0;
			}
			mutex_unlock(&accessory->usb_otg_ctrl_lock);

			if (accessory->cable_last_detected != USBSWITCH_USBHOST &&
				accessory->cable_last_detected != USB_CABLE)
				acessory_func_list[accessory->cable_last_detected]
					(accessory, false);

			accessory->cable_last_detected = USBSWITCH_NONE;

			queue_delayed_work(accessory->iddet_workqueue,
					&accessory->cable_detection,
				msecs_to_jiffies(DETECTION_INTERVAL_MS));
			return;
		}
	}
	/*TODO:ForABcut2: IDDetPu10k1v8Ena Ena Here */

	/* IdDetKeyNegPosOn Ena */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
			IDDETKEYNEGPOSON, IDDETKEYNEGPOSON);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto btn_det_restart;
	}

btn_det_restart:
	queue_delayed_work(btn_detection_work_queue, &accessory->detect_button,
			msecs_to_jiffies(BTN_DETECTION_RESET_INTERVAL_MS));
}

static int enable_headphone_mic(struct usb_accessory_state *accessory, bool connected)
{
	struct device *dev = accessory->dev;
	static unsigned char eargainmicsel;
	int ret;
	if (accessory->cable_last_detected != USBSWITCH_DESKTOP_DOCK) {
	/* Vbus OVP external switch force opened */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL3, USBSWCTRL | USBCPMICSUBCLKENA,
			connected ? USBSWCTRL | USBCPMICSUBCLKENA : 0x0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
		}
	}
	/* Enable Left and Right USB Headset */
	ret = abx500_set(dev, AB8505_AUDIO, USBDRVCTRL,
				connected ? ENUHSR | ENUHSL : 0xC);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	if (connected) {
		ret = abx500_get(dev, AB8505_AUDIO,
				USBGAINMICSEL, &eargainmicsel);
		if (ret < 0) {
			dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
			return ret;
		}

		/* DP ball is selected as usb microphone input */
		ret = abx500_mask_and_set(dev, AB8505_AUDIO,
				USBGAINMICSEL, USBMICSEL, 0x20);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	} else {
		ret = abx500_set(dev, AB8505_AUDIO,
				USBGAINMICSEL, eargainmicsel);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	}
	if (accessory->cable_last_detected != USBSWITCH_DESKTOP_DOCK) {
	/* Enable VusbEna and VusbFlp */
	ret = abx500_mask_and_set(dev, AB8505_SUPPLY_CONTROL,
				VUSBCTRL, VUSBENA | VUSBFLP,
				connected ? VUSBENA | VUSBFLP : 0x0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			return ret;
		}
	}
	return ret;
}

static int initialize_headset(struct usb_accessory_state *accessory,
		bool connected)
{
	struct device *dev = accessory->dev;
	int ret;

	ret = enable_headphone_mic(accessory, connected);
	if (ret < 0) {
		dev_err(dev, "%s write to Headphone and mic failed %d\n",
							 __func__, __LINE__);
		return ret;
	}

	if (accessory->cable_last_detected != USBSWITCH_DESKTOP_DOCK) {
		init_button_press_detection(accessory, connected);

		key_press_interrupt(connected);

		/* IdDetKeyNegPosOn Ena */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER, MISSKEYPRESS,
				IDDETKEYNEGPOSON, connected ? IDDETKEYNEGPOSON : 0x0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
		}

		if (connected)
			queue_delayed_work(btn_detection_work_queue,
					&accessory->detect_button,
					msecs_to_jiffies
					(BTN_DETECTION_RESET_INTERVAL_MS));
	}
	return ret;
}

static int carkit_dock_dev(struct usb_accessory_state *accessory,
				int type, bool connected)
{
	struct device *dev = accessory->dev;
	int ret;
	unsigned char vbusdet;
	unsigned int current_limit = 0;

	/* Current limit is in milli amp*/
	if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE1)
		current_limit = (connected ? CURRENT_LIMIT_CKT_TYPE1 : 0);
	else if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE2)
		current_limit = (connected ? CURRENT_LIMIT_CKT_TYPE2 : 0);
	else if (accessory->cable_last_detected == USBSWITCH_DESKTOP_DOCK)
		current_limit = (connected ? CURRENT_LIMIT_DESKTOP_DOCK : 0);
	if (connected) {
		/* Set pulldown */
		ret = abx500_mask_and_set(dev,
					  AB8505_USB,
					  USBLINECTRL1,
					  IDHOSTENA|ENCKLOLDP|ENCKLOLDM,
					  ENCKLOLDP|ENCKLOLDM);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
		}

		/*
		 * Enable, kick, disable ab watchdog
		 * Need to be done to enable usb phy
		 */
		ret = abx500_mask_and_set(dev,
					  AB8500_SYS_CTRL2_BLOCK,
					  AB8500_MAIN_WDOG_CTRL_REG,
					  MAIN_WDOG_ENA,
					  MAIN_WDOG_ENA);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		ret = abx500_mask_and_set(dev,
					  AB8500_SYS_CTRL2_BLOCK,
					  AB8500_MAIN_WDOG_CTRL_REG,
					  MAIN_WDOG_ENA|MAIN_WDOG_KICK,
					  MAIN_WDOG_ENA|MAIN_WDOG_KICK);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		ret = abx500_mask_and_set(dev,
					  AB8500_SYS_CTRL2_BLOCK,
					  AB8500_MAIN_WDOG_CTRL_REG,
					  0xF,
					  MAIN_WDOG_DIS);

		/* Enable usb phy */
		ret = abx500_mask_and_set(dev, AB8505_USB, USBPHYCTRL,
					  0x1, 0x1);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
		}

		/* read VbusDet state */
		ret = abx500_get(dev, AB8505_INTERRUPT, ITSOURCE2, &vbusdet);
		if (ret < 0) {
			dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
			return ret;
		}

		vbusdet &= VBUSDET;
		if (vbusdet) {
			/* Notify carkit plug status*/
			blocking_notifier_call_chain(&micro_usb_notifier_list,
					type, &current_limit);
		}
		accessory_claim_irq(VBUS_F, accessory);
	} else {
		/* Notify carkit plug status*/
		blocking_notifier_call_chain(&micro_usb_notifier_list,
				type, &current_limit);
		accessory_release_irq(VBUS_F, accessory);

		/* Disable pull down */
		ret = abx500_mask_and_set(dev,
				AB8505_USB,
				USBLINECTRL1,
				IDHOSTENA|ENCKLOLDP|ENCKLOLDM,
				IDHOSTENA);

		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
		}

		/* Disable usb phy */
		ret = abx500_mask_and_set(dev, AB8505_USB, USBPHYCTRL,
				0x1, 0x0);

		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
		}
	}

	ret = initialize_headset(accessory, connected);
	if (ret < 0) {
		dev_err(dev, "%s Setting Desktop Dock failed %d\n",
				   	__func__, __LINE__);
		return ret;
	}
	return 0;
}
static int carkit_dev_type1(struct usb_accessory_state *accessory,
		bool connected)
{
	struct device *dev = accessory->dev;
	int ret;

	dev_info(dev, "Carkit Type1 %s\n", connected ? "PLUGGED" : "UNPLUGGED");

	ret = carkit_dock_dev(accessory, connected ? CARKIT_TYPE1_PLUGGED : CARKIT_TYPE1_UNPLUGGED, connected);
	if (ret < 0) {
		dev_err(dev, "failed in carkit_dev\n");
		return ret;
	}
	return ret;
}

static int carkit_dev_type2(struct usb_accessory_state *accessory,
		bool connected)
{
	struct device *dev = accessory->dev;
	int ret;

	dev_info(dev, "Carkit Type2 %s\n", connected ? "PLUGGED" : "UNPLUGGED");
	ret = carkit_dock_dev(accessory, connected ? CARKIT_TYPE2_PLUGGED : CARKIT_TYPE1_UNPLUGGED, connected);
	if (ret < 0) {
		dev_err(dev, "failed in carkit_dev\n");
		return ret;
	}
	return ret;
}

static int audio_dev_type1(struct usb_accessory_state *accessory,
		bool connected)
{
	int ret;
	struct device *dev = accessory->dev;

	dev_info(dev, "Audio Device %s\n", connected ? "PLUGGED" : "UNPLUGGED");

	ret = initialize_headset(accessory, connected);
	if (ret < 0) {
		dev_err(dev, "%s write to Headphone and mic failed %d\n",
							 __func__, __LINE__);
		return ret;
	}

	//KSND
	set_android_switch_state(connected);

	return ret;
}

static int desktop_dev_dock(struct usb_accessory_state *accessory,
				bool connected)
{
	struct device *dev = accessory->dev;
	int ret;

	dev_info(dev, "Desktop Dock %s\n", connected ? "PLUGGED" : "UNPLUGGED");

	ret = carkit_dock_dev(accessory, connected ? DESKTOP_DOCK_PLUGGED: DESKTOP_DOCK_UNPLUGGED, connected);
	if (ret < 0) {
		dev_err(dev, "failed in desktop_dock_dev\n");
		return ret;
	}

	switch_set_state(&switch_dock, (int)connected);

	return ret;
}
static int cable_unknown(struct usb_accessory_state *accessory, bool connected)
{
	struct device *dev = accessory->dev;
	int ret;

	dev_warn(dev, "Unknown Cable %s\n", connected ? "PLUGGED" :
			"UNPLUGGED");

	mutex_lock(&accessory->usb_otg_ctrl_lock);
	if (accessory->usb_otg_ctrl) {
		/* Restore the register back */
		ret = abx500_mask_and_set(dev, AB8505_USB,
			USBOTGCTRL, IDHOSTENA | IDDEVENA |
			VBUSVALIDENA, accessory->usb_otg_ctrl);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			mutex_unlock(&accessory->usb_otg_ctrl_lock);
			return ret;
		}

		accessory->usb_otg_ctrl = 0;
	} else {
		/*
		 * USBOTGCTRL should have the IDHOSTENA enabled to detect the
		 * usb cable connected ( Host mode ).
		 */
		ret = abx500_mask_and_set(dev, AB8505_USB,
			USBOTGCTRL, IDHOSTENA | IDDEVENA |
			VBUSVALIDENA, IDHOSTENA);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
			mutex_unlock(&accessory->usb_otg_ctrl_lock);
			return ret;
		}
	}
	mutex_unlock(&accessory->usb_otg_ctrl_lock);
	return 0;
}

static int legacy_charger(struct usb_accessory_state *accessory, bool connected)
{
	struct device *dev = accessory->dev;
	/* Current limit is in milli amp*/
	unsigned int current_limit = CURRENT_LIMIT_LEGACY_CHARGER;

	dev_info(dev, "Legacy charger %s\n", connected ? "PLUGGED" :
			"UNPLUGGED");

	if (connected)
		accessory_claim_irq(VBUS_F, accessory);
	else
		accessory_release_irq(VBUS_F, accessory);

	blocking_notifier_call_chain(&micro_usb_notifier_list,
			connected ? LEGACY_CHARGER_PLUGGED :
			LEGACY_CHARGER_UNPLUGGED, &current_limit);
	return 0;
}

/* Callbacks for the type of cable connected */
static int (*acessory_func_list[])(struct usb_accessory_state *, bool) = {
	[USBSWITCH_UART_BOOT_ON] = uart_boot_on,
	[USBSWITCH_UART_BOOT_OFF] = uart_boot_off,
	[USBSWITCH_USB_BOOT_ON] = usb_boot_on,
	[USBSWITCH_USB_BOOT_OFF] = usb_boot_off,
	[USBSWITCH_AUDIODEV_TYPE1] = audio_dev_type1,
	[USBSWITCH_TTY_CONV] = tty_converter,
	[USBSWITCH_UART] = NULL,
	[USBSWITCH_LEGACY_CHARGER] = legacy_charger,
	[USBSWITCH_CARKIT_TYPE1] = carkit_dev_type1,
	[USBSWITCH_CARKIT_TYPE2] = carkit_dev_type2,
	[USBSWITCH_PPD] = phone_powered_device,
	[USBSWITCH_DESKTOP_DOCK] = desktop_dev_dock,
	[USBSWITCH_UNKNOWN] = cable_unknown,
	[USBSWITCH_USBHOST] = legacy_charger,
};

static  bool read_fm_comparator(struct usb_accessory_state *accessory)
{
	int ret;
	unsigned char id_voltage;
	unsigned char det_vth = 0x40;
	struct device *dev = accessory->dev;

	dev_info(dev, "in %s %d\n", __func__, __LINE__);
	/* enable IDDetFmDetRefPuEna,IDDetFmDetLoEna and IDDetFmDetCompHiEna */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL2,
				FMCOMPENA, FMCOMPENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return false;
	}
	while (det_vth >= 0x20) {
		/* Write required Detect Threshold value to REGIDDETVTH  */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETVTH,
				det_vth, det_vth);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return false;
		}
		msleep(30);
		/* read Iddet comparator state */
		ret = abx500_get(dev, AB8505_CHARGER,
				REGIDDETSTATE, &id_voltage);
		if (id_voltage & 0x20) {
			if (det_vth == 0x40)
				accessory->cable_detected =
					USBSWITCH_UART_BOOT_ON;
			else
				accessory->cable_detected =
					USBSWITCH_UART_BOOT_OFF;
			dev_info(dev, "Matchd %d\n", accessory->cable_detected);
			goto detected;
		}
		dev_info(dev, "FMCOMP-ID %d\n", id_voltage);
		/* reset Detect Threshold value to REGIDDETVTH  */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETVTH,
					det_vth, 0x00);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return false;
		}
		det_vth = det_vth >> 1;
	}

	/* disable IDDetFmDetRefPuEna,IDDetFmDetLoEna and IDDetFmDetCompHiEna */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL2,
				FMCOMPENA, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return false;
	}
	return false;
detected:
	/* reset Detect Threshold value to REGIDDETVTH  */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETVTH,
				det_vth, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n",
				__func__, __LINE__);
		return false;
	}
	/* disable IDDetFmDetRefPuEna,IDDetFmDetLoEna and IDDetFmDetCompHiEna */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER, REGIDDETCTRL2,
				FMCOMPENA, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return false;
	}
	return true;
}


void log_this(u8 pc, char* a, u32 extra1, char* b, u32 extra2);

/*
 * Detect the cable connected depending on the voltage across the
 * ID line.
 */
static int detect_depending_on_id_resistance(struct usb_accessory_state
		*accessory)
{
	int ret;
	struct cust_rid_adcid *p;
	int id_voltage;
	struct device *dev = accessory->dev;
	int count = 0 ;
start:
	/* 1microAmp pull up enable  */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA,
				IDDETPU1ENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Connect ID line to GPADC input */
	ret = abx500_mask_and_set(dev, AB8505_USB,
				USBLINECTRL1, IDDETADCENA, IDDETADCENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Wait for pull up/line to stabilize */
	mdelay(2);

	id_voltage = ab8500_gpadc_convert(accessory->gpadc, USB_ID);
	dev_info(dev, "USB-ID %d\n", id_voltage);
	/*
	 * GPADC value is < 445, so enable 200k pull up and compute gpadc
	 * value again to detect the cable.
	 */
	if (id_voltage < UART_BOOT_OFF_MIN) {
		dev_info(dev, "GPADC value < UART_BOOT_OFF_MIN\n");
		/* 1microAmp pull up disable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA, 0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		/* 200k pull up enable	*/
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
					REGIDDETCTRL2, IDDETPU200K18VENA,
					IDDETPU200K18VENA);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		/* Wait for pull up to stabilize */
		mdelay(2);

		id_voltage = ab8500_gpadc_convert(accessory->gpadc, USB_ID);
		dev_info(dev, "USB-ID %d\n", id_voltage);
		log_this(300, "USB-ID", id_voltage, NULL, 0);

		p = accessory->cables_param_list;
		while (p->max != 0) {
			if ((id_voltage >= p->min) && (id_voltage <= p->max)) {
				dev_info(dev, "Matched %d\n", p->cable_id);
				accessory->cable_detected = p->cable_id;
			break;
			}
			p++;
			accessory->cable_detected = USBSWITCH_UNKNOWN;
		}
		/* 200k pull up disable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
					REGIDDETCTRL2, IDDETPU200K18VENA, 0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		goto detected;		
	}
	/*
	 * If ID voltage is > 445 and < 800,
	 * Here there can be two possibilities cable can be UART or carkit type2
	 * 1.check fm comprator to know whether cable is   UART BOOTON or OFF.
	 * 2.If comparator if false, then cable can be carkit type2.
	 */
	else if ((id_voltage > UART_BOOT_OFF_MIN) && (id_voltage < UART_BOOT_ON_MAX)) {
		/* 1microAmp pull up disable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA, 0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		ret = read_fm_comparator(accessory);
		if (ret)
			goto detected;
		else {
			dev_info(dev, " %s fm comp failed so cable is carkit\n",
					__func__);
			accessory->cable_detected = USBSWITCH_CARKIT_TYPE2;
		}

	}
	/* if cable is > 800 and < 1158 it can be headset*/
	else if (id_voltage < HEADSET_MAX){
		accessory->cable_detected = USBSWITCH_AUDIODEV_TYPE1;
		dev_info(dev, "cable detected is  Headset\n");
		/* 1microAmp pull up disable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA, 0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		goto detected;

	}
	/*
	 * To handle dock detect, Some times we see high GPADC values when dock
	 * is plugged, so try for another GPADC read in this case.
	 */
	else if ((accessory->lsts == 19 || accessory->lsts == 0)) {
		msleep(200);
		ret = abx500_mask_and_set(dev, AB8505_USB, USBLINECTRL2,
				USBCHARGDETENA, 0x00);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
					__func__, __LINE__);
			return;
		}
		msleep(1);
		/* Disable host, device detection and enable VBUS Valid comporator */
		ret = abx500_mask_and_set(dev, AB8505_USB,
				USBOTGCTRL, IDDEVENA, 0x0);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ;
		}
		msleep(1);
		/* Set iddet IP SW controllable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL3, IDDETSWCTRLENA, IDDETSWCTRLENA);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return;
		}
		msleep(1);
		/* */
		ret = abx500_mask_and_set(dev, AB8505_USB,
				USBLINECTRL1, 0x02, 0x02);
		if (ret < 0)
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		msleep(100);
		count++;
		if (count < COUNT_GPADC_FAIL_TRY)
			goto start;
	}

	accessory->cable_detected = USBSWITCH_UNKNOWN;

detected:
	/* Disconnect ID line to GPADC input */
	ret = abx500_mask_and_set(dev, AB8505_USB,
				USBLINECTRL1, IDDETADCENA, 0);
	if (ret < 0)
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);

	ret = abx500_mask_and_set(dev, AB8505_USB,
				USBLINECTRL1, 0x2, 0x2);
	if (ret < 0)
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);

	return ret;
}

/*
 * In case of dock + charger + 3.5mm we are noy getting idplug irq
 * So depend on vbus and linkstatus interrupt.
 */
static void dock_plug_detect(struct work_struct *work)
{

	struct usb_accessory_state *accessory
		= container_of(work, struct usb_accessory_state,
				detect_dock_work);
	int ret;
	struct device *dev = accessory->dev;
	unsigned char count = 4;
	int timeoutval;
	long timeout = msecs_to_jiffies(500);   //700ms
	unsigned char usblink1status;
	unsigned char vbusdet;

	accessory->wait_event_lsts1 = true;
	while (count) {
		/* read VbusDet state */
		ret = abx500_get(dev, AB8505_INTERRUPT, ITSOURCE2, &vbusdet);
		if (ret < 0) {
			dev_err(dev, "%s read failed %d\n", __func__,
					__LINE__);
			return;
		}
		vbusdet &= VBUSDET;
		if (vbusdet) {
			/* wait for linkstatsu interrupt*/
			timeoutval = wait_event_interruptible_timeout(accessory->wait_vbus,
					(!accessory->wait_event_lsts1), timeout);
			if (!timeoutval)
				dev_info(accessory->dev, "%s Link status Time out happend %d\n",
						__func__, __LINE__);
			else
				break;
		}
		else {
			dev_info(dev, "\n no vbus in %s\n",__func__);
			accessory->handle_plug_irq = true;
			return;
		}
		count--;
	}
	accessory->wait_event_lsts1 = false;
	mdelay(50);

	dev_info(dev, "%s going to detect cable connected \n",__func__);
	accessory_release_irq(CABLE_PLUG, accessory);
	accessory_release_irq(CABLE_UNPLUG, accessory);

	get_ctrl_from_fsmcharger(accessory);

	accessory->cable_detected = USBSWITCH_NONE;

	/* Set iddet IP SW controllable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL3, IDDETSWCTRLENA, IDDETSWCTRLENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}

	/* 1microAmp current source pull up disable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL2, IDDETPU1ENA, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}

	ret = detect_depending_on_id_resistance(accessory);
	if (ret < 0) {
		dev_err(dev, "%s detection failed %d\n", __func__, __LINE__);
		return;
	}

	/* 1microAmp current source pull up enable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL2, IDDETPU1ENA, IDDETPU1ENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}

	blocking_notifier_call_chain(&micro_usblinkstatus_notifier_usb,
			accessory->lsts, NULL);
	dev_info(dev, "Cable ID Detected present %d last %d\n",
			accessory->cable_detected,
			accessory->cable_last_detected);

	if (accessory->cable_detected == USBSWITCH_UNKNOWN) {

		accessory->cable_detected = USBSWITCH_NONE;
		dev_info(dev, "\n no cable detected, check if it can be legacy charger\n");
		/* Set iddet IP HW controllable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL3, IDDETSWCTRLENA, 0x00);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return;
		}
		ret = abx500_get(dev, AB8505_USB, USBLINK1STATUS, &usblink1status);
		if (ret < 0) {
			dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
			return;
		}
		if(!usblink1status) {
			/* read VbusDet state */
			ret = abx500_get(dev, AB8505_INTERRUPT, ITSOURCE2, &vbusdet);
			if (ret < 0) {
				dev_err(dev, "%s read failed %d\n", __func__,
						__LINE__);
				return;
			}
			vbusdet &= VBUSDET;
			if (vbusdet){
				accessory->cable_detected = USBSWITCH_LEGACY_CHARGER;
				if (accessory->cable_detected ==
						accessory->cable_last_detected)
					return;

				if (accessory->cable_last_detected != USBSWITCH_NONE &&
					accessory->cable_last_detected != USBSWITCH_USBHOST &&
					accessory->cable_last_detected != USB_CABLE)
					/* Call restore callback for the last connected device */				
					acessory_func_list[accessory->cable_last_detected]
						(accessory, false);
				accessory->cable_last_detected =
					accessory->cable_detected;
				acessory_func_list[accessory->cable_last_detected]
					(accessory, true);
				accessory->handle_plug_irq =  false;
				accessory_claim_irq(CABLE_PLUG, accessory);
			}
		}
		else {
			mdelay(1);
			accessory->handle_plug_irq =  true;
			accessory_claim_irq(CABLE_PLUG, accessory);
		}
		return;
	}

	/* Incase of two plug interrupt without unplug interrupt */
	if (accessory->cable_detected == accessory->cable_last_detected) {
		accessory_claim_irq(CABLE_UNPLUG, accessory);
		return;
	}

	accessory->cable_last_detected = accessory->cable_detected;

	/* Do cable specific plug related work */
	acessory_func_list[accessory->cable_detected](accessory, true);
	accessory_claim_irq(CABLE_UNPLUG, accessory);
}

/*
 * Detect the cable connected depending on the voltage across the
 * ID line.
 */
static void accessory_plug_detect(struct kthread_work *work)
{
	struct usb_accessory_state *accessory
		= container_of(work, struct usb_accessory_state,
				detect_accessory_work);
	int ret;
	struct device *dev = accessory->dev;
	u8 id_voltage;
	unsigned char usblink1status;
	unsigned char count = 3;
	int timeoutval;
	long timeout = msecs_to_jiffies(500);  //700ms
	unsigned char vbusdet;

	give_ctrl_to_fsmcharger(accessory);
	/* wait for linkstatsu interrupt*/
	accessory->wait_event_lsts = true;
	timeoutval = wait_event_interruptible_timeout(accessory->wait,
				(!accessory->wait_event_lsts), timeout);
	if (!timeoutval) {
		dev_info(dev, "%s Link status Time out happend %d\n",
				__func__, __LINE__);

		while (count) {
			/* read VbusDet state */
			ret = abx500_get(dev, AB8505_INTERRUPT, ITSOURCE2, &vbusdet);
			if (ret < 0) {
				dev_err(dev, "%s read failed %d\n", __func__,
						__LINE__);
				return;
			}
			vbusdet &= VBUSDET;
			if (vbusdet) {
				/* wait for linkstatsu interrupt*/
				timeoutval = wait_event_interruptible_timeout(accessory->wait,
						(!accessory->wait_event_lsts), timeout);
				if (!timeoutval)
					dev_info(dev, "%s Link status Time out happend %d\n",
							__func__, __LINE__);
				else
					break;
			}
			else {
				dev_info(dev, "\n no vbus in %s\n",__func__);
				break;
			}
			count--;
		}
	}
	accessory->wait_event_lsts = false;
	/* read VbusDet state */
	ret = abx500_get(dev, AB8505_INTERRUPT, ITSOURCE2, &vbusdet);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__,
				__LINE__);
		return;
	}
	vbusdet &= VBUSDET;
	if (vbusdet) {
		dev_info(dev, " vbus so returning from %s\n",__func__);
		return;
	}

	mutex_lock(&accessory->sync_id_vbus_lock);

	ret = abx500_get(dev, AB8505_USB, USBLINK1STATUS, &usblink1status);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		mutex_unlock(&accessory->sync_id_vbus_lock);
		return;
	}
	/* Skip detection if UsbLink1Status[4:0] is set */
	if (usblink1status != UART_BOOTON_VAL &&
			usblink1status != UART_BOOTOFF_VAL) {
		if (usblink1status & LINK1_STATUS_MASK) {
			dev_info(dev, "%s Returning from IDDet handler %d usblink1status = %x\n",
					__func__, __LINE__,usblink1status);

			accessory->cable_detected = USBSWITCH_USBHOST;
			accessory->cable_last_detected = accessory->cable_detected;
			mutex_unlock(&accessory->sync_id_vbus_lock);

			/* HUGE WORKAROUND - REMOVE IT ASAP! */
			dev_info(dev, "NovaFusion: USB OTG device plugged in");
			int fd = sys_open("/sys/kernel/abb-regu/votg", O_WRONLY, 0);
			if (fd >= 0) {
				dev_info(dev, "NovaFusion: Turning on VOTG interface");
				char c = '1'; //buffer to be written to sysfs
				sys_write(fd, &c, 1);
				sys_close(fd);
			}
			else dev_err(dev, "NovaFusion: Failed to open VOTG interface");

			return;
		}
	}
	mutex_unlock(&accessory->sync_id_vbus_lock);

	get_ctrl_from_fsmcharger(accessory);
	/* Set iddet IP SW controllable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL3, IDDETSWCTRLENA, IDDETSWCTRLENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}
	/* read Iddet comparator state */
	ret = abx500_get(dev, AB8505_CHARGER,
				REGIDDETSTATE, &id_voltage);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		return;
	}

	accessory->cable_detected = USBSWITCH_NONE;

	if (!(id_voltage & IDDETPLUGDETCOMP)) {
		accessory->handle_plug_irq = true;
		/* Set iddet IP HW controllable */
		ret = abx500_mask_and_set(dev, AB8505_CHARGER,
			REGIDDETCTRL3, IDDETSWCTRLENA, 0x00);
		if (ret < 0) {
			dev_err(dev, "%s write failed %d\n",
						__func__, __LINE__);
			return;
		}
		dev_err(dev, "\n %s invalid id_voltage == %x\n",
						__func__, id_voltage);
		return;
	}

	/* 1microAmp current source pull up disable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}

	ret = detect_depending_on_id_resistance(accessory);
	if (ret < 0) {
		dev_err(dev, "%s detection failed %d\n", __func__, __LINE__);
		return;
	}
	/* 1microAmp current source pull up enable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL2, IDDETPU1ENA, IDDETPU1ENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}
	dev_info(dev, "Cable ID Detected present %d last %d\n",
			accessory->cable_detected,
			accessory->cable_last_detected);

	/* Incase of two plug interrupt without unplug interrupt */
	if (accessory->cable_detected == accessory->cable_last_detected)
		return;

	accessory->cable_last_detected = accessory->cable_detected;

	/* Do cable specific plug related work */
	acessory_func_list[accessory->cable_detected](accessory, true);
	accessory_release_irq(CABLE_PLUG, accessory);
	accessory_claim_irq(CABLE_UNPLUG, accessory);
}

/*
 * set the registers for scaning the USB device connected
 * here HW detection of the device connected is disabled.
 */
static int usb_switch_init(struct usb_accessory_state *accessory)
{
	int ret;
	struct device *dev = accessory->dev;

	/* Disable host, device detection and enable VBUS Valid comporator */
	ret = abx500_mask_and_set(dev, AB8505_USB,
				USBOTGCTRL, IDHOSTENA | IDDEVENA |
				VBUSVALIDENA, 0x0);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* Set iddet IP SW controllable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL3, IDDETSWCTRLENA, IDDETSWCTRLENA);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	return ret;
}

/* Check if the connected accessory has ID-USB resistance */
static int plug_unplug_monitor_init(struct usb_accessory_state *accessory)
{
	struct device *dev = accessory->dev;
	int ret;
	unsigned char id_voltage;

	/* 1microAmp current source pull up and ID detect comparator enable */
	ret = set_pullup_comparator(accessory, true);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* read Iddet comparator state */
	ret = abx500_get(dev, AB8505_CHARGER,
				REGIDDETSTATE, &id_voltage);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* 1microAmp current source pull up and ID detect comparator disable */
	ret = set_pullup_comparator(accessory, false);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	dev_info(dev, "Anything connected with ID-USB resistance? %s\n",
			(id_voltage & IDDETPLUGDETCOMP) ?
			"true" : "false");

	/* Return ID-USB resistance detected or not */
	return (ret < 0) ? ret : !!(id_voltage & IDDETPLUGDETCOMP);
}

/*
 * Work function to detect the uUSB cable plug/unplug.
 * On detecting a cable, cable specific call back is called to do the
 * cable specific initializations and on unplug of the same cable the
 * initializations done are undone.
 */
static void micro_usb_accessory_detect(struct work_struct *work)
{
	int ret;
	unsigned char usblink1status;
	struct usb_accessory_state *accessory
		= container_of(work, struct usb_accessory_state,
				cable_detection.work);
	struct device *dev = accessory->dev;
	ret = abx500_get(dev, AB8505_USB, USBLINK1STATUS, &usblink1status);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		goto restart;
	}

	/* Skip detection if UsbLink1Status[4:0] is set */
	if (usblink1status & LINK1_STATUS_MASK)
		goto restart;

	/*
	 * On Audio device connection, stop the cable unplug detection.
	 * Audio device routine, will do the unplg detection and will
	 * queue iddet_workqueue on Audio device Unplug.
	 */
	if (accessory->cable_last_detected == USBSWITCH_AUDIODEV_TYPE1 ||
		accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE1 ||
		accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE2 ||
		accessory->cable_last_detected == USBSWITCH_DESKTOP_DOCK)
		return;

	if (accessory->cable_last_detected == USBSWITCH_UART_BOOT_ON ||
			accessory->cable_last_detected ==
			USBSWITCH_UART_BOOT_OFF ||
			accessory->cable_last_detected ==
			USBSWITCH_PPD ||
			accessory->cable_last_detected ==
			USBSWITCH_UART ||
			accessory->cable_last_detected ==
			USBSWITCH_LEGACY_CHARGER)
		return;
	/*
	 * If already a cable was detected, skip to id resistance change
	 * or vbus change detection direclty, i.e for unplug detection.
	 */
	if (accessory->cable_last_detected != USBSWITCH_NONE) {
		goto detect_unplug;
	}

	accessory->cable_detected = USBSWITCH_NONE;

	/* Take a backup of the registers, about to be changed */
	ret = abx500_get(dev, AB8505_USB, USBOTGCTRL, &accessory->usb_otg_ctrl);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		goto restart;
	}

	ret = usb_switch_init(accessory);
	if (ret < 0)
		goto restore;

detect_unplug:
	ret = plug_unplug_monitor_init(accessory);
	if (!ret) {
		if (accessory->cable_last_detected != USBSWITCH_NONE) {
			acessory_func_list[accessory->cable_last_detected]
				(accessory, false);
			accessory->cable_last_detected = USBSWITCH_NONE;
		}
		goto restore;
	}

	ret = detect_depending_on_id_resistance(accessory);
	if (ret < 0)
		goto restore;

	dev_info(dev, "Cable ID Detected present %d last %d\n",
			accessory->cable_detected,
			accessory->cable_last_detected);

	if (accessory->cable_detected == accessory->cable_last_detected)
		goto restart;

	if (accessory->cable_last_detected != USBSWITCH_NONE &&
	    accessory->cable_last_detected != USBSWITCH_USBHOST &&
	    accessory->cable_last_detected != USB_CABLE)
		/* Call restore callback for the last connected device */
		acessory_func_list[accessory->cable_last_detected](accessory,
				false);

	accessory->cable_last_detected = accessory->cable_detected;

	/* Do cable specific plug related work */
	acessory_func_list[accessory->cable_detected](accessory, true);

	goto restart;

restore:
	mutex_lock(&accessory->usb_otg_ctrl_lock);
	if (accessory->usb_otg_ctrl) {
		/* Restore back registers */
		ret = abx500_set(dev, AB8505_USB, USBOTGCTRL,
				accessory->usb_otg_ctrl);
		if (ret < 0)
			dev_err(dev, "%s write failed %d\n", __func__,
					__LINE__);
		accessory->usb_otg_ctrl = 0;
	}
	mutex_unlock(&accessory->usb_otg_ctrl_lock);

restart:
	queue_delayed_work(accessory->iddet_workqueue,
			&accessory->cable_detection,
			msecs_to_jiffies(DETECTION_INTERVAL_MS));
}

/* ID resistance based, cable unplug interrupt handler */
static irqreturn_t micro_usb_accessory_unplug(int irq, void *data)
{
	struct usb_accessory_state *accessory = data;

	if (accessory->cable_last_detected != USBSWITCH_NONE &&
	    accessory->cable_last_detected != USBSWITCH_USBHOST &&
	    accessory->cable_last_detected != USB_CABLE) {
		/* Call restore callback for the last connected device */
		acessory_func_list[accessory->cable_last_detected](accessory,
				false);
		accessory->cable_last_detected = USBSWITCH_NONE;
	}
	
	queue_work(accessory->iddet_workqueue,
			&accessory->cable_unplug_work);
	accessory->handle_plug_irq = true;
	return IRQ_HANDLED;
}

/*
 * interrupt handler to detect the uUSB cable plug/unplug.
 * On detecting a cable, cable specific call back is called to do the
 * cable specific initializations.
 */
static irqreturn_t micro_usb_accessory_plug(int irq, void *data)
{
	struct usb_accessory_state *accessory = data;
	/*
	 * Due to HW issues interrupts are comming even though ITMask bit is
	 * set, this leads to extra interrupts which need not have to handle.
	 * so do not handle plug interrupt if you already receive.
	 */
	if (!accessory->handle_plug_irq)
		goto done;

	accessory->handle_plug_irq = false;
	queue_kthread_work(&accessory->kworker,
			&accessory->detect_accessory_work);
done:
	return IRQ_HANDLED;
}

static int init_key_press(struct platform_device *pdev,
		struct usb_accessory_state *accessory)
{
	int i;
	int irq;
	int ret;

	for (i = 0; i < sizeof(key_press_interrupts)/
			sizeof(key_press_interrupts[0]); i++) {

		irq = platform_get_irq_byname(pdev,
				key_press_interrupts[i].name);
		if (irq < 0) {
			dev_err(&pdev->dev,
				"%s: Failed to get irq %s\n", __func__,
				key_press_interrupts[i].name);
			return irq;
		}

		ret = request_threaded_irq(irq, NULL,
					key_press_interrupts[i].function,
					IRQF_TRIGGER_FALLING,
					key_press_interrupts[i].name,
					accessory);
		if (ret != 0) {
			dev_err(&pdev->dev,
				"%s: Failed to claim irq %s (%d)\n",
				__func__,
				key_press_interrupts[i].name,
				ret);
			return ret;
		}

		key_press_interrupts[i].irq = irq;
		disable_irq(key_press_interrupts[i].irq);
	}

	return 0;
}

static int deinit_keypress(struct platform_device *pdev,
		struct usb_accessory_state *accessory)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(key_press_interrupts); i++)
		free_irq(key_press_interrupts[i].irq, accessory);
	return 0;

}

/*
 * create input device for button press reporting
 */
static int init_button(struct usb_accessory_state *accessory)
{
	int err;

	usb_button = input_allocate_device();
	if (!usb_button) {
		dev_err(accessory->dev, "Input device alloc failed.\n");
		return -ENOMEM;
	}

	input_set_capability(usb_button, EV_KEY, KEY_MEDIA);

	usb_button->name = "uUSB button";
	usb_button->uniq = "uUSBbtn";
	usb_button->dev.parent = accessory->dev;

	err = input_register_device(usb_button);
	if (err) {
		dev_err(accessory->dev, "Input device registration failed %d",
			       err);
		input_free_device(usb_button);
		usb_button = NULL;
	}

	return err;
}

/*
 * On Vbus Fall interrupt send carkit charger unplug
 * notification to charger driver
 */
static irqreturn_t vbus_fall_irq_handler(int irq, void *data)
{
	struct usb_accessory_state  *accessory = data;
	unsigned char device_type = 0;
	unsigned int current_limit = 0;


	/* Current limit is in milli amp*/
	if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE1) {
		device_type = CARKIT_TYPE1_UNPLUGGED;
		current_limit = CURRENT_LIMIT_CKT_TYPE1;
	} else if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE2) {
		device_type = CARKIT_TYPE2_UNPLUGGED;
		current_limit = CURRENT_LIMIT_CKT_TYPE2;
	}  else if (accessory->cable_last_detected == USBSWITCH_DESKTOP_DOCK) {
		device_type = DESKTOP_DOCK_UNPLUGGED;
		current_limit = CURRENT_LIMIT_DESKTOP_DOCK;
	} else if (accessory->cable_last_detected == USBSWITCH_LEGACY_CHARGER) {

		queue_work(accessory->iddet_workqueue,
			&accessory->legacy_unplug_work);
		return IRQ_HANDLED;
	}

	/* Notify carkit plug status*/
	blocking_notifier_call_chain(&micro_usb_notifier_list,
		device_type, &current_limit);

	return IRQ_HANDLED;
}

/*
 * On Vbus Raise interrupt send carkit charger plug
 * notification to charger driver
 */
static irqreturn_t vbus_irq_handler(int irq, void *data)
{
	struct usb_accessory_state  *accessory = data;
	unsigned char device_type = 0;
	unsigned int current_limit = 0;
	accessory->vbus_raise_irq = true;
	if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE1 ||
		accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE2 ||
		accessory->cable_last_detected == USBSWITCH_DESKTOP_DOCK) {

		/* Current limit is in milli amp*/
		if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE1) {
			device_type = CARKIT_TYPE1_PLUGGED;
			current_limit = CURRENT_LIMIT_CKT_TYPE1;
		} else if (accessory->cable_last_detected == USBSWITCH_CARKIT_TYPE2) {
			device_type = CARKIT_TYPE2_PLUGGED;
			current_limit = CURRENT_LIMIT_CKT_TYPE2;
		} else if (accessory->cable_last_detected == USBSWITCH_DESKTOP_DOCK) {
			device_type = DESKTOP_DOCK_PLUGGED;
			current_limit = CURRENT_LIMIT_DESKTOP_DOCK;
		}

		/* Notify carkit plug status*/
		blocking_notifier_call_chain(&micro_usb_notifier_list,
				device_type, &current_limit);
		return IRQ_HANDLED;
	}

	queue_work(accessory->iddet_workqueue,
			&accessory->detect_dock_work);

	return IRQ_HANDLED;
}

/*
 * Handle link1status register change interrupt, which is because of the
 * link1status detectable cable plug/unplug.
 */
static irqreturn_t link_status_irq_handler(int irq, void *data)
{
	struct usb_accessory_state  *accessory = data;
	unsigned char usblink1status;
	struct device *dev = accessory->dev;
	int ret;
	unsigned int current_limit = CURRENT_LIMIT_DESKTOP_DOCK;
	unsigned char vbusdet;

	ret = abx500_get(dev, AB8505_USB, USBLINK1STATUS, &usblink1status);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__, __LINE__);
		return IRQ_HANDLED;
	}
	accessory->lsts = (usblink1status >> 3);
	/* read VbusDet state */
	ret = abx500_get(dev, AB8505_INTERRUPT, ITSOURCE2, &vbusdet);
	if (ret < 0) {
		dev_err(dev, "%s read failed %d\n", __func__,
				__LINE__);
		goto err_abx500_set;
	}
	vbusdet &= VBUSDET;

	if (!vbusdet) {
		blocking_notifier_call_chain(&micro_usblinkstatus_notifier_usb,
			accessory->lsts, NULL);
	} else if (!accessory->vbus_raise_irq) {
		if (usblink1status == 0 && accessory->cable_last_detected == USBSWITCH_NONE) {
			printk("\n sending notification \n");
			blocking_notifier_call_chain(&micro_usb_notifier_list,
				LEGACY_CHARGER_PLUGGED, &current_limit);
		}
	}

	/* send event to plug_detect workqueue*/
	if (accessory->wait_event_lsts) {
		accessory->wait_event_lsts = false;
		wake_up_interruptible(&accessory->wait);
	}
	/* send event to dock detect workqueue*/
	if (accessory->wait_event_lsts1) {
		accessory->wait_event_lsts1 = false;
		wake_up_interruptible(&accessory->wait_vbus);
	}

	/* 
	 * If the following cables are connected USB OTG, USB, CDP the sysfs
	 * /sys/class/sec/switch/usb_state returns "USB_STATE_CONFIGURED"
	 */
	if (accessory->lsts == USB_LINK_STD_HOST ||
			accessory->lsts == USB_LINK_CDP ||
			accessory->lsts == USB_LINK_ACA_RID ||
			accessory->lsts == USB_LINK_HM_IDGND) {
			accessory->cable_last_detected_sysfs_info = USB_CABLE;
	}

	/*
	 * In cut1 we are changing OTGCTRL register, so need to write
	 * back the value of OTGCTRL reg, in linkstatus interrupt for USB
	 * functionalities to work. No need to do this for cut2.
	 */
	if (is_ab8505_2p0_earlier(accessory->parent) ||
		is_ab9540_2p0_or_earlier(accessory->parent)) {
		/*
		 * Incase of UART cable and Legacy charger restoration is not
		 * needed On UART cable plug usblink1status is set to 0x80.
		 */
		if ((usblink1status != UART_BOOT_VAL) && (accessory->cable_last_detected
					!= USBSWITCH_LEGACY_CHARGER)) {
			if (accessory->cable_last_detected != USBSWITCH_NONE &&
			    accessory->cable_last_detected != USBSWITCH_USBHOST &&
			    accessory->cable_last_detected != USB_CABLE) {	
				acessory_func_list[accessory->cable_last_detected]
					(accessory, false);
				accessory->cable_last_detected = USBSWITCH_NONE;
			}

			mutex_lock(&accessory->usb_otg_ctrl_lock);
			if (accessory->usb_otg_ctrl) {
				/* Restore the register back */
				ret = abx500_mask_and_set(dev, AB8505_USB,
					USBOTGCTRL, IDHOSTENA | IDDEVENA |
					VBUSVALIDENA, accessory->usb_otg_ctrl);
				if (ret < 0)
					dev_err(dev, "%s write failed %d\n",
							__func__, __LINE__);

				accessory->usb_otg_ctrl = 0;
			}
			mutex_unlock(&accessory->usb_otg_ctrl_lock);
		}
	} else {
		/*
		 * If cable last detected is USBHOST then enable IDDETPU1ENA
		 * & PLUGDETCOMPENA to get interrupts when cable is connected.
		 */
		if (usblink1status == 0 &&
			(accessory->cable_last_detected == USBSWITCH_USBHOST)) {

			/*
			 * 1microAmp current source pull up and
			 * ID detect comparator enable
			 */
			ret = set_pullup_comparator(accessory, true);
			if (ret < 0) {
				dev_err(dev, "%s write failed %d\n",
						__func__, __LINE__);
				goto err_abx500_set;
			}
			ret = abx500_mask_and_set(dev, AB8505_USB, USBLINECTRL2,
							USBCHARGDETENA, 0x00);
			if (ret < 0) {
				dev_err(dev, "%s write failed %d\n",
						__func__, __LINE__);
				goto err_abx500_set;
			}
			accessory->handle_plug_irq = true;
			accessory->cable_last_detected = USBSWITCH_NONE;
		}
	}
err_abx500_set:
	return IRQ_HANDLED;
}
static void accessory_unplug_detect(struct work_struct *work)
{
	struct usb_accessory_state *accessory
		= container_of(work, struct usb_accessory_state,
				cable_unplug_work);
	struct device *dev = accessory->dev;
	int ret;

	/* Set iddet IP HW controllable */
	ret = abx500_mask_and_set(dev, AB8505_CHARGER,
				REGIDDETCTRL3, IDDETSWCTRLENA, 0x00);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}
	accessory_release_irq(CABLE_UNPLUG, accessory);
	accessory_claim_irq(CABLE_PLUG, accessory);

	/* HUGE WORKAROUND - REMOVE IT ASAP! */
	dev_info(dev, "NovaFusion: USB OTG device unplugged");
}

static void legacy_unplug_detect(struct work_struct *work)
{
	struct usb_accessory_state *accessory
		= container_of(work, struct usb_accessory_state,
				legacy_unplug_work);
	struct device *dev = accessory->dev;
	int ret;

	if (accessory->cable_last_detected != USBSWITCH_NONE &&
		accessory->cable_last_detected != USBSWITCH_USBHOST &&
		accessory->cable_last_detected != USB_CABLE) {

		/* Call restore callback for the last connected device */
		acessory_func_list[accessory->cable_last_detected](accessory,
				false);
		accessory->cable_last_detected = USBSWITCH_NONE;
	}

	/*
	 * In case of cut1 no need to enable pull up and IDDETECTRL
	 * schedule cable detecttion work queue
	 */
	if (is_ab8505_2p0_earlier(accessory->parent) ||
		is_ab9540_2p0_or_earlier(accessory->parent)) {
		queue_delayed_work(accessory->iddet_workqueue,
			&accessory->cable_detection,
			msecs_to_jiffies(DETECTION_INTERVAL_MS));
		return;
	}

	/* 1microAmp current source pull up and ID det comp enable */
	ret = set_pullup_comparator(accessory, true);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return;
	}

	accessory->handle_plug_irq = true;
}

static int accessory_claim_irq(int irq_id,
		struct usb_accessory_state *accessory)
{
	int irq, ret = 0;

	if (plug_unplug_irqs[irq_id].state)
		return 0;

	dev_info(accessory->dev, "claim %s\n",
			plug_unplug_irqs[irq_id].irq_name);
	irq = platform_get_irq_byname(accessory->pdev,
			plug_unplug_irqs[irq_id].irq_name);
	if (irq < 0) {
		dev_err(accessory->dev, "Failed to get irq\n");
		return irq;
	}

	ret = request_threaded_irq(irq, NULL,
				plug_unplug_irqs[irq_id].irq_fn,
				IRQF_NO_SUSPEND | IRQF_SHARED,
				plug_unplug_irqs[irq_id].irq_name,
				accessory);
	if (ret < 0) {
		dev_err(accessory->dev, "irq request failed\n");
		return ret;
	}

	plug_unplug_irqs[irq_id].irq = irq;
	plug_unplug_irqs[irq_id].state = true;

	return ret;
}

static int accessory_release_irq(int irq_id,
		struct usb_accessory_state *accessory)
{
	int irq;

	if (!plug_unplug_irqs[irq_id].state)
		return 0;

	dev_info(accessory->dev, "release %s\n",
			plug_unplug_irqs[irq_id].irq_name);
	irq = platform_get_irq_byname(accessory->pdev,
			plug_unplug_irqs[irq_id].irq_name);
	if (irq < 0) {
		dev_err(accessory->dev, "Failed to get irq\n");
		return irq;
	}

	free_irq(plug_unplug_irqs[irq_id].irq, accessory);
	plug_unplug_irqs[irq_id].state = false;

	return 0;
}

int set_uartgpio(struct usb_accessory_state *accessory)
{
	struct device *dev = accessory->dev;
	int ret;
	/* DeSelect UARTTX data on pad GPIO13 */
	ret = abx500_mask_and_set(dev, AB8505_GPIO,
				ALTERNATFUNCTION, SETALTERUSBVDATULPIUARTTX,
				0x0);
	printk("!!DeSelect UARTTX ret = %d\n",ret);
	if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
	}

	/* DeSelect UARTRxData function on pad Gpio50 */
	ret = abx500_mask_and_set(dev, AB8505_GPIO,
				ALTERNATFUNCTION, SETALTERULPIUARTRX,
				0x0);
	printk("!!DeSelect UARTRxData ret = %d\n",ret);
	if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
	}

	/* DeSelect alternate function Gpio13Sel */
	ret = abx500_mask_and_set(dev, AB8505_GPIO, GPIOSEL2,
					GPIO13SEL_ALT, GPIO13SEL_ALT);
	printk("!!DeSelect alternate Gpio13Sel ret = %d\n",ret);
	if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
	}

	/* Deselect alternate function on Gpio50Sel */
	ret = abx500_mask_and_set(dev, AB8505_GPIO, GPIOSEL7,
					GPIO50SEL_ALT, GPIO50SEL_ALT);
	printk("Deselect alternate Gpio50Sel ret = %d\n",ret);
	if (ret < 0) {
			dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
			return ret;
	}
	return 0;
}

static bool check_cableconnected_onboot(struct usb_accessory_state *accessory,
		unsigned char usblink1status)
{
	switch (usblink1status) {
	case USB_BOOTOFF_VAL:
		accessory->cable_detected = USBSWITCH_USB_BOOT_OFF;
		return true;
	case USB_BOOTON_VAL:
		accessory->cable_detected = USBSWITCH_USB_BOOT_ON;
		return true;
	case UART_BOOTOFF_VAL:
		accessory->cable_detected = USBSWITCH_UART_BOOT_OFF;
		return true;
	case UART_BOOTON_VAL:
		accessory->cable_detected = USBSWITCH_UART_BOOT_ON;
		return true;
	default:
		return false;
	}
}

static int usbswitch_iddet(struct usb_accessory_state *accessory)
{
	int ret;
	struct device *dev = accessory->dev;
	unsigned char usblink1status;
	unsigned char turnonstatus;
	bool value;

	ret = abx500_get(dev, AB8505_SYS_CTRL, TURNONSTATUS, &turnonstatus);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return false;
	}
	dev_info(dev, "\n in %s turnonstatus = %x\n", __func__, turnonstatus);

	ret = abx500_get(dev, AB8505_USB, USBLINK1STATUS, &usblink1status);
	if (ret < 0) {
		dev_err(dev, "%s write failed %d\n", __func__, __LINE__);
		return false;
	}
	dev_info(dev, "\n in %s usblink1status = %x\n",
				__func__, usblink1status);

	/* check if bit 7 in Turnonstatus reg is set*/
	if (turnonstatus & 0x80) {
		/* check if cable connected is UART BOOTON/OFF,USB BOOTON/OFF */
		value = check_cableconnected_onboot(accessory, usblink1status);
		if (value) {
			/* Need to run this routine to detect cable unplug */
			ret = usb_switch_init(accessory);
			if (ret < 0) {
				dev_err(dev, "\n %s usb switch init failed %d\n",
						__func__, __LINE__);
				return false;
			}
			goto detected;
		}
	}

	ret = usb_switch_init(accessory);
	if (ret < 0) {
		dev_err(dev, "\n %s usb switch init failed %d\n",
						__func__, __LINE__);
		return false;
	}

	ret = plug_unplug_monitor_init(accessory);
	if (!ret) {
		dev_info(dev, "\n %s No cable is present %d\n",
						__func__, __LINE__);
		return false;
	}

	ret = detect_depending_on_id_resistance(accessory);
	if (ret < 0) {
		dev_err(dev, "\n %s detection failed %d\n",
						__func__, __LINE__);
		return false;
	}
detected:
	dev_info(dev, "\n in %s cable detected is %d\n", __func__,
			accessory->cable_detected);

	if (accessory->cable_detected == USBSWITCH_UNKNOWN)
		accessory->cable_detected = USBSWITCH_NONE;

	if (accessory->cable_detected != USBSWITCH_NONE) {
		accessory->cable_last_detected = accessory->cable_detected;
		/* Do cable specific plug related work */
		acessory_func_list[accessory->cable_detected](accessory, true);
		return true;
	}
	return false;
}

/* Samsung Specific */
/* 
 * If the following USBSWITCH_UART_BOOT_OFF is connected the sysfs
 * /sys/class/sec/switch/adc returns "1C"
 */
static ssize_t jig_uart_state_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
   	struct usb_accessory_state *accessory = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", (accessory->cable_last_detected == USBSWITCH_UART_BOOT_OFF)
					? "1C" : "0");
}

static ssize_t usb_state_show(struct device *dev, struct device_attribute *attr, 
		char *buf)
{
	struct usb_accessory_state *accessory = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n",(accessory->cable_last_detected == USBSWITCH_USB_BOOT_ON ||
							accessory->cable_last_detected == USBSWITCH_USB_BOOT_OFF ||
							accessory->cable_last_detected_sysfs_info == USB_CABLE )
					? "USB_STATE_CONFIGURED" : "USB_STATE_NOTCONFIGURED");
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

static DEVICE_ATTR(adc, S_IRUGO | S_IWUSR, jig_uart_state_show, NULL);
static DEVICE_ATTR(usb_state, S_IRUGO | S_IWUSR, usb_state_show, NULL);
static DEVICE_ATTR(jig_smd, 0644, show_jig_smd, store_jig_smd);

int create_sys_class_file(struct usb_accessory_state *accessory)
{
	int ret;

	usb_switch_class = class_create(THIS_MODULE, "usb_switch");
	fsa_temp = device_create(usb_switch_class, NULL, 0, NULL, "FSA_SWITCH");
	device_create_file(fsa_temp, &dev_attr_jig_smd);

	micro_usb_switch = device_create(sec_class, NULL, 0, NULL, "switch");
	if (IS_ERR(micro_usb_switch)) {
		printk(KERN_ERR "Failed to create device(sec_switch)!\n");
	}

	if (device_create_file(micro_usb_switch, &dev_attr_usb_state) < 0) {
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_usb_state.attr.name);
		goto err;
	}
	if (device_create_file(micro_usb_switch, &dev_attr_adc) < 0) {
		printk(KERN_ERR "Failed to create device file(%s)!\n",
				dev_attr_usb_state.attr.name);
		goto err;
	}
	dev_set_drvdata(micro_usb_switch, accessory);
	return 0;

err:
	return ret;
}
/* End of Samsung specific */

static int __devinit ab8505_iddet_probe(struct platform_device *pdev)
{
	int ret = 0;
	/* Set the thread as a priority one, with  MAX_RT_PRIO-5 */
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-5 };
	struct ab8500_platform_data *plat;
	struct ab8505_iddet_platdata *pdata;
	struct usb_accessory_state  *accessory;

	accessory = kzalloc(sizeof(*accessory), GFP_KERNEL);
	if (!accessory) {
		dev_err(&pdev->dev, "alloc failed\n");
		return -ENOMEM;
	}

	mutex_init(&accessory->usb_otg_ctrl_lock);
	mutex_init(&accessory->sync_id_vbus_lock);

	accessory->gpadc = ab8500_gpadc_get();
	if (!accessory->gpadc) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "failed to get gpadc\n");
		goto gpadc_get_fail;

	}

	accessory->dev = &pdev->dev;
	accessory->pdev = pdev;
	plat = dev_get_platdata(pdev->dev.parent);
	pdata = plat->iddet;
	accessory->cables_param_list = pdata->adc_id_list;
	acessory_func_list[USBSWITCH_UART] = pdata->uart_cable;
	accessory->btn_param_list = pdata->btn_list;
	accessory->parent = dev_get_drvdata(pdev->dev.parent);
	handle_gpio[GPIO_ENABLE] = pdata->gpio_enable;
	handle_gpio[GPIO_DISABLE] = pdata->gpio_disable;

	switch_dock_init();

	ret = init_button(accessory);
	if (ret < 0) {
		dev_err(&pdev->dev, "Button init failed %d\n", ret);
		goto btn_init_fail;
	}

	accessory->iddet_workqueue =
		create_singlethread_workqueue("id_resistance_change_det_wq");
	if (!accessory->iddet_workqueue) {
		dev_err(&pdev->dev, "%s: Failed to create wq\n", __func__);
		ret = -ENOMEM;
		goto create_iddet_worqueue_fail;
	}

	INIT_DELAYED_WORK(&accessory->detect_button, button_detection_function);

	btn_detection_work_queue =
		create_singlethread_workqueue("iddet_btn_detect");
	if (!btn_detection_work_queue) {
		dev_err(&pdev->dev, "%s: Failed to create wq\n", __func__);
		ret = -ENOMEM;
		goto create_btn_detection_fail;
	}
	/*
	 * Thread to read the gpadc value, needed to detect the button on
	 * the headset pressed. Since the GPADC read depends on a AB interrupt,
	 * the GPADC cannot be read here in the key_press AB interrupt. So a
	 * HIGH Priotiry thread is created.
	 */
	init_kthread_worker(&accessory->kworker);
	accessory->gpadc_read_thread = kthread_run(kthread_worker_fn,
					&accessory->kworker,
					dev_name(accessory->dev));
	if (IS_ERR(accessory->gpadc_read_thread)) {
		dev_err(&pdev->dev,
			"failed to create id detect thread\n");
		ret = -ENOMEM;
		goto kthread_err;
	}

	accessory->micusb_ph5clk = clk_get(&pdev->dev, "usbclk");
	if (IS_ERR(accessory->micusb_ph5clk)) {
		dev_err(&pdev->dev, "failed to clk_get(usbclk)\n");
		ret = PTR_ERR(accessory->micusb_ph5clk);
		goto kthread_err;
	}
	accessory->micusb_sysclk = clk_get(&pdev->dev, "sysclk");
	if (IS_ERR(accessory->micusb_sysclk)) {
		dev_err(&pdev->dev, "failed to clk_get(sysclk)\n");
		ret = PTR_ERR(accessory->micusb_sysclk);
		goto clk_get_fail;
	}

	/*
	 * The key press may last for a very small time, it needs to be read
	 * quickly so make GPADC reading thread HIGHer priority one.
	 */
	sched_setscheduler(accessory->gpadc_read_thread, SCHED_FIFO, &param);

	init_kthread_work(&accessory->detect_button_work,
			get_button_adc_val);

	ret = init_key_press(pdev, accessory);
	if (ret < 0) {
		dev_err(&pdev->dev, "key press interrupt init failed\n");
		goto key_init_fail;
	}

	accessory->cable_last_detected = USBSWITCH_NONE;

	platform_set_drvdata(pdev, accessory);

	/* Legacy charger unplug detection thread */
	INIT_WORK(&accessory->legacy_unplug_work, legacy_unplug_detect);

	accessory->vbus_raise_irq = false;

	/* Request for Linkstatus irq */
	ret = accessory_claim_irq(LINK_STATUS, accessory);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s claim irq failed %d\n",
						__func__, __LINE__);
		goto irq_get_fail;
	}
	ret = accessory_claim_irq(VBUS_R, accessory);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s claim irq failed %d\n",
						__func__, __LINE__);
		goto irq_get_fail_vbus;
	}

	/*
	 * In case of cut1 no need to enable pull up and IDDETECTRL and
	 * register for interrupts schedule cable detecttion work queue.
	 */
	if (is_ab8505_2p0_earlier(accessory->parent) ||
		is_ab9540_2p0_or_earlier(accessory->parent)) {
		/* Cable Plug/Unplug detection thread */
		INIT_DELAYED_WORK(&accessory->cable_detection,
				micro_usb_accessory_detect);

		queue_delayed_work(accessory->iddet_workqueue,
				&accessory->cable_detection,
				msecs_to_jiffies(DETECTION_INTERVAL_MS));
	} else {
		init_waitqueue_head(&accessory->wait);
		init_waitqueue_head(&accessory->wait_vbus);

		/* Dock plug detection thread */
		INIT_WORK(&accessory->detect_dock_work,
				dock_plug_detect);
		/* Cable Unplug detection thread */
		INIT_WORK(&accessory->cable_unplug_work,
				accessory_unplug_detect);

		/* Cable plug detection thread */
		init_kthread_work(&accessory->detect_accessory_work,
				accessory_plug_detect);
		/*
		 * This sleep is required for GPADC to detect headset, without
		 * delay even though audio cable is connected it is not detected
		 */
		msleep(100);
		ret = usbswitch_iddet(accessory);
		if (ret) {
			/* Request for unplug irq */
			ret = accessory_claim_irq(CABLE_UNPLUG, accessory);
			if (ret < 0) {
				dev_err(&pdev->dev, "%s write failed %d\n",
							__func__, __LINE__);
				goto irq_get_fail1;
			}
			accessory->handle_plug_irq = false;

			if (accessory->cable_last_detected !=
				USBSWITCH_UART_BOOT_ON &&
				accessory->cable_last_detected !=
				USBSWITCH_UART_BOOT_OFF &&
				accessory->cable_last_detected !=
				USBSWITCH_PPD &&
				accessory->cable_last_detected !=
				USBSWITCH_UART) {
					ret = set_uartgpio(accessory);
					if (ret < 0) {
						dev_err(&pdev->dev, "\n %s set_uartgpio failed %d\n",
									__func__, __LINE__);
					return false;
					}
				/* Notify to usb driver to reset PHY register */
					blocking_notifier_call_chain(
							&micro_usb_switch_notifier,
							USB_PHY_RESET, NULL);
			}
		}
		/* 1microAmp current source pull up and ID det comp enable */
		ret = set_pullup_comparator(accessory, true);
		if (ret < 0) {
			dev_err(&pdev->dev, "%s write failed %d\n",
						__func__, __LINE__);
			goto irq_get_fail1;
		}
		if (accessory->cable_last_detected == USBSWITCH_NONE) {

			/* Notify to usb driver to reset PHY register*/
			blocking_notifier_call_chain(&micro_usb_switch_notifier,
				USB_PHY_RESET, NULL);

			/* Give control to charger FSM */
			ret = abx500_mask_and_set(&pdev->dev, AB8505_CHARGER,
					REGIDDETCTRL3, IDDETSWCTRLENA, 0x00);
			if (ret < 0) {
				dev_err(&pdev->dev, "%s claim irq failed %d\n",
							__func__, __LINE__);
				goto irq_get_fail1;
			}
			/* Request for plug irq */
			ret = accessory_claim_irq(CABLE_PLUG, accessory);
			if (ret < 0) {
				dev_err(&pdev->dev, "%s write failed %d\n",
							__func__, __LINE__);
				goto irq_get_fail1;
			}
			ret = set_uartgpio(accessory);
			if (ret < 0) {
					dev_err(&pdev->dev, "\n %s set_uartgpio failed %d\n",
									__func__, __LINE__);
					return false;
			}
			accessory->handle_plug_irq = true;
			/* Disable IDHOSTENA n otgctrl register */
			ret = abx500_mask_and_set(&pdev->dev, AB8505_USB,
					USBOTGCTRL, IDHOSTENA, 0x00);
			if (ret < 0) {
				dev_err(&pdev->dev, "%s write failed %d\n",
							__func__, __LINE__);
				goto irq_get_fail1;
			}
		}
	}

	ret = create_sys_class_file(accessory);
	if (ret < 0)
		goto irq_get_fail1;

	return 0;

irq_get_fail1:
	accessory_release_irq(VBUS_R, accessory);
irq_get_fail_vbus:
	accessory_release_irq(LINK_STATUS, accessory);
btn_init_fail:
	deinit_keypress(pdev, accessory);
irq_get_fail:
	clk_put(accessory->micusb_sysclk);
clk_get_fail:
	clk_put(accessory->micusb_ph5clk);
kthread_err:
	flush_workqueue(btn_detection_work_queue);
	destroy_workqueue(btn_detection_work_queue);
create_btn_detection_fail:
	flush_workqueue(accessory->iddet_workqueue);
	destroy_workqueue(accessory->iddet_workqueue);
create_iddet_worqueue_fail:
	input_unregister_device(usb_button);
	input_free_device(usb_button);
key_init_fail:
gpadc_get_fail:
	mutex_destroy(&accessory->usb_otg_ctrl_lock);
	mutex_destroy(&accessory->sync_id_vbus_lock);
	kfree(accessory);
	return ret;
}
static int __devexit ab8505_id_remove(struct platform_device *pdev)
{
	struct usb_accessory_state *accessory;

	accessory = platform_get_drvdata(pdev);
	hrtimer_cancel(&accessory->timer);
	flush_kthread_worker(&accessory->kworker);
	kthread_stop(accessory->gpadc_read_thread);
	flush_workqueue(btn_detection_work_queue);
	destroy_workqueue(btn_detection_work_queue);
	flush_workqueue(accessory->iddet_workqueue);
	destroy_workqueue(accessory->iddet_workqueue);

	accessory_release_irq(VBUS_R, accessory);
	accessory_release_irq(LINK_STATUS, accessory);
	clk_put(accessory->micusb_sysclk);
	clk_put(accessory->micusb_ph5clk);
	input_unregister_device(usb_button);
	input_free_device(usb_button);
	deinit_keypress(pdev, accessory);
	mutex_destroy(&accessory->usb_otg_ctrl_lock);
	mutex_destroy(&accessory->sync_id_vbus_lock);
	kfree(accessory);

	return 0;
}

static struct platform_driver ab8505_iddet_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ab-iddet",
	},
	.probe = ab8505_iddet_probe,
	.remove = __devexit_p(ab8505_id_remove),
};

static int __init ab8505_iddet_init(void)
{
#if defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_SEC_KYLE) || defined(CONFIG_MACH_CODINA)
	if (use_ab8505_iddet)
		return platform_driver_register(&ab8505_iddet_driver);
	else
		return 0;
#else
	return platform_driver_register(&ab8505_iddet_driver);
#endif
}

static void __exit ab8505_iddet_exit(void)
{
	platform_driver_unregister(&ab8505_iddet_driver);
}

late_initcall(ab8505_iddet_init);
module_exit(ab8505_iddet_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("micro usb accessory detection");
