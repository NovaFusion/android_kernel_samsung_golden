/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_MMDSP_DSP_MACROS
#define __INC_MMDSP_DSP_MACROS

#include <cm/engine/dsp/mmdsp/inc/mmdsp_hwp.h>

#define MMDSP_ENABLE_WRITE_POSTING(pRegs) \
{                                                       \
   (pRegs)->mmio_16.dcache.control |= DCACHE_CONTROL_WRITE_POSTING_ENABLE;  \
}

#define MMDSP_FLUSH_DCACHE(pRegs) \
{ /* Today, only full cache flush (clear all the ways) */ \
   (pRegs)->mmio_16.dcache.command = DCACHE_CMD_FLUSH; \
}

#define MMDSP_FLUSH_DCACHE_BY_SERVICE(pRegs, startAddr, endAddr)

#define MMDSP_FLUSH_ICACHE(pRegs) \
{ /* Flush the Instruction cache */ \
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_ICACHE_FLUSH_REG, (IHOST_ICACHE_FLUSH_ALL_ENTRIES_CMD | IHOST_ICACHE_FLUSH_CMD_ENABLE)); \
}

#ifndef __STN_8810
#define MMDSP_FLUSH_ICACHE_BY_SERVICE(pRegs, startAddr, endAddr) \
{ /* Flush the Instruction cache by service */ \
    /*t_uint64 start_clear_addr = startAddr & ~(MMDSP_ICACHE_LINE_SIZE_IN_INST - 1);*/ \
    t_uint64 start_clear_addr = (startAddr)>>2; \
    t_uint64 end_clear_addr = ((endAddr) + MMDSP_ICACHE_LINE_SIZE_IN_INST) & ~(MMDSP_ICACHE_LINE_SIZE_IN_INST - 1); \
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_ICACHE_START_CLEAR_REG, start_clear_addr); \
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_ICACHE_END_CLEAR_REG, end_clear_addr); \
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_ICACHE_FLUSH_REG, (IHOST_ICACHE_FLUSH_BY_SERVICE | IHOST_ICACHE_FLUSH_CMD_ENABLE)); \
}
#else
#define MMDSP_FLUSH_ICACHE_BY_SERVICE(pRegs, startAddr, endAddr) {(void)pRegs; (void)startAddr; (void)endAddr; }
#endif

#define MMDSP_RESET_CORE(pRegs) \
{  /* Assert DSP core soft reset */ \
   (pRegs)->host_reg.softreset = 1; \
}

#define MMDSP_START_CORE(pRegs) \
{ \
   /* Enable external memory access (set bit 3 of ubkcmd) */ \
   (pRegs)->host_reg.emul_ubkcmd |= MMDSP_UBKCMD_EXT_CODE_MEM_ACCESS_ENABLE; \
  \
   /* Start core clock */ \
   (pRegs)->host_reg.emul_clockcmd = MMDSP_CLOCKCMD_START_CLOCK; \
}

#define MMDSP_STOP_CORE(pRegs) \
{ \
   /* Disable external memory access (reset bit 3 of ubkcmd) */ \
   (pRegs)->host_reg.emul_ubkcmd = MMDSP_UBKCMD_EXT_CODE_MEM_ACCESS_DISABLE; \
  \
   /* Stop core clock */ \
   (pRegs)->host_reg.emul_clockcmd = MMDSP_CLOCKCMD_STOP_CLOCK; \
}

#define MMDSP_ASSERT_IRQ(pRegs, irqNum) \
{ \
   (pRegs)->host_reg.cmd[irqNum] = 1; \
}

#define MMDSP_ACKNOWLEDGE_IRQ(pRegs, irqNum) \
{ \
    volatile t_uint16 dummy; \
    dummy =(pRegs)->host_reg.intx[irqNum]; \
}

#define MMDSP_WRITE_XWORD(pRegs, offset, value) \
{ \
    (pRegs)->mem24[offset] = value; \
}

#define MMDSP_READ_XWORD(pRegs, offset) (pRegs)->mem24[offset]

#endif /* __INC_MMDSP_DSP_MACROS */
