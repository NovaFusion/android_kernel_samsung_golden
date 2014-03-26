/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Charger driver for AB8500
 *
 * License Terms: GNU General Public License v2
 * Author: Johan Palsson <johan.palsson@stericsson.com>
 * Author: Karl Komierowski <karl.komierowski@stericsson.com>
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/usb/otg.h>
#include <linux/wakelock.h>
#include <linux/mutex.h>
#include <linux/timer.h>

#if defined( CONFIG_USB_SWITCHER )
#include <linux/usb_switcher.h>
#elif defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
#include <linux/input/ab8505_micro_usb_iddet.h>
#endif

#define MAIN_CH_OUT_CUR_LIM		0xf6
#define MAIN_CH_OUT_CUR_LIM_SHIFT	4
#define MAIN_CH_OUT_CUR_LIM_100_MA	0
#define MAIN_CH_OUT_CUR_LIM_200_MA	1
#define MAIN_CH_OUT_CUR_LIM_300_MA	2
#define MAIN_CH_OUT_CUR_LIM_400_MA	3
#define MAIN_CH_OUT_CUR_LIM_500_MA	4
#define MAIN_CH_OUT_CUR_LIM_600_MA	5
#define MAIN_CH_OUT_CUR_LIM_700_MA	6
#define MAIN_CH_OUT_CUR_LIM_800_MA	7
#define MAIN_CH_OUT_CUR_LIM_900_MA	8
#define MAIN_CH_OUT_CUR_LIM_1000_MA	9
#define MAIN_CH_OUT_CUR_LIM_1100_MA	0xa
#define MAIN_CH_OUT_CUR_LIM_1200_MA	0xb
#define MAIN_CH_OUT_CUR_LIM_1300_MA	0xc
#define MAIN_CH_OUT_CUR_LIM_1400_MA	0xd
#define MAIN_CH_OUT_CUR_LIM_1500_MA	0xe

#define MAIN_CH_OUT_CUR_LIM_ENABLE	1


/* Charger constants */
#define NO_PW_CONN			0
#define AC_PW_CONN			1
#define USB_PW_CONN			2

#define MAIN_WDOG_ENA			0x01
#define MAIN_WDOG_KICK			0x02
#define MAIN_WDOG_DIS			0x00
#define CHARG_WD_KICK			0x01
#define MAIN_CH_ENA			0x01
#define MAIN_CH_NO_OVERSHOOT_ENA_N	0x02
#define USB_CH_ENA			0x01
#define USB_CHG_NO_OVERSHOOT_ENA_N	0x02
#define MAIN_CH_DET			0x01
#define MAIN_CH_CV_ON			0x04
#define USB_CH_CV_ON			0x08
#define VBUS_DET_DBNC100		0x02
#define VBUS_DET_DBNC1			0x01
#define OTP_ENABLE_WD			0x01
#define USB_CH_DET			0x01
#define AUTO_VBUS_CHG_INP_CURR_MASK		0xF0
#define AUTO_VBUS_CHG_INP_CURR_SHIFT	4

#define MAIN_CH_INPUT_CURR_SHIFT	4
#define VBUS_IN_CURR_LIM_SHIFT		4

#define LED_INDICATOR_PWM_ENA		0x01
#define LED_INDICATOR_PWM_DIS		0x00
#define LED_IND_CUR_5MA			0x04
#define LED_INDICATOR_PWM_DUTY_252_256	0xBF

/* HW failure constants */
#define MAIN_CH_TH_PROT			0x02
#define VBUS_CH_NOK			0x08
#define USB_CH_TH_PROT			0x02
#define VBUS_OVV_TH			0x01
#define MAIN_CH_NOK			0x01
#define VBUS_DET			0x80

#define MAIN_CH_STATUS2_MAINCHGDROP		0x80
#define MAIN_CH_STATUS2_MAINCHARGERDETDBNC	0x40
#define USB_CH_VBUSDROP				0x40
#define USB_CH_VBUSDETDBNC			0x01

/* UsbLineStatus register bit masks */
#define AB8500_USB_LINK_STATUS		0x78
#define AB8505_USB_LINK_STATUS		0xF8
#define AB8500_STD_HOST_SUSP		0x18

/* Watchdog timeout constant */
#define WD_TIMER			0x30 /* 4min */
#define WD_KICK_INTERVAL		(60 * HZ)

/* Lowest charger voltage is 3.39V -> 0x4E */
#define LOW_VOLT_REG			0x4E

/* Step up/down delay in us */
#define STEP_UDELAY			10000

/* Wait for enumeration before charging in ms */
#define WAIT_FOR_USB_ENUMERATION	5 * 1000

#define CHARGER_STATUS_POLL 10 /* in ms */

#define CHG_WD_INTERVAL			(60 * HZ)

#define AB8500_SW_CONTROL_FALLBACK	0x03

/* UsbLineStatus register - usb types */
enum ab8500_charger_link_status {
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
	USB_STAT_PHY_EN,
	USB_STAT_SUP_NO_IDGND_VBUS,
	USB_STAT_SUP_IDGND_VBUS,
	USB_STAT_CHARGER_LINE_1,
	USB_STAT_CARKIT_1,
	USB_STAT_CARKIT_2,
	USB_STAT_ACA_DOCK_CHARGER,
};

enum ab8500_usb_state {
	AB8500_BM_USB_STATE_RESET_HS,	/* HighSpeed Reset */
	AB8500_BM_USB_STATE_RESET_FS,	/* FullSpeed/LowSpeed Reset */
	AB8500_BM_USB_STATE_CONFIGURED,
	AB8500_BM_USB_STATE_SUSPEND,
	AB8500_BM_USB_STATE_RESUME,
	AB8500_BM_USB_STATE_MAX,
};

/* VBUS input current limits supported in AB8500 in mA */
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
#define MAIN_CH_IP_CUR_LVL_0P1         100
#define MAIN_CH_IP_CUR_LVL_0P2         200
#define MAIN_CH_IP_CUR_LVL_0P3         300
#define MAIN_CH_IP_CUR_LVL_0P4         400
#define MAIN_CH_IP_CUR_LVL_0P5         500
#define MAIN_CH_IP_CUR_LVL_0P6         600
#define MAIN_CH_IP_CUR_LVL_0P7         700
#define MAIN_CH_IP_CUR_LVL_0P8         800
#define MAIN_CH_IP_CUR_LVL_0P9         900
#define MAIN_CH_IP_CUR_LVL_1P0         1000
#define MAIN_CH_IP_CUR_LVL_1P1         1100
#define MAIN_CH_IP_CUR_LVL_1P2         1200
#define MAIN_CH_IP_CUR_LVL_1P3         1300
#define MAIN_CH_IP_CUR_LVL_1P4         1400
#define MAIN_CH_IP_CUR_LVL_1P5         1500

#define VBAT_TRESH_IP_CUR_RED		3800
#define VBAT_TRESH_BL1			0100
#define VBAT_TRESH_BL2			0050


#define to_ab8500_charger_usb_device_info(x) container_of((x), \
	struct ab8500_charger, usb_chg)
#define to_ab8500_charger_ac_device_info(x) container_of((x), \
	struct ab8500_charger, ac_chg)

#define MAX_DEDICATED_CHARGER_CURRENT 	USB_CH_IP_CUR_LVL_0P6
#define MAX_NON_ENUMERATED_USB_CURRENT  USB_CH_IP_CUR_LVL_0P5

/**
 * struct ab8500_charger_interrupts - ab8500 interupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab8500_charger_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab8500_charger_info {
	int charger_connected;
	int charger_online;
	int charger_voltage;
	int cv_active;
	bool wd_expired;
	int charger_current;
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int charger_suspended;
#endif
};

struct ab8500_charger_event_flags {
	bool mainextchnotok;
	bool main_thermal_prot;
	bool usb_thermal_prot;
	bool vbus_ovv;
	bool usbchargernotok;
	bool chgwdexp;
	bool vbus_collapse;
};

struct ab8500_charger_usb_state {
	bool usb_changed;
	int usb_current;
	enum ab8500_usb_state state;
	spinlock_t usb_lock;
};

/**
 * struct ab8500_charger - ab8500 Charger device information
 * @dev:		Pointer to the structure device
 * @max_usb_in_curr:	Max USB charger input current
 * @vbus_detected:	VBUS detected
 * @vbus_detected_start:
 *			VBUS detected during startup
 * @ac_conn:		This will be true when the AC charger has been plugged
 * @vddadc_en_ac:	Indicate if VDD ADC supply is enabled because AC
 *			charger is enabled
 * @vddadc_en_usb:	Indicate if VDD ADC supply is enabled because USB
 *			charger is enabled
 * @vbat		Battery voltage
 * @old_vbat		Previously measured battery voltage
 * @usb_device_is_unrecognised	USB device is unrecognised by the hardware
 * @autopower		Indicate if we should have automatic pwron after pwrloss
 * @link_status:	The link status received from the micro usb
 * @invalid_charger_detect_state	State when forcing AB to use invalid charger
 * @parent:		Pointer to the struct ab8500
 * @gpadc:		Pointer to the struct gpadc
 * @pdata:		Pointer to the ab8500_charger platform data
 * @bat:		Pointer to the ab8500_bm platform data
 * @flags:		Structure for information about events triggered
 * @usb_state:		Structure for usb stack information
 * @ac_chg:		AC charger power supply
 * @usb_chg:		USB charger power supply
 * @ac:			Structure that holds the AC charger properties
 * @usb:		Structure that holds the USB charger properties
 * @regu:		Pointer to the struct regulator
 * @charger_wq:		Work queue for the IRQs and checking HW state
 * @check_vbat_work	Work for checking vbat threshold to adjust vbus current
 * @check_hw_failure_work:	Work for checking HW state
 * @check_usbchgnotok_work:	Work for checking USB charger not ok status
 * @kick_wd_work:		Work for kicking the charger watchdog in case
 *				of ABB rev 1.* due to the watchog logic bug
 * @attach_work:		Work for checking the usb enumeration
 * @ac_charger_attached_work:	Work for checking if AC charger is still
 *				connected
 * @usb_charger_attached_work:	Work for checking if USB charger is still
 *				connected
 * @ac_work:			Work for checking AC charger connection
 * @detect_usb_type_work:	Work for detecting the USB type connected
 * @usb_link_status_work:	Work for checking the new USB link status
 * @usb_state_changed_work:	Work for checking USB state
 * @check_main_thermal_prot_work:
 *				Work for checking Main thermal status
 * @check_usb_thermal_prot_work:
 *				Work for checking USB thermal status
 * @ac_charger_attached_wake_lock:	Blocks suspend as long as a charger is
 *					connected
 * @usb_charger_attached_wake_lock:	Blocks suspend as long as a charger is
 *					connected
 * @charger_attached_mutex:	For controlling the wakelock
 */
struct ab8500_charger {
	struct device *dev;
	int max_usb_in_curr;
	bool vbus_detected;
	bool vbus_detected_start;
	bool ac_conn;
	bool vddadc_en_ac;
	bool vddadc_en_usb;
	int vbat;
	int old_vbat;
	bool usb_device_is_unrecognised;
	bool autopower;
	int invalid_charger_detect_state;
	int link_status;
	struct ab8500 *parent;
	struct ab8500_gpadc *gpadc;
	struct ab8500_charger_platform_data *pdata;
	struct ab8500_bm_data *bat;
	struct ab8500_charger_event_flags flags;
	struct ab8500_charger_usb_state usb_state;
	struct ux500_charger ac_chg;
	struct ux500_charger usb_chg;
	struct ab8500_charger_info ac;
	struct ab8500_charger_info usb;
	struct regulator *regu;
	struct workqueue_struct *charger_wq;
	struct delayed_work check_vbat_work;
	struct delayed_work check_hw_failure_work;
	struct delayed_work check_usbchgnotok_work;
	struct delayed_work kick_wd_work;
	struct delayed_work attach_work;
#if !defined( CONFIG_USB_SWITCHER ) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	struct delayed_work ac_charger_attached_work;
	struct delayed_work usb_charger_attached_work;
	struct work_struct ac_work;
#endif
	struct work_struct detect_usb_type_work;
	struct work_struct usb_link_status_work;
	struct work_struct usb_state_changed_work;
	struct work_struct check_main_thermal_prot_work;
	struct work_struct check_usb_thermal_prot_work;
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int cable_type;
	bool chg_pw_conn;
	int plugged_action;
	bool usb_connection_timeout ;
	bool vbus_detect_charging;
	int registered_with_power ;
	struct delayed_work ac_work;
	struct work_struct handle_main_voltage_drop_work;
	struct work_struct handle_vbus_voltage_drop_work;
	struct work_struct tsp_vbus_notify_work;
	struct timer_list  usb_link_status_timer;
	struct wake_lock ab8500_vbus_wake_lock;
	struct wake_lock ab8500_vbus_detect_charging_lock;
	struct notifier_block usbsw_nb ;
	struct platform_device *pdev ;

#endif
	struct otg_transceiver *otg;
	struct notifier_block usb_nb;
	struct notifier_block musb_nb;
	struct wake_lock ac_charger_attached_wake_lock;
	struct wake_lock usb_charger_attached_wake_lock;
	struct mutex charger_attached_mutex;
#endif
};

/* AC properties */
static enum power_supply_property ab8500_charger_ac_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_AVG,
#endif
};

/* USB properties */
static enum power_supply_property ab8500_charger_usb_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	POWER_SUPPLY_PROP_STATUS,
#endif
};

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
bool vbus_state = 0;
EXPORT_SYMBOL(vbus_state);
#endif

#ifdef CONFIG_MACH_JANICE
extern void cypress_touchkey_change_thd(bool vbus_status);
static void (*cypress_touchkey_ta_status)(bool vbus_status);
extern void mxt224e_ts_change_vbus_state(bool vbus_status);
static void (*mxt224e_ts_vbus_state)(bool vbus_status);
#endif

#if defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_SEC_KYLE)
extern int use_ab8505_iddet;
#endif

/*
 * Function for enabling and disabling sw fallback mode
 * should always be disabled when no charger is connected.
 */
static void ab8500_enable_disable_sw_fallback(struct ab8500_charger *di,
		bool fallback)
{
	u8 val;
	u8 reg;
	u8 bank;
	u8 bit;
	int ret;

	dev_dbg(di->dev, "SW Fallback: %d\n", fallback);

	if (is_ab8500(di->parent)) {
		bank = 0x15;
		reg = 0x0;
		bit = 3;

		/* read the register containing fallback bit */
		ret = abx500_get_register_interruptible(di->dev, bank, reg, &val);
		if (ret < 0) {
			dev_err(di->dev, "%d read failed\n", __LINE__);
			return;
		}

		/* enable the OPT emulation registers */
		ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x2);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			return;
		}

		if (fallback)
			val |= (1 << bit);
		else
			val &= ~(1 << bit);

		/* write back the changed fallback bit value to register */
		ret = abx500_set_register_interruptible(di->dev, bank, reg, val);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			return;
		}

		/* disable the set OTP registers again */
		ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x0);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			return;
		}
	} else {
		bank = AB8500_SYS_CTRL1_BLOCK;
		reg = AB8500_SW_CONTROL_FALLBACK;
		bit = 0;
	}

	/* read the register containing fallback bit */
	ret = abx500_get_register_interruptible(di->dev, bank, reg, &val);
	if (ret < 0) {
		dev_err(di->dev, "%d read failed\n", __LINE__);
		return;
	}

	if (is_ab8500(di->parent)) {
		/* enable the OPT emulation registers */
		ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x2);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			goto disable_otp;
		}
	}

	if (fallback)
		val |= (1 << bit);
	else
		val &= ~(1 << bit);

	/* write back the changed fallback bit value to register */
	ret = abx500_set_register_interruptible(di->dev, bank, reg, val);
	if (ret) {
		dev_err(di->dev, "%d write failed\n", __LINE__);
	}

disable_otp:
	if (is_ab8500(di->parent)) {
		/* disable the set OTP registers again */
		ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x0);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
		}
	}
}

