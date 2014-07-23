/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <mach/hardware.h>
#include <linux/gpio.h>
#include <plat/gpio-nomadik.h>
#include <linux/io.h>
#include <plat/pincfg.h>

#define HWOBS_NB 18	/* nb HWOBS pads */
#define HWOBS_MASK_FOR_MODE_CFG 3

#define PRCM_HWOBS_H	IO_ADDRESS(U8500_PRCMU_BASE + 0x4B0)
#define PRCM_HWOBS_L	IO_ADDRESS(U8500_PRCMU_BASE + 0x4B4)
#define PRCM_GPIOCR	IO_ADDRESS(U8500_PRCMU_BASE + 0x138)
#define PRCM_GPIOB4	IO_ADDRESS(U8500_GPIOBANK4_BASE)
#define PRCM_GPIOB5	IO_ADDRESS(U8500_GPIOBANK5_BASE)

#define PRCM_ENABLE_HW_OBS 0x00000010
#define PRCM_DDRCFG_HW_OBS 0x00000100
#define PRCM_GPIOCR_HW_OBS 0x03000000
#define PRCM_BITS_IN_BANK4 0xFF800000
#define PRCM_BITS_IN_BANK5 0x000001FF

/* hw_obs_cfg - configrations for a single ux500 HW_OBSERVERs
 *
 * @name:	label used as debugfs entry
 * @offset:	offset
 * @reg:	register
 * @mode:	human readable current config
 */
struct hw_obs_cfg {
	const char *name;
	unsigned int offset;
	unsigned int reg;
	char mode[32];
	int gpio;
	int gpio_req;
};

/* ux500_hw_obs - configrations all all ux500 HW_OBSERVERs
 *
 * @cfg:	target HW observers config
 * @enable:	non-zero if a HW observer is setup.
 * @save_gpiocr:	store HW cfg before enabling HW Obs
 * @save_afsla_bank4:	store HW cfg before enabling HW Obs
 * @save_afsla_bank5:	store HW cfg before enabling HW Obs
 * @save_afslb_bank4:	store HW cfg before enabling HW Obs
 * @save_afslb_bank5:	store HW cfg before enabling HW Obs
 * @dir:	debugfs directory dentry reference
 */
struct ux500_hw_obs {
	struct hw_obs_cfg cfg[HWOBS_NB];
	int enable;
	struct dentry *dir;
	struct mutex mutex;
};

enum hwobs_cfg {
	HWOBS_CFG_C2C = 0,
	HWOBS_CFG_MODEM,
	HWOBS_CFG_WAKE_UP,
	HWOBS_CFG_DDR,
	HWOBS_CFG_DDR0,
	HWOBS_CFG_DDR1,
	HWOBS_CFG_CLOCKING,
	HWOBS_CFG_GPIOH,
	HWOBS_CFG_GPIOL,
	HWOBS_CFG_MAX
};

static char **modes = NULL;

static const char *available_mode_ux540[HWOBS_CFG_MAX] = {
	[HWOBS_CFG_C2C] = "c2c",
	[HWOBS_CFG_WAKE_UP] = "wake-up",
	[HWOBS_CFG_DDR0] = "ddr0",
	[HWOBS_CFG_DDR1] = "ddr1",
	[HWOBS_CFG_CLOCKING] = "clocking",
	[HWOBS_CFG_GPIOH] = "gpioH",
	[HWOBS_CFG_GPIOL] = "gpioL",
};

static const char *available_mode_ux500[HWOBS_CFG_MAX] = {
	[HWOBS_CFG_MODEM] = "modem",
	[HWOBS_CFG_WAKE_UP] = "wake-up",
	[HWOBS_CFG_DDR] = "ddr",
	[HWOBS_CFG_CLOCKING] = "clocking",
	[HWOBS_CFG_GPIOH] = "gpioH",
	[HWOBS_CFG_GPIOL] = "gpioL",
};

#define HWOBS_INST(n, o, r, g) { .name = n, .offset = o, .reg = r, .gpio = g }

