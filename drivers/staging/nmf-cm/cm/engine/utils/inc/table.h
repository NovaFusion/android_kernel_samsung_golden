/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Dynamic table manipulation.
 */
#ifndef H_CM_UTILS_TABLE
#define H_CM_UTILS_TABLE

#include <cm/inc/cm_type.h>

/*
  This implement a (generic) dynamic table (the size is dynamic)
  to register some pointers of a given kind of elements
  
  It also allows to compute/convert each kernel pointer registered in the
  table to a user handler, that can be checked.

  The "user" handler is composed by the index in this table
  (the low INDEX_SHIFT bits) and the low bits of the "local" pointer
  shifted by INDEX_SHIFT are stored in the high bits:

  handle bits: 31 ................................ 12 11 ...... 0
              | lower bits of of the local pointer   |   index   |

  This allows a straight translation from a user handle to a local pointer
  + a strong check to validate the value of a user handle.
  The reverse translation from pointer to a user handle is
  slower as it requires an explicit search in the list.
 */


/* INDEX_SHIFT determines the index size and thus the max index */
#define INDEX_SHIFT    12
#define INDEX_MAX      (1UL << INDEX_SHIFT)
#define INDEX_MASK     (INDEX_MAX-1)
#define ENTRY2HANDLE(pointer, index) (((unsigned int)pointer << INDEX_SHIFT) | index)
#define TABLE_DEF_SIZE 0x1000

typedef struct {
	t_uint32 idxNb;  /**< number of entries used */
	t_uint32 idxCur; /**< current index: point to next supposed
			    free entry: used to look for the next
			    free entry */
	t_uint32 idxMax; /**< index max currently allowed */
	void **entries;   /**< table itself */
} t_nmf_table;

t_cm_error cm_initTable(t_nmf_table* table);
void cm_destroyTable(t_nmf_table* table);
t_uint32 cm_addEntry(t_nmf_table *table, void *entry);
void cm_delEntry(t_nmf_table *table, t_uint32 idx);
void *cm_lookupEntry(const t_nmf_table *table, const t_uint32 hdl);
t_uint32 cm_lookupHandle(const t_nmf_table *table, const void *entry);

#endif
