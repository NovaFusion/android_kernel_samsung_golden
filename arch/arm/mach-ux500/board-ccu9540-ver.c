/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL), version 2
 *
 * Version specific initialization for U9540 CCU boards.
 */

#define pr_fmt(fmt)	"u9540-ccu: " fmt

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/sysfs.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include <mach/hardware.h>
#include <mach/devices.h>

#include "pins-db8500.h"
#include "pins.h"
#include "board-mop500.h"

static struct platform_device *ccu9540_hsi_us_v1_platform_devs[] __initdata = {
#ifdef CONFIG_HSI
	&u8500_hsi_device,
	&ap9540_modem_control_device,
#endif
};

static struct platform_device *ccu9540_c2c_us_v1_platform_devs[] __initdata = {
#ifdef CONFIG_C2C
	&ap9540_c2c_device,
	&ap9540_modem_control_device,
#endif
};

static const pin_cfg_t u9540_pins_hsi[] = {
	GPIO219_HSIR_FLA0 | PIN_INPUT_PULLDOWN,
	GPIO220_HSIR_DAT0 | PIN_INPUT_PULLDOWN,
	GPIO221_HSIR_RDY0 | PIN_OUTPUT_LOW,
	GPIO222_HSIT_FLA0 | PIN_OUTPUT_LOW,
};
static const pin_cfg_t u9540_pins_c2c[] = {
	GPIO219_MC3_CLK,
	GPIO220_MC3_FBCLK,
	GPIO221_MC3_CMD,
	GPIO222_MC3_DAT0,
};

static const pin_cfg_t u9540_c2c_us_V1_1_pins[] = {
	GPIO128_GPIO | PIN_INPUT_PULLDOWN,
	GPIO223_GPIO | PIN_INPUT_PULLUP,
	GPIO224_GPIO | PIN_INPUT_PULLUP,
};

static u8 rcf;

static void ccu9540_hsi_us_v1_init(void)
{
	nmk_config_pins((pin_cfg_t *)u9540_pins_hsi,
			ARRAY_SIZE(u9540_pins_hsi));

	platform_add_devices(ccu9540_hsi_us_v1_platform_devs,
		ARRAY_SIZE(ccu9540_hsi_us_v1_platform_devs));
}

#define U8500_CR_REG1			0x04
#define U8500_CR_REG1_MSPR4ACTIVE	(1<<15)

static void ccu9540_c2c_us_v1_init(void)
{
	void __iomem *crbase;

	nmk_config_pins((pin_cfg_t *)u9540_pins_c2c,
			ARRAY_SIZE(u9540_pins_c2c));

	/* config Pressure sensor interrupt gpios on X9540 C2C/US V1.1 Board, */
	if (ccu_is_u9540ccu_c2c_us_v1_1()) {
		nmk_config_pins((pin_cfg_t *)u9540_c2c_us_V1_1_pins,
				ARRAY_SIZE(u9540_c2c_us_V1_1_pins));
	}

	platform_add_devices(ccu9540_c2c_us_v1_platform_devs,
		ARRAY_SIZE(ccu9540_c2c_us_v1_platform_devs));

	/* set bit Msp4active to 1 (MSP4 selected) in the ConfigRegister 1 */
	crbase = ioremap(U8500_CR_BASE, PAGE_SIZE);
	if (!crbase) {
		pr_err("Failed to map U8500_CR_BASE\n");
		return;
	}
	writel(readl(crbase + U8500_CR_REG1) | U8500_CR_REG1_MSPR4ACTIVE,
		crbase + U8500_CR_REG1);
	iounmap(crbase);
}

enum ccu9540_ver {
	CCU9540_HSI_US_V1,
	CCU9540_C2C_US_V1,
	CCU9540_C2C_US_V1_1, /*Board ID for C2C V1.1 HW */
	CCU9540_HSI_US_V2,
	CCU9540_C2C_US_V2,
	CCU8550_US_V1,
};

struct ccu {
	const char *name;
	void (*init)(void);
};

static u8 type_of_ccu;

static struct ccu __initdata u9540_ccus[] = {
	[CCU9540_HSI_US_V1] = {
		.name	= "CCU9540-HSI-US-V1",
		.init	= ccu9540_hsi_us_v1_init,
	},
	[CCU9540_C2C_US_V1] = {
		.name	= "CCU9540-C2C-US-V1",
		.init	= ccu9540_c2c_us_v1_init,
	},
	[CCU9540_C2C_US_V1_1] = {
		.name	= "CCU9540-C2C-US-V1_1",
		.init	= ccu9540_c2c_us_v1_init,
	},
	[CCU8550_US_V1] = {
		.name	= "CCU8550-US-V1",
		.init	= ccu9540_c2c_us_v1_init,
	},
};

int ccu_is_u9540ccu_hsi(void)
{
	return ((type_of_ccu == CCU9540_HSI_US_V1) ||
		(type_of_ccu == CCU9540_HSI_US_V2));
}
int ccu_is_u9540ccu_hsi_us_v1(void)
{
	return (type_of_ccu == CCU9540_HSI_US_V1);
}

