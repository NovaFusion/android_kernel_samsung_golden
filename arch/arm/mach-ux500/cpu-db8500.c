/*
 * Copyright (C) 2008-2009 ST-Ericsson SA
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/sys_soc.h>
#include <linux/delay.h>

#include <plat/gpio-nomadik.h>

#include <asm/pmu.h>
#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/reboot_reasons.h>
#include <mach/usb.h>
#include <mach/ste-dma40-db8500.h>

#include "devices-db8500.h"
#include "prcc.h"
#if defined (CONFIG_SAMSUNG_USE_GETLOG)
#include <mach/sec_getlog.h>
#define KYLE_MEM_BANK_0_ADDR	0x00000000
#define KYLE_MEM_BANK_0_SIZE	0x20000000
#define KYLE_MEM_BANK_1_ADDR	0x20000000
#define KYLE_MEM_BANK_1_SIZE	0x10000000
#endif

/* minimum static i/o mapping required to boot U8500 platforms */
static struct map_desc u8500_uart_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_UART0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_UART2_BASE, SZ_4K),
};
/*  U8500 and U9540 common io_desc */
static struct map_desc u8500_common_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_GIC_DIST_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_L2CC_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_MTU1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_RTC_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_SCU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_BACKUPRAM0_BASE, SZ_8K),
	__IO_DEV_DESC(U8500_CLKRST1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST3_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST5_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST6_BASE, SZ_4K),

	__IO_DEV_DESC(U8500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO3_BASE, SZ_4K),
};

/* U8500 IO map specific description */
static struct map_desc u8500_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K),
	__MEM_DEV_DESC(U8500_BOOT_ROM_BASE, SZ_1M),
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE, SZ_4K),

};

/* U9540 IO map specific description */
static struct map_desc u9540_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K + SZ_8K),
	__MEM_DEV_DESC_DB9540_ROM(U9540_BOOT_ROM_BASE, SZ_1M),
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE, SZ_4K + SZ_8K),
	#ifdef CONFIG_C2C
	__IO_DEV_DESC(U8500_C2C_BASE, SZ_4K),
	#endif
};


void __init u8500_map_io(void)
{
	/*
	 * Map the UARTs early so that the DEBUG_LL stuff continues to work.
	 */
	iotable_init(u8500_uart_io_desc, ARRAY_SIZE(u8500_uart_io_desc));

	ux500_map_io();

	iotable_init(u8500_common_io_desc, ARRAY_SIZE(u8500_common_io_desc));

	if (cpu_is_u9540())
		iotable_init(u9540_io_desc, ARRAY_SIZE(u9540_io_desc));
	else
		iotable_init(u8500_io_desc, ARRAY_SIZE(u8500_io_desc));

	_PRCMU_BASE = __io_address(U8500_PRCMU_BASE);
	
#if defined (CONFIG_SAMSUNG_USE_GETLOG)
	sec_getlog_supply_meminfo(KYLE_MEM_BANK_0_SIZE,
				  KYLE_MEM_BANK_0_ADDR,
				  KYLE_MEM_BANK_1_SIZE,
				  KYLE_MEM_BANK_1_ADDR);
#endif
	
}

/*
 * 8500 revisions
 */

bool cpu_is_u8500v20(void)
{
	return cpu_is_u8500() && (dbx500_revision() == 0xB0);
}

bool cpu_is_u8500v21(void)
{
	return cpu_is_u8500() && (dbx500_revision() == 0xB1);
}

bool cpu_is_u8500v22(void)
{
	return cpu_is_u8500() && (dbx500_revision() == 0xB2);
}

static struct resource db8500_pmu_resources[] = {
	[0] = {
		.start		= IRQ_DB8500_PMU,
		.end		= IRQ_DB8500_PMU,
		.flags		= IORESOURCE_IRQ,
	},
};

/*
 * The PMU IRQ lines of two cores are wired together into a single interrupt.
 * Bounce the interrupt to the other core if it's not ours.
 */
static irqreturn_t db8500_pmu_handler(int irq, void *dev, irq_handler_t handler)
{
	irqreturn_t ret = handler(irq, dev);
	int other = !smp_processor_id();

	if (ret == IRQ_NONE && cpu_online(other))
		irq_set_affinity(irq, cpumask_of(other));

	/*
	 * We should be able to get away with the amount of IRQ_NONEs we give,
	 * while still having the spurious IRQ detection code kick in if the
	 * interrupt really starts hitting spuriously.
	 */
	return ret;
}

static struct arm_pmu_platdata db8500_pmu_platdata = {
	.handle_irq		= db8500_pmu_handler,
};

static struct platform_device db8500_pmu_device = {
	.name			= "arm-pmu",
	.id			= ARM_PMU_DEVICE_CPU,
	.num_resources		= ARRAY_SIZE(db8500_pmu_resources),
	.resource		= db8500_pmu_resources,
	.dev.platform_data	= &db8500_pmu_platdata,
};

static unsigned int per_clkrst_base[7] = {
	0,
	U8500_CLKRST1_BASE,
	U8500_CLKRST2_BASE,
	U8500_CLKRST3_BASE,
	0,
	0,
	U8500_CLKRST6_BASE,
};

