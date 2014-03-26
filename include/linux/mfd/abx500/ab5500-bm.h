/*
 * Copyright ST-Ericsson 2011.
 *
 * Author: Arun Murthy <arun.murthy@stericsson.com>
 * Licensed under GPLv2.
 */

#ifndef _AB5500_BM_H
#define _AB5500_BM_H

#define AB5500_MCB		0x2F
/*
 * USB/ULPI register offsets
 * Bank : 0x5
 */
#define AB5500_USB_LINE_STATUS		0x80
#define AB5500_USB_PHY_STATUS		0x89
#define AB5500_CHGFSM_CHARGER_DETECT	0xBF
#define AB5500_CHGFSM_USB_BTEMP_CURR_LIM	0xAD
#define AB5500_USB_LINE_CTRL2		0x82
#define AB5500_USB_OTG_CTRL		0x87

/*
 * Charger / control register offfsets
 * Bank : 0x0B
 */
#define AB5500_CVBUSM			0x11
#define AB5500_LEDT			0x12
#define AB5500_VSRC			0x13
#define AB5500_ICSR			0x14
#define AB5500_OCSRV			0x15
#define AB5500_CVREC			0x16
#define AB5500_CREVS			0x17
#define AB5500_CCTRL			0x18
#define AB5500_TBDATA			0x19
#define AB5500_CPWM			0x1A
#define AB5500_DCIOCURRENT		0x1B
#define AB5500_USB_HS_CURR_LIM		0x1C
#define AB5500_WALL_HS_CURR_LIM		0x1D

/*
 * FG, Battcom and ACC registers offsets
 * Bank : 0x0C
 */
#define AB5500_FG_CH0			0x00
#define AB5500_FG_CH1			0x01
#define AB5500_FG_CH2			0x02
#define AB5500_FG_DIS_CH0		0x03
#define AB5500_FG_DIS_CH1		0x04
#define AB5500_FG_DIS_CH2		0x05
#define AB5500_FGDIS_COUNT0		0x06
#define AB5500_FGDIS_COUNT1		0x07
#define AB5500_FG_VAL_COUNT0		0x08
#define AB5500_FG_VAL_COUNT1		0x09
#define AB5500_FGDIR_READ0		0x0A
#define AB5500_FGDIR_READ1		0x0B
#define AB5500_FG_CONTROL_A		0x0C
#define AB5500_FG_CONTROL_B		0x0F
#define AB5500_FG_CONTROL_C		0x10
#define AB5500_FG_DIS			0x0D
#define AB5500_FG_EOC			0x0E
#define AB5500_FG_CB			0x0F
#define AB5500_FG_CC			0x10
#define AB5500_UIOR			0x1A
#define AB5500_UART			0x1B
#define AB5500_URI			0x1C
#define AB5500_UART_RQ			0x1D
#define AB5500_ACC_DETECT1		0x20
#define AB5500_ACC_DETECT2		0x21
#define AB5500_ACC_DETECTCTRL		0x23
#define AB5500_ACC_AVCTRL		0x24
#define AB5500_ACC_DETECT3_DEG_LITCH_TIME	0x30
#define AB5500_ACC_DETECT3_KEY_PRESS_TIME	0x31
#define AB5500_ACC_DETECT3_LONG_KEY_TIME	0x32
#define AB5500_ACC_DETECT3_TIME_READ_MS		0x33
#define AB5500_ACC_DETECT3_TIME_READ_LS		0x34
#define AB5500_ACC_DETECT3_CONTROL		0x35
#define AB5500_ACC_DETECT3_LEVEL		0x36
#define AB5500_ACC_DETECT3_TIMER_READ_CTL	0x37

/*
 * Interrupt register offsets
 * Bank : 0x0E
 */
#define AB5500_IT_SOURCE8		0x28
#define AB5500_IT_SOURCE9		0x29

/* BatCtrl Current Source Constants */
#define BAT_CTRL_7U_ENA			(0x01 << 0)
#define BAT_CTRL_15U_ENA		(0x01 << 1)
#define BAT_CTRL_30U_ENA		(0x01 << 2)
#define BAT_CTRL_60U_ENA		(0x01 << 3)
#define BAT_CTRL_120U_ENA		(0x01 << 4)
#define BAT_CTRL_CMP_ENA		0x04
#define FORCE_BAT_CTRL_CMP_HIGH		0x08
#define BAT_CTRL_PULL_UP_ENA		0x10

/* Battery type */
#define BATTERY_UNKNOWN			0

#ifdef CONFIG_AB5500_BM
struct ab5500_btemp *ab5500_btemp_get(void);
int ab5500_btemp_get_batctrl_temp(struct ab5500_btemp *btemp);
void ab5500_fg_reinit(void);
#else
static inline struct ab5500_btemp *ab5500_btemp_get(void)
{
	return 0;
}
static inline int ab5500_btemp_get_batctrl_temp(struct ab5500_btemp *btemp)
{
	return 0;
}
static inline void ab5500_fg_reinit(void) {}
#endif
#endif /* _AB5500_BM_H */
