/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Charger driver for AB5500
 *
 * License Terms: GNU General Public License v2
 * Authors:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500/ab5500-bm.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/usb/otg.h>

/* Charger constants */
#define NO_PW_CONN			0
#define USB_PW_CONN			2

/* HW failure constants */
#define VBUS_CH_NOK			0x0A
#define VBUS_OVV_TH			0x06

/* AB5500 Charger constants */
#define AB5500_USB_LINK_STATUS		0x78
#define CHARGER_REV_SUP			0x10
#define SW_EOC				0x40
#define USB_CHAR_DET			0x02
#define VBUS_RISING			0x20
#define VBUS_FALLING			0x40
#define USB_LINK_UPDATE			0x02
#define USB_CH_TH_PROT_LOW		0x02
#define USB_CH_TH_PROT_HIGH		0x01
#define USB_ID_HOST_DET_ENA_MASK	0x02
#define USB_ID_HOST_DET_ENA		0x02
#define USB_ID_DEVICE_DET_ENA_MASK	0x01
#define USB_ID_DEVICE_DET_ENA		0x01
#define CHARGER_ISET_IN_1_1A		0x0C
#define LED_ENABLE			0x01
#define RESET				0x00
#define SSW_ENABLE_REBOOT		0x80
#define SSW_REBOOT_EN			0x40
#define SSW_CONTROL_AUTOC		0x04
#define SSW_PSEL_480S			0x00

/* UsbLineStatus register - usb types */
enum ab5500_charger_link_status {
	USB_STAT_NOT_CONFIGURED,
	USB_STAT_STD_HOST_NC,
	USB_STAT_STD_HOST_C_NS,
	USB_STAT_STD_HOST_C_S,
	USB_STAT_HOST_CHG_NM,
	USB_STAT_HOST_CHG_HS,
	USB_STAT_HOST_CHG_HS_CHIRP,
	USB_STAT_DEDICATED_CHG,
	USB_STAT_ACA_RID_A,
	USB_STAT_ACA_RID_B,
	USB_STAT_ACA_RID_C_NM,
	USB_STAT_ACA_RID_C_HS,
	USB_STAT_ACA_RID_C_HS_CHIRP,
	USB_STAT_HM_IDGND,
	USB_STAT_RESERVED,
	USB_STAT_NOT_VALID_LINK,
};

enum ab5500_usb_state {
	AB5500_BM_USB_STATE_RESET_HS,	/* HighSpeed Reset */
	AB5500_BM_USB_STATE_RESET_FS,	/* FullSpeed/LowSpeed Reset */
	AB5500_BM_USB_STATE_CONFIGURED,
	AB5500_BM_USB_STATE_SUSPEND,
	AB5500_BM_USB_STATE_RESUME,
	AB5500_BM_USB_STATE_MAX,
};

/* VBUS input current limits supported in AB5500 in mA */
#define USB_CH_IP_CUR_LVL_0P05		50
#define USB_CH_IP_CUR_LVL_0P09		98
#define USB_CH_IP_CUR_LVL_0P19		193
#define USB_CH_IP_CUR_LVL_0P29		290
#define USB_CH_IP_CUR_LVL_0P38		380
#define USB_CH_IP_CUR_LVL_0P45		450
#define USB_CH_IP_CUR_LVL_0P5		500
#define USB_CH_IP_CUR_LVL_0P6		600
#define USB_CH_IP_CUR_LVL_0P7		700
#define USB_CH_IP_CUR_LVL_0P8		800
#define USB_CH_IP_CUR_LVL_0P9		900
#define USB_CH_IP_CUR_LVL_1P0		1000
#define USB_CH_IP_CUR_LVL_1P1		1100
#define USB_CH_IP_CUR_LVL_1P3		1300
#define USB_CH_IP_CUR_LVL_1P4		1400
#define USB_CH_IP_CUR_LVL_1P5		1500

#define to_ab5500_charger_usb_device_info(x) container_of((x), \
	struct ab5500_charger, usb_chg)

/**
 * struct ab5500_charger_interrupts - ab5500 interupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab5500_charger_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab5500_charger_info {
	int charger_connected;
	int charger_online;
	int charger_voltage;
	int cv_active;
	bool wd_expired;
};

struct ab5500_charger_event_flags {
	bool usb_thermal_prot;
	bool vbus_ovv;
	bool usbchargernotok;
	bool vbus_collapse;
};

struct ab5500_charger_usb_state {
	bool usb_changed;
	int usb_current;
	enum ab5500_usb_state state;
	spinlock_t usb_lock;
};

/**
 * struct ab5500_charger - ab5500 Charger device information
 * @dev:		Pointer to the structure device
 * @chip_id:		Chip-Id of the ab5500
 * @max_usb_in_curr:	Max USB charger input current
 * @vbus_detected:	VBUS detected
 * @vbus_detected_start:
 *			VBUS detected during startup
 * @parent:		Pointer to the struct ab5500
 * @gpadc:		Pointer to the struct gpadc
 * @pdata:		Pointer to the ab5500_charger platform data
 * @bat:		Pointer to the ab5500_bm platform data
 * @flags:		Structure for information about events triggered
 * @usb_state:		Structure for usb stack information
 * @usb_chg:		USB charger power supply
 * @ac:			Structure that holds the AC charger properties
 * @usb:		Structure that holds the USB charger properties
 * @charger_wq:		Work queue for the IRQs and checking HW state
 * @check_hw_failure_work:	Work for checking HW state
 * @check_usbchgnotok_work:	Work for checking USB charger not ok status
 * @ac_work:			Work for checking AC charger connection
 * @detect_usb_type_work:	Work for detecting the USB type connected
 * @usb_link_status_work:	Work for checking the new USB link status
 * @usb_state_changed_work:	Work for checking USB state
 * @check_main_thermal_prot_work:
 *				Work for checking Main thermal status
 * @check_usb_thermal_prot_work:
 *				Work for checking USB thermal status
 * @ otg:			pointer to struct otg_transceiver, used to
 *				notify the current during a standard host
 *				charger.
 * @nb:				structture of type notifier_block, which has
 *				a function pointer referenced by usb driver.
 */
