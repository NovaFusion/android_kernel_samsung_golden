/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/dsp/mmdsp/inc/mmdsp_macros.h>

#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <cm/engine/power_mgt/inc/power.h>
#include <cm/engine/memory/inc/migration.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/trace/inc/xtitrace.h>

#include <share/inc/nomadik_mapping.h>

#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/component/inc/component_type.h>

static t_dsp_allocator_desc     esramDesc;
static t_dsp_desc               mpcDesc[NB_CORE_IDS];
static t_mmdsp_hw_regs          *pMmdspRegs[NB_CORE_IDS];

struct s_base_descr
{
    t_uint32            startAddress[2 /* DSP16 = 0, DSP24 = 1*/];
    t_dsp_segment_type  segmentType;
};

#if defined(__STN_8500) && (__STN_8500 > 10)

#define DATA_BASE_NUMBER    4

// In bytes
#define SDRAM_CODE_SPACE_SPLIT 0x8000
#define ESRAM_CODE_SPACE_SPLIT 0x4000
#define SDRAM_DATA_SPACE_SPLIT 0x40000 // This is the modulo constraint of mmdsp
#define ESRAM_DATA_SPACE_SPLIT 0x40000

// In MMDSP word
static const struct s_base_descr DATA_ADDRESS_BASE[DATA_BASE_NUMBER + 1 /* For guard */] = {
        {{SDRAMMEM16_BASE_ADDR,                                      SDRAMMEM24_BASE_ADDR},                                 SDRAM_DATA_EE},
        {{SDRAMMEM16_BASE_ADDR + (SDRAM_DATA_SPACE_SPLIT / 2),       SDRAMMEM24_BASE_ADDR + (SDRAM_DATA_SPACE_SPLIT / 4)},  SDRAM_DATA_USER},
        {{ESRAMMEM16_BASE_ADDR,                                      ESRAMMEM24_BASE_ADDR},                                 ESRAM_DATA_EE},
        {{ESRAMMEM16_BASE_ADDR + (ESRAM_DATA_SPACE_SPLIT / 2),       ESRAMMEM24_BASE_ADDR + (ESRAM_DATA_SPACE_SPLIT / 4)},  ESRAM_DATA_USER},
        {{MMIO_BASE_ADDR,                                            SDRAMMEM16_BASE_ADDR},                                 NB_DSP_SEGMENT_TYPE /* Not used*/}
};

#else

#define DATA_BASE_NUMBER    2

// In MMDSP word
static const struct s_base_descr DATA_ADDRESS_BASE[DATA_BASE_NUMBER + 1 /* For guard */] = {
        {{SDRAMMEM16_BASE_ADDR,                                      SDRAMMEM24_BASE_ADDR},                                 SDRAM_DATA_EE},
        {{ESRAMMEM16_BASE_ADDR,                                      ESRAMMEM24_BASE_ADDR},                                 ESRAM_DATA_EE},
        {{MMIO_BASE_ADDR,                                            SDRAMMEM16_BASE_ADDR},                                 NB_DSP_SEGMENT_TYPE /* Not used*/}
};

#endif

#if defined(__STN_8500) && (__STN_8500 > 10)
// In word
static const t_uint32 CODE_ADDRESS_BASE[4] = {
        SDRAMTEXT_BASE_ADDR,
        SDRAMTEXT_BASE_ADDR + (SDRAM_CODE_SPACE_SPLIT / 8),
        ESRAMTEXT_BASE_ADDR,
        ESRAMTEXT_BASE_ADDR + (ESRAM_CODE_SPACE_SPLIT / 8)
};
#endif

static void arm_Init(void);
static t_cm_error mmdsp_Init(const t_cm_system_address *dspSystemAddr,
        t_uint8 nbXramBlocks, t_uint8 nbYramBlocks,
        t_dsp_allocator_desc *sdramCodeDesc,
        t_dsp_allocator_desc *sdramDataDesc,
        t_cm_domain_id eeDomain,
        t_dsp_desc *pDspDesc,
        t_mmdsp_hw_regs **pRegs);
static t_cm_error mmdsp_Configure(t_nmf_core_id coreId, t_mmdsp_hw_regs *pRegs, const t_dsp_desc *pDspDesc);
static t_cm_error mmdsp_ConfigureAfterBoot(t_nmf_core_id coreId, t_uint8 nbXramBlocks, t_uint8 nbYramBlocks);
static void cm_DSP_SEM_Init(t_nmf_core_id coreId);

PUBLIC const t_dsp_desc* cm_DSP_GetState(t_nmf_core_id coreId)
{
    return &mpcDesc[coreId];
}
PUBLIC void cm_DSP_SetStatePanic(t_nmf_core_id coreId)
{
    mpcDesc[coreId].state = MPC_STATE_PANIC;
}

PUBLIC void cm_DSP_Init(const t_nmf_memory_segment *pEsramDesc)
{
    t_nmf_core_id coreId;
    int i;

    /* Create esram desc */
    esramDesc.allocDesc = cm_MM_CreateAllocator(pEsramDesc->size, 0, "esram");
    esramDesc.baseAddress = pEsramDesc->systemAddr;
    esramDesc.referenceCounter = 1; // Don't free it with destroy mechanism

    /* Create ARM */
    arm_Init();

    mpcDesc[ARM_CORE_ID].state = MPC_STATE_BOOTED;

    /* Reset MPC configuration */
    for (coreId = FIRST_MPC_ID; coreId <= LAST_CORE_ID; coreId++)
    {
        mpcDesc[coreId].state = MPC_STATE_UNCONFIGURED;

        for(i = 0; i < NB_DSP_MEMORY_TYPE; i++)
            mpcDesc[coreId].allocator[i] = NULL;
    }

}

PUBLIC void cm_DSP_Destroy(void)
{
    t_nmf_core_id coreId;
    int i;

    for (coreId = ARM_CORE_ID; coreId <= LAST_CORE_ID; coreId++)
    {
        for(i = 0; i < NB_DSP_MEMORY_TYPE; i++)
        {
            if (mpcDesc[coreId].allocator[i] != NULL)
            {
                if(--mpcDesc[coreId].allocator[i]->referenceCounter == 0)
                {
                    cm_MM_DeleteAllocator(mpcDesc[coreId].allocator[i]->allocDesc);

                    OSAL_Free(mpcDesc[coreId].allocator[i]);
                }
            }
        }
	if ((coreId >= FIRST_MPC_ID) && (coreId <= LAST_MPC_ID))
		cm_SRV_freeTraceBufferMemory(coreId);
    }

    cm_MM_DeleteAllocator(esramDesc.allocDesc);
}


