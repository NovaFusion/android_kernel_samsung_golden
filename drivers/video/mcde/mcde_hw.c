/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE base driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/atomic.h>

#include <linux/mfd/dbx500-prcmu.h>

#include <video/mcde.h>
#include <video/nova_dsilink.h>

#include "mcde_regs.h"
#include "mcde_struct.h"
#include "mcde_hw.h"

#include "mcde_debugfs.h"
#define CREATE_TRACE_POINTS
#include "mcde_trace.h"

static int set_channel_state_atomic(struct mcde_chnl_state *chnl,
							enum chnl_state state);
static int set_channel_state_sync(struct mcde_chnl_state *chnl,
							enum chnl_state state);
static void stop_channel(struct mcde_chnl_state *chnl);
static int _mcde_chnl_enable(struct mcde_chnl_state *chnl);
static int _mcde_chnl_apply(struct mcde_chnl_state *chnl);
static void disable_flow(struct mcde_chnl_state *chnl, bool setstate);
static void enable_flow(struct mcde_chnl_state *chnl, bool setstate);
static void do_softwaretrig(struct mcde_chnl_state *chnl);
static int wait_for_vcmp(struct mcde_chnl_state *chnl);
static int probe_hw(struct platform_device *pdev);
static void wait_for_flow_disabled(struct mcde_chnl_state *chnl);
static int enable_mcde_hw(void);
//static int enable_mcde_hw_pre(void);
static int update_channel_static_registers(struct mcde_chnl_state *chnl);
static void _mcde_chnl_update_color_conversion(struct mcde_chnl_state *chnl);
static void chnl_update_overlay(struct mcde_chnl_state *chnl,
						struct mcde_ovly_state *ovly);
#define OVLY_TIMEOUT 100
#define CHNL_TIMEOUT 100
#define FLOW_STOP_TIMEOUT 20
#define SCREEN_PPL_HIGH 1280
#define SCREEN_PPL_CEA2 720
#define SCREEN_LPF_CEA2 480
#define DSI_DELAY0_CEA2_ADD 10

#define MCDE_SLEEP_WATCHDOG 500
#define MCDE_FLOWEN_MAX_TRIAL 60

#define MCDE_VERSION_4_1_3 0x04010300
#define MCDE_VERSION_4_0_4 0x04000400
#define MCDE_VERSION_3_0_8 0x03000800
#define MCDE_VERSION_3_0_5 0x03000500
#define MCDE_VERSION_1_0_4 0x01000400

#define CLK_MCDE	"mcde"
#define CLK_DPI		"lcd"

#define FIFO_EMPTY_POLL_TIMEOUT 500

u8 *mcdeio;
u8 num_channels;
u8 num_overlays;
int mcde_irq;
u32 input_fifo_size;
u32 output_fifo_ab_size;
u32 output_fifo_c0c1_size;
u8 mcde_is_enabled;
u8 dsi_pll_is_enabled;
u8 dsi_ifc_is_supported;
u8 dsi_use_clk_framework;
u32 mcde_clk_rate; /* In Hz */

u8 hw_alignment;
u8 mcde_dynamic_power_management = true;
static struct platform_device *mcde_dev;
static struct regulator *regulator_vana;
static struct regulator *regulator_mcde_epod;
static struct regulator *regulator_esram_epod;
static struct clk *clock_mcde;
static struct delayed_work hw_timeout_work;
static struct mutex mcde_hw_lock;
static inline void mcde_lock(const char *func, int line)
{
	mutex_lock(&mcde_hw_lock);
	dev_vdbg(&mcde_dev->dev, "Enter MCDE: %s:%d\n", func, line);
}

static inline void mcde_unlock(const char *func, int line)
{
	dev_vdbg(&mcde_dev->dev, "Exit MCDE: %s:%d\n", func, line);
	mutex_unlock(&mcde_hw_lock);
}

static inline bool mcde_trylock(const char *func, int line)
{
	bool locked = mutex_trylock(&mcde_hw_lock) == 1;
	if (locked)
		dev_vdbg(&mcde_dev->dev, "Enter MCDE: %s:%d\n", func, line);
	return locked;
}

u32 mcde_rreg(u32 reg)
{
	return readl(mcdeio + reg);
}
void mcde_wreg(u32 reg, u32 val)
{
	writel(val, mcdeio + reg);
}


#define mcde_rfld(__reg, __fld) \
({ \
	const u32 mask = __reg##_##__fld##_MASK; \
	const u32 shift = __reg##_##__fld##_SHIFT; \
	((mcde_rreg(__reg) & mask) >> shift); \
})

#define mcde_wfld(__reg, __fld, __val) \
({ \
	const u32 mask = __reg##_##__fld##_MASK; \
	const u32 shift = __reg##_##__fld##_SHIFT; \
	const u32 oldval = mcde_rreg(__reg); \
	const u32 newval = ((__val) << shift); \
	mcde_wreg(__reg, (oldval & ~mask) | (newval & mask)); \
})

static struct mcde_ovly_state *overlays;
static struct mcde_chnl_state *channels;
static inline u32 get_ovly_bw(struct mcde_ovly_state *ovly)
{
	if (!ovly || !ovly->regs.enabled)
		return 0;

	//TODO: Use pixclock for fps
	return ovly->regs.ppl * ovly->regs.lpf * 60;
}

static void update_opp_requirements(void)
{
	int i;
	u32 rot = 0;
	u32 bw = 0;
	u32 ovly = 0;
	struct mcde_opp_requirements reqs;
	struct mcde_platform_data *pdata = mcde_dev->dev.platform_data;

	for (i=0; i<num_channels; i++) {
		u32 tmp;
		struct mcde_chnl_state *chnl = &channels[i];
		if (chnl->regs.roten)
			rot++;
		tmp = get_ovly_bw(chnl->ovly0);
		if (tmp) {
			ovly++;
			bw += tmp;
		}
		tmp = get_ovly_bw(chnl->ovly1);
		if (tmp) {
			ovly++;
			bw += tmp;
		}
	}

	reqs.num_rot_channels = rot;
	reqs.num_overlays = ovly;
	reqs.total_bw = bw;

	if (pdata->update_opp)
		pdata->update_opp(&mcde_dev->dev, &reqs);
}

static void get_dsi_port(struct mcde_chnl_state *chnl, struct dsilink_port *port)
{
	port->link = chnl->port.link;
	port->phy = chnl->port.phy.dsi;
	if (chnl->port.mode == MCDE_PORTMODE_CMD)
		port->mode = DSILINK_MODE_CMD;
	else
		port->mode = DSILINK_MODE_VID;
	if (chnl->port.sync_src == MCDE_SYNCSRC_TE_POLLING)
		port->sync_src = DSILINK_SYNCSRC_TE_POLLING;
	else
		port->sync_src = DSILINK_SYNCSRC_BTA;
}

static void enable_clocks_and_power(struct platform_device *pdev)
{
	struct mcde_platform_data *pdata = pdev->dev.platform_data;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	/* VANA should be enabled before a DSS hard reset */
	if (regulator_vana)
		WARN_ON_ONCE(regulator_enable(regulator_vana));

	WARN_ON_ONCE(regulator_enable(regulator_mcde_epod));

	if (!dsi_use_clk_framework)
		pdata->platform_set_clocks();

	WARN_ON_ONCE(clk_enable(clock_mcde));
}

static void disable_clocks_and_power(struct platform_device *pdev)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	clk_disable(clock_mcde);

	WARN_ON_ONCE(regulator_disable(regulator_mcde_epod));

	if (regulator_vana)
		WARN_ON_ONCE(regulator_disable(regulator_vana));
}

static void update_mcde_registers(void)
{
	struct mcde_platform_data *pdata = mcde_dev->dev.platform_data;

	/* Setup output muxing */
	mcde_wreg(MCDE_CONF0,
		MCDE_CONF0_IFIFOCTRLWTRMRKLVL(7) |
		MCDE_CONF0_OUTMUX0(pdata->outmux[0]) |
		MCDE_CONF0_OUTMUX1(pdata->outmux[1]) |
		MCDE_CONF0_OUTMUX2(pdata->outmux[2]) |
		MCDE_CONF0_OUTMUX3(pdata->outmux[3]) |
		MCDE_CONF0_OUTMUX4(pdata->outmux[4]) |
		pdata->syncmux);

	mcde_wfld(MCDE_RISPP, VCMPARIS, 1);
	mcde_wfld(MCDE_RISPP, VCMPBRIS, 1);
	mcde_wfld(MCDE_RISPP, VCMPC0RIS, 1);
	mcde_wfld(MCDE_RISPP, VCMPC1RIS, 1);

	/*
	 * Enable VCMP interrupts for all channels
	 * and VSYNC0 and VSYNC1 capture interrupts
	 */
	mcde_wreg(MCDE_IMSCPP,
		MCDE_IMSCPP_VCMPAIM(true) |
		MCDE_IMSCPP_VCMPBIM(true) |
		MCDE_IMSCPP_VSCC0IM(true) |
		MCDE_IMSCPP_VSCC1IM(true) |
		MCDE_IMSCPP_VCMPC0IM(true) |
		MCDE_IMSCPP_VCMPC1IM(true));

	mcde_wreg(MCDE_IMSCCHNL, MCDE_IMSCCHNL_CHNLAIM(0xf));
	mcde_wreg(MCDE_IMSCERR, 0xFFFF01FF);
}

static void disable_dsi_pll(void)
{
	if (dsi_pll_is_enabled && (--dsi_pll_is_enabled == 0)) {
		struct mcde_platform_data *pdata =
				mcde_dev->dev.platform_data;
		dev_dbg(&mcde_dev->dev, "%s disable dsipll\n", __func__);
		pdata->platform_disable_dsipll();
	}
}

static void enable_dsi_pll(void)
{
	if (!dsi_pll_is_enabled) {
		int ret;
		struct mcde_platform_data *pdata =
				mcde_dev->dev.platform_data;
		ret = pdata->platform_enable_dsipll();
		if (ret < 0) {
			dev_warn(&mcde_dev->dev, "%s: "
				"enable_dsipll failed ret = %d\n",
							__func__, ret);
		}
		dev_dbg(&mcde_dev->dev, "%s enable dsipll\n",
							__func__);
	}
	dsi_pll_is_enabled++;
}

static void disable_mcde_hw(bool force_disable, bool suspend)
{
	int i;
	bool mcde_up = false;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (!mcde_is_enabled)
		return;

	for (i = 0; i < num_channels; i++) {
		struct mcde_chnl_state *chnl = &channels[i];
		if (force_disable || (chnl->enabled &&
					chnl->state != CHNLSTATE_RUNNING)) {
			stop_channel(chnl);
			if (chnl->formatter_updated) {
				if (chnl->port.type == MCDE_PORTTYPE_DSI) {
					nova_dsilink_wait_while_running(
								chnl->dsilink);
					nova_dsilink_set_clk_continous(
							chnl->dsilink, false);
					nova_dsilink_enter_ulpm(chnl->dsilink);
					nova_dsilink_disable(chnl->dsilink);
					if (!dsi_use_clk_framework)
						disable_dsi_pll();
				} else if (chnl->port.type
							== MCDE_PORTTYPE_DPI) {
					clk_disable(chnl->clk_dpi);
				}
				chnl->formatter_updated = false;
			}
			if (chnl->esram_is_enabled) {
				WARN_ON_ONCE(regulator_disable(
							regulator_esram_epod));
				chnl->esram_is_enabled = false;
			}
		} else if (chnl->enabled && chnl->state == CHNLSTATE_RUNNING) {
			mcde_up = true;
		}
	}

	if (mcde_up)
		return;

	free_irq(mcde_irq, &mcde_dev->dev);

	disable_clocks_and_power(mcde_dev);

	mcde_is_enabled = false;

	mcde_debugfs_hw_disabled();
}

static void dpi_video_mode_apply(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);
	chnl->tv_regs.interlaced_en = chnl->vmode.interlaced;

	chnl->tv_regs.sel_mode_tv = chnl->port.phy.dpi.tv_mode;
	if (chnl->tv_regs.sel_mode_tv) {
		/* TV mode */
		u32 bel;
		/* -4 since hsw is excluding SAV/EAV, 2 bytes each */
		chnl->tv_regs.hsw  = chnl->vmode.hbp + chnl->vmode.hfp - 4;
		/* vbp_field2 = vbp_field1 + 1 */
		chnl->tv_regs.fsl1 = chnl->vmode.vbp / 2;
		chnl->tv_regs.fsl2 = chnl->vmode.vbp - chnl->tv_regs.fsl1;
		/* +1 since vbp_field2 = vbp_field1 + 1 */
		bel = chnl->vmode.vbp + chnl->vmode.vfp;
		/* in TV mode: bel2 = bel1 + 1 */
		chnl->tv_regs.bel1 = bel / 2;
		chnl->tv_regs.bel2 = bel - chnl->tv_regs.bel1;
		if (chnl->port.phy.dpi.bus_width == 4)
			chnl->tv_regs.tv_mode = MCDE_TVCRA_TVMODE_SDTV_656P_BE;
		else
			chnl->tv_regs.tv_mode = MCDE_TVCRA_TVMODE_SDTV_656P;
		chnl->tv_regs.inv_clk = true;
	} else {
		/* LCD mode */
		u32 polarity;
		chnl->tv_regs.hsw  = chnl->vmode.hsw;
		chnl->tv_regs.dho  = chnl->vmode.hbp;
		chnl->tv_regs.alw  = chnl->vmode.hfp;
		chnl->tv_regs.bel1 = chnl->vmode.vsw;
		chnl->tv_regs.bel2 = chnl->tv_regs.bel1;
		chnl->tv_regs.dvo  = chnl->vmode.vbp;
		chnl->tv_regs.bsl  = chnl->vmode.vfp;
		chnl->tv_regs.fsl1 = 0;
		chnl->tv_regs.fsl2 = 0;
		polarity = chnl->port.phy.dpi.polarity;
		chnl->tv_regs.lcdtim1 = MCDE_LCDTIM1A_IHS(
				(polarity & DPI_ACT_LOW_HSYNC) != 0);
		chnl->tv_regs.lcdtim1 |= MCDE_LCDTIM1A_IVS(
				(polarity & DPI_ACT_LOW_VSYNC) != 0);
		chnl->tv_regs.lcdtim1 |= MCDE_LCDTIM1A_IOE(
				(polarity & DPI_ACT_LOW_DATA_ENABLE) != 0);
		chnl->tv_regs.lcdtim1 |= MCDE_LCDTIM1A_IPC(
				(polarity & DPI_ACT_ON_FALLING_EDGE) != 0);
	}
	chnl->tv_regs.dirty = true;
}

static void update_dpi_registers(enum mcde_chnl chnl_id, struct tv_regs *regs)
{
	u8 idx = chnl_id;

	dev_dbg(&mcde_dev->dev, "%s\n", __func__);
	mcde_wreg(MCDE_TVCRA + idx * MCDE_TVCRA_GROUPOFFSET,
			MCDE_TVCRA_SEL_MOD(regs->sel_mode_tv)             |
			MCDE_TVCRA_INTEREN(regs->interlaced_en)           |
			MCDE_TVCRA_IFIELD(0)                              |
			MCDE_TVCRA_TVMODE(regs->tv_mode)                  |
			MCDE_TVCRA_SDTVMODE(MCDE_TVCRA_SDTVMODE_Y0CBY1CR) |
			MCDE_TVCRA_CKINV(regs->inv_clk)                   |
			MCDE_TVCRA_AVRGEN(0));
	mcde_wreg(MCDE_TVBLUA + idx * MCDE_TVBLUA_GROUPOFFSET,
		MCDE_TVBLUA_TVBLU(MCDE_CONFIG_TVOUT_BACKGROUND_LUMINANCE) |
		MCDE_TVBLUA_TVBCB(MCDE_CONFIG_TVOUT_BACKGROUND_CHROMINANCE_CB)|
		MCDE_TVBLUA_TVBCR(MCDE_CONFIG_TVOUT_BACKGROUND_CHROMINANCE_CR));

	/* Vertical timing registers */
	mcde_wreg(MCDE_TVDVOA + idx * MCDE_TVDVOA_GROUPOFFSET,
					MCDE_TVDVOA_DVO1(regs->dvo) |
					MCDE_TVDVOA_DVO2(regs->dvo));
	mcde_wreg(MCDE_TVBL1A + idx * MCDE_TVBL1A_GROUPOFFSET,
					MCDE_TVBL1A_BEL1(regs->bel1) |
					MCDE_TVBL1A_BSL1(regs->bsl));
	mcde_wreg(MCDE_TVBL2A + idx * MCDE_TVBL1A_GROUPOFFSET,
					MCDE_TVBL2A_BEL2(regs->bel2) |
					MCDE_TVBL2A_BSL2(regs->bsl));
	mcde_wreg(MCDE_TVISLA + idx * MCDE_TVISLA_GROUPOFFSET,
					MCDE_TVISLA_FSL1(regs->fsl1) |
					MCDE_TVISLA_FSL2(regs->fsl2));

	/* Horizontal timing registers */
	mcde_wreg(MCDE_TVLBALWA + idx * MCDE_TVLBALWA_GROUPOFFSET,
				MCDE_TVLBALWA_LBW(regs->hsw) |
				MCDE_TVLBALWA_ALW(regs->alw));
	mcde_wreg(MCDE_TVTIM1A + idx * MCDE_TVTIM1A_GROUPOFFSET,
				MCDE_TVTIM1A_DHO(regs->dho));
	if (!regs->sel_mode_tv)
		mcde_wreg(MCDE_LCDTIM1A + idx * MCDE_LCDTIM1A_GROUPOFFSET,
								regs->lcdtim1);
	regs->dirty = false;
}

static void update_oled_registers(enum mcde_chnl chnl_id,
							struct oled_regs *regs)
{
	u8 idx = chnl_id;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);
	mcde_wreg(MCDE_OLEDCONV1A + idx * MCDE_OLEDCONV1A_GROUPOFFSET,
				MCDE_OLEDCONV1A_ALPHA_RED(regs->alfa_red) |
				MCDE_OLEDCONV1A_ALPHA_GREEN(regs->alfa_green));
	mcde_wreg(MCDE_OLEDCONV2A + idx * MCDE_OLEDCONV2A_GROUPOFFSET,
				MCDE_OLEDCONV2A_ALPHA_BLUE(regs->alfa_blue) |
				MCDE_OLEDCONV2A_BETA_RED(regs->beta_red));
	mcde_wreg(MCDE_OLEDCONV3A + idx * MCDE_OLEDCONV3A_GROUPOFFSET,
				MCDE_OLEDCONV3A_BETA_GREEN(regs->beta_green) |
				MCDE_OLEDCONV3A_BETA_BLUE(regs->beta_blue));
	mcde_wreg(MCDE_OLEDCONV4A + idx * MCDE_OLEDCONV4A_GROUPOFFSET,
				MCDE_OLEDCONV4A_GAMMA_RED(regs->gamma_red) |
				MCDE_OLEDCONV4A_GAMMA_GREEN(regs->gamma_green));
	mcde_wreg(MCDE_OLEDCONV5A + idx * MCDE_OLEDCONV5A_GROUPOFFSET,
				MCDE_OLEDCONV5A_GAMMA_BLUE(regs->gamma_blue) |
				MCDE_OLEDCONV5A_OFF_RED(regs->off_red));
	mcde_wreg(MCDE_OLEDCONV6A + idx * MCDE_OLEDCONV6A_GROUPOFFSET,
				MCDE_OLEDCONV6A_OFF_GREEN(regs->off_green) |
				MCDE_OLEDCONV6A_OFF_BLUE(regs->off_blue));
	regs->dirty = false;
}


