/* arch/arm/mach-omap2/sec_gaf.h
 *
 * Copyright (C) 2011 Samsung Electronics Co, Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __SEC_GAF_H__
#define __SEC_GAF_H__

#if defined(CONFIG_SAMSUNG_ADD_GAFORENSICINFO)

extern void sec_gaf_supply_rqinfo(unsigned short curr_offset,
				  unsigned short rq_offset);

extern void sec_gaf_dump_all_task_info(void);

extern void sec_gaf_dump_cpu_stat(void);

#else /* CONFIG_SAMSUNG_ADD_GAFORENSICINFO */

#define sec_gaf_supply_rqinfo(curr_offset, rq_offset)
#define sec_gaf_dump_all_task_info()
#define sec_gaf_dump_cpu_stat()

#endif /* CONFIG_SAMSUNG_ADD_GAFORENSICINFO */

#endif /* __SEC_GAF_H__ */
