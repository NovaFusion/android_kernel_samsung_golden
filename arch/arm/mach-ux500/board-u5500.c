/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/abx500.h>
#include <linux/ab5500-vibra.h>
#include <linux/led-lm3530.h>
#include <../drivers/staging/ste_rmi4/synaptics_i2c_rmi4.h>
#include <linux/input/matrix_keypad.h>
#include <linux/lsm303dlh.h>
#include <linux/leds-ab5500.h>
#include <linux/cyttsp.h>
#include <linux/input/abx500-accdet.h>

#include <video/av8100.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/pincfg.h>
#include <plat/i2c.h>

#include <mach/hardware.h>
#include <mach/ste-dma40-db5500.h>
#include <mach/msp.h>
#include <mach/devices.h>
#include <mach/setup.h>
#include <mach/db5500-keypad.h>
#include <mach/crypto-ux500.h>
#include <mach/usb.h>

#include "pins-db5500.h"
#include "pins.h"
#include "devices-db5500.h"
#include "board-u5500.h"
#include "board-u5500-bm.h"
#include "board-u5500-wlan.h"
#include "board-ux500-usb.h"

/*
 * LSM303DLH
 */

static struct lsm303dlh_platform_data lsm303dlh_pdata = {
	.axis_map_x = 1,
	.axis_map_y = 0,
	.axis_map_z = 2,
/* display is mounted reverse in the hardware */
	.negative_x = 1,
	.negative_y = 1,
	.negative_z = 1,
};

/*
 * Touchscreen
 */
static struct synaptics_rmi4_platform_data rmi4_i2c_platformdata = {
	.irq_number	= NOMADIK_GPIO_TO_IRQ(179),
	.irq_type	= (IRQF_TRIGGER_FALLING | IRQF_SHARED),
	.x_flip		= false,
	.y_flip		= true,
	.regulator_en	= true,
};

static struct av8100_platform_data av8100_plat_data = {
	.irq = NOMADIK_GPIO_TO_IRQ(223),
	.reset = 225,
	.alt_powerupseq = true,
	.mclk_freq = 1, /* MCLK_RNG_22_27 */
};

/*
 * leds LM3530
 */
static struct lm3530_platform_data u5500_als_platform_data = {
	.mode = LM3530_BL_MODE_MANUAL,
	.als_input_mode = LM3530_INPUT_ALS1,
	.max_current = LM3530_FS_CURR_26mA,
	.pwm_pol_hi = true,
	.als_avrg_time = LM3530_ALS_AVRG_TIME_4096ms,
	.brt_ramp_law = 1,	/* Linear */
	.brt_ramp_fall = LM3530_RAMP_TIME_260ms,
	.brt_ramp_rise = LM3530_RAMP_TIME_260ms,
	.als1_resistor_sel = LM3530_ALS_IMPD_13_53kOhm,
	.als2_resistor_sel = LM3530_ALS_IMPD_Z,
	.als_vmin = 730,	/* mV */
	.als_vmax = 1020,	/* mV */
	.brt_val = 0x7F,	/* Max brightness */
	.hw_en_gpio = LM3530_BL_ENABLE_GPIO,
};


/* leds-ab5500 */
static struct ab5500_hvleds_platform_data ab5500_hvleds_data = {
	.hw_fade = false,
	.leds = {
		[0] = {
			.name = "red",
			.led_on = true,
			.led_id = 0,
			.fade_hi = 255,
			.fade_lo = 0,
			.max_current = 10, /* wrong value may damage h/w */
		},
		[1] = {
			.name = "green",
			.led_on = true,
			.led_id = 1,
			.fade_hi = 255,
			.fade_lo = 0,
			.max_current = 10, /* wrong value may damage h/w */
		},
		[2] {
			.name = "blue",
			.led_on = true,
			.led_id = 2,
			.fade_hi = 255,
			.fade_lo = 0,
			.max_current = 10, /* wrong value may damage h/w */
		},
	},
};

static struct ab5500_ponkey_platform_data ab5500_ponkey_data = {
	/*
	 * Shutdown time in secs. Can be set
	 * to 10sec, 5sec and 0sec(disabled)
	 */
	.shutdown_secs = 10,
};

/* ab5500-vibra */
static struct ab5500_vibra_platform_data ab5500_vibra_data = {
	.type           = AB5500_VIB_ROTARY,
	.voltage        = AB5500_VIB_VOLT_MIN,
	/*
	 * EOL voltage in millivolts. By default, it is
	 * disabled. Set threshold volatge to enable.
	 */
	.eol_voltage	= 0,
	.res_freq       = AB5500_VIB_RFREQ_150HZ,
	.magnitude      = 0x7F,
	.pulse          = AB5500_VIB_PULSE_130ms,
};

