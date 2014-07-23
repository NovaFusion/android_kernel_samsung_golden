/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * License Terms: GNU General Public License v2
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>,
 *	   Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/amba/serial.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <mach/pm.h>
#include <mach/pm-timer.h>
#include <mach/gpio.h>

#include <asm/hardware/gic.h>

#include "cpuidle-dbx500.h"
#include "cpuidle-dbx500_dbg.h"

#define APE_ON_TIMER_INTERVAL 20 /* Seconds */

#define UART011_MIS_RTIS (1 << 6) /* receive timeout interrupt status */
#define UART011_MIS_RXIS (1 << 4) /* receive interrupt status */
#define UART011_MIS 0x40 /* Masked interrupt status register */

enum latency_type {
	LATENCY_ENTER = 0,
	LATENCY_EXIT,
	LATENCY_WAKE,
	NUM_LATENCY,
};

struct state_history_state {
	u64 counter;
	ktime_t time;

	u64 hit_rate;
	u64 state_ok;
	u64 state_error;
	u64 prcmu_int;
	u64 pending_int;

	u64 latency_count[NUM_LATENCY];
	ktime_t latency_sum[NUM_LATENCY];
	ktime_t latency_min[NUM_LATENCY];
	ktime_t latency_max[NUM_LATENCY];
};

struct state_history {
	ktime_t start;
	u32 state;
	u64 exit_counter;
	ktime_t measure_begin;
	u64 ape_blocked;
	u64 uart_blocked;
	u64 modem_blocked;
	u64 time_blocked;
	u64 several_blocked;
	u64 gov_blocked;

	ktime_t total_offline;
	ktime_t when_offline;
	bool is_offline;

	bool apidle_several_is_blocking;
	bool apidle_ape_is_blocking;
	bool apidle_modem_is_blocking;
	bool apidle_uart_is_blocking;
	bool apidle_time_is_blocking;
	bool apidle_gov_is_blocking;

	struct state_history_state *states;
};
static DEFINE_PER_CPU(struct state_history, *state_history);
static DEFINE_SPINLOCK(state_lock);

static struct delayed_work cpuidle_work;
static u32 dbg_console_enable = 1;
static void __iomem *uart_base;
static struct clk *uart_clk;

/* Blocks ApSleep and ApDeepSleep */
static bool force_APE_on;
static bool reset_timer;
static int deepest_allowed_state = CONFIG_DBX500_CPUIDLE_DEEPEST_STATE;
static u32 measure_latency;
static bool wake_latency;
static int verbose;

static struct cstate *cstates;
static int cstates_len;
static DEFINE_SPINLOCK(console_lock);

/* This is stored for post-mortem analysis */
#ifdef CONFIG_CRASH_DUMP
#define POST_MORTEM_LEN 16

struct cpuidle_post_mortem_entry {
	enum ci_pwrst pwr_state;
	bool is_last;
	s64 enter;
	s64 woke;
	s64 est_wake;
	s64 est_wake_common;
	enum post_mortem_sleep sleep; /* will be handled as u32 */
};

volatile struct {
	int idx;
	struct cpuidle_post_mortem_entry status[POST_MORTEM_LEN];
} cpuidle_post_mortem[NR_CPUS];
static DEFINE_SPINLOCK(cpuidle_post_mortem_lock);
#endif

bool ux500_ci_dbg_force_ape_on(void)
{
	clk_enable(uart_clk);
	if (readw(uart_base + UART01x_FR) & UART01x_FR_BUSY) {
		clk_disable(uart_clk);
		return true;
	}
	clk_disable(uart_clk);

	return force_APE_on;
}

int ux500_ci_dbg_deepest_state(void)
{
	return deepest_allowed_state;
}

void ux500_ci_dbg_set_deepest_state(int state)
{
	deepest_allowed_state = state;
}

void ux500_ci_dbg_console_handle_ape_suspend(void)
{
	if (!dbg_console_enable)
		return;

	enable_irq_wake(GPIO_TO_IRQ(ux500_console_uart_gpio_pin));
}

void ux500_ci_dbg_console_handle_ape_resume(void)
{
	unsigned int mask = 1 << (ux500_console_uart_gpio_pin % 32);
	int bank = ux500_console_uart_gpio_pin / 32;
	unsigned long flags;
	u32 WKS_reg_value;

	if (!dbg_console_enable)
		return;

	WKS_reg_value = ux500_pm_gpio_read_wake_up_status(bank);

	if (WKS_reg_value & mask) {
		spin_lock_irqsave(&console_lock, flags);
		reset_timer = true;
		spin_unlock_irqrestore(&console_lock, flags);

	}
	disable_irq_wake(GPIO_TO_IRQ(ux500_console_uart_gpio_pin));

}

