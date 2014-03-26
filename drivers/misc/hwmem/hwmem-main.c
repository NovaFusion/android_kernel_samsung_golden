/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Hardware memory driver, hwmem
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>,
 * Johan Mossberg <johan.xx.mossberg@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/list.h>
#include <linux/hwmem.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/kallsyms.h>
#include <linux/vmalloc.h>
#include "cache_handler.h"

#define S32_MAX 2147483647

struct hwmem_alloc_threadg_info {
	struct list_head list;

	struct pid *threadg_pid; /* Ref counted */

	enum hwmem_access access;
};

struct hwmem_alloc {
	struct list_head list;

	atomic_t ref_cnt;

	enum hwmem_alloc_flags flags;
	struct hwmem_mem_type_struct *mem_type;

	void *allocator_hndl;
	phys_addr_t paddr;
	void *kaddr;
	size_t size;
	s32 name;

	/* Access control */
	enum hwmem_access default_access;
	struct list_head threadg_info_list;

	/* Cache handling */
	struct cach_buf cach_buf;

	/* Scattered allocation parameters */
	struct page **sglist;
	size_t nr_of_pages;

#ifdef CONFIG_DEBUG_FS
	/* Debug */
	void *creator;
	pid_t creator_tgid;
#endif /* #ifdef CONFIG_DEBUG_FS */
};

static struct platform_device *hwdev;

static LIST_HEAD(alloc_list);
static DEFINE_IDR(global_idr);
static DEFINE_MUTEX(lock);

static void vm_open(struct vm_area_struct *vma);
static void vm_close(struct vm_area_struct *vma);
static struct vm_operations_struct vm_ops = {
	.open = vm_open,
	.close = vm_close,
};

static void kunmap_alloc(struct hwmem_alloc *alloc);

/* Helpers */

static void destroy_alloc_threadg_info(
		struct hwmem_alloc_threadg_info *info)
{
	if (info->threadg_pid)
		put_pid(info->threadg_pid);

	kfree(info);
}

static void clean_alloc_threadg_info_list(struct hwmem_alloc *alloc)
{
	struct hwmem_alloc_threadg_info *info;
	struct hwmem_alloc_threadg_info *tmp;

	list_for_each_entry_safe(info, tmp, &(alloc->threadg_info_list),
									list) {
		list_del(&info->list);
		destroy_alloc_threadg_info(info);
	}
}

static enum hwmem_access get_access(struct hwmem_alloc *alloc)
{
	struct hwmem_alloc_threadg_info *info;
	struct pid *my_pid;
	bool found = false;

	my_pid = find_get_pid(task_tgid_nr(current));
	if (!my_pid)
		return 0;

	list_for_each_entry(info, &(alloc->threadg_info_list), list) {
		if (info->threadg_pid == my_pid) {
			found = true;
			break;
		}
	}

	put_pid(my_pid);

	if (found)
		return info->access;
	else
		return alloc->default_access;
}

static void clear_alloc_mem(struct hwmem_alloc *alloc)
{
	cach_set_domain(&alloc->cach_buf, HWMEM_ACCESS_WRITE,
						HWMEM_DOMAIN_CPU, NULL);

	memset(alloc->kaddr, 0, alloc->size);
}

static void destroy_alloc(struct hwmem_alloc *alloc)
{
	list_del(&alloc->list);

	if (alloc->name != 0) {
		idr_remove(&global_idr, alloc->name);
		alloc->name = 0;
	}

	clean_alloc_threadg_info_list(alloc);

	kunmap_alloc(alloc);

	if (!IS_ERR_OR_NULL(alloc->allocator_hndl))
		alloc->mem_type->allocator_api.free(
					alloc->mem_type->allocator_instance,
							alloc->allocator_hndl);

	kfree(alloc);
}

