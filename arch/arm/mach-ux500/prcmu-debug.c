/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Martin Persson for ST-Ericsson
 *         Etienne Carriere <etienne.carriere@stericsson.com> for ST-Ericsson
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>
#include <linux/mfd/dbx500-prcmu.h>

#define MAX_STATES 6
#define MAX_NAMELEN 16
#define U8500_PRCMU_TCDM_SIZE 4096

struct state_history {
	ktime_t start;
	u32 state;
	u32 counter[MAX_STATES];
	u8 opps[MAX_STATES];
	int max_states;
	int req;
	bool reqs[MAX_STATES];
	ktime_t time[MAX_STATES];
	int state_names[MAX_STATES];
	char name[MAX_NAMELEN];
	spinlock_t lock;
};

static struct state_history ape_sh = {
	.name = "APE OPP",
	.req = PRCMU_QOS_APE_OPP,
	.opps = {APE_50_OPP, APE_100_OPP},
	.state_names = {50, 100},
	.max_states = 2,
};

static struct state_history ddr_sh = {
	.name = "DDR OPP",
	.req = PRCMU_QOS_DDR_OPP,
	.opps = {DDR_25_OPP, DDR_50_OPP, DDR_100_OPP},
	.state_names = {25, 50, 100},
	.max_states = 3,
};

static struct state_history vsafe_sh = {
	.name = "VSAFE",
	.req = PRCMU_QOS_VSAFE_OPP,
	.opps = {VSAFE_50_OPP, VSAFE_100_OPP},
	.state_names = {50, 100},
	.max_states = 2,
};

static struct state_history arm_sh = {
	.name = "ARM KHZ",
	.req = PRCMU_QOS_ARM_KHZ,
	.opps = {ARM_EXTCLK, ARM_50_OPP, ARM_100_OPP, ARM_MAX_OPP},
	.max_states = 4,
};

