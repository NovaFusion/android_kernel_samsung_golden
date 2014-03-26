/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>,
 *         Rickard Andersson <rickard.andersson@stericsson.com>,
 *         Jonas Aaberg <jonas.aberg@stericsson.com>,
 *         Sundar Iyer for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 *
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/gpio-nomadik.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/pm.h>
#include <mach/context.h>

#include <asm/hardware/gic.h>
#include <asm/smp_twd.h>

#include "scu.h"
#include "../product.h"
#include "../prcc.h"

#define GPIO_NUM_BANKS 9
#define GPIO_NUM_SAVE_REGISTERS 7

/*
 * TODO:
 * - Use the "UX500*"-macros instead where possible
 */

#define U8500_BACKUPRAM_SIZE SZ_64K

#define U8500_PUBLIC_BOOT_ROM_BASE (U8500_BOOT_ROM_BASE + 0x17000)
#define U9540_PUBLIC_BOOT_ROM_BASE (U9540_BOOT_ROM_BASE + 0x17000)
#define U5500_PUBLIC_BOOT_ROM_BASE (U5500_BOOT_ROM_BASE + 0x18000)

/*
 * Special dedicated addresses in backup RAM.  The 5500 addresses are identical
 * to the 8500 ones.
 */
#define U8500_EXT_RAM_LOC_BACKUPRAM_ADDR		0x80151FDC
#define U8500_CPU0_CP15_CR_BACKUPRAM_ADDR		0x80151F80
#define U8500_CPU1_CP15_CR_BACKUPRAM_ADDR		0x80151FA0

#define U8500_CPU0_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR	0x80151FD8
#define U8500_CPU1_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR	0x80151FE0

#define GIC_DIST_ENABLE_NS 0x0
#define GIC_DIST_ENABLE_PPI_MASK 0xF0000000

/* 32 interrupts fits in 4 bytes */
#define GIC_DIST_ENABLE_SET_COMMON_NUM ((DBX500_NR_INTERNAL_IRQS - \
					 IRQ_SHPI_START) / 32)
#define GIC_DIST_ENABLE_SET_CPU_NUM (IRQ_SHPI_START / 32)
#define GIC_DIST_ENABLE_SET_SPI0 GIC_DIST_ENABLE_SET
#define GIC_DIST_ENABLE_SET_SPI32 (GIC_DIST_ENABLE_SET + IRQ_SHPI_START / 8)

#define GIC_DIST_ENABLE_CLEAR_0 GIC_DIST_ENABLE_CLEAR
#define GIC_DIST_ENABLE_CLEAR_32 (GIC_DIST_ENABLE_CLEAR + 4)
#define GIC_DIST_ENABLE_CLEAR_64 (GIC_DIST_ENABLE_CLEAR + 8)
#define GIC_DIST_ENABLE_CLEAR_96 (GIC_DIST_ENABLE_CLEAR + 12)
#define GIC_DIST_ENABLE_CLEAR_128 (GIC_DIST_ENABLE_CLEAR + 16)

#define GIC_DIST_PRI_COMMON_NUM ((DBX500_NR_INTERNAL_IRQS - IRQ_SHPI_START) / 4)
#define GIC_DIST_PRI_CPU_NUM (IRQ_SHPI_START / 4)
#define GIC_DIST_PRI_SPI0 GIC_DIST_PRI
#define GIC_DIST_PRI_SPI32 (GIC_DIST_PRI + IRQ_SHPI_START)

#define GIC_DIST_SPI_TARGET_COMMON_NUM ((DBX500_NR_INTERNAL_IRQS - \
					 IRQ_SHPI_START) / 4)
#define GIC_DIST_SPI_TARGET_CPU_NUM (IRQ_SHPI_START / 4)
#define GIC_DIST_SPI_TARGET_SPI0 GIC_DIST_TARGET
#define GIC_DIST_SPI_TARGET_SPI32 (GIC_DIST_TARGET + IRQ_SHPI_START)

/* 16 interrupts per 4 bytes */
#define GIC_DIST_CONFIG_COMMON_NUM ((DBX500_NR_INTERNAL_IRQS - IRQ_SHPI_START) \
				    / 16)
#define GIC_DIST_CONFIG_CPU_NUM (IRQ_SHPI_START / 16)
#define GIC_DIST_CONFIG_SPI0 GIC_DIST_CONFIG
#define GIC_DIST_CONFIG_SPI32 (GIC_DIST_CONFIG + IRQ_SHPI_START / 4)

/* TODO! Move STM reg offsets to suitable place */
#define STM_CR_OFFSET	0x00
#define STM_MMC_OFFSET	0x08
#define STM_TER_OFFSET	0x10

#define TPIU_PORT_SIZE 0x4
#define TPIU_TRIGGER_COUNTER 0x104
#define TPIU_TRIGGER_MULTIPLIER 0x108
#define TPIU_CURRENT_TEST_PATTERN 0x204
#define TPIU_TEST_PATTERN_REPEAT 0x208
#define TPIU_FORMATTER 0x304
#define TPIU_FORMATTER_SYNC 0x308
#define TPIU_LOCK_ACCESS_REGISTER 0xFB0

