/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * UX500 common part of Power domain regulators
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mfd/dbx500-prcmu.h>

#include "dbx500-prcmu.h"

/*
 * power state reference count
 */
static int power_state_active_cnt; /* will initialize to zero */
static DEFINE_SPINLOCK(power_state_active_lock);

extern int (*prcmu_set_epod) (u16 epod_id, u8 epod_state);

int power_state_active_get(void)
{
	unsigned long flags;
	int cnt;

	spin_lock_irqsave(&power_state_active_lock, flags);
	cnt = power_state_active_cnt;
	spin_unlock_irqrestore(&power_state_active_lock, flags);

	return cnt;
}

void power_state_active_enable(void)
{
	unsigned long flags;

	spin_lock_irqsave(&power_state_active_lock, flags);
	power_state_active_cnt++;
	spin_unlock_irqrestore(&power_state_active_lock, flags);
}

int power_state_active_disable(void)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&power_state_active_lock, flags);
	if (power_state_active_cnt <= 0) {
		pr_err("power state: unbalanced enable/disable calls\n");
		ret = -EINVAL;
		goto out;
	}

	power_state_active_cnt--;
out:
	spin_unlock_irqrestore(&power_state_active_lock, flags);
	return ret;
}

/*
 * Exported interface for CPUIdle only. This function is called when interrupts
 * are turned off. Hence, no locking.
 */
int power_state_active_is_enabled(void)
{
	return (power_state_active_cnt > 0);
}

struct ux500_regulator {
	char *name;
	void (*enable)(void);
	int (*disable)(void);
	int count;
};

static struct ux500_regulator ux500_atomic_regulators[] = {
	{
		.name    = "dma40.0",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "ssp0",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "ssp1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi0",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi2",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi3",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "cryp1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "hash1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
};

struct ux500_regulator *__must_check ux500_regulator_get(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ux500_atomic_regulators); i++) {
		if (!strcmp(dev_name(dev), ux500_atomic_regulators[i].name))
			return &ux500_atomic_regulators[i];
	}

	return  ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(ux500_regulator_get);

int ux500_regulator_atomic_enable(struct ux500_regulator *regulator)
{
	if (regulator) {
		regulator->count++;
		regulator->enable();
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ux500_regulator_atomic_enable);

int ux500_regulator_atomic_disable(struct ux500_regulator *regulator)
{
	if (regulator) {
		regulator->count--;
		return regulator->disable();
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ux500_regulator_atomic_disable);

void ux500_regulator_put(struct ux500_regulator *regulator)
{
	/* Here for symetric reasons and for possible future use */
}
EXPORT_SYMBOL_GPL(ux500_regulator_put);

#ifdef CONFIG_REGULATOR_DEBUG

static struct ux500_regulator_debug {
	struct dentry *dir;
	struct dbx500_regulator_info *regulator_array;
	int num_regulators;
	u8 *state_before_suspend;
	u8 *state_after_suspend;
} rdebug;

void ux500_regulator_suspend_debug(void)
{
	int i;
	for (i = 0; i < rdebug.num_regulators; i++)
		rdebug.state_before_suspend[i] =
			rdebug.regulator_array[i].is_enabled;
}

void ux500_regulator_resume_debug(void)
{
	int i;
	for (i = 0; i < rdebug.num_regulators; i++)
		rdebug.state_after_suspend[i] =
			rdebug.regulator_array[i].is_enabled;
}

static int ux500_regulator_power_state_cnt_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int err;

	/* print power state count */
	err = seq_printf(s, "ux500-regulator power state count: %i\n",
		power_state_active_get());
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	return 0;
}

static int ux500_regulator_power_state_cnt_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, ux500_regulator_power_state_cnt_print,
		inode->i_private);
}

