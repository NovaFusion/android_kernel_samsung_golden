/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_MMDSP_HWP_H
#define __INC_MMDSP_HWP_H

#include <cm/inc/cm_type.h>

#define MMDSP_NB_BLOCK_RAM      8
#define MMDSP_RAM_BLOCK_SIZE    4096    /* 0x1000 */
#define MMDSP_NB_TIMER          3
#define MMDSP_NB_BIT_SEM        8
#define MMDSP_NB_DMA_IF         8
#define MMDSP_NB_DMA_CTRL       4
#define MMDSP_NB_ITREMAP_REG    32

#define MMDSP_INSTRUCTION_WORD_SIZE     (sizeof(t_uint64))
#define MMDSP_ICACHE_LINE_SIZE_IN_INST  (4)
#define MMDSP_ICACHE_LINE_SIZE  (MMDSP_ICACHE_LINE_SIZE_IN_INST * MMDSP_INSTRUCTION_WORD_SIZE)

#define MMDSP_DATA_WORD_SIZE     (3)
#define MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE (sizeof(t_uint32))
#define MMDSP_DATA_WORD_SIZE_IN_EXT24 (sizeof(t_uint32))
#define MMDSP_DATA_WORD_SIZE_IN_EXT16 (sizeof(t_uint16))
#define MMDSP_DCACHE_LINE_SIZE_IN_WORDS  (8)
#define MMDSP_DCACHE_LINE_SIZE  (MMDSP_DCACHE_LINE_SIZE_IN_WORDS * sizeof(t_uint32))

#define MMDSP_NB_IO             16

#define MMDSP_CODE_CACHE_WAY_SIZE   256

//#define MMDSP_ESRAM_DSP_BASE_ADDR         0xE0000 /* 64-bit words */
//#define MMDSP_DATA24_DSP_BASE_ADDR        0x10000
//#define MMDSP_DATA16_DSP_BASE_ADDR        0x800000
//#define MMDSP_MMIO_DSP_BASE_ADDR          0xF80000

/* Specified according MMDSP & ELF convention */
/* Note: Here we assume that ESRAM is less than 2MB */
#define SDRAMTEXT_BASE_ADDR             0x00000000
#define ESRAMTEXT_BASE_ADDR             0x000E0000

#define SDRAMMEM24_BASE_ADDR            0x00010000
#define ESRAMMEM24_BASE_ADDR            0x00600000 /* ELF == 0x00400000 TODO: Update it in MMDSP ELF compiler */
#define SDRAMMEM16_BASE_ADDR            0x00800000
#define ESRAMMEM16_BASE_ADDR            0x00D80000 /* ELF == 0x00BC0000 TODO: Update it in MMDSP ELF compiler */

#define MMIO_BASE_ADDR                  0x00F80000

/*
 * Definition of indirect host registers
 */
#define IHOST_ICACHE_FLUSH_REG                            0x0
#define IHOST_ICACHE_FLUSH_CMD_ENABLE                     (t_uint64)MASK_BIT0
#define IHOST_ICACHE_FLUSH_ALL_ENTRIES_CMD                (t_uint64)0x0
#if 0
#define IHOST_ICACHE_INVALID_ALL_UNLOCKED_L2_LINES_CMD    (t_uint64)0x8
#define IHOST_ICACHE_INVALID_ALL_LOCKED_L2_LINES_CMD      (t_uint64)0xA
#define IHOST_ICACHE_UNLOCK_ALL_LOCKED_L2_LINES_CMD       (t_uint64)0xC
#define IHOST_ICACHE_LOCK_ALL_WAYS_LESSER_THAN_LOCK_V_CMD (t_uint64)0xE
#else
#define IHOST_ICACHE_INVALID_ALL_UNLOCKED_L2_LINES_CMD    (t_uint64)0x10
#define IHOST_ICACHE_INVALID_ALL_LOCKED_L2_LINES_CMD      (t_uint64)0x12
#define IHOST_ICACHE_UNLOCK_ALL_LOCKED_L2_LINES_CMD       (t_uint64)0x14
#define IHOST_ICACHE_LOCK_ALL_WAYS_LESSER_THAN_LOCK_V_CMD (t_uint64)0x16
#define IHOST_ICACHE_FLUSH_BY_SERVICE                     (t_uint64)0x18
#define IHOST_ICACHE_FLUSH_OUTSIDE_RANGE                  (t_uint64)0x1A
#endif

#define IHOST_ICACHE_LOCK_V_REG                           0x1

#define IHOST_ICACHE_MODE_REG                             0x2
#define IHOST_ICACHE_MODE_PERFMETER_ON                    (t_uint64)MASK_BIT0
#define IHOST_ICACHE_MODE_PERFMETER_OFF                   (t_uint64)0x0
#define IHOST_ICACHE_MODE_L2_CACHE_ON                     (t_uint64)MASK_BIT1
#define IHOST_ICACHE_MODE_L2_CACHE_OFF                    (t_uint64)0x0
#define IHOST_ICACHE_MODE_L1_CACHE_ON                     (t_uint64)MASK_BIT2
#define IHOST_ICACHE_MODE_L1_CACHE_OFF                    (t_uint64)0x0
#define IHOST_ICACHE_MODE_FILL_MODE_ON                    (t_uint64)MASK_BIT3
#define IHOST_ICACHE_MODE_FILL_MODE_OFF                   (t_uint64)0x0

#define IHOST_CLEAR_PERFMETER_REG                         0x3
#define IHOST_CLEAR_PERFMETER_ON                          (t_uint64)0x1
#define IHOST_CLEAR_PERFMETER_OFF                         (t_uint64)0x0

#define IHOST_PERF_HIT_STATUS_REG                         0x4

#define IHOST_PERF_MISS_STATUS_REG                        0x5

#define IHOST_FILL_START_WAY_REG                          0x6
#define IHOST_FILL_START_ADDR_VALUE_SHIFT                 0U
#define IHOST_FILL_WAY_NUMBER_SHIFT                       20U

#define IHOST_PRG_BASE_ADDR_REG                           0x7
#define IHOST_PRG_BASE1_ADDR_SHIFT                        0
#define IHOST_PRG_BASE2_ADDR_SHIFT                        32