void ux500_ci_dbg_console_check_uart(void)
{
	unsigned long flags;
	u32 status;

	if (!dbg_console_enable)
		return;

	clk_enable(uart_clk);
	spin_lock_irqsave(&console_lock, flags);
	status = readw(uart_base + UART011_MIS);

	if (status & (UART011_MIS_RTIS | UART011_MIS_RXIS))
		reset_timer = true;

	spin_unlock_irqrestore(&console_lock, flags);
	clk_disable(uart_clk);
}

void ux500_ci_dbg_console(void)
{
	unsigned long flags;

	if (!dbg_console_enable)
		return;

	spin_lock_irqsave(&console_lock, flags);
	if (reset_timer) {
		reset_timer = false;
		spin_unlock_irqrestore(&console_lock, flags);

		cancel_delayed_work(&cpuidle_work);
		force_APE_on = true;
		schedule_delayed_work(&cpuidle_work,
				      msecs_to_jiffies(APE_ON_TIMER_INTERVAL *
						       1000));
	} else {
		spin_unlock_irqrestore(&console_lock, flags);
	}
}
EXPORT_SYMBOL(ux500_ci_dbg_console);

static void dbg_cpuidle_work_function(struct work_struct *work)
{
	force_APE_on = false;
}

void ux500_ci_dbg_plug(int cpu)
{
	struct state_history *sh;
	ktime_t now;
	unsigned long flags;

	now = ktime_get();

	sh = per_cpu(state_history, cpu);
	spin_lock_irqsave(&state_lock, flags);

	sh->total_offline = ktime_add(sh->total_offline,
				      ktime_sub(now, sh->when_offline));
	sh->is_offline = false;

	sh->start = now;
	sh->state = CI_RUNNING;
	sh->states[sh->state].counter++;
	spin_unlock_irqrestore(&state_lock, flags);
}

void ux500_ci_dbg_unplug(int cpu)
{
	struct state_history *sh;
	ktime_t dtime;
	unsigned long flags;

	sh = per_cpu(state_history, cpu);
	spin_lock_irqsave(&state_lock, flags);

	sh->when_offline = ktime_get();
	sh->is_offline = true;

	dtime = ktime_sub(sh->when_offline, sh->start);
	sh->states[sh->state].time = ktime_add(sh->states[sh->state].time,
					       dtime);
	spin_unlock_irqrestore(&state_lock, flags);
}

static void store_latency(struct state_history *sh,
			  int ctarget,
			  enum latency_type type,
			  ktime_t d)
{
	sh->states[ctarget].latency_count[type]++;

	sh->states[ctarget].latency_sum[type] =
		ktime_add(sh->states[ctarget].latency_sum[type], d);

	if (ktime_to_us(d) > ktime_to_us(sh->states[ctarget].latency_max[type]))
		sh->states[ctarget].latency_max[type] = d;

	if (ktime_to_us(d) < ktime_to_us(sh->states[ctarget].latency_min[type]))
		sh->states[ctarget].latency_min[type] = d;
}

void ux500_ci_dbg_exit_latency(int ctarget, ktime_t now, ktime_t exit,
			       ktime_t enter)
{
	struct state_history *sh;
	bool hit = true;
	enum prcmu_power_status prcmu_status;
	unsigned int d;
	bool went_ok = true;
	unsigned long flags;

	if (!measure_latency && !verbose)
		return;

	sh = per_cpu(state_history, smp_processor_id());

	spin_lock_irqsave(&state_lock, flags);

	sh->exit_counter++;

	d = ktime_to_us(ktime_sub(now, enter));

	if ((ctarget + 1) < deepest_allowed_state)
		hit = d	< cstates[ctarget + 1].threshold;
	if (d < cstates[ctarget].threshold)
		hit = false;

	if (cstates[ctarget].state < CI_IDLE)
		goto out;

	prcmu_status = prcmu_get_power_state_result();

	switch (prcmu_status) {

	case PRCMU_DEEP_SLEEP_OK:
		if (cstates[ctarget].state == CI_DEEP_SLEEP)
			sh->states[ctarget].state_ok++;
		break;
	case PRCMU_SLEEP_OK:
		if (cstates[ctarget].state == CI_SLEEP)
			sh->states[ctarget].state_ok++;
		break;
	case PRCMU_IDLE_OK:
		if (cstates[ctarget].state == CI_IDLE)
			sh->states[ctarget].state_ok++;
		break;
	case PRCMU_DEEPIDLE_OK:
		if (cstates[ctarget].state == CI_DEEP_IDLE)
			sh->states[ctarget].state_ok++;
		break;
	case PRCMU_PRCMU2ARMPENDINGIT_ER:
		sh->states[ctarget].prcmu_int++;
		went_ok = false;
		break;
	case PRCMU_ARMPENDINGIT_ER:
		sh->states[ctarget].pending_int++;
		went_ok = false;
		break;
	default:
		pr_err("cpuidle: unknown prcmu exit code: 0x%x state: %d\n",
		       prcmu_status, cstates[ctarget].state);
		sh->states[ctarget].state_error++;
		went_ok = false;
		break;
	}

	if (measure_latency && went_ok)
		store_latency(sh,
			      ctarget,
			      LATENCY_EXIT,
			      ktime_sub(now, exit));
out:
	if (hit && went_ok)
		sh->states[ctarget].hit_rate++;
	spin_unlock_irqrestore(&state_lock, flags);
}