PUBLIC t_cm_error cm_DSP_Add(t_nmf_core_id coreId,
        t_uint8 nbYramBanks,
        const t_cm_system_address *pDspMapDesc,
        const t_cm_domain_id eeDomain,
        t_dsp_allocator_desc *sdramCodeAllocDesc,
        t_dsp_allocator_desc *sdramDataAllocDesc)
{
    t_cm_error error;
    t_ee_state *state = cm_EEM_getExecutiveEngine(coreId);

    /* checking nbYramBanks is valid */
    if (nbYramBanks >= SxA_NB_BLOCK_RAM)
        return CM_MPC_INVALID_CONFIGURATION;

    if((error = cm_DM_CheckDomain(eeDomain, DOMAIN_NORMAL)) != CM_OK)
        return error;

    mpcDesc[coreId].domainEE = eeDomain;
    mpcDesc[coreId].nbYramBank = nbYramBanks;
    mpcDesc[coreId].state = MPC_STATE_BOOTABLE;

    error = mmdsp_Init(
            pDspMapDesc,
            SxA_NB_BLOCK_RAM, /* nb of data tcm bank minus one (reserved for cache) */
            nbYramBanks,
            sdramCodeAllocDesc,
            sdramDataAllocDesc,
            eeDomain,
            &mpcDesc[coreId],
            &pMmdspRegs[coreId]
    );

    if(error != CM_OK)
        return error;

    state->traceBufferSize = TRACE_BUFFER_SIZE;
    return cm_SRV_allocateTraceBufferMemory(coreId, eeDomain);
}

PUBLIC t_cm_error cm_DSP_Boot(t_nmf_core_id coreId)
{
    t_cm_error error;

    // Enable the associated power domain
    if((error = cm_PWR_EnableMPC(MPC_PWR_CLOCK, coreId)) != CM_OK)
      return error;

    cm_SEM_PowerOn[coreId](coreId);

    if((error = mmdsp_Configure(
            coreId,
            pMmdspRegs[coreId],
            &mpcDesc[coreId])) != CM_OK)
    {
        cm_PWR_DisableMPC(MPC_PWR_CLOCK, coreId);
    }

    // Put it in auto idle mode ; it's the default in Step 2 of power implementation
    if((error = cm_PWR_EnableMPC(MPC_PWR_AUTOIDLE, coreId)) != CM_OK)
      return error;

    return error;
}

/*
 * This method is required since MMDSP C bootstrap set some value that must be set differently !!!
 */
PUBLIC void cm_DSP_ConfigureAfterBoot(t_nmf_core_id coreId)
{
    mpcDesc[coreId].state = MPC_STATE_BOOTED;

    mmdsp_ConfigureAfterBoot(coreId, SxA_NB_BLOCK_RAM, mpcDesc[coreId].nbYramBank);

    cm_DSP_SEM_Init(coreId);
}

PUBLIC void cm_DSP_Stop(t_nmf_core_id coreId)
{
    MMDSP_STOP_CORE(pMmdspRegs[coreId]);

    {
        volatile t_uint32 loopme = 0xfff;
        while(loopme--) ;
    }
}

PUBLIC void cm_DSP_Start(t_nmf_core_id coreId)
{
    MMDSP_START_CORE(pMmdspRegs[coreId]);

    {
        volatile t_uint32 loopme = 0xfff;
        while(loopme--) ;
    }
}

PUBLIC void cm_DSP_Shutdown(t_nmf_core_id coreId)
{
    MMDSP_FLUSH_DCACHE(pMmdspRegs[coreId]);
    MMDSP_FLUSH_ICACHE(pMmdspRegs[coreId]);

    // Due to a hardware bug that breaks MTU when DSP are powered off, don't do that
    // on mop500_ed for now
#if !defined(__STN_8500) ||  (__STN_8500 > 10)
    MMDSP_RESET_CORE(pMmdspRegs[coreId]);
    {
        volatile t_uint32 loopme = 0xfff;
        while(loopme--) ;
    }
    MMDSP_STOP_CORE(pMmdspRegs[coreId]);
    {
        volatile t_uint32 loopme = 0xfff;
        while(loopme--) ;
    }
#endif

    mpcDesc[coreId].state = MPC_STATE_BOOTABLE;

    cm_SEM_PowerOff[coreId](coreId);

    cm_PWR_DisableMPC(MPC_PWR_AUTOIDLE, coreId);
    cm_PWR_DisableMPC(MPC_PWR_CLOCK, coreId);
}

PUBLIC t_uint32 cm_DSP_ReadXRamWord(t_nmf_core_id coreId, t_uint32 dspOffset)
{
    t_uint32 value;

    value = pMmdspRegs[coreId]->mem24[dspOffset];

    LOG_INTERNAL(3, "cm_DSP_ReadXRamWord: [%x]=%x\n",
            dspOffset, value,
            0, 0, 0, 0);

    return value;
}


PUBLIC void cm_DSP_WriteXRamWord(t_nmf_core_id coreId, t_uint32 dspOffset, t_uint32 value)
{
    LOG_INTERNAL(3, "cm_DSP_WriteXRamWord: [%x]<-%x\n",
            dspOffset, value,
            0, 0, 0, 0);

    pMmdspRegs[coreId]->mem24[dspOffset] = value;
}

static void cm_DSP_SEM_Init(t_nmf_core_id coreId)
{
    pMmdspRegs[coreId]->mmio_16.sem[1].value = 1;
}

PUBLIC void cm_DSP_SEM_Take(t_nmf_core_id coreId, t_semaphore_id semId)
{
    /* take semaphore */
    while(pMmdspRegs[coreId]->mmio_16.sem[1].value) ;
}

PUBLIC void cm_DSP_SEM_Give(t_nmf_core_id coreId, t_semaphore_id semId)
{
    /* release semaphore */
    pMmdspRegs[coreId]->mmio_16.sem[1].value = 1;
}

PUBLIC void cm_DSP_SEM_GenerateIrq(t_nmf_core_id coreId, t_semaphore_id semId)
{
    MMDSP_ASSERT_IRQ(pMmdspRegs[coreId], ARM2DSP_IRQ_0);
}


PUBLIC void cm_DSP_AssertDspIrq(t_nmf_core_id coreId, t_host2mpc_irq_num irqNum)
{
    MMDSP_ASSERT_IRQ(pMmdspRegs[coreId], irqNum);
    return;
}

PUBLIC void cm_DSP_AcknowledgeDspIrq(t_nmf_core_id coreId, t_mpc2host_irq_num irqNum)
{
    MMDSP_ACKNOWLEDGE_IRQ(pMmdspRegs[coreId], irqNum);
    return;
}

//TODO, juraj, cleanup INTERNAL_XRAM vs INTERNAL_XRAM16/24
static const t_uint32 dspMemoryTypeId2OffsetShifter[NB_DSP_MEMORY_TYPE] =
{
    2, /* INTERNAL_XRAM24:   Internal X memory but seen by host as 32-bit memory */
    2, /* INTERNAL_XRAM16:   Internal X memory but seen by host as 16-bit memory */
    2, /* INTERNAL_YRAM24:   Internal Y memory but seen by host as 32-bit memory */
    2, /* INTERNAL_YRAM16:   Internal Y memory but seen by host as 16-bit memory */
    2, /* SDRAM_EXT24:       24-bit external "X" memory */
    1, /* SDRAM_EXT16:       16-bit external "X" memory */
    2, /* ESRAM_EXT24:       ESRAM24 */
    1, /* ESRAM_EXT16:       ESRAM16 */
    3, /* SDRAM_CODE:        Program memory */
    3, /* ESRAM_CODE:        ESRAM code */
    3, /* LOCKED_CODE:        ESRAM code */
};