static void update_col_registers(enum mcde_chnl chnl_id, struct col_regs *regs)
{
	u8 idx = chnl_id;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);
	mcde_wreg(MCDE_RGBCONV1A + idx * MCDE_RGBCONV1A_GROUPOFFSET,
				MCDE_RGBCONV1A_YR_RED(regs->y_red) |
				MCDE_RGBCONV1A_YR_GREEN(regs->y_green));
	mcde_wreg(MCDE_RGBCONV2A + idx * MCDE_RGBCONV2A_GROUPOFFSET,
				MCDE_RGBCONV2A_YR_BLUE(regs->y_blue) |
				MCDE_RGBCONV2A_CR_RED(regs->cr_red));
	mcde_wreg(MCDE_RGBCONV3A + idx * MCDE_RGBCONV3A_GROUPOFFSET,
				MCDE_RGBCONV3A_CR_GREEN(regs->cr_green) |
				MCDE_RGBCONV3A_CR_BLUE(regs->cr_blue));
	mcde_wreg(MCDE_RGBCONV4A + idx * MCDE_RGBCONV4A_GROUPOFFSET,
				MCDE_RGBCONV4A_CB_RED(regs->cb_red) |
				MCDE_RGBCONV4A_CB_GREEN(regs->cb_green));
	mcde_wreg(MCDE_RGBCONV5A + idx * MCDE_RGBCONV5A_GROUPOFFSET,
				MCDE_RGBCONV5A_CB_BLUE(regs->cb_blue) |
				MCDE_RGBCONV5A_OFF_RED(regs->off_cr));
	mcde_wreg(MCDE_RGBCONV6A + idx * MCDE_RGBCONV6A_GROUPOFFSET,
				MCDE_RGBCONV6A_OFF_GREEN(regs->off_y) |
				MCDE_RGBCONV6A_OFF_BLUE(regs->off_cb));
	regs->dirty = false;
}

static void mcde_update_ovly_control(u8 idx, struct ovly_regs *regs)
{
	mcde_wreg(MCDE_OVL0CR + idx * MCDE_OVL0CR_GROUPOFFSET,
		MCDE_OVL0CR_OVLEN(regs->enabled) |
		MCDE_OVL0CR_COLCCTRL(regs->col_conv) |
		MCDE_OVL0CR_CKEYGEN(false) |
		MCDE_OVL0CR_ALPHAPMEN(false) |
		MCDE_OVL0CR_OVLF(false) |
		MCDE_OVL0CR_OVLR(false) |
		MCDE_OVL0CR_OVLB(false) |
		MCDE_OVL0CR_FETCH_ROPC(0) |
		MCDE_OVL0CR_STBPRIO(0) |
		MCDE_OVL0CR_BURSTSIZE_ENUM(8W) |
		/* TODO: enum, get from ovly */
		MCDE_OVL0CR_MAXOUTSTANDING_ENUM(8_REQ) |
		/* TODO: _8W, calculate? */
		MCDE_OVL0CR_ROTBURSTSIZE_ENUM(8W));
}

static void mcde_update_non_buffered_color_conversion(struct mcde_chnl_state *chnl)
{
	mcde_wfld(MCDE_CRA0, OLEDEN, chnl->regs.oled_enable);
	chnl->update_color_conversion = false;
}

static void mcde_update_double_buffered_color_conversion(
					struct mcde_chnl_state *chnl)
{
	u8 red;
	u8 green;
	u8 blue;

	/* Change of background */
	if (chnl->regs.background_yuv) {
		red = 0x80;
		green = 0x10;
		blue = 0x80;
	} else {
		red = 0x00;
		green = 0x00;
		blue = 0x00;
	}
	mcde_wreg(MCDE_CHNL0BCKGNDCOL + chnl->id * MCDE_CHNL0BCKGNDCOL_GROUPOFFSET,
		MCDE_CHNL0BCKGNDCOL_B(blue) |
		MCDE_CHNL0BCKGNDCOL_G(green) |
		MCDE_CHNL0BCKGNDCOL_R(red));

	mcde_update_ovly_control(chnl->ovly0->idx, &chnl->ovly0->regs);
	mcde_update_ovly_control(chnl->ovly1->idx, &chnl->ovly1->regs);
}

/* MCDE internal helpers */
static u8 portfmt2dsipacking(enum mcde_port_pix_fmt pix_fmt)
{
	switch (pix_fmt) {
	case MCDE_PORTPIXFMT_DSI_16BPP:
		return MCDE_DSIVID0CONF0_PACKING_RGB565;
	case MCDE_PORTPIXFMT_DSI_18BPP_PACKED:
		return MCDE_DSIVID0CONF0_PACKING_RGB666;
	case MCDE_PORTPIXFMT_DSI_18BPP:
	case MCDE_PORTPIXFMT_DSI_24BPP:
	default:
		return MCDE_DSIVID0CONF0_PACKING_RGB888;
	case MCDE_PORTPIXFMT_DSI_YCBCR422:
		return MCDE_DSIVID0CONF0_PACKING_HDTV;
	}
}

static u8 portfmt2bpp(enum mcde_port_pix_fmt pix_fmt)
{
	/* TODO: Check DPI spec *//* REVIEW: Remove or check */
	switch (pix_fmt) {
	case MCDE_PORTPIXFMT_DPI_16BPP_C1:
	case MCDE_PORTPIXFMT_DPI_16BPP_C2:
	case MCDE_PORTPIXFMT_DPI_16BPP_C3:
	case MCDE_PORTPIXFMT_DSI_16BPP:
	case MCDE_PORTPIXFMT_DSI_YCBCR422:
		return 16;
	case MCDE_PORTPIXFMT_DPI_18BPP_C1:
	case MCDE_PORTPIXFMT_DPI_18BPP_C2:
	case MCDE_PORTPIXFMT_DSI_18BPP_PACKED:
		return 18;
	case MCDE_PORTPIXFMT_DSI_18BPP:
	case MCDE_PORTPIXFMT_DPI_24BPP:
	case MCDE_PORTPIXFMT_DSI_24BPP:
		return 24;
	default:
		return 1;
	}
}

static u8 bpp2outbpp(u8 bpp)
{
	switch (bpp) {
	case 16:
		return MCDE_CRA1_OUTBPP_16BPP;
	case 18:
		return MCDE_CRA1_OUTBPP_18BPP;
	case 24:
		return MCDE_CRA1_OUTBPP_24BPP;
	default:
		return 0;
	}
}

static u8 portfmt2cdwin(enum mcde_port_pix_fmt pix_fmt)
{
	switch (pix_fmt) {
	case MCDE_PORTPIXFMT_DPI_16BPP_C1:
		return MCDE_CRA1_CDWIN_16BPP_C1;
	case MCDE_PORTPIXFMT_DPI_16BPP_C2:
		return MCDE_CRA1_CDWIN_16BPP_C2;
	case MCDE_PORTPIXFMT_DPI_16BPP_C3:
		return MCDE_CRA1_CDWIN_16BPP_C3;
	case MCDE_PORTPIXFMT_DPI_18BPP_C1:
		return MCDE_CRA1_CDWIN_18BPP_C1;
	case MCDE_PORTPIXFMT_DPI_18BPP_C2:
		return MCDE_CRA1_CDWIN_18BPP_C2;
	case MCDE_PORTPIXFMT_DPI_24BPP:
		return MCDE_CRA1_CDWIN_24BPP;
	default:
		/* only DPI formats are relevant */
		return 0;
	}
}

static u32 get_output_fifo_size(enum mcde_fifo fifo)
{
	u32 ret = 1; /* Avoid div by zero */

	switch (fifo) {
	case MCDE_FIFO_A:
	case MCDE_FIFO_B:
		ret = output_fifo_ab_size;
		break;
	case MCDE_FIFO_C0:
	case MCDE_FIFO_C1:
		ret = output_fifo_c0c1_size;
		break;
	default:
		dev_warn(&mcde_dev->dev, "Unsupported fifo");
		break;
	}
	return ret;
}

static inline u8 get_dsi_formatter_id(const struct mcde_port *port)
{
	if (dsi_ifc_is_supported)
		return 2 * port->link;
	else
		return port->link;
}

static inline void mcde_add_bta_te_oneshot_listener(
		struct mcde_chnl_state *chnl)
{
	bool need_statechange = false;
	/*
	 * Request a BTA TE oneshot event.
	 * Can only be done in CHNLSTATE_SETUP or
	 * CHNLSTATE_REQUEST_BTA_TE
	 */
	if (chnl->state != CHNLSTATE_SETUP) {
		need_statechange = true;
		set_channel_state_sync(chnl, CHNLSTATE_REQ_BTA_TE);
	}
	nova_dsilink_te_request(chnl->dsilink);

	if (need_statechange)
		set_channel_state_atomic(chnl, CHNLSTATE_IDLE);
}

static inline void mcde_add_vsync_capture_listener(struct mcde_chnl_state *chnl)
{
	int n_listeners = atomic_inc_return(&chnl->n_vsync_capture_listeners);

	trace_keyvalue((0xF0 | chnl->id), "mcde_add_vsync_capture_listener",
			n_listeners);

	if (n_listeners == 1) {
		switch (chnl->port.sync_src) {
		case MCDE_SYNCSRC_TE0:
			mcde_wfld(MCDE_CRC, SYCEN0, true);
			break;
		case MCDE_SYNCSRC_TE1:
			mcde_wfld(MCDE_CRC, SYCEN1, true);
			break;
		default:
			break;
		}
	}
}

static inline void mcde_remove_vsync_capture_listener(
		struct mcde_chnl_state *chnl)
{
	int n_listeners = atomic_dec_return(&chnl->n_vsync_capture_listeners);

	trace_keyvalue((0xF0 | chnl->id), "mcde_remove_vsync_capture_listener",
			n_listeners);

	if (n_listeners == 0) {
		switch (chnl->port.sync_src) {
		case MCDE_SYNCSRC_TE0:
			mcde_wfld(MCDE_CRC, SYCEN0, false);
			break;
		case MCDE_SYNCSRC_TE1:
			mcde_wfld(MCDE_CRC, SYCEN1, false);
			break;
		default:
			break;
		}
	}
}

static inline void mcde_handle_vsync(struct mcde_chnl_state *chnl)
{
	trace_vsync(chnl->id, chnl->state);
	atomic_inc(&chnl->vsync_cnt);
	chnl->vcmp_cnt_wait = atomic_read(&chnl->vcmp_cnt) + 1;
	if (chnl->port.type == MCDE_PORTTYPE_DSI) {
		if (!chnl->port.update_auto_trig) {
			if (chnl->state == CHNLSTATE_WAIT_TE) {
				set_channel_state_atomic(chnl, CHNLSTATE_RUNNING);
				mcde_remove_vsync_capture_listener(chnl);
				disable_flow(chnl, true);
			}
		} else {
			if (chnl->state == CHNLSTATE_STOPPING) {
				mcde_remove_vsync_capture_listener(chnl);
				disable_flow(chnl, false);
			}
		}
	}
	wake_up_all(&chnl->vsync_waitq);
}

static inline void mcde_handle_vcmp_state_stopping(struct mcde_chnl_state *chnl)
{
	bool change_state = true;
	if (chnl->port.update_auto_trig) {
		switch (chnl->port.sync_src) {
		case MCDE_SYNCSRC_TE0:
			change_state = !mcde_rfld(MCDE_CRC, SYCEN0);
			break;
		case MCDE_SYNCSRC_TE1:
			change_state = !mcde_rfld(MCDE_CRC, SYCEN1);
			break;
		case MCDE_SYNCSRC_OFF:
		case MCDE_SYNCSRC_BTA:
		case MCDE_SYNCSRC_TE_POLLING:
		default:
			break;
		}
	}
	if (change_state)
		set_channel_state_atomic(chnl, CHNLSTATE_STOPPED);
}

static inline void mcde_handle_vcmp(struct mcde_chnl_state *chnl)
{
	trace_vcmp(chnl->id, chnl->state);
	if (!chnl->vcmp_per_field ||
			(chnl->vcmp_per_field && chnl->even_vcmp)) {
		if (chnl->state == CHNLSTATE_STOPPING)
			mcde_handle_vcmp_state_stopping(chnl);

		wake_up_all(&chnl->vcmp_waitq);
	}
	chnl->even_vcmp = !chnl->even_vcmp;

	if (chnl->vcmp_cnt_to_change_col_conv == atomic_read(&chnl->vcmp_cnt))
		mcde_update_non_buffered_color_conversion(chnl);
}

#define NUM_FAST_RESTARTS 2
#define NUM_FRAMES_WITH_FAST_RESTART 10
static void handle_dsi_irq(struct mcde_chnl_state *chnl)
{
	u8 events;

	events = nova_dsilink_handle_irq(chnl->dsilink);
	if (events & DSILINK_IRQ_BTA_TE) {
		trace_vsync(chnl->id, chnl->state);
		atomic_inc(&chnl->vsync_cnt);
		chnl->vcmp_cnt_wait = atomic_read(&chnl->vcmp_cnt) + 1;

		if (chnl->port.frame_trig == MCDE_TRIG_SW &&
				chnl->is_bta_te_listening == true) {
			do_softwaretrig(chnl);
			chnl->is_bta_te_listening = false;
		}

		if (chnl->port.frame_trig == MCDE_TRIG_HW) {
			set_channel_state_atomic(chnl, CHNLSTATE_RUNNING);
			set_channel_state_atomic(chnl, CHNLSTATE_STOPPING);
		}
		wake_up_all(&chnl->vsync_waitq);
	}

	if (events & DSILINK_IRQ_NO_TE)
		set_channel_state_atomic(chnl, CHNLSTATE_STOPPED);
	if ((events & DSILINK_IRQ_MISSING_VSYNC) &&
					chnl->state == CHNLSTATE_RUNNING) {
		nova_dsilink_enable_video_mode(chnl->dsilink, false);
		nova_dsilink_enable_video_mode(chnl->dsilink, true);
		dev_warn(&mcde_dev->dev, "DSI restart - missing VSYNC\n");
	}
	if ((events & DSILINK_IRQ_MISSING_DATA) &&
					chnl->state == CHNLSTATE_RUNNING) {
		chnl->force_restart_frame_cnt = 0;
		atomic_set(&chnl->force_restart, true);
		queue_work(system_long_wq, &chnl->restart_work);
		dev_warn(&mcde_dev->dev, "Force restart - missing DATA\n");
	}
}

static void mcde_register_vcmp(struct mcde_chnl_state *chnl)
{
	if (!chnl->vcmp_per_field ||
			(chnl->vcmp_per_field && chnl->even_vcmp)) {
		int vcmp_cnt = atomic_add_return(1, &chnl->vcmp_cnt);
		int vcmp_cnt_since_restart = vcmp_cnt - chnl->force_restart_first_cnt;
		if (chnl->force_restart_frame_cnt > 0 &&
		    vcmp_cnt_since_restart > NUM_FRAMES_WITH_FAST_RESTART)
			chnl->force_restart_frame_cnt = 0;

		chnl->vsync_cnt_wait = atomic_read(&chnl->vsync_cnt) + 1;
	}
}

static irqreturn_t mcde_irq_handler(int irq, void *dev)
{
	u32 irq_status;
	u32 active_interrupts;

	active_interrupts = mcde_rreg(MCDE_AIS);

	if (active_interrupts & (MCDE_AIS_DSI0AI_MASK |
				MCDE_AIS_DSI1AI_MASK |
				MCDE_AIS_DSI2AI_MASK)) {
		/* Handle dsi link interrupts */
		struct mcde_chnl_state *chnl;
		for (chnl = channels; chnl < &channels[num_channels]; chnl++) {
			if (chnl->port.type == MCDE_PORTTYPE_DSI &&
						chnl->dsilink != NULL)
				handle_dsi_irq(chnl);
		}
	}

	if (active_interrupts & MCDE_AIS_MCDECHNLI_MASK) {
		irq_status = mcde_rreg(MCDE_MISCHNL);
		if (irq_status) {
			trace_chnl_err(irq_status);
			dev_err(&mcde_dev->dev,
					"chnl error=%.8x\n", irq_status);
			mcde_wreg(MCDE_RISCHNL, irq_status);
		}
	}
	if (active_interrupts & MCDE_AIS_MCDEERRI_MASK) {
		irq_status = mcde_rreg(MCDE_MISERR);
		if (irq_status) {
			trace_err(irq_status);
			dev_err(&mcde_dev->dev, "error=%.8x\n", irq_status);
			mcde_wreg(MCDE_RISERR, irq_status);
		}
	}

	if (active_interrupts & MCDE_AIS_MCDEPPI_MASK) {
		struct mcde_chnl_state *chnl;

		/* Get the channel irqs to handle */
		irq_status = mcde_rreg(MCDE_MISPP);

		/* Register which vcmps that will be handled */
		if (irq_status & MCDE_MISPP_VCMPAMIS_MASK)
			mcde_register_vcmp(&channels[MCDE_CHNL_A]);
		if (irq_status & MCDE_MISPP_VCMPBMIS_MASK)
			mcde_register_vcmp(&channels[MCDE_CHNL_B]);
		if (irq_status & MCDE_MISPP_VCMPC0MIS_MASK)
			mcde_register_vcmp(&channels[MCDE_CHNL_C0]);
		if (irq_status & MCDE_MISPP_VCMPC1MIS_MASK)
			mcde_register_vcmp(&channels[MCDE_CHNL_C1]);

		/* Call the necessary interrupt handlers */
		if (irq_status & MCDE_MISPP_VSCC0MIS_MASK) {
			for (chnl = channels; chnl < &channels[num_channels]; chnl++) {
				if (chnl->port.sync_src == MCDE_SYNCSRC_TE0)
					mcde_handle_vsync(chnl);
			}
		}
		if (irq_status & MCDE_MISPP_VSCC1MIS_MASK) {
			for (chnl = channels; chnl < &channels[num_channels]; chnl++) {
				if (chnl->port.sync_src == MCDE_SYNCSRC_TE1)
					mcde_handle_vsync(chnl);
			}
		}

		if (irq_status & MCDE_MISPP_VCMPAMIS_MASK)
			mcde_handle_vcmp(&channels[MCDE_CHNL_A]);
		if (irq_status & MCDE_MISPP_VCMPBMIS_MASK)
			mcde_handle_vcmp(&channels[MCDE_CHNL_B]);
		if (irq_status & MCDE_MISPP_VCMPC0MIS_MASK)
			mcde_handle_vcmp(&channels[MCDE_CHNL_C0]);
		if (irq_status & MCDE_MISPP_VCMPC1MIS_MASK)
			mcde_handle_vcmp(&channels[MCDE_CHNL_C1]);

		mcde_wreg(MCDE_RISPP, irq_status);
	}

	return IRQ_HANDLED;
}

/*
 * Transitions allowed: SETUP -> (WAIT_TE ->) RUNNING -> STOPPING -> STOPPED
 *                      WAIT_TE -> STOPPED
 *                      SUSPEND -> IDLE
 *                      DSI_READ -> IDLE
 *                      DSI_WRITE -> IDLE
 *                      REQUEST_BTA_TE -> IDLE
 */
static int set_channel_state_atomic(struct mcde_chnl_state *chnl,
			enum chnl_state state)
{
	enum chnl_state chnl_state = chnl->state;

	dev_dbg(&mcde_dev->dev, "Channel state change"
		" (chnl=%d, old=%d, new=%d)\n", chnl->id, chnl_state, state);

	if ((chnl_state == CHNLSTATE_SETUP && state == CHNLSTATE_WAIT_TE) ||
	    (chnl_state == CHNLSTATE_SETUP && state == CHNLSTATE_RUNNING) ||
	    (chnl_state == CHNLSTATE_WAIT_TE && state == CHNLSTATE_RUNNING) ||
	    (chnl_state == CHNLSTATE_RUNNING && state == CHNLSTATE_STOPPING)) {
		/* Set wait TE, running, or stopping state */
		chnl->state = state;
		trace_state(chnl->id, chnl->state);
		return 0;
	} else if ((chnl_state == CHNLSTATE_STOPPING &&
						state == CHNLSTATE_STOPPED) ||
		   (chnl_state == CHNLSTATE_WAIT_TE &&
						state == CHNLSTATE_STOPPED)) {
		/* Set stopped state */
		chnl->state = state;
		trace_state(chnl->id, chnl->state);
		wake_up_all(&chnl->state_waitq);
		return 0;
	} else if (state == CHNLSTATE_IDLE) {
		/* Set idle state */
		WARN_ON_ONCE(chnl_state != CHNLSTATE_DSI_READ &&
			     chnl_state != CHNLSTATE_DSI_WRITE &&
			     chnl_state != CHNLSTATE_REQ_BTA_TE &&
			     chnl_state != CHNLSTATE_SUSPEND);
		chnl->state = state;
		trace_state(chnl->id, chnl->state);
		wake_up_all(&chnl->state_waitq);
		return 0;
	} else {
		/* Invalid atomic state transition */
		dev_warn(&mcde_dev->dev, "Channel state change error (chnl=%d,"
			" old=%d, new=%d)\n", chnl->id, chnl_state, state);
		WARN_ON_ONCE(true);
		return -EINVAL;
	}
}