#if defined(__STN_8500) && (__STN_8500>10)
#define IHOST_PRG_BASE_34_ADDR_REG                        0x1A
#define IHOST_PRG_BASE3_ADDR_SHIFT                        0
#define IHOST_PRG_BASE4_ADDR_SHIFT                        32
#endif

#if defined(__STN_8815) /* __STN_8815 */
#define IHOST_PRG_AHB_CONF_REG                            0x8
#define IHOST_PRG_AHB_LOCKED_SHIFT                        0U
#define IHOST_PRG_AHB_PROT_SHIFT                          1U

#define AHB_LOCKED_ON                                   (t_uint64)1
#define AHB_LOCKED_OFF                                  (t_uint64)0

#define AHB_PROT_USER                                   (t_uint64)0
#define AHB_PROT_PRIVILEGED                             (t_uint64)MASK_BIT0
#define AHB_PROT_NONBUFFERABLE                          (t_uint64)0
#define AHB_PROT_BUFFERABLE                             (t_uint64)MASK_BIT1
#define AHB_PROT_NONCACHEABLE                           (t_uint64)0
#define AHB_PROT_CACHEABLE                              (t_uint64)MASK_BIT2


#define IHOST_DATA_AHB_CONF_REG                           0x9
#define IHOST_DATA_AHB_LOCKED_SHIFT                       0U
#define IHOST_DATA_AHB_PROT_SHIFT                         1U
#else /* def __STN_8820 or __STN_8500 */
#define IHOST_STBUS_ID_CONF_REG                           0x8
#define SAA_STBUS_ID                                      176 /* = 0xB0 */
#define SVA_STBUS_ID                                      4   /* = 0x4  */
#define SIA_STBUS_ID                                      180 /* = 0xB4 */

#define IHOST_STBUF_CONF_REG                              0x9  /* RESERVED */
#endif /* __STN_8820 or __STN_8500 */

#define IHOST_DATA_EXT_BUS_BASE_REG                       0xA
#define IHOST_DATA_EXT_BUS_BASE_16_SHIFT                  32ULL
#define IHOST_DATA_EXT_BUS_BASE_24_SHIFT                  0ULL

#define IHOST_EXT_MMIO_BASE_DATA_EXT_BUS_TOP_REG          0xB
#define IHOST_EXT_MMIO_DATA_EXT_BUS_TOP_SHIFT             0ULL
#define IHOST_EXT_MMIO_BASE_ADDR_SHIFT                    32ULL

#define IHOST_DATA_EXT_BUS_BASE2_REG                      0xC
#define IHOST_DATA_EXT_BUS_BASE2_16_SHIFT                 32ULL
#define IHOST_DATA_EXT_BUS_BASE2_24_SHIFT                 0ULL

#if defined(__STN_8500) && (__STN_8500>10)

#define IHOST_DATA_EXT_BUS_BASE3_REG                      0x1B
#define IHOST_DATA_EXT_BUS_BASE3_16_SHIFT                 32ULL
#define IHOST_DATA_EXT_BUS_BASE3_24_SHIFT                 0ULL

#define IHOST_DATA_EXT_BUS_BASE4_REG                      0x1C
#define IHOST_DATA_EXT_BUS_BASE4_16_SHIFT                 32ULL
#define IHOST_DATA_EXT_BUS_BASE4_24_SHIFT                 0ULL

#endif

#define IHOST_ICACHE_STATE_REG                            0xD
#define IHOST_ICACHE_STATE_RESET                          0x0
#define IHOST_ICACHE_STATE_INITAGL2                       0x1
#define IHOST_ICACHE_STATE_READY_TO_START                 0x2
#define IHOST_ICACHE_STATE_WAIT_FOR_MISS                  0x3
#define IHOST_ICACHE_STATE_FILLDATARAM0                   0x4
#define IHOST_ICACHE_STATE_FILLDATARAM1                   0x5
#define IHOST_ICACHE_STATE_FILLDATARAM2                   0x6
#define IHOST_ICACHE_STATE_FILLDATARAM3                   0x7
#define IHOST_ICACHE_STATE_FLUSH                          0x8
#define IHOST_ICACHE_STATE_FILL_INIT                      0x9
#define IHOST_ICACHE_STATE_FILL_LOOP                      0xA
#define IHOST_ICACHE_STATE_FILL_LOOP0                     0xB
#define IHOST_ICACHE_STATE_FILL_LOOP1                     0xC
#define IHOST_ICACHE_STATE_FILL_LOOP2                     0xD
#define IHOST_ICACHE_STATE_FILL_LOOP3                     0xE
#define IHOST_ICACHE_STATE_FILL_END                       0xF
#define IHOST_ICACHE_STATE_SPECIFIC_FLUSH_R               0x10
#define IHOST_ICACHE_STATE_SPECIFIC_FLUSH_W               0x11
#define IHOST_ICACHE_STATE_SPECIFIC_FLUSH_END             0x12
#define IHOST_ICACHE_STATE_OTHERS                         0x1F

#define IHOST_EN_EXT_BUS_TIMEOUT_REG                      0xE
#define IHOST_TIMEOUT_ENABLE                              1ULL
#define IHOST_TIMEOUT_DISABLE                             0ULL

#define IHOST_DATA2_1624_XA_BASE_REG                      0xF
#define IHOST_DATA2_24_XA_BASE_SHIFT                      0ULL
#define IHOST_DATA2_16_XA_BASE_SHIFT                      32ULL
#if defined(__STN_8500) && (__STN_8500>10)
#define IHOST_DATA3_24_XA_BASE_SHIFT                      8ULL
#define IHOST_DATA3_16_XA_BASE_SHIFT                      40ULL
#define IHOST_DATA4_24_XA_BASE_SHIFT                      16ULL
#define IHOST_DATA4_16_XA_BASE_SHIFT                      48ULL
#endif

#define IHOST_PERFMETERS_MODE_REG                         0x10

