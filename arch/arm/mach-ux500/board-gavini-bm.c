/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms:  GNU General Public License (GPL), version 2
 *
 * U8500 board specific charger and battery initialization parameters.
 *
 * Author: Johan Palsson <johan.palsson@stericsson.com> for ST-Ericsson.
 * Author: Johan Gardsmark <johan.gardsmark@stericsson.com> for ST-Ericsson.
 *
 */

#include <linux/power_supply.h>
#include "board-sec-bm.h"

#ifdef CONFIG_AB8500_BATTERY_THERM_ON_BATCTRL
/*
 * These are the defined batteries that uses a NTC and ID resistor placed
 * inside of the battery pack.
 * Note that the res_to_temp table must be strictly sorted by falling resistance
 * values to work.
 */
static struct res_to_temp temp_tbl_A[] = {
	{-5, 53407},
	{ 0, 48594},
	{ 5, 43804},
	{10, 39188},
	{15, 34870},
	{20, 30933},
	{25, 27422},
	{30, 24347},
	{35, 21694},
	{40, 19431},
	{45, 17517},
	{50, 15908},
	{55, 14561},
	{60, 13437},
	{65, 12500},
};
static struct res_to_temp temp_tbl_B[] = {
	{-5, 165418},
	{ 0, 159024},
	{ 5, 151921},
	{10, 144300},
	{15, 136424},
	{20, 128565},
	{25, 120978},
	{30, 113875},
	{35, 107397},
	{40, 101629},
	{45,  96592},
	{50,  92253},
	{55,  88569},
	{60,  85461},
	{65,  82869},
};
static struct v_to_cap cap_tbl_A[] = {
	{4171,	100},
	{4114,	 95},
	{4009,	 83},
	{3947,	 74},
	{3907,	 67},
	{3863,	 59},
	{3830,	 56},
	{3813,	 53},
	{3791,	 46},
	{3771,	 33},
	{3754,	 25},
	{3735,	 20},
	{3717,	 17},
	{3681,	 13},
	{3664,	  8},
	{3651,	  6},
	{3635,	  5},
	{3560,	  3},
	{3408,    1},
	{3247,	  0},
};
static struct v_to_cap cap_tbl_B[] = {
	{4161,	100},
	{4124,	 98},
	{4044,	 90},
	{4003,	 85},
	{3966,	 80},
	{3933,	 75},
	{3888,	 67},
	{3849,	 60},
	{3813,	 55},
	{3787,	 47},
	{3772,	 30},
	{3751,	 25},
	{3718,	 20},
	{3681,	 16},
	{3660,	 14},
	{3589,	 10},
	{3546,	  7},
	{3495,	  4},
	{3404,	  2},
	{3250,	  0},
};
#endif

static struct v_to_cap cap_tbl_200ma_1850ma[] = {
	{4186,	100},
	{4163,	 99},
	{4114,	 95},
	{4068,	 90},
	{3990,	 80},
	{3926,	 70},
	{3898,	 65},
	{3866,	 60},
	{3833,	 55},
	{3812,	 50},
	{3787,	 40},
	{3768,	 30},
	{3747,	 25},
	{3730,	 20},
	{3705,	 15},
	{3699,	 14},
	{3684,	 12},
	{3672,	  9},
	{3657,	  7},
	{3638,	  6},
	{3556,	  4},
	{3424,	  2},
	{3317,	  1},
	{3094,	  0},
};

static struct v_to_cap cap_tbl_5ma_1850ma[] = {
	{4172, 100},
	{4151, 99},
	{4108, 95},
	{4080, 92},
	{4054, 89},
	{4020, 85},
	{3996, 82},
	{3963, 77},
	{3920, 70},
	{3895, 65},
	{3879, 62},
	{3862, 59},
	{3826, 55},
	{3801, 50},
	{3778, 40},
	{3769, 30},
	{3758, 25},
	{3739, 20},
	{3731, 18},
	{3707, 15},
	{3679, 12},
	{3675, 10},
	{3672, 9},
	{3665, 7},
	{3660, 6},
	{3637, 4},
	{3547, 2},
	{3469, 1},
	{3338, 0},
};

