/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Battery temperature driver for ab5500
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
#include <linux/workqueue.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500/ab5500-bm.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>

#define BTEMP_THERMAL_LOW_LIMIT		-10
#define BTEMP_THERMAL_MED_LIMIT		0
#define BTEMP_THERMAL_HIGH_LIMIT_62	62

#define BTEMP_BATCTRL_CURR_SRC_7UA	7
#define BTEMP_BATCTRL_CURR_SRC_15UA	15
#define BTEMP_BATCTRL_CURR_SRC_20UA	20

#define UART_MODE			0x0F
#define BAT_CUR_SRC			0x1F
#define RESIS_ID_MODE			0x03
#define RESET				0x00
#define ADOUT_10K_PULL_UP		0x07

#define to_ab5500_btemp_device_info(x) container_of((x), \
	struct ab5500_btemp, btemp_psy);

/**
 * struct ab5500_btemp_interrupts - ab5500 interrupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab5500_btemp_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab5500_btemp_events {
	bool batt_rem;
	bool usb_conn;
};

/**
 * struct ab5500_btemp - ab5500 BTEMP device information
 * @dev:		Pointer to the structure device
 * @chip_id:		Chip-Id of the AB5500
 * @curr_source:	What current source we use, in uA
 * @bat_temp:		Battery temperature in degree Celcius
 * @prev_bat_temp	Last dispatched battery temperature
 * @node:		struct of type list_head
 * @parent:		Pointer to the struct ab5500
 * @gpadc:		Pointer to the struct gpadc
 * @gpadc-auto:		Pointer to the struct adc_auto_input
 * @pdata:		Pointer to the ab5500_btemp platform data
 * @bat:		Pointer to the ab5500_bm platform data
 * @btemp_psy:		Structure for BTEMP specific battery properties
 * @events:		Structure for information about events triggered
 * @btemp_wq:		Work queue for measuring the temperature periodically
 * @btemp_periodic_work:	Work for measuring the temperature periodically
 */
struct ab5500_btemp {
	struct device *dev;
	u8 chip_id;
	int curr_source;
	int bat_temp;
	int prev_bat_temp;
	struct list_head node;
	struct ab5500 *parent;
	struct ab5500_gpadc *gpadc;
	struct adc_auto_input *gpadc_auto;
	struct abx500_btemp_platform_data *pdata;
	struct abx500_bm_data *bat;
	struct power_supply btemp_psy;
	struct ab5500_btemp_events events;
	struct workqueue_struct *btemp_wq;
	struct delayed_work btemp_periodic_work;
};

/* BTEMP power supply properties */
static enum power_supply_property ab5500_btemp_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
};

static LIST_HEAD(ab5500_btemp_list);

static int ab5500_btemp_bat_temp_trig(int mux);

struct ab5500_btemp *ab5500_btemp_get(void)
{
	struct ab5500_btemp *di;
	di = list_first_entry(&ab5500_btemp_list, struct ab5500_btemp, node);

	return di;
}

/**
 * ab5500_btemp_get_batctrl_temp() - get the temperature
 * @di:      pointer to the ab5500_btemp structure
 *
 * Returns the batctrl temperature in millidegrees
 */
int ab5500_btemp_get_batctrl_temp(struct ab5500_btemp *di)
{
	return di->bat_temp * 1000;
}

/**
 * ab5500_btemp_volt_to_res() - convert batctrl voltage to resistance
 * @di:		pointer to the ab5500_btemp structure
 * @volt:	measured batctrl/btemp_ball voltage
 * @batcrtl:	batctrl/btemp_ball node
 *
 * This function returns the battery resistance that is
 * derived from the BATCTRL/BTEMP_BALL voltage.
 * Returns value in Ohms.
 */
static int ab5500_btemp_volt_to_res(struct ab5500_btemp *di,
	int volt, bool batctrl)
{
	int rbs;

	if (batctrl) {
		/*
		 * If the battery has internal NTC, we use the current
		 * source to calculate the resistance, 7uA or 20uA
		 */
		rbs = volt * 1000 / di->curr_source;
	} else {
		/*
		 * BTEMP_BALL is internally
		 * connected to 1.8V through a 10k resistor
		 */
		rbs = (10000 * volt) / (1800 - volt);
	}
	return rbs;
}

/**
 * ab5500_btemp_read_batctrl_voltage() - measure batctrl voltage
 * @di:		pointer to the ab5500_btemp structure
 *
 * This function returns the voltage on BATCTRL. Returns value in mV.
 */