struct ab5500_charger {
	struct device *dev;
	u8 chip_id;
	int max_usb_in_curr;
	bool vbus_detected;
	bool vbus_detected_start;
	struct ab5500 *parent;
	struct ab5500_gpadc *gpadc;
	struct abx500_charger_platform_data *pdata;
	struct abx500_bm_data *bat;
	struct ab5500_charger_event_flags flags;
	struct ab5500_charger_usb_state usb_state;
	struct ux500_charger usb_chg;
	struct ab5500_charger_info usb;
	struct workqueue_struct *charger_wq;
	struct delayed_work check_hw_failure_work;
	struct delayed_work check_usbchgnotok_work;
	struct work_struct detect_usb_type_work;
	struct work_struct usb_link_status_work;
	struct work_struct usb_state_changed_work;
	struct work_struct check_usb_thermal_prot_work;
	struct otg_transceiver *otg;
	struct notifier_block nb;
};

/* USB properties */
static enum power_supply_property ab5500_charger_usb_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

/**
 * ab5500_charger_get_vbus_voltage() - get vbus voltage
 * @di:		pointer to the ab5500_charger structure
 *
 * This function returns the vbus voltage.
 * Returns vbus voltage (on success)
 */
static int ab5500_charger_get_vbus_voltage(struct ab5500_charger *di)
{
	int vch;

	/* Only measure voltage if the charger is connected */
	if (di->usb.charger_connected) {
		vch = ab5500_gpadc_convert(di->gpadc, VBUS_V);
		if (vch < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		vch = 0;
	}
	return vch;
}

/**
 * ab5500_charger_get_usb_current() - get usb charger current
 * @di:		pointer to the ab5500_charger structure
 *
 * This function returns the usb charger current.
 * Returns usb current (on success) and error code on failure
 */
static int ab5500_charger_get_usb_current(struct ab5500_charger *di)
{
	int ich;

	/* Only measure current if the charger is online */
	if (di->usb.charger_online) {
		ich = ab5500_gpadc_convert(di->gpadc, USB_CHARGER_C);
		if (ich < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		ich = 0;
	}
	return ich;
}

/**
 * ab5500_charger_detect_chargers() - Detect the connected chargers
 * @di:		pointer to the ab5500_charger structure
 *
 * Returns the type of charger connected.
 * For USB it will not mean we can actually charge from it
 * but that there is a USB cable connected that we have to
 * identify. This is used during startup when we don't get
 * interrupts of the charger detection
 *
 * Returns an integer value, that means,
 * NO_PW_CONN  no power supply is connected
 * USB_PW_CONN  if the USB power supply is connected
 */
static int ab5500_charger_detect_chargers(struct ab5500_charger *di)
{
	int result = NO_PW_CONN;
	int ret;
	u8 val;
	/* Check for USB charger */
	/*
	 * TODO: Since there are no status register validating by
	 * reading the IT souce registers
	 */
	ret = abx500_get_register_interruptible(di->dev, AB5500_BANK_IT,
		AB5500_IT_SOURCE8, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab5500 read failed\n", __func__);
		return ret;
	}

	if (val & VBUS_RISING)
		result |= USB_PW_CONN;
	else if (val & VBUS_FALLING)
		result = NO_PW_CONN;

	return result;
}

/**
 * ab5500_charger_max_usb_curr() - get the max curr for the USB type
 * @di:			pointer to the ab5500_charger structure
 * @link_status:	the identified USB type
 *
 * Get the maximum current that is allowed to be drawn from the host
 * based on the USB type.
 * Returns error code in case of failure else 0 on success
 */
static int ab5500_charger_max_usb_curr(struct ab5500_charger *di,
	enum ab5500_charger_link_status link_status)
{
	int ret = 0;

	switch (link_status) {
	case USB_STAT_STD_HOST_NC:
	case USB_STAT_STD_HOST_C_NS:
	case USB_STAT_STD_HOST_C_S:
		dev_dbg(di->dev, "USB Type - Standard host is "
			"detected through USB driver\n");
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P09;
		break;
	case USB_STAT_HOST_CHG_HS_CHIRP:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P5;
		break;
	case USB_STAT_HOST_CHG_HS:
	case USB_STAT_ACA_RID_C_HS:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P9;
		break;
	case USB_STAT_ACA_RID_A:
		/*
		 * Dedicated charger level minus maximum current accessory
		 * can consume (300mA). Closest level is 1100mA
		 */
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P1;
		break;
	case USB_STAT_ACA_RID_B:
		/*
		 * Dedicated charger level minus 120mA (20mA for ACA and
		 * 100mA for potential accessory). Closest level is 1300mA
		 */
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P3;
		break;
	case USB_STAT_DEDICATED_CHG:
	case USB_STAT_HOST_CHG_NM:
	case USB_STAT_ACA_RID_C_HS_CHIRP:
	case USB_STAT_ACA_RID_C_NM:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P5;
		break;
	case USB_STAT_RESERVED:
		/*
		 * This state is used to indicate that VBUS has dropped below
		 * the detection level 4 times in a row. This is due to the
		 * charger output current is set to high making the charger
		 * voltage collapse. This have to be propagated through to
		 * chargalg. This is done using the property
		 * POWER_SUPPLY_PROP_CURRENT_AVG = 1
		 */
		di->flags.vbus_collapse = true;
		dev_dbg(di->dev, "USB Type - USB_STAT_RESERVED "
			"VBUS has collapsed\n");
		ret = -1;
		break;
	case USB_STAT_HM_IDGND:
	case USB_STAT_NOT_CONFIGURED:
	case USB_STAT_NOT_VALID_LINK:
		dev_err(di->dev, "USB Type - Charging not allowed\n");
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		ret = -ENXIO;
		break;
	default:
		dev_err(di->dev, "USB Type - Unknown\n");
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		ret = -ENXIO;
		break;
	};

	dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d",
		link_status, di->max_usb_in_curr);

	return ret;
}

/**
 * ab5500_charger_read_usb_type() - read the type of usb connected
 * @di:		pointer to the ab5500_charger structure
 *
 * Detect the type of the plugged USB
 * Returns error code in case of failure else 0 on success
 */
static int ab5500_charger_read_usb_type(struct ab5500_charger *di)
{
	int ret;
	u8 val;

	ret = abx500_get_register_interruptible(di->dev, AB5500_BANK_USB,
		AB5500_USB_LINE_STATUS, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab5500 read failed\n", __func__);
		return ret;
	}

	/* get the USB type */
	val = (val & AB5500_USB_LINK_STATUS) >> 3;
	ret = ab5500_charger_max_usb_curr(di,
		(enum ab5500_charger_link_status) val);

	return ret;
}

static int ab5500_charger_voltage_map[] = {
	3500 ,
	3525 ,
	3550 ,
	3575 ,
	3600 ,
	3625 ,
	3650 ,
	3675 ,
	3700 ,
	3725 ,
	3750 ,
	3775 ,
	3800 ,
	3825 ,
	3850 ,
	3875 ,
	3900 ,
	3925 ,
	3950 ,
	3975 ,
	4000 ,
	4025 ,
	4050 ,
	4060 ,
	4070 ,
	4080 ,
	4090 ,
	4100 ,
	4110 ,
	4120 ,
	4130 ,
	4140 ,
	4150 ,
	4160 ,
	4170 ,
	4180 ,
	4190 ,
	4200 ,
	4210 ,
	4220 ,
	4230 ,
	4240 ,
	4250 ,
	4260 ,
	4270 ,
	4280 ,
	4290 ,
	4300 ,
	4310 ,
	4320 ,
	4330 ,
	4340 ,
	4350 ,
	4360 ,
	4370 ,
	4380 ,
	4390 ,
	4400 ,
	4410 ,
	4420 ,
	4430 ,
	4440 ,
	4450 ,
	4460 ,
	4470 ,
	4480 ,
	4490 ,
	4500 ,
	4510 ,
	4520 ,
	4530 ,
	4540 ,
	4550 ,
	4560 ,
	4570 ,
	4580 ,
	4590 ,
	4600 ,
};

/*
 * This array maps the raw hex value to charger current used by the ab5500
 * Values taken from the AB5500 product specification manual
 */
static int ab5500_charger_current_map[] = {
	100 ,
	200 ,
	300 ,
	400 ,
	500 ,
	600 ,
	700 ,
	800 ,
	900 ,
	1000,
	1100,
	1200,
	1300,
	1400,
	1500,
	1500,
};

static int ab5500_icsr_current_map[] = {
	50,
	93,
	193,
	290,
	380,
	450,
	500 ,
	600 ,
	700 ,
	800 ,
	900 ,
	1000,
	1100,
	1300,
	1400,
	1500,
};

static int ab5500_cvrec_voltage_map[] = {
	3300,
	3325,
	3350,
	3375,
	3400,
	3425,
	3450,
	3475,
	3500,
	3525,
	3550,
	3575,
	3600,
	3625,
	3650,
	3675,
	3700,
	3725,
	3750,
	3775,
	3800,
	3825,
	3850,
	3875,
	3900,
	3925,
	4000,
	4025,
	4050,
	4075,
	4100,
	4125,
	4150,
	4175,
	4200,
	4225,
	4250,
	4275,
	4300,
	4325,
	4350,
	4375,
	4400,
	4425,
	4450,
	4475,
	4500,
	4525,
	4550,
	4575,
	4600,
};

static int ab5500_cvrec_voltage_to_regval(int voltage)
{
	int i;

	/* Special case for voltage below 3.3V */
	if (voltage < ab5500_cvrec_voltage_map[0])
		return 0;

	for (i = 1; i < ARRAY_SIZE(ab5500_cvrec_voltage_map); i++) {
		if (voltage < ab5500_cvrec_voltage_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab5500_cvrec_voltage_map) - 1;
	if (voltage == ab5500_cvrec_voltage_map[i])
		return i;
	else
		return -1;
}

static int ab5500_voltage_to_regval(int voltage)
{
	int i;

	/* Special case for voltage below 3.3V */
	if (voltage < ab5500_charger_voltage_map[0])
		return 0;

	for (i = 1; i < ARRAY_SIZE(ab5500_charger_voltage_map); i++) {
		if (voltage < ab5500_charger_voltage_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab5500_charger_voltage_map) - 1;
	if (voltage == ab5500_charger_voltage_map[i])
		return i;
	else
		return -1;
}

static int ab5500_icsr_curr_to_regval(int curr)
{
	int i;

	if (curr < ab5500_icsr_current_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab5500_icsr_current_map); i++) {
		if (curr < ab5500_icsr_current_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab5500_icsr_current_map) - 1;
	if (curr == ab5500_icsr_current_map[i])
		return i;
	else
		return -1;
}

static int ab5500_current_to_regval(int curr)
{
	int i;

	if (curr < ab5500_charger_current_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab5500_charger_current_map); i++) {
		if (curr < ab5500_charger_current_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab5500_charger_current_map) - 1;
	if (curr == ab5500_charger_current_map[i])
		return i;
	else
		return -1;
}

/**
 * ab5500_charger_get_usb_cur() - get usb current
 * @di:		pointer to the ab5500_charger structre
 *
 * The usb stack provides the maximum current that can be drawn from
 * the standard usb host. This will be in mA.
 * This function converts current in mA to a value that can be written
 * to the register. Returns -1 if charging is not allowed
 */
static int ab5500_charger_get_usb_cur(struct ab5500_charger *di)
{
	switch (di->usb_state.usb_current) {
	case 50:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		break;
	case 100:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P09;
		break;
	case 200:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P19;
		break;
	case 300:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P29;
		break;
	case 400:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P38;
		break;
	case 500:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P5;
		break;
	default:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		return -1;
		break;
	};
	return 0;
}

/**
 * ab5500_charger_set_vbus_in_curr() - set VBUS input current limit
 * @di:		pointer to the ab5500_charger structure
 * @ich_in:	charger input current limit
 *
 * Sets the current that can be drawn from the USB host
 * Returns error code in case of failure else 0(on success)
 */
static int ab5500_charger_set_vbus_in_curr(struct ab5500_charger *di,
		int ich_in)
{
	int ret;
	int input_curr_index;
	int min_value;

	/* We should always use to lowest current limit */
	min_value = min(di->bat->chg_params->usb_curr_max, ich_in);

	input_curr_index = ab5500_icsr_curr_to_regval(min_value);
	if (input_curr_index < 0) {
		dev_err(di->dev, "VBUS input current limit too high\n");
		return -ENXIO;
	}

	ret = abx500_set_register_interruptible(di->dev, AB5500_BANK_CHG,
		AB5500_ICSR, input_curr_index);
	if (ret)
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);

	return ret;
}

/**
 * ab5500_charger_usb_en() - enable usb charging
 * @di:		pointer to the ab5500_charger structure
 * @enable:	enable/disable flag
 * @vset:	charging voltage
 * @ich_out:	charger output current
 *
 * Enable/Disable USB charging and turns on/off the charging led respectively.
 * Returns error code in case of failure else 0(on success)
 */
static int ab5500_charger_usb_en(struct ux500_charger *charger,
	int enable, int vset, int ich_out)
{
	int ret;
	int volt_index;
	int curr_index;

	struct ab5500_charger *di = to_ab5500_charger_usb_device_info(charger);

	if (enable) {
		/* Check if USB is connected */
		if (!di->usb.charger_connected) {
			dev_err(di->dev, "USB charger not connected\n");
			return -ENXIO;
		}

		/* Enable USB charging */
		dev_dbg(di->dev, "Enable USB: %dmV %dmA\n", vset, ich_out);

		volt_index = ab5500_voltage_to_regval(vset);
		curr_index = ab5500_current_to_regval(ich_out) ;

		/* ChVoltLevel: max voltage upto which battery can be charged */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_VSRC, (u8) volt_index);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		/* current that can be drawn from the usb */
		ret = ab5500_charger_set_vbus_in_curr(di, ich_out);
		if (ret) {
			dev_err(di->dev, "%s setting icsr failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		/* ChOutputCurentLevel: protected output current */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_OCSRV, (u8) curr_index);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		/*
		 * Battery voltage when charging should be resumed after
		 * completion of charging
		 */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_CVREC,
				ab5500_cvrec_voltage_to_regval(
			di->bat->bat_type[di->bat->batt_id].recharge_vol));
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		/*
		 * Battery temperature:
		 * Input to the TBDATA register corresponds to the battery
		 * temperature(temp being multiples of 2)
		 * In order to obatain the value to be written to this reg
		 * divide the temperature obtained from gpadc by 2
		 */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_TBDATA,
				di->bat->temp_now / 2);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		/* If success power on charging LED indication */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_LEDT, LED_ENABLE);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		/*
		 * Register DCIOCURRENT is one among the charging watchdog
		 * rekick sequence, hence irrespective of usb charging this
		 * register will have to be written.
		 */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_DCIOCURRENT,
				RESET);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}

		di->usb.charger_online = 1;
	} else {
		/* ChVoltLevel: max voltage upto which battery can be charged */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_VSRC, RESET);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		/* USBChInputCurr: current that can be drawn from the usb */
		ret = ab5500_charger_set_vbus_in_curr(di, RESET);
		if (ret) {
			dev_err(di->dev, "%s resetting icsr failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		/* If success power off charging LED indication */
		ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_LEDT, RESET);
		if (ret) {
			dev_err(di->dev, "%s write failed %d\n",
					__func__, __LINE__);
			return ret;
		}
		di->usb.charger_online = 0;
		di->usb.wd_expired = false;
		dev_dbg(di->dev, "%s Disabled USB charging\n", __func__);
	}
	power_supply_changed(&di->usb_chg.psy);

	return ret;
}

