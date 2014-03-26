/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/switch.h>

static struct switch_dev ux500_android_switch;
void set_android_switch_state(int state);

void set_android_switch_state(int state)
{
	switch_set_state(&ux500_android_switch, state);
}

static int __init android_switch_init(void)
{
	int ret;

	/* Android switch interface */
	ux500_android_switch.name = "usb_audio";

	ret = switch_dev_register(&ux500_android_switch);
	if (ret < 0) {
		printk(KERN_ERR "Switch reg failed %d\n", ret);
		return ret;
	}

	return 0;
}
module_init(android_switch_init);
