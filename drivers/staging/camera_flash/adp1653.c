/*
 * Copyright (C) ST-Ericsson SA 2010
 * adp1653: Driver Adp1653 HPLED flash driver chip. This driver
 *          currently support I2C interface, 2bit interface is not supported.
 * Author: Pankaj Chauhan/pankaj.chauhan@stericsson.com for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c/adp1653_plat.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/mach-types.h>
#include "flash_common.h"
#include "adp1653.h"

/* This data is platform specific for 8500 href-v1 platform,
 * Ideally this should be supplied from platform code
 */

static int adapter_i2c2 = 2;
static int flash_position = 0;
module_param(adapter_i2c2, int, S_IRUGO);
MODULE_PARM_DESC(adapter_i2c2, "use the given I2C adaptater to communicate with the chip");
module_param(flash_position, int, S_IRUGO);
MODULE_PARM_DESC(flash_position, "the position of the flash chip (0=PRIMARY, 1=SECONDARY)");


int __flash_gpio_to_irq(int gpio)
{

	return NOMADIK_GPIO_TO_IRQ(gpio);
}

#define DEBUG_LOG(...) printk(KERN_DEBUG "Adp1653 flash driver: " __VA_ARGS__)

#define ADP1653_SUPPORTED_MODES (FLASH_MODE_VIDEO_LED | FLASH_MODE_STILL_LED | \
	FLASH_MODE_STILL_LED_EXTERNAL_STROBE |                                 \
	FLASH_MODE_AF_ASSISTANT | FLASH_MODE_INDICATOR)

#define ADP1653_SELFTEST_SUPPORTED_MODES (FLASH_SELFTEST_CONNECTION | FLASH_SELFTEST_FLASH_WITH_STROBE | \
	FLASH_SELFTEST_VIDEO_LIGHT | FLASH_SELFTEST_AF_LIGHT | FLASH_SELFTEST_INDICATOR | FLASH_SELFTEST_TORCH_LIGHT)

static int adp1653_trigger_strobe(void *priv_data, int enable);

static int adp1653_get_modes(void *priv_data,unsigned long *modes)
{
	int err;
	struct adp1653_priv_data *priv_p = (struct adp1653_priv_data *)priv_data;
	err = i2c_smbus_read_byte_data(priv_p->i2c_client, FAULT_STATUS_REG);
	if (err)
		*modes = 0x0;
	else
		*modes = ADP1653_SUPPORTED_MODES;
	return 0;
}

static int adp1653_get_mode_details(void *priv_data, unsigned long mode,
struct flash_mode_details *details_p)
{
	int err = 0;
	memset(details_p,0,sizeof(struct flash_mode_details));

	details_p->led_type = 2;

	/* Still LED settings*/
	details_p->nbFaultRegisters = 1;
	if(mode & (FLASH_MODE_STILL_LED | FLASH_MODE_STILL_LED_EXTERNAL_STROBE)){
		details_p->max_intensity_uAmp = FLASH_MAX_INTENSITY;
		details_p->min_intensity_uAmp = FLASH_MIN_INTENSITY;
		details_p->max_strobe_duration_uSecs = FLASH_MAX_STROBE_DURATION;
		details_p->feature_bitmap = INTENSITY_PROGRAMMABLE | DURATION_PROGRAMMABLE;
		goto out;
	}
	/*Video LED settings*/
	if(mode & FLASH_MODE_VIDEO_LED){
		details_p->max_intensity_uAmp = TORCH_MAX_INTENSITY;
		details_p->min_intensity_uAmp = TORCH_MIN_INTENSITY;
		details_p->max_strobe_duration_uSecs = 0;
		details_p->feature_bitmap = INTENSITY_PROGRAMMABLE;
		goto out;
	}
	/*Privacy Indicator settings */
	if(mode & FLASH_MODE_INDICATOR){
		details_p->max_intensity_uAmp = ILED_MAX_INTENSITY;
		details_p->min_intensity_uAmp = ILED_MIN_INTENSITY;
		details_p->max_strobe_duration_uSecs = 0;
		details_p->feature_bitmap = INTENSITY_PROGRAMMABLE;
		goto out;
	}
	DEBUG_LOG("Mode %lx, not supported\n",mode);
	err = EINVAL;
out:
	return err;
}

