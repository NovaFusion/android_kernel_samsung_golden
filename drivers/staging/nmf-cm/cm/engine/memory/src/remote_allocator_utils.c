/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/memory/inc/remote_allocator_utils.h>
#include <cm/engine/trace/inc/trace.h>

/***************************************************************************/
/*
 * linkChunk
 * param prev     : Pointer on previous chunk where the chunk will be added
 * param add      : Pointer on chunk to add
 *
 * Add a chunk in the memory list
 *
 */
/***************************************************************************/
PUBLIC void linkChunk(t_cm_allocator_desc* alloc, t_cm_chunk* prev, t_cm_chunk* add)
{
    // Link previous
    if(prev == 0)
    {
        add->next = alloc->chunks;
        alloc->chunks = add;
    }
    else
    {
        add->prev = prev;
        add->next = prev->next;
        prev->next = add;
    }

    // Link next
    if(add->next == 0)
    {
        // Link at the end
        alloc->lastChunk = add;
    }
    else
        add->next->prev = add;
}

/***************************************************************************/
/*
 * unlinkChunk
 * param allocHandle : Allocator handle
 * param current     : Pointer on chunk to remove
 *
 * Remove a chunk in the memory list and update first pointer
 *
 */
/***************************************************************************/
PUBLIC void unlinkChunk(t_cm_allocator_desc* alloc, t_cm_chunk* current)
{
    /* Link previous with next */
    if (current->prev != 0)
        current->prev->next = current->next;
    else
    {
        CM_ASSERT(alloc->chunks == current);

        // We remove the first, update chunks
        alloc->chunks = current->next;
    }

    /* Link next with previous */
    if(current->next != 0)
        current->next->prev= current->prev;
    else
    {
        CM_ASSERT(alloc->lastChunk == current);

        // We remove the last, update lastChunk
        alloc->lastChunk = current->prev;
    }
}


/***************************************************************************/
/*
 * unlinkFreeMem() unlinks chunk from free memory double-linked list
 *   makes the previous and next chunk in the list point to each other..
 * param allocHandle : Allocator handle
 * param current     : Pointer on chunk to remove
 *
 * Remove a chunk in the free memory list and update pointer
 *
 */
/***************************************************************************/
PUBLIC void unlinkFreeMem(t_cm_allocator_desc* alloc ,t_cm_chunk* current)
{
    int bin = bin_index(current->size);

    /* unlink previous */
    if (current->prev_free_mem != 0)
    {
        current->prev_free_mem->next_free_mem = current->next_free_mem;
    }

    /* Unlink next */
    if (current->next_free_mem !=0 )
    {
        current->next_free_mem->prev_free_mem = current->prev_free_mem;
    }

    /* update first free pointer */
    if (alloc->free_mem_chunks[bin] == current)
    {
        alloc->free_mem_chunks[bin] = current->next_free_mem;
    }

    current->prev_free_mem = 0;
    current->next_free_mem = 0;
}

/***************************************************************************/
/*
 * linkFreeMemBefore
 * param add      : Pointer on chunk to add
 * param next     : Pointer on next chunk where the chunk will be added before
 *
 * Add a chunk in the free memory list
 *
 */
/***************************************************************************/
PUBLIC void linkFreeMemBefore(t_cm_chunk* add, t_cm_chunk* next)
{
    /* Link next */
    add->prev_free_mem = next->prev_free_mem;
    add->next_free_mem = next;

    /* Link previous */
    if (next->prev_free_mem != 0)
    {
        next->prev_free_mem->next_free_mem = add;
    }
    next->prev_free_mem = add;
}

/***************************************************************************/
/*
 * linkFreeMemAfter
 * param add      : Pointer on chunk to add
 * param prev     : Pointer on previous chunk where the chunk will be added after
 *
 * Add a chunk in the free memory list
 *
 */
/***************************************************************************/
PUBLIC void linkFreeMemAfter(t_cm_chunk* prev,t_cm_chunk* add)
{
    /* Link previous */
    add->prev_free_mem = prev;
    add->next_free_mem = prev->next_free_mem;

    /* Link next */
    if (prev->next_free_mem != 0)
    {
        prev->next_free_mem->prev_free_mem = add;
    }
    prev->next_free_mem = add;
}


/***************************************************************************/
/*
 * updateFreeList
 * param allocHandle : Allocator handle
 * param offset      : Pointer on chunk
 *
 * Update free memory list, ordered by size
 *
 */
/***************************************************************************/
PUBLIC void updateFreeList(t_cm_allocator_desc* alloc , t_cm_chunk* chunk)
{
    t_cm_chunk* free_chunk;
    int bin = bin_index(chunk->size);

    /* check case with no more free block */
    if (alloc->free_mem_chunks[bin] == 0)
    {
        alloc->free_mem_chunks[bin] = chunk;
        return ;
    }

    /* order list */
    free_chunk = alloc->free_mem_chunks[bin];
    while ((free_chunk->next_free_mem != 0) && (chunk->size > free_chunk->size))
    {
        free_chunk = free_chunk->next_free_mem;
    }

    /* Add after free chunk if smaller -> we are the last */
    if(free_chunk->size <= chunk->size)
    {
        linkFreeMemAfter(free_chunk,chunk);
    }
    else // This mean that we are smaller
    {
        linkFreeMemBefore(chunk,free_chunk);

        /* Update first free chunk */
        if (alloc->free_mem_chunks[bin] == free_chunk)
        {
            alloc->free_mem_chunks[bin] = chunk;
        }
    }
}


/***************************************************************************/
/*
 * splitChunk
 * param allocHandle : Allocator handle
 * param chunk       : Current chunk (modified in place)
 * param offset      : Offset address of the start memory
 * return            : New chunk handle or 0 if an error occurs
 *
 * Create new chunk before/after the current chunk with the size
 */
/***************************************************************************/
PUBLIC t_cm_chunk* splitChunk(t_cm_allocator_desc* alloc ,t_cm_chunk *chunk,
        t_uint32 offset, t_mem_split_position position)
{
    t_cm_chunk *free;
    t_cm_chunk *returned;

    t_cm_chunk* new_chunk = allocChunk();

    if (position == FREE_CHUNK_AFTER) {
        returned = chunk;
        free = new_chunk;
    } else { //FREE_CHUNK_BEFORE
        returned = new_chunk;
        free = chunk;
    }

    new_chunk->offset = offset;
    new_chunk->size   = chunk->offset + chunk->size - offset;
    new_chunk->alloc  = alloc;
    chunk->size = offset - chunk->offset;

    linkChunk(alloc, chunk, new_chunk);
    unlinkFreeMem(alloc, free);
    updateFreeList(alloc, free);

    return returned;
}
