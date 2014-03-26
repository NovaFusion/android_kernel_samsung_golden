/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson MCDE DRM/KMS driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifdef CONFIG_DMA_BUF
	#include <linux/dma-buf.h>
	#include <linux/scatterlist.h>
#endif

#include "nova_drm_priv.h"

static void nova_gem_free_object(struct drm_gem_object *bo);

#define to_nova_bo(x) container_of(x, struct nova_gem_object, base)

struct nova_gem_object {
	struct drm_gem_object base;
	struct drm_local_map map;
	void *vaddr;
	dma_addr_t paddr;
	uint32_t size;
};

static int
nova_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int ret;
	struct nova_gem_object *obj = to_nova_bo(vma->vm_private_data);
	size_t vma_size = vma->vm_end - vma->vm_start;

	ret = remap_pfn_range(vma, vma->vm_start, PFN_DOWN(obj->paddr),
			min(vma_size, obj->base.size), vma->vm_page_prot);

	switch (ret) {
	case 0:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	case -EINVAL:
	default:
		return VM_FAULT_SIGBUS;
	}
}

static struct vm_operations_struct nova_gem_vm_ops = {
	.fault = nova_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

struct drm_gem_object *
nova_gem_create_object(struct drm_device *ddev, u32 size)
{
	int ret;
	struct nova_gem_object *nbo;

	if (!size)
		return ERR_PTR(-EINVAL);

	nbo = kzalloc(sizeof(*nbo), GFP_KERNEL);
	if (nbo == NULL)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_private_object_init(ddev, &nbo->base, size);
	if (ret)
		goto gem_init;

	nbo->vaddr = dma_alloc_coherent(ddev->dev, size, &nbo->paddr,
								GFP_KERNEL);
	if (!nbo->vaddr) {
		DRM_DEBUG_DRIVER("Failed to allocate gem memory "
						"(size=%u)\n", size);
		ret = -ENOMEM;
		goto no_dma;
	}
	nbo->size = size;
	DRM_DEBUG_DRIVER("GEM alloc: size=0x%.8x, paddr=0x%.8x\n", size,
								nbo->paddr);

	return &nbo->base;
no_dma:
	drm_gem_object_release(&nbo->base);
gem_init:
	kfree(nbo);
	return ERR_PTR(ret);
}

static int
nova_dumb_create(struct drm_file *file,
		     struct drm_device *ddev,
		     struct drm_mode_create_dumb *args)
{
	int ret;
	struct drm_gem_object *bo;

	args->pitch = ALIGN(args->width * ((args->bpp + 7) / 8), 64);
	args->size = PAGE_ALIGN(args->pitch * args->height);

	if (args->size <= 0)
		return -EINVAL;

	bo = nova_gem_create_object(ddev, args->size);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = drm_gem_handle_create(file, bo, &args->handle);
	if (ret)
		goto no_handle;

	drm_gem_object_unreference_unlocked(bo);
	return 0;
no_handle:
	drm_gem_object_unreference_unlocked(bo);
	args->handle = 0;
	args->pitch = 0;
	args->size = 0;
	return ret;
}

static int
nova_dumb_map_offset(struct drm_file *file,
			 struct drm_device *dev,
			 uint32_t handle,
			 uint64_t *offset)
{
	int ret;
	struct nova_gem_object *nbo;
	struct drm_map_list *ml;
	struct drm_gem_mm *mm = dev->mm_private;

	nbo = to_nova_bo(drm_gem_object_lookup(dev, file, handle));
	if (nbo == NULL)
		return -ENOENT;

	ml = &nbo->base.map_list;
	if (!ml->map) {
		struct drm_local_map *map = ml->map = &nbo->map;
		map->type = _DRM_GEM;
		map->size = nbo->base.size;
		map->handle = nbo;
	}
	ml->hash.key = nbo->paddr;
	ret = drm_ht_insert_item(&mm->offset_hash, &ml->hash);
	if (ret) {
		ml->map = NULL;
		drm_gem_object_unreference(&nbo->base);
		return ret;
	}
	*offset = (u64)nbo->paddr << PAGE_SHIFT;
	drm_gem_object_unreference(&nbo->base);

	return 0;
}

static int
nova_dumb_destroy(struct drm_file *file,
		      struct drm_device *dev,
		      uint32_t handle)
{
	return drm_gem_handle_delete(file, handle);
}

static int
nova_gem_init_object(struct drm_gem_object *obj)
{
	/* Don't allow SHMFS backed gem objects */
	BUG_ON(1);
	return 0;
}

static void
nova_gem_free_object(struct drm_gem_object *bo)
{
	struct drm_device *drmdev = bo->dev;
	struct drm_gem_mm *mm = drmdev->mm_private;
	struct nova_gem_object *mbo = to_nova_bo(bo);

	if (bo->map_list.map)
		drm_ht_remove_item(&mm->offset_hash, &bo->map_list.hash);
	drm_gem_object_release(bo);

	if (mbo->vaddr)
		dma_free_coherent(bo->dev->dev, bo->size, mbo->vaddr,
								mbo->paddr);
	kfree(mbo);
}

phys_addr_t
nova_gem_get_paddr(struct drm_gem_object *bo)
{
	struct nova_gem_object *nbo = to_nova_bo(bo);

	return nbo->paddr;
}

void *
nova_gem_get_vaddr(struct drm_gem_object *bo)
{
	struct nova_gem_object *nbo = to_nova_bo(bo);

	return nbo->vaddr;
}

#ifdef CONFIG_DMA_BUF
static struct sg_table *
nova_map_dma_buf(struct dma_buf_attachment *attachment,
						enum dma_data_direction dir)
{
	struct drm_gem_object *bo = attachment->dmabuf->priv;
	struct sg_table *sg;
	phys_addr_t paddr;
	int ret;

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sg, 1, GFP_KERNEL);
	if (ret)
		goto fail_sg;

