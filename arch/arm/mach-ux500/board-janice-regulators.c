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
#include <mach/gp2a.h>
#include <mach/mpu60x0.h>
#include "board-janice-regulators.h"

static struct regulator_consumer_supply ab8500_vaux1_consumers[] = {
	/* Proximity Sensor GP2A */
	REGULATOR_SUPPLY("v-prox-vcc", GP2A_I2C_DEVICE_NAME),
	/* Accel BMA022 */
	REGULATOR_SUPPLY("v-accel-vdd", "bma022"),
	/* Sensors MPU 60X0 or 3050 */
	REGULATOR_SUPPLY(MPU60X0_REGULATOR, MPU60X0_I2C_DEVICE_NAME),
	REGULATOR_SUPPLY("v_mpu_vdd", "8-0068"),
	REGULATOR_SUPPLY("v_sensor_3v", "mmio_camera"),
};

static struct regulator_consumer_supply ab8500_vaux2_consumers[] = {
	/* Proximity Sensor GP2A */
	REGULATOR_SUPPLY("v-prox-vio", GP2A_I2C_DEVICE_NAME),
	/* Accel BMA022 */
	REGULATOR_SUPPLY("v-accel-vio", "bma022"),
	/* Compass MMC3280 */
	REGULATOR_SUPPLY("v-compass", "mmc3280"),
	/* Sensors MPU 3050 */
	REGULATOR_SUPPLY("v_mpu_vlogic", "8-0068"),
	REGULATOR_SUPPLY("v_sensor_1v8", "mmio_camera"),	
};

static struct regulator_consumer_supply ab8500_vaux3_consumers[] = {
	REGULATOR_SUPPLY("v-SD-STM", "stm"),
	/* External MMC slot power */
	REGULATOR_SUPPLY("vmmc", "sdi0"),
};

static struct regulator_consumer_supply ab8500_vtvout_consumers[] = {
	/* TV-out DENC supply */
	REGULATOR_SUPPLY("vtvout", "ab8500-denc.0"),
	/* Internal general-purpose ADC */
	REGULATOR_SUPPLY("vddadc", "ab8500-gpadc.0"),
	/* ADC for charger */
	REGULATOR_SUPPLY("vddadc", "ab8500-charger.0"),
	/* AB8500 Tv-out device */
	REGULATOR_SUPPLY("vtvout", "mcde_tv_ab8500.4"),
};

static struct regulator_consumer_supply ab8500_vaudio_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-audio", NULL),
};

static struct regulator_consumer_supply ab8500_vamic1_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-amic1", NULL),
};

static struct regulator_consumer_supply ab8500_vamic2_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-amic2", NULL),
};

static struct regulator_consumer_supply ab8500_vdmic_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-dmic", NULL),
};

static struct regulator_consumer_supply ab8500_vintcore_consumers[] = {
	/* SoC core supply, no device */
	REGULATOR_SUPPLY("v-intcore", NULL),
	/* USB Transceiver */
	REGULATOR_SUPPLY("vddulpivio18", "ab8500-usb.0"),
};

static struct regulator_consumer_supply ab8500_vana_consumers[] = {
	/* DB8500 DSI */
	REGULATOR_SUPPLY("vdddsi1v2", "mcde"),
	/* DB8500 CSI */
	REGULATOR_SUPPLY("vddcsi1v2", "mmio_camera"),
	REGULATOR_SUPPLY("vdddsi1v2", "b2r2_core"),
	REGULATOR_SUPPLY("vdddsi1v2", "b2r2_1_core"),
};

static struct regulator_consumer_supply ab8500_sysclkreq_2_consumers[] = {
};

static struct regulator_consumer_supply ab8500_sysclkreq_4_consumers[] = {
};