static const u16 u8500_prcmu_dump_regs[] = {
	/*ARMCLKFIX_MGT*/ 0x0,			/*ACLK_MGT*/ 0x4,
	/*SVAMMDSPCLK_MGT*/ 0x8,		/*SIAMMDSPCLK_MGT*/ 0xc,
	/*SGACLK_MGT*/ 0x14,			/*UARTCLK_MGT*/ 0x18,
	/*MSP02CLK_MGT*/ 0x1c,			/*I2CCLK_MGT*/ 0x20,
	/*SDMMCCLK_MGT*/ 0x24,			/*SLIMCLK_MGT*/ 0x28,
	/*PER1CLK_MGT*/ 0x2c,			/*PER2CLK_MGT*/ 0x30,
	/*PER3CLK_MGT*/ 0x34,			/*PER5CLK_MGT*/ 0x38,
	/*PER6CLK_MGT*/ 0x3c,			/*PER7CLK_MGT*/ 0x40,
	/*LCDCLK_MGT*/ 0x44,			/*SPARE1CLK_MGT*/ 0x48,
	/*BMLCLK_MGT*/ 0x4c,			/*HSITXCLK_MGT*/ 0x50,
	/*HSIRXCLK_MGT*/ 0x54,			/*HDMICLK_MGT*/ 0x58,
	/*APEATCLK_MGT*/ 0x5c,			/*APETRACECLK_MGT*/ 0x60,
	/*MCDECLK_MGT*/ 0x64,			/*IPI2CCLK_MGT*/ 0x68,
	/*DSIALTCLK_MGT*/ 0x6c,			/*SPARE2CLK_MGT*/ 0x70,
	/*DMACLK_MGT*/ 0x74,			/*B2R2CLK_MGT*/ 0x78,
	/*TVCLK_MGT*/ 0x7c,			/*PLLSOC0_FREQ*/ 0x80,
	/*PLLSOC1_FREQ*/ 0x84,			/*PLLARM_FREQ*/ 0x88,
	/*PLLDDR_FREQ*/ 0x8c,			/*PLLSOC0_ENABLE*/ 0x90,
	/*PLLSOC1_ENABLE*/ 0x94,		/*PLLARM_ENABLE*/ 0x98,
	/*PLLDDR_ENABLE*/ 0x9c,			/*PLLSOC0_LOCKP*/ 0xa0,
	/*PLLSOC1_LOCKP*/ 0xa4,			/*PLLARM_LOCKP*/ 0xa8,
	/*PLLDDR_LOCKP*/ 0xac,			/*XP70CLK_MGT*/ 0xb0,
	/*TIMER_0_REF*/ 0xb4,			/*TIMER_0_DOWNCOUNT*/ 0xb8,
	/*TIMER_0_MODE*/ 0xbc,			/*TIMER_1_REF*/ 0xc0,
	/*TIMER_1_DOWNCOUNT*/ 0xc4,		/*TIMER_1_MODE*/ 0xc8,
	/*TIMER_2_REF*/ 0xcc,			/*TIMER_2_DOWNCOUNT*/ 0xd0,
	/*TIMER_2_MODE*/ 0xd4,			/*CLK009_MGT*/ 0xe4,
	/*MODECLK*/ 0xe8,			/*4500_CLK_REQ*/ 0xf8,
	/*MBOX_CPU_VAL*/ 0xfc,			/*PLL32K_ENABLE*/ 0x10c,
	/*PLL32K_LOCKP*/ 0x110,			/*ARM_CHGCLKREQ*/ 0x114,
	/*ARM_PLLDIVPS*/ 0x118,			/*ARMITMSK31TO0*/ 0x11c,
	/*ARMITMSK63TO32*/ 0x120,		/*ARMITMSK95TO64*/ 0x124,
	/*ARMITMSK127TO96*/ 0x128,		/*ARMSTANDBY_STATUS*/ 0x130,
	/*CGATING_BYPASS*/ 0x134,		/*GPIOCR*/ 0x138,
	/*LEMI*/ 0x13c,				/*COMPCR*/ 0x140,
	/*COMPSTA*/ 0x144,			/*ITSTATUS0*/ 0x148,
	/*ITSTATUS1*/ 0x150,			/*ITSTATUS2*/ 0x158,
	/*ITSTATUS3*/ 0x160,			/*ITSTATUS4*/ 0x168,
	/*LINE_VALUE*/ 0x170,			/*HOLD_EVT*/ 0x174,
	/*EDGE_SENS_L*/ 0x178,			/*EDGE_SENS_H*/ 0x17c,
	/*DEBUG_CTRL_VAL*/ 0x190,		/*DEBUG_NOPWRDOWN_VAL*/ 0x194,
	/*DEBUG_CTRL_ACK*/ 0x198,		/*A9PL_FORCE_CLKEN*/ 0x19c,
	/*TPIU_FLUSHIN_REQ*/ 0x1a0,		/*TPIU_FLUSHIN_ACK*/ 0x1a4,
	/*STP_FLUSHIN_REQ*/ 0x1a8,		/*STP_FLUSHIN_ACK*/ 0x1ac,
	/*HWI2C_DIV*/ 0x1b0,			/*HWI2C_CMD*/ 0x1b8,
	/*HWI2C_DATA123*/ 0x1bc,		/*HWI2C_SR*/ 0x1c0,
	/*REMAPCR*/ 0x1c4,			/*TCR*/ 0x1c8,
	/*CLKOCR*/ 0x1cc,			/*ITSTATUS_DBG*/ 0x1d0,
	/*LINE_VALUE_DBG*/ 0x1d8,		/*DBG_HOLD*/ 0x1dc,
	/*EDGE_SENS_DBG*/ 0x1e0,		/*APE_RESETN_VAL*/ 0x1ec,
	/*A9_RESETN_SET*/ 0x1f0,		/*A9_RESETN_VAL*/ 0x1f8,
	/*MOD_RESETN_VAL*/ 0x204,		/*GPIO_RESETN_VAL*/ 0x210,
	/*4500_RESETN_VAL*/ 0x21c,		/*SWD_RST_TEMPO*/ 0x238,
	/*RST_4500_TEMPO*/ 0x23c,		/*SVAMMDSP_IT*/ 0x240,
	/*SIAMMDSP_IT*/ 0x248,			/*POWER_STATE_VAL*/ 0x25c,
	/*ARMITVALUE31TO0*/ 0x260,		/*ARMITVALUE63TO32*/ 0x264,
	/*ARMITVALUE95TO64*/ 0x268,		/*ARMITVALUE127TO96*/ 0x26c,
	/*REDUN_LOAD*/ 0x270,			/*REDUN_STATUS*/ 0x274,
	/*UNIPROCLK_MGT*/ 0x278,		/*UICCCLK_MGT*/ 0x27c,
	/*SSPCLK_MGT*/ 0x280,			/*RNGCLK_MGT*/ 0x284,
	/*MSP1CLK_MGT*/ 0x288,			/*DAP_RESETN_SET*/ 0x2a0,
	/*DAP_RESETN_VAL*/ 0x2a8,		/*SRAM_DEDCSTOV*/ 0x300,
	/*SRAM_LS_SLEEP*/ 0x304,		/*SRAM_A9*/ 0x308,
	/*ARM_LS_CLAMP*/ 0x30c,			/*IOCR*/ 0x310,
	/*MODEM_SYSCLKOK*/ 0x314,		/*SYSCLKOK_DELAY*/ 0x318,
	/*SYSCLKSTATUS*/ 0x31c,			/*DSI_SW_RESET*/ 0x324,
	/*A9_MASK_REQ*/ 0x328,			/*A9_MASK_ACK*/ 0x32c,
	/*HOSTACCESS_REQ*/ 0x334,		/*TIMER_3_REF*/ 0x338,
	/*TIMER_3_DOWNCOUNT*/ 0x33c,		/*TIMER_3_MODE*/ 0x340,
	/*PMB_SENS_CTRL*/ 0x344,		/*PMB_REF_COUNTER*/ 0x348,
	/*PMB_SENSOR_STATUS*/ 0x34c,		/*APE_EPOD_CFG*/ 0x404,
	/*DDR_EPOD_CFG*/ 0x408,			/*EPOD_C_VAL*/ 0x418,
	/*EPOD_VOK*/ 0x41c,			/*MMIP_LS_CLAMP_VAL*/ 0x428,
	/*VSAFE_LS_CLAMP_VAL*/ 0x434,		/*DDRSUBSYS_APE_MINBW*/ 0x438,
	/*DDRSUBSYS_STATUS*/ 0x43c,		/*DDRSUBSYS_CONTROL*/ 0x440,
	/*DDRSUBSYS_HIGH_LEAK_COND*/ 0x444,	/*DDRSUBSYS_CONFIG*/ 0x448,
	/*TIMER_4_REF*/ 0x450,			/*TIMER_4_DOWNCOUNT*/ 0x454,
	/*TIMER_4_MODE*/ 0x458,			/*TIMER_5_REF*/ 0x45c,
	/*TIMER_5_DOWNCOUNT*/ 0x460,		/*TIMER_5_MODE*/ 0x464,
	/*APE_MEM_REQ*/ 0x470,			/*DBG_FRCS_APE_MEM_REQ*/ 0x474,
	/*APE_MEM_WFX_EN*/ 0x478,		/*APE_MEM_LATENCY*/ 0x47c,
	/*APE_MEM_ACK*/ 0x480,			/*ITSTATUS5*/ 0x484,
	/*ARM_IT1_VAL*/ 0x494,			/*MOD_PWR_OK*/ 0x498,
	/*MOD_AUXCLKOK*/ 0x49c,			/*MOD_AWAKE_STATUS*/ 0x4a0,
	/*MOD_SWRESET_IRQ_ACK*/ 0x4a4,		/*MOD_SWRESET_ACK*/ 0x4a8,
	/*DBG_PWRCTL*/ 0x4ac,			/*HWOBS_H*/ 0x4b0,
	/*HWOBS_L*/ 0x4b4,			/*PLLDSI_FREQ*/ 0x500,
	/*PLLDSI_ENABLE*/ 0x504,		/*PLLDSI_LOCKP*/ 0x508,
	/*RNG_ENABLE*/ 0x50c,			/*YYCLKEN0_MGT_SET*/ 0x510,
	/*YYCLKEN0_MGT_VAL*/ 0x520,		/*YYCLKEN1_MGT_VAL*/ 0x524,
	/*XP70CLK_MGT2*/ 0x528,			/*DSITVCLK_DIV*/ 0x52c,
	/*DSI_PLLOUT_SEL*/ 0x530,		/*DSI_GLITCHFREE_EN*/ 0x534,
	/*CLKACTIV*/ 0x538,			/*SIA_MMDSP_MEM_MGT*/ 0x53c,
	/*SVA_MMDSP_MEM_MGT*/ 0x540,		/*SXAMMDSP_FORCE_CLKEN*/ 0x544,
	/*UICC_NANDTREE*/ 0x570,		/*GPIOCR2*/ 0x574,
	/*MDM_ACWAKE*/ 0x578,			/*MOD_MEM_REQ*/ 0x5a4,
	/*MOD_MEM_ACK*/ 0x5a8,			/*ARM_PLLDIVPS_REQ*/ 0x5b0,
	/*ARM_PLLDIVPS_ACK*/ 0x5b4,		/*SRPTIMER_VAL*/ 0x5d0,
};

