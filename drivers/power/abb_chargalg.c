/*
 * copyright875 (c) ST-Ericsson SA 2010
 *
 * Charging algorithm driver for AB8500
 *
 * License Terms: GNU General Public License v2
 * Author: Johan Palsson <johan.palsson@stericsson.com>
 * Author: Karl Komierowski <karl.komierowski@stericsson.com>
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
#include <linux/kobject.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>

/* Watchdog kick interval */
#define CHG_WD_INTERVAL			(60 * HZ)

/* End-of-charge criteria counter */
#define EOC_COND_CNT_1ST			5  /* 1st Full Charging check */
#define EOC_COND_CNT_2ND			10 /* 2nd Full Charging Check */

/* OVV check delay time */
#define OVV_DELAY			2

/* Recharge criteria counter */
#define RCH_COND_CNT			3

#define to_ab8500_chargalg_device_info(x) container_of((x), \
	struct ab8500_chargalg, chargalg_psy);

enum ab8500_chargers {
	NO_CHG,
	AC_CHG,
	USB_CHG,
};

struct ab8500_chargalg_charger_info {
	enum ab8500_chargers conn_chg;
	enum ab8500_chargers prev_conn_chg;
	enum ab8500_chargers online_chg;
	enum ab8500_chargers prev_online_chg;
	enum ab8500_chargers charger_type;
	bool usb_chg_ok;
	bool ac_chg_ok;
	int usb_volt;
	int usb_curr;
	int ac_volt;
	int ac_curr;
	int usb_vset;
	int usb_iset;
	int ac_vset;
	int ac_iset;
};

struct ab8500_chargalg_suspension_status {
	bool suspended_change;
	bool ac_suspended;
	bool usb_suspended;
};

struct ab8500_chargalg_battery_data {
	int temp;
	int volt;
	int avg_curr;
	int inst_curr;
	int percent;
};

enum ab8500_chargalg_states {
	STATE_HANDHELD_INIT,
	STATE_HANDHELD,
	STATE_CHG_NOT_OK_INIT,
	STATE_CHG_NOT_OK,
	STATE_HW_TEMP_PROTECT_INIT,
	STATE_HW_TEMP_PROTECT,
	STATE_NORMAL_INIT,
	STATE_NORMAL,
	STATE_WAIT_FOR_RECHARGE_INIT,
	STATE_WAIT_FOR_RECHARGE,
	STATE_MAINTENANCE_A_INIT,
	STATE_MAINTENANCE_A,
	STATE_MAINTENANCE_B_INIT,
	STATE_MAINTENANCE_B,
	STATE_TEMP_UNDEROVER_INIT,
	STATE_TEMP_UNDEROVER,
	STATE_TEMP_LOWHIGH_INIT,
	STATE_TEMP_LOWHIGH,
	STATE_SUSPENDED_INIT,
	STATE_SUSPENDED,
	STATE_OVV_PROTECT_INIT,
	STATE_OVV_PROTECT,
	STATE_SAFETY_TIMER_EXPIRED_INIT,
	STATE_SAFETY_TIMER_EXPIRED,
	STATE_BATT_REMOVED_INIT,
	STATE_BATT_REMOVED,
	STATE_WD_EXPIRED_INIT,
	STATE_WD_EXPIRED,
	STATE_CHARGE_TIMEOUT_INIT,
	STATE_CHARGE_TIMEOUT,
	STATE_TIMED_OUT_CHARGING_INIT,
	STATE_TIMED_OUT_CHARGING,

};

static const char *states[] = {
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

struct ab8500_chargalg_events {
	bool batt_unknown;
	bool mainextchnotok;
	bool batt_ovv;
	bool batt_rem;
	bool btemp_underover;
	bool btemp_lowhigh;
	bool main_thermal_prot;
	bool usb_thermal_prot;
	bool main_ovv;
	bool vbus_ovv;
	bool usbchargernotok;
	bool safety_timer_expired;
	bool maintenance_timer_expired;
	bool ac_wd_expired;
	bool usb_wd_expired;
	bool ac_cv_active;
	bool usb_cv_active;
	bool vbus_collapsed;
};





/**
 * struct ab8500_charge_curr_maximization - Charger maximization parameters
 * @original_iset:	the non optimized/maximised charger current
 * @current_iset:	the charging current used at this moment
 * @test_delta_i:	the delta between the current we want to charge and the
			current that is really going into the battery
 * @condition_cnt:	number of iterations needed before a new charger current
			is set
 * @max_current:	maximum charger current
 * @wait_cnt:		to avoid too fast current step down in case of charger
 *			voltage collapse, we insert this delay between step
 *			down
 * @level:		tells in how many steps the charging current has been
			increased
 */
struct ab8500_charge_curr_maximization {
	int original_iset;
	int current_iset;
	int test_delta_i;
	int condition_cnt;
	int max_current;
	int wait_cnt;
	u8 level;
};

enum maxim_ret {
	MAXIM_RET_NOACTION,
	MAXIM_RET_CHANGE,
	MAXIM_RET_IBAT_TOO_HIGH,
};

/**
 * struct ab8500_chargalg - ab8500 Charging algorithm device information
 * @dev:		pointer to the structure device
 * @charge_status:	battery operating status
 * @eoc_cnt_1st:		counter used to determine end-of_charge (1st full charging check)
 * @eoc_cnt_2nd:		counter used to determine end-of_charge (2nd full charging check)
 * @rch_cnt:		counter used to determine start of recharge
 * @maintenance_chg:	indicate if maintenance charge is active
 * @recharging_status:	indicate if recharging is active
 * @full_charging_status_1st:	indicate 1st full charging status
 * @t_hyst_norm		temperature hysteresis when the temperature has	been
 *			over or under normal limits
 * @t_hyst_lowhigh	temperature hysteresis when the temperature has	been
 *			over or under the high or low limits
 * @charge_state:	current state of the charging algorithm
 * @ccm			charging current maximization parameters
 * @chg_info:		information about connected charger types
 * @batt_data:		data of the battery
 * @susp_status:	current charger suspension status
 * @parent:		pointer to the struct ab8500
 * @pdata:		pointer to the ab8500_chargalg platform data
 * @bat:		pointer to the ab8500_bm platform data
 * @chargalg_psy:	structure that holds the battery properties exposed by
 *			the charging algorithm
 * @events:		structure for information about events triggered
 * @chargalg_wq:		work queue for running the charging algorithm
 * @chargalg_periodic_work:	work to run the charging algorithm periodically
 * @chargalg_wd_work:		work to kick the charger watchdog periodically
 * @chargalg_work:		work to run the charging algorithm instantly
 * @safety_timer:		charging safety timer
 * @maintenance_timer:		maintenance charging timer
 * @chargalg_kobject:		structure of type kobject
 */
struct ab8500_chargalg {
	struct device *dev;
	int charge_status;
	int timeouttimer_active;
	unsigned long charge_timeout_jiffies ;
	unsigned long timeout_chargeoff_jiffies;
	unsigned long error_jiffies ;
	unsigned long ovv_jiffies;
	unsigned long ovv_recovery_jiffies;
	int initial_timeout_expire ;
	int initial_timeout_expire_under_threshold;
	int eoc_cnt_1st;
	int eoc_cnt_2nd;
	int rch_cnt;
	int check_cnt;
	bool check_state;
	bool maintenance_chg;
	bool recharging_status;
	bool full_charging_status_1st;
	bool lpm_chg_mode;
	bool siop_activated;
	int t_hyst_norm;
	int t_hyst_lowhigh;
	enum ab8500_chargalg_states charge_state;
	enum ab8500_chargalg_states charge_prev_state;
	struct ab8500_charge_curr_maximization ccm;
	struct ab8500_chargalg_charger_info chg_info;
	struct ab8500_chargalg_battery_data batt_data;
	struct ab8500_chargalg_suspension_status susp_status;
	struct ab8500 *parent;
	struct ab8500_chargalg_platform_data *pdata;
	struct ab8500_bm_data *bat;
	struct power_supply chargalg_psy;
	struct ux500_charger *ac_chg;
	struct ux500_charger *usb_chg;
	struct ab8500_chargalg_events events;
	struct workqueue_struct *chargalg_wq;
	struct delayed_work chargalg_periodic_work;
	struct delayed_work chargalg_wd_work;
	struct work_struct chargalg_work;
	struct timer_list safety_timer;
	struct timer_list maintenance_timer;
	struct kobject chargalg_kobject;
};




static unsigned long get_charge_timeout_duration( struct ab8500_chargalg * di)
{
	unsigned long ret;

	if (di->initial_timeout_expire) {
		if (di->initial_timeout_expire_under_threshold)
			ret =
			di->bat->bat_type[
				di->bat->batt_id].initial_timeout_time;
		else
			ret =
			di->bat->bat_type[
				di->bat->batt_id].subsequent_timeout_time;
	} else {
		ret = di->bat->bat_type[di->bat->batt_id].initial_timeout_time;
	}

	return ret;
}

/**
 * get_charging_start_state() - After handling exception what state to go into.
 * @data:	pointer to the ab8500_chargalg structure
 *
 * This function gets called after handling and exception/interrupt.
 */

static int get_charging_start_state( struct ab8500_chargalg * di)
{
	int state;

	switch ( di->charge_state ){
	case STATE_HANDHELD_INIT:
	case STATE_HANDHELD:
	case STATE_WAIT_FOR_RECHARGE_INIT:
	case STATE_WAIT_FOR_RECHARGE:
	case STATE_NORMAL_INIT:
	case STATE_NORMAL:
	case STATE_MAINTENANCE_A_INIT:
	case STATE_MAINTENANCE_A:
	case STATE_MAINTENANCE_B_INIT:
	case STATE_MAINTENANCE_B:
	case STATE_SUSPENDED_INIT:
	case STATE_SUSPENDED:
	case STATE_SAFETY_TIMER_EXPIRED_INIT:
	case STATE_SAFETY_TIMER_EXPIRED:
	case STATE_CHARGE_TIMEOUT_INIT:
	case STATE_CHARGE_TIMEOUT:
	case STATE_TIMED_OUT_CHARGING_INIT:
	case STATE_TIMED_OUT_CHARGING:
	case STATE_CHG_NOT_OK_INIT:
	case STATE_HW_TEMP_PROTECT_INIT:
	case STATE_TEMP_UNDEROVER_INIT:
	case STATE_OVV_PROTECT_INIT:
	case STATE_WD_EXPIRED_INIT:
	case STATE_TEMP_LOWHIGH_INIT:
	case STATE_TEMP_LOWHIGH:
	case STATE_BATT_REMOVED_INIT:
		dev_dbg(di->dev,"Invalid State: [%d] %s\n", di->charge_state,
			states[di->charge_state]);
		state = di->charge_state;
		break;

	case STATE_HW_TEMP_PROTECT:
	case STATE_TEMP_UNDEROVER:
	case STATE_WD_EXPIRED:
	case STATE_OVV_PROTECT:
	case STATE_CHG_NOT_OK:
		switch ( di->charge_prev_state ){
		case STATE_CHARGE_TIMEOUT_INIT:
		case STATE_CHARGE_TIMEOUT:
		case STATE_TIMED_OUT_CHARGING_INIT:
		case STATE_TIMED_OUT_CHARGING:
			state = STATE_TIMED_OUT_CHARGING_INIT;
			break;

		default:
			state = STATE_NORMAL_INIT;
			break;
		}
		break;

	case STATE_BATT_REMOVED:
		if ( di->charge_prev_state == STATE_HANDHELD ||
				di->charge_prev_state == STATE_HANDHELD_INIT )
			state = di->charge_prev_state;
		else
			state = STATE_NORMAL_INIT;

		break;

	default:
		dev_err(di->dev,"Unknown State: [%d] %s\n", di->charge_state,
			states[di->charge_state]);
		state = STATE_HANDHELD_INIT;
		break;
	}

	return state;
}

/* Main battery properties */
static enum power_supply_property ab8500_chargalg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_UI_FULL,
	POWER_SUPPLY_PROP_CHARGING_TIMEOUT,
	POWER_SUPPLY_PROP_CHARGING_SOURCE,
};

