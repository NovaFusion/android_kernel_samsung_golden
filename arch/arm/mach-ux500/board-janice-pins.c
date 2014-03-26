/*
 * Copyright (C) 2011 Samsung Electronics
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/string.h>

#include <plat/pincfg.h>
#include <plat/gpio-nomadik.h>
#include <linux/mfd/abx500/ab8500-gpio.h>

#include <mach/hardware.h>
#include <mach/suspend.h>

#include "pins-db8500.h"
#include "pins.h"

#include "board-pins-sleep-force.h"

#include <linux/workqueue.h>
#include <mach/board-sec-ux500.h>
#include "board-janice-regulators.h"

#include <asm/io.h>

extern unsigned int system_rev;

/*
* Configuration of pins, pull resisitors states
*/
static pin_cfg_t janice_r0_0_pins[] = {
	/* GBF UART */
	/* uart-0 pins gpio configuration should be
	 * kept intact to prevent glitch in tx line
	 * when tty dev is opened. Later these pins
	 * are configured to uart janice_r0_0_pins_uart0
	 *
	 * It will be replaced with uart configuration
	 * once the issue is solved.
	 */
	GPIO0_GPIO		| PIN_INPUT_PULLUP,
	GPIO1_GPIO		| PIN_OUTPUT_HIGH,
	GPIO2_GPIO		| PIN_INPUT_PULLUP,
	GPIO3_GPIO		| PIN_OUTPUT_LOW,

	GPIO6_GPIO		| PIN_OUTPUT_HIGH,	/* MEM_LDO_EN */

	GPIO7_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */

	GPIO8_I2C2_SDA,	/* CAM I2C */
	GPIO9_I2C2_SCL,	/* CAM I2C */

	/* MSP0 (BT) */
	GPIO12_MSP0_TXD,
	GPIO13_MSP0_TFS,
	GPIO14_MSP0_TCK,
	GPIO15_MSP0_RXD,

	/* Debug/Console UART */
	GPIO29_U2_RXD	| PIN_INPUT_PULLUP,
	GPIO30_U2_TXD	| PIN_OUTPUT_HIGH,

	GPIO31_GPIO		| PIN_INPUT_NOPULL,	/* NFC_FIRMWARE */
	GPIO32_GPIO		| PIN_INPUT_NOPULL,	/* NFC_IRQ */

	/* MSP AB8500 */
	GPIO33_MSP1_TXD,
	GPIO34_MSP1_TFS,
	GPIO35_MSP1_TCK,
	GPIO36_MSP1_RXD,

	GPIO64_GPIO		| PIN_OUTPUT_LOW,	/* VT_CAM_STBY */
	GPIO65_GPIO		| PIN_OUTPUT_LOW,	/* RST_VT_CAM */
	GPIO66_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO67_GPIO		| PIN_INPUT_PULLUP,	/* VOL_UP */
	GPIO68_GPIO		| PIN_OUTPUT_HIGH,	/* EN_LED_LDO */
	GPIO69_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */

	GPIO86_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO87_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO88_GPIO		| PIN_OUTPUT_LOW,	/* NFC_EN */
	GPIO89_GPIO		| PIN_OUTPUT_LOW,	/* TSP_LDO_ON2 */
	GPIO90_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */

	GPIO91_GPIO		| PIN_INPUT_PULLUP,	/* HOME_KEY */
	GPIO92_GPIO		| PIN_INPUT_PULLUP,	/* VOL_DOWN */
	GPIO93_GPIO		| PIN_INPUT_NOPULL,	/* OLED_DETECT_BB */
	GPIO94_GPIO		| PIN_OUTPUT_HIGH,	/* TSP_LDO_ON1 */
	GPIO95_GPIO		| PIN_INPUT_PULLUP,	/* JACK_nINT */
	GPIO96_GPIO		| PIN_OUTPUT_LOW,
	GPIO97_GPIO		| PIN_INPUT_PULLDOWN,	/* BT_HOST_WAKE */

	/* MMC2 (eMMC) */
	GPIO128_MC2_CLK		| PIN_OUTPUT_LOW,
	GPIO129_MC2_CMD		| PIN_INPUT_PULLUP,
	GPIO130_MC2_FBCLK	| PIN_INPUT_NOPULL,
	GPIO131_MC2_DAT0	| PIN_INPUT_PULLUP,
	GPIO132_MC2_DAT1	| PIN_INPUT_PULLUP,
	GPIO133_MC2_DAT2	| PIN_INPUT_PULLUP,
	GPIO134_MC2_DAT3	| PIN_INPUT_PULLUP,
	GPIO135_MC2_DAT4	| PIN_INPUT_PULLUP,
	GPIO136_MC2_DAT5	| PIN_INPUT_PULLUP,
	GPIO137_MC2_DAT6	| PIN_INPUT_PULLUP,
	GPIO138_MC2_DAT7	| PIN_INPUT_PULLUP,

	GPIO139_GPIO		| PIN_OUTPUT_HIGH,	/* LCD_RESX */
	GPIO140_GPIO		| PIN_OUTPUT_LOW,	/* CAM_FLASH_EN */
	GPIO141_GPIO		| PIN_OUTPUT_LOW,	/* CAM_FLASH_MODE */
	GPIO142_GPIO		| PIN_OUTPUT_LOW,	/* 5M_CAM_STBY */
	GPIO143_GPIO		| PIN_INPUT_NOPULL,	/* SUBPMU_SCL GPIO I2C (also compass MMC328) */
	GPIO144_GPIO		| PIN_INPUT_NOPULL,	/* SUBPMU_SDA GPIO I2C (also compass MMC328) */
	GPIO145_GPIO		| PIN_OUTPUT_LOW,	/* SUBPMU_PWRON */

	GPIO146_GPIO		| PIN_INPUT_NOPULL,	/* PS_VOUT */

	GPIO149_GPIO		| PIN_OUTPUT_LOW,	/* RST_5M_CAM */

	GPIO151_GPIO		| PIN_INPUT_NOPULL,	/* NFC_SCL GPIO I2C */
	GPIO152_GPIO		| PIN_INPUT_NOPULL,	/* NFC_SDA GPIO I2C */

	GPIO192_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO193_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO194_GPIO		| PIN_OUTPUT_LOW,	/* MOT_HEN */
	GPIO195_GPIO		| PIN_OUTPUT_LOW,	/* MOT_LEN */
	GPIO196_GPIO		| PIN_INPUT_NOPULL,	/* TOUCHKEY_SCL GPIO I2C */
	GPIO197_GPIO		| PIN_INPUT_NOPULL,	/* TOUCHKEY_SDA GPIO I2C */
	GPIO198_GPIO		| PIN_INPUT_NOPULL,	/* TOUCHKEY_INT */
	GPIO199_GPIO		| PIN_OUTPUT_HIGH,	/* BT_WAKE */
	GPIO200_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */

	GPIO201_GPIO		| PIN_INPUT_NOPULL,	/* MOT_SCL GPIO I2C */
	GPIO202_GPIO		| PIN_INPUT_NOPULL,	/* MOT_SDA GPIO I2C */
	GPIO203_GPIO		| PIN_INPUT_PULLUP,	/* SMD pogo pin */
	GPIO204_GPIO		| PIN_OUTPUT_HIGH,	/* COMPASS_RST */
	GPIO205_GPIO		| PIN_OUTPUT_LOW,	/* SPK_AMP_CTRL */
	GPIO206_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO207_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO209_GPIO		| PIN_OUTPUT_LOW,	/* BT_RST_N */

	/* SDI1 (SDIO) WLAN */
	GPIO208_MC1_CLK		| PIN_OUTPUT_LOW,
	GPIO210_MC1_CMD		| PIN_INPUT_PULLUP,
	GPIO211_MC1_DAT0	| PIN_INPUT_PULLUP,
	GPIO212_MC1_DAT1	| PIN_INPUT_PULLUP,
	GPIO213_MC1_DAT2	| PIN_INPUT_PULLUP,
	GPIO214_MC1_DAT3	| PIN_INPUT_PULLUP,

	GPIO215_GPIO		| PIN_OUTPUT_LOW,	/* WLAN_RST_N */
	GPIO216_GPIO		| PIN_INPUT_PULLDOWN,	/* WL_HOST_WAKE */
	GPIO217_GPIO		| PIN_INPUT_NOPULL,	/* T_FLASH_DETECT */
	GPIO218_GPIO		| PIN_INPUT_NOPULL,	/* TSP_INT_1.8V */
	GPIO219_GPIO		| PIN_OUTPUT_HIGH,	/* LCD_PWR_EN */

	GPIO221_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO222_GPIO		| PIN_OUTPUT_LOW,	/* BT_VREG_EN */
#ifdef CONFIG_SPI_GPIO
	/* DPI LCD SPI I/F */
	GPIO220_GPIO		| PIN_OUTPUT_HIGH,	/* GPIO220_SPI0_CLK */
	GPIO223_GPIO		| PIN_OUTPUT_HIGH,
	GPIO224_GPIO		| PIN_OUTPUT_HIGH,	/* GPIO224_SPI0_TXD */
#else
	/* DPI LCD SPI I/F */
	GPIO220_SPI0_CLK,
	GPIO223_GPIO		| PIN_OUTPUT_HIGH,
	GPIO224_SPI0_TXD,
#endif
	GPIO225_GPIO		| PIN_INPUT_PULLDOWN,	/* NC */
	GPIO226_GPIO		| PIN_INPUT_NOPULL,	/* SENSOR_INT */
	GPIO227_GPIO		| PIN_OUTPUT_LOW,	/* CAM_MCLK */
	GPIO228_GPIO		| PIN_OUTPUT_LOW,	/* MOT_PWM(CLK) */

};

