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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#include "b2r2_internal.h"
#include "b2r2_mem_alloc.h"

/* Forward declarations */
static struct b2r2_mem_block *b2r2_mem_block_alloc(
		struct b2r2_control *cont, u32 offset, u32 size, bool free);
static void b2r2_mem_block_free(struct b2r2_mem_block *mem_block);
static int b2r2_mem_heap_status(struct b2r2_mem_heap *mem_heap,
		struct b2r2_mem_heap_status *mem_heap_status);

/* Align value down to specified alignment */
static inline u32 align_down(u32 align, u32 value)
{
	return value & ~(align - 1);
}

/* Align value up to specified alignment */
static inline u32 align_up(u32 align, u32 value)
{
	return (value + align - 1) & ~(align - 1);
}


#ifdef CONFIG_DEBUG_FS
/* About debugfs:
 *  debugfs is a mountable debug file system.
 *
 *  Mount like this:
 *  mkdir /debug
 *  mount -t debugfs none /debug
 *  ls /debug/b2r2/mem
 *
 *  ls -al /debug/b2r2/mem/blocks
 *  cat /debug/b2r2/mem/stats
 */


/* Create string containing memory heap status */
static char *get_b2r2_mem_stats(struct b2r2_mem_heap *mem_heap, char *buf)
{
	struct b2r2_mem_heap_status mem_heap_status;

	if (b2r2_mem_heap_status(mem_heap, &mem_heap_status) != 0) {
		strcpy(buf, "Error, failed to get status\n");
		return buf;
	}

	sprintf(buf,
			"Handle                 : 0x%lX\n"
			"Physical start address : 0x%lX\n"
			"Size                   : %lu\n"
			"Align                  : %lu\n"
			"No of blocks allocated : %lu\n"
			"Allocated size         : %lu\n"
			"No of free blocks      : %lu\n"
			"Free size              : %lu\n"
			"No of locks            : %lu\n"
			"No of locked           : %lu\n"
			"No of nodes            : %lu\n",
			(unsigned long) mem_heap,
			(unsigned long) mem_heap_status.start_phys_addr,
			(unsigned long) mem_heap_status.size,
			(unsigned long) mem_heap_status.align,
			(unsigned long) mem_heap_status.num_alloc,
			(unsigned long) mem_heap_status.allocated_size,
			(unsigned long) mem_heap_status.num_free,
			(unsigned long) mem_heap_status.free_size,
			(unsigned long) mem_heap_status.num_locks,
			(unsigned long) mem_heap_status.num_locked,
			(unsigned long) mem_heap_status.num_nodes);

	return buf;
}

/*
 * Print memory heap status on file
 * (Use like "cat /debug/b2r2/mem/stats")
 */
static int debugfs_b2r2_mem_stats_read(struct file *filp, char __user *buf,
				       size_t count, loff_t *f_pos)
{
	struct b2r2_mem_heap *mem_heap = filp->f_dentry->d_inode->i_private;
	char Buf[400];
	size_t dev_size;
	int ret = 0;

	get_b2r2_mem_stats(mem_heap, Buf);
	dev_size = strlen(Buf);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, Buf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;

out:
	return ret;
}

/* debugfs file operations for the "stats" file */
static const struct file_operations debugfs_b2r2_mem_stats_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_mem_stats_read,
};

/* read function for file in the "blocks" sub directory */
static int debugfs_b2r2_mem_block_read(struct file *filp, char __user *buf,
				       size_t count, loff_t *f_pos)
{
	struct b2r2_mem_block *mem_block = filp->f_dentry->d_inode->i_private;
	char Buf[200];
	size_t dev_size;
	int ret = 0;

	dev_size = sprintf(Buf, "offset: %08lX %s size: %8d "
			"lock_count: %2d\n",
			(unsigned long) mem_block->offset,
			mem_block->free ? "free" : "allc",
			mem_block->size,
			mem_block->lock_count);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, Buf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;

out:
	return ret;
}

/* debugfs file operations for files in the "blocks" directory */
static const struct file_operations debugfs_b2r2_mem_block_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_mem_block_read,
};

