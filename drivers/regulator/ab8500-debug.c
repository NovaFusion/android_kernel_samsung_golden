/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson.
 *
 * License Terms: GNU General Public License v2
 */

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500.h>
#include <linux/regulator/ab8500-debug.h>
#include <linux/io.h>

#include <mach/db8500-regs.h> /* U8500_BACKUPRAM1_BASE */
#include <mach/hardware.h>

#include "ab8500-debug.h"

/* board profile address - to determine if suspend-force is default */
#define BOOT_INFO_BACKUPRAM1 (U8500_BACKUPRAM1_BASE + 0xf7c)
#define BOARD_PROFILE_BACKUPRAM1 (0x3)

/* board profile option */
#define OPTION_BOARD_VERSION_500_V5X 1010

/* for error prints */
struct device *dev;
struct platform_device *pdev;

/* setting for suspend force (disabled by default) */
static bool setting_suspend_force = true;

/*
 * regulator states
 */
enum ab8500_regulator_state_id {
	AB8500_REGULATOR_STATE_INIT,
	AB8500_REGULATOR_STATE_SUSPEND,
	AB8500_REGULATOR_STATE_SUSPEND_CORE,
	AB8500_REGULATOR_STATE_RESUME_CORE,
	AB8500_REGULATOR_STATE_RESUME,
	AB8500_REGULATOR_STATE_CURRENT,
	NUM_REGULATOR_STATE
};

static const char *regulator_state_name[NUM_REGULATOR_STATE] = {
	[AB8500_REGULATOR_STATE_INIT] = "init",
	[AB8500_REGULATOR_STATE_SUSPEND] = "suspend",
	[AB8500_REGULATOR_STATE_SUSPEND_CORE] = "suspend-core",
	[AB8500_REGULATOR_STATE_RESUME_CORE] = "resume-core",
	[AB8500_REGULATOR_STATE_RESUME] = "resume",
	[AB8500_REGULATOR_STATE_CURRENT] = "current",
};

/*
 * regulator register definitions
 */
enum ab8500_register_id {
	AB8500_REGU_NOUSE, /* if not defined */
	AB8500_REGU_REQUEST_CTRL1,
	AB8500_REGU_REQUEST_CTRL2,
	AB8500_REGU_REQUEST_CTRL3,
	AB8500_REGU_REQUEST_CTRL4,
	AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
	AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
	AB8500_REGU_HW_HP_REQ1_VALID1,
	AB8500_REGU_HW_HP_REQ1_VALID2,
	AB8500_REGU_HW_HP_REQ2_VALID1,
	AB8500_REGU_HW_HP_REQ2_VALID2,
	AB8500_REGU_SW_HP_REQ_VALID1,
	AB8500_REGU_SW_HP_REQ_VALID2,
	AB8500_REGU_SYSCLK_REQ1_VALID,
	AB8500_REGU_SYSCLK_REQ2_VALID,
	AB8500_REGU_VAUX4_REQ_VALID,
	AB8500_REGU_MISC1,
	AB8500_REGU_OTG_SUPPLY_CTRL,
	AB8500_REGU_VUSB_CTRL,
	AB8500_REGU_VAUDIO_SUPPLY,
	AB8500_REGU_CTRL1_VAMIC,
	AB8500_REGU_ARM_REGU1,
	AB8500_REGU_ARM_REGU2,
	AB8500_REGU_VAPE_REGU,
	AB8500_REGU_VSMPS1_REGU,
	AB8500_REGU_VSMPS2_REGU,
	AB8500_REGU_VSMPS3_REGU,
	AB8500_REGU_VPLL_VANA_REGU,
	AB8500_REGU_VREF_DDR,
	AB8500_REGU_EXT_SUPPLY_REGU,
	AB8500_REGU_VAUX12_REGU,
	AB8500_REGU_VRF1_VAUX3_REGU,
	AB8500_REGU_VARM_SEL1,
	AB8500_REGU_VARM_SEL2,
	AB8500_REGU_VARM_SEL3,
	AB8500_REGU_VAPE_SEL1,
	AB8500_REGU_VAPE_SEL2,
	AB8500_REGU_VAPE_SEL3,
	AB8500_REGU_VAUX4_REQ_CTRL,
	AB8500_REGU_VAUX4_REGU,
	AB8500_REGU_VAUX4_SEL,
	AB8500_REGU_VAUX5_SEL,
	AB8500_REGU_VAUX6_SEL,
	AB8500_REGU_VBB_SEL1,
	AB8500_REGU_VBB_SEL2,
	AB8500_REGU_VSMPS1_SEL1,
	AB8500_REGU_VSMPS1_SEL2,
	AB8500_REGU_VSMPS1_SEL3,
	AB8500_REGU_VSMPS2_SEL1,
	AB8500_REGU_VSMPS2_SEL2,
	AB8500_REGU_VSMPS2_SEL3,
	AB8500_REGU_VSMPS3_SEL1,
	AB8500_REGU_VSMPS3_SEL2,
	AB8500_REGU_VSMPS3_SEL3,
	AB8500_REGU_VAUX1_SEL,
	AB8500_REGU_VAUX2_SEL,
	AB8500_REGU_VRF1_VAUX3_SEL,
	AB8500_REGU_CTRL_EXT_SUP,
	AB8500_REGU_VMOD_REGU,
	AB8500_REGU_VMOD_SEL1,
	AB8500_REGU_VMOD_SEL2,
	AB8500_REGU_CTRL_DISCH,
	AB8500_REGU_CTRL_DISCH2,
	AB8500_REGU_CTRL_DISCH3,
	AB8500_OTHER_SYSCLK_CTRL, /* Other */
	AB8500_OTHER_VSIM_SYSCLK_CTRL, /* Other */
	AB8500_OTHER_SYSULPCLK_CTRL1, /* Other */
	NUM_AB8500_REGISTER
};

struct ab8500_register {
	const char *name;
	u8 bank;
	u8 addr;
	u8 unavailable; /* Used to flag when AB doesn't support a register */
};

