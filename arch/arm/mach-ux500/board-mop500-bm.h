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

#ifndef __BOARD_MOP500_BM_H
#define __BOARD_MOP500_BM_H

#include <linux/mfd/abx500/ab8500-bm.h>

extern struct ab8500_charger_platform_data ab8500_charger_plat_data;
extern struct ab8500_btemp_platform_data ab8500_btemp_plat_data;
extern struct ab8500_fg_platform_data ab8500_fg_plat_data;
extern struct ab8500_chargalg_platform_data ab8500_chargalg_plat_data;
extern struct ab8500_bm_data ab8500_bm_data;
extern struct ab8500_pwmled_platform_data ab8500_pwmled_plat_data;

#endif