static int adp1653_enable_flash_mode(void *priv_data,
				     unsigned long mode, int enable)
{
	int err = 0;
	struct adp1653_priv_data *priv_p = (struct adp1653_priv_data *)priv_data;

	if(enable){

		if((!(mode & ADP1653_SUPPORTED_MODES)) &&
		  (mode != FLASH_MODE_NONE)) {
			DEBUG_LOG("Unsupported mode %lx\n",mode);
			err = -EINVAL;
			goto out;
		}
		/*Nothing to be done in enabling, just set current mode and return*/
		/*May be enable disable can be done here but why not enable in
		*probe and keep it on always
		*/
		adp1653_trigger_strobe(priv_p,0);
		priv_p->curr_mode = mode;
	}else{
		adp1653_trigger_strobe(priv_p,0);
		priv_p->curr_mode =0;
	}
out:
	return err;
}

static int adp1653_configure_flash_mode(void *priv,unsigned long mode,
struct flash_mode_params *params_p)
{
	int err = 0;
	unsigned char intensity_code;
	struct adp1653_priv_data *priv_p = (struct adp1653_priv_data *)priv;

	if(!(mode & ADP1653_SUPPORTED_MODES)){
		DEBUG_LOG("Mode %lx not supported\n",mode);
		err = -EINVAL;
		goto out;
	}
	switch(mode){
	case FLASH_MODE_STILL_LED:
	case FLASH_MODE_STILL_LED_EXTERNAL_STROBE:
		{
			FLASH_UAMP_TO_CODE(intensity_code,params_p->intensity_uAmp);
			if(params_p->duration_uSecs){
				DURATION_USEC_TO_CODE(priv_p->flash_duration,
					params_p->duration_uSecs);
				DEBUG_LOG("Duration %lu, code 0x%x\n",params_p->duration_uSecs,
					priv_p->flash_duration);
				priv_p->flash_duration |= TIMER_ENABLE;
			}else{
				priv_p->flash_duration = 0;
			}
			priv_p->flash_intensity = intensity_code << 3;
		}
		break;
	case FLASH_MODE_VIDEO_LED:
		{
			TORCH_UAMP_TO_CODE(intensity_code,params_p->intensity_uAmp);
			DEBUG_LOG("Torch mode setting intensity 0x%x, current(uA) %lu\n",
				intensity_code,params_p->intensity_uAmp);
			priv_p->torch_intensity = intensity_code << 3;
		}
		break;
	case FLASH_MODE_INDICATOR:
		{
			ILED_UAMP_TO_CODE(intensity_code,params_p->intensity_uAmp);
			DEBUG_LOG("ILED setting intensity 0x%x, current(uA) %lu\n",
				intensity_code,params_p->intensity_uAmp);
			priv_p->indicator_intensity = intensity_code;
		}
		break;
	default:
		err = -EINVAL;
		DEBUG_LOG("Unsupported mode %lx\n",mode);
		break;
	}

	if((mode == FLASH_MODE_STILL_LED_EXTERNAL_STROBE) || (mode == FLASH_MODE_STILL_LED))
	{
		adp1653_trigger_strobe(priv_p,0);
		DEBUG_LOG("CONFIG_TIMER_REG : 0x%x\n",priv_p->flash_duration);
		DEBUG_LOG("OUTPUT_SEL_REG : 0x%x\n",priv_p->flash_intensity);

		/*TimeOut Must be programmed before Intensity*/
		err = i2c_smbus_write_byte_data(priv_p->i2c_client,CONFIG_TIMER_REG,
			priv_p->flash_duration);
		if(err){
			DEBUG_LOG("I2C: Unsable to write timer config, err %d\n",err);
			goto out;
		}
		err = i2c_smbus_write_byte_data(priv_p->i2c_client,OUTPUT_SEL_REG,
			priv_p->flash_intensity);
		if(err){
			DEBUG_LOG("I2C: Unable to write   OUTPUT_SEL_REG , err %d\n",err);
			goto out;
		}
	}
out:
	return err;
}

static int adp1653_set_intensity(struct adp1653_priv_data *priv_p, uint8_t intensity)
{
	return i2c_smbus_write_byte_data(priv_p->i2c_client,OUTPUT_SEL_REG,intensity);
}

