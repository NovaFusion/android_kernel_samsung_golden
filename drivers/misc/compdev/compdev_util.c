/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Util functions for Compdev
 *
 * Author: Per-Daniel Olsson <per-daniel.olsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/compdev_util.h>
#include <linux/compdev.h>
#include <video/mcde.h>
#include <video/b2r2_blt.h>
#include <linux/kref.h>
#include <linux/hwmem.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#ifdef CONFIG_COMPDEV_JANITOR
/* 200ms */
#define FREE_BUFFERS_TIMEOUT (HZ/5)
#endif

enum b2r2_blt_fmt compdev_to_blt_format(enum compdev_fmt fmt)
{
	switch (fmt) {
	case COMPDEV_FMT_RGBA8888:
		return B2R2_BLT_FMT_32_BIT_ARGB8888;
	case COMPDEV_FMT_RGB888:
		return B2R2_BLT_FMT_24_BIT_RGB888;
	case COMPDEV_FMT_RGB565:
		return B2R2_BLT_FMT_16_BIT_RGB565;
	case COMPDEV_FMT_YUV422:
		return B2R2_BLT_FMT_CB_Y_CR_Y;
	case COMPDEV_FMT_YCBCR42XMBN:
		return B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE;
	case COMPDEV_FMT_YUV420_SP:
		return B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR;
	case COMPDEV_FMT_YVU420_SP:
		return B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR;
	case COMPDEV_FMT_YUV420_P:
		return B2R2_BLT_FMT_YUV420_PACKED_PLANAR;
	case COMPDEV_FMT_YVU420_P:
		return B2R2_BLT_FMT_YVU420_PACKED_PLANAR;
	case COMPDEV_FMT_YV12:
		return B2R2_BLT_FMT_YV12;
	default:
		return B2R2_BLT_FMT_UNUSED;
	}
}

enum b2r2_blt_transform compdev_to_blt_transform(
			enum compdev_transform transform)
{
	switch (transform) {
	case COMPDEV_TRANSFORM_ROT_0:
		return B2R2_BLT_TRANSFORM_NONE;
	case COMPDEV_TRANSFORM_ROT_90_CCW:
		return B2R2_BLT_TRANSFORM_CCW_ROT_90;
	case COMPDEV_TRANSFORM_ROT_180:
		return B2R2_BLT_TRANSFORM_CCW_ROT_180;
	case COMPDEV_TRANSFORM_ROT_270_CCW:
		return B2R2_BLT_TRANSFORM_CCW_ROT_270;
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_H:
		return B2R2_BLT_TRANSFORM_FLIP_H_CCW_ROT_90;
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_V:
		return B2R2_BLT_TRANSFORM_FLIP_V_CCW_ROT_90;
	case COMPDEV_TRANSFORM_FLIP_H:
		return B2R2_BLT_TRANSFORM_FLIP_H;
	case COMPDEV_TRANSFORM_FLIP_V:
		return B2R2_BLT_TRANSFORM_FLIP_V;
	default:
		return B2R2_BLT_TRANSFORM_NONE;
	}
}

u32 compdev_get_stride(u32 width, enum compdev_fmt fmt)
{
	u32 stride = 0;
	switch (fmt) {
	case COMPDEV_FMT_RGB565:
		stride = width * 2;
		break;
	case COMPDEV_FMT_RGB888:
		stride = width * 3;
		break;
	case COMPDEV_FMT_RGBX8888:
		stride = width * 4;
		break;
	case COMPDEV_FMT_RGBA8888:
		stride = width * 4;
		break;
	case COMPDEV_FMT_YUV422:
		stride = width * 2;
		break;
	case COMPDEV_FMT_YCBCR42XMBN:
	case COMPDEV_FMT_YUV420_SP:
	case COMPDEV_FMT_YVU420_SP:
	case COMPDEV_FMT_YUV420_P:
	case COMPDEV_FMT_YVU420_P:
	case COMPDEV_FMT_YV12:
		stride = width;
		break;
	}

	/* Align the stride with MCDE requirements */
	stride = ALIGN(stride, MCDE_BUF_LINE_ALIGMENT);

	return stride;
}

