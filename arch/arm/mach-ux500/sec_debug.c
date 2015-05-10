/* arch/arm/mach-omap2/sec_debug.c
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

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/keyboard.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/nmi.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/thread_notify.h>
#include <asm/stacktrace.h>
#include <asm/mach/time.h>


#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_ARCH_S5PV310)
#include <mach/system.h>
#endif

#include <linux/mfd/dbx500-prcmu.h>
#include <mach/reboot_reasons.h>


#include <mach/sec_param.h>
#include <mach/sec_debug.h>
#include <mach/sec_gaf.h>
#include <asm/io.h>


#if defined(CONFIG_MACH_SAMSUNG_UX500)
void ux500_clean_l2_cache_all(void);

/* Forced Upload Mode */
extern int jack_is_detected;
#endif

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
#define outer_flush_all()
#endif
enum sec_debug_upload_cause_t {
	UPLOAD_CAUSE_INIT = 0xCAFEBABE,
	UPLOAD_CAUSE_KERNEL_PANIC = 0x000000C8,
	UPLOAD_CAUSE_FORCED_UPLOAD = 0x00000022,
	UPLOAD_CAUSE_CP_ERROR_FATAL = 0x000000CC,
	UPLOAD_CAUSE_USER_FAULT = 0x0000002F,
	UPLOAD_CAUSE_HSIC_DISCONNECTED = 0x000000DD,
};

struct sec_debug_mmu_reg_t {
	int SCTLR;
	int TTBR0;
	int TTBR1;
	int TTBCR;
	int DACR;
	int DFSR;
	int DFAR;
	int IFSR;
	int IFAR;
	int DAFSR;
	int IAFSR;
	int PMRRR;
	int NMRRR;
	int FCSEPID;
	int CONTEXT;
	int URWTPID;
	int UROTPID;
	int POTPIDR;
};

/* ARM CORE regs mapping structure */
struct sec_debug_core_t {
	/* COMMON */
	unsigned int r0;
	unsigned int r1;
	unsigned int r2;
	unsigned int r3;
	unsigned int r4;
	unsigned int r5;
	unsigned int r6;
	unsigned int r7;
	unsigned int r8;
	unsigned int r9;
	unsigned int r10;
	unsigned int r11;
	unsigned int r12;

	/* SVC */
	unsigned int r13_svc;
	unsigned int r14_svc;
	unsigned int spsr_svc;

	/* PC & CPSR */
	unsigned int pc;
	unsigned int cpsr;

	/* USR/SYS */
	unsigned int r13_usr;
	unsigned int r14_usr;

	/* FIQ */
	unsigned int r8_fiq;
	unsigned int r9_fiq;
	unsigned int r10_fiq;
	unsigned int r11_fiq;
	unsigned int r12_fiq;
	unsigned int r13_fiq;
	unsigned int r14_fiq;
	unsigned int spsr_fiq;

	/* IRQ */
	unsigned int r13_irq;
	unsigned int r14_irq;
	unsigned int spsr_irq;

	/* MON */
	unsigned int r13_mon;
	unsigned int r14_mon;
	unsigned int spsr_mon;

	/* ABT */
	unsigned int r13_abt;
	unsigned int r14_abt;
	unsigned int spsr_abt;

	/* UNDEF */
	unsigned int r13_und;
	unsigned int r14_und;
	unsigned int spsr_und;

};

/* enable/disable sec_debug feature
 * level = 0 when enable = 0 && enable_user = 0
 * level = 1 when enable = 1 && enable_user = 0
 * level = 0x10001 when enable = 1 && enable_user = 1
 * The other cases are not considered
 */
union sec_debug_level_t sec_debug_level = { .en.kernel_fault = 1, };

module_param_named(enable, sec_debug_level.en.kernel_fault, ushort, 0644);
module_param_named(enable_user, sec_debug_level.en.user_fault, ushort, 0644);
module_param_named(level, sec_debug_level.uint_val, uint, 0644);

static int __init sec_debug_parse_enable(char *str)
{
	if (kstrtou16(str, 0, &sec_debug_level.en.kernel_fault))
		sec_debug_level.en.kernel_fault = 1;
	return 0;
}
early_param("sec_debug.enable", sec_debug_parse_enable);