/* STM trace or SD Card pin configurations */
static pin_cfg_t janice_r0_0_ape_trace[] = {
	GPIO18_GPIO | PIN_OUTPUT_LOW,	/* CMD.Dir		*/
	GPIO19_GPIO | PIN_OUTPUT_HIGH,	/* DAT0.Dir	*/
	GPIO20_GPIO | PIN_OUTPUT_HIGH,	/* DAT123.Dir */
	GPIO22_GPIO | PIN_INPUT_NOPULL,	/* CLK-f */

	PIN_CFG(23, ALT_C),	/* APE CLK */
	PIN_CFG(25, ALT_C),	/* APE DAT0 */
	PIN_CFG(26, ALT_C),	/* APE DAT1 */
	PIN_CFG(27, ALT_C),	/* APE DAT2 */
	PIN_CFG(28, ALT_C),	/* APE DAT3 */
};

static pin_cfg_t janice_r0_0_modem_trace[] = {
	GPIO18_GPIO | PIN_OUTPUT_LOW,	/* CMD.Dir		*/
	GPIO19_GPIO | PIN_OUTPUT_HIGH,	/* DAT0.Dir	*/
	GPIO20_GPIO | PIN_OUTPUT_HIGH,	/* DAT123.Dir */
	GPIO22_GPIO | PIN_INPUT_NOPULL, /* CLK-f */
	GPIO23_STMMOD_CLK | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,	/* STM CLK */
	GPIO24_UARTMOD_RXD | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP, /* STM UART RXD */
	GPIO25_STMMOD_DAT0 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP, /* STM DAT0 */
	GPIO26_STMMOD_DAT1 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP, /* STM DAT0 */
	GPIO27_STMMOD_DAT2 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP, /* STM DAT0 */
	GPIO28_STMMOD_DAT3 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP, /* STM DAT0 */
};

