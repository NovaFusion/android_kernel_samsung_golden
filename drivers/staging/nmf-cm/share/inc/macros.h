/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief NMF Macro API.
 */

#ifndef _COMMON_MACROS_H_
#define _COMMON_MACROS_H_

#undef ALIGN_VALUE
#define ALIGN_VALUE(value, alignment) (((value) + (alignment - 1)) & ~(alignment - 1))

#undef MIN
#define MIN(a,b) (((a)>(b))?(b):(a))

#undef MAX
#define MAX(a,b) (((a)<(b))?(b):(a))

/*-----------------------------------------------------------------------------
 * endianess switch macros (32 bits and 16 bits)
 *---------------------------------------------------------------------------*/
#define ENDIANESS_32_SWITCH(value) ( \
        (((value) & MASK_BYTE3) >> SHIFT_BYTE3) | \
        (((value) & MASK_BYTE2) >> SHIFT_BYTE1) | \
        (((value) & MASK_BYTE1) << SHIFT_BYTE1) | \
        (((value) & MASK_BYTE0) << SHIFT_BYTE3)   \
        )

#define ENDIANESS_16_SWITCH(value) ( \
        (((value) & MASK_BYTE0) << SHIFT_BYTE1) | \
        (((value) & MASK_BYTE1) >> SHIFT_BYTE1)   \
        )

/*-----------------------------------------------------------------------------
 * field offset extraction from a structure
 *---------------------------------------------------------------------------*/
#undef FIELD_OFFSET
#define FIELD_OFFSET(typeName, fieldName) ((t_uint32)(&(((typeName *)0)->fieldName)))

#undef MASK_BIT
#define MASK_BIT(n)  				(1UL << ((n) - 1))

/*-----------------------------------------------------------------------------
 * Misc definition
 *---------------------------------------------------------------------------*/

#undef ONE_KB
#define ONE_KB (1024)
#undef ONE_MB
#define ONE_MB (ONE_KB * ONE_KB)

/*-----------------------------------------------------------------------------
 * Bit mask definition
 *---------------------------------------------------------------------------*/
#undef MASK_NULL8
#define MASK_NULL8     0x00U
#undef MASK_NULL16
#define MASK_NULL16    0x0000U
#undef MASK_NULL32
#define MASK_NULL32    0x00000000UL
#undef MASK_ALL8
#define MASK_ALL8      0xFFU
#undef MASK_ALL16
#define MASK_ALL16     0xFFFFU
#undef MASK_ALL32
#define MASK_ALL32     0xFFFFFFFFUL

#undef MASK_BIT0
#define MASK_BIT0      (1UL<<0)
#undef MASK_BIT1
#define MASK_BIT1      (1UL<<1)
#undef MASK_BIT2
#define MASK_BIT2      (1UL<<2)
#undef MASK_BIT3
#define MASK_BIT3      (1UL<<3)
#undef MASK_BIT4
#define MASK_BIT4      (1UL<<4)
#undef MASK_BIT5
#define MASK_BIT5      (1UL<<5)
#undef MASK_BIT6
#define MASK_BIT6      (1UL<<6)
#undef MASK_BIT7
#define MASK_BIT7      (1UL<<7)
#undef MASK_BIT8
#define MASK_BIT8      (1UL<<8)
#undef MASK_BIT9
#define MASK_BIT9      (1UL<<9)
#undef MASK_BIT10
#define MASK_BIT10     (1UL<<10)
#undef MASK_BIT11
#define MASK_BIT11     (1UL<<11)
#undef MASK_BIT12
#define MASK_BIT12     (1UL<<12)
#undef MASK_BIT13
#define MASK_BIT13     (1UL<<13)
#undef MASK_BIT14
#define MASK_BIT14     (1UL<<14)
#undef MASK_BIT15
#define MASK_BIT15     (1UL<<15)
#undef MASK_BIT16
#define MASK_BIT16     (1UL<<16)
#undef MASK_BIT17
#define MASK_BIT17     (1UL<<17)
#undef MASK_BIT18
#define MASK_BIT18     (1UL<<18)
#undef MASK_BIT19
#define MASK_BIT19     (1UL<<19)
#undef MASK_BIT20
#define MASK_BIT20     (1UL<<20)
#undef MASK_BIT21
#define MASK_BIT21     (1UL<<21)
#undef MASK_BIT22
#define MASK_BIT22     (1UL<<22)
#undef MASK_BIT23
#define MASK_BIT23     (1UL<<23)
#undef MASK_BIT24
#define MASK_BIT24     (1UL<<24)
#undef MASK_BIT25
#define MASK_BIT25     (1UL<<25)
#undef MASK_BIT26
#define MASK_BIT26     (1UL<<26)
#undef MASK_BIT27
#define MASK_BIT27     (1UL<<27)
#undef MASK_BIT28
#define MASK_BIT28     (1UL<<28)
#undef MASK_BIT29
#define MASK_BIT29     (1UL<<29)
#undef MASK_BIT30
#define MASK_BIT30     (1UL<<30)
#undef MASK_BIT31
#define MASK_BIT31     (1UL<<31)