/* Offsets from secure base which is U8500_PRCMU_BASE + SZ_4K */
static const u16 u8500_prcmu_dump_secure_regs[] = {
	/*SECNONSEWM*/ 0x00,		/*ESRAM0_INITN*/ 0x04,
	/*ARMITMSKSEC_31TO0*/ 0x08,	/*ARMITMSKSEC_63TO32*/ 0x0C,
	/*ARMITMSKSEC_95TO64*/ 0x10,	/*ARMITMSKSEC_127TO96*/ 0x14,
	/*ARMIT_MASKXP70_IT*/ 0x18,	/*ESRAM0_EPOD_CFG*/ 0x1C,
	/*ESRAM0_EPOD_C_VAL*/ 0x20,	/*ESRAM0_EPOD_VOK*/ 0x2C,
	/*ESRAM0_LS_SLEEP*/ 0x30,	/*SECURE_ONGOING*/ 0x34,
	/*I2C_SECURE*/ 0x38,		/*RESET_STATUS*/ 0x3C,
	/*PERIPH4_RESETN_VAL*/ 0x48,	/*SPAREOUT_SEC*/ 0x4C,
	/*PIPELINEDCR*/ 0xD8,
};

static int ape_voltage_count;

static void log_set(struct state_history *sh, u8 opp)
{
	ktime_t now;
	ktime_t dtime;
	unsigned long flags;
	int state;

	now = ktime_get();
	spin_lock_irqsave(&sh->lock, flags);

	for (state = 0 ; sh->opps[state] != opp; state++)
		;
	BUG_ON(state >= sh->max_states);

	dtime = ktime_sub(now, sh->start);
	sh->time[sh->state] = ktime_add(sh->time[sh->state], dtime);
	sh->start = now;
	sh->counter[sh->state]++;
	sh->state = state;

	spin_unlock_irqrestore(&sh->lock, flags);
}