static struct ab8500_register
		ab8500_register[NUM_AB8500_REGISTER] = {
	[AB8500_REGU_REQUEST_CTRL1] = {
		.name = "ReguRequestCtrl1",
		.bank = 0x03,
		.addr = 0x03,
	},
	[AB8500_REGU_REQUEST_CTRL2] = {
		.name = "ReguRequestCtrl2",
		.bank = 0x03,
		.addr = 0x04,
	},
	[AB8500_REGU_REQUEST_CTRL3] = {
		.name = "ReguRequestCtrl3",
		.bank = 0x03,
		.addr = 0x05,
	},
	[AB8500_REGU_REQUEST_CTRL4] = {
		.name = "ReguRequestCtrl4",
		.bank = 0x03,
		.addr = 0x06,
	},
	[AB8500_REGU_SYSCLK_REQ1_HP_VALID1] = {
		.name = "ReguSysClkReq1HPValid",
		.bank = 0x03,
		.addr = 0x07,
	},
	[AB8500_REGU_SYSCLK_REQ1_HP_VALID2] = {
		.name = "ReguSysClkReq1HPValid2",
		.bank = 0x03,
		.addr = 0x08,
	},
	[AB8500_REGU_HW_HP_REQ1_VALID1] = {
		.name = "ReguHwHPReq1Valid1",
		.bank = 0x03,
		.addr = 0x09,
	},
	[AB8500_REGU_HW_HP_REQ1_VALID2] = {
		.name = "ReguHwHPReq1Valid2",
		.bank = 0x03,
		.addr = 0x0a,
	},
	[AB8500_REGU_HW_HP_REQ2_VALID1] = {
		.name = "ReguHwHPReq2Valid1",
		.bank = 0x03,
		.addr = 0x0b,
	},
	[AB8500_REGU_HW_HP_REQ2_VALID2] = {
		.name = "ReguHwHPReq2Valid2",
		.bank = 0x03,
		.addr = 0x0c,
	},
	[AB8500_REGU_SW_HP_REQ_VALID1] = {
		.name = "ReguSwHPReqValid1",
		.bank = 0x03,
		.addr = 0x0d,
	},
	[AB8500_REGU_SW_HP_REQ_VALID2] = {
		.name = "ReguSwHPReqValid2",
		.bank = 0x03,
		.addr = 0x0e,
	},
	[AB8500_REGU_SYSCLK_REQ1_VALID] = {
		.name = "ReguSysClkReqValid1",
		.bank = 0x03,
		.addr = 0x0f,
	},
	[AB8500_REGU_SYSCLK_REQ2_VALID] = {
		.name = "ReguSysClkReqValid2",
		.bank = 0x03,
		.addr = 0x10,
	},
	[AB8500_REGU_VAUX4_REQ_VALID] = {
		.name = "ReguVaux4ReqValid",
		.bank = 0x03,
		.addr = 0x11,
		.unavailable = true, /* ab9540 register */
	},
	[AB8500_REGU_MISC1] = {
		.name = "ReguMisc1",
		.bank = 0x03,
		.addr = 0x80,
	},
	[AB8500_REGU_OTG_SUPPLY_CTRL] = {
		.name = "OTGSupplyCtrl",
		.bank = 0x03,
		.addr = 0x81,
	},
	[AB8500_REGU_VUSB_CTRL] = {
		.name = "VusbCtrl",
		.bank = 0x03,
		.addr = 0x82,
	},
	[AB8500_REGU_VAUDIO_SUPPLY] = {
		.name = "VaudioSupply",
		.bank = 0x03,
		.addr = 0x83,
	},
	[AB8500_REGU_CTRL1_VAMIC] = {
		.name = "ReguCtrl1VAmic",
		.bank = 0x03,
		.addr = 0x84,
	},
	[AB8500_REGU_ARM_REGU1] = {
		.name = "ArmRegu1",
		.bank = 0x04,
		.addr = 0x00,
	},
	[AB8500_REGU_ARM_REGU2] = {
		.name = "ArmRegu2",
		.bank = 0x04,
		.addr = 0x01,
	},
	[AB8500_REGU_VAPE_REGU] = {
		.name = "VapeRegu",
		.bank = 0x04,
		.addr = 0x02,
	},
	[AB8500_REGU_VSMPS1_REGU] = {
		.name = "Vsmps1Regu",
		.bank = 0x04,
		.addr = 0x03,
	},
	[AB8500_REGU_VSMPS2_REGU] = {
		.name = "Vsmps2Regu",
		.bank = 0x04,
		.addr = 0x04,
	},
	[AB8500_REGU_VSMPS3_REGU] = {
		.name = "Vsmps3Regu",
		.bank = 0x04,
		.addr = 0x05,
	},
	[AB8500_REGU_VPLL_VANA_REGU] = {
		.name = "VpllVanaRegu",
		.bank = 0x04,
		.addr = 0x06,
	},
	[AB8500_REGU_VREF_DDR] = {
		.name = "VrefDDR",
		.bank = 0x04,
		.addr = 0x07,
	},
	[AB8500_REGU_EXT_SUPPLY_REGU] = {
		.name = "ExtSupplyRegu",
		.bank = 0x04,
		.addr = 0x08,
	},
	[AB8500_REGU_VAUX12_REGU] = {
		.name = "Vaux12Regu",
		.bank = 0x04,
		.addr = 0x09,
	},
	[AB8500_REGU_VRF1_VAUX3_REGU] = {
		.name = "VRF1Vaux3Regu",
		.bank = 0x04,
		.addr = 0x0a,
	},
	[AB8500_REGU_VARM_SEL1] = {
		.name = "VarmSel1",
		.bank = 0x04,
		.addr = 0x0b,
	},
	[AB8500_REGU_VARM_SEL2] = {
		.name = "VarmSel2",
		.bank = 0x04,
		.addr = 0x0c,
	},
	[AB8500_REGU_VARM_SEL3] = {
		.name = "VarmSel3",
		.bank = 0x04,
		.addr = 0x0d,
	},
	[AB8500_REGU_VAPE_SEL1] = {
		.name = "VapeSel1",
		.bank = 0x04,
		.addr = 0x0e,
	},
	[AB8500_REGU_VAPE_SEL2] = {
		.name = "VapeSel2",
		.bank = 0x04,
		.addr = 0x0f,
	},
	[AB8500_REGU_VAPE_SEL3] = {
		.name = "VapeSel3",
		.bank = 0x04,
		.addr = 0x10,
	},
	[AB8500_REGU_VAUX4_REQ_CTRL] = {
		.name = "Vaux4ReqCtrl",
		.bank = 0x04,
		.addr = 0x2d,
		.unavailable = true, /* ab8505/ab9540 register */
	},
	[AB8500_REGU_VAUX4_REGU] = {
		.name = "Vaux4Regu",
		.bank = 0x04,
		.addr = 0x2e,
		.unavailable = true, /* ab8505/ab9540 register */
	},
	[AB8500_REGU_VAUX4_SEL] = {
		.name = "Vaux4Sel",
		.bank = 0x04,
		.addr = 0x2f,
		.unavailable = true, /* ab8505/ab9540 register */
	},
	[AB8500_REGU_VAUX5_SEL] = {
		.name = "Vaux5Sel",
		.bank = 0x01,
		.addr = 0x55,
		.unavailable = true, /* ab8505 register */
	},
	[AB8500_REGU_VAUX6_SEL] = {
		.name = "Vaux6Sel",
		.bank = 0x01,
		.addr = 0x56,
		.unavailable = true, /* ab8505 register */
	},
	[AB8500_REGU_VBB_SEL1] = {
		.name = "VBBSel1",
		.bank = 0x04,
		.addr = 0x11,
	},
	[AB8500_REGU_VBB_SEL2] = {
		.name = "VBBSel2",
		.bank = 0x04,
		.addr = 0x12,
	},
	[AB8500_REGU_VSMPS1_SEL1] = {
		.name = "Vsmps1Sel1",
		.bank = 0x04,
		.addr = 0x13,
	},
	[AB8500_REGU_VSMPS1_SEL2] = {
		.name = "Vsmps1Sel2",
		.bank = 0x04,
		.addr = 0x14,
	},
	[AB8500_REGU_VSMPS1_SEL3] = {
		.name = "Vsmps1Sel3",
		.bank = 0x04,
		.addr = 0x15,
	},
	[AB8500_REGU_VSMPS2_SEL1] = {
		.name = "Vsmps2Sel1",
		.bank = 0x04,
		.addr = 0x17,
	},
	[AB8500_REGU_VSMPS2_SEL2] = {
		.name = "Vsmps2Sel2",
		.bank = 0x04,
		.addr = 0x18,
	},
	[AB8500_REGU_VSMPS2_SEL3] = {
		.name = "Vsmps2Sel3",
		.bank = 0x04,
		.addr = 0x19,
	},
	[AB8500_REGU_VSMPS3_SEL1] = {
		.name = "Vsmps3Sel1",
		.bank = 0x04,
		.addr = 0x1b,
	},
	[AB8500_REGU_VSMPS3_SEL2] = {
		.name = "Vsmps3Sel2",
		.bank = 0x04,
		.addr = 0x1c,
	},
	[AB8500_REGU_VSMPS3_SEL3] = {
		.name = "Vsmps3Sel3",
		.bank = 0x04,
		.addr = 0x1d,
	},
	[AB8500_REGU_VAUX1_SEL] = {
		.name = "Vaux1Sel",
		.bank = 0x04,
		.addr = 0x1f,
	},
	[AB8500_REGU_VAUX2_SEL] = {
		.name = "Vaux2Sel",
		.bank = 0x04,
		.addr = 0x20,
	},
	[AB8500_REGU_VRF1_VAUX3_SEL] = {
		.name = "VRF1Vaux3Sel",
		.bank = 0x04,
		.addr = 0x21,
	},
	[AB8500_REGU_CTRL_EXT_SUP] = {
		.name = "ReguCtrlExtSup",
		.bank = 0x04,
		.addr = 0x22,
	},
	[AB8500_REGU_VMOD_REGU] = {
		.name = "VmodRegu",
		.bank = 0x04,
		.addr = 0x40,
	},
	[AB8500_REGU_VMOD_SEL1] = {
		.name = "VmodSel1",
		.bank = 0x04,
		.addr = 0x41,
	},
	[AB8500_REGU_VMOD_SEL2] = {
		.name = "VmodSel2",
		.bank = 0x04,
		.addr = 0x42,
	},
	[AB8500_REGU_CTRL_DISCH] = {
		.name = "ReguCtrlDisch",
		.bank = 0x04,
		.addr = 0x43,
	},
	[AB8500_REGU_CTRL_DISCH2] = {
		.name = "ReguCtrlDisch2",
		.bank = 0x04,
		.addr = 0x44,
	},
	[AB8500_REGU_CTRL_DISCH3] = {
		.name = "ReguCtrlDisch3",
		.bank = 0x04,
		.addr = 0x48,
		.unavailable = true, /* ab9540 register */
	},
	/* Outside regulator banks */
	[AB8500_OTHER_SYSCLK_CTRL] = {
		.name = "SysClkCtrl",
		.bank = 0x02,
		.addr = 0x0c,
	},
	[AB8500_OTHER_VSIM_SYSCLK_CTRL] = {
		.name = "VsimSysClkCtrl",
		.bank = 0x02,
		.addr = 0x33,
	},
	[AB8500_OTHER_SYSULPCLK_CTRL1] = {
		.name = "SysUlpClkCtrl1",
		.bank = 0x02,
		.addr = 0x0b,
	},
};

struct ab8500_register_update {
	/* Identity of register to be updated */
	u8 bank;
	u8 addr;
	/* New value for unavailable flag */
	u8 unavailable;
};