static int __init sec_debug_parse_enable_user(char *str)
{
	if (kstrtou16(str, 0, &sec_debug_level.en.user_fault))
		sec_debug_level.en.user_fault = 0;
	return 0;
}
early_param("sec_debug.enable_user", sec_debug_parse_enable_user);

static int __init sec_debug_parse_level(char *str)
{
	if (kstrtou32(str, 0, &sec_debug_level.uint_val))
		sec_debug_level.uint_val = 1;
	return 0;
}
early_param("sec_debug.level", sec_debug_parse_level);
int sec_get_debug_enable(void)
{
	return sec_debug_level.en.kernel_fault;
}
EXPORT_SYMBOL(sec_get_debug_enable);

int sec_get_debug_enable_user(void)
{
	return sec_debug_level.en.user_fault;
}
EXPORT_SYMBOL(sec_get_debug_enable_user);

static const char * const gkernel_sec_build_info_date_time[] = {
	__DATE__,
	__TIME__
};

static char gkernel_sec_build_info[100];

/* klaatu - schedule log */
#ifdef CONFIG_SEC_DEBUG_SCHED_LOG
#define SCHED_LOG_MAX		4096

struct sched_log {
	unsigned long long time;
	union task_log {
		struct task_info {
			char comm[TASK_COMM_LEN];
			pid_t pid;
		} task;
		struct irq_log {
			int irq;
			void *fn;
			int en;
		} irq;
		struct work_log {
			struct worker *worker;
			struct work_struct *work;
			work_func_t f;
		} work;
	} log;
};

static struct sched_log excp_task_log[NR_CPUS][SCHED_LOG_MAX]
	__cacheline_aligned;
static atomic_t excp_task_log_idx[NR_CPUS] = {
	ATOMIC_INIT(-1), ATOMIC_INIT(-1) };
static unsigned long long excp_irq_exit_time[NR_CPUS];
static struct sched_log (*excp_task_log_ptr)[NR_CPUS][SCHED_LOG_MAX]
	= (&excp_task_log);

static int checksum_sched_log(void)
{
	int sum = 0, i;

	for (i = 0; i < sizeof(excp_task_log); i++)
		sum += *((char *)excp_task_log + i);

	return sum;
}
#else
static int checksum_sched_log(void)
{
	return 0;
}
#endif /* CONFIG_SEC_DEBUG_SCHED_LOG */

/* klaatu - semaphore log */
#ifdef CONFIG_SEC_DEBUG_SEMAPHORE_LOG
#define SEMAPHORE_LOG_MAX		100

struct sem_debug {
	struct list_head list;
	struct semaphore *sem;
	struct task_struct *task;
	pid_t pid;
	int cpu;
	/* char comm[TASK_COMM_LEN]; */
};

enum {
	READ_SEM,
	WRITE_SEM,
};

struct sem_debug sem_debug_free_head;
struct sem_debug sem_debug_done_head;
int sem_debug_free_head_cnt;
int sem_debug_done_head_cnt;
int sem_debug_init;
spinlock_t sem_debug_lock;

/* rwsemaphore logging */
#define RWSEMAPHORE_LOG_MAX		100

struct rwsem_debug {
	struct list_head list;
	struct rw_semaphore *sem;
	struct task_struct *task;
	pid_t pid;
	int cpu;
	int direction;
	/* char comm[TASK_COMM_LEN]; */
};

struct rwsem_debug rwsem_debug_free_head;
struct rwsem_debug rwsem_debug_done_head;
int rwsem_debug_free_head_cnt;
int rwsem_debug_done_head_cnt;
int rwsem_debug_init;
spinlock_t rwsem_debug_lock;

#endif /* CONFIG_SEC_DEBUG_SEMAPHORE_LOG */

int sec_debug_get_level(void)
{
	return sec_debug_level.uint_val;
}

/* Exynos compatibility */
int get_sec_debug_level(void)
	__attribute__ ((weak, alias("sec_debug_get_level")));

/* klaatu - schedule log */
#ifdef CONFIG_SEC_DEBUG_SCHED_LOG
void __sec_debug_task_log(int cpu, struct task_struct *task)
{
	unsigned i;

	if (!sec_debug_level.en.kernel_fault)
		return;

	i = atomic_inc_return(&excp_task_log_idx[cpu]) & (SCHED_LOG_MAX - 1);

	(*excp_task_log_ptr)[cpu][i].time = cpu_clock(cpu);
	strcpy((*excp_task_log_ptr)[cpu][i].log.task.comm, task->comm);
	(*excp_task_log_ptr)[cpu][i].log.task.pid = task->pid;
}

