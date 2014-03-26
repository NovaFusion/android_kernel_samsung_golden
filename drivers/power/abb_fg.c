/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * Main and Back-up battery management driver.
 *
 * Note: Backup battery management is required in case of Li-Ion battery and not
 * for capacitive battery. HREF boards have capacitive battery and hence backup
 * battery management is not used and the supported code is available in this
 * driver.
 *
 * License Terms: GNU General Public License v2
 * Author: Johan Palsson <johan.palsson@stericsson.com>
 * Author: Karl Komierowski <karl.komierowski@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>
#include <linux/wakelock.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/delay.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500.h>
#include <linux/time.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <mach/board-sec-u8500.h>
#include <mach/sec_param.h>
#include <linux/kernel.h>

/* fg_res parameter should be re-calculated
   according to the model and HW revision */

#define INS_CURR_TIMEOUT		(3 * HZ)

#if defined(CONFIG_MACH_JANICE)
#define FGRES_HWREV_02			133
#define FGRES_HWREV_02_CH		133
#define FGRES_HWREV_03			121
#define FGRES_HWREV_03_CH		120
#elif defined(CONFIG_MACH_CODINA)
#define USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
#define FGRES				130
#define FGRES_CH			125
#elif defined(CONFIG_MACH_GAVINI)
#define USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
#define FGRES				130
#define FGRES_CH			112
#else
#define FGRES				130
#define FGRES_CH			120
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
#define LOWBAT_ZERO_VOLTAGE		3320

#define MAIN_CH_NO_OVERSHOOT_ENA_N	0x02
#define MAIN_CH_ENA			0x01


#define MILLI_TO_MICRO			1000
#define FG_LSB_IN_MA			1627
#define QLSB_NANO_AMP_HOURS_X10		1129

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

#ifndef ABS
#define ABS(a) ((a) > 0 ? (a) : -(a))
#endif

#define interpolate(x, x1, y1, x2, y2) \
	((y1) + ((((y2) - (y1)) * ((x) - (x1))) / ((x2) - (x1))));

#define to_ab8500_fg_device_info(x) container_of((x), \
	struct ab8500_fg, fg_psy);

extern void (*sec_set_param_value) (int idx, void *value);
extern void (*sec_get_param_value) (int idx, void *value);
extern int register_reboot_notifier(struct notifier_block *nb);

extern bool vbus_state;

extern unsigned int system_rev;
/* This list came from ab8500_chargalg.c */
static char *states[] = {
	"HANDHELD_INIT",
	"HANDHELD",
	"CHG_NOT_OK_INIT",
	"CHG_NOT_OK",
	"HW_TEMP_PROTECT_INIT",
	"HW_TEMP_PROTECT",
	"NORMAL_INIT",
	"NORMAL",
	"WAIT_FOR_RECHARGE_INIT",
	"WAIT_FOR_RECHARGE",
	"MAINTENANCE_A_INIT",
	"MAINTENANCE_A",
	"MAINTENANCE_B_INIT",
	"MAINTENANCE_B",
	"TEMP_UNDEROVER_INIT",
	"TEMP_UNDEROVER",
	"TEMP_LOWHIGH_INIT",
	"TEMP_LOWHIGH",
	"SUSPENDED_INIT",
	"SUSPENDED",
	"OVV_PROTECT_INIT",
	"OVV_PROTECT",
	"SAFETY_TIMER_EXPIRED_INIT",
	"SAFETY_TIMER_EXPIRED",
	"BATT_REMOVED_INIT",
	"BATT_REMOVED",
	"WD_EXPIRED_INIT",
	"WD_EXPIRED",
	"CHARGE_TIMEOUT_INIT",
	"CHARGE_TIMEOUT",
	"TIMED_OUT_CHARGING_INIT",
	"TIMED_OUT_CHARGING",
};

/**
 * struct ab8500_fg_interrupts - ab8500 fg interupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab8500_fg_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

enum ab8500_fg_discharge_state {
	AB8500_FG_DISCHARGE_INIT,
	AB8500_FG_DISCHARGE_INITMEASURING,
	AB8500_FG_DISCHARGE_INIT_RECOVERY,
	AB8500_FG_DISCHARGE_RECOVERY,
	AB8500_FG_DISCHARGE_READOUT,
	AB8500_FG_DISCHARGE_WAKEUP,
};

static char *discharge_state[] = {
	"DISCHARGE_INIT",
	"DISCHARGE_INITMEASURING",
	"DISCHARGE_INIT_RECOVERY",
	"DISCHARGE_RECOVERY",
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
};

struct ab8500_fg_flags {
	bool fg_enabled;
	bool conv_done;
	bool charging;
	bool fully_charged;
	bool fully_charged_1st;
	bool chg_timed_out;
	bool low_bat_delay;
	bool low_bat;
	bool batt_unknown;
	bool calibrate;
};

enum cal_channels {
	ADC_INPUT_VMAIN = 0,
	ADC_INPUT_BTEMP,
	ADC_INPUT_VBAT,
	NBR_CAL_INPUTS,
};

struct adc_cal_data {
	u64 gain;
	u64 offset;
};

struct ab8500_gpadc {
	u8 chip_id;
	struct device *dev;
	struct list_head node;
	struct completion ab8500_gpadc_complete;
	struct mutex ab8500_gpadc_lock;
	struct regulator *regu;
	int irq;
	struct adc_cal_data cal_data[NBR_CAL_INPUTS];
	/*int regulator_enabled;*/
	int reference_count ;
	spinlock_t reference_count_spinlock ;
};

/**
 * struct ab8500_fg - ab8500 FG device information
 * @dev:		Pointer to the structure device
 * @vbat:		Battery voltage in mV
 * @vbat_adc:		Battery voltage in ADC
 * @vbat_nom:		Nominal battery voltage in mV
 * @inst_curr:		Instantenous battery current in mA
 * @avg_curr:		Average battery current in mA
 * @fg_samples:		Number of samples used in the FG accumulation
 * @accu_charge:	Accumulated charge from the last conversion
 * @recovery_cnt:	Counter for recovery mode
 * @high_curr_cnt:	Counter for high current mode
 * @init_cnt:		Counter for init mode
 * @recovery_needed:	Indicate if recovery is needed
 * @high_curr_mode:	Indicate if we're in high current mode
 * @vbat_cal_offset:	voltage offset  for calibarating vbat
 * @init_capacity:	Indicate if initial capacity measuring should be done
 * @calib_state		State during offset calibration
 * @discharge_state:	Current discharge state
 * @charge_state:	Current charge state
 * @node:		Struct of type list_head
 * @flags:		Structure for information about events triggered
 * @bat_cap:		Structure for battery capacity specific parameters
 * @avg_cap:		Average capacity filter
 * @parent:		Pointer to the struct ab8500
 * @gpadc:		Pointer to the struct gpadc
 * @pdata:		Pointer to the ab8500_fg platform data
 * @bat:		Pointer to the ab8500_bm platform data
 * @fg_psy:		Structure that holds the FG specific battery properties
 * @fg_wq:		Work queue for running the FG algorithm
 * @fg_periodic_work:	Work to run the FG algorithm periodically
 * @fg_low_bat_work:	Work to check low bat condition
 * @fg_work:		Work to run the FG algorithm instantly
 * @fg_reinit_work	Work used to reset and reinitialise the FG algorithm
 * @fg_acc_cur_work:	Work to read the FG accumulator
 * @cc_lock:		Mutex for locking the CC
 * @lowbat_wake_lock	Wakelock for low battery
 * @cc_wake_lock	Wakelock for Coulomb Counter
 */
struct ab8500_fg {
	struct device *dev;
	int vbat;
	int irq;
	int vbat_adc;
	int vbat_adc_compensated;
	int vbat_cal_offset;
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
	int new_capacity;
	int param_capacity;
	int prev_capacity;
	int gpadc_vbat_gain;
	int gpadc_vbat_offset;
	int gpadc_vbat_ideal;
	int smd_on;
	int reenable_charing;
	int fg_res_dischg;
	int fg_res_chg;
	int input_curr_reg;
	bool turn_off_fg;
	bool initial_capacity_calib;
	bool recovery_needed;
	bool high_curr_mode;
	bool init_capacity;
	bool reinit_capacity;
	bool lpm_chg_mode;
	bool lowbat_poweroff;
	bool lowbat_poweroff_locked;
	bool max_cap_changed;
	enum ab8500_fg_calibration_state calib_state;
	enum ab8500_fg_discharge_state discharge_state;
	enum ab8500_fg_charge_state charge_state;
	struct list_head node;
	struct completion ab8500_fg_complete;
	struct ab8500_fg_flags flags;
	struct ab8500_fg_battery_capacity bat_cap;
	struct ab8500_fg_vbased_cap vbat_cap;
	struct ab8500_fg_instant_current inst_cur;
	struct ab8500_fg_avg_cap avg_cap;
	struct ab8500 *parent;
	struct ab8500_gpadc *gpadc;
	struct ab8500_fg_platform_data *pdata;
	struct ab8500_bm_data *bat;
	struct power_supply fg_psy;
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
	struct wake_lock cc_wake_lock;
};
static LIST_HEAD(ab8500_fg_list);

/**
 * ab8500_fg_get() - returns a reference to the primary AB8500 fuel gauge
 * (i.e. the first fuel gauge in the instance list)
 */
struct ab8500_fg *ab8500_fg_get(void)
{
	struct ab8500_fg *fg;

	if (list_empty(&ab8500_fg_list))
		return NULL;

	fg = list_first_entry(&ab8500_fg_list, struct ab8500_fg, node);
	return fg;
}

/* Main battery properties */
static enum power_supply_property ab8500_fg_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
};

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

static u8 ab8500_volt_to_regval(int voltage)
{
	int i;

	if (voltage < ab8500_fg_lowbat_voltage_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_fg_lowbat_voltage_map); i++) {
		if (voltage < ab8500_fg_lowbat_voltage_map[i])
			return (u8) i - 1;
	}

	/* If not captured above, return index of last element */
	return (u8) ARRAY_SIZE(ab8500_fg_lowbat_voltage_map) - 1;
}

/**
 * ab8500_fg_is_low_curr() - Low or high current mode
 * @di:		pointer to the ab8500_fg structure
 * @curr:	the current to base or our decision on
 *
 * Low current mode if the current consumption is below a certain threshold
 */
static int ab8500_fg_is_low_curr(struct ab8500_fg *di, int curr)
{
	/*
	 * We want to know if we're in low current mode
	 */
	if (curr > -di->bat->fg_params->high_curr_threshold)
		return true;
	else
		return false;
}

