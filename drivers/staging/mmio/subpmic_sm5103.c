/*
 * Silicon-Mitus (Camera Sub-PMIC Driver)
 * SM5103
 * Samsung Electronics, 2012-06-18
*/

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

/* I2C Register Map - SM5103 */
#define SM5103_REG_LDO1_ADDRESS           0x00
#define SM5103_REG_LDO2_ADDRESS           0x01
#define SM5103_REG_LDO3_ADDRESS           0x02
#define SM5103_REG_LDO4_ADDRESS           0x03
#define SM5103_REG_LDO5_ADDRESS           0x04
#define SM5103_REG_BUCK_ADDRESS           0x05
#define SM5103_REG_TIMING1_ADDRESS        0x06
#define SM5103_REG_TIMING2_ADDRESS        0x07
#define SM5103_REG_TIMING3_ADDRESS        0x08
#define SM5103_REG_ONOFFCNTL1_ADDRESS     0x09
#define SM5103_REG_ONOFFCNTL2_ADDRESS     0x0A
#define SM5103_REG_STATUS_ADDRESS         0x0B
#define SM5103_REG_INTERRUPT_ADDRESS      0x0C
#define SM5103_REG_INTERRUPT_MASK_ADDRESS 0x0D

#define SM5103_REG_REGEN_BIT_LDO1EN 0x01
#define SM5103_REG_REGEN_BIT_LDO2EN 0x02
#define SM5103_REG_REGEN_BIT_LDO3EN 0x04
#define SM5103_REG_REGEN_BIT_LDO4EN 0x08
#define SM5103_REG_REGEN_BIT_LDO5EN 0x10
#define SM5103_REG_REGEN_BIT_BUCKEN 0x20

#define SM5103_DEVNAME "sm5103"

static struct i2c_client *pClient;
static unsigned int gpio_power_on;
u8 SM5103_pinstate;



struct SM5103_platform_data {
    unsigned int subpmu_pwron_gpio;
};


static const struct i2c_device_id SM5103_i2c_idtable[] = {
		{SM5103_DEVNAME, 0},
		{}
};



#ifdef CONFIG_PM
static int SM5103_i2c_suspend(struct device *dev)
{
    int ret = 0;
	// ret = SM5103_dev_poweroff();
	
	return ret;
}


static int SM5103_i2c_resume(struct device *dev)
{
    int ret = 0;
	// ret = SM5103_dev_poweron();

	return ret;
}



static const struct dev_pm_ops SM5103_pm_ops = {
    .suspend = SM5103_i2c_suspend,
	.resume  = SM5103_i2c_resume,
};
#endif // CONFIG_PM


static int SM5103_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	printk(KERN_INFO "-> SM5103_%s(client=%s, id=%s)", __func__, client->name, id->name);

    int ret = 0;
	/* Struct NCP6914_platform_data * platdata = client->dev.platform_data; */

	dev_set_name(&client->dev, client->name);
	pClient = client;
	gpio_power_on  = 145; /* SM5103 SUBPMU_PWRON -> GPIO145 */

	printk(KERN_INFO "<- SM5103_%s(client=%s) = %d", __func__, client->name, ret);
	return ret;	
}



static int SM5103_i2c_remove(struct i2c_client *client)
{
    printk(KERN_INFO "-> SM5103_%s(client=%s)", __func__, client->name);
	printk(KERN_INFO "<- SM5103_%s(client=%s) = 0", __func__, client->name);
    return 0;
}



static struct i2c_driver subPMIC_i2c_driver = {
    .driver = {
		/* This should be the same as the module name */
		.name  = SM5103_DEVNAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm    = &SM5103_pm_ops,
#endif
        },
    .id_table = SM5103_i2c_idtable,
    .probe    = SM5103_i2c_probe,
    .remove   = SM5103_i2c_remove,
};



static int _SM5103_i2c_send(struct i2c_client *client, const u8 *data, int len)
{	
	int ret = 0;

	if (len <= 0) {
		printk(KERN_ERR "%s(): invalid length %d", __func__, len);
		return -EINVAL;
	}
	
	ret = i2c_master_send(client, data, len);

	if (ret < 0) {
		printk(KERN_ERR "Failed to send %d bytes to Sm5103 [errno=%d]", len, ret);
	}
	else if (ret != len) {
		printk(KERN_ERR "Failed to send exactly %d bytes to Sm5103 (send %d)", len, ret);
		ret = -EIO;
	}
	else {
		ret = 0;
	}

	return ret;
}


static int SM5103_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
    u8 buf[2];

	buf[0] = reg;
	buf[1] = val;

	return _SM5103_i2c_send(client, buf, 2);
}