static int ab5500_btemp_read_batctrl_voltage(struct ab5500_btemp *di)
{
	int vbtemp;
	static int prev;

	vbtemp = ab5500_gpadc_convert(di->gpadc, BAT_CTRL);
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
 * ab5500_btemp_curr_source_enable() - enable/disable batctrl current source
 * @di:		pointer to the ab5500_btemp structure
 * @enable:	enable or disable the current source
 *
 * Enable or disable the current sources for the BatCtrl AD channel
 */
static int ab5500_btemp_curr_source_enable(struct ab5500_btemp *di,
	bool enable)
{
	int ret = 0;

	/* Only do this for batteries with internal NTC */
	if (di->bat->adc_therm == ABx500_ADC_THERM_BATCTRL && enable) {

		dev_dbg(di->dev, "Set BATCTRL %duA\n", di->curr_source);

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_UART,
			UART_MODE, RESIS_ID_MODE);
		if (ret) {
			dev_err(di->dev,
				"%s failed setting resistance identification mode\n",
				__func__);
			return ret;
		}

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_URI,
			BAT_CUR_SRC, BAT_CTRL_15U_ENA);
		if (ret) {
			dev_err(di->dev, "%s failed enabling current source\n",
				__func__);
			goto disable_curr_source;
		}
	} else if (di->bat->adc_therm == ABx500_ADC_THERM_BATCTRL && !enable) {
		dev_dbg(di->dev, "Disable BATCTRL curr source\n");

		/* Write 0 to the curr bits */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_URI,
			BAT_CUR_SRC, RESET);
		if (ret) {
			dev_err(di->dev, "%s failed disabling current source\n",
				__func__);
			goto disable_curr_source;
		}

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_UART,
			UART_MODE, RESET);
		if (ret) {
			dev_err(di->dev, "%s failed disabling force comp\n",
				__func__);
		}
	}
	return ret;
disable_curr_source:
	/* Write 0 to the curr bits */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB5500_BANK_FG_BATTCOM_ACC, AB5500_URI,
			BAT_CUR_SRC, RESET);
	if (ret) {
		dev_err(di->dev, "%s failed disabling current source\n",
			__func__);
	}
	return ret;
}

/**
 * ab5500_btemp_get_batctrl_res() - get battery resistance
 * @di:		pointer to the ab5500_btemp structure
 *
 * This function returns the battery pack identification resistance.
 * Returns value in Ohms.
 */
static int ab5500_btemp_get_batctrl_res(struct ab5500_btemp *di)
{
	int ret;
	int batctrl;
	int res;

	ret = ab5500_btemp_curr_source_enable(di, true);
	/* TODO: This delay has to be optimised */
	msleep(100);
	if (ret) {
		dev_err(di->dev, "%s curr source enable failed\n", __func__);
		return ret;
	}

	batctrl = ab5500_btemp_read_batctrl_voltage(di);
	res = ab5500_btemp_volt_to_res(di, batctrl, true);

	ret = ab5500_btemp_curr_source_enable(di, false);
	if (ret) {
		dev_err(di->dev, "%s curr source disable failed\n", __func__);
		return ret;
	}

	dev_dbg(di->dev, "%s batctrl: %d res: %d ",
		__func__, batctrl, res);

	return res;
}

/**
 * ab5500_btemp_get_btemp_ball_res() - get battery resistance
 * @di:		pointer to the ab5500_btemp structure
 *
 * This function returns the battery pack identification
 * resistance using resistor pull-up mode. Returns value in Ohms.
 */
static int ab5500_btemp_get_btemp_ball_res(struct ab5500_btemp *di)
{
	int ret, vntc;

	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB5500_BANK_FG_BATTCOM_ACC, AB5500_UART,
		UART_MODE, ADOUT_10K_PULL_UP);
	if (ret) {
		dev_err(di->dev,
			"failed to enable 10k pull up to Vadout\n");
			return ret;
	}

	vntc = ab5500_gpadc_convert(di->gpadc, BTEMP_BALL);
	if (vntc < 0) {
		dev_err(di->dev, "%s gpadc conversion failed,"
				" using previous value\n", __func__);
		return vntc;
	}

	return ab5500_btemp_volt_to_res(di, vntc, false);
}

