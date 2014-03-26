/*
 * Copyright (C) 2011 ST-Ericsson SA
 *
 * Author: Pawel SZYSZUK <pawel.szyszuk@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/init.h>

#include <mach/id.h>

#include "pins.h"

/*
 * U9500 is currently using U8500v2 HW. Therefore, the platform detection
 * is based on the kernel cmd line setting (early_param "pinsfor").
 */
inline bool cpu_is_u9500()
{
	if (pins_for_u9500())
		return true;
	else
		return false;
}
