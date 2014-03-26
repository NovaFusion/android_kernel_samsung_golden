 /*****************************************************************************
 * Title: Linux Device Driver for Proximity Sensor CM3607
 * COPYRIGHT(C) : Samsung Electronics Co.Ltd, 2006-2015 ALL RIGHTS RESERVED
 *
 *****************************************************************************/
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <mach/hardware.h>
#include <plat/gpio-cfg.h>
#include <linux/i2c/pmic.h>

 
#include "cm3607.h"
#include "prox_ioctls.h"

struct cm3607_prox_data *cm3607_data;
struct workqueue_struct *cm3607_wq;
static int proximity_enable;

static struct file_operations proximity_fops = {
	.owner  	= THIS_MODULE,
	.ioctl 		= proximity_ioctl,
	.open   	= proximity_open,
    .release 	= proximity_release,    
};

static struct miscdevice cm3607_misc_device = {
    .minor  = MISC_DYNAMIC_MINOR,
    .name   = "proximity",
    .fops   = &proximity_fops,
};

static int proximity_open(struct inode *ip, struct file *fp)
{
	debug("%s called",__func__);
	return nonseekable_open(ip, fp);	
}

static int proximity_release(struct inode *ip, struct file *fp)
{	
	debug("%s called",__func__);
	return 0;
}

static int proximity_ioctl(struct inode *inode, struct file *filp, 
	                        unsigned int ioctl_cmd,  unsigned long arg)
{	int ret = 0;

	if( _IOC_TYPE(ioctl_cmd) != PROX_IOC_MAGIC )
    {
        error("Wrong _IOC_TYPE 0x%x",ioctl_cmd);
        return -ENOTTY;
    }
    if( _IOC_NR(ioctl_cmd) > PROX_IOC_NR_MAX )
    {
        error("Wrong _IOC_NR 0x%x",ioctl_cmd);	
        return -ENOTTY;
    }
	switch (ioctl_cmd)
    {
        case PROX_IOC_NORMAL_MODE:
			{
				debug("PROX_IOC_NORMAL_MODE called");
				if(0==proximity_enable)
				{
					if( (ret = proximity_mode(1)) < 0 )        
						error("PROX_IOC_NORMAL_MODE failed"); 
				}
				else
					debug("Proximity Sensor is already set in Normal-mode");
				break;
			}
        case PROX_IOC_SHUTDOWN_MODE:			
			{
				debug("PROX_IOC_SHUTDOWN_MODE called");				
				if(1==proximity_enable)
				{
					if( (ret = proximity_mode(0)) < 0 )        
						error("PROX_IOC_SHUTDOWN_MODE failed"); 
				}
				else
					debug("Proximity Sensor is already set in Shutdown-mode");
				break;
			}
		default:
			error("Unknown IOCTL command");
            ret = -ENOTTY;
            break;
	}
	return ret;
}

/* 
 * Setting proximity sensor operation mode, 
 * enable=1-->Normal Operation Mode
 * enable=0-->Shutdown Mode 
 */
static int proximity_mode(int enable)
{		
	if(1==enable)
	{
		gpio_set_value(GPIO_PS_EN, GPIO_LEVEL_LOW);	
		proximity_enable=1;
	}
	else
	{
		gpio_set_value(GPIO_PS_EN, GPIO_LEVEL_HIGH);
		proximity_enable=0;
	}
	
	debug("gpio_get_value of GPIO_PS_EN is %d",gpio_get_value(GPIO_PS_EN));     
	printk("--------GPIO_PS_EN setting successfull \n");
	return 0;
}

/* 
 * PS_OUT =0, when object is near
 * PS_OUT =1, when object is far
 */
/*
 * get_cm3607_proximity_value() is called by magnetic sensor driver(ak8973)
 * for reading proximity value.
 */
int get_cm3607_proximity_value(void)
{
	return gpio_get_value(GPIO_PS_OUT);
}
EXPORT_SYMBOL(get_cm3607_proximity_value);

