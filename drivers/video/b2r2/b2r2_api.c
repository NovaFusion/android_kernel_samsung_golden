/*
 * Copyright (C) ST-Ericsson SA 2010/2012
 *
 * ST-Ericsson B2R2 Blitter module API
 *
 * Author: Jorgen Nilsson <jorgen.nilsson@stericsson.com>
 * Author: Robert Fekete <robert.fekete@stericsson.com>
 * Author: Paul Wannback
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#ifdef CONFIG_ANDROID_PMEM
#include <linux/android_pmem.h>
#endif
#include <linux/fb.h>
#include <linux/uaccess.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include <asm/cacheflush.h>
#include <linux/smp.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/hwmem.h>
#include <linux/kref.h>

#include "b2r2_internal.h"
#include "b2r2_control.h"
#include "b2r2_core.h"
#include "b2r2_timing.h"
#include "b2r2_utils.h"
#include "b2r2_debug.h"
#include "b2r2_input_validation.h"
#include "b2r2_profiler_socket.h"
#include "b2r2_hw.h"

/*
 * TODO:
 * Implementation of query cap
 * Support for user space virtual pointer to physically consecutive memory
 * Support for user space virtual pointer to physically scattered memory
 * Callback reads lagging behind in blt_api_stress app
 * Store smaller items in the report list instead of the whole request
 * Support read of many report records at once.
 */

#define DATAS_START_SIZE 10
#define DATAS_GROW_SIZE 5

/**
 * @miscdev: The miscdev presenting b2r2 to the system
 */
struct b2r2_blt {
	spinlock_t                      lock;
	int                             next_job_id;
	struct miscdevice               miscdev;
	struct device                   *dev;
	struct mutex                    datas_lock;
	/**
	 * datas - Stores the b2r2_blt_data mapped to the cliend handle
	 */
	struct b2r2_blt_data **datas;
	/**
	 * data_count - The current maximum of active datas
	 */
	int data_count;
};

struct b2r2_blt_data {
	struct b2r2_control_instance *ctl_instace[B2R2_MAX_NBR_DEVICES];
};

/**
 * Used to keep track of coming and going b2r2 cores and
 * the number of active instance references
 */
struct b2r2_control_ref {
	struct b2r2_control *b2r2_control;
	spinlock_t lock;
};

/**
 * b2r2_blt - The blitter device, /dev/b2r2_blt
 */
struct kref blt_refcount;
static struct b2r2_blt *b2r2_blt;

/**
 * b2r2_control - The core controls and synchronization mechanism
 */
static struct b2r2_control_ref b2r2_controls[B2R2_MAX_NBR_DEVICES];

/**
 * b2r2_blt_add_control - Add the b2r2 core control
 */
void b2r2_blt_add_control(struct b2r2_control *cont)
{
	unsigned long flags;
	BUG_ON(cont->id < 0 || cont->id >= B2R2_MAX_NBR_DEVICES);

	spin_lock_irqsave(&b2r2_controls[cont->id].lock, flags);
	if (b2r2_controls[cont->id].b2r2_control == NULL)
		b2r2_controls[cont->id].b2r2_control = cont;
	spin_unlock_irqrestore(&b2r2_controls[cont->id].lock, flags);
}

/**
 * b2r2_blt_remove_control - Remove the b2r2 core control
 */
void b2r2_blt_remove_control(struct b2r2_control *cont)
{
	unsigned long flags;
	BUG_ON(cont->id < 0 || cont->id >= B2R2_MAX_NBR_DEVICES);

	spin_lock_irqsave(&b2r2_controls[cont->id].lock, flags);
	b2r2_controls[cont->id].b2r2_control = NULL;
	spin_unlock_irqrestore(&b2r2_controls[cont->id].lock, flags);
}

/**
 * b2r2_blt_get_control - Lock control for writing/removal
 */
static struct b2r2_control *b2r2_blt_get_control(int i)
{
	struct b2r2_control *cont;
	unsigned long flags;
	BUG_ON(i < 0 || i >= B2R2_MAX_NBR_DEVICES);

	spin_lock_irqsave(&b2r2_controls[i].lock, flags);
	cont = (struct b2r2_control *) b2r2_controls[i].b2r2_control;
	if (cont != NULL) {
		if (!cont->enabled)
			cont = NULL;
		else
			kref_get(&cont->ref);
	}
	spin_unlock_irqrestore(&b2r2_controls[i].lock, flags);

	return cont;
}

/**
 * b2r2_blt_release_control - Unlock control for writing/removal
 */
static void b2r2_blt_release_control(int i)
{
	struct b2r2_control *cont;
	unsigned long flags;
	BUG_ON(i < 0 || i >= B2R2_MAX_NBR_DEVICES);

	spin_lock_irqsave(&b2r2_controls[i].lock, flags);
	cont = (struct b2r2_control *) b2r2_controls[i].b2r2_control;
	spin_unlock_irqrestore(&b2r2_controls[i].lock, flags);
	if (cont != NULL)
		kref_put(&cont->ref, b2r2_core_release);
}

/**
 * Increase size of array containing b2r2 handles
 */