void prcmu_debug_ape_opp_log(u8 opp)
{
	if (opp == APE_50_PARTLY_25_OPP)
		opp = APE_50_OPP;

	log_set(&ape_sh, opp);
}

void prcmu_debug_ddr_opp_log(u8 opp)
{
	log_set(&ddr_sh, opp);
}


void prcmu_debug_vsafe_opp_log(u8 opp)
{
	log_set(&vsafe_sh, opp);
}

/*
 * value will be a u8 enum if logging U8500 ARM OPPs
 * value will be a u32 freq (kHz) if logging U9540 ARM OPPs
 */
void prcmu_debug_arm_opp_log(u32 value)
{
	log_set(&arm_sh, value);
}

static void log_reset(struct state_history *sh)
{
	unsigned long flags;
	int i;

	pr_info("reset\n");

	spin_lock_irqsave(&sh->lock, flags);
	for (i = 0; i < sh->max_states; i++) {
		sh->counter[i] = 0;
		sh->time[i] = ktime_set(0, 0);
	}

	sh->start = ktime_get();
	spin_unlock_irqrestore(&sh->lock, flags);

}

static ssize_t ape_stats_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	log_reset(&ape_sh);
	return count;
}

static ssize_t ddr_stats_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	log_reset(&ddr_sh);
	return count;
}

static ssize_t vsafe_stats_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	log_reset(&vsafe_sh);
	return count;
}

static ssize_t arm_stats_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	log_reset(&arm_sh);
	return count;
}

static int log_print(struct seq_file *s, struct state_history *sh)
{
	int i;
	unsigned long flags;
	ktime_t total;
	ktime_t dtime;
	s64 t_ms;
	s64 perc;
	s64 total_ms;

	spin_lock_irqsave(&sh->lock, flags);

	dtime = ktime_sub(ktime_get(), sh->start);

	total = dtime;

	for (i = 0; i < sh->max_states; i++)
		total = ktime_add(total, sh->time[i]);
	total_ms = ktime_to_ms(total);

	for (i = 0; i < sh->max_states; i++) {
		ktime_t t = sh->time[i];
		if (sh->state == i)
			t = ktime_add(t, dtime);

		t_ms = ktime_to_ms(t);
		perc = 100 * t_ms;
		do_div(perc, total_ms);

		seq_printf(s, "%s %d: # %u in %lld ms %d%%\n",
			   sh->name, sh->state_names[i],
			   sh->counter[i] + (int)(sh->state == i),
			   t_ms, (u32)perc);

	}
	spin_unlock_irqrestore(&sh->lock, flags);
	return 0;
}

static int ape_stats_print(struct seq_file *s, void *p)
{
	log_print(s, &ape_sh);
	return 0;
}

static int ddr_stats_print(struct seq_file *s, void *p)
{
	log_print(s, &ddr_sh);
	return 0;
}

static int vsafe_stats_print(struct seq_file *s, void *p)
{
	log_print(s, &vsafe_sh);
	return 0;
}

static int arm_stats_print(struct seq_file *s, void *p)
{
	log_print(s, &arm_sh);
	return 0;
}

static int opp_read(struct seq_file *s, void *p)
{
	int opp;
	struct state_history *sh = (struct state_history *)s->private;

	switch (sh->req) {
	case PRCMU_QOS_DDR_OPP:
		opp = prcmu_get_ddr_opp();
		seq_printf(s, "%s (%d)\n",
			   (opp == DDR_100_OPP) ? "100%" :
			   (opp == DDR_50_OPP) ? "50%" :
			   (opp == DDR_25_OPP) ? "25%" :
			   "unknown", opp);
		break;
	case PRCMU_QOS_APE_OPP:
		opp = prcmu_get_ape_opp();
		seq_printf(s, "%s (%d)\n",
			   (opp == APE_100_OPP) ? "100%" :
			   (opp == APE_50_OPP) ? "50%" :
			   "unknown", opp);
		break;
	case PRCMU_QOS_VSAFE_OPP:
		opp = prcmu_get_vsafe_opp();
		seq_printf(s, "%s (%d)\n",
			   (opp == VSAFE_100_OPP) ? "100%" :
			   (opp == VSAFE_50_OPP) ? "50%" :
			   "unknown", opp);
		break;
	case PRCMU_QOS_ARM_KHZ:
		opp = prcmu_get_arm_opp();
		seq_printf(s, "%d kHz (OPP %s %d)\n", cpufreq_get(0),
			   (opp == ARM_MAX_OPP) ? "max" :
			   (opp == ARM_MAX_FREQ100OPP) ? "max-freq100" :
			   (opp == ARM_100_OPP) ? "100%" :
			   (opp == ARM_50_OPP) ? "50%" :
			   (opp == ARM_EXTCLK) ? "25% (extclk)" :
			   "unknown", opp);
		break;
	default:
		break;
	}
	return 0;

}

