/*
 * Trusted application for starting the modem.
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/elf.h>
#include <mach/hardware.h>

#include "mach/tee_ta_start_modem.h"

static int reset_modem(unsigned long modem_start_addr)
{
	void __iomem *base = ioremap(U5500_ACCCON_BASE_SEC, 0x2FF);
	if (!base)
		return -ENOMEM;

	pr_info("[%s] Setting modem start address!\n", __func__);
	writel(base + (U5500_ACCCON_CPUVEC_RESET_ADDR_OFFSET/sizeof(uint32_t)),
	       modem_start_addr);

	pr_info("[%s] resetting the modem!\n", __func__);
	writel(base + (U5500_ACCCON_ACC_CPU_CTRL_OFFSET/sizeof(uint32_t)), 1);

	iounmap(base);

	return 0;
}

int tee_ta_start_modem(struct tee_ta_start_modem *data)
{
	int ret = 0;
	struct elfhdr *elfhdr;
	void __iomem *vaddr;

	vaddr = ioremap((unsigned long)data->access_image_descr.elf_hdr,
			sizeof(struct elfhdr));
	if (!vaddr)
		return -ENOMEM;

	elfhdr = (struct elfhdr *)readl(vaddr);
	pr_info("Reading in kernel:elfhdr 0x%x:elfhdr->entry=0x%x\n",
			(uint32_t)elfhdr, (uint32_t)elfhdr->e_entry);

	pr_info("[%s] reset modem()...\n", __func__);
	ret = reset_modem(elfhdr->e_entry);

	iounmap(vaddr);

	return ret;
}
