/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 Blitter module
 *
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
#include <linux/ktime.h>

#include "b2r2_internal.h"
#include "b2r2_control.h"
#include "b2r2_node_split.h"
#include "b2r2_generic.h"
#include "b2r2_mem_alloc.h"
#include "b2r2_profiler_socket.h"
#include "b2r2_timing.h"
#include "b2r2_debug.h"
#include "b2r2_utils.h"
#include "b2r2_input_validation.h"
#include "b2r2_core.h"
#include "b2r2_filters.h"

#define B2R2_HEAP_SIZE (4 * PAGE_SIZE)
#define MAX_TMP_BUF_SIZE (128 * PAGE_SIZE)
#define CHARS_IN_REQ (2048)

/*
 * TODO:
 * Implementation of query cap
 * Support for user space virtual pointer to physically consecutive memory
 * Support for user space virtual pointer to physically scattered memory
 * Callback reads lagging behind in blt_api_stress app
 * Store smaller items in the report list instead of the whole request
 * Support read of many report records at once.
 */

/* Local functions */
static void inc_stat(struct b2r2_control *cont, unsigned long *stat);
static void dec_stat(struct b2r2_control *cont, unsigned long *stat);

#ifndef CONFIG_B2R2_GENERIC_ONLY
static void job_callback(struct b2r2_core_job *job);
static void job_release(struct b2r2_core_job *job);
static int job_acquire_resources(struct b2r2_core_job *job, bool atomic);
static void job_release_resources(struct b2r2_core_job *job, bool atomic);
#endif

#ifdef CONFIG_B2R2_GENERIC
static void job_callback_gen(struct b2r2_core_job *job);
static void job_release_gen(struct b2r2_core_job *job);
static int job_acquire_resources_gen(struct b2r2_core_job *job, bool atomic);
static void job_release_resources_gen(struct b2r2_core_job *job, bool atomic);
static void tile_job_callback_gen(struct b2r2_core_job *job);
static void tile_job_release_gen(struct b2r2_core_job *job);
#endif


static int resolve_buf(struct b2r2_control *cont,
		struct b2r2_blt_img *img, struct b2r2_blt_rect *rect_2b_used,
		bool is_dst, struct b2r2_resolved_buf *resolved);
static void unresolve_buf(struct b2r2_control *cont,
		struct b2r2_blt_buf *buf, struct b2r2_resolved_buf *resolved);
static void sync_buf(struct b2r2_control *cont, struct b2r2_blt_img *img,
		struct b2r2_resolved_buf *resolved, bool is_dst,
		struct b2r2_blt_rect *rect);
static bool is_report_list_empty(struct b2r2_control_instance *instance);
static bool is_synching(struct b2r2_control_instance *instance);
static void get_actual_dst_rect(struct b2r2_blt_req *req,
		struct b2r2_blt_rect *actual_dst_rect);
static void set_up_hwmem_region(struct b2r2_control *cont,
		struct b2r2_blt_img *img, struct b2r2_blt_rect *rect,
		struct hwmem_region *region);
static int resolve_hwmem(struct b2r2_control *cont, struct b2r2_blt_img *img,
		struct b2r2_blt_rect *rect_2b_used, bool is_dst,
		struct b2r2_resolved_buf *resolved_buf);
static void unresolve_hwmem(struct b2r2_resolved_buf *resolved_buf);

/**
 * struct sync_args - Data for clean/flush
 *
 * @start: Virtual start address
 * @end: Virtual end address
 */
struct sync_args {
	unsigned long start;
	unsigned long end;
};
/**
 * flush_l1_cache_range_curr_cpu() - Cleans and invalidates L1 cache on the
 * current CPU
 *
 * @arg: Pointer to sync_args structure
 */
static inline void flush_l1_cache_range_curr_cpu(void *arg)
{
	struct sync_args *sa = (struct sync_args *)arg;

	dmac_flush_range((void *)sa->start, (void *)sa->end);
}

#ifdef CONFIG_SMP
/**
 * inv_l1_cache_range_all_cpus() - Cleans and invalidates L1 cache on all CPU:s
 *
 * @sa: Pointer to sync_args structure
 */
static void flush_l1_cache_range_all_cpus(struct sync_args *sa)
{
	on_each_cpu(flush_l1_cache_range_curr_cpu, sa, 1);
}
#endif

/**
 * clean_l1_cache_range_curr_cpu() - Cleans L1 cache on current CPU
 *
 * Ensures that data is written out from the CPU:s L1 cache,
 * it will still be in the cache.
 *
 * @arg: Pointer to sync_args structure
 */
static inline void clean_l1_cache_range_curr_cpu(void *arg)
{
	struct sync_args *sa = (struct sync_args *)arg;

	dmac_map_area((void *)sa->start,
		(void *)sa->end - (void *)sa->start,
		DMA_TO_DEVICE);
}

#ifdef CONFIG_SMP
/**
 * clean_l1_cache_range_all_cpus() - Cleans L1 cache on all CPU:s
 *
 * Ensures that data is written out from all CPU:s L1 cache,
 * it will still be in the cache.
 *
 * @sa: Pointer to sync_args structure
 */
static void clean_l1_cache_range_all_cpus(struct sync_args *sa)
{
	on_each_cpu(clean_l1_cache_range_curr_cpu, sa, 1);
}
#endif

/**
 * b2r2_blt_open - Implements file open on the b2r2_blt device
 *
 * @inode: File system inode
 * @filp: File pointer
 *
 * A B2R2 BLT instance is created and stored in the file structure.
 */
int b2r2_control_open(struct b2r2_control_instance *instance)
{
	int ret = 0;
	struct b2r2_control *cont = instance->control;

	b2r2_log_info(cont->dev, "%s\n", __func__);
	inc_stat(cont, &cont->stat_n_in_open);

	INIT_LIST_HEAD(&instance->report_list);
	mutex_init(&instance->lock);
	init_waitqueue_head(&instance->report_list_waitq);
	init_waitqueue_head(&instance->synch_done_waitq);
	dec_stat(cont, &cont->stat_n_in_open);

	return ret;
}

/**
 * b2r2_blt_release - Implements last close on an instance of
 *                    the b2r2_blt device
 *
 * @inode: File system inode
 * @filp: File pointer
 *
 * All active jobs are finished or cancelled and allocated data
 * is released.
 */
int b2r2_control_release(struct b2r2_control_instance *instance)
{
	int ret;
	struct b2r2_control *cont = instance->control;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	inc_stat(cont, &cont->stat_n_in_release);

	/* Finish all outstanding requests */
	ret = b2r2_control_synch(instance, 0);
	if (ret < 0)
		b2r2_log_warn(cont->dev, "%s: b2r2_blt_sync failed with %d\n",
			__func__, ret);

	/* Now cancel any remaining outstanding request */
	if (instance->no_of_active_requests) {
		struct b2r2_core_job *job;

		b2r2_log_warn(cont->dev, "%s: %d active requests\n", __func__,
			instance->no_of_active_requests);

		/* Find and cancel all jobs belonging to us */
		job = b2r2_core_job_find_first_with_tag(cont,
			(int) instance);
		while (job) {
			b2r2_core_job_cancel(job);
			/* Matches addref in b2r2_core_job_find... */
			b2r2_core_job_release(job, __func__);
			job = b2r2_core_job_find_first_with_tag(cont,
				(int) instance);
		}

		b2r2_log_warn(cont->dev, "%s: %d active requests after "
			"cancel\n", __func__, instance->no_of_active_requests);
	}

	/* Release jobs in report list */
	mutex_lock(&instance->lock);
	while (!list_empty(&instance->report_list)) {
		struct b2r2_blt_request *request = list_first_entry(
			&instance->report_list,
			struct b2r2_blt_request,
			list);
		list_del_init(&request->list);
		mutex_unlock(&instance->lock);
		/*
		 * This release matches the addref when the job was put into
		 * the report list
		 */
		b2r2_core_job_release(&request->job, __func__);
		mutex_lock(&instance->lock);
	}
	mutex_unlock(&instance->lock);

	dec_stat(cont, &cont->stat_n_in_release);

	return 0;
}

size_t b2r2_control_read(struct b2r2_control_instance *instance,
		struct b2r2_blt_request **request_out, bool block)
{
	struct b2r2_blt_request *request = NULL;
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_control *cont = instance->control;
#endif

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/*
	 * Loop and wait here until we have anything to return or
	 * until interrupted
	 */
	mutex_lock(&instance->lock);
	while (list_empty(&instance->report_list)) {
		mutex_unlock(&instance->lock);

		/* Return if non blocking read */
		if (!block)
			return -EAGAIN;

		b2r2_log_info(cont->dev, "%s - Going to sleep\n", __func__);
		if (wait_event_interruptible(
				instance->report_list_waitq,
				!is_report_list_empty(instance)))
			/* signal: tell the fs layer to handle it */
			return -ERESTARTSYS;

		/* Otherwise loop, but first reaquire the lock */
		mutex_lock(&instance->lock);
	}

	if (!list_empty(&instance->report_list))
		request = list_first_entry(
			&instance->report_list, struct b2r2_blt_request, list);

	if (request) {
		/* Remove from list to avoid reading twice */
		list_del_init(&request->list);

		*request_out = request;
	}
	mutex_unlock(&instance->lock);

	if (request)
		return 1;

	/* No report returned */
	return 0;
}

size_t b2r2_control_read_id(struct b2r2_control_instance *instance,
		struct b2r2_blt_request **request_out, bool block,
		int request_id)
{
	struct b2r2_blt_request *request = NULL;
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_control *cont = instance->control;
#endif

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/*
	 * Loop and wait here until we have anything to return or
	 * until interrupted
	 */
	mutex_lock(&instance->lock);
	while (list_empty(&instance->report_list)) {
		mutex_unlock(&instance->lock);

		/* Return if non blocking read */
		if (!block)
			return -EAGAIN;

		b2r2_log_info(cont->dev, "%s - Going to sleep\n", __func__);
		if (wait_event_interruptible(
				instance->report_list_waitq,
				!is_report_list_empty(instance)))
			/* signal: tell the fs layer to handle it */
			return -ERESTARTSYS;

		/* Otherwise loop, but first reaquire the lock */
		mutex_lock(&instance->lock);
	}

	if (!list_empty(&instance->report_list)) {
		struct b2r2_blt_request *pos;
		list_for_each_entry(pos, &instance->report_list, list) {
			if (pos->request_id)
				request = pos;
		}
	}

	if (request) {
		/* Remove from list to avoid reading twice */
		list_del_init(&request->list);
		*request_out = request;
	}
	mutex_unlock(&instance->lock);

	if (request)
		return 1;

	/* No report returned */
	return 0;
}

#ifndef CONFIG_B2R2_GENERIC_ONLY
/**
 * b2r2_blt - Implementation of the B2R2 blit request
 *
 * @instance: The B2R2 BLT instance
 * @request; The request to perform
 */
