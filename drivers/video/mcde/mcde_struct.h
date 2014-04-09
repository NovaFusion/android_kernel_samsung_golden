/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson header defining MCDE structures
 *
 * Author: Craig Sutherland <craig.sutherland@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __MCDE_STRUCT__H__
#define __MCDE_STRUCT__H__

/* MCDE channel states
 *
 * Allowed state transitions:
 *   IDLE <-> SUSPEND
 *   IDLE <-> DSI_READ
 *   IDLE <-> DSI_WRITE
 *   IDLE -> SETUP -> (WAIT_TE ->) RUNNING -> STOPPING1 -> STOPPING2 -> IDLE
 *   WAIT_TE -> STOPPED (for missing TE to allow re-enable)
 */
enum chnl_state {
	CHNLSTATE_SUSPEND,   /* HW in suspended mode, initial state */
	CHNLSTATE_IDLE,      /* Channel acquired, but not running, FLOEN==0 */
	CHNLSTATE_DSI_READ,  /* Executing DSI read */
	CHNLSTATE_DSI_WRITE, /* Executing DSI write */
	CHNLSTATE_SETUP,     /* Channel register setup to prepare for running */
	CHNLSTATE_WAIT_TE,   /* Waiting for BTA or external TE */
	CHNLSTATE_RUNNING,   /* Update started, FLOEN=1, FLOEN==1 */
	CHNLSTATE_STOPPING,  /* Stopping, FLOEN=0, FLOEN==1, awaiting VCMP */
	CHNLSTATE_STOPPED,   /* Stopped, after VCMP, FLOEN==0|1 */
	CHNLSTATE_REQ_BTA_TE,/* Requesting BTA TE over DSI */
};

enum dsi_lane_status {
	DSI_LANE_STATE_START	= 0x00,
	DSI_LANE_STATE_IDLE	= 0x01,
	DSI_LANE_STATE_WRITE	= 0x02,
	DSI_LANE_STATE_ULPM	= 0x03,
};

struct ovly_regs {
	bool enabled;
	bool dirty;
	bool dirty_buf;

	u8   ch_id;
	u32  baseaddress0;
	u32  baseaddress1;
	u8   bits_per_pixel;
	u8   bpp;
	bool bgr;
	bool bebo;
	bool opq;
	u8   col_conv;
	u8   alpha_source;
	u8   alpha_value;
	u8   pixoff;
	u16  ppl;
	u16  lpf;
	u16  cropx;
	u16  cropy;
	u16  xpos;
	u16  ypos;
	u8   z;
};

struct mcde_ovly_state {
	bool inuse;
	u8 idx; /* MCDE overlay index */
	struct mcde_chnl_state *chnl; /* Owner channel */
	bool dirty;
	bool dirty_buf;

	/* Staged settings */
	u32 paddr;
	void *kaddr;
	u16 stride;
	enum mcde_ovly_pix_fmt pix_fmt;

	u16 src_x;
	u16 src_y;
	u16 dst_x;
	u16 dst_y;
	u16 dst_z;
	u16 w;
	u16 h;

	u8 alpha_source;
	u8 alpha_value;

	/* Applied settings */
	struct ovly_regs regs;
};

struct chnl_regs {
	bool dirty;

	bool floen;
	u16  x;
	u16  y;
	u16  ppl;
	u16  lpf;
	u8   bpp;
	bool internal_clk; /* CLKTYPE field */
	u16  pcd;
	u8   clksel;
	u8   cdwin;
	u16 (*map_r)(u8);
	u16 (*map_g)(u8);
	u16 (*map_b)(u8);
	bool palette_enable;
	bool oled_enable;
	bool background_yuv;
	bool bcd;
	bool roten;
	u8   rotdir;
	u32  rotbuf1;
	u32  rotbuf2;
	u32  rotbufsize;
	u32  ovly_xoffset;

	/* Blending */
	u8 blend_ctrl;
	bool blend_en;
	u8 alpha_blend;

