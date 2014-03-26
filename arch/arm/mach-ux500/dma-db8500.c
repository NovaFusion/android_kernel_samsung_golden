/*
 * Copyright (C) ST-Ericsson SA 2007-2010
 *
 * Author: Per Friden <per.friden@stericsson.com> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <plat/ste_dma40.h>

#include <mach/hsi.h>
#include <mach/setup.h>
#include <mach/ste-dma40-db8500.h>
#include <mach/pm.h>
#include <mach/context.h>



static struct resource u8500_dma40_resources[] = {
	[0] = {
		.start = U8500_DMA_BASE,
		.end   = U8500_DMA_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "base",
	},
	[1] = {
		.start = U8500_DMA_LCPA_BASE,
		.end   = U8500_DMA_LCPA_BASE + 2 * SZ_1K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcpa",
	},
	[2] = {
		.start = IRQ_DB8500_DMA,
		.end   = IRQ_DB8500_DMA,
		.flags = IORESOURCE_IRQ
	},
	[3] = {
		.start = U8500_DMA_LCLA_BASE,
		.end   = U8500_DMA_LCLA_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcla_esram",
	}
};

static struct resource u9540_dma40_resources[] = {
	[0] = {
		.start = U8500_DMA_BASE,
		.end   = U8500_DMA_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "base",
	},
	[1] = {
		.start = U9540_DMA_LCPA_BASE,
		.end   = U9540_DMA_LCPA_BASE + 2 * SZ_1K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcpa",
	},
	[2] = {
		.start = IRQ_DB8500_DMA,
		.end   = IRQ_DB8500_DMA,
		.flags = IORESOURCE_IRQ
	},
	[3] = {
		.start = U8500_DMA_LCLA_BASE,
		.end   = U8500_DMA_LCLA_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcla_esram",
	}
};

/* Default configuration for physcial memcpy */
static struct stedma40_chan_cfg dma40_memcpy_conf_phy = {
	.mode = STEDMA40_MODE_PHYSICAL,
	.dir = STEDMA40_MEM_TO_MEM,

	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.src_info.psize = STEDMA40_PSIZE_PHY_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.psize = STEDMA40_PSIZE_PHY_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

};

/* Default configuration for logical memcpy */
static struct stedma40_chan_cfg dma40_memcpy_conf_log = {
	.dir = STEDMA40_MEM_TO_MEM,

	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.src_info.psize = STEDMA40_PSIZE_LOG_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.psize = STEDMA40_PSIZE_LOG_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

};

/*
 * Mapping between soruce event lines and physical device address
 * This was created assuming that the event line is tied to a device and
 * therefore the address is constant, however this is not true for at least
 * USB, and the values are just placeholders for USB.  This table is preserved
 * and used for now.
 */
