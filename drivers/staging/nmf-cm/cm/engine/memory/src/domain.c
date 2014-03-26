/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/inc/cm_type.h>
#include <inc/nmf-limits.h>

#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/memory/inc/migration.h>
#include <cm/engine/memory/inc/chunk_mgr.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/power_mgt/inc/power.h>
#include <cm/engine/trace/inc/trace.h>

/*
 * domain_memory structure is all we need
 */
#define MAX_USER_DOMAIN_NB 64
#define MAX_SCRATCH_DOMAIN_NB 16

t_cm_domain_desc domainDesc[MAX_USER_DOMAIN_NB];
t_cm_domain_scratch_desc domainScratchDesc[MAX_SCRATCH_DOMAIN_NB];

static t_cm_allocator_desc *cm_DM_getAllocator(t_cm_domain_id domainId, t_dsp_memory_type_id memType);
static void cm_DM_DomainError(const t_cm_domain_id parentId, const t_nmf_client_id client);

#define INIT_DOMAIN_STRUCT(domainDesc)        do {       \
    domainDesc.client                   = 0;             \
    domainDesc.type                     = DOMAIN_NORMAL; \
    domainDesc.refcount                 = 0;             \
    domainDesc.domain.coreId            = MASK_ALL8;     \
    domainDesc.domain.esramCode.offset  = 0;             \
    domainDesc.domain.esramCode.size    = 0;             \
    domainDesc.domain.esramData.offset  = 0;             \
    domainDesc.domain.esramData.size    = 0;             \
    domainDesc.domain.sdramCode.offset  = 0;             \
    domainDesc.domain.sdramCode.size    = 0;             \
    domainDesc.domain.sdramData.offset  = 0;             \
    domainDesc.domain.sdramData.size    = 0;             \
    domainDesc.scratch.parent.handle    = 0;             \
    domainDesc.scratch.child.alloc      = 0;             \
    domainDesc.scratch.child.parent_ref = 0;             \
    domainDesc.dbgCooky                 = NULL;          \
    } while (0)

#define FIND_DOMAIN_ID(domainId)                                                           \
    {                                                                                      \
        domainId = 0;                                                                      \
        while (domainDesc[domainId].client != 0 && domainId < MAX_USER_DOMAIN_NB) {     \
            domainId++;                                                                    \
        }                                                                                  \
        if (domainId >= MAX_USER_DOMAIN_NB) {                                              \
            return CM_INTERNAL_DOMAIN_OVERFLOW;                                            \
        }                                                                                  \
    }

#define FIND_SCRATCH_DOMAIN_ID(domainId)                                                   \
    {                                                                                      \
        domainId = 0;                                                                      \
        while (domainScratchDesc[domainId].allocDesc != 0 && domainId < MAX_SCRATCH_DOMAIN_NB) {     \
            domainId++;                                                                    \
        }                                                                                  \
        if (domainId >= MAX_SCRATCH_DOMAIN_NB) {                                           \
            return CM_INTERNAL_DOMAIN_OVERFLOW;                                            \
        }                                                                                  \
    }

PUBLIC t_cm_error cm_DM_CheckDomain(t_cm_domain_id handle, t_cm_domain_type type)
{
    if ((handle <= 3)
            || (handle >= MAX_USER_DOMAIN_NB)) { //remember, domain[0-3] are reserved
        return CM_INVALID_DOMAIN_HANDLE;
    }

    if (domainDesc[handle].client == 0) {
        return CM_INVALID_DOMAIN_HANDLE;
    }

    if (type != DOMAIN_ANY) {
        if (domainDesc[handle].type != type) {
            return CM_INVALID_DOMAIN_HANDLE;
        }
    }

    return CM_OK;
}

PUBLIC t_cm_error cm_DM_CheckDomainWithClient(t_cm_domain_id handle, t_cm_domain_type type, t_nmf_client_id client)
{
    t_cm_error error;

    if((error = cm_DM_CheckDomain(handle, type)) != CM_OK)
        return error;

#ifdef CHECK_TO_BE_REACTIVATED_IN_2_11
    if(domainDesc[handle].client != client)
    {
        ERROR("CM_DOMAIN_VIOLATION: domain %d created by client %d not usable by client %d.", handle, domainDesc[handle].client, client, 0, 0, 0);
        return CM_DOMAIN_VIOLATION;
    }
#endif

    return CM_OK;
}

