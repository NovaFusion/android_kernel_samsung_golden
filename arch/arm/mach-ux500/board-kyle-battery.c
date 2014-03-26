#include <linux/kernel.h>
#include <linux/platform_device.h>


#if defined(CONFIG_BATTERY_SAMSUNG)
#include "board-sec-bm.h"

#include <linux/battery/sec_battery.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/usb_switcher.h>
#include <linux/input/ab8505_micro_usb_iddet.h>
#include <linux/battery/charger/abb_sec_charger.h>

#define SEC_BATTERY_PMIC_NAME	""

#define SEC_FUELGAUGE_I2C_ID	9
#define SEC_FUELGAUGE_IRQ	IRQ_EINT(19)
#define SEC_CHARGER_I2C_ID	19
#define SEC_CHARGER_IRQ IRQ_EINT(1)

bool power_off_charging;
struct notifier_block cable_nb;
struct notifier_block cable_accessory_nb;
extern int micro_usb_register_notifier(struct notifier_block *nb);
extern int micro_usb_register_usb_notifier(struct notifier_block *nb);
extern void usb_switch_register_notify(struct notifier_block *nb);
extern unsigned int board_id;
extern int use_ab8505_iddet;
bool vbus_state;
EXPORT_SYMBOL(vbus_state);

static int abb_jig_charging(void)
{
	union power_supply_propval value;
	int i, ret = 0;
	struct power_supply *psy;

	int cable_status;
	cable_status = abb_get_cable_status();


	if (cable_status == CABLE_TYPE_UARTOFF &&
	    vbus_state) {
		pr_info("%s, JIG UART OFF + VBUS : detected\n", __func__);
		value.intval = POWER_SUPPLY_TYPE_MAINS;
	} else if (cable_status == CABLE_TYPE_UARTOFF &&
		   !vbus_state) {
		pr_info("%s, JIG UART OFF + VBUS : vbus is disapeared\n",
			__func__);
		value.intval = POWER_SUPPLY_TYPE_BATTERY;
	} else
		return 0;

	for (i = 0; i < 10; i++) {
		psy = power_supply_get_by_name("battery");
		if (psy)
			break;
	}

	if (i == 10) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return 0;
	}

	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
		&value);
	if (ret) {
		pr_err("%s: fail to set power_suppy ONLINE property(%d)\n",
		       __func__, ret);
	}
	return 1;
}

static int abb_dock_charging(void)
{
	union power_supply_propval value;
	int i, ret = 0;
	struct power_supply *psy;

	int cable_status;
	cable_status = abb_get_cable_status();


	if (cable_status == CABLE_TYPE_CARDOCK &&
	    vbus_state) {
		pr_info("%s, CARKIT/DESKDOCK + VBUS : detected\n", __func__);
		value.intval = POWER_SUPPLY_TYPE_MAINS;
	} else if (cable_status == CABLE_TYPE_CARDOCK &&
		   !vbus_state) {
		pr_info("%s, CARKIT/DESKDOCK + VBUS : vbus is disapeared\n",
			__func__);
		value.intval = POWER_SUPPLY_TYPE_BATTERY;
	} else
		return 0;

	for (i = 0; i < 10; i++) {
		psy = power_supply_get_by_name("battery");
		if (psy)
			break;
	}

	if (i == 10) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return 0;
	}

	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
		&value);
	if (ret) {
		pr_err("%s: fail to set power_suppy ONLINE property(%d)\n",
			__func__, ret);
	}
	return 1;
}

static void abb_vbus_is_detected(bool state)
{
	vbus_state = state;
	if ((abb_jig_charging() | abb_dock_charging()) || state)
		return;

	/* VBUS falling but not handled */
	abb_battery_cb();
}

static bool sec_bat_adc_none_init(
		struct platform_device *pdev) {return true; }
static bool sec_bat_adc_none_exit(void) {return true; }
static int sec_bat_adc_none_read(unsigned int channel) {return 0; }

static bool sec_bat_adc_ap_init(
		struct platform_device *pdev) {return true; }
