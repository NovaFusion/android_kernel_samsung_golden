/*
 * Copyright (C) ST-Ericsson AB 2011
 *
 * Main and Back-up battery management driver.
 *
 * Note: Backup battery management is required in case of Li-Ion battery and not
 * for capacitive battery. HREF boards have capacitive battery and hence backup
 * battery management is not used and the supported code is available in this
 * driver.
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
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>
#include <linux/mfd/abx500/ab5500-bm.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>

static LIST_HEAD(ab5500_fg_list);

/* U5500 Constants */
#define FG_ON_MASK			0x04
#define FG_ON				0x04
#define FG_ACC_RESET_ON_READ_MASK	0x08
#define FG_ACC_RESET_ON_READ		0x08
#define EN_READOUT_MASK			0x01
#define EN_READOUT			0x01
#define EN_ACC_RESET_ON_READ		0x08
#define ACC_RESET_ON_READ		0x08
#define RESET				0x00
#define EOC_52_mA			0x04
#define MILLI_TO_MICRO			1000
#define FG_LSB_IN_MA			770
#define QLSB_NANO_AMP_HOURS_X100	5353
#define SEC_TO_SAMPLE(S)		(S * 4)
#define NBR_AVG_SAMPLES			20
#define LOW_BAT_CHECK_INTERVAL		(2 * HZ)
#define FG_PERIODIC_START_INTERVAL	(250 * HZ)/1000 /* 250 msec */

#define VALID_CAPACITY_SEC		(45 * 60) /* 45 minutes */

#define interpolate(x, x1, y1, x2, y2) \
	((y1) + ((((y2) - (y1)) * ((x) - (x1))) / ((x2) - (x1))));

#define to_ab5500_fg_device_info(x) container_of((x), \
	struct ab5500_fg, fg_psy);

/**
 * struct ab5500_fg_interrupts - ab5500 fg interupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab5500_fg_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

enum ab5500_fg_discharge_state {
	AB5500_FG_DISCHARGE_INIT,
	AB5500_FG_DISCHARGE_INITMEASURING,
	AB5500_FG_DISCHARGE_INIT_RECOVERY,
	AB5500_FG_DISCHARGE_RECOVERY,
	AB5500_FG_DISCHARGE_READOUT,
	AB5500_FG_DISCHARGE_WAKEUP,
};

static char *discharge_state[] = {
	"DISCHARGE_INIT",
	"DISCHARGE_INITMEASURING",
	"DISCHARGE_INIT_RECOVERY",
	"DISCHARGE_RECOVERY",
	"DISCHARGE_READOUT",
	"DISCHARGE_WAKEUP",
};

enum ab5500_fg_charge_state {
	AB5500_FG_CHARGE_INIT,
	AB5500_FG_CHARGE_READOUT,
};

static char *charge_state[] = {
	"CHARGE_INIT",
	"CHARGE_READOUT",
};

enum ab5500_fg_calibration_state {
	AB5500_FG_CALIB_INIT,
	AB5500_FG_CALIB_WAIT,
	AB5500_FG_CALIB_END,
};

struct ab5500_fg_avg_cap {
	int avg;
	int samples[NBR_AVG_SAMPLES];
	__kernel_time_t time_stamps[NBR_AVG_SAMPLES];
	int pos;
	int nbr_samples;
	int sum;
};

struct ab5500_fg_battery_capacity {
	int max_mah_design;
	int max_mah;
	int mah;
	int permille;
	int level;
	int prev_mah;
	int prev_percent;
	int prev_level;
};

struct ab5500_fg_flags {
	bool fg_enabled;
	bool conv_done;
	bool charging;
	bool fully_charged;
	bool low_bat_delay;
	bool low_bat;
	bool bat_ovv;
	bool batt_unknown;
	bool calibrate;
};

/**
 * struct ab5500_fg - ab5500 FG device information
 * @dev:		Pointer to the structure device
 * @vbat:		Battery voltage in mV
 * @vbat_nom:		Nominal battery voltage in mV
 * @inst_curr:		Instantenous battery current in mA
 * @avg_curr:		Average battery current in mA
 * @fg_samples:		Number of samples used in the FG accumulation
 * @accu_charge:	Accumulated charge from the last conversion
 * @recovery_cnt:	Counter for recovery mode
 * @high_curr_cnt:	Counter for high current mode
 * @init_cnt:		Counter for init mode
 * @v_to_cap:		capacity based on battery voltage
 * @recovery_needed:	Indicate if recovery is needed
 * @high_curr_mode:	Indicate if we're in high current mode
 * @init_capacity:	Indicate if initial capacity measuring should be done
 * @calib_state		State during offset calibration
 * @discharge_state:	Current discharge state
 * @charge_state:	Current charge state
 * @flags:		Structure for information about events triggered
 * @bat_cap:		Structure for battery capacity specific parameters
 * @avg_cap:		Average capacity filter
 * @parent:		Pointer to the struct ab5500
 * @gpadc:		Pointer to the struct gpadc
 * @gpadc_auto:		Pointer tot he struct adc_auto_input
 * @pdata:		Pointer to the ab5500_fg platform data
 * @bat:		Pointer to the ab5500_bm platform data
 * @fg_psy:		Structure that holds the FG specific battery properties
 * @fg_wq:		Work queue for running the FG algorithm
 * @fg_periodic_work:	Work to run the FG algorithm periodically
 * @fg_low_bat_work:	Work to check low bat condition
 * @fg_reinit_work:	Work to reset and re-initialize fuel gauge
 * @fg_work:		Work to run the FG algorithm instantly
 * @fg_acc_cur_work:	Work to read the FG accumulator
 * @cc_lock:		Mutex for locking the CC
 * @node:		struct of type list_head
 */
