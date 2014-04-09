/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson NOVA dsilink device
 *
 * Author: Jimmy Rubin <jimmy.rubin@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __NOVA_DSILINK__H__
#define __NOVA_DSILINK__H__

#include <linux/device.h>

#define DSI_IO_AREA "DSI I/O Area"
#define MAX_NBR_OF_DSILINKS 4

#define DSILINK_MAX_DCS_READ   4
#define DSILINK_MAX_DSI_DIRECT_CMD_WRITE 16

/* Interface mode */
enum dsilink_irq {
	DSILINK_IRQ_BTA_TE		 = 0x1,
	DSILINK_IRQ_NO_TE		 = 0x2,
	DSILINK_IRQ_TRIGGER		 = 0x4,
	DSILINK_IRQ_TE_MISS		 = 0x8,
	DSILINK_IRQ_ACK_WITH_ERR	 = 0x10,
	DSILINK_IRQ_MISSING_VSYNC        = 0x20,
	DSILINK_IRQ_MISSING_DATA	 = 0x40,
};

/* Interface mode */
enum dsilink_mode {
	DSILINK_MODE_CMD = 0,
	DSILINK_MODE_VID = 1,
};

enum dsilink_sync_src {
	DSILINK_SYNCSRC_BTA		= 0, /* DSI BTA */
	DSILINK_SYNCSRC_TE_POLLING	= 1, /* DSI TE_POLLING */
};

/* DSI video mode */
enum dsilink_vid_mode {
	DSILINK_NON_BURST_MODE_WITH_SYNC_EVENT = 0,
	/* enables tvg, test video generator */
	DSILINK_NON_BURST_MODE_WITH_SYNC_EVENT_TVG_ENABLED = 1,
	DSILINK_BURST_MODE_WITH_SYNC_EVENT  = 2,
	DSILINK_BURST_MODE_WITH_SYNC_PULSE  = 3,
};

enum dsilink_cmd_nat {
	DSILINK_CMD_NAT_WRITE		= 0,
	DSILINK_CMD_NAT_READ		= 1,
	DSILINK_CMD_NAT_TE		= 2,
	DSILINK_CMD_NAT_TRIGGER		= 3,
	DSILINK_CMD_NAT_BTA		= 3,
};

enum dsilink_cmd_datatype {
	DSILINK_CMD_GENERIC_WRITE		= 0,
	DSILINK_CMD_DCS_WRITE			= 1,
	DSILINK_CMD_DCS_READ			= 2,
	DSILINK_CMD_SET_MAX_PKT_SIZE		= 3,
	DSILINK_CMD_TURN_ON_PERIPHERAL		= 4,
	DSILINK_CMD_SHUT_DOWN_PERIPHERAL	= 5,
};

enum dsilink_lane_status {
	DSILINK_LANE_STATE_START	= 0x00,
	DSILINK_LANE_STATE_IDLE		= 0x01,
	DSILINK_LANE_STATE_WRITE	= 0x02,
	DSILINK_LANE_STATE_ULPM		= 0x03,
	DSILINK_LANE_STATE_READ		= 0x04,
};

struct dsilink_dsi_vid_registers {
	bool	burst_mode;
	bool	sync_is_pulse;
	bool	tvg_enable;
	u32	hfp;
	u32	hbp;
	u32	hsa;
	u32	blkline_pck;
	u32	line_duration;
	u32	blkeol_pck;
	u32	blkeol_duration;
	u8	pixel_mode;
	u8	rgb_header;
};

/* Video mode descriptor */
struct dsilink_video_mode {
	u32 xres;
	u32 yres;
	u32 pixclock;	/* pixel clock in ps (pico seconds) */
	u32 hbp;	/* horizontal back porch: left margin (excl. hsync) */
	u32 hfp;	/* horizontal front porch: right margin (excl. hsync) */
	u32 hsw;	/* horizontal sync width */
	u32 vbp;	/* vertical back porch: upper margin (excl. vsync) */
	u32 vfp;	/* vertical front porch: lower margin (excl. vsync) */
	u32 vsw;	/* vertical sync width*/
	bool interlaced;
};


struct dsilink_phy {
	u8 virt_id;
	u8 num_data_lanes;
	u8 ui;
	bool clk_cont;
	bool host_eot_gen;
	/*
	* DPHY spec:
	* After power-up, the Slave side PHY shall be initialized when
	* the Master PHY drives a Stop State (LP-11) for a period
	* longer then TINIT. The first Stop state longer than the
	* specified TINIT is called the Initialization period.
	*/
	u32 t_init;