static int adp1653_strobe_still_led(struct adp1653_priv_data *priv_p,int enable)
{
	int err=0,gpio_val;
	uint8_t intensity,duration;

	if(enable){
		intensity = priv_p->flash_intensity;
		duration = priv_p->flash_duration;
		gpio_val = 1;
	}else{
		intensity = 0;
		duration = 0;
		gpio_val = 0;
	}

	err = adp1653_set_intensity(priv_p,intensity);
	if(err){
		DEBUG_LOG("I2C: Unable to write OUTPUT_SEL_REG reg, err %d\n",err);
		goto out;
	}

	/*TimeOut Must be programmed before Intensity*/
	err = i2c_smbus_write_byte_data(priv_p->i2c_client,CONFIG_TIMER_REG,
		priv_p->flash_duration);
	if(err){
		DEBUG_LOG("I2C: Unsable to write timer config, err %d\n",err);
		goto out;
	}
	err = i2c_smbus_write_byte_data(priv_p->i2c_client,OUTPUT_SEL_REG,intensity);
	if(err){
		DEBUG_LOG("I2C: Unable to write OUTPUT_SEL_REG, err %d\n",err);
		goto out;
	}

out:
	return err;
}

static int adp1653_trigger_strobe(void *priv, int enable)
{
	int err = 0;
	uint8_t intensity;
	struct adp1653_priv_data *priv_p = (struct adp1653_priv_data *)priv;

	switch(priv_p->curr_mode){
	case FLASH_MODE_STILL_LED:
	case FLASH_MODE_STILL_LED_EXTERNAL_STROBE:
		err = adp1653_strobe_still_led(priv_p,enable);
		break;
	case FLASH_MODE_VIDEO_LED:
		{
			if(enable)
				intensity = priv_p->torch_intensity;
			else
				intensity = 0;
			err = adp1653_set_intensity(priv_p,intensity);
		}
		break;
	case FLASH_MODE_INDICATOR:
		{
			if(enable)
				intensity = priv_p->indicator_intensity;
			else
				intensity =0;
			err = adp1653_set_intensity(priv_p,intensity);
		}
		break;
	default:
		DEBUG_LOG("Unsupported mode %lx\n",priv_p->curr_mode);
		goto out;
	}
	if(err){
		DEBUG_LOG("Unable to enable/disable %d, strobe. Mode %lx, err %d\n",enable,
			priv_p->curr_mode,err);
		goto out;
	}
	disable_irq(priv_p->i2c_client->irq);
	if(enable)
		SET_FLASH_STATUS(priv_p->status,FLASH_STATUS_LIT);
	else
		CLR_FLASH_STATUS(priv_p->status,FLASH_STATUS_LIT);

	enable_irq(priv_p->i2c_client->irq);

out:
	return err;
}
#define FLASH_ERR_ALL	(FLASH_ERR_OVER_CHARGE |FLASH_ERR_OVER_HEAT |	\
	FLASH_ERR_SHORT_CIRCUIT | FLASH_ERR_TIMEOUT |			\
	FLASH_ERR_OVER_VOLTAGE)
int  adp1653_get_status(void *priv_data,unsigned long *status)
{
	struct adp1653_priv_data *priv_p= (struct adp1653_priv_data *)priv_data;
	disable_irq(priv_p->i2c_client->irq);
	if(priv_p->fault){
		if(priv_p->fault & OVER_VOLTAGE_FAULT)
			SET_FLASH_ERROR(priv_p->status,FLASH_ERR_OVER_VOLTAGE);
		if(priv_p->fault & TIMEOUT_FAULT)
			SET_FLASH_ERROR(priv_p->status,FLASH_ERR_TIMEOUT);
		if(priv_p->fault & OVER_TEMPERATURE_FAULT)
			SET_FLASH_ERROR(priv_p->status,FLASH_ERR_OVER_HEAT);
		if(priv_p->fault & SHORT_CIRCUIT_FAULT){
			CLR_FLASH_STATUS(priv_p->status,FLASH_STATUS_READY);
			SET_FLASH_STATUS(priv_p->status,FLASH_STATUS_BROKEN);
			SET_FLASH_ERROR(priv_p->status,FLASH_ERR_SHORT_CIRCUIT);
		}
		priv_p->fault =0;
	}else{
		CLR_FLASH_ERROR(priv_p->status,FLASH_ERR_ALL);
	}
	enable_irq(priv_p->i2c_client->irq);
	*status = priv_p->status;
	return 0;
}

