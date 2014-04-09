/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson DSI link device driver for link version 2
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

#include <video/nova_dsilink.h>

#include "dsilink_regs.h"
#include "dsilink.h"

/*
 * Wait for CSM_RUNNING, all data sent for display
 */
static bool Ispba_status = false;
static void wait_while_running(u8 *io, struct device *dev)
{
	u8 counter = DSI_READ_TIMEOUT_MS;

	while (dsi_rfld(io, DSI_CMD_MODE_STS, CSM_RUNNING) && --counter)
		udelay(DSI_READ_DELAY_US);
	WARN_ON(!counter);
	if (!counter)
		dev_warn(dev, "%s: read timeout!\n", __func__);
}

static u8 handle_irq(u8 *io)
{
	u32 irq_status;
	u8 ret = 0;

	irq_status = dsi_rreg(io, DSI_DIRECT_CMD_STS_FLAG);
	if (irq_status & DSI_DIRECT_CMD_STS_FLAG_TE_RECEIVED_FLAG_MASK)
		ret |= DSILINK_IRQ_BTA_TE;

	if (irq_status & DSI_DIRECT_CMD_STS_FLAG_TRIGGER_RECEIVED_FLAG_MASK)
		/* DSI TE polling answer received */
		ret |= DSILINK_IRQ_TRIGGER;

	if (irq_status &
		DSI_DIRECT_CMD_STS_FLAG_ACKNOWLEDGE_WITH_ERR_RECEIVED_FLAG_MASK)
		ret |= DSILINK_IRQ_ACK_WITH_ERR;

	dsi_wreg(io, DSI_DIRECT_CMD_STS_CLR, irq_status);

	irq_status = dsi_rreg(io, DSI_CMD_MODE_STS_FLAG);
	if (irq_status & DSI_CMD_MODE_STS_FLAG_ERR_NO_TE_FLAG_MASK)
		ret |= DSILINK_IRQ_NO_TE;

	if (irq_status & DSI_CMD_MODE_STS_FLAG_ERR_TE_MISS_FLAG_MASK)
		ret |= DSILINK_IRQ_TE_MISS;

	dsi_wreg(io, DSI_CMD_MODE_STS_CLR, irq_status);

	irq_status = dsi_rreg(io, DSI_VID_MODE_STS_FLAG);
	dsi_wreg(io, DSI_VID_MODE_STS_CLR, irq_status);
	if (irq_status & DSI_VID_MODE_STS_FLAG_ERR_MISSING_DATA_FLAG_MASK)
		ret |= DSILINK_IRQ_MISSING_DATA;
	if (irq_status & DSI_VID_MODE_STS_FLAG_ERR_MISSING_VSYNC_FLAG_MASK)
		ret |= DSILINK_IRQ_MISSING_VSYNC;

	return ret;
}