/**
 * ab8500_power_supply_changed - a wrapper with local extentions for
 * power_supply_changed
 * @di:	  pointer to the ab8500_charger structure
 * @psy:  pointer to power_supply_that have changed.
 *
 */
static void ab8500_power_supply_changed(struct ab8500_charger *di,
					struct power_supply *psy)
{
	if (di->pdata->autopower_cfg) {
		if (!di->usb.charger_connected &&
		    !di->ac.charger_connected &&
		    di->autopower) {
			di->autopower = false;
			ab8500_enable_disable_sw_fallback(di, false);
		} else if (!di->autopower &&
			   (di->ac.charger_connected ||
			    di->usb.charger_connected)) {
			di->autopower = true;
			ab8500_enable_disable_sw_fallback(di, true);
		}
	}
	power_supply_changed(psy);
}

static void ab8500_charger_set_usb_connected(struct ab8500_charger *di,
	bool connected)
{
	if (connected != di->usb.charger_connected) {
		dev_dbg(di->dev, "USB connected:%i\n", connected);
		di->usb.charger_connected = connected;
		sysfs_notify(&di->usb_chg.psy.dev->kobj, NULL, "present");
#if !defined( CONFIG_USB_SWITCHER ) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		if (connected) {
			mutex_lock(&di->charger_attached_mutex);
			if (!wake_lock_active(&di->usb_charger_attached_wake_lock))
				wake_lock(&di->usb_charger_attached_wake_lock);
			mutex_unlock(&di->charger_attached_mutex);

			queue_delayed_work(di->charger_wq,
					   &di->usb_charger_attached_work,
					   HZ);
		} else {
			cancel_delayed_work_sync(&di->usb_charger_attached_work);
			mutex_lock(&di->charger_attached_mutex);
			if (wake_lock_active(&di->usb_charger_attached_wake_lock))
				wake_unlock(&di->usb_charger_attached_wake_lock);
			mutex_unlock(&di->charger_attached_mutex);
		}
#endif
	}
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
static void ab8500_charger_set_ac_connected(struct ab8500_charger *di,
	bool connected)
{
	if (connected != di->ac.charger_connected) {
		dev_dbg(di->dev, "AC connected:%i\n", connected);
		di->ac.charger_connected = connected;
		sysfs_notify(&di->ac_chg.psy.dev->kobj, NULL, "present");
	}
}
#endif

/**
 * ab8500_charger_get_ac_voltage() - get ac charger voltage
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns ac charger voltage (on success)
 */
static int ab8500_charger_get_ac_voltage(struct ab8500_charger *di)
{
	int vch;

	/* Only measure voltage if the charger is connected */
	if (di->usb.charger_connected || di->ac.charger_connected) {
		vch = ab8500_gpadc_convert(di->gpadc, MAIN_CHARGER_V);
		if (vch < 0)
			dev_err(di->dev, "%s gpadc conv failed,\n", __func__);
	} else {
		vch = 0;
	}
	return vch;
}

/**
 * ab8500_charger_ac_cv() - check if the main charger is in CV mode
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns ac charger CV mode (on success) else error code
 */
static int ab8500_charger_ac_cv(struct ab8500_charger *di)
{
	u8 val;
	int ret = 0;

	/* Only check CV mode if the charger is online */
	if (di->ac.charger_online) {
		ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_STATUS1_REG, &val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return 0;
		}

		if (val & MAIN_CH_CV_ON)
			ret = 1;
		else
			ret = 0;
	}

	return ret;
}

/**
 * ab8500_charger_get_vbus_voltage() - get vbus voltage
 * @di:		pointer to the ab8500_charger structure
 *
 * This function returns the vbus voltage.
 * Returns vbus voltage (on success)
 */
static int ab8500_charger_get_vbus_voltage(struct ab8500_charger *di)
{
	int vch;

	/* Only measure voltage if the charger is connected */
	if (di->usb.charger_connected || di->ac.charger_connected) {
		vch = ab8500_gpadc_convert(di->gpadc, VBUS_V);
		if (vch < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		vch = 0;
	}
	return vch;
}

/**
 * ab8500_charger_get_usb_current() - get usb charger current
 * @di:		pointer to the ab8500_charger structure
 *
 * This function returns the usb charger current.
 * Returns usb current (on success) and error code on failure
 */
static int ab8500_charger_get_usb_current(struct ab8500_charger *di)
{
	int ich;

	/* Only measure current if the charger is online */
	if (di->usb.charger_online || di->ac.charger_online) {
		ich = ab8500_gpadc_convert(di->gpadc, USB_CHARGER_C);
		if (ich < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		ich = 0;
	}
	return ich;
}

#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
/*
	ab8500_charger_set_main_charger_usb_current_limiter
	This function sets a register which limits the charger current
	if the USB connection is active and in high speed mode
*/
static void ab8500_charger_set_main_charger_usb_current_limiter(struct ab8500_charger *di)
{

	abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			MAIN_CH_OUT_CUR_LIM,
			MAIN_CH_OUT_CUR_LIM_ENABLE|
			(MAIN_CH_OUT_CUR_LIM_900_MA<< MAIN_CH_OUT_CUR_LIM_SHIFT));

}
#endif

/**
 * ab8500_charger_get_ac_current() - get ac charger current
 * @di:		pointer to the ab8500_charger structure
 *
 * This function returns the ac charger current.
 * Returns ac current (on success) and error code on failure.
 */
static int ab8500_charger_get_ac_current(struct ab8500_charger *di)
{
	int ich;

	/* Only measure current if the charger is online */
	if (di->ac.charger_online) {
		ich = ab8500_gpadc_convert(di->gpadc, MAIN_CHARGER_C);
		if (ich < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		ich = 0;
	}
	return ich;
}

/**
 * ab8500_charger_usb_cv() - check if the usb charger is in CV mode
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns ac charger CV mode (on success) else error code
 */

static int ab8500_charger_usb_cv(struct ab8500_charger *di)
{
	int ret;
	u8 val;

	/* Only check CV mode if the charger is online */
	if (di->usb.charger_online) {
		ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_USBCH_STAT1_REG, &val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return 0;
		}

		if (val & USB_CH_CV_ON)
			ret = 1;
		else
			ret = 0;
	} else {
		ret = 0;
	}

	return ret;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/* For checking VBUS status */

static int ab8500_vbus_is_detected(struct ab8500_charger *di)
{
	u8 data;
	int vbus;

	if ( is_ab8500(di->parent) ){
		abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
					  AB8500_CH_STATUS1_REG, &data);

		vbus = data & 0x1;
	}
	else{
		/* AB8505 and AB9540 only have USB charger input */
		abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
					  AB8500_CH_USBCH_STAT1_REG, &data);

		vbus = ((data & VBUS_DET_DBNC1) && (data & VBUS_DET_DBNC100));
	}

	return vbus;
}

static int ab8500_usb_line_state(struct ab8500_charger *di)
{
	u8 data;

	abx500_get_register_interruptible(di->dev, AB8500_USB,
			AB8500_USB_LINE_STAT_REG, &data);

	return (data & AB8500_USB_LINK_STATUS) >> 3;
}
#endif

/**
 * ab8500_charger_detect_chargers() - Detect the connected chargers
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns the type of charger connected.
 * For USB it will not mean we can actually charge from it
 * but that there is a USB cable connected that we have to
 * identify. This is used during startup when we don't get
 * interrupts of the charger detection
 *
 * Returns an integer value, that means,
 * NO_PW_CONN  no power supply is connected
 * AC_PW_CONN  if the AC power supply is connected
 * USB_PW_CONN  if the USB power supply is connected
 * AC_PW_CONN + USB_PW_CONN if USB and AC power supplies are both connected
 */
static int ab8500_charger_detect_chargers(struct ab8500_charger *di)
{
	int result = NO_PW_CONN;

#if defined( CONFIG_USB_SWITCHER )
	unsigned long connection ;
	int usb_line_state;

	connection = usb_switch_get_current_connection() ;

	/* If the USBSW state is currently disconnected then no connection  */
	if ( connection & USB_SWITCH_DISCONNECTION_EVENT ){
		connection = 0;
		usb_line_state =  0;
		vbus_state = 0;
	}else{
		usb_line_state =  ab8500_usb_line_state(di);
		vbus_state = ab8500_vbus_is_detected(di);
	}

	dev_dbg(di->dev, "%s: REG= 0x%x, vbus_state= %d, "
		 "usb_line_state= 0x%x\n",
		 __func__, (unsigned int) connection,
		 vbus_state,
		 usb_line_state);

	wake_lock_timeout(&di->ab8500_vbus_wake_lock, 2 * HZ);

	if (!(connection & 0xFFFFF) && vbus_state ){
		connection = EXTERNAL_MISC;
		di->vbus_detect_charging = true;
	}
	else{
		di->vbus_detect_charging = false;
	}

	/* For 12v Carkit charger */
	if (((connection & 0xFFFFF) & EXTERNAL_USB) &&
	     usb_line_state == 0x7)
		connection = EXTERNAL_DEDICATED_CHARGER;

	switch (connection & 0xFFFFF)
	{
	case	EXTERNAL_MISC:
		dev_dbg(di->dev, "No cable type, VBUS detected. Charging "
			"enabled\n");
	case	EXTERNAL_DEDICATED_CHARGER:
	case	EXTERNAL_CAR_KIT:
	case	EXTERNAL_USB_CHARGER:
		dev_dbg(di->dev,"%s: TA is inserted\n", __func__);
		wake_lock(&di->ab8500_vbus_wake_lock);
		di->cable_type = POWER_SUPPLY_TYPE_MAINS;
		result = AC_PW_CONN ;
		break ;

	case	EXTERNAL_JIG_USB_OFF:
	case	EXTERNAL_USB:
		wake_lock(&di->ab8500_vbus_wake_lock);
		dev_dbg(di->dev,"%s: USB is inserted\n", __func__);
		di->cable_type = POWER_SUPPLY_TYPE_USB;
		result = USB_PW_CONN ;
		break ;

	case	EXTERNAL_JIG_UART_OFF:
		if (vbus_state) {
			/* This case is provided for Reliabilty group to test fuelguage.*/
			wake_lock(&di->ab8500_vbus_wake_lock);
			dev_info(di->dev,"%s: JIG+VBUS cable is inserted\n", __func__);
			di->cable_type = POWER_SUPPLY_TYPE_USB;
			result = USB_PW_CONN;
		} else {
			dev_info(di->dev,
			 "%s: JIG cable is inserted, but there is no VBUS\n", __func__);
			di->cable_type = POWER_SUPPLY_TYPE_BATTERY;
			result = NO_PW_CONN;
		}
		break;

	case	EXTERNAL_USB_OTG:
	case	EXTERNAL_DEVICE_UNKNOWN:
	case	EXTERNAL_UART:
	case	EXTERNAL_AV_CABLE:
	case	EXTERNAL_PHONE_POWERED_DEVICE:
	case	EXTERNAL_TTY:
	case	EXTERNAL_AUDIO_1:
	case	EXTERNAL_AUDIO_2:
	default:
		dev_info(di->dev,"%s: Cable is disconnected\n", __func__);
		di->cable_type = POWER_SUPPLY_TYPE_BATTERY;
		result = NO_PW_CONN ;
		break ;
	}
#else
	int ret;
	u8 val;

	/* Check for AC charger */
	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_STATUS1_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	if (val & MAIN_CH_DET)
		result = AC_PW_CONN;

	/* Check for USB charger */
	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_USBCH_STAT1_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	if ((val & VBUS_DET_DBNC1) && (val & VBUS_DET_DBNC100))
		result |= USB_PW_CONN;
#endif

	return result;
}

/**
 * ab8500_charger_max_usb_curr() - get the max curr for the USB type
 * @di:			pointer to the ab8500_charger structure
 * @link_status:	the identified USB type
 *
 * Get the maximum current that is allowed to be drawn from the host
 * based on the USB type.
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_charger_max_usb_curr(struct ab8500_charger *di,
		enum ab8500_charger_link_status link_status)
{
	int ret = 0;

	di->usb_device_is_unrecognised = false;

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int vbus_status;
	unsigned long connection ;

	/*we could have got here via an interrupt or a timer */
	del_timer_sync(&di->usb_link_status_timer);

	if (ab8500_charger_detect_chargers(di) == AC_PW_CONN)
	{
		di->max_usb_in_curr = di->bat->chg_params->ac_curr_max ;
	}
	else
#endif
	{
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		if ( link_status != USB_STAT_STD_HOST_NC) /* we can see anything other than a non charging USB host  */
			di->usb_connection_timeout = 0 ;  /* clear timeout flag */
#endif

		switch (link_status) {
		case USB_STAT_STD_HOST_NC:
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		{
			if (di->usb_connection_timeout) {
				/*
				 The timer has gone off and the gadget driver hasn't enumerated
				 so we assume it isn't active we will try to charge with out enumerating
				 we must have been in this state for at least one second.
				*/
				 di->max_usb_in_curr =  MAX_NON_ENUMERATED_USB_CURRENT ;
			  break ;
			} else {
				/* start a timer to retry detection in a second */
				mod_timer(&di->usb_link_status_timer,jiffies+(HZ*2));
				ret = -1;
			}
			break ;
		} //fall though intended
#endif
		case USB_STAT_STD_HOST_C_NS:
		case USB_STAT_STD_HOST_C_S:
			dev_info(di->dev, "%s: USB Type - Standard host is "
				"detected through USB driver\n", __func__);
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
			di->max_usb_in_curr = di->bat->chg_params->usb_curr_max;
#else
			di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P09;
#endif
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
		case USB_STAT_HOST_CHG_NM:
		case USB_STAT_DEDICATED_CHG:
		case USB_STAT_ACA_RID_C_NM:
		case USB_STAT_ACA_RID_C_HS_CHIRP:
			di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P5;
			break;
		case USB_STAT_NOT_CONFIGURED:
			if (di->vbus_detected) {
				di->usb_device_is_unrecognised = true;
				dev_dbg(di->dev, "USB Type - Legacy charger.\n");
				di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P5;
				break;
			}
		case USB_STAT_HM_IDGND:
			dev_info(di->dev, "%s: USB Type - Charging not allowed\n", __func__);
			di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
			ret = -ENXIO;
			break;
		case USB_STAT_RESERVED:
			if (is_ab8500(di->parent)) {
			/*
			 * This state is used to indicate that VBUS has dropped below
			 * the detection level 4 times in a row. This is due to the
			 * charger output current is set to high making the charger
			 * voltage collapse. This have to be propagated through to
			 * chargalg. This is done using the property
			 * POWER_SUPPLY_PROP_CURRENT_AVG = 1
			 */
				di->flags.vbus_collapse = true;
				dev_info(di->dev, "%s: USB Type - USB_STAT_RESERVED "
					"VBUS has collapsed\n", __func__ );
				ret = -ENXIO;
			break;
			}
			if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
				dev_dbg(di->dev, "USB Type - Charging not allowed\n");
				di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
				dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d",
					link_status, di->max_usb_in_curr);
				ret = -ENXIO;
				break;
			}
			break;
		case USB_STAT_CARKIT_1:
		case USB_STAT_CARKIT_2:
		case USB_STAT_ACA_DOCK_CHARGER:
		case USB_STAT_CHARGER_LINE_1:
			di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P5;
			dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d", link_status,
				di->max_usb_in_curr);
			break;
		case USB_STAT_NOT_VALID_LINK:
			dev_err(di->dev, "USB Type invalid - try charging anyway\n");
			di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P5;
			break;

		default:
			dev_info(di->dev, "%s: USB Type - Unknown\n", __func__);
			di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
			ret = -ENXIO;
			break;
		};
	}
	dev_info(di->dev, "%s: USB Type - 0x%02x MaxCurr: %d\n", __func__,
		link_status, di->max_usb_in_curr);

	return ret;
}

