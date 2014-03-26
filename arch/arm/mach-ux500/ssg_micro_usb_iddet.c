/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/input/ab8505_micro_usb_iddet.h>

/* Cables with less than 400kohm iddet resistance */
static struct cust_rid_adcid cust_rid_adcid_1_8V_200k[] = {

	CUST_RID_ADCID(598, 617, 102000, USBSWITCH_PPD),
	CUST_RID_ADCID(668, 688, 121000, USBSWITCH_TTY_CONV),
	CUST_RID_ADCID(761, 781, 150000, USBSWITCH_UART),
	CUST_RID_ADCID(888, 911, 200000, USBSWITCH_CARKIT_TYPE1),
	CUST_RID_ADCID(997, 1020, 255000, USBSWITCH_USB_BOOT_OFF),
	CUST_RID_ADCID(1069, 1093, 301000, USBSWITCH_USB_BOOT_ON),
	CUST_RID_ADCID(1125, 1185, 365000, USBSWITCH_DESKTOP_DOCK),
	CUST_RID_ADCID(1227, 1250, 440000, USBSWITCH_CARKIT_TYPE2),
	CUST_RID_ADCID_END
};

enum types_of_btn {
	SEND_END,
	BTN_S01,
	BTN_S02,
	BTN_S03,
	BTN_S04,
	BTN_S05,
	BTN_S06,
	BTN_S07,
	BTN_S08,
	BTN_S09,
	BTN_S10,
	BTN_S11,
	BTN_S12,
};

static struct button_param_list btn_param_list[] = {
	BTN_PARAM(0x001, 0x07C, 200, SEND_END, "SendEnd"),
	BTN_PARAM(0x07D, 0x0FF, 260, BTN_S01, "S1"),
	BTN_PARAM(0x100, 0x133, 320, BTN_S02, "S2"),
	BTN_PARAM(0x134, 0x169, 401, BTN_S03, "S3"),
	BTN_PARAM(0x170, 0x1A1, 482, BTN_S04, "S4"),
	BTN_PARAM(0x1A2, 0x1DF, 603, BTN_S05, "S5"),
	BTN_PARAM(0x1E0, 0x233, 803, BTN_S06, "S6"),
	BTN_PARAM(0x234, 0x287, 100, BTN_S07, "S7"),
	BTN_PARAM(0x288, 0x2CB, 120, BTN_S08, "S8"),
	BTN_PARAM(0x2CC, 0x309, 144, BTN_S09, "S9"),
	BTN_PARAM(0x310, 0x344, 172, BTN_S10, "S10"),
	BTN_PARAM(0x345, 0x37B, 205, BTN_S11, "S11"),
	BTN_PARAM(0x37C, 0x3AD, 240, BTN_S12, "S12"),
	BTN_PARAM_END,
};

struct ab8505_iddet_platdata iddet_adc_val_list = {
	.adc_id_list = cust_rid_adcid_1_8V_200k,
	.btn_list = btn_param_list,
};