/* LOCKING: mcde_hw_lock */
static int set_channel_state_sync(struct mcde_chnl_state *chnl,
			enum chnl_state state)
{
	int ret = 0;
	enum chnl_state chnl_state = chnl->state;

	dev_dbg(&mcde_dev->dev, "Channel state change"
		" (chnl=%d, old=%d, new=%d)\n", chnl->id, chnl->state, state);

	/* No change */
	if (chnl_state == state)
		return 0;

	/* Wait for IDLE before changing state */
	if (chnl_state != CHNLSTATE_IDLE) {
		ret = wait_event_timeout(chnl->state_waitq,
			/* STOPPED -> IDLE is manual, so wait for both */
			chnl->state == CHNLSTATE_STOPPED ||
			chnl->state == CHNLSTATE_IDLE,
						msecs_to_jiffies(CHNL_TIMEOUT));
		if (WARN_ON_ONCE(!ret))
			dev_warn(&mcde_dev->dev, "Wait for channel timeout "
						"(chnl=%d, curr=%d, new=%d)\n",
						chnl->id, chnl->state, state);
		chnl_state = chnl->state;
	}

	/* Do manual transition from STOPPED to IDLE */
	if (chnl_state == CHNLSTATE_STOPPED)
		wait_for_flow_disabled(chnl);

	/* State is IDLE, do transition to new state */
	chnl->state = state;
	trace_state(chnl->id, chnl->state);

	return ret;
}

/* reentrant compatible*/
int mcde_chnl_wait_for_next_vsync(struct mcde_chnl_state *chnl,
		s64 *timestamp)
{
	long rem_jiffies;
	int w;

	mcde_lock(__func__, __LINE__);

	/* Handle the special case if mcde is disabled */
	if (enable_mcde_hw()) {
		mcde_unlock(__func__, __LINE__);
		return -EIO;
	}

	if (!chnl->formatter_updated)
		(void)update_channel_static_registers(chnl);

	/*
	 * Only if sync_src is MCDE_SYNCSRC_TE0, MCDE_SYNCSRC_TE1 or
	 * MCDE_SYNCSRC_BTA the vsync signal will be generated by
	 * the HW and handled by the mcde_irq_handler().
	 */
	switch (chnl->port.sync_src) {
	case MCDE_SYNCSRC_TE0:
		/* Intentional */
	case MCDE_SYNCSRC_TE1:
		mcde_add_vsync_capture_listener(chnl);
		break;
	case MCDE_SYNCSRC_BTA:
		mcde_add_bta_te_oneshot_listener(chnl);
		break;
	default:
		return -EIO;
	}

	w = atomic_read(&chnl->vsync_cnt) + 1;

	mcde_unlock(__func__, __LINE__);
	/*
	 * After this there can be context switches and other calls to
	 * other mcde_hw functions.
	 */
	trace_keyvalue((0xF0 | chnl->id), "Wait for next vsync", w);
	rem_jiffies = wait_event_timeout(chnl->vsync_waitq,
			atomic_read(&chnl->vsync_cnt) >= w,
			msecs_to_jiffies(CHNL_TIMEOUT));

	if (timestamp != NULL) {
		struct timespec ts;
		ktime_get_ts(&ts);
		*timestamp = timespec_to_ns(&ts);
	}

	mcde_lock(__func__, __LINE__);

        _mcde_chnl_enable(chnl);
        if (enable_mcde_hw()) {
                mcde_unlock(__func__, __LINE__);
                return -EIO;
        }

	switch (chnl->port.sync_src) {
	case MCDE_SYNCSRC_TE0:
		/* Intentional */
	case MCDE_SYNCSRC_TE1:
		mcde_remove_vsync_capture_listener(chnl);
		break;
	case MCDE_SYNCSRC_BTA:
		break;
	default:
		mcde_unlock(__func__, __LINE__);
		return -2; /* Should never happen */
	}
	mcde_unlock(__func__, __LINE__);

	if (rem_jiffies == 0) {
		trace_keyvalue((0xF0 | chnl->id),
				"Timeout waiting for next vsync", w);
		return -1;
	} else {
		trace_keyvalue((0xF0 | chnl->id),
				"Done waiting for vsync", w);
		return 0;
	}
}

static int wait_for_vcmp(struct mcde_chnl_state *chnl)
{
	int w = chnl->vcmp_cnt_wait;
	trace_keyvalue((0xF0 | chnl->id), "Wait for vcmp_cnt", w);
	if (wait_event_timeout(chnl->vcmp_waitq,
			atomic_read(&chnl->vcmp_cnt) >= w,
			msecs_to_jiffies(CHNL_TIMEOUT)) == 0) {
		trace_keyvalue((0xF0 | chnl->id), "Timeout waiting, vcmp_cnt",
				atomic_read(&chnl->vcmp_cnt));
		return -1;
	} else {
		trace_keyvalue((0xF0 | chnl->id), "Done waiting, vcmp_cnt",
				atomic_read(&chnl->vcmp_cnt));
		return 0;
	}
}

static int wait_for_vsync(struct mcde_chnl_state *chnl)
{
	int w = chnl->vsync_cnt_wait;
	trace_keyvalue((0xF0 | chnl->id), "Wait for vsync_cnt", w);
	if (wait_event_timeout(chnl->vsync_waitq,
			atomic_read(&chnl->vsync_cnt) >= w,
			msecs_to_jiffies(CHNL_TIMEOUT)) == 0) {
		trace_keyvalue((0xF0 | chnl->id), "Timeout waiting, vsync_cnt",
				atomic_read(&chnl->vsync_cnt));
		return -1;
	} else {
		trace_keyvalue((0xF0 | chnl->id), "Done waiting, vsync_cnt",
				atomic_read(&chnl->vsync_cnt));
		return 0;
	}
}


static int update_channel_static_registers(struct mcde_chnl_state *chnl)
{
	const struct mcde_port *port = &chnl->port;

	switch (chnl->fifo) {
	case MCDE_FIFO_A:
		mcde_wreg(MCDE_CHNL0MUXING + chnl->id *
			MCDE_CHNL0MUXING_GROUPOFFSET,
			MCDE_CHNL0MUXING_FIFO_ID_ENUM(FIFO_A));
		if (port->type == MCDE_PORTTYPE_DPI) {
			mcde_wfld(MCDE_CTRLA, FORMTYPE,
					MCDE_CTRLA_FORMTYPE_DPITV);
			mcde_wfld(MCDE_CTRLA, FORMID, port->link);
		} else if (port->type == MCDE_PORTTYPE_DSI) {
			mcde_wfld(MCDE_CTRLA, FORMTYPE,
					MCDE_CTRLA_FORMTYPE_DSI);
			mcde_wfld(MCDE_CTRLA, FORMID,
						get_dsi_formatter_id(port));
		}
		break;
	case MCDE_FIFO_B:
		mcde_wreg(MCDE_CHNL0MUXING + chnl->id *
			MCDE_CHNL0MUXING_GROUPOFFSET,
			MCDE_CHNL0MUXING_FIFO_ID_ENUM(FIFO_B));
		if (port->type == MCDE_PORTTYPE_DPI) {
			mcde_wfld(MCDE_CTRLB, FORMTYPE,
					MCDE_CTRLB_FORMTYPE_DPITV);
			mcde_wfld(MCDE_CTRLB, FORMID, port->link);
		} else if (port->type == MCDE_PORTTYPE_DSI) {
			mcde_wfld(MCDE_CTRLB, FORMTYPE,
					MCDE_CTRLB_FORMTYPE_DSI);
			mcde_wfld(MCDE_CTRLB, FORMID,
						get_dsi_formatter_id(port));
		}

		break;
	case MCDE_FIFO_C0:
		mcde_wreg(MCDE_CHNL0MUXING + chnl->id *
			MCDE_CHNL0MUXING_GROUPOFFSET,
			MCDE_CHNL0MUXING_FIFO_ID_ENUM(FIFO_C0));
		if (port->type == MCDE_PORTTYPE_DPI)
			return -EINVAL;
		mcde_wfld(MCDE_CTRLC0, FORMTYPE,
					MCDE_CTRLC0_FORMTYPE_DSI);
		mcde_wfld(MCDE_CTRLC0, FORMID, get_dsi_formatter_id(port));
		break;
	case MCDE_FIFO_C1:
		mcde_wreg(MCDE_CHNL0MUXING + chnl->id *
			MCDE_CHNL0MUXING_GROUPOFFSET,
			MCDE_CHNL0MUXING_FIFO_ID_ENUM(FIFO_C1));
		if (port->type == MCDE_PORTTYPE_DPI)
			return -EINVAL;
		mcde_wfld(MCDE_CTRLC1, FORMTYPE,
					MCDE_CTRLC1_FORMTYPE_DSI);
		mcde_wfld(MCDE_CTRLC1, FORMID, get_dsi_formatter_id(port));
		break;
	default:
		return -EINVAL;
	}

	/* Formatter */
	if (port->type == MCDE_PORTTYPE_DSI) {
		u8 idx;
		struct dsilink_port dsi_port;

		idx = get_dsi_formatter_id(port);

		get_dsi_port(chnl, &dsi_port);
		(void)nova_dsilink_setup(chnl->dsilink, &dsi_port);

		if (!dsi_use_clk_framework)
			enable_dsi_pll();

		if (nova_dsilink_enable(chnl->dsilink))
			goto dsi_link_error;

		nova_dsilink_exit_ulpm(chnl->dsilink);

		mcde_wreg(MCDE_DSIVID0CONF0 +
			idx * MCDE_DSIVID0CONF0_GROUPOFFSET,
			MCDE_DSIVID0CONF0_BLANKING(0) |
			MCDE_DSIVID0CONF0_VID_MODE(
				port->mode == MCDE_PORTMODE_VID) |
			MCDE_DSIVID0CONF0_CMD8(true) |
			MCDE_DSIVID0CONF0_BIT_SWAP(false) |
			MCDE_DSIVID0CONF0_BYTE_SWAP(false) |
			MCDE_DSIVID0CONF0_DCSVID_NOTGEN(true));
	}

	if (port->type == MCDE_PORTTYPE_DPI) {
		if (port->phy.dpi.lcd_freq != clk_round_rate(chnl->clk_dpi,
							port->phy.dpi.lcd_freq))
			dev_warn(&mcde_dev->dev, "Could not set lcd freq"
					" to %d\n", port->phy.dpi.lcd_freq);
		WARN_ON_ONCE(clk_set_rate(chnl->clk_dpi,
						port->phy.dpi.lcd_freq));
		WARN_ON_ONCE(clk_enable(chnl->clk_dpi));
	}

	mcde_wfld(MCDE_CR, MCDEEN, true);
	chnl->formatter_updated = true;

	dev_vdbg(&mcde_dev->dev, "Static registers setup, chnl=%d\n", chnl->id);

	return 0;
dsi_link_error:
	return -EINVAL;
}

static void mcde_chnl_oled_convert_apply(struct mcde_chnl_state *chnl,
					struct mcde_oled_transform *transform)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (chnl->oled_transform != transform) {
		chnl->oled_regs.alfa_red     = transform->matrix[0][0];
		chnl->oled_regs.alfa_green   = transform->matrix[0][1];
		chnl->oled_regs.alfa_blue    = transform->matrix[0][2];
		chnl->oled_regs.beta_red     = transform->matrix[1][0];
		chnl->oled_regs.beta_green   = transform->matrix[1][1];
		chnl->oled_regs.beta_blue    = transform->matrix[1][2];
		chnl->oled_regs.gamma_red    = transform->matrix[2][0];
		chnl->oled_regs.gamma_green  = transform->matrix[2][1];
		chnl->oled_regs.gamma_blue   = transform->matrix[2][2];
		chnl->oled_regs.off_red      = transform->offset[0];
		chnl->oled_regs.off_green    = transform->offset[1];
		chnl->oled_regs.off_blue     = transform->offset[2];
		chnl->oled_regs.dirty = true;

		chnl->oled_transform = transform;
	}

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

void mcde_chnl_col_convert_apply(struct mcde_chnl_state *chnl,
					struct mcde_col_transform *transform)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (chnl->transform != transform) {

		chnl->col_regs.y_red     = transform->matrix[0][0];
		chnl->col_regs.y_green   = transform->matrix[0][1];
		chnl->col_regs.y_blue    = transform->matrix[0][2];
		chnl->col_regs.cb_red    = transform->matrix[1][0];
		chnl->col_regs.cb_green  = transform->matrix[1][1];
		chnl->col_regs.cb_blue   = transform->matrix[1][2];
		chnl->col_regs.cr_red    = transform->matrix[2][0];
		chnl->col_regs.cr_green  = transform->matrix[2][1];
		chnl->col_regs.cr_blue   = transform->matrix[2][2];
		chnl->col_regs.off_y     = transform->offset[0];
		chnl->col_regs.off_cb    = transform->offset[1];
		chnl->col_regs.off_cr    = transform->offset[2];
		chnl->col_regs.dirty = true;

		chnl->transform = transform;
	}

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

static void setup_channel_and_overlay_color_conv(struct mcde_chnl_state *chnl,
						struct mcde_ovly_state *ovly)
{
	struct ovly_regs *regs = &ovly->regs;

	if (chnl->port.update_auto_trig &&
			chnl->port.type == MCDE_PORTTYPE_DSI) {
		if (ovly->pix_fmt == MCDE_OVLYPIXFMT_YCbCr422 &&
				regs->col_conv != MCDE_OVL0CR_COLCCTRL_ENABLED_SAT) {
			regs->col_conv = MCDE_OVL0CR_COLCCTRL_ENABLED_SAT;
			ovly->chnl->update_color_conversion = true;
		} else if (ovly->pix_fmt != MCDE_OVLYPIXFMT_YCbCr422 &&
				regs->col_conv != MCDE_OVL0CR_COLCCTRL_DISABLED) {
			regs->col_conv = MCDE_OVL0CR_COLCCTRL_DISABLED;
			ovly->chnl->update_color_conversion = true;
		}
	}
}

static void chnl_ovly_pixel_format_apply(struct mcde_chnl_state *chnl,
						struct mcde_ovly_state *ovly)
{
	struct mcde_port *port = &chnl->port;
	struct ovly_regs *regs = &ovly->regs;

	/* Note: YUV -> YUV: blending YUV overlays will not make sense. */
	static struct mcde_col_transform crycb_2_ycbcr = {
		/* Note that in MCDE YUV 422 pixels come as VYU pixels */
		.matrix = {
			{0x0000, 0x0100, 0x0000},
			{0x0000, 0x0000, 0x0100},
			{0x0100, 0x0000, 0x0000},
		},
		.offset = {0, 0, 0},
	};


	if (port->type == MCDE_PORTTYPE_DPI) {
		if (port->phy.dpi.tv_mode) {
			regs->col_conv = MCDE_OVL0CR_COLCCTRL_ENABLED_NO_SAT;
			if (ovly->pix_fmt != MCDE_OVLYPIXFMT_YCbCr422)
				mcde_chnl_col_convert_apply(chnl, &chnl->rgb_2_ycbcr);
			else
				mcde_chnl_col_convert_apply(chnl, &crycb_2_ycbcr);
		} else { /* DPI LCD mode */
			/* Note: DPI YUV not handled, assuming it is always RGB */
			/* Note: YUV is not a supported port pixel format for DPI */
			if (ovly->pix_fmt != MCDE_OVLYPIXFMT_YCbCr422) {
				/* standard case: DPI: RGB -> RGB */
				regs->col_conv = MCDE_OVL0CR_COLCCTRL_DISABLED;
			} else {
				/* DPI: YUV -> RGB */
				regs->col_conv = MCDE_OVL0CR_COLCCTRL_ENABLED_SAT;
				mcde_chnl_col_convert_apply(chnl, &chnl->ycbcr_2_rgb);
			}
		}
	} else { /* MCDE_PORTTYPE_DSI */
		if (port->pixel_format != MCDE_PORTPIXFMT_DSI_YCBCR422) {
			setup_channel_and_overlay_color_conv(chnl, ovly);
		} else {
			if (ovly->pix_fmt != MCDE_OVLYPIXFMT_YCbCr422)
				/* DSI: RGB -> YUV */
				mcde_chnl_col_convert_apply(chnl,
							&chnl->rgb_2_ycbcr);
			else
				/* DSI: YUV -> YUV */
				mcde_chnl_col_convert_apply(chnl,
							&crycb_2_ycbcr);
			regs->col_conv = MCDE_OVL0CR_COLCCTRL_ENABLED_NO_SAT;
		}
	}
}

static void update_overlay_registers(struct mcde_ovly_state *ovly,
		struct ovly_regs *regs,
		struct mcde_port *port, enum mcde_fifo fifo,
		s16 stride, bool interlaced,
		enum mcde_hw_rotation hw_rot)
{
	/* TODO: fix clipping for small overlay */
	u8 idx = ovly->idx;
	u32 lmrgn = regs->cropx * regs->bits_per_pixel;
	u32 tmrgn = regs->cropy * stride;
	u32 ppl = regs->ppl;
	u32 lpf = regs->lpf;
	s32 ljinc = stride;
	u32 pixelfetchwtrmrklevel;
	u8  nr_of_bufs = 1;
	u32 sel_mod = MCDE_EXTSRC0CR_SEL_MOD_SOFTWARE_SEL;
	struct mcde_platform_data *pdata = mcde_dev->dev.platform_data;

	if (hw_rot == MCDE_HW_ROT_VERT_MIRROR) {
		ljinc = -ljinc;
		tmrgn += stride * (regs->lpf - 1) / 8;
	}

	/*
	 * Preferably most of this is done in some apply function instead of for
	 * every update. However lpf has a dependency on update_y.
	 */
	if (interlaced && port->type == MCDE_PORTTYPE_DSI) {
		nr_of_bufs = 2;
		lpf = lpf / 2;
		ljinc *= 2;
	}

	pixelfetchwtrmrklevel = pdata->pixelfetchwtrmrk[idx];
	if (pixelfetchwtrmrklevel == 0) {
		/* Not set: Use default value */
		switch (idx) {
		case 0:
			pixelfetchwtrmrklevel = MCDE_PIXFETCH_WTRMRKLVL_OVL0;
			break;
		case 1:
			pixelfetchwtrmrklevel = MCDE_PIXFETCH_WTRMRKLVL_OVL1;
			break;
		case 2:
			pixelfetchwtrmrklevel = MCDE_PIXFETCH_WTRMRKLVL_OVL2;
			break;
		case 3:
			pixelfetchwtrmrklevel = MCDE_PIXFETCH_WTRMRKLVL_OVL3;
			break;
		case 4:
			pixelfetchwtrmrklevel = MCDE_PIXFETCH_WTRMRKLVL_OVL4;
			break;
		case 5:
			pixelfetchwtrmrklevel = MCDE_PIXFETCH_WTRMRKLVL_OVL5;
			break;
		}
	}
	if (regs->enabled)
		dev_dbg(&mcde_dev->dev, "ovly%d pfwml:%d %dbpp\n", idx,
				pixelfetchwtrmrklevel, regs->bits_per_pixel);