static int kmap_alloc(struct hwmem_alloc *alloc)
{
	int ret;
	pgprot_t pgprot;
	void *alloc_kaddr;
	void *vmap_addr;

	if (alloc->mem_type->id != HWMEM_MEM_SCATTERED_SYS) {
		alloc_kaddr = alloc->mem_type->allocator_api.get_alloc_kaddr(
			alloc->mem_type->allocator_instance, alloc->allocator_hndl);
		if (IS_ERR(alloc_kaddr))
			return PTR_ERR(alloc_kaddr);
	}

	pgprot = PAGE_KERNEL;
	cach_set_pgprot_cache_options(&alloc->cach_buf, &pgprot);

	if (alloc->mem_type->id == HWMEM_MEM_SCATTERED_SYS) {
		/* map an array of pages into virtually contiguous space */
		vmap_addr = vmap(alloc->sglist, alloc->nr_of_pages, VM_MAP, PAGE_KERNEL);
		if (IS_ERR_OR_NULL(vmap_addr)) {
			dev_warn(&hwdev->dev, "Failed to vmap size %d", alloc->size);
			return -ENOMEM;
		}
		alloc->kaddr = vmap_addr;
	} else { /* contiguous or protected */
		ret = ioremap_page_range((unsigned long)alloc_kaddr,
			(unsigned long)alloc_kaddr + alloc->size, alloc->paddr, pgprot);
		if (ret < 0) {
			dev_warn(&hwdev->dev, "Failed to map %#x - %#x", alloc->paddr,
							alloc->paddr + alloc->size);
			return ret;
		}

		alloc->kaddr = alloc_kaddr;
	}

	return 0;
}

static void kunmap_alloc(struct hwmem_alloc *alloc)
{
	if (alloc->kaddr == NULL)
		return;

	if (alloc->mem_type->id == HWMEM_MEM_SCATTERED_SYS)
		vunmap(alloc->kaddr); /* release virtual mapping obtained by vmap() */
	else /* contiguous or protected */
		unmap_kernel_range((unsigned long)alloc->kaddr, alloc->size);

	alloc->kaddr = NULL;
}

static struct hwmem_mem_type_struct *resolve_mem_type(
						enum hwmem_mem_type mem_type)
{
	unsigned int i;
	for (i = 0; i < hwmem_num_mem_types; i++) {
		if (hwmem_mem_types[i].id == mem_type)
			return &hwmem_mem_types[i];
	}

	return ERR_PTR(-ENOENT);
}

/* HWMEM API */

struct hwmem_alloc *hwmem_alloc(size_t size, enum hwmem_alloc_flags flags,
		enum hwmem_access def_access, enum hwmem_mem_type mem_type)
{
	int ret;
	struct hwmem_alloc *alloc;

	if (hwdev == NULL) {
		printk(KERN_ERR "HWMEM: Badly configured\n");
		return ERR_PTR(-ENOMSG);
	}

	if (size == 0)
		return ERR_PTR(-EINVAL);

	mutex_lock(&lock);

	size = PAGE_ALIGN(size);

	alloc = kzalloc(sizeof(struct hwmem_alloc), GFP_KERNEL);
	if (alloc == NULL) {
		ret = -ENOMEM;
		goto alloc_alloc_failed;
	}

	INIT_LIST_HEAD(&alloc->list);
	atomic_inc(&alloc->ref_cnt);
	alloc->flags = flags;
	alloc->default_access = def_access;
	INIT_LIST_HEAD(&alloc->threadg_info_list);
#ifdef CONFIG_DEBUG_FS
	alloc->creator = __builtin_return_address(0);
	alloc->creator_tgid = task_tgid_nr(current);
#endif
	alloc->mem_type = resolve_mem_type(mem_type);

	if (IS_ERR(alloc->mem_type)) {
		ret = PTR_ERR(alloc->mem_type);
		goto resolve_mem_type_failed;
	}

	alloc->allocator_hndl = alloc->mem_type->allocator_api.alloc(
				alloc->mem_type->allocator_instance, size);
	if (IS_ERR(alloc->allocator_hndl)) {
		ret = PTR_ERR(alloc->allocator_hndl);
		goto allocator_failed;
	}

