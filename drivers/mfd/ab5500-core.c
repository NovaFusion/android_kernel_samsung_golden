/*
 * Copyright (C) 2007-2011 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Low-level core for exclusive access to the AB5500 IC on the I2C bus
 * and some basic chip-configuration.
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
 * Author: Karl Komierowski  <karl.komierowski@stericsson.com>
 * Author: Bibek Basu <bibek.basu@stericsson.com>
 */

#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/mfd/core.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/mfd/dbx500-prcmu.h>

#define AB5500_NAME_STRING "ab5500"
#define AB5500_ID_FORMAT_STRING "AB5500 %s"
#define AB5500_NUM_EVENT_V1_REG 23
#define AB5500_IT_LATCH0_REG		0x40
#define AB5500_IT_MASTER0_REG		0x00
#define AB5500_IT_MASK0_REG		0x60
/* These are the only registers inside AB5500 used in this main file */

/* Read/write operation values. */
#define AB5500_PERM_RD (0x01)
#define AB5500_PERM_WR (0x02)

/* Read/write permissions. */
#define AB5500_PERM_RO (AB5500_PERM_RD)
#define AB5500_PERM_RW (AB5500_PERM_RD | AB5500_PERM_WR)

#define AB5500_MASK_BASE (0x60)
#define AB5500_MASK_END (0x79)
#define AB5500_CHIP_ID (0x20)
#define AB5500_INTERRUPTS 0x01FFFFFF

/* Turn On Status Event */
#define RTC_ALARM		0x80
#define POW_KEY_2_ON		0x40
#define POW_KEY_1_ON		0x10
#define POR_ON_VBAT		0x10
#define VBUS_DET		0x20
#define VBUS_CH_DROP_R		0x08
#define USB_CH_DET_DONE		0x02

/* Global Variables */
u8 turn_on_stat = 0x00;

/**
 * struct ab5500_bank
 * @slave_addr: I2C slave_addr found in AB5500 specification
 * @name: Documentation name of the bank. For reference
 */
struct ab5500_bank {
	u8 slave_addr;
	const char *name;
};

static const struct ab5500_bank bankinfo[AB5500_NUM_BANKS] = {
	[AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP] = {
		AB5500_ADDR_VIT_IO_I2C_CLK_TST_OTP, "VIT_IO_I2C_CLK_TST_OTP"},
	[AB5500_BANK_VDDDIG_IO_I2C_CLK_TST] = {
		AB5500_ADDR_VDDDIG_IO_I2C_CLK_TST, "VDDDIG_IO_I2C_CLK_TST"},
	[AB5500_BANK_VDENC] = {AB5500_ADDR_VDENC, "VDENC"},
	[AB5500_BANK_SIM_USBSIM] = {AB5500_ADDR_SIM_USBSIM, "SIM_USBSIM"},
	[AB5500_BANK_LED] = {AB5500_ADDR_LED, "LED"},
	[AB5500_BANK_ADC] = {AB5500_ADDR_ADC, "ADC"},
	[AB5500_BANK_RTC] = {AB5500_ADDR_RTC, "RTC"},
	[AB5500_BANK_STARTUP] = {AB5500_ADDR_STARTUP, "STARTUP"},
	[AB5500_BANK_DBI_ECI] = {AB5500_ADDR_DBI_ECI, "DBI-ECI"},
	[AB5500_BANK_CHG] = {AB5500_ADDR_CHG, "CHG"},
	[AB5500_BANK_FG_BATTCOM_ACC] = {
		AB5500_ADDR_FG_BATTCOM_ACC, "FG_BATCOM_ACC"},
	[AB5500_BANK_USB] = {AB5500_ADDR_USB, "USB"},
	[AB5500_BANK_IT] = {AB5500_ADDR_IT, "IT"},
	[AB5500_BANK_VIBRA] = {AB5500_ADDR_VIBRA, "VIBRA"},
	[AB5500_BANK_AUDIO_HEADSETUSB] = {
		AB5500_ADDR_AUDIO_HEADSETUSB, "AUDIO_HEADSETUSB"},
};

#define AB5500_IRQ(bank, bit)	((bank) * 8 + (bit))

/* I appologize for the resource names beeing a mix of upper case
 * and lower case but I want them to be exact as the documentation */
