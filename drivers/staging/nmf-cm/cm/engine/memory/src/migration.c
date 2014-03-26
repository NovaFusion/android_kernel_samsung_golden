/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/inc/cm_type.h>
#include <inc/type.h>
#include <inc/nmf-limits.h>

#include <cm/engine/communication/fifo/inc/nmf_fifo_arm.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/memory/inc/migration.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/mem.h>

#if defined(__STN_8500) && (__STN_8500 > 10)

typedef enum {
    CM_MIGRATION_OK = 0,
    CM_MIGRATION_INVALID_ARGUMENT = 1,
    CM_MIGRATION_ERROR = 2,
} t_cm_migration_error;

extern t_nmf_fifo_arm_desc* mpc2mpcComsFifoId[NB_CORE_IDS][NB_CORE_IDS];

/*!
 * \brief Data structure representing a segment to migrate
 *
 * segment:
 *      - used to determine which mmdsp-hw base is to be updated, index in mpcDesc->segments[] structure
 *      * this is hard-coded in cm_migrate(), could be computed (would be nice) LIMITATION
 * srcAdr.physical:
 *      - new base setting
 *      * computed from the src domain in cm_DM_GetAbsAdresses() which uses the start of the allocator for the memory
 *        this is a LIMITATION, as this information is valid only before migration
 * srcAdr.logical:
 *      - cm_MemCopy()
 *      * computed as srcAdr.logical
 * dstAdr.physical: see srcAdr.physical
 * dstAdr.logical:  see srcAdr.logical
 * size:
 *      - cm_MemCopy()
 *      - setting the top when new base is set
 */
typedef struct {
    t_dsp_segment_type       segment; //!< the link to the segment type
    t_cm_system_address      srcAdr;  //!< source address
    t_cm_system_address      dstAdr;  //!< destination address
    t_uint32                 size;    //!< size of the segment
} t_cm_migration_segment;

/*!
 * \brief Internal data structure 1/ during migration, and 2/ between migration and unmigration calls
 *
 * all needed information are computed before calling _cm_migration_move()
 */
typedef struct {
    t_cm_migration_state    state;                            //!< migration state
    t_nmf_core_id           coreId;                           //!< migration only on one mpc
    t_cm_migration_segment  segments[NB_MIGRATION_SEGMENT];   //!< segments to migrate (selected on migration_move)
    t_memory_handle         handles[NB_MIGRATION_SEGMENT];    //!< memory handles for destination chunks allocated prior migration
} t_cm_migration_internal_state;

static t_cm_migration_internal_state migrationState = {STATE_NORMAL, };

static t_cm_error _cm_migration_initSegment(
        t_dsp_segment_type      dspSegment,
        t_cm_system_address    *srcAdr,
        t_uint32                size,
        t_cm_domain_id          dst,
        t_cm_migration_internal_state *info
        )
{
    t_cm_system_address      dstAdr;
    t_cm_migration_segment  *segment = &info->segments[dspSegment];
    t_memory_handle          handle;

    handle = cm_DM_Alloc(dst, ESRAM_EXT16, size >> 1, CM_MM_ALIGN_AHB_BURST, TRUE); //note: byte to half-word conversion
    if (handle == 0) {
        ERROR("CM_NO_MORE_MEMORY: Unable to init segment for migration\n", 0, 0, 0, 0, 0, 0);
        return CM_NO_MORE_MEMORY;
    }

    info->handles[dspSegment] = handle;

    cm_DSP_GetHostSystemAddress(handle, &dstAdr);

    segment->segment   = dspSegment; //this is redundant and could be avoided by recoding move(), but nice to have for debug
    segment->size      = size;
    segment->srcAdr    = *srcAdr;
    segment->dstAdr    = dstAdr;

    return CM_OK;
}

static void _cm_migration_releaseSegment(t_cm_migration_internal_state *info, t_dsp_segment_type segId)
{
    cm_DM_Free(info->handles[segId], TRUE);
}