void ux500_ci_dbg_wake_latency(int ctarget, int sleep_time)
{
	struct state_history *sh;
	ktime_t l;
	ktime_t zero_time;
	unsigned long flags;

	if (!wake_latency || cstates[ctarget].state < CI_IDLE)
		return;

	l = zero_time = ktime_set(0, 0);
	sh = per_cpu(state_history, smp_processor_id());

	spin_lock_irqsave(&state_lock, flags);

	if (cstates[ctarget].state >= CI_SLEEP)
		l = u8500_rtc_exit_latency_get();

	if (cstates[ctarget].state == CI_IDLE) {
		ktime_t d = ktime_set(0, sleep_time * 1000);
		ktime_t now = ktime_get();

		d = ktime_add(d, sh->start);
		if (ktime_to_us(now) > ktime_to_us(d))
			l = ktime_sub(now, d);
		else
			l = zero_time;
	}

	if (!ktime_equal(zero_time, l))
		store_latency(sh,
			      ctarget,
			      LATENCY_WAKE,
			      l);
	spin_unlock_irqrestore(&state_lock, flags);
}

static void state_record_time(struct state_history *sh, int ctarget,
			      ktime_t now, ktime_t start, bool latency)
{
	ktime_t dtime;

	dtime = ktime_sub(now, sh->start);
	sh->states[sh->state].time = ktime_add(sh->states[sh->state].time,
					       dtime);

	sh->start = now;
	sh->state = ctarget;

	if (latency && cstates[ctarget].state != CI_RUNNING && measure_latency)
		store_latency(sh,
			      ctarget,
			      LATENCY_ENTER,
			      ktime_sub(now, start));

	sh->states[sh->state].counter++;
}

void ux500_ci_dbg_register_reason(int idx, bool ape, bool modem, bool uart,
				  u32 time, u32 max_depth)
{
	struct state_history *sh;
	unsigned long flags;

	sh = per_cpu(state_history, smp_processor_id());

	spin_lock_irqsave(&state_lock, flags);

	if ((cstates[idx].state == CI_IDLE) && verbose) {
		sh->apidle_ape_is_blocking = ape;
		sh->apidle_uart_is_blocking = uart;
		sh->apidle_modem_is_blocking = modem;

		sh->apidle_time_is_blocking =
			time <= cstates[idx + 1].threshold;
		sh->apidle_several_is_blocking =
			(ape || uart || modem) && sh->apidle_time_is_blocking;
		sh->apidle_gov_is_blocking =
			cstates[max_depth].state == CI_IDLE;
	}
	spin_unlock_irqrestore(&state_lock, flags);
}

#ifdef CONFIG_CRASH_DUMP
void ux500_ci_dbg_wake_time(ktime_t time_wake)
{
	int this_cpu = smp_processor_id();
	int idx;

	spin_lock(&cpuidle_post_mortem_lock);
	idx = cpuidle_post_mortem[this_cpu].idx;

	cpuidle_post_mortem[this_cpu].status[idx].woke =
		ktime_to_us(time_wake);
	spin_unlock(&cpuidle_post_mortem_lock);
}

void ux500_ci_dbg_log_post_mortem(int ctarget,
				  ktime_t enter_time, ktime_t est_wake_common,
				  ktime_t est_wake, int sleep, bool is_last)
{
	int idx;
	int this_cpu;

	this_cpu = smp_processor_id();

	spin_lock(&cpuidle_post_mortem_lock);

	cpuidle_post_mortem[this_cpu].idx++;
	if (cpuidle_post_mortem[this_cpu].idx >= POST_MORTEM_LEN)
		cpuidle_post_mortem[this_cpu].idx = 0;
	idx = cpuidle_post_mortem[this_cpu].idx;

	cpuidle_post_mortem[this_cpu].status[idx].pwr_state = ctarget;
	cpuidle_post_mortem[this_cpu].status[idx].enter =
		ktime_to_us(enter_time);
	cpuidle_post_mortem[this_cpu].status[idx].est_wake =
		ktime_to_us(est_wake);
	cpuidle_post_mortem[this_cpu].status[idx].est_wake_common =
		ktime_to_us(est_wake_common);
	cpuidle_post_mortem[this_cpu].status[idx].is_last = is_last;
	cpuidle_post_mortem[this_cpu].status[idx].woke = 0;
	cpuidle_post_mortem[this_cpu].status[idx].sleep = sleep;

	spin_unlock(&cpuidle_post_mortem_lock);

}
#else
void ux500_ci_dbg_wake_time(ktime_t time_wake)
{
}
void ux500_ci_dbg_log_post_mortem(ktime_t enter_time, ktime_t est_wake_common,
				  ktime_t est_wake, int sleep, bool is_last)
{
}
#endif

