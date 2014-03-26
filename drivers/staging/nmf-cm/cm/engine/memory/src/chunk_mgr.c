/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 * Include
 */
#include <cm/inc/cm_type.h>
#include "../inc/chunk_mgr.h"
#include <cm/engine/trace/inc/trace.h>

#define CHUNKS_PER_PAGE 500
#define CHUNK_THRESOLD 5

struct t_page_chuncks {
    struct t_page_chuncks   *nextPage;
  //  unsigned int            freeChunkInPage;
    t_cm_chunk              chunks[CHUNKS_PER_PAGE];
};

static struct t_page_chuncks   *firstPage = 0;

static unsigned int            freeChunks = 0;
static t_cm_chunk              *firstFreeChunk = 0;

t_cm_chunk* allocChunk()
{
    t_cm_chunk* chunk = firstFreeChunk;

    firstFreeChunk = chunk->next;

    chunk->next_free_mem    = 0;
    chunk->prev_free_mem    = 0;
    chunk->prev             = 0;
    chunk->next             = 0;
    chunk->status           = MEM_FREE;
    // chunk->offset = 0;
    // chunk->size   = 0;
    // chunk->alloc  = 0;
    // chunk->userData = 0;

    freeChunks--;

    return chunk;
}

void freeChunk(t_cm_chunk* chunk)
{
    // Link chunk in free list
    chunk->next = firstFreeChunk;
    firstFreeChunk = chunk;

    // Increase counter
    freeChunks++;
}

t_cm_error allocChunkPool(void)
{
    struct t_page_chuncks* newPage;
    int i;

    newPage = (struct t_page_chuncks*)OSAL_Alloc(sizeof(struct t_page_chuncks));
    if(newPage == NULL)
        return CM_NO_MORE_MEMORY;

    // Link page
    newPage->nextPage = firstPage;
    firstPage = newPage;

    // Put chunk in free list
    for(i = 0; i < CHUNKS_PER_PAGE; i++)
        freeChunk(&newPage->chunks[i]);

    return CM_OK;
}

t_cm_error fillChunkPool(void)
{
    if(freeChunks < CHUNK_THRESOLD)
        return allocChunkPool();

    return CM_OK;
}

void freeChunkPool(void)
{
    while(firstPage != NULL)
    {
        struct t_page_chuncks* tofree = firstPage;
        firstPage = firstPage->nextPage;
        OSAL_Free(tofree);
    }

    firstFreeChunk = 0;
    freeChunks = 0;
}