static struct v_to_cap cap_tbl_200ma_2000ma[] = {
	{4322, 100},
	{4272, 99},
	{4257, 98},
	{4216, 95},
	{4155, 90},
	{4118, 87},
	{4086, 84},
	{4041, 80},
	{4020, 78},
	{4010, 77},
	{3979, 74},
	{3959, 72},
	{3932, 69},
	{3905, 66},
	{3876, 63},
	{3849, 60},
	{3833, 58},
	{3811, 55},
	{3795, 52},
	{3784, 50},
	{3762, 45},
	{3743, 40},
	{3730, 35},
	{3720, 30},
	{3705, 25},
	{3697, 23},
	{3678, 20},
	{3660, 18},
	{3627, 15},
	{3606, 12},
	{3591, 10},
	{3580, 9},
	{3557, 8},
	{3528, 7},
	{3461, 5},
	{3426, 4},
	{3395, 3},
	{3359, 2},
	{3328, 1},
	{3300, 0},
};

static struct v_to_cap cap_tbl_5ma_2000ma[] = {
	{4320, 100},
	{4296, 99},
	{4283, 98},
	{4245, 95},
	{4185, 90},
	{4152, 87},
	{4119, 84},
	{4077, 80},
	{4057, 78},
	{4048, 77},
	{4020, 74},
	{4003, 72},
	{3978, 69},
	{3955, 66},
	{3934, 63},
	{3912, 60},
	{3894, 58},
	{3860, 55},
	{3837, 52},
	{3827, 50},
	{3806, 45},
	{3791, 40},
	{3779, 35},
	{3770, 30},
	{3758, 25},
	{3739, 20},
	{3730, 18},
	{3706, 15},
	{3684, 13},
	{3675, 10},
	{3673, 9},
	{3665, 7},
	{3649, 5},
	{3628, 4},
	{3585, 3},
	{3525, 2},
	{3441, 1},
	{3300, 0},
};

/* Battery voltage to Resistance table*/
static struct v_to_res res_tbl[] = {
	{4194, 121},
	{4169, 188},
	{4136, 173},
	{4108, 158},
	{4064, 143},
	{3956, 160},
	{3847, 262},
	{3806, 280},
	{3801, 266},
	{3794, 259},
	{3785, 234},
	{3779, 227},
	{3772, 222},
	{3765, 221},
	{3759, 216},
	{3754, 206},
	{3747, 212},
	{3743, 208},
	{3737, 212},
	{3733, 200},
	{3728, 203},
	{3722, 207},
	{3719, 208},
	{3715, 209},
	{3712, 211},
	{3709, 210},
	{3704, 216},
	{3701, 218},
	{3698, 222},
	{3694, 218},
	{3692, 215},
	{3688, 224},
	{3686, 224},
	{3683, 228},
	{3681, 228},
	{3679, 229},
	{3676, 232},
	{3675, 229},
	{3673, 229},
	{3672, 223},
	{3669, 224},
	{3666, 224},
	{3663, 221},
	{3660, 218},
	{3657, 215},
	{3654, 212},
	{3649, 215},
	{3644, 215},
	{3636, 215},
	{3631, 206},
	{3623, 205},
	{3616, 193},
	{3605, 193},
	{3600, 198},
	{3597, 198},
	{3592, 203},
	{3591, 188},
	{3587, 188},
	{3583, 177},
	{3577, 170},
	{3568, 135},
	{3552, 54},
	{3526, 130},
	{3501, 48},
	{3442, 183},
	{3326, 372},
	{3161, 452},
};

static struct v_to_res chg_res_tbl[] = {
	{4360 , 128},
	{4325 , 130},
	{4316 , 148},
	{4308 , 162},
	{4301 , 162},
	{4250 , 162},
	{4230 , 164},
	{4030 , 164},
	{4000 , 193},
	{3950 , 204},
	{3850 , 210},
	{3800 , 230},
	{3790 , 240},
	{3780 , 311},
	{3760 , 420},
	{3700 , 504},
	{3600 , 565},
};


/*
 * Note that the res_to_temp table must be strictly sorted by falling
 * resistance values to work.
 */
static struct res_to_temp temp_tbl[] = {
	{-5, 214834},
	{ 0, 162943},
	{ 5, 124820},
	{10,  96520},
	{15,  75306},
	{20,  59254},
	{25,  47000},
	{30,  37566},
	{35,  30245},
	{40,  24520},
	{45,  20010},
	{50,  16432},
	{55,  13576},
	{60,  11280},
	{65,   9425},
};

static struct res_to_temp adc_temp_tbl[] = {
	{-10, 718},
	{-5, 619},
	{ 0, 528},
	{ 5, 450},
	{10, 381},
	{15, 319},
	{20, 269},
	{25, 220},
	{30, 180},
	{35, 153},
	{40, 130},
	{43, 118},
	{45, 107},
	{47, 100},
	{50, 90},
	{55, 73},
	{60, 61},
	{63, 57},
	{65, 53},
};