/**
 * ab8500_charger_detect_usb_type() - get the type of usb connected
 * @di:		pointer to the ab8500_charger structure
 *
 * Detect the type of the plugged USB
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_charger_detect_usb_type(struct ab8500_charger *di)
{
	int i, ret;
	u8 val;

#if defined( CONFIG_USB_SWITCHER )
	if (usb_switch_get_current_connection() & (EXTERNAL_CAR_KIT|EXTERNAL_DEDICATED_CHARGER)) {
		val= USB_STAT_DEDICATED_CHG ;
	}
	else
#endif
	{
	/*
	 * On getting the VBUS rising edge detect interrupt there
	 * is a 250ms delay after which the register UsbLineStatus
	 * is filled with valid data.
	 */
	for (i = 0; i < 10; i++) {
		msleep(250);
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_INTERRUPT, AB8500_IT_SOURCE21_REG,
			&val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}

		if (is_ab8500(di->parent)) {
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_USB, AB8500_USB_LINE_STAT_REG, &val);
		} else if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_USB, AB8500_USB_LINK1_STAT_REG, &val);
		} else {
			dev_err(di->dev, "%s unsupported analog baseband\n", __func__);
			return -ENXIO;
		}
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}
		/*
		 * Until the IT source register is read the UsbLineStatus
		 * register is not updated, hence doing the same
		 * Revisit this:
		 */

		/* get the USB type */
		if (is_ab8500(di->parent)) {
			val = (val & AB8500_USB_LINK_STATUS) >> 3;
		} else if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
				val = (val & AB8505_USB_LINK_STATUS) >> 3;
		} else {
			dev_err(di->dev, "%s unsupported analog baseband\n", __func__);
			return -ENXIO;
		}
		if (val)
			break;
		}
	}

	ret = ab8500_charger_max_usb_curr(di,
		(enum ab8500_charger_link_status) val);

	return ret;
}

/*
 * This array maps the raw hex value to charger voltage used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_charger_voltage_map[] = {
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
 * This array maps the raw hex value to charger current used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_charger_current_map[] = {
	100 ,
	200 ,
	300 ,
	400 ,
	500 ,
	600 ,
	700 ,
	800 ,
	900 ,
	1000 ,
	1100 ,
	1200 ,
	1300 ,
	1400 ,
	1500 ,
};

/*
 * This array maps the raw hex value to VBUS input current used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_charger_vbus_in_curr_map[] = {
	USB_CH_IP_CUR_LVL_0P05,
	USB_CH_IP_CUR_LVL_0P09,
	USB_CH_IP_CUR_LVL_0P19,
	USB_CH_IP_CUR_LVL_0P29,
	USB_CH_IP_CUR_LVL_0P38,
	USB_CH_IP_CUR_LVL_0P45,
	USB_CH_IP_CUR_LVL_0P5,
	USB_CH_IP_CUR_LVL_0P6,
	USB_CH_IP_CUR_LVL_0P7,
	USB_CH_IP_CUR_LVL_0P8,
	USB_CH_IP_CUR_LVL_0P9,
	USB_CH_IP_CUR_LVL_1P0,
	USB_CH_IP_CUR_LVL_1P1,
	USB_CH_IP_CUR_LVL_1P3,
	USB_CH_IP_CUR_LVL_1P4,
	USB_CH_IP_CUR_LVL_1P5,
};

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/**
 * This array maps the raw hex value to MAIN charger input current used by the
 * AB8500 Values taken from the UM0836
 */
static int ab8500_charger_main_in_curr_map[] = {
       MAIN_CH_IP_CUR_LVL_0P1,
       MAIN_CH_IP_CUR_LVL_0P2,
       MAIN_CH_IP_CUR_LVL_0P3,
       MAIN_CH_IP_CUR_LVL_0P4,
       MAIN_CH_IP_CUR_LVL_0P5,
       MAIN_CH_IP_CUR_LVL_0P6,
       MAIN_CH_IP_CUR_LVL_0P7,
       MAIN_CH_IP_CUR_LVL_0P8,
       MAIN_CH_IP_CUR_LVL_0P9,
       MAIN_CH_IP_CUR_LVL_1P0,
       MAIN_CH_IP_CUR_LVL_1P1,
       MAIN_CH_IP_CUR_LVL_1P2,
       MAIN_CH_IP_CUR_LVL_1P3,
       MAIN_CH_IP_CUR_LVL_1P4,
       MAIN_CH_IP_CUR_LVL_1P5,
};
#endif

static int ab8500_voltage_to_regval(int voltage)
{
	int i;

	/* Special case for voltage below 3.5V */
	if (voltage < ab8500_charger_voltage_map[0])
		return LOW_VOLT_REG;

	for (i = 1; i < ARRAY_SIZE(ab8500_charger_voltage_map); i++) {
		if (voltage < ab8500_charger_voltage_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_charger_voltage_map) - 1;
	if (voltage == ab8500_charger_voltage_map[i])
		return i;
	else
		return -1;
}

static int ab8500_current_to_regval(int curr)
{
	int i;

	if (curr < ab8500_charger_current_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_charger_current_map); i++) {
		if (curr < ab8500_charger_current_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_charger_current_map) - 1;
	if (curr == ab8500_charger_current_map[i])
		return i;
	else
		return -1;
}

static int ab8500_vbus_in_curr_to_regval(int curr)
{
	int i;

	if (curr < ab8500_charger_vbus_in_curr_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_charger_vbus_in_curr_map); i++) {
		if (curr < ab8500_charger_vbus_in_curr_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_charger_vbus_in_curr_map) - 1;
	if (curr == ab8500_charger_vbus_in_curr_map[i])
		return i;
	else
		return -1;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
static int ab8500_main_in_curr_to_regval(int curr)
{
       int i;
       if (curr < ab8500_charger_main_in_curr_map[0])
	       return 0;
       for (i = 0; i < ARRAY_SIZE(ab8500_charger_main_in_curr_map); i++) {
	       if (curr <= ab8500_charger_main_in_curr_map[i])
		       return i - 1;
       }

       /* If not last element, return error */
       i = ARRAY_SIZE(ab8500_charger_main_in_curr_map) - 1;
       if (curr == ab8500_charger_main_in_curr_map[i])
	       return i;
       else
	       return -1;
}
#endif

#ifdef CONFIG_AB8XX0_USBCHRG_ON_MAIN
/**
 * ab8500_charger_get_usbtoac_cur() - get usb current
 * @di:		pointer to the ab8500_charger structre
 *
 * The usb stack provides the maximum current that can be drawn from
 * the standard usb host. This will be in mA.
 * This function converts current in mA to a suitable value that can be
 * written to the Main Charger register, that does not exceed what usb host
 * can provide. Returns -1 if charging is not allowed.
 */
static int ab8500_charger_get_usbtoac_cur(struct ab8500_charger *di)
{
	switch (di->usb_state.usb_current) {
	case 100:
		di->max_usb_in_curr = MAIN_CH_IP_CUR_LVL_0P1;
		break;
	case 200:
		di->max_usb_in_curr = MAIN_CH_IP_CUR_LVL_0P2;
		break;
	case 300:
		di->max_usb_in_curr = MAIN_CH_IP_CUR_LVL_0P3;
		break;
	case 400:
		di->max_usb_in_curr = MAIN_CH_IP_CUR_LVL_0P4;
		break;
	case 500:
		di->max_usb_in_curr = MAIN_CH_IP_CUR_LVL_0P5;
		break;
	default:
		di->max_usb_in_curr = MAIN_CH_IP_CUR_LVL_0P1;
		return -1;
		break;
	};
	return 0;
}
#endif

/**
 * ab8500_charger_get_usb_cur() - get usb current
 * @di:		pointer to the ab8500_charger structre
 *
 * The usb stack provides the maximum current that can be drawn from
 * the standard usb host. This will be in mA.
 * This function converts current in mA to a value that can be written
 * to the register. Returns -1 if charging is not allowed
 */
static int ab8500_charger_get_usb_cur(struct ab8500_charger *di)
{
	switch (di->usb_state.usb_current) {
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
 * ab8500_charger_set_current() - set charger current
 * @di:		pointer to the ab8500_charger structure
 * @ich:	charger current, in mA
 * @reg:	select what charger register to set
 *
 * Set charger current.
 * There is no state machine in the AB to step up/down the charger
 * current to avoid dips and spikes on MAIN, VBUS and VBAT when
 * charging is started. Instead we need to implement
 * this charger current step-up/down here.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_current(struct ab8500_charger *di,
	int ich, int reg)
{
	int ret, i;
	int curr_index, prev_curr_index, shift_value;
	u8 reg_value;

	switch (reg) {
	case AB8500_MCH_IPT_CURLVL_REG:
		shift_value = MAIN_CH_INPUT_CURR_SHIFT;
		curr_index = ab8500_current_to_regval(ich);
		break;
	case AB8500_USBCH_IPT_CRNTLVL_REG:
		shift_value = VBUS_IN_CURR_LIM_SHIFT;
		curr_index = ab8500_vbus_in_curr_to_regval(ich);
		break;
	case AB8500_CH_OPT_CRNTLVL_REG:
		shift_value = 0;
		curr_index = ab8500_current_to_regval(ich);
		break;
	default:
		dev_err(di->dev, "%s current register not valid\n", __func__);
		return -ENXIO;
	}

	if (curr_index < 0) {
		dev_err(di->dev, "requested current limit out-of-range\n");
		return -ENXIO;
	}

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		reg, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s read failed\n", __func__);
		return ret;
	}
	prev_curr_index = (reg_value >> shift_value);

	/* only update current if it's been changed */
	if (prev_curr_index == curr_index)
		return 0;

	dev_dbg(di->dev, "%s set charger current: %d mA for reg: 0x%02x\n",
		__func__, ich, reg);

	if (prev_curr_index > curr_index) {
		for (i = prev_curr_index - 1; i >= curr_index; i--) {
			usleep_range(STEP_UDELAY * 2, STEP_UDELAY * 3);
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER, reg, (u8) i << shift_value);
			if (ret) {
				dev_err(di->dev, "%s write failed\n", __func__);
				return ret;
			}
		}
	} else {
		for (i = prev_curr_index + 1; i <= curr_index; i++) {
			usleep_range(STEP_UDELAY * 2, STEP_UDELAY * 3);
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER, reg, (u8) i << shift_value);
			if (ret) {
				dev_err(di->dev, "%s write failed\n", __func__);
				return ret;
			}
		}
	}
	return ret;
}

/**
 * ab8500_charger_set_vbus_in_curr() - set VBUS input current limit
 * @di:		pointer to the ab8500_charger structure
 * @ich_in:	charger input current limit
 *
 * Sets the current that can be drawn from the USB host
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_vbus_in_curr(struct ab8500_charger *di,
		int ich_in)
{
	int min_value;

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int input_index;
	int imax;

	if ( di->cable_type == POWER_SUPPLY_TYPE_MAINS || di->vbus_detect_charging ) {
		imax = di->bat->chg_params->ac_curr_max;
		min_value = min(imax, ich_in);
	}else if ( di->cable_type == POWER_SUPPLY_TYPE_USB ) {
		imax = di->bat->chg_params->usb_curr_max;
		min_value = min(imax, ich_in);

		/*
		 * Need to reduce USB Charger input current when USB cable
		 * inserted as USB IP block takes some current from VBUS.
		 */
		switch (min_value) {
		case 100:
			if (di->vbat < VBAT_TRESH_IP_CUR_RED)
				min_value = USB_CH_IP_CUR_LVL_0P05;
			break;
		case 500:
			if (di->vbat < VBAT_TRESH_IP_CUR_RED)
				min_value = USB_CH_IP_CUR_LVL_0P45;
			break;
		default:
			break;
		}

	}else{
		imax = 100;
		min_value = min(imax, ich_in);
	}

#else
	/* We should always use to lowest current limit */
	min_value = min(di->bat->chg_params->usb_curr_max, ich_in);

	if (di->usb_state.usb_current >= 0)
		min_value = min(di->usb_state.usb_current, min_value);

	switch (min_value) {
	case 100:
		if (di->vbat < VBAT_TRESH_IP_CUR_RED)
			min_value = USB_CH_IP_CUR_LVL_0P05;
		break;
	case 500:
		if (di->vbat < VBAT_TRESH_IP_CUR_RED)
			min_value = USB_CH_IP_CUR_LVL_0P45;
		break;
	default:
		break;
	}

#endif

	return ab8500_charger_set_current(di, min_value,
		AB8500_USBCH_IPT_CRNTLVL_REG);
}

/**
 * ab8500_charger_set_main_in_curr() - set main charger input current
 * @di:		pointer to the ab8500_charger structure
 * @ich_in:	input charger current, in mA
 *
 * Set main charger input current.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_main_in_curr(struct ab8500_charger *di,
	int ich_in)
{
#if (defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )) && defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
	int input_index;
	int imax;

	if ( di->cable_type == POWER_SUPPLY_TYPE_MAINS || di->vbus_detect_charging ) {
		imax = di->bat->chg_params->ac_curr_max;
	}else if ( di->cable_type == POWER_SUPPLY_TYPE_USB ) {
		imax = di->bat->chg_params->usb_curr_max;
	}else
		imax = 100;

	ich_in = min(imax, ich_in);

	input_index = ab8500_main_in_curr_to_regval(ich_in);
	if (input_index < 0) {
		dev_err(di->dev, "MAIN input current limit too high, %d\n",
			input_index);
		return -ENXIO;
	}

	return ab8500_charger_set_current(di, ich_in,
		AB8500_MCH_IPT_CURLVL_REG);
#else
	return ab8500_charger_set_current(di, ich_in,
		AB8500_MCH_IPT_CURLVL_REG);
#endif

}

/**
 * ab8500_charger_set_output_curr() - set charger output current
 * @di:		pointer to the ab8500_charger structure
 * @ich_out:	output charger current, in mA
 *
 * Set charger output current.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_output_curr(struct ab8500_charger *di,
	int ich_out)
{
	return ab8500_charger_set_current(di, ich_out,
		AB8500_CH_OPT_CRNTLVL_REG);
}

/**
 * ab8500_charger_led_en() - turn on/off chargign led
 * @di:		pointer to the ab8500_charger structure
 * @on:		flag to turn on/off the chargign led
 *
 * Power ON/OFF charging LED indication
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_led_en(struct ab8500_charger *di, int on)
{
	int ret;

	if (on) {
		/* Power ON charging LED indicator, set LED current to 5mA */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_CTRL,
			(LED_IND_CUR_5MA | LED_INDICATOR_PWM_ENA));
		if (ret) {
			dev_err(di->dev, "Power ON LED failed\n");
			return ret;
		}
		/* LED indicator PWM duty cycle 252/256 */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_DUTY,
			LED_INDICATOR_PWM_DUTY_252_256);
		if (ret) {
			dev_err(di->dev, "Set LED PWM duty cycle failed\n");
			return ret;
		}
	} else {
		/* Power off charging LED indicator */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_CTRL,
			LED_INDICATOR_PWM_DIS);
		if (ret) {
			dev_err(di->dev, "Power-off LED failed\n");
			return ret;
		}
	}

	return ret;
}

/**
 * ab8500_charger_ac_en() - enable or disable ac charging
 * @di:		pointer to the ab8500_charger structure
 * @enable:	enable/disable flag
 * @vset:	charging voltage
 * @iset:	charging current
 *
 * Enable/Disable AC/Mains charging and turns on/off the charging led
 * respectively.
 **/
