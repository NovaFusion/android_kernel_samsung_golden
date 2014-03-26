/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
*
* File Name	: lps001wp.h
* Authors	: MSH - Motion Mems BU - Application Team
*		: Matteo Dameno (matteo.dameno@st.com)*
*		: Carmine Iascone (carmine.iascone@st.com)
* Version	: V 1.1.1
* Date		: 05/11/2010
* Description	: LPS001WP pressure temperature sensor driver
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
*******************************************************************************/

#ifndef	__LPS001WP_H__
#define	__LPS001WP_H__


#include	<linux/input.h>

#define	SAD0L				0x00
#define	SAD0H				0x01
#define	LPS001WP_PRS_I2C_SADROOT	0x2E
#define	LPS001WP_PRS_I2C_SAD_L		((LPS001WP_PRS_I2C_SADROOT<<1)|SAD0L)
#define	LPS001WP_PRS_I2C_SAD_H		((LPS001WP_PRS_I2C_SADROOT<<1)|SAD0H)
#define	LPS001WP_PRS_DEV_NAME		"lps001wp_prs"

/* input define mappings */
#define ABS_PR		ABS_PRESSURE
#define ABS_TEMP	ABS_GAS
#define ABS_DLTPR	ABS_MISC



/************************************************/
/* 	Pressure section defines	 	*/
/************************************************/

/* Pressure Sensor Operating Mode */
#define	LPS001WP_PRS_ENABLE		0x01
#define	LPS001WP_PRS_DISABLE		0x00




#define	LPS001WP_PRS_PM_NORMAL		0x40
#define	LPS001WP_PRS_PM_OFF		LPS001WP_PRS_DISABLE

#define	SENSITIVITY_T		64	/** =	64 LSB/degrC	*/
#define	SENSITIVITY_P		16	/** =	16 LSB/mbar	*/


#ifdef __KERNEL__
/**
 * struct lps001wp_prs_platform_data - platform datastructure for lps001wp_prs
 * @poll_interval: maximum polling interval
 * @min_interval:  minimum polling interval
 * @init: pointer to init function
 * @exit: pointer to deinitialisation function
 * @power_on: pointer to device enable function
 * @power_off: pointer to device disable function
 */
struct lps001wp_prs_platform_data {

	int poll_interval;
	int min_interval;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

};

#endif /* __KERNEL__ */

#endif  /* __LPS001WP_H__ */