static int grow_datas(void)
{
	struct b2r2_blt_data **new_datas = NULL;
	int new_data_count = b2r2_blt->data_count + DATAS_GROW_SIZE;
	int ret = 0;

	new_datas = kzalloc(new_data_count * sizeof(*new_datas), GFP_KERNEL);
	if (new_datas == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	memcpy(new_datas, b2r2_blt->datas,
		b2r2_blt->data_count * sizeof(*b2r2_blt->datas));

	kfree(b2r2_blt->datas);

	b2r2_blt->data_count = new_data_count;
	b2r2_blt->datas = new_datas;
exit:
	return ret;
}

/**
 * Allocate and/or reserve a b2r2 handle
 */
static int alloc_handle(struct b2r2_blt_data *blt_data)
{
	int handle;
	int ret;

	mutex_lock(&b2r2_blt->datas_lock);

	if (b2r2_blt->datas == NULL) {
		b2r2_blt->datas = kzalloc(
			DATAS_START_SIZE * sizeof(*b2r2_blt->datas),
			GFP_KERNEL);
		if (b2r2_blt->datas == NULL) {
			ret = -ENOMEM;
			goto exit;
		}
		b2r2_blt->data_count = DATAS_START_SIZE;
	}

	for (handle = 0; handle < b2r2_blt->data_count; handle++) {
		if (b2r2_blt->datas[handle] == NULL) {
			b2r2_blt->datas[handle] = blt_data;
			break;
		}

		if (handle == b2r2_blt->data_count - 1) {
			ret = grow_datas();
			if (ret < 0)
				goto exit;
		}
	}
	ret = handle;
exit:
	mutex_unlock(&b2r2_blt->datas_lock);

	return ret;
}

/**
 * Get b2r2 data from b2r2 handle
 */
static struct b2r2_blt_data *get_data(int handle)
{
	if (handle >= b2r2_blt->data_count || handle < 0)
		return NULL;
	else
		return b2r2_blt->datas[handle];
}

/**
 * Unreserve b2r2 handle
 */
static void free_handle(int handle)
{
	if (handle < b2r2_blt->data_count && handle >= 0)
		b2r2_blt->datas[handle] = NULL;
}

/**
 * Get the next job number. This is the one returned to the client
 * if the blit request was successful.
 */
static int get_next_job_id(void)
{
	int job_id;
	unsigned long flags;

	spin_lock_irqsave(&b2r2_blt->lock, flags);
	if (b2r2_blt->next_job_id < 1)
		b2r2_blt->next_job_id = 1;
	job_id = b2r2_blt->next_job_id++;
	spin_unlock_irqrestore(&b2r2_blt->lock, flags);

	return job_id;
}

/**
 * Check for macroblock formats
 */
static bool is_mb_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return true;
	default:
		return false;
	}
}

/**
 * Limit the number of cores used in some "easy" and impossible cases
 */
static int limit_blits(int n_split, struct b2r2_blt_req *user_req)
{
	if (n_split <= 1)
		return n_split;

	if (user_req->dst_rect.width < 24 && user_req->dst_rect.height < 24)
		return 1;

	if (user_req->src_rect.width < n_split &&
			user_req->src_rect.height < n_split)
		return 1;

	/*
	 * Handle macroblock formats with one
	 * core for now since there seems to be some bug
	 * related to macroblock access patterns
	 */
	if (is_mb_fmt(user_req->src_img.fmt))
		return 1;

	return n_split;
}

/**
 * Check if the format inherently requires the b2r2 scaling engine to be active
 */
static bool is_scaling_fmt(enum b2r2_blt_fmt fmt)
{
	/* Plane separated formats must be treated as scaling */
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12:
		return true;
	default:
		return false;
	}
}


/**
 * Split a request rectangle on available cores
 */
