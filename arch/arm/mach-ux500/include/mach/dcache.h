/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Data cache helpers
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _MACH_UX500_DCACHE_H_
#define _MACH_UX500_DCACHE_H_

#include <linux/types.h>

void drain_cpu_write_buf(void);
void clean_cpu_dcache(void *vaddr, u32 paddr, u32 length, bool inner_only,
						bool *cleaned_everything);
void flush_cpu_dcache(void *vaddr, u32 paddr, u32 length, bool inner_only,
						bool *flushed_everything);
bool speculative_data_prefetch(void);
/* Returns 1 if no cache is present */
u32 get_dcache_granularity(void);

#endif /* _MACH_UX500_DCACHE_H_ */