struct ab5500_fg {
	struct device *dev;
	int vbat;
	int vbat_nom;
	int inst_curr;
	int avg_curr;
	int fg_samples;
	int accu_charge;
	int recovery_cnt;
	int high_curr_cnt;
	int init_cnt;
	int v_to_cap;
	bool recovery_needed;
	bool high_curr_mode;
	bool init_capacity;
	enum ab5500_fg_calibration_state calib_state;
	enum ab5500_fg_discharge_state discharge_state;
	enum ab5500_fg_charge_state charge_state;
	struct ab5500_fg_flags flags;
	struct ab5500_fg_battery_capacity bat_cap;
	struct ab5500_fg_avg_cap avg_cap;
	struct ab5500 *parent;
	struct ab5500_gpadc *gpadc;
	struct adc_auto_input *gpadc_auto;
	struct abx500_fg_platform_data *pdata;
	struct abx500_bm_data *bat;
	struct power_supply fg_psy;
	struct workqueue_struct *fg_wq;
	struct delayed_work fg_periodic_work;
	struct delayed_work fg_low_bat_work;
	struct delayed_work fg_reinit_work;
	struct work_struct fg_work;
	struct delayed_work fg_acc_cur_work;
	struct mutex cc_lock;
	struct list_head node;
	struct timer_list avg_current_timer;
};

/* Main battery properties */
static enum power_supply_property ab5500_fg_props[] = {
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
};

/* Function Prototype */
static int ab5500_fg_bat_v_trig(int mux);

static int prev_samples, prev_val;

struct ab5500_fg *ab5500_fg_get(void)
{
	struct ab5500_fg *di;
	di = list_first_entry(&ab5500_fg_list, struct ab5500_fg, node);

	return di;
}

/**
 * ab5500_fg_is_low_curr() - Low or high current mode
 * @di:		pointer to the ab5500_fg structure
 * @curr:	the current to base or our decision on
 *
 * Low current mode if the current consumption is below a certain threshold
 */
static int ab5500_fg_is_low_curr(struct ab5500_fg *di, int curr)
{
	/*
	 * We want to know if we're in low current mode
	 */
	if (curr > -di->bat->fg_params->high_curr_threshold)
		return true;
	else
		return false;
}

/**
 * ab5500_fg_add_cap_sample() - Add capacity to average filter
 * @di:		pointer to the ab5500_fg structure
 * @sample:	the capacity in mAh to add to the filter
 *
 * A capacity is added to the filter and a new mean capacity is calculated and
 * returned
 */
static int ab5500_fg_add_cap_sample(struct ab5500_fg *di, int sample)
{
	struct timespec ts;
	struct ab5500_fg_avg_cap *avg = &di->avg_cap;

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
 * ab5500_fg_clear_cap_samples() - Clear average filter
 * @di:                pointer to the ab5500_fg structure
 *
 * The capacity filter is is reset to zero.
 */
static void ab5500_fg_clear_cap_samples(struct ab5500_fg *di)
{
	int i;
	struct ab5500_fg_avg_cap *avg = &di->avg_cap;

	avg->pos = 0;
	avg->nbr_samples = 0;
	avg->sum = 0;
	avg->avg = 0;

	for (i = 0; i < NBR_AVG_SAMPLES; i++) {
		avg->samples[i] = 0;
		avg->time_stamps[i] = 0;
	}
}


/**
 * ab5500_fg_fill_cap_sample() - Fill average filter
 * @di:		pointer to the ab5500_fg structure
 * @sample:	the capacity in mAh to fill the filter with
 *
 * The capacity filter is filled with a capacity in mAh
 */
static void ab5500_fg_fill_cap_sample(struct ab5500_fg *di, int sample)
{
	int i;
	struct timespec ts;
	struct ab5500_fg_avg_cap *avg = &di->avg_cap;

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
 * ab5500_fg_coulomb_counter() - enable coulomb counter
 * @di:		pointer to the ab5500_fg structure
 * @enable:	enable/disable
 *
 * Enable/Disable coulomb counter.
 * On failure returns negative value.
 */
static int ab5500_fg_coulomb_counter(struct ab5500_fg *di, bool enable)
{
	int ret = 0;
	mutex_lock(&di->cc_lock);
	if (enable) {
		/* Power-up the CC */
		ret = abx500_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			(FG_ON | FG_ACC_RESET_ON_READ));
		if (ret)
			goto cc_err;

		di->flags.fg_enabled = true;
	} else {
		/* Stop the CC */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			FG_ON_MASK, RESET);
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
 * ab5500_fg_inst_curr() - battery instantaneous current
 * @di:         pointer to the ab5500_fg structure
 *
 * Returns battery instantenous current(on success) else error code
 */
static int ab5500_fg_inst_curr(struct ab5500_fg *di)
{
	u8 low, high;
	static int val;
	int ret = 0;
	bool fg_off = false;

	if (!di->flags.fg_enabled) {
		fg_off = true;
		/* Power-up the CC */
		ab5500_fg_coulomb_counter(di, true);
		msleep(250);
	}

	mutex_lock(&di->cc_lock);

	/* Enable read request */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_B,
		EN_READOUT_MASK, EN_READOUT);
	if (ret)
		goto inst_curr_err;

	/* Read CC Sample conversion value Low and high */
	ret = abx500_get_register_interruptible(di->dev,
		AB5500_BANK_FG_BATTCOM_ACC,
		AB5500_FGDIR_READ0,  &low);
	if (ret < 0)
		goto inst_curr_err;

	ret = abx500_get_register_interruptible(di->dev,
		AB5500_BANK_FG_BATTCOM_ACC,
		AB5500_FGDIR_READ1,  &high);
	if (ret < 0)
		goto inst_curr_err;

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
	 * R(FGSENSE) = 20 mOhm
	 * Scaling of LSB: This corresponds fro R(FGSENSE) to a current of
	 * I = Q/t = 192.7 uC * 4 Hz = 0.77mA
	 */
	val = (val * 770) / 1000;

	mutex_unlock(&di->cc_lock);

	if (fg_off) {
		dev_dbg(di->dev, "%s Disable FG\n", __func__);
		/* Power-off the CC */
		ab5500_fg_coulomb_counter(di, false);
	}

	return val;

inst_curr_err:
	dev_err(di->dev, "%s Get instanst current failed\n", __func__);
	mutex_unlock(&di->cc_lock);
	return ret;
}

static void ab5500_fg_acc_cur_timer_expired(unsigned long data)
{
	struct ab5500_fg *di = (struct ab5500_fg *) data;
	dev_dbg(di->dev, "Avg current timer expired\n");

	/* Trigger execution of the algorithm instantly */
	queue_delayed_work(di->fg_wq, &di->fg_acc_cur_work, 0);
}

/**
 * ab5500_fg_acc_cur_work() - average battery current
 * @work:	pointer to the work_struct structure
 *
 * Updated the average battery current obtained from the
 * coulomb counter.
 */
static void ab5500_fg_acc_cur_work(struct work_struct *work)
{
	int val, raw_val, sample;
	int ret;
	u8 low, med, high, cnt_low, cnt_high;

	struct ab5500_fg *di = container_of(work,
		struct ab5500_fg, fg_acc_cur_work.work);

	if (!di->flags.fg_enabled) {
		/* Power-up the CC */
		ab5500_fg_coulomb_counter(di, true);
		msleep(250);
	}
	mutex_lock(&di->cc_lock);
	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_C,
			EN_READOUT_MASK, EN_READOUT);
	if (ret < 0)
		goto exit;
	/* If charging read charging registers for accumulated values */
	if (di->flags.charging) {
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			ACC_RESET_ON_READ, EN_ACC_RESET_ON_READ);
		if (ret < 0)
			goto exit;
		/* Read CC Sample conversion value Low and high */
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_CH0, &low);
		if (ret < 0)
			goto exit;

		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_CH1, &med);
		if (ret < 0)
			goto exit;
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_CH2, &high);
		if (ret < 0)
			goto exit;
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_VAL_COUNT0, &cnt_low);
		if (ret < 0)
			goto exit;
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_VAL_COUNT1, &cnt_high);
		if (ret < 0)
			goto exit;
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			ACC_RESET_ON_READ, RESET);
		if (ret < 0)
			goto exit;
		queue_delayed_work(di->fg_wq, &di->fg_acc_cur_work,
				di->bat->interval_charging * HZ);
	} else { /* discharging */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			ACC_RESET_ON_READ, EN_ACC_RESET_ON_READ);
		if (ret < 0)
			goto exit;
		/* Read CC Sample conversion value Low and high */
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_DIS_CH0, &low);
		if (ret < 0)
			goto exit;

		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_DIS_CH1, &med);
		if (ret < 0)
			goto exit;
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_DIS_CH2, &high);
		if (ret < 0)
			goto exit;
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_VAL_COUNT0, &cnt_low);
		if (ret < 0)
			goto exit;
		ret = abx500_get_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC,
			AB5500_FG_VAL_COUNT1, &cnt_high);
		if (ret < 0)
			goto exit;
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			ACC_RESET_ON_READ, RESET);
		if (ret < 0)
			goto exit;
		queue_delayed_work(di->fg_wq, &di->fg_acc_cur_work,
				di->bat->interval_not_charging * HZ);
	}
	di->fg_samples = (cnt_low | (cnt_high << 8));
	/*
	 * TODO: Workaround due to the hardware issue that accumulator is not
	 * reset after setting reset_on_read bit and reading the accumulator
	 * Registers.
	 */
	if (prev_samples > di->fg_samples) {
		/* overflow has occured */
		sample = (0xFFFF - prev_samples) + di->fg_samples;
	} else
		sample = di->fg_samples - prev_samples;
	prev_samples = di->fg_samples;
	di->fg_samples = sample;
	val = (low | (med << 8) | (high << 16));
	/*
	 * TODO: Workaround due to the hardware issue that accumulator is not
	 * reset after setting reset_on_read bit and reading the accumulator
	 * Registers.
	 */
	if (prev_val > val)
		raw_val = (0xFFFFFF - prev_val) + val;
	else
		raw_val = val - prev_val;
	prev_val = val;
	val = raw_val;

	if (di->fg_samples) {
		di->accu_charge = (val * QLSB_NANO_AMP_HOURS_X100)/100000;
		di->avg_curr = (val * FG_LSB_IN_MA) / (di->fg_samples * 1000);
	} else
		dev_err(di->dev,
			"samples is zero, using previous calculated average current\n");
	di->flags.conv_done = true;
	di->calib_state = AB5500_FG_CALIB_END;

	mutex_unlock(&di->cc_lock);

	queue_work(di->fg_wq, &di->fg_work);

	return;