static struct ux500_hw_obs hwobs = {
	.cfg = {
		HWOBS_INST("hw_obs_0_mode",   0, PRCM_HWOBS_L, 168),
		HWOBS_INST("hw_obs_1_mode",   2, PRCM_HWOBS_L, 167),
		HWOBS_INST("hw_obs_2_mode",   4, PRCM_HWOBS_L, 166),
		HWOBS_INST("hw_obs_3_mode",   6, PRCM_HWOBS_L, 165),
		HWOBS_INST("hw_obs_4_mode",   8, PRCM_HWOBS_L, 164),
		HWOBS_INST("hw_obs_5_mode",  10, PRCM_HWOBS_L, 163),
		HWOBS_INST("hw_obs_6_mode",  12, PRCM_HWOBS_L, 162),
		HWOBS_INST("hw_obs_7_mode",  14, PRCM_HWOBS_L, 161),
		HWOBS_INST("hw_obs_8_mode",  16, PRCM_HWOBS_L, 160),
		HWOBS_INST("hw_obs_9_mode",  18, PRCM_HWOBS_L, 159),
		HWOBS_INST("hw_obs_10_mode", 20, PRCM_HWOBS_L, 158),
		HWOBS_INST("hw_obs_11_mode", 22, PRCM_HWOBS_L, 157),
		HWOBS_INST("hw_obs_12_mode", 24, PRCM_HWOBS_L, 156),
		HWOBS_INST("hw_obs_13_mode", 26, PRCM_HWOBS_L, 155),
		HWOBS_INST("hw_obs_14_mode", 28, PRCM_HWOBS_L, 154),
		HWOBS_INST("hw_obs_15_mode", 30, PRCM_HWOBS_L, 153),
		HWOBS_INST("hw_obs_16_mode",  0, PRCM_HWOBS_H, 152),
		HWOBS_INST("hw_obs_17_mode",  2, PRCM_HWOBS_H, 151),
	},
};

static inline enum hwobs_cfg str2mode(char *s)
{
	int i;
	for (i = 0; i < HWOBS_CFG_MAX; i++) {
		if (modes[i] && (!strncmp(s, modes[i], strlen(modes[i]))))
			return i;
	}
	return HWOBS_CFG_MAX;
}

/* configure_mode - config target HW_OBS pads
 *
 * HW_OBS pad can be configure either in a HW_OBS mode or in
 * GPIO mode, set High or Low. GPIO High/Low modes help validating
 * the board HW_OBS test points.
 */
#define MUX_HWOBS(i) (PIN_CFG(i, ALT_C) | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP)
#define MUX_GPIO(i) (PIN_CFG(i, GPIO) | PIN_DIR_OUTPUT | \
		PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP)
static void configure_mode(struct hw_obs_cfg *hw_obs, enum hwobs_cfg cfg)
{
	u32 temp;

	mutex_lock(&hwobs.mutex);

	if (!(hw_obs->gpio_req)) {
		if (gpio_request(hw_obs->gpio, "hwobs") < 0) {
			/* GPIO already reserved */
			hw_obs->gpio_req = -1;
			strncpy(hw_obs->mode, "GPIO reserved",
					sizeof(hw_obs->mode));
			return;
		} else
			hw_obs->gpio_req = 1;
	}

	/* mux pad */
	temp = readl(hw_obs->reg);
	temp &= ~(HWOBS_MASK_FOR_MODE_CFG << hw_obs->offset);

	switch (cfg) {
	case HWOBS_CFG_C2C:
	case HWOBS_CFG_MODEM:
		nmk_config_pin(MUX_HWOBS(hw_obs->gpio), false);
		writel(temp, hw_obs->reg);
		break;
	case HWOBS_CFG_WAKE_UP:
		nmk_config_pin(MUX_HWOBS(hw_obs->gpio), false);
		writel(temp | (1 << hw_obs->offset), hw_obs->reg);
		break;
	case HWOBS_CFG_DDR:
		nmk_config_pin(MUX_HWOBS(hw_obs->gpio), false);
		writel(temp | (2 << hw_obs->offset), hw_obs->reg);
		break;
	case HWOBS_CFG_DDR0:
		nmk_config_pin(MUX_HWOBS(hw_obs->gpio), false);
		writel(temp | (2 << hw_obs->offset), hw_obs->reg);
		temp = readl(PRCM_HWOBS_H);
		writel(temp & ~PRCM_DDRCFG_HW_OBS, PRCM_HWOBS_H);
		break;
	case HWOBS_CFG_DDR1:
		nmk_config_pin(MUX_HWOBS(hw_obs->gpio), false);
		writel(temp | (2 << hw_obs->offset), hw_obs->reg);
		temp = readl(PRCM_HWOBS_H);
		writel(temp | PRCM_DDRCFG_HW_OBS, PRCM_HWOBS_H);
		break;
	case HWOBS_CFG_CLOCKING:
		nmk_config_pin(MUX_HWOBS(hw_obs->gpio), false);
		writel(temp | (3 << hw_obs->offset), hw_obs->reg);
		break;
	case HWOBS_CFG_GPIOH:
		nmk_config_pin(MUX_GPIO(hw_obs->gpio), false);
		gpio_set_value_cansleep(hw_obs->gpio, 1);
		break;
	case HWOBS_CFG_GPIOL:
		nmk_config_pin(MUX_GPIO(hw_obs->gpio), false);
		gpio_set_value_cansleep(hw_obs->gpio, 0);
		break;
	default:
		return;
	}
	strncpy(hw_obs->mode, modes[cfg], sizeof(hw_obs->mode));

	mutex_unlock(&hwobs.mutex);
}