/*
 * I2C
 */

#define U5500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, t_out, _sm) \
static struct nmk_i2c_controller u5500_i2c##id##_data = { \
	/*				\
	 * slave data setup time, which is	\
	 * 250 ns,100ns,10ns which is 14,6,2	\
	 * respectively for a 48 Mhz	\
	 * i2c clock			\
	 */				\
	.slsu		= _slsu,	\
	/* Tx FIFO threshold */		\
	.tft		= _tft,		\
	/* Rx FIFO threshold */		\
	.rft		= _rft,		\
	/* std. mode operation */	\
	.clk_freq	= clk,		\
	/* Slave response timeout(ms) */\
	.timeout	= t_out,	\
	.sm		= _sm,		\
}

/*
 * The board uses 3 i2c controllers, initialize all of
 * them with slave data setup time of 250 ns,
 * Tx & Rx FIFO threshold values as 1 and standard
 * mode of operation
 */

U5500_I2C_CONTROLLER(1,	0xe, 1, 10, 400000, 200, I2C_FREQ_MODE_FAST);
U5500_I2C_CONTROLLER(2,	0xe, 1, 10, 400000, 200, I2C_FREQ_MODE_FAST);
U5500_I2C_CONTROLLER(3,	0xe, 1, 10, 400000, 200, I2C_FREQ_MODE_FAST);

static struct i2c_board_info __initdata u5500_i2c2_sensor_devices[] = {
	{
		/* LSM303DLHC Accelerometer */
		I2C_BOARD_INFO("lsm303dlhc_a", 0x19),
		.platform_data = &lsm303dlh_pdata,
	},
	{
		/* LSM303DLH Magnetometer */
		I2C_BOARD_INFO("lsm303dlh_m", 0x1E),
		.platform_data = &lsm303dlh_pdata,
	},
};

static struct i2c_board_info __initdata u5500_i2c2_devices[] = {
	{
		/* Backlight */
		I2C_BOARD_INFO("lm3530-led", 0x36),
		.platform_data = &u5500_als_platform_data,
	},
	{
		I2C_BOARD_INFO("av8100", 0x70),
		.platform_data = &av8100_plat_data,
	},
};

/*
 * Keypad
 */

#define ROW_PIN_I0      128
#define ROW_PIN_I1      130
#define ROW_PIN_I2      132
#define ROW_PIN_I3      134
#define COL_PIN_O4      137
#define COL_PIN_O5      139

static int db5500_kp_rows[] = {
	ROW_PIN_I0, ROW_PIN_I1, ROW_PIN_I2, ROW_PIN_I3,
};

static int db5500_kp_cols[] = {
	COL_PIN_O4, COL_PIN_O5,
};

static bool db5500_config;
static int db5500_set_gpio_row(int gpio)
{
	int ret = -1;


	if (!db5500_config) {
		ret = gpio_request(gpio, "db5500_kpd");
		if (ret < 0) {
			pr_err("db5500_set_gpio_row: gpio request failed\n");
			return ret;
		}
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret < 0) {
		pr_err("db5500_set_gpio_row: gpio direction failed\n");
		gpio_free(gpio);
	}

	return ret;
}

static int db5500_kp_init(void)
{
	struct ux500_pins *pins;
	int ret, i;

	pins = ux500_pins_get("db5500_kp");
	if (pins)
		ux500_pins_enable(pins);

	for (i = 0; i < ARRAY_SIZE(db5500_kp_rows); i++) {
		ret = db5500_set_gpio_row(db5500_kp_rows[i]);
		if (ret < 0) {
			pr_err("db5500_kp_init: failed init\n");
			return ret;
		}
	}

	if (!db5500_config)
		db5500_config = true;

	return 0;
}

static int db5500_kp_exit(void)
{
	struct ux500_pins *pins;

	pins = ux500_pins_get("db5500_kp");
	if (pins)
		ux500_pins_disable(pins);

	return 0;
}

