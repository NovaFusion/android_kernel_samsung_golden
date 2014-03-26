/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Xie Xiaolei (xie.xiaolei@stericsson.com)
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/io.h>
#include <sound/soc.h>
#include <asm/mach-types.h>

#include "ux500_pcm.h"
#include "ux500_msp_dai.h"

#include <linux/spi/spi.h>
#include <sound/initval.h>

#ifdef CONFIG_SND_SOC_UX500_AB5500
#include "ux500_ab5500.h"
#endif

#ifdef CONFIG_SND_SOC_UX500_AV8100
#include "ux500_av8100.h"
#endif
#ifdef CONFIG_SND_SOC_UX500_CG29XX
#include "ux500_cg29xx.h"
#endif
static struct platform_device *u5500_platform_dev;
/* Create dummy devices for platform drivers */

static struct platform_device ux500_pcm = {
		.name = "ux500-pcm",
		.id = 0,
		.dev = {
			.platform_data = NULL,
		},
};

#ifdef CONFIG_SND_SOC_UX500_AV8100
static struct platform_device av8100_codec = {
		.name = "av8100-codec",
		.id = 0,
		.dev = {
			.platform_data = NULL,
		},
};
#endif

#ifdef CONFIG_SND_SOC_UX500_CG29XX
static struct platform_device cg29xx_codec = {
		.name = "cg29xx-codec",
		.id = 0,
		.dev = {
			.platform_data = NULL,
		},
};
#endif
/* Define the whole U5500 soundcard, linking platform to the codec-drivers  */
struct snd_soc_dai_link u5500_dai_links[] = {
	{
		.name = "ab5500_0",
		.stream_name = "ab5500_0",
		.cpu_dai_name = "ux500-msp-i2s.0",
		.codec_dai_name = "ab5500-codec-dai.0",
		.platform_name = "ux500-pcm.0",
		.codec_name = "ab5500-codec.0",
		.init = ux500_ab5500_machine_codec_init,
		.ops = (struct snd_soc_ops[]) {
			{
				.startup = ux500_ab5500_startup,
				.shutdown = ux500_ab5500_shutdown,
				.hw_params = ux500_ab5500_hw_params,
			}
		}
	},
	#ifdef CONFIG_SND_SOC_UX500_CG29XX
	{
		.name = "cg29xx_0",
		.stream_name = "cg29xx_0",
		.cpu_dai_name = "ux500-msp-i2s.1",
		.codec_dai_name = "cg29xx-codec-dai.0",
		.platform_name = "ux500-pcm.0",
		.codec_name = "cg29xx-codec.0",
		.init = NULL,
		.ops = u5500_cg29xx_ops,
	},
	{
		.name = "cg29xx_1",
		.stream_name = "cg29xx_1",
		.cpu_dai_name = "ux500-msp-i2s.0",
		.codec_dai_name = "cg29xx-codec-dai.1",
		.platform_name = "ux500-pcm.0",
		.codec_name = "cg29xx-codec.0",
		.init = NULL,
		.ops = u5500_cg29xx_ops,
	},
	#endif
	{
		.name = "ab5500_1",
		.stream_name = "ab5500_1",
		.cpu_dai_name = "ux500-msp-i2s.1",
		.codec_dai_name = "ab5500-codec-dai.1",
		.platform_name = "ux500-pcm.0",
		.codec_name = "ab5500-codec.0",
		.init = ux500_ab5500_machine_codec_init,
		.ops = (struct snd_soc_ops[]) {
			{
				.startup = ux500_ab5500_startup,
				.shutdown = ux500_ab5500_shutdown,
				.hw_params = ux500_ab5500_hw_params,
			}
		}
	},
	#ifdef CONFIG_SND_SOC_UX500_AV8100
	{
		.name = "hdmi",
		.stream_name = "hdmi",
		.cpu_dai_name = "ux500-msp-i2s.2",
		.codec_dai_name = "av8100-codec-dai",
		.platform_name = "ux500-pcm.0",
		.codec_name = "av8100-codec.0",
		.init = NULL,
		.ops = ux500_av8100_ops,
	},
	#endif
};

static struct snd_soc_card u5500_drvdata = {
	.name = "U5500-card",
	.probe = NULL,
	.dai_link = u5500_dai_links,
	.num_links = ARRAY_SIZE(u5500_dai_links),
};

static int __init u5500_soc_init(void)
{
	int ret = 0;

	pr_debug("%s: Enter.\n", __func__);

	if (!machine_is_u5500())
		return 0;

	pr_debug("%s: Enter.\n", __func__);

	#ifdef CONFIG_SND_SOC_UX500_AV8100
	pr_debug("%s: Register device to generate a probe for AV8100 codec.\n",
		__func__);
	platform_device_register(&av8100_codec);
	#endif

	#ifdef CONFIG_SND_SOC_UX500_CG29XX
	pr_debug("%s: Register device to generate a probe for CG29xx codec.\n",
		__func__);
	platform_device_register(&cg29xx_codec);
	#endif
	pr_debug("%s: Register device to generate a probe for Ux500-pcm platform.\n",
		__func__);
	platform_device_register(&ux500_pcm);

	u5500_platform_dev = platform_device_alloc("soc-audio", -1);
	if (!u5500_platform_dev)
		return -ENOMEM;

	platform_set_drvdata(u5500_platform_dev, &u5500_drvdata);
	u5500_drvdata.dev = &u5500_platform_dev->dev;

	ret = platform_device_add(u5500_platform_dev);
	if (ret) {
		pr_err("%s: Error: Failed to add platform device (%s).\n",
			__func__,
			u5500_drvdata.name);
		platform_device_put(u5500_platform_dev);
	}

	return ret;
}

static void __exit u5500_soc_exit(void)
{
	pr_debug("%s: Enter.\n", __func__);

	platform_device_unregister(u5500_platform_dev);
}

module_init(u5500_soc_init);
module_exit(u5500_soc_exit);

MODULE_LICENSE("GPLv2");
