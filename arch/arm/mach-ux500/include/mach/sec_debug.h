/* arch/arm/mach-omap2/sec_debug.h
 *
 * Copyright (C) 2010-2011 Samsung Electronics Co, Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef SEC_DEBUG_H
#define SEC_DEBUG_H

#include <linux/sched.h>
#include <linux/semaphore.h>
#if defined (CONFIG_MACH_SAMSUNG_U9540)
#define SEC_DEBUG_MAGIC_ADDR		0xE400000
#elif defined (CONFIG_MACH_SAMSUNG_UX500)
#define SEC_DEBUG_MAGIC_ADDR		0x0DF00000 /* magic addres from android/bootloader/loke4/include/configs/kyleatt/kyleatt.h */
#elif defined(CONFIG_MACH_OMAP4_SAMSUNG)
/* for OMAP4 chips - use scratchpad */
#define SEC_DEBUG_MAGIC_ADDR		0x4A326FF0
#define SEC_DEBUG_UPLOAD_CAUSE_ADDR	(SEC_DEBUG_MAGIC_ADDR - 0x04)
#else
/* for LSI chips - use external SRAM base */
#define SEC_DEBUG_MAGCI_ADDR		0xC0000000
#endif /* CONFIG_MACH_* */

#define SEC_DEBUG_MAGIC_DUMP		0x66262564

#ifdef CONFIG_SEC_DEBUG

union sec_debug_level_t {
	struct {
		u16 kernel_fault;
		u16 user_fault;
	} en __aligned(sizeof(u16));
	u32 uint_val;
};

extern union sec_debug_level_t sec_debug_level;

struct sec_crash_key {
	unsigned int *keycode;		/* keycode array */
	unsigned int size;		/* number of used keycode (reserved) */
	unsigned int timeout;		/* msec timeout */
	unsigned int unlock;		/* unlocking mask value */
};

extern void sec_debug_init_crash_key(struct sec_crash_key *crash_key);

extern int sec_debug_get_level(void);
/* Exynos compatiable */
extern int get_sec_debug_level(void);

extern void sec_getlog_supply_loggerinfo(void *p_main, void *p_radio,
	void *p_events, void *p_system);

#else

#define sec_debug_init_crash_key(crash_key)
#define sec_debug_level()	0

static inline void sec_getlog_supply_loggerinfo(void *p_main,
	void *p_radio, void *p_events, void *p_system)
{
}

#endif /* CONFIG_SEC_DEBUG */

#define IRQ_HANDLER_NONE	0
#define IRQ_HANDLER_ENTRY	1
#define IRQ_HANDLER_EXIT	2

#ifdef CONFIG_SEC_DEBUG_SCHED_LOG

struct worker;
struct work_struct;

extern void __sec_debug_task_log(int cpu, struct task_struct *task);

extern void __sec_debug_irq_log(unsigned int irq, void *fn, int en);

extern void __sec_debug_work_log(struct worker *worker,
				 struct work_struct *work, work_func_t f);

static inline void sec_debug_task_log(int cpu, struct task_struct *task)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_task_log(cpu, task);
}

static inline void sec_debug_irq_log(unsigned int irq, void *fn, int en)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_irq_log(irq, fn, en);
}

static inline void sec_debug_work_log(struct worker *worker,
				      struct work_struct *work, work_func_t f)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_work_log(worker, work, f);
}

#ifdef CONFIG_SEC_DEBUG_IRQ_EXIT_LOG
extern void sec_debug_irq_last_exit_log(void);
#endif /* CONFIG_SEC_DEBUG_IRQ_EXIT_LOG */

#else

#define sec_debug_task_log(cpu, task)
#define sec_debug_irq_log(irq, fn, en)
#define sec_debug_work_log(worker, work, f)

#define sec_debug_irq_last_exit_log()

#endif /* CONFIG_SEC_DEBUG_SCHED_LOG */

#ifdef CONFIG_SEC_DEBUG_SEMAPHORE_LOG

extern void debug_semaphore_init(void);

extern void debug_semaphore_down_log(struct semaphore *sem);

extern void debug_semaphore_up_log(struct semaphore *sem);

extern void debug_rwsemaphore_init(void);

extern void debug_rwsemaphore_down_log(struct rw_semaphore *sem, int dir);

extern void debug_rwsemaphore_up_log(struct rw_semaphore *sem);

#else

#define debug_semaphore_init()
#define debug_semaphore_down_log(sem)
#define debug_semaphore_up_log(sem)
#define debug_rwsemaphore_init()
#define debug_rwsemaphore_down_log(sem, dir)
#define debug_rwsemaphore_up_log(sem)

#endif /* CONFIG_SEC_DEBUG_SEMAPHORE_LOG */

#define debug_rwsemaphore_down_read_log(x)	\
	debug_rwsemaphore_down_log(x, READ_SEM)
#define debug_rwsemaphore_down_write_log(x)	\
	debug_rwsemaphore_down_log(x, WRITE_SEM)

/* MISC facilities */
#if defined(CONFIG_ARCH_OMAP)
/* arch/arm/mach-omap2/serial.c */
extern char *cmdline_find_option(char *str);
#endif

int sec_get_debug_enable(void);
int sec_get_debug_enable_user(void);

#endif /* SEC_DEBUG_H */