/*-----------------------------------------------------------------------------
 * quartet shift definition
 *---------------------------------------------------------------------------*/
#undef MASK_QUARTET
#define MASK_QUARTET     (0xFUL)
#undef SHIFT_QUARTET0
#define SHIFT_QUARTET0   0
#undef SHIFT_QUARTET1
#define SHIFT_QUARTET1   4
#undef SHIFT_QUARTET2
#define SHIFT_QUARTET2   8
#undef SHIFT_QUARTET3
#define SHIFT_QUARTET3   12
#undef SHIFT_QUARTET4
#define SHIFT_QUARTET4   16
#undef SHIFT_QUARTET5
#define SHIFT_QUARTET5   20
#undef SHIFT_QUARTET6
#define SHIFT_QUARTET6   24
#undef SHIFT_QUARTET7
#define SHIFT_QUARTET7   28
#undef MASK_QUARTET0
#define MASK_QUARTET0    (MASK_QUARTET << SHIFT_QUARTET0)
#undef MASK_QUARTET1
#define MASK_QUARTET1    (MASK_QUARTET << SHIFT_QUARTET1)
#undef MASK_QUARTET2
#define MASK_QUARTET2    (MASK_QUARTET << SHIFT_QUARTET2)
#undef MASK_QUARTET3
#define MASK_QUARTET3    (MASK_QUARTET << SHIFT_QUARTET3)
#undef MASK_QUARTET4
#define MASK_QUARTET4    (MASK_QUARTET << SHIFT_QUARTET4)
#undef MASK_QUARTET5
#define MASK_QUARTET5    (MASK_QUARTET << SHIFT_QUARTET5)
#undef MASK_QUARTET6
#define MASK_QUARTET6    (MASK_QUARTET << SHIFT_QUARTET6)
#undef MASK_QUARTET7
#define MASK_QUARTET7    (MASK_QUARTET << SHIFT_QUARTET7)

/*-----------------------------------------------------------------------------
 * Byte shift definition
 *---------------------------------------------------------------------------*/
#undef MASK_BYTE
#define MASK_BYTE       (0xFFUL)
#undef SHIFT_BYTE0
#define SHIFT_BYTE0 	0U
#undef SHIFT_BYTE1
#define SHIFT_BYTE1 	8U
#undef SHIFT_BYTE2
#define SHIFT_BYTE2 	16U
#undef SHIFT_BYTE3
#define SHIFT_BYTE3 	24U
#undef MASK_BYTE0
#define MASK_BYTE0       (MASK_BYTE << SHIFT_BYTE0)
#undef MASK_BYTE1
#define MASK_BYTE1       (MASK_BYTE << SHIFT_BYTE1)
#undef MASK_BYTE2
#define MASK_BYTE2       (MASK_BYTE << SHIFT_BYTE2)
#undef MASK_BYTE3
#define MASK_BYTE3       (MASK_BYTE << SHIFT_BYTE3)

/*-----------------------------------------------------------------------------
 * Halfword shift definition
 *---------------------------------------------------------------------------*/
#undef MASK_HALFWORD
#define MASK_HALFWORD        (0xFFFFUL)
#undef SHIFT_HALFWORD0
#define SHIFT_HALFWORD0 	    0U
#undef SHIFT_HALFWORD1
#define SHIFT_HALFWORD1 	    16U
#undef MASK_HALFWORD0
#define MASK_HALFWORD0       (MASK_HALFWORD << SHIFT_HALFWORD0)
#undef MASK_HALFWORD1
#define MASK_HALFWORD1       (MASK_HALFWORD << SHIFT_HALFWORD1)

#endif /* _COMMON_MACROS_H_ */

