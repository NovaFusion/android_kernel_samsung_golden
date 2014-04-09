/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson DSI link device driver
 *
 * Author: Joseph V P <joseph.vp@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/device.h>

#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include "dsilink_debugfs.h"

#define DEBUGFS_LOG_BUF_SZ 4096

static struct dsi_info {
	struct dentry *debugfs_dsi_root_dir;
	char log_buf[DEBUGFS_LOG_BUF_SZ];
	int log_idx;
	int line_count;
	int log_end_idx;
} dsi;

struct dcs_cmd_string_map {
	const char *str;
	u8 cmd;
};

static const struct dcs_cmd_string_map dsi_cmds[] = {
	{"enter_idle_mode",			0x39},
	{"enter_invert_mode",		0x21},
	{"enter_normal_mode",		0x13},
	{"enter_partial_mode",		0x12},
	{"enter_sleep_mode",		0x10},
	{"exit_idle_mode",			0x38},
	{"exit_invert_mode",		0x20},
	{"exit_sleep_mode",			0x11},
	{"get_address_mode",		0x0B},
	{"get_blue_channel",		0x08},
	{"get_diagnostic_result",	0x0F},
	{"get_display_mode",		0x0D},
	{"get_green_channel",		0x07},
	{"get_pixel_format",		0x0C},
	{"get_power_mode",			0x0A},
	{"get_red_channel",			0x06},
	{"get_scanline",			0x45},
	{"get_signal_mode",			0x0E},
	{"nop",						0x00},
	{"read_DDB_continue",		0xA8},
	{"read_DDB_start",			0xA1},
	{"read_memory_continue",	0x3E},
	{"read_memory_start",		0x2E},
	{"set_address_mode",		0x36},
	{"set_column_address",		0x2A},
	{"set_display_off",			0x28},
	{"set_display_on",			0x29},
	{"set_gamma_curve",			0x26},
	{"set_page_address",		0x2B},
	{"set_partial_columns",		0x31},
	{"set_partial_rows",		0x30},
	{"set_pixel_format",		0x3A},
	{"set_scroll_area",			0x33},
	{"set_scroll_start",		0x37},
	{"set_tear_off",			0x34},
	{"set_tear_on",				0x35},
	{"set_tear_scanline",		0x44},
	{"soft_reset",				0x01},
	{"write_LUT",				0x2D},
	{"write_memory_continue",	0x3C},
	{"write_memory_start",		0x2C}
};

#define DCS_CMDS_SZ (sizeof(dsi_cmds) / sizeof(struct dcs_cmd_string_map))

/**
 * debugfs_dsi_cmds_read() - Implements debugfs for logging DSI commands
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_dsi_cmds_read(struct file *filp, char __user *buf,
			size_t count, loff_t *f_pos)
{
	int ret = 0;

	struct dsi_info *info = filp->f_dentry->d_inode->i_private;

	/* log_end_idx is zero till log buffer wrap around */
	if (info->log_end_idx != 0) {
		if (*f_pos > info->log_end_idx)
			return -EINVAL;

		if ((*f_pos + count) > info->log_end_idx)
			count = info->log_end_idx - *f_pos;

		if (count > info->log_idx) {
			/* Copy log prints in buffer, after current index */
			if (copy_to_user(buf,
				&info->log_buf[*f_pos + info->log_idx],
				count - info->log_idx))
				return -EINVAL;

			/* Copy log prints in buffer, till current index */
			if (copy_to_user(&buf[count - info->log_idx],
				&info->log_buf[*f_pos], info->log_idx))
				return -EINVAL;
		} else {
			/* Requested count is less than current index */
			if (copy_to_user(buf, &info->log_buf[*f_pos], count))
				return -EINVAL;
		}
	} else {
		/* Log print not reached end of buffer */
		if (*f_pos > info->log_idx)
			return -EINVAL;

		if (*f_pos + count > info->log_idx)
			count = info->log_idx - *f_pos;

		if (copy_to_user(buf, &info->log_buf[*f_pos], count))
			return -EINVAL;

	}
	*f_pos += count;
	ret = count;

	return ret;
}

/**
 * debugfs_dsi_cmds_fops - File operations for dsi commands log debugfs
 */
static const struct file_operations debugfs_dsi_cmds_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_dsi_cmds_read,
};

static const char *dsi_get_cmd_name(u8 cmd)
{
	int index;

	for (index = 0; index < DCS_CMDS_SZ; index++) {
		if (cmd == dsi_cmds[index].cmd)
			return dsi_cmds[index].str;
	}
	return "unknown_cmd";
}

int dsilink_debugfs_print_cmd(u8 cmd, u8 *data, int len, u8 *rd_wr_str)
{
	int i = 0;
	size_t size = 0;
	size_t max_size = DEBUGFS_LOG_BUF_SZ - dsi.log_idx;
	char *buf = dsi.log_buf + dsi.log_idx;
	char const *str = dsi_get_cmd_name(cmd);

	dsi.line_count++;
	size = snprintf(buf, max_size,
		"%08d: %s cmd=0x%02X (%s) len=%d data=",
		dsi.line_count, rd_wr_str, cmd, str, len);
	if (size > max_size)
		goto wrap_around;

	if (data) {
		for (i = 0; i < len; i++) {
			size += snprintf(buf + size, max_size - size,
				"0x%02X ", data[i]);

			if (size > max_size)
				goto wrap_around;
		}
	}
	size += snprintf(buf + size, max_size - size, "\n");
	if (size > max_size)
		goto wrap_around;

	dsi.log_idx += size;
	return 0;

wrap_around:
	/*
	 * Current command print needs more space than size remaining.
	 * Reset and start from the begining of the buffer and
	 * discard last command print.
	 * Return negative error code if buffer size not enough
	 * for one command log.
	 */

	dsi.log_end_idx = dsi.log_idx;
	dsi.log_idx = 0;
	buf = dsi.log_buf;
	max_size = DEBUGFS_LOG_BUF_SZ;

	size = snprintf(buf, max_size,
		"%08d: %s cmd=0x%02X (%s) len=%d data=",
		dsi.line_count, rd_wr_str, cmd, str, len);
	if (size > max_size)
		return -EINVAL;

	if (data) {
		for (i = 0; i < len; i++) {
			size += snprintf(buf + size, max_size - size,
				"0x%02X ", data[i]);
			if (size > max_size)
				return -EINVAL;
		}
	}
	size += snprintf(buf + size, max_size - size, "\n");
	if (size > max_size)
		return -EINVAL;

	dsi.log_idx += size;
	return 0;
}

int dsilink_debugfs_create()
{
	if (dsi.debugfs_dsi_root_dir == NULL) {
		dsi.debugfs_dsi_root_dir = debugfs_create_dir("dsi", NULL);

		if (!IS_ERR_OR_NULL(dsi.debugfs_dsi_root_dir))
			debugfs_create_file("cmds", 0664,
					dsi.debugfs_dsi_root_dir,
					&dsi, &debugfs_dsi_cmds_fops);
	}

	return 0;
}
