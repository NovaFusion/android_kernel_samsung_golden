/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/elf/inc/mmdsp.h>
#include <cm/engine/elf/inc/bfd.h>
#include <cm/engine/elf/inc/mpcal.h>

#include <cm/engine/component/inc/initializer.h>

#include <cm/engine/utils/inc/string.h>
#include <cm/engine/utils/inc/swap.h>
#include <cm/engine/trace/inc/trace.h>

#include <cm/engine/dsp/mmdsp/inc/mmdsp_hwp.h>

static const t_elfmemory mmdspMemories[NUMBER_OF_MMDSP_MEMORY] = {
    {0,  SDRAM_CODE,      SDRAMTEXT_BASE_ADDR,        CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_CODE, 8, 8, "SDRAM_CODE"},  /* 0: Program memory */
    {1,  INTERNAL_XRAM24, 0,                          CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_DATA, 3, 4, "XROM"},        /* 1: Internal X memory */
    {2,  INTERNAL_YRAM24, 0,                          CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_DATA, 3, 4, "YROM"},        /* 2: Y memory */
    {3,  SDRAM_EXT24,     SDRAMMEM24_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_DATA, 3, 4, "SDR0M24"},     /* 5: SDRAM24 */
    {4,  SDRAM_EXT16,     SDRAMMEM16_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_DATA, 3, 2, "SDROM16"},     /* 6: SDRAM16 */
    {5,  ESRAM_EXT24,     ESRAMMEM24_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_DATA, 3, 4, "ESROM24"},     /* 8: ESRAM24 */
    {6,  ESRAM_EXT16,     ESRAMMEM16_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_DATA, 3, 2, "ESROM16"},     /* 9: ESRAM16 */
    {7,  ESRAM_CODE,      ESRAMTEXT_BASE_ADDR,        CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_CODE, 8, 8, "ESRAM_CODE"},  /*10: ESRAM code */
    {8,  INTERNAL_XRAM24, 0,                          CM_MM_ALIGN_2WORDS, MEM_PRIVATE,  MEM_DATA, 3, 4, "XRAM"},        /* 1: Internal X memory */
    {9,  INTERNAL_YRAM24, 0,                          CM_MM_ALIGN_2WORDS, MEM_PRIVATE,  MEM_DATA, 3, 4, "YRAM"},        /* 2: Y memory */
    {10, SDRAM_EXT24,     SDRAMMEM24_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_PRIVATE,  MEM_DATA, 3, 4, "SDRAM24"},     /* 5: SDRAM24 */
    {11, SDRAM_EXT16,     SDRAMMEM16_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_PRIVATE,  MEM_DATA, 3, 2, "SDRAM16"},     /* 6: SDRAM16 */
    {12, ESRAM_EXT24,     ESRAMMEM24_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_PRIVATE,  MEM_DATA, 3, 4, "ESRAM24"},     /* 8: ESRAM24 */
    {13, ESRAM_EXT16,     ESRAMMEM16_BASE_ADDR,       CM_MM_ALIGN_2WORDS, MEM_PRIVATE,  MEM_DATA, 3, 2, "ESRAM16"},     /* 9: ESRAM16 */
    {14, LOCKED_CODE,     SDRAMTEXT_BASE_ADDR,        CM_MM_ALIGN_2WORDS, MEM_SHARABLE, MEM_CODE, 8, 8, "LOCKED_CODE"},  /*  : .locked */
};

#define MAX_ELFSECTIONNAME  10
struct memoryMapping {
    char        *elfSectionName;
    t_uint32    memoryIndex[MEM_FOR_LAST]; // memoryIndex[t_instance_property]
};

static const struct memoryMapping mappingmem0[] = {
        {"mem0.0",  {0,  0}},
        {"mem0.1",  {0,  0}},
        {"mem0.2",  {0,  0}}
};
static const struct memoryMapping mappingmem10 =
        {"mem10",   {7,  7}};
static const struct memoryMapping mappinglocked =
        {".locked", {14,  14}};