/**
 * ab8500_chargalg_safety_timer_expired() - Expiration of the safety timer
 * @data:	pointer to the ab8500_chargalg structure
 *
 * This function gets called when the safety timer for the charger
 * expires
 */
static void ab8500_chargalg_safety_timer_expired(unsigned long data)
{
	struct ab8500_chargalg *di = (struct ab8500_chargalg *) data;
	dev_err(di->dev, "Safety timer expired\n");
	di->events.safety_timer_expired = true;

	/* Trigger execution of the algorithm instantly */
	queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * ab8500_chargalg_maintenance_timer_expired() - Expiration of
 * the maintenance timer
 * @i:		pointer to the ab8500_chargalg structure
 *
 * This function gets called when the maintenence timer
 * expires
 */
static void ab8500_chargalg_maintenance_timer_expired(unsigned long data)
{

	struct ab8500_chargalg *di = (struct ab8500_chargalg *) data;
	dev_dbg(di->dev, "Maintenance timer expired\n");
	di->events.maintenance_timer_expired = true;

	/* Trigger execution of the algorithm instantly */
	queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * ab8500_chargalg_state_to() - Change charge state
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function gets called when a charge state change should occur
 */
static void ab8500_chargalg_state_to(struct ab8500_chargalg *di,
	enum ab8500_chargalg_states state)
{
	dev_info(di->dev,
		"State changed: %s (From state: [%d] %s =to=> [%d] %s )\n",
		di->charge_state == state ? "NO" : "YES",
		di->charge_state,
		states[di->charge_state],
		state,
		states[state]);

	di->charge_state = state;
	di->bat->charge_state = state;
}

/**
 * ab8500_chargalg_check_charger_connection() - Check charger connection change
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function will check if there is a change in the charger connection
 * and change charge state accordingly. AC has precedence over USB.
 */
static int ab8500_chargalg_check_charger_connection(struct ab8500_chargalg *di)
{
	if (di->chg_info.conn_chg != di->chg_info.prev_conn_chg ||
		di->susp_status.suspended_change) {
		/*
		 * Charger state changed or suspension
		 * has changed since last update
		 */
		if ((di->chg_info.conn_chg & AC_CHG) &&
			!di->susp_status.ac_suspended) {
			dev_dbg(di->dev, "Charging source is AC\n");
			if (di->chg_info.charger_type != AC_CHG) {
				di->chg_info.charger_type = AC_CHG;
				di->charge_timeout_jiffies=jiffies + get_charge_timeout_duration(di);
				di->initial_timeout_expire=0 ;
				di->initial_timeout_expire_under_threshold = 0;
				ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			}
		} else if ((di->chg_info.conn_chg & USB_CHG) &&
			!di->susp_status.usb_suspended) {
			dev_dbg(di->dev, "Charging source is USB\n");
			di->chg_info.charger_type = USB_CHG;
			di->initial_timeout_expire=0 ;
			di->initial_timeout_expire_under_threshold = 0;
			di->charge_timeout_jiffies=jiffies + get_charge_timeout_duration(di);
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		} else if (di->chg_info.conn_chg &&
			(di->susp_status.ac_suspended ||
			di->susp_status.usb_suspended)) {
			dev_dbg(di->dev, "Charging is suspended\n");
			di->chg_info.charger_type = NO_CHG;
			ab8500_chargalg_state_to(di, STATE_SUSPENDED_INIT);
		} else {
			dev_dbg(di->dev, "Charging source is OFF\n");
			di->chg_info.charger_type = NO_CHG;
			ab8500_chargalg_state_to(di, STATE_HANDHELD_INIT);
		}
		di->chg_info.prev_conn_chg = di->chg_info.conn_chg;
		di->susp_status.suspended_change = false;
		di->recharging_status = false;
	}
	return di->chg_info.conn_chg;
}

/**
 * ab8500_chargalg_start_safety_timer() - Start charging safety timer
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The safety timer is used to avoid overcharging of old or bad batteries.
 * There are different timers for AC and USB
 */
static void ab8500_chargalg_start_safety_timer(struct ab8500_chargalg *di)
{
	unsigned long timer_expiration = 0;

	switch (di->chg_info.charger_type) {
	case AC_CHG:
		timer_expiration =
		round_jiffies(jiffies +
			(di->bat->main_safety_tmr_h * 3600 * HZ));
		break;

	case USB_CHG:
		timer_expiration =
		round_jiffies(jiffies +
			(di->bat->usb_safety_tmr_h * 3600 * HZ));
		break;

	default:
		dev_err(di->dev, "Unknown charger to charge from\n");
		break;
	}

	di->events.safety_timer_expired = false;
	di->safety_timer.expires = timer_expiration;
	if (!timer_pending(&di->safety_timer))
		add_timer(&di->safety_timer);
	else
		mod_timer(&di->safety_timer, timer_expiration);
}

/**
 * ab8500_chargalg_stop_safety_timer() - Stop charging safety timer
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The safety timer is stopped whenever the NORMAL state is exited
 */
static void ab8500_chargalg_stop_safety_timer(struct ab8500_chargalg *di)
{
	di->events.safety_timer_expired = false;
	del_timer(&di->safety_timer);
}

/**
 * ab8500_chargalg_start_maintenance_timer() - Start charging maintenance timer
 * @di:		pointer to the ab8500_chargalg structure
 * @duration:	duration of ther maintenance timer in hours
 *
 * The maintenance timer is used to maintain the charge in the battery once
 * the battery is considered full. These timers are chosen to match the
 * discharge curve of the battery
 */
static void ab8500_chargalg_start_maintenance_timer(struct ab8500_chargalg *di,
	int duration)
{
	unsigned long timer_expiration;

	/* Convert from hours to jiffies */
	timer_expiration = round_jiffies(jiffies + (duration * 3600 * HZ));

	di->events.maintenance_timer_expired = false;
	di->maintenance_timer.expires = timer_expiration;
	if (!timer_pending(&di->maintenance_timer))
		add_timer(&di->maintenance_timer);
	else
		mod_timer(&di->maintenance_timer, timer_expiration);
}

/**
 * ab8500_chargalg_stop_maintenance_timer() - Stop maintenance timer
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The maintenance timer is stopped whenever maintenance ends or when another
 * state is entered
 */
static void ab8500_chargalg_stop_maintenance_timer(struct ab8500_chargalg *di)
{
	di->events.maintenance_timer_expired = false;
	del_timer(&di->maintenance_timer);
}

/**
 * ab8500_chargalg_kick_watchdog() - Kick charger watchdog
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The charger watchdog have to be kicked periodically whenever the charger is
 * on, else the ABB will reset the system
 */
static int ab8500_chargalg_kick_watchdog(struct ab8500_chargalg *di)
{
	/* Check if charger exists and kick watchdog if charging */
	if (di->ac_chg && di->ac_chg->ops.kick_wd &&
			di->chg_info.online_chg & AC_CHG)
		return di->ac_chg->ops.kick_wd(di->ac_chg);
	else if (di->usb_chg && di->usb_chg->ops.kick_wd &&
			di->chg_info.online_chg & USB_CHG)
		return di->usb_chg->ops.kick_wd(di->usb_chg);

	return -ENXIO;
}

/**
 * ab8500_chargalg_ac_en() - Turn on/off the AC charger
 * @di:		pointer to the ab8500_chargalg structure
 * @enable:	charger on/off
 * @vset:	requested charger output voltage
 * @iset:	requested charger output current
 *
 * The AC charger will be turned on/off with the requested charge voltage and
 * current
 */
static int ab8500_chargalg_ac_en(struct ab8500_chargalg *di, int enable,
	int vset, int iset)
{
	if (!di->ac_chg || !di->ac_chg->ops.enable)
		return -ENXIO;

	/* Select maximum of what both the charger and the battery supports */
	if (di->ac_chg->max_out_volt)
		vset = min(vset, di->ac_chg->max_out_volt);
	if (di->ac_chg->max_out_curr)
		iset = min(iset, di->ac_chg->max_out_curr);

	di->chg_info.ac_iset = iset;
	di->chg_info.ac_vset = vset;

	return di->ac_chg->ops.enable(di->ac_chg, enable, vset, iset);
}

/**
 * ab8500_chargalg_usb_en() - Turn on/off the USB charger
 * @di:		pointer to the ab8500_chargalg structure
 * @enable:	charger on/off
 * @vset:	requested charger output voltage
 * @iset:	requested charger output current
 *
 * The USB charger will be turned on/off with the requested charge voltage and
 * current
 */
static int ab8500_chargalg_usb_en(struct ab8500_chargalg *di, int enable,
	int vset, int iset)
{
	if (!di->usb_chg || !di->usb_chg->ops.enable)
		return -ENXIO;

	/* Select maximum of what both the charger and the battery supports */
	if (di->usb_chg->max_out_volt)
		vset = min(vset, di->usb_chg->max_out_volt);
	if (di->usb_chg->max_out_curr)
		iset = min(iset, di->usb_chg->max_out_curr);

	di->chg_info.usb_iset = iset;
	di->chg_info.usb_vset = vset;

	return di->usb_chg->ops.enable(di->usb_chg, enable, vset, iset);
}

/**
 * ab8500_chargalg_update_chg_curr() - Update charger current
 * @di:		pointer to the ab8500_chargalg structure
 * @iset:	requested charger output current
 *
 * The charger output current will be updated for the charger
 * that is currently in use
 */
static int ab8500_chargalg_update_chg_curr(struct ab8500_chargalg *di,
		int iset)
{
	/* Check if charger exists and update current if charging */
	if (di->ac_chg && di->ac_chg->ops.update_curr &&
			di->chg_info.charger_type & AC_CHG) {
		/*
		 * Select maximum of what both the charger
		 * and the battery supports
		 */
		if (di->ac_chg->max_out_curr)
			iset = min(iset, di->ac_chg->max_out_curr);

		di->chg_info.ac_iset = iset;

		return di->ac_chg->ops.update_curr(di->ac_chg, iset);
	} else if (di->usb_chg && di->usb_chg->ops.update_curr &&
			di->chg_info.charger_type & USB_CHG) {
		/*
		 * Select maximum of what both the charger
		 * and the battery supports
		 */
		if (di->usb_chg->max_out_curr)
			iset = min(iset, di->usb_chg->max_out_curr);

		di->chg_info.usb_iset = iset;

		return di->usb_chg->ops.update_curr(di->usb_chg, iset);
	}

	return -ENXIO;
}

/**
 * ab8500_chargalg_stop_charging() - Stop charging
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function is called from any state where charging should be stopped.
 * All charging is disabled and all status parameters and timers are changed
 * accordingly
 */
static void ab8500_chargalg_stop_charging(struct ab8500_chargalg *di)
{
	ab8500_chargalg_ac_en(di, false, 0, 0);
	ab8500_chargalg_usb_en(di, false, 0, 0);
	ab8500_chargalg_stop_safety_timer(di);
	ab8500_chargalg_stop_maintenance_timer(di);
	di->timeouttimer_active = 0;
	di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	di->ovv_jiffies = 0;
	di->ovv_recovery_jiffies = jiffies + OVV_DELAY*HZ;
	di->maintenance_chg = false;
	cancel_delayed_work(&di->chargalg_wd_work);
	power_supply_changed(&di->chargalg_psy);
	printk(KERN_INFO "Charging is Stop\n"); 
	
}

 /**
 * ab8500_chargalg_hold_charging() - Pauses charging
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This function is called in the case where maintenance charging has been
 * disabled and instead a battery voltage mode is entered to check when the
 * battery voltage has reached a certain recharge voltage
 */
static void ab8500_chargalg_hold_charging(struct ab8500_chargalg *di)
{
	ab8500_chargalg_ac_en(di, false, 0, 0);
	ab8500_chargalg_usb_en(di, false, 0, 0);
	ab8500_chargalg_stop_safety_timer(di);
	ab8500_chargalg_stop_maintenance_timer(di);
	di->timeouttimer_active = 0;
	di->charge_status = POWER_SUPPLY_STATUS_FULL;
	di->ovv_jiffies = 0;
	di->ovv_recovery_jiffies = jiffies + OVV_DELAY*HZ;
	di->maintenance_chg = false;
	cancel_delayed_work(&di->chargalg_wd_work);
	power_supply_changed(&di->chargalg_psy);
	printk(KERN_INFO "Charging is Pause\n"); 
	
}

/**
 * ab8500_chargalg_start_charging() - Start the charger
 * @di:		pointer to the ab8500_chargalg structure
 * @vset:	requested charger output voltage
 * @iset:	requested charger output current
 *
 * A charger will be enabled depending on the requested charger type that was
 * detected previously.
 */
static void ab8500_chargalg_start_charging(struct ab8500_chargalg *di,
	int vset, int iset)
{
	switch (di->chg_info.charger_type) {
	case AC_CHG:
		printk(KERN_INFO "Charging is started by AC Type charger\n");	
		dev_dbg(di->dev,
			"AC parameters: Vset %d, Ich %d\n", vset, iset);
		ab8500_chargalg_usb_en(di, false, 0, 0);
		ab8500_chargalg_ac_en(di, true, vset, iset);
		di->ovv_jiffies = jiffies + OVV_DELAY*HZ;
		di->ovv_recovery_jiffies = 0;
		break;

	case USB_CHG:
		printk(KERN_INFO "Charging is started by USB Type charger\n");	
		dev_dbg(di->dev,
			"USB parameters: Vset %d, Ich %d\n", vset, iset);
		ab8500_chargalg_ac_en(di, false, 0, 0);
		ab8500_chargalg_usb_en(di, true, vset, iset);
		di->ovv_jiffies = jiffies + OVV_DELAY*HZ;
		di->ovv_recovery_jiffies = 0;
		break;

	default:
		dev_err(di->dev, "Unknown charger to charge from\n");
		return;
	}

	cancel_delayed_work(&di->chargalg_wd_work);
	queue_delayed_work(di->chargalg_wq, &di->chargalg_wd_work, 0);

	if ( !di->timeouttimer_active ){
		di->timeouttimer_active = 1;
		di->charge_timeout_jiffies=jiffies + get_charge_timeout_duration(di);
	}
}

/**
 * ab8500_chargalg_check_temp() - Check battery temperature ranges
 * @di:		pointer to the ab8500_chargalg structure
 *
 * The battery temperature is checked against the predefined limits and the
 * charge state is changed accordingly
 */

static int ab8500_temp_too_high(struct ab8500_chargalg *di)
{
	//int hysteresis  ;
	//hysteresis = (di->events.btemp_underover)?di->bat->temp_hysteresis:0;   

	if (di->charge_state == STATE_TEMP_UNDEROVER)
		return (di->batt_data.temp >  di->bat->temp_high) ;  
	else
		return (di->batt_data.temp >=  di->bat->temp_over) ;  
}

static int ab8500_temp_too_low(struct ab8500_chargalg *di)
{
	//int hysteresis  ;
	//hysteresis =(di->events.btemp_underover)?di->bat->low_temp_hysteresis:0;   

	if (di->charge_state == STATE_TEMP_UNDEROVER)
		return (di->batt_data.temp <  di->bat->temp_low ) ;  
	else
		return (di->batt_data.temp <=  di->bat->temp_under ) ;  
}


static void ab8500_chargalg_check_temp(struct ab8500_chargalg *di)
{	
	di->events.btemp_lowhigh = false;	
	di->events.btemp_underover = (ab8500_temp_too_low(di)||ab8500_temp_too_high(di))?true:false;
}


/**
 * ab8500_chargalg_check_charger_voltage() - Check charger voltage
 * @di:		pointer to the ab8500_chargalg structure
 *
 * Charger voltage is checked against maximum limit
 */
static void ab8500_chargalg_check_charger_voltage(struct ab8500_chargalg *di)
{
	int usb_volt_max;

	/* We are not using VBUS OVV,but using MAINCH OVV
	if (di->chg_info.usb_volt > di->bat->chg_params->usb_volt_max)
		di->chg_info.usb_chg_ok = false;
	else
		di->chg_info.usb_chg_ok = true;
	*/
	/* usb_chg_ok will be always true */
	di->chg_info.usb_chg_ok = true;


	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (di->ovv_jiffies != 0 &&
		    time_after(jiffies, di->ovv_jiffies))
			usb_volt_max = di->bat->chg_params->ac_volt_max;
		else
			usb_volt_max =
				di->bat->chg_params->ac_volt_max_recovery;
	} else {
		if (di->charge_status == POWER_SUPPLY_STATUS_DISCHARGING) {
			usb_volt_max =
				di->bat->chg_params->ac_volt_max_recovery;
		} else if (di->ovv_jiffies == 0 && di->ovv_recovery_jiffies != 0
			   && !time_after(jiffies, di->ovv_recovery_jiffies))
			usb_volt_max = di->bat->chg_params->ac_volt_max;
		else
			usb_volt_max =
				di->bat->chg_params->ac_volt_max_recovery;
	}



	di->chg_info.ac_chg_ok =  !(di->chg_info.usb_volt > usb_volt_max);

	if (!di->chg_info.ac_chg_ok)
		dev_info(di->dev, "USB : %dmV, AC : %dmV, USB_VOLT_MAX : %dmV",
			 di->chg_info.usb_volt,
			 di->chg_info.ac_volt,
			 usb_volt_max);

}

/**
 * ab8500_chargalg_end_of_charge() - Check if end-of-charge criteria is fulfilled
 * @di:		pointer to the ab8500_chargalg structure
 *
 * End-of-charge criteria is fulfilled when the battery voltage is above a
 * certain limit and the battery current is below a certain limit for a
 * predefined number of consecutive seconds. If true, the battery is full
 */

/*
if (cv )
 current < termination current
 else
 voltage < termination voltage

Vbat > Vterm		0	1	0	1	0	1	0	1	

CV mode			0	0	1	1	0	0	1	1
		
Ibat < Iterm		0	0	0	0	1	1	1	1



output			0	0	0	0	0	1	1	1	

*/

static void ab8500_chargalg_end_of_charge(struct ab8500_chargalg *di)
{
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING &&
	    (di->charge_state == STATE_NORMAL ||
	     di->charge_state == STATE_TIMED_OUT_CHARGING) &&
	    !di->maintenance_chg && (di->batt_data.volt >=
		di->bat->bat_type[di->bat->batt_id].termination_vol ||
		di->events.usb_cv_active || di->events.ac_cv_active) &&
	    di->batt_data.avg_curr >= 0) {

		/* In the Samsung charging concept,
		*  if charging current is reached to
		*  the 0.1C of battery full capacity
		*  charging status should be shown in the UI as FULL
		*/
		if (!di->recharging_status && !di->initial_timeout_expire &&
		    di->batt_data.avg_curr <=
		    di->bat->bat_type[di->bat->batt_id].termination_curr_1st) {

			if (!di->full_charging_status_1st) {
				if (++di->eoc_cnt_1st >= EOC_COND_CNT_1ST) {
					dev_dbg(di->dev,
					"1st Full Charging EOC reached!\n");
					di->eoc_cnt_1st = 0;
					di->full_charging_status_1st = true;
					dev_dbg(di->dev,
					 "Full charging status will be shown \
in the UI, BUT NOT Real Full charging\n");
					power_supply_changed(&di->chargalg_psy);

				} else {
					dev_dbg(di->dev,
					"1st Full Charging EOC limit reached \
for the %d time, out of %d before EOC\n",  di->eoc_cnt_1st, EOC_COND_CNT_1ST);
				}
			}
		} else {
			di->eoc_cnt_1st = 0;
		}

		if (di->batt_data.avg_curr <=
		   di->bat->bat_type[di->bat->batt_id].termination_curr_2nd) {

			if (++di->eoc_cnt_2nd >= EOC_COND_CNT_2ND) {
				di->eoc_cnt_2nd = 0;
				di->charge_status = POWER_SUPPLY_STATUS_FULL;
				di->maintenance_chg = true;
				di->full_charging_status_1st = false;
				dev_dbg(di->dev, "real EOC reached!\n");
				power_supply_changed(&di->chargalg_psy);
				dev_dbg(di->dev, "Charging is end\n");
			} else {
				dev_dbg(di->dev,
				" real EOC limit reached for the %d"
				" time, out of %d before EOC\n",
						di->eoc_cnt_2nd,
						EOC_COND_CNT_2ND);
			}
		} else {
			di->eoc_cnt_2nd = 0;
		}
		
	} else {
		di->eoc_cnt_1st = 0;
		di->eoc_cnt_2nd = 0;
	}
}