/* Voltage based capacity */
static void ab8500_fg_fill_vcap_sample(struct ab8500_fg *di, int sample)
{
	int i;
	struct timespec ts;
	struct ab8500_fg_vbased_cap *avg = &di->vbat_cap;

	getnstimeofday(&ts);

	for (i = 0; i < NBR_AVG_SAMPLES; i++) {
		avg->samples[i] = sample;
		avg->time_stamps[i] = ts.tv_sec;
	}
	dev_dbg(di->dev, "Filled vcap: %d\n", sample);
	avg->pos = 0;
	avg->nbr_samples = NBR_AVG_SAMPLES;
	avg->sum = sample * NBR_AVG_SAMPLES;
	avg->avg = sample;
}
static int ab8500_fg_add_vcap_sample(struct ab8500_fg *di, int sample)
{
	struct timespec ts;
	struct ab8500_fg_vbased_cap *avg = &di->vbat_cap;

	getnstimeofday(&ts);

	do {
		avg->sum += sample - avg->samples[avg->pos];
		avg->samples[avg->pos] = sample;
		avg->time_stamps[avg->pos] = ts.tv_sec;
		avg->pos++;

		if (avg->pos == NBR_AVG_SAMPLES)
			avg->pos = 0;

		if (avg->nbr_samples < NBR_AVG_SAMPLES)
			avg->nbr_samples++;

		/*
		 * Check the time stamp for each sample. If too old,
		 * replace with latest sample
		 */
	} while (ts.tv_sec - VALID_CAPACITY_SEC > avg->time_stamps[avg->pos]);
	dev_dbg(di->dev, "Added vcap: %d, average: %d, nsamples: %d\n",
		sample, avg->avg, avg->nbr_samples);

	avg->avg = avg->sum / avg->nbr_samples;

	return avg->avg;
}

static void ab8500_fg_fill_i_sample(struct ab8500_fg *di, int sample)
{
	int i;
	struct timespec ts;
	struct ab8500_fg_instant_current *avg = &di->inst_cur;
	getnstimeofday(&ts);

	for (i = 0; i < NBR_CURR_SAMPLES; i++) {
		avg->samples[i] = 0;
		avg->time_stamps[i] = ts.tv_sec;
	}
	dev_dbg(di->dev, "Filled curr sampe: 0, pre_sample : %d\n", sample);
	avg->pos = 0;
	avg->nbr_samples = 1;
	avg->sum = 0;
	avg->avg = 0;
	avg->pre_sample = sample;
}

static void ab8500_fg_add_i_sample(struct ab8500_fg *di, int sample)
{
	int diff, i;
	struct timespec ts;
	struct ab8500_fg_instant_current *avg = &di->inst_cur;

	getnstimeofday(&ts);

	do {
		if (sample > avg->pre_sample)
			diff = sample - avg->pre_sample;
		else
			diff = avg->pre_sample - sample;
		avg->sum += diff;
		avg->samples[avg->pos] = diff;
		avg->time_stamps[avg->pos] = ts.tv_sec;
		avg->pos++;
		avg->pre_sample = sample;

		if (avg->pos == NBR_CURR_SAMPLES)
			avg->pos = 0;

		if (avg->nbr_samples < NBR_CURR_SAMPLES)
			avg->nbr_samples++;

		/*
		 * Check the time stamp for each sample. If too old,
		 * replace with latest sample
		 */
	} while (ts.tv_sec - VALID_CAPACITY_SEC > avg->time_stamps[avg->pos]);

	avg->high_diff_nbr = 0;
	for (i = 0; i < NBR_CURR_SAMPLES; i++) {
		if (avg->samples[i] > CURR_VAR_HIGH)
			avg->high_diff_nbr++;
	}

	dev_dbg(di->dev,
		"Added curr sample: %d, diff:%d, average: "
		"%d, high_diff_nbr: %d\n",
		sample, diff, avg->avg, avg->high_diff_nbr);

	avg->avg = avg->sum / avg->nbr_samples;

}

/* Voltage based capacity END */
/**
 * ab8500_fg_add_cap_sample() - Add capacity to average filter
 * @di:		pointer to the ab8500_fg structure
 * @sample:	the capacity in mAh to add to the filter
 *
 * A capacity is added to the filter and a new mean capacity is calculated and
 * returned
 */
static int ab8500_fg_add_cap_sample(struct ab8500_fg *di, int sample)
{
	struct timespec ts;
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

	getnstimeofday(&ts);

	do {
		avg->sum += sample - avg->samples[avg->pos];
		avg->samples[avg->pos] = sample;
		avg->time_stamps[avg->pos] = ts.tv_sec;
		avg->pos++;

		if (avg->pos == NBR_AVG_SAMPLES)
			avg->pos = 0;

		if (avg->nbr_samples < NBR_AVG_SAMPLES)
			avg->nbr_samples++;

		/*
		 * Check the time stamp for each sample. If too old,
		 * replace with latest sample
		 */
	} while (ts.tv_sec - VALID_CAPACITY_SEC > avg->time_stamps[avg->pos]);

	avg->avg = avg->sum / avg->nbr_samples;

	return avg->avg;
}

/**
 * ab8500_fg_clear_cap_samples() - Clear average filter
 * @di:        pointer to the ab8500_fg structure
 *
 * The capacity filter is filled with a capacity in mAh
 */
static void ab8500_fg_clear_cap_samples(struct ab8500_fg *di)
{
	int i;
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

	for (i = 0; i < NBR_AVG_SAMPLES; i++) {
		avg->samples[i] = 0;
		avg->time_stamps[i] = 0;
		avg->pos = 0;
		avg->nbr_samples = 0;
		avg->sum = 0;
		avg->avg = 0;
	}

}

/**
 * ab8500_fg_fill_cap_sample() - Fill average filter
 * @di:		pointer to the ab8500_fg structure
 * @sample:	the capacity in mAh to fill the filter with
 *
 * The capacity filter is filled with a capacity in mAh
 */
static void ab8500_fg_fill_cap_sample(struct ab8500_fg *di, int sample)
{
	int i;
	struct timespec ts;
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

	getnstimeofday(&ts);

	for (i = 0; i < NBR_AVG_SAMPLES; i++) {
		avg->samples[i] = sample;
		avg->time_stamps[i] = ts.tv_sec;
	}

	avg->pos = 0;
	avg->nbr_samples = NBR_AVG_SAMPLES;
	avg->sum = sample * NBR_AVG_SAMPLES;
	avg->avg = sample;
}

/**
 * ab8500_fg_coulomb_counter() - enable coulomb counter
 * @di:		pointer to the ab8500_fg structure
 * @enable:	enable/disable
 *
 * Enable/Disable coulomb counter.
 * On failure returns negative value.
 */
static int ab8500_fg_coulomb_counter(struct ab8500_fg *di, bool enable)
{
	int ret = 0;
	mutex_lock(&di->cc_lock);
	if (enable) {
		/* To be able to reprogram the number of samples, we have to
		 * first stop the CC and then enable it again */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG, 0x00);
		if (ret)
			goto cc_err;

		/* Program the samples */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_NCOV_ACCU,
			di->fg_samples);
		if (ret)
			goto cc_err;

		/* Start the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG,
			(CC_DEEP_SLEEP_ENA | CC_PWR_UP_ENA));
		if (ret)
			goto cc_err;

		di->flags.fg_enabled = true;
	} else {
		/* Clear any pending read requests */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG, 0);
		if (ret)
			goto cc_err;

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_NCOV_ACCU_CTRL, 0);
		if (ret)
			goto cc_err;

		/* Stop the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG, 0);
		if (ret)
			goto cc_err;

		di->flags.fg_enabled = false;

	}
	dev_dbg(di->dev, " CC enabled: %d Samples: %d\n",
		enable, di->fg_samples);

	mutex_unlock(&di->cc_lock);

	return ret;
cc_err:
	dev_err(di->dev, "%s Enabling coulomb counter failed\n", __func__);
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr_start() - start battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 0 or error code
 * Note: This is part "one" and has to be called before
 * ab8500_fg_inst_curr_finalize()
 */
int ab8500_fg_inst_curr_start(struct ab8500_fg *di)
{
	u8 reg_val;
	int ret;

	mutex_lock(&di->cc_lock);
	dev_dbg(di->dev, "Inst curr start\n");
	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8500_RTC_CC_CONF_REG, &reg_val);
	if (ret < 0)
		goto fail;

	if (!(reg_val & CC_PWR_UP_ENA)) {
		dev_dbg(di->dev, "%s Enable FG\n", __func__);
		di->turn_off_fg = true;

		/* Program the samples */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_NCOV_ACCU,
			SEC_TO_SAMPLE(10));
		if (ret)
			goto fail;

		/* Start the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG,
			(CC_DEEP_SLEEP_ENA | CC_PWR_UP_ENA));
		if (ret)
			goto fail;
	} else {
		di->turn_off_fg = false;
	}

	/* Return and WFI */
	INIT_COMPLETION(di->ab8500_fg_complete);
	enable_irq(di->irq);

	/* Note: cc_lock is still locked */
	return 0;
fail:
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr_done() - check if fg conversion is done
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 1 if conversion done, 0 if still waiting
 */
int ab8500_fg_inst_curr_done(struct ab8500_fg *di)
{
	return completion_done(&di->ab8500_fg_complete);
}

/**
 * ab8500_fg_inst_curr_finalize() - battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 * @res:	battery instantenous current(on success)
 *
 * Returns 0 or an error code
 * Note: This is part "two" and has to be called at earliest 250 ms
 * after ab8500_fg_inst_curr_start()
 */
int ab8500_fg_inst_curr_finalize(struct ab8500_fg *di, int *res)
{
	u8 low, high;
	int val;
	int ret;
	int timeout;

	if (!completion_done(&di->ab8500_fg_complete)) {
		timeout = wait_for_completion_timeout(&di->ab8500_fg_complete,
			INS_CURR_TIMEOUT);
		dev_dbg(di->dev, "Finalize time: %d ms\n",
			((INS_CURR_TIMEOUT - timeout) * 1000) / HZ);
		if (!timeout) {
			ret = -ETIME;
			disable_irq(di->irq);
			dev_err(di->dev, "completion timed out [%d]\n",
				__LINE__);
			goto fail;
		}
	}

	disable_irq(di->irq);

	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			READ_REQ, READ_REQ);

	/* 100uS between read request and read is needed */
	udelay(100);

	/* Read CC Sample conversion value Low and high */
	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_SMPL_CNVL_REG,  &low);
	if (ret < 0)
		goto fail;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_SMPL_CNVH_REG,  &high);
	if (ret < 0)
		goto fail;

	/*
	 * negative value for Discharging
	 * convert 2's compliment into decimal
	 */
	if (high & 0x10)
		val = (low | (high << 8) | 0xFFFFE000);
	else
		val = (low | (high << 8));

	/*
	 * Convert to unit value in mA
	 * Full scale input voltage is
	 * 66.660mV => LSB = 66.660mV/(4096*res) = 1.627mA
	 * Given a 250ms conversion cycle time the LSB corresponds
	 * to 112.9 nAh. Convert to current by dividing by the conversion
	 * time in hours (250ms = 1 / (3600 * 4)h)
	 * 112.9nAh assumes 10mOhm, but fg_res is in 0.1mOhm
	 */
	val = (val * QLSB_NANO_AMP_HOURS_X10 * 36 * 4) /
		(1000 * di->bat->fg_res);

	if (di->turn_off_fg) {
		dev_dbg(di->dev, "%s Disable FG\n", __func__);

		/* Clear any pending read requests */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG, 0);
		if (ret)
			goto fail;

		/* Stop the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG, 0);
		if (ret)
			goto fail;
	}
	mutex_unlock(&di->cc_lock);
	(*res) = val;

	return 0;
fail:
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr() - battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns battery instantenous current(on success) else error code
 */
int ab8500_fg_inst_curr(struct ab8500_fg *di)
{
	int ret;
	int res = 0;
	dev_dbg(di->dev, "Inst curr blocking\n");
	ret = ab8500_fg_inst_curr_start(di);
	if (ret) {
		dev_err(di->dev, "Failed to initialize fg_inst\n");
		return 0;
	}

	ret = ab8500_fg_inst_curr_finalize(di, &res);
	dev_dbg(di->dev, "Inst curr blocking read %d\n", res);
	if (ret) {
		dev_err(di->dev, "Failed to finalize fg_inst\n");
		return 0;
	}

	return res;
}