/**
 * ab5500_charger_watchdog_kick() - kick charger watchdog
 * @di:		pointer to the ab5500_charger structure
 *
 * Kick charger watchdog
 * Returns error code in case of failure else 0(on success)
 */
static int ab5500_charger_watchdog_kick(struct ux500_charger *charger)
{
	int ret;
	struct ab5500_charger *di;
	int volt_index, curr_index;
	u8 value = 0;

	/* TODO: update */
	if (charger->psy.type == POWER_SUPPLY_TYPE_USB)
		di = to_ab5500_charger_usb_device_info(charger);
	else
		return -ENXIO;

	ret = abx500_get_register_interruptible(di->dev, AB5500_BANK_STARTUP,
		AB5500_MCB, &value);
	if (ret)
		dev_err(di->dev, "Failed to read!\n");

	value = value | (SSW_ENABLE_REBOOT | SSW_REBOOT_EN |
			SSW_CONTROL_AUTOC | SSW_PSEL_480S);
	ret = abx500_set_register_interruptible(di->dev, AB5500_BANK_STARTUP,
		AB5500_MCB, value);
	if (ret)
		dev_err(di->dev, "Failed to kick WD!\n");

	volt_index = ab5500_voltage_to_regval(
			di->bat->bat_type[di->bat->batt_id].normal_vol_lvl);
	curr_index = ab5500_current_to_regval(di->max_usb_in_curr);

	/* ChVoltLevel: max voltage upto which battery can be charged */
	ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_VSRC, (u8) volt_index);
	if (ret) {
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/* current that can be drawn from the usb */
	ret = ab5500_charger_set_vbus_in_curr(di, di->max_usb_in_curr);
	if (ret) {
		dev_err(di->dev, "%s setting icsr failed %d\n",
				__func__, __LINE__);
		return ret;
	}

	/* ChOutputCurentLevel: protected output current */
	ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_OCSRV, (u8) curr_index);
	if (ret) {
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	/*
	 * Battery voltage when charging should be resumed after
	 * completion of charging
	 */
	/* Charger_Vrechar[5:0] = '4.025 V' */
	ret = abx500_set_register_interruptible(di->dev,
			AB5500_BANK_CHG, AB5500_CVREC,
			ab5500_cvrec_voltage_to_regval(
			di->bat->bat_type[di->bat->batt_id].recharge_vol));
	if (ret) {
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}
	/*
	 * Battery temperature:
	 * Input to the TBDATA register corresponds to the battery
	 * temperature(temp being multiples of 2)
	 * In order to obatain the value to be written to this reg
	 * divide the temperature obtained from gpadc by 2
	 */
	ret = abx500_set_register_interruptible(di->dev,
				AB5500_BANK_CHG, AB5500_TBDATA,
				di->bat->temp_now / 2);
	if (ret) {
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}
	/*
	 * Register DCIOCURRENT is one among the charging watchdog
	 * rekick sequence, hence irrespective of usb charging this
	 * register will have to be written.
	 */
	ret = abx500_set_register_interruptible(di->dev,
			AB5500_BANK_CHG, AB5500_DCIOCURRENT,
			RESET);
	if (ret) {
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	return ret;
}

/**
 * ab5500_charger_update_charger_current() - update charger current
 * @di:		pointer to the ab5500_charger structure
 *
 * Update the charger output current for the specified charger
 * Returns error code in case of failure else 0(on success)
 */
static int ab5500_charger_update_charger_current(struct ux500_charger *charger,
		int ich_out)
{
	int ret = 0;
	int curr_index;
	struct ab5500_charger *di;

	if (charger->psy.type == POWER_SUPPLY_TYPE_USB)
		di = to_ab5500_charger_usb_device_info(charger);
	else
		return -ENXIO;

	curr_index = ab5500_current_to_regval(ich_out);
	if (curr_index < 0) {
		dev_err(di->dev,
			"Charger current too high, "
			"charging not started\n");
		return -ENXIO;
	}

	ret = abx500_set_register_interruptible(di->dev, AB5500_BANK_CHG,
		AB5500_OCSRV, (u8) curr_index);
	if (ret) {
		dev_err(di->dev, "%s write failed %d\n", __func__, __LINE__);
		return ret;
	}

	return ret;
}

/**
 * ab5500_charger_check_hw_failure_work() - check main charger failure
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the main charger status
 */
static void ab5500_charger_check_hw_failure_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab5500_charger *di = container_of(work,
		struct ab5500_charger, check_hw_failure_work.work);

	/* Check if the status bits for HW failure is still active */
	if (di->flags.vbus_ovv) {
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_USB, AB5500_USB_PHY_STATUS,
			&reg_value);
		if (ret < 0) {
			dev_err(di->dev, "%s ab5500 read failed\n", __func__);
			return;
		}
		if (!(reg_value & VBUS_OVV_TH)) {
			di->flags.vbus_ovv = false;
			power_supply_changed(&di->usb_chg.psy);
		}
	}
	/* If we still have a failure, schedule a new check */
	if (di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, round_jiffies(HZ));
	}
}

