/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com> for ST-Ericsson
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <mach/hardware.h>

#include "clock.h"

struct clk_debug_info {
	struct clk *clk;
	struct dentry *dir;
	struct dentry *enable;
	struct dentry *requests;
	int enabled;
};

#ifdef CONFIG_DEBUG_FS

static struct dentry *clk_dir;
static struct dentry *clk_show;
static struct dentry *clk_show_enabled_only;

static struct clk_debug_info *cdi;
static int num_clks;

static int clk_show_print(struct seq_file *s, void *p)
{
	int i;
	int enabled_only = (int)s->private;

	seq_printf(s, "\n%-20s %10s %s\n", "name", "rate",
		"enabled (kernel + debug)");
	for (i = 0; i < num_clks; i++) {
		if (enabled_only && !cdi[i].clk->enabled)
			continue;
		seq_printf(s,
			"%-20s %10lu %5d + %d\n",
			cdi[i].clk->name,
			clk_get_rate(cdi[i].clk),
			cdi[i].clk->enabled - cdi[i].enabled,
			cdi[i].enabled);
	}

	return 0;
}

static int clk_show_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_show_print, inode->i_private);
}

static int clk_enable_print(struct seq_file *s, void *p)
{
	struct clk_debug_info *cdi = s->private;

	return seq_printf(s, "%d\n", cdi->enabled);
}

static int clk_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_enable_print, inode->i_private);
}

static ssize_t clk_enable_write(struct file *file, const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct clk_debug_info *cdi;
	long user_val;
	int err;

	cdi = ((struct seq_file *)(file->private_data))->private;

	err = kstrtol_from_user(user_buf, count, 0, &user_val);

	if (err)
		return err;

	if ((user_val > 0) && (!cdi->enabled)) {
		err = clk_enable(cdi->clk);
		if (err) {
			pr_err("clock: clk_enable(%s) failed.\n",
				cdi->clk->name);
			return -EFAULT;
		}
		cdi->enabled = 1;
	} else if ((user_val <= 0) && (cdi->enabled)) {
		clk_disable(cdi->clk);
		cdi->enabled = 0;
	}
	return count;
}

static int clk_requests_print(struct seq_file *s, void *p)
{
	struct clk_debug_info *cdi = s->private;

	return seq_printf(s, "%d\n", cdi->clk->enabled);
}

static int clk_requests_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_requests_print, inode->i_private);
}

static const struct file_operations clk_enable_fops = {
	.open = clk_enable_open,
	.write = clk_enable_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations clk_requests_fops = {
	.open = clk_requests_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations clk_show_fops = {
	.open = clk_show_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int create_clk_dirs(struct clk_debug_info *cdi, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		cdi[i].dir = debugfs_create_dir(cdi[i].clk->name, clk_dir);
		if (!cdi[i].dir)
			goto no_dir;
	}

	for (i = 0; i < size; i++) {
		cdi[i].enable = debugfs_create_file("enable",
						    (S_IRUGO | S_IWUSR | S_IWGRP),
						    cdi[i].dir, &cdi[i],
						    &clk_enable_fops);
		if (!cdi[i].enable)
			goto no_enable;
	}
	for (i = 0; i < size; i++) {
		cdi[i].requests = debugfs_create_file("requests", S_IRUGO,
						       cdi[i].dir, &cdi[i],
						       &clk_requests_fops);
		if (!cdi[i].requests)
			goto no_requests;
	}
	return 0;

no_requests:
	while (i--)
		debugfs_remove(cdi[i].requests);
	i = size;
no_enable:
	while (i--)
		debugfs_remove(cdi[i].enable);
	i = size;
no_dir:
	while (i--)
		debugfs_remove(cdi[i].dir);

	return -ENOMEM;
}

int __init dbx500_clk_debug_init(struct clk **clks, int num)
{
	int i;

	cdi = kcalloc(sizeof(struct clk_debug_info), num, GFP_KERNEL);
	if (!cdi)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		cdi[i].clk = clks[i];

	num_clks = num;

	clk_dir = debugfs_create_dir("clk", NULL);
	if (!clk_dir)
		goto no_dir;

	clk_show = debugfs_create_file("show", S_IRUGO, clk_dir, (void *)0,
				       &clk_show_fops);
	if (!clk_show)
		goto no_show;

	clk_show_enabled_only = debugfs_create_file("show-enabled-only",
					       S_IRUGO, clk_dir, (void *)1,
					       &clk_show_fops);
	if (!clk_show_enabled_only)
		goto no_enabled_only;

	if (create_clk_dirs(cdi, num))
		goto no_clks;

	return 0;

no_clks:
	debugfs_remove(clk_show_enabled_only);
no_enabled_only:
	debugfs_remove(clk_show);
no_show:
	debugfs_remove(clk_dir);
no_dir:
	kfree(cdi);
	return -ENOMEM;
}

static int __init clk_debug_init(void)
{
	if (cpu_is_u8500() || cpu_is_u9540())
		db8500_clk_debug_init();
	else if (cpu_is_u5500())
		db5500_clk_debug_init();

	return 0;
}
module_init(clk_debug_init);

#endif /* CONFIG_DEBUG_FS */
