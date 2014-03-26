/*
 * ab8500_fuelgauge.h
 * Samsung Mobile Fuel Gauge Header for AB850x
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

#ifndef __AB8500_FUELGAUGE_H
#define __AB8500_FUELGAUGE_H __FILE__

#include <linux/battery/charger/abb_sec_charger.h>
#include <linux/battery/sec_charging_common.h>


#define USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
#define INS_CURR_TIMEOUT		(3 * HZ)

/* fg_res parameter should be re-calculated
   according to the model and HW revision */

#if defined(CONFIG_MACH_JANICE)
#define FGRES_HWREV_02			133
#define FGRES_HWREV_02_CH		133
#define FGRES_HWREV_03			121
#define FGRES_HWREV_03_CH		120
#elif defined(CONFIG_MACH_CODINA) || \
	defined(CONFIG_MACH_SEC_GOLDEN) || \
	defined(CONFIG_MACH_SEC_KYLE) || \
	defined(CONFIG_MACH_SEC_RICCO)
#define FGRES				130
#define FGRES_CH			125
#else
#define FGRES				130
#define FGRES_CH			133
#endif

#define MAGIC_CODE			0x29
#define MAGIC_CODE_RESET		0x2F
#define OFF_MAGIC_CODE			25
#define OFF_MAGIC_CODE_MASK		0x3F

#define OFF_CHARGE_STATE		0
#define OFF_CAPACITY			5
#define OFF_VOLTAGE			12

#define OFF_CHARGE_STATE_MASK		0x1F
#define OFF_CAPACITY_MASK		0x7F
#define OFF_VOLTAGE_MASK		0x1FFF

#define LOWBAT_TOLERANCE		40

#define MAIN_CH_NO_OVERSHOOT_ENA_N	0x02
#define MAIN_CH_ENA			0x01

#define MILLI_TO_MICRO			1000
#define FG_LSB_IN_MA			1627
#define QLSB_NANO_AMP_HOURS_X10		1129
#define INS_CURR_TIMEOUT		(3 * HZ)

#define SEC_TO_SAMPLE(S)		(S * 4)

#define NBR_AVG_SAMPLES			20

#define LOW_BAT_CHECK_INTERVAL		(5 * HZ)

#define VALID_CAPACITY_SEC		(45 * 60) /* 45 minutes */

#define VBAT_ADC_CAL			3700

#define CONFIG_BATT_CAPACITY_PARAM
#define BATT_CAPACITY_PATH		"/efs/last_battery_capacity"

#define WAIT_FOR_INST_CURRENT_MAX	70
#define IGNORE_VBAT_HIGHCUR	-500 /* -500mA */
#define CURR_VAR_HIGH	100 /*100mA*/
#define NBR_CURR_SAMPLES	10

#define BATT_OK_MIN			2360 /* mV */
#define BATT_OK_INCREMENT		50 /* mV */
#define BATT_OK_MAX_NR_INCREMENTS	0xE

/* FG constants */

#define CHARGSTOP_SECUR			0x08
#define USBCH_ON			0x04

#define VBUS_DET_DBNC100		0x02
#define VBUS_DET_DBNC1			0x01