static const struct ab8500_register_update ab8505_update[] = {
	/* AB8500 register which is unavailable to AB8505 */
	/* AB8500_REGU_VREF_DDR */
	{
		.bank = 0x04,
		.addr = 0x07,
		.unavailable = true,
	},
	/*
	 * Registers which were not available to AB8500 but are on the
	 * AB8505.
	 */
	/* AB8500_REGU_VAUX4_REQ_VALID */
	{
		.bank = 0x03,
		.addr = 0x11,
	},
	/* AB8500_REGU_VAUX4_REQ_CTRL */
	{
		.bank = 0x04,
		.addr = 0x2d,
	},
	/* AB8500_REGU_VAUX4_REGU */
	{
		.bank = 0x04,
		.addr = 0x2e,
	},
	/* AB8500_REGU_VAUX4_SEL */
	{
		.bank = 0x04,
		.addr = 0x2f,
	},
	/* AB8500_REGU_VAUX5_SEL */
	{
		.bank = 0x01,
		.addr = 0x55,
	},
	/* AB8500_REGU_VAUX6_SEL */
	{
		.bank = 0x01,
		.addr = 0x56,
	},
	/* AB8500_REGU_CTRL_DISCH3 */
	{
		.bank = 0x04,
		.addr = 0x48,
	},
};

static const struct ab8500_register_update ab9540_update[] = {
	/* AB8500 register which is unavailable to AB9540 */
	/* AB8500_REGU_VREF_DDR */
	{
		.bank = 0x04,
		.addr = 0x07,
		.unavailable = true,
	},
	/*
	 * Registers which were not available to AB8500 but are on the
	 * AB9540.
	 */
	/* AB8500_REGU_VAUX4_REQ_VALID */
	{
		.bank = 0x03,
		.addr = 0x11,
	},
	/* AB8500_REGU_VAUX4_REQ_CTRL */
	{
		.bank = 0x04,
		.addr = 0x2d,
	},
	/* AB8500_REGU_VAUX4_REGU */
	{
		.bank = 0x04,
		.addr = 0x2e,
	},
	/* AB8500_REGU_VAUX4_SEL */
	{
		.bank = 0x04,
		.addr = 0x2f,
	},
	/* AB8500_REGU_CTRL_DISCH3 */
	{
		.bank = 0x04,
		.addr = 0x48,
	},
};

static void ab8505_registers_update(void)
{
	int i;
	int j;

	for (i = 0; i < NUM_AB8500_REGISTER; i++)
		for (j = 0; j < ARRAY_SIZE(ab8505_update); j++)
			if (ab8500_register[i].bank == ab8505_update[j].bank &&
			    ab8500_register[i].addr == ab8505_update[j].addr) {
				ab8500_register[i].unavailable =
					ab8505_update[j].unavailable;
				break;
			}
}

static void ab9540_registers_update(void)
{
	int i;
	int j;

	for (i = 0; i < NUM_AB8500_REGISTER; i++)
		for (j = 0; j < ARRAY_SIZE(ab9540_update); j++)
			if (ab8500_register[i].bank == ab9540_update[j].bank &&
			    ab8500_register[i].addr == ab9540_update[j].addr) {
				ab8500_register[i].unavailable =
					ab9540_update[j].unavailable;
				break;
			}
}

static u8 ab8500_register_state[NUM_REGULATOR_STATE][NUM_AB8500_REGISTER];
static bool ab8500_register_state_saved[NUM_REGULATOR_STATE];
static bool ab8500_register_state_save = true;

static int ab8500_regulator_record_state(int state)
{
	u8 val;
	int i;
	int ret;

	/* check arguments */
	if ((state >= NUM_REGULATOR_STATE) || (state < 0)) {
		dev_err(dev, "Wrong state specified\n");
		return -EINVAL;
	}

	/* record */
	if (!ab8500_register_state_save)
		goto exit;

	ab8500_register_state_saved[state] = true;

	for (i = 1; i < NUM_AB8500_REGISTER; i++) {
		if (ab8500_register[i].unavailable)
			continue;

		ret = abx500_get_register_interruptible(dev,
			ab8500_register[i].bank,
			ab8500_register[i].addr,
			&val);
		if (ret < 0) {
			dev_err(dev, "abx500_get_reg fail %d, %d\n",
				ret, __LINE__);
			return -EINVAL;
		}

		ab8500_register_state[state][i] = val;
	}
exit:
	return 0;
}

/*
 * regulator register dump
 */
static int ab8500_regulator_dump_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int state, reg_id, i;
	int err;

	/* record current state */
	ab8500_regulator_record_state(AB8500_REGULATOR_STATE_CURRENT);

	/* print dump header */
	err = seq_printf(s, "ab8500-regulator dump:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	/* print states */
	for (state = NUM_REGULATOR_STATE - 1; state >= 0; state--) {
		if (ab8500_register_state_saved[state])
			err = seq_printf(s, "%16s saved -------",
				regulator_state_name[state]);
		else
			err = seq_printf(s, "%12s not saved -------",
				regulator_state_name[state]);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i\n", __LINE__);

		for (i = 0; i < NUM_REGULATOR_STATE; i++) {
			if (i < state)
				err = seq_printf(s, "-----");
			else if (i == state)
				err = seq_printf(s, "----+");
			else
				err = seq_printf(s, "    |");
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i\n",
					__LINE__);
		}
		err = seq_printf(s, "\n");
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	}

	/* print labels */
	err = seq_printf(s, "\n                       addr\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);

	/* dump registers */
	for (reg_id = 1; reg_id < NUM_AB8500_REGISTER; reg_id++) {
		if (ab8500_register[reg_id].unavailable)
			continue;

		err = seq_printf(s, "%22s 0x%02x%02x:",
			ab8500_register[reg_id].name,
			ab8500_register[reg_id].bank,
			ab8500_register[reg_id].addr);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				reg_id, __LINE__);

		for (state = 0; state < NUM_REGULATOR_STATE; state++) {
			err = seq_printf(s, " 0x%02x",
				ab8500_register_state[state][reg_id]);
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i, %i\n",
					reg_id, __LINE__);
		}

		err = seq_printf(s, "\n");
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				reg_id, __LINE__);
	}

	return 0;
}

static int ab8500_regulator_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_regulator_dump_print, inode->i_private);
}

