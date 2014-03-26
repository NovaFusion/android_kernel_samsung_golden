/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Added support for overriding memory settings in arch_configuration using
 * mali_mem module parameter.
 *
 * Author: Magnus Wendt <magnus.wendt@stericsson.com> for
 * ST-Ericsson.
 */

/**
 * @file mali_osk_mali.c
 * Implementation of the OS abstraction layer which is specific for the Mali kernel device driver
 */
#include <linux/kernel.h>
#include <asm/uaccess.h>

#include "mali_kernel_common.h" /* MALI_xxx macros */
#include "mali_osk.h"           /* kernel side OS functions */
#include "mali_uk_types.h"
#include "arch/config.h"        /* contains the configuration of the arch we are compiling for */

extern char* mali_mem;
_mali_osk_errcode_t _mali_osk_resources_init( _mali_osk_resource_t **arch_config, u32 *num_resources )
{
    *num_resources = sizeof(arch_configuration) / sizeof(arch_configuration[0]);
    *arch_config = arch_configuration;

	/* override the MEMORY resource if a value has been supplied from the command line */
	if ('\0' != mali_mem[0]) {
		char *p = mali_mem;
		_mali_osk_resource_type_t mem_type = MEMORY;
		unsigned long mem_base = 0;
		unsigned long mem_size = memparse(p, &p);
		if (*p == '@') {
			if ((*(p + 1) == 'O') || (*(p + 1) == 'o')) { /* as in OS. e.g. mali_mem=64M@OS_MEMORY */
				mem_type = OS_MEMORY;
				mem_base = 0;
			} else { /* parse the base address */
				mem_type = MEMORY;
				mem_base = memparse(p + 1, &p);
			}
		}

		/* change the first memory entry in the architecture config. */
		if (0 <= mem_size) {
			int i;
			for (i = 0; i < *num_resources; ++i) {
				if (MEMORY == arch_configuration[i].type) {
					MALI_DEBUG_PRINT( 1, ("Overriding arch resource[%d] :\n",i));
					MALI_DEBUG_PRINT( 1, ("Type: %s, base: %x, size %x\n",
						(OS_MEMORY==mem_type?"OS_MEMORY":"MEMORY"),mem_base,mem_size));
					arch_configuration[i].type = mem_type;
					arch_configuration[i].base = mem_base;
					arch_configuration[i].size = mem_size;
					break;
				}
			}
		}
	}

    return _MALI_OSK_ERR_OK;
}

void _mali_osk_resources_term( _mali_osk_resource_t **arch_config, u32 num_resources )
{
    /* Nothing to do */
}
