/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 
#ifndef __INC_NOMADIK_MAPPING_H
#define __INC_NOMADIK_MAPPING_H

/*--------------------------------------------------------------------------*/
#if defined(__STN_8810)

/* XTI (CPU OSMO/OSMOT address space) */
#define XTI_CPU_BASE_ADDR	0x10000000
#define XTI_CPU_END_ADDR	0x100FFFFF

/* XTI configuration registers */
#define XTI_CFG_REG_BASE_ADDR	0x101A0000
#define XTI_CFG_REG_END_ADDR	0x101AFFFF

/* Core APB Peripherals */
#define CORE_APB_BASE_ADDR	0x101E0000
#define CORE_APB_END_ADDR	0x101EFFFF

/* DMA APB Peripherals */
#define DMA_APB_BASE_ADDR	0x101F0000
#define DMA_APB_END_ADDR	0x101FFFFF

/* XTI (DSP OSMO/OSMOT address space) */
#define XTI_DSP_BASE_ADDR	0x10200000
#define XTI_DSP_END_ADDR	0x1020FFFF

#endif  /*  defined(__STN_8810)   */

/*--------------------------------------------------------------------------*/
#if defined(__STN_8815)

/* XTI (CPU OSMO/OSMOT address space) */
#define XTI_CPU_BASE_ADDR	0x10000000
#define XTI_CPU_END_ADDR	0x100FFFFF

/* XTI configuration registers */
#define XTI_CFG_REG_BASE_ADDR	0x101A0000
#define XTI_CFG_REG_END_ADDR	0x101AFFFF

/* Core APB Peripherals */
#define CORE_APB_BASE_ADDR	0x101E0000
#define CORE_APB_END_ADDR	0x101EFFFF

/* DMA APB Peripherals */
#define DMA_APB_BASE_ADDR	0x101F0000
#define DMA_APB_END_ADDR	0x101FFFFF

/* XTI (DSP OSMO/OSMOT address space) */
#define XTI_DSP_BASE_ADDR	0x10220000
#define XTI_DSP_END_ADDR	0x1022FFFF

#endif  /*  defined(__STN_8815)   */


/*--------------------------------------------------------------------------*/
#if defined(__STN_8820)

/* STM (System Trace Module address space) */
#define STM_BASE_ADDR	0x700F0000
#define STM_END_ADDR	0x700FFFFF

/* AHB2 Peripherals */
#define AHB2_PERIPH_BASE_ADDR	0x70100000
#define AHB2_PERIPH_END_ADDR	0x7010FFFF

/* APB2 Peripherals */
#define APB2_PERIPH_BASE_ADDR	0x70110000
#define APB2_PERIPH_END_ADDR	0x7011FFFF

/* APB1 Peripherals */
#define APB1_PERIPH_BASE_ADDR	0x70120000
#define APB1_PERIPH_END_ADDR	0x7012FFFF

#endif  /*  defined(__STN_8820)   */

/*--------------------------------------------------------------------------*/
#if defined(__STN_8500)
/* STM (System Trace Module address space) */
#define STM_BASE_ADDR       0x80100000
#define STM_END_ADDR        0x8010FFFF

#define HSEM_BASE_ADDR      0x80140000
#define HSEM_END_ADDR       0x8014FFFF

#define DMA_CTRL_BASE_ADDR  0x801C0000
#define DMA_CTRL_END_ADDR   0x801C0FFF


#endif  /*  defined(__STN_8500)   */

#endif /*__INC_NOMADIK_MAPPING_H */
