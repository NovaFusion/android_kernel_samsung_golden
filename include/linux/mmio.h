/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Joakim Axelsson <joakim.axelsson@stericsson.com> for ST-Ericsson
 * Author: Rajat Verma <rajat.verma@stericsson.com> for ST-Ericsson
 * Author: Vincent Abriou <vincent.abriou@stericsson.com> for ST-Ericsson.
 * License Terms: GNU General Public License v2
 */

#ifndef MMIO_H
#define MMIO_H

#include <linux/ioctl.h>

#define MMIO_NAME     "mmio_camera"      /* kept for backward compatibility */
#define MMIO_RAW_NAME "mmio_camera_raw"
#define MMIO_YUV_NAME "mmio_camera_yuv"

#define SRA_SUPPORT 1

#ifdef SRA_SUPPORT
#define SREG_16_BIT (0x1)
#define SREG_32_BIT (0x2)
#endif
/* Kernel side interface for MMIO */
/* Which camera is currently active */
enum camera_slot_t {
	PRIMARY_CAMERA = 0,
	SECONDARY_CAMERA,
	CAMERA_SLOT_END
};

enum camera_type_t {
	RAW_CAMERA = 0,
	YUV_CAMERA,
	CAMERA_TYPE_END
};

struct mmio_gpio {
	/* Name of the gpio */
	const char *name;
	/* Gpio number */
	int gpio;
	/* pin configuration when feature is enabled */
	unsigned long cfg_ena;
	/* pin configuration when feature is disabled */
	unsigned long cfg_disa;
	/* Set if pin is active high */
	/* kept for backward compatibility */
	int active_high;
	/* Time to wait when activating the pin, in usec */
	/* kept for backward compatibility */
	int udelay;
};

struct mmio_clk {
	const char *name;          /* Name of the clock */
	struct clk *clk_ptr;       /* Pointer on the allocated clock */
};

struct mmio_regulator {
	const char *name;          /* Name of the clock */
	struct regulator *reg_ptr; /* Pointer on the allocated regulator */
};

enum mmio_select_i2c_t {
	MMIO_ACTIVATE_IPI2C2 = 0, /* kept for backward compatibility */
	MMIO_ACTIVATE_I2C_HOST,   /* kept for backward compatibility */
	MMIO_ACTIVATE_I2C,
	MMIO_DEACTIVATE_I2C
};

enum mmio_select_xshutdown_t {
	MMIO_ENABLE_XSHUTDOWN_FW = 0,
	MMIO_ENABLE_XSHUTDOWN_HOST,
	MMIO_DISABLE_XSHUTDOWN
};
struct mmio_platform_data {
	struct device *dev;
	enum camera_slot_t camera_slot; /* Which camera is currently used,
					 * Primary/Secondary */
	void *extra;			/* Board's private data structure
					 * placeholder */
	int reset_ipgpio[CAMERA_SLOT_END]; /* Contains logical IP GPIO for
					    * reset pin */
	int sia_base;
	int cr_base;
	int (*platform_init)(struct mmio_platform_data *pdata);
	void (*platform_exit)(struct mmio_platform_data *pdata);
	int (*power_enable)(struct mmio_platform_data *pdata);
	void (*power_disable)(struct mmio_platform_data *pdata);
	/* kept for backward compatibility */
	int (*config_xshutdown_pins)(struct mmio_platform_data *pdata,
		enum mmio_select_xshutdown_t select, int is_active_high);
	int (*config_i2c_pins)(struct mmio_platform_data *pdata,
		enum mmio_select_i2c_t select);
	int (*clock_enable)(struct mmio_platform_data *pdata);
	void (*clock_disable)(struct mmio_platform_data *pdata);
	/* kept for backward compatibility */
	void (*set_xshutdown)(struct mmio_platform_data *pdata);
};

#define USER_SIDE_INTERFACE 1
/* User side is only allowed to access code in USER_SIDE_INTERFACE block */
#ifdef USER_SIDE_INTERFACE
enum mmio_bool_t {
	MMIO_FALSE = 0,
	MMIO_TRUE = !MMIO_FALSE,
	MMIO_BOOL_MAX = 0x7FFFFFFF
};

struct xshutdown_info_t {
	int ip_gpio;
	int camera_function;
};

struct xp70_fw_t {
	void __iomem *addr_sdram_ext;
	void __iomem *addr_esram_ext;
	void __iomem *addr_split;
	void __iomem *addr_data;
	unsigned int size_sdram_ext;
	unsigned int size_esram_ext;
	unsigned int size_split;
	unsigned int size_data;
};

struct isp_write_t {
	unsigned long t1_dest;
	unsigned long *data;
	unsigned long count;
};

struct trace_buf_t {
	void *address;
	unsigned int size;
};

