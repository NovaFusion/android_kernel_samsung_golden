/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Scattered memory allocator
 *
 * Author: Andreas Karlsson <andreas.z.karlsson@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/err.h>
#include <linux/slab.h>

#define MAX_INSTANCE_NAME_LENGTH 31

struct alloc {
	size_t size;
	struct page **pages;
	size_t nr_of_pages;
};

struct instance {
	char name[MAX_INSTANCE_NAME_LENGTH + 1];
};

static DEFINE_MUTEX(lock);

void *scatt_create(const char *name)
{
	struct instance *instance;

	instance = kmalloc(sizeof(*instance), GFP_KERNEL);
	if (instance == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(instance->name, name, MAX_INSTANCE_NAME_LENGTH + 1);
	/* Truncate name if necessary */
	instance->name[MAX_INSTANCE_NAME_LENGTH] = '\0';

	return instance;
}

void *scatt_alloc(void *instance, size_t size)
{
	struct alloc *new_alloc;
	unsigned int array_size, i;

	new_alloc = kmalloc(sizeof(struct alloc), GFP_KERNEL);
	if (new_alloc == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&lock);

	new_alloc->size = size;
	new_alloc->nr_of_pages = new_alloc->size >> PAGE_SHIFT;

	array_size = (new_alloc->nr_of_pages * sizeof(struct page *));
	new_alloc->pages = kmalloc(array_size, GFP_KERNEL);

	if (!new_alloc->pages) {
		printk(KERN_ERR "HWMEM: Scattered alloc pages array failed\n");
		goto alloc_failed;
	}

	/* Alloc page and place in the sglist */
	for (i = 0; i < new_alloc->nr_of_pages; i++) {
		new_alloc->pages[i] = alloc_page(GFP_KERNEL);
		if (!new_alloc->pages[i]) {
			printk(KERN_ERR "HWMEM: Scattered alloc page failed\n");
			goto alloc_failed;
		}
	}

	mutex_unlock(&lock);

	return new_alloc;

alloc_failed:
	kfree(new_alloc);
	mutex_unlock(&lock);
	return ERR_PTR(-ENOMEM);
}

void scatt_free(void *instance, void *alloc)
{
	int i;

	struct alloc *alloc_l = (struct alloc *)alloc;

	mutex_lock(&lock);

	/* Free all pages in the sglist */
	for (i = 0; i < alloc_l->nr_of_pages; i++)
		__free_page(alloc_l->pages[i]);

	kfree(alloc_l->pages);
	kfree(alloc_l);

	mutex_unlock(&lock);
}

size_t scatt_get_alloc_size(void *alloc)
{
	if (alloc == NULL)
		return -EINVAL;

	return ((struct alloc *)alloc)->size;
}

struct page **scatt_get_alloc_sglist(void *alloc)
{
	if (alloc == NULL)
		return ERR_PTR(-EINVAL);

	return ((struct alloc *)alloc)->pages;
}