static struct mfd_cell ab5500_devs[AB5500_NUM_DEVICES] = {
	[AB5500_DEVID_LEDS] = {
		.name = "ab5500-leds",
	},
	[AB5500_DEVID_POWER] = {
		.name = "ab5500-power",
	},
	[AB5500_DEVID_REGULATORS] = {
		.name = "ab5500-regulator",
	},
	[AB5500_DEVID_SIM] = {
		.name = "ab5500-sim",
		.num_resources = 1,
		.resources = (struct resource[]) {
			{
				.name = "SIMOFF",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(2, 0), /*rising*/
				.end = AB5500_IRQ(2, 1), /*falling*/
			},
		},
	},
	[AB5500_DEVID_RTC] = {
		.name = "ab5500-rtc",
		.num_resources = 1,
		.resources = (struct resource[]) {
			{
				.name	= "RTC_Alarm",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(1, 7),
				.end	= AB5500_IRQ(1, 7),
			}
		},
	},
	[AB5500_DEVID_CHARGER] = {
		.name = "ab5500-charger",
		.num_resources = 29,
		.resources = (struct resource[]) {
			{
				.name = "VBAT_INSERT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(2, 4),
				.end = AB5500_IRQ(2, 4),
			},
			{
				.name = "TEMP_ASIC_ALARM",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(2, 2),
				.end = AB5500_IRQ(2, 2),
			},
			{
				.name = "BATT_REMOVAL",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 6),
				.end = AB5500_IRQ(7, 6),
			},
			{
				.name = "BATT_ATTACH",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 5),
				.end = AB5500_IRQ(7, 5),
			},
			{
				.name = "CGSTATE_10_PCVBUS_CHG",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 7),
				.end = AB5500_IRQ(8, 7),
			},
			{
				.name = "VBUS_FALLING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 6),
				.end = AB5500_IRQ(8, 6),
			},
			{
				.name = "VBUS_RISING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 5),
				.end = AB5500_IRQ(8, 5),
			},
			{
				.name = "UART_RDY_TX",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 2),
				.end = AB5500_IRQ(8, 2),
			},
			{
				.name = "UART_RDY_RX",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 1),
				.end = AB5500_IRQ(8, 1),
			},
			{
				.name = "UART_OVERRUN",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 0),
				.end = AB5500_IRQ(8, 0),
			},
			{
				.name = "VBUS_IMEAS_MAX_CHANGE_RISING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 7),
				.end = AB5500_IRQ(9, 7),
			},
			{
				.name = "USB_SUSPEND",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 3),
				.end = AB5500_IRQ(9, 3),
			},
			{
				.name = "USB_CHAR_DET_DONE",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 1),
				.end = AB5500_IRQ(9, 1),
			},
			{
				.name = "VBUS_IMEAS_MAX_CHANGE_FALLING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(10, 0),
				.end = AB5500_IRQ(10, 0),
			},
			{
				.name = "OVV",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 5),
				.end = AB5500_IRQ(14, 5),
			},
			{
				.name = "USB_CH_TH_PROTECTION",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 4),
				.end = AB5500_IRQ(15, 4),
			},
			{
				.name = "USB_CH_NOT_OK",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 3),
				.end = AB5500_IRQ(15, 3),
			},
			{
				.name = "CHAR_TEMP_WINDOW_OK_RISING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 7),
				.end = AB5500_IRQ(17, 7),
			},
			{
				.name = "CHARGING_STOPPED_BY_TEMP",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 6),
				.end = AB5500_IRQ(18, 6),
			},
			{
				.name = "VBUS_DROP_FALLING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 4),
				.end = AB5500_IRQ(18, 4),
			},
			{
				.name = "VBUS_DROP_RISING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 3),
				.end = AB5500_IRQ(18, 3),
			},
			{
				.name = "CHAR_TEMP_WINDOW_OK_FALLING",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 0),
				.end = AB5500_IRQ(18, 0),
			},
			{
				.name = "CHG_STATE_13_COMP_VBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 3),
				.end = AB5500_IRQ(21, 3),
			},
			{
				.name = "CHG_STATE_12_COMP_VBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 2),
				.end = AB5500_IRQ(21, 2),
			},
			{
				.name = "CHG_STATE_11_SAFE_MODE_VBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 1),
				.end = AB5500_IRQ(21, 1),
			},
			{
				.name = "USB_LINK_UPDATE",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(22, 1),
				.end = AB5500_IRQ(22, 1),
			},
			{
				.name = "CHG_SW_TIMER_OUT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(23, 7),
				.end = AB5500_IRQ(23, 7),
			},
			{
				.name = "CHG_HW_TIMER_OUT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(23, 6),
				.end = AB5500_IRQ(23, 6),
			},
		},
	},
	[AB5500_DEVID_CHARGALG] = {
		.name = "abx500-chargalg",
	},
	[AB5500_DEVID_BTEMP] = {
		.name = "ab5500-btemp",
		.num_resources = 2,
		.resources = (struct resource[]) {
			{
				.name = "BATT_ATTACH",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 5),
				.end = AB5500_IRQ(7, 5),
			},
			{
				.name = "BATT_REMOVAL",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 6),
				.end = AB5500_IRQ(7, 6),
			},
		},
	},
	[AB5500_DEVID_ADC] = {
		.name = "ab5500-adc",
		.num_resources = 10,
		.resources = (struct resource[]) {
			{
				.name = "TRIGGER-0",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 0),
				.end = AB5500_IRQ(0, 0),
			},
			{
				.name = "TRIGGER-1",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 1),
				.end = AB5500_IRQ(0, 1),
			},
			{
				.name = "TRIGGER-2",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 2),
				.end = AB5500_IRQ(0, 2),
			},
			{
				.name = "TRIGGER-3",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 3),
				.end = AB5500_IRQ(0, 3),
			},
			{
				.name = "TRIGGER-4",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 4),
				.end = AB5500_IRQ(0, 4),
			},
			{
				.name = "TRIGGER-5",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 5),
				.end = AB5500_IRQ(0, 5),
			},
			{
				.name = "TRIGGER-6",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 6),
				.end = AB5500_IRQ(0, 6),
			},
			{
				.name = "TRIGGER-7",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(0, 7),
				.end = AB5500_IRQ(0, 7),
			},
			{
				.name = "TRIGGER-VBAT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(1, 0),
				.end = AB5500_IRQ(1, 0),
			},
			{
				.name = "TRIGGER-VBAT-TXON",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(1, 1),
				.end = AB5500_IRQ(1, 1),
			},
		},
	},
	[AB5500_DEVID_FG] = {
		.name = "ab5500-fg",
		.num_resources = 6,
		.resources = (struct resource[]) {
			{
				.name = "Batt_attach",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 5),
				.end = AB5500_IRQ(7, 5),
			},
			{
				.name = "Batt_removal",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 6),
				.end = AB5500_IRQ(7, 6),
			},
			{
				.name = "UART_framing",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(7, 7),
				.end = AB5500_IRQ(7, 7),
			},
			{
				.name = "UART_overrun",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 0),
				.end = AB5500_IRQ(8, 0),
			},
			{
				.name = "UART_Rdy_RX",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 1),
				.end = AB5500_IRQ(8, 1),
			},
			{
				.name = "UART_Rdy_TX",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 2),
				.end = AB5500_IRQ(8, 2),
			},
		},
	},
	[AB5500_DEVID_VIBRATOR] = {
		.name = "ab5500-vibrator",
	},
	[AB5500_DEVID_CODEC] = {
		.name = "ab5500-codec",
		.num_resources = 3,
		.resources = (struct resource[]) {
			{
				.name = "audio_spkr1_ovc",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 5),
				.end = AB5500_IRQ(9, 5),
			},
			{
				.name = "audio_plllocked",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 6),
				.end = AB5500_IRQ(9, 6),
			},
			{
				.name = "audio_spkr2_ovc",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 4),
				.end = AB5500_IRQ(17, 4),
			},
		},
	},
	[AB5500_DEVID_USB] = {
		.name = "ab5500-usb",
		.num_resources = 36,
		.resources = (struct resource[]) {
			{
				.name = "Link_Update",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(22, 1),
				.end = AB5500_IRQ(22, 1),
			},
			{
				.name = "DCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 3),
				.end = AB5500_IRQ(8, 4),
			},
			{
				.name = "VBUS_R",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 5),
				.end = AB5500_IRQ(8, 5),
			},
			{
				.name = "VBUS_F",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 6),
				.end = AB5500_IRQ(8, 6),
			},
			{
				.name = "CHGstate_10_PCVBUSchg",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(8, 7),
				.end = AB5500_IRQ(8, 7),
			},
			{
				.name = "DCIOreverse_ovc",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 0),
				.end = AB5500_IRQ(9, 0),
			},
			{
				.name = "USBCharDetDone",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 1),
				.end = AB5500_IRQ(9, 1),
			},
			{
				.name = "DCIO_no_limit",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 2),
				.end = AB5500_IRQ(9, 2),
			},
			{
				.name = "USB_suspend",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 3),
				.end = AB5500_IRQ(9, 3),
			},
			{
				.name = "DCIOreverse_fwdcurrent",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 4),
				.end = AB5500_IRQ(9, 4),
			},
			{
				.name = "Vbus_Imeasmax_change",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(9, 7),
				.end = AB5500_IRQ(9, 7),
			},
			{
				.name = "OVV",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 5),
				.end = AB5500_IRQ(14, 5),
			},
			{
				.name = "USBcharging_NOTok",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 3),
				.end = AB5500_IRQ(15, 3),
			},
			{
				.name = "usb_adp_sensoroff",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 6),
				.end = AB5500_IRQ(15, 6),
			},
			{
				.name = "usb_adp_probeplug",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 7),
				.end = AB5500_IRQ(15, 7),
			},
			{
				.name = "usb_adp_sinkerror",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 0),
				.end = AB5500_IRQ(16, 6),
			},
			{
				.name = "usb_adp_sourceerror",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 1),
				.end = AB5500_IRQ(16, 1),
			},
			{
				.name = "usb_idgnd_r",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 2),
				.end = AB5500_IRQ(16, 2),
			},
			{
				.name = "usb_idgnd_f",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 3),
				.end = AB5500_IRQ(16, 3),
			},
			{
				.name = "usb_iddetR1",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 4),
				.end = AB5500_IRQ(16, 5),
			},
			{
				.name = "usb_iddetR2",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(16, 6),
				.end = AB5500_IRQ(16, 7),
			},
			{
				.name = "usb_iddetR3",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 0),
				.end = AB5500_IRQ(17, 1),
			},
			{
				.name = "usb_iddetR4",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 2),
				.end = AB5500_IRQ(17, 3),
			},
			{
				.name = "CharTempWindowOk",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(17, 7),
				.end = AB5500_IRQ(18, 0),
			},
			{
				.name = "USB_SprDetect",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 1),
				.end = AB5500_IRQ(18, 1),
			},
			{
				.name = "usb_adp_probe_unplug",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 2),
				.end = AB5500_IRQ(18, 2),
			},
			{
				.name = "VBUSChDrop",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 3),
				.end = AB5500_IRQ(18, 4),
			},
			{
				.name = "dcio_char_rec_done",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 5),
				.end = AB5500_IRQ(18, 5),
			},
			{
				.name = "Charging_stopped_by_temp",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(18, 6),
				.end = AB5500_IRQ(18, 6),
			},
			{
				.name = "CHGstate_11_SafeModeVBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 1),
				.end = AB5500_IRQ(21, 1),
			},
			{
				.name = "CHGstate_12_comletedVBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 2),
				.end = AB5500_IRQ(21, 2),
			},
			{
				.name = "CHGstate_13_completedVBUS",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 3),
				.end = AB5500_IRQ(21, 3),
			},
			{
				.name = "CHGstate_14_FullChgDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 4),
				.end = AB5500_IRQ(21, 4),
			},
			{
				.name = "CHGstate_15_SafeModeDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 5),
				.end = AB5500_IRQ(21, 5),
			},
			{
				.name = "CHGstate_16_OFFsuspendDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 6),
				.end = AB5500_IRQ(21, 6),
			},
			{
				.name = "CHGstate_17_completedDCIO",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(21, 7),
				.end = AB5500_IRQ(21, 7),
			},
		},
	},
	[AB5500_DEVID_OTP] = {
		.name = "ab5500-otp",
	},
	[AB5500_DEVID_VIDEO] = {
		.name = "ab5500-video",
		.num_resources = 2,
		.resources = (struct resource[]) {
			{
				.name = "plugTVdet",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(13, 7),
				.end = AB5500_IRQ(13, 7),
			},
			{
				.name = "plugTVdet_removal",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(23, 2),
				.end = AB5500_IRQ(23, 2),
			},

		},
	},
	[AB5500_DEVID_DBIECI] = {
		.name = "ab5500-dbieci",
		.num_resources = 10,
		.resources = (struct resource[]) {
			{
				.name = "COLL",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 0),
				.end = AB5500_IRQ(14, 0),
			},
			{
				.name = "RESERR",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 1),
				.end = AB5500_IRQ(14, 1),
			},
			{
				.name = "FRAERR",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 2),
				.end = AB5500_IRQ(14, 2),
			},
			{
				.name = "COMERR",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 3),
				.end = AB5500_IRQ(14, 3),
			},
			{
				.name = "BSI_indicator",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 4),
				.end = AB5500_IRQ(14, 4),
			},
			{
				.name = "SPDSET",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 6),
				.end = AB5500_IRQ(14, 6),
			},
			{
				.name = "DSENT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(14, 7),
				.end = AB5500_IRQ(14, 7),
			},
			{
				.name = "DREC",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 0),
				.end = AB5500_IRQ(15, 0),
			},
			{
				.name = "ACCINT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 1),
				.end = AB5500_IRQ(15, 1),
			},
			{
				.name = "NOPINT",
				.flags = IORESOURCE_IRQ,
				.start = AB5500_IRQ(15, 2),
				.end = AB5500_IRQ(15, 2),
			},
		},
	},
	[AB5500_DEVID_ONSWA] = {
		.name = "ab5500-onswa",
		.num_resources = 2,
		.resources = (struct resource[]) {
			{
				.name	= "ONSWAn_rising",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(1, 3),
				.end	= AB5500_IRQ(1, 3),
			},
			{
				.name	= "ONSWAn_falling",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(1, 4),
				.end	= AB5500_IRQ(1, 4),
			},
		},
	},
	[AB5500_DEVID_TEMPMON] = {
		.name = "abx500-temp",
		.id = AB5500_DEVID_TEMPMON,
		.num_resources = 1,
		.resources = (struct resource[]) {
			{
				.name   = "ABX500_TEMP_WARM",
				.flags  = IORESOURCE_IRQ,
				.start  = AB5500_IRQ(2, 2),
				.end    = AB5500_IRQ(2, 2),
			},
		},
	},
	[AB5500_DEVID_ACCDET] = {
		.name = "ab5500-acc-det",
		.id = AB5500_DEVID_ACCDET,
		.num_resources = 8,
		.resources = (struct resource[]) {
			{
				.name	= "acc_detedt22db_rising",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(2, 7),
				.end	= AB5500_IRQ(2, 7),
			},
				{
				.name	= "acc_detedt21db_falling",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(2, 6),
				.end	= AB5500_IRQ(2, 6),
			},
			{
				.name	= "acc_detedt21db_rising",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(2, 5),
				.end	= AB5500_IRQ(2, 5),
			},
			{
				.name	= "acc_detedt3db_falling",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(3, 4),
				.end	= AB5500_IRQ(3, 4),
			},
			{
				.name	= "acc_detedt3db_rising",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(3, 3),
				.end	= AB5500_IRQ(3, 3),
			},
			{
				.name	= "acc_detedt1db_falling",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(3, 2),
				.end	= AB5500_IRQ(3, 2),
			},
			{
				.name	= "acc_detedt1db_rising",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(3, 1),
				.end	= AB5500_IRQ(3, 1),
			},
			{
				.name	= "acc_detedt22db_falling",
				.flags	= IORESOURCE_IRQ,
				.start	= AB5500_IRQ(3, 0),
				.end	= AB5500_IRQ(3, 0),
			},
		},
	},
};

