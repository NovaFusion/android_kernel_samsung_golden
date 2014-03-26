/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Battery temperature driver for AB8500
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
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
//#define MAKE_PROC_BATTERY_ID_ENTRY
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
#define FAST_MONITOR	5
#define NORMAL_MONITOR	20
#endif

#define AB8500_BAT_CTRL_CURRENT_SOURCE_DEFAULT 0x14

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

#ifdef MAKE_PROC_BATTERY_ID_ENTRY
#include <linux/proc_fs.h>
#endif

#include <linux/jiffies.h>

#define VTVOUT_V			1800

#define BTEMP_THERMAL_LOW_LIMIT		-10
#define BTEMP_THERMAL_MED_LIMIT		0
#define BTEMP_THERMAL_HIGH_LIMIT_52	52
#define BTEMP_THERMAL_HIGH_LIMIT_57	57
#define BTEMP_THERMAL_HIGH_LIMIT_62	62

#define BTEMP_BATCTRL_CURR_SRC_7UA	7
#define BTEMP_BATCTRL_CURR_SRC_20UA	20

#define BTEMP_BATCTRL_CURR_SRC_16UA	16
#define BTEMP_BATCTRL_CURR_SRC_18UA	18

#define to_ab8500_btemp_device_info(x) container_of((x), \
	struct ab8500_btemp, btemp_psy);

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
enum battery_monitoring_state
{
	temperature_monitoring_off = 0 ,
	temperature_monitoring_no_charging ,
	temperature_monitoring_with_charging ,
} ;
#endif

/**
 * struct ab8500_btemp_interrupts - ab8500 interrupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab8500_btemp_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab8500_btemp_events {
	bool batt_rem;
	bool btemp_high;
	bool btemp_medhigh;
	bool btemp_lowmed;
	bool btemp_low;
	bool ac_conn;
	bool usb_conn;
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	bool battery_ovv ;
	unsigned long battery_ovv_time ;
#endif
};

struct ab8500_btemp_ranges {
	int btemp_high_limit;
	int btemp_med_limit;
	int btemp_low_limit;
};

/**
 * struct ab8500_btemp - ab8500 BTEMP device information
 * @dev:		Pointer to the structure device
 * @node:		List of AB8500 BTEMPs, hence prepared for reentrance
 * @curr_source:	What current source we use, in uA
 * @bat_temp:		Battery temperature in degree Celcius
 * @prev_bat_temp	Last dispatched battery temperature
 * @parent:		Pointer to the struct ab8500
 * @gpadc:		Pointer to the struct gpadc
 * @fg:			Pointer to the struct fg
 * @pdata:		Pointer to the ab8500_btemp platform data
 * @bat:		Pointer to the ab8500_bm platform data
 * @btemp_psy:		Structure for BTEMP specific battery properties
 * @events:		Structure for information about events triggered
 * @btemp_ranges:	Battery temperature range structure
 * @btemp_wq:		Work queue for measuring the temperature periodically
 * @btemp_periodic_work:	Work for measuring the temperature periodically
 * @initialized:	True if battery id read.
 */
struct ab8500_btemp {
	struct device *dev;
	struct list_head node;
	int curr_source;
	int bat_temp;
	int prev_bat_temp;
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	int battery_monitoring_state ;
	int vf_error_cnt;
	int vf_ok_cnt;
	int monitor_time;
	int batt_id;
	bool initial_vf_check;
#endif
	struct ab8500 *parent;
	struct ab8500_gpadc *gpadc;
	struct ab8500_fg *fg;
	struct ab8500_btemp_platform_data *pdata;
	struct ab8500_bm_data *bat;
	struct power_supply btemp_psy;
	struct ab8500_btemp_events events;
	struct ab8500_btemp_ranges btemp_ranges;
	struct workqueue_struct *btemp_wq;
	struct delayed_work btemp_periodic_work;
	bool initialized;
#ifdef MAKE_PROC_BATTERY_ID_ENTRY
	struct proc_dir_entry * battery_proc_entry ;
#endif //MAKE_PROC_BATTERY_ID_ENTRY
};

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
static struct ab8500_btemp * battery = NULL ;
#endif

/* BTEMP power supply properties */
static enum power_supply_property ab8500_btemp_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
};

static LIST_HEAD(ab8500_btemp_list);

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
struct over_temperature_lookup {
	int cutoff_temperature ;
	int reg_value 		;

} ;

static struct over_temperature_lookup over_temperature_lookups[] = {
	{  62	, 0x3	} ,
	{  57	, 0x2	} ,
	{  52	, 0x1	} ,
	{  57	, 0x0	} ,
} ;
#endif

/**
 * ab8500_btemp_get() - returns a reference to the primary AB8500 BTEMP
 * (i.e. the first BTEMP in the instance list)
 */
struct ab8500_btemp *ab8500_btemp_get(void)
{
	struct ab8500_btemp *btemp;
	btemp = list_first_entry(&ab8500_btemp_list, struct ab8500_btemp, node);

