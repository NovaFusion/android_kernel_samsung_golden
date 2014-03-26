/*
 *  Copyright (C) 2009 ST-Ericsson
 *  Copyright (C) 2009 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>
#include <linux/mfd/dbx500-prcmu.h>

#include "clock.h"
#include "prcc.h"

DEFINE_MUTEX(clk_opp100_mutex);
static DEFINE_SPINLOCK(clk_spin_lock);
#define NO_LOCK &clk_spin_lock

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES
void __clk_panic_disable(struct clk *clk, void *current_lock)
{
	if (clk == NULL)
		return;

	if (clk->enabled && (--clk->enabled == 0)) {
		if ((clk->ops != NULL) && (clk->ops->disable != NULL))
			clk->ops->disable(clk);
		__clk_panic_disable(clk->parent, clk->mutex);
		__clk_panic_disable(clk->bus_parent, clk->mutex);
	}

	return;
}

int __clk_panic_enable(struct clk *clk, void *current_lock)
{
	int err;

	if (clk == NULL)
		return 0;

	if (!clk->enabled) {
		err = __clk_panic_enable(clk->bus_parent, clk->mutex);
		if (unlikely(err))
			goto bus_parent_error;

		err = __clk_panic_enable(clk->parent, clk->mutex);
		if (unlikely(err))
			goto parent_error;

		if ((clk->ops != NULL) && (clk->ops->enable != NULL)) {
			err = clk->ops->enable(clk);
			if (unlikely(err))
				goto enable_error;
		}
	}
	clk->enabled++;

	return 0;

enable_error:
	__clk_panic_disable(clk->parent, clk->mutex);
parent_error:
	__clk_panic_disable(clk->bus_parent, clk->mutex);
bus_parent_error:


	return err;
}

unsigned long __clk_panic_get_rate(struct clk *clk, void *current_lock)
{
	unsigned long rate;

	if (clk == NULL)
		return 0;

	if ((clk->ops != NULL) && (clk->ops->get_rate != NULL))
		rate = clk->ops->get_rate(clk);
	else if (clk->rate)
		rate = clk->rate;
	else
		rate = __clk_panic_get_rate(clk->parent, clk->mutex);

	return rate;
}


int clk_panic_enable(struct clk *clk)
{
	if (clk == NULL)
		return -EINVAL;

	return __clk_panic_enable(clk, NO_LOCK);
}
EXPORT_SYMBOL(clk_panic_enable);

void clk_panic_disable(struct clk *clk)
{

	if (clk == NULL)
		return;

	WARN_ON(!clk->enabled);
	__clk_panic_disable(clk, NO_LOCK);
}
EXPORT_SYMBOL(clk_panic_disable);

unsigned long clk_panic_get_rate(struct clk *clk)
{
	if (clk == NULL)
		return 0;

	return __clk_panic_get_rate(clk, NO_LOCK);
}
EXPORT_SYMBOL(clk_panic_get_rate);

static int prcmu_panic_clk_enable(struct clk *clk)
{
//	return prcmu_panic_request_clock(clk->cg_sel, true);
}

static void prcmu_panic_clk_disable(struct clk *clk)
{
//	if (prcmu_panic_request_clock(clk->cg_sel, false)) {
//		pr_err("clock: %s failed to disable %s.\n", __func__,
//			clk->name);
//	}
}

void prcmu_reroute_clk_fp(void)
{
	prcmu_clk_ops.enable = prcmu_panic_clk_enable;
	prcmu_clk_ops.disable = prcmu_panic_clk_disable;
}

#endif

static void __clk_lock(struct clk *clk, void *last_lock, unsigned long *flags)
{
	if (clk->mutex != last_lock) {
		if (clk->mutex == NULL)
			spin_lock_irqsave(&clk_spin_lock, *flags);
		else
			mutex_lock(clk->mutex);
	}
}

static void __clk_unlock(struct clk *clk, void *last_lock, unsigned long flags)
{
	if (clk->mutex != last_lock) {
		if (clk->mutex == NULL)
			spin_unlock_irqrestore(&clk_spin_lock, flags);
		else
			mutex_unlock(clk->mutex);
	}
}

void __clk_disable(struct clk *clk, void *current_lock)
{
	unsigned long flags;

	if (clk == NULL)
		return;

	__clk_lock(clk, current_lock, &flags);

	if (clk->enabled && (--clk->enabled == 0)) {
		if ((clk->ops != NULL) && (clk->ops->disable != NULL))
			clk->ops->disable(clk);
		__clk_disable(clk->parent, clk->mutex);
		__clk_disable(clk->bus_parent, clk->mutex);
	}

	__clk_unlock(clk, current_lock, flags);

	return;
}

int __clk_enable(struct clk *clk, void *current_lock)
{
	int err;
	unsigned long flags;

	if (clk == NULL)
		return 0;

	__clk_lock(clk, current_lock, &flags);

	if (!clk->enabled) {
		err = __clk_enable(clk->bus_parent, clk->mutex);
		if (unlikely(err))
			goto bus_parent_error;

		err = __clk_enable(clk->parent, clk->mutex);
		if (unlikely(err))
			goto parent_error;

		if ((clk->ops != NULL) && (clk->ops->enable != NULL)) {
			err = clk->ops->enable(clk);
			if (unlikely(err))
				goto enable_error;
		}
	}
	clk->enabled++;

	__clk_unlock(clk, current_lock, flags);

	return 0;

enable_error:
	__clk_disable(clk->parent, clk->mutex);
parent_error:
	__clk_disable(clk->bus_parent, clk->mutex);
bus_parent_error:

	__clk_unlock(clk, current_lock, flags);

	return err;
}

unsigned long __clk_get_rate(struct clk *clk, void *current_lock)
{
	unsigned long rate;
	unsigned long flags;

	if (clk == NULL)
		return 0;

	__clk_lock(clk, current_lock, &flags);

	if ((clk->ops != NULL) && (clk->ops->get_rate != NULL))
		rate = clk->ops->get_rate(clk);
	else if (clk->rate)
		rate = clk->rate;
	else
		rate = __clk_get_rate(clk->parent, clk->mutex);

	__clk_unlock(clk, current_lock, flags);

	return rate;
}

static long __clk_round_rate(struct clk *clk, unsigned long rate)
{
	if ((clk->ops != NULL) && (clk->ops->round_rate != NULL))
		return clk->ops->round_rate(clk, rate);

	return -ENOSYS;
}

static int __clk_set_rate(struct clk *clk, unsigned long rate)
{
	if ((clk->ops != NULL) && (clk->ops->set_rate != NULL))
		return clk->ops->set_rate(clk, rate);

	return -ENOSYS;
}

int clk_enable(struct clk *clk)
{
	if (clk == NULL)
		return -EINVAL;

	return __clk_enable(clk, NO_LOCK);
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{

	if (clk == NULL)
		return;

	WARN_ON(!clk->enabled);
	__clk_disable(clk, NO_LOCK);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL)
		return 0;

	return __clk_get_rate(clk, NO_LOCK);
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	long rounded_rate;
	unsigned long flags;

	if (clk == NULL)
		return -EINVAL;

	__clk_lock(clk, NO_LOCK, &flags);

	rounded_rate = __clk_round_rate(clk, rate);

	__clk_unlock(clk, NO_LOCK, flags);

	return rounded_rate;
}
EXPORT_SYMBOL(clk_round_rate);

long clk_round_rate_rec(struct clk *clk, unsigned long rate)
{
	long rounded_rate;
	unsigned long flags;

	if ((clk == NULL) || (clk->parent == NULL))
		return -EINVAL;

	__clk_lock(clk->parent, clk->mutex, &flags);

	rounded_rate = __clk_round_rate(clk->parent, rate);

	__clk_unlock(clk->parent, clk->mutex, flags);

	return rounded_rate;
}

static void lock_parent_rate(struct clk *clk)
{
	unsigned long flags;

	if (clk->parent == NULL)
		return;

	__clk_lock(clk->parent, clk->mutex, &flags);

	lock_parent_rate(clk->parent);
	clk->parent->rate_locked++;

	__clk_unlock(clk->parent, clk->mutex, flags);
}

static void unlock_parent_rate(struct clk *clk)
{
	unsigned long flags;

	if (clk->parent == NULL)
		return;

	__clk_lock(clk->parent, clk->mutex, &flags);

	unlock_parent_rate(clk->parent);
	clk->parent->rate_locked--;

	__clk_unlock(clk->parent, clk->mutex, flags);
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int err;
	unsigned long flags;

	if (clk == NULL)
		return -EINVAL;

	__clk_lock(clk, NO_LOCK, &flags);

	if (clk->enabled) {
		err = -EBUSY;
		goto unlock_and_return;
	}
	if (clk->rate_locked) {
		err = -EAGAIN;
		goto unlock_and_return;
	}

	lock_parent_rate(clk);
	err =  __clk_set_rate(clk, rate);
	unlock_parent_rate(clk);

unlock_and_return:
	__clk_unlock(clk, NO_LOCK, flags);

	return err;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_rate_rec(struct clk *clk, unsigned long rate)
{
	int err;
	unsigned long flags;

	if ((clk == NULL) || (clk->parent == NULL))
		return -EINVAL;

	__clk_lock(clk->parent, clk->mutex, &flags);

	if (clk->parent->enabled) {
		err = -EBUSY;
		goto unlock_and_return;
	}
	if (clk->parent->rate_locked != 1) {
		err = -EAGAIN;
		goto unlock_and_return;
	}
	err = __clk_set_rate(clk->parent, rate);

unlock_and_return:
	__clk_unlock(clk->parent, clk->mutex, flags);

	return err;
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int err = 0;
	unsigned long flags;
	struct clk **p;

	if ((clk == NULL) || (clk->parents == NULL))
		return -EINVAL;
	for (p = clk->parents; *p != parent; p++) {
		if (*p == NULL) /* invalid parent */
			return -EINVAL;
	}

	__clk_lock(clk, NO_LOCK, &flags);

	if ((clk->ops != NULL) && (clk->ops->set_parent != NULL)) {
		err = clk->ops->set_parent(clk, parent);
		if (err)
			goto unlock_and_return;
	} else if (clk->enabled) {
		err = __clk_enable(parent, clk->mutex);
		if (err)
			goto unlock_and_return;
		__clk_disable(clk->parent, clk->mutex);
	}

	clk->parent = parent;