//TODO, juraj, use these values in mmdsp_Configure
static const t_uint32 dspMemoryTypeId2DspAddressOffset[NB_DSP_MEMORY_TYPE] =
{
    0, /* INTERNAL_XRAM24 */
    0, /* INTERNAL_XRAM16 */
    0, /* INTERNAL_YRAM24 */
    0, /* INTERNAL_YRAM16 */
    SDRAMMEM24_BASE_ADDR,           /* SDRAM_EXT24:       24-bit external "X" memory */
    SDRAMMEM16_BASE_ADDR,           /* SDRAM_EXT16:       16-bit external "X" memory */
    ESRAMMEM24_BASE_ADDR,           /* ESRAM_EXT24:       ESRAM24 */
    ESRAMMEM16_BASE_ADDR,           /* ESRAM_EXT16:       ESRAM16 */
    SDRAMTEXT_BASE_ADDR,            /* SDRAM_CODE:        Program memory */
    ESRAMTEXT_BASE_ADDR,            /* ESRAM_CODE:        ESRAM code */
    SDRAMTEXT_BASE_ADDR,            /* ESRAM_CODE:        ESRAM code */
};

PUBLIC t_cm_allocator_desc* cm_DSP_GetAllocator(t_nmf_core_id coreId, t_dsp_memory_type_id memType)
{
    return mpcDesc[coreId].allocator[memType] ? mpcDesc[coreId].allocator[memType]->allocDesc : NULL;
}

PUBLIC void cm_DSP_GetDspChunkInfo(t_memory_handle memHandle, t_dsp_chunk_info *info)
{
    t_uint16 userData;

    cm_MM_GetMemoryHandleUserData(memHandle, &userData, &info->alloc);

    info->coreId            = (t_nmf_core_id)       ((userData >> SHIFT_BYTE1) & MASK_BYTE0);
    info->memType           = (t_dsp_memory_type_id)((userData >> SHIFT_BYTE0) & MASK_BYTE0);
}

PUBLIC t_cm_error cm_DSP_GetInternalMemoriesInfo(t_cm_domain_id domainId, t_dsp_memory_type_id memType,
        t_uint32 *offset, t_uint32 *size)
{
    t_nmf_core_id coreId = domainDesc[domainId].domain.coreId;

    switch(memType)
    {
    case INTERNAL_XRAM24:
    case INTERNAL_XRAM16:
        *offset = 0;
        *size = mpcDesc[coreId].yram_offset;
        break;
    case INTERNAL_YRAM24:
    case INTERNAL_YRAM16:
        *offset = mpcDesc[coreId].yram_offset;
        *size = mpcDesc[coreId].yram_size;
        break;
    case LOCKED_CODE:
        *offset = mpcDesc[coreId].locked_offset;
        *size = mpcDesc[coreId].locked_size;
        break;
    case SDRAM_EXT24:
    case SDRAM_EXT16:
        *offset = domainDesc[domainId].domain.sdramData.offset;
        *size   = domainDesc[domainId].domain.sdramData.size;
        break;
    case ESRAM_EXT24:
    case ESRAM_EXT16:
        *offset = domainDesc[domainId].domain.esramData.offset;
        *size   = domainDesc[domainId].domain.esramData.size;
        break;
    case SDRAM_CODE:
        *offset = domainDesc[domainId].domain.sdramCode.offset;
        *size   = domainDesc[domainId].domain.sdramCode.size;

        // update domain size to take into account .locked section
        if(*offset + *size > mpcDesc[coreId].locked_offset)
            *size = mpcDesc[coreId].locked_offset - *offset;
        break;
    case ESRAM_CODE:
        *offset = domainDesc[domainId].domain.esramCode.offset;
        *size   = domainDesc[domainId].domain.esramCode.size;
        break;
    default:
        //return CM_INVALID_PARAMETER;
        //params are checked at the level above, so this should never occur
        ERROR("Invalid memType\n",0,0,0,0,0,0);
        *offset = 0;
        *size   = 0;
        CM_ASSERT(0);
    }

    return CM_OK;
}


PUBLIC t_uint32 cm_DSP_ConvertSize(t_dsp_memory_type_id memType, t_uint32 wordSize)
{
    return wordSize << dspMemoryTypeId2OffsetShifter[memType];
}

PUBLIC t_cm_logical_address cm_DSP_ConvertDspAddressToHostLogicalAddress(t_nmf_core_id coreId, t_shared_addr dspAddress)
{
    t_dsp_address_info info;
    cm_DSP_GetDspDataAddressInfo(coreId, dspAddress, &info);
    return mpcDesc[coreId].segments[info.segmentType].base.logical + info.baseOffset;
}

PUBLIC t_cm_error cm_DSP_GetAllocatorStatus(t_nmf_core_id coreId, t_dsp_memory_type_id dspMemType, t_uint32 offset, t_uint32 size, t_cm_allocator_status *pStatus)
{
    t_cm_error error;

    if(mpcDesc[coreId].allocator[dspMemType] == NULL)
        return CM_UNKNOWN_MEMORY_HANDLE;

    error = cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(coreId, dspMemType), offset, size, pStatus);
    if (error != CM_OK)
        return error;

    // complete status with stack sizes, for all dsps
    //NOTE, well, surely this isn't very clean, as dsp and memory allocator are different things ..
    {
        t_uint8 i;
        for (i = 0; i < NB_CORE_IDS; i++) {
            t_ee_state *state = cm_EEM_getExecutiveEngine(coreId);
            pStatus->stack[i].sizes[0] = state->currentStackSize[0];
            pStatus->stack[i].sizes[1] = state->currentStackSize[1];
            pStatus->stack[i].sizes[2] = state->currentStackSize[2];
        }
    }

    // Change bytes to words
    pStatus->global.accumulate_free_memory  = pStatus->global.accumulate_free_memory >> dspMemoryTypeId2OffsetShifter[dspMemType];
    pStatus->global.accumulate_used_memory  = pStatus->global.accumulate_used_memory >> dspMemoryTypeId2OffsetShifter[dspMemType];
    pStatus->global.maximum_free_size  = pStatus->global.maximum_free_size >> dspMemoryTypeId2OffsetShifter[dspMemType];
    pStatus->global.minimum_free_size  = pStatus->global.minimum_free_size >> dspMemoryTypeId2OffsetShifter[dspMemType];

    return error;
}

