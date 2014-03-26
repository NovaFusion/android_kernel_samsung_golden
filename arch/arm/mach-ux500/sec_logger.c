/* arch/arm/mach-omap2/sec_logger.c
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

#include <linux/console.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/rtc.h>

#include "../../../drivers/staging/android/logger.h"

#include <mach/sec_common.h>
#include <mach/sec_debug.h>

#if defined(CONFIG_SAMSUNG_PRINT_PLATFORM_LOG)

enum {
	SEC_LOGGER_LEVEL_UNKNOWN = 0,
	SEC_LOGGER_LEVEL_VERBOSE = 2,
	SEC_LOGGER_LEVEL_DEBUG,
	SEC_LOGGER_LEVEL_INFO,
	SEC_LOGGER_LEVEL_WARN,
	SEC_LOGGER_LEVEL_ERROR,
	SEC_LOGGER_LEVEL_FATAL,
	SEC_LOGGER_LEVEL_SILENT,
};

static void (*sec_ram_console_write_ext)(struct console *console,
					 const char *s, unsigned int count);

static bool sec_platform_log_en;

static struct device *sec_logger_dev;
static unsigned int sec_logger_level = SEC_LOGGER_LEVEL_WARN;

static ssize_t sec_logger_level_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", sec_logger_level);
}

static ssize_t sec_logger_level_store(struct debug *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned int level;

	if (!kstrtouint(buf, 0, &level))
		sec_logger_level = level;

	return count;
}

static DEVICE_ATTR(logger, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		   sec_logger_level_show, sec_logger_level_store);

static char pri_to_char(unsigned int pri)
{
	char p2c_tbl[] = {
		'?',		/* UNKNOWN */
		'?',		/* UNKNOWN */
		'V',		/* VERBOSE */
		'D',		/* DEBUG */
		'I',		/* INFO */
		'W',		/* WARN */
		'E',		/* ERROR */
		'F',		/* FATAL */
		'S',		/* SILENT */
	};

	if (unlikely(pri > ARRAY_SIZE(p2c_tbl) - 1))
		return '?';

	return p2c_tbl[pri];
}

static int filter_log(const char *log_name, char pri, int min_pri)
{
	if (strcmp(log_name, LOGGER_LOG_SYSTEM) &&
	    strcmp(log_name, LOGGER_LOG_MAIN))
		return -EPERM;

	if (pri_to_char(pri) == '?' || pri < min_pri)
		return -EPERM;

	return 0;
}

/* simplified(?) version of logger_log */
struct sec_logger_log {
	unsigned char *buffer;
	struct miscdevice misc;
};

static char logger_buf[1024];

int sec_logger_add_log_ram_console(void *logp, size_t orig)
{
	struct logger_entry *entry;
	struct rtc_time tm;
	char time[32];
	char pri, *tag, *message;
	struct sec_logger_log *log = (struct sec_logger_log *)logp;

	if (likely(!sec_platform_log_en))
		return 0;

	entry = (struct logger_entry *)(log->buffer + orig);
	pri = entry->msg[0];
	tag = entry->msg + 1;
	message = tag + strlen(tag) + 1;

	if (filter_log(log->misc.name, pri, sec_logger_level) < 0)
		/* printable minimum level */
		return -EPERM;

	rtc_time_to_tm(entry->sec, &tm);
	snprintf(time, sizeof(time), "%02d-%02d %02d:%02d:%02d.%03u",
		 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		 tm.tm_min, tm.tm_sec, entry->nsec / 1000000);

	snprintf(logger_buf, sizeof(logger_buf) - 1, "%s %5d %5d %c %-8s: %s",
		 time, entry->pid, entry->tid, pri_to_char(pri), tag, message);
	if (logger_buf[strlen(logger_buf) - 1] != '\n')
		/* add a line-feed if needed */
		strcat(logger_buf, "\n");

	if (likely(sec_ram_console_write_ext))
		sec_ram_console_write_ext(NULL, logger_buf,
					  strlen(logger_buf));
	else
		pr_info("%s", logger_buf);

	return 0;
}

static void __init sec_logger_create_sysfs(void)
{
	sec_logger_dev = device_create(sec_class, NULL, 0, NULL, "sec_logger");
	if (IS_ERR(sec_logger_dev))
		pr_err("(%s): failed to create device(sec_logger)!\n",
		       __func__);

	if (device_create_file(sec_logger_dev, &dev_attr_logger))
		pr_err("(%s): failed to create device file(logger)!\n",
		       __func__);
}

static void __init sec_logger_ram_console_init(void)
{
	const char *console[] = {
		"ram_console_write",
		"sec_log_buf_write",
	};
	int i;

	sec_platform_log_en = true;

	sec_logger_create_sysfs();

	for (i = 0; i < ARRAY_SIZE(console); i++) {
		sec_ram_console_write_ext =
			(void *)kallsyms_lookup_name(console[i]);

		if (sec_ram_console_write_ext) {
			pr_debug("(%s): init success - %s (0x%p)\n",
			 __func__, console[i], sec_ram_console_write_ext);
			return;
		}
	}

	pr_debug("(%s): init fail - printk\n", __func__);
}

#else

#define sec_logger_ram_console_init()

#endif /* CONFIG_SAMSUNG_PRINT_PLATFORM_LOG */

static char sec_klog_buf[256];

void sec_logger_update_buffer(const char *log_str, int count)
{
	const int maxlen = ARRAY_SIZE(sec_klog_buf) - 1;
	int len;

	sec_klog_buf[0] = '\0';

	if (unlikely(*(u16 *)"!@" == *(u16 *)log_str)) {
		len = count < maxlen ? count : maxlen;
		memcpy(sec_klog_buf, log_str, len);
		sec_klog_buf[len] = '\0';
	}
}

void sec_logger_print_buffer(void)
{
	if (sec_klog_buf[0])
		pr_info("%s\n", sec_klog_buf);
}

static void __init sec_logger_message_init(void)
{
	sec_klog_buf[0] = '\0';
}

static int __init sec_logger_init(void)
{
	sec_logger_message_init();

	if (sec_debug_get_level())
		sec_logger_ram_console_init();

	return 0;
}

late_initcall(sec_logger_init);