/**
 * ab5500_charger_detect_usb_type_work() - work to detect USB type
 * @work:	Pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
void ab5500_charger_detect_usb_type_work(struct work_struct *work)
{
	int ret;

	struct ab5500_charger *di = container_of(work,
		struct ab5500_charger, detect_usb_type_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if is
	 * connected by reading the status register
	 */
	ret = ab5500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (!(ret & USB_PW_CONN)) {
		di->vbus_detected = 0;
		di->usb.charger_connected = 0;
		power_supply_changed(&di->usb_chg.psy);
	} else {
		di->vbus_detected = 1;
	}
}

/**
 * ab5500_charger_usb_link_status_work() - work to detect USB type
 * @work:	pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab5500_charger_usb_link_status_work(struct work_struct *work)
{
	int ret;

	struct ab5500_charger *di = container_of(work,
		struct ab5500_charger, usb_link_status_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if  is
	 * connected by reading the status register
	 */
	ret = ab5500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (!(ret & USB_PW_CONN)) {
		di->vbus_detected = 0;
		di->usb.charger_connected = 0;
		power_supply_changed(&di->usb_chg.psy);
	} else {
		di->vbus_detected = 1;
		ret = ab5500_charger_read_usb_type(di);
		if (!ret) {
			/* Update maximum input current */
			ret = ab5500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr);
			if (ret)
				return;

			di->usb.charger_connected = 1;
			power_supply_changed(&di->usb_chg.psy);
		} else if (ret == -ENXIO) {
			/* No valid charger type detected */
			di->usb.charger_connected = 0;
			power_supply_changed(&di->usb_chg.psy);
		}
	}
}