	/* DSI video operating modes */
	enum dsilink_vid_mode vid_mode;

	/*
	 * wakeup_time is the time to perform
	 * LP->HS on D-PHY. Given in clock
	 * cycles of byte clock frequency.
	 */
	u32 vid_wakeup_time;

	u32 hs_freq;
	u32 lp_freq;

	/* DSI data lanes are swapped if true */
	/* TODO handle more than two lanes swap */
	bool data_lanes_swap;
	/* For PBA test of manufactory process */	
	bool check_pba;
};

struct dsilink_port {
	enum dsilink_mode mode;
	enum dsilink_sync_src sync_src;
	u8 link;
	struct dsilink_phy phy;
};

struct dsilink_ops {
	void (*wait_while_running)(u8 *io, struct device *dev);
	u8 (*handle_irq)(u8 *io);
	int (*read)(u8 *io, struct device *dev, u8 cmd, u32 *data, int *len);
	int (*write)(u8 *io, struct device *dev, enum dsilink_cmd_datatype type,
						u8 cmd, u8 *data, int len);
	void (*te_request)(u8 *io);
	void (*force_stop)(u8 *io);
	int (*enable)(u8 *io, struct device *dev,
					const struct dsilink_port *port,
				struct dsilink_dsi_vid_registers *vid_regs);
	void (*disable)(u8 *io);
	int (*update_frame_parameters)(u8 *io,
					struct dsilink_video_mode *vmode,
				struct dsilink_dsi_vid_registers *vid_regs,
									u8 bpp);
	void (*set_clk_continous)(u8 *io, bool on);
	void (*enable_video_mode)(u8 *io, bool enable);
	int (*handle_ulpm)(u8 *io, struct device *dev,
			const struct dsilink_port *port, bool enter_ulpm);
};

struct dsilink_device {
	struct device				*dev;
	struct dsilink_ops			ops;

	struct regulator			*reg_epod_dss;
	struct regulator			*reg_vana;
	struct clk				*clk_dsi_sys;
	struct clk				*clk_dsi_hs;
	struct clk				*clk_dsi_lp;
	struct timer_list			te_timer;
	struct dsilink_port			port;
	struct dsilink_dsi_vid_registers	vid_regs;

	bool					reserved;
	bool					enabled;
	bool					update_dsi_freq;
	u8					*io;
	u32					version;
};

int nova_dsilink_setup(struct dsilink_device *dsilink,
					const struct dsilink_port *port);
int nova_dsilink_send_max_read_len(struct dsilink_device *dsilink);
int nova_dsilink_turn_on_peripheral(struct dsilink_device *dsilink);
int nova_dsilink_shut_down_peripheral(struct dsilink_device *dsilink);
void nova_dsilink_wait_while_running(struct dsilink_device *dsilink);
u8 nova_dsilink_handle_irq(struct dsilink_device *dsilink);
int nova_dsilink_dsi_write(struct dsilink_device *dsilink, u8 *data, int len);
int nova_dsilink_dcs_write(struct dsilink_device *dsilink, u8 cmd, u8 *data,
								int len);
int nova_dsilink_dsi_read(struct dsilink_device *dsilink, u8 cmd, u32 *data,
								int *len);
void nova_dsilink_te_request(struct dsilink_device *dsilink);
int nova_dsilink_enable(struct dsilink_device *dsilink);
void nova_dsilink_disable(struct dsilink_device *dsilink);
int nova_dsilink_update_frame_parameters(struct dsilink_device *dsilink,
				struct dsilink_video_mode *vmode, u8 bpp,
						u8 pixel_mode, u8 rgb_header);
void nova_dsilink_set_clk_continous(struct dsilink_device *dsilink, bool on);
void nova_dsilink_enable_video_mode(struct dsilink_device *dsilink, bool en);
int nova_dsilink_enter_ulpm(struct dsilink_device *dsilink);
int nova_dsilink_exit_ulpm(struct dsilink_device *dsilink);
int nova_dsilink_force_stop(struct dsilink_device *dsilink);
struct dsilink_device *nova_dsilink_get(int link_id);
void nova_dsilink_put(struct dsilink_device *dsilink);
#endif