exit:
	dev_err(di->dev,
		"Failed to read or write gas gauge registers\n");
	mutex_unlock(&di->cc_lock);
	queue_work(di->fg_wq, &di->fg_work);
}

/**
 * ab5500_fg_bat_voltage() - get battery voltage
 * @di:		pointer to the ab5500_fg structure
 *
 * Returns battery voltage(on success) else error code
 */
static int ab5500_fg_bat_voltage(struct ab5500_fg *di)
{
	int vbat;
	static int prev;

	vbat = ab5500_gpadc_convert(di->gpadc, MAIN_BAT_V);
	if (vbat < 0) {
		dev_err(di->dev,
			"%s gpadc conversion failed, using previous value\n",
			__func__);
		return prev;
	}

	prev = vbat;
	return vbat;
}

/**
 * ab5500_fg_volt_to_capacity() - Voltage based capacity
 * @di:		pointer to the ab5500_fg structure
 * @voltage:	The voltage to convert to a capacity
 *
 * Returns battery capacity in per mille based on voltage
 */
static int ab5500_fg_volt_to_capacity(struct ab5500_fg *di, int voltage)
{
	int i, tbl_size;
	struct abx500_v_to_cap *tbl;
	int cap = 0;

	tbl = di->bat->bat_type[di->bat->batt_id].v_to_cap_tbl,
	tbl_size = di->bat->bat_type[di->bat->batt_id].n_v_cap_tbl_elements;

	for (i = 0; i < tbl_size; ++i) {
		if (di->vbat < tbl[i].voltage && di->vbat > tbl[i+1].voltage)
			di->v_to_cap = tbl[i].capacity;
	}

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
 * ab5500_fg_uncomp_volt_to_capacity() - Uncompensated voltage based capacity
 * @di:		pointer to the ab5500_fg structure
 *
 * Returns battery capacity based on battery voltage that is not compensated
 * for the voltage drop due to the load
 */
static int ab5500_fg_uncomp_volt_to_capacity(struct ab5500_fg *di)
{
	di->vbat = ab5500_fg_bat_voltage(di);
	return ab5500_fg_volt_to_capacity(di, di->vbat);
}

/**
 * ab5500_fg_load_comp_volt_to_capacity() - Load compensated voltage based capacity
 * @di:		pointer to the ab5500_fg structure
 *
 * Returns battery capacity based on battery voltage that is load compensated
 * for the voltage drop
 */
static int ab5500_fg_load_comp_volt_to_capacity(struct ab5500_fg *di)
{
	int vbat_comp;

	di->inst_curr = ab5500_fg_inst_curr(di);
	di->vbat = ab5500_fg_bat_voltage(di);

	/* Use Ohms law to get the load compensated voltage */
	vbat_comp = di->vbat - (di->inst_curr *
		di->bat->bat_type[di->bat->batt_id].battery_resistance) / 1000;

	dev_dbg(di->dev, "%s Measured Vbat: %dmV,Compensated Vbat %dmV, "
		"R: %dmOhm, Current: %dmA\n",
		__func__,
		di->vbat,
		vbat_comp,
		di->bat->bat_type[di->bat->batt_id].battery_resistance,
		di->inst_curr);

	return ab5500_fg_volt_to_capacity(di, vbat_comp);
}

/**
 * ab5500_fg_convert_mah_to_permille() - Capacity in mAh to permille
 * @di:		pointer to the ab5500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in permille
 */
static int ab5500_fg_convert_mah_to_permille(struct ab5500_fg *di, int cap_mah)
{
	return (cap_mah * 1000) / di->bat_cap.max_mah_design;
}

/**
 * ab5500_fg_convert_permille_to_mah() - Capacity in permille to mAh
 * @di:		pointer to the ab5500_fg structure
 * @cap_pm:	capacity in permille
 *
 * Converts capacity in permille to capacity in mAh
 */
static int ab5500_fg_convert_permille_to_mah(struct ab5500_fg *di, int cap_pm)
{
	return cap_pm * di->bat_cap.max_mah_design / 1000;
}

/**
 * ab5500_fg_convert_mah_to_uwh() - Capacity in mAh to uWh
 * @di:		pointer to the ab5500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in uWh
 */
static int ab5500_fg_convert_mah_to_uwh(struct ab5500_fg *di, int cap_mah)
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
 * ab5500_fg_calc_cap_charging() - Calculate remaining capacity while charging
 * @di:		pointer to the ab5500_fg structure
 *
 * Return the capacity in mAh based on previous calculated capcity and the FG
 * accumulator register value. The filter is filled with this capacity
 */
static int ab5500_fg_calc_cap_charging(struct ab5500_fg *di)
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
		di->flags.fully_charged)
		di->bat_cap.mah = di->bat_cap.max_mah_design;

	ab5500_fg_fill_cap_sample(di, di->bat_cap.mah);
	di->bat_cap.permille =
		ab5500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

	/* We need to update battery voltage and inst current when charging */
	di->vbat = ab5500_fg_bat_voltage(di);
	di->inst_curr = ab5500_fg_inst_curr(di);

	return di->bat_cap.mah;
}

