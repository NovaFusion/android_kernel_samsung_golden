/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Display output device driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/ioctl.h>

#include <linux/dispdev.h>
#include <linux/hwmem.h>
#include <video/mcde_dss.h>

#define DENSITY_CHECK (16)
#define MAX_BUFFERS 4

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_list_lock);

enum buffer_state {
	BUF_UNUSED = 0,
	BUF_QUEUED,
	BUF_ACTIVATED,
/*TODO:waitfordone	BUF_DEACTIVATED,*/
	BUF_FREE,
	BUF_DEQUEUED,
};

struct dispdev_buffer {
	struct hwmem_alloc *alloc;
	u32 size;
	enum buffer_state state;
	u32 paddr; /* if pinned */
};

struct dispdev {
	bool open;
	struct mutex lock;
	struct miscdevice mdev;
	struct list_head list;
	struct mcde_display_device *ddev;
	struct mcde_overlay *ovly;
	struct mcde_overlay *parent_ovly;
	struct dispdev_config config;
	bool overlay;
	struct dispdev_buffer buffers[MAX_BUFFERS];
	wait_queue_head_t waitq_dq;
	/*
	 * For the rotation use case
	 * buffers_need_update is used to ensure that a set_config that
	 * changes width or height is followed by a unregister_buffer.
	 */
	bool buffers_need_update;
	/*
	 * For the overlay startup use case.
	 * first_update is used to handle the first update after a set_config.
	 * In this case a queue_buffer will arrive after set_config and not a
	 * unregister_buffer as in the rotation use case.
	 */
	bool first_update;
	char name[sizeof(DISPDEV_DEFAULT_DEVICE_PREFIX) + 3];
};

static int find_buf(struct dispdev *dd, enum buffer_state state)
{
	int i;
	for (i = 0; i < MAX_BUFFERS; i++)
		if (dd->buffers[i].state == state)
			return i;
	return -1;
}

int dispdev_open(struct inode *inode, struct file *file)
{
	int ret;
	struct dispdev *dd = NULL;

	mutex_lock(&dev_list_lock);
	list_for_each_entry(dd, &dev_list, list)
		if (dd->mdev.minor == iminor(inode))
			break;

	if (&dd->list == &dev_list) {
		mutex_unlock(&dev_list_lock);
		return -ENODEV;
	}

	if (dd->open) {
		mutex_unlock(&dev_list_lock);
		return -EBUSY;
	}

	dd->open = true;

	mutex_unlock(&dev_list_lock);

	ret = mcde_dss_enable_overlay(dd->ovly);
	if (ret)
		return ret;

	file->private_data = dd;

	return 0;
}

int dispdev_release(struct inode *inode, struct file *file)
{
	int i;
	struct dispdev *dd = NULL;

	mutex_lock(&dev_list_lock);
	list_for_each_entry(dd, &dev_list, list)
		if (dd->mdev.minor == iminor(inode))
			break;
	mutex_unlock(&dev_list_lock);

	if (&dd->list == &dev_list)
		return -ENODEV;

	/* TODO: Make sure it waits for completion */
	mcde_dss_disable_overlay(dd->ovly);
	for (i = 0; i < MAX_BUFFERS; i++) {
		if (dd->buffers[i].paddr)
			hwmem_unpin(dd->buffers[i].alloc);
		if (dd->buffers[i].alloc)
			hwmem_release(dd->buffers[i].alloc);
		dd->buffers[i].alloc = NULL;
		dd->buffers[i].state = BUF_UNUSED;
		dd->buffers[i].size = 0;
		dd->buffers[i].paddr = 0;
	}
	dd->open = false;
	wake_up(&dd->waitq_dq);
	return 0;
}

static enum mcde_ovly_pix_fmt get_ovly_fmt(enum dispdev_fmt fmt)
{
	switch (fmt) {
	default:
	case DISPDEV_FMT_RGB565:
		return MCDE_OVLYPIXFMT_RGB565;
	case DISPDEV_FMT_RGB888:
		return MCDE_OVLYPIXFMT_RGB888;
	case DISPDEV_FMT_RGBA8888:
		return MCDE_OVLYPIXFMT_RGBA8888;
	case DISPDEV_FMT_RGBX8888:
		return MCDE_OVLYPIXFMT_RGBX8888;
	case DISPDEV_FMT_YUV422:
		return MCDE_OVLYPIXFMT_YCbCr422;
	}
}

static void get_ovly_info(struct dispdev_config *cfg,
				struct mcde_overlay_info *info)
{
	info->paddr = 0;
	info->stride = cfg->stride;
	info->fmt = get_ovly_fmt(cfg->format);
	info->src_x = 0;
	info->src_y = 0;
	info->dst_x = cfg->x;
	info->dst_y = cfg->y;
	info->dst_z = cfg->z;
	info->w = cfg->width;
	info->h = cfg->height;

}

