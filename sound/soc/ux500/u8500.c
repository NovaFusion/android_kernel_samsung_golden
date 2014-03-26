/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Ola Lilja (ola.o.lilja@stericsson.com)
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
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <asm/mach-types.h>

#include "ux500_pcm.h"
#include "ux500_msp_dai.h"

#include <linux/spi/spi.h>
#include <sound/initval.h>

#ifdef CONFIG_SND_SOC_UX500_AB3550
#include "ux500_ab3550.h"
#endif

#ifdef CONFIG_SND_SOC_UX500_AB8500
#include <sound/ux500_ab8500.h>
#endif

#ifdef CONFIG_SND_SOC_UX500_AV8100
#include "ux500_av8100.h"
#endif

#ifdef CONFIG_SND_SOC_UX500_CG29XX
#include "ux500_cg29xx.h"
#endif


static struct platform_device *u8500_platform_dev;

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
#else
int ux500_gen_msp0_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int channels = params_channels(params);
	int err = 0;

	pr_debug("%s: Enter.\n", __func__);
	pr_debug("%s: substream->pcm->name = %s.\n", __func__, substream->pcm->name);
	pr_debug("%s: substream->pcm->id = %s.\n", __func__, substream->pcm->id);
	pr_debug("%s: substream->name = %s.\n", __func__, substream->name);
	pr_debug("%s: substream->number = %d.\n", __func__, substream->number);
	pr_debug("%s: channels = %d.\n", __func__, channels);
	pr_debug("%s: DAI-index (Platform): %d\n", __func__, cpu_dai->id);

	if (channels != 1) {
		pr_err("%s: Only 1 channel.\n", __func__);
		err = -EINVAL;
		goto out_err;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	default:
		pr_err("%s: Only S16_LE.\n", __func__);
		err = -EINVAL;
		goto out_err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai,
				SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS |
				SND_SOC_DAIFMT_NB_NF);

	if (err) {
		pr_err("%s: snd_soc_dai_set_fmt(cpu_dai) failed with %d.\n",
			__func__,
			err);
		goto out_err;
	}

	err = snd_soc_dai_set_sysclk(cpu_dai,
		UX500_MSP_MASTER_CLOCK,
		19200000,
		0);

	if (err) {
		pr_err("%s: snd_soc_dai_set_sysclk(cpu_dai) failed with %d.\n",
			__func__, err);
		goto out_err;
	}

	err = snd_soc_dai_set_tdm_slot(cpu_dai, 0x1, 0x1, 1, 16);

	if (err) {
		pr_err("%s: cg29xx_set_tdm_slot(cpu_dai) failed with %d.\n",
			__func__,
			err);
		goto out_err;
	}
	ux500_msp_dai_set_data_delay(cpu_dai, MSP_DELAY_1);
out_err:
	return err;
}

struct snd_soc_ops ux500_gen_msp0_ops[] = {
	{
		.hw_params = ux500_gen_msp0_hw_params,
	}
};
#endif