#ifdef SRA_SUPPORT
struct s_reg {
	unsigned int addr;
	unsigned int value;
	unsigned int mask;
};

struct s_reg_list {
	unsigned int access_mode;
	unsigned int entries;
	struct s_reg  *s_regs_p;
};
#endif
struct mmio_input_output_t {
	union {
		enum mmio_bool_t	power_on;
		struct xp70_fw_t	xp70_fw;
		struct isp_write_t	isp_write;
		unsigned int		addr_to_map;
		struct xshutdown_info_t	xshutdown_info;
		enum camera_slot_t	camera_slot;
		struct trace_buf_t	trace_buf;
#ifdef SRA_SUPPORT
		struct s_reg_list s_reg_list;
#endif
	} mmio_arg;
};

#define MMIO_TRUE	(1)
#define MMIO_FALSE	(0)
#define MMIO_INVALID	(~0)

/*Xshutdown from host takes two arguments*/
/* kept for backward compatibility */
#define MMIO_XSHUTDOWN_ENABLE			(0x1)
/* kept for backward compatibility */
#define MMIO_XSHUTDOWN_ACTIVE_HIGH		(0x2)
#define MMIO_MAGIC_NUMBER 0x15

#define MMIO_CAM_INITBOARD	_IOW(MMIO_MAGIC_NUMBER, 1,\
struct mmio_input_output_t*)
#define MMIO_CAM_PWR_SENSOR	_IOW(MMIO_MAGIC_NUMBER, 2,\
struct mmio_input_output_t*)
#define MMIO_CAM_SET_EXT_CLK	_IOW(MMIO_MAGIC_NUMBER, 3,\
struct mmio_input_output_t*)
/* kept for backward compatibility */
#define MMIO_CAM_SET_PRI_HWIF	_IO(MMIO_MAGIC_NUMBER, 4)
/* kept for backward compatibility */
#define MMIO_CAM_SET_SEC_HWIF	_IO(MMIO_MAGIC_NUMBER, 5)
#define MMIO_CAM_INITMMDSPTIMER	_IO(MMIO_MAGIC_NUMBER, 6)
#define MMIO_CAM_LOAD_XP70_FW	_IOW(MMIO_MAGIC_NUMBER, 7,\
struct mmio_input_output_t*)
#define MMIO_CAM_MAP_STATS_AREA	_IOWR(MMIO_MAGIC_NUMBER, 8,\
struct mmio_input_output_t*)
/* kept for backward compatibility */
#define MMIO_ACTIVATE_I2C2	_IOW(MMIO_MAGIC_NUMBER, 9, int*)
/* kept for backward compatibility */
#define MMIO_ENABLE_XSHUTDOWN_FROM_HOST _IOW(MMIO_MAGIC_NUMBER, 10, int*)
#define MMIO_CAM_ISP_WRITE	_IOW(MMIO_MAGIC_NUMBER, 11,\
struct mmio_input_output_t*)
#define MMIO_CAM_GET_IP_GPIO	_IOWR(MMIO_MAGIC_NUMBER, 12,\
struct mmio_input_output_t*)
#define MMIO_CAM_DESINITBOARD	_IO(MMIO_MAGIC_NUMBER, 13)
#define MMIO_CAM_SET_TRACE_BUFFER _IOW(MMIO_MAGIC_NUMBER, 14,\
struct mmio_input_output_t*)

#ifdef SRA_SUPPORT
#define MMIO_CAM_READ_REGS	_IOWR(MMIO_MAGIC_NUMBER, 15,\
struct mmio_input_output_t*)
#define MMIO_CAM_MODIFY_REGS	_IOWR(MMIO_MAGIC_NUMBER, 16,\
struct mmio_input_output_t*)
#define MMIO_CAM_WRITE_REGS	_IOWR(MMIO_MAGIC_NUMBER, 17,\
struct mmio_input_output_t*)
#endif

#define MMIO_CAM_SYSTEM_REV     _IOW(MMIO_MAGIC_NUMBER, 0x12, int*)
#define MMIO_CAM_FLASH_ON_OFF     _IOW(MMIO_MAGIC_NUMBER, 0x13, int*)
#define MMIO_CAM_FLASH_SET_MODE   _IOW(MMIO_MAGIC_NUMBER, 0x14, int*)
#define MMIO_CAM_GPIO_PIN_CONTROL   _IOW(MMIO_MAGIC_NUMBER, 0x15, int*)
#define MMIO_CAM_POWER_PIN_CONTROL   _IOW(MMIO_MAGIC_NUMBER, 0x16, int*)
#define MMIO_CAM_FRONT_CAM_ID        _IOW(MMIO_MAGIC_NUMBER, 0x17, int*)
#endif /* USER_SIDE_INTERFACE */

#endif
/* MMIO_H */