static int dispdev_set_config(struct dispdev *dd, struct dispdev_config *cfg)
{
	int ret = 0;
	if (memcmp(&dd->config, cfg, sizeof(struct dispdev_config)) == 0)
		return 0;

	/*
	 * Only update MCDE if format, stride, width and height
	 * is the same. Otherwise just store the new config and update
	 * MCDE in the next queue buffer. This because the buffer that is
	 * active can be have the wrong format, width ...
	 */
	if (cfg->format == dd->config.format &&
			cfg->stride == dd->config.stride &&
			cfg->width == dd->config.width &&
			cfg->height == dd->config.height) {

		int buf_index;
		if (!dd->buffers_need_update) {
			buf_index = find_buf(dd, BUF_ACTIVATED);
			if (buf_index >= 0) {
				struct mcde_overlay_info info;
				struct dispdev_buffer *buf;

				buf = &dd->buffers[buf_index];
				get_ovly_info(cfg, &info);
				info.paddr = buf->paddr;
				ret = mcde_dss_apply_overlay(dd->ovly, &info);
				if (!ret)
					mcde_dss_update_overlay(dd->ovly,
									false);
			}
		}
	} else {
		dd->buffers_need_update = true;
	}

	dd->config = *cfg;

	return ret;
}

static int dispdev_register_buffer(struct dispdev *dd, s32 hwmem_name)
{
	int ret;
	struct dispdev_buffer *buf;
	enum hwmem_mem_type memtype;
	enum hwmem_access access;

	ret = find_buf(dd, BUF_UNUSED);
	if (ret < 0)
		return -ENOMEM;
	buf = &dd->buffers[ret];
	buf->alloc = hwmem_resolve_by_name(hwmem_name);
	if (IS_ERR(buf->alloc)) {
		ret = PTR_ERR(buf->alloc);
		goto resolve_failed;
	}

	hwmem_get_info(buf->alloc, &buf->size, &memtype, &access);

	if (!(access & HWMEM_ACCESS_READ) ||
					memtype == HWMEM_MEM_SCATTERED_SYS) {
		ret = -EACCES;
		goto invalid_mem;
	}

	buf->state = BUF_FREE;
	goto out;
invalid_mem:
	hwmem_release(buf->alloc);
resolve_failed:
out:
	return ret;
}

static int dispdev_unregister_buffer(struct dispdev *dd, u32 buf_idx)
{
	struct dispdev_buffer *buf = &dd->buffers[buf_idx];

	if (buf_idx >= ARRAY_SIZE(dd->buffers))
		return -EINVAL;

	if (buf->state == BUF_UNUSED)
		return -EINVAL;

	if (dd->buffers_need_update)
		dd->buffers_need_update = false;

	if (buf->state == BUF_ACTIVATED) {
		/* Disable the overlay */
		struct mcde_overlay_info info;

		/* TODO Wait for frame done */
		get_ovly_info(&dd->config, &info);
		mcde_dss_apply_overlay(dd->ovly, &info);
		mcde_dss_update_overlay(dd->ovly, false);
		hwmem_unpin(dd->buffers[buf_idx].alloc);
	}

	hwmem_release(buf->alloc);
	buf->state = BUF_UNUSED;
	buf->alloc = NULL;
	buf->size = 0;
	buf->paddr = 0;
	dd->first_update = false;

	return 0;
}


/**
 * @brief Check if the buffer is transparent or black (ARGB = X000)
 *        Note: Only for ARGB32.
 *        Worst case: a ~full transparent buffer
 *        Results: ~2200us @800Mhz for a WVGA screen, with DENSITY_CHECK=8
 *                 ~520us  @800Mhz for a WVGA screen, with DENSITY_CHECK=16
 *
 * @param w witdh
 * @param h height
 * @param addr buffer addr
 *
 * @return 1 if the buffer is transparent, else 0
 */
static int is_transparent(int w, int h, u32 *addr)
{
	int i, j;
	u32 *c, *next_line;
	u32 sum;

	next_line = addr;
	sum = 0;

	/* TODO Optimize me */
	for (j = 0; j < h; j += DENSITY_CHECK) {
		c = next_line;
		for (i = 0; i < w; i += DENSITY_CHECK) {
			sum += ((*c) & 0x00FFFFFF);
			c += DENSITY_CHECK;
		}
		if (sum)
			return 0; /* Not "transparent" */
		next_line += (w * DENSITY_CHECK);
	}

	return 1; /* "Transparent" */
}

