/*
 * ST L3G4200D 3-Axis Gyroscope header file
 *
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Chethan Krishna N <chethan.krishna@stericsson.com> for ST-Ericsson
 * Licence terms: GNU General Public Licence (GPL) version 2
 */

#ifndef __L3G4200D_H__
#define __L3G4200D_H__

#ifdef __KERNEL__
/**
 * struct l3g4200d_gyr_platform_data - platform datastructure for l3g4200d
 * @axis_map_x: x axis position on the hardware, 0 1 or 2
 * @axis_map_y: y axis position on the hardware, 0 1 or 2
 * @axis_map_z: z axis position on the hardware, 0 1 or 2
 * @negative_x: x axis is orientation, 0 or 1
 * @negative_y: y axis is orientation, 0 or 1
 * @negative_z: z axis is orientation, 0 or 1
 */
struct l3g4200d_gyr_platform_data {
	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negative_x;
	u8 negative_y;
	u8 negative_z;
};

#endif /* __KERNEL__ */

#endif  /* __L3G4200D_H__ */
