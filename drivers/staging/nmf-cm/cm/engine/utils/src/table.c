/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/mem.h>
#include <cm/engine/utils/inc/table.h>

/*
 * Methods
 */
t_cm_error cm_initTable(t_nmf_table* table)
{
	table->idxMax = TABLE_DEF_SIZE / sizeof(table->entries);

	table->entries = OSAL_Alloc_Zero(table->idxMax*sizeof(table->entries));

	if (table->entries == NULL) {
		table->idxMax = 0;
		return CM_NO_MORE_MEMORY;
	}

	return CM_OK;
}

void cm_destroyTable(t_nmf_table* table)
{
	if (table->idxNb) {
		ERROR("Attempt to free non-empty table !!!\n", 0, 0, 0, 0, 0, 0);
		return;
	}
	OSAL_Free(table->entries);
	table->idxMax = 0;
}

static t_cm_error cm_increaseTable(t_nmf_table* table)
{
	t_uint32 new_max;
	void *mem;

	if (table->idxMax == INDEX_MASK) {
		ERROR("CM_NO_MORE_MEMORY: Maximum table entries reached\n", 0, 0, 0, 0, 0, 0);
		return CM_NO_MORE_MEMORY;
	}

	new_max = table->idxMax
		+ TABLE_DEF_SIZE / sizeof(table->entries);

	if (new_max > INDEX_MAX)
		new_max = INDEX_MAX;

	mem = OSAL_Alloc(new_max * sizeof(table->entries));

	if (mem == NULL) {
		ERROR("CM_NO_MORE_MEMORY: Unable to allocate memory for a table\n", 0, 0, 0, 0, 0, 0);
		return CM_NO_MORE_MEMORY;
	}

	cm_MemCopy(mem, table->entries,
		   table->idxMax*sizeof(table->entries));
	cm_MemSet((void *)((t_uint32) mem + table->idxMax*sizeof(*table->entries)), 0,
		  (new_max-table->idxMax) * sizeof(*table->entries));

	OSAL_Free(table->entries);
	table->entries = mem;
	table->idxMax = new_max;

	return CM_OK;
}

/** cm_addEntry - Add an local pointer to an element to the list
 *
 *  1. Increase the size of the list if it's full
 *  2. Search an empty entry
 *  3. Add the element to the list
 *  4. Compute and return the "user handle"
 */
t_uint32 cm_addEntry(t_nmf_table *table, void *entry)
{
	unsigned int i;
	t_uint32 hdl = 0;

	if (table->idxNb == table->idxMax)
		cm_increaseTable(table);

	for (i = table->idxCur;
	     table->entries[i] != 0 && i != (table->idxCur-1);
	     i = (i+1)%table->idxMax);

	if (table->entries[i] == 0) {
		table->entries[i] = entry;
		table->idxCur = (i+1) % table->idxMax;
		table->idxNb++;
		hdl = ENTRY2HANDLE(entry, i);
	} else
		ERROR("No free entry found in table\n", 0, 0, 0, 0, 0, 0);

	return hdl;
}

/** cm_delEntry - remove the given element from the list
 *
 *  1. Check if the handle is valid
 *  2. Search the entry and free it
 */
void cm_delEntry(t_nmf_table *table, t_uint32 idx)
{
	table->entries[idx] = NULL;
	table->idxNb--;
}

/** cm_lookupEntry - search the entry corresponding to
 *                   the user handle.
 *
 * 1. Check if the handle is valid
 * 2. Return a pointer to the element
 */
void *cm_lookupEntry(const t_nmf_table *table, const t_uint32 hdl)
{
	unsigned int idx = hdl & INDEX_MASK;

	if ((idx >= table->idxMax)
	    || (((unsigned int)table->entries[idx] << INDEX_SHIFT) != (hdl & ~INDEX_MASK)))
		return NULL;
	else
		return table->entries[idx];
}

/** cm_lookupHandle - search the handle corresponding
 *                    to the given element
 *
 * 1. Check if the handler is valid or is a special handler
 * 2. Loop in the table to retrieve the entry matching and return its value
 */
t_uint32 cm_lookupHandle(const t_nmf_table *table, const void *entry)
{
	t_uint32 i;

	/* NULL is an invalid value that must be handle separatly
	   as it'll match all used/free entries value */
	if (entry == NULL)
		return 0;

	for (i=0; i < table->idxMax; i++) {
		if (table->entries[i] == entry)
			return ENTRY2HANDLE(table->entries[i], i);
	}

	return 0;
}
