/*
 * Linux device driver for KTD267 FLASH LED
 * -------------------------------------------------------------------------
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

/*************************************************************************
*	 Header Files
**************************************************************************/
#include <linux/module.h>	/* module stuff  */
#include <linux/ktd267.h>	/* ktd267 related header file */
#include <linux/gpio.h>		/* gpio_set_value() */


/*************************************************************************
*	 DEFINES
**************************************************************************/
#define ktd267_err(fmt,...)   printk(KERN_ERR " KTD267 ERROR: %s[%d]"fmt,__FUNCTION__,__LINE__, ##__VA_ARGS__);

#ifndef KTD267_DEBUG
#define ktd267_dbg(fmt,...)
#else
#define ktd267_dbg(fmt,...)   printk(KERN_DEBUG " KTD267 DEBUG: %s[%d]"fmt,__FUNCTION__,__LINE__, ##__VA_ARGS__);
#endif

/*************************************************************************
*	 DEFINITIONS
**************************************************************************/
struct ktd267_platform_data ktd267data ={-1,-1};

/*************************************************************************
*	 FUNCTION DEFINITIONS
**************************************************************************/
int ktd267_set_mode(enum ktd267_mode mode)
{

	if(ktd267data.gpio_ENF < 0 ||  ktd267data.gpio_ENM < 0) {
		ktd267_err("\n GPIO pins not set for Device KTD267!!");	
		return -EINVAL;
	}
	
	switch(mode){
	case KTD267_MODE_FLASH:
		ktd267_dbg("\n Set KTD267 in Flash mode");
		gpio_set_value(ktd267data.gpio_ENF,1);
		gpio_set_value(ktd267data.gpio_ENM,0);
		break;

	case KTD267_MODE_MOVIE:	
		ktd267_dbg("\n Set KTD267 in Movie mode");
		gpio_set_value(ktd267data.gpio_ENF,1);
		gpio_set_value(ktd267data.gpio_ENM,1);		
		break;

	case KTD267_MODE_TERMINATE:	
		ktd267_dbg("\n Shutdown KTD267");
		gpio_set_value(ktd267data.gpio_ENF,0);
		gpio_set_value(ktd267data.gpio_ENM,0);		
		break;
	}

	return 0;
}

MODULE_AUTHOR("Arun Menon <a.menon@samsung.com>");
MODULE_DESCRIPTION("Driver for KTD267 FLASH LED");
MODULE_LICENSE("GPL");