	if (port->update_auto_trig && port->type == MCDE_PORTTYPE_DSI) {
		switch (port->sync_src) {
		case MCDE_SYNCSRC_OFF:
			sel_mod = MCDE_EXTSRC0CR_SEL_MOD_SOFTWARE_SEL;
			break;
		case MCDE_SYNCSRC_TE0:
		case MCDE_SYNCSRC_TE1:
		case MCDE_SYNCSRC_TE_POLLING:
		default:
			sel_mod = MCDE_EXTSRC0CR_SEL_MOD_AUTO_TOGGLE;
			break;
		}
	} else if (port->type == MCDE_PORTTYPE_DPI)
		sel_mod = MCDE_EXTSRC0CR_SEL_MOD_SOFTWARE_SEL;

	mcde_wreg(MCDE_EXTSRC0CONF + idx * MCDE_EXTSRC0CONF_GROUPOFFSET,
		MCDE_EXTSRC0CONF_BUF_ID(0) |
		MCDE_EXTSRC0CONF_BUF_NB(nr_of_bufs) |
		MCDE_EXTSRC0CONF_PRI_OVLID(idx) |
		MCDE_EXTSRC0CONF_BPP(regs->bpp) |
		MCDE_EXTSRC0CONF_BGR(regs->bgr) |
		MCDE_EXTSRC0CONF_BEBO(regs->bebo) |
		MCDE_EXTSRC0CONF_BEPO(false));
	mcde_wreg(MCDE_EXTSRC0CR + idx * MCDE_EXTSRC0CR_GROUPOFFSET,
		MCDE_EXTSRC0CR_SEL_MOD(sel_mod) |
		MCDE_EXTSRC0CR_MULTIOVL_CTRL_ENUM(PRIMARY) |
		MCDE_EXTSRC0CR_FS_DIV_DISABLE(false) |
		MCDE_EXTSRC0CR_FORCE_FS_DIV(false));
	mcde_wreg(MCDE_OVL0CONF + idx * MCDE_OVL0CONF_GROUPOFFSET,
		MCDE_OVL0CONF_PPL(ppl) |
		MCDE_OVL0CONF_EXTSRC_ID(idx) |
		MCDE_OVL0CONF_LPF(lpf));
	mcde_wreg(MCDE_OVL0CONF2 + idx * MCDE_OVL0CONF2_GROUPOFFSET,
		MCDE_OVL0CONF2_BP(regs->alpha_source) |
		MCDE_OVL0CONF2_ALPHAVALUE(regs->alpha_value) |
		MCDE_OVL0CONF2_OPQ(regs->opq) |
		MCDE_OVL0CONF2_PIXOFF(lmrgn & 63) |
		MCDE_OVL0CONF2_PIXELFETCHERWATERMARKLEVEL(
			pixelfetchwtrmrklevel));
	mcde_wreg(MCDE_OVL0LJINC + idx * MCDE_OVL0LJINC_GROUPOFFSET,
		ljinc);
	mcde_wreg(MCDE_OVL0CROP + idx * MCDE_OVL0CROP_GROUPOFFSET,
		MCDE_OVL0CROP_TMRGN(tmrgn) |
		MCDE_OVL0CROP_LMRGN(lmrgn >> 6));

	mcde_update_ovly_control(idx, regs);

	regs->dirty = false;

	dev_vdbg(&mcde_dev->dev, "Overlay registers setup, idx=%d\n", idx);
}

static void update_overlay_registers_on_the_fly(u8 idx, struct ovly_regs *regs, u32 xoffset)
{
	mcde_wreg(MCDE_OVL0COMP + idx * MCDE_OVL0COMP_GROUPOFFSET,
		MCDE_OVL0COMP_XPOS(regs->xpos + xoffset) |
		MCDE_OVL0COMP_CH_ID(regs->ch_id) |
		MCDE_OVL0COMP_YPOS(regs->ypos) |
		MCDE_OVL0COMP_Z(regs->z));

	mcde_wreg(MCDE_EXTSRC0A0 + idx * MCDE_EXTSRC0A0_GROUPOFFSET,
		regs->baseaddress0);
	mcde_wreg(MCDE_EXTSRC0A1 + idx * MCDE_EXTSRC0A1_GROUPOFFSET,
		regs->baseaddress1);
	regs->dirty_buf = false;
}

static void do_softwaretrig(struct mcde_chnl_state *chnl)
{
	unsigned long flags;

	local_irq_save(flags);

	enable_flow(chnl, true);
	mcde_wreg(MCDE_CHNL0SYNCHSW +
		chnl->id * MCDE_CHNL0SYNCHSW_GROUPOFFSET,
		MCDE_CHNL0SYNCHSW_SW_TRIG(true));
	disable_flow(chnl, true);

	local_irq_restore(flags);

	dev_vdbg(&mcde_dev->dev, "Software TRIG on channel %d\n", chnl->id);
}

static void disable_flow(struct mcde_chnl_state *chnl, bool setstate)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (chnl->id) {
	case MCDE_CHNL_A:
		mcde_wfld(MCDE_CRA0, FLOEN, false);
		break;
	case MCDE_CHNL_B:
		mcde_wfld(MCDE_CRB0, FLOEN, false);
		break;
	case MCDE_CHNL_C0:
		mcde_wfld(MCDE_CRC, C1EN, false);
		break;
	case MCDE_CHNL_C1:
		mcde_wfld(MCDE_CRC, C2EN, false);
		break;
	}

	if (setstate)
		set_channel_state_atomic(chnl, CHNLSTATE_STOPPING);

	local_irq_restore(flags);
}

static void stop_channel(struct mcde_chnl_state *chnl)
{
	const struct mcde_port *port = &chnl->port;

	dev_vdbg(&mcde_dev->dev, "%s %d\n", __func__, chnl->state);

	if (!chnl->port.update_auto_trig ||
				chnl->state != CHNLSTATE_RUNNING) {
		set_channel_state_sync(chnl, CHNLSTATE_SUSPEND);
		return;
	}

	if (chnl->port.sync_src == MCDE_SYNCSRC_OFF)
		disable_flow(chnl, true);
	else
		set_channel_state_atomic(chnl, CHNLSTATE_STOPPING);

	set_channel_state_sync(chnl, CHNLSTATE_SUSPEND);

	if (port->type == MCDE_PORTTYPE_DSI && port->mode == MCDE_PORTMODE_VID) {
			nova_dsilink_enable_video_mode(chnl->dsilink, false);
	}

	if (WARN_ON_ONCE(!mcde_rfld(MCDE_CTRLA, FIFOEMPTY)))
		dev_warn(&mcde_dev->dev, "Fifo no empty chnl=%d\n", chnl->id);
	if (WARN_ON_ONCE(mcde_rfld(MCDE_CRA0, FLOEN)))
		dev_warn(&mcde_dev->dev, "FLOEN at stop chnl=%d\n", chnl->id);
}

static void wait_for_flow_disabled(struct mcde_chnl_state *chnl)
{
	int i = 0;

	switch (chnl->id) {
	case MCDE_CHNL_A:
		for (i = 0; i < MCDE_FLOWEN_MAX_TRIAL; i++) {
			if (!mcde_rfld(MCDE_CRA0, FLOEN)) {
				dev_vdbg(&mcde_dev->dev,
					"Flow (A) disable after >= %d ms\n", i);
				break;
			}
			usleep_range(1000, 1500);
		}
		break;
	case MCDE_CHNL_B:
		for (i = 0; i < MCDE_FLOWEN_MAX_TRIAL; i++) {
			if (!mcde_rfld(MCDE_CRB0, FLOEN)) {
				dev_vdbg(&mcde_dev->dev,
				"Flow (B) disable after >= %d ms\n", i);
				break;
			}
			usleep_range(1000, 1500);
		}
		break;
	case MCDE_CHNL_C0:
		for (i = 0; i < MCDE_FLOWEN_MAX_TRIAL; i++) {
			if (!mcde_rfld(MCDE_CRC, C1EN)) {
				dev_vdbg(&mcde_dev->dev,
				"Flow (C1) disable after >= %d ms\n", i);
				break;
			}
			usleep_range(1000, 1500);
		}
		break;
	case MCDE_CHNL_C1:
		for (i = 0; i < MCDE_FLOWEN_MAX_TRIAL; i++) {
			if (!mcde_rfld(MCDE_CRC, C2EN)) {
				dev_vdbg(&mcde_dev->dev,
				"Flow (C2) disable after >= %d ms\n", i);
				break;
			}
			usleep_range(1000, 1500);
		}
		break;
	}
	if (i == MCDE_FLOWEN_MAX_TRIAL)
		dev_err(&mcde_dev->dev, "%s: channel %d timeout\n",
							__func__, chnl->id);
}

static void enable_flow(struct mcde_chnl_state *chnl, bool setstate)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (chnl->port.type == MCDE_PORTTYPE_DSI) {
		nova_dsilink_set_clk_continous(chnl->dsilink, true);
		if (chnl->port.mode == MCDE_PORTMODE_VID)
			nova_dsilink_enable_video_mode(chnl->dsilink, true);
	}

	/*
	 * When ROTEN is set, the FLOEN bit will also be set but
	 * the flow has to be started anyway.
	 */
	switch (chnl->id) {
	case MCDE_CHNL_A:
		WARN_ON_ONCE(mcde_rfld(MCDE_CRA0, FLOEN));
		mcde_wfld(MCDE_CRA0, ROTEN, chnl->regs.roten);
		mcde_wfld(MCDE_CRA0, FLOEN, true);
		break;
	case MCDE_CHNL_B:
		WARN_ON_ONCE(mcde_rfld(MCDE_CRB0, FLOEN));
		mcde_wfld(MCDE_CRB0, ROTEN, chnl->regs.roten);
		mcde_wfld(MCDE_CRB0, FLOEN, true);
		break;
	case MCDE_CHNL_C0:
		WARN_ON_ONCE(mcde_rfld(MCDE_CRC, C1EN));
		mcde_wfld(MCDE_CRC, C1EN, true);
		break;
	case MCDE_CHNL_C1:
		WARN_ON_ONCE(mcde_rfld(MCDE_CRC, C2EN));
		mcde_wfld(MCDE_CRC, C2EN, true);
		break;
	}

	if (setstate)
		set_channel_state_atomic(chnl, CHNLSTATE_RUNNING);
}

/* TODO get from register */
#define MCDE_CLK_FREQ_MHZ 160
static u32 get_pkt_div(u32 disp_ppl,
		struct mcde_port *port,
		enum mcde_fifo fifo)
{
	/*
	 * The lines can be split in several packets only on DSI CMD mode.
	 * In DSI VIDEO mode, 1 line = 1 packet.
	 * DPI is like DSI VIDEO (watermark = 1 line).
	 * DPI waits for fifo ready only for the first line of the first frame.
	 * If line is wider than fifo size, one can set watermark
	 * at fifo size, or set it to line size as watermark will be
	 * saturated at fifo size inside MCDE.
	 */
	switch (port->type) {
	case MCDE_PORTTYPE_DSI:
		if (port->mode == MCDE_PORTMODE_CMD) {
			u32 fifo_size = get_output_fifo_size(fifo);
			/*
			 * Only divide into several packets if ppl is a
			 * multiple of fifo size.
			 */
			if ((disp_ppl % fifo_size) == 0)
				/* Equivalent of ceil(disp_ppl/fifo_size) */
				return (disp_ppl - 1) / fifo_size + 1;
			else
				return 1;
		} else {
			return 1;
		}
		break;
	case MCDE_PORTTYPE_DPI:
		return 1;
		break;
	default:
		break;
	}
	return 1;
}

static void update_vid_frame_parameters(struct mcde_chnl_state *chnl,
				struct mcde_video_mode *vmode, u8 bpp)
{
	u8 pixel_mode;
	u8 rgb_header;

	/*
	 * The rgb_header identifies the pixel stream format,
	 * as described in the MIPI DSI Specification:
	 *
	 * 0x0E: Packed pixel stream, 16-bit RGB, 565 format
	 * 0x1E: Packed pixel stream, 18-bit RGB, 666 format
	 * 0x2E: Loosely Packed pixel stream, 18-bit RGB, 666 format
	 * 0x3E: Packed pixel stream, 24-bit RGB, 888 format
	 */
	switch (chnl->port.pixel_format) {
	case MCDE_PORTPIXFMT_DSI_16BPP:
		pixel_mode = 0;
		rgb_header = 0x0E;
		break;
	case MCDE_PORTPIXFMT_DSI_18BPP:
		pixel_mode = 2;
		rgb_header = 0x2E;
		break;
	case MCDE_PORTPIXFMT_DSI_18BPP_PACKED:
		pixel_mode = 1;
		rgb_header = 0x1E;
		break;
	case MCDE_PORTPIXFMT_DSI_24BPP:
		pixel_mode = 3;
		rgb_header = 0x3E;
		break;
	default:
		pixel_mode = 3;
		rgb_header = 0x3E;
		dev_warn(&mcde_dev->dev,
			"%s: invalid pixel format %d\n",
			__func__, chnl->port.pixel_format);
		break;
	}

	nova_dsilink_update_frame_parameters(chnl->dsilink,
					(struct dsilink_video_mode *)vmode, bpp,
							pixel_mode, rgb_header);
}

static void set_vsync_method(u8 idx, struct mcde_port *port)
{
	u32 out_synch_src = MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_FORMATTER;
	u32 src_synch = MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;

	if (port->type == MCDE_PORTTYPE_DSI) {
		switch (port->frame_trig) {
		case MCDE_TRIG_HW:
			src_synch = MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;
			break;
		case MCDE_TRIG_SW:
			src_synch = MCDE_CHNL0SYNCHMOD_SRC_SYNCH_SOFTWARE;
			break;
		default:
			src_synch = MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;
			break;
		}

		switch (port->sync_src) {
		case MCDE_SYNCSRC_OFF:
			out_synch_src =
				MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_FORMATTER;
			break;
		case MCDE_SYNCSRC_TE0:
			out_synch_src = MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_TE0;
			if (src_synch ==
				MCDE_CHNL0SYNCHMOD_SRC_SYNCH_SOFTWARE) {
				dev_dbg(&mcde_dev->dev, "%s: badly configured "
						"frame sync, TE0 defaulting "
						"to hw frame trig\n", __func__);
				src_synch =
					MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;
			}
			break;
		case MCDE_SYNCSRC_TE1:
			out_synch_src = MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_TE1;
			if (src_synch ==
				MCDE_CHNL0SYNCHMOD_SRC_SYNCH_SOFTWARE) {
				dev_dbg(&mcde_dev->dev, "%s: badly configured "
						"frame sync, TE1 defaulting "
						"to hw frame trig\n", __func__);
				src_synch =
					MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;
			}
			break;
		case MCDE_SYNCSRC_BTA:
			out_synch_src =
				MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_FORMATTER;
			break;
		case MCDE_SYNCSRC_TE_POLLING:
			out_synch_src =
				MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_FORMATTER;
			if (src_synch ==
				MCDE_CHNL0SYNCHMOD_SRC_SYNCH_SOFTWARE) {
				dev_dbg(&mcde_dev->dev, "%s: badly configured "
					"frame sync, TE_POLLING defaulting "
						"to hw frame trig\n", __func__);
				src_synch =
					MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;
			}
			break;
		default:
			out_synch_src =
				MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_FORMATTER;
			src_synch = MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE;
			dev_dbg(&mcde_dev->dev, "%s: no sync src selected, "
						"defaulting to DSI BTA with "
						"hw frame trig\n", __func__);
			break;
		}
	} else if (port->type == MCDE_PORTTYPE_DPI) {
		out_synch_src = MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_FORMATTER;
		src_synch = port->update_auto_trig ?
					MCDE_CHNL0SYNCHMOD_SRC_SYNCH_HARDWARE :
					MCDE_CHNL0SYNCHMOD_SRC_SYNCH_SOFTWARE;
	}

	mcde_wreg(MCDE_CHNL0SYNCHMOD +
		idx * MCDE_CHNL0SYNCHMOD_GROUPOFFSET,
		MCDE_CHNL0SYNCHMOD_SRC_SYNCH(src_synch) |
		MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC(out_synch_src));
}

void update_channel_registers(enum mcde_chnl chnl_id, struct chnl_regs *regs,
				struct mcde_port *port, enum mcde_fifo fifo,
				struct mcde_video_mode *video_mode)
{
	u8 idx = chnl_id;
	u32 fifo_wtrmrk = 0;
	u8 red;
	u8 green;
	u8 blue;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	/*
	 * Select appropriate fifo watermark.
	 * Watermark will be saturated at fifo size inside MCDE.
	 */
	fifo_wtrmrk = video_mode->xres /
		get_pkt_div(video_mode->xres, port, fifo);

	/* Don't set larger than fifo size */
	switch (chnl_id) {
	case MCDE_CHNL_A:
	case MCDE_CHNL_B:
		if (fifo_wtrmrk > output_fifo_ab_size)
			fifo_wtrmrk = output_fifo_ab_size;
		break;
	case MCDE_CHNL_C0:
	case MCDE_CHNL_C1:
		if (fifo_wtrmrk > output_fifo_c0c1_size)
			fifo_wtrmrk = output_fifo_c0c1_size;
		break;
	default:
		break;
	}

	dev_vdbg(&mcde_dev->dev, "%s fifo_watermark=%d for chnl_id=%d\n",
		__func__, fifo_wtrmrk, chnl_id);

	switch (chnl_id) {
	case MCDE_CHNL_A:
		mcde_wfld(MCDE_CTRLA, FIFOWTRMRK, fifo_wtrmrk);
		break;
	case MCDE_CHNL_B:
		mcde_wfld(MCDE_CTRLB, FIFOWTRMRK, fifo_wtrmrk);
		break;
	case MCDE_CHNL_C0:
		mcde_wfld(MCDE_CTRLC0, FIFOWTRMRK, fifo_wtrmrk);
		break;
	case MCDE_CHNL_C1:
		mcde_wfld(MCDE_CTRLC1, FIFOWTRMRK, fifo_wtrmrk);
		break;
	default:
		break;
	}

	set_vsync_method(idx, port);

        /* +445681 display padding */
	#if 1
		mcde_wreg(MCDE_CHNL0CONF + idx * MCDE_CHNL0CONF_GROUPOFFSET,
		MCDE_CHNL0CONF_PPL(regs->ppl-1) |
		MCDE_CHNL0CONF_LPF(regs->lpf-1));
	 /*
	   Because of display distortion issue or no display of DV2.0 patch
	   +++++++++++++ error when display is abnomal state ++++++++++++++++++
	    mcde mcde: FLOEN at stop chnl=0                     
	    mcde mcde: wait_for_flow_disabled: channel 0 timeout
                 mcde mcde: Fifo no empty chnl=0 
                ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	 */	
	#else
        if (regs->roten) {
                mcde_wreg(MCDE_CHNL0CONF + idx * MCDE_CHNL0CONF_GROUPOFFSET,
                        MCDE_CHNL0CONF_PPL(regs->ppl + video_mode->yres_padding - 1) |
                        MCDE_CHNL0CONF_LPF(regs->lpf + video_mode->xres_padding - 1));
        }
        else {
                mcde_wreg(MCDE_CHNL0CONF + idx * MCDE_CHNL0CONF_GROUPOFFSET,
                        MCDE_CHNL0CONF_PPL(regs->ppl + video_mode->xres_padding - 1) |
                        MCDE_CHNL0CONF_LPF(regs->lpf + video_mode->yres_padding - 1));
        }
	#endif
        /* -445681 display padding */
	mcde_wreg(MCDE_CHNL0STAT + idx * MCDE_CHNL0STAT_GROUPOFFSET,
		MCDE_CHNL0STAT_CHNLBLBCKGND_EN(true) |
		MCDE_CHNL0STAT_CHNLRD(true));
	if (regs->background_yuv) {
		red = 0x80;
		green = 0x10;
		blue = 0x80;
	} else {
		red = 0x00;
		green = 0x00;
		blue = 0x00;
	}
	mcde_wreg(MCDE_CHNL0BCKGNDCOL + idx * MCDE_CHNL0BCKGNDCOL_GROUPOFFSET,
		MCDE_CHNL0BCKGNDCOL_B(blue) |
		MCDE_CHNL0BCKGNDCOL_G(green) |
		MCDE_CHNL0BCKGNDCOL_R(red));

