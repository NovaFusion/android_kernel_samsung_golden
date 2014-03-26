/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson DSI link device driver
 *
 * Author: Jimmy Rubin <jimmy.rubin@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>

/* TODO See if the parent clock can be used in the clock framework instead */
#include <linux/mfd/dbx500-prcmu.h>

#include <video/nova_dsilink.h>

#include "dsilink_regs.h"
#include "dsilink.h"
#include "dsilink_debugfs.h"

#define CLK_DSI_SYS	"dsisys"
#define CLK_DSI_LP	"dsilp"
#define CLK_DSI_HS	"dsihs"
#define REG_EPOD_DSS	"vsupply"
#define REG_VANA	"vdddsi1v2"

#define DSILINK_VERSION_3 0x02327820
#define DSILINK_VERSION_2_1 0x02327568
#define DSILINK_VERSION_2_0 0x02327457

/* TODO Check how long these timeouts should be */
#define DSI_TE_NO_ANSWER_TIMEOUT_INIT_MS 2500
#define DSI_TE_NO_ANSWER_TIMEOUT_MS 250

/*
 * Default init period according to spec if not
 * specified in the port struct
 */
#define DSI_DPHY_DEFAULT_TINIT 100

static struct dsilink_device *dsilinks[MAX_NBR_OF_DSILINKS];

static void set_link_reset(u8 link)
{
	u32 value;

	value = prcmu_read(DB8500_PRCM_DSI_SW_RESET);
	switch (link) {
	case 0:
		value &= ~DB8500_PRCM_DSI_SW_RESET_DSI0_SW_RESETN;
		break;
	case 1:
		value &= ~DB8500_PRCM_DSI_SW_RESET_DSI1_SW_RESETN;
		break;
	case 2:
		value &= ~DB8500_PRCM_DSI_SW_RESET_DSI2_SW_RESETN;
		break;
	default:
		break;
	}
	prcmu_write(DB8500_PRCM_DSI_SW_RESET, value);
}

static void release_link_reset(u8 link)
{
	u32 value;

	value = prcmu_read(DB8500_PRCM_DSI_SW_RESET);
	switch (link) {
	case 0:
		value |= DB8500_PRCM_DSI_SW_RESET_DSI0_SW_RESETN;
		break;
	case 1:
		value |= DB8500_PRCM_DSI_SW_RESET_DSI1_SW_RESETN;
		break;
	case 2:
		value |= DB8500_PRCM_DSI_SW_RESET_DSI2_SW_RESETN;
		break;
	default:
		break;
	}
	prcmu_write(DB8500_PRCM_DSI_SW_RESET, value);
}

static inline void te_poll_set_timer(struct dsilink_device *dsilink,
							unsigned int timeout)
{
	mod_timer(&dsilink->te_timer, jiffies + msecs_to_jiffies(timeout));
}

static void te_timer_function(unsigned long arg)
{
	struct dsilink_device *dsilink;

	if (WARN_ON(arg >= MAX_NBR_OF_DSILINKS))
		return;

	dsilink = dsilinks[arg];

	if (dsilink->reserved && dsilink->enabled) {
		/* No TE answer; force stop */
		dsilink->ops.force_stop(dsilink->io);
		dev_warn(dsilink->dev, "%s force stop\n", __func__);
		te_poll_set_timer(dsilink, DSI_TE_NO_ANSWER_TIMEOUT_MS);
	}
}

static void disable_clocks_and_power(struct dsilink_device *dsilink)
{
	clk_disable(dsilink->clk_dsi_hs);
	clk_disable(dsilink->clk_dsi_lp);
	clk_disable(dsilink->clk_dsi_sys);
	regulator_disable(dsilink->reg_epod_dss);
	if (dsilink->reg_vana)
		regulator_disable(dsilink->reg_vana);
}