PUBLIC t_cm_error cm_DM_Init(void)
{
    t_cm_error error;

    int i = 0;
    for(i = 0; i < MAX_USER_DOMAIN_NB; i++) {
        INIT_DOMAIN_STRUCT(domainDesc[i]);
    }

    //domains[0-3] are reserved - allows to catch some cases of incorrect usage,
    //especially when user uses coreId instead of domainId, ie id = 1, 2, 3
    domainDesc[0].client = NMF_CORE_CLIENT;
    domainDesc[1].client = NMF_CORE_CLIENT;
    domainDesc[2].client = NMF_CORE_CLIENT;
    domainDesc[3].client = NMF_CORE_CLIENT;

    /* We use domain 1 and 2 for the singleton, only used for components structure */
    domainDesc[DEFAULT_SVA_DOMAIN].type = DOMAIN_NORMAL;
    domainDesc[DEFAULT_SVA_DOMAIN].domain.coreId= SVA_CORE_ID;
    domainDesc[DEFAULT_SVA_DOMAIN].domain.esramCode.size = (t_uint32)-1;
    domainDesc[DEFAULT_SVA_DOMAIN].domain.esramData.size = (t_uint32)-1;
    domainDesc[DEFAULT_SVA_DOMAIN].domain.sdramCode.size = (t_uint32)-1;
    domainDesc[DEFAULT_SVA_DOMAIN].domain.sdramData.size = (t_uint32)-1;
    domainDesc[DEFAULT_SIA_DOMAIN].type = DOMAIN_NORMAL;
    domainDesc[DEFAULT_SIA_DOMAIN].domain.coreId= SIA_CORE_ID;
    domainDesc[DEFAULT_SIA_DOMAIN].domain.esramCode.size = (t_uint32)-1;
    domainDesc[DEFAULT_SIA_DOMAIN].domain.esramData.size = (t_uint32)-1;
    domainDesc[DEFAULT_SIA_DOMAIN].domain.sdramCode.size = (t_uint32)-1;
    domainDesc[DEFAULT_SIA_DOMAIN].domain.sdramData.size = (t_uint32)-1;

    for(i = 0; i < MAX_SCRATCH_DOMAIN_NB; i++) {
        domainScratchDesc[i].domainId  = 0;
        domainScratchDesc[i].parentId  = 0;
        domainScratchDesc[i].allocDesc = 0;
    }

    // Alloc twice for having comfortable chunk
    if((error = allocChunkPool()) != CM_OK)
        return error;
    if((error = allocChunkPool()) != CM_OK)
    {
        freeChunkPool();
        return error;
    }

    return CM_OK;
}

PUBLIC void cm_DM_Destroy(void)
{
    //cm_DM_Init();
    freeChunkPool();
}

PUBLIC t_nmf_core_id cm_DM_GetDomainCoreId(const t_cm_domain_id domainId)
{

    return domainDesc[domainId].domain.coreId;
}

#if 0
static t_uint32 cm_DM_isSegmentOverlaping(const t_cm_domain_segment *d0, const t_cm_domain_segment *d1)
{
    t_uint32 min0 = d0->offset;
    t_uint32 max0 = d0->offset + d0->size;
    t_uint32 min1 = d1->offset;
    t_uint32 max1 = d1->offset + d1->size;

    if ( (min0 < min1) && (min1 < max0) ){    /* min0 < min1 < max0  OR  min1 in [min0:max0] */
        return 1;
    }
    if ( (min1 < min0) && (min0 <= max1) ){   /* min1 < min0 < max0  OR  min0 in [min1:max1] */
        return 1;
    }

    return 0;
}
{
    ...

    t_uint32 i;
    //check non-overlapp with other domains
    for (i = 0; i < MAX_USER_DOMAIN_NB; i++) {
        if (domainDesc[i].client != 0) {
            if (cm_DM_isSegmentOverlaping(&domainDesc[i].domain.esramData, &domain->esramData)) {
                return CM_DOMAIN_OVERLAP;
            }
            /*
            if (cm_DM_isSegmentOverlaping(&domainDesc[i].domain.esramData, &domain->esramData)) {
                return CM_DOMAIN_OVERLAP;
            }
            */
        }
    }

    ...
}
#endif

