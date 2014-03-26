/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief DSP abstraction layer
 *
 * \defgroup DSP_INTERNAL Private DSP Abstraction Layer API.
 *
 */
#ifndef __INC_CM_DSP_H
#define __INC_CM_DSP_H

#include <cm/inc/cm_type.h>
#include <share/inc/nmf.h>
#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/memory/inc/remote_allocator.h>


#define SxA_NB_BLOCK_RAM        8 /*32kworks (24-bit) */

#define SxA_LOCKED_WAY          1

/*
 * Type defintion to handle dsp offset in word
 */
typedef t_uint32 t_dsp_offset;

typedef t_uint32 t_dsp_address;

typedef enum {
    DSP2ARM_IRQ_0,
    DSP2ARM_IRQ_1
} t_mpc2host_irq_num;

typedef enum {
    ARM2DSP_IRQ_0,
    ARM2DSP_IRQ_1,
    ARM2DSP_IRQ_2,
    ARM2DSP_IRQ_3
} t_host2mpc_irq_num;

typedef enum {
    INTERNAL_XRAM24 = 0,  /* 24-bit XRAM */
    INTERNAL_XRAM16 = 1,  /* 16-bit XRAM */
    INTERNAL_YRAM24 = 2,  /* 24-bit YRAM */
    INTERNAL_YRAM16 = 3,  /* 16-bit YRAM */
    SDRAM_EXT24     = 4,  /* 24-bit external "X" memory */
    SDRAM_EXT16     = 5,  /* 16-bit external "X" memory */
    ESRAM_EXT24     = 6,  /* ESRAM24 */
    ESRAM_EXT16     = 7,  /* ESRAM16 */
    SDRAM_CODE      = 8,  /* Program memory */
    ESRAM_CODE      = 9,  /* ESRAM code */
    LOCKED_CODE     = 10, /* For way locking */
    NB_DSP_MEMORY_TYPE,
    DEFAULT_DSP_MEM_TYPE = MASK_ALL16
} t_dsp_memory_type_id;

typedef struct {
    t_cm_allocator_desc  *allocDesc;
    t_cm_system_address   baseAddress;
    t_uint32              referenceCounter;
} t_dsp_allocator_desc;

typedef struct {
    t_cm_system_address  base;
    t_uint32             size;
} t_dsp_segment;

typedef enum {
#if defined(__STN_8500) && (__STN_8500 > 10)
    SDRAM_CODE_EE,
    SDRAM_CODE_USER,
    SDRAM_DATA_EE,
    SDRAM_DATA_USER,
    NB_MIGRATION_SEGMENT,
    ESRAM_CODE_EE = NB_MIGRATION_SEGMENT,
    ESRAM_CODE_USER,
    ESRAM_DATA_EE,
    ESRAM_DATA_USER,
#else
    SDRAM_CODE_EE,
    SDRAM_DATA_EE,
    ESRAM_CODE_EE,
    ESRAM_DATA_EE,
#endif
    NB_DSP_SEGMENT_TYPE
} t_dsp_segment_type;

typedef struct {
    t_dsp_segment_type  segmentType;
    t_uint32            baseOffset;
} t_dsp_address_info;

typedef enum  {
    MPC_STATE_UNCONFIGURED,
    MPC_STATE_BOOTABLE,
    MPC_STATE_BOOTED,
    MPC_STATE_PANIC,
} t_dsp_state;

typedef struct {
    t_dsp_state             state;
    t_uint8                 nbYramBank;
    t_cm_domain_id          domainEE;
    t_dsp_allocator_desc   *allocator[NB_DSP_MEMORY_TYPE];
    t_dsp_segment           segments[NB_DSP_SEGMENT_TYPE];
    t_uint32                yram_offset;
    t_uint32                yram_size;
    t_uint32                locked_offset;
    t_uint32                locked_size;
} t_dsp_desc;

typedef struct {
    t_nmf_core_id coreId;
    t_dsp_memory_type_id memType; // Index in MPC desc allocator
    t_cm_allocator_desc *alloc;
} t_dsp_chunk_info;

PUBLIC const t_dsp_desc* cm_DSP_GetState(t_nmf_core_id coreId);
PUBLIC void cm_DSP_SetStatePanic(t_nmf_core_id coreId);

PUBLIC void cm_DSP_Init(const t_nmf_memory_segment *pEsramDesc);
PUBLIC void cm_DSP_Destroy(void);

