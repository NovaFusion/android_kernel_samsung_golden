/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Contiguous memory allocator
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>,
 * Johan Mossberg <johan.xx.mossberg@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/pasr.h>
#include <asm/sizes.h>

#define MAX_INSTANCE_NAME_LENGTH 31

struct alloc {
	struct list_head list;

	bool in_use;
	phys_addr_t paddr;
	size_t size;
};

struct instance {
	struct list_head list;

	char name[MAX_INSTANCE_NAME_LENGTH + 1];

	phys_addr_t region_paddr;
	void *region_kaddr;
	size_t region_size;

	struct list_head alloc_list;

#ifdef CONFIG_DEBUG_FS
	struct inode *debugfs_inode;
	int cona_status_free;
	int cona_status_used;
	int cona_status_max_cont;
	int cona_status_max_check;
	int cona_status_biggest_free;
	int cona_status_printed;
#endif /* #ifdef CONFIG_DEBUG_FS */
};

static LIST_HEAD(instance_list);

static DEFINE_MUTEX(lock);

void *cona_create(const char *name, phys_addr_t region_paddr,
							size_t region_size);
void *cona_alloc(void *instance, size_t size);
void cona_free(void *instance, void *alloc);
phys_addr_t cona_get_alloc_paddr(void *alloc);
void *cona_get_alloc_kaddr(void *instance, void *alloc);
size_t cona_get_alloc_size(void *alloc);

static int init_alloc_list(struct instance *instance);
static void clean_alloc_list(struct instance *instance);
static struct alloc *find_free_alloc_bestfit(struct instance *instance,
								size_t size);
static struct alloc *split_allocation(struct alloc *alloc,
							size_t new_alloc_size);
static phys_addr_t get_alloc_offset(struct instance *instance,
							struct alloc *alloc);