/*
 * This array maps the raw hex value to lowbat voltage used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_fg_lowbat_voltage_map[] = {
	2300 ,
	2325 ,
	2350 ,
	2375 ,
	2400 ,
	2425 ,
	2450 ,
	2475 ,
	2500 ,
	2525 ,
	2550 ,
	2575 ,
	2600 ,
	2625 ,
	2650 ,
	2675 ,
	2700 ,
	2725 ,
	2750 ,
	2775 ,
	2800 ,
	2825 ,
	2850 ,
	2875 ,
	2900 ,
	2925 ,
	2950 ,
	2975 ,
	3000 ,
	3025 ,
	3050 ,
	3075 ,
	3100 ,
	3125 ,
	3150 ,
	3175 ,
	3200 ,
	3225 ,
	3250 ,
	3275 ,
	3300 ,
	3325 ,
	3350 ,
	3375 ,
	3400 ,
	3425 ,
	3450 ,
	3475 ,
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
	3850 ,
};

static enum power_supply_property ab8500_fuelgauge_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
};

#ifndef ABS
#define ABS(a) ((a) > 0 ? (a) : -(a))
#endif

#define interpolate(x, x1, y1, x2, y2) \
	((y1) + ((((y2) - (y1)) * ((x) - (x1))) / ((x2) - (x1))));

extern void (*sec_set_param_value) (int idx, void *value);
extern void (*sec_get_param_value) (int idx, void *value);
extern int register_reboot_notifier(struct notifier_block *nb);

extern unsigned int system_rev;

struct ab8500_fg_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

enum ab8500_fg_discharge_state {
	AB8500_FG_DISCHARGE_INIT,
	AB8500_FG_DISCHARGE_INITMEASURING,
	AB8500_FG_DISCHARGE_INIT_RECOVERY,
	AB8500_FG_DISCHARGE_RECOVERY,
	AB8500_FG_DISCHARGE_READOUT_INIT,
	AB8500_FG_DISCHARGE_READOUT,
	AB8500_FG_DISCHARGE_WAKEUP,
};

static char *discharge_state[] = {
	"DISCHARGE_INIT",
	"DISCHARGE_INITMEASURING",
	"DISCHARGE_INIT_RECOVERY",
	"DISCHARGE_RECOVERY",
	"DISCHARGE_READOUT_INIT",
	"DISCHARGE_READOUT",
	"DISCHARGE_WAKEUP",
};

enum ab8500_fg_charge_state {
	AB8500_FG_CHARGE_INIT,
	AB8500_FG_CHARGE_READOUT,
};

static char *charge_state[] = {
	"CHARGE_INIT",
	"CHARGE_READOUT",
};

enum ab8500_fg_calibration_state {
	AB8500_FG_CALIB_INIT,
	AB8500_FG_CALIB_WAIT,
	AB8500_FG_CALIB_END,
};

struct ab8500_fg_avg_cap {
	int avg;
	int samples[NBR_AVG_SAMPLES];
	__kernel_time_t time_stamps[NBR_AVG_SAMPLES];
	int pos;
	int nbr_samples;
	int sum;
};

struct ab8500_fg_cap_scaling {
	bool enable;
	int cap_to_scale[2];
	int disable_cap_level;
	int scaled_cap;
};

struct ab8500_fg_vbased_cap {
	int avg;
	int samples[NBR_AVG_SAMPLES];
	__kernel_time_t time_stamps[NBR_AVG_SAMPLES];
	int pos;
	int nbr_samples;
	int sum;
};

struct ab8500_fg_instant_current {
	int avg;
	int pre_sample;
	int samples[NBR_AVG_SAMPLES];
	__kernel_time_t time_stamps[NBR_AVG_SAMPLES];
	int nbr_samples;
	int high_diff_nbr;
	int sum;
	int pos;
};

struct ab8500_fg_battery_capacity {
	int max_mah_design;
	int max_mah;
	int mah;
	int permille;
	int level;
	int prev_mah;
	int prev_percent;
	int prev_level;
	int user_mah;
	struct ab8500_fg_cap_scaling cap_scale;
};

struct ab8500_fg_flags {
	bool fg_enabled;
	bool conv_done;
	bool charging;
	bool fully_charged;
	bool force_full;

	bool low_bat_delay;
	bool low_bat;
	bool batt_unknown;
	bool calibrate;
};

struct ab8500_fuelgauge_info {

	struct device *dev;
	sec_battery_platform_data_t *pdata;
	struct ab8500 *parent;
	struct power_supply		psy;

	int cable_type;

	int irq;
	int vbat;
	unsigned long boot_time;

	int vbat_nom;
	int inst_curr;
	int skip_add_sample;
	int n_skip_add_sample;

	int avg_curr;
	int fg_samples;
	int accu_charge;
	int recovery_cnt;
	int high_curr_cnt;
	int init_cnt;
	int low_bat_cnt;
	int nbr_cceoc_irq_cnt;

	int new_capacity;
	int new_capacity_volt;
	int param_capacity;
	int prev_capacity;
	int fg_res_dischg;
	int fg_res_chg;
	int fg_res;
	int lowbat_zero_voltage;

	bool initial_capacity_calib;

	bool recovery_needed;
	bool high_curr_mode;
	bool init_capacity;

	bool reinit_capacity;
	bool lpm_chg_mode;
	bool lowbat_poweroff;
	bool lowbat_poweroff_locked;
	bool max_cap_changed;

	bool turn_off_fg;
	enum ab8500_fg_calibration_state calib_state;
	enum ab8500_fg_discharge_state discharge_state;
	enum ab8500_fg_charge_state charge_state;
	struct completion ab8500_fg_started;
	struct completion ab8500_fg_complete;
	struct ab8500_fg_flags flags;
	struct ab8500_fg_battery_capacity bat_cap;

	struct ab8500_fg_vbased_cap vbat_cap;
	struct ab8500_fg_instant_current inst_cur;

	struct ab8500_fg_avg_cap avg_cap;
	struct ab8500_gpadc *gpadc;

	struct workqueue_struct *fg_wq;

	struct delayed_work fg_periodic_work;
	struct delayed_work fg_low_bat_work;
	struct delayed_work fg_reinit_work;
	struct delayed_work fg_reinit_param_work;

	struct work_struct fg_work;
	struct work_struct fg_acc_cur_work;

	struct notifier_block fg_notifier;

	struct mutex cc_lock;

	struct wake_lock lowbat_wake_lock;
	struct wake_lock lowbat_poweroff_wake_lock;

};

ssize_t ab8500_fg_show_attrs(struct device *dev,
			     struct device_attribute *attr, char *buf);

ssize_t ab8500_fg_store_attrs(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);

#define SEC_FG_ATTR(_name)				\
{							\
	.attr = {.name = #_name, .mode = 0664},	\
	.show = ab8500_fg_show_attrs,			\
	.store = ab8500_fg_store_attrs,			\
}

static struct device_attribute ab8500_fg_attrs[] = {
	SEC_FG_ATTR(reg),
	SEC_FG_ATTR(data),
	SEC_FG_ATTR(regs),
	SEC_FG_ATTR(reinit_cap),
};

enum {
	FG_REG = 0,
	FG_DATA,
	FG_REGS,
	REINIT_CAP,
};

#endif /* __AB8500_FUELGAUGE_H */
