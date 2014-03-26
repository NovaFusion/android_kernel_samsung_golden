/*
 * Copyright ST-Ericsson 2012.
 *
 * Author: Naga Radhesh <naga.radheshy@stericsson.com>
 * Licensed under GPLv2.
 */
#ifndef _AB8500_PWMLED_H
#define _AB8500_PWMLED_H

struct ab8500_led_pwm {
	int	pwm_id;
	int	blink_en;
};

struct ab8500_pwmled_platform_data {
	int	num_pwm;
	struct	ab8500_led_pwm *leds;
};

#endif