	if (chnl_id == MCDE_CHNL_A || chnl_id == MCDE_CHNL_B) {
		u32 mcde_crx1;
		u32 mcde_pal0x;
		u32 mcde_pal1x;
		if (chnl_id == MCDE_CHNL_A) {
			mcde_crx1 = MCDE_CRA1;
			mcde_pal0x = MCDE_PAL0A;
			mcde_pal1x = MCDE_PAL1A;
			mcde_wfld(MCDE_CRA0, PALEN, regs->palette_enable);
			mcde_wfld(MCDE_CRA0, OLEDEN, regs->oled_enable);
		} else {
			mcde_crx1 = MCDE_CRB1;
			mcde_pal0x = MCDE_PAL0B;
			mcde_pal1x = MCDE_PAL1B;
			mcde_wfld(MCDE_CRB0, PALEN, regs->palette_enable);
			mcde_wfld(MCDE_CRB0, OLEDEN, regs->oled_enable);
		}
		mcde_wreg(mcde_crx1,
			MCDE_CRA1_PCD(regs->pcd) |
			MCDE_CRA1_CLKSEL(regs->clksel) |
			MCDE_CRA1_CDWIN(regs->cdwin) |
			MCDE_CRA1_OUTBPP(bpp2outbpp(regs->bpp)) |
			MCDE_CRA1_BCD(regs->bcd) |
			MCDE_CRA1_CLKTYPE(regs->internal_clk));
		if (regs->palette_enable) {
			int i;
			for (i = 0; i < 256; i++) {
				mcde_wreg(mcde_pal0x,
					MCDE_PAL0A_GREEN(regs->map_g(i)) |
					MCDE_PAL0A_BLUE(regs->map_b(i)));
				mcde_wreg(mcde_pal1x,
					MCDE_PAL1A_RED(regs->map_r(i)));
			}
		}
	}

	/* Formatter */
	if (port->type == MCDE_PORTTYPE_DSI) {
		u8 fidx;
		u32 temp, packet;
		/* pkt_div is used to avoid underflow in output fifo for
		 * large packets */
		u32 pkt_div = 1;
		u32 dsi_delay0 = 0;
		u32 screen_ppl, screen_lpf;

		fidx = get_dsi_formatter_id(port);

		/* +445681 display padding */
	 	/*
	 	Because of display distortion issue or no display of DV2.0 patch
	   	+++++++++++++ error when display is abnomal state +++++++++++++++
	   	 mcde mcde: FLOEN at stop chnl=0                     
		 mcde mcde: wait_for_flow_disabled: channel 0 timeout
             	 mcde mcde: Fifo no empty chnl=0 
		++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	 	*/
	 	#if 1
		screen_ppl = video_mode->xres;
		screen_lpf = video_mode->yres;
		#else
		screen_ppl = video_mode->xres + video_mode->xres_padding;
		screen_lpf = video_mode->yres + video_mode->yres_padding;
		#endif
		/* -445681 display padding */

		pkt_div = get_pkt_div(screen_ppl, port, fifo);

		if (video_mode->interlaced)
			screen_lpf /= 2;

		if (port->mode == MCDE_PORTMODE_CMD && port->update_auto_trig) {
			/* pkt_delay_progressive = pixelclock * htot /
			 * (1E12 / 160E6) / pkt_div */
			dsi_delay0 = (video_mode->pixclock) * (video_mode->xres
				+ video_mode->hbp + video_mode->hfp) /
				(100000000 / ((mcde_clk_rate / 10000))) / pkt_div;

			if ((screen_ppl == SCREEN_PPL_CEA2) &&
			    (screen_lpf == SCREEN_LPF_CEA2))
				dsi_delay0 += DSI_DELAY0_CEA2_ADD;
		}

		temp = mcde_rreg(MCDE_DSIVID0CONF0 +
			fidx * MCDE_DSIVID0CONF0_GROUPOFFSET);
		mcde_wreg(MCDE_DSIVID0CONF0 +
			fidx * MCDE_DSIVID0CONF0_GROUPOFFSET,
			(temp & ~MCDE_DSIVID0CONF0_PACKING_MASK) |
			MCDE_DSIVID0CONF0_PACKING(regs->dsipacking));
		/* no extra command byte in video mode */
		if (port->mode == MCDE_PORTMODE_CMD)
			packet = ((screen_ppl / pkt_div * regs->bpp) >> 3) + 1;
		else
			packet = ((screen_ppl / pkt_div * regs->bpp) >> 3);
		mcde_wreg(MCDE_DSIVID0FRAME +
			fidx * MCDE_DSIVID0FRAME_GROUPOFFSET,
			MCDE_DSIVID0FRAME_FRAME(packet * pkt_div * screen_lpf));
		mcde_wreg(MCDE_DSIVID0PKT + fidx * MCDE_DSIVID0PKT_GROUPOFFSET,
			MCDE_DSIVID0PKT_PACKET(packet));
		mcde_wreg(MCDE_DSIVID0SYNC +
			fidx * MCDE_DSIVID0SYNC_GROUPOFFSET,
			MCDE_DSIVID0SYNC_SW(0) |
			MCDE_DSIVID0SYNC_DMA(0));
		mcde_wreg(MCDE_DSIVID0CMDW +
			fidx * MCDE_DSIVID0CMDW_GROUPOFFSET,
			MCDE_DSIVID0CMDW_CMDW_START(DCS_CMD_WRITE_START) |
			MCDE_DSIVID0CMDW_CMDW_CONTINUE(DCS_CMD_WRITE_CONTINUE));
		mcde_wreg(MCDE_DSIVID0DELAY0 +
			fidx * MCDE_DSIVID0DELAY0_GROUPOFFSET,
			MCDE_DSIVID0DELAY0_INTPKTDEL(dsi_delay0));
		mcde_wreg(MCDE_DSIVID0DELAY1 +
			fidx * MCDE_DSIVID0DELAY1_GROUPOFFSET,
			MCDE_DSIVID0DELAY1_TEREQDEL(0) |
			MCDE_DSIVID0DELAY1_FRAMESTARTDEL(0));

		/* Setup VSYNC capture */
		if (port->sync_src == MCDE_SYNCSRC_TE0) {
			mcde_wreg(MCDE_VSCRC0,
				MCDE_VSCRC0_VSDBL(0) |
				MCDE_VSCRC0_VSSEL_ENUM(VSYNC0) |
				MCDE_VSCRC0_VSPOL(port->vsync_polarity) |
				MCDE_VSCRC0_VSPDIV(port->vsync_clock_div) |
				MCDE_VSCRC0_VSPMAX(port->vsync_max_duration) |
				MCDE_VSCRC0_VSPMIN(port->vsync_min_duration));
		} else if (port->sync_src == MCDE_SYNCSRC_TE1) {
			mcde_wreg(MCDE_VSCRC1,
				MCDE_VSCRC1_VSDBL(0) |
				MCDE_VSCRC1_VSSEL_ENUM(VSYNC1) |
				MCDE_VSCRC1_VSPOL(port->vsync_polarity) |
				MCDE_VSCRC1_VSPDIV(port->vsync_clock_div) |
				MCDE_VSCRC1_VSPMAX(port->vsync_max_duration) |
				MCDE_VSCRC1_VSPMIN(port->vsync_min_duration));
		}

		if (port->mode == MCDE_PORTMODE_VID)
			update_vid_frame_parameters(&channels[chnl_id],
						video_mode, regs->bpp / 8);
	} else if (port->type == MCDE_PORTTYPE_DPI &&
						!port->phy.dpi.tv_mode) {
		/* DPI LCD Mode */
		if (chnl_id == MCDE_CHNL_A) {
			mcde_wreg(MCDE_SYNCHCONFA,
				MCDE_SYNCHCONFA_HWREQVEVENT_ENUM(
							ACTIVE_VIDEO) |
				MCDE_SYNCHCONFA_HWREQVCNT(
							video_mode->yres - 1) |
				MCDE_SYNCHCONFA_SWINTVEVENT_ENUM(
							ACTIVE_VIDEO) |
				MCDE_SYNCHCONFA_SWINTVCNT(
							video_mode->yres - 1));
		} else if (chnl_id == MCDE_CHNL_B) {
			mcde_wreg(MCDE_SYNCHCONFB,
				MCDE_SYNCHCONFB_HWREQVEVENT_ENUM(
							ACTIVE_VIDEO) |
				MCDE_SYNCHCONFB_HWREQVCNT(
							video_mode->yres - 1) |
				MCDE_SYNCHCONFB_SWINTVEVENT_ENUM(
							ACTIVE_VIDEO) |
				MCDE_SYNCHCONFB_SWINTVCNT(
							video_mode->yres - 1));
		}
	}

	if (regs->roten) {
		u32 stripwidth;
		u32 stripwidth_val;

		/* calc strip width, 32 bits used internally */
		stripwidth = regs->rotbufsize / (video_mode->xres * 4);

		if (stripwidth >= 32)
			stripwidth_val = MCDE_ROTACONF_STRIP_WIDTH_32PIX;
		else if (stripwidth >= 16)
			stripwidth_val = MCDE_ROTACONF_STRIP_WIDTH_16PIX;
		else if (stripwidth >= 8)
			stripwidth_val = MCDE_ROTACONF_STRIP_WIDTH_8PIX;
		else if (stripwidth >= 4)
			stripwidth_val = MCDE_ROTACONF_STRIP_WIDTH_4PIX;
		else
			stripwidth_val = MCDE_ROTACONF_STRIP_WIDTH_2PIX;
		dev_vdbg(&mcde_dev->dev, "%s stripwidth=%d\n", __func__,
						1 << (stripwidth_val + 1));
		mcde_wreg(MCDE_ROTADD0A + chnl_id * MCDE_ROTADD0A_GROUPOFFSET,
			regs->rotbuf1);
		mcde_wreg(MCDE_ROTADD1A + chnl_id * MCDE_ROTADD1A_GROUPOFFSET,
			regs->rotbuf2);
		mcde_wreg(MCDE_ROTACONF + chnl_id * MCDE_ROTACONF_GROUPOFFSET,
			MCDE_ROTACONF_ROTBURSTSIZE_ENUM(8W) |
			MCDE_ROTACONF_ROTDIR(regs->rotdir) |
			MCDE_ROTACONF_STRIP_WIDTH(stripwidth_val) |
			MCDE_ROTACONF_RD_MAXOUT_ENUM(4_REQ) |
			MCDE_ROTACONF_WR_MAXOUT_ENUM(8_REQ));
	}

	/* Blending */
	if (chnl_id == MCDE_CHNL_A) {
		mcde_wfld(MCDE_CRA0, BLENDEN, regs->blend_en);
		mcde_wfld(MCDE_CRA0, BLENDCTRL, regs->blend_ctrl);
		mcde_wfld(MCDE_CRA0, ALPHABLEND, regs->alpha_blend);
	} else if (chnl_id == MCDE_CHNL_B) {
		mcde_wfld(MCDE_CRB0, BLENDEN, regs->blend_en);
		mcde_wfld(MCDE_CRB0, BLENDCTRL, regs->blend_ctrl);
		mcde_wfld(MCDE_CRB0, ALPHABLEND, regs->alpha_blend);
	}

	dev_vdbg(&mcde_dev->dev, "Channel registers setup, chnl=%d\n", chnl_id);
	regs->dirty = false;
}

void mcde_invalidate_channel(struct mcde_chnl_state *chnl)
{
	chnl->update_color_conversion = true;
	chnl->regs.dirty = true;
	chnl->col_regs.dirty = true;
	chnl->tv_regs.dirty = true;
	chnl->oled_regs.dirty = true;

	chnl->ovly0->dirty = true;
	chnl->ovly0->regs.dirty = true;
	chnl->ovly0->regs.dirty_buf = true;

	if (chnl->ovly1) {
		chnl->ovly1->dirty = true;
		chnl->ovly1->regs.dirty = true;
		chnl->ovly1->regs.dirty_buf = true;
	}
}

static int enable_mcde_hw_pre(void)
{
	int ret;
	int i;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	cancel_delayed_work(&hw_timeout_work);
#if 0	/* avoid distrubing video mode splash screen */
	schedule_delayed_work(&hw_timeout_work,
					msecs_to_jiffies(MCDE_SLEEP_WATCHDOG));
#endif
	for (i = 0; i < num_channels; i++) {
		struct mcde_chnl_state *chnl = &channels[i];
		if (chnl->state == CHNLSTATE_SUSPEND) {
			/* Mark all registers as dirty */
			set_channel_state_atomic(chnl, CHNLSTATE_IDLE);

			if (!mcde_is_enabled)
				chnl->first_frame_vsync_fix = true;

			mcde_invalidate_channel(chnl);

			atomic_set(&chnl->vcmp_cnt, 0);
			atomic_set(&chnl->vsync_cnt, 0);
			chnl->vsync_cnt_wait = 0;
			chnl->vcmp_cnt_wait = 0;
			chnl->vcmp_cnt_to_change_col_conv = 0;
			atomic_set(&chnl->n_vsync_capture_listeners, 0);
			chnl->is_bta_te_listening = false;
		}

		if (chnl->regs.roten && !chnl->esram_is_enabled) {
			WARN_ON_ONCE(regulator_enable(regulator_esram_epod));
			chnl->esram_is_enabled = true;
		} else if (!chnl->regs.roten && chnl->esram_is_enabled) {
			WARN_ON_ONCE(regulator_disable(regulator_esram_epod));
			chnl->esram_is_enabled = false;
		}
	}

	if (mcde_is_enabled) {
		dev_vdbg(&mcde_dev->dev, "%s - already enabled\n", __func__);
		return 0;
	}

	enable_clocks_and_power(mcde_dev);

	/* clear underflow irq */
	mcde_wreg(MCDE_RISERR, MCDE_RISERR_FUARIS_MASK);
	ret = request_irq(mcde_irq, mcde_irq_handler, 0, "mcde",
							&mcde_dev->dev);
	if (ret) {
		dev_dbg(&mcde_dev->dev, "Failed to request irq (irq=%d)\n",
								mcde_irq);
		cancel_delayed_work(&hw_timeout_work);
		return -EINVAL;
	}

	mcde_debugfs_hw_enabled();

	update_mcde_registers();

	dev_vdbg(&mcde_dev->dev, "%s - enable done\n", __func__);

	mcde_is_enabled = true;
	return 0;
}

static int enable_mcde_hw(void)
{
	int ret;
	int i;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	cancel_delayed_work(&hw_timeout_work);
	schedule_delayed_work(&hw_timeout_work,
					msecs_to_jiffies(MCDE_SLEEP_WATCHDOG));

	for (i = 0; i < num_channels; i++) {
		struct mcde_chnl_state *chnl = &channels[i];
		if (chnl->state == CHNLSTATE_SUSPEND) {
			/* Mark all registers as dirty */
			set_channel_state_atomic(chnl, CHNLSTATE_IDLE);

			if (!mcde_is_enabled)
				chnl->first_frame_vsync_fix = true;

			mcde_invalidate_channel(chnl);

			atomic_set(&chnl->vcmp_cnt, 0);
			atomic_set(&chnl->vsync_cnt, 0);
			chnl->vsync_cnt_wait = 0;
			chnl->vcmp_cnt_wait = 0;
			chnl->vcmp_cnt_to_change_col_conv = 0;
			atomic_set(&chnl->n_vsync_capture_listeners, 0);
			chnl->is_bta_te_listening = false;
		}

		if (chnl->regs.roten && !chnl->esram_is_enabled) {
			WARN_ON_ONCE(regulator_enable(regulator_esram_epod));
			chnl->esram_is_enabled = true;
		} else if (!chnl->regs.roten && chnl->esram_is_enabled) {
			WARN_ON_ONCE(regulator_disable(regulator_esram_epod));
			chnl->esram_is_enabled = false;
		}
	}

	if (mcde_is_enabled) {
		dev_vdbg(&mcde_dev->dev, "%s - already enabled\n", __func__);
		return 0;
	}

	/* FIXME: Temp patch for power toggling MCDE */
	usleep_range(50000, 50000);
	enable_clocks_and_power(mcde_dev);

	ret = request_irq(mcde_irq, mcde_irq_handler, 0, "mcde",
							&mcde_dev->dev);
	if (ret) {
		dev_dbg(&mcde_dev->dev, "Failed to request irq (irq=%d)\n",
								mcde_irq);
		cancel_delayed_work(&hw_timeout_work);
		return -EINVAL;
	}

	mcde_debugfs_hw_enabled();

	update_mcde_registers();

	dev_vdbg(&mcde_dev->dev, "%s - enable done\n", __func__);

	mcde_is_enabled = true;
	return 0;
}

/* DSI */

/**
 * @brief Send a more than 15-byte data sequence to a DSI display,
 * using the MCDE_WDATAx functionality (MCDE output fifo).
 * Note: Call this function only during the LCD init sequence, means not
 * during normal display refreshes where the MCDE output fifo is used too.
 *
 * @param chnl mcde channel
 * @param cmd  display register number
 * @param data byte buffer containing the display register parameters
 * @param len  display reg parameter number (>=MCDE_MAX_DSI_DIRECT_CMD_WRITE)
 *
 * @return 0 if ok, -EIO or -EINVAL if error
 */