#define TPIU_UNLOCK_CODE 0xc5acce55

#define SCU_FILTER_STARTADDR	0x40
#define SCU_FILTER_ENDADDR	0x44
#define SCU_ACCESS_CTRL_SAC	0x50

/* The context of the Trace Port Interface Unit (TPIU) */
static struct {
	void __iomem *base;
	u32 port_size;
	u32 trigger_counter;
	u32 trigger_multiplier;
	u32 current_test_pattern;
	u32 test_pattern_repeat;
	u32 formatter;
	u32 formatter_sync;
} context_tpiu;

static struct {
	void __iomem *base;
	u32 cr;
	u32 mmc;
	u32 ter;
} context_stm_ape;

struct context_gic_cpu {
	void __iomem *base;
	u32 ctrl;
	u32 primask;
	u32 binpoint;
};
static DEFINE_PER_CPU(struct context_gic_cpu, context_gic_cpu);

static struct {
	void __iomem *base;
	u32 ns;
	u32 enable_set[GIC_DIST_ENABLE_SET_COMMON_NUM]; /* IRQ 32 to 160 */
	u32 priority_level[GIC_DIST_PRI_COMMON_NUM];
	u32 spi_target[GIC_DIST_SPI_TARGET_COMMON_NUM];
	u32 config[GIC_DIST_CONFIG_COMMON_NUM];
} context_gic_dist_common;

struct context_gic_dist_cpu {
	void __iomem *base;
	u32 enable_set[GIC_DIST_ENABLE_SET_CPU_NUM]; /* IRQ 0 to 31 */
	u32 priority_level[GIC_DIST_PRI_CPU_NUM];
	u32 spi_target[GIC_DIST_SPI_TARGET_CPU_NUM];
	u32 config[GIC_DIST_CONFIG_CPU_NUM];
};
static DEFINE_PER_CPU(struct context_gic_dist_cpu, context_gic_dist_cpu);

static struct {
	void __iomem *base;
	u32 ctrl;
	u32 cpu_pwrstatus;
	u32 inv_all_nonsecure;
	u32 filter_start_addr;
	u32 filter_end_addr;
	u32 access_ctrl_sac;
} context_scu;

#define UX500_NR_PRCC_BANKS 5
static struct {
	void __iomem *base;
	struct clk *clk;
	u32 bus_clk;
	u32 kern_clk;
} context_prcc[UX500_NR_PRCC_BANKS];

static u32 backup_sram_storage[NR_CPUS] = {
	IO_ADDRESS(U8500_CPU0_CP15_CR_BACKUPRAM_ADDR),
	IO_ADDRESS(U8500_CPU1_CP15_CR_BACKUPRAM_ADDR),
};

static u32 gpio_bankaddr[GPIO_NUM_BANKS] = {IO_ADDRESS(U8500_GPIOBANK0_BASE),
					    IO_ADDRESS(U8500_GPIOBANK1_BASE),
					    IO_ADDRESS(U8500_GPIOBANK2_BASE),
					    IO_ADDRESS(U8500_GPIOBANK3_BASE),
					    IO_ADDRESS(U8500_GPIOBANK4_BASE),
					    IO_ADDRESS(U8500_GPIOBANK5_BASE),
					    IO_ADDRESS(U8500_GPIOBANK6_BASE),
					    IO_ADDRESS(U8500_GPIOBANK7_BASE),
					    IO_ADDRESS(U8500_GPIOBANK8_BASE)
};

static u32 gpio_save[GPIO_NUM_BANKS][GPIO_NUM_SAVE_REGISTERS];

/*
 * Stacks and stack pointers
 */
static DEFINE_PER_CPU(u32[128], varm_registers_backup_stack);
static DEFINE_PER_CPU(u32 *, varm_registers_pointer);

static DEFINE_PER_CPU(u32[128], varm_cp15_backup_stack);
static DEFINE_PER_CPU(u32 *, varm_cp15_pointer);

static ATOMIC_NOTIFIER_HEAD(context_ape_notifier_list);
static ATOMIC_NOTIFIER_HEAD(context_arm_notifier_list);

/*
 * Store PPI irq mask before in ARM retention - use this to restore when
 * waking up. One per CPU.
 */
static unsigned int cpu_ppi_irqs[NR_CPUS];

/*
 * Register a simple callback for handling vape context save/restore
 */
int context_ape_notifier_register(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&context_ape_notifier_list, nb);
}
EXPORT_SYMBOL(context_ape_notifier_register);

/*
 * Remove a previously registered callback
 */
int context_ape_notifier_unregister(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&context_ape_notifier_list,
						    nb);
}
EXPORT_SYMBOL(context_ape_notifier_unregister);

/*
 * Register a simple callback for handling varm context save/restore
 */
int context_arm_notifier_register(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&context_arm_notifier_list, nb);
}
EXPORT_SYMBOL(context_arm_notifier_register);