static pin_cfg_t janice_r0_0_fidobox_trace[] = {
	/* UARTMOD for FIDO box */
	GPIO153_U2_RXD,
	GPIO154_U2_TXD,
	/* STMMOD for FIDO box */
	GPIO155_STMAPE_CLK,
	GPIO156_STMAPE_DAT3,
	GPIO157_STMAPE_DAT2,
	GPIO158_STMAPE_DAT1,
	GPIO159_STMAPE_DAT0,
};

static pin_cfg_t janice_r0_0_sdmmc[] = {
	/* MMC0 (MicroSD card) */
	GPIO18_MC0_CMDDIR	| PIN_OUTPUT_HIGH,
	GPIO19_MC0_DAT0DIR	| PIN_OUTPUT_HIGH,
	GPIO20_MC0_DAT2DIR	| PIN_OUTPUT_HIGH,
	GPIO22_MC0_FBCLK	| PIN_INPUT_NOPULL,
	GPIO23_MC0_CLK		| PIN_OUTPUT_LOW,
	GPIO24_MC0_CMD		| PIN_INPUT_PULLUP,
	GPIO25_MC0_DAT0		| PIN_INPUT_PULLUP,
	GPIO26_MC0_DAT1		| PIN_INPUT_PULLUP,
	GPIO27_MC0_DAT2		| PIN_INPUT_PULLUP,
	GPIO28_MC0_DAT3		| PIN_INPUT_PULLUP,
};


/*
 * Pins disabled when not used to save power
 */

	/* Sensors(Gyro/Accel MPU3050/BMA222) */
static UX500_PINS(janice_r0_0_i2c2,
	GPIO8_I2C2_SDA |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO9_I2C2_SCL |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
);

	/* TSU6111(micro-USB switch  */
static UX500_PINS(janice_r0_0_i2c1,
	GPIO16_I2C1_SCL |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO17_I2C1_SDA |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
);

	/* Proximity Sensor I2C */
static UX500_PINS(janice_r0_0_i2c0,
	GPIO147_I2C0_SCL |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO148_I2C0_SDA |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
);
	/* Touchscreen I2C */
static UX500_PINS(janice_r0_0_i2c3,
	GPIO229_I2C3_SDA |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO230_I2C3_SCL |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
);

static UX500_PINS(janice_r0_0_mcde_dpi,
	/* DPI LCD RGB I/F */
	GPIO70_LCD_D0,
	GPIO71_LCD_D1,
	GPIO72_LCD_D2,
	GPIO73_LCD_D3,
	GPIO74_LCD_D4,
	GPIO75_LCD_D5,
	GPIO76_LCD_D6,
	GPIO77_LCD_D7,
	GPIO78_LCD_D8,
	GPIO79_LCD_D9,
	GPIO80_LCD_D10,
	GPIO81_LCD_D11,
	GPIO82_LCD_D12,
	GPIO83_LCD_D13,
	GPIO84_LCD_D14,
	GPIO85_LCD_D15,

	GPIO150_LCDA_CLK,

	GPIO161_LCD_D32,
	GPIO162_LCD_D33,
	GPIO163_LCD_D34,
	GPIO164_LCD_D35,
	GPIO165_LCD_D36,
	GPIO166_LCD_D37,
	GPIO167_LCD_D38,
	GPIO168_LCD_D39,
	GPIO169_LCDA_DE,
	GPIO170_LCDA_VSO,
	GPIO171_LCDA_HSO,
);

/* USB */
static UX500_PINS(janice_pins_usb,
	GPIO256_USB_NXT,
	GPIO257_USB_STP		| PIN_OUTPUT_HIGH,
	GPIO258_USB_XCLK,
	GPIO259_USB_DIR,
	GPIO260_USB_DAT7,
	GPIO261_USB_DAT6,
	GPIO262_USB_DAT5,
	GPIO263_USB_DAT4,
	GPIO264_USB_DAT3,
	GPIO265_USB_DAT2,
	GPIO266_USB_DAT1,
	GPIO267_USB_DAT0,
);

static UX500_PINS(janice_pins_uart0,
	GPIO0_U0_CTSn	| PIN_INPUT_PULLUP |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO1_U0_RTSn	| PIN_OUTPUT_HIGH |
		PIN_SLPM_GPIO | PIN_SLPM_OUTPUT_HIGH,
	GPIO2_U0_RXD	| PIN_INPUT_PULLUP |
		PIN_SLPM_GPIO | PIN_SLPM_INPUT_NOPULL,
	GPIO3_U0_TXD	| PIN_OUTPUT_HIGH |
		PIN_SLPM_GPIO | PIN_SLPM_OUTPUT_HIGH,
);

