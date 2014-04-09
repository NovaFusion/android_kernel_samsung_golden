/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson MCDE display sub system frame buffer driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __MCDE_FB__H__
#define __MCDE_FB__H__

#include <linux/fb.h>
#include <linux/ioctl.h>
#if !defined(__KERNEL__) && !defined(_KERNEL)
#include <stdint.h>
#else
#include <linux/types.h>
#include <linux/hwmem.h>
#endif

#ifdef __KERNEL__
#include "mcde_dss.h"
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#endif

#define MCDE_GET_BUFFER_NAME_IOC _IO('M', 1)
#define MCDE_SET_VSCREENINFO_IOC _IOW('D', 2, struct fb_var_screeninfo)

#ifdef __KERNEL__
#define to_mcde_fb(x) ((struct mcde_fb *)(x)->par)

#define MCDE_FB_MAX_NUM_OVERLAYS 3

struct mcde_fb {
	int num_ovlys;
	struct mcde_overlay *ovlys[MCDE_FB_MAX_NUM_OVERLAYS];
	u32 pseudo_palette[17];
	enum mcde_ovly_pix_fmt pix_fmt;
	int id;
	struct hwmem_alloc *alloc;
	int alloc_name;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

/* MCDE fbdev API */
struct fb_info *mcde_fb_create(struct mcde_display_device *ddev,
		uint16_t w, uint16_t h, uint16_t vw, uint16_t vh,
		enum mcde_ovly_pix_fmt pix_fmt, uint32_t rotate);

int mcde_fb_attach_overlay(struct fb_info *fb_info,
	struct mcde_overlay *ovl);
void mcde_fb_destroy(struct mcde_display_device *ddev);

/* +452052 ESD recovery for DSI video */
int mcde_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi);
int mcde_fb_set_par(struct fb_info *fbi);
/* -452052 ESD recovery for DSI video */
/* MCDE fb driver */
int mcde_fb_init(void);
void mcde_fb_exit(void);
#endif

#endif /* __MCDE_FB__H__ */

