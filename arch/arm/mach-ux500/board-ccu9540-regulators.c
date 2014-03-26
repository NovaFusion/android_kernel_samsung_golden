/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Alexandre Torgue <alexandre.torgue@stericsson.com>
 *
 *
 * CCU9540 board specific initialization for regulators
 */
#include <linux/kernel.h>
#include <asm/mach-types.h>
#include <mach/id.h> /* to identify older boards for fixes */
#include "board-ccu9540-regulators.h"

static struct regulator_consumer_supply ab9540_vaux1_consumers[] = {
	/* Main display, u8500 R3 uib */
	REGULATOR_SUPPLY("vddi", "mcde_disp_sony_acx424akp.0"),
	/* Main display, u8500 uib and ST uib */
	REGULATOR_SUPPLY("vdd1", "samsung_s6d16d0.0"),
	/* Secondary display, ST uib */
	REGULATOR_SUPPLY("vdd1", "samsung_s6d16d0.1"),
	/* Main display on tablet UIB */
	REGULATOR_SUPPLY("vdd1", "toshiba_dsi2lvds.0"),
	/* Main display, U9540_S_V2 UIB */
	REGULATOR_SUPPLY("vddi", "himax_hx8392.0"),
	/* SFH7741 proximity sensor */
	REGULATOR_SUPPLY("vcc", "gpio-keys.0"),
	/* BH1780GLS ambient light sensor */
	REGULATOR_SUPPLY("vcc", "2-0029"),
	/* lsm303dlh accelerometer */
	REGULATOR_SUPPLY("vdd", "lsm303dlh.0"),
	/* lsm303dlh magnetometer */
	REGULATOR_SUPPLY("vdd", "lsm303dlh.1"),
	/* Rohm BU21013 Touchscreen devices */
	REGULATOR_SUPPLY("avdd", "3-005c"),
	REGULATOR_SUPPLY("avdd", "3-005d"),
	/* Synaptics RMI4 Touchscreen device */
	REGULATOR_SUPPLY("vdd", "3-004b"),
	/* L3G4200D Gyroscope device */
	REGULATOR_SUPPLY("vdd", "l3g4200d"),
	/* Proximity and Hal sensor device */
	REGULATOR_SUPPLY("vdd", "sensor1p.0"),
	/* Ambient light sensor device */
	REGULATOR_SUPPLY("vdd", "3-0029"),
	/* Pressure sensor device */
	REGULATOR_SUPPLY("vdd", "2-005c"),
	/* Cypress TrueTouch Touchscreen device */
	REGULATOR_SUPPLY("vcpin", "spi8.0"),
	/* Camera device */
	REGULATOR_SUPPLY("vaux12v5", "mmio_camera"),
	REGULATOR_SUPPLY("vaux12v5", "mmio_camera_raw"),
	REGULATOR_SUPPLY("vaux12v5", "mmio_camera_yuv"),
};

static struct regulator_consumer_supply ab9540_vaux2_consumers[] = {
	/* On-board eMMC power */
	REGULATOR_SUPPLY("vmmc", "sdi4"),
	/* AB8500 audio codec */
	REGULATOR_SUPPLY("vcc-N2158", "ab8500-codec.0"),
	/* AB8500 accessory detect 1 */
	REGULATOR_SUPPLY("vcc-N2158", "ab8500-acc-det.0"),
	/* AB8500 Tv-out device */
	REGULATOR_SUPPLY("vcc-N2158", "mcde_tv_ab8500.4"),
	/* AV8100 HDMI device */
	REGULATOR_SUPPLY("vcc-N2158", "av8100_hdmi.3"),
};

static struct regulator_consumer_supply ab9540_vaux3_consumers[] = {
	REGULATOR_SUPPLY("v-SD-STM", "stm"),
	/* External MMC slot power */
	REGULATOR_SUPPLY("vmmc", "sdi0"),
};
static struct regulator_consumer_supply ab9540_vaux4_consumers[] = {
	/* NFC and standalone secure element device */
	REGULATOR_SUPPLY("vnfc-se", "st21nfca"),
};