static struct ux500_pin_lookup janice_r0_0_lookup_pins[] = {
	PIN_LOOKUP("uart0", &janice_pins_uart0),
	PIN_LOOKUP("mcde-dpi", &janice_r0_0_mcde_dpi),
	PIN_LOOKUP("nmk-i2c.0", &janice_r0_0_i2c0),
	PIN_LOOKUP("nmk-i2c.1", &janice_r0_0_i2c1),
	PIN_LOOKUP("nmk-i2c.2", &janice_r0_0_i2c2),
	PIN_LOOKUP("nmk-i2c.3", &janice_r0_0_i2c3),
	PIN_LOOKUP("musb-ux500.0", &janice_pins_usb),
};

extern struct device *gps_dev;
extern struct class *sec_class;

static pin_cfg_t janice_gps_rev0_0_pins[] = {
	GPIO4_U1_RXD | PIN_INPUT_PULLUP, /* GPS UART */
	GPIO5_U1_TXD | PIN_OUTPUT_HIGH, /* GPS UART */
	PIN_CFG(GPS_RST_N_JANICE_R0_0, GPIO) | PIN_OUTPUT_HIGH, /* GPS_RST_N */
	PIN_CFG(EN_GPS_JANICE_R0_0, GPIO) | PIN_OUTPUT_HIGH, /* GPS_EN */
	PIN_CFG(GPS_ON_OFF_JANICE_R0_0, GPIO) | PIN_OUTPUT_LOW, /* GPS_ON_OFF */
};

static pin_cfg_t janice_gps_rev0_2_pins[] = {
	GPIO4_U1_RXD | PIN_INPUT_PULLUP, /* GPS UART */
	GPIO5_U1_TXD | PIN_OUTPUT_HIGH, /* GPS UART */
	PIN_CFG(GPS_RST_N_JANICE_R0_0, GPIO) | PIN_OUTPUT_HIGH, /* GPS_RST_N */
	PIN_CFG(86, GPIO) | PIN_INPUT_PULLDOWN, /* NC */
	PIN_CFG(GPS_ON_OFF_JANICE_R0_0, GPIO) | PIN_OUTPUT_LOW, /* GPS_ON_OFF */
};

static void __init gps_pins_init(void)
{
	gps_dev = device_create(sec_class, NULL, 0, NULL, "gps");
	if (!gps_dev)
		pr_err("Failed to create device(gps)!\n");
	if (system_rev == JANICE_R0_0 || system_rev == JANICE_R0_1) {
		nmk_config_pins(janice_gps_rev0_0_pins,
			ARRAY_SIZE(janice_gps_rev0_0_pins));
	} else {
		nmk_config_pins(janice_gps_rev0_2_pins,
			ARRAY_SIZE(janice_gps_rev0_2_pins));
	}

	gpio_request(GPS_RST_N_JANICE_R0_0, "GPS_nRST");
	gpio_direction_output(GPS_RST_N_JANICE_R0_0,0);
	gpio_request(GPS_ON_OFF_JANICE_R0_0, "GPS_ON_OFF");
	gpio_direction_output(GPS_ON_OFF_JANICE_R0_0,0);

	gpio_export(GPS_RST_N_JANICE_R0_0, 1);
	gpio_export(GPS_ON_OFF_JANICE_R0_0, 1);

	BUG_ON(!gps_dev);
	gpio_export_link(gps_dev, "GPS_nRST", GPS_RST_N_JANICE_R0_0);
	gpio_export_link(gps_dev, "GPS_ON_OFF", GPS_ON_OFF_JANICE_R0_0);

	if (system_rev == JANICE_R0_0 || system_rev == JANICE_R0_1) {
		gpio_request(EN_GPS_JANICE_R0_0, "GPS_PWR_EN");
		gpio_direction_output(EN_GPS_JANICE_R0_0, 1);
		gpio_export(EN_GPS_JANICE_R0_0, 1);
		gpio_export_link(gps_dev, "GPS_PWR_EN", EN_GPS_JANICE_R0_0);
	}
}

static void __init sdmmc_pins_init(void)
{
	u32 value;
	const void *prcm_gpiocr = __io_address(U8500_PRCMU_BASE) + 0x138;

	if (sec_debug_settings & SEC_DBG_STM_APE_OPT) {

		/* Set GPIO ALT to C3 */
		value = readl(prcm_gpiocr);
		value |= 0x00000200;	/* Set bit 9 */
		writel(value, prcm_gpiocr);

		nmk_config_pins(janice_r0_0_ape_trace, ARRAY_SIZE(janice_r0_0_ape_trace));

		/* also need to ensure VAUX3 turned on (defaults to 2.91V) */
		janice_regulators[AB8500_LDO_AUX3].constraints.valid_ops_mask = 0;
		janice_regulators[AB8500_LDO_AUX3].constraints.always_on = 1;

		printk(KERN_INFO "SD Card I/F set for STM APE Trace\n");

	} else if (sec_debug_settings & SEC_DBG_STM_MODEM_OPT) {

		/* Set GPIO ALT to B */
		value = readl(prcm_gpiocr);
		value &= ~0x00002202; /* For UART_MOD */
		writel(value, prcm_gpiocr);

		nmk_config_pins(janice_r0_0_modem_trace, ARRAY_SIZE(janice_r0_0_modem_trace));

		/* also need to ensure VAUX3 turned on (defaults to 2.91V) */
		janice_regulators[AB8500_LDO_AUX3].constraints.valid_ops_mask = 0;
		janice_regulators[AB8500_LDO_AUX3].constraints.always_on = 1;

		printk(KERN_INFO "SD Card I/F set for STM Modem Trace\n");
	} else if (sec_debug_settings & SEC_DBG_STM_FIDO_OPT) {

		value = readl(prcm_gpiocr);
		value |= 0x00002002;
		writel(value, prcm_gpiocr);

		nmk_config_pins(janice_r0_0_fidobox_trace, ARRAY_SIZE(janice_r0_0_fidobox_trace));

		printk(KERN_INFO "XTI I/F set for STM Fidobox Trace\n");
	} else {
		/* Set GPIO ALT to A */
		value = readl(prcm_gpiocr);
		value &= ~0x00000200; /* clear bit 9 */
		writel(value, prcm_gpiocr);

		nmk_config_pins(janice_r0_0_sdmmc, ARRAY_SIZE(janice_r0_0_sdmmc));
	}
}


