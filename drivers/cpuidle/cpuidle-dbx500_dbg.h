/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * License Terms: GNU General Public License v2
 * Author: Rickard Andersson <rickard.andersson@stericsson.com> for ST-Ericsson
 *	   Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 */

#ifndef __DBX500_CPUIDLE_DBG_H
#define __DBX500_CPUIDLE_DBG_H

#include <linux/ktime.h>

#ifndef CONFIG_DBX500_CPUIDLE_DEEPEST_STATE
#define CONFIG_DBX500_CPUIDLE_DEEPEST_STATE 1
#endif

enum post_mortem_sleep {
	NO_SLEEP_PROGRAMMED = 0x12345678,
};

#ifdef CONFIG_DBX500_CPUIDLE_DEBUG
void ux500_ci_dbg_init(void);
void ux500_ci_dbg_remove(void);

void ux500_ci_dbg_log(int ctarget,
		      ktime_t enter_time);

void ux500_ci_dbg_log_post_mortem(int target,
				  ktime_t enter_time, ktime_t est_wake_common,
				  ktime_t est_wake, int sleep, bool is_last);

void ux500_ci_dbg_wake_latency(int ctarget, int sleep_time);
void ux500_ci_dbg_exit_latency(int ctarget, ktime_t now, ktime_t exit,
			       ktime_t enter);
void ux500_ci_dbg_wake_time(ktime_t time_wake);
void ux500_ci_dbg_register_reason(int idx,
				  bool ape,
				  bool modem,
				  bool uart,
				  u32 sleep_time,
				  u32 max_depth);

bool ux500_ci_dbg_force_ape_on(void);
int ux500_ci_dbg_deepest_state(void);
void ux500_ci_dbg_set_deepest_state(int state);

void ux500_ci_dbg_console(void);
void ux500_ci_dbg_console_check_uart(void);
void ux500_ci_dbg_console_handle_ape_resume(void);
void ux500_ci_dbg_console_handle_ape_suspend(void);

void ux500_ci_dbg_plug(int cpu);
void ux500_ci_dbg_unplug(int cpu);

#else

static inline void ux500_ci_dbg_init(void) { }
static inline void ux500_ci_dbg_remove(void) { }

static inline void ux500_ci_dbg_log(int ctarget,
				    ktime_t enter_time) { }

static inline void ux500_ci_dbg_log_post_mortem(int target,
						ktime_t enter_time,
						ktime_t est_wake_common,
						ktime_t est_wake, int sleep,
						bool is_last) { }
static inline void ux500_ci_dbg_exit_latency(int ctarget,
					     ktime_t now, ktime_t exit,
					     ktime_t enter) { }
static inline void ux500_ci_dbg_wake_latency(int ctarget, int sleep_time) { }
static inline void ux500_ci_dbg_wake_time(ktime_t time_wake) { }
static inline void ux500_ci_dbg_register_reason(int idx,
						bool ape,
						bool modem,
						bool uart,
						u32 sleep_time,
						u32 max_depth) { }

static inline bool ux500_ci_dbg_force_ape_on(void)
{
	return false;
}

static inline int ux500_ci_dbg_deepest_state(void)
{
	return CONFIG_DBX500_CPUIDLE_DEEPEST_STATE;
}

static inline void ux500_ci_dbg_set_deepest_state(int state) { }

static inline void ux500_ci_dbg_console(void) { }
static inline void ux500_ci_dbg_console_check_uart(void) { }
static inline void ux500_ci_dbg_console_handle_ape_resume(void) { }
static inline void ux500_ci_dbg_console_handle_ape_suspend(void) { }

static inline void ux500_ci_dbg_plug(int cpu) { }
static inline void ux500_ci_dbg_unplug(int cpu) { }

#endif
#endif