void __sec_debug_irq_log(unsigned int irq, void *fn, int en)
{
	int cpu = raw_smp_processor_id();
	unsigned i;

	if (!sec_debug_level.en.kernel_fault)
		return;

	i = atomic_inc_return(&excp_task_log_idx[cpu]) & (SCHED_LOG_MAX - 1);

	(*excp_task_log_ptr)[cpu][i].time = cpu_clock(cpu);
	(*excp_task_log_ptr)[cpu][i].log.irq.irq = irq;
	(*excp_task_log_ptr)[cpu][i].log.irq.fn = (void *)fn;
	(*excp_task_log_ptr)[cpu][i].log.irq.en = en;
}

void __sec_debug_work_log(struct worker *worker,
			  struct work_struct *work, work_func_t f)
{
	int cpu = raw_smp_processor_id();
	unsigned i;

	if (!sec_debug_level.en.kernel_fault)
		return;

	i = atomic_inc_return(&excp_task_log_idx[cpu]) & (SCHED_LOG_MAX - 1);

	(*excp_task_log_ptr)[cpu][i].time = cpu_clock(cpu);
	(*excp_task_log_ptr)[cpu][i].log.work.worker = worker;
	(*excp_task_log_ptr)[cpu][i].log.work.work = work;
	(*excp_task_log_ptr)[cpu][i].log.work.f = f;
}
#ifdef CONFIG_SEC_DEBUG_IRQ_EXIT_LOG
void sec_debug_irq_last_exit_log(void)
{
	int cpu = raw_smp_processor_id();
	excp_irq_exit_time[cpu] = cpu_clock(cpu);
}
#endif /* CONFIG_SEC_DEBUG_IRQ_EXIT_LOG */
#endif /* CONFIG_SEC_DEBUG_SCHED_LOG */

/* klaatu - semaphore log */
#ifdef CONFIG_SEC_DEBUG_SEMAPHORE_LOG
void debug_semaphore_init(void)
{
	int i = 0;
	struct sem_debug *sem_debug = NULL;

	spin_lock_init(&sem_debug_lock);
	sem_debug_free_head_cnt = 0;
	sem_debug_done_head_cnt = 0;

	/* initialize list head of sem_debug */
	INIT_LIST_HEAD(&sem_debug_free_head.list);
	INIT_LIST_HEAD(&sem_debug_done_head.list);

	for (i = 0; i < SEMAPHORE_LOG_MAX; i++) {
		/* malloc semaphore */
		sem_debug = kmalloc(sizeof(struct sem_debug), GFP_KERNEL);
		/* add list */
		list_add(&sem_debug->list, &sem_debug_free_head.list);
		sem_debug_free_head_cnt++;
	}

	sem_debug_init = 1;
}

void debug_semaphore_down_log(struct semaphore *sem)
{
	struct list_head *tmp;
	struct sem_debug *sem_dbg;
	unsigned long flags;

	if (!sem_debug_init)
		return;

	spin_lock_irqsave(&sem_debug_lock, flags);
	list_for_each(tmp, &sem_debug_free_head.list) {
		sem_dbg = list_entry(tmp, struct sem_debug, list);
		sem_dbg->task = current;
		sem_dbg->sem = sem;
		/* strcpy(sem_dbg->comm,current->group_leader->comm); */
		sem_dbg->pid = current->pid;
		sem_dbg->cpu = smp_processor_id();
		list_del(&sem_dbg->list);
		list_add(&sem_dbg->list, &sem_debug_done_head.list);
		sem_debug_free_head_cnt--;
		sem_debug_done_head_cnt++;
		break;
	}
	spin_unlock_irqrestore(&sem_debug_lock, flags);
}

