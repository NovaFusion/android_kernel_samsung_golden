/*
 * abb_charger.h
 * Samsung Mobile Charger Header for AB850x
 *
 * Copyright (C) 2012 Samsung Electronics, Inc.
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __AB8500_CHARGER_H
#define __AB8500_CHARGER_H __FILE__

#include <linux/battery/charger/abb_sec_charger.h>
#include <linux/battery/sec_charging_common.h>



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
#define AUTO_VBUS_CHG_INP_CURR_MASK		0xF0
#define AUTO_VBUS_CHG_INP_CURR_SHIFT	4

#define MAIN_CH_INPUT_CURR_SHIFT	4
#define VBUS_IN_CURR_LIM_SHIFT		4
#define VBUS_DROP_COUNT_LIMIT		2

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
#define AB8500_STD_HOST_SUSP		0x18

/* Watchdog timeout constant */
#define WD_TIMER			0x30 /* 4min */
#define WD_KICK_INTERVAL		(60 * HZ)

/* Lowest charger voltage is 3.39V -> 0x4E */
#define LOW_VOLT_REG			0x4E

/* Step up/down delay in us */
#define STEP_UDELAY			10000

/* Wait for enumeration before charging in ms */
#define WAIT_FOR_USB_ENUMERATION	(5 * 1000)

#define CHARGER_STATUS_POLL 10 /* in ms */

#define CHG_WD_INTERVAL			(60 * HZ)
#define BATT_OVV			0x01
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

#define AB8500_SW_CONTROL_FALLBACK	0x03

#define AB8500_BAT_CTRL_CURRENT_SOURCE_DEFAULT 0x14

#define BTEMP_BATCTRL_CURR_SRC_7UA	7
#define BTEMP_BATCTRL_CURR_SRC_20UA	20
#define BTEMP_BATCTRL_CURR_SRC_16UA	16
#define BTEMP_BATCTRL_CURR_SRC_18UA	18

/* flag mask of interrupt */
#define F_MAIN_EXT_CH_NOT_OK	(1 << 0)
#define F_USB_CHARGER_NOT_OK	(1 << 1)
#define F_MAIN_THERMAL_PROT	(1 << 2)
#define F_USB_THERMAL_PROT	(1 << 3)
#define F_VBUS_OVV		(1 << 4)
#define F_BATT_OVV		(1 << 5)
#define F_CHG_WD_EXP		(1 << 6)
#define F_BATT_REMOVE		(1 << 7)
#define F_BTEMP_HIGH		(1 << 8)
#define F_BTEMP_MEDHIGH		(1 << 9)
#define F_BTEMP_LOWMED		(1 << 10)
#define F_BTEMP_LOW		(1 << 11)

/* register mask of interrupt */
#define M_MAIN_EXT_CH_NOT_OK	(1 << 0)
#define M_USB_CHARGER_NOT_OK	(1 << 3)
#define M_MAIN_THERMAL_PROT	(1 << 1)
#define M_USB_THERMAL_PROT	(1 << 1)
#define M_VBUS_OVV		(1 << 0)
#define M_BATT_OVV		(1 << 0)

/*
 * This array maps the raw hex value to charger voltage used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_chg_voltage_map[] = {
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

static int ab8500_chg_current_map[] = {
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

static int ab8500_chg_vbus_in_curr_map[] = {
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

static int ab8500_chg_register[] = {
	AB8500_CH_STATUS1_REG,
	AB8500_CH_STATUS2_REG,
	AB8500_CH_USBCH_STAT1_REG,
	AB8500_CH_USBCH_STAT2_REG,
	AB8500_CH_STAT_REG,
	AB8500_CH_VOLT_LVL_REG,
	AB8500_CH_VOLT_LVL_MAX_REG,
	AB8500_CH_OPT_CRNTLVL_REG,
	AB8500_CH_OPT_CRNTLVL_MAX_REG,
	0x44,
	AB8500_CH_WD_TIMER_REG,
	AB8500_CHARG_WD_CTRL,
	AB8500_BTEMP_HIGH_TH,
	AB8500_LED_INDICATOR_PWM_CTRL,
	AB8500_LED_INDICATOR_PWM_DUTY,
	AB8500_BATT_OVV,
	AB8500_CHARGER_CTRL,
	AB8500_BAT_CTRL_CURRENT_SOURCE,
	AB8500_MCH_CTRL1,
	AB8500_MCH_CTRL2,
	AB8500_MCH_IPT_CURLVL_REG,
	AB8500_USBCH_CTRL1_REG,
	AB8500_USBCH_CTRL2_REG,
	AB8500_USBCH_IPT_CRNTLVL_REG,
	AB8500_USBCH_OPT_CRNTLVL_REG,
	0xF6,
};

static enum power_supply_property ab8500_chg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

struct ab8500_charger_event_flags {
	int irq_flag;
	int irq_flag_shadow;
	int irq_first;
	__kernel_time_t irq_first_time_stamps;
	char reg_addr[150];
	char reg_data[150];
};

struct ab8500_charger_info {

	struct device *dev;
	sec_battery_platform_data_t *pdata;
	struct ab8500 *parent;
	struct power_supply		psy;

	int cable_type;
	bool is_charging;

	/* charging current : + charging, - OTG */
	int charging_current;

	/* register programming */
	int reg_addr;
	int reg_data;

	bool autopower;
	bool vddadc_en;

	int vbat;
	int old_vbat;
	int curr_source;
	int prev_batctrl;
	int res_batctrl;
	int charging_reenabled;
	int vbus_drop_count;
	int input_current_limit;

	struct ab8500_gpadc *gpadc;
	struct ab8500_charger_event_flags flags;
	struct workqueue_struct *charger_wq;
	struct delayed_work check_hw_failure_work;
	struct delayed_work check_hw_failure_delay_work;
	struct delayed_work kick_wd_work;
	struct delayed_work chg_attached_work;
	struct delayed_work handle_vbus_voltage_drop_work;
	struct work_struct handle_main_voltage_drop_work;

	struct mutex chg_attached_mutex;
	struct wake_lock chg_attached_wake_lock;
	struct regulator *regu;
};

struct ab8500_charger_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab8500_charger_event_list {
	char *name;
	int flag_mask;
	u8 reg_mask;
};

ssize_t ab8500_chg_show_attrs(struct device *dev,
			      struct device_attribute *attr, char *buf);

ssize_t ab8500_chg_store_attrs(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count);

#define SEC_CHARGER_ATTR(_name)				\
{							\
	.attr = {.name = #_name, .mode = 0664},	\
	.show = ab8500_chg_show_attrs,			\
	.store = ab8500_chg_store_attrs,			\
}

static struct device_attribute ab8500_charger_attrs[] = {
	SEC_CHARGER_ATTR(reg),
	SEC_CHARGER_ATTR(data),
	SEC_CHARGER_ATTR(regs),
};

enum {
	CHG_REG = 0,
	CHG_DATA,
	CHG_REGS,
};

#endif /* __AB8500_CHARGER_H */