/**
 * ab8500_fg_acc_cur_work() - average battery current
 * @work:	pointer to the work_struct structure
 *
 * Updated the average battery current obtained from the
 * coulomb counter.
 */
static void ab8500_fg_acc_cur_work(struct work_struct *work)
{
	int val;
	int ret;
	u8 low, med, high;

	struct ab8500_fg *di = container_of(work,
		struct ab8500_fg, fg_acc_cur_work);

	mutex_lock(&di->cc_lock);
	ret = abx500_set_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_CTRL, RD_NCONV_ACCU_REQ);
	if (ret)
		goto exit;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_LOW,  &low);
	if (ret < 0)
		goto exit;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_MED,  &med);
	if (ret < 0)
		goto exit;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_HIGH, &high);
	if (ret < 0)
		goto exit;

	/* Check for sign bit in case of negative value, 2's compliment */
	if (high & 0x10)
		val = (low | (med << 8) | (high << 16) | 0xFFE00000);
	else
		val = (low | (med << 8) | (high << 16));

	/*
	 * Convert to uAh
	 * Given a 250ms conversion cycle time the LSB corresponds
	 * to 112.9 nAh.
	 * 112.9nAh assumes 10mOhm, but fg_res is in 0.1mOhm
	 */
	di->accu_charge = (val * QLSB_NANO_AMP_HOURS_X10) /
		(100 * di->bat->fg_res);

	/*
	 * Convert to unit value in mA
	 * Full scale input voltage is
	 * 66.660mV => LSB = 66.660mV/(4096*res) = 1.627mA
	 * Given a 250ms conversion cycle time the LSB corresponds
	 * to 112.9 nAh. Convert to current by dividing by the conversion
	 * time in hours (= samples / (3600 * 4)h)
	 * 112.9nAh assumes 10mOhm, but fg_res is in 0.1mOhm
	 */
	di->avg_curr = (val * QLSB_NANO_AMP_HOURS_X10 * 36) /
		(1000 * di->bat->fg_res * (di->fg_samples / 4));

	di->flags.conv_done = true;

	mutex_unlock(&di->cc_lock);

	queue_work(di->fg_wq, &di->fg_work);

	return;
exit:
	dev_err(di->dev,
		"Failed to read or write gas gauge registers\n");
	mutex_unlock(&di->cc_lock);
	queue_work(di->fg_wq,
		&di->fg_work);
}

/**
 * ab8500_fg_bat_voltage() - get battery voltage
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery voltage(on success) else error code
 */
static int ab8500_fg_bat_voltage(struct ab8500_fg *di,
	bool fg_only)
{
	int vbat;
	int ad_value;
	int raw_adc;

	static int prev;

	ad_value = ab8500_gpadc_read_raw(di->gpadc, MAIN_BAT_V, SAMPLE_16, RISING_EDGE, 0, ADC_SW);
	if (ad_value < 0) {
		dev_err(di->dev, "GPADC raw value failed ch: %d\n", MAIN_BAT_V);
		return prev;
	}

	raw_adc = ad_value;
	/* FG algotithm is compensating this offset.
	   so removing for the duplicate work. */
	if (!fg_only)
		ad_value += di->vbat_cal_offset;

	vbat = ab8500_gpadc_ad_to_voltage(di->gpadc, MAIN_BAT_V, ad_value);
	if (vbat < 0) {
		dev_err(di->dev, "GPADC to voltage conversion failed ch:"
			" %d ADC: 0x%x\n", MAIN_BAT_V, ad_value);
		return prev;
	}

	di->vbat_adc = raw_adc;
	di->vbat_adc_compensated = ad_value;

#ifdef CONFIG_MACH_JANICE
	if (di->smd_on)
		vbat += 150;
#endif
	prev = vbat;
	return vbat;
}

/**
 * ab8500_fg_volt_to_resistance() - get battery resistance depending on voltage
 * @di:		pointer to the ab8500_fg structure
 * @voltage:	The voltage to convert to a capacity
 *
 * Returns battery resistance based on voltage
 */
static int ab8500_fg_volt_to_resistance(struct ab8500_fg *di, int voltage)
{
	int i, tbl_size;
	struct v_to_res *tbl;
	int res = 0;

#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
	if (di->flags.charging) {
		tbl = di->bat->bat_type[di->bat->batt_id].v_to_chg_res_tbl,
		tbl_size = di->bat->bat_type[di->bat->batt_id].
			n_v_chg_res_tbl_elements;
	} else {
#endif
		tbl = di->bat->bat_type[di->bat->batt_id].v_to_res_tbl,
		tbl_size = di->bat->bat_type[di->bat->batt_id].
			n_v_res_tbl_elements;
#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
	}
#endif

	for (i = 0; i < tbl_size; ++i) {
		if (voltage > tbl[i].voltage)
			break;
	}

	if ((i > 0) && (i < tbl_size)) {
		res = interpolate(voltage,
			tbl[i].voltage,
			tbl[i].resistance,
			tbl[i-1].voltage,
			tbl[i-1].resistance);
		res += di->bat->bat_type[di->bat->batt_id].
			line_impedance; /* adding line impedance */
	} else if (i >= tbl_size) {
		res = tbl[tbl_size-1].resistance +
			di->bat->bat_type[di->bat->batt_id].line_impedance;
	} else {
#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
		if (di->flags.charging) {
			res = di->bat->bat_type[di->bat->batt_id].
				battery_resistance_for_charging +
				di->bat->bat_type[di->bat->batt_id].
				line_impedance;
		} else {
#endif
			res = di->bat->bat_type[di->bat->batt_id].
				battery_resistance +
				di->bat->bat_type[di->bat->batt_id].
				line_impedance;
#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
		}
#endif
	}

	dev_dbg(di->dev, "[NEW BATT RES]%s Vbat: %d, Res: %d mohm",
		__func__, voltage, res);

	return res;
}

/**
 * ab8500_comp_fg_bat_voltage() - get battery voltage
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns compensated battery voltage(on success) else error code
 */
static int ab8500_comp_fg_bat_voltage(struct ab8500_fg *di,
		bool always)
{
	int vbat_comp;
	int i = 0;
	int vbat = 0;
	int bat_res_comp = 0;

	ab8500_fg_inst_curr_start(di);

	do {
		vbat += ab8500_fg_bat_voltage(di, true);
		i++;
		dev_dbg(di->dev, "LoadComp Vbat avg [%d] %d\n", i, vbat/i);
		msleep(5);
	} while (!ab8500_fg_inst_curr_done(di) &&
		i <= WAIT_FOR_INST_CURRENT_MAX);

	if (i > WAIT_FOR_INST_CURRENT_MAX) {
		dev_info(di->dev, "Inst curr reading took too long, %d times\n",
			i);
		dev_dbg(di->dev, "returned uncompensated vbat\n");
		if (!always)
			return -1;
		di->vbat = vbat / i;
		return di->vbat;
	}

	ab8500_fg_inst_curr_finalize(di, &di->inst_curr);

	if (!always && di->inst_curr < IGNORE_VBAT_HIGHCUR)
		return -1;

	if (!di->flags.charging)
		ab8500_fg_add_i_sample(di, di->inst_curr);

	di->vbat = vbat / i;

#if defined(CONFIG_MACH_JANICE) || \
	defined(CONFIG_MACH_CODINA) || \
	defined(CONFIG_MACH_GAVINI)
	bat_res_comp = ab8500_fg_volt_to_resistance(di, di->vbat);
#else
	bat_res_comp = di->bat->bat_type[di->bat->batt_id].
			battery_resistance +
			di->bat->bat_type[di->bat->batt_id].
			line_impedance;
#endif
	/* Use Ohms law to get the load compensated voltage */
	vbat_comp = di->vbat - (di->inst_curr * bat_res_comp) / 1000;

	dev_dbg(di->dev, "%s Measured Vbat: %dmV,Compensated Vbat %dmV, "
		"R: %dmOhm, Current: %dmA Vbat Samples: %d\n",
		__func__, di->vbat, vbat_comp,
		bat_res_comp,
		di->inst_curr, i);
	dev_dbg(di->dev, "[TuningData]\t%d\t%d\t%d\t%d\t%d\n",
		DIV_ROUND_CLOSEST(di->bat_cap.permille, 10),
		di->vbat, vbat_comp,
		di->inst_curr, bat_res_comp);

	di->vbat = vbat_comp;
	return vbat_comp;
}

/**
 * ab8500_fg_volt_to_capacity() - Voltage based capacity
 * @di:		pointer to the ab8500_fg structure
 * @voltage:	The voltage to convert to a capacity
 *
 * Returns battery capacity in per mille based on voltage
 */
static int ab8500_fg_volt_to_capacity(struct ab8500_fg *di, int voltage)
{
	int i, tbl_size;
	struct v_to_cap *tbl;
	int cap = 0;

	tbl = di->bat->bat_type[di->bat->batt_id].v_to_cap_tbl,
	tbl_size = di->bat->bat_type[di->bat->batt_id].n_v_cap_tbl_elements;

	for (i = 0; i < tbl_size; ++i) {
		if (voltage > tbl[i].voltage)
			break;
	}

	if ((i > 0) && (i < tbl_size)) {
		cap = interpolate(voltage,
			tbl[i].voltage,
			tbl[i].capacity * 10,
			tbl[i-1].voltage,
			tbl[i-1].capacity * 10);
	} else if (i == 0) {
		cap = 1000;
	} else {
		cap = 0;
	}

	dev_dbg(di->dev, "%s Vbat: %d, Cap: %d per mille",
		__func__, voltage, cap);

	return cap;
}


/**
 * ab8500_fg_uncomp_volt_to_capacity() - Uncompensated voltage based capacity
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery capacity based on battery voltage that is not compensated
 * for the voltage drop due to the load
 */
static int ab8500_fg_uncomp_volt_to_capacity(struct ab8500_fg *di)
{
	di->vbat = ab8500_fg_bat_voltage(di, false);
	return ab8500_fg_volt_to_capacity(di, di->vbat);
}

/**
 * ab8500_fg_load_comp_volt_to_capacity() - Load compensated voltage based capacity
 * @di:		pointer to the ab8500_fg structure
 * @always:	always return the volt to cap no mather what
 *
 * Returns battery capacity based on battery voltage that is load compensated
 * for the voltage drop
 */
static int ab8500_fg_load_comp_volt_to_capacity(struct ab8500_fg *di,
		bool always)
{
	int vbat_ret = 0;

	vbat_ret = ab8500_comp_fg_bat_voltage(di, always);
	if (vbat_ret == -1)
		return -1;

	return ab8500_fg_volt_to_capacity(di, vbat_ret);
}

/**
 * ab8500_fg_convert_mah_to_permille() - Capacity in mAh to permille
 * @di:		pointer to the ab8500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in permille
 */
static int ab8500_fg_convert_mah_to_permille(struct ab8500_fg *di, int cap_mah)
{
	return (cap_mah * 1000) / di->bat_cap.max_mah_design;
}

/**
 * ab8500_fg_convert_permille_to_mah() - Capacity in permille to mAh
 * @di:		pointer to the ab8500_fg structure
 * @cap_pm:	capacity in permille
 *
 * Converts capacity in permille to capacity in mAh
 */