static void init_maxim_chg_curr(struct ab8500_chargalg *di)
{
	di->ccm.original_iset =
		di->bat->bat_type[di->bat->batt_id].normal_cur_lvl;
	di->ccm.current_iset =
		di->bat->bat_type[di->bat->batt_id].normal_cur_lvl;
	di->ccm.test_delta_i = di->bat->maxi->charger_curr_step;
	di->ccm.max_current = di->bat->maxi->chg_curr;
	di->ccm.condition_cnt = di->bat->maxi->wait_cycles;
	di->ccm.level = 0;
}

/**
 * ab8500_chargalg_chg_curr_maxim - increases the charger current to
 *			compensate for the system load
 * @di		pointer to the ab8500_chargalg structure
 *
 * This maximization function is used to raise the charger current to get the
 * battery current as close to the optimal value as possible. The battery
 * current during charging is affected by the system load
 */
static enum maxim_ret ab8500_chargalg_chg_curr_maxim(struct ab8500_chargalg *di)
{
	int delta_i;

	if (!di->bat->maxi->ena_maxi)
		return MAXIM_RET_NOACTION;

	delta_i = di->ccm.original_iset - di->batt_data.inst_curr;

	if (di->events.vbus_collapsed) {
		dev_dbg(di->dev, "Charger voltage has collapsed %d\n",
				di->ccm.wait_cnt);
		if (di->ccm.wait_cnt == 0) {
			dev_dbg(di->dev, "lowering current\n");
			di->ccm.wait_cnt++;
			di->ccm.condition_cnt = di->bat->maxi->wait_cycles;
			di->ccm.max_current =
				di->ccm.current_iset - di->ccm.test_delta_i;
			di->ccm.current_iset = di->ccm.max_current;
			di->ccm.level--;
			return MAXIM_RET_CHANGE;
		} else {
			dev_dbg(di->dev, "waiting\n");
			/* Let's go in here twice before lowering curr again */
			di->ccm.wait_cnt = (di->ccm.wait_cnt + 1) % 3;
			return MAXIM_RET_NOACTION;
		}
	}

