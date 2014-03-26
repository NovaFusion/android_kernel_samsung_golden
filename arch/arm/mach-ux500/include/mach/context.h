/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 *         Rickard Andersson <rickard.andersson@stericsson.com> for
 *         ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 *
 */
#ifndef CONTEXT_H
#define CONTEXT_H

#include <linux/notifier.h>

#ifdef CONFIG_DBX500_CONTEXT

/* Defines to be with
 * context_ape_notifier_register
 */
#define CONTEXT_APE_SAVE 0 /* APE save */
#define CONTEXT_APE_RESTORE 1 /* APE restore */

/* Defines to be with
 * context_arm_notifier_register
 */
#define CONTEXT_ARM_CORE_SAVE 0 /* Called for each ARM core */
#define CONTEXT_ARM_CORE_RESTORE 1 /* Called for each ARM core */
#define CONTEXT_ARM_COMMON_SAVE 2 /* Called when ARM common is saved */
#define CONTEXT_ARM_COMMON_RESTORE 3 /* Called when ARM common is restored */

int context_ape_notifier_register(struct notifier_block *nb);
int context_ape_notifier_unregister(struct notifier_block *nb);

int context_arm_notifier_register(struct notifier_block *nb);
int context_arm_notifier_unregister(struct notifier_block *nb);

void context_vape_save(void);
void context_vape_restore(void);

void context_gpio_save(void);
void context_gpio_restore(void);
void context_gpio_restore_mux(void);
void context_gpio_mux_safe_switch(bool begin);

void context_gic_dist_store_ppi_irqs(void);
void context_gic_dist_restore_ppi_irqs(void);
void context_gic_dist_disable_unneeded_irqs(void);

void context_varm_save_common(void);
void context_varm_restore_common(void);

void context_varm_save_core(void);
void context_varm_restore_core(void);

void context_save_cpu_registers(void);
void context_restore_cpu_registers(void);

void context_save_to_sram_and_wfi(bool cleanL2cache);

void context_clean_l1_cache_all(void);
void context_save_arm_registers(u32 **backup_stack);
void context_restore_arm_registers(u32 **backup_stack);

void context_save_cp15_registers(u32 **backup_stack);
void context_restore_cp15_registers(u32 **backup_stack);

void context_save_to_sram_and_wfi_internal(u32 backup_sram_storage,
					   bool cleanL2cache);

/* DB specific functions in either context-db8500 or context-db5500 */
void u8500_context_save_icn(void);
void u8500_context_restore_icn(void);
void u8500_context_init(void);

void u5500_context_save_icn(void);
void u5500_context_restore_icn(void);
void u5500_context_init(void);

void u9540_context_save_icn(void);
void u9540_context_restore_icn(void);
void u9540_context_init(void);
#else

static inline void context_varm_save_core(void) {}
static inline void context_save_cpu_registers(void) {}
static inline void context_save_to_sram_and_wfi(bool cleanL2cache) {}
static inline void context_restore_cpu_registers(void) {}
static inline void context_varm_restore_core(void) {}

#endif

#endif