static void cm3607_prox_work_func(struct work_struct *work)
{	
	debug("Reporting to the input events");
	debug("gpio_get_value of GPIO_PS_OUT is %d",gpio_get_value(GPIO_PS_OUT));   //test			
	input_report_abs(cm3607_data->prox_input_dev,ABS_DISTANCE,gpio_get_value(GPIO_PS_OUT));
	input_sync(cm3607_data->prox_input_dev);
	mdelay(1);
	
	enable_irq(cm3607_data->irq);
	debug("enable_irq IRQ_NO:%d",cm3607_data->irq);
}

/*
 * When sensor detects that the object is near,ps_out changes from 1->0, which is 
 * treated as edge-falling interrupt, then it reports to the input event only 
 * once.If,reporting to the input events should be done as long as the object is 
 * near, treat ps_out as low-level interrupt.
 */
static irqreturn_t cm3607_irq_handler( int irq, void *unused )
{
  	debug("cm3607_irq_handler called IRQ_NO:%d",irq);
    if(cm3607_data->irq !=-1)
	{
		disable_irq(cm3607_data->irq);
		debug("disable_irq IRQ_NO:%d",cm3607_data->irq);
		queue_work(cm3607_wq, &cm3607_data->work_prox);
	}
	else
	{
		error("PROX_INT not handled");
		return IRQ_NONE;
	}
	debug("PROX_INT handled");
	return IRQ_HANDLED;
}
//TEST
static ssize_t cm3607_show_prox_value(struct device *dev, struct device_attribute *attr, char *buf)
{	
	return sprintf(buf,"%d\n", gpio_get_value(GPIO_PS_OUT) );	
}
static DEVICE_ATTR(prox_value, S_IRUGO, cm3607_show_prox_value, NULL);

static int cm3607_prox_probe( struct platform_device* pdev )
{	
	int ret=0;	
	printk("--------Proximity Sensor driver probe start \n");
	
	/*misc device registration*/
    if( (ret = misc_register(&cm3607_misc_device)) < 0 )
    {
        error("cm3607_probe misc_register failed");
        goto ERR_MISC_REG; 	  	
    }
	
	/*Enabling LD016 of MAX8998(PMIC) and setting it to 2.8V*/
	Set_MAX8998_PM_REG(ELDO16, 1);  // enable LDO16
	Set_MAX8998_PM_REG(LDO16, 0x0C);  // Set LDO16 voltage to 2.8V
	
	/*Initialisation of GPIO_PS_OUT of proximity sensor*/
	s3c_gpio_cfgpin(GPIO_PS_OUT, S3C_GPIO_SFN(GPIO_PS_OUT_STATE));
	s3c_gpio_setpull(GPIO_PS_OUT, S3C_GPIO_PULL_NONE);

	/* Allocate driver_data */
	cm3607_data = kzalloc(sizeof(struct cm3607_prox_data),GFP_KERNEL);
	if(!cm3607_data)
	{
		error("kzalloc:allocating driver_data error");
		ret = -ENOMEM;
		goto ERR_MISC_REG;
	} 
	
	/*Input Device Settings*/
	cm3607_data->prox_input_dev = input_allocate_device();
	if (!cm3607_data->prox_input_dev) 
	{
        error("Not enough memory for cm3607_data->prox_input_dev");
        ret = -ENOMEM;
        goto ERR_INPUT_DEV;
    }
	cm3607_data->prox_input_dev->name = "proximity_cm3607";
	set_bit(EV_SYN,cm3607_data->prox_input_dev->evbit);
	set_bit(EV_ABS,cm3607_data->prox_input_dev->evbit);	
	input_set_abs_params(cm3607_data->prox_input_dev, ABS_DISTANCE, 0, 1, 0, 0);
	ret = input_register_device(cm3607_data->prox_input_dev);
    if (ret) 
	{
        error("Failed to register input device");
		input_free_device(cm3607_data->prox_input_dev);
        goto ERR_INPUT_REG;
    }
	debug("Input device settings complete");
	
	/* Workqueue Settings */
    cm3607_wq = create_singlethread_workqueue("cm3607_wq");
    if (!cm3607_wq)
	{
        error("Not enough memory for cm3607_wq");
        ret = -ENOMEM;
        goto ERR_INPUT_REG;
    }	     
    INIT_WORK(&cm3607_data->work_prox, cm3607_prox_work_func);
	debug("Workqueue settings complete");	
	
	/* Setting platform driver data */
	platform_set_drvdata(pdev, cm3607_data);

	cm3607_data->irq = -1;	
	set_irq_type(PROX_INT, IRQ_TYPE_EDGE_FALLING);	
	if( (ret = request_irq(PROX_INT, cm3607_irq_handler,IRQF_DISABLED , "proximity_int", NULL )) )
	{
        error("CM3607 request_irq failed IRQ_NO:%d", PROX_INT);
        goto ERR_INT_REQ;
	} 
	else
		debug("CM3607 request_irq success IRQ_NO:%d", PROX_INT);
	
	cm3607_data->irq = PROX_INT;
	
	/*GPIO_PS_EN Initialisation*/
	if (gpio_is_valid(GPIO_PS_EN)) 
	{
		if (gpio_request(GPIO_PS_EN, "GPJ3"))	
		{
			error("Failed to request GPIO_PS_EN");
			ret =-1;
			goto ERR_GPIO_REQ;
		}
		if(gpio_direction_output(GPIO_PS_EN, GPIO_LEVEL_LOW))
		{
			error("Failed to set output direction GPIO_PS_EN");
			ret=-1;
			goto ERR_GPIO_REQ;
		}		
	}
	else
	{
		error("GPIO_PS_EN is not valid");
		ret =-1;
		goto ERR_GPIO_REQ;
	}
	/*Setting CM3607 proximity sensor in normal operation mode*/
	proximity_mode(1);

	/*Pulling the Interrupt Pin High*/
	s3c_gpio_setpull(GPIO_PS_OUT, S3C_GPIO_PULL_UP);		
	debug("gpio_get_value of GPIO_PS_OUT is %d",gpio_get_value(GPIO_PS_OUT));   //test			
	
	input_report_abs(cm3607_data->prox_input_dev,ABS_DISTANCE,gpio_get_value(GPIO_PS_OUT));
	input_sync(cm3607_data->prox_input_dev);
	mdelay(1);
	printk("--------Proximity Sensor driver probe end \n");
	
	//TEST
	if (device_create_file(cm3607_misc_device.this_device, &dev_attr_prox_value) < 0)
		error("Failed to create device file %s \n", dev_attr_prox_value.attr.name);
	
	return ret;
ERR_GPIO_REQ:	
ERR_INT_REQ:
    destroy_workqueue(cm3607_wq);
	input_unregister_device(cm3607_data->prox_input_dev);
ERR_INPUT_REG:
ERR_INPUT_DEV:
	kfree(cm3607_data);
ERR_MISC_REG:
	return ret;
} 