/**
 * ab5500_fg_calc_cap_discharge_voltage() - Capacity in discharge with voltage
 * @di:		pointer to the ab5500_fg structure
 * @comp:	if voltage should be load compensated before capacity calc
 *
 * Return the capacity in mAh based on the battery voltage. The voltage can
 * either be load compensated or not. This value is added to the filter and a
 * new mean value is calculated and returned.
 */
static int ab5500_fg_calc_cap_discharge_voltage(struct ab5500_fg *di, bool comp)
{
	int permille, mah;

	if (comp)
		permille = ab5500_fg_load_comp_volt_to_capacity(di);
	else
		permille = ab5500_fg_uncomp_volt_to_capacity(di);

	mah = ab5500_fg_convert_permille_to_mah(di, permille);

	di->bat_cap.mah = ab5500_fg_add_cap_sample(di, mah);
	di->bat_cap.permille =
		ab5500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

	return di->bat_cap.mah;
}

/**
 * ab5500_fg_calc_cap_discharge_fg() - Capacity in discharge with FG
 * @di:		pointer to the ab5500_fg structure
 *
 * Return the capacity in mAh based on previous calculated capcity and the FG
 * accumulator register value. This value is added to the filter and a
 * new mean value is calculated and returned.
 */
static int ab5500_fg_calc_cap_discharge_fg(struct ab5500_fg *di)
{
	int permille_volt, permille;

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

	/*
	 * Check against voltage based capacity. It can not be lower
	 * than what the uncompensated voltage says
	 */
	permille = ab5500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
	permille_volt = ab5500_fg_uncomp_volt_to_capacity(di);

	if (permille < permille_volt) {
		di->bat_cap.permille = permille_volt;
		di->bat_cap.mah = ab5500_fg_convert_permille_to_mah(di,
			di->bat_cap.permille);

		dev_dbg(di->dev, "%s voltage based: perm %d perm_volt %d\n",
			__func__,
			permille,
			permille_volt);

		ab5500_fg_fill_cap_sample(di, di->bat_cap.mah);
	} else {
		ab5500_fg_fill_cap_sample(di, di->bat_cap.mah);
		di->bat_cap.permille =
			ab5500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
	}

	return di->bat_cap.mah;
}

/**
 * ab5500_fg_capacity_level() - Get the battery capacity level
 * @di:		pointer to the ab5500_fg structure
 *
 * Get the battery capacity level based on the capacity in percent
 */
