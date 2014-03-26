/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/utils/inc/mem.h>


/*
 * Methods
 */
void cm_MemCopy(void* dest, const void *src, int count) {
    char *tmp = (char *) dest, *s = (char *) src;

    while (count--)
        *tmp++ = *s++;
}

void cm_MemSet(void *str, int c, int count) {
  char *tmp = (char *)str;

  while (count--)
    *tmp++ = c;
}