	return btemp;
}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
static void ab8500_charger_set_high_temperature_cutoff_temp(struct ab8500_btemp *di)
{
	int x ;
	int i = -1 ;
	char val ;
	int ret ;
	if (di && di->bat) {	/* lookup nearest temperature available */
		for (x=0;x<ARRAY_SIZE(over_temperature_lookups)-1;x++) {
			if ( di->bat->temp_over >= over_temperature_lookups[x].cutoff_temperature) {
				i=x;
				break ;
			}
		}
		i=(i<0)?0:i ;
		abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		     AB8500_BTEMP_HIGH_TH,over_temperature_lookups[x].reg_value);
		ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,AB8500_BTEMP_HIGH_TH, &val);
		if (ret>=0) { /* convert value we set to temperature and sync software and hardware thermal limit*/
			val &= 0x3 ;
			for(x=0;ARRAY_SIZE(over_temperature_lookups);x++) {	/* actual register settings temperature*/
				if (over_temperature_lookups[x].reg_value == val) {
					di->bat->temp_hysteresis -= (di->bat->temp_over - over_temperature_lookups[x].cutoff_temperature) ;
					//di->bat->temp_over = over_temperature_lookups[x].cutoff_temperature ;
					dev_dbg(di->dev, "%s Charging shutdown temp=%d hysteresis=%d restart temp=%d ",__func__
								,di->bat->temp_over
								,di->bat->temp_hysteresis ,
								di->bat->temp_over-di->bat->temp_hysteresis);

					break ;
				}
			}
		}
	}
}
#endif

/**
 * ab8500_btemp_batctrl_volt_to_res() - convert batctrl voltage to resistance
 * @di:		pointer to the ab8500_btemp structure
 * @v_batctrl:	measured batctrl voltage
 *
 * This function returns the battery resistance that is
 * derived from the BATCTRL voltage.
 * Returns value in Ohms.
 */
static int ab8500_btemp_batctrl_volt_to_res(struct ab8500_btemp *di,
	int v_batctrl)
{
	int rbs;

	if (is_ab8500_1p1_or_earlier(di->parent)) {
		/*
		 * For ABB cut1.0 and 1.1 BAT_CTRL is internally
		 * connected to 1.8V through a 450k resistor
		 */
		rbs = (450000 * (v_batctrl)) / (1800 - v_batctrl);
	} else {
		/*
		 * BAT_CTRL is internally
		 * connected to 1.8V through a 80k resistor
		 */
		rbs = (80000 * (v_batctrl)) / (1800 - v_batctrl);
	}

	return rbs;
}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
#define SEC_TO_SAMPLE(S)		(S * 4)
#define PCB_RESISTANCE_ESTIMATE 0 /*fudge factor estimating resistence between battery ground and ground (bar sense resistor) milliohms */
#endif

/**
 * ab8500_btemp_read_batctrl_voltage() - measure batctrl voltage
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function returns the voltage on BATCTRL. Returns value in mV.
 */
static int ab8500_btemp_read_batctrl_voltage(struct ab8500_btemp *di)
{
	int vbtemp;
	static int prev;

	vbtemp = ab8500_gpadc_convert(di->gpadc, BAT_CTRL);
	if (vbtemp < 0) {
		dev_err(di->dev,
			"%s gpadc conversion failed, using previous value",
			__func__);
		return prev;
	}
	prev = vbtemp;
	return vbtemp;
}

/**
 * ab8500_btemp_curr_source_enable() - enable/disable batctrl current source
 * @di:		pointer to the ab8500_btemp structure
 * @enable:	enable or disable the current source
 *
 * Enable or disable the current sources for the BatCtrl AD channel
 */
static int ab8500_btemp_curr_source_enable(struct ab8500_btemp *di,
	bool enable)
{
	int curr;
	int ret = 0;

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	if (di->bat->adc_therm == ADC_THERM_BATTEMP) {
		return 0;
	}
#endif

	/*
	 * BATCTRL current sources are included on AB8500 cut2.0
	 * and future versions
	 */
	if (is_ab8500_1p1_or_earlier(di->parent))
		return 0;

	/* Only do this for batteries with internal NTC */
	if (di->bat->adc_therm == ADC_THERM_BATCTRL && enable) {

		if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			if (di->curr_source == BTEMP_BATCTRL_CURR_SRC_16UA)
				curr = BAT_CTRL_16U_ENA;
			else
				curr = BAT_CTRL_18U_ENA;
		} else {
			if (di->curr_source == BTEMP_BATCTRL_CURR_SRC_7UA)
				curr = BAT_CTRL_7U_ENA;
			else
				curr = BAT_CTRL_20U_ENA;
		}

		dev_dbg(di->dev, "Set BATCTRL %duA\n", di->curr_source);

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			FORCE_BAT_CTRL_CMP_HIGH, FORCE_BAT_CTRL_CMP_HIGH);
		if (ret) {
			dev_err(di->dev, "%s failed setting cmp_force\n",
				__func__);
			return ret;
		}

		/*
		 * We have to wait one 32kHz cycle before enabling
		 * the current source, since ForceBatCtrlCmpHigh needs
		 * to be written in a separate cycle
		 */
		udelay(32);

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			FORCE_BAT_CTRL_CMP_HIGH | curr);
		if (ret) {
			dev_err(di->dev, "%s failed enabling current source\n",
				__func__);
			goto disable_curr_source;
		}
	} else if (di->bat->adc_therm == ADC_THERM_BATCTRL && !enable) {
		dev_dbg(di->dev, "Disable BATCTRL curr source\n");

		if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			/* Write 0 to the curr bits */
			ret = abx500_mask_and_set_register_interruptible(
				di->dev,
				AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
				BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA,
				~(BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA));
		} else {
			/* Write 0 to the curr bits */
			ret = abx500_mask_and_set_register_interruptible(
				di->dev,
				AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
				BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA,
				~(BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA));
		}

		if (ret) {
			dev_err(di->dev, "%s failed disabling current source\n",
				__func__);
			goto disable_curr_source;
		}

		/* Enable Pull-Up and comparator */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER,	AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA,
			BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA);
		if (ret) {
			dev_err(di->dev, "%s failed enabling PU and comp\n",
				__func__);
			goto enable_pu_comp;
		}

		/*
		 * We have to wait one 32kHz cycle before disabling
		 * ForceBatCtrlCmpHigh since this needs to be written
		 * in a separate cycle
		 */
		udelay(32);

		/* Disable 'force comparator' */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			FORCE_BAT_CTRL_CMP_HIGH, ~FORCE_BAT_CTRL_CMP_HIGH);
		if (ret) {
			dev_err(di->dev, "%s failed disabling force comp\n",
				__func__);
			goto disable_force_comp;
		}
	}
	return ret;

	/*
	 * We have to try unsetting FORCE_BAT_CTRL_CMP_HIGH one more time
	 * if we got an error above
	 */
