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

#ifndef __TAOS_H__
#define __TAOS_H__

/* power control */
#define ON		1
#define OFF		0

/* for proximity adc avg */
#define PROX_READ_NUM 40
#define TAOS_PROX_MAX 1023
#define TAOS_PROX_MIN 0

/* Triton register offsets */
#define CNTRL    0x00
#define PRX_TIME   0x02
#define WAIT_TIME   0x03
#define PRX_MINTHRESHLO   0X08
#define PRX_MINTHRESHHI   0X09
#define PRX_MAXTHRESHLO   0X0A
#define PRX_MAXTHRESHHI   0X0B
#define INTERRUPT   0x0C
#define PRX_CFG    0x0D
#define PRX_COUNT   0x0E
#define GAIN    0x0F
#define REVID    0x11
#define CHIPID    0x12
#define STATUS    0x13
#define PRX_LO    0x18
#define PRX_HI    0x19
#define PRX_OFFSET			0x1E
#define TEST_STATUS   0x1F

/* Triton cmd reg masks */
#define CMD_REG    0X80
#define CMD_BYTE_RW   0x00
#define CMD_WORD_BLK_RW   0x20
#define CMD_SPL_FN   0x60
#define CMD_PROX_INTCLR   0X05
#define CMD_TST_REG   0X08
#define CMD_USER_REG   0X09

/* Triton cntrl reg masks */
#define CNTL_REG_CLEAR   0x00
#define CNTL_PROX_INT_ENBL  0X20
#define CNTL_WAIT_TMR_ENBL  0X08
#define CNTL_PROX_DET_ENBL  0X04
#define CNTL_ADC_ENBL   0x02
#define CNTL_PWRON   0x01
#define CNTL_PROXPON_ENBL  0x0F
#define CNTL_INTPROXPON_ENBL  0x2D

/* Thresholds */
#define PROX_THRESHOLD_LO_LIMIT  0x0000
#define PROX_THRESHOLD_HI_LIMIT  0xFFFF

/* Device default configuration */
#define CALIB_TGT_PARAM   300000
#define SCALE_FACTOR_PARAM  1
#define GAIN_TRIM_PARAM   512
#define GAIN_PARAM   1

#define PRX_THRSH_HI_PARAM  0x29E	/* 670 */
#define PRX_THRSH_LO_PARAM  0x1F4	/* 500 */
#define PRX_THRSH_HI_CALPARAM	450
#define PRX_THRSH_LO_CALPARAM	350

#define PRX_ADC_TIME_PARAM  0xFF	/* [HSS] Original value : 0XEE */
#define PRX_WAIT_TIME_PARAM  0xFF	/* 2.73ms */
#define INTR_FILTER_PARAM  0x30
#define PRX_CONFIG_PARAM  0x00
#define PRX_PULSE_CNT_PARAM  0x06
#define PRX_GAIN_PARAM   0x28
#define PRX_GAIN_OFFSET		0x17

#define TMD_FUNC_DBG 1

#if TMD_FUNC_DBG
#define func_dbg()	do { \
	printk(KERN_INFO "[PROXIMITY] %s\n", __func__); \
	} while (0)
#else
#define func_dbg()
#endif

/* prototype */
int sensors_register(struct device *dev, void * drvdata,
		struct device_attribute *attributes[], char *name);
static void taos_early_suspend(struct early_suspend *handler);
static void taos_early_resume(struct early_suspend *handler);

#endif