unlock_and_return:
	__clk_unlock(clk, NO_LOCK, flags);

	return err;
}

/* PRCMU clock operations. */

static int prcmu_clk_enable(struct clk *clk)
{
	return prcmu_request_clock(clk->cg_sel, true);
}

static void prcmu_clk_disable(struct clk *clk)
{
	if (prcmu_request_clock(clk->cg_sel, false)) {
		pr_err("clock: %s failed to disable %s.\n", __func__,
			clk->name);
	}
}

static int request_ape_opp100(bool enable)
{
	static unsigned int requests;

	if (enable) {
		if (0 == requests++) {
			return prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
							 "clock", 100);
		}
	} else if (1 == requests--) {
		prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "clock");
	}
	return 0;
}

static int prcmu_opp100_clk_enable(struct clk *clk)
{
	int r;

	r = request_ape_opp100(true);
	if (r) {
		pr_err("clock: %s failed to request APE OPP 100%% for %s.\n",
			__func__, clk->name);
		return r;
	}
	return prcmu_request_clock(clk->cg_sel, true);
}

static void prcmu_opp100_clk_disable(struct clk *clk)
{
	if (prcmu_request_clock(clk->cg_sel, false))
		goto out_error;
	if (request_ape_opp100(false))
		goto out_error;
	return;

out_error:
	pr_err("clock: %s failed to disable %s.\n", __func__, clk->name);
}