static int write(u8 *io, struct device *dev, enum dsilink_cmd_datatype type,
					u8 dcs_cmd, u8 *data, int data_len)
{
	int i, ret = 0;
	u32 wrdat[4] = { 0, 0, 0, 0 };
	u32 settings, pck_len, loop_counter;
	const u32 loop_delay_us = 10 /* us */;

	if (data_len < 0)
		return -EINVAL;

	/* Setup packet data and length */
	if (type == DSILINK_CMD_DCS_WRITE) {
		pck_len = data_len + 1;
		if (pck_len > DSILINK_MAX_DSI_DIRECT_CMD_WRITE)
			return -EINVAL;
		wrdat[0] = dcs_cmd;
		for (i = 1; i <= data_len; i++)
			wrdat[i>>2] |= ((u32)data[i-1] << ((i & 3) * 8));
	} else if (type == DSILINK_CMD_GENERIC_WRITE) {
		pck_len = data_len;
		if (pck_len > DSILINK_MAX_DSI_DIRECT_CMD_WRITE)
			return -EINVAL;
		for (i = 0; i < data_len; i++)
			wrdat[i>>2] |= ((u32)data[i] << ((i & 3) * 8));
	} else if (type == DSILINK_CMD_SET_MAX_PKT_SIZE) {
		pck_len = 2;
		if (data_len != 2)
			return -EINVAL;
		wrdat[0] = data[0] | ((u32)data[1] << 8);
	} else {
		pck_len = 0;
	}

	/* Setup command */
	settings = DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_NAT_ENUM(WRITE) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LONGNOTSHORT(pck_len > 2) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_ID(0) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_SIZE(pck_len) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LP_EN(true);

	switch (type) {
	case DSILINK_CMD_DCS_WRITE:
		if (data_len == 0)
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							DCS_SHORT_WRITE_0);
		else if (data_len == 1)
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							DCS_SHORT_WRITE_1);
		else
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							DCS_LONG_WRITE);
		break;
	case DSILINK_CMD_GENERIC_WRITE:
		if (pck_len == 0)
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							GENERIC_SHORT_WRITE_0);
		else if (pck_len == 1)
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							GENERIC_SHORT_WRITE_1);
		else if (pck_len == 2)
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							GENERIC_SHORT_WRITE_2);
		else
			settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							GENERIC_LONG_WRITE);
		break;
	case DSILINK_CMD_SET_MAX_PKT_SIZE:
		settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							SET_MAX_PKT_SIZE);
		break;
	case DSILINK_CMD_TURN_ON_PERIPHERAL:
		settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							TURN_ON_PERIPHERAL);
		break;
	case DSILINK_CMD_SHUT_DOWN_PERIPHERAL:
		settings |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(
							SHUT_DOWN_PERIPHERAL);
		break;
	default:
		return -EINVAL;
	}

	dsi_wreg(io, DSI_DIRECT_CMD_MAIN_SETTINGS, settings);
	dsi_wreg(io, DSI_DIRECT_CMD_WRDAT0, wrdat[0]);
	if (pck_len >  4)
		dsi_wreg(io, DSI_DIRECT_CMD_WRDAT1, wrdat[1]);
	if (pck_len >  8)
		dsi_wreg(io, DSI_DIRECT_CMD_WRDAT2, wrdat[2]);
	if (pck_len > 12)
		dsi_wreg(io, DSI_DIRECT_CMD_WRDAT3, wrdat[3]);

	dsi_wreg(io, DSI_DIRECT_CMD_STS_CLR, ~0);
	dsi_wreg(io, DSI_CMD_MODE_STS_CLR, ~0);
	dsi_wreg(io, DSI_DIRECT_CMD_SEND, true);

	loop_counter = DSI_WRITE_CMD_TIMEOUT_MS * 1000 / loop_delay_us;
	while (!dsi_rfld(io, DSI_DIRECT_CMD_STS, WRITE_COMPLETED)
							&& --loop_counter)
		usleep_range(loop_delay_us, (loop_delay_us * 3) / 2);

	if (!loop_counter) {
		dev_err(dev, "%s: DSI write cmd 0x%x timeout!\n", __func__,
								dcs_cmd);
		ret = -ETIME;
	} else {
		dev_vdbg(dev, "DSI Write ok %x error %x\n",
					dsi_rreg(io, DSI_DIRECT_CMD_STS_FLAG),
					dsi_rreg(io, DSI_CMD_MODE_STS_FLAG));
	}

	return ret;
}

static void te_request(u8 *io)
{
	u32 settings;

	settings = DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_NAT_ENUM(TE_REQ) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LONGNOTSHORT(false) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_ID(0) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_SIZE(2) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LP_EN(true) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(DCS_SHORT_WRITE_1);
	dsi_wreg(io, DSI_DIRECT_CMD_MAIN_SETTINGS, settings);
	dsi_wreg(io, DSI_DIRECT_CMD_STS_CLR,
		DSI_DIRECT_CMD_STS_CLR_TE_RECEIVED_CLR(true));
	dsi_wfld(io, DSI_DIRECT_CMD_STS_CTL, TE_RECEIVED_EN, true);
	dsi_wreg(io, DSI_DIRECT_CMD_STS_CLR,
		DSI_DIRECT_CMD_STS_CLR_ACKNOWLEDGE_WITH_ERR_RECEIVED_CLR(true));
	dsi_wfld(io, DSI_DIRECT_CMD_STS_CTL, ACKNOWLEDGE_WITH_ERR_EN, true);
	dsi_wreg(io, DSI_CMD_MODE_STS_CLR,
		DSI_CMD_MODE_STS_CLR_ERR_NO_TE_CLR(true));
	dsi_wfld(io, DSI_CMD_MODE_STS_CTL, ERR_NO_TE_EN, true);
	dsi_wreg(io, DSI_CMD_MODE_STS_CLR,
		DSI_CMD_MODE_STS_CLR_ERR_TE_MISS_CLR(true));
	dsi_wfld(io, DSI_CMD_MODE_STS_CTL, ERR_TE_MISS_EN, true);
	dsi_wreg(io, DSI_DIRECT_CMD_SEND, true);
}