int b2r2_control_blt(struct b2r2_blt_request *request)
{
	int ret = 0;
	struct b2r2_blt_rect actual_dst_rect;
	int request_id = 0;
	struct b2r2_node *last_node = request->first_node;
	int node_count;
	struct b2r2_control_instance *instance = request->instance;
	struct b2r2_control *cont = instance->control;

	unsigned long long thread_runtime_at_start = 0;

	if (request->profile) {
		ktime_get_ts(&request->ts_start);
		thread_runtime_at_start = task_sched_runtime(current);
	}

	b2r2_log_info(cont->dev, "%s\n", __func__);

	inc_stat(cont, &cont->stat_n_in_blt);

	/* Debug prints of incoming request */
	b2r2_log_info(cont->dev,
		"src.fmt=%#010x src.buf={%d,%d,%d} "
		"src.w,h={%d,%d} src.rect={%d,%d,%d,%d}\n",
		request->user_req.src_img.fmt,
		request->user_req.src_img.buf.type,
		request->user_req.src_img.buf.fd,
		request->user_req.src_img.buf.offset,
		request->user_req.src_img.width,
		request->user_req.src_img.height,
		request->user_req.src_rect.x,
		request->user_req.src_rect.y,
		request->user_req.src_rect.width,
		request->user_req.src_rect.height);

	if (request->user_req.flags & B2R2_BLT_FLAG_BG_BLEND)
		b2r2_log_info(cont->dev,
			"bg.fmt=%#010x bg.buf={%d,%d,%d} "
			"bg.w,h={%d,%d} bg.rect={%d,%d,%d,%d}\n",
			request->user_req.bg_img.fmt,
			request->user_req.bg_img.buf.type,
			request->user_req.bg_img.buf.fd,
			request->user_req.bg_img.buf.offset,
			request->user_req.bg_img.width,
			request->user_req.bg_img.height,
			request->user_req.bg_rect.x,
			request->user_req.bg_rect.y,
			request->user_req.bg_rect.width,
			request->user_req.bg_rect.height);

	b2r2_log_info(cont->dev,
		"dst.fmt=%#010x dst.buf={%d,%d,%d} "
		"dst.w,h={%d,%d} dst.rect={%d,%d,%d,%d}\n",
		request->user_req.dst_img.fmt,
		request->user_req.dst_img.buf.type,
		request->user_req.dst_img.buf.fd,
		request->user_req.dst_img.buf.offset,
		request->user_req.dst_img.width,
		request->user_req.dst_img.height,
		request->user_req.dst_rect.x,
		request->user_req.dst_rect.y,
		request->user_req.dst_rect.width,
		request->user_req.dst_rect.height);

	inc_stat(cont, &cont->stat_n_in_blt_synch);

	/* Wait here if synch is ongoing */
	ret = wait_event_interruptible(instance->synch_done_waitq,
			!is_synching(instance));
	if (ret) {
		b2r2_log_warn(cont->dev, "%s: Sync wait interrupted, %d\n",
			__func__, ret);
		ret = -EAGAIN;
		dec_stat(cont, &cont->stat_n_in_blt_synch);
		goto synch_interrupted;
	}

	dec_stat(cont, &cont->stat_n_in_blt_synch);

	/* Resolve the buffers */

	/* Source buffer */
	ret = resolve_buf(cont, &request->user_req.src_img,
		&request->user_req.src_rect,
		false, &request->src_resolved);
	if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Resolve src buf failed, %d\n",
				__func__, ret);
		ret = -EAGAIN;
		goto resolve_src_buf_failed;
	}

	/* Background buffer */
	if (request->user_req.flags & B2R2_BLT_FLAG_BG_BLEND) {
		ret = resolve_buf(cont, &request->user_req.bg_img,
			&request->user_req.bg_rect,
			false, &request->bg_resolved);
		if (ret < 0) {
			b2r2_log_warn(cont->dev, "%s: Resolve bg buf failed,"
				" %d\n", __func__, ret);
			ret = -EAGAIN;
			goto resolve_bg_buf_failed;
		}
	}

	/* Source mask buffer */
	ret = resolve_buf(cont, &request->user_req.src_mask,
			&request->user_req.src_rect, false,
			&request->src_mask_resolved);
	if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Resolve src mask buf failed,"
			" %d\n", __func__, ret);
		ret = -EAGAIN;
		goto resolve_src_mask_buf_failed;
	}

	/* Destination buffer */
	get_actual_dst_rect(&request->user_req, &actual_dst_rect);
	ret = resolve_buf(cont, &request->user_req.dst_img, &actual_dst_rect,
		true, &request->dst_resolved);
	if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Resolve dst buf failed, %d\n",
			__func__, ret);
		ret = -EAGAIN;
		goto resolve_dst_buf_failed;
	}

	/* Debug prints of resolved buffers */
	b2r2_log_info(cont->dev, "src.rbuf={%X,%p,%d} {%p,%X,%X,%d}\n",
		request->src_resolved.physical_address,
		request->src_resolved.virtual_address,
		request->src_resolved.is_pmem,
		request->src_resolved.filep,
		request->src_resolved.file_physical_start,
		request->src_resolved.file_virtual_start,
		request->src_resolved.file_len);

	if (request->user_req.flags & B2R2_BLT_FLAG_BG_BLEND)
		b2r2_log_info(cont->dev, "bg.rbuf={%X,%p,%d} {%p,%X,%X,%d}\n",
			request->bg_resolved.physical_address,
			request->bg_resolved.virtual_address,
			request->bg_resolved.is_pmem,
			request->bg_resolved.filep,
			request->bg_resolved.file_physical_start,
			request->bg_resolved.file_virtual_start,
			request->bg_resolved.file_len);

	b2r2_log_info(cont->dev, "dst.rbuf={%X,%p,%d} {%p,%X,%X,%d}\n",
		request->dst_resolved.physical_address,
		request->dst_resolved.virtual_address,
		request->dst_resolved.is_pmem,
		request->dst_resolved.filep,
		request->dst_resolved.file_physical_start,
		request->dst_resolved.file_virtual_start,
		request->dst_resolved.file_len);

	/* Calculate the number of nodes (and resources) needed for this job */
	ret = b2r2_node_split_analyze(request, MAX_TMP_BUF_SIZE, &node_count,
		&request->bufs, &request->buf_count,
		&request->node_split_job);
	if (ret == -ENOSYS) {
		/* There was no optimized path for this request */
		b2r2_log_info(cont->dev, "%s: No optimized path for request\n",
			__func__);
		goto no_optimized_path;

	} else if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Failed to analyze request,"
			" ret = %d\n", __func__, ret);
#ifdef CONFIG_DEBUG_FS
		{
			/* Failed, dump job to dmesg */
			char *Buf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);

			b2r2_log_info(cont->dev, "%s: Analyze failed for:\n",
				__func__);
			if (Buf != NULL) {
				sprintf_req(request, Buf, sizeof(char) * 4096);
				b2r2_log_info(cont->dev, "%s", Buf);
				kfree(Buf);
			} else {
				b2r2_log_info(cont->dev, "Unable to print the"
					" request. Message buffer"
					" allocation failed.\n");
			}
		}
#endif
		goto generate_nodes_failed;
	}

	/* Allocate the nodes needed */
#ifdef B2R2_USE_NODE_GEN
	request->first_node = b2r2_blt_alloc_nodes(cont,
		node_count);
	if (request->first_node == NULL) {
		b2r2_log_warn(cont->dev, "%s: Failed to allocate nodes,"
			" ret = %d\n", __func__, ret);
		goto generate_nodes_failed;
	}
#else
	ret = b2r2_node_alloc(cont, node_count, &(request->first_node));
	if (ret < 0 || request->first_node == NULL) {
		b2r2_log_warn(cont->dev,
			"%s: Failed to allocate nodes, ret = %d\n",
			__func__, ret);
		goto generate_nodes_failed;
	}
