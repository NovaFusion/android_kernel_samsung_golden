/*
 * Copyright (C) 2010 ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <plat/ste_dma40.h>
#include <plat/pincfg.h>
#include <mach/devices.h>
#include <mach/gpio.h>
#include <mach/ste-dma40-db8500.h>

#include "devices-db8500.h"
#include "pins-db8500.h"
#include <mach/board-sec-u8500.h>

#define WLAN_STATIC_BUF  1
#ifdef WLAN_STATIC_BUF
#include <linux/skbuff.h>
#endif


/*
 * SDI0 (SD/MMC card)
 */
#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg sdi0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB8500_DMA_DEV1_SD_MMC0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
	.use_fixed_channel = true,
	.phy_channel = 0,
};

static struct stedma40_chan_cfg sdi0_dma_cfg_tx = {
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

static int sdi0_ios_handler(struct device *dev, struct mmc_ios *ios,  enum rpm_status pm)
{
	static int power_mode = -1;

	if (power_mode == ios->power_mode)
		return 0;

	switch (ios->power_mode) {
	case MMC_POWER_UP:
	case MMC_POWER_ON:
		/* Enable level shifter */
		gpio_direction_output(TXS0206_EN_GAVINI_R0_0, 1);
		udelay(100);
		break;
	case MMC_POWER_OFF:
		/* Disable level shifter */
		gpio_direction_output(TXS0206_EN_GAVINI_R0_0, 0);
		break;
	}
	power_mode = ios->power_mode;
	return 0;
}

static struct mmci_platform_data ssg_sdi0_data = {
	.ios_handler	= sdi0_ios_handler,
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_MMC_HIGHSPEED |
				MMC_CAP_UHS_SDR12 |
				MMC_CAP_UHS_SDR25,
	.gpio_cd	= T_FLASH_DETECT_GAVINI_R0_0,
	.gpio_wp	= -1,
	.cd_invert	= true,
	.sigdir		= MCI_ST_FBCLKEN,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi0_dma_cfg_rx,
	.dma_tx_param	= &sdi0_dma_cfg_tx,
#endif
};

static void __init sdi0_configure(void)
{
	int ret;

	ret = gpio_request(TXS0206_EN_GAVINI_R0_0, "SD Card LS EN");
	if (ret) {
		printk(KERN_WARNING "unable to config gpios for level shifter.\n");
		return;
	}

	/* Enable level shifter */
	gpio_direction_output(TXS0206_EN_GAVINI_R0_0, 0);
}

/*
 * SDI1 (SDIO WLAN)
 */
static bool sdi1_card_power_on = false;
 
static unsigned int sdi1_card_status(struct device *dev)
{
	if (sdi1_card_power_on) 
		return 1;
	else
		return 0;
 }

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

static struct mmci_platform_data ssg_sdi1_data = {
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SD_HIGHSPEED |
				/* MMC_CAP_SDIO_IRQ | */
				MMC_CAP_NONREMOVABLE,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
	/* .pm_flags	= MMC_PM_KEEP_POWER, */
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	.status = sdi1_card_status,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi1_dma_cfg_rx,
	.dma_tx_param	= &sdi1_dma_cfg_tx,
#endif
};


/*
 * SDI2 (POPed eMMC)
 */
#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg sdi2_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB8500_DMA_DEV28_SD_MM2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
static struct stedma40_chan_cfg sdi2_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV28_SD_MM2_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif


static void suspend_resume_handler_sdi2(struct mmc_host *host, bool suspend)
{
   if (suspend) {
	printk(KERN_ERR "[MMC] TURN OFF EXTERNAL LDO\n");
	gpio_set_value(MEM_LDO_EN_GAVINI_R0_0, 0);
   } else {
	printk(KERN_ERR "[MMC] TURN ON EXTERNAL LDO\n");
	/* Enable external LDO */
	gpio_set_value(MEM_LDO_EN_GAVINI_R0_0, 1);
   }
}

static struct mmci_platform_data ssg_sdi2_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED |
				MMC_CAP_ERASE,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
	/* .pm_flags	= MMC_PM_KEEP_POWER, */
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	/* .suspend_resume_handler	= suspend_resume_handler_sdi2, */
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi2_dma_cfg_rx,
	.dma_tx_param	= &sdi2_dma_cfg_tx,
#endif
};


/* BCM */
static int wifi_gpio_reset	= WLAN_RST_N_GAVINI_R0_0;
static int wifi_gpio_irq	= WL_HOST_WAKE_GAVINI_R0_0;

static void gavini_wifi_init(void)
{
	int32_t status = 0;

	/* Enable the WLAN GPIO */
	status = gpio_request(wifi_gpio_reset, "wlan_power");
	if (status)
	{
		printk(KERN_INFO "INIT : Unable to request GPIO_WLAN_ENABLE\n");
		return;
	}

	gpio_direction_output(wifi_gpio_reset, 0);

	if(gpio_request(wifi_gpio_irq, "wlan_irq"))
	{
		printk(KERN_INFO "Unable to request WLAN_IRQ\n");
		return;
	}

	if(gpio_direction_input(wifi_gpio_irq))
	{
		printk(KERN_INFO "Unable to set directtion on WLAN_IRQ\n");
		return;
	} 
	return;
}