disable_curr_source:
	if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
		/* Write 0 to the curr bits */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA,
			~(BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA));
	} else {
		/* Write 0 to the curr bits */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA,
			~(BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA));
	}

	if (ret) {
		dev_err(di->dev, "%s failed disabling current source\n",
			__func__);
		return ret;
	}
enable_pu_comp:
	/* Enable Pull-Up and comparator */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER,	AB8500_BAT_CTRL_CURRENT_SOURCE,
		BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA,
		BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA);
	if (ret) {
		dev_err(di->dev, "%s failed enabling PU and comp\n",
			__func__);
		return ret;
	}

disable_force_comp:
	/*
	 * We have to wait one 32kHz cycle before disabling
	 * ForceBatCtrlCmpHigh since this needs to be written
	 * in a separate cycle
	 */
	udelay(32);

	/* Disable 'force comparator' */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
		FORCE_BAT_CTRL_CMP_HIGH, ~FORCE_BAT_CTRL_CMP_HIGH);
	if (ret) {
		dev_err(di->dev, "%s failed disabling force comp\n",
			__func__);
		return ret;
	}

	return ret;
}

static bool ab8500_batctrl_nc(struct ab8500_btemp *di)
{
	int ret, batctrl;
	/*
	 * BATCTRL current sources are included on AB8500 cut2.0
	 * and future versions
	 */
	ret = ab8500_btemp_curr_source_enable(di, true);
	if (ret) {
		dev_err(di->dev, "%s curr source enabled failed\n", __func__);
		return ret;
	}

	batctrl = ab8500_btemp_read_batctrl_voltage(di);
	dev_dbg(di->dev, "Read battctrl voltage to: %d\n", batctrl);

	ret = ab8500_btemp_curr_source_enable(di, false);

	if (batctrl > 1300)
		return true;
	return false;
}

/**
 * ab8500_btemp_get_batctrl_res() - get battery resistance
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function returns the battery pack identification resistance.
 * Returns value in Ohms.
 */
static int ab8500_btemp_get_batctrl_res(struct ab8500_btemp *di)
{
	int ret;
	int batctrl = 0;
	int res;
	int inst_curr;
	int i;

	/*
	 * BATCTRL current sources are included on AB8500 cut2.0
	 * and future versions
	 */
	ret = ab8500_btemp_curr_source_enable(di, true);
	if (ret) {
		dev_err(di->dev, "%s curr source enabled failed\n", __func__);
		return ret;
	}

	batctrl = ab8500_btemp_read_batctrl_voltage(di);

	res = ab8500_btemp_batctrl_volt_to_res(di, batctrl);

	ret = ab8500_btemp_curr_source_enable(di, false);
	if (ret) {
		dev_err(di->dev, "%s curr source disable failed\n", __func__);
		return ret;
	}

	dev_dbg(di->dev, "%s batctrl: %d res: %d samples: %d\n",
		__func__, batctrl, res, i);

	return res;
}

#ifdef MAKE_PROC_BATTERY_ID_ENTRY

static int battery_resistence_readproc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	struct ab8500_btemp *di = (struct ab8500_btemp *) data ;
	int len = 0 ;
	int res = ab8500_btemp_get_batctrl_res(di);
	len = sprintf(page,"battery resistence = %d ohms\n",res);
	*eof=-1 ;
	return len ;
}

#endif // MAKE_PROC_BATTERY_ID_ENTRY


/**
 * ab8500_btemp_res_to_temp() - resistance to temperature
 * @di:		pointer to the ab8500_btemp structure
 * @tbl:	pointer to the resiatance to temperature table
 * @tbl_size:	size of the resistance to temperature table
 * @res:	resistance to calculate the temperature from
 *
 * This function returns the battery temperature in degrees Celcius
 * based on the NTC resistance.
 */
static int ab8500_btemp_res_to_temp(struct ab8500_btemp *di,
	const struct res_to_temp *tbl, int tbl_size, int res)
{
	int i, temp;
	/*
	 * Calculate the formula for the straight line
	 * Simple interpolation if we are within
	 * the resistance table limits, extrapolate
	 * if resistance is outside the limits.
	 */
	if (res > tbl[0].resist)
		i = 0;
	else if (res <= tbl[tbl_size - 1].resist)
		i = tbl_size - 2;
	else {
		i = 0;
		while (!(res <= tbl[i].resist &&
			res > tbl[i + 1].resist))
			i++;
	}

	temp = tbl[i].temp + ((tbl[i + 1].temp - tbl[i].temp) *
		(res - tbl[i].resist)) / (tbl[i + 1].resist - tbl[i].resist);
	return temp;
}