/**
 * ab5500_btemp_temp_to_res() - temperature to resistance
 * @di:		pointer to the ab5500_btemp structure
 * @tbl:	pointer to the resiatance to temperature table
 * @tbl_size:	size of the resistance to temperature table
 * @temp:	temperature to calculate the resistance from
 *
 * This function returns the battery resistance in ohms
 * based on temperature.
 */
static int ab5500_btemp_temp_to_res(struct ab5500_btemp *di,
	const struct abx500_res_to_temp *tbl, int tbl_size, int temp)
{
	int i, res;
	/*
	 * Calculate the formula for the straight line
	 * Simple interpolation if we are within
	 * the resistance table limits, extrapolate
	 * if resistance is outside the limits.
	 */
	if (temp < tbl[0].temp)
		i = 0;
	else if (temp >= tbl[tbl_size - 1].temp)
		i = tbl_size - 2;
	else {
		i = 0;
		while (!(temp >= tbl[i].temp &&
			temp < tbl[i + 1].temp))
			i++;
	}

	res = tbl[i].resist + ((tbl[i + 1].resist - tbl[i].resist) *
		(temp - tbl[i].temp)) / (tbl[i + 1].temp - tbl[i].temp);
	return res;
}

/**
 * ab5500_btemp_temp_to_volt() - temperature to adc voltage
 * @di:		pointer to the ab5500_btemp structure
 * @temp:	temperature to calculate the voltage from
 *
 * This function returns the adc voltage in millivolts
 * based on temperature.
 */
static int ab5500_btemp_temp_to_volt(struct ab5500_btemp *di, int temp)
{
	int res, id;

	id = di->bat->batt_id;
	res = ab5500_btemp_temp_to_res(di,
		di->bat->bat_type[id].r_to_t_tbl,
                di->bat->bat_type[id].n_temp_tbl_elements,
		temp);
	/*
	 * BTEMP_BALL is internally connected to 1.8V
	 * through a 10k resistor
	 */
	return((1800 * res) / (10000 + res));
}

/**
 * ab5500_btemp_res_to_temp() - resistance to temperature
 * @di:		pointer to the ab5500_btemp structure
 * @tbl:	pointer to the resiatance to temperature table
 * @tbl_size:	size of the resistance to temperature table
 * @res:	resistance to calculate the temperature from
 *
 * This function returns the battery temperature in degrees Celcius
 * based on the NTC resistance.
 */
static int ab5500_btemp_res_to_temp(struct ab5500_btemp *di,
	const struct abx500_res_to_temp *tbl, int tbl_size, int res)
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
 * ab5500_btemp_measure_temp() - measure battery temperature
 * @di:		pointer to the ab5500_btemp structure
 *
 * Returns battery temperature (on success) else the previous temperature
 */
static int ab5500_btemp_measure_temp(struct ab5500_btemp *di)
{
	int temp, rbat;
	u8 id;

	id = di->bat->batt_id;
	if (di->bat->adc_therm == ABx500_ADC_THERM_BATCTRL &&
			id != BATTERY_UNKNOWN && !di->bat->auto_trig)
		rbat = ab5500_btemp_get_batctrl_res(di);
	else
		rbat = ab5500_btemp_get_btemp_ball_res(di);

	if (rbat < 0) {
		dev_err(di->dev, "%s failed to get resistance\n", __func__);
		/*
		 * Return out-of-range temperature so that
		 * charging is stopped
		 */
		return BTEMP_THERMAL_LOW_LIMIT;
	}

	temp = ab5500_btemp_res_to_temp(di,
		di->bat->bat_type[id].r_to_t_tbl,
		di->bat->bat_type[id].n_temp_tbl_elements, rbat);
	dev_dbg(di->dev, "Battery temperature is %d\n", temp);

	return temp;
}

/**
 * ab5500_btemp_id() - Identify the connected battery
 * @di:		pointer to the ab5500_btemp structure
 *
 * This function will try to identify the battery by reading the ID
 * resistor. Some brands use a combined ID resistor with a NTC resistor to
 * both be able to identify and to read the temperature of it.
 */
