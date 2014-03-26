/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/utils/inc/swap.h>


/*
 * Methods
 */
t_uint16 swap16(t_uint16 x)
{
    return ((x >> 8) |
            ((x << 8) & 0xff00U));
}

#ifdef LINUX

#if defined(__STN_8815) /* __STN_8815 -> ARMv5*/
t_uint32 swap32(t_uint32 x)
{
    asm volatile (
            "EOR      r1, r0, r0, ROR #16           \n\t"
            "BIC      r1, r1, #0xFF0000             \n\t"
            "MOV      r0, r0, ROR #8                \n\t"
            "EOR      r0, r0, r1, LSR #8"
            :  :  : "r3" );

    return x;
}

t_uint64 swap64(t_uint64 x)
{
    asm volatile (
            "MOV      r2, r1                        \n\t"
            "                                       \n\t"
            "EOR      r3, r0, r0, ROR #16           \n\t"
            "BIC      r3, r3, #0xFF0000             \n\t"
            "MOV      r0, r0, ROR #8                \n\t"
            "EOR      r1, r0, r3, LSR #8            \n\t"
            "                                       \n\t"
            "EOR      r3, r2, r2, ROR #16           \n\t"
            "BIC      r3, r3, #0xFF0000             \n\t"
            "MOV      r2, r2, ROR #8                \n\t"
            "EOR      r0, r2, r3, LSR #8"
            :  :  : "r3", "r2" );

    return x;
}
#else /* -> ARMv6 or later */

t_uint32 swap32(t_uint32 x)
{
    asm volatile (
            "REV      %0, %0"
            :  "+r"(x) : );

    return x;
}

t_uint64 swap64(t_uint64 x)
{
    asm volatile (
            "REV      r2, %Q0                \n\t"
            "REV      %Q0, %R0                \n\t"
            "MOV      %R0, r2"
            :  "+&r" (x) : : "r2" );

    return x;
}

#endif

#else /* Symbian, Think -> We assume ARMCC */

#if defined(__thumb__)

t_uint32 swap32(t_uint32 x)
{
    return ((x >> 24) |
            ((x >> 8) & 0xff00U) |
            ((x << 8) & 0xff0000U) |
            ((x << 24) & 0xff000000U));
}

t_uint64 swap64(t_uint64 x)
{
    return ((x >> 56) |
            ((x >> 40) & 0xff00UL) |
            ((x >> 24) & 0xff0000UL) |
            ((x >> 8) & 0xff000000UL) |
            ((x << 8) & 0xff00000000ULL) |
            ((x << 24) & 0xff0000000000ULL) |
            ((x << 40) & 0xff000000000000ULL) |
            ((x << 56)));
}

#elif (__TARGET_ARCH_ARM < 6)

__asm t_uint32 swap32(t_uint32 x)
{
    EOR      r1, r0, r0, ROR #16
    BIC      r1, r1, #0xFF0000
    MOV      r0, r0, ROR #8
    EOR      r0, r0, r1, LSR #8

    BX       lr
}

__asm t_uint64 swap64(t_uint64 x)
{
    MOV      r2, r1

    EOR      r3, r0, r0, ROR #16    // Swap low (r0) and store it in high (r1)
    BIC      r3, r3, #0xFF0000
    MOV      r0, r0, ROR #8
    EOR      r1, r0, r3, LSR #8

    EOR      r3, r2, r2, ROR #16    // Swap high (r2 = ex r1) and store it in low (r0)
    BIC      r3, r3, #0xFF0000
    MOV      r2, r2, ROR #8
    EOR      r0, r2, r3, LSR #8

    BX       lr
}

#else /* -> ARMv6 or later */

__asm t_uint32 swap32(t_uint32 x)
{
    REV      r0, r0

    BX       lr
}

__asm t_uint64 swap64(t_uint64 x)
{
    REV      r2, r0
    REV      r0, r1
    MOV      r1, r2

    BX       lr
}

#endif

#endif

t_uint32 noswap32(t_uint32 x) {
    return x;
}

