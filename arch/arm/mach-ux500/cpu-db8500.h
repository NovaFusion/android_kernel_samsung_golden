/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms:  GNU General Public License (GPL), version 2
 *
 * CPU specific routines.
 *
 * Author: Shreshtha Kumar SAHU <shreshthakumar.sahu@stericsson.com>
 */

#ifndef __CPU_DB8500_H__
#define __CPU_DB8500_H__

/* PER1 IP's softreset masks */
#define PRCC_K_SOFTRST_UART0_MASK	(1 << 0)
#define PRCC_K_SOFTRST_UART1_MASK	(1 << 1)
#define PRCC_K_SOFTRST_I2C1_MASK	(1 << 2)
#define PRCC_K_SOFTRST_MSP0_MASK	(1 << 3)
#define PRCC_K_SOFTRST_MSP1_MASK	(1 << 4)
#define PRCC_K_SOFTRST_SDI0_MASK	(1 << 5)
#define PRCC_K_SOFTRST_I2C2_MASK	(1 << 6)
#define PRCC_K_SOFTRST_SP13_MASK	(1 << 7)
#define PRCC_K_SOFTRST_SLIMBUS0_MASK	(1 << 8)
#define PRCC_K_SOFTRST_I2C4_MASK	(1 << 9)
#define PRCC_K_SOFTRST_MSP3_MASK	(1 << 10)
#define PRCC_K_SOFTRST_PER_MSP3_MASK	(1 << 11)
#define PRCC_K_SOFTRST_PER_MSP1_MASK	(1 << 12)
#define PRCC_K_SOFTRST_PER_MSP0_MASK	(1 << 13)
#define PRCC_K_SOFTRST_PER_SLIMBUS_MASK	(1 << 14)

/* PER2 IP's softreset masks */
#define PRCC_K_SOFTRST_I2C3_MASK	(1 << 0)
#define PRCC_K_SOFTRST_PWL_MASK		(1 << 1)
#define PRCC_K_SOFTRST_SDI4_MASK	(1 << 2)
#define PRCC_K_SOFTRST_MSP2_MASK	(1 << 3)
#define PRCC_K_SOFTRST_SDI1_MASK	(1 << 4)
#define PRCC_K_SOFTRST_SDI3_MASK	(1 << 5)
#define PRCC_K_SOFTRST_HSIRX_MASK	(1 << 6)
#define PRCC_K_SOFTRST_HSITX_MASK	(1 << 7)
#define PRCC_K_SOFTRST_PER_MSP2_MASK	(1 << 8)

/* PER3 IP's softreset masks */
#define PRCC_K_SOFTRST_SSP0_MASK	(1 << 1)
#define PRCC_K_SOFTRST_SSP1_MASK	(1 << 2)
#define PRCC_K_SOFTRST_I2C0_MASK	(1 << 3)
#define PRCC_K_SOFTRST_SDI2_MASK	(1 << 4)
#define PRCC_K_SOFTRST_SKE_MASK		(1 << 5)
#define PRCC_K_SOFTRST_UART2_MASK	(1 << 6)
#define PRCC_K_SOFTRST_SDI5_MASK	(1 << 7)

/* PER6 IP's softreset masks */
#define PRCC_K_SOFTRST_RNG_MASK		(1 << 0)

void u8500_reset_ip(unsigned char per, unsigned int ip_mask);

#endif
