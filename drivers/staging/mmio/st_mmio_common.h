/*
 * st_mmio_common.h
 *
 * Copyright (C) ST-Ericsson - Le Mans SA 2011
 * Author: Vincent Abriou <vincent.abriou@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _MMIO_COMMON_H_
#define _MMIO_COMMON_H_

#include <linux/delay.h>
#include <linux/init.h>		/* Initiliasation support */
#include <linux/module.h>	/* Module support */
#include <linux/kernel.h>	/* Kernel support */
#include <linux/version.h>	/* Kernel version */
#include <linux/fs.h>		/* File operations (fops) defines */
#include <linux/errno.h>	/* Defines standard err codes */
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mmio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/ratelimit.h>

#define MAX_PRCMU_QOS_APP       (0x64)

#define CR_REG0_HTIMEN		(1 << 26)
#define SIA_TIMER_ITC		(0x5BC00)
#define SIA_ISP_MCU_SYS_SIZE	(0x100000)

#define XP70_BLOCK_SIZE		124
#define XP70_NB_BLOCK		50

#define clrbits32(_addr, _clear) \
	writel(readl(_addr) & ~(u32)(_clear), _addr)

#define setbits32(_addr, _set) \
	writel(readl(_addr) | (u32)(_set), _addr)

#define upper_16_bits(n) ((u16)((u32)(n) >> 16))

struct trace_block {
	u32 msg_id;
	char data[XP70_BLOCK_SIZE];
};

struct mmio_trace {
	u32 nb_block;
	u32 block_size;
	u32 block_id;
	u32 overwrite_count;
	struct trace_block block[XP70_NB_BLOCK];
};

struct trace_buffer_status {
	u32 prev_overwrite_count;
	u32 prev_block_id;
};

struct mmio_info {
	/* Config from board */
	struct mmio_platform_data  *pdata;
	/* My device */
	struct device              *dev;
	/* Runtime variables */
	struct miscdevice          misc_dev;
	void __iomem               *siabase;
	void __iomem               *crbase;
	/* States */
	int                        xshutdown_enabled;
	int                        xshutdown_is_active_high;
	/* tracing */
	struct trace_buffer_status trace_status;
	struct mmio_trace          *trace_buffer;
	struct delayed_work        trace_work;
	int                        trace_allowed;
	struct mutex               lock;
};

int copy_user_buffer(void __iomem **dest_buf, void __iomem *src_buf, u32 size);
int mmio_cam_init_mmdsp_timer(struct mmio_info *info);
int mmio_cam_initboard(struct mmio_info *info);
int mmio_cam_desinitboard(struct mmio_info *info);
int mmio_cam_pwr_sensor(struct mmio_info *info, enum mmio_bool_t on);
int mmio_cam_control_clocks(struct mmio_info *info, enum mmio_bool_t on);

#endif
