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
#include <linux/kobject.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/slab.h>
#include <linux/mfd/abx500/ab8500-bm.h>

#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>
#include <linux/wakelock.h>

#include <linux/delay.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500.h>
#include <linux/time.h>
#include <linux/completion.h>
#include <linux/kernel.h>

#include <linux/regulator/consumer.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <mach/board-sec-ux500.h>
#include <mach/sec_param.h>
#include <linux/kernel.h>

#include <linux/battery/fuelgauge/abb_fuelgauge.h>

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
static int ab8500_fg_is_low_curr(struct ab8500_fuelgauge_info *di, int curr)
{
	/*
	 * We want to know if we're in low current mode
	 */
	if (curr > -get_battery_data(di).fg_params->high_curr_threshold)
		return true;
	else
		return false;
}

/* Voltage based capacity */
static void ab8500_fg_fill_vcap_sample(struct ab8500_fuelgauge_info *di,
				       int sample)
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

static int ab8500_fg_add_vcap_sample(struct ab8500_fuelgauge_info *di,
				     int sample)
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

static void ab8500_fg_fill_i_sample(struct ab8500_fuelgauge_info *di,
				    int sample)
{
	int i;
	struct timespec ts;
	struct ab8500_fg_instant_current *avg = &di->inst_cur;
	getnstimeofday(&ts);

	for (i = 0; i < NBR_CURR_SAMPLES; i++) {
		avg->samples[i] = 0;
		avg->time_stamps[i] = ts.tv_sec;
	}
	dev_dbg(di->dev, "Filled curr sample: 0, pre_sample : %d\n", sample);
	avg->pos = 0;
	avg->nbr_samples = 1;
	avg->sum = 0;
	avg->avg = 0;
	avg->pre_sample = sample;
}

static void ab8500_fg_add_i_sample(struct ab8500_fuelgauge_info *di,
				   int sample)
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

	dev_dbg(di->dev, "Added curr sample: %d, diff:%d, average: %d,"
		" high_diff_nbr: %d\n",
		sample, diff, avg->avg, avg->high_diff_nbr);

	avg->avg = avg->sum / avg->nbr_samples;

}

/**
 * ab8500_fg_add_cap_sample() - Add capacity to average filter
 * @di:		pointer to the ab8500_fg structure
 * @sample:	the capacity in mAh to add to the filter
 *
 * A capacity is added to the filter and a new mean capacity is calculated and
 * returned
 */
static int ab8500_fg_add_cap_sample(struct ab8500_fuelgauge_info *di,
				    int sample)
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
 * @di:		pointer to the ab8500_fg structure
 *
 * The capacity filter is is reset to zero.
 */
static void ab8500_fg_clear_cap_samples(struct ab8500_fuelgauge_info *di)
{
	int i;
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

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
 * ab8500_fg_fill_cap_sample() - Fill average filter
 * @di:		pointer to the ab8500_fg structure
 * @sample:	the capacity in mAh to fill the filter with
 *
 * The capacity filter is filled with a capacity in mAh
 */
static void ab8500_fg_fill_cap_sample(struct ab8500_fuelgauge_info *di,
				      int sample)
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
static int ab8500_fg_coulomb_counter(struct ab8500_fuelgauge_info *di,
				     bool enable)
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
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			(RESET_ACCU | READ_REQ), 0);
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
int ab8500_fg_inst_curr_start(struct ab8500_fuelgauge_info *di)
{
	u8 reg_val;
	int ret;

	mutex_lock(&di->cc_lock);
	dev_dbg(di->dev, "Inst curr start\n");

	di->nbr_cceoc_irq_cnt = 0;
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
	INIT_COMPLETION(di->ab8500_fg_started);
	INIT_COMPLETION(di->ab8500_fg_complete);
	enable_irq(di->irq);

	/* Note: cc_lock is still locked */
	return 0;
fail:
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr_started() - check if fg conversion has started
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 1 if conversion started, 0 if still waiting
 */
int ab8500_fg_inst_curr_started(struct ab8500_fuelgauge_info *di)
{
	return completion_done(&di->ab8500_fg_started);
}

/**
 * ab8500_fg_inst_curr_done() - check if fg conversion is done
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 1 if conversion done, 0 if still waiting
 */
int ab8500_fg_inst_curr_done(struct ab8500_fuelgauge_info *di)
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
int ab8500_fg_inst_curr_finalize(struct ab8500_fuelgauge_info *di,
				 int *res)
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
			di->nbr_cceoc_irq_cnt = 0;
			dev_err(di->dev, "completion timed out [%d]\n",
				__LINE__);
			goto fail;
		}
	}

	disable_irq(di->irq);
	di->nbr_cceoc_irq_cnt = 0;

	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			READ_REQ, READ_REQ);

	/* 100uS between read request and read is needed */
	usleep_range(100, 100);

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
		(1000 * di->fg_res);

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
 * ab8500_fg_inst_curr_blocking() - battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 * @res:	battery instantenous current(on success)
 *
 * Returns 0 else error code
 */
int ab8500_fg_inst_curr_blocking(struct ab8500_fuelgauge_info *di)
{
	int ret;
	int timeout;
	int res = 0;
	dev_dbg(di->dev, "Inst curr blocking\n");
	ret = ab8500_fg_inst_curr_start(di);
	if (ret) {
		dev_err(di->dev, "Failed to initialize fg_inst\n");
		return 0;
	}

	/* Wait for CC to actually start */
	if (!completion_done(&di->ab8500_fg_started)) {
		timeout = wait_for_completion_timeout(
			&di->ab8500_fg_started,
			INS_CURR_TIMEOUT);
		dev_dbg(di->dev, "Start time: %d ms\n",
			((INS_CURR_TIMEOUT - timeout) * 1000) / HZ);
		if (!timeout) {
			ret = -ETIME;
			dev_err(di->dev, "completion timed out [%d]\n",
				__LINE__);
			goto fail;
		}
	}

	ret = ab8500_fg_inst_curr_finalize(di, &res);
	if (ret) {
		dev_err(di->dev, "Failed to finalize fg_inst\n");
		return 0;
	}

	dev_dbg(di->dev, "%s instant current: %d", __func__, res);
	return res;
fail:
	disable_irq(di->irq);
	mutex_unlock(&di->cc_lock);
	return ret;
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

	struct ab8500_fuelgauge_info *di = container_of(work,
		struct ab8500_fuelgauge_info, fg_acc_cur_work);

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
		(100 * di->fg_res);

	/*
	 * Convert to unit value in mA
	 * by dividing by the conversion
	 * time in hours (= samples / (3600 * 4)h)
	 * and multiply with 1000
	 */

	di->avg_curr = (di->accu_charge * 36) /
			((di->fg_samples / 4) * 10);

	di->flags.conv_done = true;

	mutex_unlock(&di->cc_lock);

	queue_work(di->fg_wq, &di->fg_work);

	dev_dbg(di->dev, "fg_res: %d, fg_samples: %d, gasg: %d,"
		"accu_charge: %d\n",
		di->fg_res, di->fg_samples, val, di->accu_charge);
	return;
exit:
	dev_err(di->dev,
		"Failed to read or write gas gauge registers\n");
	mutex_unlock(&di->cc_lock);
	queue_work(di->fg_wq, &di->fg_work);
}

