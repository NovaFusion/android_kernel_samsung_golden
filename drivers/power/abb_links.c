#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>

#define AB8500_PROC_DEBUG_ENTRY 1

extern struct class * power_supply_class ;
extern struct class *sec_class;

struct charger_extra_sysfs
{
	struct device *dev;	
	int polling_active ;
	int batt_lp_charging;
	int batt_reinit_capacity;
	int suspend_lock;
	int siop_activated;
	struct proc_dir_entry * proc_entry ;
	struct power_supply btemp_psy;
	struct workqueue_struct *polling_queue;
	struct delayed_work polling_work;
	struct wake_lock test_wake_lock;
} ;	

struct charging_query
{
	int check_type ;
	int power_supply_type ;
	int property_enum;
	union power_supply_propval value ;
} ;

struct average_attribute {
	struct device_attribute device_attr ;
	int next_entry;
	int entry_count ;
	int last_entries[5] ;
	} ;

struct adc_attribute {
	struct device_attribute device_attr;
	unsigned adc_code ;
	struct average_attribute  * average;
	int last_value ;
	int is_voltage;
	} ;

struct callback_attribute {
	struct device_attribute device_attr;
	int (*update)(struct callback_attribute *,int *res);
	struct average_attribute  * average;
	int last_value ;
	} ;

struct power_supply_attribute {
	struct device_attribute device_attr;
	int check_type ;
	int power_supply_type ;
	int property_enum;
	struct average_attribute  * average;
	int last_value ;
	} ;

/* Main battery properties */
static enum power_supply_property ab8500_battery_props[] = {
	POWER_SUPPLY_PROP_BATT_CAL,	/* Calibarating Vbat ADC*/
	POWER_SUPPLY_PROP_LPM_MODE,	/* LPM mode */
	POWER_SUPPLY_PROP_REINIT_CAPACITY, /* Re-initialize capacity */
	POWER_SUPPLY_PROP_SIOP,	/* Adjust charging current */
};

static struct charger_extra_sysfs charger_extra_sysfs ={0} ;


static int _power_supply_get_property(struct device *dev, void *data)
{
	struct charging_query * query = (struct charging_query *) data ;
	struct power_supply *psy = dev_get_drvdata(dev);
	int retvalue= 0;

	if (query) {
		if ( query->check_type && psy->type != query->power_supply_type   ) {
		retvalue= 0 ;
		}
		else {
			if (0==psy->get_property(psy,query->property_enum, &query->value)) {
			retvalue= 1 ;
			}
		}
	}

	return retvalue;
}

static ssize_t show_empty_reading(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",0);
}

