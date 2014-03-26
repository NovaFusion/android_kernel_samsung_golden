/*
 * Copyright (C) 2009 ST-Ericsson SA
 * Copyright (C) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __BOARD_CODINA_REGULATORS_H
#define __BOARD_CODINA_REGULATORS_H

#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>

extern struct regulator_init_data codina_ab8500_regulators[AB8500_NUM_REGULATORS];
extern struct ab8500_regulator_platform_data codina_ab8500_regulator_plat_data;

extern struct regulator_init_data codina_ab8505_regulators[];
extern struct ab8500_regulator_platform_data codina_ab8505_regulator_plat_data;

#endif