/*
 * hwobs_enable - request gpios and enable HW_OBS in HWOBS
 * GPIO will be configured on demand
 */
static int hwobs_enable(void)
{
	mutex_lock(&hwobs.mutex);
	writel(readl(PRCM_HWOBS_H) | PRCM_ENABLE_HW_OBS, PRCM_HWOBS_H);
	writel(readl(PRCM_GPIOCR) | PRCM_GPIOCR_HW_OBS, PRCM_GPIOCR);
	mutex_unlock(&hwobs.mutex);

	return 0;
}
/* hwobs_disable - reconfig/free gpios and disable hw obs */
static int hwobs_disable(void)
{
	struct hw_obs_cfg *hw_obs;
	int i;

	mutex_lock(&hwobs.mutex);
	for (i = 0, hw_obs = hwobs.cfg; i < HWOBS_NB; i++, hw_obs++) {
		if (hw_obs->gpio_req > 0) {
			/* reconfig pin as GPIO/input/SleepSupport */
			nmk_config_pin(PIN_CFG(hw_obs->gpio, GPIO), false);
			gpio_free(hw_obs->gpio);
			hw_obs->gpio_req = 0;
		}
	}
	writel(readl(PRCM_HWOBS_H) & ~PRCM_ENABLE_HW_OBS, PRCM_HWOBS_H);
	writel(readl(PRCM_GPIOCR) & ~PRCM_GPIOCR_HW_OBS, PRCM_GPIOCR);
	mutex_unlock(&hwobs.mutex);
	return 0;
}

static const char readme[] =
	"How to use hw observer debufs:\n"
	"------------------------------\n"
	"\n"
	"./show_all_status: show current status of all hw observers.\n"
	"\n"
	"./enable_hwobs:    set/get hw observer feature 'enable' state.\n"
	"      0        disable HW_OBS\n"
	"      1        enable HW_OBS\n"
	"      <MODE>   set all HW_OBS in target MODE (see available_mode)\n"
	"\n"
	"./hw_obs_x_mode:   set/get each hw observer 'mode' configuration.\n"
	"      <MODE>   refer to 'available_mode' for supported values.\n"
	"\n"
	"./available_mode:  list of supported <MODE> configuration values.\n";

static int hwobs_readme(struct seq_file *s, void *p)
{
	return seq_printf(s, "%s", readme);
}

static int hwobs_showall(struct seq_file *s, void *p)
{
	int i;

	if (hwobs.enable == 0)
		return seq_printf(s, "Not available, feature disabled.\n");

	for (i = 0; i < HWOBS_NB; i++)
		seq_printf(s, "%s: %s\n", hwobs.cfg[i].name, hwobs.cfg[i].mode);
	return 0;
}

static int hwobs_avmodes(struct seq_file *s, void *p)
{
	int i;

	for (i = 0; i < HWOBS_CFG_MAX; i++)
		if(modes[i] != NULL)
			seq_printf(s, "%s\n", modes[i]);
	return 0;
}

static int hwobs_mode_read(struct seq_file *s, void *p)
{
	struct hw_obs_cfg *cfg = ((struct hw_obs_cfg *)(s->private));

	if (hwobs.enable)
		return seq_printf(s,"Mode: %s \n", cfg->mode);

	return seq_printf(s, "HW OBS disable, no mode available\n");
}

static ssize_t hwobs_mode_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct hw_obs_cfg *cfg;
	char buf[32];
	int bsize, id;

	/* Get userspace string and assure termination */
	bsize = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, bsize))
		return -EFAULT;
	buf[bsize] = 0;

	cfg = ((struct seq_file *)(file->private_data))->private;
	id = str2mode(buf);
	if (id == HWOBS_CFG_MAX)
		return -EINVAL;
	configure_mode(cfg, id);
	return count;
}

