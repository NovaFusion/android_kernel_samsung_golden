/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/string.h>

#include <plat/gpio-nomadik.h>
#include <mach/hardware.h>

#include "board-pins-sleep-force.h"
#include "pins-db8500.h"
#include "pins.h"

static u32 u8500_gpio_banks[] = {U8500_GPIOBANK0_BASE,
				 U8500_GPIOBANK1_BASE,
				 U8500_GPIOBANK2_BASE,
				 U8500_GPIOBANK3_BASE,
				 U8500_GPIOBANK4_BASE,
				 U8500_GPIOBANK5_BASE,
				 U8500_GPIOBANK6_BASE,
				 U8500_GPIOBANK7_BASE,
				 U8500_GPIOBANK8_BASE};

/*
 * This function is called to force gpio power save
 * settings during suspend.
 */
void sleep_pins_config_pm(pin_cfg_t *cfgs, int num)
{
	int i = 0;
	int gpio = 0;
	u32 w_imsc = 0;
	u32 imsc = 0;
	u32 offset;
	u32 bitmask = 1;
	u32 dirs_register = 0;
	u32 dirc_register = 0;
	u32 dats_register = 0;
	u32 datc_register = 0;
	u32 pdis_register_disable = 0;
	u32 pdis_register_enabled = 0;
	u32 slpm_register_disabled = 0;
	u32 slpm_register_enabled = 0;
	u32 bankaddr = 0;

	gpio = PIN_NUM(cfgs[i]);

	/* Get the bank number the pin is mapped to */
	bankaddr = IO_ADDRESS(u8500_gpio_banks[(gpio >> GPIO_BLOCK_SHIFT)]);

	w_imsc = readl(bankaddr + NMK_GPIO_RWIMSC) |
	readl(bankaddr + NMK_GPIO_FWIMSC);

	imsc = readl(bankaddr + NMK_GPIO_RIMSC) |
	readl(bankaddr + NMK_GPIO_FIMSC);

	for (i = 0; i < num; i++) {
		/* Get the pin number  */
		gpio = PIN_NUM(cfgs[i]);

		/* get the offest into the register */
		offset = gpio % NMK_GPIO_PER_CHIP;
		/* Set the bit to toggle */
		bitmask = 1 << offset ;

		/* Next we check for direction (INPUT/OUTPUT) */
		switch (PIN_SLPM_DIR(cfgs[i])) {
		case GPIO_IS_INPUT:
			/* GPIO is set to input */
			dirc_register |= bitmask;

			/*
			 * Next check for pull (PULLUP/PULLDOWN)
			 * and configure accordingly.
			 */
			switch (PIN_SLPM_PULL(cfgs[i])) {
			case GPIO_PULL_UPDOWN_DISABLED:
				pdis_register_disable |= bitmask;
				break;

			case GPIO_IS_PULLUP:
				dats_register |= bitmask;
				pdis_register_enabled |= bitmask;
				break;

			case GPIO_IS_PULLDOWN:
				datc_register |= bitmask;
				pdis_register_enabled |= bitmask;
				break;

			case GPIO_PULL_NO_CHANGE:
				break;

			default:
				BUG();
				break;

			}
			break;

		case GPIO_IS_OUTPUT:
			/* GPIO is set to output */
			dirs_register |= bitmask;

			/*
			 * Since its output there should not
			 * be a need to disable  PULL UP/DOWN
			 * but better safe than sorry.
			 */
			pdis_register_disable |= bitmask;
			/* Next we check for setting GPIO HIGH/LOW */
			switch (PIN_SLPM_VAL(cfgs[i])) {
			case GPIO_IS_OUTPUT_LOW:
				/* GPIO is set to LOW */
				datc_register |= bitmask;
				break;

			case GPIO_IS_OUTPUT_HIGH:
				/* GPIO is set to high */
				dats_register |= bitmask;
				break;

			case GPIO_IS_NO_CHANGE:
				break;

			default:
				BUG();
				break;
			}

			break;
		case GPIO_IS_NOT_CHANGED:
			break;

		default:
			BUG();
			break;
		}

		/* Next check for Sleep Power Managment (SLPM) */
		switch (PIN_SLPM(cfgs[i])) {
		case GPIO_WAKEUP_IS_ENABLED:
			slpm_register_enabled |= bitmask;
			break;

		case GPIO_WAKEUP_IS_DISBLED:
			slpm_register_disabled |= bitmask;
			break;

		default:
			BUG();
			break;
		}

		/* Next check for Sleep Power Managment (SLPM) */
		switch (PIN_SLPM_PDIS(cfgs[i])) {
		case GPIO_PDIS_NO_CHANGE:
			break;

		case GPIO_PDIS_DISABLED:
			pdis_register_disable |= bitmask;
			break;

		case GPIO_PDIS_ENABLED:
			pdis_register_enabled |= bitmask;
			break;

		default:
			BUG();
			break;
		}

	}

	/* Write the register settings GPIO direction */
	writel(dirs_register & ~w_imsc, bankaddr + NMK_GPIO_DIRS);
	writel(dirc_register, bankaddr + NMK_GPIO_DIRC);

	writel(datc_register & ~w_imsc, bankaddr + NMK_GPIO_DATC);
	writel(dats_register & ~w_imsc, bankaddr + NMK_GPIO_DATS);

	/* Write the PDIS enable/disable */
	writel(readl(bankaddr + NMK_GPIO_PDIS)
		| (pdis_register_disable & ~w_imsc & ~imsc),
	       bankaddr + NMK_GPIO_PDIS);
	writel(readl(bankaddr + NMK_GPIO_PDIS)
		& (~pdis_register_enabled & ~w_imsc & ~imsc),
	       bankaddr + NMK_GPIO_PDIS);

	/* Write the SLPM enable/disable */
	writel(readl(bankaddr + NMK_GPIO_SLPC) | slpm_register_disabled,
	       bankaddr + NMK_GPIO_SLPC);
	writel(readl(bankaddr + NMK_GPIO_SLPC) & ~slpm_register_enabled,
	       bankaddr + NMK_GPIO_SLPC);
}

