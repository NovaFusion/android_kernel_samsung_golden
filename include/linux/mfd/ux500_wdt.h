/*
 * Copyright (C) ST Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 *
 * STE Ux500 PRCMU API
 */
#ifndef __UX500_WDT_H
#define __UX500_WDT_H

/**
 * struct ux500_wdt_ops - mfd device ux500_wdt platform data
 */
struct ux500_wdt_ops {
	/* watchdog */
	int (*enable) (u8 id);
	int (*disable) (u8 id);
	int (*kick) (u8 id);
	int (*load) (u8 id, u32 timeout);
	int (*config) (u8 num, bool sleep_auto_off);
};
#ifdef CONFIG_SAMSUNG_LOG_BUF
void wdog_disable();
#endif
extern u32 sec_dbug_level;
#endif /* __UX500_WDT_H */