static int ab8500_charger_ac_en(struct ux500_charger *charger,
	int enable, int vset, int iset)
{
	int ret = 0;
	int volt_index = 0;
	int curr_index = 0;
	int input_index = 0;
	u8 overshoot = 0;

	struct ab8500_charger *di = to_ab8500_charger_ac_device_info(charger);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int charger_status;
	int vbus_status;
	int input_curr_max = 0;
#endif

	if (!di->usb.charger_connected)
		return ret;

	if (enable) {
#if defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_SEC_KYLE) || defined(CONFIG_MACH_SEC_GOLDEN)
		msleep(100);
#endif

		/* Check if AC is connected */
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		charger_status = ab8500_charger_detect_chargers(di);
		vbus_status = ab8500_vbus_is_detected(di);

		if (enable && (charger_status == NO_PW_CONN) && !vbus_status) {
			dev_info(di->dev, "Charging is enabled, but there's no "
				"VBUS and MUIC cannot detect cable. "
				"So charging will be disabled\n");

			ab8500_charger_set_ac_connected(di, false);
			ab8500_power_supply_changed(di, &di->ac_chg.psy);
			return ret;
		}
#else
		if (!di->ac.charger_connected) {
			dev_err(di->dev, "AC charger not connected\n");
			return -ENXIO;
		}
#endif

#if (defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )) && defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		di->usb.charger_suspended = 0;
		del_timer_sync(&di->usb_link_status_timer);
#endif

		/* Enable AC charging */

		/*
		 * Due to a bug in AB8500, BTEMP_HIGH/LOW interrupts
		 * will be triggered everytime we enable the VDD ADC supply.
		 * This will turn off charging for a short while.
		 * It can be avoided by having the supply on when
		 * there is a charger enabled. Normally the VDD ADC supply
		 * is enabled everytime a GPADC conversion is triggered. We will
		 * force it to be enabled from this driver to have
		 * the GPADC module independant of the AB8500 chargers
		 */
		if (!di->vddadc_en_ac) {
			regulator_enable(di->regu);
			di->vddadc_en_ac = true;
		}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		ab8500_charger_set_main_charger_usb_current_limiter(di);
#endif

		/* Check if the requested voltage or current is valid */
		if( di->cable_type == POWER_SUPPLY_TYPE_MAINS ){

			input_curr_max = di->bat->chg_params->ac_curr_max;

			if ( !di->vbus_detect_charging) {
				dev_info(di->dev, "Enable AC: %dmV %dmA, MaxIn %dmA\n",
					 di->bat->ta_chg_voltage,
					 di->bat->ta_chg_current,
					 di->bat->chg_params->ac_curr_max);

				volt_index = ab8500_voltage_to_regval(
					di->bat->ta_chg_voltage);

				curr_index = ab8500_current_to_regval(
					di->bat->ta_chg_current);
			}
			else {
			/*
			 * Charger not recognised by USB Switcher but VBUS
			 * powered. Allow charging to start, set up
			 * as per TA charger, but it maybe necessary to
			 * change.
			 */
				dev_info(di->dev, "Enable MISC: %dmV %dmA, MaxIn %dmA\n",
					 di->bat->ta_chg_voltage,
					 di->bat->ta_chg_current,
					 di->bat->chg_params->ac_curr_max);

				volt_index = ab8500_voltage_to_regval(
					di->bat->ta_chg_voltage);

				curr_index = ab8500_current_to_regval(
					di->bat->ta_chg_current);
			}
		}
#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		else {
			dev_info(di->dev, "Enable USB: %dmV %dmA, MaxIn %dmA\n",
				di->bat->usb_chg_voltage,
				di->bat->usb_chg_current,
				di->max_usb_in_curr);

			volt_index = ab8500_voltage_to_regval(
				di->bat->usb_chg_voltage);

			curr_index = ab8500_current_to_regval(
				di->bat->usb_chg_current);

			/*
			 * This value maybe 0 on first call until USB system
			 * has determined how much current to draw from
			 * USB type charger.
			 */
			input_curr_max = di->max_usb_in_curr;
		}
#endif

		input_index = ab8500_current_to_regval(input_curr_max);

		dev_info(di->dev, "vset %dmV, iset %dmA\n", vset, iset);
#else /* ! CONFIG_USB_SWITCHER */
		dev_info(di->dev, "Enable AC: %dmV %dmA\n", vset, iset);

		volt_index = ab8500_voltage_to_regval(vset);
		curr_index = ab8500_current_to_regval(iset);

		input_index = ab8500_current_to_regval(
			di->bat->chg_params->ac_curr_max);
#endif

		if (volt_index < 0 || curr_index < 0 || input_index < 0) {
			dev_err(di->dev,
				"Charger voltage or current too high, "
				"charging not started\n");
			return -ENXIO;
		}

		/* ChVoltLevel: maximum battery charging voltage */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_REG, (u8) volt_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_MCH_IPT_CURLVL_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_OPT_CRNTLVL_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* Check if VBAT overshoot control should be enabled */
		if (!di->bat->enable_overshoot)
			overshoot = MAIN_CH_NO_OVERSHOOT_ENA_N;

		/* Enable Main Charger */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_MCH_CTRL1, MAIN_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* MainChInputCurr: current that can be drawn from the charger*/
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		ret = ab8500_charger_set_main_in_curr(di, input_curr_max);

#else
		ret = ab8500_charger_set_main_in_curr(di,
			di->bat->chg_params->ac_curr_max);
#endif
		if (ret) {
			dev_err(di->dev, "%s Failed to set MainChInputCurr\n",
				__func__);
			return ret;
		}

		/* ChOutputCurentLevel: protected output current */
		ret = ab8500_charger_set_output_curr(di, iset);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to set ChOutputCurentLevel\n",
				__func__);
			return ret;
		}

		/* Power on charging LED indication */
		ret = ab8500_charger_led_en(di, true);
		if (ret < 0)
			dev_err(di->dev, "failed to enable LED\n");

		di->ac.charger_online = 1;
	} else {
		/* Disable AC charging */
		if (is_ab8500_1p1_or_earlier(di->parent)) {
			/*
			 * For ABB revision 1.0 and 1.1 there is a bug in the
			 * watchdog logic. That means we have to continously
			 * kick the charger watchdog even when no charger is
			 * connected. This is only valid once the AC charger
			 * has been enabled. This is a bug that is not handled
			 * by the algorithm and the watchdog have to be kicked
			 * by the charger driver when the AC charger
			 * is disabled
			 */
			if (di->ac_conn) {
				queue_delayed_work(di->charger_wq,
					&di->kick_wd_work,
					round_jiffies(WD_KICK_INTERVAL));
			}

			/*
			 * We can't turn off charging completely
			 * due to a bug in AB8500 cut1.
			 * If we do, charging will not start again.
			 * That is why we set the lowest voltage
			 * and current possible
			 */
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_CH_VOLT_LVL_REG, CH_VOL_LVL_3P5);
			if (ret) {
				dev_err(di->dev,
					"%s write failed\n", __func__);
				return ret;
			}

			ret = ab8500_charger_set_output_curr(di, 0);
			if (ret) {
				dev_err(di->dev, "%s "
					"Failed to set ChOutputCurentLevel\n",
					__func__);
				return ret;
			}
		} else {
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_MCH_CTRL1, 0);
			if (ret) {
				dev_err(di->dev,
					"%s write failed\n", __func__);
				return ret;
			}
		}

		ret = ab8500_charger_led_en(di, false);
		if (ret < 0)
			dev_err(di->dev, "failed to disable LED\n");

		di->ac.charger_online = 0;
		di->ac.wd_expired = false;

		/* Disable regulator if enabled */
		if (di->vddadc_en_ac) {
			regulator_disable(di->regu);
			di->vddadc_en_ac = false;
		}

		dev_dbg(di->dev, "%s Disabled AC charging\n", __func__);
	}

	ab8500_power_supply_changed(di, &di->ac_chg.psy);

	return ret;
}

/**
 * ab8500_charger_usb_en() - enable usb charging
 * @di:		pointer to the ab8500_charger structure
 * @enable:	enable/disable flag
 * @vset:	charging voltage
 * @ich_out:	charger output current
 *
 * Enable/Disable USB charging and turns on/off the charging led respectively.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_usb_en(struct ux500_charger *charger,
	int enable, int vset, int ich_out)
{
	int ret = 0;
	int volt_index = 0;
	int curr_index = 0;
	int input_index = 0;
	u8 overshoot = 0;

	struct ab8500_charger *di = to_ab8500_charger_usb_device_info(charger);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int charger_status;
	int vbus_status;
#endif

	if (enable) {
		/* Check if USB is connected */
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		charger_status = ab8500_charger_detect_chargers(di);

		if (enable && (charger_status == NO_PW_CONN) ) {
			dev_info(di->dev, "Charging is enabled, but there's no "
				"VBUS and MUIC cannot detect cable. "
				"So charging will be disabled\n");

			ab8500_charger_set_usb_connected(di, false);
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
			return -ENXIO;
		}
#else
		if (!di->usb.charger_connected) {
			dev_err(di->dev, "USB charger not connected\n");
			return -ENXIO;
		}
#endif

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		di->usb.charger_suspended = 0;
		del_timer_sync(&di->usb_link_status_timer);
#endif

		/*
		 * Due to a bug in AB8500, BTEMP_HIGH/LOW interrupts
		 * will be triggered everytime we enable the VDD ADC supply.
		 * This will turn off charging for a short while.
		 * It can be avoided by having the supply on when
		 * there is a charger enabled. Normally the VDD ADC supply
		 * is enabled everytime a GPADC conversion is triggered. We will
		 * force it to be enabled from this driver to have
		 * the GPADC module independant of the AB8500 chargers
		 */
		if (!di->vddadc_en_usb) {
			regulator_enable(di->regu);
			di->vddadc_en_usb = true;
		}

		/* Enable USB charging */
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		/* Check if the requested voltage or current is valid */
		if( di->cable_type == POWER_SUPPLY_TYPE_MAINS ){
			if ( !di->vbus_detect_charging) {
				dev_info(di->dev, "Enable AC: %dmV %dmA, MaxIn %dmA\n",
					 di->bat->ta_chg_voltage,
					 di->bat->ta_chg_current,
					 di->bat->chg_params->ac_curr_max);

				volt_index = ab8500_voltage_to_regval(
					di->bat->ta_chg_voltage);

				curr_index = ab8500_current_to_regval(
					di->bat->ta_chg_current);

				di->max_usb_in_curr =
					di->bat->chg_params->ac_curr_max;
			}
			else {
			/*
			 * Charger not recognised by USB Switcher but VBUS
			 * powered. Allow charging to start, set up
			 * as per TA charger, but it maybe necessary to
			 * change.
			 */
				dev_info(di->dev, "Enable MISC: %dmV %dmA, MaxIn %dmA\n",
					di->bat->ta_chg_voltage,
					di->bat->ta_chg_current,
					di->bat->chg_params->ac_curr_max);

				volt_index = ab8500_voltage_to_regval(
					di->bat->ta_chg_voltage);

				curr_index = ab8500_current_to_regval(
					di->bat->ta_chg_current);

				di->max_usb_in_curr =
					di->bat->chg_params->ac_curr_max;
			}
		}else if( di->cable_type == POWER_SUPPLY_TYPE_USB ){
			dev_info(di->dev, "Enable USB: %dmV %dmA, MaxIn %dmA\n",
				di->bat->usb_chg_voltage,
				di->bat->usb_chg_current,
				di->max_usb_in_curr);

			volt_index = ab8500_voltage_to_regval(
				di->bat->usb_chg_voltage);

			curr_index = ab8500_current_to_regval(
				di->bat->usb_chg_current);
		}else{
			dev_info(di->dev, "UNKNOWN!");
			return -ENXIO;
		}

		input_index = ab8500_current_to_regval(di->max_usb_in_curr);

		dev_info(di->dev, "vset %dmV, iset %dmA\n", vset, ich_out);

#else /* ! CONFIG_USB_SWITCHER */
		dev_dbg(di->dev, "Enable USB: %dmV %dmA\n", vset, ich_out);

		/* Check if the requested voltage or current is valid */
		volt_index = ab8500_voltage_to_regval(vset);
		curr_index = ab8500_current_to_regval(ich_out);
		input_index = ab8500_current_to_regval(di->max_usb_in_curr);
#endif

		if (volt_index < 0 || curr_index < 0 || input_index < 0) {
			dev_err(di->dev,
				"Charger voltage or current too high, "
				"charging not started\n");
			return -ENXIO;
		}

		/* ChVoltLevel: max voltage upto which battery can be charged */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_REG, (u8) volt_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_USBCH_IPT_CRNTLVL_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_OPT_CRNTLVL_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* Check if VBAT overshoot control should be enabled */
		if (!di->bat->enable_overshoot)
			overshoot = USB_CHG_NO_OVERSHOOT_ENA_N;

		/* Enable USB Charger */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, USB_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* USBChInputCurr: current that can be drawn from the connected charger cable */
		ret = ab8500_charger_set_vbus_in_curr(di, di->max_usb_in_curr);

		if (ret) {
			dev_err(di->dev, "setting USBChInputCurr failed\n");
			return ret;
		}

		/* ChOutputCurrentLevel: protected output current */
		ret = ab8500_charger_set_output_curr(di, ich_out);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to set ChOutputCurentLevel\n",
				__func__);
			return ret;
		}

		/* If success power on charging LED indication */
		ret = ab8500_charger_led_en(di, true);
		if (ret < 0)
			dev_err(di->dev, "failed to enable LED\n");

		queue_delayed_work(di->charger_wq, &di->check_vbat_work, HZ);

		di->usb.charger_online = 1;
	} else {
		/* Disable USB charging */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, 0);
		if (ret) {
			dev_err(di->dev,
				"%s write failed\n", __func__);
			return ret;
		}

		ret = ab8500_charger_led_en(di, false);
		if (ret < 0)
			dev_err(di->dev, "failed to disable LED\n");

		di->usb.charger_online = 0;
		di->usb.wd_expired = false;

		/* Disable regulator if enabled */
		if (di->vddadc_en_usb) {
			regulator_disable(di->regu);
			di->vddadc_en_usb = false;
		}

		dev_info(di->dev, "%s Disabled USB charging\n", __func__);

		/* Cancel any pending Vbat check work */
		if (delayed_work_pending(&di->check_vbat_work))
			cancel_delayed_work(&di->check_vbat_work);

	}
	ab8500_power_supply_changed(di, &di->usb_chg.psy);

	return ret;
}

static int ab8500_charger_check_enable(struct ux500_charger *charger,
	int vset, int iset)
{
	u8 mainch_ctrl1=0;
	u8 ch_ctrl=0;
	int ret = 0;

	struct ab8500_charger *di = to_ab8500_charger_usb_device_info(charger);


	if (!di->usb.charger_connected)
		return ret;

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_MCH_CTRL1, &mainch_ctrl1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	if (!(mainch_ctrl1 & MAIN_CH_ENA)) {
		dev_info(di->dev, "%s Charging has been disabled abonormally "
			"and  will be enabled\n", __func__);

		ch_ctrl = 0x01;
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
					AB8500_CHARGER_CTRL, ch_ctrl);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}

		ret = ab8500_charger_ac_en(&di->usb_chg, true, vset, iset);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}
	}

	return ret;
}

/**
 * ab8500_charger_watchdog_kick() - kick charger watchdog
 * @di:		pointer to the ab8500_charger structure
 *
 * Kick charger watchdog
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_watchdog_kick(struct ux500_charger *charger)
{
	int ret;
	struct ab8500_charger *di;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS)
		di = to_ab8500_charger_ac_device_info(charger);
	else if (charger->psy.type == POWER_SUPPLY_TYPE_USB)
		di = to_ab8500_charger_usb_device_info(charger);
	else
		return -ENXIO;

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
	if (ret)
		dev_err(di->dev, "Failed to kick WD!\n");

	return ret;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/**
 * ab8500_charger_siop_activation()
 *	- Adjust input current limit
 *	for Samsung Intelligent Overheat Protection
 * @di:	   pointer to the ab8500_charger structure
 *
 * Update the charger input current limit
 * Returns error code in case of failure else 0(on success)
 */