static int read(u8 *io, struct device *dev, u8 cmd, u32 *data, int *len)
{
	int ret = 0;
	u32 settings;
	bool error = false, ok = false;
	bool ack_with_err = false;
	u8 nbr_of_retries = DSI_READ_NBR_OF_RETRIES;

	if (Ispba_status == true) {
		dev_info(dev,"mipi read is pba test! \n");
		return ret;
	}
	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, BTA_EN, true);
	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, READ_EN, true);
	settings = DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_NAT_ENUM(READ) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LONGNOTSHORT(false) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_ID(0) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_SIZE(1) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LP_EN(true) |
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_ENUM(DCS_READ);
	dsi_wreg(io, DSI_DIRECT_CMD_MAIN_SETTINGS, settings);

	dsi_wreg(io, DSI_DIRECT_CMD_WRDAT0, cmd);

	do {
		u8 wait  = DSI_READ_TIMEOUT_MS;
		u32 inc_time_us = DSI_READ_DELAY_US;
		dsi_wreg(io, DSI_DIRECT_CMD_STS_CLR, ~0);
		dsi_wreg(io, DSI_DIRECT_CMD_RD_STS_CLR, ~0);
		dsi_wreg(io, DSI_DIRECT_CMD_SEND, true);

		while (wait-- && !(error = dsi_rfld(io, DSI_DIRECT_CMD_STS,
					READ_COMPLETED_WITH_ERR)) &&
				!(ok = dsi_rfld(io, DSI_DIRECT_CMD_STS,
							READ_COMPLETED)))
			usleep_range(DSI_READ_DELAY_US, inc_time_us+=DSI_READ_TIMEOUT_MS);

		ack_with_err = dsi_rfld(io, DSI_DIRECT_CMD_STS,
						ACKNOWLEDGE_WITH_ERR_RECEIVED);
		if (ack_with_err)
			dev_dbg(dev,
					"DCS Acknowledge Error Report %.4X\n",
				dsi_rfld(io, DSI_DIRECT_CMD_STS, ACK_VAL));
	} while (--nbr_of_retries && ack_with_err);

	if (ok) {
		int rdsize;
		u32 rddat;

		rdsize = dsi_rfld(io, DSI_DIRECT_CMD_RD_PROPERTY, RD_SIZE);
		rddat = dsi_rreg(io, DSI_DIRECT_CMD_RDDAT);
		if (rdsize < *len) {
			ret = -EINVAL;
		} else {
			*len = min(*len, rdsize);
			memcpy(data, &rddat, *len);
		}
	} else {
		u8 dat1_status;
		u32 sts, vid_sts;

		sts = dsi_rreg(io, DSI_DIRECT_CMD_STS);
		vid_sts = dsi_rreg(io, DSI_VID_MODE_STS);
		dat1_status = dsi_rfld(io, DSI_MCTL_LANE_STS, DATLANE1_STATE);
		dev_err(dev, "DCS read failed, err=%d, D0 state %d sts %X vid_sts %X\n",
						error, dat1_status, sts, vid_sts);
		dsi_wreg(io, DSI_DIRECT_CMD_RD_INIT, true);
		/* If dat1 is still in read to a force stop */
		if (dat1_status == DSILINK_LANE_STATE_READ ||
						sts == DSI_CMD_TRANSMISSION)
			ret = -EAGAIN;
		else
			ret = -EIO;
	}

	dsi_wreg(io, DSI_CMD_MODE_STS_CLR, ~0);
	dsi_wreg(io, DSI_DIRECT_CMD_STS_CLR, ~0);

	return ret;
}