static int b2r2_blt_split_request(struct b2r2_blt_data *blt_data,
		struct b2r2_blt_req *user_req,
		struct b2r2_blt_request **split_requests,
		struct b2r2_control_instance **ctl,
		int *n_split)
{
	int sstep_x, sstep_y, dstep_x, dstep_y;
	int dstart_x, dstart_y;
	int bstart_x, bstart_y;
	int dpos_x, dpos_y;
	int bpos_x, bpos_y;
	int dso_x = 1;
	int dso_y = 1;
	int sf_x, sf_y;
	int i;
	int srw, srh;
	int drw, drh;
	bool ssplit_x = true;
	bool dsplit_x = true;
	enum b2r2_blt_transform transform;
	bool is_rotation = false;
	bool is_scaling = false;
	bool bg_blend = false;
	u32 core_mask = 0;

	srw = user_req->src_rect.width;
	srh = user_req->src_rect.height;
	drw = user_req->dst_rect.width;
	drh = user_req->dst_rect.height;
	transform = user_req->transform;

	/* Early exit in the basic cases */
	if (*n_split == 0) {
		return -ENOSYS;
	} else if (*n_split == 1 ||
			(srw < *n_split && srh < *n_split) ||
			(drw < *n_split && drh < *n_split)) {
		memcpy(&split_requests[0]->user_req,
				user_req,
				sizeof(*user_req));
		split_requests[0]->core_mask =
				(1 << split_requests[0]->instance->control_id);
		*n_split = 1;
		return 0;
	}

	/*
	 * TODO: fix the load balancing algorithm
	 */

	is_rotation = (transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) != 0;

	/* Check for scaling */
	if (is_rotation) {
		is_scaling = (user_req->src_rect.width !=
				user_req->dst_rect.height) ||
			(user_req->src_rect.height !=
				user_req->dst_rect.width);
	} else {
		is_scaling = (user_req->src_rect.width !=
				user_req->dst_rect.width) ||
			(user_req->src_rect.height !=
				user_req->dst_rect.height);
	}

	is_scaling = is_scaling ||
			is_scaling_fmt(user_req->src_img.fmt) ||
			is_scaling_fmt(user_req->dst_img.fmt);

	bg_blend = ((user_req->flags & B2R2_BLT_FLAG_BG_BLEND) != 0);

	/*
	 * Split the request
	 */

	b2r2_log_info(b2r2_blt->dev, "%s: In (t:0x%08X, f:0x%08X):\n"
		"\tsrc_rect x:%d, y:%d, w:%d, h:%d src fmt:0x%x\n"
		"\tdst_rect x:%d, y:%d, w:%d, h:%d dst fmt:0x%x\n",
		__func__,
		user_req->transform,
		user_req->flags,
		user_req->src_rect.x,
		user_req->src_rect.y,
		user_req->src_rect.width,
		user_req->src_rect.height,
		user_req->src_img.fmt,
		user_req->dst_rect.x,
		user_req->dst_rect.y,
		user_req->dst_rect.width,
		user_req->dst_rect.height,
		user_req->dst_img.fmt);

	/*
	 * TODO: We need sub pixel precision here,
	 * or a better way to split rects
	 */
	dstart_x = user_req->dst_rect.x;
	dstart_y = user_req->dst_rect.y;
	if (bg_blend) {
		bstart_x = user_req->bg_rect.x;
		bstart_y = user_req->bg_rect.y;
	}

	if (srw && srh) {
		if ((srw < srh) && !is_scaling) {
			ssplit_x = false;
			sstep_y = srh / *n_split;
			/* Round up */
			if (srh % (*n_split))
				sstep_y++;

			if (srh > 16)
				sstep_y = ((sstep_y + 16) >> 4) << 4;

			if (transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
				sf_y = (drw << 10) / srh;
				dstep_x = (sf_y * sstep_y) >> 10;
			} else {
				dsplit_x = false;
				sf_y = (drh << 10) / srh;
				dstep_y = (sf_y * sstep_y) >> 10;
			}
		} else {
			sstep_x = srw / *n_split;
			/* Round up */
			if (srw % (*n_split))
				sstep_x++;

			if (is_scaling) {
				int scale_step_size =
						B2R2_RESCALE_MAX_WIDTH - 1;
				int pad = (scale_step_size -
						(sstep_x % scale_step_size));
				if ((sstep_x + pad) < srw)
					sstep_x += pad;
			} else {
				/* Aim for even 16px multiples */
				if ((sstep_x & 0xF) && ((sstep_x + 16) < srw))
					sstep_x = ((sstep_x + 16) >> 4) << 4;
			}

			if (transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
				dsplit_x = false;
				sf_x = (drh << 10) / srw;
				dstep_y = (sf_x * sstep_x) >> 10;
			} else {
				sf_x = (drw << 10) / srw;
				dstep_x = (sf_x * sstep_x) >> 10;
			}
		}

	} else {
		sstep_x = sstep_y = 0;

		if (drw < drh) {
			dsplit_x = false;
			dstep_y = drh / *n_split;
			/* Round up */
			if (drh % *n_split)
				dstep_y++;

			/* Aim for even 16px multiples */
			if ((dstep_y & 0xF) && ((dstep_y + 16) < drh))
				dstep_y = ((dstep_y + 16) >> 4) << 4;
		} else {
			dstep_x = drw / *n_split;
			/* Round up */
			if (drw % *n_split)
				dstep_x++;

			/* Aim for even 16px multiples */
			if ((dstep_x & 0xF) && ((dstep_x + 16) < drw))
				dstep_x = ((dstep_x + 16) >> 4) << 4;
		}
	}

	/*
	 * Check for flip and rotate to establish destination
	 * step order
	 */
	if (transform & B2R2_BLT_TRANSFORM_FLIP_H) {
		dstart_x +=  drw;
		if (bg_blend)
			bstart_x +=  drw;
		dso_x = -1;
	}
	if (transform != B2R2_BLT_TRANSFORM_CCW_ROT_270) {
		if ((transform & B2R2_BLT_TRANSFORM_FLIP_V) ||
				(transform & B2R2_BLT_TRANSFORM_CCW_ROT_90)) {
			dstart_y += drh;
			if (bg_blend)
				bstart_y += drh;
			dso_y = -1;
		}
	}

	/* Set scan starting position */
	dpos_x = dstart_x;
	dpos_y = dstart_y;
	if (bg_blend) {
		bpos_x = bstart_x;
		bpos_y = bstart_y;
	}

	for (i = 0; i < *n_split; i++) {
		struct b2r2_blt_req *sreq =
				&split_requests[i]->user_req;

		/* First mimic all */
		memcpy(sreq, user_req, sizeof(*user_req));

		/* Then change the rects */
		if (srw && srh) {
			if (ssplit_x) {
				if (sstep_x > 0) {
					sreq->src_rect.width =
						min(sstep_x, srw);
					sreq->src_rect.x += i*sstep_x;
					srw -= sstep_x;
				} else {
					sreq->src_rect.width = srw;
				}
			} else {
				if (sstep_y > 0) {
					sreq->src_rect.y += i*sstep_y;
					sreq->src_rect.height =
						min(sstep_y, srh);
					srh -= sstep_y;
				} else {
					sreq->src_rect.height = srh;
				}
			}
		}

		if (dsplit_x) {
			int sx = min(dstep_x, drw);
			if (dso_x < 0) {
				dpos_x += dso_x * sx;
				if (bg_blend)
					bpos_x += dso_x * sx;
			}
			sreq->dst_rect.width = sx;
			sreq->dst_rect.x = dpos_x;
			if (bg_blend) {
				sreq->bg_rect.width = sx;
				sreq->bg_rect.x = bpos_x;
			}
			if (dso_x > 0) {
				dpos_x += dso_x * sx;
				if (bg_blend)
					bpos_x += dso_x * sx;
			}
			drw -= sx;
		} else {
			int sy = min(dstep_y, drh);
			if (dso_y < 0) {
				dpos_y += dso_y * sy;
				if (bg_blend)
					bpos_y += dso_y * sy;
			}
			sreq->dst_rect.height = sy;
			sreq->dst_rect.y = dpos_y;
			if (bg_blend) {
				sreq->bg_rect.height = sy;
				sreq->bg_rect.y = bpos_y;
			}
			if (dso_y > 0) {
				dpos_y += dso_y * sy;
				if (bg_blend)
					bpos_y += dso_y * sy;
			}
			drh -= sy;
		}

		b2r2_log_info(b2r2_blt->dev, "%s: Out:\n"
				"\tsrc_rect x:%d, y:%d, w:%d, h:%d\n"
				"\tdst_rect x:%d, y:%d, w:%d, h:%d\n"
				"\tbg_rect  x:%d, y:%d, w:%d, h:%d\n",
				__func__,
				sreq->src_rect.x,
				sreq->src_rect.y,
				sreq->src_rect.width,
				sreq->src_rect.height,
				sreq->dst_rect.x,
				sreq->dst_rect.y,
				sreq->dst_rect.width,
				sreq->dst_rect.height,
				sreq->bg_rect.x,
				sreq->bg_rect.y,
				sreq->bg_rect.width,
				sreq->bg_rect.height);

		core_mask |= (1 << split_requests[i]->instance->control_id);
	}

	for (i = 0; i < *n_split; i++)
		split_requests[i]->core_mask = core_mask;

	return 0;
}