PUBLIC t_cm_error cm_DM_CreateDomain(const t_nmf_client_id client, const t_cm_domain_memory *domain, t_cm_domain_id *handle)
{
    t_cm_domain_id domainId;
    FIND_DOMAIN_ID(domainId);

    if (client == 0)
        return CM_INVALID_PARAMETER;

    if (domain->coreId > LAST_CORE_ID)
        return CM_INVALID_DOMAIN_DEFINITION;

    //FIXME, juraj, check invalid domain definition
    domainDesc[domainId].client = client;
    domainDesc[domainId].domain = *domain;

    if (osal_debug_ops.domain_create)
	    osal_debug_ops.domain_create(domainId);

    *handle = domainId;

    return CM_OK;
}

//TODO, juraj, add assert to cm_MM_GetOffset(), if domain is scratch parent
PUBLIC t_cm_error cm_DM_CreateDomainScratch(const t_nmf_client_id client, const t_cm_domain_id parentId, const t_cm_domain_memory *domain, t_cm_domain_id *handle)
{
    t_cm_error error;
    t_memory_handle memhandle;
    t_cm_allocator_desc *alloc;
    t_uint32 parentMin, parentMax;
    t_uint32 scratchMin, scratchMax;

    /* check if the parent domain exists */
    /* parent could be DOMAIN_NORMAL (1st call) or DOMAIN_SCRATCH_PARENT (other calls) */
    if ((error = cm_DM_CheckDomain(parentId, DOMAIN_ANY)) != CM_OK) {
        return error;
    }

    parentMin = domainDesc[parentId].domain.esramData.offset;
    parentMax = domainDesc[parentId].domain.esramData.offset + domainDesc[parentId].domain.esramData.size;
    scratchMin = domain->esramData.offset;
    scratchMax = domain->esramData.offset + domain->esramData.size;
    /* check if the scratch domain respects the parent domain (esram data only )*/
    if ( (parentMin > scratchMin) || (parentMax < scratchMax) ) {
        return CM_INVALID_DOMAIN_DEFINITION;
    }

    /* create the scratch domain */
    if ((error = cm_DM_CreateDomain(client, domain, handle)) != CM_OK) {
        return error;
    }

    /* check if this is the first scratch domain */
    if (domainDesc[parentId].scratch.parent.handle == 0) {
        /* 1st scratch domain */
        t_cm_domain_segment tmp;

        /* reserve the zone for the scratch domain */
        tmp = domainDesc[parentId].domain.esramData;
        domainDesc[parentId].domain.esramData = domain->esramData;
        memhandle = cm_DM_Alloc(parentId, ESRAM_EXT16, domain->esramData.size / 2, CM_MM_ALIGN_NONE, FALSE); //note byte to 16bit-word conversion
        domainDesc[parentId].domain.esramData = tmp;
        if (memhandle == 0) {
            cm_DM_DestroyDomain(*handle);
            cm_DM_DomainError(parentId, client);
            return CM_NO_MORE_MEMORY;
        }

        domainDesc[parentId].type = DOMAIN_SCRATCH_PARENT;
        domainDesc[parentId].refcount = 0; //reinit the refcount
        domainDesc[parentId].scratch.parent.handle = memhandle;

    } else {
        /* nth scratch domain */
        t_uint32 i;
        t_uint32 oldMin = domainDesc[parentId].domain.esramData.offset + domainDesc[parentId].domain.esramData.offset;
        t_uint32 oldMax = 0;

        /* compute the new scratch zone size */
        for(i = 0; i < MAX_USER_DOMAIN_NB; i++) {
            if ((domainDesc[i].type == DOMAIN_SCRATCH_CHILD) && (domainDesc[i].scratch.child.parent_ref == parentId)) {
                /* ok, here we have a scratch domain created from the same child domain */
                t_uint32 min = domainDesc[i].domain.esramData.offset;
                t_uint32 max = domainDesc[i].domain.esramData.offset + domainDesc[i].domain.esramData.size;

                oldMin = (min < oldMin)?min:oldMin;
                oldMax = (max > oldMax)?max:oldMax;
            }
        }

        /* resize the scratch zone */
        if ((oldMin > scratchMin) || (oldMax < scratchMax)) {
            t_uint32 newMin = (oldMin > scratchMin)?scratchMin:oldMin;
            t_uint32 newMax = (oldMax < scratchMax)?scratchMax:oldMax;

            if(cm_MM_Realloc(cm_DM_getAllocator(parentId, ESRAM_EXT16), newMax - newMin, newMin,
                                      &domainDesc[parentId].scratch.parent.handle) != CM_OK)
            {
                /* failed to extend the zone */
                cm_DM_DestroyDomain(*handle);
                cm_DM_DomainError(parentId, client);
                return CM_NO_MORE_MEMORY;
            }
        }
    }

    /* create esram-data allocator in the scratch domain */
    alloc = cm_MM_CreateAllocator(domainDesc[*handle].domain.esramData.size,
                                  domainDesc[*handle].domain.esramData.offset,
                                  "scratch");

    domainDesc[*handle].type = DOMAIN_SCRATCH_CHILD;
    domainDesc[*handle].scratch.child.parent_ref = parentId;
    domainDesc[*handle].scratch.child.alloc = alloc;
    domainDesc[parentId].refcount++;

    return error;
}