static const struct battery_type bat_type[] = {
	[BATTERY_UNKNOWN] = {
		/* First element always represent the UNKNOWN battery */
		.name = POWER_SUPPLY_TECHNOLOGY_UNKNOWN,
		.resis_high = 0,
		.resis_low = 0,
		.battery_resistance = 105,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
		.line_impedance = 43,
		.battery_resistance_for_charging = 160,
#endif
		.charge_full_design = 2000,
		.nominal_voltage = 3800,
		.termination_vol = 4320,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
		.termination_curr_1st = 160,
		.termination_curr_2nd = 120,
		.recharge_vol = 4300,
#else
		.termination_curr = 120,
#endif
		.normal_cur_lvl = 1500,
		.normal_vol_lvl = 4350,
		.maint_a_cur_lvl = 400,
		.maint_a_vol_lvl = 4050,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 400,
		.maint_b_vol_lvl = 4000,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
#ifdef CONFIG_MEASURE_TEMP_BY_ADC_TABLE
		.n_temp_tbl_elements = ARRAY_SIZE(adc_temp_tbl),
		.r_to_t_tbl = adc_temp_tbl,
#else
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl),
		.r_to_t_tbl = temp_tbl,
#endif
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl_5ma_2000ma),
		.v_to_cap_tbl = cap_tbl_5ma_2000ma,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
		.n_v_res_tbl_elements = ARRAY_SIZE(res_tbl),
		.v_to_res_tbl = res_tbl,
		.n_v_chg_res_tbl_elements = ARRAY_SIZE(chg_res_tbl),
		.v_to_chg_res_tbl = chg_res_tbl,
		.timeout_chargeoff_time = 21*HZ,	// specification is 25 +/- 5 seconds. 30*HZ ,
		.initial_timeout_time = HZ*3600*6 ,
		.subsequent_timeout_time = HZ*60*90,	//1.5 hours for second and later attempts
		.error_charge_stoptime = HZ*60,		// After an error stop charging for a minute.
		.over_voltage_threshold =  4500 ,
#else
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl),
		.batres_tbl = temp_to_batres_tbl,
#endif
	},

/*
 * These are the batteries that doesn't have an internal NTC resistor to measure
 * its temperature. The temperature in this case is measure with a NTC placed
 * near the battery but on the PCB.
 */


/*
		This battery entry is the real 1650/1500 mAh battery to be fitted to Godin identified by a 1.5K resistor 
	
*/
	{
		.name = POWER_SUPPLY_TECHNOLOGY_LION,
		.resis_high = 15000,	/* 1500 * 1.7 +70% */
		.resis_low = 0,		/* 1500 * 0.3 -70% */
		.battery_resistance = 105,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
		.line_impedance = 43,
		.battery_resistance_for_charging = 160,
#endif
		.charge_full_design = 2000,
		.nominal_voltage = 3800,
		.termination_vol =  4320,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
		.termination_curr_1st = 160,
		.termination_curr_2nd = 120,
		.recharge_vol = 4300,
#else
		.termination_curr = 120,
#endif
		.normal_cur_lvl = 1500,
		.normal_vol_lvl = 4350,		/* 4210 */
		.maint_a_cur_lvl = 600,
		.maint_a_vol_lvl = 4150,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 600,
		.maint_b_vol_lvl = 4100,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
#ifdef CONFIG_MEASURE_TEMP_BY_ADC_TABLE
		.n_temp_tbl_elements = ARRAY_SIZE(adc_temp_tbl),
		.r_to_t_tbl = adc_temp_tbl,
#else
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl),
		.r_to_t_tbl = temp_tbl,
#endif
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl_5ma_2000ma),
		.v_to_cap_tbl = cap_tbl_5ma_2000ma,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
		.n_v_res_tbl_elements = ARRAY_SIZE(res_tbl),
		.v_to_res_tbl = res_tbl,
		.n_v_chg_res_tbl_elements = ARRAY_SIZE(chg_res_tbl),
		.v_to_chg_res_tbl = chg_res_tbl,
		.timeout_chargeoff_time = 21*HZ,	// specification is 25 +/- 5 seconds. 30*HZ ,
		.initial_timeout_time = HZ*3600*6 ,
		.subsequent_timeout_time = HZ*60*90,	//1.5 hours for second and later attempts
		.error_charge_stoptime = HZ*60,		// After an error stop charging for a minute.
		.over_voltage_threshold =  4500 ,
#else
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl),
		.batres_tbl = temp_to_batres_tbl,
#endif
	},
};