	di->ccm.wait_cnt = 0;

	if ((di->batt_data.inst_curr > di->ccm.original_iset)) {
		dev_dbg(di->dev, " Maximization Ibat (%dmA) too high"
			" (limit %dmA) (current iset: %dmA)!\n",
			di->batt_data.inst_curr, di->ccm.original_iset,
			di->ccm.current_iset);

		if (di->ccm.current_iset == di->ccm.original_iset)
			return MAXIM_RET_NOACTION;

		di->ccm.condition_cnt =	di->bat->maxi->wait_cycles;
		di->ccm.current_iset = di->ccm.original_iset;
		di->ccm.level = 0;

		return MAXIM_RET_IBAT_TOO_HIGH;
	}

	if (delta_i > di->ccm.test_delta_i &&
		(di->ccm.current_iset + di->ccm.test_delta_i) <
		di->ccm.max_current) {
		if (di->ccm.condition_cnt-- == 0) {
			/* Increse the iset with cco.test_delta_i */
			di->ccm.condition_cnt =	di->bat->maxi->wait_cycles;
			di->ccm.current_iset += di->ccm.test_delta_i;
			di->ccm.level++;
			dev_dbg(di->dev, " Maximization needed, increase"
				" with %d mA to %dmA (Optimal ibat: %d)"
				" Level %d\n",
				di->ccm.test_delta_i,
				di->ccm.current_iset,
				di->ccm.original_iset,
				di->ccm.level);
			return MAXIM_RET_CHANGE;
		} else {
			return MAXIM_RET_NOACTION;
		}
	}  else {
		di->ccm.condition_cnt =	di->bat->maxi->wait_cycles;
		return MAXIM_RET_NOACTION;
	}
}

static void handle_maxim_chg_curr(struct ab8500_chargalg *di)
{
	enum maxim_ret ret;
	int result;

	ret = ab8500_chargalg_chg_curr_maxim(di);
	switch (ret) {
	case MAXIM_RET_CHANGE:
		result = ab8500_chargalg_update_chg_curr(di,
			di->ccm.current_iset);
		if (result)
			dev_err(di->dev, "failed to set chg curr\n");
		break;
	case MAXIM_RET_IBAT_TOO_HIGH:
		result = ab8500_chargalg_update_chg_curr(di,
			di->bat->bat_type[di->bat->batt_id].normal_cur_lvl);
		if (result)
			dev_err(di->dev, "failed to set chg curr\n");
		break;

	case MAXIM_RET_NOACTION:
	default:
		/* Do nothing..*/
		break;
	}
}