static bool sec_bat_adc_ap_exit(void) {return true; }
static int sec_bat_adc_ap_read(unsigned int channel)
{
	int adc;

	switch (channel) {
	case SEC_BAT_ADC_CHANNEL_TEMP:
	case SEC_BAT_ADC_CHANNEL_TEMP_AMBIENT:
		adc = ab8500_gpadc_read_raw(
			ab8500_gpadc_get(), BTEMP_BALL,
			SAMPLE_16, RISING_EDGE, 0, ADC_SW);
		break;
	case SEC_BAT_ADC_CHANNEL_FULL_CHECK:
		adc = ab8500_gpadc_convert(
			ab8500_gpadc_get(), USB_CHARGER_C);
		break;
	default:
		return -1;
		break;
	}
	return adc;
}
static bool sec_bat_adc_ic_init(
		struct platform_device *pdev) {return true; }
static bool sec_bat_adc_ic_exit(void) {return true; }
static int sec_bat_adc_ic_read(unsigned int channel) {return 0; }

static bool sec_bat_gpio_init(void)
{
	return true;
}
static bool sec_fg_gpio_init(void)
{
	return true;
}

static bool sec_chg_gpio_init(void)
{
	return true;
}

static bool sec_bat_is_lpm(void)
{
	return power_off_charging;
}

static void sec_bat_initial_check(void)
{
	union power_supply_propval value;

	switch (abb_get_cable_status()) {
	case CABLE_TYPE_AC:
		value.intval = POWER_SUPPLY_TYPE_MAINS;
		break;
	case CABLE_TYPE_MISC:
		value.intval = POWER_SUPPLY_TYPE_MISC;
		break;
	case CABLE_TYPE_USB:
		value.intval = POWER_SUPPLY_TYPE_USB;
		break;
	case CABLE_TYPE_CARDOCK:
		value.intval = POWER_SUPPLY_TYPE_CARDOCK;
		break;
	case CABLE_TYPE_NONE:
		value.intval = POWER_SUPPLY_TYPE_BATTERY;
		break;
	default:
		pr_err("%s: invalid cable :%d\n",
			__func__, abb_get_cable_status());
		return;
	}

	psy_do_property("battery", set,
		POWER_SUPPLY_PROP_ONLINE, value);
}

static bool sec_bat_check_jig_status(void) {return false; }
static void sec_bat_switch_to_check(void) {}
static void sec_bat_switch_to_normal(void) {}

static int sec_bat_check_cable_callback(void)
{
	return 0;
}

/* callback for battery check
 * return : bool
 * true - battery detected, false battery NOT detected
 */
static bool sec_bat_check_callback(void) {return true; }

static bool sec_bat_check_cable_result_callback(
				int cable_type)
{
	return true;
}

static bool sec_bat_check_result_callback(void) {return true; }

/* callback for OVP/UVLO check
 * return : int
 * battery health
 */
static int sec_bat_ovp_uvlo_callback(void)
{
	int health;
	health = POWER_SUPPLY_HEALTH_GOOD;

	return health;
}

static bool sec_bat_ovp_uvlo_result_callback(int health) {return true; }

/*
 * val.intval : temperature
 */
static bool sec_bat_get_temperature_callback(
				enum power_supply_property psp,
			    union power_supply_propval *val) {return true; }

static bool sec_fg_fuelalert_process(bool is_fuel_alerted) {return true; }

static const int temp_table[][2] = {
	{42,	700},
	{60,	600},
	{86,	500},
	{124,	400},
	{183,	300},
	{264,	200},
	{379,	100},
	{527,	0},
	{671,	-100},
	{884,	-200},
};


/* ADC region should be exclusive */
static sec_bat_adc_region_t cable_adc_value_table[] = {
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
	{0,	0},
};

static sec_charging_current_t charging_current_table[] = {
	{0,	0,	0,	0},
	{0,	0,	0,	0},
	{600,	1500,	185,	145}, /* POWER_SUPPLY_TYPE_MAINS */
	{500,	500,	185,	145}, /* POWER_SUPPLY_TYPE_USB */
	{600,	1500,	185,	145}, /* POWER_SUPPLY_TYPE_DCP */
	{500,	500,	185,	145}, /* POWER_SUPPLY_TYPE_CDP */
	{500,	500,	185,	145},   /* POWER_SUPPLY_TYPE_ACA */
	{0,	0,	0,	0},
	{0,	0,	0,	0},
	{0,	0,	0,	0},
	{0,	0,	0,	0},
	{0,	0,	0,	0},
};