int nova_dsilink_setup(struct dsilink_device *dsilink,
						const struct dsilink_port *port)
{
	if (!dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	dsilink_debugfs_create();

	dsilink->port = *port;

	if (port->mode == DSILINK_MODE_VID) {
		switch (port->phy.vid_mode) {
		case DSILINK_NON_BURST_MODE_WITH_SYNC_EVENT:
			dsilink->vid_regs.burst_mode = false;
			dsilink->vid_regs.sync_is_pulse = false;
			dsilink->vid_regs.tvg_enable = false;
			break;
		case DSILINK_NON_BURST_MODE_WITH_SYNC_EVENT_TVG_ENABLED:
			dsilink->vid_regs.burst_mode = false;
			dsilink->vid_regs.sync_is_pulse = false;
			dsilink->vid_regs.tvg_enable = true;
			break;
		case DSILINK_BURST_MODE_WITH_SYNC_EVENT:
			dsilink->vid_regs.burst_mode = true;
			dsilink->vid_regs.sync_is_pulse = false;
			dsilink->vid_regs.tvg_enable = false;
			break;
		case DSILINK_BURST_MODE_WITH_SYNC_PULSE:
			dsilink->vid_regs.burst_mode = true;
			dsilink->vid_regs.sync_is_pulse = true;
			dsilink->vid_regs.tvg_enable = false;
			break;
		default:
			dev_err(dsilink->dev, "Unsupported video mode");
			goto error;
		}
	}

	return 0;

error:
	return -EINVAL;
}

/*
 * Set Maximum Return Packet size is a command that specifies the
 * maximum size of the payload transmitted from peripheral back to
 * the host processor.
 *
 * During power-on or reset sequence, the Maximum Return Packet Size
 * is set to a default value of one. In order to be able to use
 * mcde_dsi_dcs_read for reading more than 1 byte at a time, this
 * parameter should be set by the host processor to the desired value
 * in the initialization routine before commencing normal operation.
 */
int nova_dsilink_send_max_read_len(struct dsilink_device *dsilink)
{
	u8 data[2] = { DSILINK_MAX_DCS_READ & 0xFF, DSILINK_MAX_DCS_READ >> 8 };
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	return dsilink->ops.write(dsilink->io, dsilink->dev,
				DSILINK_CMD_SET_MAX_PKT_SIZE, -1, data, 2);
}

int nova_dsilink_turn_on_peripheral(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	return dsilink->ops.write(dsilink->io, dsilink->dev,
				DSILINK_CMD_TURN_ON_PERIPHERAL, -1, NULL, 0);
}

int nova_dsilink_shut_down_peripheral(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	return dsilink->ops.write(dsilink->io, dsilink->dev,
				DSILINK_CMD_SHUT_DOWN_PERIPHERAL, -1, NULL, 0);
}

/*
 * Wait for CSM_RUNNING, all data sent for display
 */
void nova_dsilink_wait_while_running(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return;

	DSILINK_TRACE(dsilink->dev);

	dsilink->ops.wait_while_running(dsilink->io, dsilink->dev);
}

u8 nova_dsilink_handle_irq(struct dsilink_device *dsilink)
{
	u8 ret = 0;

	if (!dsilink->enabled)
		return ret;

	ret = dsilink->ops.handle_irq(dsilink->io);

	if (ret & DSILINK_IRQ_BTA_TE) {
		dev_vdbg(dsilink->dev, "BTA TE DSI\n");
	} else if (ret & DSILINK_IRQ_NO_TE) {
		dev_warn(dsilink->dev, "NO_TE DSI\n");
	} else if (ret & DSILINK_IRQ_TRIGGER) {
		dev_vdbg(dsilink->dev, "TRIGGER\n");
		if (dsilink->port.sync_src == DSILINK_SYNCSRC_TE_POLLING)
			te_poll_set_timer(dsilink,
						DSI_TE_NO_ANSWER_TIMEOUT_MS);
	}
	else if (ret & DSILINK_IRQ_ACK_WITH_ERR) {
		dev_dbg(dsilink->dev, "ACK WITH ERR\n");
	} else if (ret & DSILINK_IRQ_TE_MISS) {
		dev_dbg(dsilink->dev, "TE MISS\n");
	}

	return ret;
}

int nova_dsilink_dcs_write(struct dsilink_device *dsilink, u8 cmd,
							u8 *data, int len)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	dsilink_debugfs_print_cmd(cmd, data, len, "WRITE");

	return dsilink->ops.write(dsilink->io, dsilink->dev,
							DSILINK_CMD_DCS_WRITE,
								cmd, data, len);
}

int nova_dsilink_dsi_write(struct dsilink_device *dsilink, u8 *data, int len)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	return dsilink->ops.write(dsilink->io, dsilink->dev,
						DSILINK_CMD_GENERIC_WRITE,
								-1, data, len);
}