void ux500_ci_dbg_log(int ctarget, ktime_t enter_time)
{
	int i;
	ktime_t now;
	unsigned long flags;
	struct state_history *sh;
	struct state_history *sh_other;
	int this_cpu;

	this_cpu = smp_processor_id();

	now = ktime_get();

	sh = per_cpu(state_history, this_cpu);

	spin_lock_irqsave(&state_lock, flags);

	if ((cstates[ctarget].state == CI_IDLE) && verbose) {
		if (sh->apidle_several_is_blocking)
			sh->several_blocked++;
		if (sh->apidle_ape_is_blocking)
			sh->ape_blocked++;
		if (sh->apidle_uart_is_blocking)
			sh->uart_blocked++;
		if (sh->apidle_modem_is_blocking)
			sh->modem_blocked++;
		if (sh->apidle_time_is_blocking)
			sh->time_blocked++;
		if (sh->apidle_gov_is_blocking)
			sh->gov_blocked++;
	}

	/*
	 * Check if current state is just a repeat of
	 *  the state we're already in, then just quit.
	 */
	if (ctarget == sh->state)
		goto done;

	state_record_time(sh, ctarget, now, enter_time, true);

	/*
	 * Update other cpus, (this_cpu = A, other cpus = B) if:
	 * - A = running and B != WFI | running: Set B to WFI
	 * - A = WFI and then B must be running: No changes
	 * - A = !WFI && !RUNNING and then B must be WFI: B sets to A
	 */

	if (sh->state == CI_WFI)
		goto done;

	for_each_possible_cpu(i) {

		if (this_cpu == i)
			continue;

		sh_other = per_cpu(state_history, i);

		/* Same state, continue */
		if (sh_other->state == sh->state)
			continue;

		if ((cstates[ctarget].state == CI_RUNNING) &&
		    (cstates[sh_other->state].state != CI_WFI)) {
			state_record_time(sh_other, CI_WFI, now,
					  enter_time, false);
			continue;
		}
		/*
		 * This cpu is something else than running or wfi, both must be
		 * in the same state.
		 */
		state_record_time(sh_other, ctarget, now, enter_time, true);
	}
done:
	spin_unlock_irqrestore(&state_lock, flags);
}

static void state_history_reset(void)
{
	unsigned long flags;
	unsigned int cpu;
	int i, j;
	struct state_history *sh;

	spin_lock_irqsave(&state_lock, flags);

	for_each_possible_cpu(cpu) {
		sh = per_cpu(state_history, cpu);
		for (i = 0; i < cstates_len; i++) {
			sh->states[i].counter = 0;
			sh->states[i].hit_rate = 0;
			sh->states[i].state_ok = 0;
			sh->states[i].state_error = 0;
			sh->states[i].prcmu_int = 0;
			sh->states[i].pending_int = 0;

			sh->states[i].time = ktime_set(0, 0);

			for (j = 0; j < NUM_LATENCY; j++) {
				sh->states[i].latency_count[j] = 0;
				sh->states[i].latency_min[j] = ktime_set(0,
									 10000000);
				sh->states[i].latency_max[j] = ktime_set(0, 0);
				sh->states[i].latency_sum[j] = ktime_set(0, 0);
			}
		}

		sh->start = ktime_get();
		sh->measure_begin = sh->start;
		/* Don't touch sh->state, since that is where we are now */

		sh->exit_counter = 0;
		sh->ape_blocked = 0;
		sh->uart_blocked = 0;
		sh->modem_blocked = 0;
		sh->time_blocked = 0;
		sh->several_blocked = 0;
		sh->gov_blocked = 0;

		sh->total_offline = ktime_set(0, 0);
		sh->is_offline = !cpu_online(cpu);
		if (sh->is_offline)
			sh->when_offline  = sh->start;
		else
			sh->when_offline  = ktime_set(0, 0);
	}
	spin_unlock_irqrestore(&state_lock, flags);
}

static int get_val(const char __user *user_buf,
		   size_t count, int min, int max)
{
	long unsigned val;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &val);

	if (err)
		return err;

	if (val > max)
		val = max;
	if (val < min)
		val = min;

	return val;
}

static ssize_t set_deepest_state(struct file *file,
				 const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	int val;

	val = get_val(user_buf, count, CI_WFI, cstates_len - 1);

	if (val < 0)
		return val;

	deepest_allowed_state = val;

	pr_debug("cpuidle: changed deepest allowed sleep state to %d.\n",
		 deepest_allowed_state);

	return count;
}

