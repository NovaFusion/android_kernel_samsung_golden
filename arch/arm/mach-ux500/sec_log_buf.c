/* arch/arm/mach-ux500/sec_logbuf.c
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

#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <mach/hardware.h>

#if defined(CONFIG_ARCH_OMAP)
#include <plat/omap-serial.h>
#endif

#include <mach/sec_common.h>
#include <mach/sec_debug.h>
#include <mach/sec_getlog.h>
#include <mach/sec_log_buf.h>

static unsigned s_log_buf_msk;
static struct sec_log_buf s_log_buf;
static struct device *sec_log_dev;
extern struct class *sec_class;

#define sec_log_buf_get_log_end(_idx)	\
	((char *)(s_log_buf.data + ((_idx) & s_log_buf_msk)))

static void sec_log_buf_write(struct console *console, const char *s,
			      unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		*sec_log_buf_get_log_end(*s_log_buf.count) = *s++;
		(*s_log_buf.count)++;
	}
}

static struct console sec_console = {
	.name = "sec_log_buf",
	.write = sec_log_buf_write,
	.flags = CON_PRINTBUFFER | CON_ENABLED,
	.index = -1,
};

static ssize_t sec_log_buf_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%p(flag) - 0x%08x(count) - 0x%p(data)\n",
		       s_log_buf.flag, *s_log_buf.count, s_log_buf.data);
}

static DEVICE_ATTR(log, S_IRUGO, sec_log_buf_show, NULL);

static unsigned int sec_log_buf_start;
static unsigned int sec_log_buf_size;
static const unsigned int sec_log_buf_flag_size = (4 * 1024);
static const unsigned int sec_log_buf_magic = 0x404C4F47;	/* @LOG */

#ifdef CONFIG_SAMSUNG_USE_LAST_SEC_LOG_BUF
static char *last_log_buf;
static unsigned int last_log_buf_size;

static void __init sec_last_log_buf_reserve(void)
{
	last_log_buf = (char *)alloc_bootmem(s_log_buf_msk + 1);
}

static void __init sec_last_log_buf_setup(void)
{
	unsigned int max_size = s_log_buf_msk + 1;
	unsigned int head;

	if (*s_log_buf.count > max_size) {
		head = *s_log_buf.count & s_log_buf_msk;
		memcpy(last_log_buf,
		       s_log_buf.data + head, max_size - head);
		if (head != 0)
			memcpy(last_log_buf + max_size - head,
			       s_log_buf.data, head);
		last_log_buf_size = max_size;
	} else {
		memcpy(last_log_buf, s_log_buf.data, *s_log_buf.count);
		last_log_buf_size = *s_log_buf.count;
	}
}

static ssize_t sec_last_log_buf_read(struct file *file, char __user *buf,
				     size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= last_log_buf_size || !last_log_buf)
		return 0;

	count = min(len, (size_t) (last_log_buf_size - pos));
	if (copy_to_user(buf, last_log_buf + pos, count))
		return -EFAULT;

	*offset += count;

	return count;
}

static const struct file_operations last_log_buf_fops = {
	.owner		= THIS_MODULE,
	.read		= sec_last_log_buf_read,
};

#if defined(CONFIG_SAMSUNG_REPLACE_LAST_KMSG)
#define LAST_LOG_BUF_NODE	"last_kmsg"
#else
#define LAST_LOG_BUF_NODE	"last_sec_log_buf"
#endif

static int __init sec_last_log_buf_init(void)
{
	struct proc_dir_entry *entry;

	if (!last_log_buf)
		return 0;

	entry = create_proc_entry(LAST_LOG_BUF_NODE,
				  S_IFREG | S_IRUGO, NULL);
	if (!entry) {
		pr_err("(%s): failed to create proc entry\n", __func__);
		return 0;
	}

	entry->proc_fops = &last_log_buf_fops;
	entry->size = last_log_buf_size;

	return 0;
}

late_initcall(sec_last_log_buf_init);

#else /* CONFIG_SAMSUNG_USE_LAST_SEC_LOG_BUF */
#define sec_last_log_buf_reserve()
#define sec_last_log_buf_setup()
#endif /* CONFIG_SAMSUNG_USE_LAST_SEC_LOG_BUF */

static int __init sec_log_buf_setup(char *str)
{
	unsigned long res;

	sec_log_buf_size = memparse(str, &str);

	memset(&s_log_buf, 0x00, sizeof(struct sec_log_buf));

	if (sec_log_buf_size && (*str == '@')) {
		if (kstrtoul(++str, 16, &res))
			goto __err;
		sec_log_buf_start = res;
		/* call reserve_bootmem to prevent the area accessed by
		 * others */
		if (reserve_bootmem
		    (sec_log_buf_start, sec_log_buf_size, BOOTMEM_EXCLUSIVE)) {
			pr_err("(%s): failed to reserve size %d@0x%X\n",
			       __func__, sec_log_buf_size / 1024,
			       sec_log_buf_start);
			goto __err;
		}
	}

	/* call memblock_remove to use ioremap */
	if (memblock_remove(sec_log_buf_start, sec_log_buf_size)) {
		pr_err("(%s): failed to remove size %d@0x%x\n",
		       __func__, sec_log_buf_size / 1024, sec_log_buf_start);
		goto __err;
	}
	s_log_buf_msk = sec_log_buf_size - sec_log_buf_flag_size - 1;

	sec_last_log_buf_reserve();
	return 1;

__err:
	sec_log_buf_start = 0;
	sec_log_buf_size = 0;
	return 0;
}

__setup("sec_log=", sec_log_buf_setup);

static void __init sec_log_buf_create_sysfs(void)
{
	sec_log_dev = device_create(sec_class, NULL, 0, NULL, "sec_log");
	if (IS_ERR(sec_log_dev))
		pr_err("(%s): failed to create device(sec_log)!\n", __func__);

	if (device_create_file(sec_log_dev, &dev_attr_log))
		pr_err("(%s): failed to create device file(log)!\n", __func__);
}

#if defined(CONFIG_ARCH_OMAP)
static bool __init sec_log_buf_disable(void)
{
	char console_opt[32];
	int i;

	/* using ttyOx means, developer is watching kernel logs through out
	 * the uart-serial. at this time, we don't need sec_log_buf to save
	 * serial log and __log_buf is enough to analyze kernel log.
	 */
	for (i = 0; i < OMAP_MAX_HSUART_PORTS; i++) {
		sprintf(console_opt, "console=%s%d", OMAP_SERIAL_NAME, i);
		if (cmdline_find_option(console_opt))
			return true;
	}

	return false;
}
#else
#define sec_log_buf_disable()	false
#endif

int __init sec_log_buf_init(void)
{
	void *start;
	int tmp_console_loglevel = console_loglevel;

	if (unlikely(sec_log_buf_disable()))
		return 0;

	if (unlikely(!sec_log_buf_start || !sec_log_buf_size))
		return 0;

	start = (void *)ioremap(sec_log_buf_start, sec_log_buf_size);

	s_log_buf.flag = (unsigned int *)start;
	s_log_buf.count = (unsigned int *)(start + 4);
	s_log_buf.data = (char *)(start + sec_log_buf_flag_size);

	sec_last_log_buf_setup();

	if (sec_debug_get_level())
		tmp_console_loglevel = 7;	/* KERN_DEBUG */

	if (console_loglevel < tmp_console_loglevel)
		console_loglevel = tmp_console_loglevel;

	register_console(&sec_console);
	sec_log_buf_create_sysfs();

	return 0;
}

late_initcall(sec_log_buf_init);
