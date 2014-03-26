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

#ifndef _COMPDEV_UTIL_H_
#define _COMPDEV_UTIL_H_

#include <linux/types.h>
#include <linux/compdev.h>
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/kobject.h>
#include <linux/workqueue.h>

#define BUFFER_CACHE_DEPTH 3

struct compdev_img_internal {
	struct compdev_img img;
	struct kref ref_count;
	bool protected;
};

struct buffer_cache_context {
	struct compdev_img_internal
		*img[BUFFER_CACHE_DEPTH];
	u8 index;
	u8 unused_counter;
	u8 allocated;
	struct device *dev;
	struct workqueue_struct *janitor_thread;
	struct delayed_work free_buffers_work;
	struct mutex janitor_lock;
};

enum b2r2_blt_fmt compdev_to_blt_format(enum compdev_fmt fmt);

enum b2r2_blt_transform compdev_to_blt_transform(
		enum compdev_transform transform);

u32 compdev_get_stride(u32 width, enum compdev_fmt fmt);

u32 compdev_get_bpp(enum compdev_fmt fmt);

void compdev_image_release(struct kref *ref);

void compdev_free_img(struct buffer_cache_context *cache_ctx,
		struct compdev_img_internal *img);

struct compdev_img_internal *compdev_buffer_cache_get_image(
		struct buffer_cache_context *cache_ctx, enum compdev_fmt fmt,
		u16 width, u16 height, bool protected);

void compdev_buffer_cache_mark_frame(struct buffer_cache_context *cache_ctx);
void compdev_free_cache_context_buffers(struct work_struct *work);

bool check_hw_format(enum compdev_fmt fmt);

enum compdev_fmt find_compatible_fmt(enum compdev_fmt fmt, bool rotation);

#endif /* _COMPDEV_UTIL_H_ */