static int ab5500_fg_capacity_level(struct ab5500_fg *di)
{
	int ret, percent;

	percent = di->bat_cap.permille / 10;

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
 * ab5500_fg_check_capacity_limits() - Check if capacity has changed
 * @di:		pointer to the ab5500_fg structure
 * @init:	capacity is allowed to go up in init mode
 *
 * Check if capacity or capacity limit has changed and notify the system
 * about it using the power_supply framework
 */
static void ab5500_fg_check_capacity_limits(struct ab5500_fg *di, bool init)
{
	bool changed = false;

	di->bat_cap.level = ab5500_fg_capacity_level(di);

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

	/*
	 * If we have received the LOW_BAT IRQ, set capacity to 0 to initiate
	 * shutdown
	 */
	if (di->flags.low_bat) {
		dev_dbg(di->dev, "Battery low, set capacity to 0\n");
		di->bat_cap.prev_percent = 0;
		di->bat_cap.permille = 0;
		di->bat_cap.prev_mah = 0;
		di->bat_cap.mah = 0;
		changed = true;
	} else if (di->bat_cap.prev_percent != di->bat_cap.permille / 10) {
		if (di->bat_cap.permille / 10 == 0) {
			/*
			 * We will not report 0% unless we've got
			 * the LOW_BAT IRQ, no matter what the FG
			 * algorithm says.
			 */
			di->bat_cap.prev_percent = 1;
			di->bat_cap.permille = 1;
			di->bat_cap.prev_mah = 1;
			di->bat_cap.mah = 1;

			changed = true;
		} else if (!(!di->flags.charging &&
			(di->bat_cap.permille / 10) >
			di->bat_cap.prev_percent) || init) {
			/*
			 * We do not allow reported capacity to go up
			 * unless we're charging or if we're in init
			 */
			dev_dbg(di->dev,
				"capacity changed from %d to %d (%d)\n",
				di->bat_cap.prev_percent,
				di->bat_cap.permille / 10,
				di->bat_cap.permille);
			di->bat_cap.prev_percent = di->bat_cap.permille / 10;
			di->bat_cap.prev_mah = di->bat_cap.mah;

			changed = true;
		} else {
			dev_dbg(di->dev, "capacity not allowed to go up since "
				"no charger is connected: %d to %d (%d)\n",
				di->bat_cap.prev_percent,
				di->bat_cap.permille / 10,
				di->bat_cap.permille);
		}
	}

	if (changed)
		power_supply_changed(&di->fg_psy);

}

static void ab5500_fg_charge_state_to(struct ab5500_fg *di,
	enum ab5500_fg_charge_state new_state)
{
	dev_dbg(di->dev, "Charge state from %d [%s] to %d [%s]\n",
		di->charge_state,
		charge_state[di->charge_state],
		new_state,
		charge_state[new_state]);

	di->charge_state = new_state;
}

static void ab5500_fg_discharge_state_to(struct ab5500_fg *di,
	enum ab5500_fg_charge_state new_state)
{
	dev_dbg(di->dev, "Disharge state from %d [%s] to %d [%s]\n",
		di->discharge_state,
		discharge_state[di->discharge_state],
		new_state,
		discharge_state[new_state]);

	di->discharge_state = new_state;
}

/**
 * ab5500_fg_algorithm_charging() - FG algorithm for when charging
 * @di:		pointer to the ab5500_fg structure
 *
 * Battery capacity calculation state machine for when we're charging
 */
static void ab5500_fg_algorithm_charging(struct ab5500_fg *di)
{
	/*
	 * If we change to discharge mode
	 * we should start with recovery
	 */
	if (di->discharge_state != AB5500_FG_DISCHARGE_INIT_RECOVERY)
		ab5500_fg_discharge_state_to(di,
			AB5500_FG_DISCHARGE_INIT_RECOVERY);

	switch (di->charge_state) {
	case AB5500_FG_CHARGE_INIT:
		di->fg_samples = SEC_TO_SAMPLE(
			di->bat->fg_params->accu_charging);

		ab5500_fg_coulomb_counter(di, true);
		ab5500_fg_charge_state_to(di, AB5500_FG_CHARGE_READOUT);

		break;

	case AB5500_FG_CHARGE_READOUT:
		/*
		 * Read the FG and calculate the new capacity
		 */
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

		ab5500_fg_calc_cap_charging(di);

		break;

	default:
		break;
	}

	/* Check capacity limits */
	ab5500_fg_check_capacity_limits(di, false);
}

/**
 * ab5500_fg_algorithm_discharging() - FG algorithm for when discharging
 * @di:		pointer to the ab5500_fg structure
 *
 * Battery capacity calculation state machine for when we're discharging
 */
static void ab5500_fg_algorithm_discharging(struct ab5500_fg *di)
{
	int sleep_time;

	/* If we change to charge mode we should start with init */
	if (di->charge_state != AB5500_FG_CHARGE_INIT)
		ab5500_fg_charge_state_to(di, AB5500_FG_CHARGE_INIT);

	switch (di->discharge_state) {
	case AB5500_FG_DISCHARGE_INIT:
		/* We use the FG IRQ to work on */
		di->init_cnt = 0;
		di->fg_samples = SEC_TO_SAMPLE(di->bat->fg_params->init_timer);
		ab5500_fg_coulomb_counter(di, true);
		ab5500_fg_discharge_state_to(di,
			AB5500_FG_DISCHARGE_INITMEASURING);

		/* Intentional fallthrough */
	case AB5500_FG_DISCHARGE_INITMEASURING:
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

			ab5500_fg_calc_cap_discharge_voltage(di, true);

			ab5500_fg_check_capacity_limits(di, true);
		}

		di->init_cnt += sleep_time;
		if (di->init_cnt >
			di->bat->fg_params->init_total_time) {
			di->fg_samples = SEC_TO_SAMPLE(
				di->bat->fg_params->accu_high_curr);

			ab5500_fg_coulomb_counter(di, true);
			ab5500_fg_discharge_state_to(di,
				AB5500_FG_DISCHARGE_READOUT);
		}

		break;

	case AB5500_FG_DISCHARGE_INIT_RECOVERY:
		di->recovery_cnt = 0;
		di->recovery_needed = true;
		ab5500_fg_discharge_state_to(di,
			AB5500_FG_DISCHARGE_RECOVERY);

		/* Intentional fallthrough */

	case AB5500_FG_DISCHARGE_RECOVERY:
		sleep_time = di->bat->fg_params->recovery_sleep_timer;

		/*
		 * We should check the power consumption
		 * If low, go to READOUT (after x min) or
		 * RECOVERY_SLEEP if time left.
		 * If high, go to READOUT
		 */
		di->inst_curr = ab5500_fg_inst_curr(di);

		if (ab5500_fg_is_low_curr(di, di->inst_curr)) {
			if (di->recovery_cnt >
				di->bat->fg_params->recovery_total_time) {
				di->fg_samples = SEC_TO_SAMPLE(
					di->bat->fg_params->accu_high_curr);
				ab5500_fg_coulomb_counter(di, true);
				ab5500_fg_discharge_state_to(di,
					AB5500_FG_DISCHARGE_READOUT);
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
			ab5500_fg_coulomb_counter(di, true);
			ab5500_fg_discharge_state_to(di,
				AB5500_FG_DISCHARGE_READOUT);
		}

		break;

	case AB5500_FG_DISCHARGE_READOUT:
		di->inst_curr = ab5500_fg_inst_curr(di);

		if (ab5500_fg_is_low_curr(di, di->inst_curr)) {
			/* Detect mode change */
			if (di->high_curr_mode) {
				di->high_curr_mode = false;
				di->high_curr_cnt = 0;
			}

			if (di->recovery_needed) {
				ab5500_fg_discharge_state_to(di,
					AB5500_FG_DISCHARGE_RECOVERY);

				queue_delayed_work(di->fg_wq,
					&di->fg_periodic_work,
					0);

				break;
			}

			ab5500_fg_calc_cap_discharge_voltage(di, true);
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

			ab5500_fg_calc_cap_discharge_fg(di);
		}

		ab5500_fg_check_capacity_limits(di, false);

		break;

	case AB5500_FG_DISCHARGE_WAKEUP:
		ab5500_fg_coulomb_counter(di, true);
		di->inst_curr = ab5500_fg_inst_curr(di);

		ab5500_fg_calc_cap_discharge_voltage(di, true);

		di->fg_samples = SEC_TO_SAMPLE(
			di->bat->fg_params->accu_high_curr);
		/* Re-program number of samples set above */
		ab5500_fg_coulomb_counter(di, true);
		ab5500_fg_discharge_state_to(di, AB5500_FG_DISCHARGE_READOUT);

		ab5500_fg_check_capacity_limits(di, false);

		break;

	default:
		break;
	}
}

/**
 * ab5500_fg_algorithm_calibrate() - Internal columb counter offset calibration
 * @di:		pointer to the ab5500_fg structure
 *
 */
static void ab5500_fg_algorithm_calibrate(struct ab5500_fg *di)
{
	int ret;

	switch (di->calib_state) {
	case AB5500_FG_CALIB_INIT:
		dev_dbg(di->dev, "Calibration ongoing...\n");
		/* TODO: For Cut 1.1 no calibration */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_CONTROL_A,
			FG_ACC_RESET_ON_READ_MASK, FG_ACC_RESET_ON_READ);
		if (ret)
			goto err;
		di->calib_state = AB5500_FG_CALIB_WAIT;
		break;
	case AB5500_FG_CALIB_END:
		di->flags.calibrate = false;
		dev_dbg(di->dev, "Calibration done...\n");
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
		break;
	case AB5500_FG_CALIB_WAIT:
		dev_dbg(di->dev, "Calibration WFI\n");
	default:
		break;
	}
	return;
err:
	/* Something went wrong, don't calibrate then */
	dev_err(di->dev, "failed to calibrate the CC\n");
	di->flags.calibrate = false;
	di->calib_state = AB5500_FG_CALIB_INIT;
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
}

