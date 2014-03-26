/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>,
 *	   Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/amba/serial.h>
#include <mach/setup.h>
#include <mach/hardware.h>
#include <mach/context.h>

#ifdef CONFIG_DBX500_CONTEXT

static struct {
	struct clk *uart_clk;
	void __iomem *base;
	/* dr */
	/* rsr_err */
	u32 dma_wm;
	u32 timeout;
	/* fr */
	u32 lcrh_rx;
	u32 ilpr;
	u32 ibrd;
	u32 fbrd;
	u32 lcrh_tx;
	u32 cr;
	u32 ifls;
	u32 imsc;
	/* ris */
	/* mis */
	/* icr */
	u32 dmacr;
	u32 xfcr;
	u32 xon1;
	u32 xon2;
	u32 xoff1;
	u32 xoff2;
	/* itcr */
	/* itip */
	/* itop */
	/* tdr */
	u32 abcr;
	/* absr */
	/* abfmt */
	/* abdr */
	/* abdfr */
	/* abmr */
	u32 abimsc;
	/* abris */
	/* abmis */
	/* abicr */
	/* id_product_h_xy */
	/* id_provider */
	/* periphid0 */
	/* periphid1 */
	/* periphid2 */
	/* periphid3 */
	/* pcellid0 */
	/* pcellid1 */
	/* pcellid2 */
	/* pcellid3 */
} context_uart;

static void save_uart(void)
{
	void __iomem *membase;

	membase = context_uart.base;

	clk_enable(context_uart.uart_clk);

	context_uart.dma_wm = readl_relaxed(membase + ST_UART011_DMAWM);
	context_uart.timeout = readl_relaxed(membase + ST_UART011_TIMEOUT);
	context_uart.lcrh_rx = readl_relaxed(membase + ST_UART011_LCRH_RX);
	context_uart.ilpr = readl_relaxed(membase + UART01x_ILPR);
	context_uart.ibrd = readl_relaxed(membase + UART011_IBRD);
	context_uart.fbrd = readl_relaxed(membase + UART011_FBRD);
	context_uart.lcrh_tx = readl_relaxed(membase + ST_UART011_LCRH_TX);
	context_uart.cr = readl_relaxed(membase + UART011_CR);
	context_uart.ifls = readl_relaxed(membase + UART011_IFLS);
	context_uart.imsc = readl_relaxed(membase + UART011_IMSC);
	context_uart.dmacr = readl_relaxed(membase + UART011_DMACR);
	context_uart.xfcr = readl_relaxed(membase + ST_UART011_XFCR);
	context_uart.xon1 = readl_relaxed(membase + ST_UART011_XON1);
	context_uart.xon2 = readl_relaxed(membase + ST_UART011_XON2);
	context_uart.xoff1 = readl_relaxed(membase + ST_UART011_XOFF1);
	context_uart.xoff2 = readl_relaxed(membase + ST_UART011_XOFF2);
	context_uart.abcr = readl_relaxed(membase + ST_UART011_ABCR);
	context_uart.abimsc = readl_relaxed(membase + ST_UART011_ABIMSC);

	clk_disable(context_uart.uart_clk);
}