static void ab8500_charger_siop_activation(
				struct ux500_charger *charger,
				bool enable)
{
	struct ab8500_charger *di;
	int ich_in;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS) {
		di = to_ab8500_charger_ac_device_info(charger);

		if (enable) {
			/* USB input current limit */
			ich_in = di->bat->chg_params->usb_curr_max;
		} else {
			/* AC input current limit */
			ich_in = di->bat->chg_params->ac_curr_max ;
		}

		dev_dbg(di->dev, "adjust input current to %dmA\n", ich_in);
		ab8500_charger_set_main_in_curr(di, ich_in);
	}else if (charger->psy.type == POWER_SUPPLY_TYPE_USB) {
		di = to_ab8500_charger_usb_device_info(charger);

		if (enable) {
			/* USB input current limit */
			ich_in = di->bat->chg_params->usb_curr_max;
		} else {
			/* AC input current limit */
			ich_in = di->bat->chg_params->ac_curr_max ;
		}

		dev_dbg(di->dev, "adjust input current to %dmA\n", ich_in);
		ab8500_charger_set_vbus_in_curr(di, ich_in);
	}
}

/**
 * ab8500_charger_update_charger_input_current()
 *		 - update charger input current
 * @di:		pointer to the ab8500_charger structure
 *
 * Update the charger input current
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_update_charger_input_current(
				struct ux500_charger *charger,
				int ich_in)
{
	int ret = 0;
	struct ab8500_charger *di;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS) {
		di = to_ab8500_charger_ac_device_info(charger);
		dev_dbg(di->dev, "adjust input current to %dmA\n", ich_in);
		ab8500_charger_set_main_in_curr(di, ich_in);

	} else if (charger->psy.type == POWER_SUPPLY_TYPE_USB) {
		di = to_ab8500_charger_usb_device_info(charger);
		dev_dbg(di->dev, "adjust input current to %dmA\n", ich_in);
		ab8500_charger_set_vbus_in_curr(di, ich_in);
	} else
		return -ENXIO;

	return ret;
}
#endif

/**
 * ab8500_charger_update_charger_current() - update charger current
 * @di:		pointer to the ab8500_charger structure
 *
 * Update the charger output current for the specified charger
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_update_charger_current(struct ux500_charger *charger,
		int ich_out)
{
	int ret;
	struct ab8500_charger *di;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS)
		di = to_ab8500_charger_ac_device_info(charger);
	else if (charger->psy.type == POWER_SUPPLY_TYPE_USB)
		di = to_ab8500_charger_usb_device_info(charger);
	else
		return -ENXIO;
	dev_dbg(di->dev, "Setting current to %dmA\n",  ich_out);

	ret = ab8500_charger_set_output_curr(di, ich_out);
	if (ret) {
		dev_err(di->dev, "%s "
			"Failed to set ChOutputCurentLevel\n",
			__func__);
		return ret;
	}

	/* Reset the main and usb drop input current measurement counter */
	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_CHARGER_CTRL,
				0x1);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		return ret;
	}

	return ret;
}

static int ab8500_charger_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab8500_charger *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;
	struct ux500_charger *usb_chg;

	usb_chg = (struct ux500_charger *)data;
	psy = &usb_chg->psy;

	di = to_ab8500_charger_usb_device_info(usb_chg);

	ext = dev_get_drvdata(dev);

	/* For all psy where the driver name appears in any supplied_to */
	for (i = 0; i < ext->num_supplicants; i++) {
		if (!strcmp(ext->supplied_to[i], psy->name))
			psy_found = true;
	}

	if (!psy_found)
		return 0;

	/* Go through all properties for the psy */
	for (j = 0; j < ext->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->properties[j];

		if (ext->get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->vbat = ret.intval / 1000;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * ab8500_charger_check_vbat_work() - keep vbus current within spec
 * @work	pointer to the work_struct structure
 *
 * Due to a asic bug it is necessary to lower the input current to the vbus
 * charger when charging with at some specific levels. This issue is only valid
 * for below a certain battery voltage. This function makes sure that the
 * the allowed current limit isn't exceeded.
 */
static void ab8500_charger_check_vbat_work(struct work_struct *work)
{
	int t = 10;
	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_vbat_work.work);

	class_for_each_device(power_supply_class, NULL,
		&di->usb_chg.psy, ab8500_charger_get_ext_psy_data);

	/* First run old_vbat is 0. */
	if (di->old_vbat == 0)
		di->old_vbat = di->vbat;

	if (!((di->old_vbat <= VBAT_TRESH_IP_CUR_RED &&
		di->vbat <= VBAT_TRESH_IP_CUR_RED) ||
		(di->old_vbat > VBAT_TRESH_IP_CUR_RED &&
		di->vbat > VBAT_TRESH_IP_CUR_RED))) {

		dev_dbg(di->dev, "Vbat did cross threshold, curr: %d, new: %d,"
			" old: %d\n", di->max_usb_in_curr, di->vbat,
			di->old_vbat);
		ab8500_charger_set_vbus_in_curr(di, di->max_usb_in_curr);
		power_supply_changed(&di->usb_chg.psy);
	}

	di->old_vbat = di->vbat;

	/*
	 * No need to check the battery voltage every second when not close to
	 * the threshold.
	 */
	if (di->vbat < (VBAT_TRESH_IP_CUR_RED + VBAT_TRESH_BL1) &&
		(di->vbat > (VBAT_TRESH_IP_CUR_RED - VBAT_TRESH_BL1)))
			t = 1;

	queue_delayed_work(di->charger_wq, &di->check_vbat_work, t * HZ);
}

/**
 * ab8500_charger_check_hw_failure_work() - check main charger failure
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the main charger status
 */
static void ab8500_charger_check_hw_failure_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_hw_failure_work.work);

	/* Check if the status bits for HW failure is still active */
	if (di->flags.mainextchnotok) {
		dev_err(di->dev, "mainextchnotok seen\n");

		ret = abx500_get_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_STATUS2_REG, &reg_value);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return;
		}
		if (!(reg_value & MAIN_CH_NOK)) {
			di->flags.mainextchnotok = false;
			ab8500_power_supply_changed(di, &di->ac_chg.psy);
		}
	}
	if (di->flags.vbus_ovv) {
		dev_err(di->dev, "vbus_ovv seen\n");
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG,
			&reg_value);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return;
		}
		if (!(reg_value & VBUS_OVV_TH)) {
			di->flags.vbus_ovv = false;
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
		}
	}
	/* If we still have a failure, schedule a new check */
	if (di->flags.mainextchnotok || di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, round_jiffies(HZ));
	}
}

/**
 * ab8500_charger_kick_watchdog_work() - kick the watchdog
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for kicking the charger watchdog.
 *
 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
 * logic. That means we have to continously kick the charger
 * watchdog even when no charger is connected. This is only
 * valid once the AC charger has been enabled. This is
 * a bug that is not handled by the algorithm and the
 * watchdog have to be kicked by the charger driver
 * when the AC charger is disabled
 */
static void ab8500_charger_kick_watchdog_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, kick_wd_work.work);

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
	if (ret)
		dev_err(di->dev, "Failed to kick WD!\n");

	/* Schedule a new watchdog kick */
	queue_delayed_work(di->charger_wq,
		&di->kick_wd_work, round_jiffies(WD_KICK_INTERVAL));
}

/**
 * ab8500_charger_ac_work() - work to get and set main charger status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the main charger status
 */
static void ab8500_charger_ac_work(struct work_struct *work)
{

	int ret;
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	int connected;
	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, ac_work.work);
#else
	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, ac_work);
#endif

	dev_info(di->dev, "%s\n", __func__);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if the main charger is
	 * connected by reading the status register
	 */
	ret = ab8500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (ret & AC_PW_CONN) {
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		connected = 1;
#else
		di->ac.charger_connected = 1;
#endif
		/*
		 * di->ac_conn. Watchdog bug workaround for AB8500
		 * 1v0/1v1, do not clear once set.
		 */
		di->ac_conn = true;
	} else {
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		connected = 0;
#else
		di->ac.charger_connected = 0;
#endif
	}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	if ( is_ab8500(di->parent) ) {
		ab8500_charger_set_ac_connected(di, connected);
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
	}
	else{
		/* AB8505 and AB9540 only have USB charger input */
		ab8500_charger_set_usb_connected(di, connected);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
	}
#else
	ab8500_power_supply_changed(di, &di->ac_chg.psy);
	sysfs_notify(&di->ac_chg.psy.dev->kobj, NULL, "present");
#endif
}

#if !defined( CONFIG_USB_SWITCHER ) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
static void ab8500_charger_usb_attached_work(struct work_struct *work)
{
	int i;
	int ret;
	u8 statval;
	struct ab8500_charger *di = container_of(work,
					 struct ab8500_charger,
					 usb_charger_attached_work.work);

	for (i = 0 ; i < 10; i++) {
		ret = abx500_get_register_interruptible(di->dev,
							AB8500_CHARGER,
							AB8500_CH_USBCH_STAT1_REG,
							&statval);
		if (ret < 0) {
			dev_err(di->dev, "ab8500 read failed %d\n",
				__LINE__);
			goto reschedule;
		}
		if ((statval & (USB_CH_VBUSDROP |
				USB_CH_VBUSDETDBNC)) !=
		    (USB_CH_VBUSDROP | USB_CH_VBUSDETDBNC))
			goto reschedule;

		msleep(CHARGER_STATUS_POLL);
	}

	(void) ab8500_charger_usb_en(&di->usb_chg, 0, 0, 0);

	mutex_lock(&di->charger_attached_mutex);
	if (wake_lock_active(&di->usb_charger_attached_wake_lock))
		wake_unlock(&di->usb_charger_attached_wake_lock);
	mutex_unlock(&di->charger_attached_mutex);

	return;
reschedule:
	queue_delayed_work(di->charger_wq,
			   &di->usb_charger_attached_work,
			   HZ);
}

static void ab8500_charger_ac_attached_work(struct work_struct *work)
{

	int i;
	int ret;
	u8 statval;
	struct ab8500_charger *di = container_of(work,
					 struct ab8500_charger,
					 ac_charger_attached_work.work);

	for (i = 0 ; i < 10; i++) {
		ret = abx500_get_register_interruptible(di->dev,
							AB8500_CHARGER,
							AB8500_CH_STATUS2_REG,
							&statval);
		if (ret < 0) {
			dev_err(di->dev, "ab8500 read failed %d\n",
				__LINE__);
			goto reschedule;
		}
		if ((statval & (MAIN_CH_STATUS2_MAINCHGDROP |
				MAIN_CH_STATUS2_MAINCHARGERDETDBNC)) !=
		     (MAIN_CH_STATUS2_MAINCHGDROP |
		      MAIN_CH_STATUS2_MAINCHARGERDETDBNC))
			goto reschedule;

		msleep(CHARGER_STATUS_POLL);
	}

	(void) ab8500_charger_ac_en(&di->ac_chg, 0, 0, 0);
	queue_work(di->charger_wq, &di->ac_work);

	mutex_lock(&di->charger_attached_mutex);
	if (wake_lock_active(&di->ac_charger_attached_wake_lock))
		wake_unlock(&di->ac_charger_attached_wake_lock);
	mutex_unlock(&di->charger_attached_mutex);

	return;
reschedule:
	queue_delayed_work(di->charger_wq,
			   &di->ac_charger_attached_work,
			   HZ);
}
#endif

/**
 * ab8500_charger_detect_usb_type_work() - work to detect USB type
 * @work:	Pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_detect_usb_type_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, detect_usb_type_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if is
	 * connected by reading the status register
	 */
	ret = ab8500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (!(ret & USB_PW_CONN)) {
		di->vbus_detected = 0;

#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		ab8500_charger_set_ac_connected(di, false);
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
#else
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
	} else {
		di->vbus_detected = 1;

		if (is_ab8500_1p1_or_earlier(di->parent)) {
			ret = ab8500_charger_detect_usb_type(di);
			if (!ret) {
#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
				ab8500_charger_set_ac_connected(di, true);
				ab8500_power_supply_changed(di,
							&di->ac_chg.psy);
#else
				ab8500_charger_set_usb_connected(di, true);
				ab8500_power_supply_changed(di,
							&di->usb_chg.psy);
#endif
			}
		} else {
			/* For ABB cut2.0 and onwards we have an IRQ,
			 * USB_LINK_STATUS that will be triggered when the USB
			 * link status changes. The exception is USB connected
			 * during startup. Then we don't get a
			 * USB_LINK_STATUS IRQ
			 */
			if (di->vbus_detected_start) {
				di->vbus_detected_start = false;
				ret = ab8500_charger_detect_usb_type(di);
				if (!ret) {
#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
					ab8500_charger_set_ac_connected(di,
						true);
					ab8500_power_supply_changed(di,
						&di->ac_chg.psy);
#else
					ab8500_charger_set_usb_connected(di,
						true);
					ab8500_power_supply_changed(di,
						&di->usb_chg.psy);
#endif
				}
			}
		}
	}
}

/**
 * ab8500_charger_usb_link_attach_work() - delayd work to detect USB type
 * @work:	pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_usb_link_attach_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, attach_work.work);

	/* Update maximum input current if USB enumeration is not detected */
	if (!di->usb.charger_online) {
		ret = ab8500_charger_set_vbus_in_curr(di, di->max_usb_in_curr);
		if (ret)
			return;
	}

	ab8500_charger_set_usb_connected(di, true);
	ab8500_power_supply_changed(di, &di->usb_chg.psy);
}

/**
 * ab8500_charger_usb_link_status_work() - work to detect USB type
 * @work:	pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_usb_link_status_work(struct work_struct *work)
{
	int detected_chargers;
	int ret;
	u8 val;
	u8 link_status;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, usb_link_status_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if  is
	 * connected by reading the status register
	 */
	detected_chargers = ab8500_charger_detect_chargers(di);
	if (detected_chargers < 0)
		return;

	/*
	 * Some chargers that breaks the USB spec is
	 * identified as invalid by AB8500 and it refuse
	 * to start the charging process. but by jumping
	 * thru a few hoops it can be forced to start.
	 */
	if (is_ab8500(di->parent))
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
					AB8500_USB_LINE_STAT_REG, &val);
	else
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
					AB8500_USB_LINK1_STAT_REG, &val);

	if (ret >= 0)
		dev_dbg(di->dev, "UsbLineStatus register = 0x%02x\n", val);
	else
		dev_dbg(di->dev, "Error reading USB link status\n");

	if (is_ab9540(di->parent) || is_ab8505(di->parent))
		link_status = AB8505_USB_LINK_STATUS;
	else
		link_status = AB8500_USB_LINK_STATUS;

	if (detected_chargers & USB_PW_CONN) {
		if (((val & link_status) >> 3) == USB_STAT_NOT_VALID_LINK &&
				di->invalid_charger_detect_state == 0) {
			dev_dbg(di->dev, "Invalid charger detected, state= 0\n");
			/*Enable charger*/
			abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_CHARGER, AB8500_USBCH_CTRL1_REG, 0x01, 0x01);
			/*Enable charger detection*/
			abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINE_CTRL2_REG,
					USB_CH_DET, USB_CH_DET);
			di->invalid_charger_detect_state = 1;
			/*exit and wait for new link status interrupt.*/
			return;

		}
		if (di->invalid_charger_detect_state == 1) {
			dev_dbg(di->dev, "Invalid charger detected, state= 1\n");
			/*Stop charger detection*/
			abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINE_CTRL2_REG,
					USB_CH_DET, 0x00);
			/*Check link status*/
			if (is_ab8500(di->parent))
				ret = abx500_get_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINE_STAT_REG,
					&val);
			else
				ret = abx500_get_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINK1_STAT_REG,
					&val);

			dev_dbg(di->dev, "USB link status= 0x%02x\n",
					(val & link_status) >> 3);
			di->invalid_charger_detect_state = 2;
		}
	} else {
		di->invalid_charger_detect_state = 0;
	}

	if (!(detected_chargers & USB_PW_CONN)) {
		di->vbus_detected = 0;

#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		ab8500_charger_set_ac_connected(di, false);
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
#else
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
	} else {
		int ret;
		di->vbus_detected = 1;
		ret = ab8500_charger_max_usb_curr(di,
			(enum ab8500_charger_link_status) di->link_status);
		if (!ret) {
			/* Update maximum input current */
#if defined(CONFIG_AB8XX0_USBCHG_ON_MAIN)
			ret = ab8500_charger_set_main_in_curr(di,
					di->max_usb_in_curr);
			if (ret)
				return;

			ab8500_charger_set_ac_connected(di, true);
			ab8500_power_supply_changed(di, &di->ac_chg.psy);
#else
			if (di->usb_device_is_unrecognised) {
				dev_dbg(di->dev,
					"Potential Legacy Charger device. "
					"Delay work for %d msec for USB enum "
					"to finish",
					WAIT_FOR_USB_ENUMERATION);
				queue_delayed_work(di->charger_wq,
					&di->attach_work,
					msecs_to_jiffies(WAIT_FOR_USB_ENUMERATION));
			} else {
				queue_delayed_work(di->charger_wq,
							&di->attach_work, 0);
			}
#endif
		}
#if !defined(CONFIG_USB_SWITCHER) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		/*
		* if Usb performs a software configuration change then the
		* USB charger properties may be unknown but its still
		* connected
		*/
		else if (ret == -ENXIO) {
			/* No valid charger type detected */
			ab8500_charger_set_usb_connected(di, false);
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
		}
#endif
	}
}