/**
 * ab5500_fg_algorithm() - Entry point for the FG algorithm
 * @di:		pointer to the ab5500_fg structure
 *
 * Entry point for the battery capacity calculation state machine
 */
static void ab5500_fg_algorithm(struct ab5500_fg *di)
{
	if (di->flags.calibrate)
		ab5500_fg_algorithm_calibrate(di);
	else {
		if (di->flags.charging)
			ab5500_fg_algorithm_charging(di);
		else
			ab5500_fg_algorithm_discharging(di);
	}

	dev_dbg(di->dev, "[FG_DATA] %d %d %d %d %d %d %d %d %d "
		"%d %d %d %d %d %d %d\n",
		di->bat_cap.max_mah_design,
		di->bat_cap.mah,
		di->bat_cap.permille,
		di->bat_cap.level,
		di->bat_cap.prev_mah,
		di->bat_cap.prev_percent,
		di->bat_cap.prev_level,
		di->vbat,
		di->inst_curr,
		di->avg_curr,
		di->accu_charge,
		di->flags.charging,
		di->charge_state,
		di->discharge_state,
		di->high_curr_mode,
		di->recovery_needed);
}

/**
 * ab5500_fg_periodic_work() - Run the FG state machine periodically
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for periodic work
 */
static void ab5500_fg_periodic_work(struct work_struct *work)
{
	struct ab5500_fg *di = container_of(work, struct ab5500_fg,
		fg_periodic_work.work);

	if (di->init_capacity) {
		/* A dummy read that will return 0 */
		di->inst_curr = ab5500_fg_inst_curr(di);
		/* Get an initial capacity calculation */
		ab5500_fg_calc_cap_discharge_voltage(di, true);
		ab5500_fg_check_capacity_limits(di, true);
		di->init_capacity = false;
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	} else
		ab5500_fg_algorithm(di);
}

/**
 * ab5500_fg_low_bat_work() - Check LOW_BAT condition
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the LOW_BAT condition
 */
static void ab5500_fg_low_bat_work(struct work_struct *work)
{
	int vbat;

	struct ab5500_fg *di = container_of(work, struct ab5500_fg,
		fg_low_bat_work.work);

	vbat = ab5500_fg_bat_voltage(di);

	/* Check if LOW_BAT still fulfilled */
	if (vbat < di->bat->fg_params->lowbat_threshold) {
		di->flags.low_bat = true;
		dev_warn(di->dev, "Battery voltage still LOW\n");

		/*
		 * We need to re-schedule this check to be able to detect
		 * if the voltage increases again during charging
		 */
		queue_delayed_work(di->fg_wq, &di->fg_low_bat_work,
			round_jiffies(LOW_BAT_CHECK_INTERVAL));
		power_supply_changed(&di->fg_psy);
	} else {
		di->flags.low_bat = false;
		dev_warn(di->dev, "Battery voltage OK again\n");
		power_supply_changed(&di->fg_psy);
	}

	/* This is needed to dispatch LOW_BAT */
	ab5500_fg_check_capacity_limits(di, false);

	/* Set this flag to check if LOW_BAT IRQ still occurs */
	di->flags.low_bat_delay = false;
}

/**
 * ab5500_fg_instant_work() - Run the FG state machine instantly
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for instant work
 */
static void ab5500_fg_instant_work(struct work_struct *work)
{
	struct ab5500_fg *di = container_of(work, struct ab5500_fg, fg_work);

	ab5500_fg_algorithm(di);
}