static dma_addr_t dma40_rx_map[DB8500_DMA_NR_DEV] = {
	[DB8500_DMA_DEV0_SPI0_RX] = 0,
	[DB8500_DMA_DEV1_SD_MMC0_RX] = U8500_SDI0_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV2_SD_MMC1_RX] = 0,
	[DB8500_DMA_DEV3_SD_MMC2_RX] = 0,
	[DB8500_DMA_DEV4_I2C1_RX] = 0,
	[DB8500_DMA_DEV5_I2C3_RX] = 0,
	[DB8500_DMA_DEV6_I2C2_RX] = 0,
	[DB8500_DMA_DEV7_I2C4_RX] = 0,
	[DB8500_DMA_DEV8_SSP0_RX] =  U8500_SSP0_BASE + SSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV9_SSP1_RX] = 0,
	[DB8500_DMA_DEV10_MCDE_RX] = 0,
	[DB8500_DMA_DEV11_UART2_RX] = 0,
	[DB8500_DMA_DEV12_UART1_RX] = 0,
	[DB8500_DMA_DEV13_UART0_RX] = 0,
	[DB8500_DMA_DEV14_MSP2_RX] = U8500_MSP2_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV15_I2C0_RX] = 0,
	[DB8500_DMA_DEV16_USB_OTG_IEP_7_15] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV17_USB_OTG_IEP_6_14] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV18_USB_OTG_IEP_5_13] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV19_USB_OTG_IEP_4_12] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV20_SLIM0_CH0_RX_HSI_RX_CH0] = U8500_HSIR_BASE + 0x0 + STE_HSI_RX_BUFFERX,
	[DB8500_DMA_DEV21_SLIM0_CH1_RX_HSI_RX_CH1] = U8500_HSIR_BASE + 0x4 + STE_HSI_RX_BUFFERX,
	[DB8500_DMA_DEV22_SLIM0_CH2_RX_HSI_RX_CH2] = U8500_HSIR_BASE + 0x8 + STE_HSI_RX_BUFFERX,
	[DB8500_DMA_DEV23_SLIM0_CH3_RX_HSI_RX_CH3] = U8500_HSIR_BASE + 0xC + STE_HSI_RX_BUFFERX,
	[DB8500_DMA_DEV24_SRC_SXA0_RX_TX] = 0,
	[DB8500_DMA_DEV25_SRC_SXA1_RX_TX] = 0,
	[DB8500_DMA_DEV26_SRC_SXA2_RX_TX] = 0,
	[DB8500_DMA_DEV27_SRC_SXA3_RX_TX] = 0,
	[DB8500_DMA_DEV28_SD_MM2_RX] = U8500_SDI2_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV29_SD_MM0_RX] = U8500_SDI0_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV30_MSP3_RX] = U8500_MSP3_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV31_MSP0_RX_SLIM0_CH0_RX] = U8500_MSP0_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV32_SD_MM1_RX] = U8500_SDI1_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV33_SPI2_RX] = 0,
	[DB8500_DMA_DEV34_I2C3_RX2] = 0,
	[DB8500_DMA_DEV35_SPI1_RX] = 0,
	[DB8500_DMA_DEV36_USB_OTG_IEP_3_11] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV37_USB_OTG_IEP_2_10] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV38_USB_OTG_IEP_1_9] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV39_USB_OTG_IEP_8] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV40_SPI3_RX] = 0,
	[DB8500_DMA_DEV41_SD_MM3_RX] = 0,
	[DB8500_DMA_DEV42_SD_MM4_RX] = U8500_SDI4_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV43_SD_MM5_RX] = 0,
	[DB8500_DMA_DEV44_SRC_SXA4_RX_TX] = 0,
	[DB8500_DMA_DEV45_SRC_SXA5_RX_TX] = 0,
	[DB8500_DMA_DEV46_SLIM0_CH8_RX_SRC_SXA6_RX_TX] = 0,
	[DB8500_DMA_DEV47_SLIM0_CH9_RX_SRC_SXA7_RX_TX] = 0,
	[DB8500_DMA_DEV48_CAC1_RX] = U8500_CRYP1_BASE + CRYP1_RX_REG_OFFSET,
	/* 49, 50 and 51 are not used */
	[DB8500_DMA_DEV52_SLIM0_CH4_RX_HSI_RX_CH4] = 0,
	[DB8500_DMA_DEV53_SLIM0_CH5_RX_HSI_RX_CH5] = 0,
	[DB8500_DMA_DEV54_SLIM0_CH6_RX_HSI_RX_CH6] = 0,
	[DB8500_DMA_DEV55_SLIM0_CH7_RX_HSI_RX_CH7] = 0,
	/* 56, 57, 58, 59 and 60 are not used */
	[DB8500_DMA_DEV61_CAC0_RX] = 0,
	/* 62 and 63 are not used */
};

