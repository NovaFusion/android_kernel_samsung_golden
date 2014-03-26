/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * Board specific file for regulator machine initialization
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/regulator/machine.h>
#include <linux/mpu.h>
#include <mach/bma023.h>
#ifdef CONFIG_PROXIMITY_GP2A
#include <mach/gp2a.h>
#endif
#ifdef CONFIG_PROXIMITY_GP2A030
#include <mach/gp2ap020.h>
#endif
#include <mach/mpu60x0.h>
#include "board-gavini-regulators.h"

/*
 * AB8500 Regulator Configuration
 */

/* ab8500 regulator register initialization */
static struct ab8500_regulator_reg_init
		gavini_ab8500_regulator_reg_init[AB8500_NUM_REGULATOR_REGISTERS] = {
	/*
	 * VanaRequestCtrl          = HP/LP depending on VxRequest
	 * VpllRequestCtrl          = HP/LP depending on VxRequest
	 * VextSupply1RequestCtrl   = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL2,       0xf0, 0x00),
	/*
	 * VextSupply2RequestCtrl   = HP/LP depending on VxRequest
	 * VextSupply3RequestCtrl   = HP/LP depending on VxRequest
	 * Vaux1RequestCtrl         = HP/LP depending on VxRequest
	 * Vaux2RequestCtrl         = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL3,       0xff, 0x00),
	/*
	 * Vaux3RequestCtrl         = HP/LP depending on VxRequest
	 * SwHPReq                  = Control through SWValid disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL4,       0x07, 0x00),
	/*
	 * Vsmps1SysClkReq1HPValid  = enabled
	 * Vsmps2SysClkReq1HPValid  = enabled
	 * Vsmps3SysClkReq1HPValid  = enabled
	 * VanaSysClkReq1HPValid    = disabled
	 * VpllSysClkReq1HPValid    = enabled
	 * Vaux1SysClkReq1HPValid   = disabled
	 * Vaux2SysClkReq1HPValid   = disabled
	 * Vaux3SysClkReq1HPValid   = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQ1HPVALID1, 0xe8, 0x00),
	/*
	 * VextSupply1SysClkReq1HPValid = disabled
	 * VextSupply2SysClkReq1HPValid = disabled
	 * VextSupply3SysClkReq1HPValid = disabled
	 * VarmSysClkReq1HPValid	= disabled
	 * VapeSysClkReq1HPValid	= disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQ1HPVALID2, 0x70, 0x00),
	/*
	 * VanaHwHPReq1Valid        = disabled
	 * Vaux1HwHPreq1Valid       = disabled
	 * Vaux2HwHPReq1Valid       = disabled
	 * Vaux3HwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ1VALID1,     0xe8, 0x00),
	/*
	 * VextSupply1HwHPReq1Valid = disabled
	 * VextSupply2HwHPReq1Valid = disabled
	 * VextSupply3HwHPReq1Valid = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ1VALID2,     0x07, 0x00),
	/*
	 * VanaHwHPReq2Valid        = disabled
	 * Vaux1HwHPReq2Valid       = disabled
	 * Vaux2HwHPReq2Valid       = disabled
	 * Vaux3HwHPReq2Valid       = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ2VALID1,     0xe8, 0x00),
	/*
	 * VextSupply1HwHPReq2Valid = disabled
	 * VextSupply2HwHPReq2Valid = disabled
	 * VextSupply3HwHPReq2Valid = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ2VALID2,     0x07, 0x00),
	/*
	 * VanaSwHPReqValid         = disabled
	 * Vaux1SwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSWHPREQVALID1,      0xa0, 0x00),
	/*
	 * Vaux2SwHPReqValid        = disabled
	 * Vaux3SwHPReqValid        = disabled
	 * VextSupply1SwHPReqValid  = disabled
	 * VextSupply2SwHPReqValid  = disabled
	 * VextSupply3SwHPReqValid  = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSWHPREQVALID2,      0x1f, 0x00),
	/*
	 * SysClkReq2Valid1         = disabled
	 * SysClkReq3Valid1         = disabled
	 * SysClkReq4Valid1         = disabled
	 * SysClkReq5Valid1         = disabled
	 * SysClkReq6Valid1         = disabled
	 * SysClkReq7Valid1         = disabled
	 * SysClkReq8Valid1         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQVALID1,    0xfe, 0x00),
	/*
	 * SysClkReq2Valid2         = disabled
	 * SysClkReq3Valid2         = disabled
	 * SysClkReq4Valid2         = disabled
	 * SysClkReq5Valid2         = disabled
	 * SysClkReq6Valid2         = disabled
	 * SysClkReq7Valid2         = disabled
	 * SysClkReq8Valid2         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQVALID2,    0xfe, 0x00),
	/*
	 * VTVoutEna                = disabled
	 * Vintcore12Ena            = disabled
	 * Vintcore12Sel            = 1.25 V
	 * Vintcore12LP             = inactive (HP)
	 * VTVoutLP                 = inactive (HP)
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUMISC1,              0xfe, 0x10),
	/*
	 * VaudioEna                = disabled
	 * VdmicEna                 = disabled
	 * Vamic1Ena                = disabled
	 * Vamic2Ena                = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUDIOSUPPLY,           0x1e, 0x00),
	/*
	 * Vamic1_dzout             = high-Z when Vamic1 is disabled
	 * Vamic2_dzout             = high-Z when Vamic2 is disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRL1VAMIC,         0x03, 0x00),
	/*
	 * VPll                     = Hw controlled
	 * VanaRegu                 = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VPLLVANAREGU,           0x0f, 0x02),
	/*
	 * VrefDDREna               = disabled
	 * VrefDDRSleepMode         = inactive (no pulldown)
	 */
	INIT_REGULATOR_REGISTER(AB8500_VREFDDR,                0x03, 0x00),
	/*
	 * No external regulators connected to AB8500 in Gavini
	 */
	INIT_REGULATOR_REGISTER(AB8500_EXTSUPPLYREGU,          0xff, 0x00),
	/*
	 * Vaux1Regu                = force HP
	 * Vaux2Regu                = force HP
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX12REGU,             0x0f, 0x05),
	/*
	 * Vrf1Regu                 = HW control
	 * Vaux3Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VRF1VAUX3REGU,          0x03, 0x08),
	/*
	 * Vaux1Sel                 = 3.0 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX1SEL,               0x0f, 0x0E),
	/*
	 * Vaux2Sel                 = 1.8 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX2SEL,               0x0f, 0x05),
	/*
	 * Vaux3Sel                 = 2.91 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VRF1VAUX3SEL,           0x07, 0x07),
	/*
	 * VextSupply12LP           = disabled (no LP)
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRL2SPARE,         0x01, 0x00),
	/*
	 * Vaux1Disch               = short discharge time
	 * Vaux2Disch               = short discharge time
	 * Vaux3Disch               = short discharge time
	 * Vintcore12Disch          = short discharge time
	 * VTVoutDisch              = short discharge time
	 * VaudioDisch              = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRLDISCH,          0xfc, 0x00),
	/*
	 * VanaDisch                = short discharge time
	 * VdmicPullDownEna         = pulldown disabled when Vdmic is disabled
	 * VdmicDisch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRLDISCH2,         0x16, 0x00),
};

/* vana regulator configuration, for analogue part of displays */
static struct regulator_consumer_supply ab8500_vana_consumers[] = {
	{
		.dev_name = "mcde",
		.supply = "v-ana",
	},
	{
		.dev_name = "mmio_camera",
		.supply = "v-ana",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.0",
		.supply = "ana",
	},
#endif
};

/* vaux1 regulator configuration */
static struct regulator_consumer_supply ab8500_vaux1_consumers[] = {
#if defined(CONFIG_ACCEL_MPU60X0)
	{
		.dev_name = MPU60X0_I2C_DEVICE_NAME,
		// NB: "v-accelerometer" is already taken
		.supply = MPU60X0_REGULATOR,
	},
#endif
#if defined(CONFIG_MPU_SENSORS_MPU3050)
	{
	/* connected to i2c bus number 2, slave address 0x68*/
		.dev_name = "8-0068",
		.supply = "v_mpu_vdd",
	},
#endif
#if defined(CONFIG_MPU_SENSORS_MPU3050)
	{
	/* connected to i2c bus number 2, slave address 0x68*/
		.dev_name = "0-0068",
		.supply = "v_vdd_00C",
	},
#endif
#if defined(CONFIG_PROXIMITY_GP2A)
	{
		.dev_name = GP2A_I2C_DEVICE_NAME,
		.supply = "v-prox-vcc",
	},
#endif
#if defined(CONFIG_PROXIMITY_GP2A030)
	{
		.dev_name = "gp2a-opt",
		.supply = "proxvcc",
	},
#endif
#if defined(CONFIG_PROXIMITY_GP2A030)
	{
		.dev_name = "gp2a-opt",
		.supply = "proxvcc_00C",
	},
#endif
#if defined(CONFIG_ACCEL_BMA022)
	{
		.dev_name = "bma022",
		.supply = "v-accel-vdd",
	},
#endif
	{
		.dev_name = "mmio_camera",
		.supply = "v-mmio-camera",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.1",
		.supply = "aux1",
	},
#endif
#if 0
	{
		.dev_name = "3-005c",
		.supply = "avdd",
	},
	{
		.dev_name = "3-005d",
		.supply = "avdd",
	},
	{
		.dev_name = "3-004b",
		.supply = "vdd",
	},
#endif
};

/* vaux2 regulator configuration */
static struct regulator_consumer_supply ab8500_vaux2_consumers[] = {
#if defined(CONFIG_MPU_SENSORS_MPU3050)
	{
	/* connected to i2c bus number 2, slave address 0x68*/
		.dev_name = "8-0068",
		.supply = "v_mpu_vlogic",
	},
#endif
#if defined(CONFIG_MPU_SENSORS_MPU3050)
	{
	/* connected to i2c bus number 2, slave address 0x68*/
		.dev_name = "0-0068",
		.supply = "v_vlogic_00C",
	},
#endif
#if defined(CONFIG_ACCEL_BMA022)
	{
		.dev_name = "bma022",
		.supply = "v-accel_vio",
	},
#endif
#if defined(CONFIG_PROXIMITY_GP2A)
	{
		.dev_name = GP2A_I2C_DEVICE_NAME,
		.supply = "v-prox_vio",
	},
#endif
#if defined(CONFIG_PROXIMITY_GP2A030)
	{
		.dev_name = "gp2a-opt",
		.supply = "proxvio",
	},
#endif
#if defined(CONFIG_PROXIMITY_GP2A030)
	{
		.dev_name = "gp2a-opt",
		.supply = "proxvio_00C",
	},
#endif
#if defined(CONFIG_COMPASS_MMC3280)
	{
		.dev_name = "mmc3280",
		.supply = "v-compass",
	},
#endif
	/* Not sure if this is required maybe specific to MOP500 */
	{
		.dev_name = "ab8500-codec.0",
		.supply = "vcc-avswitch",
	},
	{
	       .dev_name = "ab8500-acc-det.0",
	       .supply = "vcc-avswitch"
	},

#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.2",
		.supply = "aux2",
	},
#endif
};