static void force_stop(u8 *io)
{
	dsi_wfld(io, DSI_MCTL_MAIN_PHY_CTL, FORCE_STOP_MODE, true);
	dsi_wfld(io, DSI_MCTL_MAIN_PHY_CTL, CLOCK_FORCE_STOP_MODE, true);
	udelay(20);
	dsi_wfld(io, DSI_MCTL_MAIN_PHY_CTL, FORCE_STOP_MODE, false);
	dsi_wfld(io, DSI_MCTL_MAIN_PHY_CTL, CLOCK_FORCE_STOP_MODE, false);
}

static int enable(u8 *io, struct device *dev, const struct dsilink_port *port,
				struct dsilink_dsi_vid_registers *vid_regs)
{
	int i = 0;

	if (port->phy.check_pba)
		Ispba_status = true;

	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, LINK_EN, true);
	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, BTA_EN, true);
	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, READ_EN, true);
	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, REG_TE_EN, true);

	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, HOST_EOT_GEN,
						port->phy.host_eot_gen);
	dsi_wfld(io, DSI_CMD_MODE_CTL, TE_TIMEOUT, 0x3FF);

	if (port->sync_src == DSILINK_SYNCSRC_TE_POLLING) {
		dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, IF1_TE_EN, true);
		dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, TE_POLLING_EN, true);
	}

	dsi_wreg(io, DSI_MCTL_DPHY_STATIC,
		DSI_MCTL_DPHY_STATIC_UI_X4(port->phy.ui));

	dsi_wreg(io, DSI_MCTL_MAIN_PHY_CTL,
		DSI_MCTL_MAIN_PHY_CTL_WAIT_BURST_TIME(0xf) |
		DSI_MCTL_MAIN_PHY_CTL_LANE2_EN(
			port->phy.num_data_lanes >= 2) |
		DSI_MCTL_MAIN_PHY_CTL_CLK_ULPM_EN(true) |
		DSI_MCTL_MAIN_PHY_CTL_DAT1_ULPM_EN(true) |
		DSI_MCTL_MAIN_PHY_CTL_DAT2_ULPM_EN(true) |
		DSI_MCTL_MAIN_PHY_CTL_CLK_CONTINUOUS(
			false));
	dsi_wreg(io, DSI_MCTL_ULPOUT_TIME,
		DSI_MCTL_ULPOUT_TIME_CKLANE_ULPOUT_TIME(1) |
		DSI_MCTL_ULPOUT_TIME_DATA_ULPOUT_TIME(1));
	dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, DLX_REMAP_EN,
				port->phy.data_lanes_swap);
	dsi_wreg(io, DSI_DPHY_LANES_TRIM,
		DSI_DPHY_LANES_TRIM_DPHY_SPECS_90_81B_ENUM(0_90));
	dsi_wreg(io, DSI_MCTL_DPHY_TIMEOUT,
		DSI_MCTL_DPHY_TIMEOUT_CLK_DIV(0xf) |
		DSI_MCTL_DPHY_TIMEOUT_HSTX_TO_VAL(0x3fff) |
		DSI_MCTL_DPHY_TIMEOUT_LPRX_TO_VAL(0x3fff));
	dsi_wfld(io, DSI_CMD_MODE_CTL, FIL_VALUE, 0x00);
	dsi_wfld(io, DSI_CMD_MODE_CTL, ARB_MODE, false);
	dsi_wfld(io, DSI_CMD_MODE_CTL, ARB_PRI, false);
	dsi_wreg(io, DSI_MCTL_MAIN_EN,
		DSI_MCTL_MAIN_EN_PLL_START(true) |
		DSI_MCTL_MAIN_EN_CKLANE_EN(true) |
		DSI_MCTL_MAIN_EN_DAT1_EN(true) |
		DSI_MCTL_MAIN_EN_DAT2_EN(port->phy.num_data_lanes == 2) |
		DSI_MCTL_MAIN_EN_IF1_EN(true) |
		DSI_MCTL_MAIN_EN_IF2_EN(false));

	while (dsi_rfld(io, DSI_MCTL_MAIN_STS, CLKLANE_READY) == 0 ||
		dsi_rfld(io, DSI_MCTL_MAIN_STS, DAT1_READY) ==
						DSILINK_LANE_STATE_START ||
		(dsi_rfld(io, DSI_MCTL_MAIN_STS, DAT2_READY) ==
					DSILINK_LANE_STATE_START &&
						port->phy.num_data_lanes > 1)) {
		usleep_range(1000, 1000);
		if (i++ == 100) {
			dev_warn(dev, "DSI lane not ready!\n");
			break;
		}
	}

	if (port->mode == DSILINK_MODE_VID) {
		/* burst mode or non-burst mode */
		dsi_wfld(io, DSI_VID_MAIN_CTL, BURST_MODE,
							vid_regs->burst_mode);

		/* sync is pulse or event */
		dsi_wfld(io, DSI_VID_MAIN_CTL, SYNC_PULSE_ACTIVE,
						vid_regs->sync_is_pulse);
		dsi_wfld(io, DSI_VID_MAIN_CTL, SYNC_PULSE_HORIZONTAL,
						vid_regs->sync_is_pulse);

		/* disable video stream when using TVG */
		if (vid_regs->tvg_enable) {
			dsi_wfld(io, DSI_MCTL_MAIN_EN, IF1_EN, false);
			dsi_wfld(io, DSI_MCTL_MAIN_EN, IF2_EN, false);
		}

		/*
		 * behavior during blanking time
		 * 00: NULL packet 1x:LP 01:blanking-packet
		 */
#if defined(CONFIG_MACH_SEC_KYLE) || defined(CONFIG_MACH_SEC_SKOMER)
		dsi_wfld(io, DSI_VID_MAIN_CTL, REG_BLKLINE_MODE, 2);
#else
		dsi_wfld(io, DSI_VID_MAIN_CTL, REG_BLKLINE_MODE, 1);
#endif

		/*
		 * behavior during eol
		 * 00: NULL packet 1x:LP 01:blanking-packet
		 */
		dsi_wfld(io, DSI_VID_MAIN_CTL, REG_BLKEOL_MODE, 2);

		/* time to perform LP->HS on D-PHY */
		dsi_wfld(io, DSI_VID_DPHY_TIME, REG_WAKEUP_TIME,
						port->phy.vid_wakeup_time);

		/*
		 * video stream starts on VSYNC packet
		 * and stops at the end of a frame
		 */
		dsi_wfld(io, DSI_VID_MAIN_CTL, VID_ID, 0);
		dsi_wfld(io, DSI_VID_MAIN_CTL, START_MODE, 0);
		dsi_wfld(io, DSI_VID_MAIN_CTL, STOP_MODE, 0);

		/* 1: if1 in video mode, 0: if1 in command mode */
		dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, IF1_MODE, 1);
		dsi_wfld(io, DSI_CMD_MODE_CTL, IF1_LP_EN, false);

		/* enable error interrupts */
		dsi_wfld(io, DSI_VID_MODE_STS_CTL, ERR_MISSING_VSYNC_EN, true);
		dsi_wfld(io, DSI_VID_MODE_STS_CTL, ERR_MISSING_DATA_EN, true);
	} else
		dsi_wfld(io, DSI_CMD_MODE_CTL, IF1_ID, 0);

	return 0;
}