void u8500_reset_ip(unsigned char per, unsigned int ip_mask)
{
	void __iomem *prcc_rst_set, *prcc_rst_clr;

	if (per == 0 || per == 4 || per == 5 || per > 6)
		return;

	prcc_rst_set = __io_address(per_clkrst_base[per] +
			PRCC_K_SOFTRST_SET);
	prcc_rst_clr = __io_address(per_clkrst_base[per] +
			PRCC_K_SOFTRST_CLR);

	/* Activate soft reset PRCC_K_SOFTRST_CLR */
	writel(ip_mask, prcc_rst_clr);
	udelay(1);

	/* Release soft reset PRCC_K_SOFTRST_SET */
	writel(ip_mask, prcc_rst_set);
	udelay(1);
}

/* ICN Config registers */
#define NODE_HIBW1_ESRAM_IN_2_PRIORITY		0x08
#define NODE_HIBW1_DDR_IN_2_PRIORITY		0x408
#define NODE_HIBW1_DDR_IN_2_LIMIT		0x42C

/* LIMIT REG Shift */
#define NODE_HIBW1_LIMIT_SHIFT			12
#define NODE_HIBW1_FSIZE_SHIFT			4
#define NODE_HIBW1_NEWPRIO_SHIFT		0

static void __init db8500_icn_init(void)
{
	void __iomem *icnbase;
	u32 ddr_prio, ddr_limit;
	u32 esram_prio;

	icnbase = ioremap(U8500_ICN_BASE, SZ_8K);
	if (WARN_ON(!icnbase))
		return;

	/*
	 * Increase the DMA_M1 priority vs B2R2 & SVA_ESRAM/DDR on the
	 * Inter Connect Network: HiBW1_ESRAM & HiBW1_DDR nodes.
	 * SVA > B2R2 > DMA (def.) ===> DMA > SVA > B2R2
	 * Also enable the bandwidth limiter for DMA to DDR.
	 */
	esram_prio = 0x7;	/* priority = 0x7 (def: 0x6) */
	ddr_prio = 0x7;		/* priority = 0x7 (def: 0x6) */
	ddr_limit =
		0x1 << NODE_HIBW1_NEWPRIO_SHIFT | /* newprio = 0x1 (def: 0x3) */
		0xb << NODE_HIBW1_FSIZE_SHIFT |  /* frame size = 88 cycles   */
		0x4 << NODE_HIBW1_LIMIT_SHIFT;	 /* limit size = 80 bytes    */

	writel_relaxed(esram_prio, icnbase + NODE_HIBW1_ESRAM_IN_2_PRIORITY);
	writel_relaxed(ddr_prio, icnbase + NODE_HIBW1_DDR_IN_2_PRIORITY);
	writel_relaxed(ddr_limit, icnbase + NODE_HIBW1_DDR_IN_2_LIMIT);

	iounmap(icnbase);
}

static struct platform_device *platform_devs[] __initdata = {
	&u8500_gpio_devs[0],
	&u8500_gpio_devs[1],
	&u8500_gpio_devs[2],
	&u8500_gpio_devs[3],
	&u8500_gpio_devs[4],
	&u8500_gpio_devs[5],
	&u8500_gpio_devs[6],
	&u8500_gpio_devs[7],
	&u8500_gpio_devs[8],
	&db8500_pmu_device,
};

static int usb_db8500_rx_dma_cfg[] = {
	DB8500_DMA_DEV38_USB_OTG_IEP_1_9,
	DB8500_DMA_DEV37_USB_OTG_IEP_2_10,
	DB8500_DMA_DEV36_USB_OTG_IEP_3_11,
	DB8500_DMA_DEV19_USB_OTG_IEP_4_12,
	DB8500_DMA_DEV18_USB_OTG_IEP_5_13,
	DB8500_DMA_DEV17_USB_OTG_IEP_6_14,
	DB8500_DMA_DEV16_USB_OTG_IEP_7_15,
	DB8500_DMA_DEV39_USB_OTG_IEP_8
};

static int usb_db8500_tx_dma_cfg[] = {
	DB8500_DMA_DEV38_USB_OTG_OEP_1_9,
	DB8500_DMA_DEV37_USB_OTG_OEP_2_10,
	DB8500_DMA_DEV36_USB_OTG_OEP_3_11,
	DB8500_DMA_DEV19_USB_OTG_OEP_4_12,
	DB8500_DMA_DEV18_USB_OTG_OEP_5_13,
	DB8500_DMA_DEV17_USB_OTG_OEP_6_14,
	DB8500_DMA_DEV16_USB_OTG_OEP_7_15,
	DB8500_DMA_DEV39_USB_OTG_OEP_8
};

/*
 * This function is called from the board init
 */
void __init u8500_init_devices(void)
{
	ux500_init_devices();

#ifdef CONFIG_STM_TRACE
	/* Early init for STM tracing */
	platform_device_register(&ux500_stm_device);
#endif

	db8500_dma_init();
	db8500_icn_init();
	db8500_add_rtc();
	db8500_add_usb(usb_db8500_rx_dma_cfg, usb_db8500_tx_dma_cfg);

	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	return ;
}
