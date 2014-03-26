/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms:  GNU General Public License (GPL), version 2
 *
 * U5500 board specific charger and battery initialization parameters.
 *
 * License Terms: GNU General Public License v2
 * Authors:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 */

#ifndef __BOARD_U5500_BM_H
#define __BOARD_U5500_BM_H

#include <linux/mfd/abx500/ab5500-bm.h>

extern struct abx500_charger_platform_data ab5500_charger_plat_data;
extern struct abx500_btemp_platform_data ab5500_btemp_plat_data;
extern struct abx500_fg_platform_data ab5500_fg_plat_data;
extern struct abx500_chargalg_platform_data abx500_chargalg_plat_data;
extern struct abx500_bm_data ab5500_bm_data;
extern struct abx500_bm_plat_data abx500_bm_pt_data;

#endif