	if (alloc->mem_type->id == HWMEM_MEM_SCATTERED_SYS) {
		alloc->size = alloc->mem_type->allocator_api.get_alloc_size(
								alloc->allocator_hndl);
		alloc->nr_of_pages = alloc->size >> PAGE_SHIFT;
		alloc->sglist = alloc->mem_type->allocator_api.get_alloc_sglist(
								alloc->allocator_hndl);
		alloc->paddr = 0;
	} else {  /* contiguous or protected */
		alloc->paddr = alloc->mem_type->allocator_api.get_alloc_paddr(
								alloc->allocator_hndl);
		alloc->size = alloc->mem_type->allocator_api.get_alloc_size(
								alloc->allocator_hndl);
	}

	cach_init_buf(&alloc->cach_buf,
		      alloc->mem_type->id,
		      alloc->flags,
		      alloc->size);

	ret = kmap_alloc(alloc);
	if (ret < 0)
		goto kmap_alloc_failed;

	cach_set_buf_addrs(&alloc->cach_buf, alloc->kaddr, alloc->paddr);

	list_add_tail(&alloc->list, &alloc_list);

	if (alloc->mem_type->id != HWMEM_MEM_PROTECTED_SYS)
		clear_alloc_mem(alloc);

	goto out;

kmap_alloc_failed:
allocator_failed:
resolve_mem_type_failed:
	destroy_alloc(alloc);
alloc_alloc_failed:
	alloc = ERR_PTR(ret);

out:
	mutex_unlock(&lock);

	return alloc;
}
EXPORT_SYMBOL(hwmem_alloc);

void hwmem_release(struct hwmem_alloc *alloc)
{
	mutex_lock(&lock);

	if (atomic_dec_and_test(&alloc->ref_cnt))
		destroy_alloc(alloc);

	mutex_unlock(&lock);
}
EXPORT_SYMBOL(hwmem_release);

int hwmem_set_domain(struct hwmem_alloc *alloc, enum hwmem_access access,
		enum hwmem_domain domain, struct hwmem_region *region)
{
	mutex_lock(&lock);

	cach_set_domain(&alloc->cach_buf, access, domain, region);

	mutex_unlock(&lock);

	return 0;
}
EXPORT_SYMBOL(hwmem_set_domain);

int hwmem_pin(struct hwmem_alloc *alloc, struct hwmem_mem_chunk *mem_chunks,
							u32 *mem_chunks_length)
{
	/* Calculate hwmem_mem_chunk_length */
	if (mem_chunks == NULL) {
		if (alloc->mem_type->id == HWMEM_MEM_SCATTERED_SYS)
			*mem_chunks_length = alloc->nr_of_pages;
		else
			*mem_chunks_length = 1;

		return 0;
	}

	if (*mem_chunks_length < 1) {
		*mem_chunks_length = 1;
		printk(KERN_ERR "HWMEM: hwmem_pin mem_chunks_length < 1\n");
		return -ENOSPC;
	}

	mutex_lock(&lock);

	if (alloc->mem_type->id == HWMEM_MEM_SCATTERED_SYS) {
		int i;
		for (i = 0; i < alloc->nr_of_pages; i++) {
			mem_chunks[i].paddr = page_to_phys(alloc->sglist[i]);
			mem_chunks[i].size = PAGE_SIZE;
		}
		*mem_chunks_length = alloc->nr_of_pages;
	} else { /* contiguous or protected */
		mem_chunks[0].paddr = alloc->paddr;
		mem_chunks[0].size = alloc->size;
		*mem_chunks_length = 1;
	}

	mutex_unlock(&lock);

	return 0;
}
EXPORT_SYMBOL(hwmem_pin);

void hwmem_unpin(struct hwmem_alloc *alloc)
{
}
EXPORT_SYMBOL(hwmem_unpin);