/**
 * ab8500_fg_bat_voltage() - get battery voltage
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery voltage(on success) else error code
 */
static int ab8500_fg_bat_voltage(struct ab8500_fuelgauge_info *di)
{
	int vbat;
	static int prev;

	vbat = ab8500_gpadc_convert(di->gpadc, MAIN_BAT_V);
	if (vbat < 0) {
		dev_err(di->dev, "GPADC to voltage conversion failed\n");
		return prev;
	}

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
static int ab8500_fg_volt_to_resistance(struct ab8500_fuelgauge_info *di,
					int voltage)
{
	int i, tbl_size;
	struct v_to_res *tbl;
	int res = 0;

#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
	if (di->flags.charging) {
		tbl = get_battery_data(di).bat_info->v_to_chg_res_tbl,
		tbl_size = get_battery_data(di).bat_info->
			n_v_chg_res_tbl_elements;
	} else {
#endif
		tbl = get_battery_data(di).bat_info->v_to_res_tbl,
		tbl_size = get_battery_data(di).bat_info->
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
		res += get_battery_data(di).bat_info->
			line_impedance; /* adding line impedance */
	} else if (i >= tbl_size) {
		res = tbl[tbl_size-1].resistance +
			get_battery_data(di).bat_info->line_impedance;
	} else {
#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
		if (di->flags.charging) {
			res = get_battery_data(di).bat_info->
				battery_resistance_for_charging +
				get_battery_data(di).bat_info->
				line_impedance;
		} else {
#endif
			res = get_battery_data(di).bat_info->
				battery_resistance +
				get_battery_data(di).bat_info->
				line_impedance;
#ifdef USE_COMPENSATING_VOLTAGE_SAMPLE_FOR_CHARGING
		}
#endif
	}

	dev_dbg(di->dev, "[NEW BATT RES] %s Vbat: %d, Res: %d mohm",
		__func__, voltage, res);

	return res;
}

/**
 * ab8500_comp_fg_bat_voltage() - get battery voltage
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns compensated battery voltage(on success) else error code
 */
static int ab8500_comp_fg_bat_voltage(struct ab8500_fuelgauge_info *di,
		bool always)
{
	int vbat_comp;
	int i = 0;
	int vbat = 0;
	int bat_res_comp = 0;

	ab8500_fg_inst_curr_start(di);

	do {
		vbat += ab8500_fg_bat_voltage(di);
		i++;
		dev_dbg(di->dev, "LoadComp Vbat avg [%d] %d\n", i, vbat/i);
		msleep(5);
	} while (!ab8500_fg_inst_curr_done(di) &&
		i <= WAIT_FOR_INST_CURRENT_MAX);

	if (i > WAIT_FOR_INST_CURRENT_MAX) {
		dev_dbg(di->dev,
			"Inst curr reading took too long, %d times\n",
			i);
		pr_info("[FG] Returned uncompensated vbat\n");
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

	vbat = vbat / i;

#if defined(CONFIG_MACH_JANICE) || \
	defined(CONFIG_MACH_CODINA) || \
	defined(CONFIG_MACH_GAVINI) || \
	defined(CONFIG_MACH_SEC_GOLDEN) || \
	defined(CONFIG_MACH_SEC_KYLE) || \
	defined(CONFIG_MACH_SEC_RICCO)
	bat_res_comp = ab8500_fg_volt_to_resistance(di, vbat);
#else
	bat_res_comp = get_battery_data(di).bat_info->
			battery_resistance +
			get_battery_data(di).bat_info->
			line_impedance;
#endif
	/* Use Ohms law to get the load compensated voltage */
	vbat_comp = vbat - (di->inst_curr * bat_res_comp) / 1000;

	dev_dbg(di->dev, "%s Measured Vbat: %dmV,Compensated Vbat %dmV, "
		"R: %dmOhm, Current: %dmA Vbat Samples: %d\n",
		__func__, vbat, vbat_comp,
		bat_res_comp,
		di->inst_curr, i);
	dev_dbg(di->dev, "[FG] TuningData:\t%d\t%d\t%d\t%d\t%d\n",
		DIV_ROUND_CLOSEST(di->bat_cap.permille, 10),
		vbat, vbat_comp,
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
static int ab8500_fg_volt_to_capacity(struct ab8500_fuelgauge_info *di,
				      int voltage)
{
	int i, tbl_size;
	struct v_to_cap *tbl;
	int cap = 0;

	tbl = get_battery_data(di).bat_info->v_to_cap_tbl,
	tbl_size = get_battery_data(di).bat_info->n_v_cap_tbl_elements;

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

	/* store the initial capacity information */
	if (di->init_capacity) {
		di->new_capacity_volt = voltage;
		di->new_capacity = DIV_ROUND_CLOSEST(cap, 10);
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
static int ab8500_fg_uncomp_volt_to_capacity(struct ab8500_fuelgauge_info *di)
{
	di->vbat = ab8500_fg_bat_voltage(di);

	return ab8500_fg_volt_to_capacity(di, di->vbat);
}

/**
 * ab8500_fg_load_comp_volt_to_capacity() - Load compensated voltage
 *					    based capacity
 * @di:		pointer to the ab8500_fg structure
 * @always:	always return the volt to cap no mather what
 *
 * Returns battery capacity based on battery voltage that is load compensated
 * for the voltage drop
 */
static int ab8500_fg_load_comp_volt_to_capacity(
	struct ab8500_fuelgauge_info *di, bool always)
{
	int vbat_comp = 0;

	vbat_comp = ab8500_comp_fg_bat_voltage(di, always);
	if (vbat_comp == -1)
		return -1;

	return ab8500_fg_volt_to_capacity(di, vbat_comp);
}

/**
 * ab8500_fg_convert_mah_to_permille() - Capacity in mAh to permille
 * @di:		pointer to the ab8500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in permille
 */
static int ab8500_fg_convert_mah_to_permille(struct ab8500_fuelgauge_info *di,
					     int cap_mah)
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
static int ab8500_fg_convert_permille_to_mah(struct ab8500_fuelgauge_info *di,
					     int cap_pm)
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
static int ab8500_fg_convert_mah_to_uwh(struct ab8500_fuelgauge_info *di,
					int cap_mah)
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
static int ab8500_fg_calc_cap_charging(struct ab8500_fuelgauge_info *di)
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
	 * We force capacity to 100% once when the algorithm
	 * reports that it's full.
	 */
	if (di->bat_cap.mah >= di->bat_cap.max_mah_design ||
	    di->flags.fully_charged) {
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
	dev_dbg(di->dev, "[FG] Using every %d Vbat sample Now on %d loop\n",
		di->n_skip_add_sample, di->skip_add_sample);

	if (++di->skip_add_sample >= di->n_skip_add_sample) {
		dev_dbg(di->dev, "[FG] Adding voltage based samples to avg: %d\n",
			di->vbat_cap.avg);
		di->bat_cap.mah = ab8500_fg_add_cap_sample(di,
			di->vbat_cap.avg);
		di->skip_add_sample = 0;
	}
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
#else
	ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);

	/* We need to update battery voltage and inst current
	   when charging */
	di->vbat = ab8500_comp_fg_bat_voltage(di, true);
	di->inst_curr = ab8500_fg_inst_curr_blocking(di);
#endif

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
static int ab8500_fg_calc_cap_discharge_voltage(
	struct ab8500_fuelgauge_info *di, bool comp)
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
static int ab8500_fg_calc_cap_discharge_fg(struct ab8500_fuelgauge_info *di)
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
#if defined(CONFIG_MACH_CODINA) || \
	defined(CONFIG_MACH_SEC_GOLDEN) || \
	defined(CONFIG_MACH_SEC_KYLE) || \
	defined(CONFIG_MACH_SEC_RICCO)
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

		dev_dbg(di->dev, "[DIFF CAP] FG_avg : %d, VB_avg : %d, "
			"diff_cap : %d\n",
			ab8500_fg_convert_mah_to_permille(di, di->avg_cap.avg),
			ab8500_fg_convert_mah_to_permille(di, di->vbat_cap.avg),
			DIV_ROUND_CLOSEST(diff_cap, 10));

		dev_dbg(di->dev, "Adding voltage based samples to avg: %d\n",
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
static int ab8500_fg_capacity_level(struct ab8500_fuelgauge_info *di)
{
	int ret, percent;

	percent = DIV_ROUND_CLOSEST(di->bat_cap.permille, 10);

	if (percent <= get_battery_data(di).cap_levels->critical ||
		di->flags.low_bat)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (percent <= get_battery_data(di).cap_levels->low)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (percent <= get_battery_data(di).cap_levels->normal)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (percent <= get_battery_data(di).cap_levels->high)
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
static void ab8500_fg_check_capacity_limits(struct ab8500_fuelgauge_info *di,
					    bool init)
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
			    di->vbat <= di->lowbat_zero_voltage) {
				di->lowbat_poweroff = true;
			} else if ((percent > 1 && !di->flags.charging) ||
				   (percent <= 1 &&
				    di->vbat > di->lowbat_zero_voltage)) {
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
		if (percent <= 1 && di->vbat <= di->lowbat_zero_voltage)
			di->lowbat_poweroff = true;

		if (di->vbat > 3450) {
			dev_info(di->dev, "Low bat condition is recovered.\n");
			di->lowbat_poweroff_locked = false;
			wake_unlock(&di->lowbat_poweroff_wake_lock);
		}
	}

	if ((percent == 0 && di->vbat <= di->lowbat_zero_voltage) ||
	    (percent <= 1 && di->vbat <= di->lowbat_zero_voltage && !changed))
		di->lowbat_poweroff = true;

	if ((di->lowbat_poweroff && di->lpm_chg_mode) ||
	    (di->lowbat_poweroff && di->flags.charging &&
	     !time_after(jiffies, di->boot_time))) {
		di->lowbat_poweroff = false;
		dev_info(di->dev, "Low bat interrupt occured, "
			 "but we will ignore it in lpm or booting time\n");
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

	/* TODO : should handle this routine */
	/* if (changed) */
	/*	power_supply_changed(&di->psy); */

}

static void ab8500_fg_charge_state_to(struct ab8500_fuelgauge_info *di,
	enum ab8500_fg_charge_state new_state)
{
	dev_dbg(di->dev, "Charge state from %d [%s] to %d [%s]\n",
		di->charge_state,
		charge_state[di->charge_state],
		new_state,
		charge_state[new_state]);

	di->charge_state = new_state;
}

static void ab8500_fg_discharge_state_to(struct ab8500_fuelgauge_info *di,
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
static void ab8500_fg_algorithm_charging(struct ab8500_fuelgauge_info *di)
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
			get_battery_data(di).fg_params->accu_charging);

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
				"[CHARGING] Average voltage based capacity is: "
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
		    di->flags.fully_charged) {
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
static void ab8500_fg_algorithm_discharging(struct ab8500_fuelgauge_info *di)
{
	int sleep_time;

	/* If we change to charge mode we should start with init */
	if (di->charge_state != AB8500_FG_CHARGE_INIT)
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);

	switch (di->discharge_state) {
	case AB8500_FG_DISCHARGE_INIT:
		/* We use the FG IRQ to work on */
		di->init_cnt = 0;
		di->fg_samples =
			SEC_TO_SAMPLE(
				get_battery_data(di).fg_params->init_timer);
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
		sleep_time = get_battery_data(di).fg_params->init_timer;

		/* Discard the first [x] seconds */
		if (di->init_cnt >
		    get_battery_data(di).fg_params->init_discard_time) {
			ab8500_fg_calc_cap_discharge_voltage(di, true);
			ab8500_fg_check_capacity_limits(di, true);

			if (!(di->init_cnt % 5)) {
				dev_info(di->dev,
				 "[FG_DATA] volt %dmV, mah %d, permille %d, "
				 "vbat_cap.avg %d\n",
					 di->vbat,
					 di->bat_cap.mah,
					 di->bat_cap.permille,
					 di->vbat_cap.avg);
				dev_info(di->dev,
				 "[FG_DATA] inst_curr %dmA, avg_curr %dmA\n",
					 di->inst_curr,
					 di->avg_curr);
			}
		}

		di->init_cnt += sleep_time;
		if (di->init_cnt >
		    get_battery_data(di).fg_params->init_total_time) {
			di->fg_samples = SEC_TO_SAMPLE(
				get_battery_data(di).fg_params->accu_high_curr);

			ab8500_fg_coulomb_counter(di, true);
			dev_info(di->dev,
				"Filling vcap with %d mah, avg was %d, vbat %d\n",
				di->bat_cap.mah, di->vbat_cap.avg, di->vbat);
			ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);
			di->inst_curr = ab8500_fg_inst_curr_blocking(di);
			ab8500_fg_fill_i_sample(di, di->inst_curr);

			ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);
			dev_info(di->dev, "capacity initialization complete\n");
		}

		break;

	case AB8500_FG_DISCHARGE_INIT_RECOVERY:
		di->recovery_cnt = 0;
		di->recovery_needed = true;
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_RECOVERY);

		/* Intentional fallthrough */

	case AB8500_FG_DISCHARGE_RECOVERY:
		sleep_time =
			get_battery_data(di).fg_params->recovery_sleep_timer;

		/*
		 * We should check the power consumption
		 * If low, go to READOUT (after x min) or
		 * RECOVERY_SLEEP if time left.
		 * If high, go to READOUT
		 */
		di->inst_curr = ab8500_fg_inst_curr_blocking(di);

		if (ab8500_fg_is_low_curr(di, di->inst_curr)) {
			if (di->recovery_cnt >
			    get_battery_data(di).
			    fg_params->recovery_total_time) {
				di->fg_samples = SEC_TO_SAMPLE(
				get_battery_data(di).fg_params->accu_high_curr);
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
				get_battery_data(di).fg_params->accu_high_curr);
			ab8500_fg_coulomb_counter(di, true);
			ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);
		}
		break;

	case AB8500_FG_DISCHARGE_READOUT:
		di->inst_curr = ab8500_fg_inst_curr_blocking(di);
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
					&di->fg_periodic_work, 0);

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
				get_battery_data(di).fg_params->accu_high_curr;
			if (di->high_curr_cnt >
				get_battery_data(di).fg_params->high_curr_time)
				di->recovery_needed = true;

			ab8500_fg_calc_cap_discharge_fg(di);
		}

		ab8500_fg_check_capacity_limits(di, false);

#if defined(CONFIG_MACH_CODINA) || \
	defined(CONFIG_MACH_SEC_GOLDEN) || \
	defined(CONFIG_MACH_SEC_KYLE) || \
	defined(CONFIG_MACH_SEC_RICCO)
		if (DIV_ROUND_CLOSEST(di->bat_cap.permille, 10) <= 10) {
			queue_delayed_work(di->fg_wq,
				&di->fg_periodic_work,
				10 * HZ);
		}
#endif
		break;

	case AB8500_FG_DISCHARGE_WAKEUP:
		ab8500_fg_coulomb_counter(di, true);
		di->inst_curr = ab8500_fg_inst_curr_blocking(di);

		ab8500_fg_calc_cap_discharge_voltage(di, true);

		di->fg_samples = SEC_TO_SAMPLE(
			get_battery_data(di).fg_params->accu_high_curr);
		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);

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
static void ab8500_fg_algorithm_calibrate(struct ab8500_fuelgauge_info *di)
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

static void ab8500_init_columbcounter(struct ab8500_fuelgauge_info *di)
{
	int ret;
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
	return;

inst_curr_err:
	dev_err(di->dev, "%s initializing Coulomb Coulomb is failed\n",
		__func__);
	mutex_unlock(&di->cc_lock);
	return;
}

/**
 * ab8500_fg_algorithm() - Entry point for the FG algorithm
 * @di:		pointer to the ab8500_fg structure
 *
 * Entry point for the battery capacity calculation state machine
 */
static void ab8500_fg_algorithm(struct ab8500_fuelgauge_info *di)
{

	if (di->flags.calibrate) {
		ab8500_init_columbcounter(di);
		ab8500_fg_algorithm_calibrate(di);
	} else {
		if (di->flags.charging) {
			di->fg_res = get_battery_data(di).fg_res_chg;
			ab8500_fg_algorithm_charging(di);
		} else {
			di->fg_res = get_battery_data(di).fg_res_dischg;
			ab8500_fg_algorithm_discharging(di);
		}
	}

	if (di->discharge_state != AB8500_FG_DISCHARGE_INITMEASURING)
		dev_info(di->dev,
			"[FG_DATA] %dmAh/%dmAh %d%% (Prev %dmAh %d%%) %dmV %dmA"
			" %dmA %d %d %d %d %d %d %d %d %d %d %d\n",
			di->bat_cap.mah/1000,
			di->bat_cap.max_mah_design/1000,
			DIV_ROUND_CLOSEST(di->bat_cap.permille, 10),
			di->bat_cap.prev_mah/1000,
			di->bat_cap.prev_percent,
			di->vbat,
			di->inst_curr,
			di->avg_curr,
			di->accu_charge,
			di->flags.charging,
			di->charge_state,
			di->discharge_state,
			di->high_curr_mode,
			di->recovery_needed,
			di->fg_res,
			di->new_capacity,
			di->param_capacity,
			di->initial_capacity_calib,
			di->flags.fully_charged);
}

/**
 * ab8500_fg_periodic_work() - Run the FG state machine periodically
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for periodic work
 */
static void ab8500_fg_periodic_work(struct work_struct *work)
{
	struct ab8500_fuelgauge_info *di =
		container_of(work,
			     struct ab8500_fuelgauge_info,
			     fg_periodic_work.work);

	if (di->init_capacity) {
		/* A dummy read that will return 0 */
		di->inst_curr = ab8500_fg_inst_curr_blocking(di);
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

	struct ab8500_fuelgauge_info *di =
		container_of(work,
			     struct ab8500_fuelgauge_info,
			     fg_low_bat_work.work);

	vbat = ab8500_comp_fg_bat_voltage(di, true);

	/* Check if LOW_BAT still fulfilled */
	if (vbat <
	    get_battery_data(di).
	    fg_params->lowbat_threshold + LOWBAT_TOLERANCE) {
		/* Is it time to shut down? */
		if (di->low_bat_cnt < 1) {
			di->flags.low_bat = true;
			dev_warn(di->dev, "Shut down pending...\n");
		} else {
			/*
			* Else we need to re-schedule this check to be
			* able to detect
			* if the voltage increases again during charging or
			* due to decreasing load.
			*/
			di->low_bat_cnt--;
			dev_warn(di->dev, "Battery voltage still LOW\n");
			queue_delayed_work(di->fg_wq,
					   &di->fg_low_bat_work,
					   round_jiffies(
						   LOW_BAT_CHECK_INTERVAL));
		}
	} else {
		di->flags.low_bat_delay = false;
		di->low_bat_cnt = 10;
		dev_warn(di->dev, "Battery voltage OK again\n");
	}

	/* This is needed to dispatch LOW_BAT */
	ab8500_fg_check_capacity_limits(di, false);
}

/**
 * ab8500_fg_battok_calc - calculate the bit pattern corresponding
 * to the target voltage.
 * @di:       pointer to the ab8500_fg structure
 * @target    target voltage
 *
 * Returns bit pattern closest to the target voltage
 * valid return values are 0-14. (0-BATT_OK_MAX_NR_INCREMENTS)
 */

static int ab8500_fg_battok_calc(struct ab8500_fuelgauge_info *di, int target)
{
	if (target > BATT_OK_MIN +
		(BATT_OK_INCREMENT * BATT_OK_MAX_NR_INCREMENTS))
		return BATT_OK_MAX_NR_INCREMENTS;
	if (target < BATT_OK_MIN)
		return 0;
	return (target - BATT_OK_MIN) / BATT_OK_INCREMENT;
}

/**
 * ab8500_fg_battok_init_hw_register - init battok levels
 * @di:       pointer to the ab8500_fg structure
 *
 */

static int ab8500_fg_battok_init_hw_register(struct ab8500_fuelgauge_info *di)
{
	int selected;
	int sel0;
	int sel1;
	int cbp_sel0;
	int cbp_sel1;
	int ret;
	int new_val;

	sel0 = get_battery_data(di).fg_params->battok_falling_th_sel0;
	sel1 = get_battery_data(di).fg_params->battok_raising_th_sel1;

	cbp_sel0 = ab8500_fg_battok_calc(di, sel0);
	cbp_sel1 = ab8500_fg_battok_calc(di, sel1);

	selected = BATT_OK_MIN + cbp_sel0 * BATT_OK_INCREMENT;

	if (selected != sel0)
		dev_warn(di->dev, "Invalid voltage step:%d, using %d %d\n",
			sel0, selected, cbp_sel0);

	selected = BATT_OK_MIN + cbp_sel1 * BATT_OK_INCREMENT;

	if (selected != sel1)
		dev_warn(di->dev, "Invalid voltage step:%d, using %d %d\n",
			sel1, selected, cbp_sel1);

	cbp_sel1 = 0x7;  /* BATTOK falling threshold to 2.71V */
	new_val = cbp_sel0 | (cbp_sel1 << 4);

	dev_dbg(di->dev, "using: %x %d %d\n", new_val, cbp_sel0, cbp_sel1);
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_BATT_OK_REG, new_val);
	return ret;
}

/**
 * ab8500_fg_instant_work() - Run the FG state machine instantly
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for instant work
 */
static void ab8500_fg_instant_work(struct work_struct *work)
{
	struct ab8500_fuelgauge_info *di =
		container_of(work,
			     struct ab8500_fuelgauge_info,
			     fg_work);

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
	struct ab8500_fuelgauge_info *di = _di;
	if (!di->nbr_cceoc_irq_cnt) {
		di->nbr_cceoc_irq_cnt++;
		complete(&di->ab8500_fg_started);
	} else {
		di->nbr_cceoc_irq_cnt = 0;
		complete(&di->ab8500_fg_complete);
	}
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
	struct ab8500_fuelgauge_info *di = _di;
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
	struct ab8500_fuelgauge_info *di = _di;

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
	struct ab8500_fuelgauge_info *di = _di;

	/* Initiate handling in ab8500_fg_low_bat_work()
	   if not already initiated. */
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
 * ab8500_fg_init_hw_registers() - Set up FG related registers
 * @di:		pointer to the ab8500_fg structure
 *
 * Set up battery OVV, low battery voltage registers
 */
static int ab8500_fg_init_hw_registers(struct ab8500_fuelgauge_info *di)
{
	int ret;

	/* Low Battery Voltage */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_LOW_BAT_REG,
		ab8500_volt_to_regval(
			get_battery_data(di).
			fg_params->lowbat_threshold) << 1 |
		LOW_BAT_ENABLE);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		goto out;
	}

	/* Battery OK threshold */
	ret = ab8500_fg_battok_init_hw_register(di);
	if (ret) {
		dev_err(di->dev, "BattOk init write failed.\n");
		goto out;
	}

#ifdef CONFIG_AB8505_SMPL
	if ((is_ab8505(di->parent) || is_ab9540(di->parent)) &&
	    abx500_get_chip_id(di->dev) >= AB8500_CUT2P0) {
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_MAX_TIME_REG,
			get_battery_data(di).fg_params->pcut_max_time);

		if (ret) {
			dev_err(di->dev,
			"%s write failed AB8505_RTC_PCUT_MAX_TIME_REG\n",
			__func__);
			goto out;
		};

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_RESTART_REG,
			get_battery_data(di).fg_params->pcut_max_restart);

		if (ret) {
			dev_err(di->dev,
			"%s write failed AB8505_RTC_PCUT_RESTART_REG\n",
			__func__);
			goto out;
		};

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_DEBOUNCE_REG,
			get_battery_data(di).fg_params->pcut_debounce_time);

		if (ret) {
			dev_err(di->dev,
			"%s write failed AB8505_RTC_PCUT_DEBOUNCE_REG\n",
			__func__);
			goto out;
		};

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_CTL_STATUS_REG,
			get_battery_data(di).fg_params->pcut_enable);

		if (ret) {
			dev_err(di->dev,
			"%s write failed AB8505_RTC_PCUT_CTL_STATUS_REG\n",
			__func__);
			goto out;
		};
	}