static int ab5500_btemp_id(struct ab5500_btemp *di)
{
	int res;
	u8 i;

	di->curr_source = BTEMP_BATCTRL_CURR_SRC_7UA;
	di->bat->batt_id = BATTERY_UNKNOWN;

	res =  ab5500_btemp_get_batctrl_res(di);
	if (res < 0) {
		dev_err(di->dev, "%s get batctrl res failed\n", __func__);
		return -ENXIO;
	}

	/* BATTERY_UNKNOWN is defined on position 0, skip it! */
	for (i = BATTERY_UNKNOWN + 1; i < di->bat->n_btypes; i++) {
		if ((res <= di->bat->bat_type[i].resis_high) &&
			(res >= di->bat->bat_type[i].resis_low)) {
			dev_dbg(di->dev, "Battery detected on %s"
				" low %d < res %d < high: %d"
				" index: %d\n",
				di->bat->adc_therm == ABx500_ADC_THERM_BATCTRL ?
				"BATCTRL" : "BATTEMP",
				di->bat->bat_type[i].resis_low, res,
				di->bat->bat_type[i].resis_high, i);

			di->bat->batt_id = i;
			break;
		}
	}

	if (di->bat->batt_id == BATTERY_UNKNOWN) {
		dev_warn(di->dev, "Battery identified as unknown"
			", resistance %d Ohm\n", res);
		return -ENXIO;
	}

	/*
	 * We only have to change current source if the
	 * detected type is Type 1, else we use the 7uA source
	 */
	if (di->bat->adc_therm == ABx500_ADC_THERM_BATCTRL &&
			di->bat->batt_id == 1) {
		dev_dbg(di->dev, "Set BATCTRL current source to 15uA\n");
		di->curr_source = BTEMP_BATCTRL_CURR_SRC_15UA;
	}

	return di->bat->batt_id;
}

/**
 * ab5500_btemp_periodic_work() - Measuring the temperature periodically
 * @work:	pointer to the work_struct structure
 *
 * Work function for measuring the temperature periodically
 */
static void ab5500_btemp_periodic_work(struct work_struct *work)
{
	struct ab5500_btemp *di = container_of(work,
		struct ab5500_btemp, btemp_periodic_work.work);

	di->bat_temp = ab5500_btemp_measure_temp(di);

	if (di->bat_temp != di->prev_bat_temp) {
		di->prev_bat_temp = di->bat_temp;
		power_supply_changed(&di->btemp_psy);
	}
	di->bat->temp_now = di->bat_temp;

	if (!di->bat->auto_trig) {
		/* Check for temperature limits */
		if (di->bat_temp <= BTEMP_THERMAL_LOW_LIMIT) {
			dev_err(di->dev,
				"battery temp less than lower threshold\n");
			power_supply_changed(&di->btemp_psy);
		} else if (di->bat_temp >= BTEMP_THERMAL_HIGH_LIMIT_62) {
			dev_err(di->dev,
				"battery temp greater them max threshold\n");
			power_supply_changed(&di->btemp_psy);
		}

		/* Schedule a new measurement */
		if (di->events.usb_conn)
			queue_delayed_work(di->btemp_wq,
				&di->btemp_periodic_work,
				round_jiffies(di->bat->interval_charging * HZ));
		else
			queue_delayed_work(di->btemp_wq,
				&di->btemp_periodic_work,
				round_jiffies(di->bat->interval_not_charging * HZ));
	} else {
		/* Schedule a new measurement */
		queue_delayed_work(di->btemp_wq,
			&di->btemp_periodic_work,
			round_jiffies(di->bat->interval_charging * HZ));
	}
}

/**
 * ab5500_btemp_batt_removal_handler() - battery removal detected
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab5500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_btemp_batt_removal_handler(int irq, void *_di)
{
	struct ab5500_btemp *di = _di;
	dev_err(di->dev, "Battery removal detected!\n");

	di->events.batt_rem = true;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab5500_btemp_batt_attach_handler() - battery insertion detected
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab5500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_btemp_batt_attach_handler(int irq, void *_di)
{
	struct ab5500_btemp *di = _di;
	dev_err(di->dev, "Battery attached!\n");

	di->events.batt_rem = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab5500_btemp_periodic() - Periodic temperature measurements
 * @di:		pointer to the ab5500_btemp structure
 * @enable:	enable or disable periodic temperature measurements
 *
 * Starts of stops periodic temperature measurements. Periodic measurements
 * should only be done when a charger is connected.
 */
static void ab5500_btemp_periodic(struct ab5500_btemp *di,
	bool enable)
{
	dev_dbg(di->dev, "Enable periodic temperature measurements: %d\n",
		enable);

	if (enable)
		queue_delayed_work(di->btemp_wq, &di->btemp_periodic_work, 0);
	else
		cancel_delayed_work_sync(&di->btemp_periodic_work);
}