static int ab8500_fg_convert_permille_to_mah(struct ab8500_fg *di, int cap_pm)
{
	return cap_pm * di->bat_cap.max_mah_design / 1000;
}

/**
 * ab8500_fg_convert_mah_to_uwh() - Capacity in mAh to uWh
 * @di:		pointer to the ab8500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in uWh
 */
static int ab8500_fg_convert_mah_to_uwh(struct ab8500_fg *di, int cap_mah)
{
	u64 div_res;
	u32 div_rem;

	div_res = ((u64) cap_mah) * ((u64) di->vbat_nom);
	div_rem = do_div(div_res, 1000);

	/* Make sure to round upwards if necessary */
	if (div_rem >= 1000 / 2)
		div_res++;

	return (int) div_res;
}

/**
 * ab8500_fg_calc_cap_charging() - Calculate remaining capacity while charging
 * @di:		pointer to the ab8500_fg structure
 *
 * Return the capacity in mAh based on previous calculated capcity and the FG
 * accumulator register value. The filter is filled with this capacity
 */
static int ab8500_fg_calc_cap_charging(struct ab8500_fg *di)
{
	dev_dbg(di->dev, "%s cap_mah %d accu_charge %d\n",
		__func__,
		di->bat_cap.mah,
		di->accu_charge);

	/* Capacity should not be less than 0 */
	if (di->bat_cap.mah + di->accu_charge > 0)
		di->bat_cap.mah += di->accu_charge;
	else
		di->bat_cap.mah = 0;

	/*
	 * We force capacity to 100% as long as the algorithm
	 * reports that it's full.
	*/
	if (di->bat_cap.mah >= di->bat_cap.max_mah_design ||
	    (!di->flags.chg_timed_out &&
	     (di->flags.fully_charged || di->flags.fully_charged_1st))) {
		di->bat_cap.mah = di->bat_cap.max_mah_design;
		di->max_cap_changed = true;
	} else {
		di->max_cap_changed = false;
	}

	ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
	if (di->bat_cap.permille > 900) {
		di->n_skip_add_sample = 4;
	} else if (di->bat_cap.permille <= 900 &&
		di->bat_cap.permille > 800) {
		di->n_skip_add_sample = 4;
	} else if (di->bat_cap.permille <= 800 &&
		di->bat_cap.permille > 250) {
		di->n_skip_add_sample = 7;
	} else if (di->bat_cap.permille <= 250 &&
		di->bat_cap.permille > 200) {
		di->n_skip_add_sample = 5;
	} else if (di->bat_cap.permille <= 200 &&
		di->bat_cap.permille > 100) {
		di->n_skip_add_sample = 3;
	} else if (di->bat_cap.permille <= 120) {
		di->n_skip_add_sample = 1;
	}
	dev_dbg(di->dev,
		"[CHARGING] Using every %d Vbat sample Now on %d loop\n",
		di->n_skip_add_sample, di->skip_add_sample);

	if (++di->skip_add_sample >= di->n_skip_add_sample) {
		dev_dbg(di->dev,
			"[CHARGING] Adding voltage based samples to avg: %d\n",
			di->vbat_cap.avg);
		di->bat_cap.mah = ab8500_fg_add_cap_sample(di,
			di->vbat_cap.avg);
		di->skip_add_sample = 0;
	}
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
#else
	ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);

	/* We need to update battery voltage and inst current when charging */
	di->vbat = ab8500_comp_fg_bat_voltage(di, true);
	di->inst_curr = ab8500_fg_inst_curr(di);
#endif /*USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING*/

	return di->bat_cap.mah;
}

/**
 * ab8500_fg_calc_cap_discharge_voltage() - Capacity in discharge with voltage
 * @di:		pointer to the ab8500_fg structure
 * @comp:	if voltage should be load compensated before capacity calc
 *
 * Return the capacity in mAh based on the battery voltage. The voltage can
 * either be load compensated or not. This value is added to the filter and a
 * new mean value is calculated and returned.
 */
static int ab8500_fg_calc_cap_discharge_voltage(struct ab8500_fg *di, bool comp)
{
	int permille, mah;

	if (comp)
		permille = ab8500_fg_load_comp_volt_to_capacity(di, true);
	else
		permille = ab8500_fg_uncomp_volt_to_capacity(di);

	mah = ab8500_fg_convert_permille_to_mah(di, permille);

	di->bat_cap.mah = ab8500_fg_add_cap_sample(di, mah);
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

	return di->bat_cap.mah;
}

/**
 * ab8500_fg_calc_cap_discharge_fg() - Capacity in discharge with FG
 * @di:		pointer to the ab8500_fg structure
 *
 * Return the capacity in mAh based on previous calculated capcity and the FG
 * accumulator register value. This value is added to the filter and a
 * new mean value is calculated and returned.
 */
static int ab8500_fg_calc_cap_discharge_fg(struct ab8500_fg *di)
{
	int permille, mah;
	int diff_cap = 0;

	dev_dbg(di->dev, "%s cap_mah %d accu_charge %d\n",
	__func__,
	di->bat_cap.mah,
	di->accu_charge);

	/* Capacity should not be less than 0 */
	if (di->bat_cap.mah + di->accu_charge > 0)
		di->bat_cap.mah += di->accu_charge;
	else
		di->bat_cap.mah = 0;

	if (di->bat_cap.mah >= di->bat_cap.max_mah_design)
		di->bat_cap.mah = di->bat_cap.max_mah_design;

	permille = ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
	ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);

	/*
	* Use voltage based capacity as a precaution, but now when
	* capacity is around 15% since the discharge curve is too
	* flat in this region. The slightest error will have big
	* impact on the estimated capacity, and we only use the
	* FG based capacity in this case
	*/
	if (di->bat_cap.permille > 900) {
		di->n_skip_add_sample = 4;
	} else if (di->bat_cap.permille <= 900 &&
		di->bat_cap.permille > 800) {
		di->n_skip_add_sample = 4;
	} else if (di->bat_cap.permille <= 800 &&
		di->bat_cap.permille > 250) {
		di->n_skip_add_sample = 7;
	} else if (di->bat_cap.permille <= 250 &&
		di->bat_cap.permille > 200) {
		di->n_skip_add_sample = 5;
	} else if (di->bat_cap.permille <= 200 &&
		di->bat_cap.permille > 100) {
		di->n_skip_add_sample = 3;
#if defined(CONFIG_MACH_CODINA)
	} else if (di->bat_cap.permille <= 120) {
		di->n_skip_add_sample = 1;
	}
#else
	} else if (di->bat_cap.permille <= 100) {
		di->n_skip_add_sample = 2;
	}
#endif
	dev_dbg(di->dev, "Using every %d Vbat sample Now on %d loop\n",
		di->n_skip_add_sample, di->skip_add_sample);

	if (++di->skip_add_sample >= di->n_skip_add_sample) {
		diff_cap = ab8500_fg_convert_mah_to_permille(di,
		     ABS(di->vbat_cap.avg - di->avg_cap.avg));
		dev_dbg(di->dev, "[CURR VAR] high_diff_nbr : %d\n",
			di->inst_cur.high_diff_nbr);

		dev_dbg(di->dev,
			"[DIFF CAP] FG_avg : %d, VB_avg : %d, diff_cap : %d\n",
			ab8500_fg_convert_mah_to_permille(di, di->avg_cap.avg),
			ab8500_fg_convert_mah_to_permille(di, di->vbat_cap.avg),
			DIV_ROUND_CLOSEST(diff_cap, 10));

			dev_dbg(di->dev,
				"Adding voltage based samples to avg: %d\n",
				di->vbat_cap.avg);
			di->bat_cap.mah = ab8500_fg_add_cap_sample(di,
				di->vbat_cap.avg);

		di->skip_add_sample = 0;
	}
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

	return di->bat_cap.mah;
}

/**
 * ab8500_fg_capacity_level() - Get the battery capacity level
 * @di:		pointer to the ab8500_fg structure
 *
 * Get the battery capacity level based on the capacity in percent
 */
static int ab8500_fg_capacity_level(struct ab8500_fg *di)
{
	int ret, percent;

	percent = DIV_ROUND_CLOSEST(di->bat_cap.permille, 10);

	if (percent <= di->bat->cap_levels->critical ||
		di->flags.low_bat)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (percent <= di->bat->cap_levels->low)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (percent <= di->bat->cap_levels->normal)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (percent <= di->bat->cap_levels->high)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else
		ret = POWER_SUPPLY_CAPACITY_LEVEL_FULL;

	return ret;
}

/**
 * ab8500_fg_check_capacity_limits() - Check if capacity has changed
 * @di:		pointer to the ab8500_fg structure
 * @init:	capacity is allowed to go up in init mode
 *
 * Check if capacity or capacity limit has changed and notify the system
 * about it using the power_supply framework
 */
static void ab8500_fg_check_capacity_limits(struct ab8500_fg *di, bool init)
{
	bool changed = false;
	int percent = DIV_ROUND_CLOSEST(di->bat_cap.permille, 10);

	di->bat_cap.level = ab8500_fg_capacity_level(di);

	if (di->bat_cap.level != di->bat_cap.prev_level) {
		/*
		 * We do not allow reported capacity level to go up
		 * unless we're charging or if we're in init
		 */
		if (!(!di->flags.charging && di->bat_cap.level >
			di->bat_cap.prev_level) || init) {
			dev_dbg(di->dev, "level changed from %d to %d\n",
				di->bat_cap.prev_level,
				di->bat_cap.level);
			di->bat_cap.prev_level = di->bat_cap.level;
			changed = true;
		} else {
			dev_dbg(di->dev, "level not allowed to go up "
				"since no charger is connected: %d to %d\n",
				di->bat_cap.prev_level,
				di->bat_cap.level);
		}
	}

	if (di->flags.low_bat) {
		if (!di->lpm_chg_mode) {
			if (percent <= 1  &&
			    di->vbat <= LOWBAT_ZERO_VOLTAGE && !changed) {
				di->lowbat_poweroff = true;
			} else if ((percent > 1 && !di->flags.charging) ||
				   (percent <= 1 &&
				    di->vbat > LOWBAT_ZERO_VOLTAGE)) {
				/* battery capacity will be getting low to 1%.
				   we're waiting for it */
				dev_info(di->dev,
				 "Low bat interrupt occured, "
				 "now waiting for power off condition\n");
				dev_info(di->dev,
					"Capacity : %d Voltage : %dmV\n",
					 percent, di->vbat);
				di->lowbat_poweroff = false;
				di->lowbat_poweroff_locked = true;
				wake_lock(&di->lowbat_poweroff_wake_lock);
			} else {
				/* battery capacity is exceed 1% and/or
				   charging is enabled */
				dev_info(di->dev, "Low bat interrupt occured, "
					 "But not in power off condition\n");
				dev_info(di->dev,
					 "Capacity : %d Voltage : %dmV\n",
					 percent, di->vbat);
				di->lowbat_poweroff = false;
				di->lowbat_poweroff_locked = false;
				wake_unlock(&di->lowbat_poweroff_wake_lock);
			}
		}
	}

	if (di->lowbat_poweroff_locked) {
		if (percent <= 1 && di->vbat <= LOWBAT_ZERO_VOLTAGE
		    && !changed)
			di->lowbat_poweroff = true;

		if (di->vbat > 3450) {
			dev_info(di->dev, "Low bat condition is recovered.\n");
			di->lowbat_poweroff_locked = false;
			wake_unlock(&di->lowbat_poweroff_wake_lock);
		}
	}

	if ((percent == 0 && di->vbat <= LOWBAT_ZERO_VOLTAGE) ||
		(percent <= 1 && di->vbat <= LOWBAT_ZERO_VOLTAGE && !changed))
		di->lowbat_poweroff = true;

	if (di->lowbat_poweroff && di->lpm_chg_mode) {
		di->lowbat_poweroff = false;
		dev_info(di->dev, "Low bat interrupt occured, "
			 "but we will ignore it in lpm\n");
	}

	/*
	 * If we have received the LOW_BAT IRQ, set capacity to 0 to initiate
	 * shutdown
	 */
	if (di->lowbat_poweroff) {
		dev_info(di->dev, "Battery low, set capacity to 0\n");
		di->bat_cap.prev_percent = 0;
		di->bat_cap.permille = 0;
		di->bat_cap.prev_mah = 0;
		di->bat_cap.mah = 0;
		changed = true;
	} else if (di->bat_cap.prev_percent !=
			percent) {
		if (percent == 0) {
			/*
			 * We will not report 0% unless we've got
			 * the LOW_BAT IRQ, no matter what the FG
			 * algorithm says.
			 */
			di->bat_cap.prev_percent = 1;
			di->bat_cap.prev_mah = 1;
			/* di->bat_cap.permille = 1; */
			/* di->bat_cap.mah = 1; */

			changed = true;
		} else if (!(!di->flags.charging &&
					percent > di->bat_cap.prev_percent) ||
				init) {
			/*
			 * We do not allow reported capacity to go up
			 * unless we're charging or if we're in init
			 */
			dev_dbg(di->dev,
				"capacity changed from %d to %d (%d)\n",
				di->bat_cap.prev_percent,
				percent, di->bat_cap.permille);
			di->bat_cap.prev_percent = percent;
			di->bat_cap.prev_mah = di->bat_cap.mah;

			changed = true;
		} else {
			dev_dbg(di->dev, "capacity not allowed to go up since "
				"no charger is connected: %d to %d (%d)\n",
				di->bat_cap.prev_percent, percent,
				di->bat_cap.permille);
		}
	}

	if (changed)
		power_supply_changed(&di->fg_psy);

}