int ab5500_get_turn_on_status()
{
	return turn_on_stat;
}

static ssize_t show_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ab5500 *ab5500;

	ab5500 = dev_get_drvdata(dev);
	return sprintf(buf, "%#x\n", ab5500 ? ab5500->chip_id : -EINVAL);
}

static ssize_t show_turn_on_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%#x\n", turn_on_stat);
}

static DEVICE_ATTR(chip_id, S_IRUGO, show_chip_id, NULL);
static DEVICE_ATTR(turn_on_status, S_IRUGO, show_turn_on_status, NULL);

static struct attribute *ab5500_sysfs_entries[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_turn_on_status.attr,
	NULL,
};

static struct attribute_group ab5500_attr_group = {
	.attrs	= ab5500_sysfs_entries,
};

/*
 * Functionality for getting/setting register values.
 */
static int get_register_interruptible(struct ab5500 *ab, u8 bank, u8 reg,
	u8 *value)
{
	int err;

	if (bank >= AB5500_NUM_BANKS)
		return -EINVAL;

	err = mutex_lock_interruptible(&ab->access_mutex);
	if (err)
		return err;
	err = db5500_prcmu_abb_read(bankinfo[bank].slave_addr, reg, value, 1);

	mutex_unlock(&ab->access_mutex);
	return err;
}

static int get_register_page_interruptible(struct ab5500 *ab, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs)
{
	int err;

	if (bank >= AB5500_NUM_BANKS)
		return -EINVAL;

	err = mutex_lock_interruptible(&ab->access_mutex);
	if (err)
		return err;

	while (numregs) {
		/* The hardware limit for get page is 4 */
		u8 curnum = min_t(u8, numregs, 4u);

		err = db5500_prcmu_abb_read(bankinfo[bank].slave_addr,
					    first_reg, regvals, curnum);
		if (err)
			goto out;

		numregs -= curnum;
		first_reg += curnum;
		regvals += curnum;
	}

out:
	mutex_unlock(&ab->access_mutex);
	return err;
}

static int mask_and_set_register_interruptible(struct ab5500 *ab, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	int err = 0;

	if (bank >= AB5500_NUM_BANKS)
		return -EINVAL;

	if (bitmask) {
		u8 buf;

		err = mutex_lock_interruptible(&ab->access_mutex);
		if (err)
			return err;

		if (bitmask == 0xFF) /* No need to read in this case. */
			buf = bitvalues;
		else { /* Read and modify the register value. */
			err = db5500_prcmu_abb_read(bankinfo[bank].slave_addr,
				reg, &buf, 1);
			if (err)
				return err;

			buf = ((~bitmask & buf) | (bitmask & bitvalues));
		}
		/* Write the new value. */
		err = db5500_prcmu_abb_write(bankinfo[bank].slave_addr, reg,
					     &buf, 1);

		mutex_unlock(&ab->access_mutex);
	}
	return err;
}