#endif

	/* Build the B2R2 node list */
	ret = b2r2_node_split_configure(cont, &request->node_split_job,
			request->first_node);

	if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s:"
			" Failed to perform node split, ret = %d\n",
			__func__, ret);
		goto generate_nodes_failed;
	}

	/*
	 * Exit here if dry run or if we choose to
	 * omit blit jobs through debugfs
	 */
	if (request->user_req.flags & B2R2_BLT_FLAG_DRY_RUN || cont->bypass)
		goto exit_dry_run;

	/* Configure the request */
	last_node = request->first_node;
	while (last_node && last_node->next)
		last_node = last_node->next;

	request->job.tag = (int) instance;
	request->job.data = (int) cont->data;
	request->job.prio = request->user_req.prio;
	request->job.first_node_address =
		request->first_node->physical_address;
	request->job.last_node_address =
		last_node->physical_address;
	request->job.callback = job_callback;
	request->job.release = job_release;
	request->job.acquire_resources = job_acquire_resources;
	request->job.release_resources = job_release_resources;

	/* Synchronize memory occupied by the buffers */

	/* Source buffer */
	if (!(request->user_req.flags &
				B2R2_BLT_FLAG_SRC_NO_CACHE_FLUSH) &&
			(request->user_req.src_img.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.src_img.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.src_img,
			&request->src_resolved, false,
			&request->user_req.src_rect);

	/* Background buffer */
	if ((request->user_req.flags & B2R2_BLT_FLAG_BG_BLEND) &&
			!(request->user_req.flags &
				B2R2_BLT_FLAG_BG_NO_CACHE_FLUSH) &&
			(request->user_req.bg_img.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.bg_img.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.bg_img,
			&request->bg_resolved, false,
			&request->user_req.bg_rect);

	/* Source mask buffer */
	if (!(request->user_req.flags &
				B2R2_BLT_FLAG_SRC_MASK_NO_CACHE_FLUSH) &&
			(request->user_req.src_mask.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.src_mask.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.src_mask,
			&request->src_mask_resolved, false, NULL);

	/* Destination buffer */
	if (!(request->user_req.flags &
				B2R2_BLT_FLAG_DST_NO_CACHE_FLUSH) &&
			(request->user_req.dst_img.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.dst_img.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.dst_img,
			&request->dst_resolved, true,
			&request->user_req.dst_rect);

#ifdef CONFIG_DEBUG_FS
	/* Remember latest request for debugfs */
	mutex_lock(&cont->last_req_lock);
	cont->latest_request[cont->buf_index] = *request;

	/* Calculate, buf_index = (buf_index + 1) % last_request_count */
	cont->buf_index++;
	if (cont->buf_index >= cont->last_request_count)
		cont->buf_index = 0;
	mutex_unlock(&cont->last_req_lock);
#endif

	/* Submit the job */
	b2r2_log_info(cont->dev, "%s: Submitting job\n", __func__);

	inc_stat(cont, &cont->stat_n_in_blt_add);

	if (request->profile)
		request->nsec_active_in_cpu =
			(s64)(task_sched_runtime(current) -
					thread_runtime_at_start);

	mutex_lock(&instance->lock);

	/* Add the job to b2r2_core */
	request_id = b2r2_core_job_add(cont, &request->job);
	request->request_id = request_id;

	dec_stat(cont, &cont->stat_n_in_blt_add);

	if (request_id < 0) {
		b2r2_log_warn(cont->dev, "%s: Failed to add job, ret = %d\n",
			__func__, request_id);
		ret = request_id;
		mutex_unlock(&instance->lock);
		goto job_add_failed;
	}

	inc_stat(cont, &cont->stat_n_jobs_added);

	instance->no_of_active_requests++;
	mutex_unlock(&instance->lock);

	return ret >= 0 ? request_id : ret;

job_add_failed:
exit_dry_run:
no_optimized_path:
generate_nodes_failed:
	unresolve_buf(cont, &request->user_req.dst_img.buf,
		&request->dst_resolved);
resolve_dst_buf_failed:
	unresolve_buf(cont, &request->user_req.src_mask.buf,
		&request->src_mask_resolved);
resolve_src_mask_buf_failed:
	if (request->user_req.flags & B2R2_BLT_FLAG_BG_BLEND)
		unresolve_buf(cont, &request->user_req.bg_img.buf,
				&request->bg_resolved);
resolve_bg_buf_failed:
	unresolve_buf(cont, &request->user_req.src_img.buf,
		&request->src_resolved);
resolve_src_buf_failed:
synch_interrupted:
	if (((request->user_req.flags & B2R2_BLT_FLAG_DRY_RUN) == 0 ||
			cont->bypass) && (ret != 0))
		b2r2_log_warn(cont->dev, "%s returns with error %d\n",
			__func__, ret);
	job_release(&request->job);
	dec_stat(cont, &cont->stat_n_jobs_released);

	dec_stat(cont, &cont->stat_n_in_blt);

	return ret;
}

int b2r2_control_waitjob(struct b2r2_blt_request *request)
{
	int ret = 0;
	struct b2r2_control_instance *instance = request->instance;
	struct b2r2_control *cont = instance->control;

	/* Wait for the job to be done if synchronous */
	if ((request->user_req.flags & B2R2_BLT_FLAG_ASYNCH) == 0) {
		b2r2_log_info(cont->dev, "%s: Synchronous, waiting\n",
			__func__);

		inc_stat(cont, &cont->stat_n_in_blt_wait);

		ret = b2r2_core_job_wait(&request->job);

		dec_stat(cont, &cont->stat_n_in_blt_wait);

		if (ret < 0 && ret != -ENOENT)
			b2r2_log_warn(cont->dev, "%s: Failed to wait job,"
				" ret = %d\n", __func__, ret);
		else
			b2r2_log_info(cont->dev, "%s: Synchronous wait done\n",
				__func__);
	}

	/*
	 * Release matching the addref in b2r2_core_job_add,
	 * the request must not be accessed after this call
	 */
	b2r2_core_job_release(&request->job, __func__);
	dec_stat(cont, &cont->stat_n_in_blt);

	return ret;
}

/**
 * Called when job is done or cancelled
 *
 * @job: The job
 */
static void job_callback(struct b2r2_core_job *job)
{
	struct b2r2_blt_request *request = NULL;
	struct b2r2_core *core = NULL;
	struct b2r2_control *cont = NULL;

	request = container_of(job, struct b2r2_blt_request, job);
	core = (struct b2r2_core *) job->data;
	cont = core->control;

	if (cont->dev)
		b2r2_log_info(cont->dev, "%s\n", __func__);

	/* Local addref / release within this func */
	b2r2_core_job_addref(job, __func__);

	b2r2_debug_buffers_unresolve(cont, request);

	/* Unresolve the buffers */
	unresolve_buf(cont, &request->user_req.src_img.buf,
		&request->src_resolved);
	unresolve_buf(cont, &request->user_req.src_mask.buf,
		&request->src_mask_resolved);
	unresolve_buf(cont, &request->user_req.dst_img.buf,
		&request->dst_resolved);
	if (request->user_req.flags & B2R2_BLT_FLAG_BG_BLEND)
		unresolve_buf(cont, &request->user_req.bg_img.buf,
			&request->bg_resolved);

	/* Move to report list if the job shall be reported */
	/* FIXME: Use a smaller struct? */
	/*
	 * TODO: In the case of kernel API call, feed an asynch task to the
	 * instance worker (kthread) instead of polling for a report
	 */
	mutex_lock(&request->instance->lock);
	if (request->user_req.flags & B2R2_BLT_FLAG_REPORT_WHEN_DONE) {
		/* Move job to report list */
		list_add_tail(&request->list,
			&request->instance->report_list);
		inc_stat(cont, &cont->stat_n_jobs_in_report_list);

		/* Wake up poll */
		wake_up_interruptible(
			&request->instance->report_list_waitq);

		/* Add a reference because we put the job in the report list */
		b2r2_core_job_addref(job, __func__);
	}

	/*
	 * Decrease number of active requests and wake up
	 * synching threads if active requests reaches zero
	 */
	BUG_ON(request->instance->no_of_active_requests == 0);
	request->instance->no_of_active_requests--;
	if (request->instance->synching &&
			request->instance->no_of_active_requests == 0) {
		request->instance->synching = false;
		/* Wake up all syncing */

		wake_up_interruptible_all(
			&request->instance->synch_done_waitq);
	}
	mutex_unlock(&request->instance->lock);

#ifdef CONFIG_DEBUG_FS
	/* Dump job if cancelled */
	if (job->job_state == B2R2_CORE_JOB_CANCELED) {
		char *Buf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);

		b2r2_log_info(cont->dev, "%s: Job cancelled:\n", __func__);
		if (Buf != NULL) {
			sprintf_req(request, Buf, sizeof(char) * 4096);
			b2r2_log_info(cont->dev, "%s", Buf);
			kfree(Buf);
		} else {
			b2r2_log_info(cont->dev, "Unable to print the request."
				" Message buffer allocation failed.\n");
		}
	}
#endif

	if (request->profile) {
		struct timespec ts_stop;
		struct timespec ts_diff;
		ktime_get_ts(&ts_stop);
		ts_diff = timespec_sub(ts_stop, request->ts_start);
		request->total_time_nsec = timespec_to_ns(&ts_diff);
		b2r2_call_profiler_blt_done(request);
	}

	/* Local addref / release within this func */
	b2r2_core_job_release(job, __func__);
}

/**
 * Called when job should be released (free memory etc.)
 *
 * @job: The job
 */
static void job_release(struct b2r2_core_job *job)
{
	struct b2r2_blt_request *request = NULL;
	struct b2r2_core *core = NULL;
	struct b2r2_control *cont = NULL;

	request = container_of(job, struct b2r2_blt_request, job);
	core = (struct b2r2_core *) job->data;
	cont = core->control;

	inc_stat(cont, &cont->stat_n_jobs_released);

	b2r2_log_info(cont->dev, "%s, first_node=%p, ref_count=%d\n",
		__func__, request->first_node, request->job.ref_count);

	b2r2_node_split_cancel(cont, &request->node_split_job);

	if (request->first_node) {
		b2r2_debug_job_done(cont, request->first_node);
#ifdef B2R2_USE_NODE_GEN
		b2r2_blt_free_nodes(cont, request->first_node);
#else
		b2r2_node_free(cont, request->first_node);
#endif
	}

	/* Release memory for the request */
	if (request->clut != NULL) {
		dma_free_coherent(cont->dev, CLUT_SIZE, request->clut,
				request->clut_phys_addr);
		request->clut = NULL;
		request->clut_phys_addr = 0;
	}
	kfree(request);
}

/**
 * Tells the job to try to allocate the resources needed to execute the job.
 * Called just before execution of a job.
 *
 * @job: The job
 * @atomic: true if called from atomic (i.e. interrupt) context. If function
 *          can't allocate in atomic context it should return error, it
 *          will then be called later from non-atomic context.
 */
static int job_acquire_resources(struct b2r2_core_job *job, bool atomic)
{
	struct b2r2_blt_request *request =
		container_of(job, struct b2r2_blt_request, job);
	struct b2r2_core *core = (struct b2r2_core *) job->data;
	struct b2r2_control *cont = core->control;
	int ret;
	int i;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	if (request->buf_count == 0)
		return 0;

	if (request->buf_count > MAX_TMP_BUFS_NEEDED) {
		b2r2_log_err(cont->dev,
				"%s: request->buf_count > MAX_TMP_BUFS_NEEDED\n",
				__func__);
		return -ENOMSG;
	}

	/*
	 * 1 to 1 mapping between request temp buffers and temp buffers
	 * (request temp buf 0 is always temp buf 0, request temp buf 1 is
	 * always temp buf 1 and so on) to avoid starvation of jobs that
	 * require multiple temp buffers. Not optimal in terms of memory
	 * usage but we avoid get into a situation where lower prio jobs can
	 * delay higher prio jobs that require more temp buffers.
	 */
	if (cont->tmp_bufs[0].in_use)
		return -EAGAIN;

	for (i = 0; i < request->buf_count; i++) {
		if (cont->tmp_bufs[i].buf.size < request->bufs[i].size) {
			b2r2_log_err(cont->dev, "%s: "
					"cont->tmp_bufs[i].buf.size < "
					"request->bufs[i].size\n", __func__);
			ret = -ENOMSG;
			goto error;
		}

		cont->tmp_bufs[i].in_use = true;
		request->bufs[i].phys_addr = cont->tmp_bufs[i].buf.phys_addr;
		request->bufs[i].virt_addr = cont->tmp_bufs[i].buf.virt_addr;

		b2r2_log_info(cont->dev, "%s: phys=%p, virt=%p\n",
				__func__, (void *)request->bufs[i].phys_addr,
				request->bufs[i].virt_addr);

		ret = b2r2_node_split_assign_buffers(cont,
				&request->node_split_job,
				request->first_node, request->bufs,
				request->buf_count);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	for (i = 0; i < request->buf_count; i++)
		cont->tmp_bufs[i].in_use = false;

	return ret;
}

/**
 * Tells the job to free the resources needed to execute the job.
 * Called after execution of a job.
 *
 * @job: The job
 * @atomic: true if called from atomic (i.e. interrupt) context. If function
 *          can't allocate in atomic context it should return error, it
 *          will then be called later from non-atomic context.
 */
static void job_release_resources(struct b2r2_core_job *job, bool atomic)
{
	struct b2r2_blt_request *request =
		container_of(job, struct b2r2_blt_request, job);
	struct b2r2_core *core = (struct b2r2_core *) job->data;
	struct b2r2_control *cont = core->control;
	int i;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/* Free any temporary buffers */
	for (i = 0; i < request->buf_count; i++) {

		b2r2_log_info(cont->dev, "%s: freeing %d bytes\n",
				__func__, request->bufs[i].size);
		cont->tmp_bufs[i].in_use = false;
		memset(&request->bufs[i], 0, sizeof(request->bufs[i]));
	}
	request->buf_count = 0;

	/*
	 * Early release of nodes
	 * FIXME: If nodes are to be reused we don't want to release here
	 */
	if (!atomic && request->first_node) {
		b2r2_debug_job_done(cont, request->first_node);

#ifdef B2R2_USE_NODE_GEN
		b2r2_blt_free_nodes(cont, request->first_node);
#else
		b2r2_node_free(cont, request->first_node);
#endif
		request->first_node = NULL;
	}
}

#endif /* !CONFIG_B2R2_GENERIC_ONLY */

#ifdef CONFIG_B2R2_GENERIC
/**
 * Called when job for one tile is done or cancelled
 * in the generic path.
 *
 * @job: The job
 */
static void tile_job_callback_gen(struct b2r2_core_job *job)
{
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_core *core =
			(struct b2r2_core *) job->data;
	struct b2r2_control *cont = core->control;
#endif

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/* Local addref / release within this func */
	b2r2_core_job_addref(job, __func__);

#ifdef CONFIG_DEBUG_FS
	/* Notify if a tile job is cancelled */
	if (job->job_state == B2R2_CORE_JOB_CANCELED)
		b2r2_log_info(cont->dev, "%s: Tile job cancelled:\n",
				__func__);
#endif

	/* Local addref / release within this func */
	b2r2_core_job_release(job, __func__);
}

/**
 * Called when job is done or cancelled.
 * Used for the last tile in the generic path
 * to notify waiting clients.
 *
 * @job: The job
 */
static void job_callback_gen(struct b2r2_core_job *job)
{
	struct b2r2_blt_request *request =
		container_of(job, struct b2r2_blt_request, job);
	struct b2r2_core *core = (struct b2r2_core *) job->data;
	struct b2r2_control *cont = core->control;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/* Local addref / release within this func */
	b2r2_core_job_addref(job, __func__);

	b2r2_debug_buffers_unresolve(cont, request);

	/* Unresolve the buffers */
	unresolve_buf(cont, &request->user_req.src_img.buf,
		&request->src_resolved);
	unresolve_buf(cont, &request->user_req.src_mask.buf,
		&request->src_mask_resolved);
	unresolve_buf(cont, &request->user_req.dst_img.buf,
		&request->dst_resolved);

	/* Move to report list if the job shall be reported */
	/* FIXME: Use a smaller struct? */
	/*
	 * TODO: In the case of kernel API call, feed an asynch task to the
	 * instance worker (kthread) instead of polling for a report
	 */
	mutex_lock(&request->instance->lock);
	if (request->user_req.flags & B2R2_BLT_FLAG_REPORT_WHEN_DONE) {
		/* Move job to report list */
		list_add_tail(&request->list,
			&request->instance->report_list);
		inc_stat(cont, &cont->stat_n_jobs_in_report_list);

		/* Wake up poll */
		wake_up_interruptible(
			&request->instance->report_list_waitq);

		/*
		 * Add a reference because we put the
		 * job in the report list
		 */
		b2r2_core_job_addref(job, __func__);
	}

	/*
	 * Decrease number of active requests and wake up
	 * synching threads if active requests reaches zero
	 */
	BUG_ON(request->instance->no_of_active_requests == 0);
	request->instance->no_of_active_requests--;
	if (request->instance->synching &&
			request->instance->no_of_active_requests == 0) {
		request->instance->synching = false;
		/* Wake up all syncing */

		wake_up_interruptible_all(
			&request->instance->synch_done_waitq);
	}
	mutex_unlock(&request->instance->lock);

#ifdef CONFIG_DEBUG_FS
	/* Dump job if cancelled */
	if (job->job_state == B2R2_CORE_JOB_CANCELED) {
		char *Buf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);

		b2r2_log_info(cont->dev, "%s: Job cancelled:\n", __func__);
		if (Buf != NULL) {
			sprintf_req(request, Buf, sizeof(char) * 4096);
			b2r2_log_info(cont->dev, "%s", Buf);
			kfree(Buf);
		} else {
			b2r2_log_info(cont->dev, "Unable to print the request."
				" Message buffer allocation failed.\n");
		}
	}
#endif

	/* Local addref / release within this func */
	b2r2_core_job_release(job, __func__);
}

/**
 * Called when tile job should be released (free memory etc.)
 * Should be used only for tile jobs. Tile jobs should only be used
 * by b2r2_core, thus making ref_count trigger their release.
 *
 * @job: The job
 */

static void tile_job_release_gen(struct b2r2_core_job *job)
{
	struct b2r2_core *core =
			(struct b2r2_core *) job->data;
	struct b2r2_control *cont = core->control;

	inc_stat(cont, &cont->stat_n_jobs_released);

	b2r2_log_info(cont->dev, "%s, first_node_address=0x%.8x, ref_count="
		"%d\n", __func__, job->first_node_address,
		job->ref_count);

	/* Release memory for the job */
	kfree(job);
}

/**
 * Called when job should be released (free memory etc.)
 *
 * @job: The job
 */

static void job_release_gen(struct b2r2_core_job *job)
{
	struct b2r2_blt_request *request =
		container_of(job, struct b2r2_blt_request, job);
	struct b2r2_core *core = (struct b2r2_core *) job->data;
	struct b2r2_control *cont = core->control;

	inc_stat(cont, &cont->stat_n_jobs_released);

	b2r2_log_info(cont->dev, "%s, first_node=%p, ref_count=%d\n",
			__func__, request->first_node, request->job.ref_count);

	if (request->first_node) {
		b2r2_debug_job_done(cont, request->first_node);

		/* Free nodes */
#ifdef B2R2_USE_NODE_GEN
		b2r2_blt_free_nodes(cont, request->first_node);
#else
		b2r2_node_free(cont, request->first_node);
#endif
	}

	/* Release memory for the request */
	if (request->clut != NULL) {
		dma_free_coherent(cont->dev, CLUT_SIZE, request->clut,
				request->clut_phys_addr);
		request->clut = NULL;
		request->clut_phys_addr = 0;
	}
	kfree(request);
}

static int job_acquire_resources_gen(struct b2r2_core_job *job, bool atomic)
{
	/* Nothing so far. Temporary buffers are pre-allocated */
	return 0;
}
static void job_release_resources_gen(struct b2r2_core_job *job, bool atomic)
{
	/* Nothing so far. Temporary buffers are pre-allocated */
}

/**
 * b2r2_generic_blt - Generic implementation of the B2R2 blit request
 *
 * @request; The request to perform
 */
int b2r2_generic_blt(struct b2r2_blt_request *request)
{
	int ret = 0;
	struct b2r2_blt_rect actual_dst_rect;
	int request_id = 0;
	struct b2r2_node *last_node = request->first_node;
	int node_count;
	s32 tmp_buf_width = 0;
	s32 tmp_buf_height = 0;
	u32 tmp_buf_count = 0;
	s32 x;
	s32 y;
	const struct b2r2_blt_rect *dst_rect = &(request->user_req.dst_rect);
	const s32 dst_img_width = request->user_req.dst_img.width;
	const s32 dst_img_height = request->user_req.dst_img.height;
	const enum b2r2_blt_flag flags = request->user_req.flags;
	/* Descriptors for the temporary buffers */
	struct b2r2_work_buf work_bufs[4];
	struct b2r2_blt_rect dst_rect_tile;
	int i;
	struct b2r2_control_instance *instance = request->instance;
	struct b2r2_control *cont = instance->control;

	unsigned long long thread_runtime_at_start = 0;
	s32 nsec_active_in_b2r2 = 0;

	/*
	 * Early exit if zero blt.
	 * dst_rect outside of dst_img or
	 * dst_clip_rect outside of dst_img.
	 */
	if (dst_rect->x + dst_rect->width <= 0 ||
			dst_rect->y + dst_rect->height <= 0 ||
			dst_img_width <= dst_rect->x ||
			dst_img_height <= dst_rect->y ||
			((flags & B2R2_BLT_FLAG_DESTINATION_CLIP) != 0 &&
			(dst_img_width <= request->user_req.dst_clip_rect.x ||
			dst_img_height <= request->user_req.dst_clip_rect.y ||
			request->user_req.dst_clip_rect.x +
			request->user_req.dst_clip_rect.width <= 0 ||
			request->user_req.dst_clip_rect.y +
			request->user_req.dst_clip_rect.height <= 0))) {
		goto zero_blt;
	}

	if (request->profile) {
		ktime_get_ts(&request->ts_start);
		thread_runtime_at_start = task_sched_runtime(current);
	}

	memset(work_bufs, 0, sizeof(work_bufs));

	b2r2_log_info(cont->dev, "%s\n", __func__);

	inc_stat(cont, &cont->stat_n_in_blt);

	/* Debug prints of incoming request */
	b2r2_log_info(cont->dev,
		"src.fmt=%#010x flags=0x%.8x src.buf={%d,%d,0x%.8x}\n"
		"src.w,h={%d,%d} src.rect={%d,%d,%d,%d}\n",
		request->user_req.src_img.fmt,
		request->user_req.flags,
		request->user_req.src_img.buf.type,
		request->user_req.src_img.buf.fd,
		request->user_req.src_img.buf.offset,
		request->user_req.src_img.width,
		request->user_req.src_img.height,
		request->user_req.src_rect.x,
		request->user_req.src_rect.y,
		request->user_req.src_rect.width,
		request->user_req.src_rect.height);
	b2r2_log_info(cont->dev,
		"dst.fmt=%#010x dst.buf={%d,%d,0x%.8x}\n"
		"dst.w,h={%d,%d} dst.rect={%d,%d,%d,%d}\n"
		"dst_clip_rect={%d,%d,%d,%d}\n",
		request->user_req.dst_img.fmt,
		request->user_req.dst_img.buf.type,
		request->user_req.dst_img.buf.fd,
		request->user_req.dst_img.buf.offset,
		request->user_req.dst_img.width,
		request->user_req.dst_img.height,
		request->user_req.dst_rect.x,
		request->user_req.dst_rect.y,
		request->user_req.dst_rect.width,
		request->user_req.dst_rect.height,
		request->user_req.dst_clip_rect.x,
		request->user_req.dst_clip_rect.y,
		request->user_req.dst_clip_rect.width,
		request->user_req.dst_clip_rect.height);

	inc_stat(cont, &cont->stat_n_in_blt_synch);

	/* Wait here if synch is ongoing */
	ret = wait_event_interruptible(instance->synch_done_waitq,
				!is_synching(instance));
	if (ret) {
		b2r2_log_warn(cont->dev, "%s: Sync wait interrupted, %d\n",
			__func__, ret);
		ret = -EAGAIN;
		dec_stat(cont, &cont->stat_n_in_blt_synch);
		goto synch_interrupted;
	}

	dec_stat(cont, &cont->stat_n_in_blt_synch);

	/* Resolve the buffers */

	/* Source buffer */
	ret = resolve_buf(cont, &request->user_req.src_img,
		&request->user_req.src_rect, false, &request->src_resolved);
	if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Resolve src buf failed, %d\n",
			__func__, ret);
		ret = -EAGAIN;
		goto resolve_src_buf_failed;
	}

	/* Source mask buffer */
	ret = resolve_buf(cont, &request->user_req.src_mask,
					&request->user_req.src_rect, false,
						&request->src_mask_resolved);
	if (ret < 0) {
		b2r2_log_warn(cont->dev,
			"%s: Resolve src mask buf failed, %d\n",
			__func__, ret);
		ret = -EAGAIN;
		goto resolve_src_mask_buf_failed;
	}

	/* Destination buffer */
	get_actual_dst_rect(&request->user_req, &actual_dst_rect);
	ret = resolve_buf(cont, &request->user_req.dst_img, &actual_dst_rect,
						true, &request->dst_resolved);
	if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Resolve dst buf failed, %d\n",
			__func__, ret);
		ret = -EAGAIN;
		goto resolve_dst_buf_failed;
	}

	/* Debug prints of resolved buffers */
	b2r2_log_info(cont->dev, "src.rbuf={%X,%p,%d} {%p,%X,%X,%d}\n",
		request->src_resolved.physical_address,
		request->src_resolved.virtual_address,
		request->src_resolved.is_pmem,
		request->src_resolved.filep,
		request->src_resolved.file_physical_start,
		request->src_resolved.file_virtual_start,
		request->src_resolved.file_len);

	b2r2_log_info(cont->dev, "dst.rbuf={%X,%p,%d} {%p,%X,%X,%d}\n",
		request->dst_resolved.physical_address,
		request->dst_resolved.virtual_address,
		request->dst_resolved.is_pmem,
		request->dst_resolved.filep,
		request->dst_resolved.file_physical_start,
		request->dst_resolved.file_virtual_start,
		request->dst_resolved.file_len);

	/* Calculate the number of nodes (and resources) needed for this job */
	ret = b2r2_generic_analyze(request, &tmp_buf_width,
			&tmp_buf_height, &tmp_buf_count, &node_count);
	if (ret < 0) {
		b2r2_log_warn(cont->dev,
			"%s: Failed to analyze request, ret = %d\n",
			__func__, ret);
#ifdef CONFIG_DEBUG_FS
		{
			/* Failed, dump job to dmesg */
			char *Buf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);

			b2r2_log_info(cont->dev,
				"%s: Analyze failed for:\n", __func__);
			if (Buf != NULL) {
				sprintf_req(request, Buf, sizeof(char) * 4096);
				b2r2_log_info(cont->dev, "%s", Buf);
				kfree(Buf);
			} else {
				b2r2_log_info(cont->dev,
					"Unable to print the request. "
					"Message buffer allocation failed.\n");
			}
		}