/*
 * Remove a previously registered callback
 */
int context_arm_notifier_unregister(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&context_arm_notifier_list,
						    nb);
}
EXPORT_SYMBOL(context_arm_notifier_unregister);

static void save_prcc(void)
{
	int i;

	for (i = 0; i < UX500_NR_PRCC_BANKS; i++) {
		clk_enable(context_prcc[i].clk);

		context_prcc[i].bus_clk =
			readl(context_prcc[i].base + PRCC_PCKSR);
		context_prcc[i].kern_clk =
			readl(context_prcc[i].base + PRCC_KCKSR);

		clk_disable(context_prcc[i].clk);
	}
}

static void restore_prcc(void)
{
	int i;

	for (i = 0; i < UX500_NR_PRCC_BANKS; i++) {
		clk_enable(context_prcc[i].clk);

		writel(~context_prcc[i].bus_clk,
		       context_prcc[i].base + PRCC_PCKDIS);
		writel(~context_prcc[i].kern_clk,
		       context_prcc[i].base + PRCC_KCKDIS);

		writel(context_prcc[i].bus_clk,
		       context_prcc[i].base + PRCC_PCKEN);
		writel(context_prcc[i].kern_clk,
		       context_prcc[i].base + PRCC_KCKEN);
		/*
		 * Consider having a while over KCK/BCK_STATUS
		 * to check that all clocks get disabled/enabled
		 */

		clk_disable(context_prcc[i].clk);
	}
}

static void save_stm_ape(void)
{
	/*
	 * TODO: Check with PRCMU developers how STM is handled by PRCMU
	 * firmware. According to DB5500 design spec there is a "flush"
	 * mechanism supposed to be used by the PRCMU before power down,
	 * PRCMU fw might save/restore the following three registers
	 * at the same time.
	 */
	context_stm_ape.cr  = readl(context_stm_ape.base +
				    STM_CR_OFFSET);
	context_stm_ape.mmc = readl(context_stm_ape.base +
				    STM_MMC_OFFSET);
	context_stm_ape.ter = readl(context_stm_ape.base +
				    STM_TER_OFFSET);
}

static void restore_stm_ape(void)
{
	writel(context_stm_ape.ter,
	       context_stm_ape.base + STM_TER_OFFSET);
	writel(context_stm_ape.mmc,
	       context_stm_ape.base + STM_MMC_OFFSET);
	writel(context_stm_ape.cr,
	       context_stm_ape.base + STM_CR_OFFSET);
}

static bool inline tpiu_clocked(void)
{
	return ux500_jtag_enabled();
}

/*
 * Save the context of the Trace Port Interface Unit (TPIU).
 * Saving/restoring is needed for the PTM tracing to work together
 * with the sleep states ApSleep and ApDeepSleep.
 */
static void save_tpiu(void)
{
	if (!tpiu_clocked())
		return;

	context_tpiu.port_size		  = readl(context_tpiu.base +
						  TPIU_PORT_SIZE);
	context_tpiu.trigger_counter	  = readl(context_tpiu.base +
						  TPIU_TRIGGER_COUNTER);
	context_tpiu.trigger_multiplier   = readl(context_tpiu.base +
						TPIU_TRIGGER_MULTIPLIER);
	context_tpiu.current_test_pattern = readl(context_tpiu.base +
						  TPIU_CURRENT_TEST_PATTERN);
	context_tpiu.test_pattern_repeat  = readl(context_tpiu.base +
						 TPIU_TEST_PATTERN_REPEAT);
	context_tpiu.formatter		  = readl(context_tpiu.base +
						  TPIU_FORMATTER);
	context_tpiu.formatter_sync	  = readl(context_tpiu.base +
						  TPIU_FORMATTER_SYNC);
}

/*
 * Restore the context of the Trace Port Interface Unit (TPIU).
 * Saving/restoring is needed for the PTM tracing to work together
 * with the sleep states ApSleep and ApDeepSleep.
 */
static void restore_tpiu(void)
{
	if (!tpiu_clocked())
		return;

	writel(TPIU_UNLOCK_CODE,
	       context_tpiu.base + TPIU_LOCK_ACCESS_REGISTER);

	writel(context_tpiu.port_size,
	       context_tpiu.base + TPIU_PORT_SIZE);
	writel(context_tpiu.trigger_counter,
	       context_tpiu.base + TPIU_TRIGGER_COUNTER);
	writel(context_tpiu.trigger_multiplier,
	       context_tpiu.base + TPIU_TRIGGER_MULTIPLIER);
	writel(context_tpiu.current_test_pattern,
	       context_tpiu.base + TPIU_CURRENT_TEST_PATTERN);
	writel(context_tpiu.test_pattern_repeat,
	       context_tpiu.base + TPIU_TEST_PATTERN_REPEAT);
	writel(context_tpiu.formatter,
	       context_tpiu.base + TPIU_FORMATTER);
	writel(context_tpiu.formatter_sync,
	       context_tpiu.base + TPIU_FORMATTER_SYNC);
}

