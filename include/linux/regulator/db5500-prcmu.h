/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * Interface to power domain regulators on DB5500
 */

#ifndef __DB5500_REGULATOR_H__
#define __DB5500_REGULATOR_H__

#include <linux/regulator/dbx500-prcmu.h>

/* Number of DB5500 regulators and regulator enumeration */
enum db5500_regulator_id {
	DB5500_REGULATOR_VAPE,
	DB5500_REGULATOR_SWITCH_SGA,
	DB5500_REGULATOR_SWITCH_HVA,
	DB5500_REGULATOR_SWITCH_SIA,
	DB5500_REGULATOR_SWITCH_DISP,
	DB5500_REGULATOR_SWITCH_ESRAM12,
	DB5500_NUM_REGULATORS
};

/**
 * struct db5500_regulator_init_data - mfd device prcmu-regulators data
 *
 */
struct db5500_regulator_init_data {
	int (*set_epod) (u16 epod_id, u8 epod_state);
	void *regulators;
	int reg_num;
};


#endif