void debug_semaphore_up_log(struct semaphore *sem)
{
	struct list_head *tmp;
	struct sem_debug *sem_dbg;
	unsigned long flags;

	if (!sem_debug_init)
		return;

	spin_lock_irqsave(&sem_debug_lock, flags);
	list_for_each(tmp, &sem_debug_done_head.list) {
		sem_dbg = list_entry(tmp, struct sem_debug, list);
		if (sem_dbg->sem == sem && sem_dbg->pid == current->pid) {
			list_del(&sem_dbg->list);
			list_add(&sem_dbg->list, &sem_debug_free_head.list);
			sem_debug_free_head_cnt++;
			sem_debug_done_head_cnt--;
			break;
		}
	}
	spin_unlock_irqrestore(&sem_debug_lock, flags);
}

/* rwsemaphore logging */
void debug_rwsemaphore_init(void)
{
	int i = 0;
	struct rwsem_debug *rwsem_debug = NULL;

	spin_lock_init(&rwsem_debug_lock);
	rwsem_debug_free_head_cnt = 0;
	rwsem_debug_done_head_cnt = 0;

	/* initialize list head of sem_debug */
	INIT_LIST_HEAD(&rwsem_debug_free_head.list);
	INIT_LIST_HEAD(&rwsem_debug_done_head.list);

	for (i = 0; i < RWSEMAPHORE_LOG_MAX; i++) {
		/* malloc semaphore */
		rwsem_debug = kmalloc(sizeof(struct rwsem_debug), GFP_KERNEL);
		/* add list */
		list_add(&rwsem_debug->list, &rwsem_debug_free_head.list);
		rwsem_debug_free_head_cnt++;
	}

	rwsem_debug_init = 1;
}

void debug_rwsemaphore_down_log(struct rw_semaphore *sem, int dir)
{
	struct list_head *tmp;
	struct rwsem_debug *sem_dbg;
	unsigned long flags;

	if (!rwsem_debug_init)
		return;

	spin_lock_irqsave(&rwsem_debug_lock, flags);
	list_for_each(tmp, &rwsem_debug_free_head.list) {
		sem_dbg = list_entry(tmp, struct rwsem_debug, list);
		sem_dbg->task = current;
		sem_dbg->sem = sem;
		/* strcpy(sem_dbg->comm,current->group_leader->comm); */
		sem_dbg->pid = current->pid;
		sem_dbg->cpu = smp_processor_id();
		sem_dbg->direction = dir;
		list_del(&sem_dbg->list);
		list_add(&sem_dbg->list, &rwsem_debug_done_head.list);
		rwsem_debug_free_head_cnt--;
		rwsem_debug_done_head_cnt++;
		break;
	}
	spin_unlock_irqrestore(&rwsem_debug_lock, flags);
}

void debug_rwsemaphore_up_log(struct rw_semaphore *sem)
{
	struct list_head *tmp;
	struct rwsem_debug *sem_dbg;
	unsigned long flags;

	if (!rwsem_debug_init)
		return;

	spin_lock_irqsave(&rwsem_debug_lock, flags);
	list_for_each(tmp, &rwsem_debug_done_head.list) {
		sem_dbg = list_entry(tmp, struct rwsem_debug, list);
		if (sem_dbg->sem == sem && sem_dbg->pid == current->pid) {
			list_del(&sem_dbg->list);
			list_add(&sem_dbg->list, &rwsem_debug_free_head.list);
			rwsem_debug_free_head_cnt++;
			rwsem_debug_done_head_cnt--;
			break;
		}
	}
	spin_unlock_irqrestore(&rwsem_debug_lock, flags);
}
#endif /* CONFIG_SEC_DEBUG_SEMAPHORE_LOG */

#ifdef CONFIG_SEC_DEBUG_USER
void sec_user_fault_dump(void)
{
	if (sec_debug_level.en.kernel_fault == 1 &&
	    sec_debug_level.en.user_fault == 1)
		panic("User Fault");
}

static int sec_user_fault_write(struct file *file, const char __user * buffer,
				size_t count, loff_t *offs)
{
	char buf[100];

	if (count > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	buf[count] = '\0';

	if (strncmp(buf, "dump_user_fault", 15) == 0)
		sec_user_fault_dump();

	return count;
}

static const struct file_operations sec_user_fault_proc_fops = {
	.write = sec_user_fault_write,
};

static int __init sec_debug_user_fault_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("user_fault", S_IWUGO, NULL,
			    &sec_user_fault_proc_fops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

device_initcall(sec_debug_user_fault_init);
#endif /* CONFIG_SEC_DEBUG_USER */