static pin_cfg_t janice_r0_0_power_save_bank0[] = {
	GPIO0_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_ENABLED,
	GPIO1_GPIO | PIN_SLPM_OUTPUT_HIGH |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO2_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_ENABLED,
	GPIO3_GPIO | PIN_SLPM_OUTPUT_LOW |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO4_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PULL_DOWN,
	GPIO5_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PULL_DOWN,
	/* GPIO6_GPIO (MEM_LDO_EN) no change */
	GPIO7_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_ENABLED, /* NC */

	GPIO8_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PULL_DOWN,
	GPIO9_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PULL_DOWN,
	GPIO10_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO11_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO12_GPIO | PIN_SLPM_OUTPUT_LOW |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO13_GPIO | PIN_SLPM_OUTPUT_LOW |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO14_GPIO | PIN_SLPM_OUTPUT_LOW |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO15_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO16_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO17_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	/* SD/MMC card pins handled in seperate table (GPIOs 18-28, excl 21) */
	/* GPIO21_GPIO (GPS_RST_N) no change */

	GPIO29_U2_RXD | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_ENABLED,
	GPIO30_U2_TXD | PIN_SLPM_DIR_OUTPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO31_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PULL_DOWN, /* NFC_FIRMWARE */
};

static pin_cfg_t janice_r0_0_sdmmc_sleep[] = {
	/* MMC0 (MicroSD card) */
	GPIO18_GPIO | PIN_SLPM_OUTPUT_HIGH |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO19_GPIO | PIN_SLPM_OUTPUT_HIGH |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO20_GPIO | PIN_SLPM_OUTPUT_HIGH |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO22_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PULL_DOWN,
	GPIO23_GPIO | PIN_SLPM_OUTPUT_LOW |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO24_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO25_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO26_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO27_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,

	GPIO28_GPIO | PIN_SLPM_DIR_INPUT |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
	GPIO87_GPIO | PIN_SLPM_OUTPUT_LOW |
		PIN_SLPM_WAKEUP_ENABLE | PIN_SLPM_PDIS_DISABLED,
};