static int polling_time_table[] = {
	10,	/* BASIC */
	10,	/* CHARGING */
	30,	/* DISCHARGING */
	30,	/* NOT_CHARGING */
	300,	/* SLEEP */
};

static struct v_to_cap cap_tbl[] = {
	{4162, 100},
	{4131, 99},
	{4088, 95},
	{4045, 90},
	{4024, 87},
	{3955, 80},
	{3893, 70},
	{3859, 65},
	{3825, 60},
	{3799, 55},
	{3780, 50},
	{3750, 40},
	{3731, 30},
	{3714, 25},
	{3683, 20},
	{3658, 17},
	{3648, 14},
	{3640, 12},
	{3627, 10},
	{3615, 9},
	{3566, 7},
	{3539, 6},
	{3477, 4},
	{3403, 2},
	{3361, 1},
	{3320, 0},
};

static struct v_to_cap cap_tbl_5ma[] = {
	{4328,	100},
	{4299,	99},
	{4281,	98},
	{4241,	95},
	{4183,	90},
	{4150,	87},
	{4116,	84},
	{4077,	80},
	{4068,	79},
	{4058,	77},
	{4026,	75},
	{3987,	72},
	{3974,	69},
	{3953,	66},
	{3933,	63},
	{3911,	60},
	{3900,	58},
	{3873,	55},
	{3842,	52},
	{3829,	50},
	{3810,	45},
	{3793,	40},
	{3783,	35},
	{3776,	30},
	{3762,	25},
	{3746,	20},
	{3739,	18},
	{3715,	15},
	{3700,	12},
	{3690,	10},
	{3680,	9},
	{3670,	7},
	{3656,	5},
	{3634,	4},
	{3614,	3},
	{3551,	2},
	{3458,	1},
	{3300,	0},
};

/* Battery voltage to Resistance table*/
static struct v_to_res res_tbl[] = {
	{4240,	160},
	{4210,	179},
	{4180,	183},
	{4160,	184},
	{4140,	191},
	{4120,	204},
	{4080,	200},
	{4027,	202},
	{3916,	221},
	{3842,	259},
	{3800,	262},
	{3742,	263},
	{3709,	277},
	{3685,	312},
	{3668,	258},
	{3660,	247},
	{3636,	293},
	{3616,	331},
	{3600,	349},
	{3593,	345},
	{3585,	344},
	{3572,	336},
	{3553,	321},
	{3517,	336},
	{3503,	322},
	{3400,	269},
	{3360,	328},
	{3330,	305},
	{3300,	339},
};

static struct v_to_res chg_res_tbl[] = {
	{4302, 230},
	{4276, 375},
	{4227, 375},
	{4171, 376},
	{4134, 341},
	{4084, 329},
	{4049, 361},
	{4012, 349},
	{3980, 322},
	{3960, 301},
	{3945, 283},
	{3939, 345},
	{3924, 304},
	{3915, 298},
	{3911, 317},
	{3905, 326},
	{3887, 352},
	{3861, 327},
	{3850, 212},
	{3800, 232},
	{3750, 177},
	{3712, 164},
	{3674, 161},
	{3590, 164},
};

static const struct fg_parameters fg = {
	.recovery_sleep_timer = 10,
	.recovery_total_time = 100,
	.init_timer = 1,
	.init_discard_time = 5,
	.init_total_time = 40,
	.high_curr_time = 60,
	.accu_charging = 20,
	.accu_high_curr = 20,
	.high_curr_threshold = 50,
	.lowbat_threshold = 3300,
	.battok_falling_th_sel0 = 2860,
	.battok_raising_th_sel1 = 2860,
	.user_cap_limit = 15,
	.maint_thres = 97,
#ifdef CONFIG_AB8505_SMPL
	.pcut_enable = 1,
	.pcut_max_time = 10,		/* 10 * 15.625ms */
	.pcut_max_restart = 15,		/* Unlimited */
	.pcut_debounce_time = 2,	/* 15.625 ms */
#endif
};

