/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Hanumath Prasad <hanumath.prasad@stericsson.com>
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
#include <mach/ste-dma40-db8500.h>

#include "devices-db8500.h"
#include "board-mop500.h"
#include "prcc.h"
#include "cpu-db8500.h"

/* Reset pl18x controllers */
void mop500_sdi_reset(struct device *dev)
{
	struct amba_device *amba_dev =
		container_of(dev, struct amba_device, dev);

	switch (amba_dev->res.start) {
	case U8500_SDI0_BASE:
		u8500_reset_ip(1, PRCC_K_SOFTRST_SDI0_MASK);
		break;
	case U8500_SDI1_BASE:
		u8500_reset_ip(2, PRCC_K_SOFTRST_SDI1_MASK);
		break;
	case U8500_SDI2_BASE:
		u8500_reset_ip(3, PRCC_K_SOFTRST_SDI2_MASK);
		break;
	case U8500_SDI3_BASE:
		u8500_reset_ip(2, PRCC_K_SOFTRST_SDI3_MASK);
		break;
	case U8500_SDI4_BASE:
		u8500_reset_ip(2, PRCC_K_SOFTRST_SDI4_MASK);
		break;
	case U8500_SDI5_BASE:
		u8500_reset_ip(3, PRCC_K_SOFTRST_SDI5_MASK);
		break;
	default:
		dev_warn(dev, "Unknown SDI controller\n");
	}
}

/*
 * SDI 0 (MicroSD slot)
 */

/* GPIO pins used by the sdi0 level shifter */
static int sdi0_en = -1;
static int sdi0_vsel = -1;

static int mop500_sdi0_ios_handler(struct device *dev, struct mmc_ios *ios,
				   enum rpm_status pm)
{
	static unsigned char power_mode = MMC_POWER_ON;
	static unsigned char signal_voltage = MMC_SIGNAL_VOLTAGE_330;

	if (signal_voltage == ios->signal_voltage)
		goto do_power;

	/*
	 * We need to re-init the levelshifter when switching I/O voltage level.
	 * Max discharge time according to ST6G3244ME spec is 1 ms.
	 */
	if (power_mode == MMC_POWER_ON) {
		power_mode = MMC_POWER_OFF;
		gpio_direction_output(sdi0_en, 0);
		usleep_range(1000, 2000);
	}

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		gpio_direction_output(sdi0_vsel, 0);
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		gpio_direction_output(sdi0_vsel, 1);
		break;
	default:
		pr_warning("Non supported signal voltage for levelshifter.\n");
		break;
	}

	signal_voltage = ios->signal_voltage;

do_power:
	if (power_mode == ios->power_mode)
		goto do_pm;

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		break;
	case MMC_POWER_ON:
		gpio_direction_output(sdi0_en, 1);
		/* Max settling time according to ST6G3244ME spec is 100 us. */
		udelay(100);
		break;
	case MMC_POWER_OFF:
		gpio_direction_output(sdi0_en, 0);
		break;
	}

	power_mode = ios->power_mode;

do_pm:
	if ((pm == RPM_SUSPENDING) && (power_mode == MMC_POWER_ON)) {
		/* Disable levelshifter to save power */
		gpio_direction_output(sdi0_en, 0);
	} else if ((pm == RPM_RESUMING) && (power_mode == MMC_POWER_ON)) {
		/* Re-enable levelshifter. */
		gpio_direction_output(sdi0_en, 1);
		/* Max settling time according to ST6G3244ME spec is 100 us. */
		udelay(100);
	}

	return 0;
}

#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg mop500_sdi0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB8500_DMA_DEV1_SD_MMC0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
	.use_fixed_channel = true,
	.phy_channel = 0,
};

static struct stedma40_chan_cfg mop500_sdi0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV1_SD_MMC0_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
	.use_fixed_channel = true,
	.phy_channel = 0,
};
#endif

static struct mmci_platform_data mop500_sdi0_data = {
	.ios_handler	= mop500_sdi0_ios_handler,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_MMC_HIGHSPEED |
				MMC_CAP_UHS_SDR12 |
				MMC_CAP_UHS_SDR25 |
				MMC_CAP_UHS_DDR50,
	.capabilities2	= MMC_CAP2_DETECT_ON_ERR,
	.gpio_wp	= -1,
	.levelshifter	= true,
	.sigdir		= MCI_ST_FBCLKEN |
				MCI_ST_CMDDIREN |
				MCI_ST_DATA0DIREN |
				MCI_ST_DATA2DIREN,
	.reset		= mop500_sdi_reset,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi0_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi0_dma_cfg_tx,
#endif
};

