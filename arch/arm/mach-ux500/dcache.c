/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Cache handler integration and data cache helpers.
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/dma-mapping.h>

#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#include <asm/system.h>

/*
 * Values are derived from measurements on HREFP_1.1_V32_OM_S10 running
 * u8500-android-2.2_r1.1_v0.21.
 *
 * A lot of time can be spent trying to figure out the perfect breakpoints but
 * for now I've chosen the following simple way.
 *
 * breakpoint = best_case + (worst_case - best_case) * 0.666
 * The breakpoint is moved slightly towards the worst case because a full
 * clean/flush affects the entire system so we should be a bit careful.
 *
 * BEST CASE:
 * Best case is that the cache is empty and the system is idling. The case
 * where the cache contains only targeted data could be better in some cases
 * but it's hard to do measurements and calculate on that case so I choose the
 * easier alternative.
 *
 * inner_clean_breakpoint = time_2_range_clean_on_empty_cache(
 *					complete_clean_on_empty_cache_time)
 * inner_flush_breakpoint = time_2_range_flush_on_empty_cache(
 *					complete_flush_on_empty_cache_time)
 *
 * outer_clean_breakpoint = time_2_range_clean_on_empty_cache(
 *					complete_clean_on_empty_cache_time)
 * outer_flush_breakpoint = time_2_range_flush_on_empty_cache(
 *					complete_flush_on_empty_cache_time)
 *
 * WORST CASE:
 * Worst case is that the cache is filled with dirty non targeted data that
 * will be used after the synchronization and the system is under heavy load.
 *
 * inner_clean_breakpoint = time_2_range_clean_on_empty_cache(
 *				complete_clean_on_full_cache_time * 1.5)
 * Times 1.5 because it runs on both cores half the time.
 * inner_flush_breakpoint = time_2_range_flush_on_empty_cache(
 *				complete_flush_on_full_cache_time * 1.5 +
 *					complete_flush_on_full_cache_time / 2)
 * Plus "complete_flush_on_full_cache_time / 2" because all data has to be read
 * back, here we assume that both cores can fill their cache simultaneously
 * (seems to be the case as operations on full and empty inner cache takes
 * roughly the same amount of time ie the bus to outer is not the bottle neck).
 *
 * outer_clean_breakpoint = time_2_range_clean_on_empty_cache(
 *					complete_clean_on_full_cache_time +
 *					(complete_clean_on_full_cache_time -
 *					complete_clean_on_empty_cache_time))
 * Plus "(complete_flush_on_full_cache_time -
 * complete_flush_on_empty_cache_time)" because no one else can work when we
 * hog the bus with our unecessary transfer.
 * outer_flush_breakpoint = time_2_range_flush_on_empty_cache(
 *					complete_flush_on_full_cache_time * 2 +
 *					(complete_flush_on_full_cache_time -
 *				complete_flush_on_empty_cache_time) * 2)
 *
 * These values might have to be updated if changes are made to the CPU, L2$,
 * memory bus or memory.
 */
/* 28930 */
static const u32 inner_clean_breakpoint = 21324 + (32744 - 21324) * 0.666;
/* 36224 */
static const u32 inner_flush_breakpoint = 21324 + (43697 - 21324) * 0.666;
/* 254069 */
static const u32 outer_clean_breakpoint = 68041 + (347363 - 68041) * 0.666;
/* 485414 */
static const u32 outer_flush_breakpoint = 68041 + (694727 - 68041) * 0.666;

static void __clean_inner_dcache_all(void *param);
static void clean_inner_dcache_all(void);

static void __flush_inner_dcache_all(void *param);
static void flush_inner_dcache_all(void);

static bool is_cache_exclusive(void);

void drain_cpu_write_buf(void)
{
	dsb();
	outer_cache.sync();
}

