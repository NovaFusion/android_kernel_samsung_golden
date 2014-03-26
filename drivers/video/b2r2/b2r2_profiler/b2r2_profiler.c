/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson B2R2 profiler implementation
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 *         Jorgen Nilsson <jorgen.nilsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/jiffies.h>

#include <video/b2r2_blt.h>
#include "../b2r2_internal.h"
#include "../b2r2_profiler_api.h"

#define MAX_BLITS (400)
#define MAX_DROPS (4000)
#define BLIT_CACHE_SIZE (10*B2R2_MAX_NBR_DEVICES)

static int src_format_filter_on = false;
module_param(src_format_filter_on, bool, S_IRUGO | S_IWUSR);

static unsigned int src_format_filter;
module_param(src_format_filter, uint, S_IRUGO | S_IWUSR);

static int print_blts_on = 0;
module_param(print_blts_on, bool, S_IRUGO | S_IWUSR);

static int use_mpix_per_second_in_print_blts = 1;
module_param(use_mpix_per_second_in_print_blts, bool, S_IRUGO | S_IWUSR);

static int profiler_stats_on = 1;
module_param(profiler_stats_on, bool, S_IRUGO | S_IWUSR);

static const unsigned int profiler_stats_blts_used = MAX_BLITS;
static struct {
	unsigned long sampling_start_time_jiffies;

	s32 min_mpix_per_second;
	struct b2r2_blt_req min_blt_request;
	struct b2r2_blt_profiling_info min_blt_profiling_info;

	s32 max_mpix_per_second;
	struct b2r2_blt_req max_blt_request;
	struct b2r2_blt_profiling_info max_blt_profiling_info;

	s32 num_pixels[B2R2_MAX_NBR_DEVICES];
	s32 num_usecs[B2R2_MAX_NBR_DEVICES];

	s32 accumulated_num_pixels;
	s32 accumulated_num_usecs;

	u32 num_blts_done;
} profiler_stats;

static int drops;
static struct b2r2_blt_profiling_info cache[BLIT_CACHE_SIZE];

/* LOCAL FUNCTIONS */

static s32 nsec_2_usec(const s32 nsec)
{
	return nsec / 1000;
}

static int is_scale_blt(const struct b2r2_blt_req * const req)
{
	if (((req->transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) &&
			(req->src_rect.width !=
				req->dst_rect.height ||
			req->src_rect.height !=
				req->dst_rect.width)) ||
		(!(req->transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) &&
			(req->src_rect.width !=
				req->dst_rect.width ||
			req->src_rect.height !=
				req->dst_rect.height)))
		return 1;
	else
		return 0;
}

static s32 get_num_pixels_in_blt(const struct b2r2_blt_request * const req)
{
	s32 num_pixels_in_src = req->user_req.src_rect.width *
			req->user_req.src_rect.height;
	s32 num_pixels_in_dst = req->user_req.dst_rect.width *
			req->user_req.dst_rect.height;
	if (req->user_req.flags & (B2R2_BLT_FLAG_SOURCE_FILL |
				B2R2_BLT_FLAG_SOURCE_FILL_RAW))
		return num_pixels_in_dst;
	else
		return (num_pixels_in_src + num_pixels_in_dst) / 2;
}

static s32 get_mpix_per_second(const s32 num_pixels, const s32 num_usecs)
{
	if (num_usecs == 0)
		return 0;
	return num_pixels / num_usecs;
}

static s32 get_blt_mpix_per_second(
			const struct b2r2_blt_profiling_info * const info)
{
	return get_mpix_per_second(info->pixels,
			nsec_2_usec((s32)(info->nsec_active_in_cpu +
					info->nsec_active_in_b2r2)));
}