#endif

out:
	return ret;
}

static int ab8500_fg_read_battery_capacity(struct ab8500_fuelgauge_info *di)
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

static int ab8500_fg_write_battery_capacity(struct ab8500_fuelgauge_info *di,
					    int value)
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

static void ab8500_fg_reinit_param_work(struct work_struct *work)
{
	struct ab8500_fuelgauge_info *di =
		container_of(work,
			     struct ab8500_fuelgauge_info,
			     fg_reinit_param_work.work);

	int off_status = 0x0;
	int magic_code = 0;
	int param_voltage = 0;
	int param_charge_state = 0;
	int new_capacity = 0, param_capacity = 0, batt_voltage = 0;
	int delta, mah = 0;
	int valid_range = 0;
	int offset_null = -1;
	u8 switchoff_status = 0;
	bool reset_state = false;
	bool param_null = false;

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
			 "LAST_CAPACITY : %d%%\n",
			 off_status, param_voltage, param_capacity);
		param_null = true;

	} else if (off_status == -1) {
		dev_info(di->dev, "[FG_DATA] OFF STATUS, 0x%x\n"
		       "[FG_DATA] *** Phone was powered off "
		       "by abnormal way ***\n", off_status);

	} else {
		dev_info(di->dev, "[FG_DATA] OFF STATUS, 0x%x\n"
		       "[FG_DATA] *** Maybe this is a first boot ***\n",
		       off_status);
		param_null = true;
	}

	if (param_capacity < 0 || param_capacity > 100)
		param_capacity = 0;

	dev_info(di->dev, "NEW capacity : %d, PARAM capacity : %d, VBAT : %d\n",
		 di->new_capacity, param_capacity, di->new_capacity_volt);

	/* Please find the di->new_capacity in ab8500_fg_volt_to_capacity */
	new_capacity = di->new_capacity;
	di->param_capacity = param_capacity;

	if (di->new_capacity_volt > 3950)
		valid_range = 10;
	else if (di->new_capacity_volt > 3800)
		valid_range = 15;
	else if (di->new_capacity_volt > 3750)
		valid_range = 20;
	else if (di->new_capacity_volt > 3650)
		valid_range = 35;
	else if (di->new_capacity_volt > 3600)
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
			di->inst_curr = ab8500_fg_inst_curr_blocking(di);
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

	if (param_null) {
#if defined(CONFIG_BATT_CAPACITY_PARAM)
		if (sec_set_param_value)
			sec_set_param_value(__BATT_CAPACITY, &offset_null);
#else
		ab8500_fg_write_battery_capacity(di, offset_null);
#endif
	}
}

