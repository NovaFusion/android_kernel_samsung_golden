/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Memory manipulation.
 */
#ifndef H_CM_UTILS_MEM
#define H_CM_UTILS_MEM

/*
 * Utils libc methods
 */
void cm_MemCopy(void* dest, const void *src, int count);
void cm_MemSet(void *str, int c, int count);

#endif