static int ab8500_chargalg_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab8500_chargalg *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_ab8500_chargalg_device_info(psy);

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

		/* Initialize chargers if not already done */
		if (!di->ac_chg &&
			ext->type == POWER_SUPPLY_TYPE_MAINS)
			di->ac_chg = psy_to_ux500_charger(ext);
		else if (!di->usb_chg &&
			ext->type == POWER_SUPPLY_TYPE_USB)
			di->usb_chg = psy_to_ux500_charger(ext);

		if (ext->get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_LPM_MODE:
			/* LPM_MODE */
			if (ret.intval)
				di->lpm_chg_mode = true;
			else
				di->lpm_chg_mode = false;

			break;

		case POWER_SUPPLY_PROP_SIOP:
			/* SIOP Activation/Deactivation */
			if (ret.intval)
				di->siop_activated = true;
			else
				di->siop_activated = false;

			break;

		case POWER_SUPPLY_PROP_STATUS:
			if (ext->type==POWER_SUPPLY_TYPE_USB)
				di->susp_status.usb_suspended = (ret.intval==POWER_SUPPLY_STATUS_SUSPENDED);
			break ;

		case POWER_SUPPLY_PROP_PRESENT:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				/* Battery present */
				if (ret.intval)
					di->events.batt_rem = false;
				/* Battery removed */
				else
					di->events.batt_rem = true;
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				/* AC disconnected */
				if (!ret.intval &&
					(di->chg_info.conn_chg & AC_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg &= ~AC_CHG;
				}
				/* AC connected */
				else if (ret.intval &&
					!(di->chg_info.conn_chg & AC_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg |= AC_CHG;
				}
				break;
			case POWER_SUPPLY_TYPE_USB:
				/* USB disconnected */
				if (!ret.intval &&
					(di->chg_info.conn_chg & USB_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg &= ~USB_CHG;
				}
				/* USB connected */
				else if (ret.intval &&
					!(di->chg_info.conn_chg & USB_CHG)) {
					di->chg_info.prev_conn_chg =
						di->chg_info.conn_chg;
					di->chg_info.conn_chg |= USB_CHG;
				}
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_ONLINE:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				/* AC offline */
				if (!ret.intval &&
					(di->chg_info.online_chg & AC_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg &= ~AC_CHG;
				}
				/* AC online */
				else if (ret.intval &&
					!(di->chg_info.online_chg & AC_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg |= AC_CHG;
					queue_delayed_work(di->chargalg_wq,
						&di->chargalg_wd_work, 0);
				}
				break;
			case POWER_SUPPLY_TYPE_USB:
				/* USB offline */
				if (!ret.intval &&
					(di->chg_info.online_chg & USB_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg &= ~USB_CHG;
				}
				/* USB online */
				else if (ret.intval &&
					!(di->chg_info.online_chg & USB_CHG)) {
					di->chg_info.prev_online_chg =
						di->chg_info.online_chg;
					di->chg_info.online_chg |= USB_CHG;
					queue_delayed_work(di->chargalg_wq,
						&di->chargalg_wd_work, 0);
				}
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_HEALTH:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				switch (ret.intval) {
					case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
						di->events.batt_ovv = true;
					break ;
					case POWER_SUPPLY_HEALTH_GOOD:
						di->events.batt_ovv = false;
					break; 
				} ;

			case POWER_SUPPLY_TYPE_MAINS:
				switch (ret.intval) {
				case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
					di->events.mainextchnotok = true;
					di->events.main_thermal_prot = false;
					di->events.main_ovv = false;
					di->events.ac_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_DEAD:
					di->events.ac_wd_expired = true;
					di->events.mainextchnotok = false;
					di->events.main_ovv = false;
					di->events.main_thermal_prot = false;
					break;
				case POWER_SUPPLY_HEALTH_COLD:
				case POWER_SUPPLY_HEALTH_OVERHEAT:
					di->events.main_thermal_prot = true;
					di->events.mainextchnotok = false;
					di->events.main_ovv = false;
					di->events.ac_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
					di->events.main_ovv = true;
					di->events.mainextchnotok = false;
					di->events.main_thermal_prot = false;
					di->events.ac_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_GOOD:
					di->events.main_thermal_prot = false;
					di->events.mainextchnotok = false;
					di->events.main_ovv = false;
					di->events.ac_wd_expired = false;
					break;
				default:
					break;
				}
				break;

			case POWER_SUPPLY_TYPE_USB:
				switch (ret.intval) {
				case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
					di->events.usbchargernotok = true;
					di->events.usb_thermal_prot = false;
					di->events.vbus_ovv = false;
					di->events.usb_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_DEAD:
					di->events.usb_wd_expired = true;
					di->events.usbchargernotok = false;
					di->events.usb_thermal_prot = false;
					di->events.vbus_ovv = false;
					break;
				case POWER_SUPPLY_HEALTH_COLD:
				case POWER_SUPPLY_HEALTH_OVERHEAT:
					di->events.usb_thermal_prot = true;
					di->events.usbchargernotok = false;
					di->events.vbus_ovv = false;
					di->events.usb_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
					di->events.vbus_ovv = true;
					di->events.usbchargernotok = false;
					di->events.usb_thermal_prot = false;
					di->events.usb_wd_expired = false;
					break;
				case POWER_SUPPLY_HEALTH_GOOD:
					di->events.usbchargernotok = false;
					di->events.usb_thermal_prot = false;
					di->events.vbus_ovv = false;
					di->events.usb_wd_expired = false;
					break;
				default:
					break;
				}
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.volt = ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_MAINS:
				di->chg_info.ac_volt = ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_USB:
				di->chg_info.usb_volt = ret.intval / 1000;
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_MAINS:
				/* AVG is used to indicate when we are
				 * in CV mode */
				if (ret.intval)
					di->events.ac_cv_active = true;
				else
					di->events.ac_cv_active = false;

				break;
			case POWER_SUPPLY_TYPE_USB:
				/* AVG is used to indicate when we are
				 * in CV mode */
				if (ret.intval)
					di->events.usb_cv_active = true;
				else
					di->events.usb_cv_active = false;

				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_TECHNOLOGY:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				if (ret.intval)
					di->events.batt_unknown = false;
				else
					di->events.batt_unknown = true;

				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_TEMP:
			di->batt_data.temp = ret.intval / 10;
			break;

		case POWER_SUPPLY_PROP_CURRENT_NOW:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_MAINS:
					di->chg_info.ac_curr =
						ret.intval / 1000;
					break;
			case POWER_SUPPLY_TYPE_USB:
					di->chg_info.usb_curr =
						ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.inst_curr = ret.intval / 1000;
				break;
			default:
				break;
			}
			break;

		case POWER_SUPPLY_PROP_CURRENT_AVG:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->batt_data.avg_curr = ret.intval / 1000;
				break;
			case POWER_SUPPLY_TYPE_USB:
				if (ret.intval)
					di->events.vbus_collapsed = true;
				else
					di->events.vbus_collapsed = false;
				break;
			default:
				break;
			}
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			di->batt_data.percent = ret.intval;
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * ab8500_chargalg_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is the entry point of the pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in any external power
 * supply that this driver needs to be notified of.
 */
static void ab8500_chargalg_external_power_changed(struct power_supply *psy)
{
	struct ab8500_chargalg *di = to_ab8500_chargalg_device_info(psy);

	/*
	 * Trigger execution of the algorithm instantly and read
	 * all power_supply properties there instead
	 */
	queue_work(di->chargalg_wq, &di->chargalg_work);
}

/**
 * ab8500_chargalg_algorithm() - Main function for the algorithm
 * @di:		pointer to the ab8500_chargalg structure
 *
 * This is the main control function for the charging algorithm.
 * It is called periodically or when something happens that will
 * trigger a state change
 */
static void ab8500_chargalg_algorithm(struct ab8500_chargalg *di)
{
	int charger_status;

	/* Collect data from all power_supply class devices */
	class_for_each_device(power_supply_class, NULL,
		&di->chargalg_psy, ab8500_chargalg_get_ext_psy_data);

	ab8500_chargalg_end_of_charge(di);
	ab8500_chargalg_check_temp(di);
	ab8500_chargalg_check_charger_voltage(di);
	charger_status = ab8500_chargalg_check_charger_connection(di);

	/*
	 * First check if we have a charger connected.
	 * Also we don't allow charging of unknown batteries if configured
	 * this way
	 */
	if (!charger_status ||
		(di->events.batt_unknown && !di->bat->chg_unknown_bat)) {
		if (di->charge_state != STATE_HANDHELD) {
			di->events.safety_timer_expired = false;
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di, STATE_HANDHELD_INIT);
		}
	}

	/* If suspended, we should not continue checking the flags */
	else if (di->charge_state == STATE_SUSPENDED_INIT ||
		di->charge_state == STATE_SUSPENDED) {
		/* We don't do anything here, just don,t continue */
	}

	/* Safety timer expiration */
	else if (di->events.safety_timer_expired) {
		if (di->charge_state != STATE_SAFETY_TIMER_EXPIRED) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di,
				STATE_SAFETY_TIMER_EXPIRED_INIT);
		}
	}
	/*
	 * Check if any interrupts has occured
	 * that will prevent us from charging
	 */

	/* Battery removed */
	else if (di->events.batt_rem) {
		if (di->charge_state != STATE_BATT_REMOVED) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di, STATE_BATT_REMOVED_INIT);
		}
	}
	/* Main or USB charger not ok. */
	else if (di->events.mainextchnotok || di->events.usbchargernotok) {
		/*
		 * If vbus_collapsed is set, we have to lower the charger
		 * current, which is done in the normal state below
		 */
		if (di->charge_state != STATE_CHG_NOT_OK &&
		    !di->events.vbus_collapsed) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di, STATE_CHG_NOT_OK_INIT);
		}
	}
	/* VBUS, Main or VBAT OVV. */
	else if (di->events.vbus_ovv ||
			di->events.main_ovv ||
			di->events.batt_ovv ||
			!di->chg_info.usb_chg_ok ||
			!di->chg_info.ac_chg_ok) {
		if (di->charge_state != STATE_OVV_PROTECT) {
			pr_info("VBUS OVV : %d\n"
				"MAIN OVV : %d\n"
				"BATT OVV : %d\n"
				"USB_CHG_OK : %d\n"
				"AC_CHG_OK : %d\n",
				di->events.vbus_ovv,
				di->events.main_ovv,
				di->events.batt_ovv,
				di->chg_info.usb_chg_ok,
				di->chg_info.ac_chg_ok);
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di, STATE_OVV_PROTECT_INIT);
		}
	}
	/* USB Thermal, stop charging */
	else if (di->events.main_thermal_prot ||
		di->events.usb_thermal_prot) {
		if (di->charge_state != STATE_HW_TEMP_PROTECT) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di,
				STATE_HW_TEMP_PROTECT_INIT);
		}
	}
	/* Battery temp over/under */
	else if (di->events.btemp_underover) {
		if (di->charge_state != STATE_TEMP_UNDEROVER) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di,
				STATE_TEMP_UNDEROVER_INIT);
		}
	}
	/* Watchdog expired */
	else if (di->events.ac_wd_expired ||
		di->events.usb_wd_expired) {
		if (di->charge_state != STATE_WD_EXPIRED) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di, STATE_WD_EXPIRED_INIT);
		}
	}

	/* Battery temp high/low */
	else if (di->events.btemp_lowhigh) {
		if (di->charge_state != STATE_TEMP_LOWHIGH) {
			di->charge_prev_state = di->charge_state;
			ab8500_chargalg_state_to(di, STATE_TEMP_LOWHIGH_INIT);
		}
	}

	dev_dbg(di->dev,
		"[CHARGALG] Vb %d Ib_avg %d Ib_inst %d Tb %d Cap %d Maint %d "
		"State %s Active_chg %d Chg_status %d AC %d USB %d "
		"AC_online %d USB_online %d AC_CV %d USB_CV %d AC_I %d "
		"USB_I %d AC_Vset %d AC_Iset %d USB_Vset %d USB_Iset %d\n",
		di->batt_data.volt,
		di->batt_data.avg_curr,
		di->batt_data.inst_curr,
		di->batt_data.temp,
		di->batt_data.percent,
		di->maintenance_chg,
		states[di->charge_state],
		di->chg_info.charger_type,
		di->charge_status,
		di->chg_info.conn_chg & AC_CHG,
		di->chg_info.conn_chg & USB_CHG,
		di->chg_info.online_chg & AC_CHG,
		di->chg_info.online_chg & USB_CHG,
		di->events.ac_cv_active,
		di->events.usb_cv_active,
		di->chg_info.ac_curr,
		di->chg_info.usb_curr,
		di->chg_info.ac_vset,
		di->chg_info.ac_iset,
		di->chg_info.usb_vset,
		di->chg_info.usb_iset);

	switch (di->charge_state) {
	case STATE_HANDHELD_INIT:
		di->initial_timeout_expire=0 ;
		di->initial_timeout_expire_under_threshold = 0;
		di->full_charging_status_1st = false;
		di->ovv_jiffies = 0;
		di->ovv_recovery_jiffies = 0;
		if (di->ac_chg &&  di->usb_chg) {	//to address race on startup wait for chargers to register before moving to next state
			ab8500_chargalg_stop_charging(di);
			di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
			ab8500_chargalg_state_to(di, STATE_HANDHELD);
		}
		/* Intentional fallthrough */

	case STATE_HANDHELD:
		break;

	case STATE_SUSPENDED_INIT:
		if (di->susp_status.ac_suspended)
			ab8500_chargalg_ac_en(di, false, 0, 0);
		if (di->susp_status.usb_suspended)
			ab8500_chargalg_usb_en(di, false, 0, 0);
		ab8500_chargalg_stop_safety_timer(di);
		ab8500_chargalg_stop_maintenance_timer(di);
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		di->maintenance_chg = false;
		ab8500_chargalg_state_to(di, STATE_SUSPENDED);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough */

	case STATE_SUSPENDED:
		/* CHARGING is suspended */
		break;

	case STATE_BATT_REMOVED_INIT:
		di->full_charging_status_1st = false;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_BATT_REMOVED);
		/* Intentional fallthrough */

	case STATE_BATT_REMOVED:
		if (!di->events.batt_rem)
			ab8500_chargalg_state_to(di, get_charging_start_state(di));
		break;

	case STATE_HW_TEMP_PROTECT_INIT:
		di->error_jiffies = jiffies + di->bat->bat_type[di->bat->batt_id].error_charge_stoptime;
		di->full_charging_status_1st = false;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_HW_TEMP_PROTECT);
		/* Intentional fallthrough */

	case STATE_HW_TEMP_PROTECT:
		if (!di->events.main_thermal_prot &&
				!di->events.usb_thermal_prot &&
				time_after( jiffies, di->error_jiffies))
			ab8500_chargalg_state_to(di, get_charging_start_state(di));
		break;

	case STATE_OVV_PROTECT_INIT:
		di->full_charging_status_1st = false;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_OVV_PROTECT);
		cancel_delayed_work(&di->chargalg_periodic_work);
		queue_delayed_work(di->chargalg_wq,
				   &di->chargalg_periodic_work,
				   5*HZ);
		/* Intentional fallthrough */

	case STATE_OVV_PROTECT:
		if (!di->events.vbus_ovv &&
				!di->events.main_ovv &&
				!di->events.batt_ovv &&
				di->chg_info.usb_chg_ok &&
				di->chg_info.ac_chg_ok)
			ab8500_chargalg_state_to(di, get_charging_start_state(di));
		break;

	case STATE_CHG_NOT_OK_INIT:
		di->error_jiffies = jiffies + di->bat->bat_type[di->bat->batt_id].error_charge_stoptime;
		di->full_charging_status_1st = false;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_CHG_NOT_OK);
		/* Intentional fallthrough */

	case STATE_CHG_NOT_OK:
		if (!di->events.mainextchnotok &&
				!di->events.usbchargernotok &&
				time_after( jiffies, di->error_jiffies) )
			ab8500_chargalg_state_to(di, get_charging_start_state(di));
		break;

	case STATE_SAFETY_TIMER_EXPIRED_INIT:
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_SAFETY_TIMER_EXPIRED);
		/* Intentional fallthrough */

	case STATE_SAFETY_TIMER_EXPIRED:
		/* We exit this state when charger is removed */
		break;

	case STATE_NORMAL_INIT:
		ab8500_chargalg_start_charging(di,
			di->bat->bat_type[di->bat->batt_id].normal_vol_lvl,
			di->bat->bat_type[di->bat->batt_id].normal_cur_lvl);
		ab8500_chargalg_state_to(di, STATE_NORMAL);
		if (di->bat->use_safety_timer)
			ab8500_chargalg_start_safety_timer(di);
		ab8500_chargalg_stop_maintenance_timer(di);
		init_maxim_chg_curr(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		di->eoc_cnt_1st = 0;
		di->eoc_cnt_2nd = 0;		
		di->maintenance_chg = false;
		power_supply_changed(&di->chargalg_psy);
		cancel_delayed_work(&di->chargalg_periodic_work);
		queue_delayed_work(di->chargalg_wq,
				   &di->chargalg_periodic_work,
				   5*HZ);
		break;

	case STATE_NORMAL:
		handle_maxim_chg_curr(di);

		/* Originally,
		 * if usb cable is attached, USB driver set the charger
		 * input current as 500mA.
		 * But USB driver sometimes doesn't do it.
		 * So, we should set the charger input currnat for USB
		 */
		if (di->usb_chg && di->usb_chg->ops.update_input_curr &&
		    di->chg_info.online_chg & USB_CHG)
			di->usb_chg->ops.update_input_curr(di->usb_chg,
					   di->bat->usb_chg_current_input);

		/* Samsung Intelligent Overheat Protection */
		if (di->ac_chg && di->ac_chg->ops.siop_activation &&
			di->chg_info.online_chg & AC_CHG)
			di->ac_chg->ops.siop_activation(di->ac_chg,
							di->siop_activated);


		if ((di->bat->use_safety_timer == 0) &&
		    (time_after(jiffies, di->charge_timeout_jiffies)) &&
		    di->timeouttimer_active) {
			if (di->batt_data.volt < 4000)
				di->initial_timeout_expire_under_threshold = 1;
			else
				di->initial_timeout_expire_under_threshold = 0;

			di->recharging_status = false;
			ab8500_chargalg_state_to(di,
						 STATE_CHARGE_TIMEOUT_INIT);
		}

		if (di->charge_status == POWER_SUPPLY_STATUS_FULL &&
			di->maintenance_chg) {

			if (di->bat->no_maintenance)
				ab8500_chargalg_state_to(di,
						 STATE_WAIT_FOR_RECHARGE_INIT);
			else
				ab8500_chargalg_state_to(di,
						 STATE_MAINTENANCE_A_INIT);
		}
		break;

	 /* This state will be used when the maintenance state is disabled */
	case STATE_WAIT_FOR_RECHARGE_INIT:
		ab8500_chargalg_hold_charging(di);
		ab8500_chargalg_state_to(di, STATE_WAIT_FOR_RECHARGE);
		di->rch_cnt = RCH_COND_CNT;
		/* Intentional fallthrough */

	case STATE_WAIT_FOR_RECHARGE:
		if (di->batt_data.volt <=
		    di->bat->bat_type[di->bat->batt_id].recharge_vol) {
			if (di->rch_cnt-- == 0) {
				di->recharging_status = true;
				ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
			}
		 } else
			di->rch_cnt = RCH_COND_CNT;

		break;

	case STATE_MAINTENANCE_A_INIT:
		ab8500_chargalg_stop_safety_timer(di);
		ab8500_chargalg_start_maintenance_timer(di,
				di->bat->bat_type[
				di->bat->batt_id].maint_a_chg_timer_h);
		ab8500_chargalg_start_charging(di,
			 di->bat->bat_type[
				 di->bat->batt_id].maint_a_vol_lvl,
			 di->bat->bat_type[
				 di->bat->batt_id].maint_a_cur_lvl);
		ab8500_chargalg_state_to(di, STATE_MAINTENANCE_A);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough*/

	case STATE_MAINTENANCE_A:
		if (di->events.maintenance_timer_expired) {
			ab8500_chargalg_stop_maintenance_timer(di);
			ab8500_chargalg_state_to(di, STATE_MAINTENANCE_B_INIT);
		}
		break;

	case STATE_MAINTENANCE_B_INIT:
		ab8500_chargalg_start_maintenance_timer(di,
		 di->bat->bat_type[di->bat->batt_id].maint_b_chg_timer_h);
		ab8500_chargalg_start_charging(di,
			di->bat->bat_type[di->bat->batt_id].maint_b_vol_lvl,
			di->bat->bat_type[di->bat->batt_id].maint_b_cur_lvl);
		ab8500_chargalg_state_to(di, STATE_MAINTENANCE_B);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough*/

	case STATE_MAINTENANCE_B:
		if (di->events.maintenance_timer_expired) {
			ab8500_chargalg_stop_maintenance_timer(di);
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);
		}
		break;

	case STATE_TEMP_LOWHIGH_INIT:
		ab8500_chargalg_start_charging(di,
			di->bat->bat_type[di->bat->batt_id].low_high_vol_lvl,
			di->bat->bat_type[di->bat->batt_id].low_high_cur_lvl);
		ab8500_chargalg_stop_maintenance_timer(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		ab8500_chargalg_state_to(di, STATE_TEMP_LOWHIGH);
		power_supply_changed(&di->chargalg_psy);
		/* Intentional fallthrough */

	case STATE_TEMP_LOWHIGH:
		if (!di->events.btemp_lowhigh)
			ab8500_chargalg_state_to(di, get_charging_start_state(di));
		break;

	case STATE_WD_EXPIRED_INIT:
		di->error_jiffies = jiffies + di->bat->bat_type[di->bat->batt_id].error_charge_stoptime;
		di->full_charging_status_1st = false;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_WD_EXPIRED);
		/* Intentional fallthrough */

	case STATE_WD_EXPIRED:
		if (!di->events.ac_wd_expired &&
		     !di->events.usb_wd_expired &&
		     time_after(jiffies, di->error_jiffies))
			ab8500_chargalg_state_to(di, get_charging_start_state(di));
		break;

	case STATE_TEMP_UNDEROVER_INIT:
		di->error_jiffies = jiffies + di->bat->bat_type[di->bat->batt_id].error_charge_stoptime;
		di->full_charging_status_1st = false;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_TEMP_UNDEROVER);
		/* Intentional fallthrough */

	case STATE_TEMP_UNDEROVER:
		if (!di->events.btemp_underover &&
		    time_after(jiffies, di->error_jiffies))
			ab8500_chargalg_state_to(di,
						 get_charging_start_state(di));
		break;

	case STATE_CHARGE_TIMEOUT_INIT:
		di->initial_timeout_expire = 1 ;
		di->timeout_chargeoff_jiffies = jiffies +
		di->bat->bat_type[di->bat->batt_id].timeout_chargeoff_time;
		ab8500_chargalg_stop_charging(di);
		ab8500_chargalg_state_to(di, STATE_CHARGE_TIMEOUT);
		dev_info(di->dev, "CHARGE TIMED OUT\n");
		break ;

	case STATE_CHARGE_TIMEOUT:
		if (time_after(jiffies, di->timeout_chargeoff_jiffies))
			ab8500_chargalg_state_to(di, STATE_NORMAL_INIT);

		break ;

	case 	STATE_TIMED_OUT_CHARGING_INIT:
		ab8500_chargalg_start_charging(di,
		       di->bat->bat_type[di->bat->batt_id].normal_vol_lvl,
		       di->bat->bat_type[di->bat->batt_id].normal_cur_lvl);
		ab8500_chargalg_state_to(di, STATE_TIMED_OUT_CHARGING);
		if (di->bat->use_safety_timer)
			ab8500_chargalg_start_safety_timer(di);
		ab8500_chargalg_stop_maintenance_timer(di);
		init_maxim_chg_curr(di);
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		di->eoc_cnt_1st = 0;
		di->eoc_cnt_2nd = 0;
		di->maintenance_chg = false;
		power_supply_changed(&di->chargalg_psy);

		break;

	case 	STATE_TIMED_OUT_CHARGING:
		handle_maxim_chg_curr(di);
		if ((di->bat->use_safety_timer == 0) &&
			di->timeouttimer_active && 
			time_after(jiffies, di->charge_timeout_jiffies)) {
			ab8500_chargalg_state_to(di, STATE_CHARGE_TIMEOUT_INIT);
		}
		break ;

	}

	 /* Start charging directly if the new state is a charge state */
	if (di->charge_state == STATE_NORMAL_INIT ||
	    di->charge_state == STATE_CHARGE_TIMEOUT_INIT ||
	    di->charge_state == STATE_TIMED_OUT_CHARGING_INIT ||
	    di->charge_state == STATE_MAINTENANCE_A_INIT ||
	    di->charge_state == STATE_MAINTENANCE_B_INIT)
		queue_work(di->chargalg_wq, &di->chargalg_work);
}

 /**
  * ab8500_chargalg_periodic_work() - Periodic work for the algorithm
  * @work:	pointer to the work_struct structure
  *
  * Work queue function for the charging algorithm
  */