/**
 * Get available b2r2 control instances. It will be limited
 * to the number of cores available at the current point in time.
 * It will also cause the cores to stay active during the time until
 * release_control_instances is called.
 */
static void get_control_instances(struct b2r2_blt_data *blt_data,
		struct b2r2_control_instance **ctl, int max_size,
		int *count)
{
	int i;

	*count = 0;
	for (i = 0; i < max_size; i++) {
		struct b2r2_control_instance *ci = blt_data->ctl_instace[i];
		if (ci) {
			struct b2r2_control *cont =
				b2r2_blt_get_control(ci->control_id);
			if (cont) {
				ctl[*count] = ci;
				*count += 1;
			}
		}
	}
}

/**
 * Release b2r2 control instances. The cores allocated for the request
 * are given back.
 */
static void release_control_instances(struct b2r2_control_instance **ctl,
		int count)
{
	int i;

	/* Release the handles to the core controls */
	for (i = 0; i < count; i++) {
		if (ctl[i])
			b2r2_blt_release_control(ctl[i]->control_id);
	}
}

/**
 * Free b2r2 request
 */
static void b2r2_free_request(struct b2r2_blt_request *request)
{
	if (request) {
		/* Free requests in split_requests */
		if (request->clut)
			dma_free_coherent(b2r2_blt->dev,
				CLUT_SIZE,
				request->clut,
				request->clut_phys_addr);
		request->clut = NULL;
		request->clut_phys_addr = 0;
		kfree(request);
	}
}

/**
 * Allocate internal b2r2 request based on user input.
 */
static int b2r2_alloc_request(struct b2r2_blt_req *user_req,
		bool us_req, struct b2r2_blt_request **request_out)
{
	int ret = 0;
	struct b2r2_blt_request *request =
			kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	/* Initialize the structure */
	INIT_LIST_HEAD(&request->list);

	/*
	 * If the user specified a color look-up table,
	 * make a copy that the HW can use.
	 */
	if ((user_req->flags &
		B2R2_BLT_FLAG_CLUT_COLOR_CORRECTION) != 0) {
		request->clut = dma_alloc_coherent(
			b2r2_blt->dev,
			CLUT_SIZE,
			&(request->clut_phys_addr),
			GFP_DMA | GFP_KERNEL);
		if (request->clut == NULL) {
			b2r2_log_err(b2r2_blt->dev,
				"%s CLUT allocation "
				"failed.\n", __func__);
			ret = -ENOMEM;
			goto exit;
		}

		if (us_req) {
			if (copy_from_user(request->clut,
				user_req->clut, CLUT_SIZE)) {
				b2r2_log_err(b2r2_blt->dev, "%s: CLUT "
					"copy_from_user failed\n",
					__func__);
				ret = -EFAULT;
				goto exit;
			}
		} else {
			memcpy(request->clut, user_req->clut,
					CLUT_SIZE);
		}
	}

	request->profile = is_profiler_registered_approx();

	*request_out = request;
exit:
	if (ret != 0)
		b2r2_free_request(request);

	return ret;
}

/**
 * Do the blit job split on available cores.
 */