static const struct file_operations ab8500_regulator_dump_fops = {
	.open = ab8500_regulator_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/*
 * regulator_voltage
 */
struct regulator_volt {
	u8 value;
	int volt;
};

struct regulator_volt_range {
	struct regulator_volt start;
	struct regulator_volt step;
	struct regulator_volt end;
};

/*
 * ab8500_regulator
 * @name
 * @update_regid
 * @update_mask
 * @update_val[4] {off, on, hw, lp}
 * @hw_mode_regid
 * @hw_mode_mask
 * @hw_mode_val[4] {hp/lp, hp/off, hp, hp}
 * @hw_valid_regid[4] {sysclkreq1, hw1, hw2, sw}
 * @hw_valid_mask[4] {sysclkreq1, hw1, hw2, sw}
 * @vsel_sel_regid
 * @vsel_sel_mask
 * @vsel_val[333] {sel1, sel2, sel3, sel3}
 * @vsel_regid
 * @vsel_mask
 * @vsel_range
 * @vsel_range_len
 * @unavailable {true/false depending on whether AB supports the regulator}
 */
struct ab8500_regulator {
	const char *name;
	int update_regid;
	u8 update_mask;
	u8 update_val[4];
	int hw_mode_regid;
	u8 hw_mode_mask;
	u8 hw_mode_val[4];
	int hw_valid_regid[4];
	u8 hw_valid_mask[4];
	int vsel_sel_regid;
	u8 vsel_sel_mask;
	u8 vsel_sel_val[4];
	int vsel_regid[3];
	u8 vsel_mask[3];
	struct regulator_volt_range const *vsel_range[3];
	int vsel_range_len[3];
	u8 unavailable;
};

static const char *update_val_name[] = {
	"off",
	"on ",
	"hw ",
	"lp ",
	" - " /* undefined value */
};

static const char *hw_mode_val_name[] = {
	"hp/lp ",
	"hp/off",
	"hp    ",
	"hp    ",
	"-/-   ", /* undefined value */
};

/* voltage selection */
/* AB8500 device - Varm_vsel in 12.5mV steps */
#define AB8500_VARM_VSEL_MASK 0x3f
static const struct regulator_volt_range ab8500_varm_vsel[] = {
	{ {0x00,  700000}, {0x01,   12500}, {0x35, 1362500} },
	{ {0x36, 1362500}, {0x01,       0}, {0x3f, 1362500} },
};

/* AB9540/AB8505 device - Varm_vsel in 6.25mV steps */
#define AB9540_AB8505_VARM_VSEL_MASK 0x7f
static const struct regulator_volt_range ab9540_ab8505_varm_vsel[] = {
	{ {0x00,  600000}, {0x01,    6250}, {0x7f, 1393750} },
};

static const struct regulator_volt_range vape_vmod_vsel[] = {
	{ {0x00,  700000}, {0x01,   12500}, {0x35, 1362500} },
	{ {0x36, 1362500}, {0x01,       0}, {0x3f, 1362500} },
};

/* AB8500 device - Vbbp_vsel and Vbbn_sel in 100mV steps */
static const struct regulator_volt_range ab8500_vbbp_vsel[] = {
	{ {0x00,       0}, {0x10,  100000}, {0x40,  400000} },
	{ {0x50,  400000}, {0x10,       0}, {0x70,  400000} },
	{ {0x80, -400000}, {0x10,       0}, {0xb0, -400000} },
	{ {0xc0, -400000}, {0x10,  100000}, {0xf0, -100000} },
};

static const struct regulator_volt_range ab8500_vbbn_vsel[] = {
	{ {0x00,       0}, {0x01, -100000}, {0x04, -400000} },
	{ {0x05, -400000}, {0x01,       0}, {0x07, -400000} },
	{ {0x08,       0}, {0x01,  100000}, {0x0c,  400000} },
	{ {0x0d,  400000}, {0x01,       0}, {0x0f,  400000} },
};

/* AB9540/AB8505 device - Vbbp_vsel and Vbbn_sel in 50mV steps */
static const struct regulator_volt_range ab9540_ab8505_vbbp_vsel[] = {
	{ {0x00,       0}, {0x10,  -50000}, {0x70, -350000} },
	{ {0x80,   50000}, {0x10,   50000}, {0xf0,  400000} },
};

static const struct regulator_volt_range ab9540_ab8505_vbbn_vsel[] = {
	{ {0x00,       0}, {0x01,  -50000}, {0x07, -350000} },
	{ {0x08,   50000}, {0x01,   50000}, {0x0f,  400000} },
};

static const struct regulator_volt_range vsmps1_vsel[] = {
	{ {0x00, 1100000}, {0x01,       0}, {0x1f, 1100000} },
	{ {0x20, 1100000}, {0x01,   12500}, {0x30, 1300000} },
	{ {0x31, 1300000}, {0x01,       0}, {0x3f, 1300000} },
};

static const struct regulator_volt_range vsmps2_vsel[] = {
	{ {0x00, 1800000}, {0x01,       0}, {0x38, 1800000} },
	{ {0x39, 1800000}, {0x01,   12500}, {0x7f, 1875000} },
};

static const struct regulator_volt_range vsmps3_vsel[] = {
	{ {0x00,  700000}, {0x01,   12500}, {0x35, 1363500} },
	{ {0x36, 1363500}, {0x01,       0}, {0x7f, 1363500} },
};

/* for Vaux1, Vaux2 and Vaux4 */
static const struct regulator_volt_range vauxn_vsel[] = {
	{ {0x00, 1100000}, {0x01,  100000}, {0x04, 1500000} },
	{ {0x05, 1800000}, {0x01,   50000}, {0x07, 1900000} },
	{ {0x08, 2500000}, {0x01,       0}, {0x08, 2500000} },
	{ {0x09, 2650000}, {0x01,   50000}, {0x0c, 2800000} },
	{ {0x0d, 2900000}, {0x01,  100000}, {0x0e, 3000000} },
	{ {0x0f, 3300000}, {0x01,       0}, {0x0f, 3300000} },
};

static const struct regulator_volt_range vaux3_vsel[] = {
	{ {0x00, 1200000}, {0x01,  300000}, {0x03, 2100000} },
	{ {0x04, 2500000}, {0x01,  250000}, {0x05, 2750000} },
	{ {0x06, 2790000}, {0x01,       0}, {0x06, 2790000} },
	{ {0x07, 2910000}, {0x01,       0}, {0x07, 2910000} },
};

static const struct regulator_volt_range vaux5_6_vsel[] = {
	{ {0x00, 1800000}, {0x01,       0}, {0x00, 1800000} },
	{ {0x01, 1050000}, {0x01,   50000}, {0x03, 1200000} },
	{ {0x04, 1500000}, {0x01,       0}, {0x04, 1500000} },
	{ {0x05, 2200000}, {0x01,       0}, {0x05, 2200000} },
	{ {0x06, 2500000}, {0x01,       0}, {0x06, 2500000} },
	{ {0x07, 2790000}, {0x01,       0}, {0x07, 2790000} },
};

static const struct regulator_volt_range vrf1_vsel[] = {
	{ {0x00, 1800000}, {0x10,  200000}, {0x10, 2000000} },
	{ {0x20, 2150000}, {0x10,       0}, {0x20, 2150000} },
	{ {0x30, 2500000}, {0x10,       0}, {0x30, 2500000} },
};

static const struct regulator_volt_range vintcore12_vsel[] = {
	{ {0x00, 1200000}, {0x08,   25000}, {0x30, 1350000} },
	{ {0x38, 1350000}, {0x01,       0}, {0x38, 1350000} },
};

/* regulators */
static struct ab8500_regulator ab8500_regulator[AB8500_NUM_REGULATORS] = {
	[AB8500_VARM] = {
		.name		   = "Varm",
		.update_regid      = AB8500_REGU_ARM_REGU1,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x02,
		.vsel_sel_regid    = AB8500_REGU_ARM_REGU1,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VARM_SEL1,
		.vsel_mask[0]      = AB8500_VARM_VSEL_MASK,
		.vsel_range[0]     = ab8500_varm_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(ab8500_varm_vsel),
		.vsel_regid[1]     = AB8500_REGU_VARM_SEL2,
		.vsel_mask[1]      = AB8500_VARM_VSEL_MASK,
		.vsel_range[1]     = ab8500_varm_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(ab8500_varm_vsel),
		.vsel_regid[2]     = AB8500_REGU_VARM_SEL3,
		.vsel_mask[2]      = AB8500_VARM_VSEL_MASK,
		.vsel_range[2]     = ab8500_varm_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(ab8500_varm_vsel),
	},
	[AB8500_VBBP] = {
		.name		   = "Vbbp",
		.update_regid      = AB8500_REGU_ARM_REGU2,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x00},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x04,
		.vsel_sel_regid    = AB8500_REGU_ARM_REGU1,
		.vsel_sel_mask     = 0x10,
		.vsel_sel_val      = {0x00, 0x10, 0x00, 0x00},
		.vsel_regid[0]     = AB8500_REGU_VBB_SEL1,
		.vsel_mask[0]      = 0xf0,
		.vsel_range[0]     = ab8500_vbbp_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(ab8500_vbbp_vsel),
		.vsel_regid[1]     = AB8500_REGU_VBB_SEL2,
		.vsel_mask[1]      = 0xf0,
		.vsel_range[1]     = ab8500_vbbp_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(ab8500_vbbp_vsel),
	},
	[AB8500_VBBN] = {
		.name		   = "Vbbn",
		.update_regid      = AB8500_REGU_ARM_REGU2,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x00},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x04,
		.vsel_sel_regid    = AB8500_REGU_ARM_REGU1,
		.vsel_sel_mask     = 0x20,
		.vsel_sel_val      = {0x00, 0x20, 0x00, 0x00},
		.vsel_regid[0]     = AB8500_REGU_VBB_SEL1,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = ab8500_vbbn_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(ab8500_vbbn_vsel),
		.vsel_regid[1]     = AB8500_REGU_VBB_SEL2,
		.vsel_mask[1]      = 0x0f,
		.vsel_range[1]     = ab8500_vbbn_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(ab8500_vbbn_vsel),
	},
	[AB8500_VAPE] = {
		.name		   = "Vape",
		.update_regid      = AB8500_REGU_VAPE_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0x0c,
		.hw_mode_val       = {0x00, 0x04, 0x08, 0x0c},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x01,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x01,
		.vsel_sel_regid    = AB8500_REGU_VAPE_REGU,
		.vsel_sel_mask     = 0x24,
		.vsel_sel_val      = {0x00, 0x04, 0x20, 0x24},
		.vsel_regid[0]     = AB8500_REGU_VAPE_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = vape_vmod_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vape_vmod_vsel),
		.vsel_regid[1]     = AB8500_REGU_VAPE_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = vape_vmod_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vape_vmod_vsel),
		.vsel_regid[2]     = AB8500_REGU_VAPE_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = vape_vmod_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vape_vmod_vsel),
	},
	[AB8500_VSMPS1] = {
		.name		   = "Vsmps1",
		.update_regid      = AB8500_REGU_VSMPS1_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0x30,
		.hw_mode_val       = {0x00, 0x10, 0x20, 0x30},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x01,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x01,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x01,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x04,
		.vsel_sel_regid    = AB8500_REGU_VSMPS1_REGU,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VSMPS1_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = vsmps1_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vsmps1_vsel),
		.vsel_regid[1]     = AB8500_REGU_VSMPS1_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = vsmps1_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vsmps1_vsel),
		.vsel_regid[2]     = AB8500_REGU_VSMPS1_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = vsmps1_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vsmps1_vsel),
	},
	[AB8500_VSMPS2] = {
		.name		   = "Vsmps2",
		.update_regid      = AB8500_REGU_VSMPS2_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL1,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x02,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x02,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x08,
		.vsel_sel_regid    = AB8500_REGU_VSMPS2_REGU,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VSMPS2_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = vsmps2_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vsmps2_vsel),
		.vsel_regid[1]     = AB8500_REGU_VSMPS2_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = vsmps2_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vsmps2_vsel),
		.vsel_regid[2]     = AB8500_REGU_VSMPS2_SEL3,
		.vsel_mask[2]      = 0x3f,
		.vsel_range[2]     = vsmps2_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vsmps2_vsel),
	},
	[AB8500_VSMPS3] = {
		.name		   = "Vsmps3",
		.update_regid      = AB8500_REGU_VSMPS3_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x04,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x04,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x04,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x10,
		.vsel_sel_regid    = AB8500_REGU_VSMPS3_REGU,
		.vsel_sel_mask     = 0x0c,
		.vsel_sel_val      = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VSMPS3_SEL1,
		.vsel_mask[0]      = 0x7f,
		.vsel_range[0]     = vsmps3_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vsmps3_vsel),
		.vsel_regid[1]     = AB8500_REGU_VSMPS3_SEL2,
		.vsel_mask[1]      = 0x7f,
		.vsel_range[1]     = vsmps3_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vsmps3_vsel),
		.vsel_regid[2]     = AB8500_REGU_VSMPS3_SEL3,
		.vsel_mask[2]      = 0x7f,
		.vsel_range[2]     = vsmps3_vsel,
		.vsel_range_len[2] = ARRAY_SIZE(vsmps3_vsel),
	},
	[AB8500_VPLL] = {
		.name		   = "Vpll",
		.update_regid      = AB8500_REGU_VPLL_VANA_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0x0c,
		.hw_mode_val       = {0x00, 0x04, 0x08, 0x0c},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x10,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x10,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x10,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x40,
	},
	[AB8500_VREFDDR] = {
		.name		   = "VrefDDR",
		.update_regid      = AB8500_REGU_VREF_DDR,
		.update_mask       = 0x01,
		.update_val        = {0x00, 0x01, 0x00, 0x00},
	},
	[AB8500_VMOD] = {
		.name		   = "Vmod",
		.update_regid      = AB8500_REGU_VMOD_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_VMOD_REGU,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x08,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x08,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x08,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x20,
		.vsel_sel_regid    = AB8500_REGU_VMOD_REGU,
		.vsel_sel_mask     = 0x04,
		.vsel_sel_val      = {0x00, 0x04, 0x00, 0x00},
		.vsel_regid[0]     = AB8500_REGU_VMOD_SEL1,
		.vsel_mask[0]      = 0x3f,
		.vsel_range[0]     = vape_vmod_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vape_vmod_vsel),
		.vsel_regid[1]     = AB8500_REGU_VMOD_SEL2,
		.vsel_mask[1]      = 0x3f,
		.vsel_range[1]     = vape_vmod_vsel,
		.vsel_range_len[1] = ARRAY_SIZE(vape_vmod_vsel),
	},
	[AB8500_VEXTSUPPLY1] = {
		.name		   = "Vextsupply1",
		.update_regid      = AB8500_REGU_EXT_SUPPLY_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x10,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x01,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x01,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x04,
	},
	[AB8500_VEXTSUPPLY2] = {
		.name		   = "VextSupply2",
		.update_regid      = AB8500_REGU_EXT_SUPPLY_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x20,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x02,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x08,
	},
	[AB8500_VEXTSUPPLY3] = {
		.name		   = "VextSupply3",
		.update_regid      = AB8500_REGU_EXT_SUPPLY_REGU,
		.update_mask       = 0x30,
		.update_val        = {0x00, 0x10, 0x20, 0x30},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0x0c,
		.hw_mode_val       = {0x00, 0x04, 0x08, 0x0c},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID2,
		.hw_valid_mask[0]  = 0x40,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID2,
		.hw_valid_mask[1]  = 0x04,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID2,
		.hw_valid_mask[2]  = 0x04,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x10,
	},
	[AB8500_VRF1] = {
		.name		   = "Vrf1",
		.update_regid      = AB8500_REGU_VRF1_VAUX3_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.vsel_regid[0]     = AB8500_REGU_VRF1_VAUX3_SEL,
		.vsel_mask[0]      = 0x30,
		.vsel_range[0]     = vrf1_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vrf1_vsel),
	},
	[AB8500_VANA] = {
		.name		   = "Vana",
		.update_regid      = AB8500_REGU_VPLL_VANA_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL2,
		.hw_mode_mask      = 0x30,
		.hw_mode_val       = {0x00, 0x10, 0x20, 0x30},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x08,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x08,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x08,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x20,
	},
	[AB8500_VAUX1] = {
		.name		   = "Vaux1",
		.update_regid      = AB8500_REGU_VAUX12_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0x30,
		.hw_mode_val       = {0x00, 0x10, 0x20, 0x30},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x20,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x20,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x20,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID1,
		.hw_valid_mask[3]  = 0x80,
		.vsel_regid[0]     = AB8500_REGU_VAUX1_SEL,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = vauxn_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vauxn_vsel),
	},
	[AB8500_VAUX2] = {
		.name		   = "Vaux2",
		.update_regid      = AB8500_REGU_VAUX12_REGU,
		.update_mask       = 0x0c,
		.update_val        = {0x00, 0x04, 0x08, 0x0c},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL3,
		.hw_mode_mask      = 0xc0,
		.hw_mode_val       = {0x00, 0x40, 0x80, 0xc0},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x40,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x40,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x40,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x01,
		.vsel_regid[0]     = AB8500_REGU_VAUX2_SEL,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = vauxn_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vauxn_vsel),
	},
	[AB8500_VAUX3] = {
		.name		   = "Vaux3",
		.update_regid      = AB8500_REGU_VRF1_VAUX3_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_REQUEST_CTRL4,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_SYSCLK_REQ1_HP_VALID1,
		.hw_valid_mask[0]  = 0x80,
		.hw_valid_regid[1] = AB8500_REGU_HW_HP_REQ1_VALID1,
		.hw_valid_mask[1]  = 0x80,
		.hw_valid_regid[2] = AB8500_REGU_HW_HP_REQ2_VALID1,
		.hw_valid_mask[2]  = 0x80,
		.hw_valid_regid[3] = AB8500_REGU_SW_HP_REQ_VALID2,
		.hw_valid_mask[3]  = 0x02,
		.vsel_regid[0]     = AB8500_REGU_VRF1_VAUX3_SEL,
		.vsel_mask[0]      = 0x07,
		.vsel_range[0]     = vaux3_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vaux3_vsel),
	},
	[AB8500_VAUX4] = {
		.name              = "Vaux4",
		.update_regid      = AB8500_REGU_VAUX4_REGU,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x02, 0x03},
		.hw_mode_regid     = AB8500_REGU_VAUX4_REQ_CTRL,
		.hw_mode_mask      = 0x03,
		.hw_mode_val       = {0x00, 0x01, 0x02, 0x03},
		.hw_valid_regid[0] = AB8500_REGU_VAUX4_REQ_VALID,
		.hw_valid_mask[0]  = 0x08,
		.hw_valid_regid[1] = AB8500_REGU_VAUX4_REQ_VALID,
		.hw_valid_mask[1]  = 0x04,
		.hw_valid_regid[2] = AB8500_REGU_VAUX4_REQ_VALID,
		.hw_valid_mask[2]  = 0x02,
		.hw_valid_regid[3] = AB8500_REGU_VAUX4_REQ_VALID,
		.hw_valid_mask[3]  = 0x01,
		.vsel_regid[0]     = AB8500_REGU_VAUX4_SEL,
		.vsel_mask[0]      = 0x0f,
		.vsel_range[0]     = vauxn_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vauxn_vsel),
		.unavailable       = true, /* AB8505/AB9540 regulator */
	},
	[AB8500_VAUX5] = {
 		.name              = "Vaux5",
		.update_regid      = AB8500_REGU_VAUX5_SEL,
 		.update_mask       = 0x18,
 		.update_val        = {0x00, 0x10, 0x00, 0x18},
		.hw_mode_regid     = AB8500_REGU_VAUX5_SEL,
 		.hw_mode_mask      = 0x08,
 		.hw_mode_val       = {0x00, 0x00, 0x00, 0x08},
		.vsel_regid[0]     = AB8500_REGU_VAUX5_SEL,
 		.vsel_mask[0]      = 0x07,
 		.vsel_range[0]     = vaux5_6_vsel,
 		.vsel_range_len[0] = ARRAY_SIZE(vaux5_6_vsel),
		.unavailable       = true, /* AB8505 regulator */
 	},
	[AB8500_VAUX6] = {
 		.name              = "Vaux6",
		.update_regid      = AB8500_REGU_VAUX6_SEL,
 		.update_mask       = 0x10,
 		.update_val        = {0x00, 0x10, 0x00, 0x00},
		.hw_mode_regid     = AB8500_REGU_VAUX6_SEL,
 		.hw_mode_mask      = 0x08,
 		.hw_mode_val       = {0x00, 0x00, 0x00, 0x08},
		.vsel_regid[0]     = AB8500_REGU_VAUX6_SEL,
 		.vsel_mask[0]      = 0x07,
 		.vsel_range[0]     = vaux5_6_vsel,
 		.vsel_range_len[0] = ARRAY_SIZE(vaux5_6_vsel),
		.unavailable       = true, /* AB8505 regulator */
	},
	[AB8500_VAUX8] = {
		.name              = "Vaux8",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x04,
		.update_val        = {0x00, 0x04, 0x00, 0x00},
		.unavailable       = true, /* AB8505 regulator */
		.name              = "Vaux5",
		.update_regid      = AB8500_REGU_VAUX5_SEL,
		.update_mask       = 0x18,
		.update_val        = {0x00, 0x10, 0x00, 0x18},
		.hw_mode_regid     = AB8500_REGU_VAUX5_SEL,
		.hw_mode_mask      = 0x08,
		.hw_mode_val       = {0x00, 0x00, 0x00, 0x08},
		.vsel_regid[0]     = AB8500_REGU_VAUX5_SEL,
		.vsel_mask[0]      = 0x07,
		.vsel_range[0]     = vaux5_6_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vaux5_6_vsel),
		.unavailable       = true, /* AB8505 regulator */
	},
	[AB8500_VAUX6] = {
		.name              = "Vaux6",
		.update_regid      = AB8500_REGU_VAUX6_SEL,
		.update_mask       = 0x10,
		.update_val        = {0x00, 0x10, 0x00, 0x00},
		.hw_mode_regid     = AB8500_REGU_VAUX6_SEL,
		.hw_mode_mask      = 0x08,
		.hw_mode_val       = {0x00, 0x00, 0x00, 0x08},
		.vsel_regid[0]     = AB8500_REGU_VAUX6_SEL,
		.vsel_mask[0]      = 0x07,
		.vsel_range[0]     = vaux5_6_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vaux5_6_vsel),
		.unavailable       = true, /* AB8505 regulator */
	},
	[AB8500_VAUX8] = {
		.name              = "Vaux8",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x04,
		.update_val        = {0x00, 0x04, 0x00, 0x00},
		.unavailable       = true, /* AB8505 regulator */
	},
	[AB8500_VINTCORE] = {
		.name		   = "VintCore12",
		.update_regid      = AB8500_REGU_MISC1,
		.update_mask       = 0x44,
		.update_val        = {0x00, 0x04, 0x00, 0x44},
		.vsel_regid[0]     = AB8500_REGU_MISC1,
		.vsel_mask[0]      = 0x38,
		.vsel_range[0]     = vintcore12_vsel,
		.vsel_range_len[0] = ARRAY_SIZE(vintcore12_vsel),
	},
	[AB8500_VTVOUT] = {
		.name		   = "VTVout",
		.update_regid      = AB8500_REGU_MISC1,
		.update_mask       = 0x82,
		.update_val        = {0x00, 0x02, 0x00, 0x82},
	},
	[AB8500_VAUDIO] = {
		.name		   = "Vaudio",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x02,
		.update_val        = {0x00, 0x02, 0x00, 0x00},
	},
	[AB8500_VANAMIC1] = {
		.name		   = "Vanamic1",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x08,
		.update_val        = {0x00, 0x08, 0x00, 0x00},
	},
	[AB8500_VANAMIC2] = {
		.name		   = "Vanamic2",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x10,
		.update_val        = {0x00, 0x10, 0x00, 0x00},
	},
	[AB8500_VDMIC] = {
		.name		   = "Vdmic",
		.update_regid      = AB8500_REGU_VAUDIO_SUPPLY,
		.update_mask       = 0x04,
		.update_val        = {0x00, 0x04, 0x00, 0x00},
	},
	[AB8500_VUSB] = {
		.name		   = "Vusb",
		.update_regid      = AB8500_REGU_VUSB_CTRL,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x00, 0x03},
	},
	[AB8500_VOTG] = {
		.name		   = "VOTG",
		.update_regid      = AB8500_REGU_OTG_SUPPLY_CTRL,
		.update_mask       = 0x03,
		.update_val        = {0x00, 0x01, 0x00, 0x03},
	},
	[AB8500_VBUSBIS] = {
		.name		   = "Vbusbis",
		.update_regid      = AB8500_REGU_OTG_SUPPLY_CTRL,
		.update_mask       = 0x08,
		.update_val        = {0x00, 0x08, 0x00, 0x00},
	},
};