static const unsigned int u5500_keymap[] = {
	KEY(4, 0, KEY_CAMERA), /* Camera2 */
	KEY(4, 1, KEY_CAMERA_FOCUS), /* Camera1 */
	KEY(4, 2, KEY_MENU),
	KEY(4, 3, KEY_BACK),
	KEY(5, 2, KEY_SEND),
	KEY(5, 3, KEY_HOME),
#ifndef CONFIG_INPUT_AB8500_PONKEY
	/* AB5500 ONSWa is also hooked up to this key */
	KEY(8, 0, KEY_END),
#endif
	KEY(8, 1, KEY_VOLUMEUP),
	KEY(8, 2, KEY_VOLUMEDOWN),
};

static struct matrix_keymap_data u5500_keymap_data = {
	.keymap		= u5500_keymap,
	.keymap_size	= ARRAY_SIZE(u5500_keymap),
};

static struct db5500_keypad_platform_data u5500_keypad_board = {
	.init           = db5500_kp_init,
	.exit           = db5500_kp_exit,
	.gpio_input_pins = db5500_kp_rows,
	.gpio_output_pins = db5500_kp_cols,
	.keymap_data	= &u5500_keymap_data,
	.no_autorepeat	= true,
	.krow		= ARRAY_SIZE(db5500_kp_rows),
	.kcol		= ARRAY_SIZE(db5500_kp_cols),
	.debounce_ms	= 40, /* milliseconds */
	.switch_delay	= 200, /* in jiffies */
};

/*
 * MSP
 */

#define MSP_DMA(num, eventline)					\
static struct stedma40_chan_cfg msp##num##_dma_rx = {		\
	.high_priority = true,					\
	.dir = STEDMA40_PERIPH_TO_MEM,				\
	.src_dev_type = eventline##_RX,				\
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,		\
	.src_info.psize = STEDMA40_PSIZE_LOG_4,			\
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,			\
};								\
								\
static struct stedma40_chan_cfg msp##num##_dma_tx = {		\
	.high_priority = true,					\
	.dir = STEDMA40_MEM_TO_PERIPH,				\
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,		\
	.dst_dev_type = eventline##_TX,				\
	.src_info.psize = STEDMA40_PSIZE_LOG_4,			\
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,			\
}

MSP_DMA(0, DB5500_DMA_DEV9_MSP0);
MSP_DMA(1, DB5500_DMA_DEV10_MSP1);
MSP_DMA(2, DB5500_DMA_DEV11_MSP2);

static struct msp_i2s_platform_data u5500_msp0_data = {
	.id		= MSP_0_I2S_CONTROLLER,
	.msp_i2s_dma_rx	= &msp0_dma_rx,
	.msp_i2s_dma_tx	= &msp0_dma_tx,
};

static struct msp_i2s_platform_data u5500_msp1_data = {
	.id		= MSP_1_I2S_CONTROLLER,
	.msp_i2s_dma_rx	= &msp1_dma_rx,
	.msp_i2s_dma_tx	= &msp1_dma_tx,
};

static struct msp_i2s_platform_data u5500_msp2_data = {
	.id		= MSP_2_I2S_CONTROLLER,
	.msp_i2s_dma_rx	= &msp2_dma_rx,
	.msp_i2s_dma_tx	= &msp2_dma_tx,
};

static void __init u5500_msp_init(void)
{
	db5500_add_msp0_i2s(&u5500_msp0_data);
	db5500_add_msp1_i2s(&u5500_msp1_data);
	db5500_add_msp2_i2s(&u5500_msp2_data);
}

/*
 * SPI
 */

static struct pl022_ssp_controller u5500_spi3_data = {
	.bus_id		= 1,
	.num_chipselect	= 4,	/* 3 possible CS lines + 1 for tests */
};

static void __init u5500_spi_init(void)
{
	db5500_add_spi3(&u5500_spi3_data);
}

static struct resource ab5500_resources[] = {
	[0] = {
		.start = IRQ_DB5500_PRCMU_ABB,
		.end = IRQ_DB5500_PRCMU_ABB,
		.flags = IORESOURCE_IRQ
	}
};


#ifdef CONFIG_INPUT_AB5500_ACCDET
static struct abx500_accdet_platform_data ab5500_accdet_pdata = {
	       .btn_keycode = KEY_MEDIA,
	       .accdet1_dbth = ACCDET1_TH_300mV | ACCDET1_DB_10ms,
	       .accdet2122_th = ACCDET21_TH_300mV | ACCDET22_TH_300mV,
	       .is_detection_inverted = false,
	};
#endif

