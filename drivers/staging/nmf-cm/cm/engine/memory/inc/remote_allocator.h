/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 *
 * \note: In this module, we assume that parameters were checked !!
 */
#ifndef __REMOTE_ALLOCATOR_H_
#define __REMOTE_ALLOCATOR_H_

/*
 * Include
 */
#include <cm/inc/cm_type.h>
#include <cm/engine/memory/inc/memory_type.h>


/*
 * Description of the memory block status
 */
typedef enum {
    MEM_USED = 0,     /* Memory block is used */
    MEM_FREE = 1     /* Memory block is free */
} t_mem_status;

/*
 * Chunk structure.
 */
struct cm_allocator_desc;
typedef struct chunk_struct
{
    /* Double linked list of chunks */
    struct chunk_struct *prev;
    struct chunk_struct *next;

    /* Double linked list of free memory */
    struct chunk_struct *prev_free_mem;
    struct chunk_struct *next_free_mem;

    /* Offset of the block memory */
    t_uint32 offset;

    /* Size of the block memory */
    t_cm_size size;

    /* Status of the block memory */
    t_mem_status status;

    /* User data */
    t_uint16 userData;

    /* Alloc debug info*/
    t_uint32 domainId;

    /* Alloc desc backlink */
    struct cm_allocator_desc *alloc;
} t_cm_chunk;

/*!
 * \brief Identifier of an internal memory handle
 * \ingroup MEMORY_INTERNAL
 */
typedef t_cm_chunk* t_memory_handle;

#define INVALID_MEMORY_HANDLE ((t_cm_chunk*)NULL)


/*
 * Context structure
 */
#define BINS                    63

//TODO, juraj, add memType to alloc struct ?
typedef struct cm_allocator_desc {
    const char     *pAllocName;             /* Name of the allocator */
    t_uint32        maxSize;                /* Max size of the allocator -> Potentially increase/decrease by stack management */
    t_uint32        sbrkSize;               /* Current size of allocator */
    t_uint32        offset;                 /* Offset of the allocator */
    t_cm_chunk     *chunks;                 /* Array of chunk */
    t_cm_chunk     *lastChunk;              /* Null terminated last chunk of previous array declaration */
    t_cm_chunk     *free_mem_chunks[BINS];  /* List of free memory */
    struct cm_allocator_desc* next;         /* List of allocator */
} t_cm_allocator_desc;

int bin_index(unsigned int sz);

/*
 * Functions
 */
/*!
 * \brief Create a new allocator for a piece of memory (hw mapped (xram, yram))
 * Any further allocation into this piece of memory will return an offset inside it.
 * (a constant offset value can be added to this offset)
 *
 * \retval t_cm_allocator_desc*  new memory allocator identifier
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_cm_allocator_desc* cm_MM_CreateAllocator(
        t_cm_size size,                     //!< [in] Size of the addressable space in bytes
        t_uint32 offset,                    //!< [in] Constant offset to add to each allocated block base address
        const char* name                    //!< [in] Name of the allocator
        );

/*!
 * \brief Free a memory allocator descriptor
 *
 * \retval t_cm_error
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_cm_error cm_MM_DeleteAllocator(
        t_cm_allocator_desc* alloc          //!< [in] Identifier of the memory allocator to be freed
        );


/*!
 * \brief Resize an allocator to the size value.
 *
 * \retval t_cm_error
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_cm_error cm_MM_ResizeAllocator(
        t_cm_allocator_desc* alloc,         //!< [in] Identifier of the memory allocator used to allocate the piece of memory
        t_cm_size size                      //!< [in] Size of the addressable space in allocDesc granularity
        );

/*!
 * \brief Check validity of a user handle
 */
t_cm_error cm_MM_getValidMemoryHandle(t_cm_memory_handle handle, t_memory_handle* validHandle);