static void ab9540_ab8505_regulator_characteristics_update(void)
{
	ab8500_regulator[AB8500_VARM].vsel_mask[0] =
		AB9540_AB8505_VARM_VSEL_MASK;
	ab8500_regulator[AB8500_VARM].vsel_range[0] = ab9540_ab8505_varm_vsel;
	ab8500_regulator[AB8500_VARM].vsel_range_len[0] =
		ARRAY_SIZE(ab9540_ab8505_varm_vsel);
	ab8500_regulator[AB8500_VARM].vsel_mask[1] =
		AB9540_AB8505_VARM_VSEL_MASK;
	ab8500_regulator[AB8500_VARM].vsel_range[1] = ab9540_ab8505_varm_vsel;
	ab8500_regulator[AB8500_VARM].vsel_range_len[1] =
		ARRAY_SIZE(ab9540_ab8505_varm_vsel);
	ab8500_regulator[AB8500_VARM].vsel_mask[2] =
		AB9540_AB8505_VARM_VSEL_MASK;
	ab8500_regulator[AB8500_VARM].vsel_range[2] =
		ab9540_ab8505_varm_vsel;
	ab8500_regulator[AB8500_VARM].vsel_range_len[2] =
		ARRAY_SIZE(ab9540_ab8505_varm_vsel);

	ab8500_regulator[AB8500_VBBP].vsel_range[0] = ab9540_ab8505_vbbp_vsel;
	ab8500_regulator[AB8500_VBBP].vsel_range_len[0] =
		ARRAY_SIZE(ab9540_ab8505_vbbp_vsel);
	ab8500_regulator[AB8500_VBBP].vsel_range[1] = ab9540_ab8505_vbbp_vsel;
	ab8500_regulator[AB8500_VBBP].vsel_range_len[1] =
		ARRAY_SIZE(ab9540_ab8505_vbbp_vsel);

	ab8500_regulator[AB8500_VBBN].vsel_range[0] = ab9540_ab8505_vbbn_vsel;
	ab8500_regulator[AB8500_VBBN].vsel_range_len[0] =
		ARRAY_SIZE(ab9540_ab8505_vbbn_vsel);
	ab8500_regulator[AB8500_VBBN].vsel_range[1] = ab9540_ab8505_vbbn_vsel;
	ab8500_regulator[AB8500_VBBN].vsel_range_len[1] =
		ARRAY_SIZE(ab9540_ab8505_vbbn_vsel);
}

