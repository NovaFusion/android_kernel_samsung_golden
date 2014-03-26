/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Cache handler
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/hwmem.h>

#include <asm/pgtable.h>

#include <mach/dcache.h>

#include "cache_handler.h"

#define U32_MAX (~(u32)0)

enum hwmem_alloc_flags cachi_get_cache_settings(
			enum hwmem_alloc_flags requested_cache_settings);
void cachi_set_pgprot_cache_options(enum hwmem_alloc_flags cache_settings,
							pgprot_t *pgprot);

static void sync_buf_pre_cpu(struct cach_buf *buf, enum hwmem_access access,
						struct hwmem_region *region);
static void sync_buf_post_cpu(struct cach_buf *buf,
	enum hwmem_access next_access, struct hwmem_region *next_region);

static void invalidate_cpu_cache(struct cach_buf *buf,
					struct cach_range *range_2b_used);
static void clean_cpu_cache(struct cach_buf *buf,
					struct cach_range *range_2b_used);
static void flush_cpu_cache(struct cach_buf *buf,
					struct cach_range *range_2b_used);

static void null_range(struct cach_range *range);
static void expand_range(struct cach_range *range,
					struct cach_range *range_2_add);
/*
 * Expands range to one of enclosing_range's two edges. The function will
 * choose which of enclosing_range's edges to expand range to in such a
 * way that the size of range is minimized. range must be located inside
 * enclosing_range.
 */
static void expand_range_2_edge(struct cach_range *range,
					struct cach_range *enclosing_range);
static void shrink_range(struct cach_range *range,
					struct cach_range *range_2_remove);
static bool is_non_empty_range(struct cach_range *range);
static void intersect_range(struct cach_range *range_1,
		struct cach_range *range_2, struct cach_range *intersection);
/* Align_up restrictions apply here to */
static void align_range_up(struct cach_range *range, u32 alignment);
static u32 range_length(struct cach_range *range);
static void region_2_range(struct hwmem_region *region, u32 buffer_size,
						struct cach_range *range);

static void *offset_2_vaddr(struct cach_buf *buf, u32 offset);
static u32 offset_2_paddr(struct cach_buf *buf, u32 offset);

/* Saturates, might return unaligned values when that happens */
static u32 align_up(u32 value, u32 alignment);
static u32 align_down(u32 value, u32 alignment);

/*
 * Exported functions
 */
void cach_init_buf(struct cach_buf *buf,
		   enum hwmem_mem_type mem_type,
		   enum hwmem_alloc_flags cache_settings,
		   u32 size)
{
	buf->vstart = NULL;
	buf->pstart = 0;
	buf->size = size;
	buf->mem_type = mem_type;

	buf->cache_settings = cachi_get_cache_settings(cache_settings);
}

void cach_set_buf_addrs(struct cach_buf *buf, void* vaddr, u32 paddr)
{
	bool tmp;

	buf->vstart = vaddr;
	buf->pstart = paddr;

	if (buf->cache_settings & HWMEM_ALLOC_HINT_CACHED) {
		/*
		 * Keep whatever is in the cache. This way we avoid an
		 * unnecessary synch if CPU is the first user.
		 */
		buf->range_in_cpu_cache.start = 0;
		buf->range_in_cpu_cache.end = buf->size;
		align_range_up(&buf->range_in_cpu_cache,
						get_dcache_granularity());
		buf->range_dirty_in_cpu_cache.start = 0;
		buf->range_dirty_in_cpu_cache.end = buf->size;
		align_range_up(&buf->range_dirty_in_cpu_cache,
						get_dcache_granularity());
	} else {
		flush_cpu_dcache(buf->vstart, buf->pstart, buf->size, false,
									&tmp);
		drain_cpu_write_buf();

		null_range(&buf->range_in_cpu_cache);
		null_range(&buf->range_dirty_in_cpu_cache);
	}
	null_range(&buf->range_invalid_in_cpu_cache);
}

void cach_set_pgprot_cache_options(struct cach_buf *buf, pgprot_t *pgprot)
{
	cachi_set_pgprot_cache_options(buf->cache_settings, pgprot);
}