static void ab8500_fg_charge_state_to(struct ab8500_fg *di,
	enum ab8500_fg_charge_state new_state)
{
	dev_dbg(di->dev, "Charge state from %d [%s] to %d [%s]\n",
		di->charge_state,
		charge_state[di->charge_state],
		new_state,
		charge_state[new_state]);

	di->charge_state = new_state;
}

static void ab8500_fg_discharge_state_to(struct ab8500_fg *di,
	enum ab8500_fg_charge_state new_state)
{
	dev_dbg(di->dev, "Disharge state from %d [%s] to %d [%s]\n",
		di->discharge_state,
		discharge_state[di->discharge_state],
		new_state,
		discharge_state[new_state]);

	di->discharge_state = new_state;
}

/**
 * ab8500_fg_algorithm_charging() - FG algorithm for when charging
 * @di:		pointer to the ab8500_fg structure
 *
 * Battery capacity calculation state machine for when we're charging
 */
static void ab8500_fg_algorithm_charging(struct ab8500_fg *di)
{
	/*
	 * If we change to discharge mode
	 * we should start with recovery
	 */
	if (di->discharge_state != AB8500_FG_DISCHARGE_INIT_RECOVERY)
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_INIT_RECOVERY);

	switch (di->charge_state) {
	case AB8500_FG_CHARGE_INIT:
		di->fg_samples = SEC_TO_SAMPLE(
			di->bat->fg_params->accu_charging);

		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_READOUT);

		break;
	case AB8500_FG_CHARGE_READOUT:
		/*
		 * Read the FG and calculate the new capacity
		 */

#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
		{
			int mah, vbat_cap;
			vbat_cap = ab8500_fg_load_comp_volt_to_capacity(di,
								false);
			if (vbat_cap != -1) {
				mah = ab8500_fg_convert_permille_to_mah(di,
							vbat_cap);
				ab8500_fg_add_vcap_sample(di, mah);
				dev_dbg(di->dev,
				"[CHARGING]Average voltage based capacity is: "
				"%d mah Now %d mah, [%d %%]\n",
				di->vbat_cap.avg, mah, vbat_cap / 10);
			} else
				dev_dbg(di->dev,
				"[CHARGING]Ignoring average "
				"voltage based capacity\n");
		}
#endif
		/*
		 * We force capacity to 100% as long as the algorithm
		 * reports that it's full.
		 */
		if (di->bat_cap.mah >= di->bat_cap.max_mah_design ||
		    (!di->flags.chg_timed_out &&
		     (di->flags.fully_charged ||
		      di->flags.fully_charged_1st))) {
			di->bat_cap.mah = di->bat_cap.max_mah_design;
			di->max_cap_changed = true;

			ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);
			di->bat_cap.permille =
				ab8500_fg_convert_mah_to_permille(di,
						  di->bat_cap.mah);
			ab8500_fg_fill_vcap_sample(di,
						   di->bat_cap.mah);
		} else {
			di->max_cap_changed = false;
		}

		mutex_lock(&di->cc_lock);
		if (!di->flags.conv_done) {
			/* Wasn't the CC IRQ that got us here */
			mutex_unlock(&di->cc_lock);
			dev_dbg(di->dev, "%s CC conv not done\n",
				__func__);

			break;
		}
		di->flags.conv_done = false;
		mutex_unlock(&di->cc_lock);

		ab8500_fg_calc_cap_charging(di);

		break;

	default:
		break;
	}

	/* Check capacity limits */
	ab8500_fg_check_capacity_limits(di, false);
}

/**
 * ab8500_fg_algorithm_discharging() - FG algorithm for when discharging
 * @di:		pointer to the ab8500_fg structure
 *
 * Battery capacity calculation state machine for when we're discharging
 */
static void ab8500_fg_algorithm_discharging(struct ab8500_fg *di)
{
	int sleep_time;

	/* If we change to charge mode we should start with init */
	if (di->charge_state != AB8500_FG_CHARGE_INIT)
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);

	switch (di->discharge_state) {
	case AB8500_FG_DISCHARGE_INIT:
		/* We use the FG IRQ to work on */
		di->init_cnt = 0;
		di->fg_samples = SEC_TO_SAMPLE(di->bat->fg_params->init_timer);
		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_INITMEASURING);

		/* Intentional fallthrough */
	case AB8500_FG_DISCHARGE_INITMEASURING:
		/*
		 * Discard a number of samples during startup.
		 * After that, use compensated voltage for a few
		 * samples to get an initial capacity.
		 * Then go to READOUT
		 */
		sleep_time = di->bat->fg_params->init_timer;

		/* Discard the first [x] seconds */
		if (di->init_cnt >
			di->bat->fg_params->init_discard_time) {

			ab8500_fg_calc_cap_discharge_voltage(di, true);
			ab8500_fg_check_capacity_limits(di, true);
		}

		di->init_cnt += sleep_time;
		if (di->init_cnt >
			di->bat->fg_params->init_total_time) {
			di->fg_samples = SEC_TO_SAMPLE(
				di->bat->fg_params->accu_high_curr);

			ab8500_fg_coulomb_counter(di, true);
			dev_dbg(di->dev,
				"Filling vcap with %d mah, avg was %d\n",
				di->bat_cap.mah, di->vbat_cap.avg);
			ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);
			di->inst_curr = ab8500_fg_inst_curr(di);
			ab8500_fg_fill_i_sample(di, di->inst_curr);

			ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);
		}

		break;

	case AB8500_FG_DISCHARGE_INIT_RECOVERY:
		di->recovery_cnt = 0;
		di->recovery_needed = true;
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_RECOVERY);

		/* Intentional fallthrough */

	case AB8500_FG_DISCHARGE_RECOVERY:
		sleep_time = di->bat->fg_params->recovery_sleep_timer;

		/*
		 * We should check the power consumption
		 * If low, go to READOUT (after x min) or
		 * RECOVERY_SLEEP if time left.
		 * If high, go to READOUT
		 */
		di->inst_curr = ab8500_fg_inst_curr(di);

		if (ab8500_fg_is_low_curr(di, di->inst_curr)) {
			if (di->recovery_cnt >
				di->bat->fg_params->recovery_total_time) {
				di->fg_samples = SEC_TO_SAMPLE(
					di->bat->fg_params->accu_high_curr);
				ab8500_fg_coulomb_counter(di, true);
				ab8500_fg_discharge_state_to(di,
					AB8500_FG_DISCHARGE_READOUT);
				di->recovery_needed = false;
			} else {
				queue_delayed_work(di->fg_wq,
					&di->fg_periodic_work,
					sleep_time * HZ);
			}
			di->recovery_cnt += sleep_time;
		} else {
			di->fg_samples = SEC_TO_SAMPLE(
				di->bat->fg_params->accu_high_curr);
			ab8500_fg_coulomb_counter(di, true);
			ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);
		}

		break;

	case AB8500_FG_DISCHARGE_READOUT:
		di->inst_curr = ab8500_fg_inst_curr(di);

		{
			int mah, vbat_cap;
			vbat_cap = ab8500_fg_load_comp_volt_to_capacity(di,
					false);
			if (vbat_cap != -1) {
				mah = ab8500_fg_convert_permille_to_mah(di,
						vbat_cap);
				ab8500_fg_add_vcap_sample(di, mah);
				dev_dbg(di->dev,
					"Average voltage based capacity is: "
					"%d mah Now %d mah, [%d %%]\n",
					di->vbat_cap.avg, mah, vbat_cap / 10);
			} else
				dev_dbg(di->dev,
					"Ignoring average voltage"
					" based capacity\n");
		}

		if (ab8500_fg_is_low_curr(di, di->inst_curr)) {
			/* Detect mode change */
			if (di->high_curr_mode) {
				di->high_curr_mode = false;
				di->high_curr_cnt = 0;
			}

			if (di->recovery_needed) {
				ab8500_fg_discharge_state_to(di,
					AB8500_FG_DISCHARGE_RECOVERY);

				queue_delayed_work(di->fg_wq,
					&di->fg_periodic_work,
					0);

				break;
			}

			ab8500_fg_calc_cap_discharge_voltage(di, true);
		} else {
			mutex_lock(&di->cc_lock);
			if (!di->flags.conv_done) {
				/* Wasn't the CC IRQ that got us here */
				mutex_unlock(&di->cc_lock);
				dev_dbg(di->dev, "%s CC conv not done\n",
					__func__);

				break;
			}
			di->flags.conv_done = false;
			mutex_unlock(&di->cc_lock);

			/* Detect mode change */
			if (!di->high_curr_mode) {
				di->high_curr_mode = true;
				di->high_curr_cnt = 0;
			}

			di->high_curr_cnt +=
				di->bat->fg_params->accu_high_curr;
			if (di->high_curr_cnt >
				di->bat->fg_params->high_curr_time)
				di->recovery_needed = true;

			ab8500_fg_calc_cap_discharge_fg(di);
		}

		ab8500_fg_check_capacity_limits(di, false);

#if defined(CONFIG_MACH_CODINA)
		if (DIV_ROUND_CLOSEST(di->bat_cap.permille, 10) <= 10) {
			queue_delayed_work(di->fg_wq,
				&di->fg_periodic_work,
				10 * HZ);
		}