/*!
 * \brief Initialize the memory segments management of a given MPC
 *
 * \param[in] coreId Identifier of the DSP to initialize
 * \param[in] pDspMapDesc DSP mapping into host space
 * \param[in] memConf configuration of the DSP memories (standalone or shared)
 *
 * \retval t_cm_error
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_Add(t_nmf_core_id coreId,
        t_uint8 nbYramBanks,
        const t_cm_system_address *pDspMapDesc,
        const t_cm_domain_id eeDomain,
        t_dsp_allocator_desc *sdramCodeAllocDesc,
        t_dsp_allocator_desc *sdramDataAllocDesc);



/*!
 * \brief Configure a given Media Processor Core
 *
 * This routine programs the configuration (caches, ahb wrapper, ...) registers of a given MPC.
 *
 * \param[in] coreId Identifier of the DSP to initialize
 *
 * \retval t_cm_error
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_Boot(t_nmf_core_id coreId);

/*!
 * \brief Boot a given DSP
 *
 * This routine allows after having initialized and loaded the EE into a given DSP to start it (boot it)
 *
 * \param[in] coreId identifier of the DSP to boot
 * \param[in] panicReasonOffset offset of panic reason which will pass to NONE_PANIC when DSP booted.
 *
 * \retval t_cm_error
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_ConfigureAfterBoot(t_nmf_core_id coreId);

PUBLIC void cm_DSP_Start(t_nmf_core_id coreId);

PUBLIC void cm_DSP_Stop(t_nmf_core_id coreId);

/*!
 * \brief Shutdown a given DSP
 *
 * This routine allows to stop and shutdown a given DSP
 *
 * \param[in] coreId identifier of the DSP to shutdown
 *
 * \retval t_cm_error
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_Shutdown(t_nmf_core_id coreId);

PUBLIC t_uint32 cm_DSP_ReadXRamWord(t_nmf_core_id coreId, t_uint32 dspOffset);
PUBLIC void cm_DSP_WriteXRamWord(t_nmf_core_id coreId, t_uint32 dspOffset, t_uint32 value);

/*!
 * \brief Convert a Dsp address (offset inside a given DSP memory segment) into the  host address (logical)
 *
 * \param[in] coreId identifier of the given DSP
 * \param[in] dspAddress dsp address to be converted
 * \param[in] memType memory type identifier
 *
 * \retval t_cm_logical_address
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_logical_address cm_DSP_ConvertDspAddressToHostLogicalAddress(t_nmf_core_id coreId, t_shared_addr dspAddress);

/*!
 * \brief Acknowledge the local interrupt of a given DSP (when not using HW semaphore mechanisms)
 *
 * \param[in] coreId identifier of the given DSP
 * \param[in] irqNum irq identifier
 *
 * \retval void
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_AcknowledgeDspIrq(t_nmf_core_id coreId, t_mpc2host_irq_num irqNum);


/*
 * Memory Management API routines
 */