static void disable(u8 *io)
{
	dsi_wreg(io, DSI_VID_MODE_STS_CTL, 0);
}

static int update_frame_parameters(u8 *io, struct dsilink_video_mode *vmode,
				struct dsilink_dsi_vid_registers *vid_regs,
									u8 bpp)
{
	dsi_wfld(io, DSI_VID_VSIZE, VFP_LENGTH, vmode->vfp);
	dsi_wfld(io, DSI_VID_VSIZE, VBP_LENGTH, vmode->vbp);
	dsi_wfld(io, DSI_VID_VSIZE, VSA_LENGTH, vmode->vsw);
	dsi_wfld(io, DSI_VID_HSIZE1, HFP_LENGTH, vid_regs->hfp);
	dsi_wfld(io, DSI_VID_HSIZE1, HBP_LENGTH, vid_regs->hbp);
	dsi_wfld(io, DSI_VID_HSIZE1, HSA_LENGTH, vid_regs->hsa);
	dsi_wfld(io, DSI_VID_VSIZE, VACT_LENGTH, vmode->yres);
	dsi_wfld(io, DSI_VID_HSIZE2, RGB_SIZE, vmode->xres * bpp);
	dsi_wfld(io, DSI_VID_MAIN_CTL, VID_PIXEL_MODE, vid_regs->pixel_mode);
	dsi_wfld(io, DSI_VID_MAIN_CTL, HEADER, vid_regs->rgb_header);
	dsi_wfld(io, DSI_VID_MAIN_CTL, RECOVERY_MODE, 1);
	dsi_wfld(io, DSI_TVG_IMG_SIZE, TVG_NBLINE, vmode->yres);
	dsi_wfld(io, DSI_TVG_IMG_SIZE, TVG_LINE_SIZE, vmode->xres * bpp);

	if (vid_regs->tvg_enable) {
		/*
		 * with these settings, expect to see 64 pixels wide
		 * red and green vertical stripes on the screen when
		 * tvg_enable = 1
		 */
		dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, TVG_SEL, 1);

		dsi_wfld(io, DSI_TVG_CTL, TVG_STRIPE_SIZE, 6);
		dsi_wfld(io, DSI_TVG_CTL, TVG_MODE, 2);
		dsi_wfld(io, DSI_TVG_CTL, TVG_STOPMODE, 2);
		dsi_wfld(io, DSI_TVG_CTL, TVG_RUN, 1);

		dsi_wfld(io, DSI_TVG_COLOR1, COL1_BLUE, 0);
		dsi_wfld(io, DSI_TVG_COLOR1, COL1_GREEN, 0);
		dsi_wfld(io, DSI_TVG_COLOR1, COL1_RED, 0xFF);

		dsi_wfld(io, DSI_TVG_COLOR2, COL2_BLUE, 0);
		dsi_wfld(io, DSI_TVG_COLOR2, COL2_GREEN, 0xFF);
		dsi_wfld(io, DSI_TVG_COLOR2, COL2_RED, 0);
	}
	if (vid_regs->sync_is_pulse)
		dsi_wfld(io, DSI_VID_BLKSIZE2, BLKLINE_PULSE_PCK,
							vid_regs->blkline_pck);
	else
		dsi_wfld(io, DSI_VID_BLKSIZE1, BLKLINE_EVENT_PCK,
							vid_regs->blkline_pck);
	dsi_wfld(io, DSI_VID_DPHY_TIME, REG_LINE_DURATION,
						vid_regs->line_duration);
	if (vid_regs->burst_mode) {
		dsi_wfld(io, DSI_VID_BLKSIZE1, BLKEOL_PCK,
							vid_regs->blkeol_pck);
		dsi_wfld(io, DSI_VID_PCK_TIME, BLKEOL_DURATION,
						vid_regs->blkeol_duration);
		dsi_wfld(io, DSI_VID_VCA_SETTING1, MAX_BURST_LIMIT,
						vid_regs->blkeol_pck - 6);
		dsi_wfld(io, DSI_VID_VCA_SETTING2, EXACT_BURST_LIMIT,
							vid_regs->blkeol_pck);
	}
	dsi_wfld(io, DSI_VID_VCA_SETTING2, MAX_LINE_LIMIT,
						vid_regs->blkline_pck - 6);

	return 0;
}