PUBLIC void cm_DSP_GetHostSystemAddress(t_memory_handle memHandle, t_cm_system_address *pAddr)
{
    t_dsp_chunk_info chunk_info;
    t_uint32 offset; //in bytes

    cm_DSP_GetDspChunkInfo(memHandle, &chunk_info);

    offset = cm_MM_GetOffset(memHandle);

    /* MMDSP mem16 array is very specific to host access, so .... */
    /* We compute by hand the Host System address to take into account the specifities of the mmdsp mem16 array */
    /* 1 dsp word = 2 host bytes AND mem16 array is "exported" by MMDSP External Bus wrapper at the 0x40000 offet */
    if (chunk_info.memType == INTERNAL_XRAM16 || chunk_info.memType == INTERNAL_YRAM16) {
        offset = (offset >> 1) + FIELD_OFFSET(t_mmdsp_hw_regs, mem16);
    }

    //TODO, juraj, calculate correct value here - based on segments desc etc..
    pAddr->logical  = mpcDesc[chunk_info.coreId].allocator[chunk_info.memType]->baseAddress.logical  + offset;
    pAddr->physical = mpcDesc[chunk_info.coreId].allocator[chunk_info.memType]->baseAddress.physical + offset;
}


PUBLIC t_physical_address cm_DSP_GetPhysicalAdress(t_memory_handle memHandle)
{
    t_cm_system_address addr;
    cm_DSP_GetHostSystemAddress(memHandle, &addr);
    return addr.physical;
}

PUBLIC t_cm_logical_address cm_DSP_GetHostLogicalAddress(t_memory_handle memHandle)
{
    t_cm_system_address addr;
    cm_DSP_GetHostSystemAddress(memHandle, &addr);
    return addr.logical;
}

PUBLIC void cm_DSP_GetDspAddress(t_memory_handle memHandle, t_uint32 *pDspAddress)
{
    t_dsp_chunk_info chunk_info;

    cm_DSP_GetDspChunkInfo(memHandle, &chunk_info);

    *pDspAddress =
            (cm_MM_GetOffset(memHandle) >> dspMemoryTypeId2OffsetShifter[chunk_info.memType]) +
            dspMemoryTypeId2DspAddressOffset[chunk_info.memType];
}

PUBLIC t_cm_error cm_DSP_GetDspBaseAddress(t_nmf_core_id coreId, t_dsp_memory_type_id memType, t_cm_system_address *pAddr)
{
    cm_migration_check_state(coreId, STATE_NORMAL);
    if (mpcDesc[coreId].allocator[memType] == NULL)
	    return CM_INVALID_PARAMETER;
    *pAddr = mpcDesc[coreId].allocator[memType]->baseAddress;
    return CM_OK;
}

PUBLIC void cm_DSP_GetDspMemoryHandleSize(t_memory_handle memHandle, t_uint32 *pDspSize)
{
    t_dsp_chunk_info chunk_info;
    cm_DSP_GetDspChunkInfo(memHandle, &chunk_info);
    *pDspSize = cm_MM_GetSize(memHandle) >> dspMemoryTypeId2OffsetShifter[chunk_info.memType];
}

PUBLIC t_cm_error cm_DSP_setStackSize(t_nmf_core_id coreId, t_uint32 newStackSize)
{
    t_uint8 nbXramBanks;
    t_uint32 xramSize;
    t_cm_error error;

    /* compute size of xram allocator */
    nbXramBanks = SxA_NB_BLOCK_RAM - mpcDesc[coreId].nbYramBank;

    /* check first that required stack size is less then xram memory ....*/
    if (newStackSize >= nbXramBanks * 4 * ONE_KB) {
        ERROR("CM_NO_MORE_MEMORY: cm_DSP_setStackSize(), required stack size doesn't fit in XRAM.\n", 0, 0, 0, 0, 0, 0);
        return CM_NO_MORE_MEMORY;
    }

    /* compute new xram allocator size */
    xramSize = nbXramBanks * 4 * ONE_KB - newStackSize;

    /* try to resize it */
    if ((error = cm_MM_ResizeAllocator(cm_DSP_GetAllocator(coreId, INTERNAL_XRAM24),
                                 xramSize << dspMemoryTypeId2OffsetShifter[INTERNAL_XRAM24])) == CM_NO_MORE_MEMORY) {
        ERROR("CM_NO_MORE_MEMORY: Couldn't resize stack in cm_DSP_setStackSize()\n", 0, 0, 0, 0, 0, 0);
    }

    return error;
}

PUBLIC t_cm_error cm_DSP_IsNbYramBanksValid(t_nmf_core_id coreId, t_uint8 nbYramBanks)
{
    /* we use one bank for cache */
    t_uint8 nbOfRamBanksWithCacheReserved = SxA_NB_BLOCK_RAM;

    /* we want to keep at least one bank of xram */
    if (nbYramBanks < nbOfRamBanksWithCacheReserved) {return CM_OK;}
    else {return CM_MPC_INVALID_CONFIGURATION;}
}

PUBLIC t_uint32 cm_DSP_getStackAddr(t_nmf_core_id coreId)
{
    /* we use one bank for cache */
    //t_uint8 nbOfRamBanksWithCacheReserved = SxA_NB_BLOCK_RAM;
    /* */
    //return ((nbOfRamBanksWithCacheReserved * MMDSP_RAM_BLOCK_SIZE * MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE) - mpcDesc[coreId].yram_offset);
    return mpcDesc[coreId].yram_offset / MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE;
}

static void arm_Init(void)
{
    mpcDesc[ARM_CORE_ID].allocator[INTERNAL_XRAM24] = 0;
    mpcDesc[ARM_CORE_ID].allocator[INTERNAL_XRAM16] = 0;

    mpcDesc[ARM_CORE_ID].allocator[INTERNAL_YRAM24] = 0;
    mpcDesc[ARM_CORE_ID].allocator[INTERNAL_YRAM16] = 0;

    mpcDesc[ARM_CORE_ID].allocator[SDRAM_CODE] = 0;
    mpcDesc[ARM_CORE_ID].allocator[ESRAM_CODE] = 0;

    mpcDesc[ARM_CORE_ID].allocator[SDRAM_EXT16] = 0;
    mpcDesc[ARM_CORE_ID].allocator[SDRAM_EXT24] = 0;

    mpcDesc[ARM_CORE_ID].allocator[ESRAM_EXT16] = &esramDesc;
    mpcDesc[ARM_CORE_ID].allocator[ESRAM_EXT16]->referenceCounter++;
    mpcDesc[ARM_CORE_ID].allocator[ESRAM_EXT24] = &esramDesc;
    mpcDesc[ARM_CORE_ID].allocator[ESRAM_EXT24]->referenceCounter++;
}

static void _init_Segment(
        t_dsp_segment *seg,
        const t_cm_system_address base, const t_uint32 arm_offset,
        const t_uint32 size)
{
    seg->base.logical = base.logical + arm_offset;
    seg->base.physical = base.physical + arm_offset;
    seg->size = size;
}

