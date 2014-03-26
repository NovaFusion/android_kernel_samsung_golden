/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Hardware memory driver, hwmem
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mm_types.h>
#include <linux/hwmem.h>
#include <linux/device.h>
#include <linux/sched.h>

static int hwmem_open(struct inode *inode, struct file *file);
static int hwmem_ioctl_mmap(struct file *file, struct vm_area_struct *vma);
static int hwmem_release_fop(struct inode *inode, struct file *file);
static long hwmem_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg);
static unsigned long hwmem_get_unmapped_area(struct file *file,
	unsigned long addr, unsigned long len, unsigned long pgoff,
	unsigned long flags);

static const struct file_operations hwmem_fops = {
	.open = hwmem_open,
	.mmap = hwmem_ioctl_mmap,
	.unlocked_ioctl = hwmem_ioctl,
	.release = hwmem_release_fop,
	.get_unmapped_area = hwmem_get_unmapped_area,
};

static struct miscdevice hwmem_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "hwmem",
	.fops = &hwmem_fops,
};

struct hwmem_file {
	struct mutex lock;
	struct idr idr; /* id -> struct hwmem_alloc*, ref counted */
	struct hwmem_alloc *fd_alloc; /* Ref counted */
};

static s32 create_id(struct hwmem_file *hwfile, struct hwmem_alloc *alloc)
{
	int id, ret;

	while (true) {
		if (idr_pre_get(&hwfile->idr, GFP_KERNEL) == 0)
			return -ENOMEM;

		ret = idr_get_new_above(&hwfile->idr, alloc, 1, &id);
		if (ret == 0)
			break;
		else if (ret != -EAGAIN)
			return -ENOMEM;
	}

	/*
	 * IDR always returns the lowest free id so there is no wrapping issue
	 * because of this.
	 */
	if (id >= (s32)1 << (31 - PAGE_SHIFT)) {
		dev_err(hwmem_device.this_device, "Out of IDs!\n");
		idr_remove(&hwfile->idr, id);
		return -ENOMSG;
	}

	return (s32)id << PAGE_SHIFT;
}

static void remove_id(struct hwmem_file *hwfile, s32 id)
{
	idr_remove(&hwfile->idr, id >> PAGE_SHIFT);
}

static struct hwmem_alloc *resolve_id(struct hwmem_file *hwfile, s32 id)
{
	struct hwmem_alloc *alloc;

	alloc = id ? idr_find(&hwfile->idr, id >> PAGE_SHIFT) :
							hwfile->fd_alloc;
	if (alloc == NULL)
		alloc = ERR_PTR(-EINVAL);

	return alloc;
}

static s32 alloc(struct hwmem_file *hwfile, struct hwmem_alloc_request *req)
{
	s32 ret = 0;
	struct hwmem_alloc *alloc;

	alloc = hwmem_alloc(req->size, req->flags, req->default_access,
								req->mem_type);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	ret = create_id(hwfile, alloc);
	if (ret < 0)
		hwmem_release(alloc);

	return ret;
}

static int alloc_fd(struct hwmem_file *hwfile, struct hwmem_alloc_request *req)
{
	struct hwmem_alloc *alloc;

	if (hwfile->fd_alloc)
		return -EINVAL;

	alloc = hwmem_alloc(req->size, req->flags, req->default_access,
								req->mem_type);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	hwfile->fd_alloc = alloc;

	return 0;
}