static void ab8500_chargalg_periodic_work(struct work_struct *work)
{
	struct ab8500_chargalg *di = container_of(work,
		  struct ab8500_chargalg, chargalg_periodic_work.work);

	ab8500_chargalg_algorithm(di);

	/*
	 * If a charger is connected then the battery has to be monitored
	 * frequently, else the work can be delayed.
	 */
	if (di->chg_info.conn_chg)
		queue_delayed_work(di->chargalg_wq,
				   &di->chargalg_periodic_work,
				   di->bat->interval_charging * HZ);
	else
		queue_delayed_work(di->chargalg_wq,
				   &di->chargalg_periodic_work,
				   di->bat->interval_not_charging * HZ);
}

 /**
  * ab8500_chargalg_wd_work() - periodic work to kick the charger watchdog
  * @work:	pointer to the work_struct structure
  *
  * Work queue function for kicking the charger watchdog
  */
static void ab8500_chargalg_wd_work(struct work_struct *work)
{
	int ret;
	struct ab8500_chargalg *di = container_of(work,
		  struct ab8500_chargalg, chargalg_wd_work.work);

	dev_dbg(di->dev, "ab8500_chargalg_wd_work\n");

	ret = ab8500_chargalg_kick_watchdog(di);
	if (ret < 0)
		dev_err(di->dev, "failed to kick watchdog\n");

	dev_dbg(di->dev, "succeeded to kick watchdog\n");

	queue_delayed_work(di->chargalg_wq,
		 &di->chargalg_wd_work, CHG_WD_INTERVAL);
}

 /**
  * ab8500_chargalg_work() - Work to run the charging algorithm instantly
  * @work:	pointer to the work_struct structure
  *
  * Work queue function for calling the charging algorithm
  */