#endif
		goto generate_nodes_failed;
	}

	/* Allocate the nodes needed */
#ifdef B2R2_USE_NODE_GEN
	request->first_node = b2r2_blt_alloc_nodes(cont, node_count);
	if (request->first_node == NULL) {
		b2r2_log_warn(cont->dev,
			"%s: Failed to allocate nodes, ret = %d\n",
			__func__, ret);
		goto generate_nodes_failed;
	}
#else
	ret = b2r2_node_alloc(cont, node_count, &(request->first_node));
	if (ret < 0 || request->first_node == NULL) {
		b2r2_log_warn(cont->dev,
			"%s: Failed to allocate nodes, ret = %d\n",
			__func__, ret);
		goto generate_nodes_failed;
	}
#endif

	/* Allocate the temporary buffers */
	for (i = 0; i < tmp_buf_count; i++) {
		void *virt;
		work_bufs[i].size = tmp_buf_width * tmp_buf_height * 4;

		virt = dma_alloc_coherent(cont->dev,
				work_bufs[i].size,
				&(work_bufs[i].phys_addr),
				GFP_DMA | GFP_KERNEL);
		if (virt == NULL) {
			ret = -ENOMEM;
			goto alloc_work_bufs_failed;
		}

		work_bufs[i].virt_addr = virt;
		memset(work_bufs[i].virt_addr, 0xff, work_bufs[i].size);
	}
	ret = b2r2_generic_configure(request,
			request->first_node, &work_bufs[0], tmp_buf_count);

	if (ret < 0) {
		b2r2_log_warn(cont->dev,
			"%s: Failed to perform generic configure, ret = %d\n",
			__func__, ret);
		goto generic_conf_failed;
	}

	/*
	 * Exit here if dry run or if we choose to
	 * omit blit jobs through debugfs
	 */
	if (flags & B2R2_BLT_FLAG_DRY_RUN || cont->bypass)
		goto exit_dry_run;

	/*
	 * Configure the request and make sure
	 * that its job is run only for the LAST tile.
	 * This is when the request is complete
	 * and waiting clients should be notified.
	 */
	last_node = request->first_node;
	while (last_node && last_node->next)
		last_node = last_node->next;

	request->job.tag = (int) instance;
	request->job.data = (int) cont->data;
	request->job.prio = request->user_req.prio;
	request->job.first_node_address =
		request->first_node->physical_address;
	request->job.last_node_address =
		last_node->physical_address;
	request->job.callback = job_callback_gen;
	request->job.release = job_release_gen;
	/* Work buffers and nodes are pre-allocated */
	request->job.acquire_resources = job_acquire_resources_gen;
	request->job.release_resources = job_release_resources_gen;

	/* Flush the L1/L2 cache for the buffers */

	/* Source buffer */
	if (!(flags & B2R2_BLT_FLAG_SRC_NO_CACHE_FLUSH) &&
			(request->user_req.src_img.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.src_img.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.src_img,
			&request->src_resolved,
			false, /*is_dst*/
			&request->user_req.src_rect);

	/* Source mask buffer */
	if (!(flags & B2R2_BLT_FLAG_SRC_MASK_NO_CACHE_FLUSH) &&
			(request->user_req.src_mask.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.src_mask.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.src_mask,
			&request->src_mask_resolved,
			false, /*is_dst*/
			NULL);

	/* Destination buffer */
	if (!(flags & B2R2_BLT_FLAG_DST_NO_CACHE_FLUSH) &&
			(request->user_req.dst_img.buf.type !=
				B2R2_BLT_PTR_PHYSICAL) &&
			!b2r2_is_mb_fmt(request->user_req.dst_img.fmt))
		/* MB formats are never touched by SW */
		sync_buf(cont, &request->user_req.dst_img,
			&request->dst_resolved,
			true, /*is_dst*/
			&request->user_req.dst_rect);

#ifdef CONFIG_DEBUG_FS
	/* Remember latest request */
	mutex_lock(&cont->last_req_lock);
	cont->latest_request[cont->buf_index] = *request;

	/* Calculate, buf_index = (buf_index + 1) % last_request_count */
	cont->buf_index++;
	if (cont->buf_index >= cont->last_request_count)
		cont->buf_index = 0;
	mutex_unlock(&cont->last_req_lock);
#endif

	/*
	 * Same nodes are reused for all the jobs needed to complete the blit.
	 * Nodes are NOT released together with associated job,
	 * as is the case with optimized b2r2_blt() path.
	 */
	mutex_lock(&instance->lock);
	instance->no_of_active_requests++;
	mutex_unlock(&instance->lock);
	/*
	 * Process all but the last row in the destination rectangle.
	 * Consider only the tiles that will actually end up inside
	 * the destination image.
	 * dst_rect->height - tmp_buf_height being <=0 is allright.
	 * The loop will not be entered since y will always be equal to or
	 * greater than zero.
	 * Early exit check at the beginning handles the cases when nothing
	 * at all should be processed.
	 */
	y = 0;
	if (dst_rect->y < 0)
		y = -dst_rect->y;

	for (; y < dst_rect->height - tmp_buf_height &&
			y + dst_rect->y < dst_img_height - tmp_buf_height;
			y += tmp_buf_height) {
		/* Tile in the destination rectangle being processed */
		struct b2r2_blt_rect dst_rect_tile;
		dst_rect_tile.y = y;
		dst_rect_tile.width = tmp_buf_width;
		dst_rect_tile.height = tmp_buf_height;

		x = 0;
		if (dst_rect->x < 0)
			x = -dst_rect->x;

		for (; x < dst_rect->width && x + dst_rect->x < dst_img_width;
				x += tmp_buf_width) {
			/*
			 * Tile jobs are freed by the supplied release function
			 * when ref_count on a tile_job reaches zero.
			 */
			struct b2r2_core_job *tile_job =
				kmalloc(sizeof(*tile_job), GFP_KERNEL);
			if (tile_job == NULL) {
				/*
				 * Skip this tile. Do not abort,
				 * just hope for better luck
				 * with rest of the tiles.
				 * Memory might become available.
				 */
				b2r2_log_info(cont->dev, "%s: Failed to alloc "
						"job. Skipping tile at (x, y)="
						"(%d, %d)\n", __func__, x, y);
				continue;
			}
			tile_job->job_id = request->job.job_id;
			tile_job->tag = request->job.tag;
			tile_job->data = request->job.data;
			tile_job->prio = request->job.prio;
			tile_job->first_node_address =
					request->job.first_node_address;
			tile_job->last_node_address =
					request->job.last_node_address;
			tile_job->callback = tile_job_callback_gen;
			tile_job->release = tile_job_release_gen;
			/* Work buffers and nodes are pre-allocated */
			tile_job->acquire_resources =
				job_acquire_resources_gen;
			tile_job->release_resources =
				job_release_resources_gen;

			dst_rect_tile.x = x;
			if (x + dst_rect->x + tmp_buf_width > dst_img_width) {
				/*
				 * Only a part of the tile can be written.
				 * Limit imposed by buffer size.
				 */
				dst_rect_tile.width =
					dst_img_width - (x + dst_rect->x);
			} else if (x + tmp_buf_width > dst_rect->width) {
				/*
				 * Only a part of the tile can be written.
				 * In this case limit imposed by dst_rect size.
				 */
				dst_rect_tile.width = dst_rect->width - x;
			} else {
				/* Whole tile can be written. */
				dst_rect_tile.width = tmp_buf_width;
			}
			/*
			 * Where applicable, calculate area in src buffer
			 * that is needed to generate the specified part
			 * of destination rectangle.
			 */
			b2r2_generic_set_areas(request,
				request->first_node, &dst_rect_tile);
			/* Submit the job */
			b2r2_log_info(cont->dev,
				"%s: Submitting job\n", __func__);

			inc_stat(cont, &cont->stat_n_in_blt_add);

			mutex_lock(&instance->lock);

			request_id = b2r2_core_job_add(cont, tile_job);

			dec_stat(cont, &cont->stat_n_in_blt_add);

			if (request_id < 0) {
				b2r2_log_warn(cont->dev, "%s: "
					"Failed to add tile job, ret = %d\n",
					__func__, request_id);
				ret = request_id;
				mutex_unlock(&instance->lock);
				goto job_add_failed;
			}

			inc_stat(cont, &cont->stat_n_jobs_added);

			mutex_unlock(&instance->lock);

			/* Wait for the job to be done */
			b2r2_log_info(cont->dev, "%s: Synchronous, waiting\n",
				__func__);

			inc_stat(cont, &cont->stat_n_in_blt_wait);

			ret = b2r2_core_job_wait(tile_job);

			dec_stat(cont, &cont->stat_n_in_blt_wait);

			if (ret < 0 && ret != -ENOENT)
				b2r2_log_warn(cont->dev,
					"%s: Failed to wait job, ret = %d\n",
					__func__, ret);
			else {
				b2r2_log_info(cont->dev,
					"%s: Synchronous wait done\n",
					__func__);

				nsec_active_in_b2r2 +=
					tile_job->nsec_active_in_hw;
			}
			/* Release matching the addref in b2r2_core_job_add */
			b2r2_core_job_release(tile_job, __func__);
		}
	}

	x = 0;
	if (dst_rect->x < 0)
		x = -dst_rect->x;

	for (; x < dst_rect->width &&
			x + dst_rect->x < dst_img_width; x += tmp_buf_width) {
		struct b2r2_core_job *tile_job = NULL;
		if (x + tmp_buf_width < dst_rect->width &&
				x + dst_rect->x + tmp_buf_width <
				dst_img_width) {
			/*
			 * Tile jobs are freed by the supplied release function
			 * when ref_count on a tile_job reaches zero.
			 * Do NOT allocate a tile_job for the last tile.
			 * Send the job from the request. This way clients
			 * will be notified when the whole blit is complete
			 * and not just part of it.
			 */
			tile_job = kmalloc(sizeof(*tile_job), GFP_KERNEL);
			if (tile_job == NULL) {
				b2r2_log_info(cont->dev, "%s: Failed to alloc "
					"job. Skipping tile at (x, y)="
					"(%d, %d)\n", __func__, x, y);
				continue;
			}
			tile_job->job_id = request->job.job_id;
			tile_job->tag = request->job.tag;
			tile_job->data = request->job.data;
			tile_job->prio = request->job.prio;
			tile_job->first_node_address =
				request->job.first_node_address;
			tile_job->last_node_address =
				request->job.last_node_address;
			tile_job->callback = tile_job_callback_gen;
			tile_job->release = tile_job_release_gen;
			tile_job->acquire_resources =
				job_acquire_resources_gen;
			tile_job->release_resources =
				job_release_resources_gen;
		}

		dst_rect_tile.x = x;
		if (x + dst_rect->x + tmp_buf_width > dst_img_width) {
			/*
			 * Only a part of the tile can be written.
			 * Limit imposed by buffer size.
			 */
			dst_rect_tile.width = dst_img_width - (x + dst_rect->x);
		} else if (x + tmp_buf_width > dst_rect->width) {
			/*
			 * Only a part of the tile can be written.
			 * In this case limit imposed by dst_rect size.
			 */
			dst_rect_tile.width = dst_rect->width - x;
		} else {
			/* Whole tile can be written. */
			dst_rect_tile.width = tmp_buf_width;
		}
		/*
		 * y is now the last row. Either because the whole dst_rect
		 * has been processed, or because the last row that will be
		 * written to dst_img has been reached. Limits imposed in
		 * the same way as for width.
		 */
		dst_rect_tile.y = y;
		if (y + dst_rect->y + tmp_buf_height > dst_img_height)
			dst_rect_tile.height =
				dst_img_height - (y + dst_rect->y);
		else if (y + tmp_buf_height > dst_rect->height)
			dst_rect_tile.height = dst_rect->height - y;
		else
			dst_rect_tile.height = tmp_buf_height;

		b2r2_generic_set_areas(request,
			request->first_node, &dst_rect_tile);

		b2r2_log_info(cont->dev, "%s: Submitting job\n", __func__);
		inc_stat(cont, &cont->stat_n_in_blt_add);

		mutex_lock(&instance->lock);
		if (x + tmp_buf_width < dst_rect->width &&
				x + dst_rect->x + tmp_buf_width <
				dst_img_width) {
			request_id = b2r2_core_job_add(cont, tile_job);
		} else {
			/*
			 * Last tile. Send the job-struct from the request.
			 * Clients will be notified once it completes.
			 */
			request_id = b2r2_core_job_add(cont, &request->job);
		}

		dec_stat(cont, &cont->stat_n_in_blt_add);

		if (request_id < 0) {
			b2r2_log_warn(cont->dev, "%s: Failed to add tile job, "
				"ret = %d\n", __func__, request_id);
			ret = request_id;
			mutex_unlock(&instance->lock);
			if (tile_job != NULL)
				kfree(tile_job);
			goto job_add_failed;
		}

		inc_stat(cont, &cont->stat_n_jobs_added);
		mutex_unlock(&instance->lock);

		b2r2_log_info(cont->dev, "%s: Synchronous, waiting\n",
			__func__);

		inc_stat(cont, &cont->stat_n_in_blt_wait);
		if (x + tmp_buf_width < dst_rect->width &&
				x + dst_rect->x + tmp_buf_width <
				dst_img_width) {
			ret = b2r2_core_job_wait(tile_job);
		} else {
			/*
			 * This is the last tile. Wait for the job-struct from
			 * the request.
			 */
			ret = b2r2_core_job_wait(&request->job);
		}
		dec_stat(cont, &cont->stat_n_in_blt_wait);

		if (ret < 0 && ret != -ENOENT)
			b2r2_log_warn(cont->dev,
				"%s: Failed to wait job, ret = %d\n",
				__func__, ret);
		else {
			b2r2_log_info(cont->dev,
				"%s: Synchronous wait done\n", __func__);

			if (x + tmp_buf_width < dst_rect->width &&
					x + dst_rect->x + tmp_buf_width <
					dst_img_width)
				nsec_active_in_b2r2 +=
					tile_job->nsec_active_in_hw;
			else
				nsec_active_in_b2r2 +=
					request->job.nsec_active_in_hw;
		}

		/*
		 * Release matching the addref in b2r2_core_job_add.
		 * Make sure that the correct job-struct is released
		 * when the last tile is processed.
		 */
		if (x + tmp_buf_width < dst_rect->width &&
				x + dst_rect->x + tmp_buf_width <
				dst_img_width) {
			b2r2_core_job_release(tile_job, __func__);
		} else {
			/*
			 * Update profiling information before
			 * the request is released together with
			 * its core_job.
			 */
			if (request->profile) {
				struct timespec ts_stop;
				struct timespec ts_diff;
				request->nsec_active_in_cpu =
					(s64)(task_sched_runtime(current) -
						thread_runtime_at_start);
				ktime_get_ts(&ts_stop);
				ts_diff = timespec_sub(ts_stop,
					request->ts_start);
				request->total_time_nsec =
					timespec_to_ns(&ts_diff);
				request->job.nsec_active_in_hw =
					nsec_active_in_b2r2;

				b2r2_call_profiler_blt_done(request);
			}

			b2r2_core_job_release(&request->job, __func__);
		}
	}

	dec_stat(cont, &cont->stat_n_in_blt);

	for (i = 0; i < tmp_buf_count; i++) {
		dma_free_coherent(cont->dev,
			work_bufs[i].size,
			work_bufs[i].virt_addr,
			work_bufs[i].phys_addr);
		memset(&(work_bufs[i]), 0, sizeof(work_bufs[i]));
	}

	return request_id;

job_add_failed:
exit_dry_run:
generic_conf_failed:
alloc_work_bufs_failed:
	for (i = 0; i < 4; i++) {
		if (work_bufs[i].virt_addr != 0) {
			dma_free_coherent(cont->dev,
				work_bufs[i].size,
				work_bufs[i].virt_addr,
				work_bufs[i].phys_addr);
			memset(&(work_bufs[i]), 0, sizeof(work_bufs[i]));
		}
	}

generate_nodes_failed:
	unresolve_buf(cont, &request->user_req.dst_img.buf,
		&request->dst_resolved);
resolve_dst_buf_failed:
	unresolve_buf(cont, &request->user_req.src_mask.buf,
		&request->src_mask_resolved);
resolve_src_mask_buf_failed:
	unresolve_buf(cont, &request->user_req.src_img.buf,
		&request->src_resolved);
resolve_src_buf_failed:
synch_interrupted:
zero_blt:
	job_release_gen(&request->job);
	dec_stat(cont, &cont->stat_n_jobs_released);
	dec_stat(cont, &cont->stat_n_in_blt);

	b2r2_log_info(cont->dev, "b2r2:%s ret=%d", __func__, ret);
	return ret;
}
#endif /* CONFIG_B2R2_GENERIC */

/**
 * b2r2_blt_synch - Implements wait for all or a specified job
 *
 * @instance: The B2R2 BLT instance
 * @request_id: If 0, wait for all requests on this instance to finish.
 *              Else wait for request with given request id to finish.
 */
int b2r2_control_synch(struct b2r2_control_instance *instance,
			int request_id)
{
	int ret = 0;
	struct b2r2_control *cont = instance->control;

	b2r2_log_info(cont->dev, "%s, request_id=%d\n", __func__, request_id);

	if (request_id == 0) {
		/* Wait for all requests */
		inc_stat(cont, &cont->stat_n_in_synch_0);

		/* Enter state "synching" if we have any active request */
		mutex_lock(&instance->lock);
		if (instance->no_of_active_requests)
			instance->synching = true;
		mutex_unlock(&instance->lock);

		/* Wait until no longer in state synching */
		ret = wait_event_interruptible(instance->synch_done_waitq,
				!is_synching(instance));
		dec_stat(cont, &cont->stat_n_in_synch_0);
	} else {
		struct b2r2_core_job *job;

		inc_stat(cont, &cont->stat_n_in_synch_job);

		/* Wait for specific job */
		job = b2r2_core_job_find(cont, request_id);
		if (job) {
			/* Wait on find job */
			ret = b2r2_core_job_wait(job);
			/* Release matching the addref in b2r2_core_job_find */
			b2r2_core_job_release(job, __func__);
		}

		/* If job not found we assume that is has been run */
		dec_stat(cont, &cont->stat_n_in_synch_job);
	}

	b2r2_log_info(cont->dev,
		"%s, request_id=%d, returns %d\n", __func__, request_id, ret);

	return ret;
}

static void get_actual_dst_rect(struct b2r2_blt_req *req,
					struct b2r2_blt_rect *actual_dst_rect)
{
	struct b2r2_blt_rect dst_img_bounds;

	b2r2_get_img_bounding_rect(&req->dst_img, &dst_img_bounds);

	b2r2_intersect_rects(&req->dst_rect, &dst_img_bounds, actual_dst_rect);

	if (req->flags & B2R2_BLT_FLAG_DESTINATION_CLIP)
		b2r2_intersect_rects(actual_dst_rect, &req->dst_clip_rect,
				actual_dst_rect);
}

static void set_up_hwmem_region(struct b2r2_control *cont,
		struct b2r2_blt_img *img, struct b2r2_blt_rect *rect,
		struct hwmem_region *region)
{
	s32 img_size;

	memset(region, 0, sizeof(*region));

	if (b2r2_is_zero_area_rect(rect))
		return;

	img_size = b2r2_get_img_size(cont->dev, img);

	if (b2r2_is_single_plane_fmt(img->fmt) &&
			b2r2_is_independent_pixel_fmt(img->fmt)) {
		int img_fmt_bpp = b2r2_get_fmt_bpp(cont->dev, img->fmt);
		u32 img_pitch = b2r2_get_img_pitch(cont->dev, img);

		region->offset = (u32)(img->buf.offset + (rect->y *
				img_pitch));
		region->count = (u32)rect->height;
		region->start = (u32)((rect->x * img_fmt_bpp) / 8);
		region->end = (u32)b2r2_div_round_up(
				(rect->x + rect->width) * img_fmt_bpp, 8);
		region->size = img_pitch;
	} else {
		/*
		 * TODO: Locking entire buffer as a quick safe solution. In the
		 * future we should lock less to avoid unecessary cache
		 * synching. Pixel interleaved YCbCr formats should be quite
		 * easy, just align start and stop points on 2.
		 */
		region->offset = (u32)img->buf.offset;
		region->count = 1;
		region->start = 0;
		region->end = (u32)img_size;
		region->size = (u32)img_size;
	}
}

static int resolve_hwmem(struct b2r2_control *cont,
		struct b2r2_blt_img *img,
		struct b2r2_blt_rect *rect_2b_used,
		bool is_dst,
		struct b2r2_resolved_buf *resolved_buf)
{
	int return_value = 0;
	enum hwmem_mem_type mem_type;
	enum hwmem_access access;
	enum hwmem_access required_access;
	struct hwmem_mem_chunk mem_chunk;
	size_t mem_chunk_length = 1;
	struct hwmem_region region;

	resolved_buf->hwmem_alloc =
			hwmem_resolve_by_name(img->buf.hwmem_buf_name);
	if (IS_ERR(resolved_buf->hwmem_alloc)) {
		return_value = PTR_ERR(resolved_buf->hwmem_alloc);
		b2r2_log_info(cont->dev, "%s: hwmem_resolve_by_name failed, "
			"error code: %i\n", __func__, return_value);
		goto resolve_failed;
	}

	hwmem_get_info(resolved_buf->hwmem_alloc, &resolved_buf->file_len,
			&mem_type, &access);

	required_access = (is_dst ? HWMEM_ACCESS_WRITE : HWMEM_ACCESS_READ) |
			HWMEM_ACCESS_IMPORT;
	if ((required_access & access) != required_access) {
		b2r2_log_info(cont->dev,
			"%s: Insufficient access to hwmem (%d, requires %d)"
			"buffer.\n", __func__, access, required_access);
		return_value = -EACCES;
		goto access_check_failed;
	}

	if (mem_type == HWMEM_MEM_SCATTERED_SYS) {
		b2r2_log_info(cont->dev, "%s: Hwmem buffer is scattered.\n",
			__func__);
		return_value = -EINVAL;
		goto buf_scattered;
	}

	if (resolved_buf->file_len <
			img->buf.offset +
			(__u32)b2r2_get_img_size(cont->dev, img)) {
		b2r2_log_info(cont->dev, "%s: Hwmem buffer too small. (%d < "
			"%d)\n", __func__, resolved_buf->file_len,
			img->buf.offset +
			(__u32)b2r2_get_img_size(cont->dev, img));
		return_value = -EINVAL;
		goto size_check_failed;
	}

	return_value = hwmem_pin(resolved_buf->hwmem_alloc, &mem_chunk,
							 &mem_chunk_length);
	if (return_value < 0) {
		b2r2_log_info(cont->dev, "%s: hwmem_pin failed, "
			"error code: %i\n", __func__, return_value);
		goto pin_failed;
	}
	resolved_buf->file_physical_start = mem_chunk.paddr;

	set_up_hwmem_region(cont, img, rect_2b_used, &region);
	return_value = hwmem_set_domain(resolved_buf->hwmem_alloc,
		required_access, HWMEM_DOMAIN_SYNC, &region);
	if (return_value < 0) {
		b2r2_log_info(cont->dev, "%s: hwmem_set_domain failed, "
			"error code: %i\n", __func__, return_value);
		goto set_domain_failed;
	}

	resolved_buf->physical_address =
			resolved_buf->file_physical_start + img->buf.offset;

	resolved_buf->virtual_address = hwmem_kmap(resolved_buf->hwmem_alloc);

	goto out;

set_domain_failed:
	hwmem_unpin(resolved_buf->hwmem_alloc);
pin_failed:
size_check_failed:
buf_scattered:
access_check_failed:
	hwmem_release(resolved_buf->hwmem_alloc);
resolve_failed:

out:
	return return_value;
}

static void unresolve_hwmem(struct b2r2_resolved_buf *resolved_buf)
{
	hwmem_kunmap(resolved_buf->hwmem_alloc);
	hwmem_unpin(resolved_buf->hwmem_alloc);
	hwmem_release(resolved_buf->hwmem_alloc);
}

/**
 * unresolve_buf() - Must be called after resolve_buf
 *
 * @buf: The buffer specification as supplied from user space
 * @resolved: Gathered information about the buffer
 *
 * Returns 0 if OK else negative error code
 */
static void unresolve_buf(struct b2r2_control *cont,
		struct b2r2_blt_buf *buf,
		struct b2r2_resolved_buf *resolved)
{
#ifdef CONFIG_ANDROID_PMEM
	if (resolved->is_pmem && resolved->filep)
		put_pmem_file(resolved->filep);
#endif
	if (resolved->hwmem_alloc != NULL)
		unresolve_hwmem(resolved);
}

/**
 * get_fb_info() - Fill buf with framebuffer info
 *
 * @file: The framebuffer file
 * @buf: Gathered information about the buffer
 * @img_offset: Image offset info frame buffer
 *
 * Returns 0 if OK else negative error code
 */
static int get_fb_info(struct file *file,
		struct b2r2_resolved_buf *buf,
		__u32 img_offset)
{
#ifdef CONFIG_FB
	if (file && buf &&
			MAJOR(file->f_dentry->d_inode->i_rdev) == FB_MAJOR) {
		int i;
		/*
		 * (OK to do it like this, no locking???)
		 */
		for (i = 0; i < num_registered_fb; i++) {
			struct fb_info *info = registered_fb[i];

			if (info && info->dev &&
					MINOR(info->dev->devt) ==
					MINOR(file->f_dentry->d_inode->i_rdev)) {
				buf->file_physical_start = info->fix.smem_start;
				buf->file_virtual_start = (u32)info->screen_base;
				buf->file_len = info->fix.smem_len;
				buf->physical_address = buf->file_physical_start +
						img_offset;
				buf->virtual_address =
						(void *) (buf->file_virtual_start +
						img_offset);
				return 0;
			}
		}
	}
#endif
	return -EINVAL;
}

/**
 * resolve_buf() - Returns the physical & virtual addresses of a B2R2 blt buffer
 *
 * @img: The image specification as supplied from user space
 * @rect_2b_used: The part of the image b2r2 will use.
 * @usage: Specifies how the buffer will be used.
 * @resolved: Gathered information about the buffer
 *
 * Returns 0 if OK else negative error code
 */
static int resolve_buf(struct b2r2_control *cont,
		struct b2r2_blt_img *img,
		struct b2r2_blt_rect *rect_2b_used,
		bool is_dst,
		struct b2r2_resolved_buf *resolved)
{
	int ret = 0;

	memset(resolved, 0, sizeof(*resolved));

	switch (img->buf.type) {
	case B2R2_BLT_PTR_NONE:
		break;

	case B2R2_BLT_PTR_PHYSICAL:
		resolved->physical_address = img->buf.offset;
		resolved->file_len = img->buf.len;
		break;

		/* FD + OFFSET type */
	case B2R2_BLT_PTR_FD_OFFSET: {
		/*
		 * TODO: Do we need to check if the process is allowed to
		 * read/write (depending on if it's dst or src) to the file?
		 */
#ifdef CONFIG_ANDROID_PMEM
		if (!get_pmem_file(
				img->buf.fd,
				(unsigned long *) &resolved->file_physical_start,
				(unsigned long *) &resolved->file_virtual_start,
				(unsigned long *) &resolved->file_len,
				&resolved->filep)) {
			resolved->physical_address =
				resolved->file_physical_start +
				img->buf.offset;
			resolved->virtual_address = (void *)
				(resolved->file_virtual_start +
				img->buf.offset);
			resolved->is_pmem = true;
		} else
#endif
		{
			int fput_needed;
			struct file *file;

			file = fget_light(img->buf.fd, &fput_needed);
			if (file == NULL)
				return -EINVAL;

			ret = get_fb_info(file, resolved,
					img->buf.offset);
			fput_light(file, fput_needed);
			if (ret < 0)
				return ret;
		}

		/* Check bounds */
		if (img->buf.offset + img->buf.len >
				resolved->file_len) {
			ret = -ESPIPE;
			unresolve_buf(cont, &img->buf, resolved);
		}

		break;
	}

	case B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET:
		ret = resolve_hwmem(cont, img, rect_2b_used, is_dst, resolved);
		break;

	default:
		b2r2_log_warn(cont->dev, "%s: Failed to resolve buf type %d\n",
			__func__, img->buf.type);

		ret = -EINVAL;
		break;

	}

	return ret;
}

/**
 * sync_buf - Synchronizes the memory occupied by an image buffer.
 *
 * @buf: User buffer specification
 * @resolved_buf: Gathered info (physical address etc.) about buffer
 * @is_dst: true if the buffer is a destination buffer, false if the buffer is a
 *          source buffer.
 * @rect: rectangle in the image buffer that should be synced.
 *        NULL if the buffer is a source mask.
 * @img_width: width of the complete image buffer
 * @fmt: buffer format
*/
static void sync_buf(struct b2r2_control *cont,
		struct b2r2_blt_img *img,
		struct b2r2_resolved_buf *resolved,
		bool is_dst,
		struct b2r2_blt_rect *rect)
{
	struct sync_args sa;
	u32 start_phys, end_phys;

	if (B2R2_BLT_PTR_NONE == img->buf.type ||
			B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET == img->buf.type)
		return;

	start_phys = resolved->physical_address;
	end_phys = resolved->physical_address + img->buf.len;

	/*
	 * TODO: Very ugly. We should find out whether the memory is coherent in
	 * some generic way but cache handling will be rewritten soon so there
	 * is no use spending time on it. In the new design this will probably
	 * not be a problem.
	 */
	/* Frame buffer is coherent, at least now. */
	if (!resolved->is_pmem) {
		/*
		 * Drain the write buffers as they are not always part of the
		 * coherent concept.
		 */
		wmb();

		return;
	}

	/*
	 * src_mask does not have rect.
	 * Also flush full buffer for planar and semiplanar YUV formats
	 */
	if (rect == NULL ||
			(img->fmt == B2R2_BLT_FMT_YUV420_PACKED_PLANAR) ||
			(img->fmt == B2R2_BLT_FMT_YUV422_PACKED_PLANAR) ||
			(img->fmt == B2R2_BLT_FMT_YUV444_PACKED_PLANAR) ||
			(img->fmt == B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR) ||
			(img->fmt == B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR) ||
			(img->fmt ==
				B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE) ||
			(img->fmt ==
				B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE)) {
		sa.start = (unsigned long)resolved->virtual_address;
		sa.end = (unsigned long)resolved->virtual_address +
			img->buf.len;
		start_phys = resolved->physical_address;
		end_phys = resolved->physical_address + img->buf.len;
	} else {
		/*
		 * buffer is not a src_mask so make use of rect when
		 * clean & flush caches
		 */
		u32 bpp;	/* Bits per pixel */
		u32 pitch;

		switch (img->fmt) {
		case B2R2_BLT_FMT_16_BIT_ARGB4444: /* Fall through */
		case B2R2_BLT_FMT_16_BIT_ABGR4444: /* Fall through */
		case B2R2_BLT_FMT_16_BIT_ARGB1555: /* Fall through */
		case B2R2_BLT_FMT_16_BIT_RGB565:   /* Fall through */
		case B2R2_BLT_FMT_CB_Y_CR_Y:
			bpp = 16;
			break;
		case B2R2_BLT_FMT_24_BIT_RGB888:   /* Fall through */
		case B2R2_BLT_FMT_24_BIT_ARGB8565: /* Fall through */
		case B2R2_BLT_FMT_24_BIT_YUV888:
		case B2R2_BLT_FMT_24_BIT_VUY888:
			bpp =  24;
			break;
		case B2R2_BLT_FMT_32_BIT_ARGB8888: /* Fall through */
		case B2R2_BLT_FMT_32_BIT_ABGR8888: /* Fall through */
		case B2R2_BLT_FMT_32_BIT_AYUV8888:
		case B2R2_BLT_FMT_32_BIT_VUYA8888:
			bpp = 32;
			break;
		default:
			bpp = 12;
		}
		if (img->pitch == 0)
			pitch = (img->width * bpp) / 8;
		else
			pitch = img->pitch;

		/*
		 * For 422I formats 2 horizontal pixels share color data.
		 * Thus, the x position must be aligned down to closest even
		 * number and width must be aligned up.
		 */
		{
			s32 x;
			s32 width;

			switch (img->fmt) {
			case B2R2_BLT_FMT_CB_Y_CR_Y:
				x = (rect->x / 2) * 2;
				width = ((rect->width + 1) / 2) * 2;
				break;
			default:
				x = rect->x;
				width = rect->width;
				break;
			}

			sa.start = (unsigned long)resolved->virtual_address +
					rect->y * pitch + (x * bpp) / 8;
			sa.end = (unsigned long)sa.start +
					(rect->height - 1) * pitch +
					(width * bpp) / 8;

			start_phys = resolved->physical_address +
					rect->y * pitch + (x * bpp) / 8;
			end_phys = start_phys +
					(rect->height - 1) * pitch +
					(width * bpp) / 8;
		}
	}

	/*
	 * The virtual address to a pmem buffer is retrieved from ioremap, not
	 * sure if it's	ok to use such an address as a kernel virtual address.
	 * When doing it at a higher level such as dma_map_single it triggers an
	 * error but at lower levels such as dmac_clean_range it seems to work,
	 * hence the low level stuff.
	 */

	if (is_dst) {
		/*
		 * According to ARM's docs you must clean before invalidating
		 * (ie flush) to avoid loosing data.
		 */

		/* Flush L1 cache */
#ifdef CONFIG_SMP
		flush_l1_cache_range_all_cpus(&sa);
#else
		flush_l1_cache_range_curr_cpu(&sa);
#endif

		/* Flush L2 cache */
		outer_flush_range(start_phys, end_phys);
	} else {
		/* Clean L1 cache */
#ifdef CONFIG_SMP
		clean_l1_cache_range_all_cpus(&sa);
#else
		clean_l1_cache_range_curr_cpu(&sa);
#endif

		/* Clean L2 cache */
		outer_clean_range(start_phys, end_phys);
	}
}

/**
 * is_report_list_empty() - Spin lock protected check of report list
 *
 * @instance: The B2R2 BLT instance
 */
static bool is_report_list_empty(struct b2r2_control_instance *instance)
{
	bool is_empty;

	mutex_lock(&instance->lock);
	is_empty = list_empty(&instance->report_list);
	mutex_unlock(&instance->lock);

	return is_empty;
}

/**
 * is_synching() - Spin lock protected check if synching
 *
 * @instance: The B2R2 BLT instance
 */
static bool is_synching(struct b2r2_control_instance *instance)
{
	bool is_synching;

	mutex_lock(&instance->lock);
	is_synching = instance->synching;
	mutex_unlock(&instance->lock);

	return is_synching;
}

/**
 * inc_stat() - Spin lock protected increment of statistics variable
 *
 * @stat: Pointer to statistics variable that should be incremented
 */
static void inc_stat(struct b2r2_control *cont, unsigned long *stat)
{
	mutex_lock(&cont->stat_lock);
	(*stat)++;
	mutex_unlock(&cont->stat_lock);
}

/**
 * inc_stat() - Spin lock protected decrement of statistics variable
 *
 * @stat: Pointer to statistics variable that should be decremented
 */
static void dec_stat(struct b2r2_control *cont, unsigned long *stat)
{
	mutex_lock(&cont->stat_lock);
	(*stat)--;
	mutex_unlock(&cont->stat_lock);
}


#ifdef CONFIG_DEBUG_FS
/**
 * debugfs_b2r2_blt_request_read() - Implements debugfs read for B2R2
 * previous blits. Number of previous blits set using last_request_count.
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_blt_request_read(struct file *filp, char __user *buf,
			size_t count, loff_t *f_pos)
{
	size_t dev_size = 0;
	size_t max_size = 0;
	int i = 0, index = 0, ret = 0;
	unsigned int last_request_count = 0;
	char *req_buf = NULL;
	char tmpbuf[9];

	struct b2r2_control *cont = filp->f_dentry->d_inode->i_private;

	mutex_lock(&cont->last_req_lock);
	last_request_count = cont->last_request_count;

	/* Buffer size for one blit is below 2048 */
	max_size = sizeof(char) * last_request_count * CHARS_IN_REQ;
	req_buf = kmalloc(max_size, GFP_KERNEL);

	if (req_buf == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* buf_index is location to store next request */
	index = cont->buf_index;

	for (i = last_request_count; i > 0; i--) {
		if (--index < 0)
			index = last_request_count - 1;

		if (i == last_request_count)
			snprintf(tmpbuf, sizeof(tmpbuf), "(latest)");
		else if (i == 1)
			snprintf(tmpbuf, sizeof(tmpbuf), "(oldest)");
		else
			tmpbuf[0] = '\0';

		dev_size += snprintf(req_buf + dev_size,
			max_size - dev_size,
			"Details of blit request [%d]%s\n",
			last_request_count - i, tmpbuf);

		if (dev_size > max_size)
			goto out;

		dev_size += sprintf_req(&cont->latest_request[index],
			req_buf + dev_size,
			max_size - dev_size);
	}

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, &req_buf[*f_pos], count))
		ret = -EINVAL;

	*f_pos += count;
	ret = count;

