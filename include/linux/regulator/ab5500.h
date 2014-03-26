/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 */

#ifndef __LINUX_REGULATOR_AB5500_H
#define __LINUX_REGULATOR_AB5500_H

enum ab5500_regulator_id {
	AB5500_LDO_G,
	AB5500_LDO_H,
	AB5500_LDO_K,
	AB5500_LDO_L,
	AB5500_LDO_VDIGMIC,
	AB5500_LDO_SIM,
	AB5500_BIAS1,
	AB5500_BIAS2,
	AB5500_NUM_REGULATORS,
};

struct regulator_init_data;

struct ab5500_regulator_data {
	bool off_is_lowpower;
};

struct ab5500_regulator_platform_data {
	struct regulator_init_data *regulator;
	struct ab5500_regulator_data *data;
	int num_regulator;
};

#endif