static ssize_t show_average(struct device *dev, struct device_attribute *attr, char *buf)
{
	int tmp = 0 ;
	int number_to_do = 5 ;
	int i ;
	struct average_attribute * instance =  container_of(attr, struct average_attribute , device_attr);

	if (instance->entry_count) {
		if (instance->entry_count<5)
			number_to_do=instance->entry_count ;

		for (i=0;i<number_to_do ;i++ )
			tmp+=instance->last_entries[i];

		tmp /= number_to_do;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n",tmp);
}

static void update_average(struct average_attribute * average, int new_value)
{
	average->last_entries[average->next_entry++]=new_value;

	if (average->next_entry>=5)
		average->next_entry = 0 ;

	if(average->entry_count< 10000)
		average->entry_count++ ; 
}

static void update_adc_attribute(struct adc_attribute * instance)
{
	if( instance->adc_code != 0 ) {
		instance->last_value =
			ab8500_gpadc_read_raw(ab8500_gpadc_get(),
					      instance->adc_code, SAMPLE_16, RISING_EDGE, 0, ADC_SW);

		if (instance->average)
			update_average(instance->average ,instance->last_value);
	}

}

static void update_power_supply_attribute(struct power_supply_attribute * instance)
{
	struct charging_query query={instance->check_type,instance->power_supply_type ,instance->property_enum,{0}} ;

	if (class_for_each_device(power_supply_class,NULL,&query,_power_supply_get_property)) {
		instance->last_value= query.value.intval;
		if (instance->average) {
			if(instance->property_enum == POWER_SUPPLY_PROP_VOLTAGE_MIN)
				update_average(instance->average ,instance->last_value / 1000) ;
			else
				update_average(instance->average ,instance->last_value) ;
		}
		
	}
}


#define ABSOLUTE_ZERO (-273)

extern int measure_battery_temperature(void);

static int temperature_update(struct callback_attribute * instance,int * res)
{
	int val = measure_battery_temperature() ;

	if (val>ABSOLUTE_ZERO) {
		*res=val*10 ;
		return 0 ;
	}		

	return -1 ;
}


static void update_callback_attribute(struct callback_attribute * instance)
{
	int ret = -1;
	int val ;

	if (instance->update){
		ret = instance->update(instance,&val);
	}

	if (ret==0) {
		instance->last_value = val ; 
		if (instance->average)
			update_average(instance->average ,val) ;
	}
}

static ssize_t show_callback_attribute(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct callback_attribute * instance =  container_of(attr, struct callback_attribute , device_attr);

	if (!charger_extra_sysfs.polling_active)
		update_callback_attribute( instance ) ;

	return snprintf(buf, PAGE_SIZE, "%d\n",instance->last_value );
}


static ssize_t show_adc(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adc_attribute * instance =  container_of(attr, struct adc_attribute , device_attr);

	if (!charger_extra_sysfs.polling_active)
		update_adc_attribute( instance ) ;

	return snprintf(buf, PAGE_SIZE, "%d\n",instance->last_value );
}

static ssize_t show_batt_adc_cal(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adc_attribute *instance = container_of(attr, struct adc_attribute, device_attr);

	return snprintf(buf, PAGE_SIZE, "%d\n", instance->last_value );
}

static ssize_t store_batt_adc_cal(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct adc_attribute *instance = container_of(attr, struct adc_attribute, device_attr);
	
	int batt_adc_cal;
	
	sscanf(buf, "%d\n", &batt_adc_cal);
	instance->last_value = batt_adc_cal;

	dev_info(dev,
		 "Battery voltage adc calibration value is written as %d\n",
		 instance->last_value);

	power_supply_changed(&charger_extra_sysfs.btemp_psy);
	
	return count;
}

static ssize_t show_power_supply_attribute(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct power_supply_attribute * instance =  container_of(attr, struct power_supply_attribute , device_attr);

	if (!charger_extra_sysfs.polling_active)
		update_power_supply_attribute(instance ) ;

	return snprintf(buf, PAGE_SIZE, "%d\n",instance->last_value );
}


static struct average_attribute average_attributes[] = {
	{ __ATTR(batt_temp_adc_aver,0444, show_average,NULL),0 },
	{ __ATTR(batt_vol_adc_aver,0444, show_average,NULL),0 },
	{ __ATTR(batt_vol_aver,0444, show_average,NULL),0 },
	{ __ATTR(batt_temp_aver,0444, show_average,NULL),0 },
} ; 


static struct callback_attribute callback_attributes[] = {
	{ __ATTR(batt_temp,0444, show_callback_attribute,NULL),temperature_update ,&average_attributes[3],0 },
} ; 

static struct adc_attribute adc_attributes[] = {
	{ __ATTR(batt_temp_adc,0444, show_adc,NULL),BTEMP_BALL ,&average_attributes[0],0, 0 },
	{ __ATTR(batt_v_f_adc,0444, show_adc,NULL),BAT_CTRL,NULL,0, 0 },
	{ __ATTR(batt_vol_adc_cal,0664, show_batt_adc_cal, store_batt_adc_cal), 0, NULL, 0, 0},
} ; 

struct power_supply_attribute power_supply_attributes[] = { 
	{ __ATTR(capacity, 0444, show_power_supply_attribute, NULL), 1,
	  POWER_SUPPLY_TYPE_BATTERY, POWER_SUPPLY_PROP_CAPACITY, NULL, 0},
	{ __ATTR(batt_soc, 0444, show_power_supply_attribute, NULL), 1,
	  POWER_SUPPLY_TYPE_BATTERY, POWER_SUPPLY_PROP_CAPACITY, NULL, 0},
	{ __ATTR(charging_source, 0444, show_power_supply_attribute, NULL), 1,
	  POWER_SUPPLY_TYPE_BATTERY, POWER_SUPPLY_PROP_CHARGING_SOURCE,
	  NULL, 0},
	{ __ATTR(batt_vol_adc, 0444, show_power_supply_attribute, NULL), 1,
	  POWER_SUPPLY_TYPE_BATTERY, POWER_SUPPLY_PROP_VOLTAGE_MAX,
	  &average_attributes[1], 0},
	{ __ATTR(batt_vol, 0444, show_power_supply_attribute, NULL), 1,
	  POWER_SUPPLY_TYPE_BATTERY, POWER_SUPPLY_PROP_VOLTAGE_MIN,
	  &average_attributes[2], 0},
} ; 

static void add_power_supply_attributes(struct device *dev)
{
	int i ;
	int res ;

	for (i=0;i<ARRAY_SIZE(power_supply_attributes);i++) {
		res=device_create_file(dev, &power_supply_attributes[i].device_attr);
		if (res != 0) {
			dev_info(dev,
			 "Error %d creating sys file %s\n",
			 res,
			 power_supply_attributes[i].device_attr.attr.name);
		}
	}
}

static void update_power_supply_attributes(void)
{
	int i ;

	for (i=0;i<ARRAY_SIZE(power_supply_attributes);i++) {
		update_power_supply_attribute( &power_supply_attributes[i] );
	}
}

static void update_callback_attributes(void)
{
	int i ;

	for (i=0;i<ARRAY_SIZE(callback_attributes);i++) {
		update_callback_attribute( &callback_attributes[i] );
	}
}

static void update_polled_attributes(void)
{
	int i ;

	for (i=0;i<ARRAY_SIZE(adc_attributes);i++) {
		update_adc_attribute( &adc_attributes[i] );
	}
}

static void add_adc_attributes(struct device *dev)
{
	int i ;
	int res ;

	for (i=0;i<ARRAY_SIZE(adc_attributes);i++) {
		res=device_create_file(dev, &adc_attributes[i].device_attr);
		if (res!=0) {
			dev_info(dev, "Error %d creating sys file %s\n",
				 res,
				 adc_attributes[i].device_attr.attr.name);
		}
	}
}

static void add_callback_attributes(struct device *dev)
{
	int i ;
	int res ;

	for (i=0;i<ARRAY_SIZE(callback_attributes);i++) {
		res=device_create_file(dev, &callback_attributes[i].device_attr);
		if (res!=0) {
			dev_info(dev, "Error %d creating sys file %s\n",
				 res,
				 callback_attributes[i].device_attr.attr.name);
		}
	}
}

static void add_average_attributes(struct device *dev)
{
	int i ;
	int res ;

	for (i=0;i<ARRAY_SIZE(average_attributes);i++) {
		res=device_create_file(dev, &average_attributes[i].device_attr);
		if (res!=0) {
			dev_info(dev, "Error %d creating sys file %s\n",
				 res,
				 average_attributes[i].device_attr.attr.name);
		}
	}
}

static void ab8500_charger_polling_periodic_work(struct work_struct *work)
{
	struct charger_extra_sysfs *instance = container_of(work, struct charger_extra_sysfs, polling_work.work);

	if (instance->polling_active) {
		update_polled_attributes() ;
		update_power_supply_attributes() ;
		update_callback_attributes();
		/* Schedule a new set of measurements */
		queue_delayed_work(instance->polling_queue,&instance->polling_work,round_jiffies(5 * HZ));
	}
}


static ssize_t show_batt_test_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",charger_extra_sysfs.polling_active);
}