	paddr = nova_gem_get_paddr(bo);
	sg_init_table(sg->sgl, 1);
	sg_dma_len(sg->sgl) = bo->size;
	sg_dma_address(sg->sgl) = paddr;
	sg_set_page(sg->sgl, pfn_to_page(PFN_DOWN(paddr)), bo->size, 0);

	return sg;
fail_sg:
	kfree(sg);
	return ERR_PTR(ret);
}

static void
nova_unmap_dma_buf(struct dma_buf_attachment *attachment, struct sg_table *sg)
{
	sg_free_table(sg);
	kfree(sg);
}

static void
nova_dma_buf_release(struct dma_buf *buf)
{
	struct drm_gem_object *bo = buf->priv;

	drm_gem_object_unreference_unlocked(bo);
}

struct dma_buf_ops nova_dma_buf_ops = {
	.map_dma_buf = nova_map_dma_buf,
	.unmap_dma_buf = nova_unmap_dma_buf,
	.release = nova_dma_buf_release,
};

static int
nova_prime_handle_to_fd(struct drm_device *dev, struct drm_file *file_priv,
					  uint32_t handle, int *prime_fd)
{
	int ret;
	struct drm_gem_object *bo;

	bo = drm_gem_object_lookup(dev, file_priv, handle);
	if (!bo)
		return -ENOENT;

	bo->export_dma_buf = dma_buf_export(bo, &nova_dma_buf_ops, bo->size,
							S_IRUSR | S_IWUSR);
	if (IS_ERR(bo->export_dma_buf)) {
		ret = PTR_ERR(bo->export_dma_buf);
		goto fail_export;
	}
	ret = dma_buf_fd(bo->export_dma_buf);
	if (ret < 0)
		goto fail_fd;

	*prime_fd = ret;

	return 0;
fail_fd:
	/* FIXME: dma_buf_"unexport/release" */
fail_export:
	drm_gem_object_unreference(bo);
	return ret;
}

static int
nova_prime_fd_to_handle(struct drm_device *dev, struct drm_file *file_priv,
					  int prime_fd, uint32_t *handle)
{
	return -EINVAL;
}
#endif /* CONFIG_DMA_BUF */

void
nova_gem_init(struct drm_driver *drv)
{
	drv->driver_features |= DRIVER_GEM;

	drv->gem_init_object = nova_gem_init_object;
	drv->gem_free_object = nova_gem_free_object;
	drv->gem_vm_ops = &nova_gem_vm_ops;
	drv->fops.mmap = drm_gem_mmap;

	drv->dumb_create = nova_dumb_create;
	drv->dumb_map_offset = nova_dumb_map_offset;
	drv->dumb_destroy = nova_dumb_destroy;

#ifdef CONFIG_DMA_BUF
	drv->driver_features |= DRIVER_PRIME;
	drv->prime_handle_to_fd = nova_prime_handle_to_fd;
	drv->prime_fd_to_handle = nova_prime_fd_to_handle;
#endif
}