/*
 * Save GIC CPU IF registers
 *
 * This is per cpu so it needs to be called for each one.
 */
static void save_gic_if_cpu(struct context_gic_cpu *c_gic_cpu)
{
	c_gic_cpu->ctrl     = readl_relaxed(c_gic_cpu->base + GIC_CPU_CTRL);
	c_gic_cpu->primask  = readl_relaxed(c_gic_cpu->base + GIC_CPU_PRIMASK);
	c_gic_cpu->binpoint = readl_relaxed(c_gic_cpu->base + GIC_CPU_BINPOINT);
}

/*
 * Restore GIC CPU IF registers
 *
 * This is per cpu so it needs to be called for each one.
 */
static void restore_gic_if_cpu(struct context_gic_cpu *c_gic_cpu)
{
	writel_relaxed(c_gic_cpu->ctrl,     c_gic_cpu->base + GIC_CPU_CTRL);
	writel_relaxed(c_gic_cpu->primask,  c_gic_cpu->base + GIC_CPU_PRIMASK);
	writel_relaxed(c_gic_cpu->binpoint, c_gic_cpu->base + GIC_CPU_BINPOINT);
}

/*
 * Save GIC Distributor Common registers
 *
 * This context is common. Only one CPU needs to call.
 *
 * Save SPI (Shared Peripheral Interrupt) settings, IRQ 32-159.
 */
static void save_gic_dist_common(void)
{
	int i;

	context_gic_dist_common.ns = readl_relaxed(context_gic_dist_common.base
					   + GIC_DIST_ENABLE_NS);

	for (i = 0; i < GIC_DIST_ENABLE_SET_COMMON_NUM; i++)
		context_gic_dist_common.enable_set[i] =
			readl_relaxed(context_gic_dist_common.base +
			      GIC_DIST_ENABLE_SET_SPI32 +  i * 4);

	for (i = 0; i < GIC_DIST_PRI_COMMON_NUM; i++)
		context_gic_dist_common.priority_level[i] =
			readl_relaxed(context_gic_dist_common.base +
			      GIC_DIST_PRI_SPI32 +  i * 4);

	for (i = 0; i < GIC_DIST_SPI_TARGET_COMMON_NUM; i++)
		context_gic_dist_common.spi_target[i] =
			readl_relaxed(context_gic_dist_common.base +
			      GIC_DIST_SPI_TARGET_SPI32 +  i * 4);

	for (i = 0; i < GIC_DIST_CONFIG_COMMON_NUM; i++)
		context_gic_dist_common.config[i] =
			readl_relaxed(context_gic_dist_common.base +
			      GIC_DIST_CONFIG_SPI32 +  i * 4);
}

/*
 * Restore GIC Distributor Common registers
 *
 * This context is common. Only one CPU needs to call.
 *
 * Save SPI (Shared Peripheral Interrupt) settings, IRQ 32-159.
 */
static void restore_gic_dist_common(void)
{
	int i;

	for (i = 0; i < GIC_DIST_CONFIG_COMMON_NUM; i++)
		writel_relaxed(context_gic_dist_common.config[i],
		       context_gic_dist_common.base +
		       GIC_DIST_CONFIG_SPI32 +  i * 4);

	for (i = 0; i < GIC_DIST_SPI_TARGET_COMMON_NUM; i++)
		writel_relaxed(context_gic_dist_common.spi_target[i],
		       context_gic_dist_common.base +
		       GIC_DIST_SPI_TARGET_SPI32 +  i * 4);

	for (i = 0; i < GIC_DIST_PRI_COMMON_NUM; i++)
		writel_relaxed(context_gic_dist_common.priority_level[i],
			context_gic_dist_common.base +
		       GIC_DIST_PRI_SPI32 +  i * 4);

	for (i = 0; i < GIC_DIST_ENABLE_SET_COMMON_NUM; i++)
		writel_relaxed(context_gic_dist_common.enable_set[i],
		       context_gic_dist_common.base +
		       GIC_DIST_ENABLE_SET_SPI32 +  i * 4);

	writel_relaxed(context_gic_dist_common.ns,
	       context_gic_dist_common.base + GIC_DIST_ENABLE_NS);
}

/*
 * Save GIC Dist CPU registers
 *
 * This needs to be called by all cpu:s which will not call
 * save_gic_dist_common(). Only the registers of the GIC which are
 * banked will be saved.
 */
static void save_gic_dist_cpu(struct context_gic_dist_cpu *c_gic)
{
	int i;

	for (i = 0; i < GIC_DIST_ENABLE_SET_CPU_NUM; i++)
		c_gic->enable_set[i] =
			readl_relaxed(c_gic->base +
			      GIC_DIST_ENABLE_SET_SPI0 +  i * 4);

	for (i = 0; i < GIC_DIST_PRI_CPU_NUM; i++)
		c_gic->priority_level[i] =
			readl_relaxed(c_gic->base +
			      GIC_DIST_PRI_SPI0 +  i * 4);

	for (i = 0; i < GIC_DIST_SPI_TARGET_CPU_NUM; i++)
		c_gic->spi_target[i] =
			readl_relaxed(c_gic->base +
			      GIC_DIST_SPI_TARGET_SPI0 +  i * 4);

	for (i = 0; i < GIC_DIST_CONFIG_CPU_NUM; i++)
		c_gic->config[i] =
			readl_relaxed(c_gic->base +
			      GIC_DIST_CONFIG_SPI0 +  i * 4);
}