static struct abx500_init_settings ab5500_init_settings[] = {
	{
		.bank = AB5500_BANK_SIM_USBSIM,
		.reg = 0x17,
		.setting = 0x0F,
	},
	/* SIMOFF_N is not connected and is set to GPIO.
	   Set SIMOFF_N in invert mode */
	{
		.bank = AB5500_BANK_SIM_USBSIM,
		.reg = 0x13,
		.setting = 0x4,
	},
	{
		.bank = AB5500_BANK_SIM_USBSIM,
		.reg = 0x18,
		.setting = 0x10,
	},
	/* Set unused PWRCTRL0 & PWRCTRL1 as GPIOs */
	{
		.bank = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.reg = 0x30,
		.setting = 0x30,
	},
	/* Set unused EXTBST1/CLK/SLP,RTCCLK2 as GPIOs */
	{
		.bank = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.reg = 0x31,
		.setting = 0x74,
	},
	/* Set unused AVCTRL,EXTBST2/CLK/SLP,RTCCLK2 as GPIO */
	{
		.bank = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.reg = 0x33,
		.setting = 0x3C,
	},
	/* Set unused EXT32CLK as GPIO */
	{
		.bank = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.reg = 0x35,
		.setting = 0x1,
	},
	/* Set unused SIMOFF_N as GPIO */
	{
		.bank = AB5500_BANK_VDDDIG_IO_I2C_CLK_TST,
		.reg = 0x31,
		.setting = 0x80,
	},
	/* Set unused YcBcR2 & YcBcR1 as GPIO */
	{
		.bank = AB5500_BANK_VDDDIG_IO_I2C_CLK_TST,
		.reg = 0x33,
		.setting = 0xC0,
	},
	/* Set unused USBUICCPD/SE0/DATA,
		YcBcR0,YcBcR3 as GPIO */
	{
		.bank = AB5500_BANK_VDDDIG_IO_I2C_CLK_TST,
		.reg = 0x34,
		.setting = 0x1F,
	},
};

static struct ab5500_platform_data ab5500_plf_data = {
	.irq = {
		.base = IRQ_AB5500_BASE,
		.count = AB5500_NR_IRQS,
	},
	.pm_power_off	= true,
	.regulator	= &u5500_ab5500_regulator_data,
#ifdef CONFIG_INPUT_AB5500_ACCDET
	.dev_data[AB5500_DEVID_ACCDET] = &ab5500_accdet_pdata,
	.dev_data_sz[AB5500_DEVID_ACCDET] = sizeof(ab5500_accdet_pdata),
#endif
	.dev_data[AB5500_DEVID_LEDS] = &ab5500_hvleds_data,
	.dev_data_sz[AB5500_DEVID_LEDS] = sizeof(ab5500_hvleds_data),
	.dev_data[AB5500_DEVID_VIBRATOR] = &ab5500_vibra_data,
	.dev_data_sz[AB5500_DEVID_VIBRATOR] = sizeof(ab5500_vibra_data),
	.init_settings = &ab5500_init_settings,
	.init_settings_sz = ARRAY_SIZE(ab5500_init_settings),
#if defined(CONFIG_AB5500_BM)
	.dev_data[AB5500_DEVID_CHARGALG] = &abx500_bm_pt_data,
	.dev_data_sz[AB5500_DEVID_CHARGALG] = sizeof(abx500_bm_pt_data),
	.dev_data[AB5500_DEVID_CHARGER] = &abx500_bm_pt_data,
	.dev_data_sz[AB5500_DEVID_CHARGER] = sizeof(abx500_bm_pt_data),
	.dev_data[AB5500_DEVID_FG] = &abx500_bm_pt_data,
	.dev_data_sz[AB5500_DEVID_FG] = sizeof(abx500_bm_pt_data),
	.dev_data[AB5500_DEVID_BTEMP] = &abx500_bm_pt_data,
	.dev_data_sz[AB5500_DEVID_BTEMP] = sizeof(abx500_bm_pt_data),
#endif
	.dev_data[AB5500_DEVID_ONSWA] = &ab5500_ponkey_data,
	.dev_data_sz[AB5500_DEVID_ONSWA] = sizeof(ab5500_ponkey_data),
	.dev_data[AB5500_DEVID_USB] = &abx500_usbgpio_plat_data,
	.dev_data_sz[AB5500_DEVID_USB] = sizeof(abx500_usbgpio_plat_data),
};

static struct platform_device u5500_ab5500_device = {
	.name = "ab5500-core",
	.id = 0,
	.dev = {
		.platform_data = &ab5500_plf_data,
	},
	.num_resources = 1,
	.resource = ab5500_resources,
};

