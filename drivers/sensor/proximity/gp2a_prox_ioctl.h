/*
 * Linux driver driver for proximity sensor GP2A002S00F
 * ----------------------------------------------------------------------------
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef _GP2A_PROX_IOCTL_h_
#define	_GP2A_PROX_IOCTL_h_


#include <linux/ioctl.h>	/* _IO() */


/* ioctl numbers for the GP2A miscellaneous device driver */

#define	GP2A_PROX_DEVNAME	"gp2a_prox"
#define	GP2A_LIGHT_DEVNAME	"gp2a_light"

#define	GP2A_IOC_MAGIC		'^'

#define	GP2A_IOC_ENABLE		_IO(GP2A_IOC_MAGIC, 0)
#define	GP2A_IOC_DISABLE	_IO(GP2A_IOC_MAGIC, 1)



#endif