void clean_cpu_dcache(void *vaddr, u32 paddr, u32 length, bool inner_only,
						bool *cleaned_everything)
{
	/*
	 * There is no problem with exclusive caches here as the Cortex-A9
	 * documentation (8.1.4. Exclusive L2 cache) says that when a dirty
	 * line is moved from L2 to L1 it is first written to mem. Because
	 * of this there is no way a line can avoid the clean by jumping
	 * between the cache levels.
	 */
	*cleaned_everything = true;

	if (length < inner_clean_breakpoint) {
		/* Inner clean range */
		dmac_map_area(vaddr, length, DMA_TO_DEVICE);
		*cleaned_everything = false;
	} else {
		clean_inner_dcache_all();
	}

	if (!inner_only) {
		/*
		 * There is currently no outer_cache.clean_all() so we use
		 * flush instead, which is ok as clean is a subset of flush.
		 * Clean range and flush range take the same amount of time
		 * so we can use outer_flush_breakpoint here.
		 */
		if (length < outer_flush_breakpoint) {
			outer_cache.clean_range(paddr, paddr + length);
			*cleaned_everything = false;
		} else {
			outer_cache.flush_all();
		}
	}
}

void flush_cpu_dcache(void *vaddr, u32 paddr, u32 length, bool inner_only,
						bool *flushed_everything)
{
	/*
	 * There might still be stale data in the caches after this call if the
	 * cache levels are exclusive. The follwing can happen.
	 * 1. Clean L1 moves the data to L2.
	 * 2. Speculative prefetch, preemption or loads on the other core moves
	 * all the data back to L1, any dirty data will be written to mem as a
	 * result of this.
	 * 3. Flush L2 does nothing as there is no targeted data in L2.
	 * 4. Flush L1 moves the data to L2. Notice that this does not happen
	 * when the cache levels are non-exclusive as clean pages are not
	 * written to L2 in that case.
	 * 5. Stale data is still present in L2!
	 * I see two possible solutions, don't use exclusive caches or
	 * (temporarily) disable prefetching to L1, preeemption and the other
	 * core.
	 *
	 * A situation can occur where the operation does not seem atomic from
	 * the other core's point of view, even on a non-exclusive cache setup.
	 * Replace step 2 in the previous scenarion with a write from the other
	 * core. The other core will write on top of the old data but the
	 * result will not be written to memory. One would expect either that
	 * the write was performed on top of the old data and was written to
	 * memory (the write occured before the flush) or that the write was
	 * performed on top of the new data and was not written to memory (the
	 * write occured after the flush). The same problem can occur with one
	 * core if kernel preemption is enabled. The solution is to
	 * (temporarily) disable the other core and preemption. I can't think
	 * of any situation where this would be a problem and disabling the
	 * other core for the duration of this call is mighty expensive so for
	 * now I just ignore the problem.
	 */

	*flushed_everything = true;

	if (!inner_only) {
		/*
		 * Beautiful solution for the exclusive problems :)
		 */
		if (is_cache_exclusive())
			panic("%s can't handle exclusive CPU caches\n",
								__func__);

		if (length < inner_clean_breakpoint) {
			/* Inner clean range */
			dmac_map_area(vaddr, length, DMA_TO_DEVICE);
			*flushed_everything = false;
		} else {
			clean_inner_dcache_all();
		}

		if (length < outer_flush_breakpoint) {
			outer_cache.flush_range(paddr, paddr + length);
			*flushed_everything = false;
		} else {
			outer_cache.flush_all();
		}
	}

	if (length < inner_flush_breakpoint) {
		/* Inner flush range */
		dmac_flush_range(vaddr, (void *)((u32)vaddr + length));
		*flushed_everything = false;
	} else {
		flush_inner_dcache_all();
	}
}

bool speculative_data_prefetch(void)
{
	return true;
}

u32 get_dcache_granularity(void)
{
	return 32;
}

/*
 * Local functions
 */

static void __clean_inner_dcache_all(void *param)
{
	__cpuc_clean_dcache_all();
}

static void clean_inner_dcache_all(void)
{
	on_each_cpu(__clean_inner_dcache_all, NULL, 1);
}

static void __flush_inner_dcache_all(void *param)
{
	__cpuc_flush_dcache_all();
}

static void flush_inner_dcache_all(void)
{
	on_each_cpu(__flush_inner_dcache_all, NULL, 1);
}

static bool is_cache_exclusive(void)
{
	static const u32 CA9_ACTLR_EXCL = 0x80;

	u32 armv7_actlr;

	asm (
		"mrc	p15, 0, %0, c1, c0, 1"
		: "=r" (armv7_actlr)
	);

	if (armv7_actlr & CA9_ACTLR_EXCL)
		return true;
	else
		return false;
}
