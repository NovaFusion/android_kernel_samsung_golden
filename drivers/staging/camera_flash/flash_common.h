#ifndef __FLASH_COMMON_H__
#define __FLASH_COMMON_H__

#include "camera_flash_bitfields.h"
#include <linux/camera_flash.h>

struct flash_chip_ops{
	int (*get_modes)( void *priv_data, unsigned long *modes);
	int (*get_mode_details)(void *priv_data,unsigned long mode,
				struct flash_mode_details *details_p);
	int (*enable_flash_mode) (void *priv_data,unsigned long mode,
		int enable);
	int (*configure_flash_mode) (void *priv_data, unsigned long mode,
				struct flash_mode_params *params_p);
	int (*trigger_strobe) (void *priv_data, int enable);
	int (*get_life_counter) (void *priv_data);
	int (*get_status)	(void *priv_data, unsigned long *status);
	int (*get_selftest_modes)	(void *priv_data,
		unsigned long *modes);
	int (*get_fault_registers)	(void *priv_data, unsigned long mode,
		unsigned long *status);
};

#define FLASH_TYPE_XENON	(0x1)
#define FLASH_TYPE_HPLED	(0x2)

#define SET_FLASHCHIP_TYPE(flash_chip_p,_TYPE) ((flash_chip_p)->id |= _TYPE)
#define GET_FLASHHIP_TYPE(flash_chip_p) ((flash_chip_p)->id & 0xffff)
#define GET_FLASHCHIP_ID(flash_chip_p)  ((flash_chip_p)->id >> 16)
#define SET_FLASHCHIP_ID(flash_chip_p,_ID) ((flash_chip_p)->id |= (_ID << 16))

struct flash_chip {
	unsigned long id;
	struct flash_chip_ops *ops;
	void *priv_data;
	unsigned char name[FLASH_NAME_SIZE];
};

/**
 * struct flash_platform_data:
 * platform specific data For flash chip driver
 * @cam : 0 - primary, 1 - secondary
 * @strobe_gpio: GPIO used as strobe
 * @enable_gpio: GPIO used for enable/reset input
 */
struct flash_platform_data{
	unsigned long cam;
	unsigned long strobe_gpio;
	unsigned long strobe_gpio_alt_func;
	unsigned long enable_gpio;
	unsigned long enable_gpio_alt_func;
};

extern int register_flash_chip(unsigned int cam, struct flash_chip *flash_p);
extern int flash_async_notify (void );

#endif
