/* arch/arm/mach-omap2/sec_getlog.h
 *
 * Copyright (C) 2010-2011 Samsung Electronics Co, Ltd.
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

#ifndef __SEC_GETLOG_H__
#define __SEC_GETLOG_H__

#if defined(CONFIG_SAMSUNG_USE_GETLOG)

#include <linux/fb.h>

/* TODO: currently there is no other way than injecting this function. */
extern void sec_getlog_supply_fbinfo(struct fb_info *fb);

/* TODO: call this function in 'map_io' call-back */
extern void sec_getlog_supply_meminfo(u32 size0, u32 addr0,
				      u32 size1, u32 addr1);

#else

#define sec_getlog_supply_fbinfo(fb)

#define sec_getlog_supply_meminfo(size0, addr0, size1, addr1)

#endif /* CONFIG_SAMSUNG_USE_GETLOG */

#endif /* __SEC_GETLOG_H__ */