static int b2r2_blt_blit_internal(int handle,
		struct b2r2_blt_req *user_req,
		bool us_req)
{
	int request_id;
	int i;
	int n_instance = 0;
	int n_blit = 0;
	int ret = 0;
	struct b2r2_blt_data *blt_data;
	struct b2r2_blt_req ureq;

	/* The requests and the designated workers */
	struct b2r2_blt_request *split_requests[B2R2_MAX_NBR_DEVICES];
	struct b2r2_control_instance *ctl[B2R2_MAX_NBR_DEVICES];

	blt_data = get_data(handle);
	if (blt_data == NULL) {
		b2r2_log_warn(b2r2_blt->dev,
			"%s, blitter instance not found (handle=%d)\n",
			__func__, handle);
		return -ENOSYS;
	}

	/* Get the b2r2 core controls for the job */
	get_control_instances(blt_data, ctl, B2R2_MAX_NBR_DEVICES, &n_instance);
	if (n_instance == 0) {
		b2r2_log_err(b2r2_blt->dev, "%s: No b2r2 cores available.\n",
			__func__);
		return -ENOSYS;
	}

	/* Get the user data */
	if (us_req) {
		if (copy_from_user(&ureq, user_req, sizeof(ureq))) {
			b2r2_log_err(b2r2_blt->dev,
				"%s: copy_from_user failed\n",
				__func__);
			ret = -EFAULT;
			goto exit;
		}
	} else {
		memcpy(&ureq, user_req, sizeof(ureq));
	}

	/*
	 * B2R2 cannot handle destination clipping on buffers
	 * allocated close to 64MiB bank boundaries.
	 * recalculate src_ and dst_rect to avoid clipping.
	 *
	 * Also this is needed to ensure the request split
	 * operates on visible areas
	 */
	b2r2_recalculate_rects(b2r2_blt->dev, &ureq);

	if (!b2r2_validate_user_req(b2r2_blt->dev, &ureq)) {
		b2r2_log_warn(b2r2_blt->dev,
			"%s: b2r2_validate_user_req failed.\n",
			__func__);
		ret = -EINVAL;
		goto exit;
	}

	/* Don't split small requests */
	n_blit = limit_blits(n_instance, &ureq);

	/* The id needs to be universal on all cores */
	request_id = get_next_job_id();

#ifdef CONFIG_B2R2_GENERIC_ONLY
	/* Limit the generic only solution to one core (for now) */
	n_blit = 1;
#endif

	for (i = 0; i < n_blit; i++) {
		ret = b2r2_alloc_request(&ureq, us_req, &split_requests[i]);
		if (ret < 0 || !split_requests[i]) {
			b2r2_log_err(b2r2_blt->dev, "%s: Failed to alloc mem\n",
				__func__);
			ret = -ENOMEM;
			break;
		}
		split_requests[i]->instance = ctl[i];
		split_requests[i]->job.job_id = request_id;
		split_requests[i]->job.data = (int) ctl[i]->control->data;
	}

	/* Split the request */
	if (ret >= 0)
		ret = b2r2_blt_split_request(blt_data, &ureq,
				&split_requests[0], &ctl[0], &n_blit);

	/* If anything failed, clean up allocated memory */
	if (ret < 0) {
		for (i = 0; i < n_blit; i++)
			b2r2_free_request(split_requests[i]);
		b2r2_log_err(b2r2_blt->dev,
				"%s: b2r2_blt_split_request failed.\n",
				__func__);
		goto exit;
	}

#ifdef CONFIG_B2R2_GENERIC_ONLY
	if (ureq.flags & B2R2_BLT_FLAG_BG_BLEND) {
		/*
		 * No support for BG BLEND in generic
		 * implementation yet
		 */
		b2r2_log_warn(b2r2_blt->dev, "%s: Unsupported: "
			"Background blend in b2r2_generic_blt\n",
			__func__);
		ret = -ENOSYS;
		b2r2_free_request(split_requests[0]);
		goto exit;
	}
		/* Use the generic path for all operations */
	ret = b2r2_generic_blt(split_requests[0]);
#else
	/* Call each blitter control */
	for (i = 0; i < n_blit; i++) {
		ret = b2r2_control_blt(split_requests[i]);
		if (ret < 0) {
			b2r2_log_warn(b2r2_blt->dev,
				"%s: b2r2_control_blt failed.\n", __func__);
			break;
		}
	}
	if (ret != -ENOSYS) {
		int j;
		/* TODO: if one blitter fails then cancel the jobs added */

		/*
		 * Call waitjob for successful jobs
		 * (synchs if specified in request)
		 */
		if (ureq.flags & B2R2_BLT_FLAG_DRY_RUN)
			goto exit;

		for (j = 0; j < i; j++) {
			int rtmp;

			/*
			 * For debugging purposes we can choose to
			 * omit parts of the split job through debugfs
			 */
			if (split_requests[j]->instance->control->bypass)
				continue;
			rtmp = b2r2_control_waitjob(split_requests[j]);
			if (rtmp < 0) {
				b2r2_log_err(b2r2_blt->dev,
					"%s: b2r2_control_waitjob failed.\n",
					__func__);
			}

			/* Save just the one error */
			ret = (ret >= 0) ? rtmp : ret;
		}
	}
#endif
#ifdef CONFIG_B2R2_GENERIC_FALLBACK
	if (ret == -ENOSYS) {
		struct b2r2_blt_request *request_gen = NULL;
		if (ureq.flags & B2R2_BLT_FLAG_BG_BLEND) {
			/*
			 * No support for BG BLEND in generic
			 * implementation yet
			 */
			b2r2_log_warn(b2r2_blt->dev, "%s: Unsupported: "
				"Background blend in b2r2_generic_blt\n",
				__func__);
			goto exit;
		}

		b2r2_log_info(b2r2_blt->dev,
			"b2r2_blt=%d Going generic.\n", ret);
		ret = b2r2_alloc_request(&ureq, us_req, &request_gen);
		if (ret < 0 || !request_gen) {
			b2r2_log_err(b2r2_blt->dev,
				"%s: Failed to alloc mem for "
				"request_gen\n", __func__);
			ret = -ENOMEM;
			goto exit;
		}

		/* Initialize the structure */
		request_gen->instance = ctl[0];
		memcpy(&request_gen->user_req, &ureq,
				sizeof(request_gen->user_req));
		request_gen->core_mask = 1;
		request_gen->job.job_id = request_id;
		request_gen->job.data = (int) ctl[0]->control->data;

		ret = b2r2_generic_blt(request_gen);
		b2r2_log_info(b2r2_blt->dev, "\nb2r2_generic_blt=%d "
			"Generic done.\n", ret);
	}
#endif
exit:
	release_control_instances(ctl, n_instance);

	ret = ret >= 0 ? request_id : ret;

	return ret;
}

/**
 * Free the memory used for the b2r2_blt device
 */
static void b2r2_blt_release(struct kref *ref)
{
	BUG_ON(b2r2_blt == NULL);
	if (b2r2_blt == NULL)
		return;
	kfree(b2r2_blt->datas);
	kfree(b2r2_blt);
	b2r2_blt = NULL;
}

int b2r2_blt_open(void)
{
	int ret = 0;
	struct b2r2_blt_data *blt_data = NULL;
	int i;

	if (!atomic_inc_not_zero(&blt_refcount.refcount))
		return -ENOSYS;

	/* Allocate blitter instance data structure */
	blt_data = (struct b2r2_blt_data *)
			kzalloc(sizeof(*blt_data), GFP_KERNEL);
	if (!blt_data) {
		b2r2_log_err(b2r2_blt->dev, "%s: Failed to alloc\n", __func__);
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		struct b2r2_control *control = b2r2_blt_get_control(i);
		if (control != NULL) {
			struct b2r2_control_instance *ci;

			/* Allocate and initialize the control instance */
			ci = kzalloc(sizeof(*ci), GFP_KERNEL);
			if (!ci) {
				b2r2_log_err(b2r2_blt->dev,
						"%s: Failed to alloc\n",
						__func__);
				ret = -ENOMEM;
				b2r2_blt_release_control(i);
				goto err;
			}
			ci->control_id = i;
			ci->control = control;
			ret = b2r2_control_open(ci);
			if (ret < 0) {
				b2r2_log_err(b2r2_blt->dev,
					"%s: Failed to open b2r2 control %d\n",
					__func__, i);
				kfree(ci);
				b2r2_blt_release_control(i);
				goto err;
			}
			blt_data->ctl_instace[i] = ci;
			b2r2_blt_release_control(i);
		} else {
			blt_data->ctl_instace[i] = NULL;
		}
	}

	/* TODO: Create kernel worker kthread */

	ret = alloc_handle(blt_data);
	if (ret < 0)
		goto err;

	kref_put(&blt_refcount, b2r2_blt_release);

	return ret;

err:
	/* Destroy the blitter instance data structure */
	if (blt_data) {
		for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++)
			kfree(blt_data->ctl_instace[i]);
		kfree(blt_data);
	}

	kref_put(&blt_refcount, b2r2_blt_release);

	return ret;
}
EXPORT_SYMBOL(b2r2_blt_open);