static void ab5500_charger_usb_state_changed_work(struct work_struct *work)
{
	int ret;
	unsigned long flags;
	struct ab5500_charger *di = container_of(work,
		struct ab5500_charger, usb_state_changed_work);

	if (!di->vbus_detected)
		return;

	spin_lock_irqsave(&di->usb_state.usb_lock, flags);
	di->usb_state.usb_changed = false;
	spin_unlock_irqrestore(&di->usb_state.usb_lock, flags);

	/*
	 * wait for some time until you get updates from the usb stack
	 * and negotiations are completed
	 */
	msleep(250);

	if (di->usb_state.usb_changed)
		return;

	dev_dbg(di->dev, "%s USB state: 0x%02x mA: %d\n",
		__func__, di->usb_state.state, di->usb_state.usb_current);

	switch (di->usb_state.state) {
	case AB5500_BM_USB_STATE_RESET_HS:
	case AB5500_BM_USB_STATE_RESET_FS:
	case AB5500_BM_USB_STATE_SUSPEND:
	case AB5500_BM_USB_STATE_MAX:
		di->usb.charger_connected = 0;
		power_supply_changed(&di->usb_chg.psy);
		break;

	case AB5500_BM_USB_STATE_RESUME:
		/*
		 * when suspend->resume there should be delay
		 * of 1sec for enabling charging
		 */
		msleep(1000);
		/* Intentional fall through */
	case AB5500_BM_USB_STATE_CONFIGURED:
		/*
		 * USB is configured, enable charging with the charging
		 * input current obtained from USB driver
		 */
		if (!ab5500_charger_get_usb_cur(di)) {
			/* Update maximum input current */
			ret = ab5500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr);
			if (ret)
				return;

			di->usb.charger_connected = 1;
			power_supply_changed(&di->usb_chg.psy);
		}
		break;

	default:
		break;
	};
}

