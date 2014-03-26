/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_CCU9540_H
#define __BOARD_CCU9540_H

/* U9540 generic GPIOs */
#define U9540_HDMI_RST_GPIO		196

#define PRCM_DEBUG_NOPWRDOWN_VAL	0x194
#define ARM_DEBUG_NOPOWER_DOWN_REQ	1

struct i2c_board_info;

void __init ccu9540_pins_init(void);
extern void mop500_sdi_init(void);
void __init mop500_msp_init(void);
void __init mop500_vibra_init(void);

int uib_is_stuib(void);
int uib_is_u9540uibs_v1(void);
int uib_is_u9540uibs_v2(void);
int uib_is_u9540uibs(void);
int uib_is_u9540uibt_v1(void);
int uib_is_u9540uibt(void);

#endif
