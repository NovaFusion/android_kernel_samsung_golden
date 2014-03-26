/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Martin Persson for ST-Ericsson
 *         Etienne Carriere <etienne.carriere@stericsson.com> for ST-Ericsson
 *
 */

#ifndef PRCMU_DEBUG_H
#define PRCMU_DEBUG_H

#ifdef CONFIG_DBX500_PRCMU_DEBUG
void prcmu_debug_ape_opp_log(u8 opp);
void prcmu_debug_ddr_opp_log(u8 opp);
void prcmu_debug_vsafe_opp_log(u8 opp);
void prcmu_debug_arm_opp_log(u32 value);
void prcmu_debug_dump_data_mem(void);
void prcmu_debug_dump_regs(void);
void prcmu_debug_register_interrupt(u32 mailbox);
void prcmu_debug_register_mbox0_event(u32 ev, u32 mask);
#else
static inline void prcmu_debug_ape_opp_log(u8 opp) {}
static inline void prcmu_debug_ddr_opp_log(u8 opp) {}
static inline void prcmu_debug_vsafe_opp_log(u8 opp) {}
static inline void prcmu_debug_arm_opp_log(u32 value) {}
static inline void prcmu_debug_dump_data_mem(void) {}
static inline void prcmu_debug_dump_regs(void) {}
static inline void prcmu_debug_register_interrupt(u32 mailbox) {}
static inline void prcmu_debug_register_mbox0_event(u32 ev, u32 mask) {}
#endif
#endif