#ifdef CONFIG_MACH_JANICE
/*TODO: Add some header*/
static void ab8500_charger_tsp_vbus_notify_work(struct work_struct *work)
{
	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, tsp_vbus_notify_work);

	cypress_touchkey_ta_status = cypress_touchkey_change_thd;
	mxt224e_ts_vbus_state = mxt224e_ts_change_vbus_state;

	vbus_state = (bool)ab8500_vbus_is_detected(di);
	printk("%s, VBUS : %d\n", __func__, vbus_state);

	if (cypress_touchkey_ta_status)
		cypress_touchkey_ta_status(vbus_state);

	if (mxt224e_ts_vbus_state)
		mxt224e_ts_vbus_state(vbus_state);

}
#endif

static void ab8500_charger_usb_state_changed_work(struct work_struct *work)
{
	int ret;
	unsigned long flags;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, usb_state_changed_work);

	if (!di->vbus_detected)
		return;

	spin_lock_irqsave(&di->usb_state.usb_lock, flags);
	di->usb_state.usb_changed = false;
	spin_unlock_irqrestore(&di->usb_state.usb_lock, flags);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	del_timer_sync(&di->usb_link_status_timer);

	if (ab8500_charger_detect_chargers(di) != USB_PW_CONN) {
		dev_info(di->dev, "%s: From USBSW, Not standard host, exit\n",
			__func__);
		return;
	}
#endif
	/*
	 * wait for some time until you get updates from the usb stack
	 * and negotiations are completed
	 */
	msleep(250);

	if (di->usb_state.usb_changed)
		return;

	pr_info("%s: USB state=0x%02x, mA=%d\n",
		__func__, di->usb_state.state, di->usb_state.usb_current);

	switch (di->usb_state.state) {
	case AB8500_BM_USB_STATE_RESET_HS:
	case AB8500_BM_USB_STATE_RESET_FS:
	case AB8500_BM_USB_STATE_SUSPEND:
	case AB8500_BM_USB_STATE_MAX:

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		di->usb.charger_suspended = 1;
		if (di->registered_with_power){
#ifdef CONFIG_AB8XX0_USBCHRG_ON_MAIN
			ab8500_power_supply_changed(di, &di->ac_chg.psy);
#else
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
		}
#else
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
		break;

	case AB8500_BM_USB_STATE_RESUME:
		/*
		 * when suspend->resume there should be delay
		 * of 1sec for enabling charging
		 */
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		di->usb.charger_suspended = 0;
#endif
		msleep(1000);
		/* Intentional fall through */
	case AB8500_BM_USB_STATE_CONFIGURED:
		/*
		 * USB is configured, enable charging with the charging
		 * input current obtained from USB driver
		 */
#ifdef CONFIG_AB8XX0_USBCHRG_ON_MAIN
		ret = ab8500_charger_get_usbtoac_cur(di);
#else
		ret = ab8500_charger_get_usb_cur(di);
#endif
		if (!ret) {
			/* Update maximum input current */
#ifdef CONFIG_AB8XX0_USBCHRG_ON_MAIN
			ret = ab8500_charger_set_main_in_curr(di,
					di->max_usb_in_curr);
#else
			ret = ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr);
#endif
			if (ret)
				return;

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
			di->usb.charger_suspended = 0;

#ifdef CONFIG_AB8XX0_USBCHRG_ON_MAIN
			ab8500_charger_set_ac_connected(di, true);
#else
			ab8500_charger_set_usb_connected(di, true);
#endif

			if (di->registered_with_power){
#ifdef CONFIG_AB8XX0_USBCHRG_ON_MAIN
				ab8500_power_supply_changed(di, &di->ac_chg.psy);
#else
				ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
			}
#else
			ab8500_charger_set_usb_connected(di, true);
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
		}
		break;

	default:
		break;
	};
}

/**
 * ab8500_charger_check_usbchargernotok_work() - check USB chg not ok status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the USB charger Not OK status
 */
static void ab8500_charger_check_usbchargernotok_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;
	bool prev_status;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_usbchgnotok_work.work);

	/* Check if the status bit for usbchargernotok is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
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
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
static void ab8500_handle_main_voltage_drop_work(struct work_struct *work)
{
/*
	struct ab8500_charger *di = container_of(work,
					struct ab8500_charger,
					handle_main_voltage_drop_work);
*/

}





static void ab8500_handle_vbus_voltage_drop_work(struct work_struct *work)
{
	u8 reg_value;
	int ret;
	int auto_ich_in;

	struct ab8500_charger *di = container_of(work,
					struct ab8500_charger,
					handle_vbus_voltage_drop_work);

	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG, &reg_value);
	reg_value = (reg_value & AUTO_VBUS_CHG_INP_CURR_MASK) >> AUTO_VBUS_CHG_INP_CURR_SHIFT;
	auto_ich_in = ab8500_charger_vbus_in_curr_map[reg_value];
	dev_dbg(di->dev, "Measured Auto input current %dmA\n", auto_ich_in);

	ret = ab8500_charger_set_vbus_in_curr(di, auto_ich_in);
	if (ret) {
		dev_err(di->dev, "%s(), setting USBChInputCurr failed\n", __func__);
		return ret;
	}

}
#endif

/**
 * ab8500_charger_check_main_thermal_prot_work() - check main thermal status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the Main thermal prot status
 */
static void ab8500_charger_check_main_thermal_prot_work(
	struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_main_thermal_prot_work);

	/* Check if the status bit for main_thermal_prot is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_STATUS2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return;
	}
	if (reg_value & MAIN_CH_TH_PROT){
		di->flags.main_thermal_prot = true;
		dev_err(di->dev, "Main charger Overheat\n");
	}
	else
		di->flags.main_thermal_prot = false;

	ab8500_power_supply_changed(di, &di->ac_chg.psy);
}

/**
 * ab8500_charger_check_usb_thermal_prot_work() - check usb thermal status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the USB thermal prot status
 */
static void ab8500_charger_check_usb_thermal_prot_work(
	struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_usb_thermal_prot_work);

	/* Check if the status bit for usb_thermal_prot is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return;
	}
	if (reg_value & USB_CH_TH_PROT){
		di->flags.usb_thermal_prot = true;
		dev_err(di->dev, "USB charger Overheat\n");
	}
	else
		di->flags.usb_thermal_prot = false;

	ab8500_power_supply_changed(di, &di->usb_chg.psy);
}

/**
 * ab8500_charger_mainchunplugdet_handler() - main charger unplugged
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchunplugdet_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "%s Main charger unplugged\n", __func__);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/* Handled in the usb_switcher_notify */
#else
	queue_work(di->charger_wq, &di->ac_work);

	cancel_delayed_work_sync(&di->ac_charger_attached_work);
	mutex_lock(&di->charger_attached_mutex);
	if (wake_lock_active(&di->ac_charger_attached_wake_lock))
		wake_unlock(&di->ac_charger_attached_wake_lock);
	mutex_unlock(&di->charger_attached_mutex);

#endif

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainchplugdet_handler() - main charger plugged
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchplugdet_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "%s Main charger plugged\n", __func__);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/* Handled in the usb_switcher_notify */
#else
	queue_work(di->charger_wq, &di->ac_work);

	mutex_lock(&di->charger_attached_mutex);
	if (!wake_lock_active(&di->ac_charger_attached_wake_lock))
		wake_lock(&di->ac_charger_attached_wake_lock);
	mutex_unlock(&di->charger_attached_mutex);
	queue_delayed_work(di->charger_wq,
			   &di->ac_charger_attached_work,
			   HZ);
#endif
	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainextchnotok_handler() - main charger not ok
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainextchnotok_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "Main charger not ok\n");
	di->flags.mainextchnotok = true;
	ab8500_power_supply_changed(di, &di->ac_chg.psy);

	/* Schedule a new HW failure check */
	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainchthprotr_handler() - Die temp is above main charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchthprotr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev,
		"Die temp above Main charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_main_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainchthprotf_handler() - Die temp is below main charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchthprotf_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev,
		"Die temp ok for Main charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_main_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_vbusdetf_handler() - VBUS falling detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbusdetf_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "VBUS falling detected\n");

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_vbusdetr_handler() - VBUS rising detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbusdetr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "VBUS rising detected\n");

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usblinkstatus_handler() - USB link status has changed
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usblinkstatus_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;
	int ret;
	u8 val;

	dev_dbg(di->dev, "USB link status changed\n");
	ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
				AB8500_USB_LINE_STAT_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return IRQ_HANDLED;
	}
	di->link_status = (val & AB8500_USB_LINK_STATUS) >> 3;
	queue_work(di->charger_wq, &di->usb_link_status_work);

	return IRQ_HANDLED;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/* TODO: Add header*/
static void usb_link_status_timer_function(unsigned long v)
{
	struct ab8500_charger *di = (struct ab8500_charger *) v;

	dev_err(di->dev, "USB link status timed out\n");
	di->usb_connection_timeout = 1 ;
	queue_work(di->charger_wq, &di->usb_link_status_work);
}
#endif

/**
 * ab8500_charger_usbchthprotr_handler() - Die temp is above usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usbchthprotr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev,
		"Die temp above USB charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_usb_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usbchthprotf_handler() - Die temp is below usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usbchthprotf_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev,
		"Die temp ok for USB charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_usb_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usbchargernotokr_handler() - USB charger not ok detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usbchargernotokr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "Not allowed USB charger detected\n");
	queue_delayed_work(di->charger_wq, &di->check_usbchgnotok_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_chwdexp_handler() - Charger watchdog expired
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_chwdexp_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "Charger watchdog expired\n");

	/*
	 * The charger that was online when the watchdog expired
	 * needs to be restarted for charging to start again
	 */
	if (di->ac.charger_online) {
		di->ac.wd_expired = true;
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
	}
	if (di->usb.charger_online) {
		di->usb.wd_expired = true;
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
	}

	return IRQ_HANDLED;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/*TODO: Add header */
static irqreturn_t ab8500_charger_main_drop_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;
	dev_err(di->dev, "Main charger voltage drop seen\n");
	queue_work(di->charger_wq,&di->handle_main_voltage_drop_work);
	return IRQ_HANDLED;
}


static irqreturn_t ab8500_charger_vbus_drop_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;
	dev_err(di->dev, "USB charger voltage drop seen\n");
	queue_work(di->charger_wq,&di->handle_vbus_voltage_drop_work);
	return IRQ_HANDLED;
}
#endif

/**
 * ab8500_charger_vbusovv_handler() - VBUS overvoltage detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbusovv_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_err(di->dev, "VBUS overvoltage detected\n");
	di->flags.vbus_ovv = true;
#if (defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )) && defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
	ab8500_power_supply_changed(di, &di->ac_chg.psy);
#else
	ab8500_power_supply_changed(di, &di->usb_chg.psy);
#endif
	/* Schedule a new HW failure check */
	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_ac_get_property() - get the ac/mains properties
 * @psy:       pointer to the power_supply structure
 * @psp:       pointer to the power_supply_property structure
 * @val:       pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the ac/mains
 * properties by reading the sysfs files.
 * AC/Mains properties are online, present and voltage.
 * online:     ac/mains charging is in progress or not
 * present:    presence of the ac/mains
 * voltage:    AC/Mains voltage
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_charger *di;
	int ret;

	di = to_ab8500_charger_ac_device_info(psy_to_ux500_charger(psy));

	switch (psp) {
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	case POWER_SUPPLY_PROP_STATUS:
#if defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		if (di->usb.charger_suspended) {
			val->intval = POWER_SUPPLY_STATUS_SUSPENDED ;
		} else
#endif
		if (di->ac.charger_online) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING ;
		} else if (di->ac.charger_connected ) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING ;
		} else {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING ;
		}
		break ;
#endif
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->flags.mainextchnotok)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (di->ac.wd_expired || di->usb.wd_expired)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (di->flags.main_thermal_prot)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
#if (defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )) && defined( CONFIG_AB8XX0_USBCHG_ON_MAIN )
		else if (di->flags.vbus_ovv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
#endif
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (di->cable_type == POWER_SUPPLY_TYPE_MAINS) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		/* Properties of the cable connected */
		if ( di->ac.charger_connected )
			val->intval = di->cable_type;
		else
			val->intval = 0;
#else
		val->intval = di->ac.charger_connected;
#endif
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = ab8500_charger_get_vbus_voltage(di);
		if (ret >= 0)
			di->ac.charger_voltage = ret;
		/* On error, use previous value */
		val->intval = di->ac.charger_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/*
		 * This property is used to indicate when CV mode is entered
		 * for the AC charger
		 */
		di->ac.cv_active = ab8500_charger_ac_cv(di);
		val->intval = di->ac.cv_active;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = ab8500_charger_get_usb_current(di);
		if (ret >= 0)
			di->ac.charger_current = ret;
		val->intval = di->ac.charger_current * 1000;
		break;
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		/*
		 * This property is used to indicate when VBUS has collapsed
		 * due to too high output current from the charger
		 */
		if (di->flags.vbus_collapse)
			val->intval = 1;
		else
			val->intval = 0;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * ab8500_charger_usb_get_property() - get the usb properties
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
static int ab8500_charger_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_charger *di;
	int ret = 0;

	di = to_ab8500_charger_usb_device_info(psy_to_ux500_charger(psy));

	switch (psp) {
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	case POWER_SUPPLY_PROP_STATUS:
		if (di->usb.charger_suspended) {
			val->intval = POWER_SUPPLY_STATUS_SUSPENDED ;
		} else if (di->usb.charger_online) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING ;
		} else if (di->usb.charger_connected ) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING ;
		} else {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING ;
		}
		break ;
#endif

	case POWER_SUPPLY_PROP_HEALTH:
		if (di->flags.usbchargernotok)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (di->ac.wd_expired || di->usb.wd_expired)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (di->flags.usb_thermal_prot)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (di->flags.vbus_ovv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (di->cable_type == POWER_SUPPLY_TYPE_USB) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
		/* Properties of the cable connected */
		if ( di->usb.charger_connected )
			val->intval = di->cable_type;
		else
			val->intval = 0;
#else
		val->intval = di->usb.charger_connected;
#endif
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = ab8500_charger_get_vbus_voltage(di);
		if (ret >= 0)
			di->usb.charger_voltage = ret;
		val->intval = di->usb.charger_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/*
		 * This property is used to indicate when CV mode is entered
		 * for the USB charger
		 */
		di->usb.cv_active = ab8500_charger_usb_cv(di);
		val->intval = di->usb.cv_active;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = ab8500_charger_get_usb_current(di);
		if (ret >= 0)
			di->usb.charger_current = ret;
		val->intval = di->usb.charger_current * 1000;
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
		ret = -EINVAL;
	}
	return ret;
}