static t_cm_error mmdsp_Init(
        const t_cm_system_address *dspSystemAddr,
        t_uint8 nbXramBlocks, t_uint8 nbYramBlocks,
        t_dsp_allocator_desc *sdramCodeDesc,
        t_dsp_allocator_desc *sdramDataDesc,
        t_cm_domain_id eeDomain,
        t_dsp_desc *pDspDesc,
        t_mmdsp_hw_regs **pRegs)
{
    t_cm_system_address xramSysAddr;
    t_uint32 sizeInBytes;

    /* Initialize reference on hw ressources */
    *pRegs = (t_mmdsp_hw_regs *) dspSystemAddr->logical;

    /* Initialize memory segments management */
    xramSysAddr.logical = (t_cm_logical_address)(((t_mmdsp_hw_regs *)dspSystemAddr->logical)->mem24);
    xramSysAddr.physical = (t_cm_physical_address)(((t_mmdsp_hw_regs *)dspSystemAddr->physical)->mem24);

    /* The last (x)ram block will be used by cache, so ... */
    /* And the NB_YRAM_BLOCKS last available block(s) will be used as YRAM */

    /* XRAM*/
    pDspDesc->allocator[INTERNAL_XRAM16] = pDspDesc->allocator[INTERNAL_XRAM24] = (t_dsp_allocator_desc*)OSAL_Alloc(sizeof (t_dsp_allocator_desc));
    if (pDspDesc->allocator[INTERNAL_XRAM24] == NULL)
        return CM_NO_MORE_MEMORY;

    pDspDesc->allocator[INTERNAL_XRAM24]->allocDesc = cm_MM_CreateAllocator(
            ((nbXramBlocks-nbYramBlocks)*MMDSP_RAM_BLOCK_SIZE)*MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE,
            0,
            "XRAM");
    pDspDesc->allocator[INTERNAL_XRAM24]->baseAddress = xramSysAddr;
    pDspDesc->allocator[INTERNAL_XRAM24]->referenceCounter = 2;

    /* YRAM */
    pDspDesc->allocator[INTERNAL_YRAM16] = pDspDesc->allocator[INTERNAL_YRAM24] = (t_dsp_allocator_desc*)OSAL_Alloc(sizeof (t_dsp_allocator_desc));
    if (pDspDesc->allocator[INTERNAL_YRAM24] == 0) {
    	OSAL_Free(pDspDesc->allocator[INTERNAL_XRAM24]);
        return CM_NO_MORE_MEMORY;
    }

    pDspDesc->allocator[INTERNAL_YRAM24]->allocDesc = cm_MM_CreateAllocator(
            (nbYramBlocks*MMDSP_RAM_BLOCK_SIZE)*MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE,
            ((nbXramBlocks-nbYramBlocks)*MMDSP_RAM_BLOCK_SIZE)*MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE,
            "YRAM");
    pDspDesc->allocator[INTERNAL_YRAM24]->baseAddress = xramSysAddr; /* use xram base address but offset is not null */
    pDspDesc->allocator[INTERNAL_YRAM24]->referenceCounter = 2;

    pDspDesc->yram_offset = ((nbXramBlocks-nbYramBlocks)*MMDSP_RAM_BLOCK_SIZE)*MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE;
    pDspDesc->yram_size = (nbYramBlocks*MMDSP_RAM_BLOCK_SIZE)*MMDSP_DATA_WORD_SIZE_IN_HOST_SPACE;

    /* SDRAM & ESRAM */
    pDspDesc->allocator[SDRAM_CODE] = sdramCodeDesc;
    pDspDesc->allocator[SDRAM_CODE]->referenceCounter++;
    pDspDesc->allocator[ESRAM_CODE] = &esramDesc;
    pDspDesc->allocator[ESRAM_CODE]->referenceCounter++;

    /* LOCKED CODE at end of SDRAM code*/
    pDspDesc->allocator[LOCKED_CODE] = sdramCodeDesc;
    pDspDesc->allocator[LOCKED_CODE]->referenceCounter++;

    pDspDesc->locked_offset = cm_MM_GetAllocatorSize(pDspDesc->allocator[SDRAM_CODE]->allocDesc) - MMDSP_CODE_CACHE_WAY_SIZE * 8 * SxA_LOCKED_WAY;
    pDspDesc->locked_size = MMDSP_CODE_CACHE_WAY_SIZE * 8 * SxA_LOCKED_WAY;

    /* Data_16/24 memory management */
    pDspDesc->allocator[SDRAM_EXT16] = sdramDataDesc;
    pDspDesc->allocator[SDRAM_EXT16]->referenceCounter++;
    pDspDesc->allocator[SDRAM_EXT24] = sdramDataDesc;
    pDspDesc->allocator[SDRAM_EXT24]->referenceCounter++;

    pDspDesc->allocator[ESRAM_EXT16] = &esramDesc;
    pDspDesc->allocator[ESRAM_EXT16]->referenceCounter++;
    pDspDesc->allocator[ESRAM_EXT24] = &esramDesc;
    pDspDesc->allocator[ESRAM_EXT24]->referenceCounter++;

    sizeInBytes = cm_MM_GetAllocatorSize(pDspDesc->allocator[SDRAM_CODE]->allocDesc);
#if defined(__STN_8500) && (__STN_8500 > 10)
    _init_Segment(&pDspDesc->segments[SDRAM_CODE_EE],
            pDspDesc->allocator[SDRAM_CODE]->baseAddress,
            domainDesc[eeDomain].domain.sdramCode.offset,
            domainDesc[eeDomain].domain.sdramCode.size);
    _init_Segment(&pDspDesc->segments[SDRAM_CODE_USER],
            pDspDesc->allocator[SDRAM_CODE]->baseAddress,
            domainDesc[eeDomain].domain.sdramCode.offset + domainDesc[eeDomain].domain.sdramCode.size,
            sizeInBytes - domainDesc[eeDomain].domain.sdramCode.size);
#else
    _init_Segment(&pDspDesc->segments[SDRAM_CODE_EE],
            pDspDesc->allocator[SDRAM_CODE]->baseAddress,
            0x0,
            sizeInBytes);
#endif

    sizeInBytes = cm_MM_GetAllocatorSize(pDspDesc->allocator[ESRAM_CODE]->allocDesc);
#if defined(__STN_8500) && (__STN_8500 > 10)
   _init_Segment(&pDspDesc->segments[ESRAM_CODE_EE],
            pDspDesc->allocator[ESRAM_CODE]->baseAddress,
            domainDesc[eeDomain].domain.esramCode.offset,
            domainDesc[eeDomain].domain.esramCode.size);
    _init_Segment(&pDspDesc->segments[ESRAM_CODE_USER],
            pDspDesc->allocator[ESRAM_CODE]->baseAddress,
            domainDesc[eeDomain].domain.esramCode.offset + domainDesc[eeDomain].domain.esramCode.size,
            sizeInBytes - domainDesc[eeDomain].domain.esramCode.size);
#else
    _init_Segment(&pDspDesc->segments[ESRAM_CODE_EE],
            pDspDesc->allocator[ESRAM_CODE]->baseAddress,
            0x0,
            sizeInBytes);
#endif

    //the difference in the following code is the segment size used to calculate the top!!
    sizeInBytes = cm_MM_GetAllocatorSize(pDspDesc->allocator[SDRAM_EXT16]->allocDesc);
#if defined(__STN_8500) && (__STN_8500 > 10)
    _init_Segment(&pDspDesc->segments[SDRAM_DATA_EE],
            pDspDesc->allocator[SDRAM_EXT16]->baseAddress,
            domainDesc[eeDomain].domain.sdramData.offset,
            domainDesc[eeDomain].domain.sdramData.size);
    _init_Segment(&pDspDesc->segments[SDRAM_DATA_USER],
            pDspDesc->allocator[SDRAM_EXT16]->baseAddress,
            domainDesc[eeDomain].domain.sdramData.offset + domainDesc[eeDomain].domain.sdramData.size,
            sizeInBytes - domainDesc[eeDomain].domain.sdramData.size);
#else
    _init_Segment(&pDspDesc->segments[SDRAM_DATA_EE],
            pDspDesc->allocator[SDRAM_EXT16]->baseAddress,
            0x0,
            sizeInBytes);
#endif

    sizeInBytes = cm_MM_GetAllocatorSize(pDspDesc->allocator[ESRAM_EXT16]->allocDesc);
#if defined(__STN_8500) && (__STN_8500 > 10)
    _init_Segment(&pDspDesc->segments[ESRAM_DATA_EE],
            pDspDesc->allocator[ESRAM_EXT16]->baseAddress,
            domainDesc[eeDomain].domain.esramData.offset,
            domainDesc[eeDomain].domain.esramData.size);
    _init_Segment(&pDspDesc->segments[ESRAM_DATA_USER],
            pDspDesc->allocator[ESRAM_EXT16]->baseAddress,
            domainDesc[eeDomain].domain.esramData.offset + domainDesc[eeDomain].domain.esramData.size,
            sizeInBytes - domainDesc[eeDomain].domain.esramData.size);
#else
    _init_Segment(&pDspDesc->segments[ESRAM_DATA_EE],
            pDspDesc->allocator[ESRAM_EXT16]->baseAddress,
            0x0,
            sizeInBytes);
#endif

    return CM_OK;
}

