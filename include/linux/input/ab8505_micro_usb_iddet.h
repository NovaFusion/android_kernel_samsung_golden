/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/clk.h>

enum usb_state {
	USB_BOOT_ON_PLUGGED,
	USB_BOOT_ON_UNPLUGGED,
	USB_BOOT_OFF_PLUGGED,
	USB_BOOT_OFF_UNPLUGGED,
	LEGACY_CHARGER_PLUGGED,
	LEGACY_CHARGER_UNPLUGGED,
	CARKIT_TYPE1_PLUGGED,
	CARKIT_TYPE1_UNPLUGGED,
	CARKIT_TYPE2_PLUGGED,
	CARKIT_TYPE2_UNPLUGGED,
	DESKTOP_DOCK_PLUGGED,
	DESKTOP_DOCK_UNPLUGGED,
};

enum usbswitch_link {
	/* no Vbus, Rid = 618kOhm */
	USBSWITCH_UART_BOOT_ON,
	/* no Vbus, Rid = 523kOhm */
	USBSWITCH_UART_BOOT_OFF,
	/* Vbus, Rid = 301kOhm */
	USBSWITCH_USB_BOOT_ON,
	/* Vbus, Rid = 255kOhm */
	USBSWITCH_USB_BOOT_OFF,
	/* no Vbus, Rid = 1MOhm */
	USBSWITCH_AUDIODEV_TYPE1,
	USBSWITCH_TTY_CONV,
	/* no Vbus, Rid = 150kOhm */
	USBSWITCH_UART,
	/* Vbus, No Id resistance */
	USBSWITCH_LEGACY_CHARGER,
	/* carkit 5-wire with 200kOhm ID resistance */
	USBSWITCH_CARKIT_TYPE1,
	/* carkit 5-wire with 440kOhm ID resistance */
	USBSWITCH_CARKIT_TYPE2,
	/* Phone powered device, 102kOhm resistance */
	USBSWITCH_PPD,
	/* desktop dock 365kOhm ID resistance */
	USBSWITCH_DESKTOP_DOCK,
	USBSWITCH_UNKNOWN,
	USB_CABLE,
	USBSWITCH_USBHOST,
	USBSWITCH_NONE,
};

struct usb_accessory_state {
	struct device *dev;
	struct ab8500_gpadc *gpadc;
	struct kthread_worker kworker;
	struct kthread_work detect_button_work;
	struct kthread_work detect_accessory_work;
	struct task_struct *gpadc_read_thread;
	struct hrtimer timer;
	int cable_detected;
	int vbus_irq;
	int vbus_fall_irq;
	int vbus_raise_irq;
	int lsts;
	struct input_dev *btn_input_dev;
	int cable_last_detected;
	int cable_last_detected_sysfs_info;
	struct cust_rid_adcid *cables_param_list;
	struct button_param_list *btn_param_list;
	struct delayed_work detect_button;
	struct delayed_work cable_detection;
	struct work_struct cable_unplug_work;
	struct work_struct legacy_unplug_work;
	struct work_struct detect_dock_work;
	struct delayed_work legacy_plug_work;
	struct workqueue_struct *iddet_workqueue;
	unsigned char usb_otg_ctrl;
	unsigned char handle_plug_irq;
	unsigned char wait_event_lsts;
	unsigned char wait_event_lsts1;
	wait_queue_head_t wait_vbus;
	wait_queue_head_t wait;
	struct mutex usb_otg_ctrl_lock;
	struct mutex sync_id_vbus_lock;
	struct platform_device *pdev;
	struct ab8500 *parent;
	struct clk *micusb_sysclk;
	struct clk *micusb_ph5clk;
	struct class *usb_state_class;
	struct device *usb_state_dev;
};

struct cust_rid_adcid {
	/* min threshold for ADCid for resistance measure */
	unsigned int min;
	/* max threshold for ADCid for resistance measure */
	unsigned int max;
	/* USB-ID resistance in Ohms */
	int res;
	/* Cable Type */
	int cable_id;
};

/*
 * Structure to define the range of voltage and the corresponding
 * button resistance to identify the button pressed.
 */
struct button_param_list {
	unsigned int vmin;
	unsigned int vmax;
	int resistance;
	int btn_id;
	char *btn_name;
};

struct ab8505_iddet_platdata {
	struct cust_rid_adcid *adc_id_list;
	int (*uart_cable)(struct usb_accessory_state *, bool);
	struct button_param_list *btn_list;
	int (*gpio_enable)(void);
	int (*gpio_disable)(void);
};

#define CUST_RID_ADCID(mn, mx, r, id) { .min = mn, .max = mx,	\
	.res = r, .cable_id = id}
#define CUST_RID_ADCID_END { .min = 0, .max = 0, .res = 0,	\
	.cable_id = 0 }

#define BTN_PARAM(vmn, vmx, res, id, name) { .vmin = vmn, .vmax = vmx,	\
	.resistance = res, .btn_id = id, .btn_name = name }
#define BTN_PARAM_END { .vmin = 0, .vmax = 0,	\
	.resistance = 0, .btn_id = 0, .btn_name = 0 }

#define MAINWDOGENABLE			0x01
#define MAINWDOGKICK			0x02
#define WATCHDOGTIME			0x04
#define UARTLPMODEENA			0x20
#define ALTERNATFUNCTION		0x50
#define USBPHYCTRL			0x8A
#define REGIDDETCTRL4			0xA3
#define USBUARTULPIENA			0x04
#define USBDEVICEMODEENA		0x02
#define SETALTERUSBVDATULPIUARTTX	0x18
#define UARTTXDATA			0x08
#define SETALTERULPIUARTRX		0x04
#define USBUARTNONULPIENA		0x01
#define GPIO13SEL_ALT			0x10
#define GPIO50SEL_ALT			0x02
#define GPIOSEL2			0x01
#define GPIOSEL7			0x06
#define MAINWDOGCTRL			0x01
#define MAINWDOGTIMER			0x02
#define USBLINECTRL2			0x82

#define AB8505_USB			0x05
#define AB8505_CHARGER			0x0B
#define AB8505_INTERRUPT		0x0E
#define AB8505_AUDIO			0x0D
#define AB8505_GPIO			0x10
#define AB8505_SYS_CTRL			0x01
#define AB8505_SYS_CTRL2		0x02

#define AB8500_MAIN_WDOG_CTRL_REG	0x01
#define MAIN_WDOG_ENA			0x01
#define MAIN_WDOG_KICK			0x02
#define MAIN_WDOG_DIS			0x00


extern struct ab8505_iddet_platdata iddet_adc_val_list;
/* For notification to usb driver for phy enable/disable */
extern struct blocking_notifier_head micro_usb_switch_notifier;
extern int micro_usb_register_usb_notifier(struct notifier_block *nb);

extern struct blocking_notifier_head micro_usb_notifier_usb;
extern int micro_usb_register_usb_notifier(struct notifier_block *nb);
extern int micro_usb_unregister_usb_notifier(struct notifier_block *nb);

