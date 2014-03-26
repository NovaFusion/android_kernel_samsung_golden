/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Architecture configuration for ST-Ericsson Ux500 platforms
 *
 * Author: Magnus Wendt <magnus.wendt@stericsson.com> for
 * ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */

#ifndef __ARCH_UX500_CONFIG_H__
#define __ARCH_UX500_CONFIG_H__

/* #include <mach/hardware.h> */

#define U5500_SGA_BASE 0x801D0000
#define U8500_SGA_BASE 0xA0300000

#if defined(SOC_DB5500) && (1 == SOC_DB5500)
#define UX500_SGA_BASE U5500_SGA_BASE
#else
#define UX500_SGA_BASE U8500_SGA_BASE
#endif

#define MEGABYTE (1024*1024)
#define MALI_MEM_BASE (128 * MEGABYTE)
#define MALI_MEM_SIZE ( 32 * MEGABYTE)
#define OS_MEM_SIZE   (128 * MEGABYTE)

/* Hardware revision u8500 v1: GX570-BU-00000-r0p1
 * Hardware revision u8500 v2: GX570-BU-00000-r1p0
 * Hardware revision u5500: GX570-BU-00000-r1p0
 * configuration registers: 0xA0300000-0xA031FFFFF  (stw8500v1_usermanual p269)
 *
 * Shared Peripheral Interrupt assignments:     (stw8500v1_usermanual p265-266)
 * Nb  | Interrupt Source
 * 116 | Mali400 combined
 * 115 | Mali400 geometry processor
 * 114 | Mali400 geometry processor MMU
 * 113 | Mali400 pixel processor
 * 112 | Mali400 pixel processor MMU
 *
 * irq offset: 32
 */

static _mali_osk_resource_t arch_configuration [] =
{
	{
		.type = MALI400GP,
		.description = "Mali-400 GP",
		.base = UX500_SGA_BASE + 0x0000,
		.irq = 115+32,
		.mmu_id = 1
	},
	{
		.type = MALI400PP,
		.base = UX500_SGA_BASE + 0x8000,
		.irq = 113+32,
		.description = "Mali-400 PP",
		.mmu_id = 2
	},
#if USING_MMU
	{
		.type = MMU,
		.base = UX500_SGA_BASE + 0x3000,
		.irq = 114+32,
		.description = "Mali-400 MMU for GP",
		.mmu_id = 1
	},
	{
		.type = MMU,
		.base = UX500_SGA_BASE + 0x4000,
		.irq = 112+32,
		.description = "Mali-400 MMU for PP",
		.mmu_id = 2
	},
#endif
	{
		.type = MEMORY,
		.description = "Mali SDRAM",
		.alloc_order = 0, /* Highest preference for this memory */
		.base = MALI_MEM_BASE,
		.size = 0,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE |_MALI_GP_READABLE | _MALI_GP_WRITEABLE
	},
#if USING_OS_MEMORY
	{
		.type = OS_MEMORY,
		.description = "Linux kernel memory",
		.alloc_order = 5, /* Medium preference for this memory */
		.size = 2047 * MEGABYTE,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_MMU_READABLE | _MALI_MMU_WRITEABLE
	},
#endif
	{
		.type = MEM_VALIDATION,
		.description = "Framebuffer",
		.base = 0x00000000, /* Validate all memory for now */
		.size = 2047 * MEGABYTE, /* "2GB ought to be enough for anyone" */
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_WRITEABLE | _MALI_PP_READABLE
	},
	{
		.type = MALI400L2,
		.base = UX500_SGA_BASE + 0x1000,
		.description = "Mali-400 L2 cache"
	},
};

#endif /* __ARCH_UX500_CONFIG_H__ */