static int dispdev_queue_buffer(struct dispdev *dd,
					struct dispdev_buffer_info *buffer)
{
	int ret, i;
	struct mcde_overlay_info info;
	struct hwmem_mem_chunk mem_chunk;
	size_t mem_chunk_length = 1;
	struct hwmem_region rgn = { .offset = 0, .count = 1, .start = 0 };
	struct hwmem_alloc *alloc;
	u32 buf_idx = buffer->buf_idx;

	if (buf_idx >= ARRAY_SIZE(dd->buffers) ||
				 dd->buffers[buf_idx].state != BUF_DEQUEUED)
		return -EINVAL;

	alloc = dd->buffers[buf_idx].alloc;
	get_ovly_info(&dd->config, &info);
	ret = hwmem_pin(alloc, &mem_chunk, &mem_chunk_length);
	if (ret) {
		dev_warn(dd->mdev.this_device, "Pin failed, %d\n", ret);
		return -EINVAL;
	}

	rgn.size = rgn.end = dd->buffers[buf_idx].size;
	ret = hwmem_set_domain(alloc, HWMEM_ACCESS_READ,
		HWMEM_DOMAIN_SYNC, &rgn);
	if (ret)
		dev_warn(dd->mdev.this_device, "Set domain failed, %d\n", ret);

	i = find_buf(dd, BUF_ACTIVATED);
	if (i >= 0) {
		dd->buffers[i].state = BUF_FREE;
		wake_up(&dd->waitq_dq);
	}

	if (!dd->first_update) {
		dd->first_update = true;
		dd->buffers_need_update = false;
	}

	dd->buffers[buf_idx].paddr = mem_chunk.paddr;

	if (buffer->display_update && !dd->buffers_need_update &&
			dd->config.width == buffer->buf_cfg.width &&
			dd->config.height == buffer->buf_cfg.height &&
			dd->config.format == buffer->buf_cfg.format &&
			dd->config.stride == buffer->buf_cfg.stride) {
		info.paddr = mem_chunk.paddr;
		mcde_dss_apply_overlay(dd->ovly, &info);
		mcde_dss_update_overlay(dd->ovly, false);
	} else if (buffer->display_update) {
		dd->buffers_need_update = true;
	}

	/* Disable the MCDE FB overlay */
	if ((dd->parent_ovly->state != NULL) &&
			(dd->ddev->check_transparency)) {
		dd->ddev->check_transparency--;
		mcde_dss_get_overlay_info(dd->parent_ovly, &info);
		if (dd->ddev->check_transparency == 0) {
			if (is_transparent(info.w, info.h, info.vaddr)) {
				mcde_dss_disable_overlay(dd->parent_ovly);
				dev_info(dd->mdev.this_device,
						"Disable overlay\n");
			}
		}
	}

	dd->buffers[buf_idx].state = BUF_ACTIVATED;

	return 0;
}

static int dispdev_dequeue_buffer(struct dispdev *dd)
{
	int i;

	i = find_buf(dd, BUF_FREE);
	if (i < 0) {
		if (find_buf(dd, BUF_ACTIVATED) < 0)
			return -EINVAL;
		mutex_unlock(&dd->lock);
		wait_event(dd->waitq_dq, (i = find_buf(dd, BUF_FREE)) >= 0);
		mutex_lock(&dd->lock);
	}
	hwmem_unpin(dd->buffers[i].alloc);
	dd->buffers[i].state = BUF_DEQUEUED;
	dd->buffers[i].paddr = 0;

	return i;
}

long dispdev_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	int ret;
	struct dispdev *dd = (struct dispdev *)file->private_data;

	mutex_lock(&dd->lock);

	switch (cmd) {
	case DISPDEV_SET_CONFIG_IOC:
		{
			struct dispdev_config cfg;
			if (copy_from_user(&cfg, (void __user *)arg,
								sizeof(cfg)))
				ret = -EFAULT;
			else
				ret = dispdev_set_config(dd, &cfg);
		}
		break;
	case DISPDEV_GET_CONFIG_IOC:
		ret = copy_to_user((void __user *)arg, &dd->config,
							sizeof(dd->config));
		if (ret)
			ret = -EFAULT;
		break;
	case DISPDEV_REGISTER_BUFFER_IOC:
		ret = dispdev_register_buffer(dd, (s32)arg);
		break;
	case DISPDEV_UNREGISTER_BUFFER_IOC:
		ret = dispdev_unregister_buffer(dd, (u32)arg);
		break;
	case DISPDEV_QUEUE_BUFFER_IOC:
	{
		struct dispdev_buffer_info buffer;
		if (copy_from_user(&buffer, (void __user *)arg,
							sizeof(buffer)))
			ret = -EFAULT;
		else
			ret = dispdev_queue_buffer(dd, &buffer);
		break;
	}
	case DISPDEV_DEQUEUE_BUFFER_IOC:
		ret = dispdev_dequeue_buffer(dd);
		break;
	default:
		ret = -ENOSYS;
	}

	mutex_unlock(&dd->lock);

	return ret;
}