static void vm_open(struct vm_area_struct *vma)
{
	atomic_inc(&((struct hwmem_alloc *)vma->vm_private_data)->ref_cnt);
}

static void vm_close(struct vm_area_struct *vma)
{
	hwmem_release((struct hwmem_alloc *)vma->vm_private_data);
}

int hwmem_mmap(struct hwmem_alloc *alloc, struct vm_area_struct *vma)
{
	unsigned long temp_addr;
	int ret = 0;
	int i = 0;
	unsigned long vma_size = vma->vm_end - vma->vm_start;
	enum hwmem_access access;
	mutex_lock(&lock);

	access = get_access(alloc);

	/* Check permissions */
	if ((!(access & HWMEM_ACCESS_WRITE) &&
				(vma->vm_flags & VM_WRITE)) ||
			(!(access & HWMEM_ACCESS_READ) &&
				(vma->vm_flags & VM_READ))) {
		ret = -EPERM;
		goto illegal_access;
	}

	if (vma_size > alloc->size) {
		ret = -EINVAL;
		goto illegal_size;
	}

	/*
	 * We don't want Linux to do anything (merging etc) with our VMAs as
	 * the offset is not necessarily valid
	 */
	vma->vm_flags |= VM_SPECIAL;
	cach_set_pgprot_cache_options(&alloc->cach_buf, &vma->vm_page_prot);
	vma->vm_private_data = (void *)alloc;
	atomic_inc(&alloc->ref_cnt);
	vma->vm_ops = &vm_ops;

	if (alloc->mem_type->id == HWMEM_MEM_SCATTERED_SYS) {
		/* VM_MIXEDMAP can contain "struct page" and pure PFN pages */
		vma->vm_flags |= VM_MIXEDMAP;
		temp_addr = vma->vm_start;

		for (i = 0; i < alloc->nr_of_pages; i++) {
			vm_insert_page(vma, temp_addr, alloc->sglist[i]);
			temp_addr += PAGE_SIZE;
		}
	} else { /* contiguous or protected */
		ret = remap_pfn_range(vma, vma->vm_start, alloc->paddr >> PAGE_SHIFT,
			min(vma_size, (unsigned long)alloc->size), vma->vm_page_prot);
		if (ret < 0)
			goto map_failed;
	}
	goto out;

map_failed:
	atomic_dec(&alloc->ref_cnt);
illegal_size:
illegal_access:

out:
	mutex_unlock(&lock);

	return ret;
}
EXPORT_SYMBOL(hwmem_mmap);

void *hwmem_kmap(struct hwmem_alloc *alloc)
{
	void *ret;

	mutex_lock(&lock);

	ret = alloc->kaddr;

	mutex_unlock(&lock);

	return ret;
}
EXPORT_SYMBOL(hwmem_kmap);

void hwmem_kunmap(struct hwmem_alloc *alloc)
{
}
EXPORT_SYMBOL(hwmem_kunmap);

int hwmem_set_access(struct hwmem_alloc *alloc,
		enum hwmem_access access, pid_t pid_nr)
{
	int ret;
	struct hwmem_alloc_threadg_info *info;
	struct pid *pid;
	bool found = false;

	pid = find_get_pid(pid_nr);
	if (!pid) {
		ret = -EINVAL;
		goto error_get_pid;
	}

	list_for_each_entry(info, &(alloc->threadg_info_list), list) {
		if (info->threadg_pid == pid) {
			found = true;
			break;
		}
	}

	if (!found) {
		info = kmalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			ret = -ENOMEM;
			goto error_alloc_info;
		}

		info->threadg_pid = pid;
		info->access = access;

		list_add_tail(&(info->list), &(alloc->threadg_info_list));
	} else {
		info->access = access;
	}

	return 0;

error_alloc_info:
	put_pid(pid);
error_get_pid:
	return ret;
}
EXPORT_SYMBOL(hwmem_set_access);