/*
 * Restore GIC Dist CPU registers
 *
 * This needs to be called by all cpu:s which will not call
 * restore_gic_dist_common(). Only the registers of the GIC which are
 * banked will be saved.
 */
static void restore_gic_dist_cpu(struct context_gic_dist_cpu *c_gic)
{
	int i;

	for (i = 0; i < GIC_DIST_CONFIG_CPU_NUM; i++)
		writel_relaxed(c_gic->config[i],
		       c_gic->base +
		       GIC_DIST_CONFIG_SPI0 +  i * 4);

	for (i = 0; i < GIC_DIST_SPI_TARGET_CPU_NUM; i++)
		writel_relaxed(c_gic->spi_target[i],
		       c_gic->base +
		       GIC_DIST_SPI_TARGET_SPI0 +  i * 4);

	for (i = 0; i < GIC_DIST_PRI_CPU_NUM; i++)
		writel_relaxed(c_gic->priority_level[i],
			c_gic->base +
		       GIC_DIST_PRI_SPI0 +  i * 4);

	for (i = 0; i < GIC_DIST_ENABLE_SET_CPU_NUM; i++)
		writel_relaxed(c_gic->enable_set[i],
		       c_gic->base +
		       GIC_DIST_ENABLE_SET_SPI0 +  i * 4);
}

/*
 * Store irq mask and disable PPI interrupts in ARM retention
 */
void context_gic_dist_store_ppi_irqs(void)
{
	int this_cpu = smp_processor_id();

	/* Store PPI irqs */
	cpu_ppi_irqs[this_cpu] = readl_relaxed(context_gic_dist_common.base +
			GIC_DIST_ENABLE_CLEAR_0) & GIC_DIST_ENABLE_PPI_MASK;
	/* Disable PPI irqs */
	writel_relaxed(cpu_ppi_irqs[this_cpu],
			context_gic_dist_common.base + GIC_DIST_ENABLE_CLEAR_0);
}

/*
 * Restore PPI interrupts after in ARM retention
 */
void context_gic_dist_restore_ppi_irqs(void)
{
	int this_cpu = smp_processor_id();

	/* Restore PPI irqs */
	writel_relaxed(cpu_ppi_irqs[this_cpu],
			context_gic_dist_common.base + GIC_DIST_ENABLE_SET);
}

/*
 * Disable interrupts that are not necessary
 * to have turned on during ApDeepSleep.
 */
void context_gic_dist_disable_unneeded_irqs(void)
{
	writel(0xffffffff,
	       context_gic_dist_common.base +
	       GIC_DIST_ENABLE_CLEAR_0);

	writel(0xffffffff,
	       context_gic_dist_common.base +
	       GIC_DIST_ENABLE_CLEAR_32);

	/* Leave PRCMU IRQ 0 and 1 enabled */
	writel(0xffff3fff,
	       context_gic_dist_common.base +
	       GIC_DIST_ENABLE_CLEAR_64);

	writel(0xffffffff,
	       context_gic_dist_common.base +
	       GIC_DIST_ENABLE_CLEAR_96);

	writel(0xffffffff,
	       context_gic_dist_common.base +
	       GIC_DIST_ENABLE_CLEAR_128);
}

static void save_scu(void)
{
	context_scu.ctrl =
		readl_relaxed(context_scu.base + SCU_CTRL);
	context_scu.cpu_pwrstatus =
		readl_relaxed(context_scu.base + SCU_CPU_STATUS);
	context_scu.inv_all_nonsecure =
		readl_relaxed(context_scu.base + SCU_INVALIDATE);
	context_scu.filter_start_addr =
		readl_relaxed(context_scu.base + SCU_FILTER_STARTADDR);
	context_scu.filter_end_addr	=
		readl_relaxed(context_scu.base + SCU_FILTER_ENDADDR);
	context_scu.access_ctrl_sac	=
		readl_relaxed(context_scu.base + SCU_ACCESS_CTRL_SAC);
}

static void restore_scu(void)
{
	writel_relaxed(context_scu.ctrl,
	       context_scu.base + SCU_CTRL);
	writel_relaxed(context_scu.cpu_pwrstatus,
	       context_scu.base + SCU_CPU_STATUS);
	writel_relaxed(context_scu.inv_all_nonsecure,
	       context_scu.base + SCU_INVALIDATE);
	writel_relaxed(context_scu.filter_start_addr,
	       context_scu.base + SCU_FILTER_STARTADDR);
	writel_relaxed(context_scu.filter_end_addr,
	       context_scu.base + SCU_FILTER_ENDADDR);
	writel_relaxed(context_scu.access_ctrl_sac,
	       context_scu.base + SCU_ACCESS_CTRL_SAC);
}