static void ab8500_chargalg_work(struct work_struct *work)
{
	 struct ab8500_chargalg *di = container_of(work,
		 struct ab8500_chargalg, chargalg_work);

	 ab8500_chargalg_algorithm(di);
}

/*
	converts internal state into one that user space can use 

*/
static int convert_state_and_status_to_usr_status(struct ab8500_chargalg *di )
{
	int ret = POWER_SUPPLY_STATUS_DISCHARGING;

	switch(di->charge_state)
	{
	case	STATE_HANDHELD_INIT:
		/* we don't know if a charger is connected or
		* not because the charger drivers
		* may not have intialised yet so report
		* not charging rather than disconnected
		*/
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;

	case	STATE_HANDHELD:
		/* we need to distiguish between no charger present and
		* a currently inactive charger
		* as USB can take some time to setup the charging
		* thus introducing a race.
		*/
		if (di->chg_info.conn_chg &  (AC_CHG|USB_CHG))
			ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			ret = POWER_SUPPLY_STATUS_DISCHARGING ;
		break ;

	case	STATE_NORMAL_INIT:
	case	STATE_NORMAL:
		/* we have timed out this time */
		if (di->initial_timeout_expire || di->recharging_status||
		    di->full_charging_status_1st)
			ret = POWER_SUPPLY_STATUS_FULL;
		else
			ret = POWER_SUPPLY_STATUS_CHARGING;
		break ;

	case	STATE_TIMED_OUT_CHARGING_INIT:
	case	STATE_TIMED_OUT_CHARGING:
		ret = POWER_SUPPLY_STATUS_FULL ;	
		break ;

	case	STATE_CHARGE_TIMEOUT_INIT:
	case	STATE_CHARGE_TIMEOUT:
	case	STATE_WAIT_FOR_RECHARGE_INIT:
	case	STATE_WAIT_FOR_RECHARGE:
	case	STATE_MAINTENANCE_A_INIT:
	case	STATE_MAINTENANCE_A:
	case	STATE_MAINTENANCE_B_INIT:
	case	STATE_MAINTENANCE_B:
		ret = POWER_SUPPLY_STATUS_FULL ;	
		break ;
	case	STATE_OVV_PROTECT_INIT:
	case	STATE_OVV_PROTECT:
		ret = POWER_SUPPLY_STATUS_DISCHARGING;
		break ;

	case	STATE_TEMP_UNDEROVER_INIT:
	case	STATE_TEMP_UNDEROVER:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;

	case	STATE_CHG_NOT_OK_INIT:
	case	STATE_CHG_NOT_OK:
	case	STATE_HW_TEMP_PROTECT_INIT:
	case	STATE_HW_TEMP_PROTECT:
	case	STATE_TEMP_LOWHIGH_INIT:
	case	STATE_TEMP_LOWHIGH:
	case	STATE_SUSPENDED_INIT:
	case	STATE_SUSPENDED:
	case	STATE_SAFETY_TIMER_EXPIRED_INIT:
	case	STATE_SAFETY_TIMER_EXPIRED:
	case	STATE_WD_EXPIRED_INIT:
	case	STATE_WD_EXPIRED:
		ret = POWER_SUPPLY_STATUS_DISCHARGING ;
		break;

	case	STATE_BATT_REMOVED_INIT:
	case	STATE_BATT_REMOVED:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING ;
		break;

	default :
		ret = POWER_SUPPLY_STATUS_DISCHARGING ;
	}

	// In case of unknown battery, update not charging status.
	if(di->chg_info.conn_chg != NO_CHG &&  // Charger is connected
		di->bat->batt_id == BATTERY_UNKNOWN) {
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
	
	return ret ;
} ;


static int convert_state_to_usr_health(struct ab8500_chargalg *di )
{
	int ret = POWER_SUPPLY_HEALTH_GOOD;
	switch(di->charge_state)
	{
	case	STATE_HANDHELD_INIT:
	case	STATE_HANDHELD:
	case	STATE_NORMAL_INIT:
	case	STATE_NORMAL:
	case	STATE_WAIT_FOR_RECHARGE_INIT:
	case	STATE_WAIT_FOR_RECHARGE:
	case	STATE_MAINTENANCE_A_INIT:
	case	STATE_MAINTENANCE_A:
	case	STATE_MAINTENANCE_B_INIT:
	case	STATE_MAINTENANCE_B:
	case	STATE_CHARGE_TIMEOUT_INIT:
	case	STATE_CHARGE_TIMEOUT:
	case	STATE_SAFETY_TIMER_EXPIRED_INIT:
	case	STATE_SAFETY_TIMER_EXPIRED:
	case	STATE_SUSPENDED_INIT:
	case	STATE_SUSPENDED:
	case	STATE_TIMED_OUT_CHARGING_INIT:
	case	STATE_TIMED_OUT_CHARGING:

		ret = POWER_SUPPLY_HEALTH_GOOD ;	
		break ;

	case	STATE_TEMP_UNDEROVER_INIT:
	case	STATE_TEMP_UNDEROVER:
		ret= (ab8500_temp_too_low(di))?POWER_SUPPLY_HEALTH_COLD:POWER_SUPPLY_HEALTH_OVERHEAT ;
		break ;		

	case	STATE_OVV_PROTECT_INIT:
	case	STATE_OVV_PROTECT:
		ret = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break ;

	case	STATE_HW_TEMP_PROTECT_INIT:
	case	STATE_HW_TEMP_PROTECT:
		ret = POWER_SUPPLY_HEALTH_OVERHEAT ;
		break ;

	case	STATE_CHG_NOT_OK_INIT:	
	case	STATE_CHG_NOT_OK:
		ret = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE ;
		break ;

	case	STATE_WD_EXPIRED_INIT:
	case	STATE_WD_EXPIRED:
		ret = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE ;
		break ;

	case	STATE_TEMP_LOWHIGH_INIT:
	case	STATE_TEMP_LOWHIGH:
	case	STATE_BATT_REMOVED_INIT:
	case	STATE_BATT_REMOVED:
		ret = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE ;
		break ;

	default :
		ret = POWER_SUPPLY_HEALTH_GOOD;

	}

	// In case of unknown battery, update unspec failure health.
	if(di->bat->batt_id == BATTERY_UNKNOWN)
		ret = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;

	return ret ;
} ;


/**
 * ab8500_chargalg_get_property() - get the chargalg properties
 * @psy:	pointer to the power_supply structure
 * @psp:	pointer to the power_supply_property structure
 * @val:	pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the
 * chargalg properties by reading the sysfs files.
 * status:     charging/discharging/full/unknown
 * health:     health of the battery
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_chargalg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_chargalg *di;

	di = to_ab8500_chargalg_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = convert_state_and_status_to_usr_status(di) ;
		//val->intval = di->charge_status;
		//pr_info("%s: status (%d)\n", __func__, val->intval);
		break;

	case POWER_SUPPLY_PROP_HEALTH:
/*
		if (di->events.batt_ovv) {
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		} else if (di->events.btemp_underover ) {
//			if (di->batt_data.temp <= di->bat->temp_under )
			if (ab8500_temp_too_low(di))
				val->intval = POWER_SUPPLY_HEALTH_COLD;
			else
				val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		} else {
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		} */
		val->intval = convert_state_to_usr_health(di);
		//pr_info("%s: health (%d)\n", __func__, val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGING_TIMEOUT:/* Charging timeout check */
		val->intval = (di->initial_timeout_expire ? 1 : 0);
		break;

	case POWER_SUPPLY_PROP_UI_FULL:	/* 1st Full Charging */
		val->intval = ((di->charge_status ==
				POWER_SUPPLY_STATUS_CHARGING &&
				di->full_charging_status_1st) ? 1 : 0);
		break;

	case POWER_SUPPLY_PROP_CHARGING_SOURCE:
		switch (di->chg_info.charger_type) {
		case AC_CHG:
			val->intval = POWER_SUPPLY_TYPE_MAINS;
			break;
		case USB_CHG:
			val->intval = POWER_SUPPLY_TYPE_USB;
			break;
		default:
			val->intval = POWER_SUPPLY_TYPE_BATTERY;
			break;
		}
		break;
		
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef SYSFS_CHARGER_CONTROL
/* Exposure to the sysfs interface */

/**
 * ab8500_chargalg_sysfs_charger() - sysfs store operations
 * @kobj:      pointer to the struct kobject
 * @attr:      pointer to the struct attribute
 * @buf:       buffer that holds the parameter passed from userspace
 * @length:    length of the parameter passed
 *
 * Returns length of the buffer(input taken from user space) on success
 * else error code on failure
 * The operation to be performed on passing the parameters from the user space.
 */
static ssize_t ab8500_chargalg_sysfs_charger(struct kobject *kobj,
	struct attribute *attr, const char *buf, size_t length)
{
	struct ab8500_chargalg *di = container_of(kobj,
		struct ab8500_chargalg, chargalg_kobject);
	long int param;
	int ac_usb;
	int ret;
	char entry = *attr->name;

	switch (entry) {
	case 'c':
		ret = strict_strtol(buf, 10, &param);
		if (ret < 0)
			return ret;

		ac_usb = param;
		switch (ac_usb) {
		case 0:
			/* Disable charging */
			di->susp_status.ac_suspended = true;
			di->susp_status.usb_suspended = true;
			di->susp_status.suspended_change = true;
			/* Trigger a state change */
			queue_work(di->chargalg_wq,
				&di->chargalg_work);
			break;
		case 1:
			/* Enable AC Charging */
			di->susp_status.ac_suspended = false;
			di->susp_status.suspended_change = true;
			/* Trigger a state change */
			queue_work(di->chargalg_wq,
				&di->chargalg_work);
			break;
		case 2:
			/* Enable USB charging */
			di->susp_status.usb_suspended = false;
			di->susp_status.suspended_change = true;
			/* Trigger a state change */
			queue_work(di->chargalg_wq,
				&di->chargalg_work);
			break;
		default:
			dev_info(di->dev, "Wrong input\n"
				"Enter 0. Disable AC/USB Charging\n"
				"1. Enable AC charging\n"
				"2. Enable USB Charging\n");
		};
		break;
	};
	return strlen(buf);
}

static struct attribute ab8500_chargalg_en_charger = \
{
	.name = "chargalg",
	.mode = (S_IWUSR | S_IWGRP),
};

static struct attribute *ab8500_chargalg_chg[] = {
	&ab8500_chargalg_en_charger,
	NULL
};

const struct sysfs_ops ab8500_chargalg_sysfs_ops = {
	.store = ab8500_chargalg_sysfs_charger,
};

static struct kobj_type ab8500_chargalg_ktype = {
	.sysfs_ops = &ab8500_chargalg_sysfs_ops,
	.default_attrs = ab8500_chargalg_chg,
};

/**
 * ab8500_chargalg_sysfs_exit() - de-init of sysfs entry
 * @di:                pointer to the struct ab8500_chargalg
 *
 * This function removes the entry in sysfs.
 */
static void ab8500_chargalg_sysfs_exit(struct ab8500_chargalg *di)
{
	kobject_del(&di->chargalg_kobject);
}

/**
 * ab8500_chargalg_sysfs_init() - init of sysfs entry
 * @di:                pointer to the struct ab8500_chargalg
 *
 * This function adds an entry in sysfs.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_chargalg_sysfs_init(struct ab8500_chargalg *di)
{
	int ret = 0;

	ret = kobject_init_and_add(&di->chargalg_kobject,
		&ab8500_chargalg_ktype,
		NULL, "ab8500_chargalg");
	if (ret < 0)
		dev_err(di->dev, "failed to create sysfs entry\n");

	return ret;
}
/* Exposure to the sysfs interface <<END>> */
#endif //SYSFS_CHARGER_CONTROL

#if defined(CONFIG_PM)
static int ab8500_chargalg_resume(struct platform_device *pdev)
{
	struct ab8500_chargalg *di = platform_get_drvdata(pdev);

	/* Kick charger watchdog if charging (any charger online) */
	if (di->chg_info.online_chg)
		queue_delayed_work(di->chargalg_wq, &di->chargalg_wd_work, 0);

	/*
	 * Run the charging algorithm directly to be sure we don't
	 * do it too seldom
	 */
	queue_delayed_work(di->chargalg_wq, &di->chargalg_periodic_work, 0);

	return 0;
}

static int ab8500_chargalg_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_chargalg *di = platform_get_drvdata(pdev);

	if (di->chg_info.online_chg)
		cancel_delayed_work_sync(&di->chargalg_wd_work);

	cancel_delayed_work_sync(&di->chargalg_periodic_work);

	return 0;
}
#else
#define ab8500_chargalg_suspend      NULL
#define ab8500_chargalg_resume       NULL
#endif

