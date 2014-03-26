/*----------------------------------------------------------------------------
 * ab5500-vibra.h header file for ab5500 vibrator driver
 *
 * Copyright (C) 2011 ST-Ericsson SA.
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Shreshtha Kumar SAHU <shreshthakumar.sahu@stericsson.com>
 *
 */

#ifndef __AB5500_VIBRA_H__
#define __AB5500_VIBRA_H__

enum ab5500_vibra_type {
	AB5500_VIB_ROTARY,
	AB5500_VIB_LINEAR,
};

/* Vibrator Voltage */
#define AB5500_VIB_VOLT_MIN	(0x00)	/* 1.3 Volt */
#define AB5500_VIB_VOLT_MAX	(0x0A)	/* 3.5 Volt */
#define AB5500_VIB_VOLT_STEP	(0x01)	/* 0.2 Volt */


/* Linear Vibrator Resonance Frequncy */
#define AB5500_VIB_RFREQ_100HZ	(0xFB)
#define AB5500_VIB_RFREQ_150HZ	(0x52)
#define AB5500_VIB_RFREQ_196HZ	(0x03)

/* Vibrator pulse duration in milliseconds */
enum ab5500_vibra_pulse {
	AB5500_VIB_PULSE_OFF,
	AB5500_VIB_PULSE_20ms,
	AB5500_VIB_PULSE_75ms,
	AB5500_VIB_PULSE_130ms,
	AB5500_VIB_PULSE_170ms,
};

/**
 * struct ab5500_vibra_platform_data
 * @voltage:	Vibra output voltage
 * @res_freq:	Linear vibra resonance freq.
 * @type:	Vibra HW type
 * @pulse:	Vibra pulse duration in ms
 * @eol_voltage: EOL voltage in mV
 */
struct ab5500_vibra_platform_data {
	u8 voltage;
	u8 res_freq;
	u8 magnitude;
	enum ab5500_vibra_type type;
	enum ab5500_vibra_pulse pulse;
	int eol_voltage;
};

#endif /* __AB5500_VIBRA_H__ */