/*
 * Save VAPE context
 */
void context_vape_save(void)
{
	atomic_notifier_call_chain(&context_ape_notifier_list,
				   CONTEXT_APE_SAVE, NULL);

	if (cpu_is_u5500())
		u5500_context_save_icn();
	if (cpu_is_u8500())
		u8500_context_save_icn();
	if (cpu_is_u9540())
		u9540_context_save_icn();

	save_stm_ape();

	save_tpiu();

	save_prcc();
}

/*
 * Restore VAPE context
 */
void context_vape_restore(void)
{
	restore_prcc();

	restore_tpiu();

	restore_stm_ape();

	if (cpu_is_u5500())
		u5500_context_restore_icn();
	if (cpu_is_u8500())
		u8500_context_restore_icn();
	if (cpu_is_u9540())
		u9540_context_restore_icn();

	atomic_notifier_call_chain(&context_ape_notifier_list,
				   CONTEXT_APE_RESTORE, NULL);
}

/*
 * Save GPIO registers that might be modified
 * for power save reasons.
 */
void context_gpio_save(void)
{
	int i;

	for (i = 0; i < GPIO_NUM_BANKS; i++) {
		gpio_save[i][0] = readl(gpio_bankaddr[i] + NMK_GPIO_AFSLA);
		gpio_save[i][1] = readl(gpio_bankaddr[i] + NMK_GPIO_AFSLB);
		gpio_save[i][2] = readl(gpio_bankaddr[i] + NMK_GPIO_PDIS);
		gpio_save[i][3] = readl(gpio_bankaddr[i] + NMK_GPIO_DIR);
		gpio_save[i][4] = readl(gpio_bankaddr[i] + NMK_GPIO_DAT);
		gpio_save[i][6] = readl(gpio_bankaddr[i] + NMK_GPIO_SLPC);
	}
}

/*
 * Restore GPIO registers that might be modified
 * for power save reasons.
 */
void context_gpio_restore(void)
{
	int i;
	u32 output_state;
	u32 pull_up;
	u32 pull_down;
	u32 pull;

	for (i = 0; i < GPIO_NUM_BANKS; i++) {
		writel(gpio_save[i][2], gpio_bankaddr[i] + NMK_GPIO_PDIS);

		writel(gpio_save[i][3], gpio_bankaddr[i] + NMK_GPIO_DIR);

		/* Set the high outputs. outpute_state = GPIO_DIR & GPIO_DAT */
		output_state = gpio_save[i][3] & gpio_save[i][4];
		writel(output_state, gpio_bankaddr[i] + NMK_GPIO_DATS);

		/*
		 * Set the low outputs.
		 * outpute_state = ~(GPIO_DIR & GPIO_DAT) & GPIO_DIR
		 */
		output_state = ~(gpio_save[i][3] & gpio_save[i][4]) &
			gpio_save[i][3];
		writel(output_state, gpio_bankaddr[i] + NMK_GPIO_DATC);

		/*
		 * Restore pull up/down.
		 * Only write pull up/down settings on inputs where
		 * PDIS is not set.
		 * pull = (~GPIO_DIR & ~GPIO_PDIS)
		 */
		pull = (~gpio_save[i][3] & ~gpio_save[i][2]);
		nmk_gpio_read_pull(i, &pull_up);

		pull_down = pull & ~pull_up;
		pull_up = pull & pull_up;
		/* Set pull ups */
		writel(pull_up, gpio_bankaddr[i] + NMK_GPIO_DATS);
		/* Set pull downs */
		writel(pull_down, gpio_bankaddr[i] + NMK_GPIO_DATC);

		writel(gpio_save[i][6], gpio_bankaddr[i] + NMK_GPIO_SLPC);

	}
}

/*
 * Restore GPIO mux registers that might be modified
 * for power save reasons.
 */
void context_gpio_restore_mux(void)
{
	int i;

	/* Change mux settings */
	for (i = 0; i < GPIO_NUM_BANKS; i++) {
		writel(gpio_save[i][0], gpio_bankaddr[i] + NMK_GPIO_AFSLA);
		writel(gpio_save[i][1], gpio_bankaddr[i] + NMK_GPIO_AFSLB);
	}
}

/*
 * Safe sequence used to switch IOs between GPIO and Alternate-C mode:
 *  - Save SLPM registers (Not done.)
 *  - Set SLPM=0 for the IOs you want to switch. (We assume that all
 *    SLPM registers already are 0 except for the ones that wants to
 *    have the mux connected in sleep (e.g modem STM)).
 *  - Configure the GPIO registers for the IOs that are being switched
 *  - Set IOFORCE=1
 *  - Modify the AFLSA/B registers for the IOs that are being switched
 *  - Set IOFORCE=0
 *  - Restore SLPM registers (Not done.)
 *  - Any spurious wake up event during switch sequence to be ignored
 *    and cleared
 */