static void ab9540_regulators_update(void)
{
	/* Update unavailable regulators */
	ab8500_regulator[AB8500_VREFDDR].unavailable = true;
	ab8500_regulator[AB8500_VAUX4].unavailable = false;
	ab8500_regulator[AB8500_VTVOUT].unavailable = true;

	/* Update regulator characteristics for AB9540 */
	ab9540_ab8505_regulator_characteristics_update();
}

static void ab8505_regulators_update(void)
{
	/* Update unavailable regulators */
	ab8500_regulator[AB8500_VREFDDR].unavailable = true;
	ab8500_regulator[AB8500_VAUX4].unavailable = false;
	ab8500_regulator[AB8500_VAUX5].unavailable = false;
	ab8500_regulator[AB8500_VAUX6].unavailable = false;
	ab8500_regulator[AB8500_VAUX8].unavailable = false;
	ab8500_regulator[AB8500_VEXTSUPPLY1].unavailable = true;
	ab8500_regulator[AB8500_VEXTSUPPLY2].unavailable = true;
	ab8500_regulator[AB8500_VEXTSUPPLY3].unavailable = true;
	ab8500_regulator[AB8500_VDMIC].unavailable = true;
	ab8500_regulator[AB8500_VTVOUT].unavailable = true;

	/* Update regulator characteristics for AB8505 */
	ab9540_ab8505_regulator_characteristics_update();
}

static int status_state = AB8500_REGULATOR_STATE_CURRENT;

static int _get_voltage(struct regulator_volt_range const *volt_range,
	u8 value, int *volt)
{
	u8 start = volt_range->start.value;
	u8 end = volt_range->end.value;
	u8 step = volt_range->step.value;

	/* Check if witin range */
	if (step == 0) {
		if (value == start) {
			*volt = volt_range->start.volt;
			return 1;
		}
	} else {
		if ((start <= value) && (value <= end)) {
			if ((value - start) % step != 0)
				return -EINVAL; /* invalid setting */
			*volt = volt_range->start.volt
			     + volt_range->step.volt
			     *((value - start) / step);
			return 1;
		}
	}

	return 0;
}