out:
	mutex_unlock(&cont->last_req_lock);
	if (req_buf != NULL)
		kfree(req_buf);
	return ret;
}

/**
 * debugfs_b2r2_blt_request_fops - File operations for B2R2 request debugfs
 */
static const struct file_operations debugfs_b2r2_blt_request_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_blt_request_read,
};

/**
 * debugfs_b2r2_req_count_read() - Implements debugfs read for
 *                             previous request count
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_req_count_read(struct file *filp, char __user *buf,
				   size_t count, loff_t *f_pos)
{
	/* 2 characters hex number + newline + string terminator; */
	char tmpbuf[2+2];
	size_t dev_size = 0;
	int ret = 0;
	struct b2r2_control *cont = filp->f_dentry->d_inode->i_private;

	dev_size = snprintf(tmpbuf, sizeof(tmpbuf),
		"%02X\n", cont->last_request_count);
	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		return ret;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, &tmpbuf[*f_pos], count))
		return -EINVAL;
	*f_pos += count;
	ret = count;

	return ret;
}

/**
 * debugfs_b2r2_last_count_write() - Implements debugfs write for
 *                              previous request count
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to write
 * @f_pos: File position
 *
 * Returns number of bytes written or negative error code
 */
static int debugfs_b2r2_req_count_write(struct file *filp,
			const char __user *buf, size_t count, loff_t *f_pos)
{
	char tmpbuf[80];
	unsigned int req_count = 0;
	int ret = 0;
	struct b2r2_control *cont = filp->f_dentry->d_inode->i_private;

	if (count >= sizeof(tmpbuf))
		count = sizeof(tmpbuf) - 1;
	if (copy_from_user(tmpbuf, buf, count))
		return -EINVAL;

	tmpbuf[count] = 0;
	if (sscanf(tmpbuf, "%02X", &req_count) != 1)
		return -EINVAL;

	mutex_lock(&cont->last_req_lock);
	/* Reset buf_index and request array */
	cont->buf_index = 0;
	memset(cont->latest_request, 0,
		sizeof(cont->latest_request));

	/* Make req_count in [1-MAX_LAST_REQUEST] range */
	if (req_count < 1) {
		b2r2_log_warn(cont->dev,
			"%s: last_request_count is less than MIN\n", __func__);
		req_count = 1;
	}
	if (req_count > MAX_LAST_REQUEST) {
		b2r2_log_warn(cont->dev,
			"%s: last_request_count is more than MAX\n", __func__);
		req_count = MAX_LAST_REQUEST;
	}

	cont->last_request_count = req_count;
	mutex_unlock(&cont->last_req_lock);

	*f_pos += count;
	ret = count;

	return ret;
}