/**
 * ab8500_btemp_measure_temp() - measure battery temperature
 * @di:		pointer to the ab8500_btemp structure
 *
 * Returns battery temperature (on success) else the previous temperature
 */
static int ab8500_btemp_measure_temp(struct ab8500_btemp *di)
{
	int temp;
	static int prev;
#ifndef CONFIG_MEASURE_TEMP_BY_ADC_TABLE
	int rbat, rntc, vntc;
#endif
	int adc;
	u8 id;

	id = di->bat->batt_id;

#ifdef CONFIG_MEASURE_TEMP_BY_ADC_TABLE
	adc = ab8500_gpadc_read_raw(di->gpadc, BTEMP_BALL, SAMPLE_16, RISING_EDGE, 0, ADC_SW);
	if (adc < 0) {
		dev_err(di->dev,
			"%s gpadc conversion failed,"
			" using previous value\n", __func__);
		return prev;
	}

	temp = ab8500_btemp_res_to_temp(di,
		di->bat->bat_type[id].r_to_t_tbl,
		di->bat->bat_type[id].n_temp_tbl_elements, adc);
	if(temp != prev)
		pr_info("%s: adc(%d), temp(%d)\n", __func__, adc, temp);
	prev = temp;


#else
	if (di->bat->adc_therm == ADC_THERM_BATCTRL &&
			id != BATTERY_UNKNOWN) {

		rbat = ab8500_btemp_get_batctrl_res(di);
		if (rbat < 0) {
			dev_err(di->dev, "%s get batctrl res failed\n",
				__func__);
			/*
			 * Return out-of-range temperature so that
			 * charging is stopped
			 */
			return BTEMP_THERMAL_LOW_LIMIT;
		}

		temp = ab8500_btemp_res_to_temp(di,
			di->bat->bat_type[id].r_to_t_tbl,
			di->bat->bat_type[id].n_temp_tbl_elements, rbat);

		pr_info("%s: rbat(%d), temp(%d)\n", __func__, rbat, temp);
	} else {
		vntc = ab8500_gpadc_convert(di->gpadc, BTEMP_BALL);
		if (vntc < 0) {
			dev_err(di->dev,
				"%s gpadc conversion failed,"
				" using previous value\n", __func__);
			return prev;
		}
		/*
		 * The PCB NTC is sourced from VTVOUT via a 230kOhm
		 * resistor.
		 */
		rntc = 230000 * vntc / (VTVOUT_V - vntc);

		temp = ab8500_btemp_res_to_temp(di,
			di->bat->bat_type[id].r_to_t_tbl,
			di->bat->bat_type[id].n_temp_tbl_elements, rntc);
		prev = temp;

		pr_info("%s: vntc(%d), rntc(%d), temp(%d)\n", __func__, vntc, rntc, temp);
	}
#endif
	dev_dbg(di->dev, "Battery temperature is %d\n", temp);
	return temp;
}

#if defined( CONFIG_USB_SWITCHER ) || defined( CONFIG_INPUT_AB8505_MICRO_USB_DETECT )

/* For checking VBUS status */

static int ab8500_vbus_is_detected(struct ab8500_btemp *di)
{
	u8 data;

	abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
					  AB8500_CH_USBCH_STAT1_REG, &data);
	return data & VBUS_DET_DBNC100;
}
#endif

/**
 * ab8500_btemp_id() - Identify the connected battery
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function will try to identify the battery by reading the ID
 * resistor. Some brands use a combined ID resistor with a NTC resistor to
 * both be able to identify and to read the temperature of it.
 */
