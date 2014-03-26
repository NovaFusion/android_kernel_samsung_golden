#ifndef __CM3607_H__
#define __CM3607_H__

#include <linux/kernel.h>
#include <linux/types.h>

#define PROX_INT IRQ_EINT_GROUP(19,4)
//#define CM3607_DEBUG

#define error(fmt, arg...) printk("--------" fmt "\n", ##arg)

#ifdef CM3607_DEBUG
#define debug(fmt, arg...) printk("--------" fmt "\n", ##arg)
#else
#define debug(fmt,arg...)
#endif

#define GPIO_PS_EN					S5P64XX_GPJ3(5)	
#define GPIO_PS_EN_STATE			1
	
/*Driver data */
struct cm3607_prox_data {		
	int    irq;
	struct work_struct work_prox;  /* for proximity sensor */ 
	struct input_dev *prox_input_dev;
};


struct workqueue_struct *cm3607_wq;

static irqreturn_t cm3607_irq_handler( int, void* );
static int proximity_open(struct inode* , struct file* );
static int proximity_release(struct inode* , struct file* );
static int proximity_ioctl(struct inode* , struct file*, 
	                        unsigned int,unsigned long );
static int cm3607_prox_suspend( struct platform_device* , pm_message_t );
static int cm3607_prox_resume( struct platform_device* );
static int proximity_mode(int );
static void cm3607_prox_work_func(struct work_struct* );
int get_cm3607_proximity_value(void);
/*Function call for checking the hardware revision of the target*/
extern int apollo_get_remapped_hw_rev_pin(); 

#endif