int adp1653_get_selftest_modes(void *priv_data, unsigned long *modes)
{
	int err;
	struct adp1653_priv_data *priv_p = (struct adp1653_priv_data *)priv_data;
	err = i2c_smbus_read_byte_data(priv_p->i2c_client, FAULT_STATUS_REG);
	if (err) *modes = 0x0;
	else     *modes = ADP1653_SELFTEST_SUPPORTED_MODES;
	return 0;
}

int adp1653_get_fault_registers(void *priv_data, unsigned long mode, unsigned long *status)
{
	int err = 0;
	struct adp1653_priv_data *priv_p = (struct adp1653_priv_data *)priv_data;

	*status = i2c_smbus_read_byte_data(priv_p->i2c_client, FAULT_STATUS_REG);

	/* clear fault register */
	err = i2c_smbus_write_byte_data(priv_p->i2c_client,OUTPUT_SEL_REG,0);
	if(0 != err)
	{
		DEBUG_LOG("Unable to write OUTPUT_SEL_REG, err %d\n",err);
	}
	return err;
}

struct flash_chip_ops adp1653_ops = {
	.get_modes = adp1653_get_modes,
	.get_mode_details = adp1653_get_mode_details,
	.get_status = adp1653_get_status,
	.enable_flash_mode = adp1653_enable_flash_mode,
	.configure_flash_mode = adp1653_configure_flash_mode,
	.trigger_strobe = adp1653_trigger_strobe,
	.get_selftest_modes = adp1653_get_selftest_modes,
	.get_fault_registers = adp1653_get_fault_registers
};

static irqreturn_t adp1653_irq_hdlr(int irq_no,void *data)
{
	int err;
	struct adp1653_priv_data *priv_p= (struct adp1653_priv_data *)data;

	priv_p->fault = i2c_smbus_read_byte_data(priv_p->i2c_client,
		FAULT_STATUS_REG);
	DEBUG_LOG("Got Fault, status 0x%x\n",priv_p->fault);
	/*Writing 0 to OUTPUT_SEL_REG clears the interrtup
	*and FAULT_STATUS_REG register
	*/
	err = i2c_smbus_write_byte_data(priv_p->i2c_client,OUTPUT_SEL_REG,0);
	if(err)
		DEBUG_LOG("Unable to write OUTPUT_SEL_REG to clr intr, err %d\n",err);
	/*TBD: send even to user process*/
	return IRQ_HANDLED;
}
static int __devinit adp1653_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int err = 0;
	struct flash_chip *flash_chip_p=NULL;
	struct adp1653_priv_data *priv_p=NULL;
	struct adp1653_platform_data *pdata = client->dev.platform_data;

	DEBUG_LOG("> adp1653_probe\n");

	priv_p = kzalloc(sizeof(struct adp1653_priv_data),GFP_KERNEL);
	if(!priv_p){
		DEBUG_LOG("Kmalloc failed for priv data\n");
		err = ENOMEM;
		goto err_priv;
	}
	priv_p->i2c_client = client;
	flash_chip_p = kzalloc(sizeof(struct flash_chip),GFP_KERNEL);
	if(!flash_chip_p){
		DEBUG_LOG("Kmalloc failed for flash_chip_p");
		err = ENOMEM;
		goto err_flash_chip_alloc;
	}

	if (!pdata) {
		dev_err(&client->dev,
			"%s: No platform data supplied.\n", __func__);
		err = -EINVAL;
		goto err_pdata;
	}

	flash_chip_p->priv_data = priv_p;
	flash_chip_p->ops = &adp1653_ops;
	SET_FLASHCHIP_TYPE(flash_chip_p,FLASH_TYPE_HPLED);
	SET_FLASHCHIP_ID(flash_chip_p,ADP1653_ID);

	strncpy(flash_chip_p->name,"Adp1653",FLASH_NAME_SIZE);

	i2c_set_clientdata(client,priv_p);
	/*Request GPIO and Register IRQ if supported by platform and flash chip*/

	err = gpio_request(pdata->enable_gpio,"Camera LED flash Enable");
	if(err){
		DEBUG_LOG("Unable to get GPIO %d, for enable\n",pdata->enable_gpio);
		goto err_pdata;
	}

	err = gpio_direction_output(pdata->enable_gpio, 1);
	if(err){
		DEBUG_LOG("Unable to set GPIO %u in output mode, err %d\n",pdata->enable_gpio,err);
		gpio_free(pdata->enable_gpio);
		goto err_gpio_set;
	}
	gpio_set_value_cansleep(pdata->enable_gpio, 1);

	err = request_threaded_irq(gpio_to_irq(pdata->irq_no),NULL,adp1653_irq_hdlr,
		IRQF_ONESHOT|IRQF_TRIGGER_FALLING,
		"Adp1653 flash",priv_p);
	if(err){
		DEBUG_LOG("Unable to register flash IRQ handler, irq %d, err %d\n",
			pdata->irq_no,err);
		goto err_irq;
	}

	err = register_flash_chip(flash_position,flash_chip_p);
	if(err){
		DEBUG_LOG("Failed to register Adp1653 as flash for %s camera\n",
			(flash_position?"Primary":"Secondary"));
		goto err_register;
	}
	SET_FLASH_STATUS(priv_p->status,FLASH_STATUS_READY);
	DEBUG_LOG("< adp1653_probe ok\n");
	return err;