int b2r2_blt_close(int handle)
{
	int i;
	struct b2r2_blt_data *blt_data;
	int ret = 0;

	if (!atomic_inc_not_zero(&blt_refcount.refcount))
		return -ENOSYS;

	b2r2_log_info(b2r2_blt->dev, "%s\n", __func__);

	blt_data = get_data(handle);
	if (blt_data == NULL) {
		b2r2_log_warn(b2r2_blt->dev,
			"%s, blitter data not found (handle=%d)\n",
			__func__, handle);
		ret = -ENOSYS;
		goto exit;
	}
	free_handle(handle);

	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		struct b2r2_control_instance *ci =
				blt_data->ctl_instace[i];
		if (ci != NULL) {
			struct b2r2_control *cont =
				b2r2_blt_get_control(ci->control_id);
			if (cont) {
				/* Release the instance */
				b2r2_control_release(ci);
				b2r2_blt_release_control(ci->control_id);
			}
			kfree(ci);
		}
	}
	kfree(blt_data);

exit:
	kref_put(&blt_refcount, b2r2_blt_release);

	return ret;
}
EXPORT_SYMBOL(b2r2_blt_close);

int b2r2_blt_request(int handle,
		struct b2r2_blt_req *user_req)
{
	int ret = 0;

	if (!atomic_inc_not_zero(&blt_refcount.refcount))
		return -ENOSYS;

	/* Exclude some currently unsupported cases */
	if ((user_req->flags & B2R2_BLT_FLAG_REPORT_WHEN_DONE) ||
			(user_req->flags & B2R2_BLT_FLAG_REPORT_PERFORMANCE) ||
			(user_req->report1 != 0)) {
		b2r2_log_err(b2r2_blt->dev,
				"%s No callback support in the kernel API\n",
			__func__);
		ret = -ENOSYS;
		goto exit;
	}

	ret = b2r2_blt_blit_internal(handle, user_req, false);

exit:
	kref_put(&blt_refcount, b2r2_blt_release);

	return ret;
}
EXPORT_SYMBOL(b2r2_blt_request);

int b2r2_blt_synch(int handle, int request_id)
{
	int ret = 0;
	int i;
	int n_synch = 0;
	struct b2r2_control_instance *ctl[B2R2_MAX_NBR_DEVICES];
	struct b2r2_blt_data *blt_data;

	if (!atomic_inc_not_zero(&blt_refcount.refcount))
		return -ENOSYS;

	b2r2_log_info(b2r2_blt->dev, "%s\n", __func__);

	blt_data = get_data(handle);
	if (blt_data == NULL) {
		b2r2_log_warn(b2r2_blt->dev,
			"%s, blitter data not found (handle=%d)\n",
			__func__, handle);
		ret = -ENOSYS;
		goto exit;
	}

	/* Get the b2r2 core controls for the job */
	get_control_instances(blt_data, ctl, B2R2_MAX_NBR_DEVICES, &n_synch);
	if (n_synch == 0) {
		b2r2_log_err(b2r2_blt->dev, "%s: No b2r2 cores available.\n",
			__func__);
		ret = -ENOSYS;
		goto exit;
	}

	for (i = 0; i < n_synch; i++) {
		ret = b2r2_control_synch(ctl[i], request_id);
		if (ret != 0) {
			b2r2_log_err(b2r2_blt->dev,
				"%s: b2r2_control_synch failed.\n",
				__func__);
			break;
		}
	}

	/* Release the handles to the core controls */
	release_control_instances(ctl, n_synch);

exit:
	kref_put(&blt_refcount, b2r2_blt_release);

	b2r2_log_info(b2r2_blt->dev,
		"%s, request_id=%d, returns %d\n", __func__, request_id, ret);

	return ret;
}
EXPORT_SYMBOL(b2r2_blt_synch);

/**
 * The user space API
 */

/**
 * b2r2_blt_open_us - Implements file open on the b2r2_blt device
 *
 * @inode: File system inode
 * @filp: File pointer
 *
 * A b2r2_blt_data handle is created and stored in the file structure.
 */
static int b2r2_blt_open_us(struct inode *inode, struct file *filp)
{
	int ret = 0;
	int handle;

	handle = b2r2_blt_open();
	if (handle < 0) {
		b2r2_log_err(b2r2_blt->dev, "%s: Failed to open handle\n",
			__func__);
		ret = handle;
		goto exit;
	}
	filp->private_data = (void *) handle;
exit:
	return ret;
}

/**
 * b2r2_blt_release_us - Implements last close on an instance of
 *                    the b2r2_blt device
 *
 * @inode: File system inode
 * @filp: File pointer
 *
 * All active jobs are finished or cancelled and allocated data
 * is released.
 */
static int b2r2_blt_release_us(struct inode *inode, struct file *filp)
{
	int ret;
	ret = b2r2_blt_close((int) filp->private_data);
	return ret;
}

/**
 * Query B2R2 capabilities
 *
 * @blt_data: The B2R2 BLT instance
 * @query_cap: The structure receiving the capabilities
 */
static int b2r2_blt_query_cap(struct b2r2_blt_data *blt_data,
		struct b2r2_blt_query_cap *query_cap)
{
	/* FIXME: Not implemented yet */
	return -ENOSYS;
}

/**
 * b2r2_blt_ioctl_us - This routine implements b2r2_blt ioctl interface
 *
 * @file: file pointer.
 * @cmd	:ioctl command.
 * @arg: input argument for ioctl.
 *
 * Returns 0 if OK else negative error code
 */
