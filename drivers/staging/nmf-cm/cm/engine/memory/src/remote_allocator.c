/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 * Include
 */
#include "../inc/remote_allocator.h"
#include "../inc/remote_allocator_utils.h"
#include "../inc/chunk_mgr.h"

#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/trace/inc/xtitrace.h>

static void cm_MM_RA_checkAllocator(t_cm_allocator_desc* alloc);
//static void cm_MM_RA_checkAlloc(t_cm_allocator_desc* alloc, t_uint32 size, t_uint32 align, t_uint32 min, t_uint32 max);

int bin_index(unsigned int sz) {
    /*
     * 32 bins of size       2
     * 16 bins of size      16
     *  8 bins of size     128
     *  4 bins of size    1024
     *  2 bins of size    8192
     *  1 bin  of size what's left
     *
     */
    return (((sz >> 6) ==    0) ?       (sz >>  1): // 0        -> 0 .. 31
            ((sz >> 6) <=    4) ?  28 + (sz >>  4): // 64       -> 32 .. 47
            ((sz >> 6) <=   20) ?  46 + (sz >>  7): // 320      -> 48 .. 55
            ((sz >> 6) <=   84) ?  55 + (sz >> 10): // 1344     -> 56 .. 59
            ((sz >> 6) <=  340) ?  59 + (sz >> 13): // 5440     -> 60 .. 61
            62);                                    // 21824..
}

static t_cm_allocator_desc* ListOfAllocators = NULL;

PUBLIC t_cm_allocator_desc* cm_MM_CreateAllocator(t_cm_size size, t_uint32 offset, const char* name)
{
    t_cm_allocator_desc *alloc;

    CM_ASSERT(fillChunkPool() == CM_OK);

    /* Alloc structure */
    alloc = (t_cm_allocator_desc*)OSAL_Alloc_Zero(sizeof(t_cm_allocator_desc));
    CM_ASSERT(alloc != NULL);

    // Add allocator in list
    alloc->next = ListOfAllocators;
    ListOfAllocators = alloc;

    /* assign name */
    alloc->pAllocName = name;

    alloc->maxSize = size;
    alloc->sbrkSize = 0;
    alloc->offset = offset;

    //TODO, juraj, alloc impacts trace format
    cm_TRC_traceMemAlloc(TRACE_ALLOCATOR_COMMAND_CREATE, 0, size, name);

    return alloc;
}

PUBLIC t_cm_error cm_MM_DeleteAllocator(t_cm_allocator_desc *alloc)
{
    t_cm_chunk *chunk, *next_cm_chunk;

    cm_TRC_traceMemAlloc(TRACE_ALLOCATOR_COMMAND_DESTROY, 0, 0, alloc->pAllocName);

    /* Parse all chunks and free them */
    chunk = alloc->chunks;
    while(chunk != 0)
    {
        next_cm_chunk = chunk->next;
        unlinkChunk(alloc, chunk);
        freeChunk(chunk);

        chunk = next_cm_chunk;
    }

    // Remove allocator from the list
    if(ListOfAllocators == alloc)
        ListOfAllocators = alloc->next;
    else {
        t_cm_allocator_desc *prev = ListOfAllocators;
        while(prev->next != alloc)
            prev = prev->next;
        prev->next = alloc->next;
    }


    /* Free allocator descriptor */
    OSAL_Free(alloc);

    return CM_OK;
}

PUBLIC t_cm_error cm_MM_ResizeAllocator(t_cm_allocator_desc *alloc, t_cm_size size)
{
    /* sanity check */
    if (size == 0)
        return CM_INVALID_PARAMETER;

    if(alloc->sbrkSize > size)
        return CM_NO_MORE_MEMORY;

    alloc->maxSize = size;

    if (cmIntensiveCheckState)
        cm_MM_RA_checkAllocator(alloc);

    return CM_OK;
}

t_cm_error cm_MM_getValidMemoryHandle(t_cm_memory_handle handle, t_memory_handle* validHandle)
{
#ifdef LINUX
    /* On linux, there is already a check within the linux part
     * => we don't need to check twice */
    *validHandle = (t_memory_handle)handle;
    return CM_OK;
#else
    t_cm_allocator_desc *alloc = ListOfAllocators;

    for(; alloc != NULL; alloc = alloc->next)
    {
        t_cm_chunk* chunk = alloc->chunks;

        /* Parse all chunks */
        for(; chunk != NULL; chunk = chunk->next)
        {
            if(chunk == (t_memory_handle)handle)
            {
                if(chunk->status == MEM_FREE)
                    return CM_MEMORY_HANDLE_FREED;

                *validHandle = (t_memory_handle)handle;

                return CM_OK;
            }
        }
    }

    return CM_UNKNOWN_MEMORY_HANDLE;
#endif
}

