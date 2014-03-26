/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/* inc/type.h - Programming Model.
 *
 * Copyright (c) 2006, 2007, 2008 STMicroelectronics.
 *
 * Reproduction and Communication of this document is strictly prohibited
 * unless specifically authorized in writing by STMicroelectronics.
 *
 * Written by NMF team.
 */
#ifndef _NMF_TYPE_H_
#define _NMF_TYPE_H_

#include <inc/typedef.h>

PUBLIC IMPORT_SHARED void NMF_LOG(const char* fmt, ...);
PUBLIC IMPORT_SHARED void NMF_PANIC(const char* fmt, ...);

#define NMF_ASSERT(cond)  do { if(!(cond)) NMF_PANIC("NMF_ASSERT at %s:%d\n", (int)__FILE__, (int)__LINE__); } while(0)

#ifndef EXPORT_NMF_COMPONENT
    #define EXPORT_NMF_COMPONENT    EXPORT_SHARED
#endif

#ifndef IMPORT_NMF_COMPONENT
    #define IMPORT_NMF_COMPONENT    IMPORT_SHARED
#endif

#endif /* _NMF_TYPE_H_ */
