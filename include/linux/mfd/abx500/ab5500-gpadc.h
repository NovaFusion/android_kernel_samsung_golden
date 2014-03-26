/*
 * Copyright (C) 2010 ST-Ericsson SA
 * Licensed under GPLv2.
 *
 * Author: Vijaya Kumar K <vijay.kilari@stericsson.com>
 */

#ifndef	_AB5500_GPADC_H
#define _AB5500_GPADC_H

/*
 * GPADC source:
 * The BTEMP_BALL and PCB_TEMP are same. They differ if the
 * battery supports internal NTC resistor connected to BDATA
 * line. In this case, the BTEMP_BALL correspondss to BDATA
 * of GPADC as per AB5500 product spec.
 */

#define BTEMP_BALL		0
#define ACC_DETECT2		1
#define ACC_DETECT3		2
#define MAIN_BAT_V		3
#define MAIN_BAT_V_TXON		4
#define VBUS_V			5
#define USB_CHARGER_C		6
#define BK_BAT_V		7
#define DIE_TEMP		8
#define PCB_TEMP		9
#define XTAL_TEMP		10
#define USB_ID			11
#define BAT_CTRL		12
/* VBAT with TXON only min trigger */
#define MAIN_BAT_V_TXON_TRIG_MIN	13
/* VBAT with TX off only min trigger */
#define MAIN_BAT_V_TRIG_MIN		14
#define GPADC0_V		15
#define VIBRA_KELVIN		16

/*
 * Frequency of auto adc conversion
 */
#define MS1000		0x0
#define MS500		0x1
#define MS200		0x2
#define MS100		0x3
#define MS10		0x4

struct ab5500_gpadc;

/*
 * struct adc_auto_input - AB5500 GPADC auto trigger
 * @adc_mux                     Mux input
 * @freq                        freq of conversion
 * @min                         min value for trigger
 * @max                         max value for trigger
 * @auto_adc_callback           notification callback
 */
struct adc_auto_input {
	u8 mux;
	u8 freq;
	int min;
	int max;
	int (*auto_adc_callback)(int mux);
};

struct ab5500_gpadc *ab5500_gpadc_get(const char *name);
int ab5500_gpadc_convert(struct ab5500_gpadc *gpadc, u8 input);
int ab5500_gpadc_convert_auto(struct ab5500_gpadc *gpadc,
			struct adc_auto_input *auto_input);

#endif /* _AB5500_GPADC_H */