static void set_clk_continous(u8 *io, bool value)
{
	dsi_wfld(io, DSI_MCTL_MAIN_PHY_CTL, CLK_CONTINUOUS, value);
}

static void enable_video_mode(u8 *io, bool enable)
{
	if (!enable) {
		dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, VID_EN, false);

		/* Wait for DSI VSG to stop */
		int i = 2000; /* 2000 * 10us = 20 ms > 1 frame */
		while (--i && dsi_rfld(io, DSI_VID_MODE_STS, VSG_RUNNING))
			udelay(10);
		if (!i)
			pr_warning("Stop DSI video mode failed!\n");
	} else {
		dsi_wfld(io, DSI_MCTL_MAIN_DATA_CTL, VID_EN, true);
	}
}

static int handle_ulpm(u8 *io, struct device *dev,
			const struct dsilink_port *port, bool enter_ulpm)
{
	u8 num_data_lanes = port->phy.num_data_lanes;
	u8 nbr_of_retries = 0;
	u8 lane_state;
	int ret = 0;

	/*
	 * The D-PHY protocol specifies the time to leave the ULP mode
	 * in ms. It will at least take 1 ms to exit ULPM.
	 * The ULPOUT time value is using number of system clock ticks
	 * divided by 1000. The system clock for the DSI link is the dsi sys
	 * clock.
	 */
	dsi_wreg(io, DSI_MCTL_ULPOUT_TIME,
			DSI_MCTL_ULPOUT_TIME_CKLANE_ULPOUT_TIME(0x1FF) |
			DSI_MCTL_ULPOUT_TIME_DATA_ULPOUT_TIME(0x1FF));

	if (enter_ulpm) {
		lane_state = DSILINK_LANE_STATE_ULPM;
		switch_byteclk_to_sys_clk(port->link);
		dsi_wfld(io, DSI_MCTL_PLL_CTL, PLL_OUT_SEL, true);
	}

	dsi_wfld(io, DSI_MCTL_MAIN_EN, DAT1_ULPM_REQ, enter_ulpm);
	dsi_wfld(io, DSI_MCTL_MAIN_EN, DAT2_ULPM_REQ,
					enter_ulpm && num_data_lanes == 2);
	dsi_wfld(io, DSI_MCTL_MAIN_EN, CLKLANE_ULPM_REQ, enter_ulpm);

	if (!enter_ulpm) {
		lane_state = DSILINK_LANE_STATE_IDLE;
		switch_byteclk_to_hs_clk(port->link);
		dsi_wfld(io, DSI_MCTL_PLL_CTL, PLL_OUT_SEL, false);
	}

	/* Wait for data lanes to enter ULPM */
	while (dsi_rfld(io, DSI_MCTL_LANE_STS, DATLANE1_STATE)
						!= lane_state ||
		(dsi_rfld(io, DSI_MCTL_LANE_STS, DATLANE2_STATE)
						!= lane_state &&
							num_data_lanes > 1)) {
		mdelay(DSI_WAIT_FOR_ULPM_STATE_MS);
		if (nbr_of_retries++ == DSI_ULPM_STATE_NBR_OF_RETRIES) {
			dev_warn(dev,
				"Could not enter correct state=%d!\n",
								lane_state);
			ret = -EFAULT;
			break;
		}
	}

	nbr_of_retries = 0;

	/* Wait for clock lane to enter ULPM */
	while (dsi_rfld(io, DSI_MCTL_LANE_STS, CLKLANE_STATE)
						!= lane_state) {
		mdelay(DSI_WAIT_FOR_ULPM_STATE_MS);
		if (nbr_of_retries++ == DSI_ULPM_STATE_NBR_OF_RETRIES) {
			dev_warn(dev,
				"Could not enter correct state=%d!\n",
								lane_state);
			ret = -EFAULT;
			break;
		}
	}

	return ret;
}

void __devinit nova_dsilink_v2_init(struct dsilink_ops *ops)
{
	ops->force_stop = force_stop;
	ops->read = read;
	ops->write = write;
	ops->handle_irq = handle_irq;
	ops->wait_while_running = wait_while_running;
	ops->te_request = te_request;
	ops->enable = enable;
	ops->disable = disable;
	ops->update_frame_parameters = update_frame_parameters;
	ops->set_clk_continous = set_clk_continous;
	ops->enable_video_mode = enable_video_mode;
	ops->handle_ulpm = handle_ulpm;
}