#if defined(__STN_8815) /* __STN_8815 */
#define IHOST_EXT_MMIO_AHB_CONF_REG                       0x11
#define IHOST_EXT_MMIO_AHB_LOCKED_SHIFT                   0U
#define IHOST_EXT_MMIO_AHB_PROT_SHIFT                     1U
#else /* def __STN_8820 or __STN_8500 */
#define IHOST_EXT_MMIO_STBS_CONF_REG                      0x11 /* RESERVED */
#endif /* __STN_8820 or __STN_8500 */

#define IHOST_PRG_BASE_SEL_REG                            0x12
#define IHOST_PRG_BASE_SEL_OFF                            (t_uint64)0
#define IHOST_PRG_BASE_SEL_ON                             (t_uint64)1

#define IHOST_PRG_BASE2_ACTIV_REG                         0x13
#define IHOST_PRG_BASE2_ACTIV_OFF                         (t_uint64)0
#if defined(__STN_8500) && (__STN_8500>10)
/* TODO : for the moment just divide mmdsp in fix 4 spaces */
    #define IHOST_PRG_BASE2_ACTIV_ON                      (t_uint64)((((t_uint64)0xf0000>>10)<<48) | (((t_uint64)0xe0000>>10)<<32) | (((t_uint64)0x70000>>10)<<16) | 1)
#else
    #define IHOST_PRG_BASE2_ACTIV_ON                      (t_uint64)1
#endif

#define IHOST_DATA_EXT_BUS_TOP_16_24_REG                      0x14
#define IHOST_DATA_EXT_BUS_TOP_24_SHIFT                       0ULL
#define IHOST_DATA_EXT_BUS_TOP_16_SHIFT                       32ULL

#define IHOST_DATA_TOP_16_24_CHK_REG                      0x16
#define IHOST_DATA_TOP_16_24_CHK_OFF                      (t_uint64)0
#define IHOST_DATA_TOP_16_24_CHK_ON                       (t_uint64)1

#define IHOST_EXT_BUS_TOP2_16_24_REG                      0x15
#define IHOST_DATA_EXT_BUS_TOP2_24_SHIFT                  0ULL
#define IHOST_DATA_EXT_BUS_TOP2_16_SHIFT                  32ULL

#if defined(__STN_8500) && (__STN_8500>10)

#define IHOST_EXT_BUS_TOP3_16_24_REG                      0x1D
#define IHOST_DATA_EXT_BUS_TOP3_24_SHIFT                  0ULL
#define IHOST_DATA_EXT_BUS_TOP3_16_SHIFT                  32ULL

#define IHOST_EXT_BUS_TOP4_16_24_REG                      0x1E
#define IHOST_DATA_EXT_BUS_TOP4_24_SHIFT                  0ULL
#define IHOST_DATA_EXT_BUS_TOP4_16_SHIFT                  32ULL

#endif

#define IHOST_DATA_BASE2_ACTIV_REG                        0x17
#define IHOST_DATA_BASE2_ACTIV_OFF                        (t_uint64)0
#define IHOST_DATA_BASE2_ACTIV_ON                         (t_uint64)1

#define IHOST_INST_BURST_SZ_REG                           0x18
#define IHOST_INST_BURST_SZ_ALWAYS_1_LINE                 (t_uint64)0x0
#define IHOST_INST_BURST_SZ_ALWAYS_2_LINES                (t_uint64)0x1
#define IHOST_INST_BURST_SZ_AUTO                          (t_uint64)0x2  /* 2 lines for SDRAM [0, 0xE0000[, 1 line for ESRAM [0xE0000, 0xFFFFF] */

#define IHOST_ICACHE_END_CLEAR_REG                        0x19
#define IHOST_ICACHE_START_CLEAR_REG                      IHOST_FILL_START_WAY_REG

/*
 * Definition of value of the ucmd register
 */
#define MMDSP_UCMD_WRITE                  0
#define MMDSP_UCMD_READ                   4
#define MMDSP_UCMD_CTRL_STATUS_ACCESS     0x10 // (MASK_BIT4 | !MASK_BIT3 | !MASK_BIT0)
#define MMDSP_UCMD_DECREMENT_ADDR         MASK_BIT5
#define MMDSP_UCMD_INCREMENT_ADDR         MASK_BIT1

/*
 * Definition of value of the ubkcmd register
 */
#define MMDSP_UBKCMD_EXT_CODE_MEM_ACCESS_ENABLE    MASK_BIT3
#define MMDSP_UBKCMD_EXT_CODE_MEM_ACCESS_DISABLE   0

/*
 * Definition of value of the clockcmd register
 */
#define MMDSP_CLOCKCMD_STOP_CLOCK         MASK_BIT0
#define MMDSP_CLOCKCMD_START_CLOCK        0

/*
 * Definition of macros used to access indirect addressed host register
 */
#define WRITE_INDIRECT_HOST_REG(pRegs, addr, value64) \
{ \
    (pRegs)->host_reg.emul_uaddrl = addr; \
    (pRegs)->host_reg.emul_uaddrm = 0;    \
    (pRegs)->host_reg.emul_uaddrh = 0;    \
    (pRegs)->host_reg.emul_udata[0] = ((value64 >> 0ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[1] = ((value64 >> 8ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[2] = ((value64 >> 16ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[3] = ((value64 >> 24ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[4] = ((value64 >> 32ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[5] = ((value64 >> 40ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[6] = ((value64 >> 48ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_udata[7] = ((value64 >> 56ULL) & MASK_BYTE0);  \
    (pRegs)->host_reg.emul_ucmd = (MMDSP_UCMD_CTRL_STATUS_ACCESS | MMDSP_UCMD_WRITE); \
}

#define READ_INDIRECT_HOST_REG(pRegs, addr, value64) \
{ \
    (pRegs)->host_reg.emul_udata[0] = 0;  \
    (pRegs)->host_reg.emul_udata[1] = 0;  \
    (pRegs)->host_reg.emul_udata[2] = 0;  \
    (pRegs)->host_reg.emul_udata[3] = 0;  \
    (pRegs)->host_reg.emul_udata[4] = 0;  \
    (pRegs)->host_reg.emul_udata[5] = 0;  \
    (pRegs)->host_reg.emul_udata[6] = 0;  \
    (pRegs)->host_reg.emul_udata[7] = 0;  \
    (pRegs)->host_reg.emul_uaddrl = addr; \
    (pRegs)->host_reg.emul_uaddrm = 0;    \
    (pRegs)->host_reg.emul_uaddrh = 0;    \
    (pRegs)->host_reg.emul_ucmd = (MMDSP_UCMD_CTRL_STATUS_ACCESS | MMDSP_UCMD_READ); \
    value64 = (((t_uint64)((pRegs)->host_reg.emul_udata[0])) << 0ULL) |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[1])) << 8ULL)  |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[2])) << 16ULL) |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[3])) << 24ULL) |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[4])) << 32ULL) |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[5])) << 40ULL) |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[6])) << 48ULL) |  \
        (((t_uint64)((pRegs)->host_reg.emul_udata[7])) << 56ULL); \
}

