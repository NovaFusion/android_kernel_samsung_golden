/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief String manipulation.
 */
#ifndef H_CM_UTILS_STRING
#define H_CM_UTILS_STRING

#include <cm/engine/memory/inc/memory.h>

#define MAX_INTERNAL_STRING_LENGTH 2048

typedef const char *t_dup_char;

t_dup_char cm_StringGet(const char* str);
t_dup_char cm_StringReference(t_dup_char str);
t_dup_char cm_StringDuplicate(const char* orig);
void cm_StringRelease(t_dup_char orig);

/*
 * Utils libc methods
 */
void        cm_StringCopy(char* dest, const char* src, int count);
int         cm_StringCompare(const char* str1, const char* str2, int count);
int         cm_StringLength(const char * str, int count);
void        cm_StringConcatenate(char* dest, const char* src, int count);
char*       cm_StringSearch(const char* str, int c);

#endif
