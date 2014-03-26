/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <plat/pincfg.h>
#include <linux/gpio.h>

#include "pins.h"

static LIST_HEAD(pin_lookups);
static DEFINE_MUTEX(pin_lookups_mutex);
static DEFINE_SPINLOCK(pins_lock);

void __init ux500_pins_add(struct ux500_pin_lookup *pl, size_t num)
{
	mutex_lock(&pin_lookups_mutex);

	while (num--) {
		list_add_tail(&pl->node, &pin_lookups);
		pl++;
	}

	mutex_unlock(&pin_lookups_mutex);
}

struct ux500_pins *ux500_pins_get(const char *name)
{
	struct ux500_pins *pins = NULL;
	struct ux500_pin_lookup *pl;

	mutex_lock(&pin_lookups_mutex);

	list_for_each_entry(pl, &pin_lookups, node) {
		if (!strcmp(pl->name, name)) {
			pins = pl->pins;
			goto out;
		}
	}

out:
	mutex_unlock(&pin_lookups_mutex);
	return pins;
}

int ux500_pins_enable(struct ux500_pins *pins)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&pins_lock, flags);

	if (pins->usage++ == 0)
		ret = nmk_config_pins(pins->cfg, pins->num);

	spin_unlock_irqrestore(&pins_lock, flags);
	return ret;
}

int ux500_pins_disable(struct ux500_pins *pins)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&pins_lock, flags);

	if (WARN_ON(pins->usage == 0))
		goto out;

	if (--pins->usage == 0)
		ret = nmk_config_pins_sleep(pins->cfg, pins->num);

out:
	spin_unlock_irqrestore(&pins_lock, flags);
	return ret;
}

void ux500_pins_put(struct ux500_pins *pins)
{
	WARN_ON(!pins);
}

void __init ux500_offchip_gpio_init(struct ux500_pins *pins)
{
	int err;
	int i;
	int gpio;
	int output;
	int value;
	pin_cfg_t cfg;

	for (i = 0; i < pins->num; i++) {
		cfg = pins->cfg[i];
		gpio = PIN_NUM(cfg);
		output = PIN_DIR(cfg);
		value = PIN_VAL(cfg);

		err = gpio_request(gpio, "offchip_gpio_init");
		if (err < 0) {
			pr_err("pins: gpio_request for gpio=%d failed with"
				"err: %d\n", gpio, err);
			/* Pin already requested. Try to configure rest. */
			continue;
		}

		if (!output) {
			err = gpio_direction_input(gpio);
			if (err < 0)
				pr_err("pins: gpio_direction_input for gpio=%d"
					"failed with err: %d\n", gpio, err);
		} else {
			err = gpio_direction_output(gpio, value);
			if (err < 0)
				pr_err("pins: gpio_direction_output for gpio="
					"%d failed with err: %d\n", gpio, err);
		}
		gpio_free(gpio);
	}
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <plat/gpio-nomadik.h>

#include <mach/gpio.h>

static void show_pin(struct seq_file *s, pin_cfg_t pin)
{
	static char *afnames[] = {
		[NMK_GPIO_ALT_GPIO]	= "GPIO",
		[NMK_GPIO_ALT_A]	= "A",
		[NMK_GPIO_ALT_B]	= "B",
		[NMK_GPIO_ALT_C]	= "C"
	};
	static char *pullnames[] = {
		[NMK_GPIO_PULL_NONE]	= "none",
		[NMK_GPIO_PULL_UP]	= "up",
		[NMK_GPIO_PULL_DOWN]	= "down",
		[3] /* illegal */	= "??"
	};

	int pin_num = PIN_NUM(pin);
	int pull = PIN_PULL(pin);
	int af = PIN_ALT(pin);
	int slpm = PIN_SLPM(pin);
	int output = PIN_DIR(pin);
	int val = PIN_VAL(pin);
	int slpm_pull = PIN_SLPM_PULL(pin);
	int slpm_dir = PIN_SLPM_DIR(pin);
	int slpm_val = PIN_SLPM_VAL(pin);

	seq_printf(s,
		   "  pin %d [%#lx]: af %s, pull %s (%s%s) - slpm: %s%s%s%s\n",
		   pin_num, pin, afnames[af],
		   pullnames[pull],
		   output ? "output " : "input",
		   output ? (val ? "high" : "low") : "",
		   slpm ? "no-change/no-wakeup " : "input/wakeup ",
		   slpm_dir ? (slpm_dir == 1 ? "input " : "output ") : "",
		   slpm_dir == 1 ? (slpm_pull == 0 ? "pull: none " :
				    (slpm_pull == NMK_GPIO_PULL_UP ?
				     "pull: up " : "pull: down ") ): "",
		   slpm_dir == 2 ? (slpm_val == 1 ? "low " : "high ") : "");
}

static int pins_dbg_show(struct seq_file *s, void *iter)
{
	struct ux500_pin_lookup *pl;
	int i;
	bool *pins;
	int prev = -2;
	int first = 0;

	pins = kzalloc(sizeof(bool) * NOMADIK_NR_GPIO, GFP_KERNEL);

	mutex_lock(&pin_lookups_mutex);

	list_for_each_entry(pl, &pin_lookups, node) {
		seq_printf(s, "\n%s (%d) usage: %d\n",
			   pl->name, pl->pins->num, pl->pins->usage);
		for (i = 0; i < pl->pins->num; i++) {
			show_pin(s, pl->pins->cfg[i]);
			pins[PIN_NUM(pl->pins->cfg[i])] = true;
		}
	}
	mutex_unlock(&pin_lookups_mutex);

	seq_printf(s, "\nSummary allocated pins:\n");
	for (i = 0; i < NOMADIK_NR_GPIO; i++) {
		if (prev == i - 1) {
			if (pins[i])
				prev = i;
			else
				if (prev > 0) {
					if  (first != prev)
						seq_printf(s, "-%d, ", prev);
					else
						seq_printf(s, ", ");
				}
			continue;
		}
		if (pins[i]) {
			seq_printf(s, "%d", i);
			prev = i;
			first = i;
		}
	}
	if (prev == i - 1 && first != prev)
		seq_printf(s, "-%d", prev);

	seq_printf(s, "\n");

	kfree(pins);

	return 0;
}

static int pins_dbg_open(struct inode *inode,
			  struct file *file)
{
	return single_open(file, pins_dbg_show, inode->i_private);
}

static const struct file_operations pins_fops = {
	.open = pins_dbg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int __init pins_dbg_init(void)
{
	(void) debugfs_create_file("pins", S_IRUGO,
				   NULL,
				   NULL,
				   &pins_fops);
	return 0;
}
late_initcall(pins_dbg_init);
#endif