static void gavini_sdi2_init(void)
{
	int32_t status = 0;

	/* Enable the eMMC_EN GPIO */
	status = gpio_request(MEM_LDO_EN_GTI9060_R0_1, "eMMC_EN");

	gpio_direction_output(MEM_LDO_EN_GTI9060_R0_1, 1);
	gpio_set_value(MEM_LDO_EN_GTI9060_R0_1, 1);

	return;
}


/* BCM code uses a fixed name */
extern void u8500_sdio_detect_card(void);

int u8500_wifi_power(int on, int flag)
{
	printk(KERN_INFO "%s: WLAN Power %s, flag %d\n",
		__FUNCTION__, on ? "on" : "down", flag);
	if (flag != 1) {
		gpio_set_value(wifi_gpio_reset, on);
		if (on)
			udelay(200);
		sdi1_card_power_on = (on==0) ? false : true;
		return 0;
	}

	sdi1_card_power_on = (on == 0) ? false : true;

	if (on) {
		gpio_set_value(wifi_gpio_reset, 1);
		mdelay(250);
		/* u8500_sdio_detect_card(); */
		udelay(2000);
	} else {
		gpio_set_value(wifi_gpio_reset, 0);
	}
	
	return 0;
}

#ifdef WLAN_STATIC_BUF

#define PREALLOC_WLAN_SEC_NUM		4
#define PREALLOC_WLAN_BUF_NUM		160
#define PREALLOC_WLAN_SECTION_HEADER	24


#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2	(PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_BUF_NUM * 1024)

#define WLAN_SKB_BUF_NUM	17

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wifi_mem_prealloc {
	void *mem_ptr;
	unsigned long size;
};

static struct wifi_mem_prealloc wifi_mem_array[PREALLOC_WLAN_SEC_NUM] = {
	{NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

void *wlan_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_SEC_NUM)
		return wlan_static_skb;

	if ((section < 0) || (section > PREALLOC_WLAN_SEC_NUM))
		return NULL;

	if (wifi_mem_array[section].size < size)
		return NULL;

	return wifi_mem_array[section].mem_ptr;
}
EXPORT_SYMBOL(wlan_mem_prealloc);

#define DHD_SKB_HDRSIZE			336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

static int init_wifi_mem(void)
{
	int i;
	int j;

	for (i = 0; i < 8; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_1PAGE_BUFSIZE);
		if (!wlan_static_skb[i])
			goto err_skb_alloc;
	}

	for (; i < 16; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_2PAGE_BUFSIZE);
		if (!wlan_static_skb[i])
			goto err_skb_alloc;
	}

	wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_4PAGE_BUFSIZE);
	if (!wlan_static_skb[i])
		goto err_skb_alloc;

	for (i = 0 ; i < PREALLOC_WLAN_SEC_NUM ; i++) {
		wifi_mem_array[i].mem_ptr =
				kmalloc(wifi_mem_array[i].size, GFP_KERNEL);

		if (!wifi_mem_array[i].mem_ptr)
			goto err_mem_alloc;
	}
	printk(KERN_INFO "%s: WIFI MEM Allocated\n", __func__);
	return 0;

 err_mem_alloc:
	pr_err("Failed to mem_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		kfree(wifi_mem_array[j].mem_ptr);

	i = WLAN_SKB_BUF_NUM;

 err_skb_alloc:
	pr_err("Failed to skb_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		dev_kfree_skb(wlan_static_skb[j]);

	return -ENOMEM;
}

/*module_init(init_wifi_mem);*/


#endif /* WLAN_STATIC_BUF */


static int __init ssg_sdi_init(void)
{
	/* v2 has a new version of this block that need to be forced */
	u32 periphid = 0x10480180;

	/* ssg_sdi2_data.card_sleep_on_suspend = true; */
	db8500_add_sdi2(&ssg_sdi2_data, periphid);
	gavini_sdi2_init();

	if ((sec_debug_settings & SEC_DBG_STM_VIA_SD_OPTS) == 0) {
		/* not tracing via SDI0 pins, so can enable SDI0 */
		sdi0_configure();
		db8500_add_sdi0(&ssg_sdi0_data, periphid);
	}

	db8500_add_sdi1(&ssg_sdi1_data, periphid);

	/* BCM */
	gavini_wifi_init();
#ifdef WLAN_STATIC_BUF
	init_wifi_mem();
#endif /* WLAN_STATIC_BUF */

	return 0;
}


fs_initcall(ssg_sdi_init);

/*BCM*/
EXPORT_SYMBOL (u8500_wifi_power);

