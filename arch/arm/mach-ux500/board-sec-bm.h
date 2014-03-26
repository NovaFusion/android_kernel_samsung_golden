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

#ifndef __BOARD_SEC_BM_H
#define __BOARD_SEC_BM_H


#ifdef CONFIG_BATTERY_SAMSUNG
#include <linux/battery/sec_battery.h>
void sec_init_battery();
int abb_get_cable_status();
void abb_battery_cb();
void abb_usb_cb(bool attached);
void abb_charger_cb(bool attached);
void abb_jig_cb(bool attached);
void abb_uart_cb(bool attached);
void abb_dock_cb(bool attached);

extern sec_battery_platform_data_t sec_battery_pdata;
extern bool power_off_charging;
#endif

#include <linux/mfd/abx500/ab8500-bm.h>

extern struct ab8500_charger_platform_data ab8500_charger_plat_data;
extern struct ab8500_btemp_platform_data ab8500_btemp_plat_data;
extern struct ab8500_fg_platform_data ab8500_fg_plat_data;
extern struct ab8500_chargalg_platform_data ab8500_chargalg_plat_data;
extern struct ab8500_bm_data ab8500_bm_data;

#endif
