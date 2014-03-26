/*
 * Copyright (C) Samsung 2012
 *
 * License Terms: GNU General Public License v2
 * Authors: Robert Teather <robert.teather@samsung.com> for Samsung Electronics
 *
 * Board specific file for SD interface initialization
 *
 */


#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <plat/ste_dma40.h>
#include <mach/devices.h>
#include <mach/hardware.h>
#include <mach/ste-dma40-db8500.h>

#include "devices-db8500.h"
#include <mach/board-sec-u8500.h>

#include "prcc.h"
#include "cpu-db8500.h"

#include <linux/skbuff.h>
#include <linux/wlan_plat.h>

#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM

#define WLAN_STATIC_SCAN_BUF0		5
#define WLAN_STATIC_SCAN_BUF1		6
#define PREALLOC_WLAN_SEC_NUM		4
#define PREALLOC_WLAN_BUF_NUM		160
#define PREALLOC_WLAN_SECTION_HEADER	24

#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2	(PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_BUF_NUM * 1024)

#define DHD_SKB_HDRSIZE			336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

#define WLAN_SKB_BUF_NUM	17

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wlan_mem_prealloc {
	void *mem_ptr;
	unsigned long size;
};

static struct wlan_mem_prealloc wlan_mem_array[PREALLOC_WLAN_SEC_NUM] = {
	{NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

void *wlan_static_scan_buf0;
void *wlan_static_scan_buf1;
static void *brcm_wlan_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_SEC_NUM)
		return wlan_static_skb;
	if (section == WLAN_STATIC_SCAN_BUF0)
		return wlan_static_scan_buf0;
	if (section == WLAN_STATIC_SCAN_BUF1)
		return wlan_static_scan_buf1;
	if ((section < 0) || (section > PREALLOC_WLAN_SEC_NUM))
		return NULL;

	if (wlan_mem_array[section].size < size)
		return NULL;

	return wlan_mem_array[section].mem_ptr;
}

static int brcm_init_wlan_mem(void)
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
		wlan_mem_array[i].mem_ptr =
				kmalloc(wlan_mem_array[i].size, GFP_KERNEL);

		if (!wlan_mem_array[i].mem_ptr)
			goto err_mem_alloc;
	}
	wlan_static_scan_buf0 = kmalloc (65536, GFP_KERNEL);
	if(!wlan_static_scan_buf0)
		goto err_mem_alloc;
	wlan_static_scan_buf1 = kmalloc (65536, GFP_KERNEL);
	if(!wlan_static_scan_buf1)
		goto err_mem_alloc;

	printk("%s: WIFI MEM Allocated\n", __FUNCTION__);
	return 0;

 err_mem_alloc:
	pr_err("Failed to mem_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		kfree(wlan_mem_array[j].mem_ptr);

	i = WLAN_SKB_BUF_NUM;

 err_skb_alloc:
	pr_err("Failed to skb_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		dev_kfree_skb(wlan_static_skb[j]);

	return -ENOMEM;
}
#endif /* CONFIG_BROADCOM_WIFI_RESERVED_MEM */


/* Reset pl18x controllers */
void ux500_sdi_reset(struct device *dev)
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
		gpio_direction_output(KYLE_GPIO_TXS0206_EN, 1);
		udelay(100);
		break;
	case MMC_POWER_OFF:
		/* Disable level shifter */
		gpio_direction_output(KYLE_GPIO_TXS0206_EN, 0);
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
	.capabilities2	= MMC_CAP2_DETECT_ON_ERR,
	.gpio_cd	= KYLE_GPIO_T_FLASH_DETECT,
	.gpio_wp	= -1,
	.cd_invert	= true,
	.sigdir		= MCI_ST_FBCLKEN,
	.reset		= ux500_sdi_reset,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi0_dma_cfg_rx,
	.dma_tx_param	= &sdi0_dma_cfg_tx,
#endif
};

static void __init sdi0_configure(void)
{
	int ret;

	ret = gpio_request(KYLE_GPIO_TXS0206_EN, "SD Card LS EN");
	if (ret) {
		printk(KERN_WARNING "unable to config gpios for level shifter.\n");
		return;
	}

	/* Enable level shifter */
	gpio_direction_output(KYLE_GPIO_TXS0206_EN, 0);
}

/*
 * SDI1 (SDIO WLAN)
 */