int SM5103_subPMIC_module_init(void)
{
    int ret = 0;

	ret = i2c_add_driver(&subPMIC_i2c_driver);
    if(ret < 0)
		printk(KERN_ERR "Failed to add i2c driver for subPMIC [errno=%d]", ret);
    
	gpio_request(gpio_power_on, "SUBPMU_PWRON");

	return ret;
}


//void subPMIC_SM5103_module_exit(void)
void SM5103_subPMIC_module_exit(void)
{
    i2c_del_driver(&subPMIC_i2c_driver);
}



int SM5103_subPMIC_PowerOn(int opt)
{
    int ret = 0;
	u8  reg;
	u8  val;    

	if(opt == 0){
		SM5103_pinstate = 0;
		gpio_set_value(gpio_power_on, 0); /* Turn Off SUBPMU_PWRON (GPIO145) */

        reg = SM5103_REG_LDO1_ADDRESS;
		val = 0x16; /* 0001 0110 -> LDO1 Output Voltage is 2.80V -> CAM_AVDD_2V8 */
		ret = SM5103_i2c_write(pClient, reg, val);
		if(ret < 0)
			return ret;

		reg = SM5103_REG_LDO2_ADDRESS;
		val = 0x02; /* 0000 0010 -> LDO2 Output Voltage is 1.80V -> VT_DVDD_1V8 */
        ret = SM5103_i2c_write(pClient, reg, val);
		if(ret < 0)
			return ret;

		reg = SM5103_REG_LDO3_ADDRESS;
		val = 0x02; /* 0000 0010 -> LDO3 Output Voltage is 1.80V -> CAM_VDDIO_1V8 */
		ret = SM5103_i2c_write(pClient, reg, val);
		if(ret < 0)
			return ret;

		reg = SM5103_REG_LDO4_ADDRESS;
		val = 0x18; /* 0001 1000 -> LDO4 Output Voltage is 2.80V -> VT_AVDD_2V8 */
		ret = SM5103_i2c_write(pClient, reg, val);
		if(ret < 0)
			return ret;

		reg = SM5103_REG_LDO5_ADDRESS;
		val = 0x16; /* 0001 0110 -> LDO5 Output Voltage is 2.80V -> 5M_AF_2V8 */
		ret = SM5103_i2c_write(pClient, reg, val);
		if(ret < 0)
			return ret;

		reg = SM5103_REG_BUCK_ADDRESS;
		val = 0x44; /* 0100 0100 -> Buck1 Output Voltage is DVS=0 (1.20V), BUCK1 Output Voltage is DVS1 (1.20V) */
		ret = SM5103_i2c_write(pClient, reg, val);
		if(ret < 0)
			return ret;

		gpio_set_value(gpio_power_on, 1); /* Turn On SUBPMU_PWRON (GPIO145) */
	}	
	return ret;
}



int SM5103_subPMIC_PowerOff(int opt)
{
    int ret = 0;
	u8  reg;
	u8  val;

    gpio_set_value(gpio_power_on, 0); /* Turn Off SUBPMU_PWRON (GPIO145) */

    reg = SM5103_REG_ONOFFCNTL1_ADDRESS;
	val = 0x00; /* 1000 0000 -> Buck Output by BuckV1, bit[3:0]
	                                              Normal Sleep Operation,
	                                              Turn Off -> Buck, LDO1, 2, 3, 4, 5 */
	ret = SM5103_i2c_write(pClient, reg, val);
												  
	if(ret < 0){
		return ret;
	}	
	return ret;
}



int SM5103_subPMIC_PinOnOff(int pin, int on_off)
{
    int ret = 0;
	u8  val = 0;

	switch(pin){

	case 0:
		val = SM5103_REG_REGEN_BIT_BUCKEN;
		break;

	case 1:
		val = SM5103_REG_REGEN_BIT_LDO1EN;
		break;

	case 2:
		val = SM5103_REG_REGEN_BIT_LDO2EN;
		break;

	case 3:
		val = SM5103_REG_REGEN_BIT_LDO3EN;
		break;

	case 4:
		val = SM5103_REG_REGEN_BIT_LDO4EN;
		break;

	case 5:
		val = SM5103_REG_REGEN_BIT_LDO5EN;
		break;

	default:
		val = 0;
		break;
	}

    if(on_off) // On
        SM5103_pinstate |= val;
	else       // Off
	    SM5103_pinstate &= ~val;
	
    ret = SM5103_i2c_write(pClient, SM5103_REG_ONOFFCNTL1_ADDRESS, SM5103_pinstate);
    if(ret < 0) 
	{
        printk(KERN_ERR "Failed to write i2c driver for subPMIC (SM5103_REG_ONOFFCNTL1_ADDRESS) [errno=%d]", ret);
	    return ret;
    }
	return ret;
}