static int deepest_state_print(struct seq_file *s, void *p)
{
	seq_printf(s, "Deepest allowed sleep state is %d\n",
		   deepest_allowed_state);
	return 0;
}

static ssize_t stats_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	state_history_reset();
	return count;
}

static int wake_latency_read(struct seq_file *s, void *p)
{
	seq_printf(s, "wake latency measurements is %s\n",
		   wake_latency ? "on" : "off");
	return 0;
}

static ssize_t wake_latency_write(struct file *file,
				  const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	int val = get_val(user_buf, count, 0, 1);
	if (val < 0)
		return val;

	wake_latency = val;
	ux500_rtcrtt_measure_latency(wake_latency);
	return count;
}

static int verbose_read(struct seq_file *s, void *p)
{
	seq_printf(s, "verbose debug is %s\n", verbose ? "on" : "off");
	return 0;
}

static ssize_t verbose_write(struct file *file,
				  const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	int val = get_val(user_buf, count, 0, 1);
	if (val < 0)
		return val;

	verbose = val;
	state_history_reset();

	return count;
}

static void stat_perc(struct seq_file *s, ktime_t online, int cpu,
		      int i, bool show, char c)
{
	s64 perc;
	s64 total_us;
	s64 rem;
	struct state_history *sh;

	if (!show)
		goto out;

	sh = per_cpu(state_history, cpu);

	total_us = ktime_to_us(online);
	perc = ktime_to_us(sh->states[i].time);
	do_div(total_us, 100);
	if (total_us) {
		rem = do_div(perc, total_us);

		rem = rem * 10;
		do_div(rem, total_us);
		if (rem >= 5)
			perc++;

		seq_printf(s, " %5lld |", perc);

		return;
	}
out:
	seq_printf(s, "     %c |", c);
}

static void stat_ms(struct seq_file *s, ktime_t t, bool invalid, char c)
{
	s64 t_ms;

	t_ms = ktime_to_ms(t);

	if (invalid)
		seq_printf(s, "         %c |", c);
	else
		seq_printf(s, " %9llu |", t_ms);
}

static void show_latencies_row(struct seq_file *s, int cpu, int i)
{
	int j;
	s64 avg[NUM_LATENCY];
	struct state_history *sh;
	ktime_t init_time, zero_time;

	init_time = ktime_set(0, 10000000);
	zero_time = ktime_set(0, 0);

	sh = per_cpu(state_history, cpu);

	seq_printf(s, "%d | %s |  %d  |", i, cstates[i].desc, cpu);

	if (cstates[i].state == CI_RUNNING) {
		seq_printf(s, "        - |        - |        - |        - |");
		seq_printf(s, "        - |        - |        - |        - |");
		seq_printf(s, "        - |        - |        - |        - |");
		seq_printf(s, "\n");
		return;
	}

	for (j = 0; j < NUM_LATENCY; j++) {
		avg[j] = ktime_to_us(sh->states[i].latency_sum[j]);
		if (sh->states[i].latency_count[j])
			do_div(avg[j], sh->states[i].latency_count[j]);
	}

	if (!measure_latency) {
		seq_printf(s, "        M |        M |        M |        M |");
		seq_printf(s, "        M |        M |        M |        M |");
	}

	for (j = 0; j < NUM_LATENCY; j++) {

		if ((j == LATENCY_WAKE) && !wake_latency) {
			seq_printf(s,
				   "        W |        W |        W |        W |");
			continue;
		}

		if (measure_latency || wake_latency) {

			if (ktime_equal(sh->states[i].latency_min[j],
					init_time))
				seq_printf(s, "        0 |");
			else if (ktime_equal(sh->states[i].latency_min[j],
					     zero_time))
				seq_printf(s, "     < 30 |");
			else
				seq_printf(s, " %8lld |",
					   ktime_to_us(sh->states[i].latency_min[j]));

			seq_printf(s, " %8lld | %8lld | %8llu |",
				   ktime_to_us(sh->states[i].latency_max[j]),
				   avg[j],
				   sh->states[i].latency_count[j]);
		}
	}
	seq_printf(s, "\n");
}

static void show_latencies(struct seq_file *s)
{
	int i, cpu;

	seq_printf(s, "\n\nLatencies:");
	seq_printf(s, "\n                                  ");
	seq_printf(s, "|  Enter                                    ");
	seq_printf(s, "|  Exit                                     ");
	seq_printf(s, "|  Wake                                     ");
	seq_printf(s, "\n    Idle states             | CPU ");
	seq_printf(s, "| (min us) | (max us) | (avg us) |      (#) ");
	seq_printf(s, "| (min us) | (max us) | (avg us) |      (#) ");
	seq_printf(s, "| (min us) | (max us) | (avg us) |      (#) ");
	seq_printf(s, "|\n");

	for (i = 0; i < cstates_len; i++)
		for_each_possible_cpu(cpu)
			show_latencies_row(s, cpu, i);
}