void context_gpio_mux_safe_switch(bool begin)
{
	int i;

	static u32 rwimsc[GPIO_NUM_BANKS];
	static u32 fwimsc[GPIO_NUM_BANKS];

	if (begin) {
		for (i = 0; i < GPIO_NUM_BANKS; i++) {
			/* Save registers */
			rwimsc[i] = readl(gpio_bankaddr[i] + NMK_GPIO_RWIMSC);
			fwimsc[i] = readl(gpio_bankaddr[i] + NMK_GPIO_FWIMSC);

			/* Prevent spurious wakeups */
			writel(0, gpio_bankaddr[i] + NMK_GPIO_RWIMSC);
			writel(0, gpio_bankaddr[i] + NMK_GPIO_FWIMSC);
		}

		ux500_pm_prcmu_set_ioforce(true);
	} else {
		ux500_pm_prcmu_set_ioforce(false);

		/* Restore wake up settings */
		for (i = 0; i < GPIO_NUM_BANKS; i++) {
			writel(rwimsc[i], gpio_bankaddr[i] + NMK_GPIO_RWIMSC);
			writel(fwimsc[i], gpio_bankaddr[i] + NMK_GPIO_FWIMSC);
		}
	}
}

/*
 * Save common
 *
 * This function must be called once for all cores before going to deep sleep.
 */
void context_varm_save_common(void)
{
	atomic_notifier_call_chain(&context_arm_notifier_list,
				   CONTEXT_ARM_COMMON_SAVE, NULL);

	/* Save common parts */
	save_gic_dist_common();
	save_scu();
}

/*
 * Restore common
 *
 * This function must be called once for all cores when waking up from deep
 * sleep.
 */
void context_varm_restore_common(void)
{
	/* Restore common parts */
	restore_scu();
	restore_gic_dist_common();

	atomic_notifier_call_chain(&context_arm_notifier_list,
				   CONTEXT_ARM_COMMON_RESTORE, NULL);
}

/*
 * Save core
 *
 * This function must be called once for each cpu core before going to deep
 * sleep.
 */
void context_varm_save_core(void)
{
	int cpu = smp_processor_id();

	atomic_notifier_call_chain(&context_arm_notifier_list,
				   CONTEXT_ARM_CORE_SAVE, NULL);

	per_cpu(varm_cp15_pointer, cpu) = per_cpu(varm_cp15_backup_stack, cpu);

	/* Save core */
	twd_save();
	save_gic_if_cpu(&per_cpu(context_gic_cpu, cpu));
	save_gic_dist_cpu(&per_cpu(context_gic_dist_cpu, cpu));
	context_save_cp15_registers(&per_cpu(varm_cp15_pointer, cpu));
}

/*
 * Restore core
 *
 * This function must be called once for each cpu core when waking up from
 * deep sleep.
 */
void context_varm_restore_core(void)
{
	int cpu = smp_processor_id();

	/* Restore core */
	context_restore_cp15_registers(&per_cpu(varm_cp15_pointer, cpu));
	restore_gic_dist_cpu(&per_cpu(context_gic_dist_cpu, cpu));
	restore_gic_if_cpu(&per_cpu(context_gic_cpu, cpu));
	twd_restore();

	atomic_notifier_call_chain(&context_arm_notifier_list,
				   CONTEXT_ARM_CORE_RESTORE, NULL);
}

/*
 * Save CPU registers
 *
 * This function saves ARM registers.
 */
void context_save_cpu_registers(void)
{
	int cpu = smp_processor_id();

	per_cpu(varm_registers_pointer, cpu) =
		per_cpu(varm_registers_backup_stack, cpu);
	context_save_arm_registers(&per_cpu(varm_registers_pointer, cpu));
}

/*
 * Restore CPU registers
 *
 * This function restores ARM registers.
 */
void context_restore_cpu_registers(void)
{
	int cpu = smp_processor_id();

	context_restore_arm_registers(&per_cpu(varm_registers_pointer, cpu));
}

/*
 * This function stores CP15 registers related to cache and mmu
 * in backup SRAM. It also stores stack pointer, CPSR
 * and return address for the PC in backup SRAM and
 * does wait for interrupt.
 */
void context_save_to_sram_and_wfi(bool cleanL2cache)
{
	int cpu = smp_processor_id();

	context_save_to_sram_and_wfi_internal(backup_sram_storage[cpu],
					      cleanL2cache);
}