static t_cm_migration_error _cm_migration_release(t_cm_migration_internal_state *info)
{
    t_uint32 i = 0;
    for (i = 0; i < NB_MIGRATION_SEGMENT; i++) {
        cm_DM_Free(info->handles[i], TRUE);
    }

    return CM_MIGRATION_OK;
}

#define SEGMENT_START(seg) \
    seg.offset

#define SEGMENT_END(seg) \
    seg.offset + seg.size

static t_cm_error _cm_migration_check(
        const t_cm_domain_id srcShared,
        const t_cm_domain_id src,
        const t_cm_domain_id dst,
        t_cm_migration_internal_state *info
        )
{
    t_cm_error error = CM_OK;
    t_cm_domain_info domainInfoSrc;
    t_cm_domain_info domainInfoShared;
    t_cm_domain_desc *domainEE;
    t_cm_domain_desc *domainShared;
    t_nmf_core_id coreId = cm_DM_GetDomainCoreId(src);

    //coreIds in src, srcShared and dst match
    if (!((domainDesc[src].domain.coreId == domainDesc[srcShared].domain.coreId)
        && (domainDesc[src].domain.coreId == domainDesc[dst].domain.coreId))) {
        return CM_INVALID_PARAMETER;
    }

    //check srcShared starts at 0
    //FIXME, juraj, today EE code is in SDRAM, but this is flexible, so must find out where EE is instantiated
    if (domainDesc[srcShared].domain.sdramCode.offset != 0x0) {
        return CM_INVALID_PARAMETER;
    }

    //check srcShared contains EE domain
    domainEE = &domainDesc[cm_DSP_GetState(coreId)->domainEE];
    domainShared = &domainDesc[srcShared];
    if ((SEGMENT_START(domainEE->domain.esramCode) < SEGMENT_START(domainShared->domain.esramCode))
      ||(SEGMENT_END(domainEE->domain.esramCode) > SEGMENT_END(domainShared->domain.esramCode))
      ||(SEGMENT_START(domainEE->domain.esramData) < SEGMENT_START(domainShared->domain.esramData))
      ||(SEGMENT_END(domainEE->domain.esramData) > SEGMENT_END(domainShared->domain.esramData))
      ||(SEGMENT_START(domainEE->domain.sdramCode) < SEGMENT_START(domainShared->domain.sdramCode))
      ||(SEGMENT_END(domainEE->domain.sdramCode) > SEGMENT_END(domainShared->domain.sdramCode))
      ||(SEGMENT_START(domainEE->domain.sdramData) < SEGMENT_START(domainShared->domain.sdramData))
      ||(SEGMENT_END(domainEE->domain.sdramData) > SEGMENT_END(domainShared->domain.sdramData))
        ) {
        return CM_INVALID_PARAMETER;
    }

    info->coreId = coreId;
    cm_DM_GetDomainAbsAdresses(srcShared, &domainInfoShared);
    cm_DM_GetDomainAbsAdresses(src, &domainInfoSrc);

    if ((error = _cm_migration_initSegment(SDRAM_CODE_EE, &domainInfoShared.sdramCode,
                                           domainDesc[srcShared].domain.sdramCode.size, dst, info)) != CM_OK)
            goto _migration_error1;
    if ((error = _cm_migration_initSegment(SDRAM_CODE_USER, &domainInfoSrc.sdramCode,
                                           domainDesc[src].domain.sdramCode.size, dst, info)) != CM_OK)
            goto _migration_error2;
    if ((error = _cm_migration_initSegment(SDRAM_DATA_EE, &domainInfoShared.sdramData,
                                           domainDesc[srcShared].domain.sdramData.size, dst, info)) != CM_OK)
            goto _migration_error3;
    if ((error = _cm_migration_initSegment(SDRAM_DATA_USER, &domainInfoSrc.sdramData,
                                           domainDesc[src].domain.sdramData.size, dst, info)) != CM_OK)
            goto _migration_error4;
    return error;

_migration_error4: _cm_migration_releaseSegment(info, SDRAM_DATA_EE);
_migration_error3: _cm_migration_releaseSegment(info, SDRAM_CODE_USER);
_migration_error2: _cm_migration_releaseSegment(info, SDRAM_CODE_EE);
_migration_error1:
    OSAL_Log("Couldn't allocate memory for migration\n", 0, 0, 0, 0, 0, 0);
    return CM_NO_MORE_MEMORY;
}