static const struct file_operations dispdev_fops = {
	.open = dispdev_open,
	.release = dispdev_release,
	.unlocked_ioctl = dispdev_ioctl,
};

static void init_dispdev(struct dispdev *dd, struct mcde_display_device *ddev,
						const char *name, bool overlay)
{
	mutex_init(&dd->lock);
	INIT_LIST_HEAD(&dd->list);
	dd->ddev = ddev;
	dd->overlay = overlay;
	mcde_dss_get_native_resolution(ddev,
			&dd->config.width, &dd->config.height);
	dd->config.format = DISPDEV_FMT_RGB565;
	dd->config.stride = sizeof(u16) * w;
	dd->config.x = 0;
	dd->config.y = 0;
	dd->config.z = 0;
	dd->buffers_need_update = false;
	dd->first_update = false;
	init_waitqueue_head(&dd->waitq_dq);
	dd->mdev.minor = MISC_DYNAMIC_MINOR;
	dd->mdev.name = name;
	dd->mdev.fops = &dispdev_fops;
	pr_info("%s: name=%s w=%d, h=%d, fmt=%d, stride=%d\n", __func__, name,
			dd->config.width, dd->config.height, dd->config.format,
							dd->config.stride);
}

int dispdev_create(struct mcde_display_device *ddev, bool overlay,
					struct mcde_overlay *parent_ovly)
{
	int ret = 0;
	struct dispdev *dd;
	struct mcde_overlay_info info = {0};

	static int counter;

	dd = kzalloc(sizeof(struct dispdev), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	snprintf(dd->name, sizeof(dd->name), "%s%d",
		 DISPDEV_DEFAULT_DEVICE_PREFIX, counter++);
	init_dispdev(dd, ddev, dd->name, overlay);

	get_ovly_info(&dd->config, &info);

	if (!overlay) {
		ret = mcde_dss_enable_display(ddev);
		if (ret)
			goto fail_enable_display;
		ret = mcde_dss_set_pixel_format(ddev, info.fmt);
		if (ret)
			goto fail_enable_display;
		ret = mcde_dss_apply_channel(ddev);
		if (ret)
			goto fail_enable_display;
	}

	/* Save the MCDE FB overlay */
	dd->parent_ovly = parent_ovly;

	dd->ovly = mcde_dss_create_overlay(ddev, &info);
	if (!dd->ovly) {
		ret = -ENOMEM;
		goto fail_create_ovly;
	}

	ret = misc_register(&dd->mdev);
	if (ret)
		goto fail_register_misc;
	mutex_lock(&dev_list_lock);
	list_add_tail(&dd->list, &dev_list);
	mutex_unlock(&dev_list_lock);

	goto out;

fail_register_misc:
	mcde_dss_destroy_overlay(dd->ovly);
fail_create_ovly:
	if (!overlay)
		mcde_dss_disable_display(ddev);
fail_set_video_mode:
fail_enable_display:
	kfree(dd);
out:
	return ret;
}

void dispdev_destroy(struct mcde_display_device *ddev)
{
	struct dispdev *dd;
	struct dispdev *tmp;

	mutex_lock(&dev_list_lock);
	list_for_each_entry_safe(dd, tmp, &dev_list, list) {
		if (dd->ddev == ddev) {
			list_del(&dd->list);
			misc_deregister(&dd->mdev);
			mcde_dss_destroy_overlay(dd->ovly);
			/*
			 * TODO: Uncomment when DSS has reference
			 * counting of enable/disable
			 */
			/* mcde_dss_disable_display(dd->ddev); */
			kfree(dd);
			break;
		}
	}
	mutex_unlock(&dev_list_lock);
}

static void dispdev_destroy_all(void)
{
	struct dispdev *dd;
	struct dispdev *tmp;

	mutex_lock(&dev_list_lock);
	list_for_each_entry_safe(dd, tmp, &dev_list, list) {
		list_del(&dd->list);
		misc_deregister(&dd->mdev);
		mcde_dss_destroy_overlay(dd->ovly);
		/*
		 * TODO: Uncomment when DSS has reference
		 * counting of enable/disable
		 */
		/* mcde_dss_disable_display(dd->ddev); */
		kfree(dd);
	}
	mutex_unlock(&dev_list_lock);

	mutex_destroy(&dev_list_lock);
}

static int __init dispdev_init(void)
{
	pr_info("%s\n", __func__);

	mutex_init(&dev_list_lock);

	return 0;
}
module_init(dispdev_init);

static void __exit dispdev_exit(void)
{
	dispdev_destroy_all();
	pr_info("%s\n", __func__);
}
module_exit(dispdev_exit);

MODULE_AUTHOR("Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Display output device driver");