static int get_voltage(struct regulator_volt_range const *volt_range,
	int volt_range_len,
	u8 value)
{
	int volt;
	int i, ret;

	for (i = 0; i < volt_range_len; i++) {
		ret = _get_voltage(&volt_range[i], value, &volt);
		if (ret < 0)
			break; /* invalid setting */
		if (ret == 1)
			return volt; /* successful */
	}

	return -EINVAL;
}

static bool get_reg_and_mask(int regid, u8 mask, u8 *val)
{
	int ret;
	u8 t;

	if (!regid)
		return false;

	ret = abx500_get_register_interruptible(dev,
						ab8500_register[regid].bank,
						ab8500_register[regid].addr,
						&t);
	if (ret < 0)
		return false;

	(*val) = t & mask;

	return true;
}

/* Convert regulator register value to index */
static bool val2idx(u8 val, u8 *v, int len, int *idx)
{
	int i;

	for (i = 0; i < len && v[i] != val; i++);

	if (i == len)
		return false;

	(*idx) = i;
	return true;
}

int ab8500_regulator_debug_read(enum ab8500_regulator_id id,
				struct ab8500_debug_regulator_status *s)
{
	int i;
	u8 val;
	bool found;
	int idx = 0;

	if (id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	s->name = (char *)ab8500_regulator[id].name;

	/* read mode */
	(void) get_reg_and_mask(ab8500_regulator[id].update_regid,
				ab8500_regulator[id].update_mask,
				&val);

	(void) val2idx(val, ab8500_regulator[id].update_val,
		       4, &idx);

	s->mode = (u8) idx;

	/* read hw mode */
	found = get_reg_and_mask(ab8500_regulator[id].hw_mode_regid,
				 ab8500_regulator[id].hw_mode_mask,
				 &val);

	if (found)
		found = val2idx(val, ab8500_regulator[id].hw_mode_val, 4, &idx);

	if (found)
		/* +1 since 0 = HWMODE_NONE */
		s->hwmode = idx + 1;
	else
		s->hwmode = AB8500_HWMODE_NONE;

	for (i = 0; i < 4 && found; i++) {

		bool f = get_reg_and_mask(ab8500_regulator[id].hw_valid_regid[i],
					  ab8500_regulator[id].hw_valid_mask[i],
					  &val);
		if (f)
			s->hwmode_auto[i] = !!val;
		else
			s->hwmode_auto[i] = HWM_INVAL;
	}

	/* read voltage */
	found = get_reg_and_mask(ab8500_regulator[id].vsel_sel_regid,
				 ab8500_regulator[id].vsel_sel_mask,
				 &val);
	if (found)
		found = val2idx(val, ab8500_regulator[id].vsel_sel_val,
				3, &idx);

	if (found && idx < 3)
		s->volt_selected = idx + 1;
	else
		s->volt_selected = 0;

	for (s->volt_len = 0; s->volt_len < 3; s->volt_len++) {
		int volt;
		int i = s->volt_len;

		found = get_reg_and_mask(ab8500_regulator[id].vsel_regid[i],
					 ab8500_regulator[id].vsel_mask[i],
					 &val);
		if (!found)
			break;

		volt = get_voltage(ab8500_regulator[id].vsel_range[i],
				   ab8500_regulator[id].vsel_range_len[i],
				   val);
		s->volt[i] = volt;
	}
	return 0;
}

static int ab8500_regulator_status_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int id, regid;
	int i;
	u8 val;
	int err;

	/* record current state */
	ab8500_regulator_record_state(AB8500_REGULATOR_STATE_CURRENT);

	/* check if chosen state is recorded */
	if (!ab8500_register_state_saved[status_state]) {
		seq_printf(s, "ab8500-regulator status is not recorded.\n");
		goto exit;
	}

	/* print dump header */
	err = seq_printf(s, "ab8500-regulator status:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	/* print state */
	for (i = 0; i < NUM_REGULATOR_STATE; i++) {
		if (i == status_state)
			err = seq_printf(s, "-> %i. %12s\n",
				i, regulator_state_name[i]);
		else
			err = seq_printf(s, "   %i. %12s\n",
				i, regulator_state_name[i]);
		if (err < 0)
			dev_err(dev, "seq_printf overflow\n");
	}

	/* print labels */
	err = seq_printf(s,
	      "+-----------+----+--------------+-------------------------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "|       name|man |auto          |voltage                  |\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "+-----------+----+--------------+ +-----------------------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "|           |mode|mode  |0|1|2|3| |    1  |    2  |    3  |\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "+-----------+----+------+-+-+-+-+-+-------+-------+-------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);

	/* dump registers */
	for (id = 0; id < AB8500_NUM_REGULATORS; id++) {
		if (ab8500_register[id].unavailable ||
			ab8500_regulator[id].unavailable)
			continue;

		/* print name */
		err = seq_printf(s, "|%11s|",
			ab8500_regulator[id].name);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				id, __LINE__);

		/* print manual mode */
		regid = ab8500_regulator[id].update_regid;
		val = ab8500_register_state[status_state][regid]
		    & ab8500_regulator[id].update_mask;
		for (i = 0; i < 4; i++) {
			if (val == ab8500_regulator[id].update_val[i])
				break;
		}
		err = seq_printf(s, "%4s|",
			update_val_name[i]);
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				id, __LINE__);

		/* print auto mode */
		regid = ab8500_regulator[id].hw_mode_regid;
		if (regid) {
			val = ab8500_register_state[status_state][regid]
			    & ab8500_regulator[id].hw_mode_mask;
			for (i = 0; i < 4; i++) {
				if (val == ab8500_regulator[id].hw_mode_val[i])
					break;
			}
			err = seq_printf(s, "%6s|",
				hw_mode_val_name[i]);
		} else {
			err = seq_printf(s, "      |");
		}
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				id, __LINE__);

		/* print valid bits */
		for (i = 0; i < 4; i++) {
			regid = ab8500_regulator[id].hw_valid_regid[i];
			if (regid) {
				val = ab8500_register_state[status_state][regid]
				    & ab8500_regulator[id].hw_valid_mask[i];
				if (val)
					err = seq_printf(s, "1|");
				else
					err = seq_printf(s, "0|");
			} else {
				err = seq_printf(s, " |");
			}
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i, %i\n",
					regid, __LINE__);
		}

		/* print voltage selection */
		regid = ab8500_regulator[id].vsel_sel_regid;
		if (regid) {
			val = ab8500_register_state[status_state][regid]
			    & ab8500_regulator[id].vsel_sel_mask;
			for (i = 0; i < 3; i++) {
				if (val == ab8500_regulator[id].vsel_sel_val[i])
					break;
			}
			if (i < 3)
				seq_printf(s, "%i|", i + 1);
			else
				seq_printf(s, "-|");
		} else {
			seq_printf(s, " |");
		}
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				regid, __LINE__);

		for (i = 0; i < 3; i++) {
			int volt;

			regid = ab8500_regulator[id].vsel_regid[i];
			if (regid) {
				val = ab8500_register_state[status_state][regid]
				    & ab8500_regulator[id].vsel_mask[i];
				volt = get_voltage(
					ab8500_regulator[id].vsel_range[i],
					ab8500_regulator[id].vsel_range_len[i],
					val);
				seq_printf(s, "%7i|", volt);
			} else {
				seq_printf(s, "       |");
			}
			if (err < 0)
				dev_err(dev, "seq_printf overflow: %i, %i\n",
					regid, __LINE__);
		}

		err = seq_printf(s, "\n");
		if (err < 0)
			dev_err(dev, "seq_printf overflow: %i, %i\n",
				regid, __LINE__);

	}
	err = seq_printf(s,
	      "+-----------+----+------+-+-+-+-+-+-------+-------+-------+\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);
	err = seq_printf(s,
	      "Note! In HW mode, voltage selection is controlled by HW.\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow: %i\n", __LINE__);


exit:
	return 0;
}

static int ab8500_regulator_status_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;

	/* copy user data */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/* convert */
	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;

	/* set suspend force setting */
	if (user_val > NUM_REGULATOR_STATE) {
		dev_err(dev, "debugfs error input > number of states\n");
		return -EINVAL;
	}

	status_state = user_val;

	return buf_size;
}


static int ab8500_regulator_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab8500_regulator_status_print,
		inode->i_private);
}