PUBLIC t_cm_error cm_DM_DestroyDomains(const t_nmf_client_id client)
{
    t_cm_domain_id handle;
    t_cm_error error, status=CM_OK;

    for (handle=0; handle<MAX_USER_DOMAIN_NB; handle++) {
        if ((domainDesc[handle].client == client)
            && ((error=cm_DM_DestroyDomain(handle)) != CM_OK)) {
            LOG_INTERNAL(0, "Error (%d) destroying remaining domainId %d for client %u\n", error, handle, client, 0, 0, 0);
            status = error;
        }
    }
    return status;
}

PUBLIC t_cm_error cm_DM_DestroyDomain(t_cm_domain_id handle)
{
    t_cm_error error = CM_OK;
    t_uint32 i;

    if ((error = cm_DM_CheckDomain(handle, DOMAIN_ANY)) != CM_OK) {
        return error;
    }

    //forbid destruction of cm domains
    //if (handle == cm_DSP_GetState(domainDesc[handle].domain.coreId)->domainEE)
    //    return CM_INVALID_DOMAIN_HANDLE;

    /* loop all components and check if there are still components instantiated with this handle */
    //actually this check is redundant with the usage counters as component instantiations allocate memory
    for (i=0; i<ComponentTable.idxMax; i++)
    {
	    if (NULL != componentEntry(i) && componentEntry(i)->domainId == handle) {
            return CM_ILLEGAL_DOMAIN_OPERATION;
        }
    }

    //perform check based on usage counters
    if (domainDesc[handle].refcount != 0) {
        return CM_ILLEGAL_DOMAIN_OPERATION;
    }

    if (domainDesc[handle].type == DOMAIN_SCRATCH_PARENT) {
        return CM_ILLEGAL_DOMAIN_OPERATION; //parent destroyed implicitly with the last scratch
    } else if (domainDesc[handle].type == DOMAIN_SCRATCH_CHILD) {
        t_cm_allocator_status status;
        t_cm_domain_id parentId = domainDesc[handle].scratch.child.parent_ref;

        cm_MM_GetAllocatorStatus(domainDesc[handle].scratch.child.alloc, 0, 0xffff, &status);
        if (status.global.accumulate_used_memory != 0) {
            //something is still allocated
            return CM_ILLEGAL_DOMAIN_OPERATION;
        }

        domainDesc[parentId].refcount--;
        cm_MM_DeleteAllocator(domainDesc[handle].scratch.child.alloc); //returns no error

        if (domainDesc[parentId].refcount == 0) {
            /* last scratch domain */
            cm_DM_Free(domainDesc[parentId].scratch.parent.handle, FALSE);
            domainDesc[parentId].scratch.parent.handle = 0;
            domainDesc[parentId].type = DOMAIN_NORMAL;
        } else {
            /* other child scratch domains exist, check if the reserved zone needs resize, ie reduce */

            t_uint32 i;
            /* init oldMin and oldMax to values we are sure will get overwritten below */
            t_uint32 oldMin = 0xffffffff;
            t_uint32 oldMax = 0x0;
            t_uint32 scratchMin = domainDesc[handle].domain.esramData.offset;
            t_uint32 scratchMax = domainDesc[handle].domain.esramData.offset + domainDesc[handle].domain.esramData.size;

            /* compute the remaining reserved zone size */
            for(i = 0; i < MAX_USER_DOMAIN_NB; i++) {
                if (i == handle)
                    continue; //do not consider the current domain to be destroyed later in this function
                if ((domainDesc[i].type == DOMAIN_SCRATCH_CHILD) && (domainDesc[i].scratch.child.parent_ref == parentId)) {
                    /* ok, here we have a scratch domain created from the same child domain */
                    t_uint32 min = domainDesc[i].domain.esramData.offset;
                    t_uint32 max = domainDesc[i].domain.esramData.offset + domainDesc[i].domain.esramData.size;

                    oldMin = (min < oldMin)?min:oldMin;
                    oldMax = (max > oldMax)?max:oldMax;
                }
            }

            /* resize the scratch zone */
            if ((oldMin > scratchMin) || (oldMax < scratchMax)) {
                CM_ASSERT(cm_MM_Realloc(cm_DM_getAllocator(parentId, ESRAM_EXT16), oldMax - oldMin, oldMin,
                                          &domainDesc[parentId].scratch.parent.handle) == CM_OK); //the realloc shouldn't fail..
            }
        }
    }

    if (osal_debug_ops.domain_destroy)
	    osal_debug_ops.domain_destroy(handle);

    //reset the domain desc
    INIT_DOMAIN_STRUCT(domainDesc[handle]);

    return CM_OK;
}