static int mcde_fifo_write_data(struct mcde_chnl_state *chnl,
				u8 cmd, u8 *data, int len)
{
	u8 fidx;
	u32 val, px;
	int i, rest, ret = 0;
	unsigned long flags;
	enum dsilink_mode old_mode;

	dev_vdbg(&mcde_dev->dev, "%s()-cmd %d, len %d\n", __func__, cmd, len);

	if (len <= DSILINK_MAX_DSI_DIRECT_CMD_WRITE ||
			chnl->port.type != MCDE_PORTTYPE_DSI ||
			len > get_output_fifo_size(chnl->fifo) ||
			chnl->state == CHNLSTATE_RUNNING)
		return -EINVAL;

	if (chnl->state == CHNLSTATE_RUNNING) {
		dev_dbg(&mcde_dev->dev, "%s: Channel busy\n", __func__);
		return -EBUSY;
	}

	mcde_lock(__func__, __LINE__);

	ret = set_channel_state_sync(chnl, CHNLSTATE_DSI_WRITE);
	if (ret) {
		dev_err(&mcde_dev->dev, "%s: Channel state error\n", __func__);
		goto fail_set_state;
	}

	_mcde_chnl_enable(chnl);
	if (enable_mcde_hw()) {
		ret = -EIO;
		goto fail_enable_mcde;
	}

	/* Verify initial state */
	if (!mcde_rfld(MCDE_CTRLA, FIFOEMPTY))
		dev_warn(&mcde_dev->dev, "%s: FIFO error on entry\n", __func__);
	if (mcde_rreg(MCDE_RISERR))
		dev_warn(&mcde_dev->dev, "%s: Error on entry\n", __func__);

	/* Set main MCDE registers to appropriate values */
	if (!chnl->formatter_updated)
		(void)update_channel_static_registers(chnl);
	update_channel_registers(chnl->id, &chnl->regs, &chnl->port,
			chnl->fifo, &chnl->vmode);

	old_mode = chnl->port.mode; //TODO: Real enum convert
	if (old_mode != DSILINK_MODE_CMD) {
		struct dsilink_port port;

		get_dsi_port(chnl, &port);
		port.mode = DSILINK_MODE_CMD;
		nova_dsilink_disable(chnl->dsilink);//TODO: Remove
		nova_dsilink_setup(chnl->dsilink, &port); //TODO: Verify disabled
		nova_dsilink_enable(chnl->dsilink);
	}

	/* Data will be sent with 24-bit packets */
	px = (len + 2) / 3; /* px = ceil(len/3) */
	mcde_wreg(MCDE_CHNL0CONF + chnl->id * MCDE_CHNL0CONF_GROUPOFFSET,
			MCDE_CHNL0CONF_PPL(px - 1) |
			MCDE_CHNL0CONF_LPF(0));

	/* 24bpp pixel formatter, to avoid data modifications */
	fidx = get_dsi_formatter_id(&chnl->port);
	mcde_wreg(MCDE_DSIVID0CONF0 + fidx * MCDE_DSIVID0CONF0_GROUPOFFSET,
			MCDE_DSIVID0CONF0_PACKING_ENUM(RGB888) |
			MCDE_DSIVID0CONF0_DCSVID_NOTGEN(true) |
			MCDE_DSIVID0CONF0_CMD8(true) |
			MCDE_DSIVID0CONF0_VID_MODE(false));

	/* Set packet & frame sizes (cmd + param number) */
	mcde_wreg(MCDE_DSIVID0CMDW + fidx * MCDE_DSIVID0CMDW_GROUPOFFSET,
			MCDE_DSIVID0CMDW_CMDW_START(cmd) |
			MCDE_DSIVID0CMDW_CMDW_CONTINUE(0));

	/* MCDE_DSIxFRAME = MCDE_DSIxPKT = len +1 (data bytes + cmd) */
	mcde_wreg(MCDE_DSIVID0PKT + fidx * MCDE_DSIVID0PKT_GROUPOFFSET,
			MCDE_DSIVID0PKT_PACKET(1 + len));
	mcde_wreg(MCDE_DSIVID0FRAME + fidx * MCDE_DSIVID0FRAME_GROUPOFFSET,
			MCDE_DSIVID0FRAME_FRAME(1 + len));

	mcde_wreg(MCDE_CR, MCDE_CR_MCDEEN(1));//TODO: Remove?

	mcde_wreg(MCDE_CHNL0SYNCHMOD + chnl->id * MCDE_CHNL0SYNCHMOD_GROUPOFFSET,
			MCDE_CHNL0SYNCHMOD_SRC_SYNCH_ENUM(SOFTWARE) |
			MCDE_CHNL0SYNCHMOD_OUT_SYNCH_SRC_ENUM(FORMATTER));

	/* Disable interrupts during send */
	mcde_wreg(MCDE_IMSCPP, 0); //TODO: Adapt to chnl or even remove?
	mcde_wreg(MCDE_IMSCOVL, 0);
	mcde_wreg(MCDE_IMSCCHNL, 0);
	mcde_wreg(MCDE_IMSCERR, 0);

	mcde_wreg(MCDE_RISERR, 0xFFFFFFFF);

	/* Set fifo watermark level and enable flow */
	switch (chnl->id) {
	case MCDE_CHNL_A:
		mcde_wfld(MCDE_CTRLA, FIFOWTRMRK, px);
		mcde_wfld(MCDE_CRA0, FLOEN, true);
		break;
	case MCDE_CHNL_B:
		mcde_wfld(MCDE_CTRLB, FIFOWTRMRK, px);
		mcde_wfld(MCDE_CRB0, FLOEN, true);
		break;
	case MCDE_CHNL_C0:
		mcde_wfld(MCDE_CTRLC0, FIFOWTRMRK, px);
		mcde_wfld(MCDE_CRC, C1EN, true);
		break;
	case MCDE_CHNL_C1:
		mcde_wfld(MCDE_CTRLC1, FIFOWTRMRK, px);
		mcde_wfld(MCDE_CRC, C2EN, true);
		break;
	}

	/*
	* A write to MCDE_WDATAx will push a 24-bit value to the fifo
	* split the input data into chunks of 3 bytes
	*/
	rest = len % 3;
	for (i = 0; i < len/3; i++) {
		val = (data[i*3] << 16) | (data[i*3 + 1] << 8) | data[i*3 + 2];
		switch (chnl->id) {
		case MCDE_CHNL_A:
			mcde_wreg(MCDE_MCDE_WDATAA, val);
			break;
		case MCDE_CHNL_B:
			mcde_wreg(MCDE_MCDE_WDATAB, val);
			break;
		case MCDE_CHNL_C0:
			mcde_wreg(MCDE_WDATADC0, val);
			break;
		case MCDE_CHNL_C1:
			mcde_wreg(MCDE_WDATADC1, val);
			break;
		}
	}

	if (rest) {
		if (rest == 2)
			val = (data[len - 2] << 16) | (data[len - 1] << 8);
		else /* rest == 1 */
			val = data[len - 1] << 16;

		switch (chnl->id) {
		case MCDE_CHNL_A:
			mcde_wreg(MCDE_MCDE_WDATAA, val);
			break;
		case MCDE_CHNL_B:
			mcde_wreg(MCDE_MCDE_WDATAB, val);
			break;
		case MCDE_CHNL_C0:
			mcde_wreg(MCDE_WDATADC0, val);
			break;
		case MCDE_CHNL_C1:
			mcde_wreg(MCDE_WDATADC1, val);
			break;
		}
	}

	/* Wait for command to complete */
	nova_dsilink_wait_while_running(chnl->dsilink);
	mcde_wfld(MCDE_CRA0, FLOEN, false);

	//TODO: Make code generic for other channels

	/* Prepare for dummy update */
	px = 64;
	#define rr 64
	mcde_wfld(MCDE_OVL0CR, OVLEN, false); /* TODO: Deactivate chnl->ovly* */
	mcde_wreg(MCDE_DSIVID0CMDW + fidx * MCDE_DSIVID0CMDW_GROUPOFFSET,
			MCDE_DSIVID0CMDW_CMDW_START(0) |
			MCDE_DSIVID0CMDW_CMDW_CONTINUE(0));
	mcde_wreg(MCDE_DSIVID0PKT + fidx * MCDE_DSIVID0PKT_GROUPOFFSET,
			MCDE_DSIVID0PKT_PACKET(1 + px*rr*3));
	mcde_wreg(MCDE_DSIVID0FRAME + fidx * MCDE_DSIVID0FRAME_GROUPOFFSET,
			MCDE_DSIVID0FRAME_FRAME(1 + px*rr*3));
	mcde_wreg(MCDE_CHNL0CONF + chnl->id * MCDE_CHNL0CONF_GROUPOFFSET,
			MCDE_CHNL0CONF_PPL(px - 1) |
			MCDE_CHNL0CONF_LPF(rr - 1));

	/* Do dummy update to clean out the FIFO if needed */
	if (!mcde_rfld(MCDE_CTRLA, FIFOEMPTY)) {
		mcde_wfld(MCDE_CRA0, FLOEN, true);
		mcde_wreg(MCDE_CHNL0SYNCHSW +
				chnl->id * MCDE_CHNL0SYNCHSW_GROUPOFFSET,
				MCDE_CHNL0SYNCHSW_SW_TRIG(true));
		mcde_wfld(MCDE_CRA0, FLOEN, false);
		nova_dsilink_wait_while_running(chnl->dsilink);
	}

	/* Do dummy update to trigger vcmp and make FLOEN go to 0.
	 * This is not vital, but it will prevent warnings in other code. */
	mcde_wfld(MCDE_CRA0, FLOEN, true);
	local_irq_save(flags);
	preempt_disable(); //TODO: Needed?
	mcde_wreg(MCDE_CHNL0SYNCHSW + chnl->id * MCDE_CHNL0SYNCHSW_GROUPOFFSET,
			MCDE_CHNL0SYNCHSW_SW_TRIG(true));
	mcde_wfld(MCDE_CRA0, FLOEN, false);
	local_irq_restore(flags);
	preempt_enable(); //TODO: Needed?
	wait_for_flow_disabled(chnl);

	/* Clear any errors */
	mcde_wreg(MCDE_RISPP, 0xFFFFFFFF); //TODO: Adapt to chnl
	mcde_wreg(MCDE_RISOVL, 0xFFFFFFFF);
	mcde_wreg(MCDE_RISCHNL, 0xFFFFFFFF);
	mcde_wreg(MCDE_RISERR, 0xFFFFFFFF);

	/* Restore MCDE/DSI state */
	if (old_mode != DSILINK_MODE_CMD) {
		struct dsilink_port port;

		get_dsi_port(chnl, &port);
		port.mode = old_mode;
		nova_dsilink_disable(chnl->dsilink);
		nova_dsilink_setup(chnl->dsilink, &port);
	}
	update_mcde_registers();
	update_channel_static_registers(chnl);
	update_channel_registers(chnl->id, &chnl->regs, &chnl->port,
			chnl->fifo, &chnl->vmode);
	mcde_wfld(MCDE_OVL0CR, OVLEN, true); /* TODO: Remove */

fail_enable_mcde:
	set_channel_state_atomic(chnl, CHNLSTATE_IDLE);
fail_set_state:
	mcde_unlock(__func__, __LINE__);

	return 0;
}

