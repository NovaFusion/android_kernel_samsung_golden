/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson MCDE driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#include <linux/init.h>
#include <linux/module.h>

#include <video/mcde.h>
#include <video/mcde_fb.h>
#include <video/mcde_dss.h>
#include <video/mcde_display.h>

/* Module init */

static int __init mcde_subsystem_init(void)
{
	int ret;
	pr_info("MCDE subsystem init begin\n");

	/* MCDE module init sequence */
	ret = mcde_init();
	if (ret)
		goto mcde_failed;
	ret = mcde_display_init();
	if (ret)
		goto mcde_display_failed;
	ret = mcde_dss_init();
	if (ret)
		goto mcde_dss_failed;
#ifdef CONFIG_FB_MCDE
	ret = mcde_fb_init();
	if (ret)
		goto mcde_fb_failed;
#endif
	pr_info("MCDE subsystem init done\n");

	goto done;
#ifdef CONFIG_FB_MCDE
mcde_fb_failed:
	mcde_dss_exit();
#endif
mcde_dss_failed:
	mcde_display_exit();
mcde_display_failed:
	mcde_exit();
mcde_failed:
done:
	return ret;
}
#ifdef MODULE
module_init(mcde_subsystem_init);
#else
fs_initcall(mcde_subsystem_init);
#endif

static void __exit mcde_module_exit(void)
{
	mcde_exit();
	mcde_display_exit();
	mcde_dss_exit();
}
module_exit(mcde_module_exit);

MODULE_AUTHOR("Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson MCDE driver");

