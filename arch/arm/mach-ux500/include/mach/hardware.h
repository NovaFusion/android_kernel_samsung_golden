/*
 * Copyright (C) 2009 ST-Ericsson.
 *
 * U8500 hardware definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

/*
 * Macros to get at IO space when running virtually
 * We dont map all the peripherals, let ioremap do
 * this for us. We map only very basic peripherals here.
 */
#define U8500_IO_VIRTUAL	0xf0000000
#define U8500_IO_PHYSICAL	0xa0000000

/* This macro is used in assembly, so no cast */
#define IO_ADDRESS(x)           \
	(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + U8500_IO_VIRTUAL)

/*
 * For 9540, ROM code is at address 0xFFFE0000
 * The previous macro cannot be used
 * Or else its virtual address would be above 0xFFFFFFFF
 */
#define IO_ADDRESS_DB9540_ROM(x)           \
	(((x) & 0x0001ffff) + U8500_IO_VIRTUAL + 0x0B000000)

/* typesafe io address */
#define __io_address(n)		__io(IO_ADDRESS(n))

#define __io_address_db9540_rom(n)	__io(IO_ADDRESS_DB9540_ROM(n))
/* Used by some plat-nomadik code */
#define io_p2v(n)		__io_address(n)

#include <mach/db8500-regs.h>
#include <mach/db5500-regs.h>

/*
 * DDR Dies base addresses for PASR
 */
#define U8500_CS0_BASE_ADDR	0x00000000
#define U8500_CS1_BASE_ADDR	0x10000000

#define U9540_DDR0_CS0_BASE_ADDR 0x00000000
#define U9540_DDR0_CS1_BASE_ADDR 0x20000000
#define U9540_DDR1_CS0_BASE_ADDR 0xC0000000
#define U9540_DDR1_CS1_BASE_ADDR 0xE0000000

/*
 * FIFO offsets for IPs
 */
#define MSP_TX_RX_REG_OFFSET	0
#define SSP_TX_RX_REG_OFFSET	0x8
#define SPI_TX_RX_REG_OFFSET	0x8
#define SD_MMC_TX_RX_REG_OFFSET 0x80
#define CRYP1_RX_REG_OFFSET	0x10
#define CRYP1_TX_REG_OFFSET	0x8
#define HASH1_TX_REG_OFFSET	0x4

#define SSP_0_CONTROLLER 4
#define SSP_1_CONTROLLER 5

#define SPI023_0_CONTROLLER 6
#define SPI023_1_CONTROLLER 7
#define SPI023_2_CONTROLLER 8
#define SPI023_3_CONTROLLER 9

#ifndef __ASSEMBLY__

#include <mach/id.h>
extern void __iomem *_PRCMU_BASE;

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#ifdef CONFIG_UX500_SOC_DB5500
bool cpu_is_u5500v1(void);
bool cpu_is_u5500v2(void);
bool cpu_is_u5500v20(void);
bool cpu_is_u5500v21(void);
#else
static inline bool cpu_is_u5500v1(void) { return false; }
static inline bool cpu_is_u5500v2(void) { return false; }
static inline bool cpu_is_u5500v20(void) { return false; }
static inline bool cpu_is_u5500v21(void) { return false; }
#endif

#ifdef CONFIG_UX500_SOC_DB8500
bool cpu_is_u8500v20(void);
bool cpu_is_u8500v21(void);
bool cpu_is_u8500v22(void);
#else
static inline bool cpu_is_u8500v20(void) { return false; }
static inline bool cpu_is_u8500v21(void) { return false; }
static inline bool cpu_is_u8500v22(void) { return false; }
#endif

#endif				/* __ASSEMBLY__ */
#endif				/* __MACH_HARDWARE_H */