static pin_cfg_t janice_common_sleep_table[] = {
	GPIO32_GPIO | PIN_INPUT_PULLDOWN,  /* NFC_IRQ */
	GPIO33_GPIO | PIN_OUTPUT_LOW,
	GPIO34_GPIO | PIN_INPUT_NOPULL,
	GPIO35_GPIO | PIN_INPUT_NOPULL,
	GPIO36_GPIO | PIN_INPUT_NOPULL,

	GPIO64_GPIO | PIN_OUTPUT_LOW,  /* VT_CAM_STBY */
	GPIO65_GPIO | PIN_OUTPUT_LOW,  /* RST_VT_CAM */
	GPIO66_GPIO | PIN_INPUT_PULLDOWN,
	GPIO67_GPIO | PIN_INPUT_PULLUP,
	GPIO68_GPIO | PIN_INPUT_PULLDOWN,  /* EN_LED_LDO */
	GPIO69_GPIO | PIN_INPUT_PULLDOWN,

	GPIO70_GPIO | PIN_INPUT_PULLDOWN,
	GPIO71_GPIO | PIN_INPUT_PULLDOWN,
	GPIO72_GPIO | PIN_INPUT_PULLDOWN,
	GPIO73_GPIO | PIN_INPUT_PULLDOWN,
	GPIO74_GPIO | PIN_INPUT_PULLDOWN,
	GPIO75_GPIO | PIN_INPUT_PULLDOWN,
	GPIO76_GPIO | PIN_INPUT_PULLDOWN,
	GPIO77_GPIO | PIN_INPUT_PULLDOWN,
	GPIO78_GPIO | PIN_INPUT_PULLDOWN,
	GPIO79_GPIO | PIN_INPUT_PULLDOWN,

	GPIO80_GPIO | PIN_INPUT_PULLDOWN,
	GPIO81_GPIO | PIN_INPUT_PULLDOWN,
	GPIO82_GPIO | PIN_INPUT_PULLDOWN,
	GPIO83_GPIO | PIN_INPUT_PULLDOWN,
	GPIO84_GPIO | PIN_INPUT_PULLDOWN,
	GPIO85_GPIO | PIN_INPUT_PULLDOWN,
/*	GPIO86_GPIO | PIN_OUTPUT_LOW, */ /* EN_GPS */
	/*GPIO87_GPIO | PIN_INPUT_PULLDOWN,*/
	GPIO88_GPIO | PIN_OUTPUT_LOW,  /* NFC_EN */
	GPIO89_GPIO | PIN_OUTPUT_LOW,  /* TSP_LDO_ON2 */

	GPIO90_GPIO | PIN_INPUT_PULLDOWN,
	GPIO91_GPIO | PIN_INPUT_PULLUP,  /* HOME_KEY */
	GPIO92_GPIO | PIN_INPUT_PULLUP,
/*	GPIO93_GPIO | PIN_OUTPUT_LOW, */
	GPIO94_GPIO | PIN_OUTPUT_LOW,  /* TSP_LDO_ON1 */
	GPIO95_GPIO | PIN_INPUT_PULLUP,
/*	GPIO96_GPIO | PIN_OUTPUT_LOW, */ /* GPS_ON_OFF */
	GPIO97_GPIO | PIN_INPUT_PULLDOWN,  /* BT_HOST_WAKE */

/*	GPIO128_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO129_GPIO | PIN_OUTPUT_HIGH, */

/*	GPIO130_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO131_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO132_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO133_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO134_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO135_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO136_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO137_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO138_GPIO | PIN_OUTPUT_HIGH, */
/*	GPIO139_GPIO | PIN_OUTPUT_LOW, */ /* LCD_RESX */

/*	GPIO140_GPIO | PIN_OUTPUT_LOW, */ /* CAM_FLASH_EN */
/*	GPIO141_GPIO | PIN_OUTPUT_LOW, */ /* CAM_FLASH_MODE */
	GPIO142_GPIO | PIN_OUTPUT_LOW,  /* 5M_CAM_STBY */
	GPIO143_GPIO | PIN_INPUT_NOPULL,
	GPIO144_GPIO | PIN_INPUT_NOPULL,
	GPIO145_GPIO | PIN_OUTPUT_LOW,  /* SUBPMU_PWRON */
	GPIO146_GPIO | PIN_INPUT_PULLUP,  /* PS_VOUT */
	GPIO147_GPIO | PIN_INPUT_NOPULL,
	GPIO148_GPIO | PIN_INPUT_NOPULL,
	GPIO149_GPIO | PIN_OUTPUT_LOW,  /* 5M_CAM_RESET */

	GPIO150_GPIO | PIN_OUTPUT_LOW,
	GPIO151_GPIO | PIN_INPUT_NOPULL,  /* NFC_SCL_1.8V */
	GPIO152_GPIO | PIN_INPUT_NOPULL,  /* NFC_SDA_1.8V */
/*	GPIO153_GPIO | PIN_INPUT_PULLUP, */
/*	GPIO154_GPIO | PIN_INPUT_PULLUP, */
/*	GPIO155_GPIO | PIN_INPUT_PULLUP, */
/*	GPIO156_GPIO | PIN_INPUT_PULLUP, */
/*	GPIO157_GPIO | PIN_INPUT_PULLUP, */
/*	GPIO158_GPIO | PIN_INPUT_PULLUP, */
/*	GPIO159_GPIO | PIN_INPUT_PULLUP, */

/*	GPIO160_GPIO | PIN_INPUT_PULLUP, */
	GPIO161_GPIO | PIN_INPUT_PULLDOWN,
	GPIO162_GPIO | PIN_INPUT_PULLDOWN,
	GPIO163_GPIO | PIN_INPUT_PULLDOWN,
	GPIO164_GPIO | PIN_INPUT_PULLDOWN,
	GPIO165_GPIO | PIN_INPUT_PULLDOWN,
	GPIO166_GPIO | PIN_INPUT_PULLDOWN,
	GPIO167_GPIO | PIN_INPUT_PULLDOWN,
	GPIO168_GPIO | PIN_INPUT_PULLDOWN,
	GPIO169_GPIO | PIN_OUTPUT_LOW,  /* EN_LCD */

	GPIO170_GPIO | PIN_INPUT_PULLDOWN,
	GPIO171_GPIO | PIN_INPUT_PULLDOWN,

	GPIO192_GPIO | PIN_INPUT_PULLDOWN,
	GPIO193_GPIO | PIN_INPUT_PULLDOWN,
	GPIO194_GPIO | PIN_OUTPUT_LOW,  /* MOT_HEN */
	GPIO195_GPIO | PIN_OUTPUT_LOW,  /* MOT_LEN */
	GPIO196_GPIO | PIN_INPUT_PULLDOWN,  /* TOUCHKEY_SCL_1.8V */
	GPIO197_GPIO | PIN_INPUT_PULLDOWN,  /* TOUCHKEY_SDA_1.8V */
	GPIO198_GPIO | PIN_INPUT_PULLDOWN,  /* TOUCHKEY_INT_1.8V */
	GPIO199_GPIO | PIN_OUTPUT_LOW,  /* BT_WAKE */

	GPIO200_GPIO | PIN_INPUT_PULLDOWN,
	GPIO201_GPIO | PIN_INPUT_NOPULL,  /* MOT_SCL */
	GPIO202_GPIO | PIN_INPUT_NOPULL,  /* MOT_SDA */
/*	GPIO203_GPIO | PIN_INPUT_PULLDOWN, */
/*	GPIO204_GPIO | PIN_INPUT_PULLDOWN, */
/*	GPIO205_GPIO | PIN_OUTPUT_LOW, */ /* TSP_RST */
/*	GPIO206_GPIO | PIN_OUTPUT_LOW, */ /* TSP_TEST */
	GPIO207_GPIO | PIN_INPUT_PULLDOWN,
	GPIO208_GPIO | PIN_OUTPUT_LOW,  /* WLAN_SDIO_CLK */
/*	GPIO209_GPIO | PIN_OUTPUT_LOW, */ /* BT_RST_N */

	GPIO210_GPIO | PIN_INPUT_PULLUP,
	GPIO211_GPIO | PIN_INPUT_PULLUP,
	GPIO212_GPIO | PIN_INPUT_PULLUP,
	GPIO213_GPIO | PIN_INPUT_PULLUP,
	GPIO214_GPIO | PIN_INPUT_PULLUP,
/*	GPIO215_GPIO | PIN_OUTPUT_LOW, */ /* WIFI_RST_N */
	GPIO216_GPIO | PIN_INPUT_PULLDOWN,  /* WIFI_HOST_WAKE */
	GPIO217_GPIO | PIN_INPUT_NOPULL,  /* T_FLASH_DETECT */
	GPIO218_GPIO | PIN_INPUT_PULLDOWN,  /* TSP_INT_1V8 */
	GPIO219_GPIO | PIN_OUTPUT_LOW,  /* LCD_PWR_EN */

/*	GPIO220_GPIO | PIN_OUTPUT_LOW, */ /* LCD_SCL */
	GPIO221_GPIO | PIN_INPUT_PULLDOWN,
/*	GPIO222_GPIO | PIN_OUTPUT_LOW, */ /* BT_VGRE_EN */
/*	GPIO223_GPIO | PIN_OUTPUT_LOW, */ /* LCD_CSX */
/*	GPIO224_GPIO | PIN_OUTPUT_LOW, */ /* LCD_SDA */
	GPIO225_GPIO | PIN_INPUT_PULLDOWN,
	GPIO226_GPIO | PIN_INPUT_PULLDOWN,  /* SENSOR_INT */
	GPIO227_GPIO | PIN_OUTPUT_LOW,  /* CAM_MCLK */
	GPIO228_GPIO | PIN_OUTPUT_LOW,  /* MOT_PWM */
	GPIO229_GPIO | PIN_INPUT_PULLDOWN,

	GPIO230_GPIO | PIN_INPUT_PULLDOWN,

	GPIO256_GPIO | PIN_INPUT_PULLDOWN,
	GPIO257_GPIO | PIN_OUTPUT_HIGH,
	GPIO258_GPIO | PIN_INPUT_PULLDOWN,
	GPIO259_GPIO | PIN_INPUT_PULLDOWN,

	GPIO260_GPIO | PIN_INPUT_PULLDOWN,
	GPIO261_GPIO | PIN_INPUT_PULLDOWN,
	GPIO262_GPIO | PIN_INPUT_PULLDOWN,
	GPIO263_GPIO | PIN_INPUT_PULLDOWN,
	GPIO264_GPIO | PIN_INPUT_PULLDOWN,
	GPIO265_GPIO | PIN_INPUT_PULLDOWN,
	GPIO266_GPIO | PIN_INPUT_PULLDOWN,
	GPIO267_GPIO | PIN_INPUT_PULLDOWN,
};

