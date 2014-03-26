/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/ab5500.h>

#include "regulator-u5500.h"
#include "board-u5500.h"

/*
 * AB5500
 */

static struct regulator_consumer_supply ab5500_ldo_g_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sdi1"),
};

static struct regulator_consumer_supply ab5500_ldo_h_consumers[] = {
	REGULATOR_SUPPLY("vddi", "mcde_disp_sony_acx424akp.0"),
	REGULATOR_SUPPLY("vdd", "1-004b"), /* Synaptics */
	REGULATOR_SUPPLY("vin", "2-0036"), /* LM3530 */
	REGULATOR_SUPPLY("vcpin", "spi1.0"),
	REGULATOR_SUPPLY("v-ana", "mmio_camera"),
	REGULATOR_SUPPLY("vdd", "2-0019"),
	REGULATOR_SUPPLY("vdd", "2-001e"),
};

static struct regulator_consumer_supply ab5500_ldo_k_consumers[] = {
	REGULATOR_SUPPLY("v-mmio-camera", "mmio_camera"),
};

static struct regulator_consumer_supply ab5500_ldo_h_consumers_pre_r3a[] = {
	REGULATOR_SUPPLY("vddi", "mcde_disp_sony_acx424akp.0"),
	REGULATOR_SUPPLY("vdd", "1-004b"), /* Synaptics */
	REGULATOR_SUPPLY("vin", "2-0036"), /* LM3530 */
	REGULATOR_SUPPLY("vcpin", "spi1.0"),
	REGULATOR_SUPPLY("v-ana", "mmio_camera"),
};

static struct regulator_consumer_supply ab5500_ldo_k_consumers_pre_r3a[] = {
	REGULATOR_SUPPLY("vdd", "lsm303dlh.0"),
	REGULATOR_SUPPLY("vdd", "lsm303dlh.1"),
	REGULATOR_SUPPLY("v-mmio-camera", "mmio_camera"),
};

static struct regulator_consumer_supply ab5500_ldo_l_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sdi0"),
	REGULATOR_SUPPLY("vmmc", "sdi2"),
};

static struct regulator_consumer_supply ab5500_ldo_vdigmic_consumers[] = {
	REGULATOR_SUPPLY("vdigmic", "ab5500-codec.0"),
};

static struct regulator_consumer_supply ab5500_ldo_sim_consumers[] = {
	REGULATOR_SUPPLY("debug", "reg-virt-consumer.5"),
};

static struct regulator_consumer_supply ab5500_bias2_consumers[] = {
	REGULATOR_SUPPLY("v-amic", NULL),
};

static struct regulator_init_data
ab5500_regulator_init_data[AB5500_NUM_REGULATORS] = {
	/* SD Card */
	[AB5500_LDO_G] = {
		.constraints = {
			.min_uV		= 1200000,
			.max_uV		= 2910000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.consumer_supplies	= ab5500_ldo_g_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(ab5500_ldo_g_consumers),
	},
	/* Display */
	[AB5500_LDO_H] = {
		.constraints = {
			.min_uV		= 2790000,
			.max_uV		= 2790000,
			.apply_uV	= 1,
			.boot_on	= 1, /* display on during boot */
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies	= ab5500_ldo_h_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(ab5500_ldo_h_consumers),
	},
	/* Camera */
	[AB5500_LDO_K] = {
		.constraints = {
			.min_uV		= 2790000,
			.max_uV		= 2790000,
			.apply_uV	= 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies	= ab5500_ldo_k_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(ab5500_ldo_k_consumers),
	},
	/* External eMMC */
	[AB5500_LDO_L] = {
		.constraints = {
			.min_uV		= 1200000,
			.max_uV		= 2910000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.consumer_supplies	= ab5500_ldo_l_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(ab5500_ldo_l_consumers),
	},
	[AB5500_LDO_VDIGMIC] = {
		.constraints = {
			.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies	= ab5500_ldo_vdigmic_consumers,
		.num_consumer_supplies	=
			ARRAY_SIZE(ab5500_ldo_vdigmic_consumers),
	},
	[AB5500_LDO_SIM] = {
		.constraints = {
			.min_uV		= 1875000,
			.max_uV		= 2900000,
			.apply_uV	= 1,
			.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies	= ab5500_ldo_sim_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(ab5500_ldo_sim_consumers),
	},
	[AB5500_BIAS2] = {
		.constraints = {
			.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies	= ab5500_bias2_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(ab5500_bias2_consumers),
	},
};

static struct ab5500_regulator_data
ab5500_regulator_data[AB5500_NUM_REGULATORS] = {
	[AB5500_LDO_H] = {
		/*
		 * The sub camera on the dev boards needs both supplies to be
		 * on to avoid high leakage.
		 */
		.off_is_lowpower = true,
	},
};

struct ab5500_regulator_platform_data u5500_ab5500_regulator_data = {
	.regulator	= ab5500_regulator_init_data,
	.data		= ab5500_regulator_data,
	.num_regulator	= ARRAY_SIZE(ab5500_regulator_init_data),
};


static void __init u5500_regulators_init_debug(void)
{
	const char data[] = "debug";
	int i;

	for (i = 0; i < 6; i++)
		platform_device_register_data(NULL, "reg-virt-consumer", i,
			data, sizeof(data));
}

static struct regulator_consumer_supply u5500_vio_consumers[] = {
	REGULATOR_SUPPLY("gbf_1v8", "cg2900-uart.0"),
};

static struct regulator_init_data u5500_vio_init_data = {
	.constraints.always_on	= 1,
	.consumer_supplies	= u5500_vio_consumers,
	.num_consumer_supplies	= ARRAY_SIZE(u5500_vio_consumers),
};

static struct fixed_voltage_config u5500_vio_pdata __initdata = {
	.supply_name	= "vio_1v8",
	.microvolts	= 1800000,
	.init_data	= &u5500_vio_init_data,
	.gpio		= -EINVAL,
};

void __init u5500_regulators_init(void)
{
	if (u5500_board_is_pre_r3a()) {
		struct regulator_init_data *rid = ab5500_regulator_init_data;

		rid[AB5500_LDO_K].consumer_supplies
			= ab5500_ldo_k_consumers_pre_r3a;
		rid[AB5500_LDO_K].num_consumer_supplies
			= ARRAY_SIZE(ab5500_ldo_k_consumers_pre_r3a);

		rid[AB5500_LDO_H].consumer_supplies
			= ab5500_ldo_h_consumers_pre_r3a;
		rid[AB5500_LDO_H].num_consumer_supplies
			= ARRAY_SIZE(ab5500_ldo_h_consumers_pre_r3a);
	}

	u5500_regulators_init_debug();

	platform_device_register_data(NULL, "reg-fixed-voltage", -1,
				      &u5500_vio_pdata,
				      sizeof(u5500_vio_pdata));

	regulator_has_full_constraints();
}