/**
 * debugfs_b2r2_req_count_fops() - File operations for previous request count in debugfs
 */
static const struct file_operations debugfs_b2r2_req_count_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_req_count_read,
	.write = debugfs_b2r2_req_count_write,
};

/**
 * struct debugfs_reg - Represents a B2R2 node "register"
 *
 * @name: Register name
 * @offset: Offset within the node
 */
struct debugfs_reg {
	const char  name[30];
	u32   offset;
};

/**
 * debugfs_node_regs - Array with all the registers in a B2R2 node, for debug
 */
static const struct debugfs_reg debugfs_node_regs[] = {
	{"GROUP0.B2R2_NIP", offsetof(struct b2r2_link_list, GROUP0.B2R2_NIP)},
	{"GROUP0.B2R2_CIC", offsetof(struct b2r2_link_list, GROUP0.B2R2_CIC)},
	{"GROUP0.B2R2_INS", offsetof(struct b2r2_link_list, GROUP0.B2R2_INS)},
	{"GROUP0.B2R2_ACK", offsetof(struct b2r2_link_list, GROUP0.B2R2_ACK)},

	{"GROUP1.B2R2_TBA", offsetof(struct b2r2_link_list, GROUP1.B2R2_TBA)},
	{"GROUP1.B2R2_TTY", offsetof(struct b2r2_link_list, GROUP1.B2R2_TTY)},
	{"GROUP1.B2R2_TXY", offsetof(struct b2r2_link_list, GROUP1.B2R2_TXY)},
	{"GROUP1.B2R2_TSZ", offsetof(struct b2r2_link_list, GROUP1.B2R2_TSZ)},

	{"GROUP2.B2R2_S1CF", offsetof(struct b2r2_link_list, GROUP2.B2R2_S1CF)},
	{"GROUP2.B2R2_S2CF", offsetof(struct b2r2_link_list, GROUP2.B2R2_S2CF)},

	{"GROUP3.B2R2_SBA", offsetof(struct b2r2_link_list, GROUP3.B2R2_SBA)},
	{"GROUP3.B2R2_STY", offsetof(struct b2r2_link_list, GROUP3.B2R2_STY)},
	{"GROUP3.B2R2_SXY", offsetof(struct b2r2_link_list, GROUP3.B2R2_SXY)},
	{"GROUP3.B2R2_SSZ", offsetof(struct b2r2_link_list, GROUP3.B2R2_SSZ)},

	{"GROUP4.B2R2_SBA", offsetof(struct b2r2_link_list, GROUP4.B2R2_SBA)},
	{"GROUP4.B2R2_STY", offsetof(struct b2r2_link_list, GROUP4.B2R2_STY)},
	{"GROUP4.B2R2_SXY", offsetof(struct b2r2_link_list, GROUP4.B2R2_SXY)},
	{"GROUP4.B2R2_SSZ", offsetof(struct b2r2_link_list, GROUP4.B2R2_SSZ)},

	{"GROUP5.B2R2_SBA", offsetof(struct b2r2_link_list, GROUP5.B2R2_SBA)},
	{"GROUP5.B2R2_STY", offsetof(struct b2r2_link_list, GROUP5.B2R2_STY)},
	{"GROUP5.B2R2_SXY", offsetof(struct b2r2_link_list, GROUP5.B2R2_SXY)},
	{"GROUP5.B2R2_SSZ", offsetof(struct b2r2_link_list, GROUP5.B2R2_SSZ)},

	{"GROUP6.B2R2_CWO", offsetof(struct b2r2_link_list, GROUP6.B2R2_CWO)},
	{"GROUP6.B2R2_CWS", offsetof(struct b2r2_link_list, GROUP6.B2R2_CWS)},

	{"GROUP7.B2R2_CCO", offsetof(struct b2r2_link_list, GROUP7.B2R2_CCO)},
	{"GROUP7.B2R2_CML", offsetof(struct b2r2_link_list, GROUP7.B2R2_CML)},

	{"GROUP8.B2R2_FCTL", offsetof(struct b2r2_link_list, GROUP8.B2R2_FCTL)},
	{"GROUP8.B2R2_PMK", offsetof(struct b2r2_link_list, GROUP8.B2R2_PMK)},

	{"GROUP9.B2R2_RSF", offsetof(struct b2r2_link_list, GROUP9.B2R2_RSF)},
	{"GROUP9.B2R2_RZI", offsetof(struct b2r2_link_list, GROUP9.B2R2_RZI)},
	{"GROUP9.B2R2_HFP", offsetof(struct b2r2_link_list, GROUP9.B2R2_HFP)},
	{"GROUP9.B2R2_VFP", offsetof(struct b2r2_link_list, GROUP9.B2R2_VFP)},

	{"GROUP10.B2R2_RSF", offsetof(struct b2r2_link_list, GROUP10.B2R2_RSF)},
	{"GROUP10.B2R2_RZI", offsetof(struct b2r2_link_list, GROUP10.B2R2_RZI)},
	{"GROUP10.B2R2_HFP", offsetof(struct b2r2_link_list, GROUP10.B2R2_HFP)},
	{"GROUP10.B2R2_VFP", offsetof(struct b2r2_link_list, GROUP10.B2R2_VFP)},

	{"GROUP11.B2R2_FF0", offsetof(struct b2r2_link_list,
					GROUP11.B2R2_FF0)},
	{"GROUP11.B2R2_FF1", offsetof(struct b2r2_link_list,
					GROUP11.B2R2_FF1)},
	{"GROUP11.B2R2_FF2", offsetof(struct b2r2_link_list,
					GROUP11.B2R2_FF2)},
	{"GROUP11.B2R2_FF3", offsetof(struct b2r2_link_list,
					GROUP11.B2R2_FF3)},

	{"GROUP12.B2R2_KEY1", offsetof(struct b2r2_link_list,
				GROUP12.B2R2_KEY1)},
	{"GROUP12.B2R2_KEY2", offsetof(struct b2r2_link_list,
				GROUP12.B2R2_KEY2)},

	{"GROUP13.B2R2_XYL", offsetof(struct b2r2_link_list, GROUP13.B2R2_XYL)},
	{"GROUP13.B2R2_XYP", offsetof(struct b2r2_link_list, GROUP13.B2R2_XYP)},

	{"GROUP14.B2R2_SAR", offsetof(struct b2r2_link_list, GROUP14.B2R2_SAR)},
	{"GROUP14.B2R2_USR", offsetof(struct b2r2_link_list, GROUP14.B2R2_USR)},

	{"GROUP15.B2R2_VMX0", offsetof(struct b2r2_link_list,
				GROUP15.B2R2_VMX0)},
	{"GROUP15.B2R2_VMX1", offsetof(struct b2r2_link_list,
				GROUP15.B2R2_VMX1)},
	{"GROUP15.B2R2_VMX2", offsetof(struct b2r2_link_list,
				GROUP15.B2R2_VMX2)},
	{"GROUP15.B2R2_VMX3", offsetof(struct b2r2_link_list,
				GROUP15.B2R2_VMX3)},

	{"GROUP16.B2R2_VMX0", offsetof(struct b2r2_link_list,
				GROUP16.B2R2_VMX0)},
	{"GROUP16.B2R2_VMX1", offsetof(struct b2r2_link_list,
				GROUP16.B2R2_VMX1)},
	{"GROUP16.B2R2_VMX2", offsetof(struct b2r2_link_list,
				GROUP16.B2R2_VMX2)},
	{"GROUP16.B2R2_VMX3", offsetof(struct b2r2_link_list,
				GROUP16.B2R2_VMX3)},
};

