/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License, version 2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 */

#ifndef __DB5500_KEYPAD_H
#define __DB5500_KEYPAD_H

#include <linux/input/matrix_keypad.h>

#define KEYPAD_MAX_ROWS		9
#define KEYPAD_MAX_COLS		8

/**
 * struct db5500_keypad_platform_data - structure for platform specific data
 * @keymap_data: matrix scan code table for keycodes
 * @debounce_ms: platform specific debounce time
 * @no_autorepeat: flag for auto repetition
 * @init : pointer to keypad init function
 * @exit : pointer to keypad exit function
 * @krow : maximum number of rows
 * @kcol : maximum number of cols
 * @gpio_input_pins: pointer to gpio input pins
 * @gpio_output_pins: pointer to gpio output pins
 * @switch_delay : gpio switch_delay
 */
struct db5500_keypad_platform_data {
	const struct matrix_keymap_data *keymap_data;
	u8 debounce_ms;
	bool no_autorepeat;
	int (*init)(void);
	int (*exit)(void);
	u8 krow;
	u8 kcol;
	int *gpio_input_pins;
	int *gpio_output_pins;
	int switch_delay;
};

#endif