/*
 * SDI1 (SDIO WLAN)
 */
#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg sdi1_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB8500_DMA_DEV32_SD_MM1_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg sdi1_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV32_SD_MM1_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data mop500_sdi1_data = {
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	.reset		= mop500_sdi_reset,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi1_dma_cfg_rx,
	.dma_tx_param	= &sdi1_dma_cfg_tx,
#endif
};

static void sdi0_sdi1_configure(void)
{
	int ret;
	/* v2 has a new version of this block that need to be forced */
	u32 periphid = 0x10480180;

	ret = gpio_request(sdi0_en, "level shifter enable");
	if (!ret)
		ret = gpio_request(sdi0_vsel,
				   "level shifter 1v8-3v select");

	if (ret) {
		pr_warning("unable to config sdi0 gpios for level shifter.\n");
		return;
	}

	/* Select the default 2.9V and enable level shifter */
	gpio_direction_output(sdi0_vsel, 0);
	gpio_direction_output(sdi0_en, 1);


	db8500_add_sdi0(&mop500_sdi0_data, periphid);
	db8500_add_sdi1(&mop500_sdi1_data, periphid);
}

void mop500_sdi_tc35892_init(void)
{
	mop500_sdi0_data.gpio_cd = GPIO_SDMMC_CD;
	sdi0_en = GPIO_SDMMC_EN;
	sdi0_vsel = GPIO_SDMMC_1V8_3V_SEL;
	sdi0_sdi1_configure();
}

/*
 * SDI 2 (POP eMMC, not on DB8500ed)
 */
#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg mop500_sdi2_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV28_SD_MM2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg mop500_sdi2_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV28_SD_MM2_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data mop500_sdi2_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED |
				MMC_CAP_ERASE |
				MMC_CAP_1_8V_DDR |
				MMC_CAP_UHS_DDR50,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	.reset		= mop500_sdi_reset,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi2_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi2_dma_cfg_tx,
#endif
};

/*
 * SDI 4 (on-board eMMC)
 */

#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg mop500_sdi4_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV42_SD_MM4_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg mop500_sdi4_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV42_SD_MM4_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data mop500_sdi4_data = {
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED |
				MMC_CAP_1_8V_DDR |
				MMC_CAP_UHS_DDR50,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	.reset		= mop500_sdi_reset,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi4_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi4_dma_cfg_tx,
#endif
};

void __init mop500_sdi_init(void)
{
	/* v2 has a new version of this block that need to be forced */
	u32 periphid = 0x10480180;

	/* sdi2 on snowball is in ATL_B mode for FSMC (LAN) */
	if (!machine_is_snowball())
		db8500_add_sdi2(&mop500_sdi2_data, periphid);

	/* On-board eMMC */
	db8500_add_sdi4(&mop500_sdi4_data, periphid);

	if (machine_is_hrefv60() || machine_is_u8520() ||
	    machine_is_snowball() || machine_is_u9540()) {
		if (machine_is_hrefv60() || machine_is_u9540()) {
			mop500_sdi0_data.gpio_cd = HREFV60_SDMMC_CD_GPIO;
			sdi0_en = HREFV60_SDMMC_EN_GPIO;
			sdi0_vsel = HREFV60_SDMMC_1V8_3V_GPIO;
		} else if (machine_is_u8520()) {
			mop500_sdi0_data.gpio_cd = U8520_SDMMC_CD_GPIO;
			sdi0_en = U8520_SDMMC_EN_GPIO;
			sdi0_vsel = U8520_SDMMC_1V8_3V_GPIO;
		} else if (machine_is_snowball()) {
			mop500_sdi0_data.gpio_cd = SNOWBALL_SDMMC_CD_GPIO;
			mop500_sdi0_data.cd_invert = true;
			sdi0_en = SNOWBALL_SDMMC_EN_GPIO;
			sdi0_vsel = SNOWBALL_SDMMC_1V8_3V_GPIO;
		}
		sdi0_sdi1_configure();
	}

	/*
	 * On boards with the TC35892 GPIO expander, sdi0 and sdi1 will finally
	 * be added when the TC35892 initializes and calls
	 * mop500_sdi_tc35892_init() above.
	 */
}