//TODO, juraj, reuse cm_DSP_UpdateBase functions
static t_cm_error mmdsp_Configure(t_nmf_core_id coreId, t_mmdsp_hw_regs *pRegs, const t_dsp_desc *pDspDesc)
{
    t_uint64 regValue;
    static const t_uint64 coreId2stbusId[NB_CORE_IDS] =
    {
        0, /* ARM_CORE_ID no meaning */
        SVA_STBUS_ID, /* SVA_CORE_ID */
        SIA_STBUS_ID  /* SIA_CORE_ID */
    };

    //t_cm_system_address sysAddr;
    //t_cm_size sizeInBytes;

    /* Stop core (stop clock) */
    MMDSP_RESET_CORE(pRegs);
    {
        volatile t_uint32 loopme = 0xfff;
        while(loopme--) ;
    }
    MMDSP_STOP_CORE(pRegs);
    {
        volatile t_uint32 loopme = 0xfff;
        while(loopme--) ;
    }

#if 0
    /* Reset DSP internal memory (xram) */
    {
        t_uint32 *pSrc = (t_uint32 *)(pRegs->mem24);
        t_uint32 tcmSize;
        int i;
        cm_MM_GetAllocatorSize(pDspDesc->allocator[INTERNAL_XRAM], &sizeInBytes);
        tcmSize = sizeInBytes;
        cm_MM_GetAllocatorSize(pDspDesc->allocator[INTERNAL_YRAM], &sizeInBytes);
        tcmSize += sizeInBytes;
        for (i = 0; i < (tcmSize/sizeof(t_uint32)); i++)
            *(pSrc++) = 0;
    }
#endif

    /* Configure all blocks as X only, except the Y ones (MOVED TO mmdsp_InitAfterBoot()) */

    /* __STN_8815 --> __STN_8820 or __STN_8500 */
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_STBUS_ID_CONF_REG, coreId2stbusId[coreId]);

    /* Configure External Bus timeout reg */
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_EN_EXT_BUS_TIMEOUT_REG, IHOST_TIMEOUT_ENABLE);

    /* Program memory management */
#if defined(__STN_8500) && (__STN_8500 > 10)
    {
        const t_uint32 r0 = CODE_ADDRESS_BASE[1] >> 10;
        const t_uint32 r1 = CODE_ADDRESS_BASE[2] >> 10;
        const t_uint32 r2 = CODE_ADDRESS_BASE[3] >> 10;
        const t_uint32 sdram0 = pDspDesc->segments[SDRAM_CODE_EE].base.physical;
        const t_uint32 sdram1 = pDspDesc->segments[SDRAM_CODE_USER].base.physical;
        const t_uint32 esram0 = pDspDesc->segments[ESRAM_CODE_EE].base.physical;
        const t_uint32 esram1 = pDspDesc->segments[ESRAM_CODE_USER].base.physical;

        /* Bases for first two segments, going to sdram */
        regValue = ((t_uint64)(sdram1) << IHOST_PRG_BASE2_ADDR_SHIFT) + (t_uint64)sdram0;
        WRITE_INDIRECT_HOST_REG(pRegs, IHOST_PRG_BASE_ADDR_REG, regValue);

        /* Bases for second two segments, going to esram */
        regValue = ((t_uint64)(esram1) << IHOST_PRG_BASE4_ADDR_SHIFT) + (t_uint64)esram0;
        WRITE_INDIRECT_HOST_REG(pRegs, IHOST_PRG_BASE_34_ADDR_REG, regValue);

        /* Split mmdsp program adress-space and activate the mechanism */
        regValue =  (t_uint64)((t_uint64)(r2) << 48 | (t_uint64)(r1) <<32 | (t_uint64)(r0) << 16 | 1);
        WRITE_INDIRECT_HOST_REG(pRegs, IHOST_PRG_BASE2_ACTIV_REG, regValue);
    }
#else
    {
        const t_uint32 sdram0 = pDspDesc->segments[SDRAM_CODE_EE].base.physical;
        const t_uint32 esram0 = pDspDesc->segments[ESRAM_CODE_EE].base.physical;

        regValue = (t_uint64)sdram0 | ( ((t_uint64)esram0) << IHOST_PRG_BASE2_ADDR_SHIFT );
        WRITE_INDIRECT_HOST_REG(pRegs, IHOST_PRG_BASE_ADDR_REG, regValue);

        WRITE_INDIRECT_HOST_REG(pRegs, IHOST_PRG_BASE2_ACTIV_REG, IHOST_PRG_BASE2_ACTIV_ON);
    }
#endif

    /* Data_16/24 memory management */