/*
	Todo change temperate and capacity polling speeds
*/
static ssize_t store_batt_test_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	long start ;

	if(strict_strtol(buf,10,&start)>=0) {
		if (start) {
			if(!charger_extra_sysfs.polling_active) {
				queue_delayed_work(charger_extra_sysfs.polling_queue,&charger_extra_sysfs.polling_work,round_jiffies(1));
				charger_extra_sysfs.polling_active=1 ;
			} 
			
		} else {
			charger_extra_sysfs.polling_active=0 ;
		}
	}

	return 0 ;
}

static ssize_t show_battery_type(struct device *dev, struct device_attribute *attr, const char *buf)
{
	char *batt_pack_str = "ELE";
	char *batt_cell_str = "HIT";

	return scnprintf(buf, PAGE_SIZE, "%s_%s\n", batt_pack_str, batt_cell_str);
}

static ssize_t show_batt_reinit_capacity(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			charger_extra_sysfs.batt_reinit_capacity);
}

static ssize_t store_batt_reinit_capacity(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int batt_reinit_capacity;

	sscanf(buf, "%d\n", &batt_reinit_capacity);

	charger_extra_sysfs.batt_reinit_capacity = batt_reinit_capacity;
	power_supply_changed(&charger_extra_sysfs.btemp_psy);

	return 0 ;
}