/**
 * ab5500_btemp_get_property() - get the btemp properties
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
static int ab5500_btemp_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab5500_btemp *di;

	di = to_ab5500_btemp_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		if (di->events.batt_rem)
			val->intval = 0;
		else
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = di->bat->bat_type[di->bat->batt_id].name;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (di->bat->batt_id == BATTERY_UNKNOWN)
		/*
		 * In case the battery is not identified, its assumed that
		 * we are using the power supply and since no monitoring is
		 * done for the same, a nominal temp is hardocded.
		 */
			val->intval = 250;
		else
			val->intval = di->bat_temp * 10;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ab5500_btemp_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab5500_btemp *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_ab5500_btemp_device_info(psy);

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
			case POWER_SUPPLY_TYPE_USB:
				/* USB disconnected */
				if (!ret.intval && di->events.usb_conn) {
					di->events.usb_conn = false;
					if (di->bat->auto_trig)
						ab5500_btemp_periodic(di,
							false);
				}
				/* USB connected */
				else if (ret.intval && !di->events.usb_conn) {
					di->events.usb_conn = true;
					if (di->bat->auto_trig)
						ab5500_btemp_periodic(di, true);
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
 * ab5500_btemp_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is pointing to the function pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in the external power
 * supply to the btemp.
 */
static void ab5500_btemp_external_power_changed(struct power_supply *psy)
{
	struct ab5500_btemp *di = to_ab5500_btemp_device_info(psy);

	class_for_each_device(power_supply_class, NULL,
		&di->btemp_psy, ab5500_btemp_get_ext_psy_data);
}

/* ab5500 btemp driver interrupts and their respective isr */
static struct ab5500_btemp_interrupts ab5500_btemp_irq[] = {
	{"BATT_REMOVAL", ab5500_btemp_batt_removal_handler},
	{"BATT_ATTACH", ab5500_btemp_batt_attach_handler},
};

static int ab5500_btemp_bat_temp_trig(int mux)
{
	struct ab5500_btemp *di = ab5500_btemp_get();
	int temp = ab5500_btemp_measure_temp(di);

	if (temp < (BTEMP_THERMAL_LOW_LIMIT+1)) {
		dev_err(di->dev,
			"battery temp less than lower threshold (-10 deg cel)\n");
		power_supply_changed(&di->btemp_psy);
	} else if (temp > (BTEMP_THERMAL_HIGH_LIMIT_62-1)) {
		dev_err(di->dev, "battery temp greater them max threshold\n");
		power_supply_changed(&di->btemp_psy);
	}

	return 0;
}

static int ab5500_btemp_auto_temp(struct ab5500_btemp *di)
{
	struct adc_auto_input *auto_ip;
	int ret = 0;

	auto_ip = kzalloc(sizeof(struct adc_auto_input), GFP_KERNEL);
	if (!auto_ip) {
		dev_err(di->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	auto_ip->mux = BTEMP_BALL;
	auto_ip->freq = MS500;
	auto_ip->min = ab5500_btemp_temp_to_volt(di,
				BTEMP_THERMAL_HIGH_LIMIT_62);
	auto_ip->max = ab5500_btemp_temp_to_volt(di,
				BTEMP_THERMAL_LOW_LIMIT);
	auto_ip->auto_adc_callback = ab5500_btemp_bat_temp_trig;
	di->gpadc_auto = auto_ip;
	ret = ab5500_gpadc_convert_auto(di->gpadc, di->gpadc_auto);
	if (ret)
		dev_err(di->dev,
			"failed to set auto trigger for battery temp\n");
	return ret;
}

#if defined(CONFIG_PM)
static int ab5500_btemp_resume(struct platform_device *pdev)
{
	struct ab5500_btemp *di = platform_get_drvdata(pdev);

	if (di->events.usb_conn)
		ab5500_btemp_periodic(di, true);

	return 0;
}

static int ab5500_btemp_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab5500_btemp *di = platform_get_drvdata(pdev);

	if (di->events.usb_conn)
		ab5500_btemp_periodic(di, false);

	return 0;
}
#else
#define ab5500_btemp_suspend      NULL
#define ab5500_btemp_resume       NULL
#endif

static int __devexit ab5500_btemp_remove(struct platform_device *pdev)
{
	struct ab5500_btemp *di = platform_get_drvdata(pdev);
	int i, irq;

	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab5500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab5500_btemp_irq[i].name);
		free_irq(irq, di);
	}

	/* Delete the work queue */
	destroy_workqueue(di->btemp_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->btemp_psy);
	platform_set_drvdata(pdev, NULL);
	kfree(di->gpadc_auto);
	kfree(di);

	return 0;
}