/**
 * debugfs_b2r2_blt_stat_read() - Implements debugfs read for B2R2 BLT
 *                                statistics
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_blt_stat_read(struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	size_t dev_size = 0;
	int ret = 0;
	char *Buf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);
	struct b2r2_control *cont = filp->f_dentry->d_inode->i_private;

	if (Buf == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&cont->stat_lock);
	dev_size += sprintf(Buf + dev_size, "Added jobs           : %lu\n",
		cont->stat_n_jobs_added);
	dev_size += sprintf(Buf + dev_size, "Released jobs        : %lu\n",
		cont->stat_n_jobs_released);
	dev_size += sprintf(Buf + dev_size, "Jobs in report list  : %lu\n",
		cont->stat_n_jobs_in_report_list);
	dev_size += sprintf(Buf + dev_size, "Clients in open      : %lu\n",
		cont->stat_n_in_open);
	dev_size += sprintf(Buf + dev_size, "Clients in release   : %lu\n",
		cont->stat_n_in_release);
	dev_size += sprintf(Buf + dev_size, "Clients in blt       : %lu\n",
		cont->stat_n_in_blt);
	dev_size += sprintf(Buf + dev_size, "         synch       : %lu\n",
		cont->stat_n_in_blt_synch);
	dev_size += sprintf(Buf + dev_size, "           add       : %lu\n",
		cont->stat_n_in_blt_add);
	dev_size += sprintf(Buf + dev_size, "          wait       : %lu\n",
		cont->stat_n_in_blt_wait);
	dev_size += sprintf(Buf + dev_size, "Clients in synch 0   : %lu\n",
		cont->stat_n_in_synch_0);
	dev_size += sprintf(Buf + dev_size, "Clients in synch job : %lu\n",
		cont->stat_n_in_synch_job);
	dev_size += sprintf(Buf + dev_size, "Clients in query_cap : %lu\n",
		cont->stat_n_in_query_cap);
	mutex_unlock(&cont->stat_lock);

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
	if (Buf != NULL)
		kfree(Buf);
	return ret;
}

/**
 * debugfs_b2r2_blt_stat_fops() - File operations for B2R2 BLT
 *                                statistics debugfs
 */