static int __devexit ab8500_chargalg_remove(struct platform_device *pdev)
{
	struct ab8500_chargalg *di = platform_get_drvdata(pdev);

#ifdef SYSFS_CHARGER_CONTROL
	/* sysfs interface to enable/disbale charging from user space */
	ab8500_chargalg_sysfs_exit(di);
#endif

	/* Delete the work queue */
	destroy_workqueue(di->chargalg_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->chargalg_psy);
	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return 0;
}

static int __devinit ab8500_chargalg_probe(struct platform_device *pdev)
{
	struct ab8500_platform_data *plat;
	int ret = 0;

	struct ab8500_chargalg *di =
		kzalloc(sizeof(struct ab8500_chargalg), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	plat = dev_get_platdata(di->parent->dev);

	/* get chargalg specific platform data */
	if (!plat->chargalg) {
		dev_err(di->dev, "no chargalg platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->pdata = plat->chargalg;

	/* get battery specific platform data */
	if (!plat->battery) {
		dev_err(di->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}
	di->bat = plat->battery;

	/* chargalg supply */
	di->chargalg_psy.name = "ab8500_chargalg";
	di->chargalg_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->chargalg_psy.properties = ab8500_chargalg_props;
	di->chargalg_psy.num_properties = ARRAY_SIZE(ab8500_chargalg_props);
	di->chargalg_psy.get_property = ab8500_chargalg_get_property;
	di->chargalg_psy.supplied_to = di->pdata->supplied_to;
	di->chargalg_psy.num_supplicants = di->pdata->num_supplicants;
	di->chargalg_psy.external_power_changed =
		ab8500_chargalg_external_power_changed;

	/* Initilialize safety timer */
	init_timer(&di->safety_timer);
	di->safety_timer.function = ab8500_chargalg_safety_timer_expired;
	di->safety_timer.data = (unsigned long) di;

	/* Initilialize maintenance timer */
	init_timer(&di->maintenance_timer);
	di->maintenance_timer.function =
		ab8500_chargalg_maintenance_timer_expired;
	di->maintenance_timer.data = (unsigned long) di;

	/* Create a work queue for the chargalg */
	di->chargalg_wq =
		create_singlethread_workqueue("ab8500_chargalg_wq");
	if (di->chargalg_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for chargalg */
	INIT_DELAYED_WORK_DEFERRABLE(&di->chargalg_periodic_work,
		ab8500_chargalg_periodic_work);
	INIT_DELAYED_WORK_DEFERRABLE(&di->chargalg_wd_work,
		ab8500_chargalg_wd_work);

	/* Init work for chargalg */
	INIT_WORK(&di->chargalg_work, ab8500_chargalg_work);

	/* To detect charger at startup */
	di->chg_info.prev_conn_chg = -1;

	di->recharging_status = false;
	di->check_state = false;
	di->check_cnt = 0;
	
	/* Register chargalg power supply class */
	ret = power_supply_register(di->dev, &di->chargalg_psy);
	if (ret) {
		dev_err(di->dev, "failed to register chargalg psy\n");
		goto free_chargalg_wq;
	}

	platform_set_drvdata(pdev, di);

#ifdef SYSFS_CHARGER_CONTROL
	/* sysfs interface to enable/disable charging from user space */
	ret = ab8500_chargalg_sysfs_init(di);
	if (ret) {
		dev_err(di->dev, "failed to create sysfs entry\n");
		goto free_psy;
	}
#endif //SYSFS_CHARGER_CONTROL

	/* Run the charging algorithm */
	queue_delayed_work(di->chargalg_wq, &di->chargalg_periodic_work, 0);
	return ret;

#ifdef SYSFS_CHARGER_CONTROL
free_psy:
	power_supply_unregister(&di->chargalg_psy);
#endif //SYSFS_CHARGER_CONTROL

free_chargalg_wq:
	destroy_workqueue(di->chargalg_wq);
free_device_info:
	kfree(di);

	return ret;
}

static struct platform_driver ab8500_chargalg_driver = {
	.probe = ab8500_chargalg_probe,
	.remove = __devexit_p(ab8500_chargalg_remove),
	.suspend = ab8500_chargalg_suspend,
	.resume = ab8500_chargalg_resume,
	.driver = {
		.name = "ab8500-chargalg",
		.owner = THIS_MODULE,
	},
};

static int __init ab8500_chargalg_init(void)
{
	return platform_driver_register(&ab8500_chargalg_driver);
}

static void __exit ab8500_chargalg_exit(void)
{
	platform_driver_unregister(&ab8500_chargalg_driver);
}

module_init(ab8500_chargalg_init);
module_exit(ab8500_chargalg_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab8500-chargalg");
MODULE_DESCRIPTION("AB8500 battery temperature driver");
