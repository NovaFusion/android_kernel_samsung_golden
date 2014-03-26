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

/*
 * Cache handler can not handle simultaneous execution! The caller has to
 * ensure such a situation does not occur.
 */

#ifndef _CACHE_HANDLER_H_
#define _CACHE_HANDLER_H_

#include <linux/types.h>
#include <linux/hwmem.h>

/*
 * To not have to double all datatypes we've used hwmem datatypes. If someone
 * want's to use cache handler but not hwmem then we'll have to define our own
 * datatypes.
 */

struct cach_range {
	u32 start; /* Inclusive */
	u32 end; /* Exclusive */
};

/*
 * Internal, do not touch!
 */
struct cach_buf {
	void *vstart;
	u32 pstart;
	u32 size;

	/* Remaining hints are active */
	enum hwmem_alloc_flags cache_settings;

	enum hwmem_mem_type mem_type;
	bool in_cpu_write_buf;
	struct cach_range range_in_cpu_cache;
	struct cach_range range_dirty_in_cpu_cache;
	struct cach_range range_invalid_in_cpu_cache;
};

void cach_init_buf(struct cach_buf *buf, enum hwmem_mem_type,
			enum hwmem_alloc_flags cache_settings, u32 size);

void cach_set_buf_addrs(struct cach_buf *buf, void* vaddr, u32 paddr);

void cach_set_pgprot_cache_options(struct cach_buf *buf, pgprot_t *pgprot);

void cach_set_domain(struct cach_buf *buf, enum hwmem_access access,
			enum hwmem_domain domain, struct hwmem_region *region);

#endif /* _CACHE_HANDLER_H_ */
