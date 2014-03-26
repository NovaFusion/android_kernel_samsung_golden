/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __ASM_ARCH_DEVICES_H__
#define __ASM_ARCH_DEVICES_H__

struct platform_device;
struct amba_device;

extern struct platform_device u5500_gpio_devs[];
extern struct platform_device u8500_gpio_devs[];

extern struct platform_device ux500_mcde_device;
extern struct platform_device ux500_mcde_device;
extern struct platform_device u5500_dsilink_device[];
extern struct platform_device u8500_dsilink_device[];
extern struct platform_device u8500_shrm_device;
extern struct platform_device ux500_b2r2_blt_device;
extern struct platform_device ux500_b2r2_device;
extern struct platform_device ux500_b2r2_1_device;
extern struct platform_device u8500_trace_modem;
extern struct platform_device ux500_hwmem_device;
extern struct platform_device ux500_stm_device;
extern struct amba_device ux500_pl031_device;
extern struct platform_device ux500_hash1_device;
extern struct platform_device ux500_cryp1_device;
extern struct platform_device mloader_fw_device;
extern struct platform_device ux500_ske_keypad_device;
extern struct platform_device u8500_hsi_device;
extern struct platform_device ux500_mmio_device;
extern struct platform_device ux500_mmio_raw_device;
extern struct platform_device ux500_mmio_yuv_device;
extern struct platform_device u5500_mmio_device;
extern struct platform_device db8500_prcmu_device;
extern struct platform_device db8500_mali_gpu_device;
extern struct platform_device ux500_c2c_device;
extern struct platform_device dbx540_prcmu_device;

extern struct platform_device u8500_usecase_gov_device;
extern struct platform_device u9540_usecase_gov_device;

#endif