void hwmem_get_info(struct hwmem_alloc *alloc, u32 *size,
	enum hwmem_mem_type *mem_type, enum hwmem_access *access)
{
	mutex_lock(&lock);

	if (size != NULL)
		*size = alloc->size;
	if (mem_type != NULL)
		*mem_type = alloc->mem_type->id;
	if (access != NULL)
		*access = get_access(alloc);

	mutex_unlock(&lock);
}
EXPORT_SYMBOL(hwmem_get_info);

s32 hwmem_get_name(struct hwmem_alloc *alloc)
{
	int ret = 0, name;

	mutex_lock(&lock);

	if (alloc->name != 0) {
		ret = alloc->name;
		goto out;
	}

	while (true) {
		if (idr_pre_get(&global_idr, GFP_KERNEL) == 0) {
			ret = -ENOMEM;
			goto pre_get_id_failed;
		}

		ret = idr_get_new_above(&global_idr, alloc, 1, &name);
		if (ret == 0)
			break;
		else if (ret != -EAGAIN)
			goto get_id_failed;
	}

	if (name > S32_MAX) {
		ret = -ENOMSG;
		goto overflow;
	}

	alloc->name = name;

	ret = name;
	goto out;

overflow:
	idr_remove(&global_idr, name);
get_id_failed:
pre_get_id_failed:

out:
	mutex_unlock(&lock);

	return ret;
}
EXPORT_SYMBOL(hwmem_get_name);

struct hwmem_alloc *hwmem_resolve_by_name(s32 name)
{
	struct hwmem_alloc *alloc;

	mutex_lock(&lock);

	alloc = idr_find(&global_idr, name);
	if (alloc == NULL) {
		alloc = ERR_PTR(-EINVAL);
		goto find_failed;
	}
	atomic_inc(&alloc->ref_cnt);

	goto out;

find_failed:

out:
	mutex_unlock(&lock);

	return alloc;
}
EXPORT_SYMBOL(hwmem_resolve_by_name);

struct hwmem_alloc *hwmem_resolve_by_vm_addr(void *vm_addr)
{
	struct hwmem_alloc *alloc;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;

	if (vm_addr == NULL)
		return ERR_PTR(-EINVAL);

	down_write(&mm->mmap_sem);

	/* Find the first overlapping VMA */
	vma = find_vma(mm, (unsigned long)vm_addr);
	if (vma == NULL) {
		alloc = ERR_PTR(-EINVAL);
		goto out;
	}

	/* Check if VMA is from hwmem */
	if (vma->vm_ops != &vm_ops) {
		alloc = ERR_PTR(-EINVAL);
		goto out;
	}

	/* Fetch hwmem alloc reference from VMA */
	alloc = (struct hwmem_alloc *)vma->vm_private_data;
	if (alloc == NULL) {
		alloc = ERR_PTR(-EINVAL);
		goto out;
	}

	atomic_inc(&alloc->ref_cnt);

out:
	up_write(&mm->mmap_sem);

	return alloc;
}
EXPORT_SYMBOL(hwmem_resolve_by_vm_addr);

/* Debug */

#ifdef CONFIG_DEBUG_FS

static int debugfs_allocs_read(struct file *filp, char __user *buf,
						size_t count, loff_t *f_pos);

static const struct file_operations debugfs_allocs_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_allocs_read,
};