int nova_dsilink_dsi_read(struct dsilink_device *dsilink,
						u8 cmd, u32 *data, int *len)
{
	int ret;
	if (WARN_ON_ONCE(!dsilink->reserved))
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	ret = dsilink->ops.read(dsilink->io, dsilink->dev, cmd, data, len);
	if (ret == -EAGAIN)
		dsilink->ops.force_stop(dsilink->io);

	dsilink_debugfs_print_cmd(cmd, (u8 *)data, *len, "READ");

	return ret;
}

void nova_dsilink_te_request(struct dsilink_device *dsilink)
{
	if (WARN_ON_ONCE(!dsilink->reserved))
		return;

	DSILINK_TRACE(dsilink->dev);

	dsilink->ops.te_request(dsilink->io);
}

int nova_dsilink_enable(struct dsilink_device *dsilink)
{
	u32 t_init;
	if (dsilink->enabled || !dsilink->reserved)
		return -EBUSY;

	DSILINK_TRACE(dsilink->dev);

	if (dsilink->reg_vana)
		regulator_enable(dsilink->reg_vana);

	regulator_enable(dsilink->reg_epod_dss);

	if (dsilink->update_dsi_freq) {
		long dsi_lp_freq;
		long dsi_hs_freq;
		int ret;
		u32 ui_value;

		dsi_lp_freq = clk_round_rate(
				dsilink->clk_dsi_lp, dsilink->port.phy.lp_freq);

		if (dsilink->port.phy.lp_freq != dsi_lp_freq)
			/*
			 * The actual lp freq should be inside
			 * 2/3 * tx_esc_clock < dsi_lp_freq < 3/2 * tx_esc_clock
			 * where tx_esc_clock = dsilink->port.phy.lp_freq
			 */
			dev_dbg(dsilink->dev, "Could not set dsi lp freq"
				" to %d got %ld\n",
					dsilink->port.phy.lp_freq, dsi_lp_freq);

		WARN_ON_ONCE(clk_set_rate(
			dsilink->clk_dsi_lp, dsilink->port.phy.lp_freq));

		dsi_hs_freq = clk_round_rate(
				dsilink->clk_dsi_hs, dsilink->port.phy.hs_freq);

		if (dsilink->port.phy.hs_freq != dsi_hs_freq)
			dev_dbg(dsilink->dev, "Could not set dsi hs freq"
				" to %d got %ld\n",
					dsilink->port.phy.hs_freq, dsi_hs_freq);

		/*
		 * If clk_set_rate fails it is likely
		 * that the clock is already enabled
		 */
		ret = clk_set_rate(dsilink->clk_dsi_hs,
						dsilink->port.phy.hs_freq);
		if (ret)
			dev_dbg(dsilink->dev,
					"clk_set_rate failed with ret %x", ret);

		/* if a display driver has set a value this should be used */
		if (dsilink->port.phy.ui == 0) {
			/* Calulate in MHz instead */
			dsi_hs_freq = dsi_hs_freq / 1000000;
			ui_value = 4000 / dsi_hs_freq;
			dsilink->port.phy.ui = (u8) ui_value;
		}
		dev_dbg(dsilink->dev, "ui value %d dsi_hs_freq %ld in Mhz\n",
					dsilink->port.phy.ui, dsi_hs_freq);
	}

	clk_enable(dsilink->clk_dsi_sys);
	clk_enable(dsilink->clk_dsi_hs);
	clk_enable(dsilink->clk_dsi_lp);

	release_link_reset(dsilink->port.link);

	if (dsilink->ops.enable(dsilink->io, dsilink->dev, &dsilink->port,
							&dsilink->vid_regs))
		goto error;

	/*
	* DPHY spec:
	* After power-up, the Slave side PHY shall be initialized when
	* the Master PHY drives a Stop State (LP-11) for a period
	* longer then TINIT. The first Stop state longer than the
	* specified TINIT is called the Initialization period.
	*/
	if (dsilink->port.phy.t_init == 0)
		t_init = DSI_DPHY_DEFAULT_TINIT;
	else
		t_init = dsilink->port.phy.t_init;
	usleep_range(t_init, t_init);
	dev_dbg(dsilink->dev, "t_init %d\n", t_init);

	if (dsilink->port.sync_src == DSILINK_SYNCSRC_TE_POLLING) {
		init_timer(&dsilink->te_timer);
		dsilink->te_timer.function = te_timer_function;
		dsilink->te_timer.data = dsilink->port.link;
	}

	dsilink->enabled = true;
	dev_dbg(dsilink->dev, "%s link enabled\n", __func__);
	return 0;

error:
	dev_warn(dsilink->dev, "%s enable failed\n", __func__);
	disable_clocks_and_power(dsilink);
	return -EINVAL;
}