static struct platform_device u5500_mloader_device = {
	.name = "db5500_mloader",
	.id = -1,
	.num_resources = 0,
};

static struct cryp_platform_data u5500_cryp1_platform_data = {
	.mem_to_engine = {
		.dir = STEDMA40_MEM_TO_PERIPH,
		.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
		.dst_dev_type = DB5500_DMA_DEV48_CRYPTO1_TX,
		.src_info.data_width = STEDMA40_WORD_WIDTH,
		.dst_info.data_width = STEDMA40_WORD_WIDTH,
		.mode = STEDMA40_MODE_LOGICAL,
		.src_info.psize = STEDMA40_PSIZE_LOG_4,
		.dst_info.psize = STEDMA40_PSIZE_LOG_4,
	},
	.engine_to_mem = {
		.dir = STEDMA40_PERIPH_TO_MEM,
		.src_dev_type = DB5500_DMA_DEV48_CRYPTO1_RX,
		.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
		.src_info.data_width = STEDMA40_WORD_WIDTH,
		.dst_info.data_width = STEDMA40_WORD_WIDTH,
		.mode = STEDMA40_MODE_LOGICAL,
		.src_info.psize = STEDMA40_PSIZE_LOG_4,
		.dst_info.psize = STEDMA40_PSIZE_LOG_4,
	}
};

static struct stedma40_chan_cfg u5500_hash_dma_cfg_tx = {
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB5500_DMA_DEV50_HASH1_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
	.mode = STEDMA40_MODE_LOGICAL,
	.src_info.psize = STEDMA40_PSIZE_LOG_16,
	.dst_info.psize = STEDMA40_PSIZE_LOG_16,
};

static struct hash_platform_data u5500_hash1_platform_data = {
	.mem_to_engine = &u5500_hash_dma_cfg_tx,
	.dma_filter = stedma40_filter,
};