/**
 * ab8500_charger_init_hw_registers() - Set up charger related registers
 * @di:		pointer to the ab8500_charger structure
 *
 * Set up charger OVV, watchdog and maximum voltage registers as well as
 * charging of the backup battery
 */
static int ab8500_charger_init_hw_registers(struct ab8500_charger *di)
{
	int ret = 0;

	/* Setup maximum charger current and voltage for ABB cut2.0 OR devices not an AB8500 */
	if (!is_ab8500_1p1_or_earlier(di->parent) || !is_ab8500(di->parent)) {
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_MAX_REG, CH_VOL_LVL_4P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_VOLT_LVL_MAX_REG\n");
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_CH_OPT_CRNTLVL_MAX_REG, CH_OP_CUR_LVL_1P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_OPT_CRNTLVL_MAX_REG\n");
			goto out;
		}
	}

	/* VBUS OVV set to 10V and enable automatic current limitiation */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_USBCH_CTRL2_REG,
				0x44);
	if (ret) {
		dev_err(di->dev, "failed to set VBUS OVV\n");
		goto out;
	}

	/* Enable main watchdog in OTP */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_OTP_EMUL, AB8500_OTP_CONF_15, OTP_ENABLE_WD);
	if (ret) {
		dev_err(di->dev, "failed to enable main WD in OTP\n");
		goto out;
	}

	/* Enable main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG, MAIN_WDOG_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable main watchdog\n");
		goto out;
	}

	/*
	 * Due to internal synchronisation, Enable and Kick watchdog bits
	 * cannot be enabled in a single write.
	 * A minimum delay of 2*32 kHz period (62.5µs) must be inserted
	 * between writing Enable then Kick bits.
	 */
	udelay(63);

	/* Kick main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG,
		(MAIN_WDOG_ENA | MAIN_WDOG_KICK));
	if (ret) {
		dev_err(di->dev, "failed to kick main watchdog\n");
		goto out;
	}

	/* Disable main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG, MAIN_WDOG_DIS);
	if (ret) {
		dev_err(di->dev, "failed to disable main watchdog\n");
		goto out;
	}

	/* Set watchdog timeout */
	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_WD_TIMER_REG, WD_TIMER);
	if (ret) {
		dev_err(di->dev, "failed to set charger watchdog timeout\n");
		goto out;
	}

	/* Backup battery voltage and current */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_RTC,
		AB8500_RTC_BACKUP_CHG_REG,
		di->bat->bkup_bat_v |
		di->bat->bkup_bat_i);
	if (ret) {
		dev_err(di->dev, "failed to setup backup battery charging\n");
		goto out;
	}

	/* Enable backup battery charging */
	abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG,
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
		(RTC_BUP_CH_ENA | RTC_BUP_CH_OFF_VALID),
		(RTC_BUP_CH_ENA | RTC_BUP_CH_OFF_VALID) );
#else
		RTC_BUP_CH_ENA, RTC_BUP_CH_ENA);
#endif

	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

out:
	return ret;
}

/*
 * ab8500 charger driver interrupts and their respective isr
 */
static struct ab8500_charger_interrupts ab8500_charger_irq[] = {
	{"MAIN_CH_UNPLUG_DET", ab8500_charger_mainchunplugdet_handler},
	{"MAIN_CHARGE_PLUG_DET", ab8500_charger_mainchplugdet_handler},
	{"MAIN_EXT_CH_NOT_OK", ab8500_charger_mainextchnotok_handler},
	{"MAIN_CH_TH_PROT_R", ab8500_charger_mainchthprotr_handler},
	{"MAIN_CH_TH_PROT_F", ab8500_charger_mainchthprotf_handler},
	{"VBUS_DET_F", ab8500_charger_vbusdetf_handler},
	{"VBUS_DET_R", ab8500_charger_vbusdetr_handler},
	{"USB_LINK_STATUS", ab8500_charger_usblinkstatus_handler},
	{"USB_CH_TH_PROT_R", ab8500_charger_usbchthprotr_handler},
	{"USB_CH_TH_PROT_F", ab8500_charger_usbchthprotf_handler},
	{"USB_CHARGER_NOT_OKR", ab8500_charger_usbchargernotokr_handler},
	{"VBUS_OVV", ab8500_charger_vbusovv_handler},
	{"CH_WD_EXP", ab8500_charger_chwdexp_handler},
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	{"MAIN_CH_DROP_END",ab8500_charger_main_drop_handler},
	{"VBUS_CH_DROP_END",ab8500_charger_vbus_drop_handler},
#endif
};

static int ab8505_charger_musb_notifier_call(struct notifier_block *nb,
		unsigned long link_status, void *v)
{
	struct ab8500_charger *di =
		container_of(nb, struct ab8500_charger, musb_nb);

	di->link_status = link_status;
	dev_dbg(di->dev, "%s link status: 0x%02x\n", __func__, di->link_status);
	queue_work(di->charger_wq, &di->usb_link_status_work);
	return NOTIFY_OK;
}

static int ab8500_charger_usb_notifier_call(struct notifier_block *nb,
		unsigned long event, void *power)
{
	struct ab8500_charger *di =
		container_of(nb, struct ab8500_charger, usb_nb);
	enum ab8500_usb_state bm_usb_state;
	unsigned mA = *((unsigned *)power);

	if ( !di ){
		pr_crit("%s: No Charger driver info!\n", __func__);
		return NOTIFY_DONE;
	}

	if (event != USB_EVENT_VBUS) {
		dev_info(di->dev, "not a standard host EVENT, returning\n");
		return NOTIFY_DONE;
	}

	/* TODO: State is fabricate  here. See if charger really needs USB
	 * state or if mA is enough
	 */
	if ((di->usb_state.usb_current == 2) && (mA > 2))
		bm_usb_state = AB8500_BM_USB_STATE_RESUME;
	else if (mA == 0)
		bm_usb_state = AB8500_BM_USB_STATE_RESET_HS;
	else if (mA == 2)
		bm_usb_state = AB8500_BM_USB_STATE_SUSPEND;
	else if (mA >= 8) /* 8, 100, 500 */
		bm_usb_state = AB8500_BM_USB_STATE_CONFIGURED;
	else /* Should never occur */
		bm_usb_state = AB8500_BM_USB_STATE_RESET_FS;

	dev_dbg(di->dev, "%s: usb_state=0x%02x, mA=%d\n",
		__func__, bm_usb_state, mA);

	spin_lock(&di->usb_state.usb_lock);
	di->usb_state.usb_changed = true;
	spin_unlock(&di->usb_state.usb_lock);

	di->usb_state.state = bm_usb_state;
	di->usb_state.usb_current = mA;

	queue_work(di->charger_wq, &di->usb_state_changed_work);

	return NOTIFY_OK;
}

#if defined(CONFIG_PM)
static int ab8500_charger_resume(struct platform_device *pdev)
{
	int ret;
	struct ab8500_charger *di = platform_get_drvdata(pdev);

	/*
	 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
	 * logic. That means we have to continously kick the charger
	 * watchdog even when no charger is connected. This is only
	 * valid once the AC charger has been enabled. This is
	 * a bug that is not handled by the algorithm and the
	 * watchdog have to be kicked by the charger driver
	 * when the AC charger is disabled
	 */
	if (di->ac_conn && is_ab8500_1p1_or_earlier(di->parent)) {
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
		if (ret)
			dev_err(di->dev, "Failed to kick WD!\n");

		/* If not already pending start a new timer */
		if (!delayed_work_pending(
			&di->kick_wd_work)) {
			queue_delayed_work(di->charger_wq, &di->kick_wd_work,
				round_jiffies(WD_KICK_INTERVAL));
		}
	}

	/* If we still have a HW failure, schedule a new check */
	if (di->flags.mainextchnotok || di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, 0);
	}

	flush_delayed_work_sync(&di->check_usbchgnotok_work);
	flush_delayed_work_sync(&di->check_vbat_work);
	flush_delayed_work_sync(&di->kick_wd_work);
	flush_work_sync(&di->usb_link_status_work);
#if defined (CONFIG_USB_SWITCHER) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	flush_work_sync(&di->ac_work);
#else
	flush_work_sync(&di->ac_work);
#endif
	flush_work_sync(&di->detect_usb_type_work);
	return 0;
}

static int ab8500_charger_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_charger *di = platform_get_drvdata(pdev);

	/* Cancel any pending HW failure check */
	if (delayed_work_pending(&di->check_hw_failure_work))
		cancel_delayed_work(&di->check_hw_failure_work);

#if !defined (CONFIG_USB_SWITCHER) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	flush_delayed_work_sync(&di->usb_charger_attached_work);
	flush_delayed_work_sync(&di->ac_charger_attached_work);
#endif
	flush_delayed_work_sync(&di->check_usbchgnotok_work);
	flush_delayed_work_sync(&di->check_vbat_work);
	flush_delayed_work_sync(&di->kick_wd_work);

	flush_work_sync(&di->usb_link_status_work);
#if defined (CONFIG_USB_SWITCHER) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	flush_work_sync(&di->ac_work);
#else
	flush_work_sync(&di->ac_work);
#endif
	flush_work_sync(&di->detect_usb_type_work);

	return 0;
}
#else
#define ab8500_charger_suspend      NULL
#define ab8500_charger_resume       NULL
#endif

struct ab8500_charger *pCharger;

static int __devexit ab8500_charger_remove(struct platform_device *pdev)
{
	struct ab8500_charger *di = platform_get_drvdata(pdev);
	int i, irq, ret;

	/* Disable AC charging */
	ab8500_charger_ac_en(&di->ac_chg, false, 0, 0);

	/* Disable USB charging */
	ab8500_charger_usb_en(&di->usb_chg, false, 0, 0);

	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_charger_irq); i++) {
		if (!is_ab8500(di->parent) &&
				strcmp(ab8500_charger_irq[i].name,
				       "USB_LINK_STATUS") == 0) {
			continue;
		}
		irq = platform_get_irq_byname(pdev, ab8500_charger_irq[i].name);
		free_irq(irq, di);
	}

	/* disable the regulator */
	regulator_put(di->regu);

	/* Backup battery voltage and current disable */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG, RTC_BUP_CH_ENA, 0);
	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

	if (!is_ab8500(di->parent))
		micro_usb_unregister_usb_notifier(&di->musb_nb);

	otg_unregister_notifier(di->otg, &di->usb_nb);
	otg_put_transceiver(di->otg);

	/* Delete the work queue */
	destroy_workqueue(di->charger_wq);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	del_timer_sync(&di->usb_link_status_timer);
	wake_lock_destroy(&di->ab8500_vbus_wake_lock);
	wake_lock_destroy(&di->ab8500_vbus_detect_charging_lock);
#endif

	flush_scheduled_work();
	if(di->usb_chg.enabled)
		power_supply_unregister(&di->usb_chg.psy);
	if(di->ac_chg.enabled)
		power_supply_unregister(&di->ac_chg.psy);

	platform_set_drvdata(pdev, NULL);
	pCharger = NULL;
	kfree(di);

	return 0;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
static int dummy(struct ux500_charger *charger,
       int enable, int vset, int iset)
{
       struct ab8500_charger *di = to_ab8500_charger_ac_device_info(charger);
       dev_dbg(di->dev, "%s enable %d, vset %d, iset %d\n",
	       __func__, enable, vset, iset);
       return 0;
}
#endif

#ifdef CONFIG_FAKE_DFMS_DEVICES
void register_charging_i2c_dev(struct device * dev) ;
#endif

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
/*
	The USB switcher can confuse the charger detection logic so we need to refer to the switcher
	driver.
	However we have a startup race between the two drivers,which the USB switcher needs to win so
	if the switcher driver is used then this driver performs its final setup in response to a notification
	event issued by the usb switch driver.

	If the fsa wins the race and is initalise first then it calls the notication in response to the registration
	function
*/


static int usb_switcher_notify(struct notifier_block *self, unsigned long action, void *dev)
{
	static int first = 0;
	int irq, i, charger_status, ret = 0;
	static int previous_status;

	struct ab8500_charger *di = container_of(self,
		struct ab8500_charger, usbsw_nb);

	if (action & USB_SWITCH_DRIVER_STARTED ) {

		/* Initialize OVV, and other registers */
		ret = ab8500_charger_init_hw_registers(di);
		if (ret) {
			dev_err(di->dev, "failed to initialize ABB registers\n");
			goto free_charger_wq;
		}

		/* Register AC charger class */
		if(di->ac_chg.enabled) {
			ret = power_supply_register(di->dev, &di->ac_chg.psy);
			if (ret) {
				dev_err(di->dev, "failed to register AC charger\n");
				goto free_charger_wq;
			}
		}

		/* Register USB charger class */
		if(di->usb_chg.enabled) {
			ret = power_supply_register(di->dev, &di->usb_chg.psy);
			if (ret) {
				dev_err(di->dev, "failed to register USB charger\n");
				goto free_ac;
			}
		}

		di->otg = otg_get_transceiver();
		if (!di->otg) {
			dev_err(di->dev, "failed to get otg transceiver\n");
			ret = -EINVAL;
			goto free_usb;
		}
		di->nb.notifier_call = ab8500_charger_usb_notifier_call;
		ret = otg_register_notifier(di->otg, &di->nb);
		if (ret) {
			dev_err(di->dev, "failed to register otg notifier\n");
			goto put_otg_transceiver;
		}

		di->registered_with_power=1 ;

		/* Identify the connected charger types during startup */
		charger_status = ab8500_charger_detect_chargers(di);
		if (charger_status & AC_PW_CONN) {
			dev_err(di->dev, "%s MainCh Detect during startup\n", __func__);

			wake_lock_timeout(
				&di->ab8500_vbus_detect_charging_lock, 3*HZ);
			queue_delayed_work(
				di->charger_wq, &di->ac_work, 0);

#ifdef CONFIG_MACH_JANICE
			queue_work(di->charger_wq, &di->tsp_vbus_notify_work);
#endif
		}

		if (charger_status & USB_PW_CONN) {
			dev_err(di->dev, "VBUS Detect during startup\n");
			di->vbus_detected = true;
			di->vbus_detected_start = true;
			queue_work(di->charger_wq, &di->detect_usb_type_work);
		}

		/* Register interrupts */
		for (i = 0; i < ARRAY_SIZE(ab8500_charger_irq); i++) {
			irq = platform_get_irq_byname(di->pdev
				, ab8500_charger_irq[i].name);

			ret = request_threaded_irq(irq, NULL
				, ab8500_charger_irq[i].isr
				,IRQF_SHARED | IRQF_NO_SUSPEND
				, ab8500_charger_irq[i].name, di);

			if (ret != 0) {
				dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				    ,ab8500_charger_irq[i].name, irq, ret);
				break;
			}

			dev_dbg(di->dev, "Requested %s IRQ %d: %d\n"
				, ab8500_charger_irq[i].name, irq, ret);
		}

		pCharger = di;

#ifdef CONFIG_FAKE_DFMS_DEVICES
		register_charging_i2c_dev(di->dev);
#endif
	}else {
		if ( ! first ){
			dev_err(di->dev, "%s First time entry should not come here!\n", __func__);
		}

		di->usb.charger_suspended = 0;

		charger_status = ab8500_charger_detect_chargers(di);
		if ( (charger_status & AC_PW_CONN)
		   || (previous_status & AC_PW_CONN) ){

			wake_lock_timeout(
				&di->ab8500_vbus_detect_charging_lock,
				3*HZ);

			if (charger_status & AC_PW_CONN) {
				dev_err(di->dev, "%s MainCh Plug Detect\n", __func__);

				queue_delayed_work(
					di->charger_wq, &di->ac_work, 0);
			}else{
				dev_err(di->dev, "%s MainCh Unplug Detect\n", __func__);

				queue_delayed_work(di->charger_wq,
					&di->ac_work, 0);
			}

#ifdef CONFIG_MACH_JANICE
			queue_work(di->charger_wq, &di->tsp_vbus_notify_work);
#endif
		}

		if ( (charger_status & USB_PW_CONN)
		   || (previous_status & USB_PW_CONN) ){
			if (charger_status == USB_PW_CONN){
				dev_err(di->dev, "%s USB Cable Plug Detect\n", __func__);
				di->vbus_detected = true;
			}
			else{
				dev_err(di->dev, "%s USB Cable Unplug Detect\n", __func__);
			}

			queue_work(di->charger_wq,
				&di->detect_usb_type_work);
		}
	}

	first = 1;
	previous_status = charger_status;

	return NOTIFY_OK ;