static const struct file_operations debugfs_b2r2_blt_stat_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_blt_stat_read,
};

/**
 * debugfs_b2r2_bypass_read() - Implements debugfs read for
 *                             B2R2 Core Enable/Disable
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_bypass_read(struct file *filp, char __user *buf,
				   size_t count, loff_t *f_pos)
{
	/* 4 characters hex number + newline + string terminator; */
	char tmpbuf[4+2];
	size_t dev_size;
	int ret = 0;
	struct b2r2_control *cont = filp->f_dentry->d_inode->i_private;

	dev_size = sprintf(tmpbuf, "%02X\n", cont->bypass);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, tmpbuf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;
out:
	return ret;
}

/**
 * debugfs_b2r2_bypass_write() - Implements debugfs write for
 *                              B2R2 Core Enable/Disable
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to write
 * @f_pos: File position
 *
 * Returns number of bytes written or negative error code
 */
static int debugfs_b2r2_bypass_write(struct file *filp, const char __user *buf,
				    size_t count, loff_t *f_pos)
{
	u8 bypass;
	int ret = 0;
	struct b2r2_control *cont = filp->f_dentry->d_inode->i_private;

	ret = kstrtou8_from_user(buf, count, 16, &bypass);
	if (ret < 0)
		return -EINVAL;

	if (bypass)
		cont->bypass = true;
	else
		cont->bypass = false;

	*f_pos += ret;

	return ret;
}

/**
 * debugfs_b2r2_bypass_fops() - File operations for B2R2 Core Enable/Disable debugfs
 */
static const struct file_operations debugfs_b2r2_bypass_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_bypass_read,
	.write = debugfs_b2r2_bypass_write,
};
#endif

static void init_tmp_bufs(struct b2r2_control *cont)
{
	int i = 0;

	for (i = 0; i < (sizeof(cont->tmp_bufs) / sizeof(struct tmp_buf));
			i++) {
		cont->tmp_bufs[i].buf.virt_addr = dma_alloc_coherent(
			cont->dev, MAX_TMP_BUF_SIZE,
			&cont->tmp_bufs[i].buf.phys_addr, GFP_DMA);
		if (cont->tmp_bufs[i].buf.virt_addr != NULL)
			cont->tmp_bufs[i].buf.size = MAX_TMP_BUF_SIZE;
		else {
			b2r2_log_err(cont->dev, "%s: Failed to allocate temp "
				"buffer %i\n", __func__, i);
			cont->tmp_bufs[i].buf.size = 0;
		}
	}
}

static void destroy_tmp_bufs(struct b2r2_control *cont)
{
	int i = 0;

	for (i = 0; i < MAX_TMP_BUFS_NEEDED; i++) {
		if (cont->tmp_bufs[i].buf.size != 0) {
			dma_free_coherent(cont->dev,
					cont->tmp_bufs[i].buf.size,
					cont->tmp_bufs[i].buf.virt_addr,
					cont->tmp_bufs[i].buf.phys_addr);

			cont->tmp_bufs[i].buf.size = 0;
		}
	}
}

/**
 * b2r2_blt_module_init() - Module init function
 *
 * Returns 0 if OK else negative error code
 */
int b2r2_control_init(struct b2r2_control *cont)
{
	int ret;

	mutex_init(&cont->stat_lock);

#ifdef CONFIG_B2R2_GENERIC
	/* Initialize generic path */
	b2r2_generic_init(cont);
#endif
	/* Initialize node splitter */
	ret = b2r2_node_split_init(cont);
	if (ret) {
		printk(KERN_WARNING "%s: node split init fails\n", __func__);
		goto b2r2_node_split_init_fail;
	}

	b2r2_log_info(cont->dev, "%s: device registered\n", __func__);

	cont->dev->coherent_dma_mask = 0xFFFFFFFF;
	init_tmp_bufs(cont);
	ret = b2r2_filters_init(cont);
	if (ret) {
		b2r2_log_warn(cont->dev, "%s: failed to init filters\n",
			__func__);
		goto b2r2_filter_init_fail;
	}

	/* Initialize memory allocator */
	ret = b2r2_mem_init(cont, B2R2_HEAP_SIZE,
			4, sizeof(struct b2r2_node));
	if (ret) {
		printk(KERN_WARNING "%s: initializing B2R2 memhandler fails\n",
				__func__);
		goto b2r2_mem_init_fail;
	}

#ifdef CONFIG_DEBUG_FS
	/* Initialize last_request_count and lock */
	cont->last_request_count = 1;
	mutex_init(&cont->last_req_lock);

	/* Register debug fs */
	if (!IS_ERR_OR_NULL(cont->debugfs_root_dir)) {
		debugfs_create_file("last_request", 0666,
			cont->debugfs_root_dir,
			cont, &debugfs_b2r2_blt_request_fops);
		debugfs_create_file("last_request_count", 0666,
			cont->debugfs_root_dir,
			cont, &debugfs_b2r2_req_count_fops);
		debugfs_create_file("stats", 0666,
			cont->debugfs_root_dir,
			cont, &debugfs_b2r2_blt_stat_fops);
		debugfs_create_file("bypass", 0666,
			cont->debugfs_root_dir,
			cont, &debugfs_b2r2_bypass_fops);
	}
#endif

	b2r2_log_info(cont->dev, "%s: done\n", __func__);

	return ret;

b2r2_mem_init_fail:
	b2r2_filters_exit(cont);
b2r2_filter_init_fail:
	b2r2_node_split_exit(cont);
b2r2_node_split_init_fail:
#ifdef CONFIG_B2R2_GENERIC
	b2r2_generic_exit(cont);
#endif
	return ret;
}

/**
 * b2r2_control_exit() - Module exit function
 */
void b2r2_control_exit(struct b2r2_control *cont)
{
	if (cont) {
		b2r2_log_info(cont->dev, "%s\n", __func__);
#ifdef CONFIG_DEBUG_FS
		if (!IS_ERR_OR_NULL(cont->debugfs_root_dir)) {
			debugfs_remove_recursive(cont->debugfs_root_dir);
			cont->debugfs_root_dir = NULL;
		}
#endif
		b2r2_mem_exit(cont);
		destroy_tmp_bufs(cont);
		b2r2_node_split_exit(cont);
#if defined(CONFIG_B2R2_GENERIC)
		b2r2_generic_exit(cont);
#endif
		b2r2_filters_exit(cont);
	}
}

MODULE_AUTHOR("Robert Fekete <robert.fekete@stericsson.com>");
MODULE_DESCRIPTION("ST-Ericsson B2R2 Blitter module");
MODULE_LICENSE("GPL");