static void print_blt(const struct b2r2_blt_req * const req,
	const struct b2r2_blt_profiling_info * const info)
{
	char tmp_str[128];
	sprintf(tmp_str, "[core%d] id: %10i, src_fmt: %#10x, dst_fmt: %#10x, "
		"flags: %#10x, transform: %#3x, scaling: %1i, pixels: %7i",
		info->core_id,
		info->request_id,
		req->src_img.fmt,
		req->dst_img.fmt,
		req->flags,
		req->transform,
		is_scale_blt(req),
		info->pixels);
	if (use_mpix_per_second_in_print_blts)
		printk(KERN_ALERT "%s, MPix/s: %3i\n", tmp_str,
			get_blt_mpix_per_second(info));
	else
		printk(KERN_ALERT "%s, CPU: %14lld, B2R2: %14lld, "
			"Tot: %14lld ns\n",
			tmp_str,
			info->nsec_active_in_cpu,
			info->nsec_active_in_b2r2,
			info->total_time_nsec);
}

static void print_profiler_stats(void)
{
	if (B2R2_MAX_NBR_DEVICES > 1 &&
			(profiler_stats.num_pixels[0] > 0) &&
			(profiler_stats.num_pixels[1] > 0)) {
		int i;
		printk(KERN_ALERT "\nAverage overall blit speed on "
			"all active cores for %d jobs "
			"(%dpx, %dus): %3i MPix/s\n",
			profiler_stats.num_blts_done,
			profiler_stats.accumulated_num_pixels,
			profiler_stats.accumulated_num_usecs,
			get_mpix_per_second(
				profiler_stats.accumulated_num_pixels,
				profiler_stats.accumulated_num_usecs));
		if (drops > 0)
			printk(KERN_ALERT "Dropped %d blits due to cache overflow\n",
					drops);
		for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
			printk(KERN_ALERT "Average blit speed on core%d "
					"(%dpx, %dus): %3i MPix/s\n", i,
					profiler_stats.num_pixels[i],
					profiler_stats.num_usecs[i],
					get_mpix_per_second(
						profiler_stats.num_pixels[i],
						profiler_stats.num_usecs[i]));
		}
	} else {
		printk(KERN_ALERT "\nAverage blit speed for %d jobs "
			"(%dpx, %dus): %3i MPix/s\n",
			profiler_stats.num_blts_done,
			profiler_stats.accumulated_num_pixels,
			profiler_stats.accumulated_num_usecs,
			get_mpix_per_second(
				profiler_stats.accumulated_num_pixels,
				profiler_stats.accumulated_num_usecs));
	}

	printk(KERN_ALERT "Slowest blit (%3i MPix/s):\n",
			profiler_stats.min_mpix_per_second);
	print_blt(&profiler_stats.min_blt_request,
		&profiler_stats.min_blt_profiling_info);
	printk(KERN_ALERT "Fastest blit (%3i MPix/s):\n",
			profiler_stats.max_mpix_per_second);
	print_blt(&profiler_stats.max_blt_request,
		&profiler_stats.max_blt_profiling_info);
}

static void reset_profiler_stats(void)
{
	int i;

	profiler_stats.sampling_start_time_jiffies = jiffies;
	profiler_stats.min_mpix_per_second = INT_MAX;
	profiler_stats.max_mpix_per_second = 0;
	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		profiler_stats.num_pixels[i] = 0;
		profiler_stats.num_usecs[i] = 0;
	}
	profiler_stats.accumulated_num_pixels = 0;
	profiler_stats.accumulated_num_usecs = 0;
	profiler_stats.num_blts_done = 0;

	memset(cache, 0, sizeof(cache));
	drops = 0;
}

static bool request_complete(
		struct b2r2_blt_profiling_info *cache_prof,
		const uint32_t size_cache_prof,
		const struct b2r2_blt_request * const req,
		const struct b2r2_blt_profiling_info * const info)
{
	int i;
	int core_mask = req->core_mask;

	core_mask &= ~(1 << info->core_id);

	for (i = 0; i < size_cache_prof; i++) {
		if (cache_prof[i].request_id == req->request_id)
			core_mask &= ~(1 << cache_prof[i].core_id);
	}

	pr_debug("%s: core_mask: 0x%08X, id: %d, done: 0x%08X\n",
			__func__, req->core_mask, req->request_id, core_mask);

	return (core_mask == 0);
}

