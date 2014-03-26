/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Derek Morton <derek.morton@stericsson.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Power state driver for M6718 MODEM
 */
#ifndef MODEM_STATE_H
#define MODEM_STATE_H

enum modem_states {
	MODEM_STATE_OFF,
	MODEM_STATE_RESET,
	MODEM_STATE_CRASH,
	MODEM_STATE_ON,
	/*
	 * Add new states before end marker and update modem_state_str[]
	 * in modem_state.c
	 */
	MODEM_STATE_END_MARKER
};

void modem_state_power_on(void);
void modem_state_power_off(void);
void modem_state_force_reset(void);
int modem_state_get_state(void);
char *modem_state_to_str(int state);

/* Callbacks will be running in tasklet context */
int modem_state_register_callback(int (*callback) (unsigned long),
	unsigned long data);
int modem_state_remove_callback(int (*callback) (unsigned long));

#endif