//TODO, juraj, add appartenance to allocHandle (of chunk) and degage setUserData
PUBLIC t_memory_handle cm_MM_Alloc(
        t_cm_allocator_desc* alloc,
        t_cm_size size,
        t_cm_memory_alignment memAlignment,
        t_uint32 seg_offset,
        t_uint32 seg_size,
        t_uint32 domainId)
{
    t_cm_chunk* chunk;
    t_uint32 aligned_offset;
    t_uint32 aligned_end;
    t_uint32 seg_end = seg_offset + seg_size;
    int i;

    /* Sanity check */
    if ( (size == 0) || (size > seg_size) )
        return INVALID_MEMORY_HANDLE;

    if(fillChunkPool() != CM_OK)
        return INVALID_MEMORY_HANDLE;

    /* Get first chunk available for the specific size */
    // Search a list with a free chunk
    for(i = bin_index(size); i < BINS; i++)
    {
        chunk = alloc->free_mem_chunks[i];
        while (chunk != 0)
        {
            /* Alignment of the lower boundary */
            aligned_offset = ALIGN_VALUE(MAX(chunk->offset, seg_offset), (memAlignment + 1));

            aligned_end = aligned_offset + size;

            if ((aligned_end <= seg_end)
                    && aligned_end <= (chunk->offset + chunk->size)
                    && aligned_offset >= seg_offset
                    && aligned_offset >= chunk->offset)
                goto found;

            chunk = chunk->next_free_mem;
        }
    }

    // Try to increase sbrkSize through maxSize
    aligned_offset = ALIGN_VALUE(MAX((alloc->offset + alloc->sbrkSize), seg_offset), (memAlignment + 1));

    aligned_end = aligned_offset + size;

    if ((aligned_end <= seg_end)
            && aligned_end <= (alloc->offset + alloc->maxSize)
            && aligned_offset >= seg_offset
            && aligned_offset >= (alloc->offset + alloc->sbrkSize))
    {
        /* If that fit requirement, create a new free chunk at the end of current allocator */
        chunk = allocChunk();

        /* Update chunk size */
        chunk->offset = alloc->offset + alloc->sbrkSize; // offset start at end of current allocator
        chunk->size = aligned_end - chunk->offset;
        chunk->alloc = alloc;

        /* Chain it with latest chunk */
        linkChunk(alloc, alloc->lastChunk, chunk);

        /* Increase sbrkSize to end of this new chunk */
        alloc->sbrkSize += chunk->size;

        goto foundNew;
   }

    return INVALID_MEMORY_HANDLE;

found:
    /* Remove chunk from free list */
    unlinkFreeMem(alloc, chunk);

foundNew:
    //create an empty chunk before the allocated one
    if (chunk->offset < aligned_offset) {
        chunk = splitChunk(alloc, chunk, aligned_offset, FREE_CHUNK_BEFORE);
    }
    //create an empty chunk after the allocated one
    if (chunk->offset + chunk->size > aligned_end) {
        splitChunk(alloc, chunk, aligned_end, FREE_CHUNK_AFTER);
    }

    chunk->status = MEM_USED;
    chunk->prev_free_mem = 0;
    chunk->next_free_mem = 0;
    chunk->domainId = domainId;

    //TODO, juraj, alloc impacts trace format
    cm_TRC_traceMem(TRACE_ALLOC_COMMAND_ALLOC, 0, chunk->offset, chunk->size);

    if (cmIntensiveCheckState) {
        cm_MM_RA_checkAllocator(alloc);
    }

    return (t_memory_handle) chunk;
}