#if defined(__STN_8500) && (__STN_8500 > 10)
    /* Segments 1 and 2 for 16/24 map to sdram continuously */
    /* Base 1 */
    regValue = (((t_uint64)pDspDesc->segments[SDRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE_24_SHIFT) |
               (((t_uint64)pDspDesc->segments[SDRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_BASE_REG, regValue);
    /* Top 1 */
    regValue = (((t_uint64)(pDspDesc->segments[SDRAM_DATA_EE].base.physical + pDspDesc->segments[SDRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP_24_SHIFT) |
               (((t_uint64)(pDspDesc->segments[SDRAM_DATA_EE].base.physical + pDspDesc->segments[SDRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_TOP_16_24_REG, regValue);

    /* Base 2 */
    regValue = (((t_uint64)pDspDesc->segments[SDRAM_DATA_USER].base.physical) << IHOST_DATA_EXT_BUS_BASE2_24_SHIFT) |
               (((t_uint64)pDspDesc->segments[SDRAM_DATA_USER].base.physical) << IHOST_DATA_EXT_BUS_BASE2_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_BASE2_REG, regValue);
    /* Top 2 */
    regValue = (((t_uint64)(pDspDesc->segments[SDRAM_DATA_USER].base.physical + pDspDesc->segments[SDRAM_DATA_USER].size - 1)) << IHOST_DATA_EXT_BUS_TOP2_24_SHIFT) |
               (((t_uint64)(pDspDesc->segments[SDRAM_DATA_USER].base.physical + pDspDesc->segments[SDRAM_DATA_USER].size - 1)) << IHOST_DATA_EXT_BUS_TOP2_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_EXT_BUS_TOP2_16_24_REG, regValue);

    /* Segments 3 and 4 for 16/24 map to esram continuously */
    /* Base 3 */
    regValue = (((t_uint64)pDspDesc->segments[ESRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE3_24_SHIFT) |
               (((t_uint64)pDspDesc->segments[ESRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE3_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_BASE3_REG, regValue);
    /* Top 3 */
    regValue = (((t_uint64)(pDspDesc->segments[ESRAM_DATA_EE].base.physical + pDspDesc->segments[ESRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP3_24_SHIFT) |
               (((t_uint64)(pDspDesc->segments[ESRAM_DATA_EE].base.physical + pDspDesc->segments[ESRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP3_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_EXT_BUS_TOP3_16_24_REG, regValue);

    /* Base 4 */
    regValue = (((t_uint64)pDspDesc->segments[ESRAM_DATA_USER].base.physical) << IHOST_DATA_EXT_BUS_BASE4_24_SHIFT) |
               (((t_uint64)pDspDesc->segments[ESRAM_DATA_USER].base.physical) << IHOST_DATA_EXT_BUS_BASE4_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_BASE4_REG, regValue);
    /* Top 4 */
    regValue = (((t_uint64)(pDspDesc->segments[ESRAM_DATA_USER].base.physical + pDspDesc->segments[ESRAM_DATA_USER].size - 1)) << IHOST_DATA_EXT_BUS_TOP4_24_SHIFT) |
               (((t_uint64)(pDspDesc->segments[ESRAM_DATA_USER].base.physical + pDspDesc->segments[ESRAM_DATA_USER].size - 1)) << IHOST_DATA_EXT_BUS_TOP4_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_EXT_BUS_TOP4_16_24_REG, regValue);

    /* Define base 2 thresholds/offset (1MB for each up segment) */
    regValue =  ((t_uint64)DATA_ADDRESS_BASE[1].startAddress[1]>>SHIFT_HALFWORD1)<< IHOST_DATA2_24_XA_BASE_SHIFT;
    regValue |= ((t_uint64)DATA_ADDRESS_BASE[1].startAddress[0]>>SHIFT_HALFWORD1)<< IHOST_DATA2_16_XA_BASE_SHIFT;

    /* Define base 3 thresholds/offset (1MB for each up segment) */
    regValue |= ((t_uint64)DATA_ADDRESS_BASE[2].startAddress[1]>>SHIFT_HALFWORD1)<< IHOST_DATA3_24_XA_BASE_SHIFT;
    regValue |= ((t_uint64)DATA_ADDRESS_BASE[2].startAddress[0]>>SHIFT_HALFWORD1)<< IHOST_DATA3_16_XA_BASE_SHIFT;

    /* Define base 4 thresholds/offset (1MB for each up segment) */
    regValue |= ((t_uint64)DATA_ADDRESS_BASE[3].startAddress[1]>>SHIFT_HALFWORD1)<< IHOST_DATA4_24_XA_BASE_SHIFT;
    regValue |= ((t_uint64)DATA_ADDRESS_BASE[3].startAddress[0]>>SHIFT_HALFWORD1)<< IHOST_DATA4_16_XA_BASE_SHIFT;
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA2_1624_XA_BASE_REG, regValue);

#else
    /* Program data24/16 base 1 */
    regValue = (((t_uint64)pDspDesc->segments[SDRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE_24_SHIFT) |
               (((t_uint64)pDspDesc->segments[SDRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_BASE_REG, regValue);

    /* Program data24/16 top 1 */
    regValue = (((t_uint64)(pDspDesc->segments[SDRAM_DATA_EE].base.physical + pDspDesc->segments[SDRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP_24_SHIFT) |
               (((t_uint64)(pDspDesc->segments[SDRAM_DATA_EE].base.physical + pDspDesc->segments[SDRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_TOP_16_24_REG, regValue);

    /* Program data24/16 base 2 */
    regValue = (((t_uint64)pDspDesc->segments[ESRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE2_24_SHIFT) |
                (((t_uint64)pDspDesc->segments[ESRAM_DATA_EE].base.physical) << IHOST_DATA_EXT_BUS_BASE2_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_EXT_BUS_BASE2_REG, regValue);

    /* Program data24/16 top 2 */
    regValue = (((t_uint64)(pDspDesc->segments[ESRAM_DATA_EE].base.physical + pDspDesc->segments[ESRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP2_24_SHIFT) |
               (((t_uint64)(pDspDesc->segments[ESRAM_DATA_EE].base.physical + pDspDesc->segments[ESRAM_DATA_EE].size - 1)) << IHOST_DATA_EXT_BUS_TOP2_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_EXT_BUS_TOP2_16_24_REG, regValue);

    /* Define base 2 thresholds/offset (1MB for each up segment) */
    regValue = ((t_uint64)(DATA_ADDRESS_BASE[1].startAddress[1]>>SHIFT_HALFWORD1))<< IHOST_DATA2_24_XA_BASE_SHIFT; // Top address minus ONE_MB => 256KW (24/32-bit)
    regValue |= ((t_uint64)(DATA_ADDRESS_BASE[1].startAddress[0]>>SHIFT_HALFWORD1))<< IHOST_DATA2_16_XA_BASE_SHIFT; // Top address minus ONE_MB => 512KW (16-bit)
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA2_1624_XA_BASE_REG, regValue);
#endif

    /* Enable top check */
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_TOP_16_24_CHK_REG, IHOST_DATA_TOP_16_24_CHK_ON);

    /* Enable both bases */
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_DATA_BASE2_ACTIV_REG, IHOST_DATA_BASE2_ACTIV_ON);

    /* MMIO management */
    regValue = (((t_uint64)STM_BASE_ADDR) << IHOST_EXT_MMIO_BASE_ADDR_SHIFT) |
            (((t_uint64)DMA_CTRL_END_ADDR) << IHOST_EXT_MMIO_DATA_EXT_BUS_TOP_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_EXT_MMIO_BASE_DATA_EXT_BUS_TOP_REG, regValue);

    /* Configure Icache */
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_INST_BURST_SZ_REG, IHOST_INST_BURST_SZ_AUTO);

    regValue = (t_uint64)(IHOST_ICACHE_MODE_PERFMETER_OFF | IHOST_ICACHE_MODE_L2_CACHE_ON |
            IHOST_ICACHE_MODE_L1_CACHE_ON | IHOST_ICACHE_MODE_FILL_MODE_OFF);
    WRITE_INDIRECT_HOST_REG(pRegs, IHOST_ICACHE_MODE_REG, regValue);
    
    return CM_OK;
}

PUBLIC t_cm_error cm_DSP_updateCodeBase(
        t_nmf_core_id coreId,
        t_dsp_segment_type hwSegment,
        t_cm_system_address src,
        t_cm_system_address dst
        )
{
#if defined(__STN_8500) && (__STN_8500 > 10)
    t_mmdsp_hw_regs *pRegs = pMmdspRegs[coreId];
    t_uint32 offset = src.physical - mpcDesc[coreId].segments[hwSegment].base.physical;
    t_cm_system_address base;
    t_uint32 altBase = 0;
    t_uint64 regValue = 0;
    t_uint8  reg = 0;

    base.physical = dst.physical - offset;
    base.logical  = dst.logical  - offset;

    switch(hwSegment) {
    case SDRAM_CODE_EE:
        altBase  = mpcDesc[coreId].segments[SDRAM_CODE_USER].base.physical;
        regValue = ((t_uint64)(altBase) << IHOST_PRG_BASE2_ADDR_SHIFT) + (t_uint64)base.physical;
        reg = IHOST_PRG_BASE_ADDR_REG;
        break;
    case SDRAM_CODE_USER:
        altBase  = mpcDesc[coreId].segments[SDRAM_CODE_EE].base.physical;
        regValue = ((t_uint64)(base.physical) << IHOST_PRG_BASE2_ADDR_SHIFT) + (t_uint64)altBase;
        reg = IHOST_PRG_BASE_ADDR_REG;
        break;
    case ESRAM_CODE_EE:
        altBase = mpcDesc[coreId].segments[ESRAM_CODE_USER].base.physical;
        regValue = ((t_uint64)(altBase) << IHOST_PRG_BASE4_ADDR_SHIFT) + (t_uint64)base.physical;
        reg = IHOST_PRG_BASE_34_ADDR_REG;
        break;
    case ESRAM_CODE_USER:
        altBase = mpcDesc[coreId].segments[ESRAM_CODE_EE].base.physical;
        regValue = ((t_uint64)(base.physical) << IHOST_PRG_BASE4_ADDR_SHIFT) + (t_uint64)altBase;
        reg = IHOST_PRG_BASE_34_ADDR_REG;
        break;
    default:
        CM_ASSERT(0);
    }

    LOG_INTERNAL(1, "##### DSP Code Base Update [%d]: 0x%x -> 0x%x (0x%x)\n",
            hwSegment, mpcDesc[coreId].segments[hwSegment].base.physical, base.physical, base.logical, 0, 0);

    WRITE_INDIRECT_HOST_REG(pRegs, reg, regValue);

    mpcDesc[coreId].segments[hwSegment].base = base;
#endif
    return CM_OK;
}

PUBLIC t_cm_error cm_DSP_updateDataBase(
        t_nmf_core_id coreId,
        t_dsp_segment_type hwSegment,
        t_cm_system_address src,
        t_cm_system_address dst
        )
{
#if defined(__STN_8500) && (__STN_8500 > 10)
    t_mmdsp_hw_regs *pRegs = pMmdspRegs[coreId];
    t_uint32 offset = src.physical - mpcDesc[coreId].segments[hwSegment].base.physical;
    t_cm_system_address base;
    t_uint32 size = mpcDesc[coreId].segments[hwSegment].size; //in bytes
    t_uint64 regValue;
    t_uint8  reg = 0;
    t_uint8  top = 0;

    base.physical = dst.physical - offset;
    base.logical  = dst.logical  - offset;

    switch(hwSegment) {
    case SDRAM_DATA_EE:
        reg = IHOST_DATA_EXT_BUS_BASE_REG;
        top = IHOST_DATA_EXT_BUS_TOP_16_24_REG;
        break;
    case SDRAM_DATA_USER:
        reg = IHOST_DATA_EXT_BUS_BASE2_REG;
        top = IHOST_EXT_BUS_TOP2_16_24_REG;
        break;
    case ESRAM_DATA_EE:
        reg = IHOST_DATA_EXT_BUS_BASE3_REG;
        top = IHOST_EXT_BUS_TOP3_16_24_REG;
        break;
    case ESRAM_DATA_USER:
        reg = IHOST_DATA_EXT_BUS_BASE4_REG;
        top = IHOST_EXT_BUS_TOP4_16_24_REG;
        break;
    default:
        CM_ASSERT(0);
    }

    LOG_INTERNAL(1, "##### DSP Data Base Update [%d]: 0x%x -> 0x%x (0x%x)\n",
            hwSegment, mpcDesc[coreId].segments[hwSegment].base.physical, base.physical, base.logical, 0, 0);

    /* Program data24/16 base */
    regValue = (((t_uint64)(base.physical)) << IHOST_DATA_EXT_BUS_BASE2_24_SHIFT) |
               (((t_uint64)(base.physical)) << IHOST_DATA_EXT_BUS_BASE2_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, reg, regValue);

    /* Program data24/16 top */
    regValue = (((t_uint64)(base.physical + size - 1)) << IHOST_DATA_EXT_BUS_TOP2_24_SHIFT) |
               (((t_uint64)(base.physical + size - 1)) << IHOST_DATA_EXT_BUS_TOP2_16_SHIFT);
    WRITE_INDIRECT_HOST_REG(pRegs, top, regValue);

    mpcDesc[coreId].segments[hwSegment].base = base;
#endif
    return CM_OK;
}

PUBLIC t_cm_error cm_DSP_GetDspDataAddressInfo(t_nmf_core_id coreId, t_uint32 addr, t_dsp_address_info *info)
{
    t_uint32 i, j;

    for(j = 0; j < 2; j++)
    {
        for(i = 0; i < DATA_BASE_NUMBER; i++)
        {
            if(DATA_ADDRESS_BASE[i].startAddress[j] <= addr && addr < DATA_ADDRESS_BASE[i + 1].startAddress[j])
            {
                info->segmentType = DATA_ADDRESS_BASE[i].segmentType;
                info->baseOffset = (addr - DATA_ADDRESS_BASE[i].startAddress[j]) * (2 + j * 2);

                return CM_OK;
            }
        }
    }

    CM_ASSERT(0);
    //return CM_INVALID_PARAMETER;
}

static t_cm_error mmdsp_ConfigureAfterBoot(t_nmf_core_id coreId, t_uint8 nbXramBlocks, t_uint8 nbYramBlocks)
{
    /* Configure all blocks as X only, except the Y ones */
    pMmdspRegs[coreId]->mmio_16.config_data_mem.value = (t_uint16)(~(((1U << nbYramBlocks) - 1) << (nbXramBlocks-nbYramBlocks)));
    
#if defined(__STN_8500) && (__STN_8500 > 10)
    /* enable write posting */
    MMDSP_ENABLE_WRITE_POSTING(pMmdspRegs[coreId]);
#endif
    
    return CM_OK;
}