/* vaux3 regulator configuration */
static struct regulator_consumer_supply ab8500_vaux3_consumers[] = {
	{
		.dev_name = "sdi0",
		.supply = "v-MMC-SD"
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.3",
		.supply = "aux3",
	},
#endif
};

/* vtvout regulator configuration, supply for tvout, gpadc, TVOUT LDO */
static struct regulator_consumer_supply ab8500_vtvout_consumers[] = {
	{
		.supply = "ab8500-gpadc",
	},
	{
		.dev_name = "ab8500-charger.0",
		.supply = "vddadc"
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.4",
		.supply = "tvout",
	},
#endif
};
/* vaudio regulator configuration, supply for ab8500-vaudio, VAUDIO LDO */
static struct regulator_consumer_supply ab8500_vaudio_consumers[] = {
	{
		.supply = "v-audio",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.5",
		.supply = "audio",
	},
#endif
};

/* vamic1 regulator configuration */
static struct regulator_consumer_supply ab8500_vamic1_consumers[] = {
	{
		.supply = "v-amic1",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.6",
		.supply = "anamic1",
	},
#endif
};

/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
static struct regulator_consumer_supply ab8500_vamic2_consumers[] = {
	{
		.supply = "v-amic2",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.7",
		.supply = "anamic2",
	},
#endif
};