static int
set_register_interruptible(struct ab5500 *ab, u8 bank, u8 reg, u8 value)
{
	return mask_and_set_register_interruptible(ab, bank, reg, 0xff, value);
}

/*
 * The exported register access functionality.
 */
static int ab5500_get_chip_id(struct device *dev)
{
	struct ab5500 *ab = dev_get_drvdata(dev->parent);

	return (int)ab->chip_id;
}

static int ab5500_mask_and_set_register_interruptible(struct device *dev,
		u8 bank, u8 reg, u8 bitmask, u8 bitvalues)
{
	struct ab5500 *ab;

	ab = dev_get_drvdata(dev->parent);
	return mask_and_set_register_interruptible(ab, bank, reg,
		bitmask, bitvalues);
}

static int ab5500_set_register_interruptible(struct device *dev, u8 bank,
	u8 reg, u8 value)
{
	return ab5500_mask_and_set_register_interruptible(dev, bank, reg, 0xFF,
		value);
}

static int ab5500_get_register_interruptible(struct device *dev, u8 bank,
		u8 reg, u8 *value)
{
	struct ab5500 *ab;

	ab = dev_get_drvdata(dev->parent);
	return get_register_interruptible(ab, bank, reg, value);
}

static int ab5500_get_register_page_interruptible(struct device *dev, u8 bank,
		u8 first_reg, u8 *regvals, u8 numregs)
{
	struct ab5500 *ab;

	ab = dev_get_drvdata(dev->parent);
	return get_register_page_interruptible(ab, bank, first_reg, regvals,
		numregs);
}

static int
ab5500_event_registers_startup_state_get(struct device *dev, u8 *event)
{
	struct ab5500 *ab;

	ab = dev_get_drvdata(dev->parent);
	if (!ab->startup_events_read)
		return -EAGAIN; /* Try again later */

	memcpy(event, ab->startup_events, ab->num_event_reg);
	return 0;
}

static int ab5500_startup_irq_enabled(struct device *dev, unsigned int irq)
{
	struct ab5500 *ab;
	bool val;

	ab = irq_get_chip_data(irq);
	irq -= ab->irq_base;
	val = ((ab->startup_events[irq / 8] & BIT(irq % 8)) != 0);

	return val;
}

static struct abx500_ops ab5500_ops = {
	.get_chip_id = ab5500_get_chip_id,
	.get_register = ab5500_get_register_interruptible,
	.set_register = ab5500_set_register_interruptible,
	.get_register_page = ab5500_get_register_page_interruptible,
	.set_register_page = NULL,
	.mask_and_set_register = ab5500_mask_and_set_register_interruptible,
	.event_registers_startup_state_get =
		ab5500_event_registers_startup_state_get,
	.startup_irq_enabled = ab5500_startup_irq_enabled,
};

static irqreturn_t ab5500_irq(int irq, void *data)
{
	struct ab5500 *ab = data;
	u8 cnt;
	u8 value;
	s8 status, mval;

	for (cnt = 0; cnt < ab->num_master_reg; cnt++) {
		status = get_register_interruptible(ab, AB5500_BANK_IT,
				AB5500_IT_MASTER0_REG + cnt, &mval);
		if (status < 0 || mval == 0)
			continue;
serve_int:
		do {
			int mbit = __ffs(mval);
			int reg = cnt * 8 + mbit;

			status = get_register_interruptible(ab, AB5500_BANK_IT,
				AB5500_IT_LATCH0_REG + reg, &value);
			do {
				int bit = __ffs(value);
				int line = reg * 8 + bit;

				handle_nested_irq(ab->irq_base + line);
				value &= ~(1 << bit);
			} while (value);
			mval &= ~(1 << mbit);
		} while (mval);
	}
	for (cnt = 0; cnt < ab->num_master_reg; cnt++) {
		status = get_register_interruptible(ab, AB5500_BANK_IT,
				AB5500_IT_MASTER0_REG + cnt, &mval);
		if (status < 0 || mval == 0)
			continue;
		else
			goto serve_int;
	}
	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
/**
 * struct ab5500_reg_range
 * @first: the first address of the range
 * @last: the last address of the range
 */
struct ab5500_reg_range {
	u8 first;
	u8 last;
};

/**
 * struct ab5500_i2c_ranges
 * @count: the number of ranges in the list
 * @range: the list of register ranges
 */
struct ab5500_i2c_ranges {
	u8 nranges;
	u8 bankid;
	const struct ab5500_reg_range *range;
};

static struct ab5500_i2c_ranges ab5500v1_reg_ranges[AB5500_NUM_BANKS] = {
	[AB5500_BANK_LED] = {
		.bankid = AB5500_BANK_LED,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0C,
			},
		},
	},
	[AB5500_BANK_ADC] = {
		.bankid = AB5500_BANK_ADC,
		.nranges = 6,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x1F,
				.last = 0x21,
			},
			{
				.first = 0x22,
				.last = 0x24,
			},
			{
				.first = 0x26,
				.last = 0x2D,
			},
			{
				.first = 0x2F,
				.last = 0x34,
			},
			{
				.first = 0x37,
				.last = 0x57,
			},
			{
				.first = 0x58,
				.last = 0x58,
			},
		},
	},
	[AB5500_BANK_RTC] = {
		.bankid = AB5500_BANK_RTC,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x04,
			},
			{
				.first = 0x06,
				.last = 0x0C,
			},
		},
	},
	[AB5500_BANK_STARTUP] = {
		.bankid = AB5500_BANK_STARTUP,
		.nranges = 12,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x01,
			},
			{
				.first = 0x1F,
				.last = 0x1F,
			},
			{
				.first = 0x2E,
				.last = 0x2E,
			},
			{
				.first = 0x2F,
				.last = 0x30,
			},
			{
				.first = 0x50,
				.last = 0x51,
			},
			{
				.first = 0x60,
				.last = 0x61,
			},
			{
				.first = 0x66,
				.last = 0x8A,
			},
			{
				.first = 0x8C,
				.last = 0x96,
			},
			{
				.first = 0xAA,
				.last = 0xB4,
			},
			{
				.first = 0xB7,
				.last = 0xBF,
			},
			{
				.first = 0xC1,
				.last = 0xCA,
			},
			{
				.first = 0xD3,
				.last = 0xE0,
			},
		},
	},
	[AB5500_BANK_DBI_ECI] = {
		.bankid = AB5500_BANK_DBI_ECI,
		.nranges = 3,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x07,
			},
			{
				.first = 0x10,
				.last = 0x10,
			},
			{
				.first = 0x13,
				.last = 0x13,
			},
		},
	},
	[AB5500_BANK_CHG] = {
		.bankid = AB5500_BANK_CHG,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x11,
				.last = 0x11,
			},
			{
				.first = 0x12,
				.last = 0x1B,
			},
		},
	},
	[AB5500_BANK_FG_BATTCOM_ACC] = {
		.bankid = AB5500_BANK_FG_BATTCOM_ACC,
		.nranges = 5,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0B,
			},
			{
				.first = 0x0C,
				.last = 0x10,
			},
			{
				.first = 0x1A,
				.last = 0x1D,
			},
			{
				.first = 0x20,
				.last = 0x21,
			},
			{
				.first = 0x23,
				.last = 0x24,
			},

		},
	},
	[AB5500_BANK_USB] = {
		.bankid = AB5500_BANK_USB,
		.nranges = 13,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x01,
			},
			{
				.first = 0x80,
				.last = 0x80,
			},
			{
				.first = 0x81,
				.last = 0x83,
			},
			{
				.first = 0x87,
				.last = 0x8A,
			},
			{
				.first = 0x8B,
				.last = 0x8B,
			},
			{
				.first = 0x91,
				.last = 0x92,
			},
			{
				.first = 0x93,
				.last = 0x93,
			},
			{
				.first = 0x94,
				.last = 0x94,
			},
			{
				.first = 0xA8,
				.last = 0xB0,
			},
			{
				.first = 0xB2,
				.last = 0xB2,
			},
			{
				.first = 0xB4,
				.last = 0xBC,
			},
			{
				.first = 0xBF,
				.last = 0xBF,
			},
			{
				.first = 0xC1,
				.last = 0xC5,
			},
		},
	},
	[AB5500_BANK_IT] = {
		.bankid = AB5500_BANK_IT,
		.nranges = 4,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x02,
			},
			{
				.first = 0x20,
				.last = 0x36,
			},
			{
				.first = 0x40,
				.last = 0x56,
			},
			{
				.first = 0x60,
				.last = 0x76,
			},
		},
	},
	[AB5500_BANK_VDDDIG_IO_I2C_CLK_TST] = {
		.bankid = AB5500_BANK_VDDDIG_IO_I2C_CLK_TST,
		.nranges = 7,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x02,
				.last = 0x02,
			},
			{
				.first = 0x12,
				.last = 0x12,
			},
			{
				.first = 0x30,
				.last = 0x34,
			},
			{
				.first = 0x40,
				.last = 0x44,
			},
			{
				.first = 0x50,
				.last = 0x54,
			},
			{
				.first = 0x60,
				.last = 0x64,
			},
			{
				.first = 0x70,
				.last = 0x74,
			},
		},
	},
	[AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP] = {
		.bankid = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.nranges = 14,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x01,
			},
			{
				.first = 0x02,
				.last = 0x02,
			},
			{
				.first = 0x0D,
				.last = 0x0D,
			},
			{
				.first = 0x0E,
				.last = 0x0E,
			},
			{
				.first = 0x1C,
				.last = 0x1C,
			},
			{
				.first = 0x1E,
				.last = 0x1E,
			},
			{
				.first = 0x20,
				.last = 0x21,
			},
			{
				.first = 0x28,
				.last = 0x28,
			},
			{
				.first = 0x30,
				.last = 0x33,
			},
			{
				.first = 0x40,
				.last = 0x43,
			},
			{
				.first = 0x50,
				.last = 0x53,
			},
			{
				.first = 0x60,
				.last = 0x63,
			},
			{
				.first = 0x70,
				.last = 0x73,
			},
			{
				.first = 0xB1,
				.last = 0xB1,
			},
		},
	},
	[AB5500_BANK_VIBRA] = {
		.bankid = AB5500_BANK_VIBRA,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x10,
				.last = 0x13,
			},
		},
	},
	[AB5500_BANK_AUDIO_HEADSETUSB] = {
		.bankid = AB5500_BANK_AUDIO_HEADSETUSB,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x47,
			},
		},
	},
	[AB5500_BANK_SIM_USBSIM] = {
		.bankid = AB5500_BANK_SIM_USBSIM,
		.nranges = 2,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x13,
				.last = 0x19,
			},
			{
				.first = 0x20,
				.last = 0x20,
			},
		},
	},
	[AB5500_BANK_VDENC] = {
		.bankid = AB5500_BANK_VDENC,
		.nranges = 12,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x08,
			},
			{
				.first = 0x09,
				.last = 0x09,
			},
			{
				.first = 0x0A,
				.last = 0x12,
			},
			{
				.first = 0x15,
				.last = 0x19,
			},
			{
				.first = 0x1B,
				.last = 0x21,
			},
			{
				.first = 0x27,
				.last = 0x2C,
			},
			{
				.first = 0x41,
				.last = 0x41,
			},
			{
				.first = 0x45,
				.last = 0x5B,
			},
			{
				.first = 0x5D,
				.last = 0x5D,
			},
			{
				.first = 0x69,
				.last = 0x69,
			},
			{
				.first = 0x6C,
				.last = 0x6D,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
};

