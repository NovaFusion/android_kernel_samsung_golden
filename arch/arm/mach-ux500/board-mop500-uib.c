/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2
 */

#define pr_fmt(fmt)	"mop500-uib: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/regulator/consumer.h>

#include <mach/hardware.h>
#include "pins.h"
#include "board-mop500.h"

enum mop500_uib {
	STUIB,
	U8500UIB,
	U8500UIB_R3,
	U9540UIBS_V1,
	U9540UIBS_V2,
	U9540UIBT_V1,
};

struct uib {
	const char *name;
	const char *option;
	void (*init)(void);
};

static u8 type_of_uib;

static struct uib __initdata mop500_uibs[] = {
	[STUIB] = {
		.name	= "ST-UIB",
		.option	= "stuib",
		.init	= mop500_stuib_init,
	},
	[U8500UIB] = {
		.name	= "U8500-UIB",
		.option	= "u8500uib",
		.init	= mop500_u8500uib_init,
	},
	[U8500UIB_R3] = {
		.name   = "U8500-UIBR3",
		.option = "u8500uibr3",
		.init   = mop500_u8500uib_r3_init,
	},
	[U9540UIBS_V1] = {
		.name   = "U9540-UIBS_V1",
		.option = "u9540uibs_v1",
		.init   = mop500_u9540uibs_v1_init,
	},
	[U9540UIBS_V2] = {
		.name   = "U9540-UIBS_V2",
		.option = "u9540uibs_v2",
		.init   = mop500_u9540uibs_v2_init,
	},
	[U9540UIBT_V1] = {
		.name   = "U9540-UIBT_V1",
		.option = "u9540uibt_v1",
		.init   = mop500_u9540uibt_v1_init,
	},
};

static struct uib __initdata *mop500_uib;

static int __init mop500_uib_setup(char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mop500_uibs); i++) {
		struct uib *uib = &mop500_uibs[i];

		if (!strcmp(str, uib->option)) {
			mop500_uib = uib;
			break;
		}
	}

	if (i == ARRAY_SIZE(mop500_uibs))
		pr_err("invalid uib= option (%s)\n", str);

	return 1;
}
__setup("uib=", mop500_uib_setup);

/*
 * The UIBs are detected after the I2C host controllers are registered, so
 * i2c_register_board_info() can't be used.
 */
void mop500_uib_i2c_add(int busnum, struct i2c_board_info const *info,
		unsigned n)
{
	struct i2c_adapter *adap;
	struct i2c_client *client;
	int i;

	adap = i2c_get_adapter(busnum);
	if (!adap) {
		pr_err("failed to get adapter i2c%d\n", busnum);
		return;
	}

	for (i = 0; i < n; i++) {
		client = i2c_new_device(adap, &info[i]);
		if (!client)
			pr_err("failed to register %s to i2c%d\n",
					info[i].type, busnum);
	}

	i2c_put_adapter(adap);
}

static void __init __mop500_uib_init(struct uib *uib, const char *why)
{
	pr_info("%s (%s)\n", uib->name, why);

	if (strcmp("stuib", uib->option) == 0)
		type_of_uib = STUIB;
	else if (strcmp("u8500uib", uib->option) == 0)
		type_of_uib = U8500UIB;
	else if (strcmp("u8500uibr3", uib->option) == 0)
		type_of_uib = U8500UIB_R3;
	else if (strcmp("u9540uibs_v1", uib->option) == 0)
		type_of_uib = U9540UIBS_V1;
	else if (strcmp("u9540uibs_v2", uib->option) == 0)
		type_of_uib = U9540UIBS_V2;
	else if (strcmp("u9540uibt_v1", uib->option) == 0)
		type_of_uib = U9540UIBT_V1;

	uib->init();
}

int uib_is_stuib(void)
{
	return (type_of_uib == STUIB);
}

int uib_is_u8500uib(void)
{
	return (type_of_uib == U8500UIB);
}

int uib_is_u8500uibr3(void)
{
	return (type_of_uib == U8500UIB_R3);
}

int uib_is_u9540uibs_v1(void)
{
	return (type_of_uib == U9540UIBS_V1);
}

int uib_is_u9540uibs_v2(void)
{
	return (type_of_uib == U9540UIBS_V2);
}

int uib_is_u9540uibt_v1(void)
{
	return (type_of_uib == U9540UIBT_V1);
}

int uib_is_u9540uibs(void)
{
	return uib_is_u9540uibs_v1() || uib_is_u9540uibs_v2();
}

int uib_is_u9540uibt(void)
{
	return uib_is_u9540uibt_v1();
}


#ifdef CONFIG_UX500_GPIO_KEYS
static struct gpio_keys_button mop500_gpio_keys[] = {
	{
		.desc			= "SFH7741 Proximity Sensor",
		.type			= EV_SW,
		.code			= SW_FRONT_PROXIMITY,
		.active_low		= 0,
		.can_disable		= 1,
	},
	{
		.desc			= "HED54XXU11 Hall Effect Sensor",
		.type			= EV_SW,
		.code			= SW_LID, /* FIXME arbitrary usage */
		.active_low		= 0,
		.can_disable		= 1,
	}
};

