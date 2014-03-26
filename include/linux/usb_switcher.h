#ifndef USB_SWITCHER_H
#define USB_SWITCHER_H

#include <linux/notifier.h>

#define	USB_SWITCH_CONNECTION_EVENT	(1<<30)
#define	USB_SWITCH_DISCONNECTION_EVENT	(1<<31)
#define USB_SWITCH_DRIVER_STARTED	(1<<29)

#define	EXTERNAL_DEVICE_UNKNOWN		(1<<0)	
#define	EXTERNAL_USB			(1<<1)
#define	EXTERNAL_USB_OTG 		(1<<2)
#define	EXTERNAL_DEDICATED_CHARGER 	(1<<3)
#define	EXTERNAL_USB_CHARGER 		(1<<4)	
#define	EXTERNAL_UART 			(1<<5)
#define	EXTERNAL_CAR_KIT		(1<<6)
#define	EXTERNAL_AV_CABLE		(1<<7)
#define	EXTERNAL_PHONE_POWERED_DEVICE	(1<<8)
#define	EXTERNAL_TTY			(1<<9)
#define EXTERNAL_AUDIO_1		(1<<10)
#define EXTERNAL_AUDIO_2		(1<<11)	
#define EXTERNAL_JIG_USB_ON		(1<<12)	
#define EXTERNAL_JIG_USB_OFF		(1<<13)	
#define EXTERNAL_JIG_UART_ON		(1<<14)	
#define EXTERNAL_JIG_UART_OFF		(1<<15)	
#define EXTERNAL_MISC			(1<<16)

#define EXTERNAL_JIG_USB_MASK		(3<<12)


unsigned long usb_switch_get_current_connection(void);
unsigned long usb_switch_get_previous_connection(void);
void usb_switch_unregister_notify(struct notifier_block *nb);
void usb_switch_register_notify(struct notifier_block *nb);
void usb_switch_enable(void);
void usb_switch_disable(void);

struct usb_switch
{
	const char * name ;
	unsigned char id ;
	unsigned char id_mask ;
	unsigned char control_register_default ;
	unsigned char control_register_inital_value ;
	unsigned short charger_detect_gpio    ;
	unsigned short connection_changed_interrupt_gpio    ;
	unsigned char valid_device_register_1_bits ;
	unsigned char valid_device_register_2_bits ;
	unsigned char valid_registers[0x16] ; 	
};

extern int jig_smd;

#endif //USB_SWITCHER_H