static pin_cfg_t janice_r0_0_sleep_table[] = {
	GPIO93_GPIO | PIN_OUTPUT_LOW,  /* OLED_DETECT_BB */

	GPIO203_GPIO | PIN_INPUT_PULLDOWN,
	GPIO204_GPIO | PIN_INPUT_PULLDOWN,
	GPIO205_GPIO | PIN_OUTPUT_LOW,  /* TSP_RST */
	GPIO206_GPIO | PIN_OUTPUT_LOW,  /* TSP_TEST */
};

static pin_cfg_t janice_r0_2_sleep_table[] = {
	GPIO86_GPIO | PIN_INPUT_PULLDOWN,  /* NC */

	GPIO93_GPIO | PIN_OUTPUT_LOW,  /* OLED_DETECT_BB */

	GPIO204_GPIO | PIN_OUTPUT_HIGH,  /* COMPASS_RST */
	GPIO205_GPIO | PIN_OUTPUT_LOW,  /* SPK_AMP_CTRL */
	GPIO206_GPIO | PIN_INPUT_PULLDOWN,  /* NC */
};

static pin_cfg_t janice_r0_3_sleep_table[] = {
	GPIO86_GPIO | PIN_INPUT_PULLDOWN,  /* NC */

	GPIO93_GPIO | PIN_INPUT_PULLDOWN,  /* NC */

	GPIO204_GPIO | PIN_OUTPUT_HIGH,  /* COMPASS_RST */
	GPIO205_GPIO | PIN_INPUT_PULLDOWN,  /* NC */
	GPIO206_GPIO | PIN_INPUT_PULLDOWN,  /* NC */
};

/*
 * This function is called to force gpio power save
 * settings during suspend.
 * This is a temporary solution until all drivers are
 * controlling their pin settings when in inactive mode.
 */
