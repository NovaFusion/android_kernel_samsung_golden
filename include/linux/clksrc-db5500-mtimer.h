/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 */
#ifndef __CLKSRC_DB5500_MTIMER_H
#define __CLKSRC_DB5500_MTIMER_H

#include <linux/io.h>

#ifdef CONFIG_CLKSRC_DB5500_MTIMER
void db5500_mtimer_init(void __iomem *base);
#else
static inline void db5500_mtimer_init(void __iomem *base) {}
#endif

#endif