static struct regulator_consumer_supply ab9540_vtvout_consumers[] = {
	/* TV-out DENC supply */
	REGULATOR_SUPPLY("vtvout", "ab8500-denc.0"),
	/* Internal general-purpose ADC */
	REGULATOR_SUPPLY("vddadc", "ab8500-gpadc.0"),
	/* ADC for charger */
	REGULATOR_SUPPLY("vddadc", "ab8500-charger.0"),
	/* AB8500 Tv-out device */
	REGULATOR_SUPPLY("vtvout", "mcde_tv_ab8500.4"),
};

static struct regulator_consumer_supply ab9540_vaudio_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-audio", NULL),
};

static struct regulator_consumer_supply ab9540_vamic1_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-amic1", NULL),
};

static struct regulator_consumer_supply ab9540_vamic2_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-amic2", NULL),
};

static struct regulator_consumer_supply ab9540_vdmic_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-dmic", NULL),
};

static struct regulator_consumer_supply ab9540_vintcore_consumers[] = {
	/* SoC core supply, no device */
	REGULATOR_SUPPLY("v-intcore", NULL),
	/* USB Transceiver */
	REGULATOR_SUPPLY("vddulpivio18", "ab9540-usb.0"),
};

static struct regulator_consumer_supply ab9540_vana_consumers[] = {
	/* DB8500 DSI */
	REGULATOR_SUPPLY("vdddsi1v2", "mcde"),
	REGULATOR_SUPPLY("vdddsi1v2", "dsilink.0"),
	REGULATOR_SUPPLY("vdddsi1v2", "dsilink.1"),
	REGULATOR_SUPPLY("vdddsi1v2", "dsilink.2"),
	/* DB8500 CSI */
	REGULATOR_SUPPLY("vddcsi1v2", "mmio_camera"),
	REGULATOR_SUPPLY("vddcsi1v2", "mmio_camera_raw"),
	REGULATOR_SUPPLY("vddcsi1v2", "mmio_camera_yuv"),
};

static struct regulator_consumer_supply ab9540_sysclkreq_2_consumers[] = {
	/* CG2900 device */
	REGULATOR_SUPPLY("gbf_1v8", "cg2900-uart.0"),
};

static struct regulator_consumer_supply ab9540_sysclkreq_4_consumers[] = {
	/* CW1200 device */
	REGULATOR_SUPPLY("wlan_1v8", "cw1200_wlan.0"),
};

