/* arch/arm/mach-omap2/sec_gaf.c
 *
 * Copyright (C) 2011 Samsung Electronics Co, Ltd.
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

#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/pgtable.h>

#include <mach/sec_gaf.h>


void __init sec_gaf_supply_rqinfo(unsigned short curr_offset,
				  unsigned short rq_offset)
{
	unsigned short *checksum = &(GAFINFO.GAFINFOCheckSum);
	unsigned char *memory = (unsigned char *)&GAFINFO;
	unsigned char address;

	/**{{ Add GAForensicINFO-2/2
	 *  Add GAForensic init for preventing symbol removal for optimization.
	 */
	GAFINFO.phys_offset = PHYS_OFFSET;
	GAFINFO.rq_struct_curr = curr_offset;

#ifdef CONFIG_FAIR_GROUP_SCHED
	GAFINFO.cfs_rq_struct_rq_struct = rq_offset;
#else
	GAFINFO.cfs_rq_struct_rq_struct = 0x1224;
#endif

	for (*checksum = 0, address = 0;
	     address < (sizeof(GAFINFO) - sizeof(GAFINFO.GAFINFOCheckSum));
	     address++) {
		if ((*checksum) & 0x8000)
			(*checksum) =
			    (((*checksum) << 1) | 1) ^ memory[address];
		else
			(*checksum) = ((*checksum) << 1) ^ memory[address];
	}
	/**}} Add GAForensicINFO-2/2 */
}

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif
#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

static void __sec_gaf_dump_one_task_info(struct task_struct *tsk, bool isMain)
{
	char stat_array[3] = { 'R', 'S', 'D' };
	char stat_ch;
	char *pThInf = tsk->stack;
	unsigned long wchan;
	unsigned long pc = 0;
	char symname[KSYM_NAME_LEN];
	int permitted;
	struct mm_struct *mm;

	permitted = ptrace_may_access(tsk, PTRACE_MODE_READ);
	mm = get_task_mm(tsk);
	if (mm)
		if (permitted)
			pc = KSTK_EIP(tsk);

	wchan = get_wchan(tsk);

	if (lookup_symbol_name(wchan, symname) < 0) {
		if (!ptrace_may_access(tsk, PTRACE_MODE_READ))
			sprintf(symname, "_____");
		else
			sprintf(symname, "%lu", wchan);
	}

	stat_ch = tsk->state <= TASK_UNINTERRUPTIBLE ?
	    stat_array[tsk->state] : '?';

	pr_info("%8d %8d %8d %16lld %c(%d) %3d  "
		"%08x %08x  %08x %c %16s [%s]\n",
		tsk->pid, (int)(tsk->utime), (int)(tsk->stime),
		tsk->se.exec_start, stat_ch, (int)(tsk->state),
		*(int *)(pThInf + GAFINFO.thread_info_struct_cpu), (int)wchan,
		(int)pc, (int)tsk, isMain ? '*' : ' ', tsk->comm, symname);

	if (tsk->state == TASK_RUNNING ||
	    tsk->state == TASK_UNINTERRUPTIBLE || tsk->mm == NULL)
		show_stack(tsk, NULL);
}

void sec_gaf_dump_all_task_info(void)
{
	struct task_struct *frst_tsk;
	struct task_struct *curr_tsk;
	struct task_struct *frst_thr;
	struct task_struct *curr_thr;

	pr_info("\n");
	pr_info(" current proc : %d %s\n", current->pid, current->comm);
	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");
	pr_info("     pid    uTime    sTime         exec(ns) stat cpu     "
		"wchan  user_pc  ask_struct             comm sym_wchan\n");
	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");

	/* processes */
	frst_tsk = &init_task;
	curr_tsk = frst_tsk;
	while (curr_tsk != NULL) {
		__sec_gaf_dump_one_task_info(curr_tsk, true);
		/* threads */
		if (curr_tsk->thread_group.next != NULL) {
			frst_thr = container_of(curr_tsk->thread_group.next,
						struct task_struct,
						thread_group);
			curr_thr = frst_thr;
			if (frst_thr != curr_tsk) {
				while (curr_thr != NULL) {
					__sec_gaf_dump_one_task_info(curr_thr,
								     false);
					curr_thr = container_of(
						curr_thr->thread_group.
						next, struct task_struct,
						thread_group);
					if (curr_thr == curr_tsk)
						break;
				}
			}
		}
		curr_tsk = container_of(curr_tsk->tasks.next,
				struct task_struct, tasks);
		if (curr_tsk == frst_tsk)
			break;
	}

	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");
}