static int stats_print(struct seq_file *s, void *p)
{
	int cpu;
	struct state_history *sh;
	s64 total_s;
	int i;
	ktime_t now;
	ktime_t online_time[NR_CPUS];

	now = ktime_get();

	seq_printf(s, "\n                            ");

	for_each_possible_cpu(cpu)
		seq_printf(s, "|        CPU%d        ", cpu);
	seq_printf(s, "|");
	seq_printf(s, "\n  | Online                  ");

	for_each_possible_cpu(cpu) {
		u64 p1, p2;
		sh = per_cpu(state_history, cpu);

		online_time[cpu] = ktime_sub(ktime_sub(now, sh->measure_begin),
					     sh->total_offline);
		if (sh->is_offline)
			online_time[cpu] = ktime_sub(online_time[cpu],
					     ktime_sub(now,
						       sh->when_offline));
		p1 = ktime_to_ms(online_time[cpu]) * 100;
		p2 = ktime_to_ms(ktime_sub(now, sh->measure_begin));
		do_div(p1, p2);

		seq_printf(s, "| %3llu%%, %9llu ms ", p1,
			   ktime_to_ms(online_time[cpu]));
	}

	seq_printf(s, "|");

	seq_printf(s, "\n  | Wake-ups per s          ");
	for_each_possible_cpu(cpu) {
		sh = per_cpu(state_history, cpu);
		total_s = ktime_to_ms(online_time[cpu]);

		do_div(total_s, 1000);
		if (!total_s)
			seq_printf(s, "|       0            ");

		if (verbose) {
			u64 t = sh->exit_counter;
			u64 r = do_div(t, total_s) * 100;
			do_div(r, total_s);

			seq_printf(s, "|  %8llu.%2llu per s ", t, r);
		} else {
			seq_printf(s, "|       V            ");
		}

	}

	seq_printf(s, "|\n\n");
	seq_printf(s, "\n                            ");

	for_each_possible_cpu(cpu)
		seq_printf(s, "|    CPU%d   ", cpu);

	for_each_possible_cpu(cpu)
		seq_printf(s, "|    CPU%d   ", cpu);

	for_each_possible_cpu(cpu)
		seq_printf(s, "|  CPU%d ", cpu);

	seq_printf(s, "|    Both   |    Both   | Both  | Hit |  TO |");

	seq_printf(s, "\n    Idle states             ");

	for_each_possible_cpu(cpu)
		seq_printf(s, "|    (ms)   ");

	for_each_possible_cpu(cpu)
		seq_printf(s, "|     (#)   ");

	for_each_possible_cpu(cpu)
		seq_printf(s, "|   (%%) ");
	seq_printf(s, "|    (ms)   |     (#)   |   (%%) | (%%) | (%%) |\n");

	for (i = 0; i < cstates_len; i++) {
		seq_printf(s, "%d | %s |", i, cstates[i].desc);

		/* cpuX (ms) */
		for_each_possible_cpu(cpu) {
			sh = per_cpu(state_history, cpu);
			stat_ms(s, sh->states[i].time,
				cstates[i].pwrst != PRCMU_AP_NO_CHANGE, '-');
		}

		/* cpuX (#) */

		for_each_possible_cpu(cpu) {
			if (cstates[i].pwrst == PRCMU_AP_NO_CHANGE) {
				sh = per_cpu(state_history, cpu);
				seq_printf(s, " %9llu |",
					   sh->states[i].counter);
			} else {
				seq_printf(s, "         - |");
			}
		}

		/* cpuX (%) */
		for_each_possible_cpu(cpu)
			stat_perc(s, online_time[cpu], cpu, i,
				  cstates[i].pwrst == PRCMU_AP_NO_CHANGE,
				'-');

		sh = per_cpu(state_history, 0);

		/* Both (ms */
		stat_ms(s, sh->states[i].time,
			cstates[i].pwrst == PRCMU_AP_NO_CHANGE, '?');

		/* Both (#) */
		if (cstates[i].pwrst == PRCMU_AP_NO_CHANGE)
			seq_printf(s, "         ? |");
		else
			seq_printf(s, " %9llu |", sh->states[i].counter);

		sh = per_cpu(state_history, 0);
		/* Both (%) */
		stat_perc(s, online_time[0], 0, i,
			  cstates[i].pwrst != PRCMU_AP_NO_CHANGE,
			  '?');

		/* Hit rate */
		if (verbose) {

			if (sh->states[i].counter) {
				u64 t = 100 * sh->states[i].hit_rate;
				do_div(t, sh->states[i].counter);
				seq_printf(s, " %3llu |", t);

			} else {
				seq_printf(s, "   - |");
			}
		} else {
			seq_printf(s, "   V |");
		}

		/* Timed out */
		seq_printf(s, "   ? |");
		seq_printf(s, "\n");
	}

	seq_printf(s, "\n\nIdle/Sleep results from PRCMU:");
	if (verbose) {
		seq_printf(s, "\n                            ");
		seq_printf(s, "|  Number |     INT |     ERR | PENINT  |  PS |");
		seq_printf(s, "\n    Idle states             ");
		seq_printf(s, "|     (#) |     (#) |     (#) |     (#) | (%%) |\n");
	} else {
		seq_printf(s, "\n V\n");
	}

	for (i = 0; i < cstates_len && verbose; i++) {
		u32 t = 0;
		u32 oks = 0;
		u32 ints = 0;
		u32 prcmus = 0;
		u32 errs = 0;
		seq_printf(s, "%d | %s |", i, cstates[i].desc);

		/* Prcmu success rate */
		if ((cstates[i].state == CI_RUNNING) ||
		    (cstates[i].state == CI_WFI)) {
			seq_printf(s,
				   "       - |       - |       - |       - |   - |\n");
			continue;
		}

		for_each_possible_cpu(cpu) {
			sh = per_cpu(state_history, cpu);
			t += sh->states[i].state_ok +
				sh->states[i].prcmu_int +
				sh->states[i].pending_int +
				sh->states[i].state_error;
			oks += sh->states[i].state_ok;
			ints += sh->states[i].prcmu_int;
			prcmus += sh->states[i].pending_int;
			errs +=	sh->states[i].state_error;
		}
		seq_printf(s, " %7d |", t);
		seq_printf(s, " %7d |", ints);
		seq_printf(s, " %7d |", errs);
		seq_printf(s, " %7d |", prcmus);

		if (t)
			seq_printf(s, " %3d |",
				   100 *
				   oks / t);
		else
			seq_printf(s, "     |");

		seq_printf(s, "\n");
	}

	if (wake_latency || measure_latency)
		show_latencies(s);
	else
		seq_printf(s, "Latencies:\n M W\n");

	seq_printf(s, "\nReasons for ApIdle (#):\n");
	if (verbose) {
		u64 r = 0, t = 0, b = 0, g = 0, u = 0, m = 0;
		for_each_possible_cpu(cpu) {
			sh = per_cpu(state_history, cpu);
			r += sh->ape_blocked;
			t += sh->time_blocked;
			b += sh->several_blocked;
			g += sh->gov_blocked;
			u += sh->uart_blocked;
			m += sh->modem_blocked;
		}
		seq_printf(s,
			   "  regulator:%llu time:%llu uart:%llu "
			   "modem:%llu several:%llu "
			   "governor:%llu\n\n",
			   r, t, u, m, b, g);
	} else {
		seq_printf(s, " V\n\n");
	}

	if (!verbose)
		seq_printf(s,
			   "V  = Only visible in verbose mode. "
			   "echo 1  > verbose to enable.\n");
	if (!measure_latency)
		seq_printf(s,
			   "M  = You must enable measure latency. "
			   "echo 1  > measure_latency to enable.\n");

	if (!wake_latency)
		seq_printf(s,
			   "W  = You must enable wake measurements. "
			   "echo 1  > wake_latency to enable.\n");

	seq_printf(s,
		   "Hit = The rate of how often the deepest possible "
		   "state was picked.\n");
	seq_printf(s,
		   "TO = Timed out, the percentage of wake up on "
		   "programmed timeout.\n");
	seq_printf(s,
		   "PENINT = Pending interrupts.\n");
	seq_printf(s,
		   "PS = PRCMU success rate. The percentage of "
		   "Idle/Sleeps that went ok.\n");
	seq_printf(s,
		   "RB = The rate of regulator use is blocking ApSleep\n");
	seq_printf(s, "?  = Measurement not implemented.\n");
	seq_printf(s, "\n");

	if (wake_latency && measure_latency)
		seq_printf(s, "WARNING: wake latency measurement "
			   "increases measured exit latencies!\n");

	seq_printf(s, "\n");
	return 0;
}

