/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <mach/hardware.h>

struct ux500_debug_last_io {
	void *pc;
	void __iomem *vaddr;
	u64 jiffies;
} ____cacheline_aligned;

static struct ux500_debug_last_io *ux500_last_io;
static dma_addr_t ux500_last_io_phys;
static void __iomem *l2x0_base;

void ux500_debug_last_io_save(void *pc, void __iomem *vaddr)
{
	int index = smp_processor_id();

	if (ux500_last_io &&
		/* Ignore L2CC writes as they appear in each write{b,h,l} */
		((unsigned long)l2x0_base !=
			((unsigned long)vaddr & ~(SZ_4K - 1)))) {
		ux500_last_io[index].pc = pc;
		ux500_last_io[index].vaddr = vaddr;
		/* Reading without lock */
		ux500_last_io[index].jiffies = jiffies_64;
	}
}

static int __init ux500_debug_last_io_init(void)
{
	size_t size;

	size = sizeof(struct ux500_debug_last_io) * num_possible_cpus();

	ux500_last_io = dma_alloc_coherent(NULL, size, &ux500_last_io_phys,
								GFP_KERNEL);
	if (!ux500_last_io) {
		printk(KERN_ERR"%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	if (cpu_is_u5500())
		l2x0_base = __io_address(U5500_L2CC_BASE);
	else if (cpu_is_u8500() || cpu_is_u9540())
		l2x0_base = __io_address(U8500_L2CC_BASE);

	/*
	 * CONFIG_UX500_DEBUG_LAST_IO is only intended for debugging.
	 * It should not be left enabled.
	 */
	WARN_ON(1);

	return 0;
}

arch_initcall(ux500_debug_last_io_init);