void sleep_pins_config_pm_mux(pin_cfg_t *cfgs, int num)
{
	int i = 0;
	int gpio = 0;
	u32 offset;
	u32 bitmask = 1;
	u32 gpio_afsla_register_set = 0;
	u32 gpio_afslb_register_set = 0;
	u32 gpio_afsla_register_clear = 0;
	u32 gpio_afslb_register_clear = 0;
	u32 bankaddr = 0;

	gpio = PIN_NUM(cfgs[i]);

	/* Get the bank number the pin is mapped to */
	bankaddr = IO_ADDRESS(u8500_gpio_banks[(gpio >> GPIO_BLOCK_SHIFT)]);

	for (i = 0; i < num; i++) {
		/* Get the pin number  */
		gpio = PIN_NUM(cfgs[i]);

		/* get the offset into the register */
		offset = gpio % NMK_GPIO_PER_CHIP;
		/* Set the bit to toggle */
		bitmask = 1 << offset ;

		/* First check for ALT pin configuration */
		switch (PIN_ALT(cfgs[i])) {
		case NMK_GPIO_ALT_GPIO:
			/* Set bit to configured as GPIO */
			gpio_afsla_register_clear |= bitmask;
			gpio_afslb_register_clear |= bitmask;
			break;

		case NMK_GPIO_ALT_A:
			/* ALT A  setting so set corresponding bit */
			gpio_afsla_register_set |= bitmask;
			break;

		case NMK_GPIO_ALT_B:
			/* ALT B  setting so set corresponding bit */
			gpio_afslb_register_set |= bitmask;
			break;

		case NMK_GPIO_ALT_C:
			/* ALT C  setting so set corresponding bits */
			gpio_afsla_register_set |= bitmask;
			gpio_afslb_register_set |= bitmask;
			break;

		default:
			BUG();
			break;
		}
	}
	/* Set bits that configures GPIO */
	writel(readl(bankaddr + NMK_GPIO_AFSLA)
		& ~gpio_afsla_register_clear, bankaddr + NMK_GPIO_AFSLA);
	writel(readl(bankaddr + NMK_GPIO_AFSLB)
		& ~gpio_afslb_register_clear, bankaddr + NMK_GPIO_AFSLB);

	/* Set bits that configures ALT_X */
	writel(readl(bankaddr + NMK_GPIO_AFSLA)
		| gpio_afsla_register_set, bankaddr + NMK_GPIO_AFSLA);
	writel(readl(bankaddr + NMK_GPIO_AFSLB)
		| gpio_afslb_register_set, bankaddr + NMK_GPIO_AFSLB);
}