/* Common type to handle 64-bit modulo field in 32-bit mode */
typedef struct {
    t_uint32 value;
    t_uint32 dummy;
} t_mmdsp_field_32;

typedef struct {
    t_uint16 value;
    t_uint16 dummy;
} t_mmdsp_field_16;

/* DCache registers */
#define DCACHE_MODE_ENABLE              MASK_BIT0
#define DCACHE_MODE_DISABLE             0
#define DCACHE_MODE_DIVIDE_PER_2        MASK_BIT1
#define DCACHE_MODE_DIVIDE_PER_4        MASK_BIT2
#define DCACHE_MODE_CHECK_TAG_ENABLE    MASK_BIT3
#define DCACHE_MODE_CHECK_TAG_DISABLE   0
#define DCACHE_MODE_FORCE_LOCK_MODE     MASK_BIT4
#define DCACHE_MODE_LOCK_BIT            MASK_BIT5

#define DCACHE_CONTROL_PREFETCH_LINE            MASK_BIT0
#define DCACHE_CONTROL_NON_BLOCKING_REFILL      0
#define DCACHE_CONTROL_FAST_READ_DISABLE        MASK_BIT1
#define DCACHE_CONTROL_FAST_READ_ENABLE         0
#define DCACHE_CONTROL_ON_FLY_FILL_ACCESS_OFF   MASK_BIT2
#define DCACHE_CONTROL_ON_FLY_FILL_ACCESS_ON    0
#define DCACHE_CONTROL_BURST_1_WRAP8            MASK_BIT3
#define DCACHE_CONTROL_BURST_2_WRAP4            0
#define DCACHE_CONTROL_NOT_USE_DATA_BUFFER      MASK_BIT4
#define DCACHE_CONTROL_USE_DATA_BUFFER          0
#define DCACHE_CONTROL_WRITE_POSTING_ENABLE     MASK_BIT5
#define DCACHE_CONTROL_WRITE_POSTING_DISABLE    0

#define DCACHE_CMD_NOP                          0
#define DCACHE_CMD_DISCARD_WAY                  2 //see Dcache_way reg
#define DCACHE_CMD_DISCARD_LINE                 3 //see Dcache_line reg
#define DCACHE_CMD_FREE_WAY                     4 //see Dcache_way reg
#define DCACHE_CMD_FREE_LINE                    5 //see Dchache_line reg
#define DCACHE_CMD_FLUSH                        7

#define DCACHE_STATUS_CURRENT_WAY_MASK          (MASK_BIT2 | MASK_BIT1 | MASK_BIT0)
#define DCACHE_STATUS_TAG_HIT_MASK              MASK_BIT3
#define DCACHE_STATUS_TAG_LOCKED_MASK           MASK_BIT4
#define DCACHE_STATUS_PROTECTION_ERROR_MASK     MASK_BIT5

#define DCACHE_CPTRSEL_COUNTER_1_MASK           (MASK_BIT3 | MASK_BIT2 | MASK_BIT1 | MASK_BIT0)
#define DCACHE_CPTRSEL_COUNTER_1_SHIFT          0
#define DCACHE_CPTRSEL_COUNTER_2_MASK           (MASK_BIT7 | MASK_BIT6 | MASK_BIT5 | MASK_BIT4)
#define DCACHE_CPTRSEL_COUNTER_2_SHIFT          4
#define DCACHE_CPTRSEL_COUNTER_3_MASK           (MASK_BIT11 | MASK_BIT10 | MASK_BIT9 | MASK_BIT8)
#define DCACHE_CPTRSEL_COUNTER_3_SHIFT          8
#define DCACHE_CPTRSEL_XBUS_ACCESS_TO_CACHE_RAM 1
#define DCACHE_CPTRSEL_CACHE_HIT                2
#define DCACHE_CPTRSEL_LINE_MATCH               3
#define DCACHE_CPTRSEL_XBUS_WS                  4
#define DCACHE_CPTRSEL_EXTMEM_WS                5
#define DCACHE_CPTRSEL_CACHE_READ               6
#define DCACHE_CPTRSEL_CACHE_WRITE              7
#define DCACHE_CPTRSEL_TAG_HIT_READ             8
#define DCACHE_CPTRSEL_TAG_LOCKED_ACCESS        9
#define DCACHE_CPTRSEL_TAG_MEM_READ_CYCLE       10
#define DCACHE_CPTRSEL_TAG_MEM_WRITE_CYCLE      11


typedef volatile struct {
    t_uint16 padding_1[5];
    t_uint16 mode;
    t_uint16 control;
    t_uint16 way;
    t_uint16 line;
    t_uint16 command;
    t_uint16 status;
    t_uint16 cptr1l;
    t_uint16 cptr1h;
    t_uint16 cptr2l;
    t_uint16 cptr2h;
    t_uint16 cptr3l;
    t_uint16 cptr3h;
    t_uint16 cptrsel;
    t_uint16 flush_base_lsb; /* only on STn8820 and STn8500 */
    t_uint16 flush_base_msb; /* only on STn8820 and STn8500 */
    t_uint16 flush_top_lsb; /* only on STn8820 and STn8500 */
    t_uint16 flush_top_msb; /* only on STn8820 and STn8500 */
    t_uint16 padding_2[10];
} t_mmdsp_dcache_regs_16;