//caution - if successfull, the chunk offset will be aligned with seg_offset
//caution++ the offset of the allocated chunk changes implicitly
PUBLIC t_cm_error cm_MM_Realloc(
                t_cm_allocator_desc* alloc,
                const t_cm_size size,
                const t_uint32 offset,
                t_memory_handle *handle)
{
    t_cm_chunk *chunk = (t_cm_chunk*)*handle;
    t_uint32 oldOffset = chunk->offset;
    t_uint32 oldSize = chunk->size;
    t_uint32 oldDomainId = chunk->domainId;
    t_uint16 userData = chunk->userData;

    cm_MM_Free(alloc, *handle);

    *handle = cm_MM_Alloc(alloc, size, CM_MM_ALIGN_NONE, offset, size, oldDomainId);

    if(*handle == INVALID_MEMORY_HANDLE)
    {
        *handle = cm_MM_Alloc(alloc, oldSize, CM_MM_ALIGN_NONE, oldOffset, oldSize, oldDomainId);

        CM_ASSERT(*handle != INVALID_MEMORY_HANDLE);

        chunk = (t_cm_chunk*)*handle;
        chunk->userData = userData;

        return CM_NO_MORE_MEMORY;
    }

    chunk = (t_cm_chunk*)*handle;
    chunk->userData = userData;

    return CM_OK;

#if 0
    /* check reallocation is related to this chunk! */
    CM_ASSERT(chunk->offset <= (offset + size));
    CM_ASSERT(offset <= (chunk->offset + chunk->size));
    CM_ASSERT(size);

    /* check if extend low */
    if (offset < chunk->offset) {
        /* note: it is enough to check only the previous chunk,
         *      because adjacent chunks of same status are merged
         */
        if ((chunk->prev == 0)
           ||(chunk->prev->status != MEM_FREE)
           ||(chunk->prev->offset > offset)) {
            return INVALID_MEMORY_HANDLE;
        }
    }

    /* check if extend high, extend sbrk if necessary */
    if ( (offset + size) > (chunk->offset + chunk->size)) {
        if(chunk->next == 0)
        {
            // check if allocator can be extended to maxSize
            if((offset + size) > (alloc->offset + alloc->maxSize))
                return INVALID_MEMORY_HANDLE;
        }
        else
        {
            if ((chunk->next->status != MEM_FREE)
                    ||( (chunk->next->offset + chunk->next->size) < (offset + size))) {
                return INVALID_MEMORY_HANDLE;
            }
        }
    }

    if(fillChunkPool() != CM_OK)
        return INVALID_MEMORY_HANDLE;


    /* extend low
     *      all conditions should have been checked
     *      this must not fail
     */
    if (offset < chunk->offset) {
        t_uint32 delta = chunk->prev->offset + chunk->prev->size - offset;
        t_cm_chunk *prev = chunk->prev;

        chunk->offset -= delta;
        chunk->size += delta;

        CM_ASSERT(prev->status == MEM_FREE); //TODO, juraj, already checked
        unlinkFreeMem(alloc, prev);
        prev->size -= delta;
        if(prev->size == 0)
        {
            unlinkChunk(alloc, prev);
            freeChunk(prev);
        } else {
            updateFreeList(alloc, prev);
        }
    }

    /* extend high */
    if ( (offset + size) > (chunk->offset + chunk->size)) {
        t_uint32 delta = size - chunk->size;
        t_cm_chunk *next = chunk->next;

        chunk->size += delta;

        if(next == 0)
        {
            alloc->sbrkSize += delta;
        } else {
            CM_ASSERT(next->status == MEM_FREE);
            unlinkFreeMem(alloc, next);
            next->offset += delta;
            next->size -= delta;
            if(next->size == 0)
            {
                unlinkChunk(alloc, next);
                freeChunk(next);
            } else {
                updateFreeList(alloc, next);
            }
        }
    }

    /* reduce top */
    if ((offset + size) < (chunk->offset + chunk->size)) {
        t_uint32 delta = chunk->size - size;

        if(chunk->next == 0) {
            alloc->sbrkSize -= delta;
            chunk->size -= delta;

        } else if (chunk->next->status == MEM_FREE) {
            unlinkFreeMem(alloc, chunk->next);
            chunk->size -= delta;
            chunk->next->offset -= delta;
            chunk->next->size += delta;
            updateFreeList(alloc, chunk->next);
        } else {
            t_cm_chunk *tmp = splitChunk(alloc, chunk, offset + size, FREE_CHUNK_AFTER); //tmp = chunk, chunk = result
            tmp->status = MEM_USED;
            tmp->next->status = MEM_FREE;
        }
    }

    /* reduce bottom */
    if (offset > chunk->offset) {
        if (chunk->prev->status == MEM_FREE) {
            t_uint32 delta = offset - chunk->offset;
            unlinkFreeMem(alloc, chunk->prev);
            chunk->prev->size += delta;
            chunk->offset = offset;
            chunk->size -= delta;
            updateFreeList(alloc, chunk->prev);
        } else {
            t_cm_chunk *tmp = splitChunk(alloc, chunk, offset, FREE_CHUNK_BEFORE); //tmp->next = chunk, tmp = result
            tmp->status = MEM_USED;
            tmp->prev->status = MEM_FREE;
        }
    }

    cm_MM_RA_checkAllocator(alloc);

    return (t_memory_handle)chunk;
#endif
}

