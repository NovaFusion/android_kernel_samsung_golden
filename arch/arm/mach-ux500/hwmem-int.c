/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Hardware memory driver integration
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/hwmem.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/slab.h>

/* CONA API */
void *cona_create(const char *name, phys_addr_t region_paddr,
							size_t region_size);
void *cona_alloc(void *instance, size_t size);
void cona_free(void *instance, void *alloc);
phys_addr_t cona_get_alloc_paddr(void *alloc);
void *cona_get_alloc_kaddr(void *instance, void *alloc);
size_t cona_get_alloc_size(void *alloc);

/* SCATT API */
void *scatt_create(const char *name);
void *scatt_alloc(void *instance, size_t size);
void scatt_free(void *instance, void *alloc);
size_t scatt_get_alloc_size(void *alloc);
struct page **scatt_get_alloc_sglist(void *alloc);

struct hwmem_mem_type_struct *hwmem_mem_types;
unsigned int hwmem_num_mem_types;

static phys_addr_t hwmem_paddr;
static size_t hwmem_size;

static phys_addr_t hwmem_prot_paddr;
static size_t hwmem_prot_size;

static phys_addr_t hwmem_static_paddr;
static size_t hwmem_static_size;

static int __init parse_hwmem_prot_param(char *p)
{

	hwmem_prot_size = memparse(p, &p);

	if (*p != '@')
		goto no_at;

	hwmem_prot_paddr = memparse(p + 1, &p);

	return 0;

no_at:
	hwmem_prot_size = 0;

	return -EINVAL;
}
early_param("hwmem_prot", parse_hwmem_prot_param);

static int __init parse_hwmem_param(char *p)
{
	hwmem_size = memparse(p, &p);

	if (*p != '@')
		goto no_at;

	hwmem_paddr = memparse(p + 1, &p);

	return 0;

no_at:
	hwmem_size = 0;

	return -EINVAL;
}
early_param("hwmem", parse_hwmem_param);

static int __init parse_hwmem_static_param(char *p)
{
	hwmem_static_size = memparse(p, &p);

	if (*p != '@')
		goto no_at;

	hwmem_static_paddr = memparse(p + 1, &p);

	return 0;
no_at:
	hwmem_static_size = 0;

	return -EINVAL;
}
early_param("hwmem_static", parse_hwmem_static_param);

static int __init setup_hwmem(void)
{
	static const unsigned int NUM_MEM_TYPES = 4;

	int ret;

	if (hwmem_paddr != PAGE_ALIGN(hwmem_paddr) ||
		hwmem_size != PAGE_ALIGN(hwmem_size) || hwmem_size == 0) {
		printk(KERN_WARNING "HWMEM: hwmem_paddr !="
		" PAGE_ALIGN(hwmem_paddr) || hwmem_size !="
		" PAGE_ALIGN(hwmem_size) || hwmem_size == 0\n");
		return -ENOMSG;
	}

	hwmem_mem_types = kzalloc(sizeof(struct hwmem_mem_type_struct) *
						NUM_MEM_TYPES, GFP_KERNEL);
	if (hwmem_mem_types == NULL)
		return -ENOMEM;

	hwmem_mem_types[0].id = HWMEM_MEM_SCATTERED_SYS;
	hwmem_mem_types[0].allocator_api.alloc = scatt_alloc;
	hwmem_mem_types[0].allocator_api.free = scatt_free;
	hwmem_mem_types[0].allocator_api.get_alloc_size = scatt_get_alloc_size;
	hwmem_mem_types[0].allocator_api.get_alloc_sglist = scatt_get_alloc_sglist;
	hwmem_mem_types[0].allocator_instance = scatt_create("hwmem_scatt");
	if (IS_ERR(hwmem_mem_types[0].allocator_instance)) {
		ret = PTR_ERR(hwmem_mem_types[0].allocator_instance);
		goto hwmem_ima_init_failed;
	}

	hwmem_mem_types[1].id = HWMEM_MEM_CONTIGUOUS_SYS;
	hwmem_mem_types[1].allocator_api.alloc = cona_alloc;
	hwmem_mem_types[1].allocator_api.free = cona_free;
	hwmem_mem_types[1].allocator_api.get_alloc_paddr = cona_get_alloc_paddr;
	hwmem_mem_types[1].allocator_api.get_alloc_kaddr = cona_get_alloc_kaddr;
	hwmem_mem_types[1].allocator_api.get_alloc_size = cona_get_alloc_size;
	hwmem_mem_types[1].allocator_instance = cona_create("hwmem_cona",
						hwmem_paddr, hwmem_size);
	if (IS_ERR(hwmem_mem_types[1].allocator_instance)) {
		ret = PTR_ERR(hwmem_mem_types[1].allocator_instance);
		goto hwmem_ima_init_failed;
	}

	hwmem_mem_types[2] = hwmem_mem_types[1];
	hwmem_mem_types[2].id = HWMEM_MEM_PROTECTED_SYS;

	if (hwmem_prot_size > 0) {
		hwmem_mem_types[2].allocator_instance = cona_create("hwmem_prot",
							hwmem_prot_paddr, hwmem_prot_size);
		if (IS_ERR(hwmem_mem_types[2].allocator_instance)) {
			ret = PTR_ERR(hwmem_mem_types[2].allocator_instance);
			goto hwmem_ima_init_failed;
		}
	}

	hwmem_mem_types[3] = hwmem_mem_types[1];
	hwmem_mem_types[3].id = HWMEM_MEM_STATIC_SYS;

	if (hwmem_static_size > 0) {
		hwmem_mem_types[3].allocator_instance =
			cona_create("hwmem_static",
			hwmem_static_paddr, hwmem_static_size);

		if (IS_ERR(hwmem_mem_types[3].allocator_instance)) {
			ret = PTR_ERR(hwmem_mem_types[3].allocator_instance);
			goto hwmem_ima_init_failed;
		}
	}

	hwmem_num_mem_types = NUM_MEM_TYPES;

	return 0;

hwmem_ima_init_failed:
	kfree(hwmem_mem_types);

	return ret;
}
arch_initcall_sync(setup_hwmem);

