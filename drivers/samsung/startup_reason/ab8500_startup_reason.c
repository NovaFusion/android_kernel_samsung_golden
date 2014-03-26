#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/platform_device.h>
#include <linux/mfd/ab8500.h>
#include "startup_reason.h"

/*todo fix debug messages */
#ifdef dev_dbg
#undef dev_dbg
#endif

#define dev_dbg( format, ...)           \
        printk(KERN_WARNING format, ##__VA_ARGS__)


/*
The AB8500 records which triggered it starting a processor boot sequence
inside a register
*/

#define AB8500_STARTUP_TRIGGER_INCLUDED_BATTERY_RISING		(1<<0)
#define AB8500_STARTUP_TRIGGER_INCLUDED_POWER_ON_KEY_1  	(1<<1)
#define AB8500_STARTUP_TRIGGER_INCLUDED_POWER_ON_KEY_2  	(1<<2)
#define AB8500_STARTUP_TRIGGER_INCLUDED_RTC_ALARM		(1<<3)
#define AB8500_STARTUP_TRIGGER_INCLUDED_MAINS_CHARGER_DETECTED	(1<<4)
#define AB8500_STARTUP_TRIGGER_INCLUDED_VBUS_DETECTED		(1<<5)
#define AB8500_STARTUP_TRIGGER_INCLUDED_USB_ID_DETECTED 	(1<<6)

#define AB8500_TURNON_STATUS_REGISTER_ADDRESS	0

struct ab8500_startup_reason_source 
{
	struct startup_reason_source  source;
	struct device * ab8500_dev ;
	int mirror_is_valid ;
	unsigned char power_register_mirror ;
};

#define AB8500_POWER_DUMMY_VALUE 0x11


struct startup_reason {
	unsigned char mask ;
	char * name ;
	};


static const struct startup_reason ab8500_startup_reasons[]= {
	{	AB8500_STARTUP_TRIGGER_INCLUDED_BATTERY_RISING,"BATTERY_RISING",},          
	{	AB8500_STARTUP_TRIGGER_INCLUDED_POWER_ON_KEY_1,"POWER_ON_KEY_1",},          
	{	AB8500_STARTUP_TRIGGER_INCLUDED_POWER_ON_KEY_2,"POWER_ON_KEY_2",},          
	{	AB8500_STARTUP_TRIGGER_INCLUDED_RTC_ALARM,     "RTC_ALARM",},          
	{	AB8500_STARTUP_TRIGGER_INCLUDED_MAINS_CHARGER_DETECTED,  "MAINS_CHARGER",},
	{	AB8500_STARTUP_TRIGGER_INCLUDED_VBUS_DETECTED, "USB_VBUS_DETECTED",},          
	{	AB8500_STARTUP_TRIGGER_INCLUDED_USB_ID_DETECTED,"USB_ID_DETECTED",}         
};

static int ab8500_count_startup_reasons(struct ab8500_startup_reason_source * source)
{
	int i ;
	int count = 0;
 	if (!source->mirror_is_valid) {
		source->mirror_is_valid = 1;
		source->power_register_mirror= AB8500_POWER_DUMMY_VALUE ;
	}

	for (i=0;i<ARRAY_SIZE(ab8500_startup_reasons);i++) {
		if (source->power_register_mirror & ab8500_startup_reasons[i].mask) {
			count++ ;
		}
	}
	return count ;
}

static char * ab8500_get_startup_reason(struct startup_reason_source * source ,int index)
{
	int i ;
	int count=0 ;
	struct ab8500_startup_reason_source * ab8500_source 
		= container_of(source,struct ab8500_startup_reason_source,source);
	if (!ab8500_source->mirror_is_valid) {
		ab8500_source->mirror_is_valid = 1;
		ab8500_source->power_register_mirror= AB8500_POWER_DUMMY_VALUE ;

	}
	for (i=0;i<ARRAY_SIZE(ab8500_startup_reasons);i++)
	{
		if (ab8500_source->power_register_mirror & ab8500_startup_reasons[i].mask)
		{
			if (count == index){
				dev_dbg("exitering %s found name %s\n ",__FUNCTION__,ab8500_startup_reasons[i].name);

				return ab8500_startup_reasons[i].name ;
			}
			count++ ;
		}
	}
	return NULL ;	
}

static struct platform_driver ab8500_startup_reason ;
static struct ab8500_startup_reason_source ab8500;

/* register bank for AB8500_SYS_CTRL1_BLOCK*/
static int __devexit ab8500_startup_reason_remove(struct platform_device *pdev)
{
	platform_driver_unregister(&ab8500_startup_reason);
	return 0;
}

static int __devinit ab8500_startup_reason_probe(struct platform_device * pdev)
{
	int ret_value=0 ;
	ab8500.ab8500_dev=&pdev->dev;
	ab8500.mirror_is_valid= (abx500_get_register_interruptible(ab8500.ab8500_dev,
	                                        AB8500_SYS_CTRL1_BLOCK,
						AB8500_TURNON_STATUS_REGISTER_ADDRESS,
						&ab8500.power_register_mirror)<0)?0:1 ;
	if (ab8500.mirror_is_valid){
		ret_value=register_startup_reasons(&ab8500.source,ab8500_count_startup_reasons(&ab8500),&pdev->dev);
	}
	return ret_value ;
}

static struct platform_driver ab8500_startup_reason_driver = {
	.probe = ab8500_startup_reason_probe,
	.remove = __devexit_p(ab8500_startup_reason_remove),
	.driver = {
		.name = "ab8500-startup_reason",
		.owner = THIS_MODULE,
	},
};


static struct ab8500_startup_reason_source ab8500={
	.source ={	.name = "ab8500",	
			.owner = THIS_MODULE,
			.get_reasons_name = ab8500_get_startup_reason,},
};


static int __init ab8500_startup_reason_init(void)
{
	int ret_value=0 ;
	ret_value = platform_driver_register(&ab8500_startup_reason_driver);
	return ret_value ;
}

static void __exit ab8500_startup_reason_exit(void)
{
	platform_driver_unregister(&ab8500_startup_reason);
}

subsys_initcall(ab8500_startup_reason_init);
module_exit(ab8500_startup_reason_exit);

MODULE_DESCRIPTION("Reports how system was started");
MODULE_AUTHOR("Andrew Roca ");
MODULE_LICENSE("GPL");