/**
 * ab5500_fg_get_property() - get the fg properties
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
static int ab5500_fg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab5500_fg *di;

	di = to_ab5500_fg_device_info(psy);

	/*
	 * If battery is identified as unknown and charging of unknown
	 * batteries is disabled, we always report 100% capacity and
	 * capacity level UNKNOWN, since we can't calculate
	 * remaining capacity
	 */

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (di->flags.bat_ovv)
			val->intval = 47500000;
		else {
			di->vbat = ab5500_gpadc_convert
					(di->gpadc, MAIN_BAT_V);
			val->intval = di->vbat * 1000;
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		di->inst_curr = ab5500_fg_inst_curr(di);
		val->intval = di->inst_curr * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = di->avg_curr * 1000;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = ab5500_fg_convert_mah_to_uwh(di,
				di->bat_cap.max_mah_design);
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = ab5500_fg_convert_mah_to_uwh(di,
				di->bat_cap.max_mah);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (di->flags.batt_unknown && !di->bat->chg_unknown_bat)
			val->intval = ab5500_fg_convert_mah_to_uwh(di,
					di->bat_cap.max_mah);
		else
			val->intval = ab5500_fg_convert_mah_to_uwh(di,
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
		if (di->flags.batt_unknown && !di->bat->chg_unknown_bat)
			val->intval = 100;
		else
			val->intval = di->bat_cap.prev_percent;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (di->flags.batt_unknown && !di->bat->chg_unknown_bat)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		else
			val->intval = di->bat_cap.prev_level;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ab5500_fg_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab5500_fg *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_ab5500_fg_device_info(psy);

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

		switch (prop) {
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
					queue_work(di->fg_wq, &di->fg_work);
					break;
				case POWER_SUPPLY_STATUS_FULL:
					if (di->flags.fully_charged)
						break;
					di->flags.fully_charged = true;
					/* Save current capacity as maximum */
					di->bat_cap.max_mah = di->bat_cap.mah;
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
 * ab5500_fg_init_hw_registers() - Set up FG related registers
 * @di:		pointer to the ab5500_fg structure
 *
 * Set up battery OVV, low battery voltage registers
 */
static int ab5500_fg_init_hw_registers(struct ab5500_fg *di)
{
	int ret;
	struct adc_auto_input *auto_ip;

	auto_ip = kzalloc(sizeof(struct adc_auto_input), GFP_KERNEL);
	if (!auto_ip) {
		dev_err(di->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	auto_ip->mux = MAIN_BAT_V;
	auto_ip->freq = MS500;
	auto_ip->min = di->bat->fg_params->lowbat_threshold;
	auto_ip->max = di->bat->fg_params->overbat_threshold;
	auto_ip->auto_adc_callback = ab5500_fg_bat_v_trig;
	di->gpadc_auto = auto_ip;
	ret = ab5500_gpadc_convert_auto(di->gpadc, di->gpadc_auto);
	if (ret)
		dev_err(di->dev,
			"failed to set auto trigger for battery votlage\n");
	/* set End Of Charge current to 247mA */
	ret = abx500_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_FG_EOC, EOC_52_mA);
	return ret;
}

static int ab5500_fg_bat_v_trig(int mux)
{
	struct ab5500_fg *di = ab5500_fg_get();

	di->vbat = ab5500_gpadc_convert(di->gpadc, MAIN_BAT_V);

	/* check if the battery voltage is below low threshold */
	if (di->vbat < di->bat->fg_params->lowbat_threshold) {
		dev_warn(di->dev, "Battery voltage is below LOW threshold\n");
		di->flags.low_bat_delay = true;
		/*
		 * Start a timer to check LOW_BAT again after some time
		 * This is done to avoid shutdown on single voltage dips
		 */
		queue_delayed_work(di->fg_wq, &di->fg_low_bat_work,
			round_jiffies(LOW_BAT_CHECK_INTERVAL));
		power_supply_changed(&di->fg_psy);
	}
	/* check if battery votlage is above OVV */
	else if (di->vbat > di->bat->fg_params->overbat_threshold) {
		dev_warn(di->dev, "Battery OVV\n");
		di->flags.bat_ovv = true;

		power_supply_changed(&di->fg_psy);
	} else
		dev_err(di->dev,
			"Invalid gpadc auto trigger for battery voltage\n");

	kfree(di->gpadc_auto);
	ab5500_fg_init_hw_registers(di);
	return 0;
}

/**
 * ab5500_fg_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is the entry point of the pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in any external power
 * supply that this driver needs to be notified of.
 */
static void ab5500_fg_external_power_changed(struct power_supply *psy)
{
	struct ab5500_fg *di = to_ab5500_fg_device_info(psy);

	class_for_each_device(power_supply_class, NULL,
		&di->fg_psy, ab5500_fg_get_ext_psy_data);
}

/**
 * abab5500_fg_reinit_work() - work to reset the FG algorithm
 * @work:      pointer to the work_struct structure
 *
 * Used to reset the current battery capacity to be able to
 * retrigger a new voltage base capacity calculation. For
 * test and verification purpose.
 */
static void ab5500_fg_reinit_work(struct work_struct *work)
{
	struct ab5500_fg *di = container_of(work, struct ab5500_fg,
			fg_reinit_work.work);

	if (di->flags.calibrate == false) {
		dev_dbg(di->dev, "Resetting FG state machine to init.\n");
		ab5500_fg_clear_cap_samples(di);
		ab5500_fg_calc_cap_discharge_voltage(di, true);
		ab5500_fg_charge_state_to(di, AB5500_FG_CHARGE_INIT);
		ab5500_fg_discharge_state_to(di, AB5500_FG_DISCHARGE_INIT);
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);

	} else {
		dev_err(di->dev,
			"Residual offset calibration ongoing retrying..\n");
		/* Wait one second until next try*/
		queue_delayed_work(di->fg_wq, &di->fg_reinit_work,
							round_jiffies(1));
	}
}

/**
 * ab5500_fg_reinit() - forces FG algorithm to reinitialize with current values
 *
 * This function can be used to force the FG algorithm to recalculate a new
 * voltage based battery capacity.
 */
void ab5500_fg_reinit(void)
{
	struct ab5500_fg *di = ab5500_fg_get();
	/* User won't be notified if a null pointer returned. */
	if (di != NULL)
		queue_delayed_work(di->fg_wq, &di->fg_reinit_work, 0);
}

#if defined(CONFIG_PM)
static int ab5500_fg_resume(struct platform_device *pdev)
{
	struct ab5500_fg *di = platform_get_drvdata(pdev);

	/*
	 * Change state if we're not charging. If we're charging we will wake
	 * up on the FG IRQ
	 */
	if (!di->flags.charging) {
		ab5500_fg_discharge_state_to(di, AB5500_FG_DISCHARGE_WAKEUP);
		queue_work(di->fg_wq, &di->fg_work);
	}

	return 0;
}

static int ab5500_fg_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab5500_fg *di = platform_get_drvdata(pdev);

	flush_delayed_work(&di->fg_periodic_work);

	/*
	 * If the FG is enabled we will disable it before going to suspend
	 * only if we're not charging
	 */
	if (di->flags.fg_enabled && !di->flags.charging)
		ab5500_fg_coulomb_counter(di, false);

	return 0;
}
#else
#define ab5500_fg_suspend      NULL
#define ab5500_fg_resume       NULL
#endif

static int __devexit ab5500_fg_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct ab5500_fg *di = platform_get_drvdata(pdev);

	/* Disable coulomb counter */
	ret = ab5500_fg_coulomb_counter(di, false);
	if (ret)
		dev_err(di->dev, "failed to disable coulomb counter\n");

	destroy_workqueue(di->fg_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->fg_psy);
	platform_set_drvdata(pdev, NULL);
	kfree(di->gpadc_auto);
	kfree(di);
	return ret;
}

static int __devinit ab5500_fg_probe(struct platform_device *pdev)
{
	struct abx500_bm_plat_data *plat_data;
	int ret = 0;

	struct ab5500_fg *di =
		kzalloc(sizeof(struct ab5500_fg), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	mutex_init(&di->cc_lock);

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab5500_gpadc_get("ab5500-adc.0");

	plat_data = pdev->dev.platform_data;
	di->pdata = plat_data->fg;
	di->bat = plat_data->battery;

	/* get fg specific platform data */
	if (!di->pdata) {
		dev_err(di->dev, "no fg platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}

	/* get battery specific platform data */
	if (!di->bat) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	/* powerup fg to start sampling */
	ab5500_fg_coulomb_counter(di, true);

	di->fg_psy.name = "ab5500_fg";
	di->fg_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->fg_psy.properties = ab5500_fg_props;
	di->fg_psy.num_properties = ARRAY_SIZE(ab5500_fg_props);
	di->fg_psy.get_property = ab5500_fg_get_property;
	di->fg_psy.supplied_to = di->pdata->supplied_to;
	di->fg_psy.num_supplicants = di->pdata->num_supplicants;
	di->fg_psy.external_power_changed = ab5500_fg_external_power_changed;

	di->bat_cap.max_mah_design = MILLI_TO_MICRO *
		di->bat->bat_type[di->bat->batt_id].charge_full_design;

	di->bat_cap.max_mah = di->bat_cap.max_mah_design;

	di->vbat_nom = di->bat->bat_type[di->bat->batt_id].nominal_voltage;

	di->init_capacity = true;

	ab5500_fg_charge_state_to(di, AB5500_FG_CHARGE_INIT);
	ab5500_fg_discharge_state_to(di, AB5500_FG_DISCHARGE_INIT);

	/* Create a work queue for running the FG algorithm */
	di->fg_wq = create_singlethread_workqueue("ab5500_fg_wq");
	if (di->fg_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for running the fg algorithm instantly */
	INIT_WORK(&di->fg_work, ab5500_fg_instant_work);

	/* Init work for getting the battery accumulated current */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_acc_cur_work,
			ab5500_fg_acc_cur_work);

	/* Init work for reinitialising the fg algorithm */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_reinit_work,
			ab5500_fg_reinit_work);

	/* Work delayed Queue to run the state machine */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_periodic_work,
		ab5500_fg_periodic_work);

	/* Work to check low battery condition */
	INIT_DELAYED_WORK_DEFERRABLE(&di->fg_low_bat_work,
		ab5500_fg_low_bat_work);

	list_add_tail(&di->node, &ab5500_fg_list);

	/* Consider battery unknown until we're informed otherwise */
	di->flags.batt_unknown = true;

	/* Register FG power supply class */
	ret = power_supply_register(di->dev, &di->fg_psy);
	if (ret) {
		dev_err(di->dev, "failed to register FG psy\n");
		goto free_fg_wq;
	}

	/* Initialize OVV, and other registers */
	ret = ab5500_fg_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize registers\n");
		goto pow_unreg;
	}

	di->fg_samples = SEC_TO_SAMPLE(di->bat->fg_params->init_timer);

	/* Initilialize avg current timer */
	init_timer(&di->avg_current_timer);
	di->avg_current_timer.function = ab5500_fg_acc_cur_timer_expired;
	di->avg_current_timer.data = (unsigned long) di;
	di->avg_current_timer.expires = 60 * HZ;
	if (!timer_pending(&di->avg_current_timer))
		add_timer(&di->avg_current_timer);
	else
		mod_timer(&di->avg_current_timer, 60 * HZ);

	platform_set_drvdata(pdev, di);

	/* Calibrate the fg first time */
	di->flags.calibrate = true;
	di->calib_state = AB5500_FG_CALIB_INIT;
	/* Run the FG algorithm */
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work,
			FG_PERIODIC_START_INTERVAL);
	queue_delayed_work(di->fg_wq, &di->fg_acc_cur_work,
			FG_PERIODIC_START_INTERVAL);

	dev_info(di->dev, "probe success\n");
	return ret;

pow_unreg:
	power_supply_unregister(&di->fg_psy);
free_fg_wq:
	destroy_workqueue(di->fg_wq);
free_device_info:
	kfree(di);

	return ret;
}

static struct platform_driver ab5500_fg_driver = {
	.probe = ab5500_fg_probe,
	.remove = __devexit_p(ab5500_fg_remove),
	.suspend = ab5500_fg_suspend,
	.resume = ab5500_fg_resume,
	.driver = {
		.name = "ab5500-fg",
		.owner = THIS_MODULE,
	},
};

static int __init ab5500_fg_init(void)
{
	return platform_driver_register(&ab5500_fg_driver);
}

static void __exit ab5500_fg_exit(void)
{
	platform_driver_unregister(&ab5500_fg_driver);
}

subsys_initcall_sync(ab5500_fg_init);
module_exit(ab5500_fg_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab5500-fg");
MODULE_DESCRIPTION("AB5500 Fuel Gauge driver");