static void janice_pins_suspend_force(void)
{
	sleep_pins_config_pm(janice_r0_0_power_save_bank0,
				ARRAY_SIZE(janice_r0_0_power_save_bank0));

	if (!(sec_debug_settings & (SEC_DBG_STM_APE_OPT | SEC_DBG_STM_MODEM_OPT))) {
		/* not using SD card I/F for modem trace */
		sleep_pins_config_pm(janice_r0_0_sdmmc_sleep,
					ARRAY_SIZE(janice_r0_0_sdmmc_sleep));
	}

	nmk_config_pins(janice_common_sleep_table,
		ARRAY_SIZE(janice_common_sleep_table));

	if (system_rev == JANICE_R0_0 || system_rev == JANICE_R0_1)
		nmk_config_pins(janice_r0_0_sleep_table,
			ARRAY_SIZE(janice_r0_0_sleep_table));
	else if (system_rev == JANICE_R0_2)
		nmk_config_pins(janice_r0_2_sleep_table,
			ARRAY_SIZE(janice_r0_2_sleep_table));
	else if (system_rev >= JANICE_R0_3)
		nmk_config_pins(janice_r0_3_sleep_table,
			ARRAY_SIZE(janice_r0_3_sleep_table));

/*	sleep_pins_config_pm(janice_r0_0_sleep_bank1,
				ARRAY_SIZE(janice_r0_0_sleep_bank1));
	sleep_pins_config_pm(janice_r0_0_sleep_bank2,
				ARRAY_SIZE(janice_r0_0_sleep_bank2));
	sleep_pins_config_pm(janice_r0_0_sleep_bank3,
				ARRAY_SIZE(janice_r0_0_sleep_bank3));
	sleep_pins_config_pm(janice_r0_0_sleep_bank4,
				ARRAY_SIZE(janice_r0_0_sleep_bank4));
	sleep_pins_config_pm(janice_r0_0_sleep_bank5,
				ARRAY_SIZE(janice_r0_0_sleep_bank5));
	sleep_pins_config_pm(janice_r0_0_sleep_bank6,
				ARRAY_SIZE(janice_r0_0_sleep_bank6));
	sleep_pins_config_pm(janice_r0_0_sleep_bank7,
				ARRAY_SIZE(janice_r0_0_sleep_bank7));
	sleep_pins_config_pm(janice_r0_0_sleep_bank8,
				ARRAY_SIZE(janice_r0_0_sleep_bank8));	*/
}

/*
 * This function is called to force gpio power save
 * mux settings during suspend.
 * This is a temporary solution until all drivers are
 * controlling their pin settings when in inactive mode.
 */
static void janice_pins_suspend_force_mux(void)
{
	u32 bankaddr;

	/*
	 * Apply GPIO Config for DeepSleep
	 *
	 * Bank0
	 */
	sleep_pins_config_pm_mux(janice_r0_0_power_save_bank0,
				ARRAY_SIZE(janice_r0_0_power_save_bank0));

	if (!(sec_debug_settings & (SEC_DBG_STM_APE_OPT | SEC_DBG_STM_MODEM_OPT))) {
		/* not using SD card I/F for modem trace */
		sleep_pins_config_pm_mux(janice_r0_0_sdmmc_sleep,
					ARRAY_SIZE(janice_r0_0_sdmmc_sleep));
	}

	/* Bank1 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK1_BASE);

	writel(0x60000000, bankaddr + NMK_GPIO_AFSLA);
	writel(0x60000000, bankaddr + NMK_GPIO_AFSLB);

	/* Bank2 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK2_BASE);

	writel(0         , bankaddr + NMK_GPIO_AFSLA);
	writel(0         , bankaddr + NMK_GPIO_AFSLB);

	/* Bank3 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK3_BASE);

	writel(0         , bankaddr + NMK_GPIO_AFSLA);
	writel(0         , bankaddr + NMK_GPIO_AFSLB);

	/* Bank4 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK4_BASE);

	/*
	 * Keep MMC2 (eMMC) alt A. Instability otherwise.
	 */
	writel(0x000007FF, bankaddr + NMK_GPIO_AFSLA);
	writel(0x00000000, bankaddr + NMK_GPIO_AFSLB);

	/* Bank5 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK5_BASE);

	writel(0         , bankaddr + NMK_GPIO_AFSLA);
	writel(0         , bankaddr + NMK_GPIO_AFSLB);

	/* Bank6 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK6_BASE);

	writel(0         , bankaddr + NMK_GPIO_AFSLA);
	writel(0         , bankaddr + NMK_GPIO_AFSLB);

	/* Bank7 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK7_BASE);

	writel(0         , bankaddr + NMK_GPIO_AFSLA);
	writel(0         , bankaddr + NMK_GPIO_AFSLB);

	/* Bank8 */
	bankaddr = IO_ADDRESS(U8500_GPIOBANK8_BASE);

	writel(0         , bankaddr + NMK_GPIO_AFSLA);
	writel(0         , bankaddr + NMK_GPIO_AFSLB);
}


void __init ssg_pins_init(void)
{
	nmk_config_pins(janice_r0_0_pins, ARRAY_SIZE(janice_r0_0_pins));
	ux500_pins_add(janice_r0_0_lookup_pins, ARRAY_SIZE(janice_r0_0_lookup_pins));
	gps_pins_init();
	sdmmc_pins_init();
	suspend_set_pins_force_fn(janice_pins_suspend_force,
				  janice_pins_suspend_force_mux);
}

int pins_for_u9500(void)
{
	/* required by STE code */
	return 0;
}