int ccu_is_u9540ccu_c2c(void)
{
	return ((type_of_ccu == CCU9540_C2C_US_V1) ||
		(type_of_ccu == CCU9540_C2C_US_V1_1) ||
		(type_of_ccu == CCU9540_C2C_US_V2) ||
		(type_of_ccu == CCU8550_US_V1));
}
int ccu_is_u9540ccu_c2c_us_v1(void)
{
	return (type_of_ccu == CCU9540_C2C_US_V1);
}

int ccu_is_u9540ccu_c2c_us_v1_1(void)
{
	return (type_of_ccu == CCU9540_C2C_US_V1_1);
}


#define U9540_CCU_REV_PIN0 MOP500_EGPIO(12)
#define U9540_CCU_REV_PIN1 MOP500_EGPIO(13)
#define U9540_CCU_REV_PIN2 MOP500_EGPIO(14)
#define U9540_CCU_REV_PIN3 MOP500_EGPIO(15)

static u8 __init u9540_ccu_revision(void)
{
	u8 revision = 0xFF;

	if (gpio_request(U9540_CCU_REV_PIN0, __func__))
		goto err;
	if (gpio_request(U9540_CCU_REV_PIN1, __func__))
		goto err0;
	if (gpio_request(U9540_CCU_REV_PIN2, __func__))
		goto err1;
	if (gpio_request(U9540_CCU_REV_PIN3, __func__))
		goto err2;

	if (gpio_direction_input(U9540_CCU_REV_PIN0) ||
		gpio_direction_input(U9540_CCU_REV_PIN1) ||
		gpio_direction_input(U9540_CCU_REV_PIN2) ||
		gpio_direction_input(U9540_CCU_REV_PIN3))
		goto err3;

	revision = (!!gpio_get_value_cansleep(U9540_CCU_REV_PIN0)) |
			(!!gpio_get_value_cansleep(U9540_CCU_REV_PIN1) << 1) |
			(!!gpio_get_value_cansleep(U9540_CCU_REV_PIN2) << 2) |
			(!!gpio_get_value_cansleep(U9540_CCU_REV_PIN3) << 3);
err3:
	gpio_free(U9540_CCU_REV_PIN3);
err2:
	gpio_free(U9540_CCU_REV_PIN2);
err1:
	gpio_free(U9540_CCU_REV_PIN1);
err0:
	gpio_free(U9540_CCU_REV_PIN0);
err:
	if (revision == 0xFF)
		pr_err("%s: failed to access/config GPIO\n", __func__);
	return revision;
}

/* attrib for export through sysfs */

static ssize_t show_hw_detect_hsi(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ccu_is_u9540ccu_hsi());
}

static ssize_t show_hw_detect_c2c(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ccu_is_u9540ccu_c2c());
}

static ssize_t show_hw_detect_rcfilter(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rcf);
}


static DEVICE_ATTR(config_hsi, S_IRUGO, show_hw_detect_hsi, NULL);
static DEVICE_ATTR(config_c2c, S_IRUGO, show_hw_detect_c2c, NULL);
static DEVICE_ATTR(audio_half_slim, S_IRUGO, show_hw_detect_hsi, NULL);
static DEVICE_ATTR(audio_fat, S_IRUGO, show_hw_detect_c2c, NULL);
static DEVICE_ATTR(rcfilter, S_IRUGO, show_hw_detect_rcfilter, NULL);

static const struct attribute *ccuver_attr[] = {
	&dev_attr_config_hsi.attr,
	&dev_attr_config_c2c.attr,
	&dev_attr_audio_half_slim.attr,
	&dev_attr_audio_fat.attr,
	&dev_attr_rcfilter.attr,
	NULL,
};

static const struct attribute_group ccuver_attr_group = {
	.attrs = (struct attribute **)ccuver_attr,
};

static struct platform_device *pdev;

static int __init u9540_ccu_init(void)
{
	int ret = 0;
	u8 rev, id = 0;

	if (!cpu_is_u9540())
		return -ENODEV;

	rev = u9540_ccu_revision();
	switch (rev) {
	case 0x00:
		id = CCU9540_HSI_US_V1;
		break;
	case 0x02:
		id = CCU9540_C2C_US_V1;
		break;
	case 0x06:
		id = CCU9540_C2C_US_V1_1;
		rcf = 1;
		break;
	case 0x0a:
		id = CCU8550_US_V1;
		rcf = 1;
		break;
	case 0xFF:
		pr_err("failed to read CCU revision\n");
		ret = -EIO;
		break;
	default:
		pr_err("u9540 CCU ID 0x%02x not supported!\n", rev);
		ret = -EIO;
		break;
	}

	if (ret == 0) {
		pr_info("%s (identified)\n", u9540_ccus[id].name);
		type_of_ccu = id;
		if (u9540_ccus[id].init)
			u9540_ccus[id].init();
	}

	pdev = platform_device_register_simple("modem-hwcfg", -1, NULL, 0);
	return sysfs_create_group(&pdev->dev.kobj, &ccuver_attr_group);
}
module_init(u9540_ccu_init);