/* ab9540 regulator register initialization */
struct ab8500_regulator_reg_init
ab9540_regulator_reg_init[AB9540_NUM_REGULATOR_REGISTERS] = {
	/*
	 * VanaRequestCtrl          = HP/LP depending on VxRequest
	 * VpllRequestCtrl          = HP/LP depending on VxRequest
	 * VextSupply1RequestCtrl   = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUREQUESTCTRL2,       0xfc, 0x00),
	/*
	 * VextSupply2RequestCtrl   = HP/LP depending on VxRequest
	 * VextSupply3RequestCtrl   = HP/LP depending on VxRequest
	 * Vaux1RequestCtrl         = HP/LP depending on VxRequest
	 * Vaux2RequestCtrl         = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUREQUESTCTRL3,       0xff, 0x00),
	/*
	 * Vaux3RequestCtrl         = HP/LP depending on VxRequest
	 * SwHPReq                  = Control through SWValid disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUREQUESTCTRL4,       0x07, 0x00),
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
	INIT_REGULATOR_REGISTER(AB9540_REGUSYSCLKREQ1HPVALID1, 0xff, 0x17),
	/*
	 * VextSupply1SysClkReq1HPValid = disabled
	 * VextSupply2SysClkReq1HPValid = disabled
	 * VextSupply3SysClkReq1HPValid = SysClkReq1 controlled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUSYSCLKREQ1HPVALID2, 0x70, 0x40),
	/*
	 * VanaHwHPReq1Valid        = disabled
	 * Vaux1HwHPreq1Valid       = disabled
	 * Vaux2HwHPReq1Valid       = disabled
	 * Vaux3HwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUHWHPREQ1VALID1,     0xe8, 0x00),
	/*
	 * VextSupply1HwHPReq1Valid = disabled
	 * VextSupply2HwHPReq1Valid = disabled
	 * VextSupply3HwHPReq1Valid = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUHWHPREQ1VALID2,     0x07, 0x00),
	/*
	 * VanaHwHPReq2Valid        = disabled
	 * Vaux1HwHPReq2Valid       = disabled
	 * Vaux2HwHPReq2Valid       = disabled
	 * Vaux3HwHPReq2Valid       = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUHWHPREQ2VALID1,     0xe8, 0x00),
	/*
	 * VextSupply1HwHPReq2Valid = disabled
	 * VextSupply2HwHPReq2Valid = disabled
	 * VextSupply3HwHPReq2Valid = HWReq2 controlled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUHWHPREQ2VALID2,     0x07, 0x04),
	/*
	 * VanaSwHPReqValid         = disabled
	 * Vaux1SwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUSWHPREQVALID1,      0xa0, 0x00),
	/*
	 * Vaux2SwHPReqValid        = disabled
	 * Vaux3SwHPReqValid        = disabled
	 * VextSupply1SwHPReqValid  = disabled
	 * VextSupply2SwHPReqValid  = disabled
	 * VextSupply3SwHPReqValid  = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUSWHPREQVALID2,      0x1f, 0x00),
	/*
	 * SysClkReq2Valid1         = SysClkReq2 controlled
	 * SysClkReq3Valid1         = disabled
	 * SysClkReq4Valid1         = SysClkReq4 controlled
	 * SysClkReq5Valid1         = disabled
	 * SysClkReq6Valid1         = SysClkReq6 controlled
	 * SysClkReq7Valid1         = disabled
	 * SysClkReq8Valid1         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUSYSCLKREQVALID1,    0xfe, 0x2a),
	/*
	 * SysClkReq2Valid2         = disabled
	 * SysClkReq3Valid2         = disabled
	 * SysClkReq4Valid2         = disabled
	 * SysClkReq5Valid2         = disabled
	 * SysClkReq6Valid2         = SysClkReq6 controlled
	 * SysClkReq7Valid2         = disabled
	 * SysClkReq8Valid2         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUSYSCLKREQVALID2,    0xfe, 0x20),
	/*
	 * Vaux4SwHPReqValid        = disabled
	 * Vaux4HwHPReq2Valid       = disabled
	 * Vaux4HwHPReq1Valid       = disabled
	 * Vaux4SysClkReq1HPValid   = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUVAUX4REQVALID,      0x0f, 0x00),
	/*
	 * VTVoutEna                = disabled
	 * Vintcore12Ena            = disabled
	 * Vintcore12Sel            = 1.25 V
	 * Vintcore12LP             = inactive (HP)
	 * VTVoutLP                 = inactive (HP)
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUMISC1,              0xfe, 0x10),
	/*
	 * VaudioEna                = disabled
	 * VdmicEna                 = disabled
	 * Vamic1Ena                = disabled
	 * Vamic2Ena                = disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUDIOSUPPLY,           0x1e, 0x00),
	/*
	 * Vamic1_dzout             = high-Z when Vamic1 is disabled
	 * Vamic2_dzout             = high-Z when Vamic2 is disabled
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUCTRL1VAMIC,         0x03, 0x00),
	/*
	 * Vsmps1Regu               = HW control
	 * Vsmps1SelCtrl            = Vsmps1 voltage defined by Vsmsp1Sel2
	 */
	INIT_REGULATOR_REGISTER(AB9540_VSMPS1REGU,             0x0f, 0x06),
	/*
	 * Vsmps2Regu               = HW control
	 * Vsmps2SelCtrl            = Vsmps2 voltage defined by Vsmsp2Sel2
	 */
	INIT_REGULATOR_REGISTER(AB9540_VSMPS2REGU,             0x0f, 0x06),
	/*
	 * VPll                     = Hw controlled
	 * VanaRegu                 = force off
	 */
	INIT_REGULATOR_REGISTER(AB9540_VPLLVANAREGU,           0x0f, 0x02),
	/*
	 * VextSupply1Regu          = HW control
	 * VextSupply2Regu          = HW control
	 * VextSupply3Regu          = Low Power mode
	 * ExtSupply2Bypass         = ExtSupply12LPn ball is 0 when Ena is 0
	 * ExtSupply3Bypass         = ExtSupply3LPn ball is 0 when Ena is 0
	 */
	INIT_REGULATOR_REGISTER(AB9540_EXTSUPPLYREGU,          0xff, 0x1a),
	/*
	 * Vaux1Regu                = force HP
	 * Vaux2Regu                = force on
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUX12REGU,             0x0f, 0x05),
	/*
	 * Vrf1Regu                 = HW control
	 * Vaux3Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB9540_VRF1VAUX3REGU,          0x0f, 0x08),
	/*
	 * Vsmps1Sel1               = 1.2 V
	 */
	INIT_REGULATOR_REGISTER(AB9540_VSMPS1SEL1,             0x3f, 0x28),
	/*
	 * Vaux1Sel                 = 2.8 V
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUX1SEL,               0x0f, 0x0C),
	/*
	 * Vaux2Sel                 = 2.9 V
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUX2SEL,               0x0f, 0x0d),
	/*
	 * Vaux3Sel                 = 2.91 V
	 */
	INIT_REGULATOR_REGISTER(AB9540_VRF1VAUX3SEL,           0x07, 0x07),
	/*
	 * VextSupply12LP           = disabled (no LP)
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUCTRL2SPARE,         0x01, 0x00),
	/*
	 * Vaux4RequestCtrl         = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUX4REQCTRL,           0x03, 0x00),
	/*
	 * Vaux4Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUX4REGU,              0x03, 0x00),
	/*
	 * Vaux4Sel                 = 2.5 V
	 */
	INIT_REGULATOR_REGISTER(AB9540_VAUX4SEL,               0x0f, 0x08),
	/*
	 * Vaux1Disch               = short discharge time
	 * Vaux2Disch               = short discharge time
	 * Vaux3Disch               = short discharge time
	 * Vintcore12Disch          = short discharge time
	 * VTVoutDisch              = short discharge time
	 * VaudioDisch              = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUCTRLDISCH,          0xfc, 0x00),
	/*
	 * VanaDisch                = short discharge time
	 * VdmicPullDownEna         = pulldown disabled when Vdmic is disabled
	 * VdmicDisch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUCTRLDISCH2,         0x16, 0x00),
	/*
	 * Vaux4Disch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB9540_REGUCTRLDISCH3,         0x01, 0x00),
};

struct regulator_init_data ab9540_regulators[AB9540_NUM_REGULATORS] = {
	/* supplies to the display/camera */
	[AB9540_LDO_AUX1] = {
		.constraints = {
			.name = "V-DISPLAY",
			.min_uV = 2800000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
			.boot_on = 1, /* display is on at boot */
			.always_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vaux1_consumers),
		.consumer_supplies = ab9540_vaux1_consumers,
	},
	/* supplies to the on-board eMMC */
	[AB9540_LDO_AUX2] = {
		.constraints = {
			.name = "V-eMMC1",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
			/* eMMC supply must be permanently enabled */
			.always_on = 1,
			.boot_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vaux2_consumers),
		.consumer_supplies = ab9540_vaux2_consumers,
	},
	/* supply for VAUX3, supplies to SDcard slots */
	[AB9540_LDO_AUX3] = {
		.constraints = {
			.name = "V-MMC-SD",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vaux3_consumers),
		.consumer_supplies = ab9540_vaux3_consumers,
	},
	/* supply for VAUX4, supplies to NFC and standalone secure element */
	[AB9540_LDO_AUX4] = {
		.constraints = {
			.name = "V-NFC-SE",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vaux4_consumers),
		.consumer_supplies = ab9540_vaux4_consumers,
	},
	/* supply for tvout, gpadc, TVOUT LDO */
	[AB9540_LDO_TVOUT] = {
		.constraints = {
			.name = "V-TVOUT",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vtvout_consumers),
		.consumer_supplies = ab9540_vtvout_consumers,
	},
	/* supply for ab8500-vaudio, VAUDIO LDO */
	[AB9540_LDO_AUDIO] = {
		.constraints = {
			.name = "V-AUD",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vaudio_consumers),
		.consumer_supplies = ab9540_vaudio_consumers,
	},
	/* supply for v-anamic1 VAMic1-LDO */
	[AB9540_LDO_ANAMIC1] = {
		.constraints = {
			.name = "V-AMIC1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vamic1_consumers),
		.consumer_supplies = ab9540_vamic1_consumers,
	},
	/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
	[AB9540_LDO_ANAMIC2] = {
		.constraints = {
			.name = "V-AMIC2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vamic2_consumers),
		.consumer_supplies = ab9540_vamic2_consumers,
	},
	/* supply for v-dmic, VDMIC LDO */
	[AB9540_LDO_DMIC] = {
		.constraints = {
			.name = "V-DMIC",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vdmic_consumers),
		.consumer_supplies = ab9540_vdmic_consumers,
	},
	/* supply for v-intcore12, VINTCORE12 LDO */
	[AB9540_LDO_INTCORE] = {
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
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vintcore_consumers),
		.consumer_supplies = ab9540_vintcore_consumers,
	},
	/* supply for U8500 CSI-DSI, VANA LDO */
	[AB9540_LDO_ANA] = {
		.constraints = {
			.name = "V-CSI-DSI",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab9540_vana_consumers),
		.consumer_supplies = ab9540_vana_consumers,
	},
	/* sysclkreq 2 pin */
	[AB9540_SYSCLKREQ_2] = {
		.constraints = {
			.name = "V-SYSCLKREQ-2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies =
			ARRAY_SIZE(ab9540_sysclkreq_2_consumers),
		.consumer_supplies = ab9540_sysclkreq_2_consumers,
	},
	/* sysclkreq 4 pin */
	[AB9540_SYSCLKREQ_4] = {
		.constraints = {
			.name = "V-SYSCLKREQ-4",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies =
			ARRAY_SIZE(ab9540_sysclkreq_4_consumers),
		.consumer_supplies = ab9540_sysclkreq_4_consumers,
	},
};


/*
 * AB9540 external regulators
 */
static struct regulator_init_data ab9540_ext_regulators[] = {

	[AB8500_EXT_SUPPLY1] = {
		.constraints = {
			.name = "ab8500-ext-supply1",
			.min_uV = 4500000,
			.max_uV = 4500000,
			.initial_mode = REGULATOR_MODE_IDLE,
			.boot_on = 1,
			.always_on = 1,
		},
	},
	/*not already hardwired */
	[AB8500_EXT_SUPPLY2] = {
		.constraints = {
			.name = "ab8500-ext-supply2",
		},
	},

	[AB8500_EXT_SUPPLY3] = {
		.constraints = {
			.name = "ab8500-ext-supply3",
			.min_uV = 3300000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
			.boot_on = 1,
		},
	},
};

struct ab8500_regulator_platform_data ab9540_regulator_plat_data = {
	.reg_init               = ab9540_regulator_reg_init,
	.num_reg_init           = ARRAY_SIZE(ab9540_regulator_reg_init),
	.regulator              = ab9540_regulators,
	 .num_regulator          = ARRAY_SIZE(ab9540_regulators),
	.ext_regulator          = ab9540_ext_regulators,
	.num_ext_regulator      = ARRAY_SIZE(ab9540_ext_regulators),
};