/* Define the whole U8500 soundcard, linking platform to the codec-drivers  */
struct snd_soc_dai_link u8500_dai_links[] = {
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
	#ifdef CONFIG_SND_SOC_UX500_AB3550
	{
	.name = "ab3550_0",
	.stream_name = "ab3550_0",
	.cpu_dai_name = "ux500-msp-i2s.0",
	.codec_dai_name = "ab3550-codec-dai.0",
	.platform_name = "ux500-pcm.0",
	.codec_name = "ab3550-codec.11",
	.init = NULL,
	.ops = ux500_ab3550_ops,
	},
	{
	.name = "ab3550_1",
	.stream_name = "ab3550_1",
	.cpu_dai_name = "ux500-msp-i2s.1",
	.codec_dai_name = "ab3550-codec-dai.1",
	.platform_name = "ux500-pcm.0",
	.codec_name = "ab3550-codec.11",
	.init = NULL,
	.ops = ux500_ab3550_ops,
	},
	#endif
	#ifdef CONFIG_SND_SOC_UX500_AB8500
	{
	.name = "ab8500_0",
	.stream_name = "ab8500_0",
	.cpu_dai_name = "ux500-msp-i2s.1",
	.codec_dai_name = "ab8500-codec-dai.0",
	.platform_name = "ux500-pcm.0",
	.codec_name = "ab8500-codec.0",
	.init = ux500_ab8500_machine_codec_init,
	.ops = ux500_ab8500_ops,
	},
	{
	.name = "ab8500_1",
	.stream_name = "ab8500_1",
	.cpu_dai_name = "ux500-msp-i2s.3",
	.codec_dai_name = "ab8500-codec-dai.1",
	.platform_name = "ux500-pcm.0",
	.codec_name = "ab8500-codec.0",
	.init = NULL,
	.ops = ux500_ab8500_ops,
	},
	#endif
	#ifdef CONFIG_SND_SOC_UX500_CG29XX
	{
	.name = "cg29xx_0",
	.stream_name = "cg29xx_0",
	.cpu_dai_name = "ux500-msp-i2s.0",
	.codec_dai_name = "cg29xx-codec-dai.1",
	.platform_name = "ux500-pcm.0",
	.codec_name = "cg29xx-codec.0",
	.init = NULL,
	.ops = ux500_cg29xx_ops,
	},
	#else
	{
	.name = "msp_0",
	.stream_name = "msp_0",
	.cpu_dai_name = "ux500-msp-i2s.0",
	.codec_dai_name = "snd-soc-dummy-dai",
	.platform_name = "ux500-pcm.0",
	.codec_name = "snd-soc-dummy",
	.init = NULL,
	.ops = ux500_gen_msp0_ops,
	},
	#endif
};

static struct snd_soc_card u8500_drvdata = {
	.name = "U8500-card",
	.probe = NULL,
	.dai_link = u8500_dai_links,
	.num_links = ARRAY_SIZE(u8500_dai_links),
};

static int __init u8500_soc_init(void)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	if (machine_is_u5500())
		return 0;

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

	#ifdef CONFIG_SND_SOC_UX500_AB8500
	pr_debug("%s: Calling init-function for AB8500 machine driver.\n",
		__func__);
	ret = ux500_ab8500_soc_machine_drv_init();
	if (ret)
		pr_err("%s: ux500_ab8500_soc_machine_drv_init failed (%d).\n",
			__func__, ret);
	#endif

	pr_debug("%s: Register device to generate a probe for Ux500-pcm platform.\n",
		__func__);
	platform_device_register(&ux500_pcm);

	pr_debug("%s: Allocate platform device 'soc-audio'.\n",
		__func__);
	u8500_platform_dev = platform_device_alloc("soc-audio", -1);
	if (!u8500_platform_dev)
		return -ENOMEM;

	pr_debug("%s: Card %s: num_links = %d\n",
		__func__,
		u8500_drvdata.name,
		u8500_drvdata.num_links);
	pr_debug("%s: Card %s: DAI-link 0: name = %s\n",
		__func__,
		u8500_drvdata.name,
		u8500_drvdata.dai_link[0].name);
	pr_debug("%s: Card %s: DAI-link 0: stream_name = %s\n",
		__func__,
		u8500_drvdata.name,
		u8500_drvdata.dai_link[0].stream_name);

	pr_debug("%s: Card %s: Set platform drvdata.\n",
		__func__,
		u8500_drvdata.name);
	platform_set_drvdata(u8500_platform_dev, &u8500_drvdata);
	u8500_drvdata.dev = &u8500_platform_dev->dev;

	pr_debug("%s: Card %s: Add platform device.\n",
		__func__,
		u8500_drvdata.name);
	ret = platform_device_add(u8500_platform_dev);
	if (ret) {
		pr_err("%s: Error: Failed to add platform device (%s).\n",
			__func__,
			u8500_drvdata.name);
		platform_device_put(u8500_platform_dev);
	}

	return ret;
}

static void __exit u8500_soc_exit(void)
{
	pr_debug("%s: Enter.\n", __func__);

	#ifdef CONFIG_SND_SOC_UX500_AB8500
	pr_debug("%s: Calling exit-function for AB8500 machine driver.\n",
		__func__);
	ux500_ab8500_soc_machine_drv_cleanup();
	#endif

	pr_debug("%s: Unregister platform device (%s).\n",
		__func__,
		u8500_drvdata.name);
	platform_device_unregister(u8500_platform_dev);
}

module_init(u8500_soc_init);
module_exit(u8500_soc_exit);

MODULE_LICENSE("GPLv2");