#endif
		break;

	case AB8500_FG_DISCHARGE_WAKEUP:
		ab8500_fg_coulomb_counter(di, true);
		di->inst_curr = ab8500_fg_inst_curr(di);

		ab8500_fg_calc_cap_discharge_voltage(di, true);

		di->fg_samples = SEC_TO_SAMPLE(
			di->bat->fg_params->accu_high_curr);
		/* Re-program number of samples set above */
		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_READOUT);

		ab8500_fg_check_capacity_limits(di, false);

		break;

	default:
		break;
	}
}

/**
 * ab8500_fg_algorithm_calibrate() - Internal columb counter offset calibration
 * @di:		pointer to the ab8500_fg structure
 *
 */
static void ab8500_fg_algorithm_calibrate(struct ab8500_fg *di)
{
	int ret;

	switch (di->calib_state) {
	case AB8500_FG_CALIB_INIT:
		dev_info(di->dev, "Calibration ongoing...\n");

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			CC_INT_CAL_N_AVG_MASK, CC_INT_CAL_SAMPLES_8);
		if (ret < 0)
			goto err;

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			CC_INTAVGOFFSET_ENA, CC_INTAVGOFFSET_ENA);
		if (ret < 0)
			goto err;
		di->calib_state = AB8500_FG_CALIB_WAIT;
		break;
	case AB8500_FG_CALIB_END:
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			CC_MUXOFFSET, CC_MUXOFFSET);
		if (ret < 0)
			goto err;
		di->flags.calibrate = false;
		dev_info(di->dev, "Calibration done...\n");
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
		break;
	case AB8500_FG_CALIB_WAIT:
		dev_info(di->dev, "Calibration WFI\n");
	default:
		break;
	}
	return;
err:
	/* Something went wrong, don't calibrate then */
	dev_err(di->dev, "failed to calibrate the CC\n");
	di->flags.calibrate = false;
	di->calib_state = AB8500_FG_CALIB_INIT;
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
}


static void ab8500_init_columbcounter(struct ab8500_fg *di)
{
	int ret = 0;
	u8 reg_val;

	mutex_lock(&di->cc_lock);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8500_RTC_CC_CONF_REG, &reg_val);
	dev_info(di->dev, "CC_CONF_REG : %d\n", reg_val);

	if (ret < 0)
		goto inst_curr_err;

	if (!(reg_val & CC_PWR_UP_ENA)) {
		/* Start the CC */
		ret = abx500_set_register_interruptible(di->dev,
							AB8500_RTC,
							AB8500_RTC_CC_CONF_REG,
				(CC_DEEP_SLEEP_ENA | CC_PWR_UP_ENA));
		if (ret)
			goto inst_curr_err;
	}

	mutex_unlock(&di->cc_lock);
	return ret;

inst_curr_err:
	dev_err(di->dev, "%s initializing Coulomb Coulomb is failed\n",
		__func__);
	mutex_unlock(&di->cc_lock);
	return ret;
}


/**
 * ab8500_fg_reenable_charging() - function for re-enabling charger explicitly
 * @di:		pointer to the ab8500_fg structure
 *
 * Sometimes, charger is disabled even though
 * S/W doesn't do it while re-charging.
 * This function is workaround for this issue
 */

static int ab8500_fg_reenable_charging(struct ab8500_fg *di)
{

	u8 mainch_ctrl1;
	u8 usbch_ctrl1;
	u8 mainch_status1;
	u8 overshoot = 0;

	int ret;

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_MCH_CTRL1, &mainch_ctrl1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_CH_STATUS1_REG, &mainch_status1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}


	if ((mainch_ctrl1 & 0x1) && !(mainch_status1 & 0x2)) {
		dev_info(di->dev, "%s, unfortunately charger is disabled "
			 "even though s/w doesn't do it\n"
			 "charger will be re-enabled\n", __func__);

		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_MCH_CTRL1, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}


		if (!di->bat->enable_overshoot)
			overshoot = MAIN_CH_NO_OVERSHOOT_ENA_N;

		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_MCH_CTRL1, MAIN_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		if (di->reenable_charing > 2000)
			di->reenable_charing = 1;
		else
			di->reenable_charing++;
	}

	return 0;
}

static void ab8500_fg_read_input_current_reg(struct ab8500_fg *di)
{
	int ret;
	u8 reg;

	ret = abx500_get_register_interruptible(di->dev,
					  AB8500_CHARGER,
					  AB8500_MCH_IPT_CURLVL_REG,
					  &reg);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		di->input_curr_reg = 0;
		return;
	}

	di->input_curr_reg = (int)reg;
}

/**
 * ab8500_fg_algorithm() - Entry point for the FG algorithm
 * @di:		pointer to the ab8500_fg structure
 *
 * Entry point for the battery capacity calculation state machine
 */
static void ab8500_fg_algorithm(struct ab8500_fg *di)
{

	if (di->flags.calibrate) {
		ab8500_init_columbcounter(di);
		ab8500_fg_algorithm_calibrate(di);
	} else {
		if (di->flags.charging) {
			di->bat->fg_res = di->fg_res_chg;
			ab8500_fg_algorithm_charging(di);
			ab8500_fg_read_input_current_reg(di);
		} else {
			di->bat->fg_res = di->fg_res_dischg;
			ab8500_fg_algorithm_discharging(di);
		}
	}

	ab8500_fg_reenable_charging(di);

	if(di->discharge_state != AB8500_FG_DISCHARGE_INITMEASURING)
		pr_info("[FG_DATA] %dmAh/%dmAh %d%% (Prev %dmAh %d%%) %dmV %d "
			"%d %dmA "
			"%dmA %d %d %d %d %d %d %d %d %d %d %d %d %d "
			"%d %d %d %d %d %x %d\n",
			di->bat_cap.mah/1000,
			di->bat_cap.max_mah_design/1000,
			DIV_ROUND_CLOSEST(di->bat_cap.permille, 10),
			di->bat_cap.prev_mah/1000,
			di->bat_cap.prev_percent,
			di->vbat,
			di->vbat_adc,
			di->vbat_adc_compensated,
			di->inst_curr,
			di->avg_curr,
			di->accu_charge,
			di->bat->charge_state,
			di->flags.charging,
			di->charge_state,
			di->discharge_state,
			di->high_curr_mode,
			di->recovery_needed,
			di->bat->fg_res,
			di->vbat_cal_offset,
			di->new_capacity,
			di->param_capacity,
			di->initial_capacity_calib,
			di->flags.fully_charged,
			di->flags.fully_charged_1st,
			di->bat->batt_res,
			di->smd_on,
			di->max_cap_changed,
			di->flags.chg_timed_out,
			di->input_curr_reg,
			di->reenable_charing);
}

/**
 * ab8500_fg_periodic_work() - Run the FG state machine periodically
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for periodic work
 */
static void ab8500_fg_periodic_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_periodic_work.work);

	if (di->init_capacity) {
		/* A dummy read that will return 0 */
		di->inst_curr = ab8500_fg_inst_curr(di);
		if (!di->flags.charging)
			ab8500_fg_fill_i_sample(di, di->inst_curr);
		/* Get an initial capacity calculation */
		ab8500_fg_calc_cap_discharge_voltage(di, true);
		ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);
		ab8500_fg_check_capacity_limits(di, true);
		di->init_capacity = false;
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	} else
		ab8500_fg_algorithm(di);
}

/**
 * ab8500_fg_low_bat_work() - Check LOW_BAT condition
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the LOW_BAT condition
 */
static void ab8500_fg_low_bat_work(struct work_struct *work)
{
	int vbat;

	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_low_bat_work.work);

	vbat = ab8500_comp_fg_bat_voltage(di, true);

	/* Check if LOW_BAT still fulfilled */
	if (vbat < di->bat->fg_params->lowbat_threshold + LOWBAT_TOLERANCE) {
		di->flags.low_bat = true;
		dev_warn(di->dev, "Battery voltage still LOW\n");

		/*
		 * We need to re-schedule this check to be able to detect
		 * if the voltage increases again during charging
		 */
		queue_delayed_work(di->fg_wq, &di->fg_low_bat_work,
			round_jiffies(LOW_BAT_CHECK_INTERVAL));
	} else {
		di->flags.low_bat = false;
		dev_warn(di->dev, "Battery voltage OK again\n");
	}

	/* This is needed to dispatch LOW_BAT */
	ab8500_fg_check_capacity_limits(di, false);

	/* Set this flag to check if LOW_BAT IRQ still occurs */
	di->flags.low_bat_delay = false;
}

/**
 * ab8500_fg_instant_work() - Run the FG state machine instantly
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for instant work
 */
static void ab8500_fg_instant_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg, fg_work);

	ab8500_fg_algorithm(di);
}

/**
 * ab8500_fg_cc_data_end_handler() - isr to get battery avg current.
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_cc_data_end_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;
	complete(&di->ab8500_fg_complete);
	return IRQ_HANDLED;
}

/**
 * ab8500_fg_cc_convend_handler() - isr to get battery avg current.
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_cc_int_calib_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;
	di->calib_state = AB8500_FG_CALIB_END;
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	return IRQ_HANDLED;
}

/**
 * ab8500_fg_cc_convend_handler() - isr to get battery avg current.
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_cc_convend_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;

	wake_lock_timeout(&di->cc_wake_lock, HZ*2);
	queue_work(di->fg_wq, &di->fg_acc_cur_work);

	return IRQ_HANDLED;
}


/**
 * ab8500_fg_lowbatf_handler() - Battery voltage is below LOW threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_lowbatf_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;

	if (!di->flags.low_bat_delay) {
		wake_lock_timeout(&di->lowbat_wake_lock, 20 * HZ);
		dev_warn(di->dev, "Battery voltage is below LOW threshold\n");
		di->flags.low_bat_delay = true;
		/*
		 * Start a timer to check LOW_BAT again after some time
		 * This is done to avoid shutdown on single voltage dips
		 */
		queue_delayed_work(di->fg_wq, &di->fg_low_bat_work,
			round_jiffies(LOW_BAT_CHECK_INTERVAL));
	}
	return IRQ_HANDLED;
}

