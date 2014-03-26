/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson AB8500 DENC base driver
 *
 * Author: Marcel Tunnissen <marcel.tuennissen@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-denc-regs.h>
#include <linux/mfd/abx500/ab8500-denc.h>

#define AB8500_NAME      "ab8500"
#define AB8500_DENC_NAME "ab8500_denc"

struct device_usage {
	struct list_head list;
	struct platform_device *pdev;
	bool taken;
};
static LIST_HEAD(device_list);

/* To get rid of the extra bank parameter: */
#define AB8500_REG_BANK_NR(__reg)	((0xff00 & (__reg)) >> 8)
static inline u8 ab8500_rreg(struct device *dev, u32 reg)
{
	u8 val;
	if (abx500_get_register_interruptible(dev, AB8500_REG_BANK_NR(reg),
								reg, &val) < 0)
		return 0;
	else
		return val;
}

static inline int ab8500_wreg(struct device *dev, u32 reg, u8 val)
{
	return abx500_set_register_interruptible(dev, AB8500_REG_BANK_NR(reg),
								reg, val);
}

/* Only use in the macro below: */
static inline int _ab8500_wreg_fld(struct device *dev, u32 reg, u8 val,
							u8 mask, u8 shift)
{
	int ret;
	u8 org_val;

	ret = abx500_get_register_interruptible(dev, AB8500_REG_BANK_NR(reg),
								reg, &org_val);
	if (ret < 0)
		return ret;
	else
		ab8500_wreg(dev, reg,
				(org_val & ~mask) | ((val << shift) & mask));
	return 0;
}