void nova_dsilink_disable(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return;

	DSILINK_TRACE(dsilink->dev);

	if (dsilink->port.sync_src == DSILINK_SYNCSRC_TE_POLLING)
		del_timer_sync(&dsilink->te_timer);

	dsilink->ops.disable(dsilink->io);

	disable_clocks_and_power(dsilink);

	dsilink->enabled = false;

	dev_dbg(dsilink->dev, "link disabled\n");
}

/*
 * The rgb_header identifies the pixel stream format,
 * as described in the MIPI DSI Specification:
 *
 * 0x0E: Packed pixel stream, 16-bit RGB, 565 format
 * 0x1E: Packed pixel stream, 18-bit RGB, 666 format
 * 0x2E: Loosely Packed pixel stream, 18-bit RGB, 666 format
 * 0x3E: Packed pixel stream, 24-bit RGB, 888 format
 */
int nova_dsilink_update_frame_parameters(struct dsilink_device *dsilink,
				struct dsilink_video_mode *vmode, u8 bpp,
						u8 pixel_mode, u8 rgb_header)
{
	u64 bpl;
	u32 blkline_pck, line_duration;
	u32 blkeol_pck, blkeol_duration;
	int hfp = 0, hbp = 0, hsa = 0;

	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	if (dsilink->port.mode == DSILINK_MODE_CMD)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	dsilink->vid_regs.pixel_mode = pixel_mode;
	dsilink->vid_regs.rgb_header = rgb_header;
	/*
	 * vmode->hfp, vmode->hbp and vmode->hsw are given in pixels
	 * and must be re-calculated into bytes
	 *
	 * 6 + 2 is HFP header + checksum
	 */
	hfp = vmode->hfp * bpp - 6 - 2;
	if (dsilink->vid_regs.sync_is_pulse) {
		/*
		 * 6 is HBP header + checksum
		 * 4 is RGB header + checksum
		 */
		hbp = vmode->hbp * bpp - 4 - 6;
		/*
		 * 6 is HBP header + checksum
		 * 4 is HSW packet bytes
		 * 4 is RGB header + checksum
		 */
		hsa = vmode->hsw * bpp - 4 - 4 - 6;
	} else {
		/*
		 * 6 is HBP header + checksum
		 * 4 is HSW packet bytes
		 * 4 is RGB header + checksum
		 */
		hbp = (vmode->hbp + vmode->hsw) * bpp - 4 - 4 - 6;
		/* HSA is not considered in this mode and set to 0 */
		hsa = 0;
	}
	if (hfp < 0) {
		hfp = 0;
		dev_warn(dsilink->dev,
			"%s: negative calc for hfp, set to 0\n", __func__);
	}
	if (hbp < 0) {
		hbp = 0;
		dev_warn(dsilink->dev,
			"%s: negative calc for hbp, set to 0\n", __func__);
	}
	if (hsa < 0) {
		hsa = 0;
		dev_warn(dsilink->dev,
			"%s: negative calc for hsa, set to 0\n", __func__);
	}

	/*
	 * vmode->pixclock is the time between two pixels (in picoseconds)
	 */
	bpl = vmode->pixclock *
			(vmode->hsw + vmode->hbp + vmode->xres + vmode->hfp);
	bpl *= dsilink->port.phy.hs_freq / 8;
	do_div(bpl, 1000000);
	do_div(bpl, 1000000);
	bpl *= dsilink->port.phy.num_data_lanes;

	/*
	 * 6 is header + checksum, header = 4 bytes, checksum = 2 bytes
	 * 4 is short packet for vsync/hsync
	 */
	if (dsilink->vid_regs.sync_is_pulse)
		blkline_pck = bpl - vmode->hsw - 6;
	else
		blkline_pck = bpl - 4 - 6;

	line_duration = (blkline_pck + 6) / dsilink->port.phy.num_data_lanes;
	blkeol_pck = bpl -
		(vmode->hsw + vmode->hbp + vmode->xres + vmode->hfp) * bpp - 6;
	blkeol_duration = (blkeol_pck + 6) / dsilink->port.phy.num_data_lanes;

	dsilink->vid_regs.hfp = hfp;
	dsilink->vid_regs.hbp = hbp;
	dsilink->vid_regs.hsa = hsa;
	dsilink->vid_regs.blkline_pck = blkline_pck;
	dsilink->vid_regs.line_duration = line_duration;
	dsilink->vid_regs.blkeol_pck = blkeol_pck;
	dsilink->vid_regs.blkeol_duration = blkeol_duration;

	return dsilink->ops.update_frame_parameters(dsilink->io, vmode,
						&dsilink->vid_regs, bpp);
}

