/*
 * Copyright (C) 2011 ST-Ericsson
 *
 * Author: Maxime Coquelin <maxime.coquelin-nonst@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/platform_device.h>

#include <mach/mloader-dbx500.h>
#include <mach/hardware.h>

static struct dbx500_ml_area modem_areas[] = {
	{ .name = "modem_trace", .start = 0x6000000, .size = 0xf00000 },
	{ .name = "modem_shared", .start = 0x6f00000, .size = 0x100000 },
	{ .name = "modem_priv", .start = 0x7000000, .size = 0x1000000 },
};

static struct dbx500_ml_fw modem_fws[] = {
	{ .name = "MODEM", .area = &modem_areas[0], .offset = 0x0 },
	{ .name = "IPL", .area = &modem_areas[1], .offset = 0x00 },
};

static struct dbx500_mloader_pdata mloader_fw_data = {
	.fws = modem_fws,
	.nr_fws = ARRAY_SIZE(modem_fws),
	.areas = modem_areas,
	.nr_areas = ARRAY_SIZE(modem_areas),
};

static struct resource mloader_fw_rsrc[] = {
	{
		.start = (U8500_BACKUPRAM1_BASE + 0xF70),
		.end   = (U8500_BACKUPRAM1_BASE + 0xF7C),
		.flags = IORESOURCE_MEM
	}
};

struct platform_device mloader_fw_device = {
	.name = "dbx500_mloader_fw",
	.id = -1,
	.dev = {
		.platform_data	= &mloader_fw_data,
	},
	.resource = mloader_fw_rsrc,
	.num_resources = ARRAY_SIZE(mloader_fw_rsrc)
};

/* Default areas can be overloaded in cmdline */
static int __init early_modem_priv(char *p)
{
	struct dbx500_ml_area *area = &modem_areas[2];

	area->size = memparse(p, &p);

	if (*p == '@')
		area->start = memparse(p + 1, &p);

	return 0;
}
early_param("mem_modem", early_modem_priv);

static int __init early_modem_shared(char *p)
{
	struct dbx500_ml_area *area = &modem_areas[1];

	area->size = memparse(p, &p);

	if (*p == '@')
		area->start = memparse(p + 1, &p);

	return 0;
}
early_param("mem_mshared", early_modem_shared);

static int __init early_modem_trace(char *p)
{
	struct dbx500_ml_area *area = &modem_areas[0];

	area->size = memparse(p, &p);

	if (*p == '@')
		area->start = memparse(p + 1, &p);

	return 0;
}
early_param("mem_mtrace", early_modem_trace);
