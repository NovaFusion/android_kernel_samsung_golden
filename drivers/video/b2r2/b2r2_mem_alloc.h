/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 internal Memory allocator
 *
 * Author: Robert Lind <robert.lind@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __B2R2_MEM_ALLOC_H
#define __B2R2_MEM_ALLOC_H

#include "b2r2_internal.h"


/**
 * struct b2r2_mem_heap_status - Information about current state of the heap
 *
 * @start_phys_addr: Physical address of the the memory area
 * @size: Size of the memory area
 * @align: Alignment of start and allocation sizes (in bytes).
 * @num_alloc: Number of memory allocations
 * @allocated_size: Size allocated (sum of requested sizes)
 * @num_free: Number of free blocks (fragments)
 * @free_size: Free size available for allocation
 * @num_locks: Sum of number of number of locks on memory allocations
 * @num_locked: Number of locked memory allocations
 * @num_nodes: Number of node allocations
 *
 **/
struct b2r2_mem_heap_status {
	u32 start_phys_addr;
	u32 size;
	u32 align;
	u32 num_alloc;
	u32 allocated_size;
	u32 num_free;
	u32 free_size;
	u32 num_locks;
	u32 num_locked;
	u32 num_nodes;
};

/**
 * struct b2r2_mem_block - Represents one block of b2r2
 *                         physical memory, free or allocated
 *
 * @list: For membership in list
 * @offset: Offset in b2r2 physical memory area (aligned)
 * @size: Size of the object (requested size if busy, else actual)
 * @free: True if the block is free
 * @lock_count: Lock count
 * @debugfs_fname: Debugfs file name
 * @debugfs_block: Debugfs dir entry for the block
 */
struct b2r2_mem_block {
	struct list_head	list;
	u32					offset;
	u32					size;
	bool				free;
	u32					lock_count;
#ifdef CONFIG_DEBUG_FS
	char				debugfs_fname[80];
	struct dentry		*debugfs_block;
#endif
};


/* B2R2 memory API (kernel) */

/**
 * b2r2_mem_init() - Initializes the B2R2 memory manager
 * @dev: Pointer to device to use for allocating the memory heap
 * @heap_size: Size of the heap (in bytes)
 * @align: Alignment to use for memory allocations on heap (in bytes)
 * @node_size: Size of each B2R2 node (in bytes)
 *
 * Returns 0 if success, else negative error code
 **/
int b2r2_mem_init(struct b2r2_control *cont,
		u32 heap_size, u32 align, u32 node_size);

/**
 * b2r2_mem_exit() - Cleans up the B2R2 memory manager
 *
 **/
void b2r2_mem_exit(struct b2r2_control *cont);

/**
 * b2r2_mem_alloc() - Allocates memory block from physical memory heap
 * @requested_size: Requested size
 * @returned_size: Actual size of memory block. Might be adjusted due to
 *                 alignment but is always >= requested size if function
 *                 succeeds
 * @mem_handle: Returned memory handle
 *
 * All memory allocations are movable when not locked.
 * Returns 0 if OK else negative error value
 **/
int b2r2_mem_alloc(struct b2r2_control *cont, u32 requested_size,
		u32 *returned_size, u32 *mem_handle);

/**
 * b2r2_mem_free() - Frees an allocation
 * @mem_handle: Memory handle
 *
 * Returns 0 if OK else negative error value
 **/
int b2r2_mem_free(struct b2r2_control *cont, u32 mem_handle);

/**
 * b2r2_mem_lock() - Lock memory in memory and return physical address
 * @mem_handle: Memory handle
 * @phys_addr: Returned physical address to start of memory allocation.
 *             May be NULL.
 * @virt_ptr: Returned virtual address pointer to start of memory allocation.
 *            May be NULL.
 * @size: Returned size of memory allocation. May be NULL.
 *
 * The adress of the memory allocation is locked and the physical address
 * is returned.
 * The lock count is incremented by one.
 * You need to call b2r2_mem_unlock once for each call to
 * b2r2_mem_lock.
 * Returns 0 if OK else negative error value
 **/
int b2r2_mem_lock(struct b2r2_control *cont, u32 mem_handle,
		u32 *phys_addr, void **virt_ptr, u32 *size);

/**
 * b2r2_mem_unlock() - Unlock previously locked memory
 * @mem_handle: Memory handle
 *
 * Decrements lock count. When lock count reaches 0 the
 * memory area is movable again.
 * Returns 0 if OK else negative error value
 **/
int b2r2_mem_unlock(struct b2r2_control *cont, u32 mem_handle);

/**
 * b2r2_node_alloc() - Allocates B2R2 node from physical memory heap
 * @num_nodes: Number of linked nodes to allocate
 * @first_node: Returned pointer to first node in linked list
 *
 * Returns 0 if OK else negative error value
 **/
int b2r2_node_alloc(struct b2r2_control *cont, u32 num_nodes,
		struct b2r2_node **first_node);

/**
 * b2r2_node_free() - Frees a linked list of allocated B2R2 nodes
 * @first_node: Pointer to first node in linked list
 *
 * Returns 0 if OK else negative error value
 **/
void b2r2_node_free(struct b2r2_control *cont, struct b2r2_node *first_node);


#endif /* __B2R2_MEM_ALLOC_H */
