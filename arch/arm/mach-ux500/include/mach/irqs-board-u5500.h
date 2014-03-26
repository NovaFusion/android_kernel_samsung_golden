/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_IRQS_BOARD_U5500_H
#define __MACH_IRQS_BOARD_U5500_H

#include <linux/mfd/abx500/ab5500.h>

#define AB5500_NR_IRQS		(AB5500_NUM_IRQ_REGS * 8)
#define IRQ_AB5500_BASE		IRQ_BOARD_START
#define IRQ_AB5500_END		(IRQ_AB5500_BASE + AB5500_NR_IRQS)

#define U5500_IRQ_END		IRQ_AB5500_END

/*
 * We may have several boards, but only one will run at a
 * time, so the one with most IRQs will bump this ahead,
 * but the IRQ_BOARD_START remains the same for either board.
 */
#if U5500_IRQ_END > IRQ_BOARD_END
#undef IRQ_BOARD_END
#define IRQ_BOARD_END		U5500_IRQ_END
#endif

#endif
