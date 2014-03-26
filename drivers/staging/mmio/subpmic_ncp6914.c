
#include <linux/delay.h>
#include <linux/init.h>		/* Initiliasation support */
#include <linux/module.h>	/* Module support */
#include <linux/kernel.h>	/* Kernel support */
#include <linux/version.h>	/* Kernel version */
#include <linux/fs.h>		/* File operations (fops) defines */
#include <linux/errno.h>	/* Defines standard err codes */
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mmio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/gpio.h>
#include <linux/i2c.h>		/* struct i2c_client, i2c_*() */

#define  READ_I2CADDRESS   0x20
#define  WRITE_I2CADDRESS   0x21

/* Register map */
#define	NCP6914_REG_GENRAL_SETTINGS	0x00
#define	NCP6914_REG_LDO1_SETTINGS	0x01
#define	NCP6914_REG_LDO2_SETTINGS	0x02
#define	NCP6914_REG_LDO3_SETTINGS	0x03
#define	NCP6914_REG_LDO4_SETTINGS	0x04
#define	NCP6914_REG_SPARE	0x05
#define	NCP6914_REG_BUCK_SETTINGS1	 0x06
#define	NCP6914_REG_BUCK_SETTINGS2	 0x07
#define	NCP6914_REG_ENABLE_BITS	 0x08
#define	NCP6914_REG_PULLDOWN_BITS 0x09
#define	NCP6914_REG_STATUS_BITS 0x0A
#define	NCP6914_REG_INTERRUPT_BITS 0x0B
#define	NCP6914_REG_INTERRUPT_MASK 0x0C

#define LDO1_LDO2_LDO3_V1_20    0x00
#define LDO1_LDO2_LDO3_V1_25    0x01
#define LDO1_LDO2_LDO3_V1_30    0x02
#define LDO1_LDO2_LDO3_V1_35    0x03
#define LDO1_LDO2_LDO3_V1_40    0x04
#define LDO1_LDO2_LDO3_V1_45    0x05
#define LDO1_LDO2_LDO3_V1_50    0x06
#define LDO1_LDO2_LDO3_V1_55    0x07
#define LDO1_LDO2_LDO3_V1_60    0x08
#define LDO1_LDO2_LDO3_V1_65    0x09
#define LDO1_LDO2_LDO3_V1_70    0x0A
#define LDO1_LDO2_LDO3_V1_75    0x0B
#define LDO1_LDO2_LDO3_V1_80    0x0C
#define LDO1_LDO2_LDO3_V1_85    0x0D
#define LDO1_LDO2_LDO3_V1_90    0x0E
#define LDO1_LDO2_LDO3_V2_00    0x0F
#define LDO1_LDO2_LDO3_V2_10    0x10
#define LDO1_LDO2_LDO3_V2_20    0x11
#define LDO1_LDO2_LDO3_V2_30    0x12
#define LDO1_LDO2_LDO3_V2_40    0x13
#define LDO1_LDO2_LDO3_V2_50    0x14
#define LDO1_LDO2_LDO3_V2_60    0x15
#define LDO1_LDO2_LDO3_V2_65    0x16
#define LDO1_LDO2_LDO3_V2_70    0x17
#define LDO1_LDO2_LDO3_V2_75    0x18
#define LDO1_LDO2_LDO3_V2_80    0x19
#define LDO1_LDO2_LDO3_V2_85    0x1A
#define LDO1_LDO2_LDO3_V2_90    0x1B
#define LDO1_LDO2_LDO3_V2_95    0x1C
#define LDO1_LDO2_LDO3_V3_00    0x1D
#define LDO1_LDO2_LDO3_V3_10    0x1E
#define LDO1_LDO2_LDO3_V3_30    0x1F

#define LDO4_V1_00    0x00
#define LDO4_V1_10    0x04
#define LDO4_V1_20    0x05
#define LDO4_V1_25    0x06
#define LDO4_V1_30    0x07
#define LDO4_V1_35    0x08
#define LDO4_V1_40    0x09
#define LDO4_V1_45    0x0A
#define LDO4_V1_50    0x0B
#define LDO4_V1_55    0x0C
#define LDO4_V1_60    0x0D
#define LDO4_V1_65    0x0E
#define LDO4_V1_70    0x0F
#define LDO4_V1_75    0x10
#define LDO4_V1_80    0x11
#define LDO4_V1_85    0x12
#define LDO4_V1_90    0x13
#define LDO4_V2_00    0x14
#define LDO4_V2_10    0x15
#define LDO4_V2_20    0x16
#define LDO4_V2_30    0x17
#define LDO4_V2_40    0x18
#define LDO4_V2_50    0x19
#define LDO4_V2_60    0x1A
#define LDO4_V2_65    0x1B
#define LDO4_V2_70    0x0C
#define LDO4_V2_75    0x0D
#define LDO4_V2_80    0x0E
#define LDO4_V2_85    0x0F

