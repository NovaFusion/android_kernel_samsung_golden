/*
 * Copyright (C) 2011 ST-Ericsson SA.
 *
 * License Terms: GNU General Public License v2
 *
 * Simple driver for HVLED in ST-Ericsson AB5500 Analog baseband Controller
 *
 * Author: Shreshtha Kumar SAHU <shreshthakumar.sahu@stericsson.com>
 */

#define AB5500_HVLED0		0
#define AB5500_HVLED1		1
#define AB5500_HVLED2		2
#define AB5500_HVLEDS_MAX	3

enum ab5500_fade_delay {
	AB5500_FADE_DELAY_BYPASS = 0,
	AB5500_FADE_DELAY_HALFSEC,
	AB5500_FADE_DELAY_ONESEC,
	AB5500_FADE_DELAY_TWOSEC
};

struct ab5500_led_conf {
	char *name;
	u8 led_id;
	u8 max_current;
	u8 fade_hi;
	u8 fade_lo;
	bool led_on;
};

struct ab5500_hvleds_platform_data {
	bool hw_fade;
	struct ab5500_led_conf leds[AB5500_HVLEDS_MAX];
};