static char *ab8500_charger_supplied_to[] = {
	"ab8500_chargalg",
	"ab8500_fg",
	"ab8500_btemp",
};

static char *ab8500_btemp_supplied_to[] = {
	"ab8500_chargalg",
	"ab8500_fg",
};

static char *ab8500_fg_supplied_to[] = {
	"ab8500_chargalg",
};

static char *ab8500_chargalg_supplied_to[] = {
	"ab8500_fg",
};

struct ab8500_charger_platform_data ab8500_charger_plat_data = {
	.supplied_to = ab8500_charger_supplied_to,
	.num_supplicants = ARRAY_SIZE(ab8500_charger_supplied_to),
};

struct ab8500_btemp_platform_data ab8500_btemp_plat_data = {
	.supplied_to = ab8500_btemp_supplied_to,
	.num_supplicants = ARRAY_SIZE(ab8500_btemp_supplied_to),
};

struct ab8500_fg_platform_data ab8500_fg_plat_data = {
	.supplied_to = ab8500_fg_supplied_to,
	.num_supplicants = ARRAY_SIZE(ab8500_fg_supplied_to),
};

struct ab8500_chargalg_platform_data ab8500_chargalg_plat_data = {
	.supplied_to = ab8500_chargalg_supplied_to,
	.num_supplicants = ARRAY_SIZE(ab8500_chargalg_supplied_to),
};

static const struct ab8500_bm_capacity_levels cap_levels = {
	.critical	= 2,
	.low		= 10,
	.normal		= 70,
	.high		= 95,
	.full		= 100,
};

static const struct ab8500_fg_parameters fg = {
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
};

static const struct ab8500_maxim_parameters maxi_params = {
	.ena_maxi = true,
	.chg_curr = 1500,
	.wait_cycles = 10,
	.charger_curr_step = 100,
};

/*
   If you want to change the charging current,
   you should modify X_curr_max, X_chg_current parameter.

   X_curr_max			: input current limit
   X_chg_current_input  : input current limit
   X_chg_current		: output current

*/
static const struct ab8500_bm_charger_parameters chg = {
	.usb_volt_max		= 5500,
	.usb_curr_max		=  500,
	/* When power supply set as 7000mV (OVP SPEC above 6.8V)
	   SET read it as .ac_volt_max.
	   After charging is disabled, SET read the voltage
	   as .ac_volt_max_recovery.
	   This value should be modified according to the model
	   experimentally.
	   There's distinct difference between ac voltage when charging
	   and ac voltage when discharging.
	*/
	.ac_volt_max		= 6650,
	.ac_volt_max_recovery	= 6800,
	.ac_curr_max		=  800,
};

struct ab8500_bm_data ab8500_bm_data = {
	.temp_under		= -5,
	.temp_low		= 0,
	.temp_high		= 40,
	.temp_over		= 60,
	.main_safety_tmr_h	= 4,
	.temp_interval_chg	= 20,
	.temp_interval_nochg	= 120,
#ifdef CONFIG_USB_SWITCHER
	.ta_chg_current			= 1500,
	.ta_chg_current_input		= 800,
	.ta_chg_voltage			= 4350,
	.usb_chg_current		= 500,
	.usb_chg_current_input		= 500,
	.usb_chg_voltage	= 4350,
#endif
	.main_safety_tmr_h	= 4,
	.usb_safety_tmr_h	= 4,
	.bkup_bat_v		= BUP_VCH_SEL_3P1V,
	.bkup_bat_i		= BUP_ICH_SEL_50UA,
	.no_maintenance		= true,
#ifdef CONFIG_AB8500_BATTERY_THERM_ON_BATCTRL
	.adc_therm		= ADC_THERM_BATCTRL,
#else
	.adc_therm		= ADC_THERM_BATTEMP,
#endif
	.chg_unknown_bat	= false,
	.enable_overshoot	= false,
	/* Please find the real setting for fg_res
	   in the ab8500_fg.c probe function  */
	.fg_res			= 133,
	.cap_levels		= &cap_levels,
	.bat_type		= bat_type,
	.n_btypes		= ARRAY_SIZE(bat_type),
	.batt_id		= 0,
	.interval_charging	= 5,
	.interval_not_charging	= 120,
	.temp_hysteresis	= 22,		//turn back on if temp < (43 
	.maxi			= &maxi_params,
	.chg_params		= &chg,
	.fg_params		= &fg,
#ifdef CONFIG_SAMSUNG_CHARGER_SPEC
	.charge_state		= 1,
	.batt_res		= 0,
	.low_temp_hysteresis	= 3,
	.use_safety_timer       = 0 ,
#endif
};