enum hwmem_alloc_flags cachi_get_cache_settings(
			enum hwmem_alloc_flags requested_cache_settings)
{
	static const u32 CACHE_ON_FLAGS_MASK = HWMEM_ALLOC_HINT_CACHED |
		HWMEM_ALLOC_HINT_CACHE_WB | HWMEM_ALLOC_HINT_CACHE_WT |
		HWMEM_ALLOC_HINT_CACHE_NAOW | HWMEM_ALLOC_HINT_CACHE_AOW |
				HWMEM_ALLOC_HINT_INNER_AND_OUTER_CACHE |
					HWMEM_ALLOC_HINT_INNER_CACHE_ONLY;

	enum hwmem_alloc_flags cache_settings;

	if (!(requested_cache_settings & CACHE_ON_FLAGS_MASK) &&
		requested_cache_settings & (HWMEM_ALLOC_HINT_NO_WRITE_COMBINE |
		HWMEM_ALLOC_HINT_UNCACHED | HWMEM_ALLOC_HINT_WRITE_COMBINE))
		/*
		 * We never use uncached as it's extremely slow and there is
		 * no scenario where it would be better than buffered memory.
		 */
		return HWMEM_ALLOC_HINT_WRITE_COMBINE;

	/*
	 * The user has specified cached or nothing at all, both are treated as
	 * cached.
	 */
	cache_settings = (requested_cache_settings &
		 ~(HWMEM_ALLOC_HINT_UNCACHED |
		HWMEM_ALLOC_HINT_NO_WRITE_COMBINE |
		HWMEM_ALLOC_HINT_INNER_CACHE_ONLY |
		HWMEM_ALLOC_HINT_CACHE_NAOW)) |
		HWMEM_ALLOC_HINT_WRITE_COMBINE | HWMEM_ALLOC_HINT_CACHED |
		HWMEM_ALLOC_HINT_CACHE_AOW |
		HWMEM_ALLOC_HINT_INNER_AND_OUTER_CACHE;
	if (!(cache_settings & (HWMEM_ALLOC_HINT_CACHE_WB |
						HWMEM_ALLOC_HINT_CACHE_WT)))
		cache_settings |= HWMEM_ALLOC_HINT_CACHE_WB;
	/*
	 * On ARMv7 "alloc on write" is just a hint so we need to assume the
	 * worst case ie "alloc on write". We would however like to remember
	 * the requested "alloc on write" setting so that we can pass it on to
	 * the hardware, we use the reserved bit in the alloc flags to do that.
	 */
	if (requested_cache_settings & HWMEM_ALLOC_HINT_CACHE_AOW)
		cache_settings |= HWMEM_ALLOC_RESERVED_CHI;
	else
		cache_settings &= ~HWMEM_ALLOC_RESERVED_CHI;

	return cache_settings;
}

void cachi_set_pgprot_cache_options(enum hwmem_alloc_flags cache_settings,
							pgprot_t *pgprot)
{
	if (cache_settings & HWMEM_ALLOC_HINT_CACHED) {
		if (cache_settings & HWMEM_ALLOC_HINT_CACHE_WT)
			*pgprot = __pgprot_modify(*pgprot, L_PTE_MT_MASK,
							L_PTE_MT_WRITETHROUGH);
		else {
			if (cache_settings & HWMEM_ALLOC_RESERVED_CHI)
				*pgprot = __pgprot_modify(*pgprot,
					L_PTE_MT_MASK, L_PTE_MT_WRITEALLOC);
			else
				*pgprot = __pgprot_modify(*pgprot,
					L_PTE_MT_MASK, L_PTE_MT_WRITEBACK);
		}
	} else {
		*pgprot = pgprot_writecombine(*pgprot);
	}
}
