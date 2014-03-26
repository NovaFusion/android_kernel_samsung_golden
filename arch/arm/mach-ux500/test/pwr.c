/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * This is a module test for clocks and regulators.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/db8500-prcmu.h>
#include <linux/wakelock.h>

#include <mach/hardware.h>
#include <mach/pm.h>

#include "../pm/suspend_dbg.h"
#include "../../../drivers/regulator/dbx500-prcmu.h"
#include "../../../drivers/regulator/ab8500-debug.h"

/* To reach main_wake_lock */
#include "../../../../kernel/power/power.h"

#define PRCC_PCKSR		0x010
#define PRCC_KCKSR		0x014

#define PRCM_PLLSOC0_ENABLE	0x090
#define PRCM_PLLSOC1_ENABLE	0x094
#define PRCM_PLL32K_ENABLE	0x10C
#define PRCM_PLLARM_ENABLE	0x098
#define PRCM_PLLDDR_ENABLE	0x09C
#define PRCM_RNG_ENABLE		0x50C
#define PRCM_PLLDSI_ENABLE	0x504

#define PRCM_CLKOCR		0x1CC

#define PRCM_ARMCLKFIX_MGT	0x000
#define PRCM_ACLK_MGT		0x004
#define PRCM_SGACLK_MGT		0x014
#define PRCM_UARTCLK_MGT	0x018
#define PRCM_MSP02CLK_MGT	0x01C
#define PRCM_MSP1CLK_MGT	0x288
#define PRCM_I2CCLK_MGT		0x020
#define PRCM_SDMMCCLK_MGT	0x024
#define PRCM_SLIMCLK_MGT	0x028
#define PRCM_PER1CLK_MGT	0x02C
#define PRCM_PER2CLK_MGT	0x030
#define PRCM_PER3CLK_MGT	0x034
#define PRCM_PER5CLK_MGT	0x038
#define PRCM_PER6CLK_MGT	0x03C
#define PRCM_PER7CLK_MGT	0x040
#define PRCM_LCDCLK_MGT		0x044
#define PRCM_BMLCLK_MGT		0x04C
#define PRCM_HSITXCLK_MGT	0x050
#define PRCM_HSIRXCLK_MGT	0x054
#define PRCM_HDMICLK_MGT	0x058
#define PRCM_APEATCLK_MGT	0x05C
#define PRCM_APETRACECLK_MGT	0x060
#define PRCM_MCDECLK_MGT	0x064
#define PRCM_IPI2CCLK_MGT	0x068
#define PRCM_DSIALTCLK_MGT	0x06C
#define PRCM_SPARE2CLK_MGT	0x070
#define PRCM_SPARE1CLK_MGT	0x048
#define PRCM_DMACLK_MGT		0x074
#define PRCM_B2R2CLK_MGT	0x078
#define PRCM_TVCLK_MGT		0x07C
#define PRCM_SSPCLK_MGT		0x280
#define PRCM_RNGCLK_MGT		0x284
#define PRCM_UICCCLK_MGT	0x27C

#define PRCM_DSITVCLK_DIV	0x52C
#define PRCM_DSI_PLLOUT_SEL	0x530

#define PRCM_YYCLKEN0_MGT_VAL	0x520

enum acc_type {
	RAW = 0,
	CLK_PRCMU,
	CLK_PAR_PRCMU,
	CLK_ABB_SYS,
	REG_DB8500,
	REG_AB8500,
};

struct pwr_test {
	enum acc_type type;
	u32 base;
	u32 off;
	u32 mask;
	u32 val;

	u32 par_off;
	u32 par_mask;
	u32 par_val;

	char *txt;
	char *txt2;

	/* For AB8500 */
	enum ab8500_regulator_mode mode;
	enum ab8500_regulator_hwmode hwmode;
	enum hwmode_auto hwmode_auto[4];
	int volt_selected;
	int alt_volt_selected;
	int volt_len;
	int volt[4];
	int alt_volt[4];
};

#define RAW_TEST(_base, _off, _mask, _val) \
	{			\
		.type = RAW,	\
		.base = _base,	\
		.off = _off,	\
		.mask = _mask,	\
		.val = _val,	\
		.txt = #_base,	\
		.txt2 = #_off	\
	 }