static struct regulator *gpio_keys_regulator;
static int mop500_gpio_keys_activate(struct device *dev);
static void mop500_gpio_keys_deactivate(struct device *dev);

static struct gpio_keys_platform_data mop500_gpio_keys_data = {
	.buttons	= mop500_gpio_keys,
	.nbuttons	= ARRAY_SIZE(mop500_gpio_keys),
	.enable		= mop500_gpio_keys_activate,
	.disable	= mop500_gpio_keys_deactivate,
};

static struct platform_device mop500_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &mop500_gpio_keys_data,
	},
};

static int mop500_gpio_keys_activate(struct device *dev)
{
	gpio_keys_regulator = regulator_get(&mop500_gpio_keys_device.dev,
						"vcc");
	if (IS_ERR(gpio_keys_regulator)) {
		dev_err(&mop500_gpio_keys_device.dev, "no regulator\n");
		return PTR_ERR(gpio_keys_regulator);
	}
	regulator_enable(gpio_keys_regulator);

	/*
	 * Please be aware that the start-up time of the SFH7741 is
	 * 120 ms and during that time the output is undefined.
	 */

	return 0;
}

static void mop500_gpio_keys_deactivate(struct device *dev)
{
	if (!IS_ERR(gpio_keys_regulator)) {
		regulator_disable(gpio_keys_regulator);
		regulator_put(gpio_keys_regulator);
	}
}

static __init void mop500_gpio_keys_init(void)
{
	struct ux500_pins *gpio_keys_pins = ux500_pins_get("gpio-keys.0");

	if (gpio_keys_pins == NULL) {
		pr_err("gpio_keys: Fail to get pins\n");
		return;
	}

	ux500_pins_enable(gpio_keys_pins);
	if (type_of_uib == U8500UIB_R3)
		mop500_gpio_keys[0].gpio = PIN_NUM(gpio_keys_pins->cfg[2]);
	else
		mop500_gpio_keys[0].gpio = PIN_NUM(gpio_keys_pins->cfg[0]);
	mop500_gpio_keys[1].gpio = PIN_NUM(gpio_keys_pins->cfg[1]);
}
#else
static inline void mop500_gpio_keys_init(void) { }
#endif

/*
 * Check which accelerometer chip is mounted on UIB and
 * read the chip ID to detect whether chip is LSM303DHL/LSM303DHLC.
 */
int mop500_get_acc_id(void)
{
	int status;
	union i2c_smbus_data data;
	struct i2c_adapter *i2c2;

	i2c2 = i2c_get_adapter(2);
	if (!i2c2) {
		pr_err("failed to get i2c adapter\n");
		return -1;
	}
	status = i2c_smbus_xfer(i2c2, 0x18 , 0 ,
			I2C_SMBUS_READ, 0x0F ,
			I2C_SMBUS_BYTE_DATA, &data);
	if (status < 0) {
		status = i2c_smbus_xfer(i2c2, 0x19 , 0 ,
				I2C_SMBUS_READ, 0x0F ,
				I2C_SMBUS_BYTE_DATA, &data);
	}
	i2c_put_adapter(i2c2);
	return (status < 0) ? status : data.byte;
}

/* add any platform devices here - TODO */
static struct platform_device *mop500_uib_platform_devs[] __initdata = {
#ifdef CONFIG_UX500_GPIO_KEYS
	&mop500_gpio_keys_device,
#endif
};

/*
 * Detect the UIB attached based on the presence or absence of i2c devices.
 */
static int __init u8500_uib_init(void)
{
	struct uib *uib = mop500_uibs;
	struct i2c_adapter *i2c0;
	struct i2c_adapter *i2c3;
	int ret;

	i2c0 = i2c_get_adapter(0);
	if (!i2c0) {
		__mop500_uib_init(&mop500_uibs[STUIB],
				"fallback, could not get i2c0");
		return -ENODEV;
	}

	/* U8500-UIB has the TC35893 at 0x44 on I2C0, the ST-UIB doesn't. */
	ret = i2c_smbus_xfer(i2c0, 0x44, 0, I2C_SMBUS_WRITE, 0,
			I2C_SMBUS_QUICK, NULL);
	i2c_put_adapter(i2c0);
	i2c3 = i2c_get_adapter(3);
	if (!i2c3) {
		__mop500_uib_init(&mop500_uibs[STUIB],
				"fallback, could not get i2c3");
		return -ENODEV;
	}

	if (ret == 0) {
		ret = i2c_smbus_xfer(i2c3, 0x4B, 0, I2C_SMBUS_WRITE, 0,
				I2C_SMBUS_QUICK, NULL);
		i2c_put_adapter(i2c3);
		if (ret == 0)
			uib = &mop500_uibs[U8500UIB];
		else
			uib = &mop500_uibs[U8500UIB_R3];
	} else {
		ret = i2c_smbus_xfer(i2c3, 0x5C, 0, I2C_SMBUS_WRITE, 0,
				I2C_SMBUS_QUICK, NULL);
		i2c_put_adapter(i2c3);
		if (ret == 0)
			uib = &mop500_uibs[STUIB];
	}
	__mop500_uib_init(uib, "detected");
	mop500_gpio_keys_init();
	platform_add_devices(mop500_uib_platform_devs,
					ARRAY_SIZE(mop500_uib_platform_devs));
	return 0;
}