/*!
 * \brief Wrapper routine to allocate some memory into a given allocator
 *
 * \retval t_memory_handle handle on the new allocated piece of memory
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_memory_handle cm_MM_Alloc(
        t_cm_allocator_desc* alloc,             //!< [in] Identifier of the memory allocator
        t_cm_size size,                         //!< [in] Size of the addressable space
        t_cm_memory_alignment memAlignment,     //!< [in] Alignment constraint
        t_uint32 seg_offset,                    //!< [in] Offset of range where allocating
        t_uint32 seg_size,                      //!< [in] Size of range where allocating
        t_uint32 domainId
        );


/*!
 * \brief Routine to reallocate memory for a given handle
 *
 * Routine to reallocate memory for a given handle. The chunk can be extended or shrinked in both
 * directions - top and bottom, depending on the offset and size arguments.
 *
 * \retval t_memory_handle handle on the reallocated piece of memory
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_cm_error cm_MM_Realloc(
                t_cm_allocator_desc* alloc,
                const t_cm_size size,
                const t_uint32 offset,
                t_memory_handle *handle);
/*!
 * \brief Frees the allocated chunk
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC void cm_MM_Free(
        t_cm_allocator_desc* alloc,             //!< [in] Identifier of the memory allocator
        t_memory_handle memHandle               //!< [in] Memory handle to free
        );


/*!
 * \brief Get the allocator status
 *
 * \param[in] alloc Identifier of the memory allocator
 * \param[out] pStatus Status of the allocator
 *
 * \retval t_cm_error
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_cm_error cm_MM_GetAllocatorStatus(t_cm_allocator_desc* alloc, t_uint32 offset, t_uint32 size, t_cm_allocator_status *pStatus);

/*!
 * \brief Returns the offset into a given memory allocator of an allocated piece of memory
 *
 * \param[in] memHandle handle on the given memory
 *
 * \retval t_uint32  offset into the given memory allocator
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_uint32 cm_MM_GetOffset(t_memory_handle memHandle);


/*!
 * \brief Returns the size in word size for a given memory allocator of an allocated piece of memory
 *
 * \param[in] memHandle handle on the given memory
 *
 * \retval t_uint32  size in wordsize for the given memory allocator
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_uint32 cm_MM_GetSize(t_memory_handle memHandle);

/*!
 * \brief Returns the size in bytes for a given memory allocator
 *
 * \param[in] allocDesc Identifier of the memory allocator
 * \retval size
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC t_uint32 cm_MM_GetAllocatorSize(t_cm_allocator_desc* allocDesc);


/*!
 * \brief Set the user data of an allocated piece of memory
 *
 * \param[in] memHandle handle on the given memory
 * \param[in] userData UsedData of the given memory piece
 *
 * \retval t_cm_error
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC void cm_MM_SetMemoryHandleUserData (t_memory_handle memHandle, t_uint16 userData);


/*!
 * \brief Return the user data of an allocated piece of memory
 *
 * \param[in] memHandle handle on the given memory
 * \param[out] pUserData returned UsedData of the given memory piece
 *
 * \retval t_cm_error
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC void cm_MM_GetMemoryHandleUserData(t_memory_handle memHandle, t_uint16 *pUserData, t_cm_allocator_desc **alloc);

/*!
 * \brief Dump chunkd in the range of [start:end]
 *
 * \param[in] alloc Allocator descriptor
 * \param[in] start Range start
 * \param[in] end Range end
 *
 * \retval void
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC void cm_MM_DumpMemory(t_cm_allocator_desc* alloc, t_uint32 start, t_uint32 end);

/*!
 * \brief Change the domain for the given chunk of memory
 *
 * \param[in] memHandle The given chunk of memory
 * \param[in] domainId  The new domain id to set
 *
 * \retval void
 *
 * \ingroup MEMORY_INTERNAL
 */
PUBLIC void cm_MM_SetDefaultDomain(t_memory_handle memHandle, t_uint32 domainId);
#endif /* _REMOTE_ALLOCATOR_H_*/
