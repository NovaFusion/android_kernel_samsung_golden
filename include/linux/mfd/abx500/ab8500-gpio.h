/*
 * Copyright ST-Ericsson 2010.
 *
 * Author: Bibek Basu <bibek.basu@stericsson.com>
 * Licensed under GPLv2.
 */

#ifndef _AB8500_GPIO_H
#define _AB8500_GPIO_H

#include <mach/gpio.h>

/*
 * Platform data to register a block: only the initial gpio/irq number.
 * Array sizes are large enough to contain all AB8500 and AB9540 GPIO
 * registers.
 */

struct ab8500_gpio_platform_data {
	int gpio_base;
	u32 irq_base;
	u8  config_reg[8];
	u8  config_direction[7];
	u8  config_pullups[7];
};

int ab8500_config_pulldown(struct device *dev,
				int gpio, bool enable);

int ab8500_gpio_config_select(struct device *dev,
				int gpio, bool gpio_select);

int ab8500_gpio_config_get_select(struct device *dev,
				int gpio, bool *gpio_select);

#endif /* _AB8500_GPIO_H */