typedef t_cm_error (*updateBase_t)(t_nmf_core_id, t_dsp_segment_type, t_cm_system_address, t_cm_system_address);

static t_cm_migration_error _cm_migration_move(
        t_nmf_core_id           coreId,
        t_cm_migration_segment *seg,
        updateBase_t            updateBase,
        char*                   name
        )
{
    LOG_INTERNAL(1, "##### Migration %s: 0x%x -> 0x%x\n", name, seg->srcAdr.logical, seg->dstAdr.logical, 0, 0, 0);
    cm_MemCopy((void*)seg->dstAdr.logical, (void*)seg->srcAdr.logical, seg->size);
    updateBase(coreId, seg->segment, seg->srcAdr, seg->dstAdr);
    cm_MemSet((void*)seg->srcAdr.logical, 0xdead, seg->size); //for debug, to be sure that we have actually moved the code and bases

    return CM_MIGRATION_OK;
}

static t_cm_migration_error _cm_migration_update_internal(
        t_cm_migration_internal_state *info,
        t_cm_migration_state state
        )
{
    t_nmf_fifo_arm_desc *pArmFifo;

    migrationState.state = state;

    switch(state) {
    case STATE_MIGRATED:
        //move fifos
        pArmFifo = mpc2mpcComsFifoId[ARM_CORE_ID][info->coreId];
        pArmFifo->fifoDesc = (t_nmf_fifo_desc*)cm_migration_translate(pArmFifo->dspAddressInfo.segmentType, (t_shared_addr)pArmFifo->fifoDescShadow);
        pArmFifo = mpc2mpcComsFifoId[info->coreId][ARM_CORE_ID];
        pArmFifo->fifoDesc = (t_nmf_fifo_desc*)cm_migration_translate(pArmFifo->dspAddressInfo.segmentType, (t_shared_addr)pArmFifo->fifoDescShadow);
        break;

    case STATE_NORMAL:
        //move fifos
        pArmFifo = mpc2mpcComsFifoId[ARM_CORE_ID][info->coreId];
        pArmFifo->fifoDesc = pArmFifo->fifoDescShadow;
        pArmFifo = mpc2mpcComsFifoId[info->coreId][ARM_CORE_ID];
        pArmFifo->fifoDesc = pArmFifo->fifoDescShadow;
        break;

    default:
        OSAL_Log("unknown state", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }

    return CM_MIGRATION_OK;
}

PUBLIC t_cm_error cm_migrate(const t_cm_domain_id srcShared, const t_cm_domain_id src, const t_cm_domain_id dst)
{
    t_cm_migration_error mError;
    t_cm_error error;

    if ((error = _cm_migration_check(srcShared, src, dst, &migrationState)) != CM_OK) {
        return error;
    }

    /* stop DSP execution */
    cm_DSP_Stop(migrationState.coreId);

    /* migrate EE and FX */
    mError = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_CODE_EE], cm_DSP_updateCodeBase, "code");
    if (mError) {
        OSAL_Log("EE code migration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }
    mError = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_DATA_EE], cm_DSP_updateDataBase, "data");
    if (mError) {
        OSAL_Log("EE data migration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }
    /* migrate user domain */
    mError = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_CODE_USER], cm_DSP_updateCodeBase, "code");
    if (mError) {
        OSAL_Log("User code migration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }
    mError = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_DATA_USER], cm_DSP_updateDataBase, "data");
    if (mError) {
        OSAL_Log("User data migration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
   }
    /* update CM internal structures */
    mError = _cm_migration_update_internal(&migrationState, STATE_MIGRATED);
    if (mError) {
        OSAL_Log("Update internal data failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }

    /* Be sure everything has been write before restarting mmdsp */
    OSAL_mb();

    /* resume DSP execution */
    cm_DSP_Start(migrationState.coreId);

    return CM_OK;
}

static void _cm_migration_swapSegments(
        t_cm_migration_segment *segment
        )
{
    t_cm_system_address tmp;
    tmp = segment->dstAdr;
    segment->dstAdr = segment->srcAdr;
    segment->srcAdr = tmp;
}

PUBLIC t_cm_error cm_unmigrate(void)
{
    t_cm_migration_error merror;

    if (migrationState.state != STATE_MIGRATED)
        return CM_INVALID_PARAMETER; //TODO, juraj, define a proper error for this migration case

    cm_DSP_Stop(migrationState.coreId);

    _cm_migration_swapSegments(&migrationState.segments[SDRAM_CODE_EE]);
    _cm_migration_swapSegments(&migrationState.segments[SDRAM_DATA_EE]);
    _cm_migration_swapSegments(&migrationState.segments[SDRAM_CODE_USER]);
    _cm_migration_swapSegments(&migrationState.segments[SDRAM_DATA_USER]);

    merror = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_CODE_EE], cm_DSP_updateCodeBase, "code");
    if (merror) {
        OSAL_Log("EE code unmigration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }
    merror = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_DATA_EE], cm_DSP_updateDataBase, "data");
    if (merror) {
        OSAL_Log("EE data unmigration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }
    merror = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_CODE_USER], cm_DSP_updateCodeBase, "code");
    if (merror) {
        OSAL_Log("User code unmigration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }
    merror = _cm_migration_move(migrationState.coreId, &migrationState.segments[SDRAM_DATA_USER], cm_DSP_updateDataBase, "data");
    if (merror) {
        OSAL_Log("User data unmigration failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }

    /* update CM internal structures */
    merror = _cm_migration_update_internal(&migrationState, STATE_NORMAL);
    if (merror) {
        OSAL_Log("Update internal data failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }

    /* Be sure everything has been write before restarting mmdsp */
    OSAL_mb();

    cm_DSP_Start(migrationState.coreId);

    /* update CM internal structures */
    merror = _cm_migration_release(&migrationState);
    if (merror) {
        OSAL_Log("Update internal data failed", 0, 0, 0, 0, 0, 0);
        CM_ASSERT(0);
    }

    return CM_OK;
}

// here we make the assumption that the offset doesn't depend from the dsp!!
PUBLIC t_uint32 cm_migration_translate(t_dsp_segment_type segmentType, t_uint32 addr)
{
    //TODO, juraj, save delta instead of recalculating it
    t_sint32 offset;
    if (migrationState.state == STATE_MIGRATED) {
        offset = migrationState.segments[segmentType].dstAdr.logical - migrationState.segments[segmentType].srcAdr.logical;
    } else {
        offset = 0;
    }
    return addr + offset;
}

PUBLIC void cm_migration_check_state(t_nmf_core_id coreId, t_cm_migration_state expected)
{
    CM_ASSERT(migrationState.state == expected);
}

#else
PUBLIC t_cm_error cm_migrate(const t_cm_domain_id srcShared, const t_cm_domain_id src, const t_cm_domain_id dst)
{
    return CM_OK;
}

PUBLIC t_cm_error cm_unmigrate(void)
{
    return CM_OK;
}

PUBLIC t_uint32 cm_migration_translate(t_dsp_segment_type segmentType, t_uint32 addr)
{
    return addr;
}

PUBLIC void cm_migration_check_state(t_nmf_core_id coreId, t_cm_migration_state expected)
{
    return;
}
#endif