/* ab8500 regulator register initialization */
static struct ab8500_regulator_reg_init
		janice_reg_init[AB8500_NUM_REGULATOR_REGISTERS] = {
	/*
	 * VanaRequestCtrl          = HP/LP depending on VxRequest
	 * VextSupply1RequestCtrl   = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL2,       0xF0, 0x00),
	/*
	 * VextSupply2RequestCtrl   = HP/LP depending on VxRequest
	 * VextSupply3RequestCtrl   = HP/LP depending on VxRequest
	 * Vaux1RequestCtrl         = HP/LP depending on VxRequest
	 * Vaux2RequestCtrl         = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL3,       0xFF, 0x00),
	/*
	 * Vaux3RequestCtrl         = HP/LP depending on VxRequest
	 * SwHPReq                  = Control through SWValid disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL4,       0x07, 0x00),
	/*
	 * VanaSysClkReq1HPValid    = disabled
	 * Vaux1SysClkReq1HPValid   = disabled
	 * Vaux2SysClkReq1HPValid   = disabled
	 * Vaux3SysClkReq1HPValid   = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQ1HPVALID1, 0xE8, 0x00),
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
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ1VALID1,     0xE8, 0x00),
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
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ2VALID1,     0xE8, 0x00),
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
	INIT_REGULATOR_REGISTER(AB8500_REGUSWHPREQVALID1,      0xA0, 0x00),
	/*
	 * Vaux2SwHPReqValid        = disabled
	 * Vaux3SwHPReqValid        = disabled
	 * VextSupply1SwHPReqValid  = disabled
	 * VextSupply2SwHPReqValid  = disabled
	 * VextSupply3SwHPReqValid  = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSWHPREQVALID2,      0x1F, 0x00),
	/*
	 * SysClkReq2Valid1         = disabled
	 * SysClkReq3Valid1         = disabled
	 * SysClkReq4Valid1         = disabled
	 * SysClkReq5Valid1         = disabled
	 * SysClkReq6Valid1         = disabled
	 * SysClkReq7Valid1         = disabled
	 * SysClkReq8Valid1         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQVALID1,    0xFE, 0x00),
	/*
	 * SysClkReq2Valid2         = disabled
	 * SysClkReq3Valid2         = disabled
	 * SysClkReq4Valid2         = disabled
	 * SysClkReq5Valid2         = disabled
	 * SysClkReq6Valid2         = disabled
	 * SysClkReq7Valid2         = disabled
	 * SysClkReq8Valid2         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQVALID2,    0xFE, 0x00),
	/*
	 * VTVoutEna                = disabled
	 * Vintcore12Ena            = disabled
	 * Vintcore12Sel            = 1.25 V
	 * Vintcore12LP             = inactive (HP)
	 * VTVoutLP                 = inactive (HP)
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUMISC1,              0xFE, 0x10),
	/*
	 * VaudioEna                = disabled
	 * VdmicEna                 = disabled
	 * Vamic1Ena                = disabled
	 * Vamic2Ena                = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUDIOSUPPLY,           0x1E, 0x00),
	/*
	 * Vamic1_dzout             = high-Z when Vamic1 is disabled
	 * Vamic2_dzout             = high-Z when Vamic2 is disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRL1VAMIC,         0x03, 0x00),
	/*
	 * VPll                     = Hw controlled (NOTE! PRCMU bits)
	 * VanaRegu                 = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VPLLVANAREGU,           0x0F, 0x02),
	/*
	 * VrefDDREna               = disabled
	 * VrefDDRSleepMode         = inactive (no pulldown)
	 */
	INIT_REGULATOR_REGISTER(AB8500_VREFDDR,                0x03, 0x00),
	/*
	 * No external regulators connected to AB8500 in Janice
	 */
	INIT_REGULATOR_REGISTER(AB8500_EXTSUPPLYREGU,          0xFF, 0x00),
	/*
	 * Vaux1Regu                = force HP
	 * Vaux2Regu                = force HP
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX12REGU,             0x0F, 0x05),
	/*
	 * Vaux3Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VRF1VAUX3REGU,          0x03, 0x00),
	/*
	 * Vaux1Sel                 = 3.0 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX1SEL,               0x0F, 0x0E),
	/*
	 * Vaux2Sel                 = 1.8 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX2SEL,               0x0F, 0x05),
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
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRLDISCH,          0xFC, 0x00),
	/*
	 * VanaDisch                = short discharge time
	 * VdmicPullDownEna         = pulldown disabled when Vdmic is disabled
	 * VdmicDisch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRLDISCH2,         0x16, 0x00),
};


/*
 * AB8500 regulators
 */
struct regulator_init_data janice_regulators[AB8500_NUM_REGULATORS] = {
	/* supplies to the Sensors 3V */
	/* 'REGULATOR_CHANGE_MODE' ops added to support LP mode */
	[AB8500_LDO_AUX1] = {
		.constraints = {
			.name = "V-SENSORS-VDD",
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
			.name = "V-SENSORS-VIO",
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
			.name = "V-MMC-SD",
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
	/* supply for tvout, gpadc, TVOUT LDO */
	[AB8500_LDO_TVOUT] = {
		.constraints = {
			.name = "V-TVOUT",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vtvout_consumers),
		.consumer_supplies = ab8500_vtvout_consumers,
	},
	/* supply for ab8500-vaudio, VAUDIO LDO */
	[AB8500_LDO_AUDIO] = {
		.constraints = {
			.name = "V-AUD",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaudio_consumers),
		.consumer_supplies = ab8500_vaudio_consumers,
	},
	/* supply for v-anamic1 VAMic1-LDO */
	[AB8500_LDO_ANAMIC1] = {
		.constraints = {
			.name = "V-AMIC1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic1_consumers),
		.consumer_supplies = ab8500_vamic1_consumers,
	},
	/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
	[AB8500_LDO_ANAMIC2] = {
		.constraints = {
			.name = "V-AMIC2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic2_consumers),
		.consumer_supplies = ab8500_vamic2_consumers,
	},
	/* supply for v-dmic, VDMIC LDO */
	[AB8500_LDO_DMIC] = {
		.constraints = {
			.name = "V-DMIC",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vdmic_consumers),
		.consumer_supplies = ab8500_vdmic_consumers,
	},
	/* supply for v-intcore12, VINTCORE12 LDO */
	[AB8500_LDO_INTCORE] = {
		.constraints = {
			.name = "V-INTCORE",
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
	/* supply for U8500 CSI/DSI, VANA LDO */
	[AB8500_LDO_ANA] = {
		.constraints = {
			.name = "V-CSI_DSI",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vana_consumers),
		.consumer_supplies = ab8500_vana_consumers,
	},
	/* sysclkreq 2 pin */
	[AB8500_SYSCLKREQ_2] = {
		.constraints = {
			.name = "V-SYSCLKREQ-2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies =
			ARRAY_SIZE(ab8500_sysclkreq_2_consumers),
		.consumer_supplies = ab8500_sysclkreq_2_consumers,
	},
	/* sysclkreq 4 pin */
	[AB8500_SYSCLKREQ_4] = {
		.constraints = {
			.name = "V-SYSCLKREQ-4",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies =
			ARRAY_SIZE(ab8500_sysclkreq_4_consumers),
		.consumer_supplies = ab8500_sysclkreq_4_consumers,
	},
};

/*
 * AB8500 external regulators
 */
static struct regulator_init_data janice_ext_regulators[] = {
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

struct ab8500_regulator_platform_data janice_regulator_plat_data = {
	.reg_init               = janice_reg_init,
	.num_reg_init           = ARRAY_SIZE(janice_reg_init),
	.regulator              = janice_regulators,
	.num_regulator          = ARRAY_SIZE(janice_regulators),
	.ext_regulator          = janice_ext_regulators,
	.num_ext_regulator      = ARRAY_SIZE(janice_ext_regulators),
};

