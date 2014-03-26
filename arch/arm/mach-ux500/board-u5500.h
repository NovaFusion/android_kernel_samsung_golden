/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_U5500_H
#define __BOARD_U5500_H

#define GPIO_SDMMC_CD          180
#define GPIO_MMC_CARD_CTRL     227
#define GPIO_MMC_CARD_VSEL     185
#define GPIO_BOARD_VERSION  0
#define GPIO_PRIMARY_CAM_XSHUTDOWN  1
#define GPIO_SECONDARY_CAM_XSHUTDOWN  2
#define GPIO_CAMERA_PMIC_EN 212
#define GPIO_SW_CRASH_INDICATOR	214

#define CYPRESS_TOUCH_INT_PIN 179
#define CYPRESS_TOUCH_RST_GPIO 135
#define CYPRESS_SLAVE_SELECT_GPIO 186

#define LM3530_BL_ENABLE_GPIO  224

struct ab5500_regulator_platform_data;
extern struct ab5500_regulator_platform_data u5500_ab5500_regulator_data;

extern void u5500_pins_init(void);
extern void __init u5500_regulators_init(void);
void u5500_cyttsp_init(void);
bool u5500_board_is_s5500(void);
int u5500_get_boot_mmc(void);
bool u5500_board_is_pre_r3a(void);

#endif