u32 compdev_get_bpp(enum compdev_fmt fmt)
{
	u32 bpp = 0;
	switch (fmt) {
	case COMPDEV_FMT_RGB565:
		bpp = 16;
		break;
	case COMPDEV_FMT_RGB888:
		bpp = 24;
		break;
	case COMPDEV_FMT_RGBX8888:
	case COMPDEV_FMT_RGBA8888:
		bpp = 32;
		break;
	case COMPDEV_FMT_YUV422:
		bpp = 16;
		break;
	case COMPDEV_FMT_YCBCR42XMBN:
	case COMPDEV_FMT_YUV420_SP:
	case COMPDEV_FMT_YVU420_SP:
	case COMPDEV_FMT_YUV420_P:
	case COMPDEV_FMT_YVU420_P:
	case COMPDEV_FMT_YV12:
		bpp = 12;
		break;
	}
	return bpp;
}

static int get_chroma_pitch(u32 luma_pitch, enum compdev_fmt fmt)
{
	int chroma_pitch;

	switch (fmt) {
	case COMPDEV_FMT_YV12:
		chroma_pitch = ALIGN((luma_pitch >> 1), 16);
	case COMPDEV_FMT_YUV420_SP:
	case COMPDEV_FMT_YVU420_SP:
		chroma_pitch = luma_pitch;
	case COMPDEV_FMT_YCBCR42XMBN:
	case COMPDEV_FMT_YUV420_P:
	case COMPDEV_FMT_YVU420_P:
		chroma_pitch = luma_pitch >> 1;
	default:
		chroma_pitch = 0;
	}

	return chroma_pitch;
}

static int get_chroma_size(u32 luma_pitch, u32 luma_height,
		enum compdev_fmt fmt)
{
	int chroma_pitch = get_chroma_pitch(luma_pitch, fmt);

	if (chroma_pitch <= 0)
		return 0;

	switch (fmt) {
	case COMPDEV_FMT_YUV420_P:
	case COMPDEV_FMT_YVU420_P:
	case COMPDEV_FMT_YV12:
		return chroma_pitch * ALIGN(luma_height, 2);
	case COMPDEV_FMT_YUV420_SP:
	case COMPDEV_FMT_YVU420_SP:
		return chroma_pitch * ((luma_height + 1) >> 1);
	case COMPDEV_FMT_YCBCR42XMBN:
		return chroma_pitch * (ALIGN(luma_height, 16) >> 1);
	default:
		return 0;
	}
}

static int alloc_comp_internal_img(enum compdev_fmt fmt,
		u16 width, u16 height, bool protected,
		struct compdev_img_internal **img_pp)
{
	struct hwmem_alloc *alloc;
	int name;
	u32 size;
	u32 stride;
	struct compdev_img_internal *img;
	enum hwmem_mem_type mem_type;

	stride = compdev_get_stride(width, fmt);
	size = stride * height;
	size += get_chroma_size(stride, height, fmt);
	size = PAGE_ALIGN(size);

	if (protected)
		mem_type = HWMEM_MEM_PROTECTED_SYS;
	else
		mem_type = HWMEM_MEM_CONTIGUOUS_SYS;

	img = kzalloc(sizeof(struct compdev_img_internal), GFP_KERNEL);
	if (!img)
		return -ENOMEM;

	alloc = hwmem_alloc(size, HWMEM_ALLOC_HINT_WRITE_COMBINE |
			HWMEM_ALLOC_HINT_UNCACHED,
			(HWMEM_ACCESS_READ  | HWMEM_ACCESS_WRITE |
			HWMEM_ACCESS_IMPORT),
			mem_type);

	if (IS_ERR_OR_NULL(alloc)) {
		kfree(img);
		img = NULL;
		return PTR_ERR(alloc);
	}

	name = hwmem_get_name(alloc);
	if (name <= 0) {
		kfree(img);
		img = NULL;
		hwmem_release(alloc);
		if (name < 0)
			return name;
		return -ENOMEM;
	}

	img->img.height = height;
	img->img.width = width;
	img->img.fmt = fmt;
	img->img.pitch = stride;
	img->img.buf.hwmem_buf_name = name;
	img->img.buf.type = COMPDEV_PTR_HWMEM_BUF_NAME_OFFSET;
	img->img.buf.offset = 0;
	img->img.buf.len = size;
	img->protected = protected;

	kref_init(&img->ref_count);

	*img_pp = img;

	return 0;
}

