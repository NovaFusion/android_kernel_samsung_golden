/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef CHUNK_MGR_H_
#define CHUNK_MGR_H_

#include <cm/engine/memory/inc/remote_allocator.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

t_cm_error allocChunkPool(void);
t_cm_error fillChunkPool(void);
void freeChunkPool(void);

/***************************************************************************/
/*
 * allocChunk
 * param current  : Pointer on chunck to free
 *
 * Add a chunk in the chunck list
 *
 */
/***************************************************************************/
t_cm_chunk* allocChunk(void);

/***************************************************************************/
/*
 * freeChunk
 * param current  : Pointer on chunck to free
 *
 * Remove a chunk in the chunck list
 *
 */
/***************************************************************************/
void freeChunk(t_cm_chunk *chunk);

#endif /*CHUNK_MGR_H_*/