void cach_set_domain(struct cach_buf *buf, enum hwmem_access access,
			enum hwmem_domain domain, struct hwmem_region *region)
{
	struct hwmem_region *__region;
	struct hwmem_region full_region;

	if (region != NULL) {
		__region = region;
	} else {
		full_region.offset = 0;
		full_region.count = 1;
		full_region.start = 0;
		full_region.end = buf->size;
		full_region.size = buf->size;

		__region = &full_region;
	}

	switch (domain) {
	case HWMEM_DOMAIN_SYNC:
		sync_buf_post_cpu(buf, access, __region);

		break;

	case HWMEM_DOMAIN_CPU:
		sync_buf_pre_cpu(buf, access, __region);

		break;
	}
}

/*
 * Local functions
 */

enum hwmem_alloc_flags __attribute__((weak)) cachi_get_cache_settings(
			enum hwmem_alloc_flags requested_cache_settings)
{
	static const u32 CACHE_ON_FLAGS_MASK = HWMEM_ALLOC_HINT_CACHED |
		HWMEM_ALLOC_HINT_CACHE_WB | HWMEM_ALLOC_HINT_CACHE_WT |
		HWMEM_ALLOC_HINT_CACHE_NAOW | HWMEM_ALLOC_HINT_CACHE_AOW |
				HWMEM_ALLOC_HINT_INNER_AND_OUTER_CACHE |
					HWMEM_ALLOC_HINT_INNER_CACHE_ONLY;
	/* We don't know the cache setting so we assume worst case. */
	static const u32 CACHE_SETTING = HWMEM_ALLOC_HINT_WRITE_COMBINE |
			HWMEM_ALLOC_HINT_CACHED | HWMEM_ALLOC_HINT_CACHE_WB |
						HWMEM_ALLOC_HINT_CACHE_AOW |
					HWMEM_ALLOC_HINT_INNER_AND_OUTER_CACHE;

	if (requested_cache_settings & CACHE_ON_FLAGS_MASK)
		return CACHE_SETTING;
	else if (requested_cache_settings & HWMEM_ALLOC_HINT_WRITE_COMBINE ||
		(requested_cache_settings & HWMEM_ALLOC_HINT_UNCACHED &&
		 !(requested_cache_settings &
					HWMEM_ALLOC_HINT_NO_WRITE_COMBINE)))
		return HWMEM_ALLOC_HINT_WRITE_COMBINE;
	else if (requested_cache_settings &
					(HWMEM_ALLOC_HINT_NO_WRITE_COMBINE |
						HWMEM_ALLOC_HINT_UNCACHED))
		return 0;
	else
		/* Nothing specified, use cached */
		return CACHE_SETTING;
}

void __attribute__((weak)) cachi_set_pgprot_cache_options(
		enum hwmem_alloc_flags cache_settings, pgprot_t *pgprot)
{
	if (cache_settings & HWMEM_ALLOC_HINT_CACHED)
		*pgprot = *pgprot; /* To silence compiler and checkpatch */
	else if (cache_settings & HWMEM_ALLOC_HINT_WRITE_COMBINE)
		*pgprot = pgprot_writecombine(*pgprot);
	else
		*pgprot = pgprot_noncached(*pgprot);
}

bool __attribute__((weak)) speculative_data_prefetch(void)
{
	/* We don't know so we go with the safe alternative */
	return true;
}

static void sync_buf_pre_cpu(struct cach_buf *buf, enum hwmem_access access,
						struct hwmem_region *region)
{
	bool write = access & HWMEM_ACCESS_WRITE;
	bool read = access & HWMEM_ACCESS_READ;

	if (!write && !read)
		return;

	if (buf->cache_settings & HWMEM_ALLOC_HINT_CACHED) {
		struct cach_range region_range;

		region_2_range(region, buf->size, &region_range);

		if (read || (write && buf->cache_settings &
						HWMEM_ALLOC_HINT_CACHE_WB))
			/* Perform defered invalidates */
			invalidate_cpu_cache(buf, &region_range);
		if (read || (write && buf->cache_settings &
						HWMEM_ALLOC_HINT_CACHE_AOW))
			expand_range(&buf->range_in_cpu_cache, &region_range);
		if (write && buf->cache_settings & HWMEM_ALLOC_HINT_CACHE_WB) {
			struct cach_range dirty_range_addition;

			if (buf->cache_settings & HWMEM_ALLOC_HINT_CACHE_AOW)
				dirty_range_addition = region_range;
			else
				intersect_range(&buf->range_in_cpu_cache,
					&region_range, &dirty_range_addition);

			expand_range(&buf->range_dirty_in_cpu_cache,
							&dirty_range_addition);
		}
	}
	if (buf->cache_settings & HWMEM_ALLOC_HINT_WRITE_COMBINE) {
		if (write)
			buf->in_cpu_write_buf = true;
	}
}