err_register:
	if(pdata->irq_no)
		free_irq(pdata->irq_no,NULL);
err_irq:
	gpio_set_value_cansleep(pdata->enable_gpio, 0);
err_gpio_set:
	if(pdata->enable_gpio)
		gpio_free(pdata->enable_gpio);
err_pdata:
	if(flash_chip_p)
		kfree(flash_chip_p);
err_flash_chip_alloc:
        if(priv_p)
                kfree(priv_p);
err_priv:
	DEBUG_LOG("< adp1653_probe (%d)\n", err);
	return err;
}

static int __devexit adp1653_remove(struct i2c_client *client)
{
	int err=0;
	/*Nothing here yet, implement it later.*/
	return err;
}
static const struct i2c_device_id adp1653_id[] = {
	{ "adp1653", 0},
	{}
};
static struct i2c_driver adp1653_i2c_driver = {
	.driver = {
		.name = "adp1653",
		.owner = THIS_MODULE,
	},
	.probe = adp1653_probe,
	.remove = __devexit_p(adp1653_remove),
	.id_table = adp1653_id,
};

int adp1653_init(void){
	int err = 0;
	struct i2c_adapter *adap_p;
	struct i2c_board_info info;

	/* Registration of I2C flash device is platform specific code
	 * Ideally it should be done from kernel (arch/arm/mach-XXX).
	 * Do it locally till the time it gets into platform code
	 * OR This portion (registration of device) and flash chip init
	 * Routine can be moved to Flash chip module init. */
	DEBUG_LOG("getting I2C adaptor %d\n",adapter_i2c2);
	adap_p = i2c_get_adapter(adapter_i2c2);
	if(!adap_p){
		DEBUG_LOG("Unable to get I2C adaptor\n");
		goto out;
	}
	memset(&info,0,sizeof( struct i2c_board_info));

	strcpy(&info.type[0],"adp1653");
	DEBUG_LOG("trying to register %s at position %d\n",
		info.type,
		flash_position);

	/* I2C framework expects least significant 7 bits as address, not complete
	* 8 bits with bit 0 (read/write bit)
	*/
	info.addr = 0x60 >> 1;

	err = i2c_add_driver(&adp1653_i2c_driver);
	if(err)
	{
		DEBUG_LOG("Failed to register i2c driver\n");
		goto out;
	}

	DEBUG_LOG("Initialized adp1653\n");
	if(!i2c_new_device(adap_p,&info)){
		DEBUG_LOG("Unable to add i2c dev: %s (err=%d)\n",info.type, err);
		goto out;
	}
out:
	return err;
}

/*
MODULE_DEPEND
*/