static int ab8500_btemp_id(struct ab8500_btemp *di)
{
	int res;
	int batt_id;
	u8 i;
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	int chg_res_tolerance = 0;
#endif
	if (is_ab9540(di->parent) || is_ab8505(di->parent))
		di->curr_source = BTEMP_BATCTRL_CURR_SRC_16UA;
	else
		di->curr_source = BTEMP_BATCTRL_CURR_SRC_7UA;

	batt_id = BATTERY_UNKNOWN;

	res =  ab8500_btemp_get_batctrl_res(di);
	if (res < 0) {
		dev_err(di->dev, "%s get batctrl res failed\n", __func__);
		return -ENXIO;
	}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	di->bat->batt_res = res;

	/* res value is changed according to the charging status */
// TODO not sure about this presence of VBUS != charging active!!!
	if (ab8500_vbus_is_detected(di))
		chg_res_tolerance = 7000;

	/* BATTERY_UNKNOWN is defined on position 0, skip it! */
	for (i = BATTERY_UNKNOWN + 1; i < di->bat->n_btypes; i++) {
		if ((res <= di->bat->bat_type[i].resis_high
		     + chg_res_tolerance)
		    && (res >= di->bat->bat_type[i].resis_low)) {
			if (di->initial_vf_check) {
				dev_info(di->dev, "Battery detected on %s"
					 " low %d < res %d < high: %d"
					 " index: %d\n",
					 di->bat->adc_therm ==
					 ADC_THERM_BATCTRL ?
					 "BATCTRL" : "BATTEMP",
					 di->bat->bat_type[i].resis_low, res,
					 di->bat->bat_type[i].resis_high
					 + chg_res_tolerance, i);
			}
			batt_id = i;
			break;
		}
	}
#else
	/* BATTERY_UNKNOWN is defined on position 0, skip it! */
	for (i = BATTERY_UNKNOWN + 1; i < di->bat->n_btypes; i++) {
		if ((res <= di->bat->bat_type[i].resis_high) &&
			(res >= di->bat->bat_type[i].resis_low)) {
			dev_dbg(di->dev, "Battery detected on %s"
				" low %d < res %d < high: %d"
				" index: %d\n",
				di->bat->adc_therm == ADC_THERM_BATCTRL ?
				"BATCTRL" : "BATTEMP",
				di->bat->bat_type[i].resis_low, res,
				di->bat->bat_type[i].resis_high, i);

			batt_id = i;
			break;
		}
	}
#endif

	di->bat->batt_id = batt_id;

	if (di->bat->batt_id == BATTERY_UNKNOWN) {
		dev_err(di->dev, "Battery identified as unknown"
			", resistance %d Ohm\n", res);
		return -ENXIO;
	}

	/*
	 * We only have to change current source if the
	 * detected type is Type 1, else we use the 7uA source
	 */
	if (di->bat->adc_therm == ADC_THERM_BATCTRL && di->bat->batt_id == 1) {
		if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			dev_dbg(di->dev,
					"Set BATCTRL current source to 16uA\n");
			di->curr_source = BTEMP_BATCTRL_CURR_SRC_16UA;
		} else {
			dev_dbg(di->dev,
					"Set BATCTRL current source to 20uA\n");
			di->curr_source = BTEMP_BATCTRL_CURR_SRC_20UA;
		}
	}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	/* if we saw a battery then */
	/* re-enable the battery sense comparator */
	/* for history */
	/* if (di->bat->adc_therm == ADC_THERM_BATTEMP &&
	   di->batt_id != BATTERY_UNKNOWN) { */

	if (di->bat->adc_therm == ADC_THERM_BATTEMP) {
		abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			AB8500_BAT_CTRL_CURRENT_SOURCE_DEFAULT);
	}
#endif

	return di->bat->batt_id;
}

/**
 * ab8500_btemp_periodic_work() - Measuring the temperature periodically
 * @work:	pointer to the work_struct structure
 *
 * Work function for measuring the temperature periodically
 */
static void ab8500_btemp_periodic_work(struct work_struct *work)
{
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	int vbat;
	int batt_id;
#else
	int interval;
#endif

	struct ab8500_btemp *di = container_of(work,
		struct ab8500_btemp, btemp_periodic_work.work);

	pr_debug("[BTEMP Period] start\n");

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	if (di->events.battery_ovv && time_after(jiffies,di->events.battery_ovv_time+(10*HZ))) {
		vbat = ab8500_gpadc_convert(di->gpadc, MAIN_BAT_V);
		dev_dbg(di->dev, "battery overvoltage interrupt seen. Battery voltage =%d flag =%d\n",vbat,di->events.battery_ovv);
		di->events.battery_ovv = (vbat>=di->bat->bat_type[di->bat->batt_id].over_voltage_threshold);
	}

	batt_id = ab8500_btemp_id(di);

	if (di->initial_vf_check) {
		di->vf_error_cnt = 2;
		di->vf_ok_cnt = 2;
		di->initial_vf_check = false;
	}

	if (batt_id < 0) {
		/* If battery is identified as UNKNOWN */
		if (!di->events.batt_rem) {
			di->vf_ok_cnt = 0;
			di->vf_error_cnt++;
			if (di->vf_error_cnt > 2) {
				dev_info(di->dev,
					 "Invalid battery VF is detected."
					 "Charging will be disabled\n");
				di->events.batt_rem = true;
				power_supply_changed(&di->btemp_psy);
			}
		} else {
			di->vf_ok_cnt = 0;
			di->vf_error_cnt = 0;
		}
	} else {
		if (di->events.batt_rem) {
			di->vf_error_cnt = 0;
			di->vf_ok_cnt++;
			if (di->vf_ok_cnt > 2) {
				dev_info(di->dev,
					 "Normal battery VF is detected."
					 "Charging will be enabled\n");
				di->events.batt_rem = false;
				power_supply_changed(&di->btemp_psy);
			}
		} else {
			di->vf_ok_cnt = 0;
			di->vf_error_cnt = 0;
		}
	}

	if (di->vf_ok_cnt > 0 || di->vf_error_cnt > 0)
		di->monitor_time = FAST_MONITOR;
	else
		di->monitor_time = NORMAL_MONITOR;
#else
	if (!di->initialized) {
		di->initialized = true;
		/* Identify the battery */
		if (ab8500_btemp_id(di) < 0)
			dev_warn(di->dev, "failed to identify the battery\n");
	}
#endif

	di->bat_temp = ab8500_btemp_measure_temp(di);

	if (di->bat_temp != di->prev_bat_temp) {
		di->prev_bat_temp = di->bat_temp;
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
		if (di->battery_monitoring_state==temperature_monitoring_with_charging)
#endif
			power_supply_changed(&di->btemp_psy);
	}

#if !defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	if (di->events.ac_conn || di->events.usb_conn)
		interval = di->bat->temp_interval_chg;
	else
		interval = di->bat->temp_interval_nochg;
#endif

	/* Schedule a new measurement */
	queue_delayed_work(di->btemp_wq,
		&di->btemp_periodic_work,
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
		round_jiffies(di->monitor_time * HZ));
#else
		round_jiffies(interval * HZ));
#endif

	pr_debug("[BTEMP Period] end\n");
}