void sec_gaf_dump_cpu_stat(void)
{
	int i, j;
	unsigned long jif;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest, guest_nice;
	u64 sum = 0;
	u64 sum_softirq = 0;
	unsigned int per_softirq_sums[NR_SOFTIRQS] = { 0 };
	struct timespec boottime;
	unsigned int per_irq_sum;
	char *softirq_to_name[NR_SOFTIRQS] = {
		"HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "BLOCK_IOPOLL",
		"TASKLET", "SCHED", "HRTIMER", "RCU"
	};
	unsigned int softirq_stat;

	user = nice = system = idle = iowait =
		irq = softirq = steal = cputime64_zero;
	guest = guest_nice = cputime64_zero;

	getboottime(&boottime);
	jif = boottime.tv_sec;
	for_each_possible_cpu(i) {
		user = cputime64_add(user, kstat_cpu(i).cpustat.user);
		nice = cputime64_add(nice, kstat_cpu(i).cpustat.nice);
		system = cputime64_add(system, kstat_cpu(i).cpustat.system);
		idle = cputime64_add(idle, kstat_cpu(i).cpustat.idle);
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = cputime64_add(iowait, kstat_cpu(i).cpustat.iowait);
		irq = cputime64_add(irq, kstat_cpu(i).cpustat.irq);
		softirq = cputime64_add(softirq, kstat_cpu(i).cpustat.softirq);

		for_each_irq_nr(j)
			sum += kstat_irqs_cpu(j, i);

		sum += arch_irq_stat_cpu(i);

		for (j = 0; j < NR_SOFTIRQS; j++) {
			softirq_stat = kstat_softirqs_cpu(j, i);
			per_softirq_sums[j] += softirq_stat;
			sum_softirq += softirq_stat;
		}
	}
	sum += arch_irq_stat();
	pr_info("");
	pr_info(" cpu    user:%-8u nice:%-4llu system:%-4llu"
		"idle:%-8llu iowait:%-4llu irq:%-8llu"
		"softirq:%llu %llu %llu %llu\n", i,
		(unsigned long long)cputime64_to_clock_t(user),
		(unsigned long long)cputime64_to_clock_t(nice),
		(unsigned long long)cputime64_to_clock_t(system),
		(unsigned long long)cputime64_to_clock_t(idle),
		(unsigned long long)cputime64_to_clock_t(iowait),
		(unsigned long long)cputime64_to_clock_t(irq),
		(unsigned long long)cputime64_to_clock_t(softirq),
		(unsigned long long)0,	/* cputime64_to_clock_t(steal), */
		(unsigned long long)0,	/* cputime64_to_clock_t(guest), */
		(unsigned long long)0);	/* cputime64_to_clock_t(guest_nice)); */
	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");

	for_each_online_cpu(i) {
		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kstat_cpu(i).cpustat.user;
		nice = kstat_cpu(i).cpustat.nice;
		system = kstat_cpu(i).cpustat.system;
		idle = kstat_cpu(i).cpustat.idle;
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = kstat_cpu(i).cpustat.iowait;
		irq = kstat_cpu(i).cpustat.irq;
		softirq = kstat_cpu(i).cpustat.softirq;
		/* steal = kstat_cpu(i).cpustat.steal; */
		/* guest = kstat_cpu(i).cpustat.guest; */
		/* guest_nice = kstat_cpu(i).cpustat.guest_nice; */
		pr_info(" cpu %2d user:%-8llu nice:%-4llu system:%-4llu"
			"idle:%-8llu iowait:%-4llu irq:%-8llu"
			"softirq:%llu %llu %llu %llu\n", i,
			(unsigned long long)cputime64_to_clock_t(user),
			(unsigned long long)cputime64_to_clock_t(nice),
			(unsigned long long)cputime64_to_clock_t(system),
			(unsigned long long)cputime64_to_clock_t(idle),
			(unsigned long long)cputime64_to_clock_t(iowait),
			(unsigned long long)cputime64_to_clock_t(irq),
			(unsigned long long)cputime64_to_clock_t(softirq),
			(unsigned long long)0,
			(unsigned long long)0,
			(unsigned long long)0);
	}

	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");
	pr_info("\n");
	pr_info(" irq       : %8llu", (unsigned long long)sum);
	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");

	/* sum again ? it could be updated? */
	for_each_irq_nr(j) {
		per_irq_sum = 0;
		for_each_possible_cpu(i)
			per_irq_sum += kstat_irqs_cpu(j, i);

		if (per_irq_sum) {
			pr_info(" irq-%-4d  : %8u %s\n", j, per_irq_sum,
				irq_to_desc(j)->action ?
					(irq_to_desc(j)->action->name ?
						: "???")
					: "???");
		}
	}

	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");
	pr_info("\n");
	pr_info(" softirq   : %8llu", (unsigned long long)sum_softirq);
	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");

	for (i = 0; i < NR_SOFTIRQS; i++)
		if (per_softirq_sums[i])
			pr_info(" softirq-%d : %8u %s\n", i,
				per_softirq_sums[i], softirq_to_name[i]);

	pr_info(" ------------------------------------------------------"
		"-------------------------------------------------------\n");
}