/*
 *   - if the domainId is scratch parent, all allocations are done as in normal domains
 *   - if the domainId is scratch child
 *          if allocation type is esram, retrieve the allocator from the domainDesc
 *          else allocation is done as for normal domain
 *   - if the domainId is normal, allocator is retrieved from mpcDesc via cm_DSP_GetAllocator()
 */
static t_cm_allocator_desc *cm_DM_getAllocator(t_cm_domain_id domainId, t_dsp_memory_type_id memType)
{
    t_cm_allocator_desc *alloc = 0;

    if ((domainDesc[domainId].type == DOMAIN_SCRATCH_CHILD)
        && ((memType == ESRAM_EXT16) || (memType == ESRAM_EXT24))) {
        alloc = domainDesc[domainId].scratch.child.alloc;
    } else {
        alloc = cm_DSP_GetAllocator(domainDesc[domainId].domain.coreId, memType);
    }

    return alloc;
}

void START(void);
void END(const char*);

//TODO, juraj, alloc would need to return finer errors then 0
PUBLIC t_memory_handle cm_DM_Alloc(t_cm_domain_id domainId, t_dsp_memory_type_id memType, t_uint32 wordSize, t_cm_mpc_memory_alignment memAlignment,  t_bool powerOn)
{
    t_nmf_core_id coreId = domainDesc[domainId].domain.coreId;
    t_memory_handle handle;
    t_cm_allocator_desc *alloc;
    t_uint32 offset;
    t_uint32 size;

    cm_DSP_GetInternalMemoriesInfo(domainId, memType, &offset, &size);

    if ((alloc = cm_DM_getAllocator(domainId, memType)) == 0) {
        return 0;
    }

    handle = cm_MM_Alloc(alloc,
            cm_DSP_ConvertSize(memType, wordSize),
            (t_cm_memory_alignment) memAlignment,
            offset, size, domainId);

    if(handle != INVALID_MEMORY_HANDLE)
    {
        cm_MM_SetMemoryHandleUserData(handle, (coreId << SHIFT_BYTE1) | (memType << SHIFT_BYTE0));

        if (powerOn) {
            // [Pwr] The associated power domain can be enabled only after the Alloc request.
            //       Associated MPC memory chunk is not accessed (Remote allocator feature)
            cm_PWR_EnableMemory(
                    coreId,
                    memType,
                    /*
                     * Compute physical address based on cm_DSP_GetHostSystemAddress but in optimized way
                     * -> See it for information
                     * -> Note TCM memory is not correctly compute, but it's not used
                     */
                    cm_DSP_GetState(coreId)->allocator[memType]->baseAddress.physical + cm_MM_GetOffset(handle),
                    cm_MM_GetSize(handle));
        }
    } else {
        LOG_INTERNAL(0, "CM_NO_MORE_MEMORY domainId: %d, memType %d, wordSize %d, alignement %d\n",
                domainId, memType, wordSize, memAlignment, 0, 0);
        cm_MM_DumpMemory(alloc, offset, offset + size);
    }

    return handle;
}

PUBLIC void cm_DM_FreeWithInfo(t_memory_handle memHandle, t_nmf_core_id *coreId, t_dsp_memory_type_id *memType, t_bool powerOff)
{
    t_dsp_chunk_info chunk_info;

    cm_DSP_GetDspChunkInfo(memHandle, &chunk_info);

    if (powerOff) {
        cm_PWR_DisableMemory(
                chunk_info.coreId,
                chunk_info.memType,
                cm_DSP_GetPhysicalAdress(memHandle),
                cm_MM_GetSize(memHandle));
    }

    cm_MM_Free(chunk_info.alloc, memHandle);

    *coreId = chunk_info.coreId;
    *memType = chunk_info.memType;
}