/**
 * abab8500_fg_reinit_work() - work to reset the FG algorithm
 * @work:	pointer to the work_struct structure
 *
 * Used to reset the current battery capacity to be able to
 * retrigger a new voltage base capacity calculation. For
 * test and verification purpose.
 */
static void ab8500_fg_reinit_work(struct work_struct *work)
{
	struct ab8500_fuelgauge_info *di =
		container_of(work,
			     struct ab8500_fuelgauge_info,
			     fg_reinit_work.work);

	if (di->flags.calibrate == false) {
		dev_dbg(di->dev, "Resetting FG state machine to init.\n");
		ab8500_fg_clear_cap_samples(di);
		ab8500_fg_calc_cap_discharge_voltage(di, true);

		ab8500_fg_fill_vcap_sample(di, di->bat_cap.mah);
		if (!di->flags.charging) {
			di->inst_curr = ab8500_fg_inst_curr_blocking(di);
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

#if defined(CONFIG_PM)
static int ab8500_fg_resume(struct platform_device *pdev)
{
	struct ab8500_fuelgauge_info *di = platform_get_drvdata(pdev);

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
	struct ab8500_fuelgauge_info *di = platform_get_drvdata(pdev);

	flush_delayed_work_sync(&di->fg_periodic_work);
	flush_work_sync(&di->fg_work);
	flush_work_sync(&di->fg_acc_cur_work);
	flush_delayed_work_sync(&di->fg_reinit_work);
	flush_delayed_work_sync(&di->fg_low_bat_work);

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

static int ab8500_vbus_is_detected(struct ab8500_fuelgauge_info *di)
{
	u8 data;
	int vbus;

	abx500_get_register_interruptible(di->dev,
	  AB8500_CHARGER, AB8500_CH_USBCH_STAT1_REG, &data);

	vbus = ((data & VBUS_DET_DBNC1) && (data & VBUS_DET_DBNC100));

	return vbus;
}

static int ab8500_fg_reboot_call(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct ab8500_fuelgauge_info *di;
	int off_status = 0x0;
	int vbus_status;

	di = container_of(self, struct ab8500_fuelgauge_info, fg_notifier);

	/* AB8500 FG cannot keep the battery capacity itself.
	   So, If we repeat the power off/on,
	   battery capacity error occur necessarily.
	   We will keep the capacity in PARAM region for avoiding it.
	*/
	/* Additionally, we will keep the last voltage and charging status
	   for checking unknown/normal power off.
	*/
	vbus_status = ab8500_vbus_is_detected(di);

	if (di->lpm_chg_mode && vbus_status)
		off_status |= (MAGIC_CODE_RESET << OFF_MAGIC_CODE);
	else
		off_status |= (MAGIC_CODE << OFF_MAGIC_CODE);

	off_status |= (di->vbat << OFF_VOLTAGE);
	off_status |= (DIV_ROUND_CLOSEST(di->bat_cap.permille, 10) <<
			OFF_CAPACITY);
	off_status |= (0 << OFF_CHARGE_STATE);

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
	struct ab8500_fuelgauge_info *di = platform_get_drvdata(pdev);

	/* Disable coulomb counter */
	ret = ab8500_fg_coulomb_counter(di, false);
	if (ret)
		dev_err(di->dev, "failed to disable coulomb counter\n");

	destroy_workqueue(di->fg_wq);

	wake_lock_destroy(&di->lowbat_wake_lock);
	wake_lock_destroy(&di->lowbat_poweroff_wake_lock);

	flush_scheduled_work();

	power_supply_unregister(&di->psy);
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


static int ab8500_fg_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	switch (psp) {
		/* Cell voltage (VCELL, mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = ab8500_fg_bat_voltage(di);
		break;
		/* Additional Voltage Information (mV) */
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		val->intval = di->bat_cap.mah/1000;
		break;
		/* Current (mA) */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->inst_curr;
		break;
		/* Average Current (mA) */
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = di->avg_curr;
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		break;
		/* SOC (%) */
	case POWER_SUPPLY_PROP_CAPACITY:
		if (di->flags.fully_charged)
			val->intval = 100;
		else
			val->intval = di->bat_cap.prev_percent;
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ab8500_fg_set_property(struct power_supply *psy,
			 enum power_supply_property psp,
			 const union power_supply_propval *val)
{
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (val->intval == POWER_SUPPLY_STATUS_FULL) {
			di->flags.fully_charged = true;
			queue_work(di->fg_wq, &di->fg_work);
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (val->intval == POWER_SUPPLY_TYPE_BATTERY) {
			di->bat_cap.mah = di->bat_cap.max_mah_design;
			ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);
			di->bat_cap.permille =
				ab8500_fg_convert_mah_to_permille(di,
						  di->bat_cap.mah);
			ab8500_fg_fill_vcap_sample(di,
						   di->bat_cap.mah);
			di->bat_cap.prev_percent = 100;
			di->bat_cap.prev_mah = di->bat_cap.mah;
			dev_info(di->dev,
				 "capacity is adjusted to 100%% explicitly\n");
			queue_work(di->fg_wq, &di->fg_work);
		}
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		di->cable_type = val->intval;
		if (val->intval == POWER_SUPPLY_TYPE_BATTERY) {
			di->flags.charging = false;
			di->flags.fully_charged = false;
		} else
			di->flags.charging = true;

		queue_work(di->fg_wq, &di->fg_work);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* reset the fuelgauge capacity */
		if (val->intval == SEC_FUELGAUGE_CAPACITY_TYPE_RESET)
			queue_delayed_work(di->fg_wq, &di->fg_reinit_work, 0);
		break;
		/* Battery Temperature */
	case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_AB8505_SMPL
static ssize_t ab8505_powercut_maxtime_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_MAX_TIME_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_MAX_TIME_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7F));

fail:
	return ret;

}

static ssize_t ab8505_powercut_maxtime_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	reg_value = simple_strtoul(buf, NULL, 10);
	if (reg_value > 0x7F) {
		dev_err(dev,
			"Incorrect parameter, echo 0 (0.0s) - 127 (1.98s) "
			"for maxtime\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_MAX_TIME_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_MAX_TIME_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_restart_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_RESTART_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev,
			"Failed to read AB8505_RTC_PCUT_RESTART_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0xF));

fail:
	return ret;
}

static ssize_t ab8505_powercut_restart_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	reg_value = simple_strtoul(buf, NULL, 10);
	if (reg_value > 0xF) {
		dev_err(dev,
		"Incorrect parameter, echo 0 - 15 for number of restart\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_RESTART_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_RESTART_REG\n");

fail:
	return count;

}

static ssize_t ab8505_powercut_timer_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_TIME_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_TIME_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7F));

fail:
	return ret;
}

static ssize_t ab8505_powercut_restart_counter_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_RESTART_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_RESTART_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0xF0) >> 4);