static ssize_t show_batt_lp_charging(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",charger_extra_sysfs.batt_lp_charging);
}

static ssize_t store_batt_lp_charging(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	int batt_lp_charging;
	
	sscanf(buf, "%d\n", &batt_lp_charging);

	charger_extra_sysfs.batt_lp_charging = batt_lp_charging;
	dev_info(dev, "LP Charging = %d\n", batt_lp_charging);
	power_supply_changed(&charger_extra_sysfs.btemp_psy);

	return 0 ;
}

static ssize_t show_suspend_lock(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",charger_extra_sysfs.suspend_lock);
}

static ssize_t store_suspend_lock(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	sscanf(buf, "%d\n", &charger_extra_sysfs.suspend_lock);

	if(charger_extra_sysfs.suspend_lock)
		wake_lock(&charger_extra_sysfs.test_wake_lock);
	else
		wake_lock_timeout(&charger_extra_sysfs.test_wake_lock, HZ);

	return 0 ;
}

static ssize_t store_fg_reset_soc(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	int reset_soc;

	sscanf(buf, "%d\n", &reset_soc);

	dev_info(dev, "ab8500_fg_reinit called !!\n");
	if(reset_soc)
		ab8500_fg_reinit();

	return 0 ;
}

static ssize_t show_siop_activated(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			charger_extra_sysfs.siop_activated);
}

static ssize_t store_siop_activated(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int siop_activated;

	sscanf(buf, "%d\n", &siop_activated);

	charger_extra_sysfs.siop_activated = siop_activated;
	dev_info(dev, "SIOP activation status : %d\n", siop_activated);
	power_supply_changed(&charger_extra_sysfs.btemp_psy);

	return 0 ;
}

static struct device_attribute misc_attributes[] = {
	__ATTR(batt_test_mode, 0644, show_batt_test_mode, store_batt_test_mode),
	__ATTR(batt_type, 0644, show_battery_type, NULL),
	__ATTR(batt_temp_adc_cal, 0444, show_empty_reading, NULL),
	__ATTR(batt_lp_charging, 0664, show_batt_lp_charging,
				      store_batt_lp_charging),
	__ATTR(batt_reinit_capacity, 0664, show_batt_reinit_capacity,
				      store_batt_reinit_capacity),
	__ATTR(suspend_lock, 0644, show_suspend_lock, store_suspend_lock),
	__ATTR(fg_reset_soc, 0664, NULL, store_fg_reset_soc),
	__ATTR(siop_activated, 0664, show_siop_activated, store_siop_activated),
} ;