/**
 * ab8500_fg_get_property() - get the fg properties
 * @psy:	pointer to the power_supply structure
 * @psp:	pointer to the power_supply_property structure
 * @val:	pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the
 * fg properties by reading the sysfs files.
 * voltage_now:		battery voltage
 * current_now:		battery instant current
 * current_avg:		battery average current
 * charge_full_design:	capacity where battery is considered full
 * charge_now:		battery capacity in nAh
 * capacity:		capacity in percent
 * capacity_level:	capacity level
 *
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_fg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_fg *di;

	di = to_ab8500_fg_device_info(psy);

	/*
	 * If battery is identified as unknown and charging of unknown
	 * batteries is disabled, we always report 100% capacity and
	 * capacity level UNKNOWN, since we can't calculate
	 * remaining capacity
	 */

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->vbat * 1000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->inst_curr * 1000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = di->avg_curr * 1000;
		break;

	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = ab8500_fg_convert_mah_to_uwh(di,
				di->bat_cap.max_mah_design);
		break;

	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = ab8500_fg_convert_mah_to_uwh(di,
				di->bat_cap.max_mah);
		break;

	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (di->flags.batt_unknown && !di->bat->chg_unknown_bat)
			val->intval = ab8500_fg_convert_mah_to_uwh(di,
					di->bat_cap.max_mah);
		else
			val->intval = ab8500_fg_convert_mah_to_uwh(di,
					di->bat_cap.prev_mah);
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = di->bat_cap.max_mah_design;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = di->bat_cap.max_mah;
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (di->flags.batt_unknown && !di->bat->chg_unknown_bat)
			val->intval = di->bat_cap.max_mah;
		else
			val->intval = di->bat_cap.prev_mah;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		if (di->flags.fully_charged || di->flags.chg_timed_out)
			/* Unknown Battery or Full charged */
			val->intval = 100;
		else if (di->flags.charging && di->flags.fully_charged_1st)
			val->intval = 100;
		else if (di->flags.charging && di->bat_cap.prev_percent == 100)
			val->intval = 99;
		else
			val->intval = di->bat_cap.prev_percent;
		break;

	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (di->flags.batt_unknown && !di->bat->chg_unknown_bat)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		else
			val->intval = di->bat_cap.prev_level;
		break;

	/* Instantaneous vbat ADC value */
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		di->vbat = ab8500_fg_bat_voltage(di, false);
		val->intval = di->vbat_adc;
		break;

	/* Instantaneous vbat voltage value */
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		di->vbat = ab8500_fg_bat_voltage(di, false);
		val->intval = di->vbat * 1000;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int ab8500_fg_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab8500_fg *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_ab8500_fg_device_info(psy);

	/*
	 * For all psy where the name of your driver
	 * appears in any supplied_to
	 */
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

		if(ext->type == POWER_SUPPLY_TYPE_MAINS)
			continue;
		
		switch (prop) {
		case POWER_SUPPLY_PROP_LPM_MODE:
			/* LPM_MODE */
			if (ret.intval)
				di->lpm_chg_mode = true;
			else
				di->lpm_chg_mode = false;

			break;

		case POWER_SUPPLY_PROP_REINIT_CAPACITY:
		/* Re-initialize battery capacity */
			if (ret.intval && di->reinit_capacity) {
				if (di->lpm_chg_mode)
					queue_delayed_work(di->fg_wq,
					&di->fg_reinit_param_work, 2*HZ);
				else
					queue_delayed_work(di->fg_wq,
					&di->fg_reinit_param_work, 5*HZ);

				di->reinit_capacity = false;
			}

			break;

		/* Calibarating vbat */
		case POWER_SUPPLY_PROP_BATT_CAL:
			if (ret.intval)
				di->vbat_cal_offset =
					di->gpadc_vbat_ideal - ret.intval;

			break;

		/* 1st Full charging */
		case POWER_SUPPLY_PROP_UI_FULL:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->flags.fully_charged_1st = ret.intval;
				queue_work(di->fg_wq, &di->fg_work);
				break;
			}
			break;

		/* Charging timeout check */
		case POWER_SUPPLY_PROP_CHARGING_TIMEOUT:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->flags.chg_timed_out = ret.intval;
				break;
			}
			break;

		case POWER_SUPPLY_PROP_STATUS:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				switch (ret.intval) {
				case POWER_SUPPLY_STATUS_UNKNOWN:
				case POWER_SUPPLY_STATUS_DISCHARGING:
				case POWER_SUPPLY_STATUS_NOT_CHARGING:
					if (!di->flags.charging)
						break;
					di->flags.charging = false;
					di->flags.fully_charged = false;
					di->flags.fully_charged_1st = false;
					queue_work(di->fg_wq, &di->fg_work);
					break;
				case POWER_SUPPLY_STATUS_FULL:
					if (di->flags.fully_charged)
						break;
					di->flags.fully_charged = true;
					queue_work(di->fg_wq, &di->fg_work);
					break;
				case POWER_SUPPLY_STATUS_CHARGING:
					if (di->flags.charging)
						break;
					di->flags.charging = true;
					di->flags.fully_charged = false;
					queue_work(di->fg_wq, &di->fg_work);
					break;
				};
			default:
				break;
			};
			break;

		case POWER_SUPPLY_PROP_TECHNOLOGY:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				if (ret.intval)
					di->flags.batt_unknown = false;
				else
					di->flags.batt_unknown = true;
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
 * ab8500_fg_init_hw_registers() - Set up FG related registers
 * @di:		pointer to the ab8500_fg structure
 *
 * Set up battery OVV, low battery voltage registers
 */
static int ab8500_fg_init_hw_registers(struct ab8500_fg *di)
{
	int ret;

	/* Set up VBAT OVV register */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_BATT_OVV,
		(BATT_OVV_ENA | BATT_OVV_TH_4P75));
	if (ret) {
		dev_err(di->dev, "failed to set BATT_OVV\n");
		goto out;
	}

	/* Low Battery Voltage */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_LOW_BAT_REG,
		ab8500_volt_to_regval(
			di->bat->fg_params->lowbat_threshold) << 1 |
		LOW_BAT_ENABLE);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		goto out;
	}

out:
	return ret;
}

static int ab8500_fg_read_battery_capacity(struct ab8500_fg *di)
{
	struct file *filp;
	mm_segment_t old_fs;
	int ret = 0;
	int off_status = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	filp = filp_open(BATT_CAPACITY_PATH, O_RDONLY, 0666);
	if (IS_ERR(filp)) {
		dev_err(di->dev,
			"[FG_DATA] Can't open last battery capacity file\n");
		set_fs(old_fs);
		return 0;
	}

	ret = filp->f_op->read(filp, (char *)&off_status,
			       sizeof(int), &filp->f_pos);

	if (ret != sizeof(int)) {
		dev_err(di->dev, "[FG_DATA] Can't read "
			"the last battery capacity from file\n");
		off_status = 0;
	} else
		dev_info(di->dev,
			 "[FG_DATA] Read data from efs successfully : 0x%x\n",
			 off_status);

	filp_close(filp, current->files);
	set_fs(old_fs);

	return off_status;
}

static int ab8500_fg_write_battery_capacity(struct ab8500_fg *di, int value)
{
	struct file *filp;
	mm_segment_t old_fs;
	int ret = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	filp = filp_open(BATT_CAPACITY_PATH,
			 O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (IS_ERR(filp)) {
		dev_err(di->dev, "[FG_DATA] Can't open last battery "
			"capacity file\n");
		set_fs(old_fs);
		ret = PTR_ERR(filp);
		return ret;
	}

	ret = filp->f_op->write(filp, (char *)&value,
				sizeof(int), &filp->f_pos);
	if (ret != sizeof(int)) {
		dev_err(di->dev, "[FG_DATA] Can't write "
			"the last battery capacity to file\n");
		ret = -EIO;
	} else
		dev_info(di->dev, "[FG_DATA] Battery status is written "
			 "in EFS successfully : 0x%x, %d\n", value, value);

	filp_close(filp, current->files);
	set_fs(old_fs);

	return ret;
}

/**
 * ab8500_fg_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is the entry point of the pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in any external power
 * supply that this driver needs to be notified of.
 */
static void ab8500_fg_external_power_changed(struct power_supply *psy)
{
	struct ab8500_fg *di = to_ab8500_fg_device_info(psy);

	class_for_each_device(power_supply_class, NULL,
		&di->fg_psy, ab8500_fg_get_ext_psy_data);
}

static void ab8500_fg_reinit_param_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
					    fg_reinit_param_work.work);

	int off_status = 0x0;
	int magic_code = 0;
	int param_voltage = 0;
	int param_charge_state = 0;
	int new_capacity = 0, param_capacity = 0, batt_voltage = 0;
	int delta, mah = 0;
	int valid_range = 0;
	int offset_null = -1;
	int switchoff_status = 0;
	bool reset_state = false;

	int ret;


	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
				0x00, &switchoff_status);
	if (ret < 0)
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);

	dev_info(di->dev, "[FG_DATA] SwitchOffStatus : 0x%x\n",
	       switchoff_status);

#if defined(CONFIG_BATT_CAPACITY_PARAM)
	if (sec_get_param_value)
		sec_get_param_value(__BATT_CAPACITY, &off_status);
#else
	off_status = ab8500_fg_read_battery_capacity(di);
#endif

	magic_code = (off_status >> OFF_MAGIC_CODE)
		& OFF_MAGIC_CODE_MASK;

	if (magic_code == MAGIC_CODE_RESET) {
		dev_info(di->dev, "[FG_DATA] RESET is detected by FG\n");
		reset_state = true;
	}

	if (magic_code == MAGIC_CODE || magic_code == MAGIC_CODE_RESET) {
		param_voltage = (off_status >> OFF_VOLTAGE)
			& OFF_VOLTAGE_MASK;
		param_capacity = (off_status >> OFF_CAPACITY)
			& OFF_CAPACITY_MASK;
		param_charge_state = (off_status >> OFF_CHARGE_STATE)
			& OFF_CHARGE_STATE_MASK;

		dev_info(di->dev, "[FG_DATA] OFF STATUS : 0x%x, "
			 "LAST_VOLTAGE : %dmV, "
			 "LAST_CAPACITY : %d%%, LAST_CHARGE_STATE : %s\n",
			 off_status, param_voltage, param_capacity,
			 states[param_charge_state]);

#if defined(CONFIG_BATT_CAPACITY_PARAM)
		if (sec_set_param_value)
			sec_set_param_value(__BATT_CAPACITY, &offset_null);
#else
		ab8500_fg_write_battery_capacity(di, offset_null);
#endif
	} else if (off_status == -1) {
		dev_info(di->dev, "[FG_DATA] OFF STATUS, 0x%x\n"
		       "[FG_DATA] *** Phone was powered off "
		       "by abnormal way ***\n", off_status);
	} else {
		dev_info(di->dev, "[FG_DATA] OFF STATUS, 0x%x\n"
		       "[FG_DATA] *** Maybe this is a first boot ***\n",
		       off_status);
#if defined(CONFIG_BATT_CAPACITY_PARAM)
		if (sec_set_param_value)
			sec_set_param_value(__BATT_CAPACITY, &offset_null);
#else
		ab8500_fg_write_battery_capacity(di, offset_null);
#endif
	}

	batt_voltage = ab8500_comp_fg_bat_voltage(di, true);
	new_capacity = DIV_ROUND_CLOSEST(di->bat_cap.permille, 10);

	if (param_capacity < 0 || param_capacity > 100)
		param_capacity = 0;

	dev_info(di->dev, "NEW capacity : %d, PARAM capacity : %d, VBAT : %d\n",
		 new_capacity, param_capacity, batt_voltage);

	di->new_capacity = new_capacity;
	di->param_capacity = param_capacity;

	if (batt_voltage > 3950)
		valid_range = 10;
	else if (batt_voltage > 3800)
		valid_range = 15;
	else if (batt_voltage > 3750)
		valid_range = 20;
	else if (batt_voltage > 3650)
		valid_range = 35;
	else if (batt_voltage > 3600)
		valid_range = 20;
	else
		valid_range = 10;

	delta = new_capacity - param_capacity;
	if (reset_state ||
	    (delta*delta/2 < valid_range && !(param_capacity <= 0))) {
		cancel_delayed_work(&di->fg_periodic_work);
		dev_info(di->dev, "Capacity in param will be used "
			 "as an initial capacity, new %d%% -> param %d%%\n",
			 new_capacity, param_capacity);
		di->initial_capacity_calib = true;
		di->prev_capacity = param_capacity;
		ab8500_fg_clear_cap_samples(di);
		mah = ab8500_fg_convert_permille_to_mah(di, param_capacity*10);
		di->bat_cap.mah = ab8500_fg_add_cap_sample(di, mah);
		di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
		ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);
		if (!di->flags.charging) {
			di->inst_curr = ab8500_fg_inst_curr(di);
			ab8500_fg_fill_i_sample(di, di->inst_curr);
		}
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);
		ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_INIT);
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	} else {
		dev_info(di->dev, "New capacity will be used "
			 "as an initial capacity, new %d%% ( param %d%% )\n",
			 new_capacity, param_capacity);
		di->initial_capacity_calib = false;
		di->prev_capacity = new_capacity;
	}
}