static void restore_uart(void)
{
	int cnt;
	int retries = 100;
	unsigned int cr;
	void __iomem *membase;
	u16 dummy;
	bool show_warn = false;

	membase = context_uart.base;
	clk_enable(context_uart.uart_clk);

	writew_relaxed(context_uart.ifls, membase + UART011_IFLS);
	cr = UART01x_CR_UARTEN | UART011_CR_TXE | UART011_CR_LBE;

	writew_relaxed(cr, membase + UART011_CR);
	writew_relaxed(0, membase + UART011_FBRD);
	writew_relaxed(1, membase + UART011_IBRD);
	writew_relaxed(0, membase + ST_UART011_LCRH_RX);
	if (context_uart.lcrh_tx != ST_UART011_LCRH_RX) {
		int i;
		/*
		 * Wait 10 PCLKs before writing LCRH_TX register,
		 * to get this delay write read only register 10 times
		 */
		for (i = 0; i < 10; ++i)
			dummy = readw(membase + ST_UART011_LCRH_RX);
		writew_relaxed(0, membase + ST_UART011_LCRH_TX);
	}
	writew(0, membase + UART01x_DR);
	do {
		if (!(readw(membase + UART01x_FR) & UART01x_FR_BUSY))
			break;
		cpu_relax();
	} while (retries-- > 0);
	if (retries < 0)
		/*
		 * We can't print out a warning here since the uart is
		 * not fully restored. Do it later.
		 */
		show_warn = true;

	writel_relaxed(context_uart.dma_wm, membase + ST_UART011_DMAWM);
	writel_relaxed(context_uart.timeout, membase + ST_UART011_TIMEOUT);
	writel_relaxed(context_uart.lcrh_rx, membase + ST_UART011_LCRH_RX);
	writel_relaxed(context_uart.ilpr, membase + UART01x_ILPR);
	writel_relaxed(context_uart.ibrd, membase + UART011_IBRD);
	writel_relaxed(context_uart.fbrd, membase + UART011_FBRD);
	/*
	 * Wait 10 PCLKs before writing LCRH_TX register,
	 * to get this delay write read only register 10-3
	 * times, as already there are 3 writes after
	 * ST_UART011_LCRH_RX
	 */
	for (cnt = 0; cnt < 7; cnt++)
		dummy = readw(membase + ST_UART011_LCRH_RX);

	writel_relaxed(context_uart.lcrh_tx, membase + ST_UART011_LCRH_TX);
	writel_relaxed(context_uart.ifls, membase + UART011_IFLS);
	writel_relaxed(context_uart.dmacr, membase + UART011_DMACR);
	writel_relaxed(context_uart.xfcr, membase + ST_UART011_XFCR);
	writel_relaxed(context_uart.xon1, membase + ST_UART011_XON1);
	writel_relaxed(context_uart.xon2, membase + ST_UART011_XON2);
	writel_relaxed(context_uart.xoff1, membase + ST_UART011_XOFF1);
	writel_relaxed(context_uart.xoff2, membase + ST_UART011_XOFF2);
	writel_relaxed(context_uart.abcr, membase + ST_UART011_ABCR);
	writel_relaxed(context_uart.abimsc, membase + ST_UART011_ABIMSC);
	writel_relaxed(context_uart.cr, membase + UART011_CR);
	writel(context_uart.imsc, membase + UART011_IMSC);

	clk_disable(context_uart.uart_clk);

	if (show_warn)
		pr_warning("%s:uart tx busy\n", __func__);
}

static int uart_context_notifier_call(struct notifier_block *this,
				     unsigned long event, void *data)
{
	switch (event) {
	case CONTEXT_APE_SAVE:
		save_uart();
		break;

	case CONTEXT_APE_RESTORE:
		restore_uart();
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block uart_context_notifier = {
	.notifier_call = uart_context_notifier_call,
};

#define __UART_BASE(soc, x)	soc##_UART##x##_BASE
#define UART_BASE(soc, x)	__UART_BASE(soc, x)

static int __init uart_context_notifier_init(void)
{
	unsigned long base;
	static const char clkname[] __initconst
		= "uart" __stringify(CONFIG_UX500_DEBUG_UART);

	if (cpu_is_u8500() || cpu_is_u9540())
		base = UART_BASE(U8500, CONFIG_UX500_DEBUG_UART);
	else if (cpu_is_u5500())
		base = UART_BASE(U5500, CONFIG_UX500_DEBUG_UART);
	else
		ux500_unknown_soc();

	context_uart.base = ioremap(base, SZ_4K);
	context_uart.uart_clk = clk_get_sys(clkname, NULL);

	if (IS_ERR(context_uart.uart_clk)) {
		pr_err("%s:unable to get clk-uart%d\n", __func__,
		       CONFIG_UX500_DEBUG_UART);
		return -EINVAL;
	}

	return WARN_ON(context_ape_notifier_register(&uart_context_notifier));
}
arch_initcall(uart_context_notifier_init);
#endif