static unsigned long prcmu_clk_get_rate(struct clk *clk)
{
	return prcmu_clock_rate(clk->cg_sel);
}

static long prcmu_clk_round_rate(struct clk *clk, unsigned long rate)
{
	return prcmu_round_clock_rate(clk->cg_sel, rate);
}

static int prcmu_clk_set_rate(struct clk *clk, unsigned long rate)
{
	return prcmu_set_clock_rate(clk->cg_sel, rate);
}

struct clkops prcmu_clk_ops = {
	.enable = prcmu_clk_enable,
	.disable = prcmu_clk_disable,
	.get_rate = prcmu_clk_get_rate,
};

struct clkops prcmu_scalable_clk_ops = {
	.enable = prcmu_clk_enable,
	.disable = prcmu_clk_disable,
	.get_rate = prcmu_clk_get_rate,
	.round_rate = prcmu_clk_round_rate,
	.set_rate = prcmu_clk_set_rate,
};

struct clkops prcmu_opp100_clk_ops = {
	.enable = prcmu_opp100_clk_enable,
	.disable = prcmu_opp100_clk_disable,
	.get_rate = prcmu_clk_get_rate,
};

/* PRCC clock operations. */

static int prcc_pclk_enable(struct clk *clk)
{
	void __iomem *io_base = __io_address(clk->io_base);

	writel(clk->cg_sel, (io_base + PRCC_PCKEN));
	while (!(readl(io_base + PRCC_PCKSR) & clk->cg_sel))
		cpu_relax();
	return 0;
}

static void prcc_pclk_disable(struct clk *clk)
{
	void __iomem *io_base = __io_address(clk->io_base);

	writel(clk->cg_sel, (io_base + PRCC_PCKDIS));
}

struct clkops prcc_pclk_ops = {
	.enable = prcc_pclk_enable,
	.disable = prcc_pclk_disable,
};

static int prcc_kclk_enable(struct clk *clk)
{
	int err;
	void __iomem *io_base = __io_address(clk->io_base);

	err = __clk_enable(clk->clock, clk->mutex);
	if (err)
		return err;

	writel(clk->cg_sel, (io_base + PRCC_KCKEN));
	while (!(readl(io_base + PRCC_KCKSR) & clk->cg_sel))
		cpu_relax();

	__clk_disable(clk->clock, clk->mutex);

	return 0;
}

static void prcc_kclk_disable(struct clk *clk)
{
	void __iomem *io_base = __io_address(clk->io_base);

	(void)__clk_enable(clk->clock, clk->mutex);
	writel(clk->cg_sel, (io_base + PRCC_KCKDIS));
	__clk_disable(clk->clock, clk->mutex);
}

struct clkops prcc_kclk_ops = {
	.enable = prcc_kclk_enable,
	.disable = prcc_kclk_disable,
};

struct clkops prcc_kclk_rec_ops = {
	.enable = prcc_kclk_enable,
	.disable = prcc_kclk_disable,
	.round_rate = clk_round_rate_rec,
	.set_rate = clk_set_rate_rec,
};

int __init clk_init(void)
{
	if (cpu_is_u8500() || cpu_is_u9540())
		db8500_clk_init();
	else if (cpu_is_u5500())
		db5500_clk_init();
	else {
		pr_err("clock: Unknown DB Asic.\n");
		return -EIO;
	}

	return 0;
}