static const struct file_operations ux500_regulator_power_state_cnt_fops = {
	.open = ux500_regulator_power_state_cnt_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ux500_regulator_power_state_use_print(struct seq_file *s, void *p)
{
	int i;

	seq_printf(s, "\nPower state usage:\n\n");

	for (i = 0; i < ARRAY_SIZE(ux500_atomic_regulators); i++) {
		seq_printf(s, "%s\t : %d\n",
			   ux500_atomic_regulators[i].name,
			   ux500_atomic_regulators[i].count);
	}
	return 0;
}

static int ux500_regulator_power_state_use_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, ux500_regulator_power_state_use_print,
		inode->i_private);
}

static const struct file_operations ux500_regulator_power_state_use_fops = {
	.open = ux500_regulator_power_state_use_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ux500_regulator_status_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int err;
	int i;

	/* print dump header */
	err = seq_printf(s, "ux500-regulator status:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	err = seq_printf(s, "%31s : %8s : %8s\n", "current",
		"before", "after");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	for (i = 0; i < rdebug.num_regulators; i++) {
		struct dbx500_regulator_info *info;
		/* Access per-regulator data */
		info = &rdebug.regulator_array[i];

		/* print status */
		err = seq_printf(s, "%20s : %8s : %8s : %8s\n", info->desc.name,
			info->is_enabled ? "enabled" : "disabled",
			rdebug.state_before_suspend[i] ? "enabled" : "disabled",
			rdebug.state_after_suspend[i] ? "enabled" : "disabled");
		if (err < 0)
			dev_err(dev, "seq_printf overflow\n");
	}

	return 0;
}

static int ux500_regulator_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, ux500_regulator_status_print,
		inode->i_private);
}

static const struct file_operations ux500_regulator_status_fops = {
	.open = ux500_regulator_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

int __attribute__((weak)) dbx500_regulator_testcase(
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	return 0;
}

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES
static int dbx500_panic_regulator_enable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL)
		return -EINVAL;

	info->is_enabled = 1;
	if (!info->exclude_from_power_state){
		power_state_active_cnt++;
	}

	return 0;
}

static int dbx500_panic_regulator_disable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret = 0;

	if (info == NULL)
		return -EINVAL;

	info->is_enabled = 0;
	if (!info->exclude_from_power_state){
		if (power_state_active_cnt <= 0) {
			pr_emerg("power state: unbalanced enable/disable calls\n");
			return -EINVAL;
		}
		
		power_state_active_cnt--;
	}

	return ret;
}

static int dbx500_panic_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL)
		return -EINVAL;

	return info->is_enabled;
}

/* db8500 regulator operations */
static struct regulator_ops dbx500_regulator_ops = {
	.enable			= dbx500_panic_regulator_enable,
	.disable		= dbx500_panic_regulator_disable,
	.is_enabled		= dbx500_panic_regulator_is_enabled,
};

static int enable_panic_epod(u16 epod_id, bool ramret)
{
	int ret;

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES
	if (ramret) {
		if (!epod_on[epod_id]) {
			ret = prcmu_panic_set_epod(epod_id, EPOD_STATE_RAMRET);
			if (ret < 0)
				return ret;
		}
		epod_ramret[epod_id] = true;
	} else {
		ret = prcmu_panic_set_epod(epod_id, EPOD_STATE_ON);
		if (ret < 0)
			return ret;
		epod_on[epod_id] = true;
	}
#endif
	return 0;
}

static int disable_panic_epod(u16 epod_id, bool ramret)
{
	int ret = -EPERM;

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES
	if (ramret) {
		if (!epod_on[epod_id]) {
			ret = prcmu_panic_set_epod(epod_id, EPOD_STATE_OFF);
			if (ret < 0)
				return ret;
		}
		epod_ramret[epod_id] = false;
	} else {
		if (epod_ramret[epod_id]) {
			ret = prcmu_panic_set_epod(epod_id, EPOD_STATE_RAMRET);
			if (ret < 0)
				return ret;
		} else {
			ret = prcmu_set_epod(epod_id, EPOD_STATE_OFF);
			if (ret < 0)
				return ret;
		}
		epod_on[epod_id] = false;
	}
#endif
	return 0;
}