PUBLIC void cm_MM_Free(t_cm_allocator_desc* alloc, t_memory_handle memHandle)
{
    t_cm_chunk* chunk = (t_cm_chunk*)memHandle;

    //TODO, juraj, alloc impacts trace format
    cm_TRC_traceMem(TRACE_ALLOC_COMMAND_FREE, 0,
            chunk->offset, chunk->size);

    /* Update chunk status */
    chunk->status = MEM_FREE;
    chunk->domainId = 0x0;

    // Invariant: Current chunk is free but not in free list

    /* Check if the previous chunk is free */
    if((chunk->prev != 0) && (chunk->prev->status == MEM_FREE))
    {
        t_cm_chunk* prev = chunk->prev;

        // Remove chunk to be freed from memory list
        unlinkChunk(alloc, chunk);

        // Remove previous from free list
        unlinkFreeMem(alloc, prev);

        // Update previous size
        prev->size += chunk->size;

        freeChunk(chunk);

        chunk = prev;
    }

    /* Check if the next chunk is free */
    if((chunk->next != 0) && (chunk->next->status == MEM_FREE))
    {
        t_cm_chunk* next = chunk->next;

        // Remove next from memory list
        unlinkChunk(alloc, next);

        // Remove next from free list
        unlinkFreeMem(alloc, next);

        // Update previous size
        chunk->size += next->size;

        freeChunk(next);
    }

    if(chunk->next == 0)
    {
        // If we are the last one, decrease sbrkSize
        alloc->sbrkSize -= chunk->size;

        unlinkChunk(alloc, chunk);
        freeChunk(chunk);

    }
    else
    {
        // Add it in free list
        updateFreeList(alloc, chunk);
    }

    if (cmIntensiveCheckState) {
        cm_MM_RA_checkAllocator(alloc);
    }
}

PUBLIC t_cm_error cm_MM_GetAllocatorStatus(t_cm_allocator_desc* alloc, t_uint32 offset, t_uint32 size, t_cm_allocator_status *pStatus)
{
    t_cm_chunk* chunk = alloc->chunks;
    t_uint32 sbrkFree = alloc->maxSize - alloc->sbrkSize;
    t_uint8 min_free_size_updated = FALSE;

    /* Init status */
    pStatus->global.used_block_number = 0;
    pStatus->global.free_block_number = 0;
    pStatus->global.maximum_free_size = 0;
    pStatus->global.minimum_free_size = 0xFFFFFFFF;
    pStatus->global.accumulate_free_memory = 0;
    pStatus->global.accumulate_used_memory = 0;
    pStatus->global.size = alloc->maxSize;
    pStatus->domain.maximum_free_size = 0;
    pStatus->domain.minimum_free_size = 0xFFFFFFFF;
    pStatus->domain.accumulate_free_memory = 0;
    pStatus->domain.accumulate_used_memory = 0;
    pStatus->domain.size= size;

    /* Parse all chunks */
    while(chunk != 0)
    {

        /* Chunk is free */
        if (chunk->status == MEM_FREE) {
            pStatus->global.free_block_number++;
            pStatus->global.accumulate_free_memory += chunk->size;

            /* Check max size */
            if (chunk->size > pStatus->global.maximum_free_size)
            {
                pStatus->global.maximum_free_size = chunk->size;
            }

            /* Check min size */
            if (chunk->size < pStatus->global.minimum_free_size)
            {
                pStatus->global.minimum_free_size = chunk->size;
                min_free_size_updated = TRUE;
            }
        } else {/* Chunk used */
            pStatus->global.used_block_number++;
            pStatus->global.accumulate_used_memory += chunk->size;
        }

        chunk = chunk->next;
    }

    /* Accumulate free space between sbrkSize and maxSize */
    pStatus->global.accumulate_free_memory += sbrkFree;
    if (sbrkFree > 0)
        pStatus->global.free_block_number++;
    if (pStatus->global.maximum_free_size < sbrkFree)
        pStatus->global.maximum_free_size = sbrkFree;
    if (pStatus->global.minimum_free_size > sbrkFree) {
        pStatus->global.minimum_free_size = sbrkFree;
        min_free_size_updated = TRUE;
    }

    /* Put max free size to min free size */
    if (min_free_size_updated == FALSE) {
        pStatus->global.minimum_free_size = pStatus->global.maximum_free_size;
    }

    return CM_OK;
}