static s32 get_accumulated_pixels(struct b2r2_blt_profiling_info *cache_prof,
		const uint32_t size_cache_prof,
		const struct b2r2_blt_request * const req,
		const struct b2r2_blt_profiling_info * const info)
{
	int i;
	s32 pixels = 0;

	for (i = 0; i < size_cache_prof; i++) {
		if (cache_prof[i].request_id == req->request_id)
			pixels += cache_prof[i].pixels;
	}
	pixels += info->pixels;

	return pixels;
}

static s32 get_accumulated_usecs(struct b2r2_blt_profiling_info *cache_prof,
		const uint32_t size_cache_prof,
		const struct b2r2_blt_request * const req,
		const struct b2r2_blt_profiling_info * const info)
{
	int i;
	s32 usecs;
	s32 tsetup;
	s32 thw;

	tsetup = nsec_2_usec(info->nsec_active_in_cpu);
	thw = nsec_2_usec(info->nsec_active_in_b2r2);
	usecs = tsetup + thw;

	for (i = 0; i < size_cache_prof; i++) {
		if (cache_prof[i].request_id == req->request_id) {
			s32 t2setup = nsec_2_usec(
					cache_prof[i].nsec_active_in_cpu);
			s32 t2hw = nsec_2_usec(
					cache_prof[i].nsec_active_in_b2r2);

			/* Only include overhead from non parallel activity */
			usecs += max(t2setup + t2hw - thw, 0);

			tsetup = t2setup;
			thw = t2hw;
		}
	}

	return usecs;
}

static void clear_profiling_info(
		struct b2r2_blt_profiling_info *cache_prof,
		const uint32_t size_cache_prof,
		const struct b2r2_blt_request * const req)
{
	int i;

	pr_debug("%s: id: %d\n", __func__, req->request_id);

	for (i = 0; i < size_cache_prof; i++) {
		if (cache_prof[i].request_id == req->request_id) {
			cache_prof[i].request_id = 0;
			pr_debug("%s: Cleared cache[%d]\n", __func__, i);
		}
	}
}

static void print_profiler_infos(
		struct b2r2_blt_profiling_info *cache_prof,
		const uint32_t size_cache_prof)
{
	int i;

	for (i = 0; i < size_cache_prof; i++) {
		pr_debug("%s: core: %d, id: %d, pixels: %7i, time: %14i us\n",
			__func__,
			cache_prof[i].core_id,
			cache_prof[i].request_id,
			cache_prof[i].pixels,
			nsec_2_usec(cache_prof[i].total_time_nsec));
	}

}

static void save_profiling_info(
		struct b2r2_blt_profiling_info *cache_prof,
		const uint32_t size_cache_prof,
		const struct b2r2_blt_profiling_info * const info)
{
	int i;
	struct b2r2_blt_profiling_info *pEmpty = NULL;

	pr_debug("%s: id: %d\n", __func__, info->request_id);

	for (i = 0; i < size_cache_prof; i++) {
		if (cache_prof[i].request_id == 0) {
			pEmpty = &cache_prof[i];
			pr_debug("%s: Save @ cache[%d]\n",
				__func__, i);
			break;
		}
	}

	if (pEmpty != NULL) {
		memcpy(pEmpty, info, sizeof(struct b2r2_blt_profiling_info));
	} else {
		pr_debug("%s: Cache is full, dropping stat id: %d\n",
			__func__, info->request_id);
		drops++;
		if (drops > MAX_DROPS) {
			/*
			 * This situation can occur when issuing insane amounts
			 * of asynchronous blit requests (i.e. not a real use
			 * case). There is a drift in the completion of jobs
			 * from the different core queues. Probably has
			 * something to do with bus bandwidth being different
			 * in the two subsystems when running parallel
			 * activity. A "solution" would be to use just the one
			 * queue for jobs, but this would add an unnecessary
			 * complexity.
			 */
			pr_debug("%s: Drop count exceeded by id: %d, resetting"
				" stats\n",
				__func__, info->request_id);
			print_profiler_infos(cache_prof, size_cache_prof);
			reset_profiler_stats();
		}
	}
}