/**
 * ab8500_btemp_batctrlindb_handler() - battery removal detected
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_batctrlindb_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;
	dev_err(di->dev, "Battery removal detected!\n");

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	/* We will check the battery VF res for deciding whether
	   battery is valid or invalid */
#else
	di->events.batt_rem = true;
	power_supply_changed(&di->btemp_psy);
#endif

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_templow_handler() - battery temp lower than 10 degrees
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_templow_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	if (is_ab8500_3p3_or_earlier(di->parent)) {
		dev_dbg(di->dev, "Ignore false btemp low irq"
			" for ABB cut 1.0, 1.1, 2.0 and 3.3\n");
	} else {
		dev_crit(di->dev, "Battery temperature lower than -10deg c\n");

		di->events.btemp_low = true;
		di->events.btemp_high = false;
		di->events.btemp_medhigh = false;
		di->events.btemp_lowmed = false;
		power_supply_changed(&di->btemp_psy);
	}

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_temphigh_handler() - battery temp higher than max temp
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_temphigh_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	dev_crit(di->dev, "Battery temperature is higher than MAX temp\n");

	di->events.btemp_high = true;
	di->events.btemp_medhigh = false;
	di->events.btemp_lowmed = false;
	di->events.btemp_low = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_lowmed_handler() - battery temp between low and medium
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_lowmed_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	dev_dbg(di->dev, "Battery temperature is between low and medium\n");

	di->events.btemp_lowmed = true;
	di->events.btemp_medhigh = false;
	di->events.btemp_high = false;
	di->events.btemp_low = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_medhigh_handler() - battery temp between medium and high
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_medhigh_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	dev_dbg(di->dev, "Battery temperature is between medium and high\n");

	di->events.btemp_medhigh = true;
	di->events.btemp_lowmed = false;
	di->events.btemp_high = false;
	di->events.btemp_low = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_periodic() - Periodic temperature measurements
 * @di:		pointer to the ab8500_btemp structure
 * @enable:	enable or disable periodic temperature measurements
 *
 * Starts of stops periodic temperature measurements. Periodic measurements
 * should only be done when a charger is connected.
 */
static void ab8500_btemp_periodic(struct ab8500_btemp *di,
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	int new_state )
#else
	bool enable)
#endif
{
	dev_dbg(di->dev, "Enable periodic temperature measurements: %d\n",
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
		new_state);
#else
		enable);
#endif

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	di->battery_monitoring_state=new_state ;
	if (new_state == temperature_monitoring_off)  {
		cancel_delayed_work_sync(&di->btemp_periodic_work);
	}
	else  {
		/*
		 * Make sure a new measurement is done directly by cancelling
		 * any pending work
		 */
		cancel_delayed_work_sync(&di->btemp_periodic_work);
		queue_delayed_work(di->btemp_wq, &di->btemp_periodic_work, 0);
	}
#else
	/*
	 * Make sure a new measurement is done directly by cancelling
	 * any pending work
	 */
	cancel_delayed_work_sync(&di->btemp_periodic_work);

	if (enable)
		queue_delayed_work(di->btemp_wq, &di->btemp_periodic_work, 0);
#endif
}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
#define ABSOLUTE_ZERO (-273)

int measure_battery_temperature(void)
{
	int retval= ABSOLUTE_ZERO;
	if (battery)
		retval= ab8500_btemp_measure_temp(battery) ;
	return retval ;
}

EXPORT_SYMBOL_GPL(measure_battery_temperature);
#endif

/**
 * ab8500_btemp_get_temp() - get battery temperature
 * @di:		pointer to the ab8500_btemp structure
 *
 * Returns battery temperature
 */