static struct ab5500_i2c_ranges ab5500v2_reg_ranges[AB5500_NUM_BANKS] = {
	[AB5500_BANK_LED] = {
		.bankid = AB5500_BANK_LED,
		.nranges = 3,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x02,
			},
			{
				.first = 0x04,
				.last = 0x0D,
			},
			{
				.first = 0x10,
				.last = 0x1B,
			},
		},
	},
	[AB5500_BANK_ADC] = {
		.bankid = AB5500_BANK_ADC,
		.nranges = 4,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x1F,
				.last = 0x24,
			},
			{
				.first = 0x26,
				.last = 0x35,
			},
			{
				.first = 0x37,
				.last = 0x57,
			},
			{
				.first = 0xA0,
				.last = 0xA5,
			},
		},
	},
	[AB5500_BANK_RTC] = {
		.bankid = AB5500_BANK_RTC,
		.nranges = 3,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x04,
			},
			{
				.first = 0x06,
				.last = 0x0B,
			},
			{
				.first = 0x20,
				.last = 0x21,
			},
		},
	},
	[AB5500_BANK_STARTUP] = {
		.bankid = AB5500_BANK_STARTUP,
		.nranges = 14,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x01,
			},
			{
				.first = 0x1F,
				.last = 0x1F,
			},
			{
				.first = 0x2E,
				.last = 0x31,
			},
			{
				.first = 0x50,
				.last = 0x51,
			},
			{
				.first = 0x60,
				.last = 0x61,
			},
			{
				.first = 0x66,
				.last = 0x8A,
			},
			{
				.first = 0x8C,
				.last = 0x96,
			},
			{
				.first = 0xAA,
				.last = 0xB5,
			},
			{
				.first = 0xB7,
				.last = 0xBF,
			},
			{
				.first = 0xC1,
				.last = 0xCA,
			},
			{
				.first = 0xD3,
				.last = 0xE0,
			},
			{
				.first = 0xEA,
				.last = 0xEA,
			},
			{
				.first = 0xF0,
				.last = 0xF0,
			},
			{
				.first = 0xF6,
				.last = 0xF6,
			},
		},
	},
	[AB5500_BANK_DBI_ECI] = {
		.bankid = AB5500_BANK_DBI_ECI,
		.nranges = 3,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x07,
			},
			{
				.first = 0x10,
				.last = 0x10,
			},
			{
				.first = 0x13,
				.last = 0x13,
			},
		},
	},
	[AB5500_BANK_CHG] = {
		.bankid = AB5500_BANK_CHG,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x11,
				.last = 0x1D,
			},
		},
	},
	[AB5500_BANK_FG_BATTCOM_ACC] = {
		.bankid = AB5500_BANK_FG_BATTCOM_ACC,
		.nranges = 5,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x10,
			},
			{
				.first = 0x1A,
				.last = 0x1D,
			},
			{
				.first = 0x20,
				.last = 0x20,
			},
			{
				.first = 0x24,
				.last = 0x24,
			},
			{
				.first = 0x30,
				.last = 0x37,
			},

		},
	},
	[AB5500_BANK_USB] = {
		.bankid = AB5500_BANK_USB,
		.nranges = 5,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x01,
			},
			{
				.first = 0x80,
				.last = 0x83,
			},
			{
				.first = 0x87,
				.last = 0x8B,
			},
			{
				.first = 0x91,
				.last = 0x94,
			},
			{
				.first = 0x98,
				.last = 0x9A,
			},
		},
	},
	[AB5500_BANK_IT] = {
		.bankid = AB5500_BANK_IT,
		.nranges = 4,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x03,
			},
			{
				.first = 0x20,
				.last = 0x38,
			},
			{
				.first = 0x40,
				.last = 0x58,
			},
			{
				.first = 0x60,
				.last = 0x78,
			},
		},
	},
	[AB5500_BANK_VDDDIG_IO_I2C_CLK_TST] = {
		.bankid = AB5500_BANK_VDDDIG_IO_I2C_CLK_TST,
		.nranges = 7,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x02,
				.last = 0x02,
			},
			{
				.first = 0x12,
				.last = 0x12,
			},
			{
				.first = 0x30,
				.last = 0x34,
			},
			{
				.first = 0x40,
				.last = 0x44,
			},
			{
				.first = 0x50,
				.last = 0x54,
			},
			{
				.first = 0x60,
				.last = 0x64,
			},
			{
				.first = 0x70,
				.last = 0x74,
			},
		},
	},
	[AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP] = {
		.bankid = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		.nranges = 17,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x01,
				.last = 0x02,
			},
			{
				.first = 0x0D,
				.last = 0x0F,
			},
			{
				.first = 0x19,
				.last = 0x1C,
			},
			{
				.first = 0x1E,
				.last = 0x1E,
			},
			{
				.first = 0x20,
				.last = 0x20,
			},
			{
				.first = 0x28,
				.last = 0x28,
			},
			{
				.first = 0x30,
				.last = 0x33,
			},
			{
				.first = 0x35,
				.last = 0x35,
			},
			{
				.first = 0x40,
				.last = 0x43,
			},
			{
				.first = 0x45,
				.last = 0x45,
			},
			{
				.first = 0x50,
				.last = 0x53,
			},
			{
				.first = 0x55,
				.last = 0x55,
			},
			{
				.first = 0x60,
				.last = 0x63,
			},
			{
				.first = 0x65,
				.last = 0x65,
			},
			{
				.first = 0x70,
				.last = 0x73,
			},
			{
				.first = 0x75,
				.last = 0x75,
			},
			{
				.first = 0xB1,
				.last = 0xB1,
			},
		},
	},
	[AB5500_BANK_VIBRA] = {
		.bankid = AB5500_BANK_VIBRA,
		.nranges = 1,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x10,
				.last = 0x13,
			},
		},
	},
	[AB5500_BANK_AUDIO_HEADSETUSB] = {
		.bankid = AB5500_BANK_AUDIO_HEADSETUSB,
		.nranges = 4,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x14,
			},
			{
				.first = 0x16,
				.last = 0x26,
			},
			{
				.first = 0x28,
				.last = 0x30,
			},
			{
				.first = 0x35,
				.last = 0x47,
			},

		},
	},
	[AB5500_BANK_SIM_USBSIM] = {
		.bankid = AB5500_BANK_SIM_USBSIM,
		.nranges = 3,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x13,
				.last = 0x1A,
			},
			{
				.first = 0x20,
				.last = 0x20,
			},
			{
				.first = 0xFE,
				.last = 0xFE,
			},
		},
	},
	[AB5500_BANK_VDENC] = {
		.bankid = AB5500_BANK_VDENC,
		.nranges = 10,
		.range = (struct ab5500_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x12,
			},
			{
				.first = 0x15,
				.last = 0x19,
			},
			{
				.first = 0x1B,
				.last = 0x21,
			},
			{
				.first = 0x27,
				.last = 0x2C,
			},
			{
				.first = 0x41,
				.last = 0x41,
			},
			{
				.first = 0x45,
				.last = 0x5B,
			},
			{
				.first = 0x5D,
				.last = 0x5D,
			},
			{
				.first = 0x69,
				.last = 0x69,
			},
			{
				.first = 0x6C,
				.last = 0x6D,
			},
			{
				.first = 0x80,
				.last = 0x81,
			},
		},
	},
};
static int ab5500_registers_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;
	unsigned int i;
	u8 bank = (u8)ab->debug_bank;
	struct ab5500_i2c_ranges *ab5500_reg_ranges;

	if (ab->chip_id >= AB5500_2_0)
		ab5500_reg_ranges = ab5500v2_reg_ranges;
	else
		ab5500_reg_ranges = ab5500v1_reg_ranges;
	seq_printf(s, AB5500_NAME_STRING " register values:\n");
	for (bank = 0; bank < AB5500_NUM_BANKS; bank++) {
		seq_printf(s, " bank %u, %s (0x%x):\n", bank,
				bankinfo[bank].name,
				bankinfo[bank].slave_addr);
		for (i = 0; i < ab5500_reg_ranges[bank].nranges; i++) {
			u8 reg;
			int err;

			for (reg = ab5500_reg_ranges[bank].range[i].first;
				reg <= ab5500_reg_ranges[bank].range[i].last;
				reg++) {
				u8 value;

				err = get_register_interruptible(ab, bank, reg,
						&value);
				if (err < 0) {
					dev_err(ab->dev, "get_reg failed %d"
						"bank 0x%x reg 0x%x\n",
						err, bank, reg);
					return err;
				}

				err = seq_printf(s, "[%d/0x%02X]: 0x%02X\n",
						bank, reg, value);
				if (err < 0) {
					/*
					 * Error is not returned here since
					 * the output is wanted in any case
					 */
					return 0;
				}
			}
		}
	}
	return 0;
}