static void ab8500_fg_reinit_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_reinit_work.work);

	if (di->flags.calibrate == false) {
		dev_dbg(di->dev, "Resetting FG state machine to init.\n");
		ab8500_fg_clear_cap_samples(di);
		ab8500_fg_calc_cap_discharge_voltage(di, true);
		ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);
		if (!di->flags.charging) {
			di->inst_curr = ab8500_fg_inst_curr(di);
			ab8500_fg_fill_i_sample(di, di->inst_curr);
		}
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);
		ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_INIT);
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);

	} else {
		dev_err(di->dev, "Residual offset calibration ongoing "
			"retrying..\n");
		/* Wait one second until next try*/
		queue_delayed_work(di->fg_wq, &di->fg_reinit_work,
			round_jiffies(1));
	}
}

void ab8500_fg_reinit(void)
{
	struct ab8500_fg *di = ab8500_fg_get();
	if (di)
		queue_delayed_work(di->fg_wq, &di->fg_reinit_work, 0);
}

#if defined(CONFIG_PM)
static int ab8500_fg_resume(struct platform_device *pdev)
{
	struct ab8500_fg *di = platform_get_drvdata(pdev);

	/*
	 * Change state if we're not charging. If we're charging we will wake
	 * up on the FG IRQ
	 */
	if (!di->flags.charging) {
		ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_WAKEUP);
		queue_work(di->fg_wq, &di->fg_work);
	}

	return 0;
}

static int ab8500_fg_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_fg *di = platform_get_drvdata(pdev);

	cancel_delayed_work(&di->fg_periodic_work);

	/*
	 * If the FG is enabled we will disable it before going to suspend
	 * only if we're not charging
	 */
	if (di->flags.fg_enabled && !di->flags.charging)
		ab8500_fg_coulomb_counter(di, false);

	return 0;
}
#else
#define ab8500_fg_suspend      NULL
#define ab8500_fg_resume       NULL
#endif

static int ab8500_fg_reboot_call(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct ab8500_fg *di;

	di = container_of(self, struct ab8500_fg, fg_notifier);

	/* AB8500 FG cannot keep the battery capacity itself.
	   So, If we repeat the power off/on,
	   battery capacity error occur necessarily.
	   We will keep the capacity in PARAM region for avoiding it.
	*/
	/* Additionally, we will keep the last voltage and charging status
	   for checking unknown/normal power off.
	*/
	int off_status = 0x0;

	if (di->lpm_chg_mode && vbus_state)
		off_status |= (MAGIC_CODE_RESET << OFF_MAGIC_CODE);
	else
		off_status |= (MAGIC_CODE << OFF_MAGIC_CODE);

	off_status |= (di->vbat << OFF_VOLTAGE);
	off_status |= (DIV_ROUND_CLOSEST(di->bat_cap.permille, 10) <<
			OFF_CAPACITY);
	off_status |= (di->bat->charge_state << OFF_CHARGE_STATE);

#if defined(CONFIG_BATT_CAPACITY_PARAM)
	if (sec_set_param_value)
		sec_set_param_value(__BATT_CAPACITY, &off_status);
#else
	ab8500_fg_write_battery_capacity(di, off_status);
#endif

	return NOTIFY_DONE;
}

static int __devexit ab8500_fg_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct ab8500_fg *di = platform_get_drvdata(pdev);

	/* Disable coulomb counter */
	ret = ab8500_fg_coulomb_counter(di, false);
	if (ret)
		dev_err(di->dev, "failed to disable coulomb counter\n");

	destroy_workqueue(di->fg_wq);
	wake_lock_destroy(&di->lowbat_wake_lock);
	wake_lock_destroy(&di->lowbat_poweroff_wake_lock);
	wake_lock_destroy(&di->cc_wake_lock);

	flush_scheduled_work();
	power_supply_unregister(&di->fg_psy);
	platform_set_drvdata(pdev, NULL);
	kfree(di);
	return ret;
}

/* ab8500 fg driver interrupts and their respective isr */
static struct ab8500_fg_interrupts ab8500_fg_irq[] = {
	{"NCONV_ACCU", ab8500_fg_cc_convend_handler},
	{"LOW_BAT_F", ab8500_fg_lowbatf_handler},
	{"CC_INT_CALIB", ab8500_fg_cc_int_calib_handler},
	{"CCEOC", ab8500_fg_cc_data_end_handler},
};

static int __devinit ab8500_fg_probe(struct platform_device *pdev)
{
	int i, irq;
	struct ab8500_platform_data *plat;
	int ret = 0;

	struct ab8500_fg *di =
		kzalloc(sizeof(struct ab8500_fg), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	mutex_init(&di->cc_lock);

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab8500_gpadc_get();

	plat = dev_get_platdata(di->parent->dev);

	/* get fg specific platform data */
	if (!plat->fg) {
		dev_err(di->dev, "no fg platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->pdata = plat->fg;

	/* get battery specific platform data */
	if (!plat->battery) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->bat = plat->battery;

	di->fg_psy.name = "ab8500_fg";
	di->fg_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->fg_psy.properties = ab8500_fg_props;
	di->fg_psy.num_properties = ARRAY_SIZE(ab8500_fg_props);
	di->fg_psy.get_property = ab8500_fg_get_property;
	di->fg_psy.supplied_to = di->pdata->supplied_to;
	di->fg_psy.num_supplicants = di->pdata->num_supplicants;
	di->fg_psy.external_power_changed = ab8500_fg_external_power_changed;

	di->bat_cap.max_mah_design = MILLI_TO_MICRO *
		di->bat->bat_type[di->bat->batt_id].charge_full_design;

	di->bat_cap.max_mah = di->bat_cap.max_mah_design;

	di->vbat_cal_offset = 0;
	di->vbat_nom = di->bat->bat_type[di->bat->batt_id].nominal_voltage;
	di->gpadc_vbat_gain = (int)di->gpadc->cal_data[ADC_INPUT_VBAT].gain;
	di->gpadc_vbat_offset = (int)di->gpadc->cal_data[ADC_INPUT_VBAT].offset;

	/* Ideal value for vbat calibration */
	di->gpadc_vbat_ideal = (VBAT_ADC_CAL*1000 - di->gpadc_vbat_offset)
				/ di->gpadc_vbat_gain;

#ifdef CONFIG_MACH_JANICE
	if (system_rev >= JANICE_R0_2) {
		if (!gpio_get_value(SMD_ON_JANICE_R0_2))
			di->smd_on = 1;
	}
#endif
	
	di->init_capacity = true;
	di->reinit_capacity = true;

	/* fg_res parameter should be re-calculated
	   according to the HW revision. */
#if defined(CONFIG_MACH_JANICE)
	if (system_rev < JANICE_R0_3) {
		di->fg_res_chg = FGRES_HWREV_02_CH;
		di->fg_res_dischg = FGRES_HWREV_02;
	} else {
		di->fg_res_chg = FGRES_HWREV_03_CH;
		di->fg_res_dischg = FGRES_HWREV_03;
	}
#else
	di->fg_res_chg = FGRES_CH;
	di->fg_res_dischg = FGRES;
#endif

	di->bat->fg_res = di->fg_res_dischg;

	ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);
	ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_INIT);

	wake_lock_init(&di->lowbat_wake_lock, WAKE_LOCK_SUSPEND,
		       "lowbat_wake_lock");

	wake_lock_init(&di->lowbat_poweroff_wake_lock, WAKE_LOCK_SUSPEND,
		       "lowbat_poweroff_wake_lock");

	wake_lock_init(&di->cc_wake_lock, WAKE_LOCK_SUSPEND,
		       "cc_wake_lock");

	/* Create a work queue for running the FG algorithm */
	di->fg_wq = create_singlethread_workqueue("ab8500_fg_wq");
	if (di->fg_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for running the fg algorithm instantly */
	INIT_WORK(&di->fg_work, ab8500_fg_instant_work);

	/* Init work for getting the battery accumulated current */
	INIT_WORK(&di->fg_acc_cur_work, ab8500_fg_acc_cur_work);

	/* Init work for reinitialising the fg algorithm */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_reinit_work,
		ab8500_fg_reinit_work);

	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_reinit_param_work,
		ab8500_fg_reinit_param_work);

	/* Work delayed Queue to run the state machine */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_periodic_work,
		ab8500_fg_periodic_work);

	/* Work to check low battery condition */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_low_bat_work,
		ab8500_fg_low_bat_work);

	/* Initialize OVV, and other registers */
	ret = ab8500_fg_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize registers\n");
		goto free_fg_wq;
	}

	/* Consider battery unknown until we're informed otherwise */
	di->flags.batt_unknown = true;

	di->flags.fully_charged_1st = false;

	di->fg_notifier.notifier_call = ab8500_fg_reboot_call;
	register_reboot_notifier(&di->fg_notifier);

	/* Register FG power supply class */
	ret = power_supply_register(di->dev, &di->fg_psy);
	if (ret) {
		dev_err(di->dev, "failed to register FG psy\n");
		goto free_fg_wq;
	}

	di->fg_samples = SEC_TO_SAMPLE(di->bat->fg_params->init_timer);
	ab8500_fg_coulomb_counter(di, true);

	/* Initialize completion used to notify completion of inst current */
	init_completion(&di->ab8500_fg_complete);

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_fg_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_fg_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab8500_fg_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab8500_fg_irq[i].name, di);

		if (ret != 0) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab8500_fg_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab8500_fg_irq[i].name, irq, ret);
	}
	di->irq = platform_get_irq_byname(pdev, "CCEOC");
	disable_irq(di->irq);

	platform_set_drvdata(pdev, di);
	list_add(&di->node, &ab8500_fg_list);


	/* Calibrate the fg first time */
	di->flags.calibrate = true;
	di->calib_state = AB8500_FG_CALIB_INIT;
	/* Run the FG algorithm */
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);

	return ret;

free_irq:
	power_supply_unregister(&di->fg_psy);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab8500_fg_irq[i].name);
		free_irq(irq, di);
	}
free_fg_wq:
	destroy_workqueue(di->fg_wq);
	wake_lock_destroy(&di->lowbat_wake_lock);
	wake_lock_destroy(&di->lowbat_poweroff_wake_lock);
	wake_lock_destroy(&di->cc_wake_lock);

free_device_info:
	kfree(di);

	return ret;
}

static struct platform_driver ab8500_fg_driver = {
	.probe = ab8500_fg_probe,
	.remove = __devexit_p(ab8500_fg_remove),
	.suspend = ab8500_fg_suspend,
	.resume = ab8500_fg_resume,
	.driver = {
		.name = "ab8500-fg",
		.owner = THIS_MODULE,
	},
};

static int __init ab8500_fg_init(void)
{
	return platform_driver_register(&ab8500_fg_driver);
}

static void __exit ab8500_fg_exit(void)
{
	platform_driver_unregister(&ab8500_fg_driver);
}

subsys_initcall_sync(ab8500_fg_init);
module_exit(ab8500_fg_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab8500-fg");
MODULE_DESCRIPTION("AB8500 Fuel Gauge driver");
