/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 *	Grzegorz Sygieda <grzegorz.sygieda@tieto.com> for ST-Ericsson.
 *	Krzysztof Antonowicz <krzysztof.antonowicz@tieto.com> for ST-Ericsson.
 *
 * License Terms: GNU General Public License v2
 */

#ifndef _STE_TIMED_VIBRA_H_
#define _STE_TIMED_VIBRA_H

/* Vibrator states */
enum ste_timed_vibra_states {
	STE_VIBRA_IDLE = 0,
	STE_VIBRA_BOOST,
	STE_VIBRA_ON,
	STE_VIBRA_OFF,
};

typedef void (*timed_vibra_control_fp)(
		unsigned char speed_left_pos,
		unsigned char speed_left_neg,
		unsigned char speed_right_pos,
		unsigned char speed_right_neg);

/*
 * Vibrator platform data structure
 * For details check ste_timed_vibra docbook
 */
struct ste_timed_vibra_platform_data {
	bool is_linear_vibra;
	unsigned int boost_level;	/* p1 */
	unsigned int boost_time;	/* p2 */
	unsigned int on_level;		/* p3 */
	unsigned int off_level;		/* p4 */
	unsigned int off_time;		/* p5 */
	timed_vibra_control_fp timed_vibra_control;
};

#endif /* _STE_TIMED_VIBRA_H_ */