fail:
	return ret;
}

static ssize_t ab8505_powercut_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_CTL_STATUS_REG, &reg_value);

	if (ret < 0)
		goto fail;

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x1));

fail:
	return ret;
}

static ssize_t ab8505_powercut_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	reg_value = simple_strtoul(buf, NULL, 10);
	if (reg_value > 0x1) {
		dev_err(dev,
			"Incorrect parameter, echo 0/1 to "
			"disable/enable Pcut feature\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_CTL_STATUS_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev,
			"Failed to set AB8505_RTC_PCUT_CTL_STATUS_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_debounce_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_DEBOUNCE_REG,  &reg_value);

	if (ret < 0) {
		dev_err(dev,
			"Failed to read AB8505_RTC_PCUT_DEBOUNCE_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7));

fail:
	return ret;
}

static ssize_t ab8505_powercut_debounce_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	reg_value = simple_strtoul(buf, NULL, 10);
	if (reg_value > 0x7) {
		dev_err(dev,
		"Incorrect parameter, echo 0 to 7 for debounce setting\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_DEBOUNCE_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_DEBOUNCE_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_enable_status_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_CTL_STATUS_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_CTL_STATUS_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ((reg_value & 0x20) >> 5));

fail:
	return ret;
}

static struct device_attribute ab8505_fg_attrs_ab8505[] = {
	__ATTR(powercut_maxtime, (S_IRUGO | S_IWUSR),
	       ab8505_powercut_maxtime_read, ab8505_powercut_maxtime_write),
	__ATTR(powercut_restart_max, (S_IRUGO | S_IWUSR),
	       ab8505_powercut_restart_read, ab8505_powercut_restart_write),
	__ATTR(powercut_timer, S_IRUGO, ab8505_powercut_timer_read, NULL),
	__ATTR(powercut_restart_counter, S_IRUGO,
	       ab8505_powercut_restart_counter_read, NULL),
	__ATTR(powercut_enable, (S_IRUGO | S_IWUSR),
	       ab8505_powercut_read, ab8505_powercut_write),
	__ATTR(powercut_debounce_time, (S_IRUGO | S_IWUSR),
	       ab8505_powercut_debounce_read, ab8505_powercut_debounce_write),
	__ATTR(powercut_enable_status, S_IRUGO,
	       ab8505_powercut_enable_status_read, NULL),
};
#endif

static int ab8500_fg_create_attrs(struct device *dev)
{
	int i, j, rc;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	for (i = 0; i < ARRAY_SIZE(ab8500_fg_attrs); i++) {
		rc = device_create_file(dev, &ab8500_fg_attrs[i]);
		if (rc)
			goto create_attrs_failed;
	}

#ifdef CONFIG_AB8505_SMPL
	if ((is_ab8505(di->parent) || is_ab9540(di->parent)) &&
			abx500_get_chip_id(dev->parent) >= AB8500_CUT2P0) {
		for (j = 0; j < ARRAY_SIZE(ab8505_fg_attrs_ab8505); j++) {
			rc =
			device_create_file(dev, &ab8505_fg_attrs_ab8505[j]);
		if (rc)
			goto create_attr_failed_ab8505;
		}
	}
#endif
	goto create_attrs_succeed;

#ifdef CONFIG_AB8505_SMPL
create_attr_failed_ab8505:
	dev_err(dev, "%s: failed (%d)\n", __func__, rc);
	while (j--)
		device_remove_file(dev, &ab8505_fg_attrs_ab8505[j]);
#endif
create_attrs_failed:
	dev_err(dev, "%s: failed (%d)\n", __func__, rc);
	while (i--)
		device_remove_file(dev, &ab8500_fg_attrs[i]);
create_attrs_succeed:
	return rc;
}

ssize_t ab8500_fg_show_attrs(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	const ptrdiff_t offset = attr - ab8500_fg_attrs;

	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	int i = 0;

	switch (offset) {
	case FG_REG:
	case FG_DATA:
	case FG_REGS:
		break;
	default:
		i = -EINVAL;
		break;
	}

	return i;
}

ssize_t ab8500_fg_store_attrs(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	const ptrdiff_t offset = attr - ab8500_fg_attrs;

	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fuelgauge_info *di =
		container_of(psy, struct ab8500_fuelgauge_info, psy);

	int ret = 0;

	switch (offset) {
	case FG_REG:
	case FG_DATA:
		break;
	case REINIT_CAP:
		queue_delayed_work(di->fg_wq,
				   &di->fg_reinit_param_work, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int __devinit ab8500_fg_probe(struct platform_device *pdev)
{
	struct ab8500_fuelgauge_info *di;
	struct ab8500_platform_data *plat;

	int ret = 0;
	int i, irq;

	dev_info(&pdev->dev,
		"%s : ABB Fuel Gauge Driver Loading\n", __func__);
	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	platform_set_drvdata(pdev, di);
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	plat = dev_get_platdata(di->parent->dev);
	if (!plat) {
		dev_err(di->dev, "%s, platform data for abb fg is null\n",
			__func__);
		return -ENODEV;
	}

	di->pdata = plat->sec_bat;
	di->gpadc = ab8500_gpadc_get();

	mutex_init(&di->cc_lock);

	di->psy.name			= "sec-fuelgauge";
	di->psy.type			= POWER_SUPPLY_TYPE_BATTERY;
	di->psy.properties		= ab8500_fuelgauge_props;
	di->psy.num_properties		= ARRAY_SIZE(ab8500_fuelgauge_props);
	di->psy.get_property		= ab8500_fg_get_property;
	di->psy.set_property		= ab8500_fg_set_property;

	di->bat_cap.max_mah_design = MILLI_TO_MICRO *
		get_battery_data(di).bat_info->charge_full_design;

	di->bat_cap.max_mah = di->bat_cap.max_mah_design;

	di->vbat_nom = get_battery_data(di).bat_info->nominal_voltage;

#ifdef CONFIG_MACH_JANICE
	if (system_rev >= JANICE_R0_2) {
		if (!gpio_get_value(SMD_ON_JANICE_R0_2))
			di->smd_on = 1;
	}
#endif
	di->reinit_capacity = true;
	di->init_capacity = true;

	di->lpm_chg_mode = di->pdata->is_lpm();

	/* fg_res parameter should be re-calculated
	   according to the model, HW revision. */
	di->fg_res = get_battery_data(di).fg_res_dischg;
	di->lowbat_zero_voltage = get_battery_data(di).lowbat_zero_voltage;

	ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);
	ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_INIT);

	wake_lock_init(&di->lowbat_wake_lock, WAKE_LOCK_SUSPEND,
		       "lowbat_wake_lock");
	wake_lock_init(&di->lowbat_poweroff_wake_lock, WAKE_LOCK_SUSPEND,
		       "lowbat_poweroff_wake_lock");

	/* Create a work queue for running the FG algorithm */
	di->fg_wq = create_singlethread_workqueue("ab8500_fg_wq");
	if (di->fg_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto err_wake_lock;
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

	/* Reset battery low voltage flag */
	di->flags.low_bat = false;

	/* Initialize low battery counter */
	di->low_bat_cnt = 10;

	/* Initialize OVV, and other registers */
	ret = ab8500_fg_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize registers\n");
		goto err_workqueue;
	}

	di->fg_notifier.notifier_call = ab8500_fg_reboot_call;
	register_reboot_notifier(&di->fg_notifier);

	/* Register FG power supply class */
	ret = power_supply_register(di->dev, &di->psy);
	if (ret) {
		dev_err(di->dev, "failed to register FG psy\n");
		goto err_workqueue;
	}

	di->fg_samples =
		SEC_TO_SAMPLE(get_battery_data(di).fg_params->init_timer);
	ab8500_fg_coulomb_counter(di, true);

	/*
	 * Initialize completion used to notify completion and start
	 * of inst current
	 */
	init_completion(&di->ab8500_fg_started);
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
			goto err_req_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab8500_fg_irq[i].name, irq, ret);
	}
	di->irq = platform_get_irq_byname(pdev, "CCEOC");
	disable_irq(di->irq);
	di->nbr_cceoc_irq_cnt = 0;

	/* Calibrate the fg first time */
	di->flags.calibrate = true;
	di->calib_state = AB8500_FG_CALIB_INIT;

	ret = ab8500_fg_create_attrs(di->psy.dev);
	if (ret) {
		dev_err(&di->dev,
			"%s : Failed to create_attrs\n", __func__);
		goto err_req_irq;
	}

	di->boot_time = jiffies + 30 * HZ;

	/* Run the FG algorithm */
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);

	return ret;

err_req_irq:
	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab8500_fg_irq[i].name);
		free_irq(irq, di);
	}
err_supply_unreg:
	power_supply_unregister(&di->psy);
err_workqueue:
	destroy_workqueue(di->fg_wq);
err_wake_lock:
	wake_lock_destroy(&di->lowbat_wake_lock);
	wake_lock_destroy(&di->lowbat_poweroff_wake_lock);
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