static long b2r2_blt_ioctl_us(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int handle = (int) file->private_data;

	b2r2_log_info(b2r2_blt->dev, "%s\n", __func__);

	/* Get the instance from the file structure */
	switch (cmd) {
	case B2R2_BLT_IOC: {
		/* arg is user pointer to struct b2r2_blt_request */
		ret = b2r2_blt_blit_internal(handle,
				(struct b2r2_blt_req *) arg, true);
		break;
	}

	case B2R2_BLT_SYNCH_IOC:
		/* arg is request_id */
		ret = b2r2_blt_synch(handle, (int) arg);
		break;

	case B2R2_BLT_QUERY_CAP_IOC: {
		/* Arg is struct b2r2_blt_query_cap */
		struct b2r2_blt_query_cap query_cap;
		struct b2r2_blt_data *blt_data = get_data(handle);

		/* Get the user data */
		if (copy_from_user(&query_cap, (void *)arg,
				sizeof(query_cap))) {
			b2r2_log_err(b2r2_blt->dev,
				"%s: copy_from_user failed\n",
				__func__);
			ret = -EFAULT;
			goto exit;
		}

		/* Fill in our capabilities */
		ret = b2r2_blt_query_cap(blt_data, &query_cap);

		/* Return data to user */
		if (copy_to_user((void *)arg, &query_cap,
				sizeof(query_cap))) {
			b2r2_log_err(b2r2_blt->dev,
				"%s: copy_to_user failed\n",
				__func__);
			ret = -EFAULT;
			goto exit;
		}
		break;
	}

	default:
		/* Unknown command */
		b2r2_log_err(b2r2_blt->dev, "%s: Unknown cmd %d\n",
			__func__, cmd);
		ret = -EINVAL;
		break;

	}

exit:
	if (ret < 0)
		b2r2_log_err(b2r2_blt->dev, "%s: Return with error %d!\n",
				__func__, -ret);

	return ret;
}

/**
 * b2r2_blt_poll - Support for user-space poll, select & epoll.
 *                 Used for user-space callback
 *
 * @filp: File to poll on
 * @wait: Poll table to wait on
 *
 * This function checks if there are anything to read
 */
static unsigned b2r2_blt_poll_us(struct file *filp, poll_table *wait)
{
	struct b2r2_blt_data *blt_data =
			(struct b2r2_blt_data *) filp->private_data;
	struct b2r2_control_instance *ctl[B2R2_MAX_NBR_DEVICES];
	unsigned int ret = POLLIN | POLLRDNORM;
	int n_poll = 0;
	int i;

	b2r2_log_info(b2r2_blt->dev, "%s\n", __func__);

	/* Get the b2r2 core controls for the job */
	get_control_instances(blt_data, ctl, B2R2_MAX_NBR_DEVICES, &n_poll);
	if (n_poll == 0) {
		b2r2_log_err(b2r2_blt->dev, "%s: No b2r2 cores available.\n",
			__func__);
		ret = -ENOSYS;
		goto exit;
	}

	/* Poll each core control instance */
	for (i = 0; i < n_poll && ret != 0; i++) {
		poll_wait(filp, &ctl[i]->report_list_waitq, wait);
		mutex_lock(&ctl[i]->lock);
		if (list_empty(&ctl[i]->report_list))
			ret = 0; /* No reports */
		mutex_unlock(&ctl[i]->lock);
	}

	/* Release the handles to the core controls */
	release_control_instances(ctl, n_poll);

exit:
	b2r2_log_info(b2r2_blt->dev, "%s: returns %d, n_poll: %d\n",
		__func__, ret, n_poll);

	return ret;
}

/**
 * b2r2_blt_read - Read report data, user for user-space callback
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static ssize_t b2r2_blt_read_us(struct file *filp,
		char __user *buf, size_t count,	loff_t *f_pos)
{
	int ret = 0;
	int n_read = 0;
	int i;
	int first_index = 0;
	struct b2r2_blt_report report;
	struct b2r2_blt_request *requests[B2R2_MAX_NBR_DEVICES];
	struct b2r2_blt_data *blt_data =
		(struct b2r2_blt_data *) filp->private_data;
	struct b2r2_control_instance *ctl[B2R2_MAX_NBR_DEVICES];
	struct b2r2_control_instance *first = NULL;
	bool block = ((filp->f_flags & O_NONBLOCK) == 0);
	u32 core_mask = 0;

	b2r2_log_info(b2r2_blt->dev, "%s\n", __func__);

	/*
	 * We return only complete report records, one at a time.
	 * Might be more efficient to support read of many.
	 */
	count = (count / sizeof(struct b2r2_blt_report)) *
		sizeof(struct b2r2_blt_report);
	if (count > sizeof(struct b2r2_blt_report))
		count = sizeof(struct b2r2_blt_report);
	if (count == 0)
		return count;

	memset(ctl, 0, sizeof(*ctl) * B2R2_MAX_NBR_DEVICES);
	/* Get the b2r2 core controls for the job */
	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		struct b2r2_control_instance *ci = blt_data->ctl_instace[i];
		if (ci) {
			struct b2r2_control *cont =
				b2r2_blt_get_control(ci->control_id);
			if (cont) {
				ctl[i] = ci;
				n_read++;
			}
		}
	}
	if (n_read == 0) {
		b2r2_log_err(b2r2_blt->dev, "%s: No b2r2 cores available.\n",
			__func__);
		return -ENOSYS;
	}

	/* Find which control to ask for a report first */
	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		if (ctl[i] != NULL) {
			first = ctl[i];
			first_index = i;
			break;
		}
	}
	if (!first) {
		b2r2_log_err(b2r2_blt->dev, "%s: Internal error.\n",
			__func__);
		return -ENOSYS;
	}

	memset(requests, 0, sizeof(*requests) * B2R2_MAX_NBR_DEVICES);
	/* Read report from core 0 */
	ret = b2r2_control_read(first, &requests[first_index], block);
	if (ret <= 0 || requests[0] == NULL) {
		b2r2_log_err(b2r2_blt->dev, "%s: b2r2_control_read failed.\n",
			__func__);
		ret = -EFAULT;
		goto exit;
	}
	core_mask = requests[first_index]->core_mask >> 1;
	core_mask &= ~(1 << first_index);

	/*
	 * If there are any more cores, try reading the report
	 * with the specific ID from the other cores
	 */
	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		if ((core_mask & 1) && (ctl[i] != NULL)) {
			/* TODO: Do we need to wait here? */
			ret = b2r2_control_read_id(ctl[i], &requests[i], block,
					requests[first_index]->request_id);
			if (ret <= 0 || requests[i] == NULL) {
				b2r2_log_err(b2r2_blt->dev,
					"%s: b2r2_control_read failed.\n",
					__func__);
				break;
			}
		}
		core_mask = core_mask >> 1;
	}

	if (ret > 0) {
		/* Construct a report and copy to userland */
		report.request_id = requests[0]->request_id;
		report.report1 = requests[0]->user_req.report1;
		report.report2 = requests[0]->user_req.report2;
		report.usec_elapsed = 0; /* TBD */

		if (copy_to_user(buf, &report, sizeof(report))) {
			b2r2_log_err(b2r2_blt->dev,
				"%s: copy_to_user failed.\n",
				__func__);
			ret = -EFAULT;
		}
	}

	if (ret > 0) {
		for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++)
			/*
			 * Release matching the addref when the job was put
			 * into the report list
			 */
			if (requests[i] != NULL)
				b2r2_core_job_release(&requests[i]->job,
						__func__);
	} else {
		/* We failed at one core or copy to user failed */
		for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++)
			if (requests[i] != NULL)
				list_add(&requests[i]->list,
					&ctl[i]->report_list);
		goto exit;
	}

	ret = count;