static void sync_buf_post_cpu(struct cach_buf *buf,
	enum hwmem_access next_access, struct hwmem_region *next_region)
{
	bool write = next_access & HWMEM_ACCESS_WRITE;
	bool read = next_access & HWMEM_ACCESS_READ;
	struct cach_range region_range;

	if (!write && !read)
		return;

	region_2_range(next_region, buf->size, &region_range);

	if (write) {
		if (speculative_data_prefetch()) {
			/* Defer invalidate */
			struct cach_range intersection;

			intersect_range(&buf->range_in_cpu_cache,
						&region_range, &intersection);

			expand_range(&buf->range_invalid_in_cpu_cache,
								&intersection);

			clean_cpu_cache(buf, &region_range);
		} else {
			flush_cpu_cache(buf, &region_range);
		}
	}
	if (read)
		clean_cpu_cache(buf, &region_range);

	if (buf->in_cpu_write_buf) {
		drain_cpu_write_buf();

		buf->in_cpu_write_buf = false;
	}
}

static void invalidate_cpu_cache(struct cach_buf *buf, struct cach_range *range)
{
	struct cach_range intersection;

	intersect_range(&buf->range_invalid_in_cpu_cache, range,
								&intersection);
	if (is_non_empty_range(&intersection)) {
		bool flushed_everything;

		expand_range_2_edge(&intersection,
					&buf->range_invalid_in_cpu_cache);

		/*
		 * Cache handler never uses invalidate to discard data in the
		 * cache so we can use flush instead which is considerably
		 * faster for large buffers.
		 */
		flush_cpu_dcache(
				offset_2_vaddr(buf, intersection.start),
				offset_2_paddr(buf, intersection.start),
				range_length(&intersection),
				buf->cache_settings &
					HWMEM_ALLOC_HINT_INNER_CACHE_ONLY,
							&flushed_everything);

		if (flushed_everything) {
			null_range(&buf->range_invalid_in_cpu_cache);
			null_range(&buf->range_dirty_in_cpu_cache);
		} else {
			/*
			 * No need to shrink range_in_cpu_cache as invalidate
			 * is only used when we can't keep track of what's in
			 * the CPU cache.
			 */
			shrink_range(&buf->range_invalid_in_cpu_cache,
								&intersection);
		}
	}
}

static void clean_cpu_cache(struct cach_buf *buf, struct cach_range *range)
{
	struct cach_range intersection;

	intersect_range(&buf->range_dirty_in_cpu_cache, range, &intersection);
	if (is_non_empty_range(&intersection)) {
		bool cleaned_everything;

		expand_range_2_edge(&intersection,
					&buf->range_dirty_in_cpu_cache);

		clean_cpu_dcache(
				offset_2_vaddr(buf, intersection.start),
				offset_2_paddr(buf, intersection.start),
				range_length(&intersection),
				buf->cache_settings &
					HWMEM_ALLOC_HINT_INNER_CACHE_ONLY,
							&cleaned_everything);

		if (cleaned_everything)
			null_range(&buf->range_dirty_in_cpu_cache);
		else
			shrink_range(&buf->range_dirty_in_cpu_cache,
								&intersection);

		if (buf->mem_type == HWMEM_MEM_SCATTERED_SYS)
			outer_flush_all();
	}
}