/*!
 * \brief Retrieve DSP information for a memory chunk.
 *
 * This function retrieves information stored in user-data of the allocated chunk.
 * See also \ref{t_dsp_chunk_info}.
 *
 * \param[in]  memHandle Handle to the allocated chunk.
 * \param[out] info Dsp information structure.
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_GetDspChunkInfo(t_memory_handle memHandle, t_dsp_chunk_info *info);

/*!
 * \brief Get memory allocator for a given memory type on a DSP.
 *
 * \param[in]  coreId Dsp identifier.
 * \param[in]  memType Memory type identifier.
 *
 * \retval reference to the allocator descriptor (or null)
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_allocator_desc* cm_DSP_GetAllocator(t_nmf_core_id coreId, t_dsp_memory_type_id memType);

/*!
 * \brief Get DSP internal memory (TCM) information for allocation.
 *
 * For DSP-internal memories (TCMX, Y 16/24), return the offset and size of the allocation zone (for domain
 * mechanism) and the allocation memory type.
 *
 * \param[in]  coreId Dsp identifier.
 * \param[in]  memType Memory type identifier.
 * \param[out] mem_info Memory information structure.
 *
 * \retval CM_OK
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_GetInternalMemoriesInfo(t_cm_domain_id domainId, t_dsp_memory_type_id memType,
        t_uint32 *offset, t_uint32 *size);


/*!
 * \brief Convert word size to byte size.
 *
 * \param[in] memType Memory type identifier.
 * \param[in] wordSize Word size to be converted.
 *
 * \retval Byte size.
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_uint32 cm_DSP_ConvertSize(t_dsp_memory_type_id memType, t_uint32 wordSize);

/*!
 * \brief Provide the Memory status of a given memory type for a given DSP
 *
 * \param[in] coreId dsp identifier.
 * \param[in] memType Type of memory.
 * \param[out] pStatus requested memory status
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_GetAllocatorStatus(t_nmf_core_id coreId, t_dsp_memory_type_id memType, t_uint32 offset, t_uint32 size, t_cm_allocator_status *pStatus);

/*!
 * \brief Provide DSP memory host shared address
 *
 * \param[in] memHandle Allocated block handle
 * \param[out] pAddr Returned system address.
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_GetHostSystemAddress( t_memory_handle memHandle, t_cm_system_address *pAddr);

/*!
 * \brief Get physical address of a memory chunk.
 *
 * \param[in] memHandle Memory handle.
 *
 * \retval Physical address.
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_physical_address cm_DSP_GetPhysicalAdress(t_memory_handle memHandle);

/*!
 * \brief Return Logical Address of an allocated memory chunk.
 *
 * \param[in] memHandle Allocated chunk handle
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_logical_address cm_DSP_GetHostLogicalAddress(t_memory_handle memHandle);

/*!
 * \brief Provide DSP memory DSP address (offset inside a given DSP memory segment)
 *
 * \param[in] memHandle Allocated block handle
 * \param[out] dspAddress allocated block address seen by the given DSP
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_GetDspAddress(t_memory_handle handle, t_uint32 *pDspAddress);

/*!
 * \brief Return the adress of the DSP base associated to the memory type.
 * Caution, this information is valid only in normal state (not when migrated).
 *
 * \param[in]  coreId DSP Identifier.
 * \param[in]  memType Type of memory.
 * \param[out] pAddr Base address.
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_GetDspBaseAddress(t_nmf_core_id coreId, t_dsp_memory_type_id memType, t_cm_system_address *pAddr);

/*!
 * \brief Return DSP memory handle offset (offset inside a given DSP memory)
 *
 * \param[in] coreId dsp identifier.
 * \param[in] memType Type of memory.
 * \param[in] memHandle Allocated block handle
 *
 * \retval t_uint32: Offset of memory handle inside memory
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_uint32 cm_DSP_GetDspMemoryHandleOffset(
        t_nmf_core_id coreId,
        t_dsp_memory_type_id dspMemType,
        t_memory_handle memHandle);

/*!
 * \brief Provide DSP memory handle size
 *
 * \param[in] memHandle Allocated block handle
 * \param[out] pDspSize Size of the given memory handle

 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC void cm_DSP_GetDspMemoryHandleSize(t_memory_handle memHandle, t_uint32 *pDspSize);

/*!
 * \brief Resize xram allocator to reserve spave for stack.
 *
 * \param[in] coreId dsp identifier.
 * \param[in] newStackSize New required stack size.

 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_setStackSize(t_nmf_core_id coreId, t_uint32 newStackSize);

/*!
 * \brief Allow to know if nbYramBanks parameter is valid for coreId. This api is need since use of nbYramBanks
 *        is deferred.
 *
 * \param[in] coreId dsp identifier.
 * \param[in] nbYramBanks number of yramBanks to use.
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_IsNbYramBanksValid(t_nmf_core_id coreId, t_uint8 nbYramBanks);

/*!
 * \brief Allow to know stack base address according to coreId and nbYramBanks use.
 *
 * \param[in] coreId dsp identifier.
 * \param[in] nbYramBanks number of yramBanks to use.
 *
 * \retval t_uint32 return stack address
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_uint32 cm_DSP_getStackAddr(t_nmf_core_id coreId);

/*!
 * \brief For a give dsp adress return the offset from the hardware base that the adress is relative to.
 *
 * \param[in]  coreId DSP identifier.
 * \param[in]  adr    DSP address.
 * \param[out] info   Info structure containing (hw base id, offset)
 *
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_GetDspDataAddressInfo(t_nmf_core_id coreId, t_uint32 adr, t_dsp_address_info *info);

/*!
 * \brief Modify the mapping of a code hardware base. Used for memory migration.
 *
 * The function calculates the new hardware base so that in the DSP address-space,
 * the source address will be mapped to the destination address.
 *
 * \param[in] coreId DSP Identifier.
 * \param[in] hwSegment Identifier of the hardware segment (thus hardware base).
 * \param[in] src Source address
 * \param[in] dst Destination address
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_updateCodeBase(t_nmf_core_id coreId, t_dsp_segment_type hwSegment, t_cm_system_address src, t_cm_system_address dst);

/*!
 * \brief Modify the mapping of a data hardware base. Used for memory migration.
 *
 * The function calculates the new hardware base so that in the DSP address-space,
 * the source address will be mapped to the destination address.
 *
 * \param[in] coreId DSP Identifier.
 * \param[in] hwSegment Identifier of the hardware segment (thus hardware base).
 * \param[in] src Source address
 * \param[in] dst Destination address
 *
 * \retval t_cm_error
 * \ingroup DSP_INTERNAL
 */
PUBLIC t_cm_error cm_DSP_updateDataBase(t_nmf_core_id coreId, t_dsp_segment_type hwSegment, t_cm_system_address src, t_cm_system_address dst);

#endif /* __INC_CM_DSP_H */