static const struct memoryMapping mappingmem1[] = {
        {"",  {0xff,  0xff}},
        {"mem1.1",  {1,  1}},
        {"mem1.2",  {8,  1}},
        {"mem1.3",  {1,  1}},
        {"mem1.4",  {8,  1}},
        {"mem1.stack", {8, 1}}
};
static const struct memoryMapping mappingmem2[] = {
        {"",  {0xff,  0xff}},
        {"mem2.1",  {2,  2}},
        {"mem2.2",  {9,  2}},
        {"mem2.3",  {2,  2}},
        {"mem2.4",  {9,  2}}
};
static const struct memoryMapping mappingmem5[] = {
        {"",  {0xff,  0xff}},
        {"mem5.1",  {3,  3}},
        {"mem5.2",  {10, 3}},
        {"mem5.3",  {3,  3}},
        {"mem5.4",  {10, 3}}
};
static const struct memoryMapping mappingmem6[] = {
        {"",  {0xff,  0xff}},
        {"mem6.1",  {4,  4}},
        {"mem6.2",  {11, 4}},
        {"mem6.3",  {4,  4}},
        {"mem6.4",  {11, 4}}
};
static const struct memoryMapping mappingmem8[] = {
        {"",  {0xff,  0xff}},
        {"mem8.1",  {5,  5}},
        {"mem8.2",  {12, 5}},
        {"mem8.3",  {5,  5}},
        {"mem8.4",  {12, 5}}
};
static const struct memoryMapping mappingmem9[] = {
        {"",  {0xff,  0xff}},
        {"mem9.1",  {6,  6}},
        {"mem9.2",  {13, 6}},
        {"mem9.3",  {6,  6}},
        {"mem9.4",  {13, 6}}
};

static const struct {
    const struct memoryMapping* mapping;
    unsigned int        number;
} hashMappings[10] = {
        {mappingmem0, sizeof(mappingmem0) / sizeof(mappingmem0[0])},
        {mappingmem1, sizeof(mappingmem1) / sizeof(mappingmem1[0])},
        {mappingmem2, sizeof(mappingmem2) / sizeof(mappingmem2[0])},
        {0x0, 0},
        {0x0, 0},
        {mappingmem5, sizeof(mappingmem5) / sizeof(mappingmem5[0])},
        {mappingmem6, sizeof(mappingmem6) / sizeof(mappingmem6[0])},
        {0x0, 0},
        {mappingmem8, sizeof(mappingmem8) / sizeof(mappingmem8[0])},
        {mappingmem9, sizeof(mappingmem9) / sizeof(mappingmem9[0])},
};

const t_elfmemory* MMDSP_getMappingById(t_memory_id memId)
{
    return &mmdspMemories[memId];
}

const t_elfmemory* MMDSP_getMappingByName(const char* sectionName, t_instance_property property)
{
    if(sectionName[0] == 'm' && sectionName[1] == 'e' && sectionName[2] == 'm')
    {
        if(sectionName[4] == '.')
        {
            if(sectionName[5] >= '0' && sectionName[5] <= '9')
            {
                if(sectionName[3] >= '0' && sectionName[3] <= '9')
                {
                    unsigned int m, sm;

                    m = sectionName[3] - '0';
                    sm = sectionName[5] - '0';
                    if(sm < hashMappings[m].number)
                        return &mmdspMemories[hashMappings[m].mapping[sm].memoryIndex[property]];
                }
            } else if(sectionName[3] == '1' && sectionName[5] == 's')
                return &mmdspMemories[mappingmem1[5].memoryIndex[property]];
        }
        else if(sectionName[3] == '1' && sectionName[4] == '0')
            return &mmdspMemories[mappingmem10.memoryIndex[property]];
    }
    else if(sectionName[0] == '.' && sectionName[1] == 'l' && sectionName[2] == 'o' && sectionName[3] == 'c' &&
            sectionName[4] == 'k' && sectionName[5] == 'e' && sectionName[6] == 'd')
    {
        return &mmdspMemories[mappinglocked.memoryIndex[property]];
    }

    return NULL;
}

void MMDSP_serializeMemories(t_instance_property property,
        const t_elfmemory** codeMemory, const t_elfmemory** thisMemory) {
    // Return meory reference
    *codeMemory = &mmdspMemories[0];
    if(property == MEM_FOR_SINGLETON)
    {
        *thisMemory = &mmdspMemories[1];
    }
    else
    {
        *thisMemory = &mmdspMemories[8];
    }
}

void MMDSP_copyCode(t_uint64 * remoteAddr64, const char* origAddr, int nb)
{
    int m;

    // Linux allow unaligned access
#ifdef LINUX
    t_uint64  *origAddr64 = (t_uint64*)origAddr;
#else
    __packed t_uint64  *origAddr64 = (__packed t_uint64*)origAddr;
#endif

    for (m = 0; m < nb; m += 8)
    {
        *remoteAddr64++ = swap64(*origAddr64++);
    }
}