static ssize_t opp_write(struct file *file,
			 const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	long unsigned i;
	int err;
	struct state_history *sh = (struct state_history *)
		((struct seq_file *)file->private_data)->private;

	err = kstrtoul_from_user(user_buf, count, 0, &i);

	if (err)
		return err;

	prcmu_qos_force_opp(sh->req, i);

	pr_info("prcmu debug: forced %s to %d\n",
		sh->name, (int)i);

	return count;
}

static int cpufreq_delay_read(struct seq_file *s, void *p)
{
	return seq_printf(s, "%lu\n", prcmu_qos_get_cpufreq_opp_delay());
}

static int ape_voltage_read(struct seq_file *s, void *p)
{
	return seq_printf(s, "This reference count only includes "
			  "requests via debugfs.\nCount: %d\n",
			  ape_voltage_count);
}

static ssize_t ape_voltage_write(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	long unsigned i;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &i);

	if (err)
		return err;

	switch (i) {
	case 0:
		if (ape_voltage_count == 0)
			pr_info("prcmu debug: reference count is already 0\n");
		else {
			err = prcmu_request_ape_opp_100_voltage(false);
			if (err)
				pr_err("prcmu debug: drop request failed\n");
			else
				ape_voltage_count--;
		}
		break;
	case 1:
		err = prcmu_request_ape_opp_100_voltage(true);
		if (err)
			pr_err("prcmu debug: request failed\n");
		else
			ape_voltage_count++;
		break;
	default:
		pr_info("prcmu debug: value not equal to 0 or 1\n");
	}
	return count;
}

static ssize_t cpufreq_delay_write(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	int err;
	long unsigned i;

	err = kstrtoul_from_user(user_buf, count, 0, &i);

	if (err)
		return err;

	prcmu_qos_set_cpufreq_opp_delay(i);

	pr_info("prcmu debug: changed delay between cpufreq change and QoS "
		 "requirement to %lu.\n", i);

	return count;
}

/* These are only for u8500 */
#define PRCM_AVS_BASE		0x2FC
#define AVS_VBB_RET		0x0
#define AVS_VBB_MAX_OPP		0x1
#define AVS_VBB_100_OPP		0x2
#define AVS_VBB_50_OPP		0x3
#define AVS_VARM_MAX_OPP	0x4
#define AVS_VARM_100_OPP	0x5
#define AVS_VARM_50_OPP		0x6
#define AVS_VARM_RET		0x7
#define AVS_VAPE_100_OPP	0x8
#define AVS_VAPE_50_OPP		0x9
#define AVS_VMOD_100_OPP	0xA
#define AVS_VMOD_50_OPP		0xB
#define AVS_VSAFE		0xC
#define AVS_VSAFE_RET		0xD
#define AVS_SIZE		14

static int avs_read(struct seq_file *s, void *p)
{

	u8 avs[AVS_SIZE];
	void __iomem *tcdm_base;

	if (cpu_is_u8500()) {
		tcdm_base = __io_address(U8500_PRCMU_TCDM_BASE);

		memcpy_fromio(avs, tcdm_base + PRCM_AVS_BASE, AVS_SIZE);

		seq_printf(s, "VBB_RET      : 0x%02x\n", avs[AVS_VBB_RET]);
		seq_printf(s, "VBB_MAX_OPP  : 0x%02x\n", avs[AVS_VBB_MAX_OPP]);
		seq_printf(s, "VBB_100_OPP  : 0x%02x\n", avs[AVS_VBB_100_OPP]);
		seq_printf(s, "VBB_50_OPP   : 0x%02x\n", avs[AVS_VBB_50_OPP]);
		seq_printf(s, "VARM_MAX_OPP : 0x%02x\n", avs[AVS_VARM_MAX_OPP]);
		seq_printf(s, "VARM_100_OPP : 0x%02x\n", avs[AVS_VARM_100_OPP]);
		seq_printf(s, "VARM_50_OPP  : 0x%02x\n", avs[AVS_VARM_50_OPP]);
		seq_printf(s, "VARM_RET     : 0x%02x\n", avs[AVS_VARM_RET]);
		seq_printf(s, "VAPE_100_OPP : 0x%02x\n", avs[AVS_VAPE_100_OPP]);
		seq_printf(s, "VAPE_50_OPP  : 0x%02x\n", avs[AVS_VAPE_50_OPP]);
		seq_printf(s, "VMOD_100_OPP : 0x%02x\n", avs[AVS_VMOD_100_OPP]);
		seq_printf(s, "VMOD_50_OPP  : 0x%02x\n", avs[AVS_VMOD_50_OPP]);
		seq_printf(s, "VSAFE        : 0x%02x\n", avs[AVS_VSAFE]);
		seq_printf(s, "VSAFE_RET    : 0x%02x\n", avs[AVS_VSAFE_RET]);
	} else {
		seq_printf(s, "Only u8500 supported.\n");
	}

	return 0;
}