static int print_alloc(struct hwmem_alloc *alloc, char **buf, size_t buf_size)
{
	int ret;
	char creator[KSYM_SYMBOL_LEN];
	int i;

	if (sprint_symbol(creator, (unsigned long)alloc->creator) < 0)
		creator[0] = '\0';

	for (i = 0; i < 2; i++) {
		size_t buf_size_l;
		if (i == 0)
			buf_size_l = 0;
		else
			buf_size_l = buf_size;

		ret = snprintf(*buf, buf_size_l,
			"%#x\n"
				"\tSize: %u\n"
				"\tMemory type: %u\n"
				"\tName: %#x\n"
				"\tReference count: %i\n"
				"\tAllocation flags: %#x\n"
				"\t$ settings: %#x\n"
				"\tDefault access: %#x\n"
				"\tPhysical address: %#x\n"
				"\tKernel virtual address: %#x\n"
				"\tCreator: %s\n"
				"\tCreator thread group id: %u\n",
			(unsigned int)alloc, alloc->size, alloc->mem_type->id,
			alloc->name, atomic_read(&alloc->ref_cnt),
			alloc->flags, alloc->cach_buf.cache_settings,
			alloc->default_access, alloc->paddr,
			(unsigned int)alloc->kaddr, creator,
			alloc->creator_tgid);
		if (ret < 0)
			return -ENOMSG;
		else if (ret + 1 > buf_size)
			return -EINVAL;
	}

	*buf += ret;

	return 0;
}

static int debugfs_allocs_read(struct file *file, char __user *buf,
						size_t count, loff_t *f_pos)
{
	/*
	 * We assume the supplied buffer and PAGE_SIZE is large enough to hold
	 * information about at least one alloc, if not no data will be
	 * returned.
	 */

	int ret;
	size_t i = 0;
	struct hwmem_alloc *curr_alloc;
	char *local_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	char *local_buf_pos = local_buf;
	size_t available_space = min((size_t)PAGE_SIZE, count);
	/* private_data is intialized to NULL in open which I assume is 0. */
	void **curr_pos = &file->private_data;
	size_t bytes_read;

	if (local_buf == NULL)
		return -ENOMEM;

	mutex_lock(&lock);

	list_for_each_entry(curr_alloc, &alloc_list, list) {
		if (i++ < (size_t)*curr_pos)
			continue;

		ret = print_alloc(curr_alloc, &local_buf_pos, available_space -
					(size_t)(local_buf_pos - local_buf));
		if (ret == -EINVAL) /* No more room */
			break;
		else if (ret < 0)
			goto out;

		*curr_pos = (void *)i;
	}

	bytes_read = (size_t)(local_buf_pos - local_buf);

	ret = copy_to_user(buf, local_buf, bytes_read);
	if (ret < 0)
		goto out;

	ret = bytes_read;

out:
	kfree(local_buf);

	mutex_unlock(&lock);

	return ret;
}

static void init_debugfs(void)
{
	/* Hwmem is never unloaded so dropping the dentrys is ok. */
	struct dentry *debugfs_root_dir = debugfs_create_dir("hwmem", NULL);
	(void)debugfs_create_file("allocs", 0444, debugfs_root_dir, 0,
							&debugfs_allocs_fops);
}

#endif /* #ifdef CONFIG_DEBUG_FS */

/* Module */

extern int hwmem_ioctl_init(void);

static int __devinit hwmem_probe(struct platform_device *pdev)
{
	int ret;

	if (hwdev) {
		dev_err(&pdev->dev, "Probed multiple times\n");
		return -EINVAL;
	}

	hwdev = pdev;

	/*
	 * No need to flush the caches here. If we can keep track of the cache
	 * content then none of our memory will be in the caches, if we can't
	 * keep track of the cache content we always assume all our memory is
	 * in the caches.
	 */

	ret = hwmem_ioctl_init();
	if (ret < 0)
		dev_warn(&pdev->dev, "Failed to start hwmem-ioctl, continuing"
								" anyway\n");

#ifdef CONFIG_DEBUG_FS
	init_debugfs();
#endif

	dev_info(&pdev->dev, "Probed OK\n");

	return 0;
}

static struct platform_driver hwmem_driver = {
	.probe	= hwmem_probe,
	.driver = {
		.name	= "hwmem",
	},
};

static int __init hwmem_init(void)
{
	return platform_driver_register(&hwmem_driver);
}
subsys_initcall(hwmem_init);

MODULE_AUTHOR("Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hardware memory driver");