static const struct chg_parameters chg = {
	.volt_ovp = 6775,
	.volt_ovp_recovery = 6900,
};

static const struct battery_info battery_info = {
	.charge_full_design = 1500,
	.nominal_voltage = 3820,
	.resis_high = 100000,
	.resis_low = 64000,
	.battery_resistance = 100,
	.line_impedance = 36,
	.battery_resistance_for_charging = 200,
	.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl_5ma),
	.v_to_cap_tbl = cap_tbl_5ma,
	.n_v_res_tbl_elements = ARRAY_SIZE(res_tbl),
	.v_to_res_tbl = res_tbl,
	.n_v_chg_res_tbl_elements = ARRAY_SIZE(chg_res_tbl),
	.v_to_chg_res_tbl = chg_res_tbl,
};

static const struct capacity_levels battery_cap_levels = {
	.critical	= 2,
	.low		= 10,
	.normal		= 70,
	.high		= 95,
	.full		= 100,
};

static struct battery_data_t abb_battery_data[] = {
	{
		/* For AB850x */
		.autopower_cfg = true,
		.enable_overshoot = false,
		.bkup_bat_v = BUP_VCH_SEL_3P1V,
		.bkup_bat_i = BUP_ICH_SEL_50UA,

		.fg_res_chg = 125,
		.fg_res_dischg = 130,
		.lowbat_zero_voltage = 3320,

		.abb_set_vbus_state = abb_vbus_is_detected,
		.bat_info = &battery_info,
		.cap_levels = &battery_cap_levels,
		.fg_params = &fg,
		.chg_params = &chg,
	}
};

