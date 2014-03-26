/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 *         Rickard Andersson <rickard.andersson@stericsson.com> for
 *         ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#include <linux/linkage.h>
#include <mach/hardware.h>
#include <asm/hardware/cache-l2x0.h>

/*
 * Save and increment macro
 */
.macro SAVE_AND_INCREMENT FROM_REG TO_REG
	str \FROM_REG, [\TO_REG], #+4
.endm

/*
 * Decrement and restore macro
 */
.macro DECREMENT_AND_RESTORE FROM_REG TO_REG
	ldr \TO_REG, [\FROM_REG, #-4]!
.endm

.extern prcmu_stay_in_wfi_check

/*
 * Save ARM registers
 *
 * This function must be called in supervisor mode.
 *
 * r0 = address to backup stack pointer
 *
 * Backup stack operations:
 * + {sp, lr}^
 * + cpsr
 * + {r3, r8-r14} (FIQ mode: r3=spsr)
 * + {r3, r13, r14} (IRQ mode: r3=spsr)
 * + {r3, r13, r14} (abort mode: r3=spsr)
 * + {r3, r13, r14} (undef mode: r3=spsr)
 */
	.align
	.section ".text", "ax"
ENTRY(context_save_arm_registers)
	stmfd sp!, {r1, r2, r3, lr}		@ Save on stack
	ldr r1, [r0]				@ Read backup stack pointer

	stmia r1, {sp, lr}^			@ Store user mode sp and lr
	                                        @ registers
	add r1, r1, #8				@ Update backup pointer (not
	                                        @ done in previous instruction)

	mrs r2, cpsr				@ Get CPSR
	SAVE_AND_INCREMENT r2 r1		@ Save CPSR register
	orr r2, r2, #0xc0			@ Disable FIQ and IRQ
	bic r2, r2, #0x1f			@ Setup r2 to change mode

	@ The suffix to CPSR refers to which field(s) of the CPSR is
	@ rereferenced (you can specify one or more). Defined fields are:
	@
	@ c - control
	@ x - extension
	@ s - status
	@ f - flags

	orr r3, r2, #0x11			@ Save FIQ mode registers
	msr cpsr_cxsf, r3
	mrs r3, spsr
	stmia r1!, {r3, r8-r14}

	orr r3, r2, #0x12			@ Save IRQ mode registers
	msr cpsr_cxsf, r3
	mrs r3, spsr
	stmia r1!, {r3, r13, r14}

	orr r3, r2, #0x17			@ Save abort mode registers +
	                                        @ common mode registers
	msr cpsr_cxsf, r3
	mrs r3, spsr
	stmia r1!, {r3, r13, r14}

	orr r3, r2, #0x1B			@ Save undef mode registers
	msr cpsr_cxsf, r3
	mrs r3, spsr
	stmia r1!, {r3, r13, r14}

	orr r3, r2, #0x13			@ Return to supervisor mode
	msr cpsr_cxsf, r3

	str r1, [r0]				@ Write backup stack pointer
	ldmfd sp!, {r1, r2, r3, pc}		@ Restore registers and return



/*
 * Restore ARM registers
 *
 * This function must be called in supervisor mode.
 *
 * r0 = address to backup stack pointer
 *
 * Backup stack operations:
 * - {r3, r13, r14} (undef mode: spsr=r3)
 * - {r3, r13, r14} (abort mode: spsr=r3)
 * - {r3, r13, r14} (IRQ mode: spsr=r3)
 * - {r3, r8-r14} (FIQ mode: spsr=r3)
 * - cpsr
 * - {sp, lr}^
 */
	.align
	.section ".text", "ax"
ENTRY(context_restore_arm_registers)
	stmfd sp!, {r1, r2, r3, lr}		@ Save on stack
	ldr r1, [r0]				@ Read backup stack pointer

	mrs r2, cpsr				@ Get CPSR
	orr r2, r2, #0xc0			@ Disable FIQ and IRQ
	bic r2, r2, #0x1f			@ Setup r2 to change mode

	orr r3, r2, #0x1b			@ Restore undef mode registers
	msr cpsr_cxsf, r3
	ldmdb r1!, {r3, r13, r14}
	msr spsr_cxsf, r3

	orr r3, r2, #0x17			@ Restore abort mode registers
	msr cpsr_cxsf, r3
	ldmdb r1!, {r3, r13, r14}
	msr spsr_cxsf, r3

	orr r3, r2, #0x12			@ Restore IRQ mode registers
	msr cpsr_cxsf, r3
	ldmdb r1!, {r3, r13, r14}
	msr spsr_cxsf, r3

	orr r3, r2, #0x11			@ Restore FIQ mode registers
	msr cpsr_cxsf, r3
	ldmdb r1!, {r3, r8-r14}
	msr spsr_cxsf, r3

	DECREMENT_AND_RESTORE r1 r3		@ Restore cpsr register
	msr cpsr_cxsf, r3

	ldmdb r1, {sp, lr}^			@ Restore sp and lr registers
	sub r1, r1, #8				@ Update backup pointer (not
	                                        @ done in previous instruction)

	str r1, [r0]				@ Write backup stack pointer
	ldmfd sp!, {r1, r2, r3, pc}		@ Restore registers and return