/**
 * ab5500_charger_check_usbchargernotok_work() - check USB chg not ok status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the USB charger Not OK status
 */
static void ab5500_charger_check_usbchargernotok_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;
	bool prev_status;

	struct ab5500_charger *di = container_of(work,
		struct ab5500_charger, check_usbchgnotok_work.work);

	/* Check if the status bit for usbchargernotok is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB5500_BANK_USB, AB5500_CHGFSM_CHARGER_DETECT, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab5500 read failed\n", __func__);
		return;
	}
	prev_status = di->flags.usbchargernotok;

	if (reg_value & VBUS_CH_NOK) {
		di->flags.usbchargernotok = true;
		/* Check again in 1sec */
		queue_delayed_work(di->charger_wq,
			&di->check_usbchgnotok_work, HZ);
	} else {
		di->flags.usbchargernotok = false;
		di->flags.vbus_collapse = false;
	}

	if (prev_status != di->flags.usbchargernotok)
		power_supply_changed(&di->usb_chg.psy);
}

/**
 * ab5500_charger_check_usb_thermal_prot_work() - check usb thermal status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the USB thermal prot status
 */
static void ab5500_charger_check_usb_thermal_prot_work(
	struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab5500_charger *di = container_of(work,
		struct ab5500_charger, check_usb_thermal_prot_work);

	/* Check if the status bit for usb_thermal_prot is still active */
	/* TODO: Interrupt source reg 15 bit 4 */
	ret = abx500_get_register_interruptible(di->dev,
		AB5500_BANK_USB, AB5500_CHGFSM_USB_BTEMP_CURR_LIM, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab5500 read failed\n", __func__);
		return;
	}
	if (reg_value & USB_CH_TH_PROT_LOW || reg_value & USB_CH_TH_PROT_HIGH)
		di->flags.usb_thermal_prot = true;
	else
		di->flags.usb_thermal_prot = false;

	power_supply_changed(&di->usb_chg.psy);
}

/**
 * ab5500_charger_vbusdetf_handler() - VBUS falling detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_vbusdetf_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	dev_dbg(di->dev, "VBUS falling detected\n");
	queue_work(di->charger_wq, &di->detect_usb_type_work);

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_vbusdetr_handler() - VBUS rising detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_vbusdetr_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	di->vbus_detected = true;
	dev_dbg(di->dev, "VBUS rising detected\n");
	queue_work(di->charger_wq, &di->detect_usb_type_work);

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_usblinkstatus_handler() - USB link status has changed
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_usblinkstatus_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	dev_dbg(di->dev, "USB link status changed\n");

	if (!di->usb.charger_online)
		queue_work(di->charger_wq, &di->usb_link_status_work);

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_usbchthprotr_handler() - Die temp is above usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_usbchthprotr_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	dev_dbg(di->dev,
		"Die temp above USB charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_usb_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_usbchargernotokr_handler() - USB charger not ok detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_usbchargernotokr_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	dev_dbg(di->dev, "Not allowed USB charger detected\n");
	queue_delayed_work(di->charger_wq, &di->check_usbchgnotok_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_chwdexp_handler() - Charger watchdog expired
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_chwdexp_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	dev_dbg(di->dev, "Charger watchdog expired\n");

	/*
	 * The charger that was online when the watchdog expired
	 * needs to be restarted for charging to start again
	 */
	if (di->usb.charger_online) {
		di->usb.wd_expired = true;
		power_supply_changed(&di->usb_chg.psy);
	}

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_vbusovv_handler() - VBUS overvoltage detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab5500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_charger_vbusovv_handler(int irq, void *_di)
{
	struct ab5500_charger *di = _di;

	dev_dbg(di->dev, "VBUS overvoltage detected\n");
	di->flags.vbus_ovv = true;
	power_supply_changed(&di->usb_chg.psy);

	/* Schedule a new HW failure check */
	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab5500_charger_usb_get_property() - get the usb properties
 * @psy:        pointer to the power_supply structure
 * @psp:        pointer to the power_supply_property structure
 * @val:        pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the usb
 * properties by reading the sysfs files.
 * USB properties are online, present and voltage.
 * online:     usb charging is in progress or not
 * present:    presence of the usb
 * voltage:    vbus voltage
 * Returns error code in case of failure else 0(on success)
 */
static int ab5500_charger_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab5500_charger *di;

	di = to_ab5500_charger_usb_device_info(psy_to_ux500_charger(psy));

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->flags.usbchargernotok)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (di->usb.wd_expired)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (di->flags.usb_thermal_prot)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (di->flags.vbus_ovv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = di->usb.charger_online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->usb.charger_connected;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		di->usb.charger_voltage = ab5500_charger_get_vbus_voltage(di);
		val->intval = di->usb.charger_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = ab5500_charger_get_usb_current(di) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		/*
		 * This property is used to indicate when VBUS has collapsed
		 * due to too high output current from the USB charger
		 */
		if (di->flags.vbus_collapse)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * ab5500_charger_hw_registers() - Set up charger related registers
 * @di:		pointer to the ab5500_charger structure
 *
 * Set up charger OVV, watchdog and maximum voltage registers as well as
 * charging of the backup battery
 */
static int ab5500_charger_init_hw_registers(struct ab5500_charger *di)
{
	int ret = 0;

	/* Enable ID Host and Device detection */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_USB, AB5500_USB_OTG_CTRL,
			USB_ID_HOST_DET_ENA_MASK, USB_ID_HOST_DET_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable usb charger detection\n");
		goto out;
	}
	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_USB, AB5500_USB_OTG_CTRL,
			USB_ID_DEVICE_DET_ENA_MASK, USB_ID_DEVICE_DET_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable usb charger detection\n");
		goto out;
	}

	/* Over current protection for reverse supply */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_CHG, AB5500_CREVS, CHARGER_REV_SUP,
			CHARGER_REV_SUP);
	if (ret) {
		dev_err(di->dev,
			"failed to enable over current protection for reverse supply\n");
		goto out;
	}

	/* Enable SW EOC at flatcurrent detection */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_CHG, AB5500_CCTRL, SW_EOC, SW_EOC);
	if (ret) {
		dev_err(di->dev,
			"failed to enable end of charge at flatcurrent detection\n");
		goto out;
	}