void nova_dsilink_set_clk_continous(struct dsilink_device *dsilink, bool on)
{
	bool value = false;

	if (!dsilink->reserved)
		return;

	DSILINK_TRACE(dsilink->dev);

	if (on)
		value = dsilink->port.phy.clk_cont;

	dsilink->ops.set_clk_continous(dsilink->io, value);
}

void nova_dsilink_enable_video_mode(struct dsilink_device *dsilink, bool enable)
{
	if (!dsilink->reserved)
		return;

	DSILINK_TRACE(dsilink->dev);

	dsilink->ops.enable_video_mode(dsilink->io, enable);
}

int nova_dsilink_enter_ulpm(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	return dsilink->ops.handle_ulpm(dsilink->io, dsilink->dev,
				&dsilink->port, true);
}

int nova_dsilink_exit_ulpm(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	return dsilink->ops.handle_ulpm(dsilink->io, dsilink->dev,
				&dsilink->port, false);
}

int nova_dsilink_force_stop(struct dsilink_device *dsilink)
{
	if (!dsilink->enabled || !dsilink->reserved)
		return -EINVAL;

	DSILINK_TRACE(dsilink->dev);

	dsilink->ops.force_stop(dsilink->io);

	return 0;
}

struct dsilink_device *nova_dsilink_get(int dev_idx)
{
	struct dsilink_device *dsilink = NULL;

	if (dev_idx >= MAX_NBR_OF_DSILINKS)
		return ERR_PTR(-EINVAL);

	dsilink = dsilinks[dev_idx];

	DSILINK_TRACE(dsilink->dev);

	if (dsilink == NULL)
		return ERR_PTR(-EINVAL);

	if (dsilink->reserved)
		return ERR_PTR(-EBUSY);

	dsilink->reserved = true;

	return dsilink;
}

void nova_dsilink_put(struct dsilink_device *dsilink)
{
	DSILINK_TRACE(dsilink->dev);

	if (dsilink->enabled)
		nova_dsilink_disable(dsilink);

	dsilink->reserved = false;
}