/* supply for v-dmic, VDMIC LDO */
static struct regulator_consumer_supply ab8500_vdmic_consumers[] = {
	{
		.supply = "v-dmic",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.8",
		.supply = "dmic",
	},
#endif
};

/* supply for v-intcore12, VINTCORE12 LDO */
static struct regulator_consumer_supply ab8500_vintcore_consumers[] = {
	{
		.supply = "v-intcore",
	},
#ifdef CONFIG_U8500_REGULATOR_DEBUG
	{
		.dev_name = "reg-virt-consumer.9",
		.supply = "intcore",
	},
#endif
	{
		.dev_name = "ab8500-usb.0",
		.supply = "v-intcore",
	},

};

/*
 * AB8500 regulators
 */
struct regulator_init_data gavini_regulators[AB8500_NUM_REGULATORS] = {
	/* supplies to the Sensors 3V */
	/* 'REGULATOR_CHANGE_MODE' ops added to support LP mode */
	[AB8500_LDO_AUX1] = {
		.constraints = {
			.name = "ab8500-vaux1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS |
					REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux1_consumers),
		.consumer_supplies = ab8500_vaux1_consumers,
	},
	/* supplies to the Sensors IO 1V8  */
	/* 'REGULATOR_CHANGE_MODE' ops added to support LP mode */
	[AB8500_LDO_AUX2] = {
		.constraints = {
			.name = "ab8500-vaux2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS |
					REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux2_consumers),
		.consumer_supplies = ab8500_vaux2_consumers,
	},
	/* supply for VAUX3, supplies to SDcard slots */
	[AB8500_LDO_AUX3] = {
		.constraints = {
			.name = "ab8500-vaux3",
			.min_uV = 1200000,
			.max_uV = 2910000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					REGULATOR_CHANGE_STATUS |
					REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux3_consumers),
		.consumer_supplies = ab8500_vaux3_consumers,
	},
	/* supply for v-intcore12, VINTCORE12 LDO */
	[AB8500_LDO_INTCORE] = {
		.constraints = {
			.name = "ab8500-vintcore",
			.min_uV = 1250000,
			.max_uV = 1350000,
			.input_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					REGULATOR_CHANGE_STATUS |
					REGULATOR_CHANGE_MODE |
					REGULATOR_CHANGE_DRMS,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vintcore_consumers),
		.consumer_supplies = ab8500_vintcore_consumers,
	},
	/* supply for tvout, gpadc, TVOUT LDO */
	[AB8500_LDO_TVOUT] = {
		.constraints = {
			.name = "ab8500-vtvout",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vtvout_consumers),
		.consumer_supplies = ab8500_vtvout_consumers,
	},
	/* supply for ab8500-vaudio, VAUDIO LDO */
	[AB8500_LDO_AUDIO] = {
		.constraints = {
			.name = "ab8500-vaudio",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaudio_consumers),
		.consumer_supplies = ab8500_vaudio_consumers,
	},
	/* supply for v-anamic1 VAMic1-LDO */
	[AB8500_LDO_ANAMIC1] = {
		.constraints = {
			.name = "ab8500-vamic1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic1_consumers),
		.consumer_supplies = ab8500_vamic1_consumers,
	},
	/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
	[AB8500_LDO_ANAMIC2] = {
		.constraints = {
			.name = "ab8500-vamic2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic2_consumers),
		.consumer_supplies = ab8500_vamic2_consumers,
	},
	/* supply for v-dmic, VDMIC LDO */
	[AB8500_LDO_DMIC] = {
		.constraints = {
			.name = "ab8500-vdmic",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vdmic_consumers),
		.consumer_supplies = ab8500_vdmic_consumers,
	},
	/* supply for U8500 CSI/DSI, VANA LDO */
	[AB8500_LDO_ANA] = {
		.constraints = {
			.name = "ab8500-vana",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vana_consumers),
		.consumer_supplies = ab8500_vana_consumers,
	},
};

/*
 * AB8500 external regulators
 */
static struct regulator_init_data gavini_ext_regulators[] = {
	/* fixed Vbat supplies VSMPS1_EXT_1V8 */
	[AB8500_EXT_SUPPLY1] = {
		.constraints = {
			.name = "ab8500-ext-supply1",
			.min_uV = 1800000,
			.max_uV = 1800000,
			.initial_mode = REGULATOR_MODE_IDLE,
			.boot_on = 1,
			.always_on = 1,
		},
	},
	/* fixed Vbat supplies VSMPS2_EXT_1V36 and VSMPS5_EXT_1V15 */
	[AB8500_EXT_SUPPLY2] = {
		.constraints = {
			.name = "ab8500-ext-supply2",
			.min_uV = 1360000,
			.max_uV = 1360000,
		},
	},
	/* fixed Vbat supplies VSMPS3_EXT_3V4 and VSMPS4_EXT_3V4 */
	[AB8500_EXT_SUPPLY3] = {
		.constraints = {
			.name = "ab8500-ext-supply3",
			.min_uV = 3400000,
			.max_uV = 3400000,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
			.boot_on = 1,
		},
	},
};

struct ab8500_regulator_platform_data gavini_regulator_plat_data = {
	.reg_init               = gavini_ab8500_regulator_reg_init,
	.num_reg_init           = ARRAY_SIZE(gavini_ab8500_regulator_reg_init),
	.regulator              = gavini_regulators,
	.num_regulator          = ARRAY_SIZE(gavini_regulators),
	.ext_regulator          = gavini_ext_regulators,
	.num_ext_regulator      = ARRAY_SIZE(gavini_ext_regulators),
};