static int ap_family_show(struct seq_file *s, void *iter)
{
	int i;
	u32 count = 0;
	unsigned long flags;
	struct state_history *sh;

	sh = per_cpu(state_history, 0);
	spin_lock_irqsave(&state_lock, flags);

	for (i = 0 ; i < cstates_len; i++) {
		if (cstates[i].state == (enum ci_pwrst)s->private)
			count += sh->states[i].counter;
	}
	spin_unlock_irqrestore(&state_lock, flags);

	seq_printf(s, "%u\n", count);
	return 0;
}

static int deepest_state_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, deepest_state_print, inode->i_private);
}

static int verbose_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, verbose_read, inode->i_private);
}

static int stats_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, stats_print, inode->i_private);
}

static int ap_family_open(struct inode *inode,
			  struct file *file)
{
	return single_open(file, ap_family_show, inode->i_private);
}

static int wake_latency_open(struct inode *inode,
			  struct file *file)
{
	return single_open(file, wake_latency_read, inode->i_private);
}

static const struct file_operations deepest_state_fops = {
	.open = deepest_state_open_file,
	.write = set_deepest_state,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations verbose_state_fops = {
	.open = verbose_open_file,
	.write = verbose_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations stats_fops = {
	.open = stats_open_file,
	.write = stats_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ap_family_fops = {
	.open = ap_family_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations wake_latency_fops = {
	.open = wake_latency_open,
	.write = wake_latency_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *cpuidle_dir;

static void __init setup_debugfs(void)
{
	cpuidle_dir = debugfs_create_dir("cpuidle", NULL);
	if (IS_ERR_OR_NULL(cpuidle_dir))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("deepest_state",
					       S_IWUSR | S_IWGRP | S_IRUGO, cpuidle_dir,
					       NULL, &deepest_state_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("verbose",
					       S_IWUSR | S_IWGRP | S_IRUGO, cpuidle_dir,
					       NULL, &verbose_state_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("stats",
					       S_IRUGO, cpuidle_dir, NULL,
					       &stats_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_bool("dbg_console_enable",
					       S_IWUSR | S_IWGRP | S_IRUGO, cpuidle_dir,
					       &dbg_console_enable)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_bool("measure_latency",
					       S_IWUSR | S_IWGRP | S_IRUGO, cpuidle_dir,
					       &measure_latency)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("wake_latency",
					       S_IWUSR | S_IWGRP | S_IRUGO, cpuidle_dir,
					       NULL,
					       &wake_latency_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("ap_idle", S_IRUGO,
					       cpuidle_dir,
					       (void *)CI_IDLE,
					       &ap_family_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("ap_sleep", S_IRUGO,
					       cpuidle_dir,
					       (void *)CI_SLEEP,
					       &ap_family_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("ap_deepidle", S_IRUGO,
					       cpuidle_dir,
					       (void *)CI_DEEP_IDLE,
					       &ap_family_fops)))
		goto fail;

	if (IS_ERR_OR_NULL(debugfs_create_file("ap_deepsleep", S_IRUGO,
					       cpuidle_dir,
					       (void *)CI_DEEP_SLEEP,
					       &ap_family_fops)))
		goto fail;

	return;
fail:
	debugfs_remove_recursive(cpuidle_dir);
}

#define __UART_BASE(soc, x)     soc##_UART##x##_BASE
#define UART_BASE(soc, x)       __UART_BASE(soc, x)

void __init ux500_ci_dbg_init(void)
{
	static const char clkname[] __initconst
		= "uart" __stringify(CONFIG_UX500_DEBUG_UART);
	unsigned long baseaddr;
	int cpu;

	struct state_history *sh;

	cstates = ux500_ci_get_cstates(&cstates_len);

	if (deepest_allowed_state > cstates_len)
		deepest_allowed_state = cstates_len;

	for_each_possible_cpu(cpu) {
		sh = kzalloc(sizeof(struct state_history),
						      GFP_KERNEL);
		sh->states = kzalloc(sizeof(struct state_history_state)
				     * cstates_len,
				     GFP_KERNEL);
		per_cpu(state_history, cpu) = sh;
	}

	state_history_reset();

	for_each_possible_cpu(cpu) {
		sh = per_cpu(state_history, cpu);
		/* Only first CPU used during boot */
		if (cpu == 0)
			sh->state = CI_RUNNING;
		else
			sh->state = CI_WFI;
	}

	setup_debugfs();

	/* Uart debug init */

	if (cpu_is_u8500() || cpu_is_u9540())
		baseaddr = UART_BASE(U8500, CONFIG_UX500_DEBUG_UART);
	else if (cpu_is_u5500())
		baseaddr = UART_BASE(U5500, CONFIG_UX500_DEBUG_UART);
	else
		ux500_unknown_soc();

	uart_base = ioremap(baseaddr, SZ_4K);
	BUG_ON(!uart_base);

	uart_clk = clk_get_sys(clkname, NULL);
	BUG_ON(IS_ERR(uart_clk));

	INIT_DELAYED_WORK_DEFERRABLE(&cpuidle_work, dbg_cpuidle_work_function);
}

void ux500_ci_dbg_remove(void)
{
	int cpu;
	struct state_history *sh;

	debugfs_remove_recursive(cpuidle_dir);

	for_each_possible_cpu(cpu) {
		sh = per_cpu(state_history, cpu);
		kfree(sh->states);
		kfree(sh);
	}

	iounmap(uart_base);
}

MODULE_LICENSE("GPL v2");