static int __devinit nova_dsilink_probe(struct platform_device *pdev)
{
	struct dsilink_device *dsilink;
	struct resource *res;
	int ret = 0;
	char dsihsclkname[10];
	char dsilpclkname[10];

	dsilink = kzalloc(sizeof(struct dsilink_device), GFP_KERNEL);
	if (!dsilink)
		return -ENOMEM;

	if (pdev->id >= MAX_NBR_OF_DSILINKS) {
		ret = -EINVAL;
		goto failed_to_get_correct_id;
	}

	dsilink->dev = &pdev->dev;

	dsilinks[pdev->id] = dsilink;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_warn(&pdev->dev, "get resource failed\n");
		ret = -ENODEV;
		goto failed_get_resource;
	}

	dsilink->io = ioremap(res->start, resource_size(res));
	if (!dsilink->io) {
		dev_warn(&pdev->dev, "iomap failed\n");
		ret = -EINVAL;
		goto failed_map_dsi_io;
	}

	dev_info(&pdev->dev, "iomap: 0x%.8X->0x%.8X\n",
				(u32)res->start, (u32)dsilink->io);


	/* Not all platform has VANA as a regulator */
	dsilink->reg_vana = regulator_get(&pdev->dev, REG_VANA);
	if (IS_ERR(dsilink->reg_vana)) {
		dsilink->reg_vana = NULL;
		dev_dbg(&pdev->dev,
			"%s: Failed to get regulator vana\n", __func__);
	}

	dsilink->reg_epod_dss = regulator_get(&pdev->dev, REG_EPOD_DSS);
	if (IS_ERR(dsilink->reg_epod_dss)) {
		ret = PTR_ERR(dsilink->reg_epod_dss);
		dev_warn(&pdev->dev,
			"%s: Failed to get regulator vsupply\n", __func__);
		goto failed_get_epod_dss;
	}

	dsilink->clk_dsi_sys = clk_get(&pdev->dev, CLK_DSI_SYS);
	if (IS_ERR(dsilink->clk_dsi_sys)) {
		ret = PTR_ERR(dsilink->clk_dsi_sys);
		dev_warn(&pdev->dev, "%s: Failed to get sys clock\n",
								__func__);
		goto failed_to_get_dsi_sys;
	}

	sprintf(dsihsclkname, "%s%d", CLK_DSI_HS, pdev->id);
	dsilink->clk_dsi_hs = clk_get(&pdev->dev, dsihsclkname);
	if (IS_ERR(dsilink->clk_dsi_hs)) {
		ret = PTR_ERR(dsilink->clk_dsi_hs);
		dev_warn(&pdev->dev, "%s: Failed to get clock %s\n",
						__func__, dsihsclkname);
		goto failed_to_get_dsi_hs;
	}

	sprintf(dsilpclkname, "%s%d", CLK_DSI_LP, pdev->id);
	dsilink->clk_dsi_lp = clk_get(&pdev->dev, dsilpclkname);
	if (IS_ERR(dsilink->clk_dsi_lp)) {
		ret = PTR_ERR(dsilink->clk_dsi_lp);
		dev_warn(&pdev->dev, "%s: Failed to get clock %s\n",
						__func__, dsilpclkname);
		goto failed_to_get_dsi_lp;
	}

#ifndef CONFIG_MACH_SAMSUNG_UX500
	/* VANA needs to be on in order to read out the hw version */
	if (dsilink->reg_vana)
		regulator_enable(dsilink->reg_vana);
	regulator_enable(dsilink->reg_epod_dss);
	clk_enable(dsilink->clk_dsi_sys);
	clk_enable(dsilink->clk_dsi_hs);
	clk_enable(dsilink->clk_dsi_lp);
	release_link_reset(pdev->id);
#endif
	dsilink->version = dsi_rreg(dsilink->io, DSI_ID_REG);

	dev_info(dsilink->dev, "hw revision 0x%.8X\n", dsilink->version);

#ifndef CONFIG_MACH_SAMSUNG_UX500
	set_link_reset(pdev->id);
	clk_disable(dsilink->clk_dsi_hs);
	clk_disable(dsilink->clk_dsi_lp);
	clk_disable(dsilink->clk_dsi_sys);
	regulator_disable(dsilink->reg_epod_dss);
	if (dsilink->reg_vana)
		regulator_disable(dsilink->reg_vana);
#endif
	if (dsilink->version == DSILINK_VERSION_3)
		nova_dsilink_v3_init(&dsilink->ops);
	else if (dsilink->version == DSILINK_VERSION_2_1 ||
				dsilink->version == DSILINK_VERSION_2_0)
		nova_dsilink_v2_init(&dsilink->ops);
	else
		goto failed_wrong_dsilink_version;

	platform_set_drvdata(pdev, dsilink);

	DSILINK_TRACE(dsilink->dev);

	/* Wait 100 us before returning */
	usleep_range(100, 100);

	return 0;

failed_wrong_dsilink_version:
	dev_warn(dsilink->dev, "Unsupported dsilink version\n");
failed_to_get_dsi_lp:
	clk_put(dsilink->clk_dsi_hs);
failed_to_get_dsi_hs:
	clk_put(dsilink->clk_dsi_sys);
failed_to_get_dsi_sys:
	regulator_put(dsilink->reg_epod_dss);
failed_get_epod_dss:
	if (dsilink->reg_vana)
		regulator_put(dsilink->reg_vana);
	dsilink->reg_epod_dss = NULL;
	iounmap(dsilink->io);
failed_map_dsi_io:
failed_get_resource:
failed_to_get_correct_id:
	kfree(dsilink);
	return ret;
}

static struct platform_driver dsilink_driver = {
	.driver = {
		.name = "dsilink",
	},
	.probe	= nova_dsilink_probe,
};

static int __init dsilink_init(void)
{
	return platform_driver_register(&dsilink_driver);
}
fs_initcall(dsilink_init);