#define LDO_BUCK_DELAY_SHIFT  5

#define LDO_BUCK_DELAY_2ms   (0 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_4ms   (1 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_6ms   (2 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_8ms   (3 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_10ms  (4 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_12ms  (5 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_14ms  (6 << LDO_DELAY_SHIFT)
#define LDO_BUCK_DELAY_16ms  (7 << LDO_DELAY_SHIFT)

/* BUCK_V1 and BUCK_V2 setting table */
#define BUCK_V0_80    0x00
#define BUCK_V0_85    0x02
#define BUCK_V0_90    0x03
#define BUCK_V0_95    0x04
#define BUCK_V1_00    0x05
#define BUCK_V1_05    0x06
#define BUCK_V1_10    0x00
#define BUCK_V1_15    0x08
#define BUCK_V1_20    0x09
#define BUCK_V1_25    0x0A
#define BUCK_V1_30    0x0B
#define BUCK_V1_35    0x0C
#define BUCK_V1_40    0x0D
#define BUCK_V1_45    0x0E
#define BUCK_V1_50    0x0F
#define BUCK_V1_55    0x10
#define BUCK_V1_60    0x11
#define BUCK_V1_65    0x12
#define BUCK_V1_70    0x13
#define BUCK_V1_75    0x14
#define BUCK_V1_80    0x15
#define BUCK_V1_85    0x16
#define BUCK_V1_90    0x17
#define BUCK_V1_95    0x18
#define BUCK_V2_00    0x19
#define BUCK_V2_05    0x1A
#define BUCK_V2_10    0x1B
#define BUCK_V2_15    0x1C
#define BUCK_V2_20    0x1D
#define BUCK_V2_25    0x1E
#define BUCK_V2_30    0x1F
/* Useful bit values and masks */

#define	NCP6914_REG_BIT0_SET		0x01
#define	NCP6914_REG_BIT1_SET		0x02
#define	NCP6914_REG_BIT2_SET		0x04
#define	NCP6914_REG_BIT3_SET		0x08
#define	NCP6914_REG_BIT4_SET		0x10
#define	NCP6914_REG_BIT5_SET		0x20
#define	NCP6914_REG_BIT6_SET		0x40
#define	NCP6914_REG_BIT7_SET		0x80

#define	NCP6914_REG_REGEN_BIT_LDO1EN		0x01
#define	NCP6914_REG_REGEN_BIT_LDO2EN		0x02
#define	NCP6914_REG_REGEN_BIT_LDO3EN		0x04
#define	NCP6914_REG_REGEN_BIT_LDO4EN		0x08
#define	NCP6914_REG_REGEN_BIT_BUCKEN		0x20
#define	NCP6914_REG_REGEN_BIT_DVS_V2V1		0x80

#define	NCP6914_DEVNAME		"ncp6914"

static unsigned int gpio_power_on;
static struct i2c_client *pClient;

u8 NCP6914_pinstate;

struct NCP6914_platform_data {
	unsigned int subpmu_pwron_gpio;
};

static const struct i2c_device_id NCP6914_i2c_idtable[] = {
	{NCP6914_DEVNAME, 0},
	{}
};

#ifdef CONFIG_PM
static int NCP6914_i2c_suspend(struct device *dev)
{
	int ret = 0;

	printk(KERN_INFO "-> %s()", __func__);

	/*ret = NCP6914_dev_poweroff();*/

	printk(KERN_INFO "<- %s() = %d", __func__, ret);
	return ret;
}

static int NCP6914_i2c_resume(struct device *dev)
{
	int ret = 0;

	printk(KERN_INFO "-> %s()", __func__);

	/*ret = NCP6914_dev_poweron();*/

	printk(KERN_INFO "<- %s() = %d", __func__, ret);
	return ret;
}

static const struct dev_pm_ops NCP6914_pm_ops = {
	.suspend = NCP6914_i2c_suspend,
	.resume = NCP6914_i2c_resume,
};

#endif