static int mcde_dsi_direct_cmd_write(struct mcde_chnl_state *chnl,
			bool dcs, u8 cmd, u8 *data, int len)
{
	int ret;

	if (!chnl || chnl->port.type != MCDE_PORTTYPE_DSI || len < 0)
		return -EINVAL;

	if ((len <= DSILINK_MAX_DSI_DIRECT_CMD_WRITE && !dcs) ||
	    (len <  DSILINK_MAX_DSI_DIRECT_CMD_WRITE &&  dcs)) {
		mcde_lock(__func__, __LINE__);

		_mcde_chnl_enable(chnl);
		if (enable_mcde_hw()) {
			mcde_unlock(__func__, __LINE__);
			return -EINVAL;
		}
		if (!chnl->formatter_updated)
			(void)update_channel_static_registers(chnl);

		/*
		 * Some panels don't allow commands during update in command mode
		 * Issue not seen on video mode panels, so we let DSI link do the
		 * arbitration on packet level.
		*/
		if (chnl->port.mode == MCDE_PORTMODE_CMD)
			set_channel_state_sync(chnl, CHNLSTATE_DSI_WRITE);

		if (dcs)
			ret = nova_dsilink_dcs_write(chnl->dsilink,
								cmd, data, len);
		else
			ret = nova_dsilink_dsi_write(chnl->dsilink, data, len);

		if (chnl->port.mode == MCDE_PORTMODE_CMD)
			set_channel_state_atomic(chnl, CHNLSTATE_IDLE);

		mcde_unlock(__func__, __LINE__);
	} else if (len <= MCDE_MAX_DSI_DIRECT_CMD_WRITE) {
		ret = mcde_fifo_write_data(chnl, cmd, data, len);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int mcde_dsi_generic_write(struct mcde_chnl_state *chnl, u8* para, int len)
{
	return mcde_dsi_direct_cmd_write(chnl, false, 0, para, len);
}

int mcde_dsi_dcs_write(struct mcde_chnl_state *chnl, u8 cmd, u8* data, int len)
{
	return mcde_dsi_direct_cmd_write(chnl, true, cmd, data, len);
}

int mcde_dsi_dcs_read(struct mcde_chnl_state *chnl, u8 cmd, u32 *data, int *len)
{
	int ret = 0;

	if (*len > MCDE_MAX_DCS_READ || chnl->port.type != MCDE_PORTTYPE_DSI)
		return -EINVAL;

	mcde_lock(__func__, __LINE__);

	_mcde_chnl_enable(chnl);
	if (enable_mcde_hw()) {
		mcde_unlock(__func__, __LINE__);
		return -EINVAL;
	}

	if (!chnl->formatter_updated)
		(void)update_channel_static_registers(chnl);

	if (chnl->port.mode == MCDE_PORTMODE_CMD)
		set_channel_state_sync(chnl, CHNLSTATE_DSI_READ);

	ret = nova_dsilink_dsi_read(chnl->dsilink, cmd, data, len);

	if (chnl->port.mode == MCDE_PORTMODE_CMD)
		set_channel_state_atomic(chnl, CHNLSTATE_IDLE);

	mcde_unlock(__func__, __LINE__);

	return ret;
}

static inline int mcde_dsi_special(struct mcde_chnl_state *chnl,
						enum dsilink_cmd_datatype dt)
{
	if (chnl->port.type != MCDE_PORTTYPE_DSI)
		return -EINVAL;

	mcde_lock(__func__, __LINE__);

	if (enable_mcde_hw()) {
		mcde_unlock(__func__, __LINE__);
		return -EIO;
	}

	if (!chnl->formatter_updated)
		(void)update_channel_static_registers(chnl);

	if (chnl->port.mode == MCDE_PORTMODE_CMD)
		set_channel_state_sync(chnl, CHNLSTATE_DSI_WRITE);

	if (dt == DSILINK_CMD_SET_MAX_PKT_SIZE)
		nova_dsilink_send_max_read_len(chnl->dsilink);
	else if (dt == DSILINK_CMD_TURN_ON_PERIPHERAL)
		nova_dsilink_turn_on_peripheral(chnl->dsilink);
	else if (dt == DSILINK_CMD_SHUT_DOWN_PERIPHERAL)
		nova_dsilink_shut_down_peripheral(chnl->dsilink);

	if (chnl->port.mode == MCDE_PORTMODE_CMD)
		set_channel_state_atomic(chnl, CHNLSTATE_IDLE);

	mcde_unlock(__func__, __LINE__);

	return 0;
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
int mcde_dsi_set_max_pkt_size(struct mcde_chnl_state *chnl)
{
	return mcde_dsi_special(chnl, DSILINK_CMD_SET_MAX_PKT_SIZE);
}

int mcde_dsi_turn_on_peripheral(struct mcde_chnl_state *chnl)
{
	return mcde_dsi_special(chnl, DSILINK_CMD_TURN_ON_PERIPHERAL);
}

int mcde_dsi_shut_down_peripheral(struct mcde_chnl_state *chnl)
{
	return mcde_dsi_special(chnl, DSILINK_CMD_SHUT_DOWN_PERIPHERAL);
}

/* MCDE channels */
static struct mcde_chnl_state *_mcde_chnl_get_drm(enum mcde_chnl chnl_id)
{
	struct mcde_chnl_state *chnl = NULL;
	int i;

	for (i = 0; i < num_channels; i++) {
		if (chnl_id == channels[i].id) {
			chnl = &channels[i];
			break;
		}
	}

	return chnl;
}

static struct mcde_chnl_state *_mcde_chnl_get(enum mcde_chnl chnl_id,
	enum mcde_fifo fifo, const struct mcde_port *port)
{
	int i;
	struct mcde_chnl_state *chnl = NULL;
	struct mcde_platform_data *pdata = mcde_dev->dev.platform_data;

	static struct mcde_col_transform ycbcr_2_rgb = {
		/* Note that in MCDE YUV 422 pixels come as VYU pixels */
		.matrix = {
			{0xff30, 0x012a, 0xff9c},
			{0x0000, 0x012a, 0x0204},
			{0x0199, 0x012a, 0x0000},
		},
		.offset = {0x0088, 0xfeeb, 0xff21},
	};

	static struct mcde_col_transform rgb_2_ycbcr = {
		.matrix = {
			{0x0042, 0x0081, 0x0019},
			{0xffda, 0xffb6, 0x0070},
			{0x0070, 0xffa2, 0xffee},
		},
		.offset = {0x0010, 0x0080, 0x0080},
	};

	/* Allocate channel */
	for (i = 0; i < num_channels; i++) {
		if (chnl_id == channels[i].id)
			chnl = &channels[i];
	}
	if (!chnl) {
		dev_dbg(&mcde_dev->dev, "Invalid channel, chnl=%d\n", chnl_id);
		return ERR_PTR(-EINVAL);
	}
	if (chnl->reserved) {
		dev_dbg(&mcde_dev->dev, "Channel in use, chnl=%d\n", chnl_id);
		return ERR_PTR(-EBUSY);
	}

	chnl->port = *port;
	chnl->fifo = fifo;
	chnl->formatter_updated = false;
	chnl->ycbcr_2_rgb = ycbcr_2_rgb;
	chnl->rgb_2_ycbcr = rgb_2_ycbcr;
	chnl->oled_color_conversion = false;

	chnl->blend_en = true;
	chnl->blend_ctrl = MCDE_CRA0_BLENDCTRL_SOURCE;
	chnl->alpha_blend = 0xFF;
	chnl->rotbuf1 = pdata->rotbuf1;
	chnl->rotbuf2 = pdata->rotbuf2;
	chnl->rotbufsize = pdata->rotbufsize;

	_mcde_chnl_apply(chnl);
	chnl->reserved = true;

	if (chnl->port.type == MCDE_PORTTYPE_DPI) {
		chnl->clk_dpi = clk_get(&mcde_dev->dev, CLK_DPI);
		if (chnl->port.phy.dpi.tv_mode)
			chnl->vcmp_per_field = true;
	} else if (chnl->port.type == MCDE_PORTTYPE_DSI) {
		chnl->dsilink = nova_dsilink_get(port->link);
		chnl->dsilink->update_dsi_freq = dsi_use_clk_framework;
	}

	return chnl;
}

static int _mcde_chnl_apply(struct mcde_chnl_state *chnl)
{
	bool roten;
	u8 rotdir;

	if (chnl->hw_rot == MCDE_HW_ROT_90_CCW) {
		roten = true;
		rotdir = MCDE_ROTACONF_ROTDIR_CCW;
		chnl->regs.ppl = chnl->vmode.yres;
		chnl->regs.lpf = chnl->vmode.xres;
	} else if (chnl->hw_rot == MCDE_HW_ROT_90_CW) {
		roten = true;
		rotdir = MCDE_ROTACONF_ROTDIR_CW;
		chnl->regs.ppl = chnl->vmode.yres;
		chnl->regs.lpf = chnl->vmode.xres;
	} else {
		roten = false;
		rotdir = 0;
		chnl->regs.ppl = chnl->vmode.xres;
		chnl->regs.lpf = chnl->vmode.yres;
	}

	chnl->regs.bpp = portfmt2bpp(chnl->port.pixel_format);
	chnl->regs.roten = roten;
	chnl->regs.rotdir = rotdir;
	if (roten && rotdir == MCDE_ROTACONF_ROTDIR_CCW)
		chnl->regs.ovly_xoffset = ALIGN(chnl->vmode.yres_padding, 0x10);
	else
		chnl->regs.ovly_xoffset = 0;
	chnl->regs.rotbuf1 = chnl->rotbuf1;
	chnl->regs.rotbuf2 = chnl->rotbuf2;
	chnl->regs.rotbufsize = chnl->rotbufsize;
	chnl->regs.palette_enable = chnl->palette_enable;
	chnl->regs.map_r = chnl->map_r;
	chnl->regs.map_g = chnl->map_g;
	chnl->regs.map_b = chnl->map_b;
	if (chnl->port.type == MCDE_PORTTYPE_DSI) {
		chnl->regs.clksel = MCDE_CRA1_CLKSEL_MCDECLK;
		chnl->regs.dsipacking =
				portfmt2dsipacking(chnl->port.pixel_format);
	} else if (chnl->port.type == MCDE_PORTTYPE_DPI) {
		if (chnl->port.phy.dpi.tv_mode) {
			chnl->regs.internal_clk = false;
			chnl->regs.bcd = true;
			if (chnl->id == MCDE_CHNL_A)
				chnl->regs.clksel = MCDE_CRA1_CLKSEL_TV1CLK;
			else
				chnl->regs.clksel = MCDE_CRA1_CLKSEL_TV2CLK;
		} else {
			chnl->regs.internal_clk = true;
			chnl->regs.clksel = MCDE_CRA1_CLKSEL_CLKPLL72;
			chnl->regs.cdwin =
					portfmt2cdwin(chnl->port.pixel_format);
			chnl->regs.bcd = (chnl->port.phy.dpi.clock_div < 2);
			if (!chnl->regs.bcd)
				chnl->regs.pcd =
					chnl->port.phy.dpi.clock_div - 2;
		}
		dpi_video_mode_apply(chnl);
	}

	chnl->regs.blend_ctrl = chnl->blend_ctrl;
	chnl->regs.blend_en = chnl->blend_en;
	chnl->regs.alpha_blend = chnl->alpha_blend;

	/* For now the update area will always be full screen */
	chnl->regs.x   = 0;
	chnl->regs.y   = 0;

	/* Set oled and color conversion states if necessary */
	_mcde_chnl_update_color_conversion(chnl);

	chnl->regs.dirty = true;

	dev_vdbg(&mcde_dev->dev, "Channel applied, chnl=%d\n", chnl->id);
	return 0;
}

static void setup_channel(struct mcde_chnl_state *chnl)
{
	static struct mcde_oled_transform yuv240_2_rgb = {
		/* Note that in MCDE YUV 422 pixels come as VYU pixels */
		.matrix = {
			{0x1990, 0x12A0, 0x0000},
			{0x2D00, 0x12A0, 0x2640},
			{0x0000, 0x12A0, 0x1FFF},
		},
		.offset = {0x2DF0, 0x0870, 0x3150},
	};
	static struct mcde_col_transform rgb_2_yuv240 = {
		/* Note that in MCDE YUV 422 pixels come as VYU pixels */
		.matrix = {
			{0x0042, 0x0081, 0x0019},
			{0xffda, 0xffb5, 0x0071},
			{0x0070, 0xffa2, 0xffee},
		},
		.offset = {0x0010, 0x0080, 0x0080},
	};

	/*
	 * If we do channel color conversion we always do
	 * rgb_2_yuv240.
	 * And we then always do oled_conversion for yuv240_2_rgb
	 * back.
	 */
	mcde_chnl_col_convert_apply(chnl, &rgb_2_yuv240);
	mcde_chnl_oled_convert_apply(chnl, &yuv240_2_rgb);

	if (chnl->port.type == MCDE_PORTTYPE_DPI && chnl->tv_regs.dirty)
		update_dpi_registers(chnl->id, &chnl->tv_regs);

	/*
	 * For command mode displays using external sync (TE0/TE1), the first
	 * frame need special treatment to avoid garbage on the panel.
	 * This mechanism is placed here because it needs the chnl_state and
	 * modifies settings before they are committed to the registers.
	 */
	if (!chnl->port.update_auto_trig && chnl->first_frame_vsync_fix) {
		/* Save requested mode. */
		chnl->port.requested_sync_src = chnl->port.sync_src;
		chnl->port.requested_frame_trig = chnl->port.frame_trig;
		/*
		 * Temporarily set other mode.
		 * Requested mode will be set at next frame.
		 */
		chnl->port.sync_src = MCDE_SYNCSRC_OFF;
		chnl->port.frame_trig = MCDE_TRIG_SW;
	}

	if (chnl->update_color_conversion) {
		mcde_update_non_buffered_color_conversion(chnl);
		mcde_update_double_buffered_color_conversion(chnl);
	}

	if (chnl->id == MCDE_CHNL_A || chnl->id == MCDE_CHNL_B) {
		if (chnl->col_regs.dirty)
			update_col_registers(chnl->id, &chnl->col_regs);
		if (chnl->oled_regs.dirty)
			update_oled_registers(chnl->id, &chnl->oled_regs);
	}

	if (chnl->regs.dirty)
		update_channel_registers(chnl->id, &chnl->regs, &chnl->port,
						chnl->fifo, &chnl->vmode);
}

static void chnl_update_continous(struct mcde_chnl_state *chnl)
{
	if (chnl->state == CHNLSTATE_RUNNING)
		return;

	setup_channel(chnl);
	mcde_add_vsync_capture_listener(chnl);
	enable_flow(chnl, true);
}

static void chnl_update_non_continous(struct mcde_chnl_state *chnl)
{
	/* Commit settings to registers */
	setup_channel(chnl);

	if (chnl->port.type != MCDE_PORTTYPE_DSI)
		return;

	switch(chnl->port.sync_src) {
	case MCDE_SYNCSRC_OFF:
		if (chnl->port.frame_trig == MCDE_TRIG_SW) {
			do_softwaretrig(chnl);
			if (chnl->first_frame_vsync_fix) {
				/* restore requested vsync mode */
				chnl->port.sync_src =
					chnl->port.requested_sync_src;
				chnl->port.frame_trig =
					chnl->port.requested_frame_trig;
				chnl->regs.dirty = true;
				chnl->first_frame_vsync_fix = false;
				dev_vdbg(&mcde_dev->dev,
					"SWITCH TO TE0 DSIx\n");
			}
		} else {
			enable_flow(chnl, true);
			disable_flow(chnl, true);
		}
		dev_vdbg(&mcde_dev->dev, "Chnl update (no sync), chnl=%d\n",
				chnl->id);
		break;
	case MCDE_SYNCSRC_BTA:
		mcde_add_bta_te_oneshot_listener(chnl);
		chnl->is_bta_te_listening = true;
		set_channel_state_atomic(chnl, CHNLSTATE_WAIT_TE);
		if (chnl->port.frame_trig == MCDE_TRIG_HW) {
			/*
			 * During BTA TE the MCDE block will be stalled,
			 * once the TE is received the DMA trig will
			 * happen
			 */
			enable_flow(chnl, false);
			disable_flow(chnl, false);
		}
		break;
	case MCDE_SYNCSRC_TE0:
	case MCDE_SYNCSRC_TE1:
		set_channel_state_atomic(chnl, CHNLSTATE_WAIT_TE);
		mcde_add_vsync_capture_listener(chnl);
		enable_flow(chnl, false);
		break;
	case MCDE_SYNCSRC_TE_POLLING:
	default:
		break;
	}
}

static void mcde_ovly_update_color_conversion(struct mcde_ovly_state *ovly,
					bool oled_color_conversion)
{
	bool ovly_yuv;
	bool ovly_valid;

	ovly_valid = ovly != NULL && ovly->paddr != 0 && ovly->inuse;
	ovly_yuv = ovly_valid &&
			ovly->pix_fmt == MCDE_OVLYPIXFMT_YCbCr422;

	if (oled_color_conversion) {
		if (ovly_yuv && (ovly->regs.col_conv !=
					MCDE_OVL0CR_COLCCTRL_DISABLED)) {
			ovly->regs.col_conv = MCDE_OVL0CR_COLCCTRL_DISABLED;
			ovly->chnl->update_color_conversion = true;
			trace_keyvalue(ovly->idx, "1. cc", ovly->regs.col_conv);
		} else if (!ovly_yuv && (ovly->regs.col_conv !=
				MCDE_OVL0CR_COLCCTRL_ENABLED_SAT)) {
			ovly->regs.col_conv = MCDE_OVL0CR_COLCCTRL_ENABLED_SAT;
			ovly->chnl->update_color_conversion = true;
			trace_keyvalue(ovly->idx, "2. cc", ovly->regs.col_conv);
		}
	} else {
		if (!ovly_yuv && (ovly->regs.col_conv !=
					MCDE_OVL0CR_COLCCTRL_DISABLED)) {
			ovly->regs.col_conv = MCDE_OVL0CR_COLCCTRL_DISABLED;
			ovly->regs.dirty = true;
			trace_keyvalue(ovly->idx, "3. cc", ovly->regs.col_conv);
		}
	}
}

static void _mcde_chnl_update_color_conversion(struct mcde_chnl_state *chnl)
{
	struct mcde_ovly_state *ovly0 = chnl->ovly0;
	struct mcde_ovly_state *ovly1 = chnl->ovly1;
	bool ovly0_yuv;
	bool ovly1_yuv;
	bool ovly0_valid;
	bool ovly1_valid;

	/* Never configure the OLED matrix for these cases */
	if (chnl->port.type == MCDE_PORTTYPE_DPI &&
			chnl->port.phy.dpi.tv_mode)
		return;
	if (chnl->port.pixel_format == MCDE_PORTPIXFMT_DSI_YCBCR422)
		return;

	ovly0_valid = ovly0 != NULL &&
		ovly0->regs.enabled &&
		ovly0->paddr != 0;
	ovly0_yuv = ovly0_valid &&
		ovly0->pix_fmt == MCDE_OVLYPIXFMT_YCbCr422;

	ovly1_valid = ovly1 != NULL &&
		ovly1->regs.enabled &&
		ovly1->paddr != 0;
	ovly1_yuv = ovly1_valid &&
		ovly1->pix_fmt == MCDE_OVLYPIXFMT_YCbCr422;

	/* Check oled/color conversion state and setup overlays */

	/*
	 * If one or more overlays are yuv enable the color conversion (
	 * constant set to rgb_2_yuv240) and enable the channels
	 * oled conversion (constant set to to yuv240_2_rgb)
	 *
	 * We prefer to work in yuv240 format (when one overlay is yuv)
	 * since then we can transform yuv240 to full range RGB.
	 */
	if (!chnl->oled_color_conversion && (ovly0_yuv || ovly1_yuv)) {
		chnl->oled_color_conversion = true;
		chnl->regs.oled_enable = true;
		chnl->regs.background_yuv = true;
		chnl->regs.dirty = true;
		chnl->update_color_conversion = true;
		trace_keyvalue((0xF0 | chnl->id), "oled_enable",
			chnl->regs.oled_enable);
	} else if (chnl->oled_color_conversion && !ovly0_yuv && !ovly1_yuv) {
		/* Turn off if no overlay needs YUV conv */
		chnl->oled_color_conversion = false;
		chnl->regs.background_yuv = false;
		chnl->regs.oled_enable = false;
		chnl->regs.dirty = true;
		chnl->update_color_conversion = true;
		trace_keyvalue((0xF0 | chnl->id), "oled_enable",
			chnl->regs.oled_enable);
	}

	if (ovly0_valid)
		mcde_ovly_update_color_conversion(ovly0,
					chnl->oled_color_conversion);

	if (ovly1_valid)
		mcde_ovly_update_color_conversion(ovly1,
					chnl->oled_color_conversion);
}

#define TIME_UPDATE_ONE_MAX	15
#define TIME_UPDATE_CNT		20
#define TIME_UPDATE_ALL_MAX	(TIME_UPDATE_ONE_MAX * TIME_UPDATE_CNT)

static bool is_update_time_long(struct mcde_chnl_state *chnl,
				ktime_t *before, ktime_t *after)
{
	static u32 sumtime;
	static u32 cnt;
	ktime_t diff;
	bool ret = false;

	diff = ktime_sub(*after, *before);
	sumtime += (u32)ktime_to_ms(diff);
	cnt++;
	if (cnt == TIME_UPDATE_CNT) {
		trace_keyvalue(chnl->id, "update_time", sumtime);
		if (sumtime > TIME_UPDATE_ALL_MAX)
			ret = true;
		sumtime = 0;
		cnt = 0;
	}
	return ret;
}

static void chnl_update_overlay(struct mcde_chnl_state *chnl,
						struct mcde_ovly_state *ovly)
{
	if (!ovly || !ovly->inuse)
		return;

	if (ovly->regs.dirty_buf)
		update_overlay_registers_on_the_fly(ovly->idx, &ovly->regs, chnl->regs.ovly_xoffset);

	if (ovly->regs.dirty) {
		update_overlay_registers(ovly, &ovly->regs, &chnl->port,
			chnl->fifo, ovly->stride,
			chnl->vmode.interlaced, chnl->hw_rot);
	}
}

static void stop_channel_if_needed(struct mcde_chnl_state *chnl)
{
	bool ovly0_valid, ovly0_yuv;
	bool ovly1_valid, ovly1_yuv;
	static bool prev_ovly0_yuv = false, prev_ovly1_yuv = false;

	ovly0_valid = chnl->ovly0 != NULL && chnl->ovly0->paddr != 0 &&
			chnl->ovly0->inuse;
	ovly0_yuv = ovly0_valid &&
			chnl->ovly0->pix_fmt == MCDE_OVLYPIXFMT_YCbCr422;

	ovly1_valid = chnl->ovly1 != NULL && chnl->ovly1->paddr != 0 &&
			chnl->ovly1->inuse;
	ovly1_yuv = ovly1_valid &&
			chnl->ovly1->pix_fmt == MCDE_OVLYPIXFMT_YCbCr422;

	if (chnl->state == CHNLSTATE_RUNNING) {
		if ((ovly0_valid && (prev_ovly0_yuv != ovly0_yuv)) ||
				(ovly0_valid && (prev_ovly1_yuv != ovly1_yuv)))
			stop_channel(chnl);
	}

	prev_ovly0_yuv = ovly0_yuv;
	prev_ovly1_yuv = ovly1_yuv;
}

static int _mcde_chnl_update(struct mcde_chnl_state *chnl,
					bool tripple_buffer)
{
	int curr_vcmp_cnt;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	stop_channel_if_needed(chnl);

	if (chnl->force_disable) {
		disable_mcde_hw(true, false);
		chnl->force_disable = false;
	}

	enable_mcde_hw();
	if (!chnl->formatter_updated)
		(void)update_channel_static_registers(chnl);

	update_opp_requirements();

	/* Syncronize updates with panel vsync */
	if (!chnl->port.update_auto_trig || chnl->state != CHNLSTATE_RUNNING) {
		/* Command mode or video mode stopped */
		set_channel_state_sync(chnl, CHNLSTATE_SETUP);
		curr_vcmp_cnt = atomic_read(&chnl->vcmp_cnt);
	} else if (chnl->port.sync_src == MCDE_SYNCSRC_TE0 ||
		   chnl->port.sync_src == MCDE_SYNCSRC_TE1) {
		/* Running video mode with vsync */
		(void)wait_for_vsync(chnl);
		(void)wait_for_vcmp(chnl);
		curr_vcmp_cnt = atomic_read(&chnl->vcmp_cnt);
	} else {
		/* Running video mode without vsync - simulate it */
		wait_for_vcmp(chnl);
		curr_vcmp_cnt = atomic_read(&chnl->vcmp_cnt);
	}
	chnl->vcmp_cnt_wait = curr_vcmp_cnt + 1;

	/* No access of HW before this line */

	chnl_update_overlay(chnl, chnl->ovly0);
	chnl_update_overlay(chnl, chnl->ovly1);

	if ((chnl->update_color_conversion == true)  &&
				(chnl->state == CHNLSTATE_RUNNING)) {
		/* Delay call to mcde_update_non_buffered_color_conversion() */
		chnl->vcmp_cnt_to_change_col_conv = curr_vcmp_cnt + 1;
		chnl->update_color_conversion = false;
		/* Update double buffered color conversion registers */
		mcde_update_double_buffered_color_conversion(chnl);
	}

	if (chnl->port.update_auto_trig)
		chnl_update_continous(chnl);
	else
		chnl_update_non_continous(chnl);

	/* All HW is now updated. HW access is not allowed */

	if (chnl->ovly0->regs.dirty || chnl->ovly0->regs.dirty_buf)
		dev_err(&mcde_dev->dev,
				"Ovly0 registers dirty after update. %d.%d",
				chnl->ovly0->regs.dirty,
				chnl->ovly0->regs.dirty_buf);
	if (chnl->ovly1->regs.dirty || chnl->ovly1->regs.dirty_buf)
		dev_err(&mcde_dev->dev,
				"Ovly1 registers dirty after update. %d.%d",
				chnl->ovly1->regs.dirty,
				chnl->ovly1->regs.dirty_buf);

	dev_vdbg(&mcde_dev->dev, "Channel updated, chnl=%d\n", chnl->id);
	return 0;
}

static int _mcde_chnl_enable(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);
	chnl->enabled = true;
	chnl->first_frame_vsync_fix = true;
	return 0;
}

/* API entry points */
/* MCDE channels */
struct mcde_chnl_state *mcde_chnl_get(enum mcde_chnl chnl_id,
			enum mcde_fifo fifo, const struct mcde_port *port)
{
	struct mcde_chnl_state *chnl;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);
	if (port)
		chnl = _mcde_chnl_get(chnl_id, fifo, port);
	else
		chnl = _mcde_chnl_get_drm(chnl_id);
	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);

	return chnl;
}

int mcde_chnl_set_pixel_format(struct mcde_chnl_state *chnl,
					enum mcde_port_pix_fmt pix_fmt)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (!chnl->reserved)
		return -EINVAL;
	chnl->port.pixel_format = pix_fmt;

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);

	return 0;
}

int mcde_chnl_set_palette(struct mcde_chnl_state *chnl,
					struct mcde_palette_table *palette)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (!chnl->reserved)
		return -EINVAL;
	if (palette != NULL) {
		chnl->map_r = palette->map_col_ch0;
		chnl->map_g = palette->map_col_ch1;
		chnl->map_b = palette->map_col_ch2;
		chnl->palette_enable = true;
	} else {
		chnl->map_r = NULL;
		chnl->map_g = NULL;
		chnl->map_b = NULL;
		chnl->palette_enable = false;
	}

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
	return 0;
}

void mcde_chnl_set_col_convert(struct mcde_chnl_state *chnl,
					struct mcde_col_transform *transform,
					enum   mcde_col_convert    convert)
{
	switch (convert) {
	case MCDE_CONVERT_RGB_2_YCBCR:
		memcpy(&chnl->rgb_2_ycbcr, transform,
				sizeof(struct mcde_col_transform));
		/* force update: */
		if (chnl->transform == &chnl->rgb_2_ycbcr) {
			chnl->transform = NULL;
			chnl->ovly0->dirty = true;
			chnl->ovly1->dirty = true;
		}
		break;
	case MCDE_CONVERT_YCBCR_2_RGB:
		memcpy(&chnl->ycbcr_2_rgb, transform,
				sizeof(struct mcde_col_transform));
		/* force update: */
		if (chnl->transform == &chnl->ycbcr_2_rgb) {
			chnl->transform = NULL;
			chnl->ovly0->dirty = true;
			chnl->ovly1->dirty = true;
		}
		break;
	default:
		/* Trivial transforms are handled internally */
		dev_warn(&mcde_dev->dev,
			"%s: unsupported col convert\n", __func__);
		break;
	}
}

int mcde_chnl_set_video_mode(struct mcde_chnl_state *chnl,
					struct mcde_video_mode *vmode)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (chnl == NULL || vmode == NULL)
		return -EINVAL;

	chnl->vmode = *vmode;
        /* +445681 display padding */
        chnl->vmode.xres += ALIGN(vmode->xres_padding, 0x10);
        chnl->vmode.yres += ALIGN(vmode->yres_padding, 0x10);
        /* -445681 display padding */

	chnl->ovly0->dirty = true;
	if (chnl->ovly1)
		chnl->ovly1->dirty = true;

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);

	return 0;
}
EXPORT_SYMBOL(mcde_chnl_set_video_mode);

int mcde_chnl_set_rotation(struct mcde_chnl_state *chnl,
					enum mcde_hw_rotation hw_rot)
{
	dev_vdbg(&mcde_dev->dev, "%s, hw_rot=%d\n", __func__, hw_rot);

	if (!chnl->reserved)
		return -EINVAL;

	if ((hw_rot == MCDE_HW_ROT_90_CW ||
			hw_rot == MCDE_HW_ROT_90_CCW) &&
			(chnl->id != MCDE_CHNL_A && chnl->id != MCDE_CHNL_B))
		return -EINVAL;

	chnl->hw_rot = hw_rot;

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);

	return 0;
}

int mcde_chnl_set_power_mode(struct mcde_chnl_state *chnl,
				enum mcde_display_power_mode power_mode)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (chnl == NULL)
		return -EINVAL;

	if (!chnl->reserved)
		return -EINVAL;

	chnl->power_mode = power_mode;

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);

	return 0;
}

int mcde_chnl_apply(struct mcde_chnl_state *chnl)
{
	int ret ;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (!chnl->reserved)
		return -EINVAL;

	mcde_lock(__func__, __LINE__);
	ret = _mcde_chnl_apply(chnl);
	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "%s exit with ret %d\n", __func__, ret);
	trace_update(chnl->id, false);

	return ret;
}

int mcde_chnl_update(struct mcde_chnl_state *chnl,
					bool tripple_buffer)
{
	int ret;
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (!chnl->reserved)
		return -EINVAL;

	trace_update(chnl->id, true);

	mcde_lock(__func__, __LINE__);

	ret = _mcde_chnl_update(chnl, tripple_buffer);
	mcde_debugfs_channel_update(chnl->id);
	if (chnl->ovly0)
		mcde_debugfs_overlay_update(chnl->id, chnl->ovly0->idx);
	if (chnl->ovly1)
		mcde_debugfs_overlay_update(chnl->id, chnl->ovly1->idx);

	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "%s exit with ret %d\n", __func__, ret);

	return ret;
}