static int ab5500_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_registers_print, inode->i_private);
}

static const struct file_operations ab5500_registers_fops = {
	.open = ab5500_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab5500_bank_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;

	seq_printf(s, "%d\n", ab->debug_bank);
	return 0;
}

static int ab5500_bank_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_bank_print, inode->i_private);
}

static ssize_t ab5500_bank_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab5500 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_bank;
	int err;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_bank);
	if (err)
		return -EINVAL;

	if (user_bank >= AB5500_NUM_BANKS) {
		dev_err(ab->dev,
			"debugfs error input > number of banks\n");
		return -EINVAL;
	}

	ab->debug_bank = user_bank;

	return buf_size;
}

static int ab5500_address_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;

	seq_printf(s, "0x%02X\n", ab->debug_address);
	return 0;
}

static int ab5500_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_address_print, inode->i_private);
}

static ssize_t ab5500_address_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab5500 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_address;
	int err;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_address);
	if (err)
		return -EINVAL;
	if (user_address > 0xff) {
		dev_err(ab->dev,
			"debugfs error input > 0xff\n");
		return -EINVAL;
	}
	ab->debug_address = user_address;
	return buf_size;
}

static int ab5500_val_print(struct seq_file *s, void *p)
{
	struct ab5500 *ab = s->private;
	int err;
	u8 regvalue;

	err = get_register_interruptible(ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, &regvalue);
	if (err) {
		dev_err(ab->dev, "get_reg failed %d, bank 0x%x"
			", reg 0x%x\n", err, ab->debug_bank,
			ab->debug_address);
		return -EINVAL;
	}
	seq_printf(s, "0x%02X\n", regvalue);

	return 0;
}

static int ab5500_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab5500_val_print, inode->i_private);
}

static ssize_t ab5500_val_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab5500 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;
	u8 regvalue;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;
	if (user_val > 0xff) {
		dev_err(ab->dev,
			"debugfs error input > 0xff\n");
		return -EINVAL;
	}
	err = mask_and_set_register_interruptible(
		ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, 0xFF, (u8)user_val);
	if (err)
		return -EINVAL;

	get_register_interruptible(ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, &regvalue);
	if (err)
		return -EINVAL;

	return buf_size;
}

