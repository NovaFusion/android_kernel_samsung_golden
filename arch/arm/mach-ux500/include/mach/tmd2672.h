/*
 * Linux driver driver for proximity sensor TMD2672
 * ----------------------------------------------------------------------------
 *
 * Copyright (C) 2011 Samsung Electronics Co. Ltd
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

#ifndef _TMD2672_h_
#define	_TMD2672_h_

#define	TMD2672_I2C_DEVICE_NAME	"tmd2672_prox"

struct tmd2672_platform_data {
	unsigned int	ps_vout_gpio;
	int	alsout;
	int (* hw_setup)( void );
	int (* hw_teardown)( void );
};

#endif