static void prcmu_data_mem_print(struct seq_file *s)
{
	int i;
	int err;
	void __iomem *tcdm_base;
	u32 dmem[4];

	if (cpu_is_u8500()) {
		tcdm_base = __io_address(U8500_PRCMU_TCDM_BASE);

		for (i = 0; i < U8500_PRCMU_TCDM_SIZE; i += 16) {
			dmem[0] = readl(tcdm_base + i +  0);
			dmem[1] = readl(tcdm_base + i +  4);
			dmem[2] = readl(tcdm_base + i +  8);
			dmem[3] = readl(tcdm_base + i + 12);

			if (s) {
				err = seq_printf(s,
					"0x%x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
					((int)tcdm_base) + i, dmem[0],
					dmem[1], dmem[2], dmem[3]);
				if (err < 0) {
					pr_err("%s: seq_printf overflow, addr=%x\n",
						__func__, ((int)tcdm_base) + i);
					/* Can't do much here */
					return;
				}
			} else {
				printk(KERN_INFO
					"0x%x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
					((int)tcdm_base) + i, dmem[0],
					dmem[1], dmem[2], dmem[3]);
			}
		}
	}
}

void prcmu_debug_dump_data_mem(void)
{
	printk(KERN_INFO "PRCMU data memory dump:\n");
	prcmu_data_mem_print(NULL);
}

static int prcmu_debugfs_data_mem_read(struct seq_file *s, void *p)
{
	seq_printf(s, "PRCMU data memory:\n");
	prcmu_data_mem_print(s);

	return 0;
}

static void prcmu_regs_print(struct seq_file *s)
{
	int i;
	int err;
	void __iomem *prcmu_base;
	u32 reg_val;

	if (cpu_is_u8500()) {
		prcmu_base = __io_address(U8500_PRCMU_BASE);

		for (i = 0; i < ARRAY_SIZE(u8500_prcmu_dump_regs); i++) {
			reg_val = readl(prcmu_base +
				u8500_prcmu_dump_regs[i]);

			if (s) {
				err = seq_printf(s, "0x%04x: 0x%08x\n",
					u8500_prcmu_dump_regs[i], reg_val);
				if (err < 0) {
					pr_err("%s: seq_printf overflow,"
						"offset=%x\n", __func__,
						u8500_prcmu_dump_regs[i]);
					/* Can't do much here */
					return;
				}
			} else {
				printk(KERN_INFO
					"0x%04x: 0x%08x\n",
					u8500_prcmu_dump_regs[i], reg_val);
			}
		}
	}
}

static void prcmu_secure_regs_print(struct seq_file *s)
{
	int i;
	int err;
	void __iomem *prcmu_sec_base;
	u32 reg_val;

	if (cpu_is_u8500()) {
		/* PRCMU secure base starts after SZ_4K */
		prcmu_sec_base = ioremap(U8500_PRCMU_BASE + SZ_4K, SZ_4K);
		if (!prcmu_sec_base) {
			pr_err("%s: ioremap faild\n", __func__);
			return;
		}

		for (i = 0; i < ARRAY_SIZE(u8500_prcmu_dump_secure_regs); i++) {
			reg_val = readl(prcmu_sec_base +
				u8500_prcmu_dump_secure_regs[i]);

			if (s) {
				err = seq_printf(s, "0x%04x: 0x%08x\n",
					u8500_prcmu_dump_secure_regs[i] +
					SZ_4K,
					reg_val);
				if (err < 0) {
					pr_err("%s: seq_printf overflow,"
					"offset=%x\n", __func__,
					u8500_prcmu_dump_secure_regs[i] +
					SZ_4K);
					/* Can't do much here */
					break;
				}
			} else {
				printk(KERN_INFO
					"0x%04x: 0x%08x\n",
					u8500_prcmu_dump_secure_regs[i] +
					SZ_4K,
					reg_val);
			}
		}

		iounmap(prcmu_sec_base);
	}
}

void prcmu_debug_dump_regs(void)
{
	printk(KERN_INFO "PRCMU registers dump:\n");
	prcmu_regs_print(NULL);
	prcmu_secure_regs_print(NULL);
}

static int prcmu_debugfs_regs_read(struct seq_file *s, void *p)
{
	seq_printf(s, "PRCMU registers:\n");
	prcmu_regs_print(s);
	prcmu_secure_regs_print(s);
	return 0;
}

/* Interrupt debugging */

/* There are eight mailboxes */
#define NUM_MAILBOXES 8
#define NUM_MAILBOX0_EVENTS 32
static u32 num_mailbox_interrupts[NUM_MAILBOXES];
static u32 num_mailbox0_events[NUM_MAILBOX0_EVENTS];
static u32 num_mailbox0_events_garbage[NUM_MAILBOX0_EVENTS];

void prcmu_debug_register_interrupt(u32 mailbox)
{
	if (mailbox < NUM_MAILBOXES)
		num_mailbox_interrupts[mailbox]++;
}