static const struct file_operations ab5500_bank_fops = {
	.open = ab5500_bank_open,
	.write = ab5500_bank_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab5500_address_fops = {
	.open = ab5500_address_open,
	.write = ab5500_address_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab5500_val_fops = {
	.open = ab5500_val_open,
	.write = ab5500_val_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab5500_dir;
static struct dentry *ab5500_reg_file;
static struct dentry *ab5500_bank_file;
static struct dentry *ab5500_address_file;
static struct dentry *ab5500_val_file;

static inline void ab5500_setup_debugfs(struct ab5500 *ab)
{
	ab->debug_bank = AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP;
	ab->debug_address = AB5500_CHIP_ID;

	ab5500_dir = debugfs_create_dir(AB5500_NAME_STRING, NULL);
	if (!ab5500_dir)
		goto exit_no_debugfs;

	ab5500_reg_file = debugfs_create_file("all-bank-registers",
		S_IRUGO, ab5500_dir, ab, &ab5500_registers_fops);
	if (!ab5500_reg_file)
		goto exit_destroy_dir;

	ab5500_bank_file = debugfs_create_file("register-bank",
		(S_IRUGO | S_IWUGO), ab5500_dir, ab, &ab5500_bank_fops);
	if (!ab5500_bank_file)
		goto exit_destroy_reg;

	ab5500_address_file = debugfs_create_file("register-address",
		(S_IRUGO | S_IWUGO), ab5500_dir, ab, &ab5500_address_fops);
	if (!ab5500_address_file)
		goto exit_destroy_bank;

	ab5500_val_file = debugfs_create_file("register-value",
		(S_IRUGO | S_IWUGO), ab5500_dir, ab, &ab5500_val_fops);
	if (!ab5500_val_file)
		goto exit_destroy_address;

	return;

exit_destroy_address:
	debugfs_remove(ab5500_address_file);
exit_destroy_bank:
	debugfs_remove(ab5500_bank_file);
exit_destroy_reg:
	debugfs_remove(ab5500_reg_file);
exit_destroy_dir:
	debugfs_remove(ab5500_dir);
exit_no_debugfs:
	dev_err(ab->dev, "failed to create debugfs entries.\n");
	return;
}

static inline void ab5500_remove_debugfs(void)
{
	debugfs_remove(ab5500_val_file);
	debugfs_remove(ab5500_address_file);
	debugfs_remove(ab5500_bank_file);
	debugfs_remove(ab5500_reg_file);
	debugfs_remove(ab5500_dir);
}

#else /* !CONFIG_DEBUG_FS */
static inline void ab5500_setup_debugfs(struct ab5500 *ab)
{
}
static inline void ab5500_remove_debugfs(void)
{
}
#endif

/*
 * ab5500_setup : Basic set-up, datastructure creation/destruction
 *		  and I2C interface.This sets up a default config
 *		  in the AB5500 chip so that it will work as expected.
 * @ab :	  Pointer to ab5500 structure
 * @settings :    Pointer to struct abx500_init_settings
 * @size :        Size of init data
 */
static int __init ab5500_setup(struct ab5500 *ab,
	struct abx500_init_settings *settings, unsigned int size)
{
	int err = 0;
	int i;

	for (i = 0; i < size; i++) {
		err = mask_and_set_register_interruptible(ab,
			settings[i].bank,
			settings[i].reg,
			0xFF, settings[i].setting);
		if (err)
			goto exit_no_setup;

		/* If event mask register update the event mask in ab5500 */
		if ((settings[i].bank == AB5500_BANK_IT) &&
			(AB5500_MASK_BASE <= settings[i].reg) &&
			(settings[i].reg <= AB5500_MASK_END)) {
			ab->mask[settings[i].reg - AB5500_MASK_BASE] =
				settings[i].setting;
		}
	}
exit_no_setup:
	return err;
}

static void ab5500_irq_mask(struct irq_data *data)
{
	struct ab5500 *ab = irq_data_get_irq_chip_data(data);
	int offset = data->irq - ab->irq_base;
	int index = offset / 8;
	int mask = BIT(offset % 8);

	ab->mask[index] |= mask;
}

static void ab5500_irq_unmask(struct irq_data *data)
{
	struct ab5500 *ab = irq_data_get_irq_chip_data(data);
	int offset = data->irq - ab->irq_base;
	int index = offset / 8;
	int mask = BIT(offset % 8);

	ab->mask[index] &= ~mask;
}

static void ab5500_irq_lock(struct irq_data *data)
{
	struct ab5500 *ab = irq_data_get_irq_chip_data(data);

	mutex_lock(&ab->irq_lock);
}

static void ab5500_irq_sync_unlock(struct irq_data *data)
{
	struct ab5500 *ab = irq_data_get_irq_chip_data(data);
	int i;

	for (i = 0; i < ab->num_event_reg; i++) {
		u8 old = ab->oldmask[i];
		u8 new = ab->mask[i];
		int reg;

		if (new == old)
			continue;

		ab->oldmask[i] = new;

		reg = AB5500_IT_MASK0_REG + i;
		set_register_interruptible(ab, AB5500_BANK_IT, reg, new);
	}

	mutex_unlock(&ab->irq_lock);
}

static struct irq_chip ab5500_irq_chip = {
	.name			= "ab5500",
	.irq_mask		= ab5500_irq_mask,
	.irq_unmask		= ab5500_irq_unmask,
	.irq_bus_lock		= ab5500_irq_lock,
	.irq_bus_sync_unlock	= ab5500_irq_sync_unlock,
};

struct ab_family_id {
	u8	id;
	char	*name;
};

static const struct ab_family_id ids[] __initdata = {
	/* AB5500 */
	{
		.id = AB5500_1_0,
		.name = "1.0"
	},
	{
		.id = AB5500_1_1,
		.name = "1.1"
	},
	{
		.id = AB5500_2_0,
		.name = "2.0"
	},
	{
		.id = AB5500_2_1,
		.name = "2.1"
	},
	/* Terminator */
	{
		.id = 0x00,
	}
};

static int ab5500_irq_init(struct ab5500 *ab)
{
	struct ab5500_platform_data *ab5500_plf_data =
				dev_get_platdata(ab->dev);
	int i;
	unsigned int irq;

	for (i = 0; i < ab5500_plf_data->irq.count; i++) {

		irq = ab5500_plf_data->irq.base + i;
		irq_set_chip_data(irq, ab);
		irq_set_chip_and_handler(irq, &ab5500_irq_chip,
			handle_simple_irq);
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		set_irq_noprobe(irq);
#endif
	}
	return 0;
}

static void ab5500_irq_remove(struct ab5500 *ab)
{
	struct ab5500_platform_data *ab5500_plf_data =
				dev_get_platdata(ab->dev);
	int i;
	unsigned int irq;

	for (i = 0; i < ab5500_plf_data->irq.count; i++) {
		irq = ab5500_plf_data->irq.base + i;
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip_and_handler(irq, NULL, NULL);
		irq_set_chip_data(irq, NULL);
	}
}

static int __init ab5500_probe(struct platform_device *pdev)
{
	struct ab5500 *ab;
	struct ab5500_platform_data *ab5500_plf_data =
		pdev->dev.platform_data;
	struct resource *res;
	int err;
	int i;
	u8 val;

	ab = kzalloc(sizeof(struct ab5500), GFP_KERNEL);
	if (!ab) {
		dev_err(&pdev->dev,
			"could not allocate " AB5500_NAME_STRING " device\n");
		return -ENOMEM;
	}

	/* Initialize data structure */
	mutex_init(&ab->access_mutex);
	mutex_init(&ab->irq_lock);
	ab->dev = &pdev->dev;
	ab->irq_base = ab5500_plf_data->irq.base;

	platform_set_drvdata(pdev, ab);

	/* Read chip ID register */
	err = get_register_interruptible(ab, AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP,
		AB5500_CHIP_ID, &ab->chip_id);
	if (err) {
		dev_err(&pdev->dev, "could not communicate with the analog "
			"baseband chip\n");
		goto exit_no_detect;
	}

	for (i = 0; ids[i].id != 0x0; i++) {
		if (ids[i].id == ab->chip_id) {
			snprintf(&ab->chip_name[0], sizeof(ab->chip_name) - 1,
				AB5500_ID_FORMAT_STRING, ids[i].name);
			break;
		}
	}

	if (ids[i].id == 0x0) {
		dev_err(&pdev->dev, "unknown analog baseband chip id: 0x%x\n",
			ab->chip_id);
		dev_err(&pdev->dev, "driver not started!\n");
		goto exit_no_detect;
	}

	dev_info(&pdev->dev, "detected AB chip: %s\n", &ab->chip_name[0]);

	/* Readout ab->starup_events when prcmu driver is in place */
	ab->startup_events[0] = 0;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "ab5500_platform_get_resource error\n");
		goto exit_no_detect;
	}
	ab->ab5500_irq = res->start;

	if (ab->chip_id >= AB5500_2_0)
		ab->num_event_reg = AB5500_NUM_IRQ_REGS;
	else
		ab->num_event_reg = AB5500_NUM_EVENT_V1_REG;
	ab->num_master_reg = AB5500_NUM_MASTER_REGS;
	/* Read the latch regs to know the reason for turn on */
	err = get_register_interruptible(ab, AB5500_BANK_IT,
			AB5500_IT_LATCH0_REG + 1, &val);
	if (err)
		goto exit_no_detect;
	if (val & RTC_ALARM) /* RTCAlarm */
		turn_on_stat = RTC_ALARM_EVENT;
	if (val & POW_KEY_2_ON) /* PonKey2dbF */
		turn_on_stat |= P_ON_KEY2_EVENT;
	if (val & POW_KEY_1_ON) /* PonKey1dbF */
		turn_on_stat |= P_ON_KEY1_EVENT;

	err = get_register_interruptible(ab, AB5500_BANK_IT,
			AB5500_IT_LATCH0_REG + 2, &val);
	if (err)
		goto exit_no_detect;
	if (val & POR_ON_VBAT)
		/* PORnVbat */
		turn_on_stat |= POR_ON_VBAT_EVENT ;
	err = get_register_interruptible(ab, AB5500_BANK_IT,
			AB5500_IT_LATCH0_REG + 8, &val);
	if (err)
		goto exit_no_detect;
	if (val & VBUS_DET)
		/* VbusDet */
		turn_on_stat |= VBUS_DET_EVENT;
	err = get_register_interruptible(ab, AB5500_BANK_IT,
			AB5500_IT_LATCH0_REG + 18, &val);
	if (err)
		goto exit_no_detect;
	if (val & VBUS_CH_DROP_R)
		/* VBUSChDrop */
		turn_on_stat |= VBUS_DET_EVENT;
	err = get_register_interruptible(ab, AB5500_BANK_IT,
			AB5500_IT_LATCH0_REG + 9, &val);
	if (err)
		goto exit_no_detect;
	if (val & USB_CH_DET_DONE)
		/* VBUSChDrop */
		turn_on_stat |= VBUS_DET_EVENT;
	err = get_register_interruptible(ab, AB5500_BANK_IT,
			AB5500_IT_LATCH0_REG + 22, &val);
	if (err)
		goto exit_no_detect;
	if (val & USB_CH_DET_DONE)
		/* USBLineStatus Change */
		turn_on_stat |= VBUS_DET_EVENT;

	/* Clear and mask all interrupts */
	for (i = 0; i < ab->num_event_reg; i++) {
		u8 latchreg = AB5500_IT_LATCH0_REG + i;
		u8 maskreg = AB5500_IT_MASK0_REG + i;

		get_register_interruptible(ab, AB5500_BANK_IT, latchreg, &val);
		set_register_interruptible(ab, AB5500_BANK_IT, maskreg, 0xff);
		ab->mask[i] = ab->oldmask[i] = 0xff;
	}

	if (ab->irq_base) {
		err = ab5500_irq_init(ab);
		if (err)
			return err;

	err = request_threaded_irq(res->start, NULL, ab5500_irq,
				   IRQF_NO_SUSPEND | IRQF_ONESHOT,
				   "ab5500-core", ab);
		if (err)
			goto exit_remove_irq;

	}
	/* This real unpredictable IRQ is of course sampled for entropy */
	rand_initialize_irq(res->start);

	err = abx500_register_ops(&pdev->dev, &ab5500_ops);
	if (err) {
		dev_err(&pdev->dev, "ab5500_register ops error\n");
		goto exit_no_irq;
	}

	/* Set up and register the platform devices. */
	for (i = 0; i < AB5500_NUM_DEVICES; i++) {
		ab5500_devs[i].platform_data = ab5500_plf_data->dev_data[i];
		ab5500_devs[i].pdata_size = ab5500_plf_data->dev_data_sz[i];
	}

	err = mfd_add_devices(&pdev->dev, 0, ab5500_devs,
		ARRAY_SIZE(ab5500_devs), NULL,
		ab5500_plf_data->irq.base);
	if (err) {
		dev_err(&pdev->dev, "ab5500_mfd_add_device error\n");
		goto exit_no_add_dev;
	}
	err = ab5500_setup(ab, ab5500_plf_data->init_settings,
		ab5500_plf_data->init_settings_sz);
	if (err) {
		dev_err(&pdev->dev, "ab5500_setup error\n");
		goto exit_no_add_dev;
	}
	ab5500_setup_debugfs(ab);
	err = sysfs_create_group(&ab->dev->kobj, &ab5500_attr_group);
	if (err) {
		dev_err(&pdev->dev, "error creating sysfs entries\n");
		goto exit_no_debugfs;
	}

	return 0;
exit_no_debugfs:
	ab5500_remove_debugfs();
exit_no_add_dev:
	mfd_remove_devices(&pdev->dev);
exit_no_irq:
	if (ab->irq_base) {
		free_irq(ab->ab5500_irq, ab);
exit_remove_irq:
	ab5500_irq_remove(ab);
	}
exit_no_detect:
	kfree(ab);
	return err;
}

static int __exit ab5500_remove(struct platform_device *pdev)
{
	struct ab5500 *ab = platform_get_drvdata(pdev);

	/*
	 * At this point, all subscribers should have unregistered
	 * their notifiers so deactivate IRQ
	 */
	sysfs_remove_group(&ab->dev->kobj, &ab5500_attr_group);
	ab5500_remove_debugfs();
	mfd_remove_devices(&pdev->dev);
	if (ab->irq_base) {
		free_irq(ab->ab5500_irq, ab);
		ab5500_irq_remove(ab);
	}
	kfree(ab);
	return 0;
}

static struct platform_driver ab5500_driver = {
	.driver = {
		.name = "ab5500-core",
		.owner = THIS_MODULE,
	},
	.remove  = __exit_p(ab5500_remove),
};

static int __init ab5500_core_init(void)
{
	return platform_driver_probe(&ab5500_driver, ab5500_probe);
}

static void __exit ab5500_core_exit(void)
{
	platform_driver_unregister(&ab5500_driver);
}

subsys_initcall(ab5500_core_init);
module_exit(ab5500_core_exit);

MODULE_AUTHOR("Mattias Wallin <mattias.wallin@stericsson.com>");
MODULE_DESCRIPTION("AB5500 core driver");
MODULE_LICENSE("GPL");