static int cm3607_prox_suspend( struct platform_device* pdev, pm_message_t state )
{
	debug("%s called",__func__);
//	proximity_mode(0);
//	disable_irq(cm3607_data->irq);	
	return 0;
}


static int cm3607_prox_resume( struct platform_device* pdev )
{
	debug("%s called",__func__);
//	proximity_mode(1);
//	enable_irq(cm3607_data->irq);
	return 0;
}
static struct platform_driver cm3607_prox_driver = {
	.probe 	 = cm3607_prox_probe,
	.suspend = cm3607_prox_suspend,
	.resume  = cm3607_prox_resume,
	.driver  = {
		.name 	= "proximity_cm3607",
		.owner 	= THIS_MODULE,
	},
};

static int __init cm3607_prox_init(void)
{
	debug("%s",__func__);	
	return platform_driver_register(&cm3607_prox_driver);	
}
static void __exit cm3607_prox_exit(void)
{  
	debug("%s",__func__);
	if (cm3607_wq)
		destroy_workqueue(cm3607_wq);
	kfree(cm3607_data);
	free_irq(PROX_INT,NULL);
	gpio_free(GPIO_PS_EN);
	input_unregister_device(cm3607_data->prox_input_dev);
	misc_deregister(&cm3607_misc_device);
	platform_driver_unregister(&cm3607_prox_driver);    
}


module_init(cm3607_prox_init);
module_exit(cm3607_prox_exit);

MODULE_AUTHOR("V.N.V.Srikanth, SAMSUNG ELECTRONICS, vnv.srikanth@samsung.com");
MODULE_DESCRIPTION("Proximity Sensor driver for CM3607");
MODULE_LICENSE("GPL");