exit:
	/* Release the handles to the core controls */
	release_control_instances(ctl, n_read);

	return ret;
}

/**
 * b2r2_blt_fops - File operations for b2r2_blt
 */
static const struct file_operations b2r2_blt_fops = {
	.owner =          THIS_MODULE,
	.open =           b2r2_blt_open_us,
	.release =        b2r2_blt_release_us,
	.unlocked_ioctl = b2r2_blt_ioctl_us,
	.poll  =          b2r2_blt_poll_us,
	.read  =          b2r2_blt_read_us,
};


/**
 * b2r2_probe() - This routine loads the B2R2 core driver
 *
 * @pdev: platform device.
 */
static int b2r2_blt_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;

	BUG_ON(pdev == NULL);

	dev_info(&pdev->dev, "%s start.\n", __func__);

	if (!b2r2_blt) {
		b2r2_blt = kzalloc(sizeof(*b2r2_blt), GFP_KERNEL);
		if (!b2r2_blt) {
			dev_err(&pdev->dev, "b2r2_blt alloc failed\n");
			ret = -EINVAL;
			goto error_exit;
		}

		/* Init b2r2 core control reference counters */
		for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++)
			spin_lock_init(&b2r2_controls[i].lock);
	}

	mutex_init(&b2r2_blt->datas_lock);
	spin_lock_init(&b2r2_blt->lock);
	b2r2_blt->dev = &pdev->dev;

	/* Register b2r2 driver */
	b2r2_blt->miscdev.parent = b2r2_blt->dev;
	b2r2_blt->miscdev.minor = MISC_DYNAMIC_MINOR;
	b2r2_blt->miscdev.name = "b2r2_blt";
	b2r2_blt->miscdev.fops = &b2r2_blt_fops;

	ret = misc_register(&b2r2_blt->miscdev);
	if (ret != 0) {
		printk(KERN_WARNING "%s: registering misc device fails\n",
				__func__);
		goto error_exit;
	}

	b2r2_blt->dev = b2r2_blt->miscdev.this_device;
	b2r2_blt->dev->coherent_dma_mask = 0xFFFFFFFF;

	kref_init(&blt_refcount);

	dev_info(&pdev->dev, "%s done.\n", __func__);

	return ret;

/** Recover from any error if something fails */
error_exit:

	kfree(b2r2_blt);

	dev_info(&pdev->dev, "%s done with errors (%d).\n", __func__, ret);

	return ret;
}

/**
 * b2r2_blt_remove - This routine unloads b2r2_blt driver
 *
 * @pdev: platform device.
 */
static int b2r2_blt_remove(struct platform_device *pdev)
{
	BUG_ON(pdev == NULL);
	dev_info(&pdev->dev, "%s started.\n", __func__);
	misc_deregister(&b2r2_blt->miscdev);
	kref_put(&blt_refcount, b2r2_blt_release);
	return 0;
}

/**
 * b2r2_blt_suspend() - This routine puts the B2R2 blitter in to sustend state.
 * @pdev: platform device.
 *
 * This routine stores the current state of the b2r2 device and puts in to
 * suspend state.
 *
 */
int b2r2_blt_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

/**
 * b2r2_blt_resume() - This routine resumes the B2R2 blitter from sustend state.
 * @pdev: platform device.
 *
 * This routine restore back the current state of the b2r2 device resumes.
 *
 */
int b2r2_blt_resume(struct platform_device *pdev)
{
	return 0;
}

/**
 * struct platform_b2r2_driver - Platform driver configuration for the
 * B2R2 core driver
 */
static struct platform_driver platform_b2r2_blt_driver = {
	.remove = b2r2_blt_remove,
	.driver = {
		.name	= "b2r2_blt",
	},
	.suspend = b2r2_blt_suspend,
	.resume =  b2r2_blt_resume,
};

/**
 * b2r2_init() - Module init function for the B2R2 core module
 */
static int __init b2r2_blt_init(void)
{
	printk(KERN_INFO "%s\n", __func__);
	return platform_driver_probe(&platform_b2r2_blt_driver, b2r2_blt_probe);
}
module_init(b2r2_blt_init);

/**
 * b2r2_exit() - Module exit function for the B2R2 core module
 */
static void __exit b2r2_blt_exit(void)
{
	printk(KERN_INFO "%s\n", __func__);
	platform_driver_unregister(&platform_b2r2_blt_driver);
	return;
}
module_exit(b2r2_blt_exit);

MODULE_AUTHOR("Robert Fekete <robert.fekete@stericsson.com>");
MODULE_DESCRIPTION("ST-Ericsson B2R2 Blitter module");
MODULE_LICENSE("GPL");
