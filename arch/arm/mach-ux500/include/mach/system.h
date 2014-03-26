/*
 * Copyright (C) 2009 ST-Ericsson.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <linux/mfd/dbx500-prcmu.h>
#include <mach/sec_common.h>
#include <mach/reboot_reasons.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>

static inline void arch_idle(void)
{
	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks
	 */
	cpu_do_idle();
}

static inline void arch_reset(char mode, const char *cmd)
{
#ifdef CONFIG_UX500_SOC_DB8500
	unsigned short reason;

	reason = sec_common_update_reboot_reason(mode, cmd);

	if (mode == 'L' || mode == 'U' || 'K' == mode || 'F' == mode)
	/* Call the PRCMU reset API (w/o reset reason code) */
	prcmu_system_reset(reason);
	else
		ab8500_restart( reason );

#endif
}

#endif