typedef volatile struct {
    t_uint32 padding_1[5];
    t_uint32 mode;
    t_uint32 control;
    t_uint32 way;
    t_uint32 line;
    t_uint32 command;
    t_uint32 status;
    t_uint32 cptr1l;
    t_uint32 cptr1h;
    t_uint32 cptr2l;
    t_uint32 cptr2h;
    t_uint32 cptr3l;
    t_uint32 cptr3h;
    t_uint32 cptrsel;
    t_uint32 flush_base_lsb; /* only on STn8820 and STn8500 */
    t_uint32 flush_base_msb; /* only on STn8820 and STn8500 */
    t_uint32 flush_top_lsb; /* only on STn8820 and STn8500 */
    t_uint32 flush_top_msb; /* only on STn8820 and STn8500 */
    t_uint32 padding_2[10];
} t_mmdsp_dcache_regs_32;

/* TIMER Registers */
typedef volatile struct {
    t_mmdsp_field_16    timer_msb;
    t_mmdsp_field_16    timer_lsb;
} t_mmdsp_timer_regs_16;

typedef volatile struct {
    t_mmdsp_field_32    timer_msb;
    t_mmdsp_field_32    timer_lsb;
} t_mmdsp_timer_regs_32;


/* DMA interface Registers */
typedef volatile struct {
    t_uint16    arm_dma_sreq;        /* dma0: 5e800, dma1: +0x20 ...*/
    t_uint16    arm_dma_breq;        /* ... 5e802 */
    t_uint16    arm_dma_lsreq;       /* ... 5e804 */
    t_uint16    arm_dma_lbreq;
    t_uint16    arm_dma_maskit;
    t_uint16    arm_dma_it;
    t_uint16    arm_dma_auto;
    t_uint16    arm_dma_lauto;
    t_uint16    dma_reserved[8];
} t_mmdsp_dma_if_regs_16;

typedef volatile struct {
    t_uint32    arm_dma_sreq;        /* dma0: 3a800, dma1: +0x40 ...*/
    t_uint32    arm_dma_breq;        /* ... 3a804 */
    t_uint32    arm_dma_lsreq;       /* ... 3a808 */
    t_uint32    arm_dma_lbreq;
    t_uint32    arm_dma_maskit;
    t_uint32    arm_dma_it;
    t_uint32    arm_dma_auto;
    t_uint32    arm_dma_lauto;
    t_uint32    dma_reserved[8];
} t_mmdsp_dma_if_regs_32;

/* MMDSP DMA controller Registers */
typedef volatile struct {
    t_uint16    dma_ctrl;            /* dma0: 0x5d400, dma1: +0x10 ... */
    t_uint16    dma_int_base;        /* ... 0x5d402 */
    t_uint16    dma_int_length;      /* ... 0x5d404 */
    t_uint16    dma_ext_baseh;
    t_uint16    dma_ext_basel;
    t_uint16    dma_count;
    t_uint16    dma_ext_length;
    t_uint16    dma_it_status;
} t_mmdsp_dma_ctrl_regs_16;

typedef volatile struct {
    t_uint32    dma_ctrl;            /* dma0: 0x3a800, dma1: +0x20 ... */
    t_uint32    dma_int_base;        /* ... 0x3a804 */
    t_uint32    dma_int_length;      /* ... 0x3a808 */
    t_uint32    dma_ext_baseh;
    t_uint32    dma_ext_basel;
    t_uint32    dma_count;
    t_uint32    dma_ext_length;
    t_uint32    dma_it_status;
} t_mmdsp_dma_ctrl_regs_32;

/* IO registers */
typedef volatile struct {
    t_mmdsp_field_16 io_bit[MMDSP_NB_IO];
    t_mmdsp_field_16 io_lsb;
    t_mmdsp_field_16 io_msb;
    t_mmdsp_field_16 io_all;
    t_mmdsp_field_16 io_en;
} t_mmdsp_io_regs_16;

typedef volatile struct {
    t_mmdsp_field_32 io_bit[MMDSP_NB_IO];
    t_mmdsp_field_32 io_lsb;
    t_mmdsp_field_32 io_msb;
    t_mmdsp_field_32 io_all;
    t_mmdsp_field_32 io_en;
} t_mmdsp_io_regs_32;

/* HOST Registers bit mapping */
#define HOST_GATEDCLK_ITREMAP   MASK_BIT0
#define HOST_GATEDCLK_SYSDMA     MASK_BIT1
#define HOST_GATEDCLK_INTEG_REGS MASK_BIT2
#define HOST_GATEDCLK_TIMER_GPIO MASK_BIT3
#define HOST_GATEDCLK_XBUSDMA   MASK_BIT4
#define HOST_GATEDCLK_STACKCTRL MASK_BIT5
#define HOST_GATEDCLK_ITC       MASK_BIT6

/* Only for STn8820 and STn8500 */
#define HOST_PWR_DBG_MODE       MASK_BIT0
#define HOST_PWR_DC_STATUS      (MASK_BIT1 | MASK_BIT2 | MASK_BIT3 | MASK_BIT4 | MASK_BIT5)
#define HOST_PWR_DE_STATUS      MASK_BIT6
#define HOST_PWR_STOV_STATUS    MASK_BIT7

