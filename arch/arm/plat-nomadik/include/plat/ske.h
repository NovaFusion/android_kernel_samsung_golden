/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Naveen Kumar Gaddipati <naveen.gaddipati@stericsson.com>
 *
 * ux500 Scroll key and Keypad Encoder (SKE) header
 */

#ifndef __SKE_H
#define __SKE_H

#include <linux/input/matrix_keypad.h>

/* register definitions for SKE peripheral */
#define SKE_CR		0x00
#define SKE_VAL0	0x04
#define SKE_VAL1	0x08
#define SKE_DBCR	0x0C
#define SKE_IMSC	0x10
#define SKE_RIS		0x14
#define SKE_MIS		0x18
#define SKE_ICR		0x1C

#define SKE_KPD_MAX_ROWS        8
#define SKE_KPD_MAX_COLS        8

/*
 * Keypad module
 */

/**
 * struct keypad_platform_data - structure for platform specific data
 * @init:	pointer to keypad init function
 * @exit:	pointer to keypad deinitialisation function
 * @gpio_input_pins:	pointer to gpio input pins
 * @gpio_output_pins:	pointer to gpio output pins
 * @keymap_data: matrix scan code table for keycodes
 * @krow:	maximum number of rows
 * @kcol:	maximum number of columns
 * @kconnected_rows: number of rows actually connected
 * @kconnected_cols: number of columns actually connected
 * @debounce_ms: platform specific debounce time
 * @no_autorepeat: flag for auto repetition
 * @wakeup_enable: allow waking up the system
 * @switch_delay: gpio switch_delay
 */
struct ske_keypad_platform_data {
	int (*init)(void);
	int (*exit)(void);
	int *gpio_input_pins;
	int *gpio_output_pins;
	const struct matrix_keymap_data *keymap_data;
	u8 krow;
	u8 kcol;
	u8 kconnected_rows;
	u8 kconnected_cols;
	u8 debounce_ms;
	bool no_autorepeat;
	bool wakeup_enable;
	int switch_delay;
};
#endif	/*__SKE_KPD_H*/