static int
NCP6914_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	/*struct NCP6914_platform_data* platdata = client->dev.platform_data; */

	printk(KERN_INFO "-> %s(client=%s, id=%s)", __func__, client->name, id->name);

	dev_set_name(&client->dev, client->name);
	pClient = client;

	gpio_power_on = 145;	/* platdata->subpmu_pwron_gpio; */

	printk(KERN_INFO "<- %s(client=%s) = %d", __func__, client->name, ret);
	return ret;
}

static int NCP6914_i2c_remove(struct i2c_client *client)
{
	printk(KERN_INFO "-> %s(client=%s)", __func__, client->name);

	printk(KERN_INFO "<- %s(client=%s) = 0", __func__, client->name);
	return 0;
}

static struct i2c_driver subPMIC_i2c_driver = {
	.driver = {
		   /* This should be the same as the module name */
		   .name = NCP6914_DEVNAME,
		   .owner = THIS_MODULE,
#ifdef CONFIG_PM
		   .pm = &NCP6914_pm_ops,
#endif
		   },
	.id_table = NCP6914_i2c_idtable,
	.probe = NCP6914_i2c_probe,
	.remove = NCP6914_i2c_remove,
};

static int
_NCP6914_i2c_send(struct i2c_client *client, const u8 * data, int len)
{
	int ret = 0;

	if (len <= 0) {
		printk(KERN_ERR "%s(): invalid length %d", __func__, len);
		return -EINVAL;
	}

	ret = i2c_master_send(client, data, len);
	if (ret < 0) {
		printk(KERN_ERR "Failed to send %d bytes to NCP6914 [errno=%d]", len,
		       ret);
	} else if (ret != len) {
		printk(KERN_ERR "Failed to send exactly %d bytes to NCP6914 (send %d)",
		       len, ret);
		ret = -EIO;
	} else {
		ret = 0;
	}
	return ret;
}

static int NCP6914_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;

	return _NCP6914_i2c_send(client, buf, 2);
}

/*
 Power ON Sequence	1	Set HWEN=Low by using GPIO
	2	Address: $00, GENERAL_SETTINGS, set 0000 0100	Not use DVS
	3	Address: $01, LDO1_SETTINGS, set 0010 0110	LDO1 delay 2ms,
	1.5V output	 -> 1.5V, 1.3M_VDD_REG
	4	Address: $02, LDO2_SETTINGS, set 0100 1100	LDO2 delay 4ms,
	 1.8V output -> 1.8V, VDDIO
	5	Address: $03, LDO3_SETTINGS, set 0001 1001	LDO3 delay
	can be control	by I2C, 2.8V output-> 2.8V, AF
	6	Address: $04, LDO4_SETTINGS, set 0001 1110	LDO4 delay 0ms,
	2.8V output -> 2.8V, VDDA
	7	Address: $05, SPARE, set 0000 0000(default)	Spare registers, no change
	8	Address: $06, BUCK_SETTINGS1, set 0000 1001	Buck delay 10us
	can be control by I2C after 1.3M_STBYN go low, 1.2V output->
	5M_VDD_REG 5M 1.2V
	9	Address: $07, BUCK_SETTINGS2, set 0010 1001	forced PWM, DVS_V2 is set 1.2V
	10	Address: $08, ENABLE_BITS, set 1000 1011	Buck Output
	voltage is set to DVS_V1, Buck disable, LDO4 enable,
	LDO3 disable,LDO2 enable, LDO1 enable
	11	Address: $09, PULLDOWN_BITS, set 1010 1111
	registers content stays	the same as before
	Thermal Shutdown,  active discharge enable
	for Buck, LDO1, LDO2, LDO3,LDO4
	12	Address: $0C, INTERRUPT_MASK, set 1111 1111
	TSD interrupt masked,Thermal warning interrupt masked
	13	Set HWEN=High by using GPIO
	14	Address: $08, ENABLE_BITS, set 1010 1011	10us delayed
	Buck enabled after 1.3M_STBYN go low
	15	Address: $08, ENABLE_BITS, set 1010 1111	LDO3 enabled by I2C for AF
Power Off Sequence	16	Address: $08, ENABLE_BITS, set 1010 1011	LDO3 disable
	17	Address: $08, ENABLE_BITS, set 1010 1001	LDO2 disable
	18	Address: $08, ENABLE_BITS, set 1010 1000	LDO1 disable
	19	Address: $08, ENABLE_BITS, set 1000 1000	Buck disable
	20	Address: $08, ENABLE_BITS, set 1000 0000	LDO4 disable
	21	Set HWEN=Low by using GPIO
*/

