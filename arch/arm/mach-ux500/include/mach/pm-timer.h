/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 *
 * License Terms: GNU General Public License v2
 *
 */

#ifndef PM_TIMER_H
#define PM_TIMER_H

#include <linux/ktime.h>

#ifdef CONFIG_DBX500_CPUIDLE_DEBUG
ktime_t u8500_rtc_exit_latency_get(void);
void ux500_rtcrtt_measure_latency(bool enable);
#else
static inline ktime_t u8500_rtc_exit_latency_get(void)
{
	return ktime_set(0, 0);
}
static inline void ux500_rtcrtt_measure_latency(bool enable) { }

#endif

void ux500_rtcrtt_off(void);
void ux500_rtcrtt_next(u32 time_us);

#endif