static int release(struct hwmem_file *hwfile, s32 id)
{
	struct hwmem_alloc *alloc;

	if (id == 0)
		return -EINVAL;

	alloc = resolve_id(hwfile, id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	remove_id(hwfile, id);
	hwmem_release(alloc);

	return 0;
}

static int set_cpu_domain(struct hwmem_file *hwfile,
					struct hwmem_set_domain_request *req)
{
	struct hwmem_alloc *alloc;

	alloc = resolve_id(hwfile, req->id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	return hwmem_set_domain(alloc, req->access, HWMEM_DOMAIN_CPU,
					(struct hwmem_region *)&req->region);
}

static int set_sync_domain(struct hwmem_file *hwfile,
					struct hwmem_set_domain_request *req)
{
	struct hwmem_alloc *alloc;

	alloc = resolve_id(hwfile, req->id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	return hwmem_set_domain(alloc, req->access, HWMEM_DOMAIN_SYNC,
					(struct hwmem_region *)&req->region);
}

static int pin(struct hwmem_file *hwfile, struct hwmem_pin_request *req)
{
	int ret;
	struct hwmem_alloc *alloc;
	enum hwmem_mem_type mem_type;
	struct hwmem_mem_chunk mem_chunk;
	size_t mem_chunk_length = 1;

	alloc = resolve_id(hwfile, req->id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	hwmem_get_info(alloc, NULL, &mem_type, NULL);

	ret = hwmem_pin(alloc, &mem_chunk, &mem_chunk_length);
	if (ret < 0)
		return ret;

	req->phys_addr = mem_chunk.paddr;

	return 0;
}

static int unpin(struct hwmem_file *hwfile, s32 id)
{
	struct hwmem_alloc *alloc;

	alloc = resolve_id(hwfile, id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	hwmem_unpin(alloc);

	return 0;
}

static int set_access(struct hwmem_file *hwfile,
		struct hwmem_set_access_request *req)
{
	struct hwmem_alloc *alloc;

	alloc = resolve_id(hwfile, req->id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	return hwmem_set_access(alloc, req->access, req->pid);
}

static int get_info(struct hwmem_file *hwfile,
		struct hwmem_get_info_request *req)
{
	struct hwmem_alloc *alloc;

	alloc = resolve_id(hwfile, req->id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	hwmem_get_info(alloc, &req->size, &req->mem_type, &req->access);

	return 0;
}

static s32 export(struct hwmem_file *hwfile, s32 id)
{
	s32 ret;
	struct hwmem_alloc *alloc;
	enum hwmem_access access;

	alloc = resolve_id(hwfile, id);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	/*
	 * The user could be about to send the buffer to a driver but
	 * there is a chance the current thread group don't have import rights
	 * if it gained access to the buffer via a inter-process fd transfer
	 * (fork, Android binder), if this is the case the driver will not be
	 * able to resolve the buffer name. To avoid this situation we give the
	 * current thread group import rights. This will not breach the
	 * security as the process already has access to the buffer (otherwise
	 * it would not be able to get here).
	 */
	hwmem_get_info(alloc, NULL, NULL, &access);

	ret = hwmem_set_access(alloc, (access | HWMEM_ACCESS_IMPORT),
							task_tgid_nr(current));
	if (ret < 0)
		return ret;

	return hwmem_get_name(alloc);
}

static s32 import(struct hwmem_file *hwfile, s32 name)
{
	s32 ret = 0;
	struct hwmem_alloc *alloc;
	enum hwmem_access access;

	alloc = hwmem_resolve_by_name(name);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	/* Check access permissions for process */
	hwmem_get_info(alloc, NULL, NULL, &access);
	if (!(access & HWMEM_ACCESS_IMPORT)) {
		ret = -EPERM;
		goto error;
	}

	ret = create_id(hwfile, alloc);
	if (ret < 0)
		goto error;

	return ret;

error:
	hwmem_release(alloc);

	return ret;
}

static int import_fd(struct hwmem_file *hwfile, s32 name)
{
	int ret;
	struct hwmem_alloc *alloc;
	enum hwmem_access access;

	if (hwfile->fd_alloc)
		return -EINVAL;

	alloc = hwmem_resolve_by_name(name);
	if (IS_ERR(alloc))
		return PTR_ERR(alloc);

	/* Check access permissions for process */
	hwmem_get_info(alloc, NULL, NULL, &access);
	if (!(access & HWMEM_ACCESS_IMPORT)) {
		ret = -EPERM;
		goto error;
	}

	hwfile->fd_alloc = alloc;

	return 0;

error:
	hwmem_release(alloc);

	return ret;
}

static int hwmem_open(struct inode *inode, struct file *file)
{
	struct hwmem_file *hwfile;

	hwfile = kzalloc(sizeof(struct hwmem_file), GFP_KERNEL);
	if (hwfile == NULL)
		return -ENOMEM;

	idr_init(&hwfile->idr);
	mutex_init(&hwfile->lock);
	file->private_data = hwfile;

	return 0;
}

static int hwmem_ioctl_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct hwmem_file *hwfile = (struct hwmem_file *)file->private_data;
	struct hwmem_alloc *alloc;

	mutex_lock(&hwfile->lock);

	alloc = resolve_id(hwfile, (s32)vma->vm_pgoff << PAGE_SHIFT);
	if (IS_ERR(alloc)) {
		ret = PTR_ERR(alloc);
		goto out;
	}

	ret = hwmem_mmap(alloc, vma);

out:
	mutex_unlock(&hwfile->lock);

	return ret;
}

static int hwmem_release_idr_for_each_wrapper(int id, void *ptr, void *data)
{
	hwmem_release((struct hwmem_alloc *)ptr);

	return 0;
}

static int hwmem_release_fop(struct inode *inode, struct file *file)
{
	struct hwmem_file *hwfile = (struct hwmem_file *)file->private_data;

	idr_for_each(&hwfile->idr, hwmem_release_idr_for_each_wrapper, NULL);
	idr_remove_all(&hwfile->idr);
	idr_destroy(&hwfile->idr);

	if (hwfile->fd_alloc)
		hwmem_release(hwfile->fd_alloc);

	mutex_destroy(&hwfile->lock);

	kfree(hwfile);

	return 0;
}

static long hwmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOSYS;
	struct hwmem_file *hwfile = (struct hwmem_file *)file->private_data;

	mutex_lock(&hwfile->lock);

	switch (cmd) {
	case HWMEM_ALLOC_IOC:
		{
			struct hwmem_alloc_request req;
			if (copy_from_user(&req, (void __user *)arg,
					sizeof(struct hwmem_alloc_request)))
				ret = -EFAULT;
			else
				ret = alloc(hwfile, &req);
		}
		break;
	case HWMEM_ALLOC_FD_IOC:
		{
			struct hwmem_alloc_request req;
			if (copy_from_user(&req, (void __user *)arg,
					sizeof(struct hwmem_alloc_request)))
				ret = -EFAULT;
			else
				ret = alloc_fd(hwfile, &req);
		}
		break;
	case HWMEM_RELEASE_IOC:
		ret = release(hwfile, (s32)arg);
		break;
	case HWMEM_SET_CPU_DOMAIN_IOC:
		{
			struct hwmem_set_domain_request req;
			if (copy_from_user(&req, (void __user *)arg,
				sizeof(struct hwmem_set_domain_request)))
				ret = -EFAULT;
			else
				ret = set_cpu_domain(hwfile, &req);
		}
		break;
	case HWMEM_SET_SYNC_DOMAIN_IOC:
		{
			struct hwmem_set_domain_request req;
			if (copy_from_user(&req, (void __user *)arg,
				sizeof(struct hwmem_set_domain_request)))
				ret = -EFAULT;
			else
				ret = set_sync_domain(hwfile, &req);
		}
		break;
	case HWMEM_PIN_IOC:
		{
			struct hwmem_pin_request req;
			if (copy_from_user(&req, (void __user *)arg,
				sizeof(struct hwmem_pin_request)))
				ret = -EFAULT;
			else
				ret = pin(hwfile, &req);
			if (ret == 0 && copy_to_user((void __user *)arg, &req,
					sizeof(struct hwmem_pin_request)))
				ret = -EFAULT;
		}
		break;
	case HWMEM_UNPIN_IOC:
		ret = unpin(hwfile, (s32)arg);
		break;
	case HWMEM_SET_ACCESS_IOC:
		{
			struct hwmem_set_access_request req;
			if (copy_from_user(&req, (void __user *)arg,
				sizeof(struct hwmem_set_access_request)))
				ret = -EFAULT;
			else
				ret = set_access(hwfile, &req);
		}
		break;
	case HWMEM_GET_INFO_IOC:
		{
			struct hwmem_get_info_request req;
			if (copy_from_user(&req, (void __user *)arg,
				sizeof(struct hwmem_get_info_request)))
				ret = -EFAULT;
			else
				ret = get_info(hwfile, &req);
			if (ret == 0 && copy_to_user((void __user *)arg, &req,
					sizeof(struct hwmem_get_info_request)))
				ret = -EFAULT;
		}
		break;
	case HWMEM_EXPORT_IOC:
		ret = export(hwfile, (s32)arg);
		break;
	case HWMEM_IMPORT_IOC:
		ret = import(hwfile, (s32)arg);
		break;
	case HWMEM_IMPORT_FD_IOC:
		ret = import_fd(hwfile, (s32)arg);
		break;
	}

	mutex_unlock(&hwfile->lock);

	return ret;
}

static unsigned long hwmem_get_unmapped_area(struct file *file,
	unsigned long addr, unsigned long len, unsigned long pgoff,
	unsigned long flags)
{
	/*
	 * pgoff will not be valid as it contains a buffer id (right shifted
	 * PAGE_SHIFT bits). To not confuse get_unmapped_area we'll not pass
	 * on file or pgoff.
	 */
	return current->mm->get_unmapped_area(NULL, addr, len, 0, flags);
}

int __devinit hwmem_ioctl_init(void)
{
	if (PAGE_SHIFT < 1 || PAGE_SHIFT > 30 || sizeof(size_t) != 4 ||
		sizeof(int) > 4 || sizeof(enum hwmem_alloc_flags) != 4 ||
					sizeof(enum hwmem_access) != 4 ||
					 sizeof(enum hwmem_mem_type) != 4) {
		dev_err(hwmem_device.this_device, "PAGE_SHIFT < 1 || PAGE_SHIFT"
			" > 30 || sizeof(size_t) != 4 || sizeof(int) > 4 ||"
			" sizeof(enum hwmem_alloc_flags) != 4 || sizeof(enum"
			" hwmem_access) != 4 || sizeof(enum hwmem_mem_type)"
								" != 4\n");
		return -ENOMSG;
	}
	if (PAGE_SHIFT > 15)
		dev_warn(hwmem_device.this_device, "Due to the page size only"
				" %u id:s per file instance are available\n",
					((u32)1 << (31 - PAGE_SHIFT)) - 1);

	return misc_register(&hwmem_device);
}

void __exit hwmem_ioctl_exit(void)
{
	misc_deregister(&hwmem_device);
}