int NCP6914_subPMIC_module_init(void)
{
	int ret = 0;

	// TODO: TEMP DEBUG gareth.phillips
	printk(KERN_INFO "-> %s", __func__);

	ret = i2c_add_driver(&subPMIC_i2c_driver);
	if (ret < 0) {
		printk(KERN_ERR "Failed to add i2c driver for subPMIC [errno=%d]", ret);
	}

	gpio_request(gpio_power_on, "SUBPMU_PWRON");

	// TODO: TEMP DEBUG gareth.phillips
	printk(KERN_INFO "<- %s", __func__);

	return ret;
}

void NCP6914_subPMIC_module_exit(void)
{
	i2c_del_driver(&subPMIC_i2c_driver);
}

int NCP6914_subPMIC_PowerOn(int opt)
{
	int ret = 0;
	u8 reg;
	u8 val;
#if ( defined(CONFIG_MACH_JANICE) || defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_GAVINI) || defined(CONFIG_MACH_SEC_KYLE)|| defined(CONFIG_MACH_SEC_GOLDEN) )
	// TODO: TEMP DEBUG gareth.phillips
	printk(KERN_INFO "-> %s", __func__);

	if (opt == 0) {
		NCP6914_pinstate = 0;
		gpio_set_value(gpio_power_on, 0);
		mdelay(1);

		reg = NCP6914_REG_GENRAL_SETTINGS;
		val = 0x04;	/*set 0000 0100   Not use DVS */
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			// TODO: TEMP DEBUG gareth.phillips
			printk(KERN_INFO "%s: NCP6914_i2c_write failed(%d)", __func__, ret);
			return ret;
		}

		reg = NCP6914_REG_LDO1_SETTINGS;
#if (defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_SEC_KYLE) || defined(CONFIG_MACH_SEC_GOLDEN))
		/* set 0000 1100  LDO1 delay 0ms, 1.8V output ->
		   1.8V, 1.3M_VDD_REG */
		val = 0x0C;
#else
		/* set 0000 0110  LDO1 delay 0ms, 1.5V output ->
		   1.5V, 1.3M_VDD_REG */
		val = 0x06;
#endif
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO2_SETTINGS;
		/*set 0000 1100   LDO2 delay 4ms, 1.8V output -> 1.8V, VDDIO */
		val = 0x0C;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO3_SETTINGS;
		/*set 0001 1001    LDO3 delay can be control by I2C,
		   2.8V output-> 2.8V, AF */
		val = 0x19;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO4_SETTINGS;
		/*set 0001 1110    LDO4 delay 0ms, 2.8V output -> 2.8V, VDDA */
		val = 0x1E;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_SPARE;
		/*set 0000 0000(default)     Spare registers, no change */
		val = 0;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_BUCK_SETTINGS1;
		/*set 0000 1001    Buck delay 10us can be control by I2C
		   after 1.3M_STBYN go low, 1.2V output-> 5M_VDD_REG 5M 1.2V */
		val = 0x09;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_BUCK_SETTINGS2;
		/*set 0010 1001   forced PWM, DVS_V2 is set 1.2V */
		val = 0x29;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_ENABLE_BITS;
		/*set 1000 0000   Buck Output voltage is set
		   to DVS_V1, Buck disable, LDO4 disable, LDO3 disable,
		   LDO2 disable, LDO1 disable */
		val = 0x80;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}
		NCP6914_pinstate = val;

		reg = NCP6914_REG_PULLDOWN_BITS;
		/*set 1010 1111   registers content stays
		   the same as before Thermal Shutdown,
		   active discharge enable for Buck, LDO1, LDO2, LDO3,LDO4 */
		val = 0xAF;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_INTERRUPT_MASK;
		/*set 1111 1111    TSD interrupt masked,
		   Thermal warning interrupt masked */
		val = 0x03;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		gpio_set_value(gpio_power_on, 1);
	}