#define CLK_TEST_PAR_PRCMU(_base, _off, _mask, _val, _par_off, \
			   _par_mask, _par_val)		       \
	{			\
		.type = CLK_PAR_PRCMU,	\
		.base = _base,	\
		.off = _off,	\
		.mask = _mask,	\
		.val = _val,	\
		.par_off = _par_off,	\
		.par_mask = _par_mask,	\
		.par_val = _par_val,	\
		.txt = #_base,	\
		.txt2 = #_off	\
	 }

#define CLK_TEST_PRCMU(_off, _mask, _val) \
	{				\
		.type = CLK_PRCMU,	\
		.off = _off,		\
		.mask = _mask,		\
		.val = _val,		\
		.txt = #_off,		\
	 }

#define CLK_TEST_ABB_SYS(_off, _mask, _val) \
	{			\
		.type = CLK_ABB_SYS,	\
		.off = _off,	\
		.mask = _mask,	\
		.val = _val,	\
		.txt = #_off,	\
	 }
#define REG_TEST_DB8500(_reg, _val) \
	{				\
		.type = REG_DB8500,	\
		.off = _reg,		\
		.val = _val,		\
	}


static struct u8500_regulators
{
	struct dbx500_regulator_info *db8500_reg;
	int db8500_num;
} u8500_reg;

/*
 * Idle - Note: this is not suspend.
 * This test shall pass when the screen is black, before
 * suspend is executed.
 */

static struct pwr_test idle_test[] = {

