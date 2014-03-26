/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef REMOTE_ALLOCATOR_UTILS_H_
#define REMOTE_ALLOCATOR_UTILS_H_

#include <cm/engine/memory/inc/remote_allocator.h>
#include <cm/engine/memory/inc/chunk_mgr.h>

typedef enum {
    FREE_CHUNK_BEFORE,
    FREE_CHUNK_AFTER,
} t_mem_split_position;


PUBLIC void updateFreeList(t_cm_allocator_desc* alloc, t_cm_chunk* chunk);

PUBLIC void linkChunk(t_cm_allocator_desc* alloc, t_cm_chunk* prev,t_cm_chunk* add);
PUBLIC void unlinkChunk(t_cm_allocator_desc* alloc,t_cm_chunk* current);

PUBLIC void unlinkFreeMem(t_cm_allocator_desc* alloc,t_cm_chunk* current);
PUBLIC void linkFreeMemBefore(t_cm_chunk* add, t_cm_chunk* next);
PUBLIC void linkFreeMemAfter(t_cm_chunk* prev,t_cm_chunk* add);

PUBLIC t_cm_chunk* splitChunk(t_cm_allocator_desc* alloc, t_cm_chunk *chunk, t_uint32 offset, t_mem_split_position position);

#endif /*REMOTE_ALLOCATOR_UTILS_H_*/