/* Mapping between destination event lines and physical device address */
static const dma_addr_t dma40_tx_map[DB8500_DMA_NR_DEV] = {
	[DB8500_DMA_DEV0_SPI0_TX] = 0,
	[DB8500_DMA_DEV1_SD_MMC0_TX] = U8500_SDI0_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV2_SD_MMC1_TX] = 0,
	[DB8500_DMA_DEV3_SD_MMC2_TX] = 0,
	[DB8500_DMA_DEV4_I2C1_TX] = 0,
	[DB8500_DMA_DEV5_I2C3_TX] = 0,
	[DB8500_DMA_DEV6_I2C2_TX] = 0,
	[DB8500_DMA_DEV7_I2C4_TX] = 0,
	[DB8500_DMA_DEV8_SSP0_TX] = U8500_SSP0_BASE + SSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV9_SSP1_TX] = 0,
	/* 10 is not used*/
	[DB8500_DMA_DEV11_UART2_TX] = 0,
	[DB8500_DMA_DEV12_UART1_TX] = 0,
	[DB8500_DMA_DEV13_UART0_TX] = 0,
	[DB8500_DMA_DEV14_MSP2_TX] = U8500_MSP2_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV15_I2C0_TX] = 0,
	[DB8500_DMA_DEV16_USB_OTG_OEP_7_15] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV17_USB_OTG_OEP_6_14] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV18_USB_OTG_OEP_5_13] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV19_USB_OTG_OEP_4_12] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV20_SLIM0_CH0_TX_HSI_TX_CH0] = U8500_HSIT_BASE + 0x0 + STE_HSI_TX_BUFFERX,
	[DB8500_DMA_DEV21_SLIM0_CH1_TX_HSI_TX_CH1] = U8500_HSIT_BASE + 0x4 + STE_HSI_TX_BUFFERX,
	[DB8500_DMA_DEV22_SLIM0_CH2_TX_HSI_TX_CH2] = U8500_HSIT_BASE + 0x8 + STE_HSI_TX_BUFFERX,
	[DB8500_DMA_DEV23_SLIM0_CH3_TX_HSI_TX_CH3] = U8500_HSIT_BASE + 0xC + STE_HSI_TX_BUFFERX,
	[DB8500_DMA_DEV24_DST_SXA0_RX_TX] = 0,
	[DB8500_DMA_DEV25_DST_SXA1_RX_TX] = 0,
	[DB8500_DMA_DEV26_DST_SXA2_RX_TX] = 0,
	[DB8500_DMA_DEV27_DST_SXA3_RX_TX] = 0,
	[DB8500_DMA_DEV28_SD_MM2_TX] = U8500_SDI2_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV29_SD_MM0_TX] = U8500_SDI0_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV30_MSP1_TX] = U8500_MSP1_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV31_MSP0_TX_SLIM0_CH0_TX] = U8500_MSP0_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV32_SD_MM1_TX] = U8500_SDI1_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV33_SPI2_TX] = 0,
	[DB8500_DMA_DEV34_I2C3_TX2] = 0,
	[DB8500_DMA_DEV35_SPI1_TX] = 0,
	[DB8500_DMA_DEV36_USB_OTG_OEP_3_11] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV37_USB_OTG_OEP_2_10] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV38_USB_OTG_OEP_1_9] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV39_USB_OTG_OEP_8] = U8500_USBOTG_BASE,
	[DB8500_DMA_DEV40_SPI3_TX] = 0,
	[DB8500_DMA_DEV41_SD_MM3_TX] = 0,
	[DB8500_DMA_DEV42_SD_MM4_TX] = U8500_SDI4_BASE + SD_MMC_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV43_SD_MM5_TX] = 0,
	[DB8500_DMA_DEV44_DST_SXA4_RX_TX] = 0,
	[DB8500_DMA_DEV45_DST_SXA5_RX_TX] = 0,
	[DB8500_DMA_DEV46_SLIM0_CH8_TX_DST_SXA6_RX_TX] = 0,
	[DB8500_DMA_DEV47_SLIM0_CH9_TX_DST_SXA7_RX_TX] = 0,
	[DB8500_DMA_DEV48_CAC1_TX] = U8500_CRYP1_BASE + CRYP1_TX_REG_OFFSET,
	[DB8500_DMA_DEV49_CAC1_TX_HAC1_TX] = 0,
	[DB8500_DMA_DEV50_HAC1_TX] = U8500_HASH1_BASE + HASH1_TX_REG_OFFSET,
	[DB8500_DMA_MEMCPY_TX_0] = 0,
	[DB8500_DMA_DEV52_SLIM1_CH4_TX_HSI_TX_CH4] = 0,
	[DB8500_DMA_DEV53_SLIM1_CH5_TX_HSI_TX_CH5] = 0,
	[DB8500_DMA_DEV54_SLIM1_CH6_TX_HSI_TX_CH6] = 0,
	[DB8500_DMA_DEV55_SLIM1_CH7_TX_HSI_TX_CH7] = 0,
	[DB8500_DMA_MEMCPY_TX_1] = 0,
	[DB8500_DMA_MEMCPY_TX_2] = 0,
	[DB8500_DMA_MEMCPY_TX_3] = 0,
	[DB8500_DMA_MEMCPY_TX_4] = 0,
	[DB8500_DMA_MEMCPY_TX_5] = 0,
	[DB8500_DMA_DEV61_CAC0_TX] = 0,
	[DB8500_DMA_DEV62_CAC0_TX_HAC0_TX] = 0,
	[DB8500_DMA_DEV63_HAC0_TX] = 0,
};