static void do_profiler_stats(const struct b2r2_blt_request * const req,
	const struct b2r2_blt_profiling_info * const info)
{
	s32 blt_px;
	s32 blt_time;
	s32 blt_mpix_per_second;

	if (time_before(jiffies, profiler_stats.sampling_start_time_jiffies)) {
		/* Flush cached entries */
		clear_profiling_info(cache, BLIT_CACHE_SIZE, req);
		return;
	}

	/* Save fastest and slowest blit */
	blt_px = info->pixels;
	blt_time = nsec_2_usec((s32)(info->nsec_active_in_cpu +
			info->nsec_active_in_b2r2));
	blt_mpix_per_second = get_mpix_per_second(blt_px, blt_time);

	if (blt_mpix_per_second <= profiler_stats.min_mpix_per_second) {
		profiler_stats.min_mpix_per_second = blt_mpix_per_second;
		memcpy(&profiler_stats.min_blt_request,
			&req->user_req,
			sizeof(profiler_stats.min_blt_request));
		memcpy(&profiler_stats.min_blt_profiling_info,
			info,
			sizeof(struct b2r2_blt_profiling_info));
	}

	if (blt_mpix_per_second >= profiler_stats.max_mpix_per_second) {
		profiler_stats.max_mpix_per_second = blt_mpix_per_second;
		memcpy(&profiler_stats.max_blt_request,
			&req->user_req,
			sizeof(profiler_stats.max_blt_request));
		memcpy(&profiler_stats.max_blt_profiling_info,
			info,
			sizeof(struct b2r2_blt_profiling_info));
	}

	profiler_stats.num_pixels[info->core_id] += blt_px;
	profiler_stats.num_usecs[info->core_id] += blt_time;

	/* Save stats to cache */
	if (!request_complete(cache, BLIT_CACHE_SIZE, req, info)) {
		save_profiling_info(cache, BLIT_CACHE_SIZE, info);
		return;
	}

	/* Calculate the stats for the entire blit job */
	blt_px = get_accumulated_pixels(cache, BLIT_CACHE_SIZE,
			req, info);
	blt_time = get_accumulated_usecs(cache, BLIT_CACHE_SIZE,
			req, info);

	/* Flush cached entries */
	clear_profiling_info(cache, BLIT_CACHE_SIZE, req);

	/* Accumulate stats */
	profiler_stats.accumulated_num_pixels += blt_px;
	profiler_stats.accumulated_num_usecs += blt_time;
	profiler_stats.num_blts_done++;

	/* Print stats when we reach the configured number of blits to use */
	if (profiler_stats.num_blts_done >= profiler_stats_blts_used) {
		print_profiler_stats();
		reset_profiler_stats();

		/*
		 * The printouts initiated above can disturb the next
		 * measurement so we delay it two seconds to give the
		 * printouts a chance to finish.
		 */
		profiler_stats.sampling_start_time_jiffies =
				jiffies + (2 * HZ);
	}
}

static void blt_done(const struct b2r2_blt_request * const req,
		struct b2r2_blt_profiling_info * const info)
{
	/* Filters */
	if (src_format_filter_on && (req->user_req.src_img.fmt !=
			src_format_filter))
		return;

	info->pixels = get_num_pixels_in_blt(req);

	/* Process stats */
	if (print_blts_on)
		print_blt(&req->user_req, info);

	if (profiler_stats_on)
		do_profiler_stats(req, info);
}

static struct b2r2_profiler this = {
	.blt_done = blt_done,
};

static int __init b2r2_profiler_init(void)
{
	reset_profiler_stats();

	return b2r2_register_profiler(&this);
}
module_init(b2r2_profiler_init);

static void __exit b2r2_profiler_exit(void)
{
	b2r2_unregister_profiler(&this);
}
module_exit(b2r2_profiler_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johan Mossberg (johan.xx.mossberg@stericsson.com)");
MODULE_DESCRIPTION("B2R2 Profiler");