/*
 * Regulator switch
 */
static int dbx500_panic_regulator_switch_enable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (info == NULL)
		return -EINVAL;

	ret = enable_panic_epod(info->epod_id, info->is_ramret);
	if (ret < 0) {
		pr_emerg("regulator-switch-%s-enable: prcmu call failed\n",
			info->desc.name);
		goto out;
	}

	info->is_enabled = 1;
out:
	return ret;
}

static int dbx500_panic_regulator_switch_disable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret = -EPERM;

	if (info == NULL)
		return -EINVAL;

	ret = disable_panic_epod(info->epod_id, info->is_ramret);
	if (ret < 0) {
		pr_emerg("regulator_switch-%s-disable: prcmu call failed\n",
			info->desc.name);
		goto out;
	}

	info->is_enabled = 0;
out:
	return ret;
}

static int dbx500_panic_regulator_switch_is_enabled(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL)
		return -EINVAL;

	return info->is_enabled;
}

static struct regulator_ops dbx500_regulator_switch_ops = {
	.enable			= dbx500_panic_regulator_switch_enable,
	.disable		= dbx500_panic_regulator_switch_disable,
	.is_enabled		= dbx500_panic_regulator_switch_is_enabled,
};

void db8500_reroute_regulator_fp(void)
{
	dbx500_regulator_ops.enable = dbx500_panic_regulator_enable;
	dbx500_regulator_ops.disable = dbx500_panic_regulator_disable;
	dbx500_regulator_ops.is_enabled = dbx500_panic_regulator_is_enabled;

	dbx500_regulator_switch_ops.enable = dbx500_panic_regulator_switch_enable;
	dbx500_regulator_switch_ops.disable = dbx500_panic_regulator_switch_disable;
	dbx500_regulator_switch_ops.is_enabled = dbx500_panic_regulator_switch_is_enabled;

}
#endif //CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES

int __devinit
ux500_regulator_debug_init(struct platform_device *pdev,
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	/* create directory */
	rdebug.dir = debugfs_create_dir("ux500-regulator", NULL);
	if (IS_ERR_OR_NULL(rdebug.dir))
		goto exit_no_debugfs;

	/* create "status" file */
	if (IS_ERR_OR_NULL(debugfs_create_file("status",
					       S_IRUGO, rdebug.dir, &pdev->dev,
					       &ux500_regulator_status_fops)))
		goto exit_fail;

	/* create "power-state-count" file */
	if (IS_ERR_OR_NULL(debugfs_create_file("power-state-count",
					       S_IRUGO, rdebug.dir, &pdev->dev,
					       &ux500_regulator_power_state_cnt_fops)))
		goto exit_fail;

	/* create "power-state-count" file */
	if (IS_ERR_OR_NULL(debugfs_create_file("power-state-usage",
					       S_IRUGO, rdebug.dir, &pdev->dev,
					       &ux500_regulator_power_state_use_fops)))
		goto exit_fail;


	rdebug.regulator_array = regulator_info;
	rdebug.num_regulators = num_regulators;

	rdebug.state_before_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_before_suspend) {
		dev_err(&pdev->dev,
			"could not allocate memory for saving state\n");
		goto exit_fail;
	}

	rdebug.state_after_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_after_suspend) {
		dev_err(&pdev->dev,
			"could not allocate memory for saving state\n");
		goto exit_fail;
	}

	dbx500_regulator_testcase(regulator_info, num_regulators);
	return 0;

exit_fail:
	kfree(rdebug.state_before_suspend);
	debugfs_remove_recursive(rdebug.dir);
exit_no_debugfs:
	dev_err(&pdev->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

int __devexit ux500_regulator_debug_exit(void)
{
	debugfs_remove_recursive(rdebug.dir);
	kfree(rdebug.state_after_suspend);
	kfree(rdebug.state_before_suspend);

	return 0;
}
#endif
