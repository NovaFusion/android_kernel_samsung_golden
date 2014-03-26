/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __MACH_SUSPEND_H
#define __MACH_SUSPEND_H

#ifdef CONFIG_UX500_SUSPEND
void suspend_block_sleep(void);
void suspend_unblock_sleep(void);
bool suspend_sleep_is_blocked(void);
void suspend_set_pins_force_fn(void (*force)(void), void (*force_mux)(void));
bool is_suspend_ongoing(void);
#else
static inline void suspend_block_sleep(void) { }
static inline void suspend_unblock_sleep(void) { }
static inline bool suspend_sleep_is_blocked(void) { return false; }
static inline void suspend_set_pins_force_fn(void (*force)(void),
					     void (*force_mux)(void)) { }
static inline bool is_suspend_ongoing(void) { return false; }
#endif

#endif /* __MACH_SUSPEND_H */