/* Reserved event lines for memcpy only */
static int dma40_memcpy_event[] = {
	DB8500_DMA_MEMCPY_TX_0,
	DB8500_DMA_MEMCPY_TX_1,
	DB8500_DMA_MEMCPY_TX_2,
	DB8500_DMA_MEMCPY_TX_3,
	DB8500_DMA_MEMCPY_TX_4,
	DB8500_DMA_MEMCPY_TX_5,
};

static struct stedma40_platform_data dma40_plat_data = {
	.dev_len = ARRAY_SIZE(dma40_rx_map),
	.dev_rx = dma40_rx_map,
	.dev_tx = dma40_tx_map,
	.memcpy = dma40_memcpy_event,
	.memcpy_len = ARRAY_SIZE(dma40_memcpy_event),
	.memcpy_conf_phy = &dma40_memcpy_conf_phy,
	.memcpy_conf_log = &dma40_memcpy_conf_log,
	/* Audio is using physical channel 2 from MMDSP */
	.disabled_channels = {2, -1},
	.use_esram_lcla = true,
	/* Physical channels for which HW LLI should not be used */
	.soft_lli_chans = NULL,
	.num_of_soft_lli_chans = 0,
};

#ifdef CONFIG_DBX500_CONTEXT
#define D40_DREG_GCC		0x000
#define D40_DREG_LCPA		0x020
#define D40_DREG_LCLA		0x024

static void __iomem *base;

static int dma_context_notifier_call(struct notifier_block *this,
				     unsigned long event, void *data)
{
	static unsigned long lcpa;
	static unsigned long lcla;
	static unsigned long gcc;

	switch (event) {
	case CONTEXT_APE_SAVE:
		lcla = readl(base + D40_DREG_LCLA);
		lcpa = readl(base + D40_DREG_LCPA);
		gcc = readl(base + D40_DREG_GCC);
		break;

	case CONTEXT_APE_RESTORE:
		writel(gcc, base + D40_DREG_GCC);
		writel(lcpa, base + D40_DREG_LCPA);
		writel(lcla, base + D40_DREG_LCLA);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block dma_context_notifier = {
	.notifier_call = dma_context_notifier_call,
};

static void dma_context_notifier_init(void)
{
	if (cpu_is_u9540())
		base = ioremap(u9540_dma40_resources[0].start,
			resource_size(&u9540_dma40_resources[0]));
	else
		base = ioremap(u8500_dma40_resources[0].start,
			resource_size(&u8500_dma40_resources[0]));
	if (WARN_ON(!base))
		return;

	WARN_ON(context_ape_notifier_register(&dma_context_notifier));
}
#else
static void dma_context_notifier_init(void)
{
}
#endif

static struct platform_device u8500_dma40_device = {
	.dev = {
		.platform_data = &dma40_plat_data,
#ifdef CONFIG_PM
		.pwr_domain = &ux500_dev_power_domain,
#endif
	},
	.name		= "dma40",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(u8500_dma40_resources),
	.resource	= u8500_dma40_resources
};

static struct platform_device u9540_dma40_device = {
	.dev = {
		.platform_data = &dma40_plat_data,
#ifdef CONFIG_PM
		.pwr_domain = &ux500_dev_power_domain,
#endif
	},
	.name		= "dma40",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(u9540_dma40_resources),
	.resource	= u9540_dma40_resources
};

void __init db8500_dma_init(void)
{
	int ret;

	if (cpu_is_u9540()) {
		ret = platform_device_register(&u9540_dma40_device);
		if (ret)
			dev_err(&u9540_dma40_device.dev, "unable to register device: %d\n",
				ret);
	}
	else {
		ret = platform_device_register(&u8500_dma40_device);
		if (ret)
			dev_err(&u8500_dma40_device.dev, "unable to register device: %d\n",
				ret);
	}

	dma_context_notifier_init();
}
