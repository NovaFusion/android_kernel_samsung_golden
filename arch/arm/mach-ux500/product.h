/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Jens Wiklander <jens.wiklander@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#ifndef UX500_PRODUCT_H
#define UX500_PRODUCT_H

#ifdef CONFIG_TEE_UX500

bool ux500_jtag_enabled(void);

#else

static inline bool ux500_jtag_enabled(void)
{
	return true;
}

#endif
#endif