#define ab8500_wr_fld(__d, __reg, __fld, __val)				\
	_ab8500_wreg_fld(__d, __reg, __val, __reg##_##__fld##_MASK,	\
						__reg##_##__fld##_SHIFT)

#define ab8500_set_fld(__cur_val, __reg, __fld, __val)			\
	(((__cur_val) & ~__reg##_##__fld##_MASK) |			\
	(((__val) << __reg##_##__fld##_SHIFT) & __reg##_##__fld##_MASK))

#define AB8500_DENC_TRACE(__pd)	dev_dbg(&(__pd)->dev, "%s\n", __func__)

#ifdef CONFIG_DEBUG_FS
static struct dentry *debugfs_ab8500_denc_dir;
static struct dentry *debugfs_ab8500_dump_regs_file;
static void ab8500_denc_conf_ddr(struct platform_device *pdev);
static int debugfs_ab8500_open_file(struct inode *inode, struct file *file);
static ssize_t debugfs_ab8500_dump_regs(struct file *file, char __user *buf,
						size_t count, loff_t *f_pos);

static const struct file_operations debugfs_ab8500_dump_regs_fops = {
	.owner = THIS_MODULE,
	.open  = debugfs_ab8500_open_file,
	.read  = debugfs_ab8500_dump_regs,
};
#endif /* CONFIG_DEBUG_FS */

static int __devinit ab8500_denc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct ab8500_platform_data *ab8500_pdata =
		dev_get_platdata(pdev->dev.parent);
	struct ab8500_denc_platform_data *pdata;
	struct device_usage *device_data;

	AB8500_DENC_TRACE(pdev);

	if (ab8500_pdata == NULL) {
		dev_err(&pdev->dev, "AB8500 platform data missing\n");
		return -EINVAL;
	}

	pdata = ab8500_pdata->denc;
	if (pdata == NULL) {
		dev_err(&pdev->dev, "Denc platform data missing\n");
		return -EINVAL;
	}

	device_data = kzalloc(sizeof(struct device_usage), GFP_KERNEL);
	if (!device_data) {
		dev_err(&pdev->dev, "Failed to allocate device data\n");
		return -ENOMEM;
	}
	device_data->pdev = pdev;
	list_add_tail(&device_data->list, &device_list);

#ifdef CONFIG_DEBUG_FS
	debugfs_ab8500_denc_dir = debugfs_create_dir(pdev->name, NULL);
	debugfs_ab8500_dump_regs_file = debugfs_create_file(
			"dumpregs", S_IRUGO,
			debugfs_ab8500_denc_dir, &pdev->dev,
			&debugfs_ab8500_dump_regs_fops
		);
#endif /* CONFIG_DEBUG_FS */
	return ret;
}

static int __devexit ab8500_denc_remove(struct platform_device *pdev)
{
	struct list_head *element;
	struct device_usage *device_data;

	AB8500_DENC_TRACE(pdev);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove(debugfs_ab8500_dump_regs_file);
	debugfs_remove(debugfs_ab8500_denc_dir);
#endif /* CONFIG_DEBUG_FS */

	list_for_each(element, &device_list) {
		device_data = list_entry(element, struct device_usage, list);
		if (device_data->pdev == pdev) {
			list_del(element);
			kzfree(device_data);
		}
	}

	return 0;
}

static struct platform_driver ab8500_denc_driver = {
	.probe	= ab8500_denc_probe,
	.remove = ab8500_denc_remove,
	.driver = {
		.name	= "ab8500-denc",
	},
};

static void setup_27mhz(struct platform_device *pdev, bool enable)
{
	u8 data = ab8500_rreg(&pdev->dev, AB8500_SYS_ULP_CLK_CONF);

	AB8500_DENC_TRACE(pdev);
	/* TODO: check if this field needs to be set */
	data = ab8500_set_fld(data, AB8500_SYS_ULP_CLK_CONF, CLK_27MHZ_PD_ENA,
									true);
	data = ab8500_set_fld(data, AB8500_SYS_ULP_CLK_CONF, CLK_27MHZ_BUF_ENA,
									enable);
	data = ab8500_set_fld(data, AB8500_SYS_ULP_CLK_CONF, TVOUT_CLK_INV,
									false);
	data = ab8500_set_fld(data, AB8500_SYS_ULP_CLK_CONF, TVOUT_CLK_DE_IN,
									false);
	data = ab8500_set_fld(data, AB8500_SYS_ULP_CLK_CONF, CLK_27MHZ_STRE,
									1);
	ab8500_wreg(&pdev->dev, AB8500_SYS_ULP_CLK_CONF, data);

	data = ab8500_rreg(&pdev->dev, AB8500_SYS_CLK_CTRL);
	data = ab8500_set_fld(data, AB8500_SYS_CLK_CTRL, TVOUT_CLK_VALID,
									enable);
	data = ab8500_set_fld(data, AB8500_SYS_CLK_CTRL, TVOUT_PLL_ENA,
									enable);
	ab8500_wreg(&pdev->dev, AB8500_SYS_CLK_CTRL, data);
}

static u32 map_tv_std(enum ab8500_denc_TV_std std)
{
	switch (std) {
	case TV_STD_PAL_BDGHI:
		return AB8500_DENC_CONF0_STD_PAL_BDGHI;
	case TV_STD_PAL_N:
		return AB8500_DENC_CONF0_STD_PAL_N;
	case TV_STD_PAL_M:
		return AB8500_DENC_CONF0_STD_PAL_M;
	case TV_STD_NTSC_M:
		return AB8500_DENC_CONF0_STD_NTSC_M;
	default:
		return 0;
	}
}

static u32 map_cr_filter(enum ab8500_denc_cr_filter_bandwidth bw)
{
	switch (bw) {
	case TV_CR_NTSC_LOW_DEF_FILTER:
		return AB8500_DENC_CONF1_FLT_1_1MHZ;
	case TV_CR_PAL_LOW_DEF_FILTER:
		return AB8500_DENC_CONF1_FLT_1_3MHZ;
	case TV_CR_NTSC_HIGH_DEF_FILTER:
		return AB8500_DENC_CONF1_FLT_1_6MHZ;
	case TV_CR_PAL_HIGH_DEF_FILTER:
		return AB8500_DENC_CONF1_FLT_1_9MHZ;
	default:
		return 0;
	}
}

static u32 map_phase_rst_mode(enum ab8500_denc_phase_reset_mode mode)
{
	switch (mode) {
	case TV_PHASE_RST_MOD_DISABLE:
		return AB8500_DENC_CONF8_PH_RST_MODE_DISABLED;
	case TV_PHASE_RST_MOD_FROM_PHASE_BUF:
		return AB8500_DENC_CONF8_PH_RST_MODE_UPDATE_FROM_PHASE_BUF;
	case TV_PHASE_RST_MOD_FROM_INC_DFS:
		return AB8500_DENC_CONF8_PH_RST_MODE_UPDATE_FROM_INC_DFS;
	case TV_PHASE_RST_MOD_RST:
		return AB8500_DENC_CONF8_PH_RST_MODE_RESET;
	default:
		return 0;
	}
}

static u32 map_plug_time(enum ab8500_denc_plug_time time)
{
	switch (time) {
	case TV_PLUG_TIME_0_5S:
		return AB8500_TVOUT_CTRL_PLUG_TV_TIME_0_5S;
	case TV_PLUG_TIME_1S:
		return AB8500_TVOUT_CTRL_PLUG_TV_TIME_1S;
	case TV_PLUG_TIME_1_5S:
		return AB8500_TVOUT_CTRL_PLUG_TV_TIME_1_5S;
	case TV_PLUG_TIME_2S:
		return AB8500_TVOUT_CTRL_PLUG_TV_TIME_2S;
	case TV_PLUG_TIME_2_5S:
		return AB8500_TVOUT_CTRL_PLUG_TV_TIME_2_5S;
	case TV_PLUG_TIME_3S:
		return AB8500_TVOUT_CTRL_PLUG_TV_TIME_3S;
	default:
		return 0;
	}
}

struct platform_device *ab8500_denc_get_device(void)
{
	struct list_head *element;
	struct device_usage *device_data;

	pr_debug("%s\n", __func__);
	list_for_each(element, &device_list) {
		device_data = list_entry(element, struct device_usage, list);
		if (!device_data->taken) {
			device_data->taken = true;
			return device_data->pdev;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(ab8500_denc_get_device);

void ab8500_denc_put_device(struct platform_device *pdev)
{
	struct list_head *element;
	struct device_usage *device_data;

	AB8500_DENC_TRACE(pdev);
	list_for_each(element, &device_list) {
		device_data = list_entry(element, struct device_usage, list);
		if (device_data->pdev == pdev)
			device_data->taken = false;
	}
}
EXPORT_SYMBOL(ab8500_denc_put_device);

void ab8500_denc_reset(struct platform_device *pdev, bool hard)
{
	AB8500_DENC_TRACE(pdev);
	if (hard) {
		u8 data = ab8500_rreg(&pdev->dev, AB8500_CTRL3);
		/* reset start */
		ab8500_wreg(&pdev->dev, AB8500_CTRL3,
			ab8500_set_fld(data, AB8500_CTRL3, RESET_DENC_N, 0)
		);
		/* reset done */
		ab8500_wreg(&pdev->dev, AB8500_CTRL3,
			ab8500_set_fld(data, AB8500_CTRL3, RESET_DENC_N, 1)
		);
	} else {
		ab8500_wr_fld(&pdev->dev, AB8500_DENC_CONF6, SOFT_RESET, 1);
		mdelay(10);
	}
}
EXPORT_SYMBOL(ab8500_denc_reset);

void ab8500_denc_power_up(struct platform_device *pdev)
{
	setup_27mhz(pdev, true);
}
EXPORT_SYMBOL(ab8500_denc_power_up);

void ab8500_denc_power_down(struct platform_device *pdev)
{
	setup_27mhz(pdev, false);
}
EXPORT_SYMBOL(ab8500_denc_power_down);

void ab8500_denc_conf(struct platform_device *pdev,
						struct ab8500_denc_conf *conf)
{
	u8 data;

	AB8500_DENC_TRACE(pdev);

	ab8500_wreg(&pdev->dev, AB8500_DENC_CONF0,
		AB8500_VAL2REG(AB8500_DENC_CONF0, STD, map_tv_std(conf->TV_std))
		|
		AB8500_VAL2REG(AB8500_DENC_CONF0, SYNC,
			conf->test_pattern ? AB8500_DENC_CONF0_SYNC_AUTO_TEST :
					AB8500_DENC_CONF0_SYNC_F_BASED_SLAVE
		)
	);
	ab8500_wreg(&pdev->dev, AB8500_DENC_CONF1,
		AB8500_VAL2REG(AB8500_DENC_CONF1, BLK_LI,
						!conf->partial_blanking)
		|
		AB8500_VAL2REG(AB8500_DENC_CONF1, FLT,
						map_cr_filter(conf->cr_filter))
		|
		AB8500_VAL2REG(AB8500_DENC_CONF1, CO_KI, conf->suppress_col)
		|
		AB8500_VAL2REG(AB8500_DENC_CONF1, SETUP_MAIN,
						conf->black_level_setup)
		/* TODO: handle cc field: set to 0 now */
	);

	data = ab8500_rreg(&pdev->dev, AB8500_DENC_CONF2);
	data = ab8500_set_fld(data, AB8500_DENC_CONF2, N_INTRL,
						conf->progressive);
	ab8500_wreg(&pdev->dev, AB8500_DENC_CONF2, data);

	ab8500_wreg(&pdev->dev, AB8500_DENC_CONF8,
		AB8500_VAL2REG(AB8500_DENC_CONF8, PH_RST_MODE,
				map_phase_rst_mode(conf->phase_reset_mode))
		|
		AB8500_VAL2REG(AB8500_DENC_CONF8, VAL_422_MUX,
						conf->act_output)
		|
		AB8500_VAL2REG(AB8500_DENC_CONF8, BLK_ALL,
						conf->blank_all)
	);
	data = ab8500_rreg(&pdev->dev, AB8500_TVOUT_CTRL);
	data = ab8500_set_fld(data, AB8500_TVOUT_CTRL, DAC_CTRL0,
							conf->dac_enable);
	data = ab8500_set_fld(data, AB8500_TVOUT_CTRL, DAC_CTRL1,
							conf->act_dc_output);
	ab8500_wreg(&pdev->dev, AB8500_TVOUT_CTRL, data);

	/* no support for DDR in early versions */
	if (AB8500_REG2VAL(AB8500_REV, FULL_MASK,
				ab8500_rreg(&pdev->dev, AB8500_REV)) > 0)
		ab8500_denc_conf_ddr(pdev);
}
EXPORT_SYMBOL(ab8500_denc_conf);

void ab8500_denc_conf_plug_detect(struct platform_device *pdev,
						bool enable, bool load_RC,
						enum ab8500_denc_plug_time time)
{
	u8 data;

	AB8500_DENC_TRACE(pdev);
	data = ab8500_rreg(&pdev->dev, AB8500_TVOUT_CTRL);
	data = ab8500_set_fld(data, AB8500_TVOUT_CTRL, TV_PLUG_ON,   enable);
	data = ab8500_set_fld(data, AB8500_TVOUT_CTRL, TV_LOAD_RC,   load_RC);
	data = ab8500_set_fld(data, AB8500_TVOUT_CTRL, PLUG_TV_TIME,
							map_plug_time(time));
	ab8500_wreg(&pdev->dev, AB8500_TVOUT_CTRL, data);
}
EXPORT_SYMBOL(ab8500_denc_conf_plug_detect);

void ab8500_denc_mask_int_plug_det(struct platform_device *pdev, bool plug,
								bool unplug)
{
	u8 data = ab8500_rreg(&pdev->dev, AB8500_IT_MASK1);

	AB8500_DENC_TRACE(pdev);
	data = ab8500_set_fld(data, AB8500_IT_MASK1, PLUG_TV_DET,   plug);
	data = ab8500_set_fld(data, AB8500_IT_MASK1, UNPLUG_TV_DET, unplug);
	ab8500_wreg(&pdev->dev, AB8500_IT_MASK1, data);
}
EXPORT_SYMBOL(ab8500_denc_mask_int_plug_det);

static void ab8500_denc_conf_ddr(struct platform_device *pdev)
{
	struct ab8500_platform_data *core_pdata;
	struct ab8500_denc_platform_data *denc_pdata;

	AB8500_DENC_TRACE(pdev);
	core_pdata = dev_get_platdata(pdev->dev.parent);
	denc_pdata = core_pdata->denc;
	ab8500_wreg(&pdev->dev, AB8500_TVOUT_CTRL2,
		AB8500_VAL2REG(AB8500_TVOUT_CTRL2,
					DENC_DDR, denc_pdata->ddr_enable) |
		AB8500_VAL2REG(AB8500_TVOUT_CTRL2, SWAP_DDR_DATA_IN,
					denc_pdata->ddr_little_endian));
}

#ifdef CONFIG_DEBUG_FS
static int debugfs_ab8500_open_file(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define DEBUG_BUF_SIZE 900

#define AB8500_GPIO_DIR5                0x1014
#define AB8500_GPIO_DIR5_35_SHIFT       2
#define AB8500_GPIO_DIR5_35_MASK        (1 << AB8500_GPIO_DIR5_35_SHIFT)
#define AB8500_GPIO_OUT5                0x1024
#define AB8500_GPIO_OUT5_35_SHIFT       2
#define AB8500_GPIO_OUT5_35_MASK        (1 << AB8500_GPIO_OUT5_35_SHIFT)
#define AB8500_GPIO_OUT5_35_VIDEO       0
#define AB8500_GPIO_OUT5_35_AUDIO       1
#define AB8500_GPIO_NPUD5                0x1034
#define AB8500_GPIO_NPUD5_35_SHIFT       2
#define AB8500_GPIO_NPUD5_35_MASK        (1 << AB8500_GPIO_NPUD5_35_SHIFT)
#define AB8500_GPIO_NPUD5_35_ACTIVE      0
#define AB8500_GPIO_NPUD5_35_INACTIVE    1

static ssize_t debugfs_ab8500_dump_regs(struct file *file, char __user *buf,
						size_t count, loff_t *f_pos)
{
	int ret = 0;
	size_t data_size = 0;
	char   buffer[DEBUG_BUF_SIZE];
	struct device *dev = file->private_data;

	data_size += sprintf(buffer + data_size,
		"AB8500 DENC registers:\n"
		"------Regulators etc ----------\n"
		"CTRL3         : 0x%04x = 0x%02x\n"
		"SYSULPCLK_CONF: 0x%04x = 0x%02x\n"
		"SYSCLK_CTRL   : 0x%04x = 0x%02x\n"
		"REGU_MISC1    : 0x%04x = 0x%02x\n"
		"VAUX12_REGU   : 0x%04x = 0x%02x\n"
		"VAUX1_SEL1    : 0x%04x = 0x%02x\n"
		"------TVout only --------------\n"
		"DENC_CONF0    : 0x%04x = 0x%02x\n"
		"DENC_CONF1    : 0x%04x = 0x%02x\n"
		"DENC_CONF2    : 0x%04x = 0x%02x\n"
		"DENC_CONF6    : 0x%04x = 0x%02x\n"
		"DENC_CONF8    : 0x%04x = 0x%02x\n"
		"TVOUT_CTRL    : 0x%04x = 0x%02x\n"
		"TVOUT_CTRL2   : 0x%04x = 0x%02x\n"
		"IT_MASK1      : 0x%04x = 0x%02x\n"
		"------AV connector-------------\n"
		"GPIO_DIR5     : 0x%04x = 0x%02x\n"
		"GPIO_OUT5     : 0x%04x = 0x%02x\n"
		"GPIO_NPUD5    : 0x%04x = 0x%02x\n"
		,
		AB8500_CTRL3,            ab8500_rreg(dev, AB8500_CTRL3),
		AB8500_SYS_ULP_CLK_CONF, ab8500_rreg(dev,
						AB8500_SYS_ULP_CLK_CONF),
		AB8500_SYS_CLK_CTRL,     ab8500_rreg(dev, AB8500_SYS_CLK_CTRL),
		AB8500_REGU_MISC1,       ab8500_rreg(dev, AB8500_REGU_MISC1),
		AB8500_VAUX12_REGU,      ab8500_rreg(dev, AB8500_VAUX12_REGU),
		AB8500_VAUX1_SEL,        ab8500_rreg(dev, AB8500_VAUX1_SEL),
		AB8500_DENC_CONF0,       ab8500_rreg(dev, AB8500_DENC_CONF0),
		AB8500_DENC_CONF1,       ab8500_rreg(dev, AB8500_DENC_CONF1),
		AB8500_DENC_CONF2,       ab8500_rreg(dev, AB8500_DENC_CONF2),
		AB8500_DENC_CONF6,       ab8500_rreg(dev, AB8500_DENC_CONF6),
		AB8500_DENC_CONF8,       ab8500_rreg(dev, AB8500_DENC_CONF8),
		AB8500_TVOUT_CTRL,       ab8500_rreg(dev, AB8500_TVOUT_CTRL),
		AB8500_TVOUT_CTRL2,      ab8500_rreg(dev, AB8500_TVOUT_CTRL2),
		AB8500_IT_MASK1,         ab8500_rreg(dev, AB8500_IT_MASK1),
		AB8500_GPIO_DIR5,        ab8500_rreg(dev, AB8500_GPIO_DIR5),
		AB8500_GPIO_OUT5,        ab8500_rreg(dev, AB8500_GPIO_OUT5),
		AB8500_GPIO_NPUD5,       ab8500_rreg(dev, AB8500_GPIO_NPUD5)
	);
	if (data_size >= DEBUG_BUF_SIZE) {
		printk(KERN_EMERG "AB8500 DENC: Buffer overrun\n");
		ret = -EINVAL;
		goto out;
	}

	/* check if read done */
	if (*f_pos > data_size)
		goto out;

	if (*f_pos + count > data_size)
		count = data_size - *f_pos;

	if (copy_to_user(buf, buffer + *f_pos, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;
out:
	return ret;
}
#endif /* CONFIG_DEBUG_FS */

/* Module init */
static int __init ab8500_denc_init(void)
{
	return platform_driver_register(&ab8500_denc_driver);
}
module_init(ab8500_denc_init);

static void __exit ab8500_denc_exit(void)
{
	platform_driver_unregister(&ab8500_denc_driver);
}
module_exit(ab8500_denc_exit);

MODULE_AUTHOR("Marcel Tunnissen <marcel.tuennissen@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson AB8500 DENC driver");