put_otg_transceiver:
	otg_put_transceiver(di->otg);
free_usb:
	power_supply_unregister(&di->usb_chg.psy);
free_ac:
	power_supply_unregister(&di->ac_chg.psy);
free_charger_wq:
	destroy_workqueue(di->charger_wq);
	del_timer_sync(&di->usb_link_status_timer);
	wake_lock_destroy(&di->ab8500_vbus_wake_lock);
	wake_lock_destroy(&di->ab8500_vbus_detect_charging_lock);
	kfree(di);
	return NOTIFY_OK ;
}
#endif

//#define CHRG_DEBUG
#ifdef CHRG_DEBUG
static int chrg_regs[] = {
	AB8500_CH_USBCH_STAT1_REG,
	AB8500_CH_USBCH_STAT2_REG,
	AB8500_CH_STAT_REG,
	AB8500_CH_VOLT_LVL_REG,
	AB8500_CH_VOLT_LVL_MAX_REG,  /*Only in Cut2.0*/
	AB8500_CH_OPT_CRNTLVL_REG,
	AB8500_CH_OPT_CRNTLVL_MAX_REG,  /*Only in Cut2.0*/
	AB8500_CH_WD_TIMER_REG,
	AB8500_CHARG_WD_CTRL,
	AB8500_BATT_OVV,
	AB8500_CHARGER_CTRL,
	AB8500_USBCH_CTRL1_REG,
	AB8500_USBCH_CTRL2_REG,
	AB8500_USBCH_IPT_CRNTLVL_REG,
	AB8500_USBCH_OPT_CRNTLVL_REG,
	-1,
};
#endif

static int __devinit ab8500_charger_probe(struct platform_device *pdev)
{
#if !defined( CONFIG_USB_SWITCHER )
	int irq, i, charger_status;
#endif //CONFIG_USB_SWITCHER

	int ret = 0;
	struct ab8500_platform_data *plat;

	struct ab8500_charger *di =
		kzalloc(sizeof(struct ab8500_charger), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	pr_debug("[CHARGER PROBE] start\n");

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab8500_gpadc_get();

	if (!di->gpadc){
		dev_err(di->dev, "No ADC device ready.\n");
		ret = -ENODEV;
		goto free_device_info;
	}

	/* initialize lock */
	spin_lock_init(&di->usb_state.usb_lock);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	init_timer(&di->usb_link_status_timer);
	di->usb_link_status_timer.function=usb_link_status_timer_function;
	di->usb_link_status_timer.data = (unsigned long) di ;
#endif

	plat = dev_get_platdata(di->parent->dev);

	/* get charger specific platform data */
	if (!plat->charger) {
		dev_err(di->dev, "no charger platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->pdata = plat->charger;

	/* get battery specific platform data */
	if (!plat->battery) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->bat = plat->battery;

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	di->cable_type = POWER_SUPPLY_TYPE_BATTERY;
#endif

	di->autopower = false;
	di->invalid_charger_detect_state = 0;

	/* AC supply */
	/* power_supply base class */
	di->ac_chg.psy.name = "ab8500_ac";
	di->ac_chg.psy.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac_chg.psy.properties = ab8500_charger_ac_props;
	di->ac_chg.psy.num_properties = ARRAY_SIZE(ab8500_charger_ac_props);
	di->ac_chg.psy.get_property = ab8500_charger_ac_get_property;
	di->ac_chg.psy.supplied_to = di->pdata->supplied_to;
	di->ac_chg.psy.num_supplicants = di->pdata->num_supplicants;
	/* ux500_charger sub-class */
	di->ac_chg.ops.enable = &ab8500_charger_ac_en;
	di->ac_chg.ops.kick_wd = &ab8500_charger_watchdog_kick;
	di->ac_chg.ops.update_curr = &ab8500_charger_update_charger_current;

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	di->ac_chg.ops.update_input_curr =
		&ab8500_charger_update_charger_input_current;
	di->ac_chg.ops.siop_activation = &ab8500_charger_siop_activation;
#endif

	di->ac_chg.max_out_volt = ab8500_charger_voltage_map[
		ARRAY_SIZE(ab8500_charger_voltage_map) - 1];
	di->ac_chg.max_out_curr = ab8500_charger_current_map[
		ARRAY_SIZE(ab8500_charger_current_map) - 1];
	di->ac_chg.wdt_refresh = CHG_WD_INTERVAL;
	di->ac_chg.enabled = di->pdata->ac_enabled;
	di->ac_chg.external = false;

	/* USB supply */
	/* power_supply base class */
	di->usb_chg.psy.name = "ab8500_usb";
	di->usb_chg.psy.type = POWER_SUPPLY_TYPE_USB;
	di->usb_chg.psy.properties = ab8500_charger_usb_props;
	di->usb_chg.psy.num_properties = ARRAY_SIZE(ab8500_charger_usb_props);
	di->usb_chg.psy.get_property = ab8500_charger_usb_get_property;
	di->usb_chg.psy.supplied_to = di->pdata->supplied_to;
	di->usb_chg.psy.num_supplicants = di->pdata->num_supplicants;
	/* ux500_charger sub-class */
	di->usb_chg.ops.enable = &ab8500_charger_usb_en;
	di->usb_chg.ops.check_enable = &ab8500_charger_check_enable;
	di->usb_chg.ops.kick_wd = &ab8500_charger_watchdog_kick;
	di->usb_chg.ops.update_curr = &ab8500_charger_update_charger_current;

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	di->usb_chg.ops.update_input_curr =
		&ab8500_charger_update_charger_input_current;
	di->usb_chg.ops.siop_activation = &ab8500_charger_siop_activation;
#endif

	di->usb_chg.max_out_volt = ab8500_charger_voltage_map[
		ARRAY_SIZE(ab8500_charger_voltage_map) - 1];
	di->usb_chg.max_out_curr = ab8500_charger_current_map[
		ARRAY_SIZE(ab8500_charger_current_map) - 1];
	di->usb_chg.wdt_refresh = CHG_WD_INTERVAL;
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	wake_lock_init(&di->ab8500_vbus_wake_lock, WAKE_LOCK_SUSPEND,
				"ab8500_vbus_wake_lock");
	wake_lock_init(&di->ab8500_vbus_detect_charging_lock, WAKE_LOCK_SUSPEND,
				"ab8500_vbus_detect_charging_lock");
#endif
	di->usb_chg.enabled = di->pdata->usb_enabled;
	di->usb_chg.external = false;
	di->usb_state.usb_current = -1;

	/* Create a work queue for the charger */
	di->charger_wq =
		create_singlethread_workqueue("ab8500_charger_wq");
	if (di->charger_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

#if !defined( CONFIG_USB_SWITCHER ) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	mutex_init(&di->charger_attached_mutex);
#endif

	/* Init work for HW failure check */
	INIT_DELAYED_WORK_DEFERRABLE(&di->check_hw_failure_work,
		ab8500_charger_check_hw_failure_work);
	INIT_DELAYED_WORK_DEFERRABLE(&di->check_usbchgnotok_work,
		ab8500_charger_check_usbchargernotok_work);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	INIT_DELAYED_WORK_DEFERRABLE(&di->ac_work, ab8500_charger_ac_work);
#else
	INIT_DELAYED_WORK(&di->ac_charger_attached_work,
			  ab8500_charger_ac_attached_work);
	INIT_DELAYED_WORK(&di->usb_charger_attached_work,
			  ab8500_charger_usb_attached_work);
#endif

	/*
	 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
	 * logic. That means we have to continously kick the charger
	 * watchdog even when no charger is connected. This is only
	 * valid once the AC charger has been enabled. This is
	 * a bug that is not handled by the algorithm and the
	 * watchdog have to be kicked by the charger driver
	 * when the AC charger is disabled
	 */
	INIT_DELAYED_WORK_DEFERRABLE(&di->kick_wd_work,
		ab8500_charger_kick_watchdog_work);

	INIT_DELAYED_WORK(&di->attach_work,
		ab8500_charger_usb_link_attach_work);

	INIT_DELAYED_WORK_DEFERRABLE(&di->check_vbat_work,
		ab8500_charger_check_vbat_work);

	/* Init work for charger detection */
	INIT_WORK(&di->usb_link_status_work,
		ab8500_charger_usb_link_status_work);

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	INIT_DELAYED_WORK_DEFERRABLE(&di->ac_work,
		ab8500_charger_ac_work);
#else
	INIT_WORK(&di->ac_work, ab8500_charger_ac_work);
#endif

	INIT_WORK(&di->detect_usb_type_work,
		ab8500_charger_detect_usb_type_work);

	INIT_WORK(&di->usb_state_changed_work,
		ab8500_charger_usb_state_changed_work);

#ifdef CONFIG_MACH_JANICE
	INIT_WORK(&di->tsp_vbus_notify_work,
		ab8500_charger_tsp_vbus_notify_work);
#endif

	/* Init work for checking HW status */
	INIT_WORK(&di->check_main_thermal_prot_work,
		ab8500_charger_check_main_thermal_prot_work);
	INIT_WORK(&di->check_usb_thermal_prot_work,
		ab8500_charger_check_usb_thermal_prot_work);

#if !defined( CONFIG_USB_SWITCHER ) && !defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	wake_lock_init(&di->usb_charger_attached_wake_lock, WAKE_LOCK_SUSPEND,
		       "usb_charger_attached_wake_lock");
	wake_lock_init(&di->ac_charger_attached_wake_lock, WAKE_LOCK_SUSPEND,
		       "ac_charger_attached_wake_lock");
#endif

	/*
	 * VDD ADC supply needs to be enabled from this driver when there
	 * is a charger connected to avoid erroneous BTEMP_HIGH/LOW
	 * interrupts during charging
	 */
	di->regu = regulator_get(di->dev, "vddadc");
	if (IS_ERR(di->regu)) {
		ret = PTR_ERR(di->regu);
		dev_err(di->dev, "failed to get vddadc regulator\n");
		goto free_charger_wq;
	}


	/* Initialize OVV, and other registers */
	ret = ab8500_charger_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize ABB registers\n");
		goto free_regulator;
	}

	/* Register AC charger class */
	if(di->ac_chg.enabled) {
		ret = power_supply_register(di->dev, &di->ac_chg.psy);
		if (ret) {
			dev_err(di->dev, "failed to register AC charger\n");
			goto free_regulator;
		}
	}

	/* Register USB charger class */
	if(di->usb_chg.enabled) {
		ret = power_supply_register(di->dev, &di->usb_chg.psy);
		if (ret) {
			dev_err(di->dev, "failed to register USB charger\n");
			goto free_ac;
		}
	}

	if (!is_ab8500(di->parent)) {
		di->musb_nb.notifier_call = ab8505_charger_musb_notifier_call;
		ret = micro_usb_register_usb_notifier(&di->musb_nb);
		if (ret) {
			dev_err(di->dev,
				"failed to register micro usb notifier\n");
		}
	}
	di->otg = otg_get_transceiver();
	if (!di->otg) {
		dev_err(di->dev, "failed to get otg transceiver\n");
		ret = -EINVAL;
		goto free_usb;
	}
	di->usb_nb.notifier_call = ab8500_charger_usb_notifier_call;
	ret = otg_register_notifier(di->otg, &di->usb_nb);
	if (ret) {
		dev_err(di->dev, "failed to register otg notifier\n");
		goto put_otg_transceiver;
	}

	/* Identify the connected charger types during startup */
	charger_status = ab8500_charger_detect_chargers(di);
	if (charger_status & AC_PW_CONN) {
		di->ac.charger_connected = 1;
		di->ac_conn = true;
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
		sysfs_notify(&di->ac_chg.psy.dev->kobj, NULL, "present");
	}

	if (charger_status & USB_PW_CONN) {
		di->vbus_detected = true;
		di->vbus_detected_start = true;
		queue_work(di->charger_wq,
			&di->detect_usb_type_work);
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_charger_irq); i++) {
		if (!is_ab8500(di->parent) &&
				strcmp(ab8500_charger_irq[i].name,
				       "USB_LINK_STATUS") == 0) {
			continue;
		}
		irq = platform_get_irq_byname(pdev, ab8500_charger_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab8500_charger_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab8500_charger_irq[i].name, di);

		if (ret != 0) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab8500_charger_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab8500_charger_irq[i].name, irq, ret);
	}

	platform_set_drvdata(pdev, di);

	mutex_lock(&di->charger_attached_mutex);

	ch_stat = ab8500_charger_detect_chargers(di);

	if ((ch_stat & AC_PW_CONN) == AC_PW_CONN) {
		if (!wake_lock_active(&di->ac_charger_attached_wake_lock)) {
			wake_lock(&di->ac_charger_attached_wake_lock);
			queue_delayed_work(di->charger_wq,
					   &di->ac_charger_attached_work,
					   HZ);
		}
	}
	if ((ch_stat & USB_PW_CONN) == USB_PW_CONN) {
		if (!wake_lock_active(&di->usb_charger_attached_wake_lock)) {
			wake_lock(&di->usb_charger_attached_wake_lock);
			queue_delayed_work(di->charger_wq,
					   &di->usb_charger_attached_work,
					   HZ);
	}
	}

	mutex_unlock(&di->charger_attached_mutex);

	return ret;

free_irq:
	otg_unregister_notifier(di->otg, &di->usb_nb);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		if (!is_ab8500(di->parent) &&
				strcmp(ab8500_charger_irq[i].name,
				       "USB_LINK_STATUS") == 0) {
			continue;
		}
		irq = platform_get_irq_byname(pdev, ab8500_charger_irq[i].name);
		free_irq(irq, di);
	}
put_otg_transceiver:
	otg_put_transceiver(di->otg);
free_usb:
	if(di->usb_chg.enabled)
		power_supply_unregister(&di->usb_chg.psy);
free_ac:
	if(di->ac_chg.enabled)
		power_supply_unregister(&di->ac_chg.psy);
free_regulator:
	regulator_put(di->regu);
free_charger_wq:
	destroy_workqueue(di->charger_wq);
free_device_info:
#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )
	del_timer_sync(&di->usb_link_status_timer);
	wake_lock_destroy(&di->ab8500_vbus_wake_lock);
	wake_lock_destroy(&di->ab8500_vbus_detect_charging_lock);
#endif
	kfree(di);

	return ret;
}

static struct platform_driver ab8500_charger_driver = {
	.probe = ab8500_charger_probe,
	.remove = __devexit_p(ab8500_charger_remove),
	.suspend = ab8500_charger_suspend,
	.resume = ab8500_charger_resume,
	.driver = {
		.name = "ab8500-charger",
		.owner = THIS_MODULE,
	},
};

static int __init ab8500_charger_init(void)
{
#if defined(CONFIG_USB_SWITCHER)
#if defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_SEC_KYLE)
	if (use_ab8505_iddet)
		return 0;

	pr_info("%s, charger is initialized with TSU6111 muic\n", __func__);
#endif
	return platform_driver_register(&ab8500_charger_driver);
#else
	return 0;
#endif
}

static void __exit ab8500_charger_exit(void)
{
	platform_driver_unregister(&ab8500_charger_driver);
}

subsys_initcall_sync(ab8500_charger_init);
module_exit(ab8500_charger_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski, Arun R Murthy");
MODULE_ALIAS("platform:ab8500-charger");
MODULE_DESCRIPTION("AB8500 charger management driver");