static int ab8500_btemp_get_temp(struct ab8500_btemp *di)
{
	int temp = 0;

	/*
	 * The BTEMP events are not reliabe on AB8500 cut3.3
	 * and prior versions
	 */
	if (is_ab8500_3p3_or_earlier(di->parent)) {
		temp = di->bat_temp * 10;
	} else {
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
		temp = di->bat_temp * 10;
#else
		if (di->events.btemp_low) {
			if (temp > di->btemp_ranges.btemp_low_limit)
				temp = di->btemp_ranges.btemp_low_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else if (di->events.btemp_high) {
			if (temp < di->btemp_ranges.btemp_high_limit)
				temp = di->btemp_ranges.btemp_high_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else if (di->events.btemp_lowmed) {
			if (temp > di->btemp_ranges.btemp_med_limit)
				temp = di->btemp_ranges.btemp_med_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else if (di->events.btemp_medhigh) {
			if (temp < di->btemp_ranges.btemp_med_limit)
				temp = di->btemp_ranges.btemp_med_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else
			temp = di->bat_temp * 10;
#endif
	}
	return temp;
}

/**
 * ab8500_btemp_get_batctrl_temp() - get the temperature
 * @btemp:      pointer to the btemp structure
 *
 * Returns the batctrl temperature in millidegrees
 */
int ab8500_btemp_get_batctrl_temp(struct ab8500_btemp *btemp)
{
	return btemp->bat_temp * 1000;
}

/**
 * ab8500_btemp_get_property() - get the btemp properties
 * @psy:        pointer to the power_supply structure
 * @psp:        pointer to the power_supply_property structure
 * @val:        pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the btemp
 * properties by reading the sysfs files.
 * online:	presence of the battery
 * present:	presence of the battery
 * technology:	battery technology
 * temp:	battery temperature
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_btemp_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_btemp *di;
	int ret = 0;
	di = to_ab8500_btemp_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		if (di->events.batt_rem)
			val->intval = 0;
		else
			val->intval = 1;
		pr_debug("[BTEMP PROPERTY] %s %d\n", (psp == POWER_SUPPLY_PROP_PRESENT)? "PRESENT":"ONLINE", val->intval);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = di->bat->bat_type[di->bat->batt_id].name;
		pr_debug("[BTEMP PROPERTY] Tech %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = ab8500_btemp_get_temp(di);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int ab8500_btemp_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab8500_btemp *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_ab8500_btemp_device_info(psy);

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
		case POWER_SUPPLY_PROP_PRESENT:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_MAINS:
				/* AC disconnected */
				if (!ret.intval && di->events.ac_conn) {
					di->events.ac_conn = false;
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
					if (!di->events.usb_conn)
						ab8500_btemp_periodic(di,temperature_monitoring_no_charging /*false*/);
#endif
				}
				/* AC connected */
				else if (ret.intval && !di->events.ac_conn) {
					di->events.ac_conn = true;
					if (!di->events.usb_conn)
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
						ab8500_btemp_periodic(di, /*true*/temperature_monitoring_with_charging);
#else
						ab8500_btemp_periodic(di, true);
#endif
				}
				break;
			case POWER_SUPPLY_TYPE_USB:
				/* USB disconnected */
				if (!ret.intval && di->events.usb_conn) {
					di->events.usb_conn = false;
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
					if (!di->events.ac_conn)
						ab8500_btemp_periodic(di,
							/*false*/temperature_monitoring_no_charging);
#endif
				}
				/* USB connected */
				else if (ret.intval && !di->events.usb_conn) {
					di->events.usb_conn = true;
					if (!di->events.ac_conn)
#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
						ab8500_btemp_periodic(di, temperature_monitoring_with_charging /* true*/);
#else
						ab8500_btemp_periodic(di, true);
#endif
				}
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
 * ab8500_btemp_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is pointing to the function pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in the external power
 * supply to the btemp.
 */
static void ab8500_btemp_external_power_changed(struct power_supply *psy)
{
	struct ab8500_btemp *di = to_ab8500_btemp_device_info(psy);

	class_for_each_device(power_supply_class, NULL,
		&di->btemp_psy, ab8500_btemp_get_ext_psy_data);
}

/* ab8500 btemp driver interrupts and their respective isr */
static struct ab8500_btemp_interrupts ab8500_btemp_irq[] = {
	{"BAT_CTRL_INDB", ab8500_btemp_batctrlindb_handler},
	{"BTEMP_LOW", ab8500_btemp_templow_handler},
	{"BTEMP_HIGH", ab8500_btemp_temphigh_handler},
	{"BTEMP_LOW_MEDIUM", ab8500_btemp_lowmed_handler},
	{"BTEMP_MEDIUM_HIGH", ab8500_btemp_medhigh_handler},
};

#if defined(CONFIG_PM)
static int ab8500_btemp_resume(struct platform_device *pdev)
{
	struct ab8500_btemp *di = platform_get_drvdata(pdev);

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	if (di->events.ac_conn || di->events.usb_conn) {
		ab8500_btemp_periodic(di, temperature_monitoring_with_charging );
		//ab8500_btemp_periodic(di, true);
	} else {
		ab8500_btemp_periodic(di, temperature_monitoring_no_charging );
	}
#else
	ab8500_btemp_periodic(di, true);
#endif

	return 0;
}

static int ab8500_btemp_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_btemp *di = platform_get_drvdata(pdev);

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	ab8500_btemp_periodic(di, temperature_monitoring_off );
#else
	ab8500_btemp_periodic(di, false);
#endif

	return 0;
}
#else
#define ab8500_btemp_suspend      NULL
#define ab8500_btemp_resume       NULL
#endif

static int __devexit ab8500_btemp_remove(struct platform_device *pdev)
{
	struct ab8500_btemp *di = platform_get_drvdata(pdev);
	int i, irq;

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	battery=NULL ;
#endif
#ifdef MAKE_PROC_BATTERY_ID_ENTRY
	remove_proc_entry("Battery_Res" ,NULL);
#endif //MAKE_PROC_BATTERY_ID_ENTRY

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	ab8500_btemp_periodic(di, temperature_monitoring_off );
#endif
	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		free_irq(irq, di);
	}

	/* Delete the work queue */
	destroy_workqueue(di->btemp_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->btemp_psy);
	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return 0;
}

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

static int __devinit ab8500_btemp_probe(struct platform_device *pdev)
{
	int irq, i, ret = 0;
	u8 val;
	struct ab8500_platform_data *plat;

	struct ab8500_btemp *di =
		kzalloc(sizeof(struct ab8500_btemp), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	pr_debug("[BTEMP PROBE] start\n");

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab8500_gpadc_get();

	di->initialized = false;

	plat = dev_get_platdata(di->parent->dev);

#ifdef CHRG_DEBUG
		u8 j = 0;
		u8 value;
		while ( chrg_regs[j] != -1 ){
			abx500_get_register_interruptible(di->dev, AB8500_CHARGER, chrg_regs[j], &value);
			pr_info("[CHARGER PROBE] Charger reg 0x%x=0x%x\n", chrg_regs[j], value);
			j++;
		}
#endif

#ifdef CHRG_DEBUG
		u8 j = 0;
		u8 value;
		while ( chrg_regs[j] != -1 ){
			abx500_get_register_interruptible(di->dev, AB8500_CHARGER, chrg_regs[j], &value);
			pr_info("[BTEMP PROBE] Charger reg 0x%x=0x%x\n", chrg_regs[j], value);
			j++;
		}
#endif

	/* get btemp specific platform data */
	if (!plat->btemp) {
		dev_err(di->dev, "no btemp platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->pdata = plat->btemp;

	/* get battery specific platform data */
	if (!plat->battery) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->bat = plat->battery;

	/* BTEMP supply */
	di->btemp_psy.name = "ab8500_btemp";
	di->btemp_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->btemp_psy.properties = ab8500_btemp_props;
	di->btemp_psy.num_properties = ARRAY_SIZE(ab8500_btemp_props);
	di->btemp_psy.get_property = ab8500_btemp_get_property;
	di->btemp_psy.supplied_to = di->pdata->supplied_to;
	di->btemp_psy.num_supplicants = di->pdata->num_supplicants;
	di->btemp_psy.external_power_changed =
		ab8500_btemp_external_power_changed;


	/* Create a work queue for the btemp */
	di->btemp_wq =
		create_singlethread_workqueue("ab8500_btemp_wq");
	if (di->btemp_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for measuring temperature periodically */
	INIT_DELAYED_WORK_DEFERRABLE(&di->btemp_periodic_work,
		ab8500_btemp_periodic_work);

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	/* We will only use batt type 1 */
	di->bat->batt_id = 1;
	di->initial_vf_check = true;
#endif

	/* Identify the battery */
	if (ab8500_btemp_id(di) < 0)
		dev_warn(di->dev, "failed to identify the battery\n");

	/* Set BTEMP thermal limits. Low and Med are fixed */
	di->btemp_ranges.btemp_low_limit = BTEMP_THERMAL_LOW_LIMIT;
	di->btemp_ranges.btemp_med_limit = BTEMP_THERMAL_MED_LIMIT;

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	ab8500_charger_set_high_temperature_cutoff_temp(di) ;
#endif

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_BTEMP_HIGH_TH, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		goto free_btemp_wq;
	}
	switch (val) {
	case BTEMP_HIGH_TH_57_0:
	case BTEMP_HIGH_TH_57_1:
		di->btemp_ranges.btemp_high_limit =
			BTEMP_THERMAL_HIGH_LIMIT_57;
		break;
	case BTEMP_HIGH_TH_52:
		di->btemp_ranges.btemp_high_limit =
			BTEMP_THERMAL_HIGH_LIMIT_52;
		break;
	case BTEMP_HIGH_TH_62:
		di->btemp_ranges.btemp_high_limit =
			BTEMP_THERMAL_HIGH_LIMIT_62;
		break;
	}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	/* Measure temperature once initially */
	di->bat_temp = ab8500_btemp_measure_temp(di);
#endif

	/* Register BTEMP power supply class */
	ret = power_supply_register(di->dev, &di->btemp_psy);
	if (ret) {
		dev_err(di->dev, "failed to register BTEMP psy\n");
		goto free_btemp_wq;
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab8500_btemp_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab8500_btemp_irq[i].name, di);

		if (ret) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab8500_btemp_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab8500_btemp_irq[i].name, irq, ret);
	}

#ifdef MAKE_PROC_BATTERY_ID_ENTRY

	di->battery_proc_entry =create_proc_read_entry("Battery_Res",0444,NULL,battery_resistence_readproc,di);
#endif //MAKE_PROC_BATTERY_ID_ENTRY

	platform_set_drvdata(pdev, di);
	if (ab8500_batctrl_nc(di)) {
		int ret;

		dev_dbg(di->dev, "Batctrl not connected, lower batt ok limit");
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			0x02, 0x04, 0xFF, 0x33);
		dev_dbg(di->dev, "Lowering batt ok sel, ret: %d", ret);
	}

#if defined( CONFIG_SAMSUNG_CHARGER_SPEC )
	ab8500_btemp_periodic(di,temperature_monitoring_no_charging /*false*/);
	battery=di ;
#else
	/* Kick off periodic temperature measurements */
	ab8500_btemp_periodic(di, true);
#endif

	list_add_tail(&di->node, &ab8500_btemp_list);

	pr_debug("[BTEMP PROBE] end\n");

	return ret;

free_irq:
	power_supply_unregister(&di->btemp_psy);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		free_irq(irq, di);
	}
free_btemp_wq:
	destroy_workqueue(di->btemp_wq);
free_device_info:
	kfree(di);

	pr_debug("[BTEMP PROBE] FAIL\n");
	return ret;
}

static struct platform_driver ab8500_btemp_driver = {
	.probe = ab8500_btemp_probe,
	.remove = __devexit_p(ab8500_btemp_remove),
	.suspend = ab8500_btemp_suspend,
	.resume = ab8500_btemp_resume,
	.driver = {
		.name = "ab8500-btemp",
		.owner = THIS_MODULE,
	},
};

static int __init ab8500_btemp_init(void)
{
	return platform_driver_register(&ab8500_btemp_driver);
}

static void __exit ab8500_btemp_exit(void)
{
	platform_driver_unregister(&ab8500_btemp_driver);
}

subsys_initcall_sync(ab8500_btemp_init);
module_exit(ab8500_btemp_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski, Arun R Murthy");
MODULE_ALIAS("platform:ab8500-btemp");
MODULE_DESCRIPTION("AB8500 battery temperature driver");