void prcmu_debug_register_mbox0_event(u32 ev, u32 mask)
{
	int i;

	for (i = 0 ; i < NUM_MAILBOX0_EVENTS; i++)
		if (ev & (1 << i)) {
			if (mask & (1 << i))
			    num_mailbox0_events[i]++;
			else
			    num_mailbox0_events_garbage[i]++;
		}
}

static int interrupt_read(struct seq_file *s, void *p)
{
	int i;
	char **mbox0names;

	static char *mbox0names_u8500[] = {
		"RTC",
		"RTT0",
		"RTT1",
		"HSI0",
		"HSI1",
		"CA_WAKE",
		"USB",
		"ABB",
		"ABB_FIFO",
		"SYSCLK_OK",
		"CA_SLEE",
		"AC_WAKE_ACK",
		"SIDE_TONE_OK",
		"ANC_OK",
		"SW_ERROR",
		"AC_SLEEP_ACK",
		NULL,
		"ARM",
		"HOTMON_LOW",
		"HOTMON_HIGH",
		"MODEM_SW_RESET_REQ",
		NULL,
		NULL,
		"GPIO0",
		"GPIO1",
		"GPIO2",
		"GPIO3",
		"GPIO4",
		"GPIO5",
		"GPIO6",
		"GPIO7",
		"GPIO8"};
	static char *mbox0names_u5500[] = {
		"RTC",
		"RTT0",
		"RTT1",
		"CD_IRQ",
		"SRP_TIM",
		"APE_REQ",
		"USB",
		"ABB",
		"LOW_POWER_AUDIO",
		"TEMP_SENSOR_LOW",
		"ARM",
		"AC_WAKE_ACK",
		NULL,
		"TEMP_SENSOR_HIGH",
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"MODEM_SW_RESET_REQ",
		NULL,
		NULL,
		"GPIO0",
		"GPIO1",
		"GPIO2",
		"GPIO3",
		"GPIO4",
		"GPIO5",
		"GPIO6",
		"GPIO7",
		"AC_REL_ACK"};

	if (cpu_is_u8500()) {
		mbox0names = mbox0names_u8500;
	} else if (cpu_is_u5500()) {
		mbox0names = mbox0names_u5500;
	} else {
		seq_printf(s, "Unknown ASIC!\n");
		return 0;
	}

	seq_printf(s, "mailbox0: %d\n", num_mailbox_interrupts[0]);

	for (i = 0; i < NUM_MAILBOX0_EVENTS; i++)
		if (mbox0names[i]) {
			seq_printf(s, " %20s %d ", mbox0names[i],
				   num_mailbox0_events[i]
				   );
			if (num_mailbox0_events_garbage[i])
				seq_printf(s, "unwanted: %d",
					   num_mailbox0_events_garbage[i]);
			seq_printf(s, "\n");
		} else if (num_mailbox0_events[i]) {
			seq_printf(s, "         unknown (%d) %d\n",
				   i, num_mailbox0_events[i]);
		}

	for (i = 1 ; i < NUM_MAILBOXES; i++)
		seq_printf(s, "mailbox%d: %d\n", i, num_mailbox_interrupts[i]);
	return 0;
}

/* FIXME: generic prcmu driver interface for fw version is missing */
#include <linux/mfd/db8500-prcmu.h>
static int version_read(struct seq_file *s, void *p)
{
	/* FIXME: generic prcmu driver interface for fw version is missing */
	struct prcmu_fw_version *fw_version = prcmu_get_fw_version();

	seq_printf(s, "PRCMU firmware: %s, version %d.%d.%d\n",
		   fw_version->project_name,
		   fw_version->api_version,
		   fw_version->func_version,
		   fw_version->errata);
	return 0;
}

static int opp_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, opp_read, inode->i_private);
}

static int ape_stats_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, ape_stats_print, inode->i_private);
}

static int ddr_stats_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, ddr_stats_print, inode->i_private);
}

static int vsafe_stats_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, vsafe_stats_print, inode->i_private);
}

static int arm_stats_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, arm_stats_print, inode->i_private);
}

static int cpufreq_delay_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, cpufreq_delay_read, inode->i_private);
}

static int ape_voltage_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, ape_voltage_read, inode->i_private);
}

static int avs_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, avs_read, inode->i_private);
}

static int prcmu_data_mem_open_file(struct inode *inode, struct file *file)
{
	int err;
	struct seq_file *s;

	err = single_open(file, prcmu_debugfs_data_mem_read, inode->i_private);
	if (!err) {
		/* Default buf size in seq_read is not enough */
		s = (struct seq_file *)file->private_data;
		s->size = (PAGE_SIZE * 4);
		s->buf = kmalloc(s->size, GFP_KERNEL);
		if (!s->buf) {
			single_release(inode, file);
			err = -ENOMEM;
		}
	}
	return err;
}