void compdev_image_release(struct kref *ref)
{
	struct compdev_img_internal *img =
			container_of(ref, struct compdev_img_internal,
				ref_count);
	struct hwmem_alloc *alloc;

	if (img->img.buf.hwmem_buf_name > 0) {
		alloc = hwmem_resolve_by_name(
				img->img.buf.hwmem_buf_name);
		if (IS_ERR_OR_NULL(alloc)) {
			kfree(img);
			return;
		}

		/* Double release needed due to the resolve above */
		hwmem_release(alloc);
		hwmem_release(alloc);
	}
	kfree(img);
}

void compdev_free_img(struct buffer_cache_context *cache_ctx,
		struct compdev_img_internal *img)
{
	if (img != NULL) {
		kref_put(&img->ref_count, compdev_image_release);

#ifdef CONFIG_COMPDEV_JANITOR
		mutex_lock(&cache_ctx->janitor_lock);
#endif
		/* Start a timed job to do the janitor work */
		cache_ctx->unused_counter++;
#ifdef CONFIG_COMPDEV_JANITOR
		if (cache_ctx->unused_counter == cache_ctx->allocated) {
			cancel_delayed_work_sync(&cache_ctx->free_buffers_work);
			queue_delayed_work(cache_ctx->janitor_thread,
				&cache_ctx->free_buffers_work,
				FREE_BUFFERS_TIMEOUT);
		}

		mutex_unlock(&cache_ctx->janitor_lock);
#endif
	}
}

static struct compdev_img_internal *compdev_cache_alloc(
		struct buffer_cache_context *cache_ctx,
		int index, enum compdev_fmt fmt,
		u16 width, u16 height, bool protected)
{
	dev_dbg(cache_ctx->dev,
		"%s: Allocating new buffer in slot %d\n",
		__func__, index);

	if (alloc_comp_internal_img(fmt, width, height,
			protected, &cache_ctx->img[index]) != 0) {
		dev_dbg(cache_ctx->dev,
			"%s: Allocation error\n",
			__func__);
	} else {
		cache_ctx->allocated++;
		cache_ctx->unused_counter++;
		cache_ctx->index = index;
		kref_get(&cache_ctx->img[index]->ref_count);
		return cache_ctx->img[index];
	}

	return NULL;
}

struct compdev_img_internal *compdev_buffer_cache_get_image(
		struct buffer_cache_context *cache_ctx,
		enum compdev_fmt fmt,
		u16 width, u16 height, bool protected)
{
	int i;
	struct compdev_img_internal *img = NULL;


#ifdef CONFIG_COMPDEV_JANITOR
	mutex_lock(&cache_ctx->janitor_lock);

	/*
	 * Cancel janitor work so that we can do
	 * a controlled cleanup if needed
	 */
	cancel_delayed_work_sync(&cache_ctx->free_buffers_work);
#endif

	if (cache_ctx->allocated < BUFFER_CACHE_DEPTH) {
		/* Allocate a new buffer in a free cache slot */
		for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
			if (cache_ctx->img[i] != NULL) {
				continue;
			} else {
				img = compdev_cache_alloc(cache_ctx, i, fmt,
					width, height, protected);
				break;
			}
		}
	} else if (cache_ctx->unused_counter > 0) {
		/*
		 * Check for a cache hit. Also make sure to cycle among
		 * buffers since they are "freed" before we can know that
		 * MCDE is done with them. But since MCDE guarantees being
		 * done with the previous buffer (for double buffering)
		 * when the next buffer is queued, we can trust in that
		 * there will be no reuse of mcde "owned" buffers.
		 */
		struct compdev_img_internal *temp = NULL;

		/* Find available buffer */
		for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
			struct compdev_img_internal *c =
				cache_ctx->img[cache_ctx->index];
			if (c != NULL && atomic_inc_not_zero(
					&c->ref_count.refcount)) {
				if (atomic_read(&c->ref_count.refcount) == 2) {
					temp = c;
					break;
				} else {
					kref_put(&c->ref_count,
						compdev_image_release);
				}
			}
			/* Try the next cache slot */
			cache_ctx->index =
				(cache_ctx->index + 1) % BUFFER_CACHE_DEPTH;
		}

		if (temp != NULL) {
			/* Check that the buffer is the right one for us.
			 * NOTE: This check is external to the loop
			 * above in order to release the entire cache
			 * if our needs should change. We always need three
			 * buffers with the same characteristics. */
			if (temp->img.fmt == fmt &&
					temp->img.width == width &&
					temp->img.height == height &&
					temp->protected == protected) {
				img = temp;
				dev_dbg(cache_ctx->dev,
					"%s: cache hit, index: %d\n",
					__func__, cache_ctx->index);
			} else {
				kref_put(&temp->ref_count,
					compdev_image_release);
			}
		}
	}

	/* Check if we found a buffer */
	if (img == NULL) {
		cache_ctx->unused_counter = 0;
		cache_ctx->allocated = 0;
		/* Release the cache */
		for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
			if (cache_ctx->img[i] != NULL) {
				kref_put(&cache_ctx->img[i]->ref_count,
					compdev_image_release);
				cache_ctx->img[i] = NULL;
			}
		}

		/* Alloc new cache buffer in slot 0 */
		img = compdev_cache_alloc(cache_ctx, 0, fmt,
			width, height, protected);
	}

	if (img != NULL) {
		cache_ctx->unused_counter--;
		cache_ctx->index = (cache_ctx->index + 1) % BUFFER_CACHE_DEPTH;
	}

	dev_dbg(cache_ctx->dev,
		"%s: cache_ctx: 0x%08X, unused_counter: %d, "
		"allocated: %d\n", __func__, (uint32_t) cache_ctx,
		cache_ctx->unused_counter, cache_ctx->allocated);