void mcde_chnl_put(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (chnl->enabled) {
		stop_channel(chnl);
		cancel_delayed_work(&hw_timeout_work);
		disable_mcde_hw(false, true);
		chnl->enabled = false;
	}

	chnl->reserved = false;
	if (chnl->port.type == MCDE_PORTTYPE_DPI) {
		clk_put(chnl->clk_dpi);
		if (chnl->port.phy.dpi.tv_mode) {
			chnl->vcmp_per_field = false;
			chnl->even_vcmp = false;
		}
	} else if (chnl->port.type == MCDE_PORTTYPE_DSI) {
		nova_dsilink_put(chnl->dsilink);
	}

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

void mcde_chnl_stop_flow(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	mcde_lock(__func__, __LINE__);
	if (mcde_is_enabled && chnl->enabled)
		stop_channel(chnl);
	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

void mcde_chnl_enable(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	mcde_lock(__func__, __LINE__);
	_mcde_chnl_enable(chnl);
	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

void mcde_chnl_disable(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	mcde_lock(__func__, __LINE__);
	cancel_delayed_work(&hw_timeout_work);
	/* The channel must be stopped before it is disabled */
	WARN_ON_ONCE(chnl->state == CHNLSTATE_RUNNING);
	disable_mcde_hw(false, true);
	chnl->enabled = false;
	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

void mcde_formatter_enable(struct mcde_chnl_state *chnl)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	mcde_lock(__func__, __LINE__);
	_mcde_chnl_enable(chnl);
	if (enable_mcde_hw()) {
		mcde_unlock(__func__, __LINE__);
		dev_err(&mcde_dev->dev, "%s enable failed\n", __func__);
		return;
	}
	if (!chnl->formatter_updated)
		(void)update_channel_static_registers(chnl);
	mcde_dynamic_power_management = false;
	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "%s exit\n", __func__);
}

/* MCDE overlays */
struct mcde_ovly_state *mcde_ovly_get(struct mcde_chnl_state *chnl)
{
	struct mcde_ovly_state *ovly;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	/* FIXME: Add reserved check once formatter is split from channel */

	if (!chnl->ovly0->inuse)
		ovly = chnl->ovly0;
	else if (chnl->ovly1 && !chnl->ovly1->inuse)
		ovly = chnl->ovly1;
	else
		ovly = ERR_PTR(-EBUSY);

	if (!IS_ERR(ovly)) {
		ovly->inuse = true;
		ovly->paddr = 0;
		ovly->kaddr = 0;
		ovly->stride = 0;
		ovly->pix_fmt = MCDE_OVLYPIXFMT_RGB565;
		ovly->src_x = 0;
		ovly->src_y = 0;
		ovly->dst_x = 0;
		ovly->dst_y = 0;
		ovly->dst_z = 0;
		ovly->w = 0;
		ovly->h = 0;
		ovly->alpha_value = 0xFF;
		ovly->alpha_source = MCDE_OVL1CONF2_BP_PER_PIXEL_ALPHA;
		ovly->dirty = true;
		mcde_ovly_apply(ovly);
	}

	return ovly;
}

void mcde_ovly_put(struct mcde_ovly_state *ovly)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	if (!ovly->inuse)
		return;
	if (ovly->regs.enabled) {
		ovly->paddr = 0;
		ovly->dirty = true;
		mcde_ovly_apply(ovly);/* REVIEW: API call calling API call! */
	}
	ovly->inuse = false;
}

void mcde_ovly_set_source_buf(
	struct mcde_ovly_state *ovly,
	u32 paddr,
	void *kaddr)
{
	if (!ovly->inuse)
		return;

	ovly->dirty = paddr == 0 || ovly->paddr == 0;
	ovly->dirty_buf = true;

	ovly->paddr = paddr;
	ovly->kaddr = kaddr;
}

void mcde_ovly_set_source_info(struct mcde_ovly_state *ovly,
	u32 stride, enum mcde_ovly_pix_fmt pix_fmt)
{
	if (!ovly->inuse)
		return;

	ovly->stride = stride;
	ovly->pix_fmt = pix_fmt;
	ovly->dirty = true;
}

void mcde_ovly_set_source_area(struct mcde_ovly_state *ovly,
	u16 x, u16 y, u16 w, u16 h)
{
	if (!ovly->inuse)
		return;

	ovly->src_x = x;
	ovly->src_y = y;
	ovly->w = w;
	ovly->h = h;
	ovly->dirty = true;
}

void mcde_ovly_set_dest_pos(struct mcde_ovly_state *ovly, u16 x, u16 y, u8 z)
{
	if (!ovly->inuse)
		return;

	ovly->dst_x = x;
	ovly->dst_y = y;
	ovly->dst_z = z;
	ovly->dirty = true;
}

void mcde_ovly_apply(struct mcde_ovly_state *ovly)
{
	if (!ovly->inuse)
		return;

	mcde_lock(__func__, __LINE__);

	if (ovly->dirty || ovly->dirty_buf) {
		ovly->regs.ch_id = ovly->chnl->id;
		ovly->regs.enabled = ovly->paddr != 0;
		ovly->regs.baseaddress0 = ovly->paddr;
		ovly->regs.baseaddress1 =
					ovly->regs.baseaddress0 + ovly->stride;
		ovly->regs.dirty_buf = true;
		ovly->dirty_buf = false;
	}
	if (!ovly->dirty) {
		mcde_unlock(__func__, __LINE__);
		return;
	}

	switch (ovly->pix_fmt) {/* REVIEW: Extract to table */
	case MCDE_OVLYPIXFMT_RGB565:
		ovly->regs.bits_per_pixel = 16;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_RGB565;
		ovly->regs.bgr = false;
		ovly->regs.bebo = false;
		ovly->regs.opq = true;
		break;
	case MCDE_OVLYPIXFMT_RGBA5551:
		ovly->regs.bits_per_pixel = 16;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_IRGB1555;
		ovly->regs.bgr = false;
		ovly->regs.bebo = false;
		ovly->regs.opq = false;
		break;
	case MCDE_OVLYPIXFMT_RGBA4444:
		ovly->regs.bits_per_pixel = 16;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_ARGB4444;
		ovly->regs.bgr = false;
		ovly->regs.bebo = false;
		ovly->regs.opq = false;
		break;
	case MCDE_OVLYPIXFMT_RGB888:
		ovly->regs.bits_per_pixel = 24;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_RGB888;
		ovly->regs.bgr = false;
		ovly->regs.bebo = false;
		ovly->regs.opq = true;
		break;
	case MCDE_OVLYPIXFMT_RGBX8888:
		ovly->regs.bits_per_pixel = 32;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_XRGB8888;
		ovly->regs.bgr = false;
		ovly->regs.bebo = true;
		ovly->regs.opq = true;
		break;
	case MCDE_OVLYPIXFMT_RGBA8888:
		ovly->regs.bits_per_pixel = 32;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_ARGB8888;
		ovly->regs.bgr = false;
		ovly->regs.bebo = false;
		ovly->regs.opq = false;
		break;
	case MCDE_OVLYPIXFMT_YCbCr422:
		ovly->regs.bits_per_pixel = 16;
		ovly->regs.bpp = MCDE_EXTSRC0CONF_BPP_YCBCR422;
		ovly->regs.bgr = false;
		ovly->regs.bebo = false;
		ovly->regs.opq = true;
		break;
	default:
		break;
	}

	ovly->regs.ppl = ovly->w;
	ovly->regs.lpf = ovly->h;
	ovly->regs.cropx = ovly->src_x;
	ovly->regs.cropy = ovly->src_y;
	ovly->regs.xpos = ovly->dst_x;
	ovly->regs.ypos = ovly->dst_y;
	ovly->regs.z = ovly->dst_z > 0; /* 0 or 1 */

	ovly->regs.alpha_source = ovly->alpha_source;
	ovly->regs.alpha_value = ovly->alpha_value;

	ovly->regs.dirty = true;
	ovly->dirty = false;

	chnl_ovly_pixel_format_apply(ovly->chnl, ovly);

	/* Apply channel to reflect any changes in the ovly to the channel */

	/* Sets chnl->oled_color_conversion */
	_mcde_chnl_update_color_conversion(ovly->chnl);

	mcde_ovly_update_color_conversion(ovly,
				ovly->chnl->oled_color_conversion);

	mcde_unlock(__func__, __LINE__);

	dev_vdbg(&mcde_dev->dev, "Overlay applied, idx=%d chnl=%d\n",
						ovly->idx, ovly->chnl->id);
}

static void work_chnl_restart(struct work_struct *ptr)
{
	struct mcde_chnl_state *chnl =
		container_of(ptr, struct mcde_chnl_state, restart_work);

	if (atomic_cmpxchg(&chnl->force_restart, true, false)) {
		mcde_lock(__func__, __LINE__);
		if (chnl->state == CHNLSTATE_RUNNING) {
			disable_mcde_hw(true, false);
			_mcde_chnl_update(chnl, true);
			dev_warn(&mcde_dev->dev, "Forced restart\n");
		}
		mcde_unlock(__func__, __LINE__);
	}
}

static void work_sleep_function(struct work_struct *ptr)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);
	if (mcde_trylock(__func__, __LINE__)) {
		if (mcde_dynamic_power_management)
			disable_mcde_hw(false, false);
		mcde_unlock(__func__, __LINE__);
	}
}

static int init_clocks_and_power(struct platform_device *pdev)
{
	int ret = 0;
	struct mcde_platform_data *pdata = pdev->dev.platform_data;

	if (pdata->regulator_mcde_epod_id) {
		regulator_mcde_epod = regulator_get(&pdev->dev,
				pdata->regulator_mcde_epod_id);
		if (IS_ERR(regulator_mcde_epod)) {
			ret = PTR_ERR(regulator_mcde_epod);
			dev_warn(&pdev->dev,
				"%s: Failed to get regulator '%s'\n",
				__func__, pdata->regulator_mcde_epod_id);
			regulator_mcde_epod = NULL;
			return ret;
		}
	} else {
		dev_warn(&pdev->dev, "%s: No mcde regulator id supplied\n",
								__func__);
		return -EINVAL;
	}

	if (pdata->regulator_esram_epod_id) {
		regulator_esram_epod = regulator_get(&pdev->dev,
				pdata->regulator_esram_epod_id);
		if (IS_ERR(regulator_esram_epod)) {
			ret = PTR_ERR(regulator_esram_epod);
			dev_warn(&pdev->dev,
				"%s: Failed to get regulator '%s'\n",
				__func__, pdata->regulator_esram_epod_id);
			regulator_esram_epod = NULL;
			goto regulator_esram_err;
		}
	} else {
		dev_warn(&pdev->dev, "%s: No esram regulator id supplied\n",
								__func__);
	}

	if (pdata->regulator_vana_id) {
		regulator_vana = regulator_get(&pdev->dev,
				pdata->regulator_vana_id);
		if (IS_ERR(regulator_vana)) {
			ret = PTR_ERR(regulator_vana);
			dev_warn(&pdev->dev,
				"%s: Failed to get regulator '%s'\n",
				__func__, pdata->regulator_vana_id);
			regulator_vana = NULL;
			goto regulator_vana_err;
		}
	} else {
		dev_dbg(&pdev->dev, "%s: No vana regulator id supplied\n",
								__func__);
	}

	clock_mcde = clk_get(&pdev->dev, CLK_MCDE);
	if (IS_ERR(clock_mcde)) {
		ret = PTR_ERR(clock_mcde);
		dev_warn(&pdev->dev, "%s: Failed to get mcde_clk\n", __func__);
		goto clk_mcde_err;
	}

	return ret;

clk_mcde_err:
	if (regulator_vana)
		regulator_put(regulator_vana);
regulator_vana_err:
	if (regulator_esram_epod)
		regulator_put(regulator_esram_epod);
regulator_esram_err:
	regulator_put(regulator_mcde_epod);
	return ret;
}

static void remove_clocks_and_power(struct platform_device *pdev)
{
	/* REVIEW: Release only if exist */
	/* REVIEW: Remove make sure MCDE is done */
	clk_put(clock_mcde);
	if (regulator_vana)
		regulator_put(regulator_vana);
	regulator_put(regulator_mcde_epod);
	regulator_put(regulator_esram_epod);
}

static int probe_hw(struct platform_device *pdev)
{
	int i;
	int ret;
	u32 pid;

	dev_info(&mcde_dev->dev, "Probe HW\n");

	/* Get MCDE HW version */
	/* don't turn on power if FLOEN is set, and channel A is used.*/
	if (!mcde_rfld(MCDE_CRA0, FLOEN)){
		regulator_enable(regulator_mcde_epod);
		clk_enable(clock_mcde);
	}
	pid = mcde_rreg(MCDE_PID);

	dev_info(&mcde_dev->dev, "MCDE HW revision 0x%.8X\n", pid);

	/* don't turn off power if FLOEN is set, and channel A is used.*/
	if (!mcde_rfld(MCDE_CRA0, FLOEN)){
		clk_disable(clock_mcde);
		regulator_disable(regulator_mcde_epod);
	}

	switch (pid) {
	case MCDE_VERSION_3_0_8:
		num_channels = 4;
		num_overlays = 6;
		dsi_ifc_is_supported = true;
		input_fifo_size = 128;
		output_fifo_ab_size = 640;
		output_fifo_c0c1_size = 160;
		dsi_use_clk_framework = true;
		hw_alignment = 8;
		dev_info(&mcde_dev->dev, "db8500 V2 HW\n");
		break;
	case MCDE_VERSION_4_0_4:
		num_channels = 2;
		num_overlays = 3;
		input_fifo_size = 80;
		output_fifo_ab_size = 320;
		dsi_ifc_is_supported = false;
		dsi_use_clk_framework = false;
		hw_alignment = 8;
		dev_info(&mcde_dev->dev, "db5500 V2 HW\n");
		break;
	case MCDE_VERSION_4_1_3:
		num_channels = 4;
		num_overlays = 6;
		dsi_ifc_is_supported = true;
		input_fifo_size = 192;
		output_fifo_ab_size = 640;
		output_fifo_c0c1_size = 160;
		dsi_use_clk_framework = true;
		hw_alignment = 64;
		dev_info(&mcde_dev->dev, "db9540 V1 HW\n");
		break;
	case MCDE_VERSION_3_0_5:
		/* Intentional */
	case MCDE_VERSION_1_0_4:
		/* Intentional */
	default:
		dev_err(&mcde_dev->dev, "Unsupported HW version\n");
		ret = -ENOTSUPP;
		goto unsupported_hw;
		break;
	}

	channels = kzalloc(num_channels * sizeof(struct mcde_chnl_state),
								GFP_KERNEL);
	if (!channels) {
		ret = -ENOMEM;
		goto failed_channels_alloc;
	}

	overlays = kzalloc(num_overlays * sizeof(struct mcde_ovly_state),
								GFP_KERNEL);
	if (!overlays) {
		ret = -ENOMEM;
		goto failed_overlays_alloc;
	}

	/* Init MCDE */
	for (i = 0; i < num_overlays; i++) {
		overlays[i].idx = i;
		overlays[i].kaddr = 0;
	}

	channels[0].ovly0 = &overlays[0];
	channels[0].ovly1 = &overlays[1];
	channels[1].ovly0 = &overlays[2];

	if (pid == MCDE_VERSION_3_0_8 || MCDE_VERSION_4_1_3) {
		channels[1].ovly1 = &overlays[3];
		channels[2].ovly0 = &overlays[4];
		channels[3].ovly0 = &overlays[5];
	}

	mcde_debugfs_create(&mcde_dev->dev);
	for (i = 0; i < num_channels; i++) {
		channels[i].id = i;

		channels[i].ovly0->chnl = &channels[i];
		if (channels[i].ovly1)
			channels[i].ovly1->chnl = &channels[i];

		init_waitqueue_head(&channels[i].state_waitq);
		init_waitqueue_head(&channels[i].vcmp_waitq);
		init_waitqueue_head(&channels[i].vsync_waitq);

		mcde_debugfs_channel_create(i, &channels[i]);
		mcde_debugfs_overlay_create(i, 0, channels[i].ovly0);
		if (channels[i].ovly1)
			mcde_debugfs_overlay_create(i, 1, channels[i].ovly1);
		channels[i].dsilink = NULL;
		INIT_WORK(&channels[i].restart_work, work_chnl_restart);
	}

	mcde_clk_rate = clk_get_rate(clock_mcde);
	dev_info(&mcde_dev->dev, "MCDE_CLK is %d Hz\n", mcde_clk_rate);

	return 0;

failed_overlays_alloc:
	kfree(channels);
	channels = NULL;
unsupported_hw:
failed_channels_alloc:
	num_channels = 0;
	num_overlays = 0;
	return ret;
}

u8 mcde_get_hw_alignment()
{
	return hw_alignment;
}
EXPORT_SYMBOL(mcde_get_hw_alignment);

static int __devinit mcde_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct mcde_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_dbg(&pdev->dev, "No platform data\n");
		return -EINVAL;
	}

	mcde_dev = pdev;

	/* Hook up irq */
	mcde_irq = platform_get_irq(pdev, 0);
	if (mcde_irq <= 0) {
		dev_dbg(&pdev->dev, "No irq defined\n");
		ret = -EINVAL;
		goto failed_irq_get;
	}

	/* Map I/O */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_dbg(&pdev->dev, "No MCDE io defined\n");
		ret = -EINVAL;
		goto failed_get_mcde_io;
	}
	mcdeio = ioremap(res->start, res->end - res->start + 1);
	if (!mcdeio) {
		dev_dbg(&pdev->dev, "MCDE iomap failed\n");
		ret = -EINVAL;
		goto failed_map_mcde_io;
	}
	dev_info(&pdev->dev, "MCDE iomap: 0x%.8X->0x%.8X\n",
		(u32)res->start, (u32)mcdeio);

	ret = init_clocks_and_power(pdev);
	if (ret < 0) {
		dev_warn(&pdev->dev, "%s: init_clocks_and_power failed\n"
					, __func__);
		goto failed_init_clocks;
	}

	INIT_DELAYED_WORK_DEFERRABLE(&hw_timeout_work, work_sleep_function);

	WARN_ON(prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
		dev_name(&pdev->dev), PRCMU_QOS_MAX_VALUE));
	WARN_ON(prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
		dev_name(&pdev->dev), PRCMU_QOS_DEFAULT_VALUE));
	ret = probe_hw(pdev);
	if (ret)
		goto failed_probe_hw;

	/*
		It is important to avoid the mcde disable (link to this timeout)
		to preserve the boot DPI splash screen until the 1st channel_update.
	*/
	ret = enable_mcde_hw_pre()/*enable_mcde_hw()*/;
	if (ret)
		goto failed_mcde_enable;

	return 0;

failed_mcde_enable:
failed_probe_hw:
	remove_clocks_and_power(pdev);
failed_init_clocks:
	iounmap(mcdeio);
failed_map_mcde_io:
failed_get_mcde_io:
failed_irq_get:
	return ret;
}

static int __devexit mcde_remove(struct platform_device *pdev)
{
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
		dev_name(&pdev->dev));
	prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP,
		dev_name(&pdev->dev));
	remove_clocks_and_power(pdev);
	return 0;
}

#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
static int mcde_resume(struct platform_device *pdev)
{
	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	mcde_lock(__func__, __LINE__);

	if (enable_mcde_hw()) {
		mcde_unlock(__func__, __LINE__);
		return -EINVAL;
	}

	mcde_unlock(__func__, __LINE__);

	return 0;
}

static int mcde_suspend(struct platform_device *pdev, pm_message_t state)
{
	int ret;

	dev_vdbg(&mcde_dev->dev, "%s\n", __func__);

	mcde_lock(__func__, __LINE__);

	cancel_delayed_work(&hw_timeout_work);

	if (!mcde_is_enabled) {
		mcde_unlock(__func__, __LINE__);
		return 0;
	}
	disable_mcde_hw(true, true);

	mcde_unlock(__func__, __LINE__);

	return ret;
}
#endif

static struct platform_driver mcde_driver = {
	.probe = mcde_probe,
	.remove = mcde_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = mcde_suspend,
	.resume = mcde_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.driver = {
		.name	= "mcde",
	},
};

int __init mcde_init(void)
{
	mutex_init(&mcde_hw_lock);
	return platform_driver_register(&mcde_driver);
}

void mcde_exit(void)
{
	/* REVIEW: shutdown MCDE? */
	platform_driver_unregister(&mcde_driver);
}
