/* arch/arm/mach-omap2/sec_getlog.c
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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>

#include <mach/sec_getlog.h>
#include <mach/sec_log_buf.h>

static struct {
	u32 special_mark_1;
	u32 special_mark_2;
	u32 special_mark_3;
	u32 special_mark_4;
	void *p_fb;		/* it must be physical address */
	u32 xres;
	u32 yres;
	u32 bpp;		/* color depth : 16 or 24 */
	u32 frames;		/* frame buffer count : 2 */
} frame_buf_mark = {
	.special_mark_1 = (('*' << 24) | ('^' << 16) | ('^' << 8) | ('*' << 0)),
	.special_mark_2 = (('I' << 24) | ('n' << 16) | ('f' << 8) | ('o' << 0)),
	.special_mark_3 = (('H' << 24) | ('e' << 16) | ('r' << 8) | ('e' << 0)),
	.special_mark_4 = (('f' << 24) | ('b' << 16) | ('u' << 8) | ('f' << 0)),
};

static void __sec_getlog_supply_fbinfo(void *p_fb, u32 xres, u32 yres,
				       u32 bpp, u32 frames)
{
	if (p_fb) {
		pr_debug("%s: 0x%p %d %d %d %d\n", __func__, p_fb, xres, yres,
			bpp, frames);
		frame_buf_mark.p_fb = p_fb;
		frame_buf_mark.xres = xres;
		frame_buf_mark.yres = yres;
		frame_buf_mark.bpp = bpp;
		frame_buf_mark.frames = frames;
	}
}

/* TODO: currently there is no other way than injecting this function .*/
void sec_getlog_supply_fbinfo(struct fb_info *fb)
{
	__sec_getlog_supply_fbinfo((void *)fb->fix.smem_start,
				   fb->var.xres,
				   fb->var.yres,
				   fb->var.bits_per_pixel, 2);
}

static struct {
	u32 special_mark_1;
	u32 special_mark_2;
	u32 special_mark_3;
	u32 special_mark_4;
	u32 log_mark_version;
	u32 framebuffer_mark_version;
	void *this;		/* this is used for addressing
				   log buffer in 2 dump files */
	struct {
		u32 size;	/* memory block's size */
		u32 addr;	/* memory block'sPhysical address */
	} mem[2];
} marks_ver_mark = {
	.special_mark_1 = (('*' << 24) | ('^' << 16) | ('^' << 8) | ('*' << 0)),
	.special_mark_2 = (('I' << 24) | ('n' << 16) | ('f' << 8) | ('o' << 0)),
	.special_mark_3 = (('H' << 24) | ('e' << 16) | ('r' << 8) | ('e' << 0)),
	.special_mark_4 = (('v' << 24) | ('e' << 16) | ('r' << 8) | ('s' << 0)),
	.log_mark_version = 1,
	.framebuffer_mark_version = 1,
	.this = &marks_ver_mark,
};

void __init sec_getlog_supply_meminfo(u32 size0, u32 addr0,
					     u32 size1, u32 addr1)
{
	pr_debug("%s: %x %x %x %x\n", __func__, size0, addr0, size1, addr1);
	marks_ver_mark.mem[0].size = size0;
	marks_ver_mark.mem[0].addr = addr0;
	marks_ver_mark.mem[1].size = size1;
	marks_ver_mark.mem[1].addr = addr1;
}

/* mark for GetLog extraction */
static struct {
	u32 special_mark_1;
	u32 special_mark_2;
	u32 special_mark_3;
	u32 special_mark_4;
	void *p_main;
	void *p_radio;
	void *p_events;
	void *p_system;
} plat_log_mark = {
	.special_mark_1 = (('*' << 24) | ('^' << 16) | ('^' << 8) | ('*' << 0)),
	.special_mark_2 = (('I' << 24) | ('n' << 16) | ('f' << 8) | ('o' << 0)),
	.special_mark_3 = (('H' << 24) | ('e' << 16) | ('r' << 8) | ('e' << 0)),
	.special_mark_4 = (('p' << 24) | ('l' << 16) | ('o' << 8) | ('g' << 0)),
};

void sec_getlog_supply_loggerinfo(void *p_main,
				  void *p_radio, void *p_events, void *p_system)
{
	/* FIXME: bug of getLog tool.
	 * 'address_mask MUST NOT be used after fixed */
	const unsigned int address_mask = 0x0fffffff;

	pr_debug("%s: 0x%p 0x%p 0x%p 0x%p\n", __func__, p_main, p_radio,
		p_events, p_system);
	plat_log_mark.p_main = (void *)
		((unsigned int)p_main & address_mask);
	plat_log_mark.p_radio = (void *)
		((unsigned int)p_radio & address_mask);
	plat_log_mark.p_events = (void *)
		((unsigned int)p_events & address_mask);
	plat_log_mark.p_system = (void *)
		((unsigned int)p_system & address_mask);
}
EXPORT_SYMBOL(sec_getlog_supply_loggerinfo);

static void __init sec_getlog_init_loggerinfo(void)
{
	void *p_main = (void *)kallsyms_lookup_name("_buf_log_main");
	void *p_radio = (void *)kallsyms_lookup_name("_buf_log_radio");
	void *p_events = (void *)kallsyms_lookup_name("_buf_log_events");
	void *p_system = (void *)kallsyms_lookup_name("_buf_log_system");

	sec_getlog_supply_loggerinfo(p_main, p_radio, p_events, p_system);
}

static struct {
	u32 special_mark_1;
	u32 special_mark_2;
	u32 special_mark_3;
	u32 special_mark_4;
	void *klog_buf;
} kernel_log_mark = {
	.special_mark_1 = (('*' << 24) | ('^' << 16) | ('^' << 8) | ('*' << 0)),
	.special_mark_2 = (('I' << 24) | ('n' << 16) | ('f' << 8) | ('o' << 0)),
	.special_mark_3 = (('H' << 24) | ('e' << 16) | ('r' << 8) | ('e' << 0)),
	.special_mark_4 = (('k' << 24) | ('l' << 16) | ('o' << 8) | ('g' << 0)),
};

static void __init sec_getlog_supply_kloginfo(void *klog_buf)
{
	pr_debug("%s: 0x%p\n", __func__, klog_buf);
	kernel_log_mark.klog_buf = klog_buf;
}

static void __init sec_getlog_init_kloginfo(void)
{
	void *klog_buf = (void *)*(char **)kallsyms_lookup_name("log_buf");

	sec_getlog_supply_kloginfo((void *)virt_to_phys(klog_buf));
}

static int __init sec_getlog_info_init(void)
{
	sec_getlog_init_loggerinfo();
	sec_getlog_init_kloginfo();

	return 0;
}

late_initcall(sec_getlog_info_init);
