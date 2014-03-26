/*
 * arch/arm/mach-u8500/include/mach/io.h
 *
 * Copyright (C) 1997-1999 Russell King
 *
 * Modifications:
 *  06-12-1997	RMK	Created.
 *  07-04-1999	RMK	Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)		__typesafe_io(a)

#ifndef CONFIG_UX500_DEBUG_LAST_IO
#define __mem_pci(a)	(a)
#else
extern void ux500_debug_last_io_save(void *pc, void __iomem *vaddr);

static inline void __iomem *__save_addr(void __iomem *p)
{
	void *pc;

	__asm__("mov %0, r15" : "=r" (pc));

	ux500_debug_last_io_save(pc, p);

	return p;
}

#define __mem_pci(a)	__save_addr((void __iomem *)(a))
#endif /* CONFIG_UX500_DEBUG_LAST_IO */

#endif /* __ASM_ARM_ARCH_IO_H */