void MMDSP_copyData24(t_uint32 * remoteAddr32, const char* origAddr, int nb)
{
    int m;

    for (m = 0; m < nb; m+=4)
    {
        t_uint32 value1;

        value1  = (*origAddr++ << 16);
        value1 |= (*origAddr++ << 8);
        value1 |= (*origAddr++ << 0);
        *remoteAddr32++ = value1;
    }
}

void MMDSP_copyData16(t_uint16 * remoteAddr16, const char* origAddr, int nb)
{
    int m;

    for (m = 0; m < nb; m+=2)
    {
        t_uint16 value1;

        origAddr++; // Skip this byte (which is put in elf file for historical reason)
        value1 = (*origAddr++ << 8);
        value1 |= (*origAddr++ << 0);
        *remoteAddr16++ = value1;
    }
}

#if 0
__asm void MMDSP_copyCode(void* dst, const void* src, int nb)
{
    PUSH     {r4-r8, lr}
    SUBS     r2,r2,#0x20
    BCC      l4

l5
    SETEND   BE
    LDR      r4, [r1], #0x4
    LDR      r3, [r1], #0x4
    LDR      r6, [r1], #0x4
    LDR      r5, [r1], #0x4
    LDR      r8, [r1], #0x4
    LDR      r7, [r1], #0x4
    LDR      lr, [r1], #0x4
    LDR      r12, [r1], #0x4

    SETEND   LE
    STM      r0!,{r3-r8,r12, lr}
    SUBS     r2,r2,#0x20
    BCS      l5

l4
    LSLS     r12,r2,#28

    SETEND   BE
    LDRCS    r4, [r1], #0x4
    LDRCS    r3, [r1], #0x4
    LDRCS    r6, [r1], #0x4
    LDRCS    r5, [r1], #0x4
    SETEND   LE
    STMCS    r0!,{r3-r6}

    SETEND   BE
    LDRMI    r4, [r1], #0x4
    LDRMI    r3, [r1], #0x4
    SETEND   LE
    STMMI    r0!,{r3-r4}

    POP      {r4-r8, pc}
}
#endif

#ifdef LINUX
static void PLD5(int r)
{
    asm volatile (
	    "PLD      [r0, #0x20]     \n\t"
	    "PLD      [r0, #0x40]     \n\t"
	    "PLD      [r0, #0x60]     \n\t"
	    "PLD      [r0, #0x80]     \n\t"
	    "PLD      [r0, #0xA0]" );
}

