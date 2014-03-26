/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Ludovic Barre <ludovic.barre@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _MLOADER_UX500_H_
#define _MLOADER_UX500_H_

/**
 * struct dbx500_ml_area - data structure for modem memory areas description
 * @name: name of the area
 * @start: start address of the area
 * @size: size of the area
 */
struct dbx500_ml_area {
	const char *name;
	u32 start;
	u32 size;
};

/**
 * struct dbx500_ml_fw - data stucture for modem firmwares description
 * @name: firmware name
 * @area: area where firmware is uploaded
 * @offset: offset in the area where firmware is uploaded
 */
struct dbx500_ml_fw {
	const char *name;
	struct dbx500_ml_area *area;
	u32 offset;
};

/**
 * struct dbx500_mloader_pdata - data structure for platform specific data
 * @fws: pointer on firmwares table
 * @nr_fws: number of firmwares
 * @areas: pointer on areas table
 * @nr_areas: number of areas
 */
struct dbx500_mloader_pdata {
	struct dbx500_ml_fw *fws;
	int nr_fws;
	struct dbx500_ml_area *areas;
	int	nr_areas;
};

#endif /* _MLOADER_UX500_H_ */
