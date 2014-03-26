/*
 * Copyright (C) ST Ericsson SA 2012
 *
 * License Terms: GNU General Public License v2
 *
 * STE Ux500 PRCMU API
 */
#ifndef __DBX500_TEMP_H
#define __DBX500_TEMP_H

/**
 * struct dbx500_temp_ops - mfd device hwmon platform data
 */
struct dbx500_temp_ops {
	/*  hwmon */
	int (*config_hotdog) (u8 threshold);
	int (*config_hotmon) (u8 low, u8 high);
	int (*start_temp_sense) (u16 cycles32k);
	int (*stop_temp_sense) (void);
	int (*thsensor_get_temp) (void);
};

struct dbx500_temp_pdata {
	struct dbx500_temp_ops *ops;
	bool monitoring_active;
};

#endif /* __DBX500_TEMP_H */