static bool sdi1_card_power_on;
static int kyle_wifi_cd; /* WIFI virtual 'card detect' status */
static unsigned int sdi1_card_status(struct device *dev)
{
#if 0		//hyeok-test
	if (sdi1_card_power_on)
		return 1;
	else
		return 0;
#else
	return kyle_wifi_cd;
#endif
 }
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int kyle_wifi_status_register(
		                void (*callback)(int card_present, void *dev_id),
				                void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;

	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;

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
				MMC_CAP_SD_HIGHSPEED /*|*/
				/* MMC_CAP_SDIO_IRQ | */
				/* MMC_CAP_NONREMOVABLE*/,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
//	.pm_flags	= MMC_PM_KEEP_POWER,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	.reset		= ux500_sdi_reset,
	.status = sdi1_card_status,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi1_dma_cfg_rx,
	.dma_tx_param	= &sdi1_dma_cfg_tx,
#endif
	.register_status_notify = kyle_wifi_status_register,
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
	gpio_set_value(KYLE_GPIO_MEM_LDO_EN, 0);
   } else {
	printk(KERN_ERR "[MMC] TURN ON EXTERNAL LDO\n");
	/* Enable external LDO */
	gpio_set_value(KYLE_GPIO_MEM_LDO_EN, 1);
   }
}

static struct mmci_platform_data ssg_sdi2_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED|
				MMC_CAP_ERASE,
	.capabilities2	= MMC_CAP2_NO_SLEEP_CMD,
//	.pm_flags	= MMC_PM_KEEP_POWER,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
	.reset		= ux500_sdi_reset,
//	.suspend_resume_handler	= suspend_resume_handler_sdi2,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &sdi2_dma_cfg_rx,
	.dma_tx_param	= &sdi2_dma_cfg_tx,
#endif
};


/* BCM */
static int wifi_gpio_reset	= KYLE_GPIO_WLAN_RST_N;
static int wifi_gpio_irq	= KYLE_GPIO_WL_HOST_WAKE;

static int brcm_wlan_power(int onoff)
{
	sdi1_card_power_on = (onoff == 0) ? false : true;

	printk("------------------------------------------------");
	printk("------------------------------------------------\n");
	printk("%s Enter: power %s\n", __FUNCTION__, onoff ? "on" : "off");
	pr_info("111%s Enter: power %s\n", __FUNCTION__, onoff ? "on" : "off");
	if (onoff) {
		gpio_set_value(wifi_gpio_reset, 1);
		printk(KERN_DEBUG "WLAN: GPIO_WLAN_EN = %d \n"
				, gpio_get_value(wifi_gpio_reset));
	} else {
		gpio_set_value(wifi_gpio_reset, 0);
		printk(KERN_DEBUG "WLAN: GPIO_WLAN_EN = %d \n"
				, gpio_get_value(wifi_gpio_reset));
	}

	return 0;
}

static int brcm_wlan_reset(int onoff)
{
	gpio_set_value(wifi_gpio_reset,	onoff );
	return 0;
}
static int brcm_set_carddetect(int val)
{
	pr_debug("%s: %d\n", __func__, val);
	kyle_wifi_cd = val;

	if (wifi_status_cb)
		wifi_status_cb(val, wifi_status_cb_devid);
	else
		pr_warning("%s: Nobody to notify\n", __func__);

	return 0;
}

/* Customized Locale table : OPTIONAL feature */
#define WLC_CNTRY_BUF_SZ        4
typedef struct cntry_locales_custom {
	char iso_abbrev[WLC_CNTRY_BUF_SZ];
	char custom_locale[WLC_CNTRY_BUF_SZ];
	int  custom_locale_rev;
} cntry_locales_custom_t;

static cntry_locales_custom_t brcm_wlan_translate_custom_table[] = {
	/* Table should be filled out based on custom platform regulatory requirement */
	{"",   "XZ", 11},  /* Universal if Country code is unknown or empty */
	{"AE", "AE", 1},
	{"AR", "AR", 1},
	{"AT", "AT", 1},
	{"AU", "AU", 2},
	{"BE", "BE", 1},
	{"BG", "BG", 1},
	{"BN", "BN", 1},
	{"CA", "CA", 2},
	{"CH", "CH", 1},
	{"CY", "CY", 1},
	{"CZ", "CZ", 1},
	{"DE", "DE", 3},
	{"DK", "DK", 1},
	{"EE", "EE", 1},
	{"ES", "ES", 1},
	{"FI", "FI", 1},
	{"FR", "FR", 1},
	{"GB", "GB", 1},
	{"GR", "GR", 1},
	{"HR", "HR", 1},
	{"HU", "HU", 1},
	{"IE", "IE", 1},
	{"IS", "IS", 1},
	{"IT", "IT", 1},
	{"JP", "JP", 3},
	{"KR", "KR", 24},
	{"KW", "KW", 1},
	{"LI", "LI", 1},
	{"LT", "LT", 1},
	{"LU", "LU", 1},
	{"LV", "LV", 1},
	{"MA", "MA", 1},
	{"MT", "MT", 1},
	{"MX", "MX", 1},
	{"NL", "NL", 1},
	{"NO", "NO", 1},
	{"PL", "PL", 1},
	{"PT", "PT", 1},
	{"PY", "PY", 1},
	{"RO", "RO", 1},
	{"RU", "RU", 5},
	{"SE", "SE", 1},
	{"SG", "SG", 4},
	{"SI", "SI", 1},
	{"SK", "SK", 1},
	{"TR", "TR", 7},
	{"TW", "TW", 2},
	{"US", "US", 46}
};