static void flush_cpu_cache(struct cach_buf *buf, struct cach_range *range)
{
	struct cach_range intersection;

	intersect_range(&buf->range_in_cpu_cache, range, &intersection);
	if (is_non_empty_range(&intersection)) {
		bool flushed_everything;

		expand_range_2_edge(&intersection, &buf->range_in_cpu_cache);

		flush_cpu_dcache(
				offset_2_vaddr(buf, intersection.start),
				offset_2_paddr(buf, intersection.start),
				range_length(&intersection),
				buf->cache_settings &
					HWMEM_ALLOC_HINT_INNER_CACHE_ONLY,
							&flushed_everything);

		if (flushed_everything) {
			if (!speculative_data_prefetch())
				null_range(&buf->range_in_cpu_cache);
			null_range(&buf->range_dirty_in_cpu_cache);
			null_range(&buf->range_invalid_in_cpu_cache);
		} else {
			if (!speculative_data_prefetch())
				shrink_range(&buf->range_in_cpu_cache,
							 &intersection);
			shrink_range(&buf->range_dirty_in_cpu_cache,
								&intersection);
			shrink_range(&buf->range_invalid_in_cpu_cache,
								&intersection);
		}
	}
}

static void null_range(struct cach_range *range)
{
	range->start = U32_MAX;
	range->end = 0;
}

static void expand_range(struct cach_range *range,
						struct cach_range *range_2_add)
{
	range->start = min(range->start, range_2_add->start);
	range->end = max(range->end, range_2_add->end);
}

/*
 * Expands range to one of enclosing_range's two edges. The function will
 * choose which of enclosing_range's edges to expand range to in such a
 * way that the size of range is minimized. range must be located inside
 * enclosing_range.
 */
static void expand_range_2_edge(struct cach_range *range,
					struct cach_range *enclosing_range)
{
	u32 space_on_low_side = range->start - enclosing_range->start;
	u32 space_on_high_side = enclosing_range->end - range->end;

	if (space_on_low_side < space_on_high_side)
		range->start = enclosing_range->start;
	else
		range->end = enclosing_range->end;
}

static void shrink_range(struct cach_range *range,
					struct cach_range *range_2_remove)
{
	if (range_2_remove->start > range->start)
		range->end = min(range->end, range_2_remove->start);
	else
		range->start = max(range->start, range_2_remove->end);

	if (range->start >= range->end)
		null_range(range);
}

static bool is_non_empty_range(struct cach_range *range)
{
	return range->end > range->start;
}

static void intersect_range(struct cach_range *range_1,
		struct cach_range *range_2, struct cach_range *intersection)
{
	intersection->start = max(range_1->start, range_2->start);
	intersection->end = min(range_1->end, range_2->end);

	if (intersection->start >= intersection->end)
		null_range(intersection);
}

/* Align_up restrictions apply here to */
static void align_range_up(struct cach_range *range, u32 alignment)
{
	if (!is_non_empty_range(range))
		return;

	range->start = align_down(range->start, alignment);
	range->end = align_up(range->end, alignment);
}

static u32 range_length(struct cach_range *range)
{
	if (is_non_empty_range(range))
		return range->end - range->start;
	else
		return 0;
}

static void region_2_range(struct hwmem_region *region, u32 buffer_size,
						struct cach_range *range)
{
	/*
	 * We don't care about invalid regions, instead we limit the region's
	 * range to the buffer's range. This should work good enough, worst
	 * case we synch the entire buffer when we get an invalid region which
	 * is acceptable.
	 */
	range->start = region->offset + region->start;
	range->end = min(region->offset + (region->count * region->size) -
				(region->size - region->end), buffer_size);
	if (range->start >= range->end) {
		null_range(range);
		return;
	}

	align_range_up(range, get_dcache_granularity());
}

static void *offset_2_vaddr(struct cach_buf *buf, u32 offset)
{
	return (void *)((u32)buf->vstart + offset);
}

static u32 offset_2_paddr(struct cach_buf *buf, u32 offset)
{
	return buf->pstart + offset;
}

/* Saturates, might return unaligned values when that happens */
static u32 align_up(u32 value, u32 alignment)
{
	u32 remainder = value % alignment;
	u32 value_2_add;

	if (remainder == 0)
		return value;

	value_2_add = alignment - remainder;

	if (value_2_add > U32_MAX - value) /* Will overflow */
		return U32_MAX;

	return value + value_2_add;
}

static u32 align_down(u32 value, u32 alignment)
{
	u32 remainder = value % alignment;
	if (remainder == 0)
		return value;

	return value - remainder;
}