PUBLIC t_uint32 cm_MM_GetOffset(t_memory_handle memHandle)
{
    /* Provide offset */
    return ((t_cm_chunk*)memHandle)->offset;
}

PUBLIC t_uint32 cm_MM_GetSize(t_memory_handle memHandle)
{
    return ((t_cm_chunk*)memHandle)->size;
}

PUBLIC t_uint32 cm_MM_GetAllocatorSize(t_cm_allocator_desc* alloc)
{
    return alloc->maxSize;
}

PUBLIC void cm_MM_SetMemoryHandleUserData(t_memory_handle memHandle, t_uint16 userData)
{
    ((t_cm_chunk*)memHandle)->userData = userData;
}

PUBLIC void cm_MM_GetMemoryHandleUserData(t_memory_handle memHandle, t_uint16 *pUserData, t_cm_allocator_desc **alloc)
{
    *pUserData = ((t_cm_chunk*)memHandle)->userData;
    if (alloc)
        *alloc = ((t_cm_chunk*)memHandle)->alloc;
}

/*
 * check free list is ordered
 * check all chunks are correctly linked
 * check adjacent chunks are not FREE
 */
static void cm_MM_RA_checkAllocator(t_cm_allocator_desc* alloc)
{
    t_cm_chunk *chunk = alloc->chunks;
    t_uint32 size = 0;
    int i;

    CM_ASSERT(alloc->sbrkSize <= alloc->maxSize);

    while(chunk != 0) {
        if(chunk == alloc->chunks)
            CM_ASSERT(chunk->prev == 0);
        if(chunk == alloc->lastChunk)
            CM_ASSERT(chunk->next == 0);

        CM_ASSERT(chunk->alloc == alloc);

        if (chunk->next != 0) {
            CM_ASSERT(!((chunk->status == MEM_FREE) && (chunk->next->status == MEM_FREE))); //two free adjacent blocks
            CM_ASSERT(chunk->offset < chunk->next->offset); //offsets reverted
            CM_ASSERT(chunk->offset + chunk->size == chunk->next->offset); // Not hole in allocator
        }
        size += chunk->size;
        chunk = chunk->next;
    }

    CM_ASSERT(size == alloc->sbrkSize);

    for(i = 0; i < BINS; i++)
    {
        chunk = alloc->free_mem_chunks[i];
        while(chunk != 0) {
            if (chunk->next_free_mem != 0) {
                CM_ASSERT(chunk->size <= chunk->next_free_mem->size); //free list not ordered
            }
            chunk = chunk->next_free_mem;
        }
    }
}

PUBLIC void cm_MM_DumpMemory(t_cm_allocator_desc* alloc, t_uint32 start, t_uint32 end)
{
    t_cm_chunk *chunk = alloc->chunks;

    LOG_INTERNAL(0, "ALLOCATOR Dumping allocator \"%s\" [0x%08x:0x%08x]\n", alloc->pAllocName, start, end, 0, 0, 0);
    while(chunk != 0) {
        if (((chunk->offset < start) && (chunk->offset + chunk->size > start))
          || ((chunk->offset < end) && (chunk->offset + chunk->size > end))
          || ((chunk->offset > start) && (chunk->offset + chunk->size < end))
          || ((chunk->offset < start) && (chunk->offset + chunk->size > end)))
        {
            LOG_INTERNAL(0, "ALLOCATOR chunk [0x%08x -> 0x%08x[: status:%s, domainId: 0x%x\n",
                    chunk->offset,
                    chunk->offset + chunk->size,
                    chunk->status?"FREE":"USED",
                    chunk->domainId, 0, 0);
        }
        chunk = chunk->next;
    }
}

PUBLIC void cm_MM_SetDefaultDomain(t_memory_handle memHandle, t_uint32 domainId)
{
	((t_cm_chunk *) memHandle)->domainId = domainId;
}