/* HOST Registers */
typedef volatile struct {
    t_uint16    ident;               /*0x...60000*/
    t_uint16    identx[4];           /*0x...60002..8*/
    t_uint16    r5;                  /*0x...6000a*/
    t_uint16    r6;                  /*0x...6000c*/
    t_uint16    inte[2];             /*0x...6000e..10*/
    t_uint16    intx[2];             /*0x...60012..14*/
    t_uint16    int_ris[2];          /*0x...60016..18*/
    t_uint16    intpol;              /*0x...6001a*/
    t_uint16    pwr;                 /*0x...6001c*/ /* only on STn8820 and STn8500 */
    t_uint16    gatedclk;            /*0x...6001e*/
    t_uint16    softreset;           /*0x...60020*/
    t_uint16    int_icr[2];          /*0x...60022..24*/
    t_uint16    cmd[4];              /*0x...60026..2c*/
    t_uint16    RESERVED4;
    t_uint16    int_mis0;            /*0x...60030*/
    t_uint16    RESERVED5;
    t_uint16    RESERVED6;
    t_uint16    RESERVED7;
    t_uint16    i2cdiv;              /*0x...60038*/
    t_uint16    int_mis1;            /*0x...6003a*/
    t_uint16    RESERVED8;
    t_uint16    RESERVED9;
    t_uint16    emul_udata[8];      /*0x...60040..4e*/
    t_uint16    emul_uaddrl;        /*0x...60050*/
    t_uint16    emul_uaddrm;        /*0x...60052*/
    t_uint16    emul_ucmd;          /*0x...60054*/
    t_uint16    emul_ubkcmd;        /*0x...60056*/
    t_uint16    emul_bk2addl;       /*0x...60058*/
    t_uint16    emul_bk2addm;       /*0x...6005a*/
    t_uint16    emul_bk2addh;       /*0x...6005c*/
    t_uint16    emul_mdata[3];      /*0x...6005e..62*/
    t_uint16    emul_maddl;         /*0x...60064*/
    t_uint16    emul_maddm;         /*0x...60066*/
    t_uint16    emul_mcmd;          /*0x...60068*/
    t_uint16    emul_maddh;         /*0x...6006a*/
    t_uint16    emul_uaddrh;        /*0x...6006c*/
    t_uint16    emul_bk_eql;        /*0x...6006e*/
    t_uint16    emul_bk_eqh;        /*0x...60070*/
    t_uint16    emul_bk_combi;      /*0x...60072*/
    t_uint16    emul_clockcmd;      /*0x...60074*/
    t_uint16    emul_stepcmd;       /*0x...60076*/
    t_uint16    emul_scanreg;       /*0x...60078*/
    t_uint16    emul_breakcountl;   /*0x...6007a*/
    t_uint16    emul_breakcounth;   /*0x...6007c*/
    t_uint16    emul_forcescan;     /*0x...6007e*/
    t_uint16    user_area[(0x200 - 0x80)>>1];
} t_mmdsp_host_regs_16;

typedef volatile struct {
    t_uint32    ident;               /*0x...60000*/
    t_uint32    identx[4];           /*0x...60004..10*/
    t_uint32    r5;                  /*0x...60014*/
    t_uint32    r6;                  /*0x...60018*/
    t_uint32    inte[2];             /*0x...6001c..20*/
    t_uint32    intx[2];             /*0x...60024..28*/
    t_uint32    int_ris[2];          /*0x...6002c..30*/
    t_uint32    intpol;              /*0x...60034*/
    t_uint32    pwr;                 /*0x...60038*/ /* only on STn8820 and STn8500 */
    t_uint32    gatedclk;            /*0x...6003c*/
    t_uint32    softreset;           /*0x...60040*/
    t_uint32    int_icr[2];          /*0x...60044..48*/
    t_uint32    cmd[4];              /*0x...6004c..58*/
    t_uint32    RESERVED4;
    t_uint32    int_mis0;            /*0x...60060*/
    t_uint32    RESERVED5;
    t_uint32    RESERVED6;
    t_uint32    RESERVED7;
    t_uint32    i2cdiv;              /*0x...60070*/
    t_uint32    int_mis1;            /*0x...60074*/
    t_uint32    RESERVED8;
    t_uint32    RESERVED9;
    t_uint32    emul_udata[8];      /*0x...60080..9c*/
    t_uint32    emul_uaddrl;        /*0x...600a0*/
    t_uint32    emul_uaddrm;        /*0x...600a4*/
    t_uint32    emul_ucmd;          /*0x...600a8*/
    t_uint32    emul_ubkcmd;        /*0x...600ac*/
    t_uint32    emul_bk2addl;       /*0x...600b0*/
    t_uint32    emul_bk2addm;       /*0x...600b4*/
    t_uint32    emul_bk2addh;       /*0x...600b8*/
    t_uint32    emul_mdata[3];      /*0x...600bc..c4*/
    t_uint32    emul_maddl;         /*0x...600c8*/
    t_uint32    emul_maddm;         /*0x...600cc*/
    t_uint32    emul_mcmd;          /*0x...600d0*/
    t_uint32    emul_maddh;         /*0x...600d4*/
    t_uint32    emul_uaddrh;        /*0x...600d8*/
    t_uint32    emul_bk_eql;        /*0x...600dc*/
    t_uint32    emul_bk_eqh;        /*0x...600e0*/
    t_uint32    emul_bk_combi;      /*0x...600e4*/
    t_uint32    emul_clockcmd;      /*0x...600e8*/
    t_uint32    emul_stepcmd;       /*0x...600ec*/
    t_uint32    emul_scanreg;       /*0x...600f0*/
    t_uint32    emul_breakcountl;   /*0x...600f4*/
    t_uint32    emul_breakcounth;   /*0x...600f8*/
    t_uint32    emul_forcescan;     /*0x...600fc*/
    t_uint32    user_area[(0x400 - 0x100)>>2];
} t_mmdsp_host_regs_32;

