/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/clockchips.h>
#include <linux/clksrc-db5500-mtimer.h>
#include <linux/boottime.h>

#include <asm/sched_clock.h>

#define MTIMER_PRIMARY_COUNTER		0x18

static void __iomem *db5500_mtimer_base;

#ifdef CONFIG_CLKSRC_DB5500_MTIMER_SCHED_CLOCK
static DEFINE_CLOCK_DATA(cd);

unsigned long long notrace sched_clock(void)
{
	u32 cyc;

	if (unlikely(!db5500_mtimer_base))
		return 0;

	cyc = readl_relaxed(db5500_mtimer_base + MTIMER_PRIMARY_COUNTER);

	return cyc_to_sched_clock(&cd, cyc, (u32)~0);
}

static void notrace db5500_mtimer_update_sched_clock(void)
{
	u32 cyc = readl_relaxed(db5500_mtimer_base + MTIMER_PRIMARY_COUNTER);
	update_sched_clock(&cd, cyc, (u32)~0);
}
#endif

#ifdef CONFIG_BOOTTIME
static unsigned long __init boottime_get_time(void)
{
	return sched_clock();
}

static struct boottime_timer __initdata boottime_timer = {
	.init     = NULL,
	.get_time = boottime_get_time,
	.finalize = NULL,
};
#endif

void __init db5500_mtimer_init(void __iomem *base)
{
	db5500_mtimer_base = base;

	clocksource_mmio_init(base + MTIMER_PRIMARY_COUNTER, "mtimer", 32768,
			      400, 32, clocksource_mmio_readl_up);

#ifdef CONFIG_CLKSRC_DB5500_MTIMER_SCHED_CLOCK
	init_sched_clock(&cd, db5500_mtimer_update_sched_clock,
			 32, 32768);
#endif
	boottime_activate(&boottime_timer);
}