sec_battery_platform_data_t sec_battery_pdata = {
	/* NO NEED TO BE CHANGED */
	.initial_check = sec_bat_initial_check,
	.bat_gpio_init = sec_bat_gpio_init,
	.fg_gpio_init = sec_fg_gpio_init,
	.chg_gpio_init = sec_chg_gpio_init,

	.is_lpm = sec_bat_is_lpm,
	.check_jig_status = sec_bat_check_jig_status,
	.check_cable_callback =
		sec_bat_check_cable_callback,
	.cable_switch_check = sec_bat_switch_to_check,
	.cable_switch_normal = sec_bat_switch_to_normal,
	.check_cable_result_callback =
		sec_bat_check_cable_result_callback,
	.check_battery_callback =
		sec_bat_check_callback,
	.check_battery_result_callback =
		sec_bat_check_result_callback,
	.ovp_uvlo_callback = sec_bat_ovp_uvlo_callback,
	.ovp_uvlo_result_callback =
		sec_bat_ovp_uvlo_result_callback,
	.fuelalert_process = sec_fg_fuelalert_process,
	.get_temperature_callback =
		sec_bat_get_temperature_callback,

	.adc_api[SEC_BATTERY_ADC_TYPE_NONE] = {
		.init = sec_bat_adc_none_init,
		.exit = sec_bat_adc_none_exit,
		.read = sec_bat_adc_none_read
		},
	.adc_api[SEC_BATTERY_ADC_TYPE_AP] = {
		.init = sec_bat_adc_ap_init,
		.exit = sec_bat_adc_ap_exit,
		.read = sec_bat_adc_ap_read
		},
	.adc_api[SEC_BATTERY_ADC_TYPE_IC] = {
		.init = sec_bat_adc_ic_init,
		.exit = sec_bat_adc_ic_exit,
		.read = sec_bat_adc_ic_read
		},
	.cable_adc_value = cable_adc_value_table,
	.charging_current = charging_current_table,
	.polling_time = polling_time_table,
	/* NO NEED TO BE CHANGED */

	.pmic_name = SEC_BATTERY_PMIC_NAME,

	.adc_check_count = 1,
	.adc_type = {
		SEC_BATTERY_ADC_TYPE_NONE,	/* CABLE_CHECK */
		SEC_BATTERY_ADC_TYPE_NONE,	/* BAT_CHECK */
		SEC_BATTERY_ADC_TYPE_AP,	/* TEMP */
		SEC_BATTERY_ADC_TYPE_NONE,	/* TEMP_AMB */
		SEC_BATTERY_ADC_TYPE_AP,	/* FULL_CHECK */
	},

	/* Battery */
	.vendor = "SDI SDI",
	.technology = POWER_SUPPLY_TECHNOLOGY_LION,
	.battery_type = 0,
	.battery_data = (void *)abb_battery_data,
	.bat_gpio_ta_nconnected = 0,
	.bat_polarity_ta_nconnected = 0,
	.bat_irq = 0,
	.bat_irq_attr =
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	.cable_check_type =
		SEC_BATTERY_CABLE_CHECK_PSY |
		SEC_BATTERY_CABLE_CHECK_INT,
	.cable_source_type =
		SEC_BATTERY_CABLE_SOURCE_EXTERNAL |
		SEC_BATTERY_CABLE_SOURCE_CALLBACK,

	.event_check = false,
	.event_waiting_time = 600,

	/* Monitor setting */
	.polling_type = SEC_BATTERY_MONITOR_WORKQUEUE,
	.monitor_initial_count = 3,

	/* Battery check */
	.battery_check_type = SEC_BATTERY_CHECK_CHARGER,
	.check_count = 0,
	/* Battery check by ADC */
	.check_adc_max = 0,
	.check_adc_min = 0,

	/* OVP/UVLO check */
	.ovp_uvlo_check_type = SEC_BATTERY_OVP_UVLO_CHGPOLLING,

	/* Temperature check */
	.thermal_source = SEC_BATTERY_THERMAL_SOURCE_ADC,
	.temp_adc_table = temp_table,
	.temp_adc_table_size =
		sizeof(temp_table)/sizeof(sec_bat_adc_table_data_t),
	.temp_amb_adc_table = temp_table,
	.temp_amb_adc_table_size =
		sizeof(temp_table)/sizeof(sec_bat_adc_table_data_t),

	.temp_check_type = SEC_BATTERY_TEMP_CHECK_TEMP,
	.temp_check_count = 2,
	.temp_high_threshold_event = 650,
	.temp_high_recovery_event = 415,
	.temp_low_threshold_event = -30,
	.temp_low_recovery_event = 0,
	.temp_high_threshold_normal = 600,
	.temp_high_recovery_normal = 400,
	.temp_low_threshold_normal = -50,
	.temp_low_recovery_normal = 0,
	.temp_high_threshold_lpm = 600,
	.temp_high_recovery_lpm = 400,
	.temp_low_threshold_lpm = -50,
	.temp_low_recovery_lpm = 0,

	.full_check_type = SEC_BATTERY_FULLCHARGED_ADC_DUAL,
	.full_check_count = 3,
	.full_check_adc_1st = 185,
	.full_check_adc_2nd = 145,
	.chg_gpio_full_check = 0,
	.chg_polarity_full_check = 1,
	.full_condition_type =
		SEC_BATTERY_FULL_CONDITION_SOC |
		SEC_BATTERY_FULL_CONDITION_VCELL |
		SEC_BATTERY_FULL_CONDITION_NOTIMEFULL,
	.full_condition_soc = 95,
	.full_condition_vcell = 4320,

	.recharge_condition_type =
		SEC_BATTERY_RECHARGE_CONDITION_VCELL,
	.recharge_condition_soc = 98,
	.recharge_condition_vcell = 4300,

	.charging_total_time = 5 * 60 * 60,
	.recharging_total_time = 90 * 60,

	/* Fuel Gauge */
	.fg_irq = 0,
	.fg_irq_attr =
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
	.fuel_alert_soc = 4,
	.repeated_fuelalert = false,
	.capacity_calculation_type =
		SEC_FUELGAUGE_CAPACITY_TYPE_RAW |
		SEC_FUELGAUGE_CAPACITY_TYPE_SCALE,
		/* SEC_FUELGAUGE_CAPACITY_TYPE_ATOMIC, */
	.capacity_max = 990,
	.capacity_max_margin = 50,
	.capacity_min = 0,

	/* Charger */
	.chg_gpio_en = 0,
	.chg_polarity_en = 0,
	.chg_gpio_status = 0,
	.chg_polarity_status = 0,
	.chg_irq = 0,
	.chg_irq_attr = 0,
	.chg_float_voltage = 4350,

};

