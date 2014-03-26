/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * License Terms: GNU General Public License v2
 * Author: Sundar Iyer
 * Author: Martin Persson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/id.h>


static struct cpufreq_frequency_table *freq_table;
static int freq_table_len;

struct clk *arm_clk;

static struct freq_attr *dbx500_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static int dbx500_cpufreq_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int dbx500_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	struct cpufreq_freqs freqs;
	unsigned int idx;
	int ret;

	/* scale the target frequency to one of the extremes supported */
	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;
	if (target_freq > policy->cpuinfo.max_freq)
		target_freq = policy->cpuinfo.max_freq;

	/* Lookup the next frequency */
	if (cpufreq_frequency_table_target
			(policy, freq_table, target_freq, relation, &idx)) {
		return -EINVAL;
	}

	freqs.old = policy->cur;
	freqs.new = freq_table[idx].frequency;

	if (freqs.old == freqs.new)
		return 0;

	/* pre-change notification */
	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	BUG_ON(idx >= freq_table_len);

	/* request the clk change*/
	ret  = clk_set_rate(arm_clk, freqs.new*1000);

	if (ret)
		pr_err("dbx500-cpufreq : Failed to set arm_clk\n");

	/* post change notification */
	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return ret;
}

static unsigned int dbx500_cpufreq_getspeed(unsigned int cpu)
{
	unsigned int rate = clk_get_rate(arm_clk);
	return rate / 1000;
}

static int __cpuinit dbx500_cpufreq_init(struct cpufreq_policy *policy)
{
	int res;
	int i;

	/* get policy fields based on the table */
	res = cpufreq_frequency_table_cpuinfo(policy, freq_table);
	if (!res)
		cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	else {
		pr_err("dbx500-cpufreq : Failed to read policy table\n");
		return res;
	}

	policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->cur = dbx500_cpufreq_getspeed(policy->cpu);

	for (i = 0; freq_table[i].frequency != policy->cur; i++)
		;

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	/*
	 * FIXME : Need to take time measurement across the target()
	 *	   function with no/some/all drivers in the notification
	 *	   list.
	 */
	policy->cpuinfo.transition_latency = 20 * 1000; /* in ns */

	/* policy sharing between dual CPUs */
	cpumask_copy(policy->cpus, &cpu_present_map);

	policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;

	return 0;
}

static struct cpufreq_driver dbx500_cpufreq_driver = {
	.flags  = CPUFREQ_STICKY,
	.verify = dbx500_cpufreq_verify_speed,
	.target = dbx500_cpufreq_target,
	.get    = dbx500_cpufreq_getspeed,
	.init   = dbx500_cpufreq_init,
	.name   = "DBX500",
	.attr   = dbx500_cpufreq_attr,
};

static int dbx500_cpu_freq_probe(struct platform_device *pdev)
{
	int i, ret;
	freq_table = dev_get_platdata(pdev->dev.parent);

	pr_info("dbx500-cpufreq : Available frequencies:\n");

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
		pr_info("  %d Mhz\n", freq_table[i].frequency / 1000);
	freq_table_len = i;

	arm_clk = clk_get(&pdev->dev, "arm_clk");

	if (IS_ERR(arm_clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(arm_clk);
		return ret;
	}
	return cpufreq_register_driver(&dbx500_cpufreq_driver);
}

static struct platform_driver db8500_cpu_freq_driver = {
	.driver = {
		.name = "cpufreq-ux500",
		.owner = THIS_MODULE,
	},
	.probe = dbx500_cpu_freq_probe,
};

static int __init dbx500_cpufreq_register(void)
{
	return platform_driver_register(&db8500_cpu_freq_driver);
}

device_initcall(dbx500_cpufreq_register);