/*
 * Save CP15 registers
 *
 * This function must be called in supervisor mode.
 *
 * r0 = address to backup stack pointer
 *
 * TTBR0, TTBR1, TTBRC, DACR CP15 registers are restored by boot ROM from SRAM.
 */
	.align 4
	.section ".text", "ax"
ENTRY(context_save_cp15_registers)
	stmfd sp!, {r1, r2, r3, lr}		@ Save on stack (r3 is saved due
						@ to 8 byte aligned stack)
	ldr r1, [r0]				@ Read backup stack pointer

	mrc p15, 0, r2, c12, c0, 0		@ Read Non-secure Vector Base
	                                        @ Address Register
	SAVE_AND_INCREMENT r2 r1

	mrc p15, 0, r2, c10, c2, 0		@ Access primary memory region
	                                        @ remap register
	SAVE_AND_INCREMENT r2 r1

	mrc p15, 0, r2, c10, c2, 1		@ Access normal memory region
	                                        @ remap register
	SAVE_AND_INCREMENT r2 r1

	mrc p15, 0, r2, c13, c0, 1		@ Read Context ID Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c13, c0, 2		@ Read Thread ID registers,
	                                        @ this register is both user
	                                        @ and privileged R/W accessible
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c13, c0, 3		@ Read Thread ID registers,
	                                        @ this register is user
	                                        @ read-only and privileged R/W
	                                        @ accessible.
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c13, c0, 4		@ Read Thread ID registers,
	                                        @ this register is privileged
	                                        @ R/W accessible only.
	SAVE_AND_INCREMENT r2 r1

	mrc p15, 2, r2, c0, c0, 0		@ Cache Size Selection Register
	SAVE_AND_INCREMENT r2 r1

	mrc p15, 0, r2, c9, c12, 0		@ Read PMNC Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c12, 1		@ Read PMCNTENSET Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c12, 5		@ Read PMSELR Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c13, 0		@ Read PMCCNTR Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c13, 1		@ Read PMXEVTYPER Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c14, 0		@ Read PMUSERENR Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c14, 1		@ Read PMINTENSET Register
	SAVE_AND_INCREMENT r2 r1
	mrc p15, 0, r2, c9, c14, 2		@ Read PMINTENCLR Register
	SAVE_AND_INCREMENT r2 r1

	mrc p15, 0, r2, c1, c0, 2		@ Read CPACR Register
	SAVE_AND_INCREMENT r2 r1

	str r1, [r0]				@ Write backup stack pointer
	ldmfd sp!, {r1, r2, r3, pc}		@ Restore registers and return



/*
 * Restore CP15 registers
 *
 * This function must be called in supervisor mode.
 *
 * r0 = address to backup stack pointer
 */
	.align 4
	.section ".text", "ax"
ENTRY(context_restore_cp15_registers)
	stmfd sp!, {r1, r2, r3, lr}		@ Save on stack (r3 is saved due
						@ to 8 byte aligned stack)
	ldr r1, [r0]				@ Read backup stack pointer

	DECREMENT_AND_RESTORE r1 r2		@ Write CPACR register
	mcr p15, 0, r2, c1, c0, 2
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c14, 2		@ Write PMINTENCLR Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c14, 1		@ Write PMINTENSET Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c14, 0		@ Write PMUSERENR Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c13, 1		@ Write PMXEVTYPER Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c13, 0		@ Write PMCCNTR Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c12, 5		@ Write PMSELR Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c12, 1		@ Write PMCNTENSET Register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c9, c12, 0		@ Write PMNC Register

	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 2, r2, c0, c0, 0		@ Cache Size Selection Register

	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c13, c0, 4		@ Write Thread ID registers,
	                                        @ this register is privileged
	                                        @ R/W accessible only
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c13, c0, 3		@ Write Thread ID registers,
	                                        @ this register is user
	                                        @ read-only and privileged R/W
	                                        @ accessible
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c13, c0, 2		@ Write Thread ID registers,
	                                        @ this register is both user
	                                        @ and privileged R/W accessible
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c13, c0, 1		@ Write Context ID Register

	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c10, c2, 1		@ Access normal memory region
	                                        @ remap register
	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c10, c2, 0		@ Access primary memory region
	                                        @ remap register

	DECREMENT_AND_RESTORE r1 r2
	mcr p15, 0, r2, c12, c0, 0		@ Write Non-secure Vector Base
	                                        @ Address Register

	str r1, [r0]				@ Write backup stack pointer
	ldmfd sp!, {r1, r2, r3, pc}		@ Restore registers and return


/*
 * L1 cache clean function. Commit 'dirty' data from L1
 * to L2 cache.
 *
 * r0, r1, r2,  used locally
 *
 */
	.align 4
	.section ".text", "ax"