static struct platform_device sec_device_battery = {
	.name = "sec-battery",
	.id = -1,
	.dev.platform_data = &sec_battery_pdata,
};

static int __init sec_bat_set_boot_mode(char *mode)
{
	if (strncmp(mode, "1", 1) == 0)
		power_off_charging = true;
	else
		power_off_charging = false;

	pr_info("%s : %s", __func__, power_off_charging ?
				"POWER OFF CHARGING MODE" : "NORMAL");
	return 1;
}

__setup("lpm_boot=", sec_bat_set_boot_mode);

static int muic_accessory_notify(struct notifier_block *self,
			       unsigned long action, void *dev)
{
	pr_info("%s is called (0x%02x)\n", __func__, action);

	switch (action) {
	case USB_BOOT_OFF_PLUGGED:
		if (vbus_state)
			abb_charger_cb(true);
		break;
	case LEGACY_CHARGER_PLUGGED:
		abb_charger_cb(true);
		break;

	case CARKIT_TYPE1_PLUGGED:
	case CARKIT_TYPE2_PLUGGED:
	case DESKTOP_DOCK_PLUGGED:
		abb_dock_cb(true);
		if (vbus_state)
			abb_dock_charging();
		break;

	case USB_BOOT_OFF_UNPLUGGED:
	case LEGACY_CHARGER_UNPLUGGED:
	case CARKIT_TYPE1_UNPLUGGED:
	case CARKIT_TYPE2_UNPLUGGED:
	case DESKTOP_DOCK_UNPLUGGED:
		abb_battery_cb();

		break;
	}

	return NOTIFY_OK;
}

static int muic_notify(struct notifier_block *self,
			       unsigned long action, void *dev)
{
	pr_info("%s is called (0x%02x)\n", __func__, action);

	if (action & USB_SWITCH_DISCONNECTION_EVENT)
		abb_battery_cb();
	else {
		if (use_ab8505_iddet) {
			switch (action & 0x1F) {
			case    USB_DCP:
			case    USB_CDP:
			case    CARKIT_CHARGER1:
			case    CARKIT_CHARGER2:
				abb_charger_cb(true);
				break;

			case    USB_SDP_NOT_CHARGING:
			case    USB_SDP_CHARGING:
			case    USB_SDP_SUSPENDED:
				abb_usb_cb(true);
				break;

			case    USB_PHY_ENABLE_WITH_NO_VBUS_NO_IDGND:
				abb_uart_cb(true);
				if (vbus_state)
					abb_jig_charging();
				break;

			case    OFF_OR_NOT_CONFIGURED:
				abb_battery_cb();
				break;
			default:
				break;

			}
		} else {
			switch (action & 0xFFFFF) {
			case	EXTERNAL_DEDICATED_CHARGER:
			case	EXTERNAL_USB_CHARGER:
			case	EXTERNAL_JIG_USB_OFF:
			case	EXTERNAL_CAR_KIT:
				abb_charger_cb(true);
				break;

			case	EXTERNAL_USB:
				abb_usb_cb(true);
				break;

			case EXTERNAL_JIG_UART_OFF:
				abb_uart_cb(true);
				if (vbus_state)
					abb_jig_charging();
				break;
			default:
				break;
			}
		}
	}
	return NOTIFY_OK;
}

void __init sec_init_battery(void)
{
	cable_nb.notifier_call = muic_notify;
	cable_accessory_nb.notifier_call = muic_accessory_notify;

	if (use_ab8505_iddet) {
		micro_usb_register_usb_notifier(&cable_nb);
		micro_usb_register_notifier(&cable_accessory_nb);
	} else
		usb_switch_register_notify(&cable_nb);

	platform_device_register(&sec_device_battery);
}
#endif
