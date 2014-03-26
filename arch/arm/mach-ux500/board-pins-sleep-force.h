/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_PINS_SLEEP_FORCE_H
#define __BOARD_PINS_SLEEP_FORCE_H

#include <plat/pincfg.h>

#define NMK_GPIO_PER_CHIP 32
#define GPIO_BLOCK_SHIFT 5

#define GPIO_IS_NOT_CHANGED 0
#define GPIO_IS_INPUT 1
#define GPIO_IS_OUTPUT 2

#define GPIO_WAKEUP_IS_ENABLED 0
#define GPIO_WAKEUP_IS_DISBLED 1

#define GPIO_IS_NO_CHANGE 0
#define GPIO_IS_OUTPUT_LOW 1
#define GPIO_IS_OUTPUT_HIGH 2

#define GPIO_PULL_NO_CHANGE 0
#define GPIO_PULL_UPDOWN_DISABLED 1
#define GPIO_IS_PULLUP 2
#define GPIO_IS_PULLDOWN 3

#define GPIO_PDIS_NO_CHANGE 0
#define GPIO_PDIS_DISABLED 1
#define GPIO_PDIS_ENABLED 2

void sleep_pins_config_pm_mux(pin_cfg_t *cfgs, int num);
void sleep_pins_config_pm(pin_cfg_t *cfgs, int num);

#endif