static void add_misc_attributes(struct device *dev)
{
	int i ;
	int res ;

	for (i=0;i<ARRAY_SIZE(misc_attributes);i++) {
		res=device_create_file(dev, &misc_attributes[i]);
		if (res!=0) {
			dev_info(dev, "Error %d creating sys file %s\n",
				 res,
				 misc_attributes[i].attr.name);
		}
	}
}


static int battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	switch (psp) {

	case POWER_SUPPLY_PROP_BATT_CAL:	/* Calibarating vbat */
		val->intval = adc_attributes[2].last_value;
		break;
	
	case POWER_SUPPLY_PROP_LPM_MODE:    /* LPM mode */
		val->intval = charger_extra_sysfs.batt_lp_charging; /* 0 or 1 */
		break;

	case POWER_SUPPLY_PROP_REINIT_CAPACITY:    /* Re-initialize capacity */
		val->intval =
			charger_extra_sysfs.batt_reinit_capacity; /* 0 or 1 */
		break;

	case POWER_SUPPLY_PROP_SIOP:     /* SIOP active/deactive */
		val->intval = charger_extra_sysfs.siop_activated;  /* 0 or 1 */
		break;

	default :
		return -EINVAL;
	}

	return 0;
}

static void battery_power_changed(struct power_supply *psy)
{

}


static char *ab8500_battery_supplied_to[] = {
	"ab8500_fg",
	"ab8500_chargalg",
};

/*
	To create a bunch of /sys entries for DFMS application. We need a dummy power supply so we can have an entry /sys/class/power_supply 
	without breaking all the for_each_class calls in the rest of the charging.
	We also attach a dummy device to the sec device class device can be part of the /sys/devices/virtual/sec path 

*/
int make_dfms_battery_device (void)
{
	charger_extra_sysfs.polling_queue = create_singlethread_workqueue("ab8500_charging_monitor");
	INIT_DELAYED_WORK_DEFERRABLE(&charger_extra_sysfs.polling_work,ab8500_charger_polling_periodic_work);
	charger_extra_sysfs.btemp_psy.name = "battery";
	charger_extra_sysfs.btemp_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	charger_extra_sysfs.btemp_psy.properties = ab8500_battery_props;
	charger_extra_sysfs.btemp_psy.num_properties = ARRAY_SIZE(ab8500_battery_props);
	charger_extra_sysfs.btemp_psy.get_property = battery_get_property;
	charger_extra_sysfs.btemp_psy.supplied_to = ab8500_battery_supplied_to;
	charger_extra_sysfs.btemp_psy.num_supplicants = ARRAY_SIZE(ab8500_battery_supplied_to);
	charger_extra_sysfs.btemp_psy.external_power_changed =battery_power_changed;
	power_supply_register( NULL, &charger_extra_sysfs.btemp_psy) ;
	add_adc_attributes(charger_extra_sysfs.btemp_psy.dev ) ;
	add_average_attributes(charger_extra_sysfs.btemp_psy.dev ) ;
	add_misc_attributes(charger_extra_sysfs.btemp_psy.dev);
	add_callback_attributes(charger_extra_sysfs.btemp_psy.dev);
	add_power_supply_attributes(charger_extra_sysfs.btemp_psy.dev );
	return 0 ;
}

#ifdef AB8500_PROC_DEBUG_ENTRY
/*
	Create a dummy file in /proc to dump out the AB8500 registers

*/
struct ab8500_power_register { 
	char * name	     ;
	unsigned char region ;
	unsigned char address ;

};