/*
 * Create or update the debugfs directory entry for a file in the
 * "blocks" directory (a memory allocation)
 */
void debugfs_create_mem_block_entry(struct b2r2_mem_block *mem_block,
				    struct dentry *parent)
{
	struct timespec tm = current_kernel_time();
	struct timespec atime = tm;
	struct timespec mtime = tm;
	struct timespec ctime = tm;

	if (!IS_ERR_OR_NULL(mem_block->debugfs_block)) {
		atime = mem_block->debugfs_block->d_inode->i_atime;
		ctime = mem_block->debugfs_block->d_inode->i_ctime;
		debugfs_remove(mem_block->debugfs_block);
		mem_block->debugfs_block = NULL;
	}

	/* Add the block in debugfs */
	if (mem_block->free)
		sprintf(mem_block->debugfs_fname, "%08lX free",
				(unsigned long) mem_block->offset);
	else {
		sprintf(mem_block->debugfs_fname, "%08lX allc h:%08lX "
				"lck:%d ",
				(unsigned long) mem_block->offset,
				(unsigned long) mem_block,
				mem_block->lock_count);
	}

	mem_block->debugfs_block = debugfs_create_file(
		mem_block->debugfs_fname,
		0444, parent, mem_block,
		&debugfs_b2r2_mem_block_fops);
	if (!IS_ERR_OR_NULL(mem_block->debugfs_block)) {
		mem_block->debugfs_block->d_inode->i_size = mem_block->size;
		mem_block->debugfs_block->d_inode->i_atime = atime;
		mem_block->debugfs_block->d_inode->i_mtime = mtime;
		mem_block->debugfs_block->d_inode->i_ctime = ctime;
	}
}
#endif   /* CONFIG_DEBUG_FS */