static const struct file_operations ab8500_regulator_status_fops = {
	.open = ab8500_regulator_status_open,
	.write = ab8500_regulator_status_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#ifdef CONFIG_PM

struct ab8500_force_reg {
	char *name;
	u8 bank;
	u8 addr;
	u8 mask;
	u8 val;
	bool restore;
	u8 restore_val;
	u8 unavailable;
};

static struct ab8500_force_reg ab8500_force_reg[] = {
	{
		/* Vaux12Regu */
		.name = "Vaux1Regu",
		.bank = 0x04,
		.addr = 0x09,
		.mask = 0x03,
		.val  = 0x03,
 	},
#ifdef CONFIG_MACH_CODINA
 	{
		/* Vaux4Regu */
		.name = "Vaux4Regu",
		.bank = 0x04,
		.addr = 0x2E,
 		.mask = 0x03,
		.val  = 0x03,
	},
#else
	{
		/* Vaux4Regu */
		.name = "Vaux4Regu",
		.bank = 0x04,
		.addr = 0x2E,
 		.mask = 0x03,
		.val  = 0x00,
	},
#endif
};

static void ab8500_force_reg_update(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ab8500_force_reg); i++) {
		if (ab8500_force_reg[i].bank == 0x02 &&
		    ab8500_force_reg[i].addr == 0x0C) {
			/*
			 * SysClkCtrl
			 * OTP: 0x00, HSI: 0x06, suspend: 0x00/0x07 (value/mask)
			 * [  2] USBClkEna = disable SysClk path to USB block
			 */
			ab8500_force_reg[i].mask = 0x04;
			ab8500_force_reg[i].val  = 0x00;
		} else if (ab8500_force_reg[i].bank == 0x06 &&
			   ab8500_force_reg[i].addr == 0x80) {
			/* TVoutCtrl not supported by AB9540/AB8505 */
			ab8500_force_reg[i].unavailable = true;
		}
	}
}

void ab8500_regulator_debug_force(void)
{
	int ret, i;

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_SUSPEND);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record suspend state.\n");

	/* check if registers should be forced */
	if (!setting_suspend_force)
		goto exit;

	/*
	 * Optimize href v2_v50_pwr board for ApSleep/ApDeepSleep
	 * power consumption measurements
	 */

	for (i = 0; i < ARRAY_SIZE(ab8500_force_reg); i++) {
		if (ab8500_force_reg[i].unavailable)
			continue;

		dev_vdbg(&pdev->dev, "Save and set %s: "
			"0x%02x, 0x%02x, 0x%02x, 0x%02x.\n",
			ab8500_force_reg[i].name,
			ab8500_force_reg[i].bank,
			ab8500_force_reg[i].addr,
			ab8500_force_reg[i].mask,
			ab8500_force_reg[i].val);

		/* assume that register should be restored */
		ab8500_force_reg[i].restore = true;

		/* get register value before forcing it */
		ret = abx500_get_register_interruptible(&pdev->dev,
			ab8500_force_reg[i].bank,
			ab8500_force_reg[i].addr,
			&ab8500_force_reg[i].restore_val);
		if (ret < 0) {
			dev_err(dev, "Failed to read %s.\n",
				ab8500_force_reg[i].name);
			ab8500_force_reg[i].restore = false;
			break;
		}

		/* force register value */
		ret = abx500_mask_and_set_register_interruptible(&pdev->dev,
			ab8500_force_reg[i].bank,
			ab8500_force_reg[i].addr,
			ab8500_force_reg[i].mask,
			ab8500_force_reg[i].val);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to write %s.\n",
				ab8500_force_reg[i].name);
			ab8500_force_reg[i].restore = false;
		}
	}

exit:
	/* save state of registers */
	ret = ab8500_regulator_record_state(
		AB8500_REGULATOR_STATE_SUSPEND_CORE);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record suspend state.\n");

	return;
}

void ab8500_regulator_debug_restore(void)
{
	int ret, i;

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_RESUME_CORE);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record resume state.\n");
	for (i = ARRAY_SIZE(ab8500_force_reg) - 1; i >= 0; i--) {
		if (ab8500_force_reg[i].unavailable)
			continue;

		/* restore register value */
		if (ab8500_force_reg[i].restore) {
			ret = abx500_mask_and_set_register_interruptible(
				 &pdev->dev,
				 ab8500_force_reg[i].bank,
				 ab8500_force_reg[i].addr,
				 ab8500_force_reg[i].mask,
				 ab8500_force_reg[i].restore_val);
			if (ret < 0)
				dev_err(&pdev->dev, "Failed to restore %s.\n",
					ab8500_force_reg[i].name);
			dev_vdbg(&pdev->dev, "Restore %s: "
				"0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
				ab8500_force_reg[i].name,
				ab8500_force_reg[i].bank,
				ab8500_force_reg[i].addr,
				ab8500_force_reg[i].mask,
				ab8500_force_reg[i].restore_val);
		}
	}

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_RESUME);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to record resume state.\n");

	return;
}

#endif

static int ab8500_regulator_suspend_force_show(struct seq_file *s, void *p)
{
	/* print suspend standby status */
	if (setting_suspend_force)
		return seq_printf(s, "suspend force enabled\n");
	else
		return seq_printf(s, "no suspend force\n");
}

static int ab8500_regulator_suspend_force_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;

	/* copy user data */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/* convert */
	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;

	/* set suspend force setting */
	if (user_val > 1) {
		dev_err(dev, "debugfs error input > 1\n");
		return -EINVAL;
	}

	if (user_val)
		setting_suspend_force = true;
	else
		setting_suspend_force = false;

	return buf_size;
}

static int ab8500_regulator_suspend_force_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, ab8500_regulator_suspend_force_show,
		inode->i_private);
}

static const struct file_operations ab8500_regulator_suspend_force_fops = {
	.open = ab8500_regulator_suspend_force_open,
	.write = ab8500_regulator_suspend_force_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab8500_regulator_dir;
static struct dentry *ab8500_regulator_dump_file;
static struct dentry *ab8500_regulator_status_file;
static struct dentry *ab8500_regulator_suspend_force_file;

int __devinit ab8500_regulator_debug_init(struct platform_device *plf)
{
	void __iomem *boot_info_backupram;
	int ret;
	struct ab8500 *ab8500;

	/* setup dev pointers */
	dev = &plf->dev;
	pdev = plf;

	/* save state of registers */
	ret = ab8500_regulator_record_state(AB8500_REGULATOR_STATE_INIT);
	if (ret < 0)
		dev_err(&plf->dev, "Failed to record init state.\n");

        ab8500 = dev_get_drvdata(plf->dev.parent);
	/* Update data structures for AB9540 */
	if (is_ab9540(ab8500)) {
		ab9540_registers_update();
		ab9540_regulators_update();
		ab8500_force_reg_update();
	} else if (is_ab8505(ab8500)) {
		/* Update data structures for AB8505 */
		ab8505_registers_update();
		ab8505_regulators_update();
		ab8500_force_reg_update();
	}
	/* make suspend-force default if board profile is v5x-power */
	boot_info_backupram = ioremap(BOOT_INFO_BACKUPRAM1, 0x4);

	if (boot_info_backupram) {
		u8 board_profile;
		board_profile = readb(
			boot_info_backupram + BOARD_PROFILE_BACKUPRAM1);
		dev_dbg(dev, "Board profile is 0x%02x\n", board_profile);

		if (board_profile >= OPTION_BOARD_VERSION_500_V5X)
			setting_suspend_force = true;

		iounmap(boot_info_backupram);
	} else {
		dev_err(dev, "Failed to read backupram.\n");
	}

	/* create directory */
	ab8500_regulator_dir = debugfs_create_dir("ab8500-regulator", NULL);
	if (!ab8500_regulator_dir)
		goto exit_no_debugfs;

	/* create "dump" file */
	ab8500_regulator_dump_file = debugfs_create_file("dump",
		S_IRUGO, ab8500_regulator_dir, &plf->dev,
		&ab8500_regulator_dump_fops);
	if (!ab8500_regulator_dump_file)
		goto exit_destroy_dir;

	/* create "status" file */
	ab8500_regulator_status_file = debugfs_create_file("status",
		S_IRUGO, ab8500_regulator_dir, &plf->dev,
		&ab8500_regulator_status_fops);
	if (!ab8500_regulator_status_file)
		goto exit_destroy_dump_file;

	/*
	 * create "suspend-force-v5x" file. As indicated by the name, this is
	 * only applicable for v2_v5x hardware versions.
	 */
	ab8500_regulator_suspend_force_file = debugfs_create_file(
		"suspend-force-v5x",
		S_IRUGO, ab8500_regulator_dir, &plf->dev,
		&ab8500_regulator_suspend_force_fops);
	if (!ab8500_regulator_suspend_force_file)
		goto exit_destroy_status_file;

	return 0;

exit_destroy_status_file:
	debugfs_remove(ab8500_regulator_status_file);
exit_destroy_dump_file:
	debugfs_remove(ab8500_regulator_dump_file);
exit_destroy_dir:
	debugfs_remove(ab8500_regulator_dir);
exit_no_debugfs:
	dev_err(&plf->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

int __devexit ab8500_regulator_debug_exit(struct platform_device *plf)
{
	debugfs_remove(ab8500_regulator_suspend_force_file);
	debugfs_remove(ab8500_regulator_status_file);
	debugfs_remove(ab8500_regulator_dump_file);
	debugfs_remove(ab8500_regulator_dir);

	return 0;
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bengt Jonsson <bengt.g.jonsson@stericsson.com");
MODULE_DESCRIPTION("AB8500 Regulator Debug");
MODULE_ALIAS("platform:ab8500-regulator-debug");