out:
	return ret;
}

/*
 * ab5500 charger driver interrupts and their respective isr
 */
static struct ab5500_charger_interrupts ab5500_charger_irq[] = {
	{"VBUS_FALLING", ab5500_charger_vbusdetf_handler},
	{"VBUS_RISING", ab5500_charger_vbusdetr_handler},
	{"USB_LINK_UPDATE", ab5500_charger_usblinkstatus_handler},
	{"USB_CH_TH_PROTECTION", ab5500_charger_usbchthprotr_handler},
	{"USB_CH_NOT_OK", ab5500_charger_usbchargernotokr_handler},
	{"OVV", ab5500_charger_vbusovv_handler},
	/* TODO: Interrupt missing, will be available in cut 2 */
	/*{"CHG_SW_TIMER_OUT", ab5500_charger_chwdexp_handler},*/
};

static int ab5500_charger_usb_notifier_call(struct notifier_block *nb,
		unsigned long event, void *power)
{
	struct ab5500_charger *di =
		container_of(nb, struct ab5500_charger, nb);
	enum ab5500_usb_state bm_usb_state;
	unsigned mA = *((unsigned *)power);

	if (event != USB_EVENT_VBUS) {
		dev_dbg(di->dev, "not a standard host, returning\n");
		return NOTIFY_DONE;
	}

	/* TODO: State is fabricate  here. See if charger really needs USB
	 * state or if mA is enough
	 */
	if ((di->usb_state.usb_current == 2) && (mA > 2))
		bm_usb_state = AB5500_BM_USB_STATE_RESUME;
	else if (mA == 0)
		bm_usb_state = AB5500_BM_USB_STATE_RESET_HS;
	else if (mA == 2)
		bm_usb_state = AB5500_BM_USB_STATE_SUSPEND;
	else if (mA >= 8) /* 8, 100, 500 */
		bm_usb_state = AB5500_BM_USB_STATE_CONFIGURED;
	else /* Should never occur */
		bm_usb_state = AB5500_BM_USB_STATE_RESET_FS;

	dev_dbg(di->dev, "%s usb_state: 0x%02x mA: %d\n",
		__func__, bm_usb_state, mA);

	spin_lock(&di->usb_state.usb_lock);
	di->usb_state.usb_changed = true;
	di->usb_state.state = bm_usb_state;
	di->usb_state.usb_current = mA;
	spin_unlock(&di->usb_state.usb_lock);

	queue_work(di->charger_wq, &di->usb_state_changed_work);

	return NOTIFY_OK;
}

#if defined(CONFIG_PM)
static int ab5500_charger_resume(struct platform_device *pdev)
{
	struct ab5500_charger *di = platform_get_drvdata(pdev);

	/* If we still have a HW failure, schedule a new check */
	if (di->flags.usbchargernotok || di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, 0);
	}

	return 0;
}

static int ab5500_charger_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab5500_charger *di = platform_get_drvdata(pdev);

	/* Cancel any pending HW failure check */
	if (delayed_work_pending(&di->check_hw_failure_work))
		cancel_delayed_work(&di->check_hw_failure_work);

	return 0;
}
#else
#define ab5500_charger_suspend      NULL
#define ab5500_charger_resume       NULL
#endif

static int __devexit ab5500_charger_remove(struct platform_device *pdev)
{
	struct ab5500_charger *di = platform_get_drvdata(pdev);
	int i, irq;

	/* Disable USB charging */
	ab5500_charger_usb_en(&di->usb_chg, false, 0, 0);

	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab5500_charger_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab5500_charger_irq[i].name);
		free_irq(irq, di);
	}

	otg_unregister_notifier(di->otg, &di->nb);
	otg_put_transceiver(di->otg);

	/* Delete the work queue */
	destroy_workqueue(di->charger_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->usb_chg.psy);
	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return 0;
}