static struct ab8500_power_register ab8500_power_registers[] = { 
{	"MainChStatus1                 "	,0x0b 	,0x00	},
{	"MainChStatus2                 "	,0x0b 	,0x01	},
{	"UsbChStatus1                  "	,0x0b 	,0x02	},
{	"UsbChStatus2                  "	,0x0b 	,0x03	},
{	"ChargerStatus                 "	,0x0b 	,0x05	},
{	"ChargerVoltageLevel           "	,0x0b 	,0x40	},
{	"ChargerMaxVoltage             "	,0x0b 	,0x41	},
{	"ChargerCurrentLevel           "	,0x0b 	,0x42	},
{	"ChargerMaxCurrent             "	,0x0b 	,0x43	},
{	"OutputCurrentTempLowMedium    "	,0x0b 	,0x44	},
{	"ChargerWatchdogTime           "	,0x0b 	,0x50	},
{	"ChargerWatchdogControl        "	,0x0b 	,0x51	},
{	"BatteryTemperatureHighThresold"	,0x0b 	,0x52	},
{	"LedPowerControl               "	,0x0b 	,0x53	},
{	"LedPowerDuty                  "	,0x0b 	,0x54	},
{	"BatteryOVV                    "	,0x0b 	,0x55	},
{	"ChargerControl                "	,0x0b 	,0x56	},
{	"MainChControl1                "	,0x0b 	,0x80	},
{	"MainChControl2                "	,0x0b 	,0x81	},
{	"MainChInputCurrent            "	,0x0b 	,0x82	},
{	"UsbChControl1                 "	,0x0b 	,0xc0	},
{	"UsbChControl2                 "	,0x0b 	,0xc1	},
{	"UsbChInputCurrent             "	,0x0b 	,0xc2	},
{	"CoulombCounterControl         "	,0x0c 	,0x00	},
{	"CCSampleL                     "	,0x0c 	,0x07	},
{	"CCSampleH                     "	,0x0c 	,0x08	},
{	"CCAverageOffset               "	,0x0c 	,0x09	},
{	"CCCounterOffset               "	,0x0c 	,0x0a	},
{	"CCNconvAcc                    "	,0x0c 	,0x10	},
{	"CCNconvAccControl             "	,0x0c 	,0x11	},
{	"CCNconvAccL                   "	,0x0c 	,0x12	},
{	"CCNconvAccM                   "	,0x0c 	,0x13	},
{	"CCNconvAccH                   "	,0x0c 	,0x14	},
{	"USB line status               "	,0x05 	,0x80	},
{	"USB line control 1            "	,0x05 	,0x81	},
{	"USB line control 2            "	,0x05 	,0x82	},
{	"USB line control 3            "	,0x05 	,0x83	},
} ;


static int charging_readproc(char *page, char **start, off_t off,
                          int count, int *eof, void *data)
{
	struct device *dev = (struct device *) data ;
	int i ;
	unsigned char c ;
	int len = 0;
	int ret ;

	for (i=0;i<ARRAY_SIZE(ab8500_power_registers);i++) {
		c=0xff ;
		ret = abx500_get_register_interruptible(dev,
				ab8500_power_registers[i].region, 
				ab8500_power_registers[i].address, &c);
		if (ret>=0) {
			len+=sprintf(page+len,"%s = 0x%02x\n",ab8500_power_registers[i].name,c);
		msleep(10);
		}
	}
	*eof=-1;

	return len ;
}

#endif //AB8500_PROC_DEBUG_ENTRY

void register_charging_i2c_dev(struct device * dev) /* todo add destructor call for caller */
{
	dev_info(dev, "%s %d\n", __func__, __LINE__);
	if (!charger_extra_sysfs.dev) {
		charger_extra_sysfs.dev = dev ;
#ifdef AB8500_PROC_DEBUG_ENTRY
		charger_extra_sysfs.proc_entry = create_proc_read_entry("AB8500_CHG",0444,NULL,charging_readproc,dev);
#endif //AB8500_PROC_DEBUG_ENTRY

		make_dfms_battery_device ( ) ;

		wake_lock_init(&charger_extra_sysfs.test_wake_lock, WAKE_LOCK_SUSPEND, "suspend lock");
	}
}


static int __init ab8500_links_init(void)
{
	return 0;
}

static void __exit ab8500_links_exit(void)
{
	
}

subsys_initcall_sync(ab8500_links_init);
module_exit(ab8500_links_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andrew Roca");
MODULE_ALIAS("platform:ab8500-links");
MODULE_DESCRIPTION("AB8500 Power Symbolic Links");
