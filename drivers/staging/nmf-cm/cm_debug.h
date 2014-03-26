/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef CM_DEBUG_H
#define CM_DEBUG_H

#ifdef CONFIG_DEBUG_FS
#include "cmld.h"

void cm_debug_init(void);
void cm_debug_exit(void);
void cm_debug_proc_init(struct cm_process_priv *entry);
void cm_debug_create_tcm_file(unsigned mpc_index);
void cm_debug_destroy_tcm_file(unsigned mpc_index);

#else

#define cm_debug_init()
#define cm_debug_exit()
#define cm_debug_proc_init(entry)
#define cm_debug_create_tcm_file(mpc_index)
#define cm_debug_destroy_tcm_file(mpc_index)

#endif /* CONFIG_DEBUG_FS */
#endif /* CM_DEBUG_H */
