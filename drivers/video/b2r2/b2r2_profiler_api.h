/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 profiling API
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */


#ifndef _LINUX_VIDEO_B2R2_PROFILER_API_H
#define _LINUX_VIDEO_B2R2_PROFILER_API_H

#include "b2r2_internal.h"

/**
 * struct b2r2_blt_profiling_info - Profiling information for a blit
 * @core_id:             The b2r2 core that executed the potentially partial
 *                       b2r2 request.
 * @request_id:          The request id.
 * @pixels:              The number of pixels computed on the core.
 * @nsec_active_in_cpu:  The number of nanoseconds the job was active in the
 *                       CPU. This is an approximate value, check out the code
 *                       for more info.
 * @nsec_active_in_b2r2: The number of nanoseconds the job was active in B2R2.
 *                       This is an approximate value, check out the code for
 *                       more info.
 * @total_time_nsec:     The total time the job took in nanoseconds.
 *                       Includes idle time.
 */
struct b2r2_blt_profiling_info {
	s32 core_id;
	s32 request_id;
	s32 pixels;
	s64 nsec_active_in_cpu;
	s64 nsec_active_in_b2r2;
	s64 total_time_nsec;
};

/**
 * struct b2r2_profiler - B2R2 profiler.
 *
 * The callbacks are never run concurrently. No heavy stuff must be done in the
 * callbacks as this might adversely affect the B2R2 driver. The callbacks must
 * not call the B2R2 profiler API as this will cause a deadlock. If the
 * callbacks call into the B2R2 driver care must be taken as deadlock
 * situations can arise.
 *
 * @blt_done: Called when a blit has finished, timed out or been canceled.
 */
struct b2r2_profiler {
	void (*blt_done)(const struct b2r2_blt_request * const request,
			struct b2r2_blt_profiling_info * const info);
};

/**
 * b2r2_register_profiler() - Registers a profiler.
 *
 * Currently only one profiler can be registered at any given time.
 *
 * @profiler: The profiler
 *
 * Returns 0 on success, negative error code on failure
 */
int b2r2_register_profiler(const struct b2r2_profiler * const profiler);

/**
 * b2r2_unregister_profiler() - Unregisters a profiler.
 *
 * @profiler: The profiler
 */
void b2r2_unregister_profiler(const struct b2r2_profiler * const profiler);

#endif /* #ifdef _LINUX_VIDEO_B2R2_PROFILER_API_H */