static int __devinit ab5500_charger_probe(struct platform_device *pdev)
{
	int irq, i, charger_status, ret = 0;
	struct abx500_bm_plat_data *plat_data;

	struct ab5500_charger *di =
		kzalloc(sizeof(struct ab5500_charger), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab5500_gpadc_get("ab5500-adc.0");

	/* initialize lock */
	spin_lock_init(&di->usb_state.usb_lock);

	plat_data = pdev->dev.platform_data;
	di->pdata = plat_data->charger;
	di->bat = plat_data->battery;

	/* get charger specific platform data */
	if (!di->pdata) {
		dev_err(di->dev, "no charger platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}

	/* get battery specific platform data */
	if (!di->bat) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	/* USB supply */
	/* power_supply base class */
	di->usb_chg.psy.name = "ab5500_usb";
	di->usb_chg.psy.type = POWER_SUPPLY_TYPE_USB;
	di->usb_chg.psy.properties = ab5500_charger_usb_props;
	di->usb_chg.psy.num_properties = ARRAY_SIZE(ab5500_charger_usb_props);
	di->usb_chg.psy.get_property = ab5500_charger_usb_get_property;
	di->usb_chg.psy.supplied_to = di->pdata->supplied_to;
	di->usb_chg.psy.num_supplicants = di->pdata->num_supplicants;
	/* ux500_charger sub-class */
	di->usb_chg.ops.enable = &ab5500_charger_usb_en;
	di->usb_chg.ops.kick_wd = &ab5500_charger_watchdog_kick;
	di->usb_chg.ops.update_curr = &ab5500_charger_update_charger_current;
	di->usb_chg.max_out_volt = ab5500_charger_voltage_map[
		ARRAY_SIZE(ab5500_charger_voltage_map) - 1];
	di->usb_chg.max_out_curr = ab5500_charger_current_map[
		ARRAY_SIZE(ab5500_charger_current_map) - 1];


	/* Create a work queue for the charger */
	di->charger_wq =
		create_singlethread_workqueue("ab5500_charger_wq");
	if (di->charger_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for HW failure check */
	INIT_DELAYED_WORK_DEFERRABLE(&di->check_hw_failure_work,
		ab5500_charger_check_hw_failure_work);
	INIT_DELAYED_WORK_DEFERRABLE(&di->check_usbchgnotok_work,
		ab5500_charger_check_usbchargernotok_work);

	/* Init work for charger detection */
	INIT_WORK(&di->usb_link_status_work,
		ab5500_charger_usb_link_status_work);
	INIT_WORK(&di->detect_usb_type_work,
		ab5500_charger_detect_usb_type_work);

	INIT_WORK(&di->usb_state_changed_work,
		ab5500_charger_usb_state_changed_work);

	/* Init work for checking HW status */
	INIT_WORK(&di->check_usb_thermal_prot_work,
		ab5500_charger_check_usb_thermal_prot_work);

	/* Get Chip ID of the ABB ASIC  */
	ret = abx500_get_chip_id(di->dev);
	if (ret < 0) {
		dev_err(di->dev, "failed to get chip ID\n");
		goto free_charger_wq;
	}
	di->chip_id = ret;
	dev_dbg(di->dev, "AB5500 CID is: 0x%02x\n", di->chip_id);

	/* Initialize OVV, and other registers */
	ret = ab5500_charger_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize ABB registers\n");
		goto free_device_info;
	}

	/* Register USB charger class */
	ret = power_supply_register(di->dev, &di->usb_chg.psy);
	if (ret) {
		dev_err(di->dev, "failed to register USB charger\n");
		goto free_device_info;
	}

	di->otg = otg_get_transceiver();
	if (!di->otg) {
		dev_err(di->dev, "failed to get otg transceiver\n");
		goto free_usb;
	}
	di->nb.notifier_call = ab5500_charger_usb_notifier_call;
	ret = otg_register_notifier(di->otg, &di->nb);
	if (ret) {
		dev_err(di->dev, "failed to register otg notifier\n");
		goto put_otg_transceiver;
	}

	/* Identify the connected charger types during startup */
	charger_status = ab5500_charger_detect_chargers(di);
	if (charger_status & USB_PW_CONN) {
		dev_dbg(di->dev, "VBUS Detect during startup\n");
		di->vbus_detected = true;
		di->vbus_detected_start = true;
		queue_work(di->charger_wq,
			&di->usb_link_status_work);
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab5500_charger_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab5500_charger_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab5500_charger_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab5500_charger_irq[i].name, di);

		if (ret != 0) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab5500_charger_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab5500_charger_irq[i].name, irq, ret);
	}

	platform_set_drvdata(pdev, di);

	dev_info(di->dev, "probe success\n");
	return ret;

free_irq:
	otg_unregister_notifier(di->otg, &di->nb);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab5500_charger_irq[i].name);
		free_irq(irq, di);
	}
put_otg_transceiver:
	otg_put_transceiver(di->otg);
free_usb:
	power_supply_unregister(&di->usb_chg.psy);
free_charger_wq:
	destroy_workqueue(di->charger_wq);
free_device_info:
	kfree(di);

	return ret;
}

static struct platform_driver ab5500_charger_driver = {
	.probe = ab5500_charger_probe,
	.remove = __devexit_p(ab5500_charger_remove),
	.suspend = ab5500_charger_suspend,
	.resume = ab5500_charger_resume,
	.driver = {
		.name = "ab5500-charger",
		.owner = THIS_MODULE,
	},
};

static int __init ab5500_charger_init(void)
{
	return platform_driver_register(&ab5500_charger_driver);
}

static void __exit ab5500_charger_exit(void)
{
	platform_driver_unregister(&ab5500_charger_driver);
}

subsys_initcall_sync(ab5500_charger_init);
module_exit(ab5500_charger_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab5500-charger");
MODULE_DESCRIPTION("AB5500 charger management driver");