/* Module initialization function */
int b2r2_mem_init(struct b2r2_control *cont,
		u32 heap_size, u32 align, u32 node_size)
{
	struct b2r2_mem_block *mem_block;
	u32 aligned_size;

	dev_info(cont->dev, "%s: Creating heap for size %d bytes\n",
		__func__, (int) heap_size);

	/* Align size */
	aligned_size = align_down(align, heap_size);
	if (aligned_size == 0)
		return -EINVAL;

	cont->mem_heap.start_virt_ptr = dma_alloc_coherent(cont->dev,
		aligned_size, &(cont->mem_heap.start_phys_addr), GFP_KERNEL);
	if (!cont->mem_heap.start_phys_addr || !cont->mem_heap.start_virt_ptr) {
		printk(KERN_ERR "B2R2_MEM: Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Initialize the heap */
	cont->mem_heap.size  = aligned_size;
	cont->mem_heap.align = align;

	INIT_LIST_HEAD(&cont->mem_heap.blocks);

#ifdef CONFIG_DEBUG_FS
	/* Register debugfs */
	if (!IS_ERR_OR_NULL(cont->mem_heap.debugfs_root_dir)) {
		cont->mem_heap.debugfs_heap_stats = debugfs_create_file(
			"stats", 0444, cont->mem_heap.debugfs_root_dir,
			&cont->mem_heap, &debugfs_b2r2_mem_stats_fops);
		cont->mem_heap.debugfs_dir_blocks = debugfs_create_dir(
			"blocks", cont->mem_heap.debugfs_root_dir);
	}
#endif

	/* Create the first _free_ memory block */
	mem_block = b2r2_mem_block_alloc(cont, 0, aligned_size, true);
	if (!mem_block) {
		dma_free_coherent(cont->dev, aligned_size,
			cont->mem_heap.start_virt_ptr,
			cont->mem_heap.start_phys_addr);
		printk(KERN_ERR "B2R2_MEM: Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Add the free block to the blocks list */
	list_add(&mem_block->list, &cont->mem_heap.blocks);

	/* Allocate separate heap for B2R2 nodes */
	cont->mem_heap.node_size = node_size;
	cont->mem_heap.node_heap = dma_pool_create("b2r2_node_cache",
		cont->dev, node_size, align, 4096);
	if (!cont->mem_heap.node_heap) {
		b2r2_mem_block_free(mem_block);
		dma_free_coherent(cont->dev, aligned_size,
			cont->mem_heap.start_virt_ptr,
			cont->mem_heap.start_phys_addr);
		printk(KERN_ERR "B2R2_MEM: Failed to allocate memory\n");
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(b2r2_mem_init);

/* Module exit function */
void b2r2_mem_exit(struct b2r2_control *cont)
{
	struct list_head *ptr;

	/* Free B2R2 node heap */
	dma_pool_destroy(cont->mem_heap.node_heap);

	list_for_each(ptr, &cont->mem_heap.blocks) {
		struct b2r2_mem_block *mem_block =
			list_entry(ptr, struct b2r2_mem_block, list);

		b2r2_mem_block_free(mem_block);
	}

	dma_free_coherent(cont->dev, cont->mem_heap.size,
		cont->mem_heap.start_virt_ptr,
		cont->mem_heap.start_phys_addr);
}
EXPORT_SYMBOL(b2r2_mem_exit);

/* Return status of the heap */
static int b2r2_mem_heap_status(struct b2r2_mem_heap *mheap,
		struct b2r2_mem_heap_status *mem_heap_status)
{
	struct list_head *ptr;

	if (!mheap || !mem_heap_status)
		return -EINVAL;
	memset(mem_heap_status, 0, sizeof(*mem_heap_status));

	/* Lock the heap */
	spin_lock(&mheap->heap_lock);

	/* Fill in static info */
	mem_heap_status->start_phys_addr = mheap->start_phys_addr;
	mem_heap_status->size = mheap->size;
	mem_heap_status->align = mheap->align;

	list_for_each(ptr, &mheap->blocks) {
		struct b2r2_mem_block *mem_block =
			list_entry(ptr, struct b2r2_mem_block, list);

		if (mem_block->free) {
			mem_heap_status->num_free++;
			mem_heap_status->free_size += mem_block->size;
		} else {
			if (mem_block->lock_count) {
				mem_heap_status->num_locked++;
				mem_heap_status->num_locks +=
					mem_block->lock_count;
			}
			mem_heap_status->num_alloc++;
			mem_heap_status->allocated_size += mem_block->size;
		}
	}

	spin_unlock(&mheap->heap_lock);

	return 0;
}
EXPORT_SYMBOL(b2r2_mem_heap_status);

/* Internal: Allocate a housekeeping structure
 * for an allocated or free memory block
 */
static struct b2r2_mem_block *b2r2_mem_block_alloc(
		struct b2r2_control *cont, u32 offset, u32 size, bool free)
{
	struct b2r2_mem_block *mem_block = kmalloc(
		sizeof(struct b2r2_mem_block), GFP_KERNEL);

	if (mem_block) {
		mem_block->offset     = offset;
		mem_block->size       = size;
		mem_block->free       = free;
		mem_block->lock_count = 0;

		INIT_LIST_HEAD(&mem_block->list);

#ifdef CONFIG_DEBUG_FS
		mem_block->debugfs_block  = NULL;
		/* Add the block in debugfs */
		debugfs_create_mem_block_entry(mem_block,
			cont->mem_heap.debugfs_dir_blocks);
#endif
	}

	return mem_block;
}

/* Internal: Release housekeeping structure */
static void b2r2_mem_block_free(struct b2r2_mem_block *mem_block)
{
	if (mem_block) {
#ifdef CONFIG_DEBUG_FS
		debugfs_remove(mem_block->debugfs_block);
#endif
		kfree(mem_block);
	}
}

/* Allocate a block from the heap */
int b2r2_mem_alloc(struct b2r2_control *cont, u32 requested_size,
		u32 *returned_size, u32 *mem_handle)
{
	int ret = 0;
	struct list_head *ptr;
	struct b2r2_mem_block *found_mem_block = NULL;
	u32 aligned_size;

	if (!mem_handle)
		return -EINVAL;

	printk(KERN_INFO "%s: size=%d\n", __func__, requested_size);

	*mem_handle = 0;

	/* Lock the heap */
	spin_lock(&cont->mem_heap.heap_lock);

	aligned_size = align_up(cont->mem_heap.align, requested_size);
	/* Try to find the best matching free block of suitable size */
	list_for_each(ptr, &cont->mem_heap.blocks) {
		struct b2r2_mem_block *mem_block =
			list_entry(ptr, struct b2r2_mem_block, list);

		if (mem_block->free && mem_block->size >= aligned_size &&
				(!found_mem_block ||
				mem_block->size < found_mem_block->size)) {
			found_mem_block = mem_block;
			if (found_mem_block->size == aligned_size)
				break;
		}
	}

	if (found_mem_block) {
		struct b2r2_mem_block *new_block
			= b2r2_mem_block_alloc(cont,
				found_mem_block->offset,
				requested_size, false);

		if (new_block) {
			/* Insert the new block before the found block */
			list_add_tail(&new_block->list,
				&found_mem_block->list);

			/* Split the free block */
			found_mem_block->offset += aligned_size;
			found_mem_block->size   -= aligned_size;

			if (found_mem_block->size == 0)
				b2r2_mem_block_free(found_mem_block);
			else {
#ifdef CONFIG_DEBUG_FS
				debugfs_create_mem_block_entry(
					found_mem_block,
					cont->mem_heap.debugfs_dir_blocks);
#endif
			}

			*mem_handle = (u32) new_block;
			*returned_size = aligned_size;
		} else {
			ret = -ENOMEM;
		}
	} else
		ret = -ENOMEM;

	if (ret != 0) {
		*returned_size = 0;
		*mem_handle = (u32) 0;
	}

	/* Unlock */
	spin_unlock(&cont->mem_heap.heap_lock);

	return ret;
}
EXPORT_SYMBOL(b2r2_mem_alloc);

/* Free the allocated block */
int b2r2_mem_free(struct b2r2_control *cont, u32 mem_handle)
{
	int ret = 0;
	struct b2r2_mem_block *mem_block = (struct b2r2_mem_block *) mem_handle;

	if (!mem_block)
		return -EINVAL;

	/* Lock the heap */
	spin_lock(&cont->mem_heap.heap_lock);

	if (!ret && mem_block->free)
		ret = -EINVAL;

	if (!ret) {
		printk(KERN_INFO "%s: freeing block 0x%p\n", __func__, mem_block);
		/* Release the block */

		mem_block->free = true;
		mem_block->size = align_up(cont->mem_heap.align,
					   mem_block->size);

		/* Join with previous block if possible */
		if (mem_block->list.prev != &cont->mem_heap.blocks) {
			struct b2r2_mem_block *prev_block =
				list_entry(mem_block->list.prev,
					struct b2r2_mem_block, list);

			if (prev_block->free &&
				(prev_block->offset + prev_block->size) ==
				mem_block->offset) {
				mem_block->offset = prev_block->offset;
				mem_block->size   += prev_block->size;

				b2r2_mem_block_free(prev_block);
			}
		}

		/* Join with next block if possible */
		if (mem_block->list.next != &cont->mem_heap.blocks) {
			struct b2r2_mem_block *next_block
				= list_entry(mem_block->list.next,
						struct b2r2_mem_block,
						list);

			if (next_block->free &&
				(mem_block->offset + mem_block->size) ==
				next_block->offset) {
				mem_block->size   += next_block->size;

				b2r2_mem_block_free(next_block);
			}
		}
#ifdef CONFIG_DEBUG_FS
		debugfs_create_mem_block_entry(mem_block,
				cont->mem_heap.debugfs_dir_blocks);
#endif
	}

	/* Unlock */
	spin_unlock(&cont->mem_heap.heap_lock);

	return ret;
}
EXPORT_SYMBOL(b2r2_mem_free);

/* Lock the allocated block in memory */
int b2r2_mem_lock(struct b2r2_control *cont, u32 mem_handle,
		u32 *phys_addr, void **virt_ptr, u32 *size)
{
	struct b2r2_mem_block *mem_block =
		(struct b2r2_mem_block *) mem_handle;

	if (!mem_block)
		return -EINVAL;

	/* Lock the heap */
	spin_lock(&cont->mem_heap.heap_lock);

	mem_block->lock_count++;

	if (phys_addr)
		*phys_addr = cont->mem_heap.start_phys_addr + mem_block->offset;
	if (virt_ptr)
		*virt_ptr = (char *) cont->mem_heap.start_virt_ptr +
			mem_block->offset;
	if (size)
		*size = align_up(cont->mem_heap.align, mem_block->size);
#ifdef CONFIG_DEBUG_FS
	debugfs_create_mem_block_entry(mem_block,
		cont->mem_heap.debugfs_dir_blocks);
#endif

	spin_unlock(&cont->mem_heap.heap_lock);

	return 0;
}
EXPORT_SYMBOL(b2r2_mem_lock);

/* Unlock the allocated block in memory */
int b2r2_mem_unlock(struct b2r2_control *cont, u32 mem_handle)
{
	struct b2r2_mem_block *mem_block =
		(struct b2r2_mem_block *) mem_handle;

	if (!mem_block)
		return -EINVAL;

	/* Lock the heap */
	spin_lock(&cont->mem_heap.heap_lock);

	mem_block->lock_count--;

	spin_unlock(&cont->mem_heap.heap_lock);

	/* debugfs will be updated in release */
	return 0;
/*	return b2r2_mem_free(mem_handle);*/
}
EXPORT_SYMBOL(b2r2_mem_unlock);

/* Allocate one or more b2r2 nodes from DMA pool */
int b2r2_node_alloc(struct b2r2_control *cont, u32 num_nodes,
		struct b2r2_node **first_node)
{
	int i;
	int ret = 0;
	u32 physical_address;
	struct b2r2_node *first_node_ptr;
	struct b2r2_node *node_ptr;

	/* Check input parameters */
	if ((num_nodes <= 0) || !first_node) {
		dev_err(cont->dev,
			"B2R2_MEM: Invalid parameter for b2r2_node_alloc, "
			"num_nodes=%d, first_node=%ld\n",
			(int) num_nodes, (long) first_node);
		return -EINVAL;
	}

	/* Allocate the first node */
	first_node_ptr = dma_pool_alloc(cont->mem_heap.node_heap,
		GFP_DMA | GFP_KERNEL, &physical_address);
	if (!first_node_ptr) {
		dev_err(cont->dev,
			"B2R2_MEM: Failed to allocate memory for node\n");
		return -ENOMEM;
	}

	/* Initialize first node */
	first_node_ptr->next = NULL;
	first_node_ptr->physical_address = physical_address +
			offsetof(struct b2r2_node, node);

	/* Allocate and initialize remaining nodes, */
	/* and link them into a list                */
	for (i = 1, node_ptr = first_node_ptr; i < num_nodes; i++) {
		node_ptr->next = dma_pool_alloc(cont->mem_heap.node_heap,
				GFP_DMA | GFP_KERNEL, &physical_address);
		if (node_ptr->next) {
			node_ptr = node_ptr->next;
			node_ptr->next = NULL;
			node_ptr->physical_address = physical_address +
					offsetof(struct b2r2_node, node);
		} else {
			printk(KERN_ERR "B2R2_MEM: Failed to allocate memory for node\n");
			ret = -ENOMEM;
			break;
		}
	}

	/* If all nodes were allocated successfully, */
	/* return the first node                     */
	if (!ret)
		*first_node = first_node_ptr;
	else
		b2r2_node_free(cont, first_node_ptr);

	return ret;
}
EXPORT_SYMBOL(b2r2_node_alloc);

/* Free a linked list of b2r2 nodes */
void b2r2_node_free(struct b2r2_control *cont, struct b2r2_node *first_node)
{
	struct b2r2_node *current_node = first_node;
	struct b2r2_node *next_node = NULL;

	/* Traverse the linked list and free the nodes */
	while (current_node != NULL) {
		next_node = current_node->next;
		dma_pool_free(cont->mem_heap.node_heap, current_node,
				current_node->physical_address -
				offsetof(struct b2r2_node, node));
		current_node = next_node;
	}
}
EXPORT_SYMBOL(b2r2_node_free);

MODULE_AUTHOR("Robert Lind <robert.lind@ericsson.com");
MODULE_DESCRIPTION("Ericsson AB B2R2 physical memory driver");
MODULE_LICENSE("GPL");
