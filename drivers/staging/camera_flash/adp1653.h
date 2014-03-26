/*
 * Copyright (C) ST-Ericsson SA 2010
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __ADP1653_H__
#define __ADP1653_H__

#include <linux/types.h>
#define ADP1653_ID	(0)	/*chip does not give any id :) so be it zero!*/

#define OUTPUT_SEL_REG		(0x00)
#define CONFIG_TIMER_REG	(0x01)
#define SW_STROBE_REG		(0x02)
#define FAULT_STATUS_REG	(0x03)

/* Fault codes, FALUT_STATUS_REG bits */
#define OVER_VOLTAGE_FAULT		(0x01)
#define TIMEOUT_FAULT			(0x02)
#define OVER_TEMPERATURE_FAULT	(0x04)
#define SHORT_CIRCUIT_FAULT		(0x08)

/*CONFIG_TIMER_REG bits*/
#define TIMER_ENABLE	(0x10)

struct adp1653_priv_data{
	struct i2c_client *i2c_client;
	unsigned long curr_mode;
	unsigned long enable_gpio;
	unsigned long strobe_gpio;
	unsigned long irq_no;
	unsigned long status;
	uint8_t fault;
	uint8_t flash_intensity;
	uint8_t flash_duration;
	uint8_t torch_intensity;
	uint8_t indicator_intensity;
};

/*Intensity current limits in Micro Amps*/
/* over 250mA flash current is reduced */
/* do not know why, neither really care about */
//#define FLASH_MAX_INTENSITY	(500000)	/*code - 31*/
#define FLASH_MAX_INTENSITY	(250000)
#define FLASH_MIN_INTENSITY	(215000)	/*code - 12*/
#define TORCH_MAX_INTENSITY	(200000)	/*code - 11*/
#define TORCH_MIN_INTENSITY	(50000)		/*code - 1*/
#define ILED_MAX_INTENSITY  (17500)		/*Code - 7*/
#define ILED_MIN_INTENSITY	(2500)		/*code - 1*/

#define FLASH_MAX_STROBE_DURATION (820000)	 /*820 uSec*/

#define DURATION_USEC_TO_CODE(_code,_duration) do{			\
	 if(_duration > FLASH_MAX_STROBE_DURATION)				\
	 	_duration = FLASH_MAX_STROBE_DURATION;				\
	_code = (FLASH_MAX_STROBE_DURATION - _duration) / 54600;\
}while(0);

#define HPLED_UAMP_TO_CODE(_current) 	((_current - 35000) / 15000)

#define FLASH_UAMP_TO_CODE(_code,_current){		\
	if(_current > FLASH_MAX_INTENSITY)			\
		_current = FLASH_MAX_INTENSITY;			\
	if(_current < FLASH_MIN_INTENSITY)			\
		_current = FLASH_MIN_INTENSITY;			\
	_code = HPLED_UAMP_TO_CODE(_current);		\
}while(0)

#define TORCH_UAMP_TO_CODE(_code,_current){		\
	if(_current > TORCH_MAX_INTENSITY)			\
		_current = TORCH_MAX_INTENSITY;			\
	if(_current < TORCH_MIN_INTENSITY)			\
		_current = TORCH_MIN_INTENSITY;			\
	_code = HPLED_UAMP_TO_CODE(_current);		\
}while(0)

#define ILED_UAMP_TO_CODE(_code,_current) do {								\
	 if(_current > ILED_MAX_INTENSITY)										\
	 	_current = ILED_MAX_INTENSITY;										\
	_code = _current / ILED_MIN_INTENSITY; /* Min current: 2.5mA/2500uA*/	\
}while(0)

#endif