/* MMIO blocks */
#if defined(__STN_8820) || defined(__STN_8500)
typedef volatile struct {
    t_uint16 RESERVED1[(0xD400-0x8000)>>1];

    t_mmdsp_dma_ctrl_regs_16 dma_ctrl[MMDSP_NB_DMA_CTRL];

    t_uint16 RESERVED2[(0xD800-0xD440)>>1];

    t_mmdsp_dcache_regs_16 dcache;

    t_uint16 RESERVED3[(0xE000-0xD840)>>1];

    t_mmdsp_io_regs_16 io;

    t_uint16 RESERVED4[(0x60-0x50)>>1];

    t_mmdsp_timer_regs_16 timer[MMDSP_NB_TIMER];

    t_uint16 RESERVED5[(0x410-0x78)>>1];

    t_mmdsp_field_16 sem[MMDSP_NB_BIT_SEM];

    t_uint16 RESERVED6[(0x450-0x430)>>1];

    t_mmdsp_field_16 ipen;
    t_uint16 itip_0;
    t_uint16 itip_1;
    t_uint16 itip_2;
    t_uint16 itip_3;
    t_uint16 itop_0;
    t_uint16 itop_1;
    t_uint16 itop_2;
    t_uint16 itop_3;
    t_uint16 RESERVED7[(0x8a-0x64)>>1];
    t_uint16 itip_4;
    t_uint16 itop_4;

    t_uint16 RESERVED8[(0x7e0-0x48e)>>1];

    t_mmdsp_field_16 id[4];
    t_mmdsp_field_16 idp[4];

    t_mmdsp_dma_if_regs_16 dma_if[MMDSP_NB_DMA_IF];

    t_uint16 RESERVED9[(0xC00-0x900)>>1];

    t_mmdsp_field_16 emu_unit_maskit;
    t_mmdsp_field_16 RESERVED[3];
    t_mmdsp_field_16 config_data_mem;
    t_mmdsp_field_16 compatibility;

    t_uint16 RESERVED10[(0xF000-0xEC18)>>1];

    t_uint16 stbus_if_config;
    t_uint16 stbus_if_mode;
    t_uint16 stbus_if_status;
    t_uint16 stbus_if_security;
    t_uint16 stbus_if_flush;
    t_uint16 stbus_reserved;
    t_uint16 stbus_if_priority;
    t_uint16 stbus_msb_attribut;

    t_uint16 RESERVED11[(0xFC00-0xF010)>>1];

    t_mmdsp_field_16 itremap_reg[MMDSP_NB_ITREMAP_REG];
    t_mmdsp_field_16 itmsk_l_reg;
    t_mmdsp_field_16 itmsk_h_reg;

    t_uint16 RESERVED12[(0xfc9c - 0xfc88)>>1];

    t_mmdsp_field_16 itmemo_l_reg;
    t_mmdsp_field_16 itmeme_h_reg;

    t_uint16 RESERVED13[(0xfd00 - 0xfca4)>>1];

    t_mmdsp_field_16 itremap1_reg[MMDSP_NB_ITREMAP_REG];

    t_uint16 RESERVED14[(0x60000 - 0x5fd80)>>1];
} t_mmdsp_mmio_regs_16;


typedef volatile struct {
    t_uint32 RESERVED1[(0xa800)>>2];

    t_mmdsp_dma_ctrl_regs_32 dma_ctrl[MMDSP_NB_DMA_CTRL];

    t_uint32 RESERVED2[(0xb000-0xa880)>>2];

    t_mmdsp_dcache_regs_32 dcache;

    t_uint32 RESERVED3[(0xc000-0xb080)>>2];

    t_mmdsp_io_regs_32 io;

    t_uint32 RESERVED4[(0xc0-0xa0)>>2];

    t_mmdsp_timer_regs_32 timer[MMDSP_NB_TIMER];

    t_uint32 RESERVED5[(0x820-0x0f0)>>2];

    t_mmdsp_field_32 sem[MMDSP_NB_BIT_SEM];

    t_uint32 RESERVED6[(0x8a0-0x860)>>2];

    t_mmdsp_field_32 ipen;
    t_uint32 itip_0;
    t_uint32 itip_1;
    t_uint32 itip_2;
    t_uint32 itip_3;
    t_uint32 itop_0;
    t_uint32 itop_1;
    t_uint32 itop_2;
    t_uint32 itop_3;
    t_uint32 RESERVED7[(0x914-0x8c8)>>2];
    t_uint32 itip_4;
    t_uint32 itop_4;

    t_uint32 RESERVED8[(0xcfc0-0xc91c)>>2];

    t_mmdsp_field_32 id[4];
    t_mmdsp_field_32 idp[4];

    t_mmdsp_dma_if_regs_32 dma_if[MMDSP_NB_DMA_IF];

    t_uint32 RESERVED9[(0x800-0x200)>>2];

    t_mmdsp_field_32 emu_unit_maskit;
    t_mmdsp_field_32 RESERVED[3];
    t_mmdsp_field_32 config_data_mem;
    t_mmdsp_field_32 compatibility;

    t_uint32 RESERVED10[(0xE000-0xD830)>>2];

    t_uint32 stbus_if_config;
    t_uint32 stbus_if_mode;
    t_uint32 stbus_if_status;
    t_uint32 stbus_if_security;
    t_uint32 stbus_if_flush;
    t_uint32 stbus_reserved;
    t_uint32 stbus_if_priority;
    t_uint32 stbus_msb_attribut;

    t_uint32 RESERVED11[(0xF800-0xE020)>>2];

    t_mmdsp_field_32 itremap_reg[MMDSP_NB_ITREMAP_REG];
    t_mmdsp_field_32 itmsk_l_reg;
    t_mmdsp_field_32 itmsk_h_reg;

    t_uint32 RESERVED12[(0xf938 - 0xf910)>>2];

    t_mmdsp_field_32 itmemo_l_reg;
    t_mmdsp_field_32 itmeme_h_reg;

    t_uint32 RESERVED13[(0xfa00 - 0xf948)>>2];

    t_mmdsp_field_32 itremap1_reg[MMDSP_NB_ITREMAP_REG];

    t_uint32 RESERVED14[(0x40000 - 0x3fb00)>>2];
} t_mmdsp_mmio_regs_32;
#endif /* __STN_8820 or __STN_8500 */