static bool __init u9540_uib_is_connected(void)
{
	struct i2c_adapter *i2c2;
	int ret;

	i2c2 = i2c_get_adapter(2);
	if (i2c2 == NULL)
		return false;

	/* try to talk to light sensor on the UIB... */
	ret = i2c_smbus_xfer(i2c2, 0x29, 0, I2C_SMBUS_WRITE, 0,
			I2C_SMBUS_QUICK, NULL);
	i2c_put_adapter(i2c2);
	return (ret == 0);
}

#define U9540_UIB_REV_PIN0 MOP500_EGPIO(8)
#define U9540_UIB_REV_PIN1 MOP500_EGPIO(9)
#define U9540_UIB_REV_PIN2 MOP500_EGPIO(10)
#define U9540_UIB_REV_PIN3 MOP500_EGPIO(11)

static u8 __init u9540_uib_revision(void)
{
	u8 revision = 0xFF;
	int ret;

	if (!u9540_uib_is_connected())
		return 0xFE;

	ret = gpio_request(U9540_UIB_REV_PIN0, __func__);
	if (ret < 0) {
		pr_err("%s: failed to request UIB ID GPIO\n", __func__);
		goto out;
	}
	ret = gpio_request(U9540_UIB_REV_PIN1, __func__);
	if (ret < 0) {
		pr_err("%s: failed to request UIB ID GPIO\n", __func__);
		goto out_free_pin0;
	}
	ret = gpio_request(U9540_UIB_REV_PIN2, __func__);
	if (ret < 0) {
		pr_err("%s: failed to request UIB ID GPIO\n", __func__);
		goto out_free_pin1;
	}
	ret = gpio_request(U9540_UIB_REV_PIN3, __func__);
	if (ret < 0) {
		pr_err("%s: failed to request UIB ID GPIO\n", __func__);
		goto out_free_pin2;
	}

	ret = gpio_direction_input(U9540_UIB_REV_PIN0);
	if (ret < 0) {
		pr_err("%s: failed to configure UIB ID GPIO\n", __func__);
		goto out_free_pin3;
	}
	ret = gpio_direction_input(U9540_UIB_REV_PIN1);
	if (ret < 0) {
		pr_err("%s: failed to configure UIB ID GPIO\n", __func__);
		goto out_free_pin3;
	}
	ret = gpio_direction_input(U9540_UIB_REV_PIN2);
	if (ret < 0) {
		pr_err("%s: failed to configure UIB ID GPIO\n", __func__);
		goto out_free_pin3;
	}
	ret = gpio_direction_input(U9540_UIB_REV_PIN3);
	if (ret < 0) {
		pr_err("%s: failed to configure UIB ID GPIO\n", __func__);
		goto out_free_pin3;
	}

	revision = (!!gpio_get_value_cansleep(U9540_UIB_REV_PIN0)) |
			(!!gpio_get_value_cansleep(U9540_UIB_REV_PIN1) << 1) |
			(!!gpio_get_value_cansleep(U9540_UIB_REV_PIN2) << 2) |
			(!!gpio_get_value_cansleep(U9540_UIB_REV_PIN3) << 3);

out_free_pin3:
	gpio_free(U9540_UIB_REV_PIN3);
out_free_pin2:
	gpio_free(U9540_UIB_REV_PIN2);
out_free_pin1:
	gpio_free(U9540_UIB_REV_PIN1);
out_free_pin0:
	gpio_free(U9540_UIB_REV_PIN0);
out:
	return revision;
}

static int __init u9540_uib_init(void)
{
	int result = 0;
	u8 rev;

	rev = u9540_uib_revision();
	switch (rev) {
	case 0x00:
		__mop500_uib_init(&mop500_uibs[U9540UIBS_V1], "identified");
		break;
	case 0x01:
		__mop500_uib_init(&mop500_uibs[U9540UIBT_V1], "identified");
		break;
	case 0x02:
		__mop500_uib_init(&mop500_uibs[U9540UIBS_V2], "identified");
		break;
	case 0xFE:
		pr_err("u9540 UIB is not connected\n");
		result = -ENODEV;
		break;
	default:
		pr_err("u9540 UIB 0x%02x not supported!\n", rev);
		result = -ENODEV;
		break;
	}
	return result;
}

static int __init mop500_uib_init(void)
{
	if (cpu_is_u8500())
		return u8500_uib_init();
	if (cpu_is_u9540())
		return u9540_uib_init();

	pr_err("unknown cpu!\n");
	return -ENODEV;
}
module_init(mop500_uib_init);
