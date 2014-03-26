/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Alexandre Torgue <alexandre.torgue@stericsson.com> for ST-Ericsson
 *
 * CCU9540 board specific initialization for regulators
 */

#ifndef __BOARD_CCU9540_REGULATORS_H
#define __BOARD_CCU9540_REGULATORS_H

#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>

extern struct ab8500_regulator_platform_data ab9540_regulator_plat_data;

#endif
