/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 * Shared string manipulation.
 * TODO This is a list today, must be a hash later !!!!!
 */
#include <cm/engine/utils/inc/string.h>
#include <cm/engine/trace/inc/trace.h>

#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

#undef NHASH
#define NHASH 257       //Use a prime number!
#define MULT 17

/*
 * Data
 */
struct t_linkedstring
{
    struct t_linkedstring	*next;
    int						referencer;
    char					string[1];
};

static struct t_linkedstring *list[NHASH];

#undef myoffsetof
#define myoffsetof(st, m) \
     ((int) ( (char *)&((st *)(0))->m - (char *)0 ))

unsigned int hash(const char *str)
{
    unsigned int h = 0;
    for(; *str; str++)
        h = MULT * h + *str;
    return h % NHASH;
}
/*
 * Methods
 */
PRIVATE struct t_linkedstring *lookupString(
        const char* str,
        struct t_linkedstring   *first)
{
    while(first != 0)
    {
        if(cm_StringCompare(str, first->string, MAX_INTERNAL_STRING_LENGTH) == 0)
            break;
        first = first->next;
    }

    return first;
}

t_dup_char cm_StringGet(const char* str)
{
    struct t_linkedstring *entry;

    entry = lookupString(str, list[hash(str)]);
    CM_ASSERT(entry != 0);

    return (t_dup_char)entry->string;
}

t_dup_char cm_StringReference(t_dup_char str)
{
    struct t_linkedstring* entry = (struct t_linkedstring*)((t_uint32)str - myoffsetof(struct t_linkedstring, string));

    // One more referencer
    entry->referencer++;

    return (t_dup_char)entry->string;
}

t_dup_char cm_StringDuplicate(const char* str)
{
    struct t_linkedstring *entry;
    unsigned int h;

    h = hash(str);
    entry = lookupString(str, list[h]);
    if(entry != 0)
    {
        // One more referencer
        entry->referencer++;
    }
    else
    {
        // Allocate new entry
        entry = (struct t_linkedstring *)OSAL_Alloc(sizeof(struct t_linkedstring)-1 + cm_StringLength(str, MAX_INTERNAL_STRING_LENGTH)+1);
        if(entry == NULL)
            return NULL;

        entry->referencer = 1;
        cm_StringCopy(entry->string, str, MAX_INTERNAL_STRING_LENGTH);

        // Link it in list
        entry->next = list[h];
        list[h] = entry;
    }

    return (t_dup_char)entry->string;
}

void cm_StringRelease(t_dup_char str)
{
    if(str != NULL)
    {
        struct t_linkedstring* entry = (struct t_linkedstring*)((t_uint32)str - myoffsetof(struct t_linkedstring, string));

        // One less referencer
        entry->referencer--;

        if(entry->referencer == 0)
        {
            int h = hash(entry->string);

            if(list[h] == entry) // This first first one
            {
                list[h] = entry->next;
            }
            else
            {
                struct t_linkedstring *tmp = list[h];

                // Here we assume that entry is in the list
                while(/*tmp != NULL && */tmp->next != entry)
                    tmp = tmp->next;

                tmp->next = entry->next;
            }
            OSAL_Free(entry);
        }
    }
}

#if 0
void checkString()
{
    struct t_linkedstring *tmp = list;

    while(tmp != 0)
    {
        printf("  stay %s %d\n", tmp->string, tmp->referencer);
        tmp = tmp->next;
    }
}
#endif

/*
 * LibC method
 */
void cm_StringCopy(char* dest, const char *src, int count)
{
    while (count-- && (*dest++ = *src++) != '\0')
        /* nothing */
        ;
}
#define DETECTNULL(X) (((X) - 0x01010101) & ~(X) & 0x80808080)

int cm_StringCompare(const char* str1, const char* str2, int count)
{
    /* If s1 and s2 are word-aligned, compare them a word at a time. */
    if ((((int)str1 & 3) | ((int)str2 & 3)) == 0)
    {
        unsigned int *a1 = (unsigned int*)str1;
        unsigned int *a2 = (unsigned int*)str2;

        while (count >= sizeof (unsigned int) && *a1 == *a2)
        {
            count -= sizeof (unsigned int);

            /* If we've run out of bytes or hit a null, return zero since we already know *a1 == *a2.  */
            if (count == 0 || DETECTNULL (*a1))
                return 0;

            a1++;
            a2++;
        }

        /* A difference was detected in last few bytes of s1, so search bytewise */
        str1 = (char*)a1;
        str2 = (char*)a2;
    }

    while (count-- > 0 && *str1 == *str2)
    {
        /* If we've run out of bytes or hit a null, return zero
       since we already know *s1 == *s2.  */
        if (count == 0 || *str1 == '\0')
            return 0;
        str1++;
        str2++;
    }

    return (*(unsigned char *) str1) - (*(unsigned char *) str2);
}

int cm_StringLength(const char * str, int count)
{
    const char *sc;

    for (sc = str; count-- && *sc != '\0'; ++sc)
        /* nothing */
        ;
    return sc - str;
}

void cm_StringConcatenate(char* dest, const char* src, int count)
{
    while ((*dest) != '\0')
    {
        dest++;
        count--;
    }
    cm_StringCopy(dest, src, count);
}

char* cm_StringSearch(const char* str, int c)
{
    for(; *str != (char) c; ++str)
        if (*str == '\0')
            return 0;
    return (char *) str;
}
