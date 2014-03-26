/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Hanumath Prasad <ulf.hansson@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include <plat/ste_dma40.h>
#include <mach/devices.h>
#include <mach/hardware.h>
#include <mach/ste-dma40-db5500.h>

#include "devices-db5500.h"
#include "board-u5500.h"

/*
 * SDI 0 (eMMC)
 */
#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg sdi0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB5500_DMA_DEV24_SDMMC0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg sdi0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB5500_DMA_DEV24_SDMMC0_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data u5500_sdi0_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi0_dma_cfg_rx,
	.dma_tx_param	= &sdi0_dma_cfg_tx,
#endif
};

/*
 * SDI 1 (MicroSD slot)
 */

static int u5500_sdi1_ios_handler(struct device *dev, struct mmc_ios *ios)
{
	static int power_mode = -1;

	if (power_mode == ios->power_mode)
		return 0;

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		break;
	case MMC_POWER_ON:
		/*
		 * Level shifter voltage should depend on vdd to when deciding
		 * on either 1.8V or 2.9V. Once the decision has been made the
		 * level shifter must be disabled and re-enabled with a changed
		 * select signal in order to switch the voltage. Since there is
		 * no framework support yet for indicating 1.8V in vdd, use the
		 * default 2.9V.
		 */
		gpio_set_value_cansleep(GPIO_MMC_CARD_CTRL, 1);
		udelay(100);
		break;
	case MMC_POWER_OFF:
		gpio_set_value_cansleep(GPIO_MMC_CARD_CTRL, 0);
		break;
	}

	power_mode = ios->power_mode;
	return 0;
}

#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg sdi1_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB5500_DMA_DEV34_SDMMC1_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg sdi1_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB5500_DMA_DEV34_SDMMC1_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data u5500_sdi1_data = {
	.ios_handler    = u5500_sdi1_ios_handler,
	.ocr_mask       = MMC_VDD_29_30,
	.f_max          = 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_MMC_HIGHSPEED,
	.gpio_cd        = GPIO_SDMMC_CD,
	.gpio_wp        = -1,
	.cd_invert	= true,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi1_dma_cfg_rx,
	.dma_tx_param	= &sdi1_dma_cfg_tx,
#endif
};

/*
 * SDI2 (EMMC2)
 */

static struct stedma40_chan_cfg sdi2_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB5500_DMA_DEV26_SDMMC2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg sdi2_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB5500_DMA_DEV26_SDMMC2_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct mmci_platform_data u5500_sdi2_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi2_dma_cfg_rx,
	.dma_tx_param	= &sdi2_dma_cfg_tx,
#endif
};

/*
 * SDI 3 (SDIO WLAN)
 */
#ifdef SDIO_DMA_ON
#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg sdi3_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB5500_DMA_DEV27_SDMMC3_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg sdi3_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB5500_DMA_DEV27_SDMMC3_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif
#endif

static struct mmci_platform_data u5500_sdi3_data = {
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
#ifdef SDIO_DMA_ON
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi3_dma_cfg_rx,
	.dma_tx_param	= &sdi3_dma_cfg_tx,
#endif
#endif
};

static void sdi1_configure(void)
{
	int pin[2];
	int ret;

	/* Level-shifter GPIOs */
	pin[0] = GPIO_MMC_CARD_CTRL;
	pin[1] = GPIO_MMC_CARD_VSEL;

	ret = gpio_request(pin[0], "MMC_CARD_CTRL");
	if (!ret)
		ret = gpio_request(pin[1], "MMC_CARD_VSEL");

	if (ret) {
		pr_warning("unable to config sdi0 gpios for level shifter.\n");
		return;
	}
	 /* Select the default 2.9V and eanble level shifter */
	gpio_direction_output(pin[0], 1);
	gpio_direction_output(pin[1], 0);
}

void __init u5500_sdi_init(void)
{
	u32 periphid = 0x10480180;

	/*
	 * Dynamic detection of booting device by reading
	 * ROM debug register from BACKUP RAM and register the
	 * corresponding EMMC.
	 */
	if (u5500_get_boot_mmc() == 2)
		db5500_add_sdi2(&u5500_sdi2_data, periphid);
	else
		db5500_add_sdi0(&u5500_sdi0_data, periphid);

	sdi1_configure();
	db5500_add_sdi1(&u5500_sdi1_data, periphid);
	db5500_add_sdi3(&u5500_sdi3_data, periphid);
}