static int prcmu_regs_open_file(struct inode *inode, struct file *file)
{
	int err;
	struct seq_file *s;

	err = single_open(file, prcmu_debugfs_regs_read, inode->i_private);
	if (!err) {
		/* Default buf size in seq_read is not enough */
		s = (struct seq_file *)file->private_data;
		s->size = (PAGE_SIZE * 2);
		s->buf = kmalloc(s->size, GFP_KERNEL);
		if (!s->buf) {
			single_release(inode, file);
			err = -ENOMEM;
		}
	}
	return err;
}

static int interrupt_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, interrupt_read, inode->i_private);
}

static int version_open_file(struct inode *inode, struct file *file)
{
	return single_open(file, version_read, inode->i_private);
}

static const struct file_operations opp_fops = {
	.open = opp_open_file,
	.write = opp_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ape_stats_fops = {
	.open = ape_stats_open_file,
	.write = ape_stats_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ddr_stats_fops = {
	.open = ddr_stats_open_file,
	.write = ddr_stats_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations vsafe_stats_fops = {
	.open = vsafe_stats_open_file,
	.write = vsafe_stats_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations arm_stats_fops = {
	.open = arm_stats_open_file,
	.write = arm_stats_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations cpufreq_delay_fops = {
	.open = cpufreq_delay_open_file,
	.write = cpufreq_delay_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ape_voltage_fops = {
	.open = ape_voltage_open_file,
	.write = ape_voltage_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations avs_fops = {
	.open = avs_open_file,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations prcmu_data_mem_fops = {
	.open = prcmu_data_mem_open_file,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations prcmu_regs_fops = {
	.open = prcmu_regs_open_file,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations interrupts_fops = {
	.open = interrupt_open_file,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations version_fops = {
	.open = version_open_file,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int setup_debugfs(void)
{
	struct dentry *dir;
	struct dentry *file;

	dir = debugfs_create_dir("prcmu", NULL);
	if (IS_ERR_OR_NULL(dir))
		goto fail;

	file = debugfs_create_file("ape_stats", (S_IRUGO | S_IWUSR | S_IWGRP),
				   dir, NULL, &ape_stats_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("ddr_stats", (S_IRUGO | S_IWUSR | S_IWGRP),
				   dir, NULL, &ddr_stats_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	if (cpu_is_u9540()) {
		file = debugfs_create_file("vsafe_stats", (S_IRUGO | S_IWUSR | S_IWGRP),
				dir, NULL, &vsafe_stats_fops);
		if (IS_ERR_OR_NULL(file))
			goto fail;
	}

	file = debugfs_create_file("arm_stats", (S_IRUGO | S_IWUSR | S_IWGRP),
				   dir, NULL, &arm_stats_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("ape_opp", (S_IRUGO),
				   dir, (void *)&ape_sh,
				   &opp_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("ddr_opp", (S_IRUGO),
				   dir, (void *)&ddr_sh,
				   &opp_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("arm_khz", (S_IRUGO),
				   dir, (void *)&arm_sh,
				   &opp_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	if (cpu_is_u9540()) {
		file = debugfs_create_file("vsafe_opp", (S_IRUGO),
				dir, (void *)&vsafe_sh,
				&opp_fops);
		if (IS_ERR_OR_NULL(file))
			goto fail;
	}

	file = debugfs_create_file("opp_cpufreq_delay", (S_IRUGO),
				   dir, NULL, &cpufreq_delay_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("ape_voltage", (S_IRUGO),
				   dir, NULL, &ape_voltage_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("avs",
				   (S_IRUGO),
				   dir, NULL, &avs_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("data_mem", (S_IRUGO),
				   dir, NULL,
				   &prcmu_data_mem_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("regs", (S_IRUGO),
				   dir, NULL,
				   &prcmu_regs_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("interrupts",
				   (S_IRUGO),
				   dir, NULL, &interrupts_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	file = debugfs_create_file("version",
				   (S_IRUGO),
				   dir, NULL, &version_fops);
	if (IS_ERR_OR_NULL(file))
		goto fail;

	return 0;
fail:
	if (!IS_ERR_OR_NULL(dir))
		debugfs_remove_recursive(dir);

	pr_err("prcmu debug: debugfs entry failed\n");
	return -ENOMEM;
}

static __init int prcmu_debug_init(void)
{
	spin_lock_init(&ape_sh.lock);
	spin_lock_init(&ddr_sh.lock);
	spin_lock_init(&vsafe_sh.lock);
	spin_lock_init(&arm_sh.lock);
	ape_sh.start = ktime_get();
	ddr_sh.start = ktime_get();
	vsafe_sh.start = ktime_get();
	arm_sh.start = ktime_get();
	return 0;
}
arch_initcall(prcmu_debug_init);

static __init int prcmu_debug_debugfs_init(void)
{
	struct cpufreq_frequency_table *table;
	int i, ret;

	table = cpufreq_frequency_get_table(0);

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++)
		arm_sh.state_names[i] = table[i].frequency;

	arm_sh.max_states = i;

	ret = setup_debugfs();
	return ret;
}
late_initcall(prcmu_debug_debugfs_init);
