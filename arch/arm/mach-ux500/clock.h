/*
 *  Copyright (C) 2010 ST-Ericsson SA
 *  Copyright (C) 2009 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef UX500_CLOCK_H
#define UX500_CLOCK_H

#include <linux/clkdev.h>

/**
 * struct clk
 * @ops:	The hardware specific operations defined for the clock.
 * @name:	The name of the clock.
 * @mutex:	The mutex to lock when operating on the clock. %NULL means that
 *		the common clock spinlock will be used.
 * @enabled:	A reference counter of the enable requests for the clock.
 * @rate_locked: A rate lock counter used by clk_set_rate().
 * @opp100:	A flag saying whether the clock is requested to run at the
 *		OPP 100%% frequency.
 * @rate:	The frequency of the clock. For scalable and scaling clocks,
 *		this is the OPP 100%% frequency.
 * @io_base:	An IO memory base address, meaningful only when considered
 *		together with the defined @ops.
 * @cg_sel:	Clock gate selector, meaningful only when considered together
 *		with the specified @ops.
 * @parent:	The current (or only) parent clock of the clock.
 * @bus_parent:	The (optional) auxiliary bus clock "parent" of the clock.
 * @parents:	A list of the possible parents the clock can have. This should
 *		be a %NULL-terminated &struct_clk array. Present if and only
 *		if clk_set_parent() is implemented for the clock.
 * @regulator:	The regulator needed to have the clock functional, if any.
 * @clock:	The clock needed to control the clock, if any.
 */
struct clk {
	const struct clkops *ops;
	const char *name;
	struct mutex *mutex;
	unsigned int enabled;
	unsigned int rate_locked;
	bool opp100;
	unsigned long rate;
	unsigned int io_base;
	u32 cg_sel;
	struct clk *parent;
	struct clk *bus_parent;
	struct clk **parents;
	struct regulator *regulator;
	struct clk *clock;
	struct list_head list;
};

/**
 * struct clkops - ux500 clock operations
 * @enable:	function to enable the clock
 * @disable:	function to disable the clock
 * @get_rate:	function to get the current clock rate
 *
 * This structure contains function pointers to functions that will be used to
 * control the clock.  All of these functions are optional.  If get_rate is
 * NULL, the rate in the struct clk will be used.
 */
struct clkops {
	int (*enable)(struct clk *);
	void (*disable)(struct clk *);
	unsigned long (*get_rate)(struct clk *);
	int (*set_rate)(struct clk *, unsigned long);
	long (*round_rate)(struct clk *, unsigned long);
	int (*set_parent)(struct clk *, struct clk *);
};

extern struct clkops prcmu_clk_ops;
extern struct clkops prcmu_scalable_clk_ops;
extern struct clkops prcmu_opp100_clk_ops;
extern struct mutex clk_opp100_mutex;
extern struct clkops prcc_pclk_ops;
extern struct clkops prcc_kclk_ops;
extern struct clkops prcc_kclk_rec_ops;
extern struct clkops sga_clk_ops;

#define CLK_LOOKUP(_clk, _dev_id, _con_id) \
	{ .dev_id = _dev_id, .con_id = _con_id, .clk = &_clk }

/* Define PRCMU Clock */
#define DEF_PRCMU_CLK(_name, _cg_sel, _rate) \
	struct clk _name = { \
		.name = #_name, \
		.ops = &prcmu_clk_ops, \
		.cg_sel = _cg_sel, \
		.rate = _rate, \
	}

#define DEF_PRCMU_SCALABLE_CLK(_name, _cg_sel) \
	struct clk _name = { \
		.name = #_name, \
		.ops = &prcmu_scalable_clk_ops, \
		.cg_sel = _cg_sel, \
	}

/* Use this for clocks that are only defined at OPP 100%. */
#define DEF_PRCMU_OPP100_CLK(_name, _cg_sel, _rate) \
	struct clk _name = { \
		.name = #_name, \
		.ops = &prcmu_opp100_clk_ops, \
		.cg_sel = _cg_sel, \
		.rate = _rate, \
		.mutex = &clk_opp100_mutex, \
	}

/* Define PRCC clock */
#define DEF_PRCC_PCLK(_name, _io_base, _cg_bit, _parent) \
	struct clk _name = { \
		.name = #_name, \
		.ops = &prcc_pclk_ops, \
		.io_base = _io_base, \
		.cg_sel = BIT(_cg_bit), \
		.parent = _parent, \
	}

#define DEF_PRCC_KCLK(_name, _io_base, _cg_bit, _parent, _clock) \
	struct clk _name = { \
		.name = #_name, \
		.ops = &prcc_kclk_ops, \
		.io_base = _io_base, \
		.cg_sel = BIT(_cg_bit), \
		.parent = _parent, \
		.clock = _clock, \
	}

#define DEF_PER_CLK(_name, _bus_parent, _parent) \
	struct clk _name = { \
		.name = #_name, \
		.parent = _parent, \
		.bus_parent = _bus_parent, \
	}

#define DEF_MTU_CLK(_cg_sel, _name, _bus_parent) \
	struct clk _name = { \
		.name = #_name, \
		.ops = &mtu_clk_ops, \
		.cg_sel = _cg_sel, \
		.bus_parent = _bus_parent, \
	}

/* Functions defined in clock.c */
int __init clk_init(void);
void clks_register(struct clk_lookup *clks, size_t num);
int __clk_enable(struct clk *clk, void *current_lock);
void __clk_disable(struct clk *clk, void *current_lock);
unsigned long __clk_get_rate(struct clk *clk, void *current_lock);
long clk_round_rate_rec(struct clk *clk, unsigned long rate);
int clk_set_rate_rec(struct clk *clk, unsigned long rate);

#ifdef CONFIG_DEBUG_FS
int dbx500_clk_debug_init(struct clk **clks, int num);
#else
static inline int dbx500_clk_debug_init(struct clk **clks, int num)
{
	return 0;
}
#endif

#ifdef CONFIG_UX500_SOC_DB8500
int __init db8500_clk_init(void);
int __init db8500_clk_debug_init(void);
#else
static inline int db8500_clk_init(void) { return 0; }
static inline int db8500_clk_debug_init(void) { return 0; }
#endif

#ifdef CONFIG_UX500_SOC_DB5500
int __init db5500_clk_init(void);
int __init db5500_clk_debug_init(void);
#else
static inline int db5500_clk_init(void) { return 0; }
static inline int db5500_clk_debug_init(void) { return 0; }
#endif

#endif