#ifdef CONFIG_COMPDEV_JANITOR
	mutex_unlock(&cache_ctx->janitor_lock);
#endif

	return img;
}

void compdev_free_cache_context_buffers(struct work_struct *work)
{
#ifdef CONFIG_COMPDEV_JANITOR
	struct delayed_work *twork = to_delayed_work(work);
	struct buffer_cache_context *cache_ctx = container_of(
			twork,
			struct buffer_cache_context,
			free_buffers_work);
	int i;


	if (!mutex_trylock(&cache_ctx->janitor_lock))
		return;

	if (cache_ctx->allocated > 0 &&
			cache_ctx->unused_counter == cache_ctx->allocated) {
		dev_dbg(cache_ctx->dev, "%s\n", __func__);
		for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
			if (cache_ctx->img[i] != NULL) {
				dev_dbg(cache_ctx->dev,
					"%s: kref_put(&cache_ctx->img[%d]->ref_count\n",
					__func__, i);
				kref_put(&cache_ctx->img[i]->ref_count,
					compdev_image_release);
				cache_ctx->img[i] = NULL;
			}
		}
		cache_ctx->unused_counter = 0;
		cache_ctx->allocated = 0;
		cache_ctx->index = 0;
	}

	mutex_unlock(&cache_ctx->janitor_lock);
#endif
}

bool check_hw_format(enum compdev_fmt fmt)
{
	if (fmt == COMPDEV_FMT_RGB565 ||
			fmt == COMPDEV_FMT_RGB888 ||
			fmt == COMPDEV_FMT_RGBA8888 ||
			fmt == COMPDEV_FMT_RGBX8888 ||
			fmt == COMPDEV_FMT_YUV422)
		return true;
	else
		return false;
}

enum compdev_fmt find_compatible_fmt(enum compdev_fmt fmt, bool rotation)
{
	if (!rotation) {
		switch (fmt) {
		case COMPDEV_FMT_RGB565:
		case COMPDEV_FMT_RGB888:
		case COMPDEV_FMT_RGBA8888:
		case COMPDEV_FMT_RGBX8888:
			return fmt;
		case COMPDEV_FMT_YUV422:
		case COMPDEV_FMT_YCBCR42XMBN:
		case COMPDEV_FMT_YUV420_SP:
		case COMPDEV_FMT_YVU420_SP:
		case COMPDEV_FMT_YUV420_P:
		case COMPDEV_FMT_YVU420_P:
		case COMPDEV_FMT_YV12:
			return COMPDEV_FMT_YUV422;
		default:
			return COMPDEV_FMT_RGBA8888;
		}
	} else {
		switch (fmt) {
		case COMPDEV_FMT_RGB565:
		case COMPDEV_FMT_RGB888:
		case COMPDEV_FMT_RGBA8888:
		case COMPDEV_FMT_RGBX8888:
			return fmt;
		case COMPDEV_FMT_YUV422:
		case COMPDEV_FMT_YCBCR42XMBN:
		case COMPDEV_FMT_YUV420_SP:
		case COMPDEV_FMT_YVU420_SP:
		case COMPDEV_FMT_YUV420_P:
		case COMPDEV_FMT_YVU420_P:
		case COMPDEV_FMT_YV12:
			return COMPDEV_FMT_RGB565;
		default:
			return COMPDEV_FMT_RGBA8888;
		}
	}
}