static void PLD1(int r)
{
    asm volatile (
	    "PLD      [r0, #0xC0]" );
}
#else /* Symbian, Think -> We assume ARMCC */
static __asm void PLD5(int r)
{
    PLD      [r0, #0x20]
    PLD      [r0, #0x40]
    PLD      [r0, #0x60]
    PLD      [r0, #0x80]
    PLD      [r0, #0xA0]

              bx lr
}

static __asm void PLD1(int r)
{
    PLD      [r0, #0xC0]

              bx lr
}
#endif

#if 0
__asm void COPY(void* dst, const void* src, int nb)
{
    PUSH     {r4-r8, lr}
    SUBS     r2,r2,#0x20
    BCC      l4a
    PLD      [r1, #0x20]
    PLD      [r1, #0x40]
    PLD      [r1, #0x60]
    PLD      [r1, #0x80]
    PLD      [r1, #0xA0]

l5a
    PLD      [r1, #0xC0]
    LDM      r1!,{r3-r8,r12,lr}
    STM      r0!,{r3-r8,r12,lr}
    SUBS     r2,r2,#0x20
    BCS      l5a

l4a
    LSLS     r12,r2,#28
    LDMCS    r1!,{r3,r4,r12,lr}
    STMCS    r0!,{r3,r4,r12,lr}
    LDMMI    r1!,{r3,r4}
    STMMI    r0!,{r3,r4}
    POP      {r4-r8,lr}
    LSLS     r12,r2,#30
    LDRCS    r3,[r1],#4
    STRCS    r3,[r0],#4
    BXEQ     lr
l6b
    LSLS     r2,r2,#31
    LDRHCS   r3,[r1],#2
    LDRBMI   r2,[r1],#1
    STRHCS   r3,[r0],#2
    STRBMI   r2,[r0],#1
    BX       lr
}
#endif


void MMDSP_copySection(t_uint32 origAddr, t_uint32 remoteAddr, t_uint32 sizeInByte) {
    t_uint32 endAddr = remoteAddr + sizeInByte;

    PLD5(origAddr);

    // Align on 32bits
    if((remoteAddr & 0x3) != 0)
    {
        *(t_uint16*)remoteAddr = *(t_uint16*)origAddr;
        remoteAddr += sizeof(t_uint16);
        origAddr += sizeof(t_uint16);
    }

    // Align on 64bits
    if((remoteAddr & 0x7) != 0 && (remoteAddr <= endAddr - sizeof(t_uint32)))
    {
        *(t_uint32*)remoteAddr = *(t_uint32*)origAddr;
        remoteAddr += sizeof(t_uint32);
        origAddr += sizeof(t_uint32);
    }

    // 64bits burst access
    for(; remoteAddr <= endAddr - sizeof(t_uint64); remoteAddr += sizeof(t_uint64), origAddr += sizeof(t_uint64))
    {
        PLD1(origAddr);
        *(volatile t_uint64*)remoteAddr = *(t_uint64*)origAddr;
    }

    // Remain 32bits access
    if(remoteAddr <= endAddr - sizeof(t_uint32))
    {
        *(t_uint32*)remoteAddr = *(t_uint32*)origAddr;
        remoteAddr += sizeof(t_uint32);
        origAddr += sizeof(t_uint32);
    }

    // Remain 16bits access
    if(remoteAddr <= endAddr - sizeof(t_uint16))
        *(t_uint16*)remoteAddr = *(t_uint16*)origAddr;
}


void MMDSP_bzeroSection(t_uint32 remoteAddr, t_uint32 sizeInByte) {
    t_uint32 endAddr = remoteAddr + sizeInByte;

    // Align on 32bits
    if((remoteAddr & 0x3) != 0)
    {
        *(t_uint16*)remoteAddr = 0;
        remoteAddr += sizeof(t_uint16);
    }

    // Align on 64bits
    if((remoteAddr & 0x7) != 0 && (remoteAddr <= endAddr - sizeof(t_uint32)))
    {
        *(t_uint32*)remoteAddr = 0;
        remoteAddr += sizeof(t_uint32);
    }

    // 64bits burst access
    for(; remoteAddr <= endAddr - sizeof(t_uint64); remoteAddr += sizeof(t_uint64))
        *(volatile t_uint64*)remoteAddr = 0ULL;

    // Remain 32bits access
    if(remoteAddr <= endAddr - sizeof(t_uint32))
    {
        *(t_uint32*)remoteAddr = 0;
        remoteAddr += sizeof(t_uint32);
    }

    // Remain 16bits access
    if(remoteAddr <= endAddr - sizeof(t_uint16))
        *(t_uint16*)remoteAddr = 0;
}

void MMDSP_loadedSection(t_nmf_core_id coreId, t_memory_id memId, t_memory_handle handle)
{
    if(mmdspMemories[memId].purpose == MEM_CODE)
    {
        OSAL_CleanDCache(cm_DSP_GetHostLogicalAddress(handle), cm_MM_GetSize(handle));
    }

    if(memId == LOCKED_CODE)
    {
        t_uint32 DspAddress, DspSize;

        cm_DSP_GetDspMemoryHandleSize(handle, &DspSize);
        cm_DSP_GetDspAddress(handle, &DspAddress);

        cm_COMP_InstructionCacheLock(coreId, DspAddress,  DspSize);
    }
}

void MMDSP_unloadedSection(t_nmf_core_id coreId, t_memory_id memId, t_memory_handle handle)
{
    if(memId == LOCKED_CODE)
    {
        t_uint32 DspAddress, DspSize;

        cm_DSP_GetDspMemoryHandleSize(handle, &DspSize);
        cm_DSP_GetDspAddress(handle, &DspAddress);

        cm_COMP_InstructionCacheUnlock(coreId, DspAddress, DspSize);
    }

}

static struct reloc_howto_struct elf64_mmdsp_howto_table[] =
{
  HOWTO (R_MMDSP_IMM20_16,  /* type */
     0,         /* rightshift */
     4,         /* size (0 = byte, 1 = short, 2 = long) */
     16,            /* bitsize */
     FALSE,         /* pc_relative */
     8,         /* bitpos */
     complain_overflow_dont, /* complain_on_overflow */
     0x0, /* special_function */
     "R_MMDSP_IMM20_16",    /* name */
     FALSE,         /* partial_inplace */
     0x0,           /* src_mask */
     0x0000000000ffff00,    /* dst_mask */
     FALSE),        /* pcrel_offset */

    /* A 4-bit absolute relocation for splitted 20 bits immediate, shifted by 56 */

  HOWTO (R_MMDSP_IMM20_4,   /* type */
     16,            /* rightshift */
     4,         /* size (0 = byte, 1 = short, 2 = long) */
     4,         /* bitsize */
     FALSE,         /* pc_relative */
     56,            /* bitpos */
     complain_overflow_dont, /* complain_on_overflow */
     0x0, /* special_function */
     "R_MMDSP_IMM20_4", /* name */
     FALSE,         /* partial_inplace */
     0x0,           /* src_mask */
     0x0f00000000000000LL,  /* dst_mask */
     FALSE),        /* pcrel_offset */

  HOWTO (R_MMDSP_24,           /* type */
        0,             /* rightshift */
        2,             /* size (0 = byte, 1 = short, 2 = long) */
        24,                /* bitsize */
        FALSE,             /* pc_relative */
        0,             /* bitpos */
        complain_overflow_bitfield,    /* complain_on_overflow */
        0x0,     /* special_function */
        "R_MMDSP_24",          /* name */
        FALSE,             /* partial_inplace */
        0x0,            /* src_mask */
        0xffffffff,            /* dst_mask */
        FALSE),            /* pcrel_offset */

  HOWTO (R_MMDSP_IMM16,       /* type */
             0,         /* rightshift */
             4,         /* size (0 = byte, 1 = short, 2 = long) */
             16,            /* bitsize */
             FALSE,         /* pc_relative */
             8,         /* bitpos */
             complain_overflow_bitfield, /* complain_on_overflow */
             0x0, /* special_function */
             "R_MMDSP_IMM16",   /* name */
             FALSE,         /* partial_inplace */
             0x0,               /* src_mask */
             0x0000000000ffff00,    /* dst_mask */
             FALSE),        /* pcrel_offset */
};

static const char* lastInPlaceAddr = 0;
static long long lastInPlaceValue;

void MMDSP_performRelocation(
        t_uint32 type,
        const char* symbol_name,
        t_uint32 symbol_addr,
        char* reloc_addr,
        const char* inPlaceAddr,
        t_uint32 reloc_offset) {
    int i;

    for(i = 0; i < sizeof(elf64_mmdsp_howto_table) / sizeof(elf64_mmdsp_howto_table[0]); i++)
    {
        struct reloc_howto_struct* howto = &elf64_mmdsp_howto_table[i];
        if(howto->type == type)
        {
            t_uint64 relocation;

            LOG_INTERNAL(2, "reloc '%s:0x%x' type %s at 0x%x (0x%x)\n",
                    symbol_name ? symbol_name : "??", symbol_addr,
                    howto->name,
                    reloc_offset, reloc_addr, 0);

            relocation = symbol_addr;

            if (howto->pc_relative) {
                // Not handle yet
            }

            if (howto->complain_on_overflow != complain_overflow_dont) {
                // Not handle yet
            }

            relocation >>= howto->rightshift;

            relocation <<= howto->bitpos;

#define DOIT(x) \
    x = ( (x & ~howto->dst_mask) | (((x & howto->src_mask) +  relocation) & howto->dst_mask))

            switch (howto->size) {
            case 2: {
                long x = *(long*)inPlaceAddr;

               // CM_ASSERT(*(long*)inPlaceAddr == *(long*)reloc_addr);

                DOIT (x);
                *(long*)reloc_addr = x;
            }
            break;
            case 4: {
                long long x;
                if(lastInPlaceAddr == inPlaceAddr)
                {
                    x = lastInPlaceValue;
                }
                else
                {
                   //  CM_ASSERT(*(__packed long long*)inPlaceAddr == *(long long*)reloc_addr);
                    x = *(long long*)inPlaceAddr;
                    lastInPlaceAddr = inPlaceAddr;
                }

                DOIT (x);
                *(long long*)reloc_addr = lastInPlaceValue = x;
            }
            break;
            default:
                CM_ASSERT(0);
            }

            return;
        }
    }

    ERROR("Relocation type %d not supported for '%s'\n", type, symbol_name, 0, 0, 0, 0);
}