static void *brcm_wlan_get_country_code(char *ccode)
{
	int size = ARRAY_SIZE(brcm_wlan_translate_custom_table);
	int i;

	if (!ccode)
		return NULL;

	for (i = 0; i < size; i++)
		if (strcmp(ccode, brcm_wlan_translate_custom_table[i].iso_abbrev) == 0)
			return &brcm_wlan_translate_custom_table[i];
	return &brcm_wlan_translate_custom_table[0];
}

static struct resource brcm_wlan_resources[] = {
	[0] = {
		.name	= "bcmdhd_wlan_irq",
		.start	= GPIO_TO_IRQ(KYLE_GPIO_WL_HOST_WAKE),
		.end	= GPIO_TO_IRQ(KYLE_GPIO_WL_HOST_WAKE),
//chanyun 12.21		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_FALLING /*IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE*/,
	},
};

static struct wifi_platform_data brcm_wlan_control = {
	.set_power	= brcm_wlan_power,
	.set_reset	= brcm_wlan_reset,
	.set_carddetect	= brcm_set_carddetect,
#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM
	.mem_prealloc	= brcm_wlan_mem_prealloc,
#endif
	.get_country_code = brcm_wlan_get_country_code,
};

static struct platform_device brcm_device_wlan = {
	.name		= "bcmdhd_wlan",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(brcm_wlan_resources),
	.resource	= brcm_wlan_resources,
	.dev		= {
		.platform_data = &brcm_wlan_control,
	},
};

static void kyle_wifi_init(void)
{
	int32_t status = 0;

	/* Enable the WLAN GPIO */
	status = gpio_request(wifi_gpio_reset, "wlan_power");
	if (status) {
		printk(KERN_INFO "INIT : Unable to request GPIO_WLAN_ENABLE\n");
		return;
	}

	gpio_direction_output(wifi_gpio_reset, 0);

	if (gpio_request(wifi_gpio_irq, "bcmsdh_sdmmc")) {
		printk(KERN_INFO "Unable to request WLAN_IRQ\n");
		return;
	}

	if (gpio_direction_input(wifi_gpio_irq)) {
		printk(KERN_INFO "Unable to set directtion on WLAN_IRQ\n");
		return;
	}
	return;
}

static void kyle_sdi2_init(void)
{
	int32_t status = 0;

	/* Enable the eMMC_EN GPIO */
	status = gpio_request(KYLE_GPIO_MEM_LDO_EN, "eMMC_EN");

	gpio_direction_output(KYLE_GPIO_MEM_LDO_EN, 1);
	gpio_set_value(KYLE_GPIO_MEM_LDO_EN, 1);

	return;
}


/* BCM code uses a fixed name */
int u8500_wifi_power(int on, int flag)
{
	printk(KERN_INFO "%s: WLAN Power %s, flag %d\n",
		__func__, on ? "on" : "down", flag);
	if (flag != 1) {
		gpio_set_value(wifi_gpio_reset, on);
		if (on)
			udelay(200);
		sdi1_card_power_on = (on == 0) ? false : true;
		return 0;
	}

	sdi1_card_power_on = (on == 0) ? false : true;

	if (on) {
		gpio_set_value(wifi_gpio_reset, 1);
		mdelay(250);
//		u8500_sdio_detect_card();
		udelay(2000);
	} else {
		gpio_set_value(wifi_gpio_reset, 0);
	}

	return 0;
}

static int __init ssg_sdi_init(void)
{
//	ssg_sdi2_data.card_sleep_on_suspend = true;
	int ret;

	/* v2 has a new version of this block that need to be forced */
	u32 periphid = 0x10480180;

	db8500_add_sdi2(&ssg_sdi2_data, periphid);
	kyle_sdi2_init();

	if ((sec_debug_settings & SEC_DBG_STM_VIA_SD_OPTS) == 0) {
		/* not tracing via SDI0 pins, so can enable SDI0 */
		sdi0_configure();
		db8500_add_sdi0(&ssg_sdi0_data, periphid);
	}

	db8500_add_sdi1(&ssg_sdi1_data, periphid);

	/* BCM */
	kyle_wifi_init();

#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM
	brcm_init_wlan_mem();
#endif
	ret =  platform_device_register(&brcm_device_wlan);
	printk("-----------------------------------------------------\n");
	printk("-----------------------------------------------------\n");
	printk("-----------------------------------------------------\n");
	printk("regist ret:%d\n", ret);

	return 0;
}


fs_initcall(ssg_sdi_init);

/*BCM*/
EXPORT_SYMBOL(u8500_wifi_power);