	/* DSI */
	u8 dsipacking;
};

struct col_regs {
	bool dirty;

	u16 y_red;
	u16 y_green;
	u16 y_blue;
	u16 cb_red;
	u16 cb_green;
	u16 cb_blue;
	u16 cr_red;
	u16 cr_green;
	u16 cr_blue;
	u16 off_y;
	u16 off_cb;
	u16 off_cr;
};

struct tv_regs {
	bool dirty;

	u16 dho; /* TV mode: left border width; destination horizontal offset */
		 /* LCD MODE: horizontal back porch */
	u16 alw; /* TV mode: right border width */
		 /* LCD mode: horizontal front porch */
	u16 hsw; /* horizontal synch width */
	u16 dvo; /* TV mode: top border width; destination horizontal offset */
		 /* LCD MODE: vertical back porch */
	u16 bsl; /* TV mode: bottom border width; blanking start line */
		 /* LCD MODE: vertical front porch */
	/* field 1 */
	u16 bel1; /* TV mode: field total vertical blanking lines */
		 /* LCD mode: vertical sync width */
	u16 fsl1; /* field vbp */
	/* field 2 */
	u16 bel2;
	u16 fsl2;
	u8 tv_mode;
	bool sel_mode_tv;
	bool inv_clk;
	bool interlaced_en;
	u32 lcdtim1;
};

struct oled_regs {
	bool dirty;

	u16 alfa_red;
	u16 alfa_green;
	u16 alfa_blue;
	u16 beta_red;
	u16 beta_green;
	u16 beta_blue;
	u16 gamma_red;
	u16 gamma_green;
	u16 gamma_blue;
	u16 off_red;
	u16 off_green;
	u16 off_blue;
};

struct mcde_chnl_state {
	bool enabled;
	bool reserved;
	enum mcde_chnl id;
	enum mcde_fifo fifo;
	struct mcde_port port;
	struct mcde_ovly_state *ovly0;
	struct mcde_ovly_state *ovly1;
	enum chnl_state state;
	wait_queue_head_t state_waitq;
	wait_queue_head_t vcmp_waitq;
	wait_queue_head_t vsync_waitq;
	atomic_t vcmp_cnt;
	int vcmp_cnt_wait;
	atomic_t vsync_cnt;
	int vsync_cnt_wait;
	atomic_t n_vsync_capture_listeners;
	bool is_bta_te_listening;	
	bool oled_color_conversion;
	/* If the channel or and any ovly needs to change it color conversion */
	bool update_color_conversion;
	struct clk *clk_dpi;
	struct dsilink_device *dsilink;

	enum mcde_display_power_mode power_mode;

	/* Staged settings */
	u16 (*map_r)(u8);
	u16 (*map_g)(u8);
	u16 (*map_b)(u8);
	bool palette_enable;
	struct mcde_video_mode vmode;
	enum mcde_hw_rotation hw_rot; /* The MCDE rotation of the display */
	u32 rotbuf1;
	u32 rotbuf2;
	u32 rotbufsize;

	struct mcde_col_transform rgb_2_ycbcr;
	struct mcde_col_transform ycbcr_2_rgb;
	struct mcde_col_transform *transform;
	struct mcde_oled_transform *oled_transform;

	/* Blending */
	u8 blend_ctrl;
	bool blend_en;
	u8 alpha_blend;

	/* Applied settings */
	struct chnl_regs regs;
	struct col_regs  col_regs;
	struct tv_regs   tv_regs;
	struct oled_regs oled_regs;

	/* an interlaced digital TV signal generates a VCMP per field */
	bool vcmp_per_field;
	bool even_vcmp;

	bool formatter_updated;
	bool esram_is_enabled;

	bool first_frame_vsync_fix;
	bool force_disable;

	atomic_t force_restart;
	int force_restart_frame_cnt;
	int force_restart_first_cnt;
	struct work_struct restart_work;
};

#endif /* __MCDE_STRUCT__H__ */