ENTRY(context_clean_l1_cache_all)

	mov	r0, #0				@ swith to cache level 0
	                                        @ (L1 cache)
	mcr	p15, 2, r0, c0, c0, 0		@ select current cache level
	                                        @ in cssr

	dmb
	mov     r1, #0				@ r1 = way index
wayLoopL1clean:
	mov     r0, #0				@ r0 = line index
lineLoopL1clean:
	mov     r2, r1, lsl #30			@ TODO: OK to hard-code
	                                        @ SoC-specific L1 cache details?
	add     r2, r0, lsl #5
	mcr     p15, 0, r2, c7, c10, 2		@ Clean cache by set/way
	add     r0, r0, #1
	cmp     r0, #256			@ TODO: Ok with hard-coded
	                                        @ set/way sizes or do we have to
	                                        @ read them from ARM regs? Is it
	                                        @ set correctly in silicon?
	bne     lineLoopL1clean
	add     r1, r1, #1
	cmp     r1, #4				@ TODO: Ditto, sizes...
	bne     wayLoopL1clean

	dsb
	isb
	mov	pc, lr

ENDPROC(context_clean_l1_cache_all)

/*
 * L2 cache clean function. Commit from PL310 L2 cache
 * controller to DDR SDRAM.
 *
 * r0, r2 used locally
 *
 */
	.align 4
	.section ".text", "ax"
ENTRY(ux500_clean_l2_cache_all)

	ldr r0, =IO_ADDRESS(U8500_L2CC_BASE)

	ldr r1, =0xff				@ TODO: Ok to assume 8-way cache
	                                        @ on Ux500?
	str r1, [r0, #L2X0_CLEAN_WAY]
	L2busywaitLoopClean:
	ldr r1, [r0, #L2X0_CLEAN_WAY]
	cmp r1, #0				@ All bits in L2X0_CLEAN_WAY
	                                        @ will be zero once clean is
	                                        @ finished
	bne L2busywaitLoopClean

	ldr r1, =0x0
	str r1, [r0, #L2X0_CACHE_SYNC]
						@ l2x0 C code busy-wait here to
	                                        @ ensure no background op is
	                                        @ running.
						@ In our case we have already
	                                        @ checked this after the cache
	                                        @ clean and CACHE_SYNC is atomic
	                                        @ according to refman
	mov	pc, lr

/*
 * Last saves to backup RAM, cache clean and WFI
 *
 * r0 = address to backup_sram_storage base adress
 * r1 = indicate whether also L2 cache should be cleaned
 */
	.align 4
	.section ".text", "ax"
ENTRY(context_save_to_sram_and_wfi_internal)

	stmfd sp!, {r2-r12, lr}			@ save on stack.

	mrc p15, 0, r2, c1, c0, 0		@ read cp15 system control
	                                        @ register
	str r2, [r0, #0x00]
	mrc p15, 0, r2, c2, c0, 0		@ read cp15 ttb0 register
	str r2, [r0, #0x04]
	mrc p15, 0, r2, c2, c0, 1		@ read cp15 ttb1 register
	str r2, [r0, #0x08]
	mrc p15, 0, r2, c2, c0, 2		@ read cp15 ttb control register
	str r2, [r0, #0x0C]
	mrc p15, 0, r2, c3, c0, 0		@ read domain access control
	                                        @ register
	str r2, [r0, #0x10]

	ldr r2, =return_here
	str r2, [r0, #0x14]			@ save program counter restore
	                                        @ value to backup_sram_storage
	mrs r2, cpsr
	str r2, [r0, #0x18]			@ save cpsr to
	                                        @ backup_sram_storage
	str sp, [r0, #0x1c]			@ save sp to backup_sram_storage

	mov r4, r1				@ Set r4 = cleanL2cache, r1
	                                        @ will be destroyed by
	                                        @ v7_clean_l1_cache_all

	bl context_clean_l1_cache_all		@ Commit all dirty data in L1
	                                        @ cache to L2 without
	                                        @ invalidating
	cmp r4, #0
	blne ux500_clean_l2_cache_all		@ If r4 != FALSE then clean all
	                                        @ dirty data in L2 cache, no
	                                        @ invalidate

	dsb					@ data synchronization barrier
	isb					@ instruction synchronization
	                                        @ barrier
stay_in_wfi:
	wfi					@ wait for interrupt

	mov r0, #0x0
	bl prcmu_stay_in_wfi_check	@ PRCMU Fw sends multiples FIQ
	cmp r0, #0x1				@ to communicate with RomCode,
	beq stay_in_wfi				@ resulting in intempestives
						@ wfi exits. So return to wfi
						@ to do not disturb DS, DI or
						@ Hotplug services

return_here:					@ both cores return here
						@ now we are out deep sleep
	                                        @ with all the context lost
	                                        @ except pc, sp and cpsr

	ldmfd sp!, {r2-r12, pc}			@ restore from stack