static int __init context_init(void)
{
	int i;
	void __iomem *ux500_backup_ptr;

	/* allocate backup pointer for RAM data */
	ux500_backup_ptr = (void *)__get_free_pages(GFP_KERNEL,
				  get_order(U8500_BACKUPRAM_SIZE));

	if (!ux500_backup_ptr) {
		pr_warning("context: could not allocate backup memory\n");
		return -ENOMEM;
	}

	/*
	 * ROM code addresses to store backup contents,
	 * pass the physical address of back up to ROM code
	 */
	writel(virt_to_phys(ux500_backup_ptr),
	       IO_ADDRESS(U8500_EXT_RAM_LOC_BACKUPRAM_ADDR));

	if (cpu_is_u5500()) {
		writel(IO_ADDRESS(U5500_PUBLIC_BOOT_ROM_BASE),
		       IO_ADDRESS(U8500_CPU0_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR));

		writel(IO_ADDRESS(U5500_PUBLIC_BOOT_ROM_BASE),
		       IO_ADDRESS(U8500_CPU1_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR));

		context_tpiu.base = ioremap(U5500_TPIU_BASE, SZ_4K);
		context_stm_ape.base = ioremap(U5500_STM_REG_BASE, SZ_4K);
		context_scu.base = ioremap(U5500_SCU_BASE, SZ_4K);

		context_prcc[0].base = ioremap(U5500_CLKRST1_BASE, SZ_4K);
		context_prcc[1].base = ioremap(U5500_CLKRST2_BASE, SZ_4K);
		context_prcc[2].base = ioremap(U5500_CLKRST3_BASE, SZ_4K);
		context_prcc[3].base = ioremap(U5500_CLKRST5_BASE, SZ_4K);
		context_prcc[4].base = ioremap(U5500_CLKRST6_BASE, SZ_4K);

		context_gic_dist_common.base = ioremap(U5500_GIC_DIST_BASE, SZ_4K);
		per_cpu(context_gic_cpu, 0).base = ioremap(U5500_GIC_CPU_BASE, SZ_4K);
	} else if (cpu_is_u8500() || cpu_is_u9540()) {
		/* Give logical address to backup RAM. For both CPUs */
		if (cpu_is_u9540()) {
			writel(IO_ADDRESS_DB9540_ROM(U9540_PUBLIC_BOOT_ROM_BASE),
					IO_ADDRESS(U8500_CPU0_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR));

			writel(IO_ADDRESS_DB9540_ROM(U9540_PUBLIC_BOOT_ROM_BASE),
					IO_ADDRESS(U8500_CPU1_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR));
		} else {
			writel(IO_ADDRESS(U8500_PUBLIC_BOOT_ROM_BASE),
					IO_ADDRESS(U8500_CPU0_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR));

			writel(IO_ADDRESS(U8500_PUBLIC_BOOT_ROM_BASE),
					IO_ADDRESS(U8500_CPU1_BACKUPRAM_ADDR_PUBLIC_BOOT_ROM_LOG_ADDR));
		}

		context_tpiu.base = ioremap(U8500_TPIU_BASE, SZ_4K);
		context_stm_ape.base = ioremap(U8500_STM_REG_BASE, SZ_4K);
		context_scu.base = ioremap(U8500_SCU_BASE, SZ_4K);

		/* PERIPH4 is always on, so no need saving prcc */
		context_prcc[0].base = ioremap(U8500_CLKRST1_BASE, SZ_4K);
		context_prcc[1].base = ioremap(U8500_CLKRST2_BASE, SZ_4K);
		context_prcc[2].base = ioremap(U8500_CLKRST3_BASE, SZ_4K);
		context_prcc[3].base = ioremap(U8500_CLKRST5_BASE, SZ_4K);
		context_prcc[4].base = ioremap(U8500_CLKRST6_BASE, SZ_4K);

		context_gic_dist_common.base = ioremap(U8500_GIC_DIST_BASE, SZ_4K);
		per_cpu(context_gic_cpu, 0).base = ioremap(U8500_GIC_CPU_BASE, SZ_4K);
	}

	if (!context_tpiu.base
	    || !context_stm_ape.base
	    || !context_scu.base
	    || !context_prcc[0].base
	    || !context_prcc[1].base
	    || !context_prcc[2].base
	    || !context_prcc[3].base
	    || !context_prcc[4].base
	    || !context_gic_dist_common.base
	    || !per_cpu(context_gic_cpu, 0).base) {
		printk("context: ioremap failed\n");
		return -ENOMEM;
	}

	per_cpu(context_gic_dist_cpu, 0).base = context_gic_dist_common.base;

	for (i = 1; i < num_possible_cpus(); i++) {
		per_cpu(context_gic_cpu, i).base
			= per_cpu(context_gic_cpu, 0).base;
		per_cpu(context_gic_dist_cpu, i).base
			= per_cpu(context_gic_dist_cpu, 0).base;
	}

	for (i = 0; i < ARRAY_SIZE(context_prcc); i++) {
		const int clusters[] = {1, 2, 3, 5, 6};
		char clkname[10];

		snprintf(clkname, sizeof(clkname), "PERIPH%d", clusters[i]);

		context_prcc[i].clk = clk_get_sys(clkname, NULL);
		BUG_ON(IS_ERR(context_prcc[i].clk));
	}

	if (cpu_is_u8500()) {
		u8500_context_init();
	} else if (cpu_is_u5500()) {
		u5500_context_init();
	} else if (cpu_is_u9540()) {
		u9540_context_init();
	} else {
		printk(KERN_ERR "context: unknown hardware!\n");
		return -EINVAL;
	}

	return 0;
}
subsys_initcall(context_init);