static int hwobs_enable_read(struct seq_file *s, void *p)
{
	return seq_printf(s, "%d (hw observers %s)\n",
			hwobs.enable, (hwobs.enable) ? "enabled" : "disabled");
}

static ssize_t hwobs_enable_write(struct file *file,
			 const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	long int user_val;
	char buf[32];
	int bsize, err;
	u32 temp = 0, i;

	/* Get userspace string and assure termination */
	bsize = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, bsize))
		return -EFAULT;
	buf[bsize] = 0;

	/* check if oneshot config of all HW_OBS */
	temp = str2mode(buf);
	if (temp != HWOBS_CFG_MAX) {
		if (hwobs_enable())
			goto disable;
		hwobs.enable = 1;
		for (i = 0; i < HWOBS_NB; i++)
			configure_mode(&hwobs.cfg[i], temp);
		return count;
	}

	/* arg = 0/1 => disable/enable HW_OBS */
	err = strict_strtol(buf, 0, &user_val);
	if (err)
		return err;

	if ((user_val == 1) && (hwobs.enable == 0)) {
		if (hwobs_enable())
			goto disable;
		hwobs.enable = 1;
	} else if ((user_val == 0) && hwobs.enable) {
		hwobs_disable();
		hwobs.enable = 0;
	} else
		return -EINVAL;

	return count;
disable:
	hwobs_disable();
	hwobs.enable = 0;
	return -EIO;
}

static int hwobs_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwobs_mode_read, inode->i_private);
}
static int hwobs_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwobs_enable_read, inode->i_private);
}
static int hwobs_showall_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwobs_showall, inode->i_private);
}
static int hwobs_avmodes_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwobs_avmodes, inode->i_private);
}
static int hwobs_readme_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwobs_readme, inode->i_private);
}

#define HWOBS_SEQF_FOPS(fops, op, wr) \
	static const struct file_operations fops = { \
		.open = op, \
		.write = wr, \
		.read = seq_read, \
		.llseek = seq_lseek, \
		.release = single_release, \
		.owner = THIS_MODULE, \
	}

HWOBS_SEQF_FOPS(hwobs_mode_fops, hwobs_mode_open, hwobs_mode_write);
HWOBS_SEQF_FOPS(hwobs_enable_fops, hwobs_enable_open, hwobs_enable_write);
HWOBS_SEQF_FOPS(hwobs_showall_fops, hwobs_showall_open, NULL);
HWOBS_SEQF_FOPS(hwobs_avmodes_fops, hwobs_avmodes_open, NULL);
HWOBS_SEQF_FOPS(hwobs_readme_fops, hwobs_readme_open, NULL);

static int __init dbx500_hw_obs_debug_init(void)
{
	int i;

	mutex_init(&hwobs.mutex);

	if(cpu_is_u9540())
		modes = (char**)available_mode_ux540;
	else
		modes = (char**)available_mode_ux500;

	hwobs.dir = debugfs_create_dir("hw_observer", NULL);
	if (hwobs.dir == NULL)
		goto fail;

	/* create file entry for each HW observer line */
	for (i = 0; i < HWOBS_NB; i++) {
		if (!debugfs_create_file(hwobs.cfg[i].name, S_IRUGO | S_IWUSR | S_IWGRP,
				hwobs.dir, &hwobs.cfg[i], &hwobs_mode_fops))
			goto fail;
	}
	if (!debugfs_create_file("show_all_status", S_IRUGO,
				hwobs.dir, NULL, &hwobs_showall_fops))
		goto fail;
	if (!debugfs_create_file("enable_hwobs", S_IRUGO | S_IWUSR | S_IWGRP,
				hwobs.dir, NULL, &hwobs_enable_fops))
		goto fail;
	if (!debugfs_create_file("available_mode", S_IRUGO,
				hwobs.dir, NULL, &hwobs_avmodes_fops))
		goto fail;
	if (!debugfs_create_file("README", S_IRUGO,
				hwobs.dir, NULL, &hwobs_readme_fops))
		goto fail;
	hwobs.enable = 0;

	printk(KERN_INFO "hw observer intialized\n");
	return 0;

fail:

	if (hwobs.dir) {
		debugfs_remove_recursive(hwobs.dir);
		hwobs.dir = NULL;
	}

	mutex_destroy(&hwobs.mutex);
	
	printk(KERN_ERR "hw observer debug: debugfs entry failed\n");
	return -ENOMEM;
}
module_init(dbx500_hw_obs_debug_init);