PUBLIC void cm_DM_Free(t_memory_handle memHandle, t_bool powerOff)
{
    t_nmf_core_id coreId;
    t_dsp_memory_type_id memType;

    cm_DM_FreeWithInfo(memHandle, &coreId, &memType, powerOff);
}

PUBLIC t_cm_error cm_DM_GetAllocatorStatus(t_cm_domain_id domainId, t_dsp_memory_type_id memType, t_cm_allocator_status *pStatus)
{
    t_cm_error error;
    t_uint32 dOffset;
    t_uint32 dSize;

    //TODO, scratch
    error = cm_DM_CheckDomain(domainId, DOMAIN_ANY);
    if (error != CM_OK) {
        return error;
    }

    cm_DSP_GetInternalMemoriesInfo(domainId, memType, &dOffset, &dSize);

    return cm_DSP_GetAllocatorStatus(domainDesc[domainId].domain.coreId, memType,
            dOffset, dSize, pStatus);
}

//WARNING: this function is only correct *before* migration! because
//the computation of absolute adresses of a domain is based on the allocator for the given
//segment (this is hidden in cm_DSP_GetDspBaseAddress and this info is not valid
//after migration (non-contiguous address-space from the ARM-side)
PUBLIC t_cm_error cm_DM_GetDomainAbsAdresses(t_cm_domain_id domainId, t_cm_domain_info *info)
{
    t_cm_error error;
    t_nmf_core_id coreId = domainDesc[domainId].domain.coreId;

    cm_migration_check_state(coreId, STATE_NORMAL);

    error = cm_DM_CheckDomain(domainId, DOMAIN_NORMAL);
    if (error != CM_OK) {
        return error;
    }

    cm_DSP_GetDspBaseAddress(coreId, SDRAM_CODE,  &info->sdramCode);
    cm_DSP_GetDspBaseAddress(coreId, ESRAM_CODE,  &info->esramCode);
    cm_DSP_GetDspBaseAddress(coreId, SDRAM_EXT24, &info->sdramData);
    cm_DSP_GetDspBaseAddress(coreId, ESRAM_EXT24, &info->esramData);

    info->sdramCode.physical += domainDesc[domainId].domain.sdramCode.offset;
    info->sdramCode.logical  += domainDesc[domainId].domain.sdramCode.offset;
    info->esramCode.physical += domainDesc[domainId].domain.esramCode.offset;
    info->esramCode.logical  += domainDesc[domainId].domain.esramCode.offset;
    info->sdramData.physical += domainDesc[domainId].domain.sdramData.offset;
    info->sdramData.logical  += domainDesc[domainId].domain.sdramData.offset;
    info->esramData.physical += domainDesc[domainId].domain.esramData.offset;
    info->esramData.logical  += domainDesc[domainId].domain.esramData.offset;

    return CM_OK;
}

static void cm_DM_DomainError(const t_cm_domain_id parentId, const t_nmf_client_id client)
{
    int i;
    LOG_INTERNAL(0, "NMF_DEBUG_SCRATCH failed to allocate domain (client %u): 0x%08x -> 0x%08x\n",
            client,
            domainDesc[parentId].domain.esramData.offset,
            domainDesc[parentId].domain.esramData.offset + domainDesc[parentId].domain.esramData.size,
            0, 0, 0);
    for(i = 0; i < MAX_USER_DOMAIN_NB; i++) {
        if (domainDesc[i].type == DOMAIN_SCRATCH_CHILD) {
            LOG_INTERNAL(0, "NMF_DEBUG_SCRATCH scratch domain %d allocated (client %u): 0x%08x -> 0x%08x\n",
                    i, domainDesc[i].client,
                    domainDesc[i].domain.esramData.offset,
                    domainDesc[i].domain.esramData.offset + domainDesc[i].domain.esramData.size,
                    0, 0);
        }
    }
    cm_MM_DumpMemory(cm_DM_getAllocator(parentId, ESRAM_EXT16),
            domainDesc[parentId].domain.esramData.offset,
            domainDesc[parentId].domain.esramData.offset + domainDesc[parentId].domain.esramData.size);
}

PUBLIC void cm_DM_SetDefaultDomain(t_memory_handle memHandle, t_nmf_core_id coreId)
{
	if (coreId == SVA_CORE_ID)
		cm_MM_SetDefaultDomain(memHandle, DEFAULT_SVA_DOMAIN);
	else if (coreId == SIA_CORE_ID)
		cm_MM_SetDefaultDomain(memHandle, DEFAULT_SIA_DOMAIN);
}