static int __devinit ab5500_btemp_probe(struct platform_device *pdev)
{
	int irq, i, ret = 0;
	struct abx500_bm_plat_data *plat_data;

	struct ab5500_btemp *di =
		kzalloc(sizeof(struct ab5500_btemp), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab5500_gpadc_get("ab5500-adc.0");

	plat_data = pdev->dev.platform_data;
	di->pdata = plat_data->btemp;
	di->bat = plat_data->battery;

	/* get btemp specific platform data */
	if (!di->pdata) {
		dev_err(di->dev, "no btemp platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}

	/* get battery specific platform data */
	if (!di->bat) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}

	/* BTEMP supply */
	di->btemp_psy.name = "ab5500_btemp";
	di->btemp_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->btemp_psy.properties = ab5500_btemp_props;
	di->btemp_psy.num_properties = ARRAY_SIZE(ab5500_btemp_props);
	di->btemp_psy.get_property = ab5500_btemp_get_property;
	di->btemp_psy.supplied_to = di->pdata->supplied_to;
	di->btemp_psy.num_supplicants = di->pdata->num_supplicants;
	di->btemp_psy.external_power_changed =
		ab5500_btemp_external_power_changed;


	/* Create a work queue for the btemp */
	di->btemp_wq =
		create_singlethread_workqueue("ab5500_btemp_wq");
	if (di->btemp_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for measuring temperature periodically */
	INIT_DELAYED_WORK_DEFERRABLE(&di->btemp_periodic_work,
		ab5500_btemp_periodic_work);

	/* Get Chip ID of the ABB ASIC  */
	ret = abx500_get_chip_id(di->dev);
	if (ret < 0) {
		dev_err(di->dev, "failed to get chip ID\n");
		goto free_btemp_wq;
	}
	di->chip_id = ret;
	dev_dbg(di->dev, "ab5500 CID is: 0x%02x\n",
		di->chip_id);

	/* Identify the battery */
	if (ab5500_btemp_id(di) < 0)
		dev_warn(di->dev, "failed to identify the battery\n");

	/* Measure temperature once initially */
	di->bat_temp = ab5500_btemp_measure_temp(di);
	di->bat->temp_now = di->bat_temp;

	/* Register BTEMP power supply class */
	ret = power_supply_register(di->dev, &di->btemp_psy);
	if (ret) {
		dev_err(di->dev, "failed to register BTEMP psy\n");
		goto free_btemp_wq;
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab5500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab5500_btemp_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab5500_btemp_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab5500_btemp_irq[i].name, di);

		if (ret) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab5500_btemp_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab5500_btemp_irq[i].name, irq, ret);
	}

	if (!di->bat->auto_trig) {
		/* Schedule monitoring work only if battery type is known */
		if (di->bat->batt_id != BATTERY_UNKNOWN)
			queue_delayed_work(di->btemp_wq, &di->btemp_periodic_work, 0);
	} else {
		ret = ab5500_btemp_auto_temp(di);
		if (ret) {
			dev_err(di->dev,
				"failed to register auto trigger for battery temp\n");
			goto free_irq;
		}
	}

	platform_set_drvdata(pdev, di);
	list_add_tail(&di->node, &ab5500_btemp_list);

	dev_info(di->dev, "probe success\n");
	return ret;

free_irq:
	power_supply_unregister(&di->btemp_psy);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab5500_btemp_irq[i].name);
		free_irq(irq, di);
	}
free_btemp_wq:
	destroy_workqueue(di->btemp_wq);
free_device_info:
	kfree(di);

	return ret;
}

static struct platform_driver ab5500_btemp_driver = {
	.probe = ab5500_btemp_probe,
	.remove = __devexit_p(ab5500_btemp_remove),
	.suspend = ab5500_btemp_suspend,
	.resume = ab5500_btemp_resume,
	.driver = {
		.name = "ab5500-btemp",
		.owner = THIS_MODULE,
	},
};

static int __init ab5500_btemp_init(void)
{
	return platform_driver_register(&ab5500_btemp_driver);
}

static void __exit ab5500_btemp_exit(void)
{
	platform_driver_unregister(&ab5500_btemp_driver);
}

subsys_initcall_sync(ab5500_btemp_init);
module_exit(ab5500_btemp_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab5500-btemp");
MODULE_DESCRIPTION("AB5500 battery temperature driver");
