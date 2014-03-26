/* arch/arm/mach-omap2/sec_logger.
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

#ifndef __SEC_LOGGER_H__
#define __SEC_LOGGER_H__

#if defined(CONFIG_SAMSUNG_USE_LOGGER_ADDON)

#if defined(CONFIG_SAMSUNG_PRINT_PLATFORM_LOG)
extern int sec_logger_add_log_ram_console(void *logp, size_t orig);
#else
#define sec_logger_add_log_ram_console(logp, orig)
#endif /* CONFIG_SAMSUNG_PRINT_PLATFORM_LOG */

extern void sec_logger_update_buffer(const char *log_str, int count);

extern void sec_logger_print_buffer(void);

#else /* CONFIG_SAMSUNG_USE_LOGGER_ADDON */

#define sec_logger_add_log_ram_console(logp, orig)
#define sec_logger_update_buffer(log_str, count)
#define sec_logger_print_buffer()

#endif /* CONFIG_SAMSUNG_USE_LOGGER_ADDON */

#endif /* __SEC_LOGGER_H__ */