#ifdef __STN_8815
typedef volatile struct {
    t_uint16 RESERVED1[(0xD400-0x8000)>>1];

    t_mmdsp_dma_ctrl_regs_16 dma_ctrl[MMDSP_NB_DMA_CTRL];

    t_uint16 RESERVED2[(0xD800-0xD440)>>1];

    t_mmdsp_dcache_regs_16 dcache;

    t_uint16 RESERVED3[(0xE000-0xD840)>>1];

    t_mmdsp_io_regs_16 io;

    t_uint16 RESERVED4[(0x60-0x50)>>1];

    t_mmdsp_timer_regs_16 timer[MMDSP_NB_TIMER];

    t_uint16 RESERVED5[(0x410-0x78)>>1];

    t_mmdsp_field_16 sem[MMDSP_NB_BIT_SEM];

    t_uint16 RESERVED6[(0x450-0x430)>>1];

    t_mmdsp_field_16 ipen;
    t_uint16 itip_0;
    t_uint16 itip_1;
    t_uint16 itip_2;
    t_uint16 itip_3;
    t_uint16 itop_0;
    t_uint16 itop_1;
    t_uint16 itop_2;
    t_uint16 itop_3;
    t_uint16 RESERVED7[(0x8a-0x64)>>1];
    t_uint16 itip_4;
    t_uint16 itop_4;

    t_uint16 RESERVED8[(0x7e0-0x48e)>>1];

    t_mmdsp_field_16 id[4];
    t_mmdsp_field_16 idp[4];

    t_mmdsp_dma_if_regs_16 dma_if[MMDSP_NB_DMA_IF];

    t_uint16 RESERVED9[(0xC00-0x900)>>1];

    t_mmdsp_field_16 emu_unit_maskit;
    t_mmdsp_field_16 RESERVED[3];
    t_mmdsp_field_16 config_data_mem;
    t_mmdsp_field_16 compatibility;

    t_uint16 RESERVED10[(0xF000-0xEC18)>>1];

    t_uint16 ahb_if_config;
    t_uint16 ahb_if_mode;
    t_uint16 ahb_if_status;
    t_uint16 ahb_if_security;
    t_uint16 ahb_if_flush;

    t_uint16 RESERVED11[(0xFC00-0xF00A)>>1];

    t_mmdsp_field_16 itremap_reg[MMDSP_NB_ITREMAP_REG];
    t_mmdsp_field_16 itmsk_l_reg;
    t_mmdsp_field_16 itmsk_h_reg;

    t_uint16 RESERVED12[(0xfc9c - 0xfc88)>>1];

    t_mmdsp_field_16 itmemo_l_reg;
    t_mmdsp_field_16 itmeme_h_reg;

    t_uint16 RESERVED13[(0xfd00 - 0xfca4)>>1];

    t_mmdsp_field_16 itremap1_reg[MMDSP_NB_ITREMAP_REG];

    t_uint16 RESERVED14[(0x60000 - 0x5fd80)>>1];
} t_mmdsp_mmio_regs_16;


typedef volatile struct {
    t_uint32 RESERVED1[(0xa800)>>2];

    t_mmdsp_dma_ctrl_regs_32 dma_ctrl[MMDSP_NB_DMA_CTRL];

    t_uint32 RESERVED2[(0xb000-0xa880)>>2];

    t_mmdsp_dcache_regs_32 dcache;

    t_uint32 RESERVED3[(0xc000-0xb080)>>2];

    t_mmdsp_io_regs_32 io;

    t_uint32 RESERVED4[(0xc0-0xa0)>>2];

    t_mmdsp_timer_regs_32 timer[MMDSP_NB_TIMER];

    t_uint32 RESERVED5[(0x820-0x0f0)>>2];

    t_mmdsp_field_32 sem[MMDSP_NB_BIT_SEM];

    t_uint32 RESERVED6[(0x8a0-0x860)>>2];

    t_mmdsp_field_32 ipen;
    t_uint32 itip_0;
    t_uint32 itip_1;
    t_uint32 itip_2;
    t_uint32 itip_3;
    t_uint32 itop_0;
    t_uint32 itop_1;
    t_uint32 itop_2;
    t_uint32 itop_3;
    t_uint32 RESERVED7[(0x914-0x8c8)>>2];
    t_uint32 itip_4;
    t_uint32 itop_4;

    t_uint32 RESERVED8[(0xcfc0-0xc91c)>>2];

    t_mmdsp_field_32 id[4];
    t_mmdsp_field_32 idp[4];

    t_mmdsp_dma_if_regs_32 dma_if[MMDSP_NB_DMA_IF];

    t_uint32 RESERVED9[(0x800-0x200)>>2];

    t_mmdsp_field_32 emu_unit_maskit;
    t_mmdsp_field_32 RESERVED[3];
    t_mmdsp_field_32 config_data_mem;
    t_mmdsp_field_32 compatibility;

    t_uint32 RESERVED10[(0xE000-0xD830)>>2];

    t_uint32 ahb_if_config;
    t_uint32 ahb_if_mode;
    t_uint32 ahb_if_status;
    t_uint32 ahb_if_security;
    t_uint32 ahb_if_flush;

    t_uint32 RESERVED11[(0xF800-0xE014)>>2];

    t_mmdsp_field_32 itremap_reg[MMDSP_NB_ITREMAP_REG];
    t_mmdsp_field_32 itmsk_l_reg;
    t_mmdsp_field_32 itmsk_h_reg;

    t_uint32 RESERVED12[(0xf938 - 0xf910)>>2];

    t_mmdsp_field_32 itmemo_l_reg;
    t_mmdsp_field_32 itmeme_h_reg;

    t_uint32 RESERVED13[(0xfa00 - 0xf948)>>2];

    t_mmdsp_field_32 itremap1_reg[MMDSP_NB_ITREMAP_REG];

    t_uint32 RESERVED14[(0x40000 - 0x3fb00)>>2];
} t_mmdsp_mmio_regs_32;
#endif /* __STN_8815 */

/* Smart xx Accelerator memory map */
typedef volatile struct {
    t_uint32    mem24[MMDSP_NB_BLOCK_RAM*MMDSP_RAM_BLOCK_SIZE];  /* 0x0000 -> 0x20000 */

    t_uint32    RESERVED1[(0x30000 - 0x20000)>>2];

    t_mmdsp_mmio_regs_32 mmio_32;

    t_uint16    mem16[MMDSP_NB_BLOCK_RAM*MMDSP_RAM_BLOCK_SIZE];  /* 0x40000 -> 0x50000 */

    t_uint32    RESERVED2[(0x58000 - 0x50000)>>2];

    t_mmdsp_mmio_regs_16 mmio_16;

    t_mmdsp_host_regs_16 host_reg;
    /*
    union host_reg {
        t_mmdsp_host_regs_16 reg16;
        t_mmdsp_host_regs_32 reg32;
    };
    */
} t_mmdsp_hw_regs;

#endif // __INC_MMDSP_HWP_H
