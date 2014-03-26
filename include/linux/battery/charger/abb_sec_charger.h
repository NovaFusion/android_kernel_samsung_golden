/*
 * abb_sec_charger.h
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

#ifndef __ABB_SEC_CHARGER_H
#define __ABB_SEC_CHARGER_H

enum cable_type_t {
	CABLE_TYPE_NONE = 0,
	CABLE_TYPE_USB,
	CABLE_TYPE_AC,
	CABLE_TYPE_MISC,
	CABLE_TYPE_CARDOCK,
	CABLE_TYPE_UARTOFF,
	CABLE_TYPE_JIG,
	CABLE_TYPE_UNKNOWN,
	CABLE_TYPE_CDP,
	CABLE_TYPE_SMART_DOCK,
#ifdef CONFIG_WIRELESS_CHARGING
	CABLE_TYPE_WPC = 10,
#endif
};

enum ab8505_chg_link_status {
	OFF_OR_NOT_CONFIGURED = 0,
	USB_SDP_NOT_CHARGING,
	USB_SDP_CHARGING,
	USB_SDP_SUSPENDED,
	USB_CDP,
	RESERVED0,
	RESERVED1,
	USB_DCP,
	USB_ACA_A,
	USB_ACA_B,
	USB_ACA_C,
	RESERVED2,
	RESERVED3,
	USB_STANDARD_UPSTREAM_PORT1, /* IDGND and no VBUS */
	USB_CHARGER_PORT_NOT_OK, /* not able to supply current set on VBUS */
	USB_CHAGER_WITH_DM_HIGH,
	USB_PHY_ENABLE_WITH_NO_VBUS_NO_IDGND,
	USB_STANDARD_UPSTREAM_PORT2, /* No IDGND and VBUS */
	USB_STANDARD_UPSTRAM_PORT3, /* IDGND and VBUS */
	CHARGER_with_Data_lines_SE1,
	CARKIT_CHARGER1,
	CARKIT_CHARGER2,
	USB_ACA_DOCK_CHARGERS,
};

struct battery_info {

	int charge_full_design;
	int nominal_voltage;
	int resis_high;
	int resis_low;

	int battery_resistance;
	int line_impedance;
	int battery_resistance_for_charging;

	int n_v_cap_tbl_elements;
	struct v_to_cap *v_to_cap_tbl;
	int n_v_res_tbl_elements;
	struct v_to_res *v_to_res_tbl;
	int n_v_chg_res_tbl_elements;
	struct v_to_res *v_to_chg_res_tbl;

};

struct capacity_levels {
	int critical;
	int low;
	int normal;
	int high;
	int full;
};

/**
 * struct chg_parameter
 * @volt_ovp:		ovp threshold in charging -> ovp status
 * @volt_ovp_recovery:	ovp threshold in ovp status -> charging
 */
struct chg_parameters {
	int volt_ovp;
	int volt_ovp_recovery;
};


/**
 * struct fg_parameters - Fuel gauge algorithm parameters, in seconds
 * if not specified
 * @recovery_sleep_timer:	Time between measurements while recovering
 * @recovery_total_time:	Total recovery time
 * @init_timer:			Measurement interval during startup
 * @init_discard_time:		Time we discard voltage measurement at startup
 * @init_total_time:		Total init time during startup
 * @high_curr_time:		Time current has to be high to go to recovery
 * @accu_charging:		FG accumulation time while charging
 * @accu_high_curr:		FG accumulation time in high current mode
 * @high_curr_threshold:	High current threshold, in mA
 * @lowbat_threshold:		Low battery threshold, in mV
 * @battok_falling_th_sel0:	Threshold in mV for battOk signal sel0
 *				Resolution in 50 mV step.
 * @battok_raising_th_sel1:	Threshold in mV for battOk signal sel1
 *				Resolution in 50 mV step.
 * @user_cap_limit:		Capacity reported from user must be within this
 *				limit to be considered as sane, in percentage
 *				points.
 * @maint_thres:		This is the threshold where we stop reporting
 *				battery full while in maintenance, in per cent
 * @pcut_enable:		Enable power cut feature in ab8505
 * @pcut_max_time:		Max time threshold
 * @pcut_max_restart:		Max number of restarts
 * @pcut_debounce_time:		Sets battery debounce time
 */
struct fg_parameters {
	int recovery_sleep_timer;
	int recovery_total_time;
	int init_timer;
	int init_discard_time;
	int init_total_time;
	int high_curr_time;
	int accu_charging;
	int accu_high_curr;
	int high_curr_threshold;
	int lowbat_threshold;
	int battok_falling_th_sel0;
	int battok_raising_th_sel1;
	int user_cap_limit;
	int maint_thres;
#ifdef CONFIG_AB8505_SMPL
	bool pcut_enable;
	u8 pcut_max_time;
	u8 pcut_max_restart;
	u8 pcut_debounce_time;
#endif
};

struct battery_data_t {
	/* For AB850x */
	bool autopower_cfg;
	bool enable_overshoot;
	int bkup_bat_v;
	int bkup_bat_i;

	int fg_res_chg;
	int fg_res_dischg;
	int lowbat_zero_voltage;

	void (*abb_set_vbus_state)(bool state);
	const struct battery_info *bat_info;
	const struct capacity_levels *cap_levels;
	const struct fg_parameters *fg_params;
	const struct chg_parameters *chg_params;
};

#endif /* __ABB_SEC_CHARGER_H */