void *cona_create(const char *name, phys_addr_t region_paddr,
							size_t region_size)
{
	int ret;
	struct instance *instance;
	struct vm_struct *vm_area = NULL;
#ifdef CONFIG_FLATMEM
	phys_addr_t region_end = region_paddr + region_size;
#endif

	if (region_size == 0)
		return ERR_PTR(-EINVAL);

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (instance == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(instance->name, name, MAX_INSTANCE_NAME_LENGTH + 1);
	/* Truncate name if necessary */
	instance->name[MAX_INSTANCE_NAME_LENGTH] = '\0';
	instance->region_paddr = region_paddr;
	instance->region_size = region_size;

#ifdef CONFIG_FLATMEM
	/*
	 * Map hwmem physical memory to the holes in the kernel LOMEM virtual
	 * address if LOMEM region is enough to contain the whole HWMEM.
	 * otherwise map hwmem to VMALLOC region.
	 */
	if (region_end > region_paddr
	    && region_end < virt_to_phys(high_memory)) {
		instance->region_kaddr = phys_to_virt(region_paddr);
		pr_info("hwmem: %s map to LOMEM, start: 0x%p, end: 0x%p\n",
			name,
			phys_to_virt(region_paddr),
			phys_to_virt(region_paddr+region_size));
	}
#endif

	if (!instance->region_kaddr) {
		vm_area = get_vm_area(region_size, VM_IOREMAP);
		if (vm_area == NULL) {
			pr_err("CONA: Failed to allocate %u bytes kernel virtual memory",
			       region_size);
			ret = -ENOMSG;
			goto vmem_alloc_failed;
		}

		instance->region_kaddr = vm_area->addr;
		pr_info("hwmem: %s map to VMALLOC, address: 0x%p\n",
			name,
			instance->region_kaddr);
	}

	/*
	 * This newly created memory area is unsused.
	 * Notify the PASR framework it does not need to be refreshed.
	 */
	pasr_put(instance->region_paddr, instance->region_size);

	INIT_LIST_HEAD(&instance->alloc_list);
	ret = init_alloc_list(instance);
	if (ret < 0)
		goto init_alloc_list_failed;

	mutex_lock(&lock);
	list_add_tail(&instance->list, &instance_list);
	mutex_unlock(&lock);

	return instance;

init_alloc_list_failed:
	if (vm_area) {
		vm_area = remove_vm_area(instance->region_kaddr);
		if (vm_area == NULL)
			pr_err("CONA: Failed to free kernel virtual memory, resource leak!\n");
	}

	kfree(vm_area);
vmem_alloc_failed:
	kfree(instance);

	return ERR_PTR(ret);
}

void *cona_alloc(void *instance, size_t size)
{
	struct instance *instance_l = (struct instance *)instance;
	struct alloc *alloc;

	if (size == 0)
		return ERR_PTR(-EINVAL);

	mutex_lock(&lock);

	alloc = find_free_alloc_bestfit(instance_l, size);
	if (IS_ERR(alloc))
		goto out;
	if (size < alloc->size) {
		alloc = split_allocation(alloc, size);
		if (IS_ERR(alloc))
			goto out;
	} else {
		alloc->in_use = true;
	}

	pasr_get(alloc->paddr, alloc->size);

#ifdef CONFIG_DEBUG_FS
	instance_l->cona_status_max_cont += alloc->size;
	instance_l->cona_status_max_check =
					max(instance_l->cona_status_max_check,
					instance_l->cona_status_max_cont);
#endif /* #ifdef CONFIG_DEBUG_FS */

out:
	mutex_unlock(&lock);

	return alloc;
}

void cona_free(void *instance, void *alloc)
{
	struct instance *instance_l = (struct instance *)instance;
	struct alloc *alloc_l = (struct alloc *)alloc;
	struct alloc *other;

	mutex_lock(&lock);

	alloc_l->in_use = false;

	pasr_put(alloc_l->paddr, alloc_l->size);

#ifdef CONFIG_DEBUG_FS
	instance_l->cona_status_max_cont -= alloc_l->size;
#endif /* #ifdef CONFIG_DEBUG_FS */

	other = list_entry(alloc_l->list.prev, struct alloc, list);
	if ((alloc_l->list.prev != &instance_l->alloc_list) &&
							!other->in_use) {
		other->size += alloc_l->size;
		list_del(&alloc_l->list);
		kfree(alloc_l);
		alloc_l = other;
	}
	other = list_entry(alloc_l->list.next, struct alloc, list);
	if ((alloc_l->list.next != &instance_l->alloc_list) &&
							!other->in_use) {
		alloc_l->size += other->size;
		list_del(&other->list);
		kfree(other);
	}

	mutex_unlock(&lock);
}

phys_addr_t cona_get_alloc_paddr(void *alloc)
{
	return ((struct alloc *)alloc)->paddr;
}

void *cona_get_alloc_kaddr(void *instance, void *alloc)
{
	struct instance *instance_l = (struct instance *)instance;

	return instance_l->region_kaddr + get_alloc_offset(instance_l,
							(struct alloc *)alloc);
}

size_t cona_get_alloc_size(void *alloc)
{
	return ((struct alloc *)alloc)->size;
}

static int init_alloc_list(struct instance *instance)
{
	/*
	 * Hack to not get any allocs that cross a 64MiB boundary as B2R2 can't
	 * handle that.
	 */
	int ret;
	u32 curr_pos = instance->region_paddr;
	u32 region_end = instance->region_paddr + instance->region_size;
	u32 next_64mib_boundary = (curr_pos + SZ_64M) & ~(SZ_64M - 1);
	struct alloc *alloc;

	if (PAGE_SIZE >= SZ_64M) {
		printk(KERN_WARNING "CONA: PAGE_SIZE >= 64MiB\n");
		return -ENOMSG;
	}

	while (next_64mib_boundary < region_end) {
		if (next_64mib_boundary - curr_pos > PAGE_SIZE) {
			alloc = kzalloc(sizeof(struct alloc), GFP_KERNEL);
			if (alloc == NULL) {
				ret = -ENOMEM;
				goto error;
			}
			alloc->paddr = curr_pos;
			alloc->size = next_64mib_boundary - curr_pos -
								PAGE_SIZE;
			alloc->in_use = false;
			list_add_tail(&alloc->list, &instance->alloc_list);
			curr_pos = alloc->paddr + alloc->size;
		}

		alloc = kzalloc(sizeof(struct alloc), GFP_KERNEL);
		if (alloc == NULL) {
			ret = -ENOMEM;
			goto error;
		}
		alloc->paddr = curr_pos;
		alloc->size = PAGE_SIZE;
		alloc->in_use = true;
		list_add_tail(&alloc->list, &instance->alloc_list);
		curr_pos = alloc->paddr + alloc->size;

#ifdef CONFIG_DEBUG_FS
		instance->cona_status_max_cont += alloc->size;
#endif /* #ifdef CONFIG_DEBUG_FS */

		next_64mib_boundary += SZ_64M;
	}

	alloc = kzalloc(sizeof(struct alloc), GFP_KERNEL);
	if (alloc == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	alloc->paddr = curr_pos;
	alloc->size = region_end - curr_pos;
	alloc->in_use = false;
	list_add_tail(&alloc->list, &instance->alloc_list);

	return 0;

error:
	clean_alloc_list(instance);

	return ret;
}

static void clean_alloc_list(struct instance *instance)
{
	while (list_empty(&instance->alloc_list) == 0) {
		struct alloc *i = list_first_entry(&instance->alloc_list,
							struct alloc, list);

		list_del(&i->list);

		kfree(i);
	}
}

static struct alloc *find_free_alloc_bestfit(struct instance *instance,
								size_t size)
{
	size_t best_diff = ~(size_t)0;
	struct alloc *alloc = NULL, *i;

	list_for_each_entry(i, &instance->alloc_list, list) {
		size_t diff = i->size - size;
		if (i->in_use || i->size < size)
			continue;
		if (diff < best_diff) {
			alloc = i;
			best_diff = diff;
		}
	}

	return alloc != NULL ? alloc : ERR_PTR(-ENOMEM);
}

static struct alloc *split_allocation(struct alloc *alloc,
							size_t new_alloc_size)
{
	struct alloc *new_alloc;

	new_alloc = kzalloc(sizeof(struct alloc), GFP_KERNEL);
	if (new_alloc == NULL)
		return ERR_PTR(-ENOMEM);

	new_alloc->in_use = true;
	new_alloc->paddr = alloc->paddr;
	new_alloc->size = new_alloc_size;
	alloc->size -= new_alloc_size;
	alloc->paddr += new_alloc_size;

	list_add_tail(&new_alloc->list, &alloc->list);

	return new_alloc;
}

static phys_addr_t get_alloc_offset(struct instance *instance,
							struct alloc *alloc)
{
	return alloc->paddr - instance->region_paddr;
}

/* Debug */

#ifdef CONFIG_DEBUG_FS

static int print_alloc(struct instance *instance, struct alloc *alloc,
						char **buf, size_t buf_size);
static int print_alloc_status(struct instance *instance, char **buf,
						size_t buf_size);
static struct instance *get_instance_from_file(struct file *file);
static int debugfs_allocs_read(struct file *filp, char __user *buf,
						size_t count, loff_t *f_pos);

static const struct file_operations debugfs_allocs_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_allocs_read,
};

static int print_alloc(struct instance *instance, struct alloc *alloc,
						char **buf, size_t buf_size)
{
	int ret;
	int i;

	for (i = 0; i < 2; i++) {
		size_t buf_size_l;
		if (i == 0)
			buf_size_l = 0;
		else
			buf_size_l = buf_size;

		if (i == 1) {
			if (alloc->in_use)
				instance->cona_status_used += alloc->size;
			else
				instance->cona_status_free += alloc->size;
		}

		if (!alloc->in_use) {
			instance->cona_status_biggest_free =
				max((size_t)alloc->size,
				(size_t)instance->cona_status_biggest_free);
		}

		ret = snprintf(*buf, buf_size_l, "paddr: %10x\tsize: %10u\t"
				"in use: %1u\t used: %10u (%dMB)"
				" \t free: %10u (%dMB)\n",
				alloc->paddr,
				alloc->size,
				alloc->in_use,
				instance->cona_status_used,
				instance->cona_status_used/1024/1024,
				instance->cona_status_free,
				instance->cona_status_free/1024/1024);

		if (ret < 0)
			return -ENOMSG;
		else if (ret + 1 > buf_size)
			return -EINVAL;
	}

	*buf += ret;

	return 0;
}

static int print_alloc_status(struct instance *instance, char **buf,
							size_t buf_size)
{
	int ret;
	int i;

	for (i = 0; i < 2; i++) {
		size_t buf_size_l;
		if (i == 0)
			buf_size_l = 0;
		else
			buf_size_l = buf_size;

		ret = snprintf(*buf, buf_size_l, "Overall peak usage:\t%10u "
				"(%dMB)\nCurrent max usage:\t%10u (%dMB)\n"
				"Current biggest free:\t%10d (%dMB)\n",
				instance->cona_status_max_check,
				instance->cona_status_max_check/1024/1024,
				instance->cona_status_max_cont,
				instance->cona_status_max_cont/1024/1024,
				instance->cona_status_biggest_free,
				instance->cona_status_biggest_free/1024/1024);

		if (ret < 0)
			return -ENOMSG;
		else if (ret + 1 > buf_size)
			return -EINVAL;
	}

	*buf += ret;

	return 0;
}

static struct instance *get_instance_from_file(struct file *file)
{
	struct instance *curr_instance;

	list_for_each_entry(curr_instance, &instance_list, list) {
		if (file->f_dentry->d_inode == curr_instance->debugfs_inode)
			return curr_instance;
	}

	return ERR_PTR(-ENOENT);
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
	struct instance *instance;
	struct alloc *curr_alloc;
	char *local_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	char *local_buf_pos = local_buf;
	size_t available_space = min((size_t)PAGE_SIZE, count);
	/* private_data is intialized to NULL in open which I assume is 0. */
	void **curr_pos = &file->private_data;
	size_t bytes_read;
	bool readout_aborted = false;

	if (local_buf == NULL)
		return -ENOMEM;

	mutex_lock(&lock);
	instance = get_instance_from_file(file);
	if (IS_ERR(instance)) {
		ret = PTR_ERR(instance);
		goto out;
	}

	list_for_each_entry(curr_alloc, &instance->alloc_list, list) {
		phys_addr_t alloc_offset = get_alloc_offset(instance,
								curr_alloc);
		if (alloc_offset < (phys_addr_t)*curr_pos)
			continue;

		ret = print_alloc(instance, curr_alloc, &local_buf_pos,
				available_space - (size_t)(local_buf_pos -
				local_buf));

		if (ret == -EINVAL) { /* No more room */
			readout_aborted = true;
			break;
		} else if (ret < 0) {
			goto out;
		}
		/*
		 * There could be an overflow issue here in the unlikely case
		 * where the region is placed at the end of the address range
		 * and the last alloc is 1 byte large. Since this is debug code
		 * and that case most likely never will happen I've chosen to
		 * defer fixing it till it happens.
		 */
		*curr_pos = (void *)(alloc_offset + 1);

		/* Make sure to also print status if there were any prints */
		instance->cona_status_printed = false;
	}

	if (!readout_aborted && !instance->cona_status_printed) {
		ret = print_alloc_status(instance, &local_buf_pos,
					available_space -
					(size_t)(local_buf_pos - local_buf));

		if (ret == -EINVAL) /* No more room */
			readout_aborted = true;
		else if (ret < 0)
			goto out;
		else
			instance->cona_status_printed = true;
	}

	if (!readout_aborted) {
		instance->cona_status_free = 0;
		instance->cona_status_used = 0;
		instance->cona_status_biggest_free = 0;
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

static int __init init_debugfs(void)
{
	struct instance *curr_instance;
	struct dentry *debugfs_root_dir = debugfs_create_dir("cona", NULL);

	mutex_lock(&lock);

	list_for_each_entry(curr_instance, &instance_list, list) {
		struct dentry *file_dentry;
		char tmp_str[MAX_INSTANCE_NAME_LENGTH + 7 + 1];
		tmp_str[0] = '\0';
		strcat(tmp_str, curr_instance->name);
		strcat(tmp_str, "_allocs");
		file_dentry = debugfs_create_file(tmp_str, 0444,
				debugfs_root_dir, 0, &debugfs_allocs_fops);
		if (file_dentry != NULL)
			curr_instance->debugfs_inode = file_dentry->d_inode;
	}

	mutex_unlock(&lock);

	return 0;
}
/*
 * Must be executed after all instances have been created, hence the
 * late_initcall.
 */
late_initcall(init_debugfs);

#endif /* #ifdef CONFIG_DEBUG_FS */