/* modem crash dump detection driver data */
static struct resource mcdd_resources[] = {
	{
		.name = "mcdd_intreset_addr",
		.start = U5500_INTCON_MBOX1_INT_RESET_ADDR,
		.end = U5500_INTCON_MBOX1_INT_RESET_ADDR,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mcdd_mbox_irq",
		.start = MBOX_PAIR1_VIRT_IRQ,
		.end = MBOX_PAIR1_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

struct platform_device u5500_mcdd_device = {
	.name	= "u5500-mcdd-modem",
	.id = 0,
	.resource = mcdd_resources,
	.num_resources = ARRAY_SIZE(mcdd_resources),
};

static struct platform_device *u5500_platform_devices[] __initdata = {
	&u5500_ab5500_device,
	&ux500_mcde_device,
	&u5500_dsilink_device[0],
	&u5500_dsilink_device[1],
	&ux500_hwmem_device,
	&ux500_b2r2_device,
	&ux500_b2r2_blt_device,
	&u5500_mloader_device,
#ifdef CONFIG_U5500_MMIO
	&u5500_mmio_device,
#endif
	&u5500_mcdd_device,
};

#define BACKUPRAM_ROM_DEBUG_ADDR	0xFFC
#define MMC_BLOCK_ID			0x20

int u5500_get_boot_mmc(void)
{
	unsigned int mmcblk;

	mmcblk = readl(__io_address(U5500_BACKUPRAM1_BASE) +
		       BACKUPRAM_ROM_DEBUG_ADDR);

	if (mmcblk & MMC_BLOCK_ID)
		return 2;

	return 0;
}

/*
 * R3A (and presumably, future) S5500 boards have different regulator
 * assignments from the earlier boards.  Since there's no clean way to identify
 * the board revision from hardware, we use the fact that R2A boots from MMC0
 * (via peripheral boot) and R3A boots from MMC2 to distinguish them.
 */
bool u5500_board_is_pre_r3a(void)
{
	if (!cpu_is_u5500v20())
		return false;

	if (!u5500_board_is_s5500())
		return true;

	if (u5500_get_boot_mmc() == 2)
		return false;

	return true;
}


/*
 * This function check whether it is Small S5500 board
 * GPIO0 is HIGH for S5500
 */
bool u5500_board_is_s5500(void)
{
	static bool s5500;
	static bool once;
	int err, val;

	if (once)
		return s5500;

	err = gpio_request(GPIO_BOARD_VERSION, "Board Version");
	if (err) {
		pr_err("Error %d while requesting GPIO for Board Version\n",
				err);
		return err;
	}

	err = gpio_direction_input(GPIO_BOARD_VERSION);
	if (err) {
		pr_err("Error %d while setting GPIO for Board Version"
				"output mode\n", err);
		return err;
	}

	val = gpio_get_value(GPIO_BOARD_VERSION);

	gpio_free(GPIO_BOARD_VERSION);

	s5500 = val;
	once = true;

	return val;
}

static long u5500_panic_blink(int state)
{
	gpio_direction_output(GPIO_SW_CRASH_INDICATOR, state);
	return 0;
}

#define PRCC_K_SOFTRST_SET      0x18
#define PRCC_K_SOFTRST_CLEAR    0x1C
/* pl011 reset */
static void ux500_uart3_reset(void)
{
	void __iomem *prcc_rst_set, *prcc_rst_clr;

	prcc_rst_set = __io_address(U5500_CLKRST5_BASE +
			PRCC_K_SOFTRST_SET);
	prcc_rst_clr = __io_address(U5500_CLKRST5_BASE +
			PRCC_K_SOFTRST_CLEAR);

	/*
	 * Activate soft reset PRCC_K_SOFTRST_CLEAR
	 *
	 * As we are dealing with IP register lockup
	 * so to make double sure that IP gets reset
	 * and reset pulse remains for more than one
	 * clock cycle a delay is added.
	 */
	writel((readl(prcc_rst_clr) | 0x08), prcc_rst_clr);
	udelay(1);

	/* Release soft reset PRCC_K_SOFTRST_SET */
	writel((readl(prcc_rst_set) | 0x08), prcc_rst_set);
	udelay(1);
}

static struct amba_pl011_data uart3_plat = {
	.reset = ux500_uart3_reset,
};

static void __init u5500_i2c_init(void)
{
	db5500_add_i2c1(&u5500_i2c1_data);
	db5500_add_i2c2(&u5500_i2c2_data);
	db5500_add_i2c3(&u5500_i2c3_data);

	i2c_register_board_info(2, ARRAY_AND_SIZE(u5500_i2c2_devices));
	i2c_register_board_info(2, ARRAY_AND_SIZE(u5500_i2c2_sensor_devices));
}

static void __init u5500_uart_init(void)
{
	db5500_add_uart0(NULL);
	db5500_add_uart1(NULL);
	db5500_add_uart2(NULL);
	db5500_add_uart3(&uart3_plat);
}

static void __init u5500_cryp1_hash1_init(void)
{
	db5500_add_cryp1(&u5500_cryp1_platform_data);
	db5500_add_hash1(&u5500_hash1_platform_data);
}

static int __init u5500_accel_sensor_init(void)
{
	int status;
	union i2c_smbus_data data;
	struct i2c_adapter *i2c2;

	i2c2 = i2c_get_adapter(2);
	if (!i2c2) {
		pr_err("failed to get i2c adapter\n");
		return -ENODEV;
	}
	status = i2c_smbus_xfer(i2c2, 0x19 , 0,
				I2C_SMBUS_READ, 0x0F ,
				I2C_SMBUS_BYTE_DATA, &data);
	if (status < 0)
		lsm303dlh_pdata.chip_id = 0;
	else
		lsm303dlh_pdata.chip_id = data.byte;

	i2c_put_adapter(i2c2);

	return status;
}
module_init(u5500_accel_sensor_init);

static void __init u5500_init_machine(void)
{
	u5500_init_devices();
	u5500_regulators_init();
	u5500_pins_init();

	u5500_i2c_init();
	u5500_msp_init();
	u5500_spi_init();

	u5500_sdi_init();
	u5500_uart_init();

	u5500_wlan_init();

	db5500_add_keypad(&u5500_keypad_board);
	u5500_cryp1_hash1_init();

#ifdef CONFIG_TOUCHSCREEN_CYTTSP_SPI
	u5500_cyttsp_init();
#endif

	platform_add_devices(u5500_platform_devices,
			     ARRAY_SIZE(u5500_platform_devices));

	if (!gpio_request_one(GPIO_SW_CRASH_INDICATOR, GPIOF_OUT_INIT_LOW,
			      "SW_CRASH_INDICATOR"))
		panic_blink = u5500_panic_blink;
}

MACHINE_START(U5500, "ST-Ericsson U5500 Platform")
	.boot_params	= 0x00000100,
	.map_io		= u5500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= u5500_init_machine,
MACHINE_END

MACHINE_START(B5500, "ST-Ericsson U5500 Big Board")
	.boot_params	= 0x00000100,
	.map_io		= u5500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= u5500_init_machine,
MACHINE_END