#else				/*for gavini */
	if (opt == 0) {
		NCP6914_pinstate = 0;
		gpio_set_value(gpio_power_on, 0);

		reg = NCP6914_REG_GENRAL_SETTINGS;
		/*set 0000 0100   Not use DVS */
		val = 0x04;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO1_SETTINGS;
		/* set 1010 0110  LDO1 delay 6ms, 1.5V output ->
		   1.5V, 1.3M_VDD_REG */
		val = 0xA6;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO2_SETTINGS;
		/*set 1110 1100   LDO2 delay 8ms, 1.8V output ->
		   1.8V, VDDIO */
		val = 0xEC;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO3_SETTINGS;
		/*set 0001 1001    LDO3 delay can be control by I2C,
		   2.8V output-> 2.8V, AF */
		val = 0x19;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_LDO4_SETTINGS;
		/*set 0111 1110    LDO4 delay 4ms, 2.8V output -> 2.8V, VDDA  */
		val = 0x7E;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0) {
			return ret;
		}

		reg = NCP6914_REG_SPARE;
		/*set 0000 0000(default)     Spare registers, no change */
		val = 0;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_BUCK_SETTINGS1;
		/*set 0010 1001    Buck delay 2ms, 1.2V output->
		   5M_VDD_REG 5M 1.2V */
		val = 0x29;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_BUCK_SETTINGS2;
		/*set 0010 1001   forced PWM, DVS_V2 is set 1.2V */
		val = 0x29;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_ENABLE_BITS;
		/*set 1100 1011   Buck Output voltage is set to DVS_V1,
		   Buck enable, LDO4 enable, LDO3 disable, LDO2 enable,
		   LDO1 enable */
		val = 0xCB;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;
		NCP6914_pinstate = val;

		reg = NCP6914_REG_PULLDOWN_BITS;
		/*set 1010 1111   registers content stays the same
		   as before Thermal Shutdown,  active discharge enable
		   for Buck, LDO1, LDO2, LDO3,LDO4  */
		val = 0xAF;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_INTERRUPT_MASK;
		/*set 1111 1111    TSD interrupt masked,
		   Thermal warning interrupt masked */
		val = 0x03;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		gpio_set_value(gpio_power_on, 1);
	}

	if (opt == 1) {

		reg = NCP6914_REG_ENABLE_BITS;
		/*set 1010 1111   LDO3 enabled by I2C for AF */
		val = 0xAF;
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		NCP6914_pinstate = val;
	}
#endif

	// TODO: TEMP DEBUG gareth.phillips
	printk(KERN_INFO "<- %s", __func__);

	return ret;
}

int NCP6914_subPMIC_PowerOff(int opt)
{
	int ret = 0;
	u8 reg;
	u8 val;
#if (defined(CONFIG_MACH_JANICE) || defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_GAVINI) || defined(CONFIG_MACH_SEC_KYLE) || defined(CONFIG_MACH_SEC_GOLDEN))
	gpio_set_value(gpio_power_on, 0);
#else				/*for gavini */
	if (opt == 0xff) {
		reg = NCP6914_REG_ENABLE_BITS;
		val = 0xA9;	/*set 1010 1001   LDO2 disable 1.8V */
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_ENABLE_BITS;
		val = 0xA8;	/*set 1010 1000   LDO1 disable 1.5V */
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_ENABLE_BITS;
		val = 0x80;	/*set 1000 0000   LDO4 disable 2.8V */
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_ENABLE_BITS;
		val = 0xAB;	/*set 1010 1011   LDO3 disable 2.8V AF */
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		reg = NCP6914_REG_ENABLE_BITS;
		val = 0x88;	/*set 1000 1000   Buck disable */
		ret = NCP6914_i2c_write(pClient, reg, val);
		if (ret < 0)
			return ret;

		gpio_set_value(gpio_power_on, 0);
	}
#endif
	return ret;
}

int NCP6914_subPMIC_PinOnOff(int pin, int on_off)
{
	int ret = 0;
	u8 val = 0;

	switch (pin) {
	case 0:
		val = NCP6914_REG_REGEN_BIT_BUCKEN;
		break;

	case 1:
		val = NCP6914_REG_REGEN_BIT_LDO1EN;
		break;

	case 2:
		val = NCP6914_REG_REGEN_BIT_LDO2EN;
		break;

	case 3:
		val = NCP6914_REG_REGEN_BIT_LDO3EN;
		break;

	case 4:
		val = NCP6914_REG_REGEN_BIT_LDO4EN;
		break;

	default:
		val = 0;
		break;
	}

	if (on_off)	/*on*/
		NCP6914_pinstate |= val;
	else
		NCP6914_pinstate &= ~val;

	ret = NCP6914_i2c_write(pClient, NCP6914_REG_ENABLE_BITS, NCP6914_pinstate);
	if (ret < 0)
		return ret;

	return ret;
}