	/* Test all periph kernel and bus clocks */
	/* bus: gpioctrl */
	CLK_TEST_PAR_PRCMU(U8500_CLKRST1_BASE, PRCC_PCKSR, 0x07ff, BIT(9),
			   PRCM_PER1CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PAR_PRCMU(U8500_CLKRST1_BASE, PRCC_KCKSR, 0x037f, 0x0000,
			   PRCM_PER1CLK_MGT,	BIT(8),  BIT(8)),
	/* bus: gpioctrl */
	CLK_TEST_PAR_PRCMU(U8500_CLKRST2_BASE, PRCC_PCKSR, 0x0fff, BIT(11),
			   PRCM_PER2CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PAR_PRCMU(U8500_CLKRST2_BASE, PRCC_KCKSR, 0x00ff, 0x0000,
			   PRCM_PER2CLK_MGT,	BIT(8),  BIT(8)),
	/* bus: uart2, gpioctrl - users??, kernel: uart2 */
	CLK_TEST_PAR_PRCMU(U8500_CLKRST3_BASE, PRCC_PCKSR, 0x01ff, BIT(8) | BIT(6),
			   PRCM_PER3CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PAR_PRCMU(U8500_CLKRST3_BASE, PRCC_KCKSR, 0x00fe, BIT(6),
			   PRCM_PER3CLK_MGT,	BIT(8),  BIT(8)),
	/* No user on periph5 */
	CLK_TEST_PAR_PRCMU(U8500_CLKRST5_BASE, PRCC_PCKSR, 0x0003, 0x0000,
			   PRCM_PER5CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PAR_PRCMU(U8500_CLKRST5_BASE, PRCC_KCKSR, 0x0000, 0x0000,
			   PRCM_PER5CLK_MGT,	BIT(8),  BIT(8)),
	/* bus: MTU0 used for scheduling ApIdle */
	CLK_TEST_PAR_PRCMU(U8500_CLKRST6_BASE, PRCC_PCKSR, 0x00ff, BIT(6),
			   PRCM_PER6CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PAR_PRCMU(U8500_CLKRST6_BASE, PRCC_KCKSR, 0x0001, 0x0000,
			   PRCM_PER6CLK_MGT,	BIT(8),  BIT(8)),

	/* Test that clkout 1/2 is off */
	CLK_TEST_PRCMU(PRCM_CLKOCR, 0x3f003f, 0x0000),

	/* Test that ulp clock is off */
	CLK_TEST_ABB_SYS(AB8500_SYSULPCLKCTRL1,  0xfc,  0x00),

	/* Test that prcm clks are in proper state */
	CLK_TEST_PRCMU(PRCM_ARMCLKFIX_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_ACLK_MGT,		BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_SGACLK_MGT,		BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_UARTCLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_MSP02CLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_MSP1CLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_I2CCLK_MGT,		BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_SDMMCCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_SLIMCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_PER1CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_PER2CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_PER3CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_PER5CLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_PER6CLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_PER7CLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_LCDCLK_MGT,		BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_BMLCLK_MGT,		BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_HSITXCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_HSIRXCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_HDMICLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_APEATCLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_APETRACECLK_MGT,	BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_MCDECLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_IPI2CCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_DSIALTCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_DMACLK_MGT,		BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_B2R2CLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_TVCLK_MGT,		BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_SSPCLK_MGT,		BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_RNGCLK_MGT,		BIT(8),  BIT(8)),
	CLK_TEST_PRCMU(PRCM_UICCCLK_MGT,	BIT(8),  0x000),
	CLK_TEST_PRCMU(PRCM_DSITVCLK_DIV,	BIT(24) | BIT(25) | BIT(26),
		       0x000),
	CLK_TEST_PRCMU(PRCM_DSI_PLLOUT_SEL,	(BIT(0) | BIT(1) | BIT(2) |
						 BIT(8) | BIT(9) | BIT(10)),
		       0x200), /* Will change */
	CLK_TEST_PRCMU(PRCM_YYCLKEN0_MGT_VAL,	0xBFFFFFFF,
		       (BIT(27) | BIT(23) | BIT(22) | BIT(15) |
			BIT(13) | BIT(12) | BIT(11) | BIT(5) |
			BIT(1) | BIT(0))),

	/* Check db8500 regulator settings  - enable */
	REG_TEST_DB8500(DB8500_REGULATOR_VAPE,			0),
	REG_TEST_DB8500(DB8500_REGULATOR_VARM,			0),
	REG_TEST_DB8500(DB8500_REGULATOR_VMODEM,		0),
	REG_TEST_DB8500(DB8500_REGULATOR_VPLL,			0),
	REG_TEST_DB8500(DB8500_REGULATOR_VSMPS1,		0),
	REG_TEST_DB8500(DB8500_REGULATOR_VSMPS2,		1),
	REG_TEST_DB8500(DB8500_REGULATOR_VSMPS3,		0),
	REG_TEST_DB8500(DB8500_REGULATOR_VRF1,			0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SVAMMDSP,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SVAMMDSPRET,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SVAPIPE,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SIAMMDSP,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SIAMMDSPRET,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SIAPIPE,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_SGA,		0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_B2R2_MCDE,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_ESRAM12,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_ESRAM12RET,	0),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_ESRAM34,	1),
	REG_TEST_DB8500(DB8500_REGULATOR_SWITCH_ESRAM34RET,	0),

	/* ab8500 regulators */
	{
		.type = REG_AB8500,
		.off  = AB8500_VARM,
		.mode  = AB8500_MODE_ON,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_INVAL, HWM_INVAL, HWM_OFF},
		.volt_selected = 2,
		/* Voltage and voltage selection depends on ARM_OPP */
		.alt_volt_selected = 1,
		.volt_len = 3,
		.volt = {1350000, 1025000,  750000},
		.alt_volt = {1250000, 1025000,  750000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VBBP,
		.mode  = AB8500_MODE_ON,
		.hwmode_auto = {HWM_OFF, HWM_INVAL, HWM_INVAL, HWM_INVAL},
		.volt_selected = 1,
		.volt_len = 2,
		.volt = {-300000, 0},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VBBN,
		.mode  = AB8500_MODE_ON,
		.hwmode_auto = {HWM_OFF, HWM_INVAL, HWM_INVAL, HWM_INVAL},
		.volt_selected = 1,
		.volt_len = 2,
		.volt = {300000, 0},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VAPE,
		.mode  = AB8500_MODE_ON,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_INVAL, HWM_INVAL, HWM_OFF},
		.volt_selected = 2,
		/* APE_OPP can get changes by cpufreq */
		.alt_volt_selected = 1,
		.volt_len = 3,
		.volt = {1225000, 1000000, 1200000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VSMPS1,
		.mode  = AB8500_MODE_HW,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_ON, HWM_OFF, HWM_OFF, HWM_OFF},
		.volt_selected = 2,
		.volt_len = 3,
		.volt = {1200000, 1200000, 1200000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VSMPS2,
		.mode  = AB8500_MODE_HW,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_ON, HWM_ON, HWM_ON, HWM_OFF},
		.volt_selected = 2,
		.volt_len = 3,
		.volt = {1800000, 1800000, 1800000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VSMPS3,
		.mode  = AB8500_MODE_HW,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_ON, HWM_OFF, HWM_OFF, HWM_OFF},
		.volt_selected = 2,
		.volt_len = 3,
		.volt = {925000, 1212500, 1200000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VPLL,
		.mode  = AB8500_MODE_HW,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_ON, HWM_OFF, HWM_OFF, HWM_OFF},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VREFDDR,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VMOD,
		.mode  = AB8500_MODE_OFF,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
		.volt_selected = 2,
		.volt_len = 2,
		.volt = {1125000, 1025000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VEXTSUPPLY1,
		.mode  = AB8500_MODE_LP,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VEXTSUPPLY2,
		.mode  = AB8500_MODE_OFF,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VEXTSUPPLY3,
		.mode  = AB8500_MODE_ON,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_ON, HWM_OFF, HWM_ON, HWM_OFF},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VRF1,
		.mode  = AB8500_MODE_HW,
		.volt_len = 1,
		.volt = {2150000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VANA,
		.mode  = AB8500_MODE_OFF,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VAUX1,
		.mode  = AB8500_MODE_ON,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
		.volt_len = 1,
		.volt = {2800000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VAUX2,
		.mode  = AB8500_MODE_ON,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
		.volt_len = 1,
		.volt = {3300000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VAUX3,
		.mode  = AB8500_MODE_OFF,
		.hwmode = AB8500_HWMODE_HPLP,
		.hwmode_auto = {HWM_OFF, HWM_OFF, HWM_OFF, HWM_OFF},
		.volt_len = 1,
		.volt = {2910000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VINTCORE,
		.mode  = AB8500_MODE_OFF,
		.volt_len = 1,
		.volt = {1250000},
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VTVOUT,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VAUDIO,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VANAMIC1,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VANAMIC2,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VDMIC,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VUSB,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VOTG,
		.mode  = AB8500_MODE_OFF,
	},
	{
		.type = REG_AB8500,
		.off  = AB8500_VBUSBIS,
		.mode  = AB8500_MODE_OFF,
	},
};

static int read_raw(struct pwr_test *t)
{
	u32 res;
	int i;

	for (i = 0 ; i < 50; i++) {
		res = readl(__io_address(t->base) + t->off);
		if ((res & t->mask) == t->val)
			return 0;
		msleep(10);
	}

	pr_err("pwr_test: ERROR: %s %s 0x%x, 0x%x reads 0x%x (masked: 0x%x), "
	       "expected 0x%x (mask: 0x%x)\n",
	       t->txt, t->txt2, t->base, t->off, res,
	       res & t->mask, t->val, t->mask);
	return -EINVAL;
}

static int clk_read_par_prcmu(struct pwr_test *t)
 {
	u32 res;
	int i;
	bool parent_enabled = false;

	/* check if parent clock is in expected configuration (usually PRCC) */
	for (i = 0 ; i < 50; i++) {
		res = prcmu_read(t->par_off);
		if ((res & t->par_mask) == t->par_val) {
			parent_enabled = true;
			break;
		}
		msleep(10);
	}

	/* If parent clock is off, but clock is expected to be on --> fail */
	if (!parent_enabled && t->val != 0) {
		pr_err("pwr_test: ERROR:  PRCMU parent clk of %s, "
		       "0x%x reads 0x%x (masked: 0x%x), "
		       "expected to be on 0x%x (mask: 0x%x)\n",
		       t->txt, t->par_off, res, res & t->par_mask,
		       t->par_val, t->par_mask);
		return -EINVAL;
	}

	/* It's ok if parent clock is off and clock is off as well */
	if (!parent_enabled && t->val == 0)
		return 0;

	return read_raw(t);
}

static int clk_read_prcmu(struct pwr_test *t)
{
	u32 res;
	int i;

	for (i = 0 ; i < 50; i++) {
		res = prcmu_read(t->off);
		if ((res & t->mask) == t->val)
			return 0;
		msleep(10);
	}

	pr_err("pwr_test: ERROR: %s PRCMU, 0x%x reads 0x%x (masked: 0x%x), "
	       "expected 0x%x (mask: 0x%x)\n",
	       t->txt, t->off, res, res & t->mask, t->val, t->mask);
	return -EINVAL;
}

static int clk_read_abb_sys(struct pwr_test *t)
{
	int ret;
	u8 val = 0;
	int i;

	for (i = 0; i < 50; i++) {
		ret = ab8500_sysctrl_read(t->off, &val);
		if (ret < 0) {
			pr_err("pwr_test: AB8500 access error: %d of "
			       "reg: 0x%x\n", ret, t->off);
			return ret;
		}
		if ((val & (u8)t->mask) == t->val)
			return 0;
		msleep(10);
	}

	pr_err("pwr_test: ERROR: AB8500 register 0x%x %s, reads 0x%x,"
	       " (masked 0x%x) expected 0x%x (mask: 0x%x)\n",
	       t->base, t->txt, val, val & (u8)t->mask, t->val, t->mask);

	return -EINVAL;
}

static int reg_read_db8500(struct pwr_test *t)
{
	int i, j;

	for (j = 0 ; j < u8500_reg.db8500_num ; j++)
		if (u8500_reg.db8500_reg[j].desc.id == t->off)
			break;

	if (j == u8500_reg.db8500_num) {
		pr_err("pwr_test: Invalid db8500 regulator\n");
		return -EINVAL;
	}

	for (i = 0; i < 50; i++) {

		/* powerstate is a special case */
		if (t->off == DB8500_REGULATOR_VAPE) {
			if ((u8500_reg.db8500_reg[j].is_enabled ||
			     power_state_active_is_enabled()) == t->val)
				return 0;
		} else {
			if (u8500_reg.db8500_reg[j].is_enabled == t->val)
				return 0;
		}
		msleep(10);
	}

	pr_err("pwr_test: ERROR: DB8500 regulator %s is 0x%x expected 0x%x\n",
	       u8500_reg.db8500_reg[j].desc.name,
	       t->off != DB8500_REGULATOR_VAPE ?
	       u8500_reg.db8500_reg[j].is_enabled :
		u8500_reg.db8500_reg[j].is_enabled ||
	       power_state_active_is_enabled(),
	       t->val);

	return -EINVAL;
}

static int reg_read_ab8500(struct pwr_test *t)
{
	int i, j;
	int ret;
	struct ab8500_debug_regulator_status s;

	for (i = 0; i < 50; i++) {
		ret = ab8500_regulator_debug_read(t->off, &s);

		if (ret) {
			pr_err("pwr_test: Fail to read ab8500 regulator\n");
			return -EINVAL;
		}

		if (t->mode != s.mode)
			goto repeat;

		if (t->hwmode != s.hwmode)
			goto repeat;

		if (t->volt_selected != s.volt_selected &&
		    t->alt_volt_selected != s.volt_selected)
			goto repeat;

		if (t->volt_len != s.volt_len)
			goto repeat;

		for (j = 0; j < 4 && s.hwmode != AB8500_HWMODE_NONE; j++)
			if (s.hwmode_auto[j] != t->hwmode_auto[j])
				goto repeat;

		for (j = 0; j < s.volt_len; j++)
			if (s.volt[j] != t->volt[j] &&
			    s.volt[j] != t->alt_volt[j])
				goto repeat;
		return 0;

repeat:
		msleep(10);
	}

	pr_err("pwr test: ERROR: AB8500 regulator %s ", s.name);

	if (t->mode != s.mode)
		printk("mode is: %d expected: %d ",
		       s.mode, t->mode);

	if (t->hwmode != s.hwmode)
		printk("hwmode is: %d expected: %d ",
		       s.hwmode, t->hwmode);

	if (t->volt_selected != s.volt_selected &&
	    t->alt_volt_selected != s.volt_selected) {
		printk("volt selected is: %d expected: %d ",
		       s.volt_selected, t->volt_selected);
		if (t->alt_volt_selected)
			printk("(alt volt: %d) ", t->alt_volt_selected);
	}

	if (t->volt_len != s.volt_len)
		printk("volt len is: %d expected: %d ",
		       s.volt_len, t->volt_len);

	for (j = 0; j < 4; j++)
		if (s.hwmode_auto[j] != t->hwmode_auto[j])
			break;
	if (j != 4 && s.hwmode != AB8500_HWMODE_NONE) {

		printk("hwmode auto:: {");
		for (j = 0; j < 4; j++) {
			switch(s.hwmode_auto[j]) {
			case HWM_OFF: { printk("OFF "); break; }
			case HWM_ON: { printk("ON  "); break; }
			case HWM_INVAL: { printk("INVAL "); break; }
			}
		}
		printk("}, expected: {");
		for (j = 0; j < 4; j++) {
			switch(t->hwmode_auto[j]) {
			case HWM_OFF: { printk("OFF "); break; }
			case HWM_ON: { printk("ON  "); break; }
			case HWM_INVAL: { printk("INVAL "); break; }
			}
		}
		printk("} ");
	}

	if (s.volt_len == t->volt_len) {
		for (j = 0; j < s.volt_len; j++)
			if (s.volt[j] != t->volt[j] &&
			    t->alt_volt[j] != s.volt[j])
				break;

	} else {
		j = 0;
	}

	if (j != s.volt_len) {
		printk("voltage: {");
		for (j = 0; j < s.volt_len; j++)
			printk("%d ", s.volt[j]);

		if (t->alt_volt) {
			printk("} alt voltage: {");
			for (j = 0; j < s.volt_len; j++)
				printk("%d ", t->alt_volt[j]);
		}
		printk("} expected: {");
		for (j = 0; j < t->volt_len; j++)
			printk("%d ", t->volt[j]);
		printk("} ");
	}

	printk("\n");

	return -EINVAL;
}


static int test_execute(struct pwr_test *t, int len)
{
	int err = 0;
	int i;

	for (i = 0 ; i < len ; i++) {
		switch (t[i].type) {
		case CLK_ABB_SYS:
			err |= clk_read_abb_sys(&t[i]);
			break;
		case CLK_PRCMU:
			err |= clk_read_prcmu(&t[i]);
			break;
		case CLK_PAR_PRCMU:
			err |= clk_read_par_prcmu(&t[i]);
			break;
		case RAW:
			err |= read_raw(&t[i]);
			break;
		case REG_DB8500:
			err |= reg_read_db8500(&t[i]);
			break;
		case REG_AB8500:
			err |= reg_read_ab8500(&t[i]);
			break;
		default:
			break;
		}

	}
	return err;
}

static int pwr_test_idle(struct seq_file *s, void *data)
{
	int err;

	err = test_execute(idle_test, ARRAY_SIZE(idle_test));

	seq_printf(s, "%s\n", err == 0 ? "PASS" : "FAIL");

	return 0;
}

static bool suspend_testing;
static int suspend_test_length;

static ssize_t pm_test_suspend_set(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	long unsigned val;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &val);

	if (err)
		return err;

	suspend_test_length = (int)val;

	ux500_suspend_dbg_test_start(suspend_test_length);

	suspend_testing = true;
#ifdef CONFIG_WAKELOCK
	wake_unlock(&main_wake_lock);
#endif

	pr_info("Will do suspend %d times.\n", suspend_test_length);
	return count;
}

static int pwr_test_suspend_status(struct seq_file *s, void *data)
{
	bool ongoing = true;
	bool success;

	if (!suspend_testing) {
		pr_info("Suspend test not started\n");
		seq_printf(s, "FAIL\n");
		return 0;
	}

	success = ux500_suspend_test_success(&ongoing);

	seq_printf(s,
		   "%s\n", ongoing ? "ONGOING" : (success ? "PASS" : "FAIL"));

	return 0;
}

int dbx500_regulator_testcase(struct dbx500_regulator_info *regulator_info,
			      int num_regulators)
{
	u8500_reg.db8500_reg = regulator_info;
	u8500_reg.db8500_num = num_regulators;
	return 0;
}

static int pwr_test_debugfs_open(struct inode *inode,
				 struct file *file)
{
	return single_open(file,
			   inode->i_private,
			   NULL);
}

static int pwr_test_suspend_debugfs_open(struct inode *inode,
				 struct file *file)
{
	return single_open(file,
			   pwr_test_suspend_status,
			   inode->i_private);
}

static const struct file_operations pwr_test_debugfs_ops = {
	.open		= pwr_test_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pwr_test_suspend_debugfs_ops = {
	.open		= pwr_test_suspend_debugfs_open,
	.read		= seq_read,
	.write		= pm_test_suspend_set,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *debugfs_dir;

static int __init pwr_test_init(void)
{
	int err = 0;
	void *err_ptr;

	debugfs_dir = debugfs_create_dir("pwr_test", NULL);
	if (IS_ERR(debugfs_dir))
		return PTR_ERR(debugfs_dir);

	err_ptr = debugfs_create_file("idle",
				      S_IFREG | S_IRUGO,
				      debugfs_dir, (void *)pwr_test_idle,
				      &pwr_test_debugfs_ops);
	if (IS_ERR(err_ptr)) {
		err = PTR_ERR(err_ptr);
		goto out;
	}

	err_ptr = debugfs_create_file("suspend",
				      S_IWUSR | S_IFREG | S_IRUGO,
				      debugfs_dir, NULL,
				      &pwr_test_suspend_debugfs_ops);
	if (IS_ERR(err_ptr)) {
		err = PTR_ERR(err_ptr);
		goto out;
	}

	return 0;
out:
	debugfs_remove_recursive(debugfs_dir);
	return err;
}
late_initcall(pwr_test_init);
