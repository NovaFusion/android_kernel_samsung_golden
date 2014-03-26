/*
 * drivers/usb/otg/ab8500_usb.c
 *
 * USB transceiver driver for AB8500 chip
 *
 * Copyright (C) 2010 ST-Ericsson AB
 * Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/kernel_stat.h>
#include <linux/pm_qos_params.h>
#include <linux/wakelock.h>
#include <linux/usb/ab8500-otg.h>

static struct wake_lock ab8500_musb_wakelock;
/* For notification to usb switch driver */
struct blocking_notifier_head micro_usb_switch_notifier =
	BLOCKING_NOTIFIER_INIT(micro_usb_switch_notifier);

#define AB8500_MAIN_WD_CTRL_REG 0x01
#define AB8500_USB_LINE_STAT_REG 0x80
#define AB8500_USB_PHY_CTRL_REG 0x8A
#define AB8500_VBUS_CTRL_REG 0x82
#define AB8500_IT_SOURCE2_REG 0x01
#define AB8500_IT_SOURCE20_REG 0x13
#define AB8500_SRC_INT_USB_HOST 0x04
#define AB8500_SRC_INT_USB_DEVICE 0x80

#define AB8500_BIT_OTG_STAT_ID (1 << 0)
#define AB8500_BIT_PHY_CTRL_HOST_EN (1 << 0)
#define AB8500_BIT_PHY_CTRL_DEVICE_EN (1 << 1)
#define AB8500_BIT_WD_CTRL_ENABLE (1 << 0)
#define AB8500_BIT_WD_CTRL_KICK (1 << 1)
#define AB8500_BIT_VBUS_ENABLE (1 << 0)

#define AB8500_V1x_LINK_STAT_WAIT (HZ/10)
#define AB8500_WD_KICK_DELAY_US 100 /* usec */
#define AB8500_WD_V11_DISABLE_DELAY_US 100 /* usec */
#define AB8500_V20_31952_DISABLE_DELAY_US 100 /* usec */
#define AB8500_WD_V10_DISABLE_DELAY_MS 100 /* ms */

/* Registers in bank 0x11 */
#define AB8500_BANK12_ACCESS	0x00

/* Registers in bank 0x12 */
#define AB8500_USB_PHY_TUNE1	0x05
#define AB8500_USB_PHY_TUNE2	0x06
#define AB8500_USB_PHY_TUNE3	0x07

static struct pm_qos_request_list usb_pm_qos_latency;
static bool usb_pm_qos_is_latency_0;

#define USB_PROBE_DELAY 1000 /* 1 seconds */
#define USB_LIMIT (200) /* If we have more than 200 irqs per second */

#define PUBLIC_ID_BACKUPRAM1 (U8500_BACKUPRAM1_BASE + 0x0FC0)
#define MAX_USB_SERIAL_NUMBER_LEN 31
#define AB8505_USB_LINE_STAT_REG 0x94

#define AB8500_USB_CHARGER_DETECTION_ENABLE 0x1
#define AB8500_USB_CHARGER_DETECTION_DISABLE 0x0

#define AB8500_USB_LINE_CTRL2_REG      0x82
#define AB8500_USBCHARGDETENA		0x01

/* Usb line status register */
enum ab8500_usb_link_status {
	USB_LINK_NOT_CONFIGURED_8500 = 0,
	USB_LINK_STD_HOST_NC_8500,
	USB_LINK_STD_HOST_C_NS_8500,
	USB_LINK_STD_HOST_C_S_8500,
	USB_LINK_HOST_CHG_NM_8500,
	USB_LINK_HOST_CHG_HS_8500,
	USB_LINK_HOST_CHG_HS_CHIRP_8500,
	USB_LINK_DEDICATED_CHG_8500,
	USB_LINK_ACA_RID_A_8500,
	USB_LINK_ACA_RID_B_8500,
	USB_LINK_ACA_RID_C_NM_8500,
	USB_LINK_ACA_RID_C_HS_8500,
	USB_LINK_ACA_RID_C_HS_CHIRP_8500,
	USB_LINK_HM_IDGND_8500,
	USB_LINK_RESERVED_8500,
	USB_LINK_NOT_VALID_LINK_8500,
};

enum ab8505_usb_link_status {
	USB_LINK_NOT_CONFIGURED_8505 = 0,
	USB_LINK_STD_HOST_NC_8505,
	USB_LINK_STD_HOST_C_NS_8505,
	USB_LINK_STD_HOST_C_S_8505,
	USB_LINK_CDP_8505,
	USB_LINK_RESERVED0_8505,
	USB_LINK_RESERVED1_8505,
	USB_LINK_DEDICATED_CHG_8505,
	USB_LINK_ACA_RID_A_8505,
	USB_LINK_ACA_RID_B_8505,
	USB_LINK_ACA_RID_C_NM_8505,
	USB_LINK_RESERVED2_8505,
	USB_LINK_RESERVED3_8505,
	USB_LINK_HM_IDGND_8505,
	USB_LINK_CHARGERPORT_NOT_OK_8505,
	USB_LINK_CHARGER_DM_HIGH_8505,
	USB_LINK_PHYEN_NO_VBUS_NO_IDGND_8505,
	USB_LINK_STD_UPSTREAM_NO_IDGNG_NO_VBUS_8505,
	USB_LINK_STD_UPSTREAM_8505,
	USB_LINK_CHARGER_SE1_8505,
	USB_LINK_CARKIT_CHGR_1_8505,
	USB_LINK_CARKIT_CHGR_2_8505,
	USB_LINK_ACA_DOCK_CHGR_8505,
	USB_LINK_SAMSUNG_BOOT_CBL_PHY_EN_8505,
	USB_LINK_SAMSUNG_BOOT_CBL_PHY_DISB_8505,
	USB_LINK_SAMSUNG_UART_CBL_PHY_EN_8505,
	USB_LINK_SAMSUNG_UART_CBL_PHY_DISB_8505,
	USB_LINK_MOTOROLA_FACTORY_CBL_PHY_EN_8505,
};

enum ab8500_usb_mode {
	USB_IDLE = 0,
	USB_PERIPHERAL,
	USB_HOST,
	USB_DEDICATED_CHG
};

struct ab8500_usb {
	struct otg_transceiver otg;
	struct device *dev;
	struct ab8500 *ab8500;
	int irq_num_id_rise;
	int irq_num_id_fall;
	int irq_num_vbus_rise;
	int irq_num_vbus_fall;
	int irq_num_link_status;
	unsigned vbus_draw;
	struct delayed_work dwork;
	struct work_struct phy_dis_work;
	unsigned long link_status_wait;
	enum ab8500_usb_mode mode;
	struct clk *sysclk;
	struct regulator *v_ape;
	struct regulator *v_musb;
	struct regulator *v_ulpi;
	struct delayed_work work_usb_workaround;
	bool sysfs_flag;
	int previous_link_status_state;
	struct notifier_block usb_nb;
	bool enable_charging_detection;
};

static inline struct ab8500_usb *xceiv_to_ab(struct otg_transceiver *x)
{
	return container_of(x, struct ab8500_usb, otg);
}

static void ab8500_usb_wd_workaround(struct ab8500_usb *ab)
{
	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		AB8500_BIT_WD_CTRL_ENABLE);

	udelay(AB8500_WD_KICK_DELAY_US);

	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		(AB8500_BIT_WD_CTRL_ENABLE
		| AB8500_BIT_WD_CTRL_KICK));

	if (!is_ab8500_1p0_or_earlier(ab->ab8500))
		udelay(AB8500_WD_V11_DISABLE_DELAY_US);

	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		0);
}

static void ab8500_usb_load(struct work_struct *work)
{
	int cpu;
	unsigned int num_irqs = 0;
	static unsigned int old_num_irqs = UINT_MAX;
	struct delayed_work *work_usb_workaround = to_delayed_work(work);
	struct ab8500_usb *ab = container_of(work_usb_workaround,
				struct ab8500_usb, work_usb_workaround);

	for_each_online_cpu(cpu)
	num_irqs += kstat_irqs_cpu(IRQ_DB8500_USBOTG, cpu);

	if ((num_irqs > old_num_irqs) &&
		(num_irqs - old_num_irqs) > USB_LIMIT) {

		prcmu_qos_update_requirement(PRCMU_QOS_ARM_KHZ,
					     "usb", 1000000);
		if (!usb_pm_qos_is_latency_0) {

			pm_qos_add_request(&usb_pm_qos_latency,
						PM_QOS_CPU_DMA_LATENCY, 0);
			usb_pm_qos_is_latency_0 = true;
		}
	} else {

		if (usb_pm_qos_is_latency_0) {

				pm_qos_remove_request(&usb_pm_qos_latency);
				usb_pm_qos_is_latency_0 = false;
		}

		prcmu_qos_update_requirement(PRCMU_QOS_ARM_KHZ,
					     "usb", PRCMU_QOS_DEFAULT_VALUE);
	}
	old_num_irqs = num_irqs;

	schedule_delayed_work_on(0,
				&ab->work_usb_workaround,
				msecs_to_jiffies(USB_PROBE_DELAY));
}

static void ab8500_usb_regulator_ctrl(struct ab8500_usb *ab, bool sel_host,
					bool enable)
{
	int ret = 0, volt = 0;

	if (enable) {
		regulator_enable(ab->v_ape);
		if (!is_ab8500_2p0_or_earlier(ab->ab8500)) {
			ret = regulator_set_voltage(ab->v_ulpi,
						1300000, 1350000);
			if (ret < 0)
				dev_err(ab->dev, "Failed to set the Vintcore"
						" to 1.3V, ret=%d\n", ret);
			ret = regulator_set_optimum_mode(ab->v_ulpi,
								28000);
			if (ret < 0)
				dev_err(ab->dev, "Failed to set optimum mode"
						" (ret=%d)\n", ret);

		}
		regulator_enable(ab->v_ulpi);
		if (!is_ab8500_2p0_or_earlier(ab->ab8500)) {
			volt = regulator_get_voltage(ab->v_ulpi);
			if ((volt != 1300000) && (volt != 1350000))
					dev_err(ab->dev, "Vintcore is not"
							" set to 1.3V"
							" volt=%d\n", volt);
		}
		regulator_enable(ab->v_musb);

	} else {
		regulator_disable(ab->v_musb);
		regulator_disable(ab->v_ulpi);
		regulator_disable(ab->v_ape);
	}
}


static void ab8500_usb_phy_enable(struct ab8500_usb *ab, bool sel_host)
{
	u8 bit;
	bit = sel_host ? AB8500_BIT_PHY_CTRL_HOST_EN :
			AB8500_BIT_PHY_CTRL_DEVICE_EN;

	wake_lock(&ab8500_musb_wakelock);

	clk_enable(ab->sysclk);

	ab8500_usb_regulator_ctrl(ab, sel_host, true);

	prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
				     (char *)dev_name(ab->dev),
				     PRCMU_QOS_APE_OPP_MAX);

	schedule_delayed_work_on(0,
					&ab->work_usb_workaround,
					msecs_to_jiffies(USB_PROBE_DELAY));

	abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				bit,
				bit);

}

static void ab8500_usb_wd_linkstatus(struct ab8500_usb *ab,u8 bit)
{
	/* Workaround for v2.0 bug # 31952 */
	if (is_ab8500_2p0(ab->ab8500)) {
		abx500_mask_and_set_register_interruptible(ab->dev,
					AB8500_USB,
					AB8500_USB_PHY_CTRL_REG,
					bit,
					bit);
		udelay(AB8500_V20_31952_DISABLE_DELAY_US);
	}
}

static void ab8500_usb_phy_disable(struct ab8500_usb *ab, bool sel_host)
{
	u8 bit;
	bit = sel_host ? AB8500_BIT_PHY_CTRL_HOST_EN :
			AB8500_BIT_PHY_CTRL_DEVICE_EN;

	ab8500_usb_wd_linkstatus(ab,bit);

	abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				bit,
				0);

	/* Needed to disable the phy.*/
	ab8500_usb_wd_workaround(ab);

	clk_disable(ab->sysclk);

	ab8500_usb_regulator_ctrl(ab, sel_host, false);

	prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
				     (char *)dev_name(ab->dev),
				     PRCMU_QOS_DEFAULT_VALUE);

	if (!sel_host) {

		cancel_delayed_work_sync(&ab->work_usb_workaround);
		prcmu_qos_update_requirement(PRCMU_QOS_ARM_KHZ,
			"usb", PRCMU_QOS_DEFAULT_VALUE);
	}

	wake_unlock(&ab8500_musb_wakelock);
}

#define ab8500_usb_host_phy_en(ab)	ab8500_usb_phy_enable(ab, true)
#define ab8500_usb_host_phy_dis(ab)	ab8500_usb_phy_disable(ab, true)
#define ab8500_usb_peri_phy_en(ab)	ab8500_usb_phy_enable(ab, false)
#define ab8500_usb_peri_phy_dis(ab)	ab8500_usb_phy_disable(ab, false)


static int ab8505_usb_link_status_update(struct ab8500_usb *ab,
				enum ab8505_usb_link_status lsts) {
	enum usb_xceiv_events event = 0;
	u8 val = 0;

	dev_info(ab->dev, "ab8505_usb_link_status_update %d\n", lsts);

	/*
	 * Spurious link_status interrupts are seen at the time of
	 * disconnection of a device in RIDA state
	 */
	if (ab->previous_link_status_state == USB_LINK_ACA_RID_A_8505 &&
			(lsts == USB_LINK_STD_HOST_NC_8505))
		return 0;

	if (ab->enable_charging_detection) {
		/*
		 * Disable Charging Detection
		 * Write @0x0582 =0x00
		 */
		abx500_mask_and_set_register_interruptible(ab->dev,
			AB8500_USB,
			AB8500_USB_LINE_CTRL2_REG,
			AB8500_USBCHARGDETENA,
			AB8500_USB_CHARGER_DETECTION_DISABLE);

		/* Read @0x0580 link_status */
		(void)abx500_get_register_interruptible(ab->dev,
			AB8500_USB, AB8505_USB_LINE_STAT_REG, &val);

		val = (val >> 3) & 0x1F;
		lsts = (enum ab8500_usb_link_status) val;
		ab->enable_charging_detection = false;
	}

	ab->previous_link_status_state = lsts;
	switch (lsts) {
	case USB_LINK_ACA_RID_B_8505:
		event = USB_EVENT_RIDB;
	case USB_LINK_NOT_CONFIGURED_8505:
	case USB_LINK_RESERVED0_8505:
	case USB_LINK_RESERVED1_8505:
	case USB_LINK_RESERVED2_8505:
	case USB_LINK_RESERVED3_8505:
		if (ab->mode == USB_PERIPHERAL)
			atomic_notifier_call_chain(&ab->otg.notifier,
						USB_EVENT_CLEAN,
						&ab->vbus_draw);
		ab->mode = USB_IDLE;
		ab->otg.default_a = false;
		ab->vbus_draw = 0;
		if (event != USB_EVENT_RIDB)
			event = USB_EVENT_NONE;
		break;

	case USB_LINK_ACA_RID_C_NM_8505:
		event = USB_EVENT_RIDC;
	case USB_LINK_STD_HOST_NC_8505:
	case USB_LINK_STD_HOST_C_NS_8505:
	case USB_LINK_STD_HOST_C_S_8505:
	case USB_LINK_CDP_8505:
		if (ab->mode == USB_HOST) {
			ab->mode = USB_PERIPHERAL;
			ab8500_usb_host_phy_dis(ab);
			ab8500_usb_peri_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
							USB_EVENT_CLEAN,
							&ab->vbus_draw);
		}
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_PERIPHERAL;
			ab8500_usb_peri_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		if (event != USB_EVENT_RIDC)
			event = USB_EVENT_VBUS;
		break;
	case USB_LINK_ACA_RID_A_8505:
		event = USB_EVENT_RIDA;
	case USB_LINK_HM_IDGND_8505:
		if (ab->mode == USB_PERIPHERAL) {
			ab->mode = USB_HOST;
			ab8500_usb_peri_phy_dis(ab);
			ab8500_usb_host_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_HOST;
			ab8500_usb_host_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		ab->otg.default_a = true;
		if (event != USB_EVENT_RIDA)
			event = USB_EVENT_ID;
		atomic_notifier_call_chain(&ab->otg.notifier,
					event,
					&ab->vbus_draw);
		break;

	case USB_LINK_DEDICATED_CHG_8505:
		ab->mode = USB_DEDICATED_CHG;
		event = USB_EVENT_CHARGER;
		atomic_notifier_call_chain(&ab->otg.notifier,
				event,
				&ab->vbus_draw);
		break;

	default:
		break;
	}
	return 0;
}

static int ab8500_usb_link_status_update(struct ab8500_usb *ab,
				enum ab8500_usb_link_status lsts) {
	enum usb_xceiv_events event = 0;
	u8 val = 0;

	dev_info(ab->dev, "ab8500_usb_link_status_update %d\n", lsts);

	/*
	 * Spurious link_status interrupts are seen in case of a
	 * disconnection of a device in IDGND and RIDA stage
	 */
	if (ab->previous_link_status_state == USB_LINK_HM_IDGND_8500 &&
			(lsts == USB_LINK_STD_HOST_C_NS_8500 ||
			 lsts == USB_LINK_STD_HOST_NC_8500))
		return 0;
	if (ab->previous_link_status_state == USB_LINK_ACA_RID_A_8500 &&
			 lsts == USB_LINK_STD_HOST_NC_8500)
		return 0;

	if (ab->enable_charging_detection) {
		/*
		 * Disable Charging Detection
		 * Write @0x0582 =0x00
		 */
		abx500_mask_and_set_register_interruptible(ab->dev,
			AB8500_USB,
			AB8500_USB_LINE_CTRL2_REG,
			AB8500_USBCHARGDETENA,
			AB8500_USB_CHARGER_DETECTION_DISABLE);

		/* Read @0x0580 link_status */
		(void)abx500_get_register_interruptible(ab->dev,
				AB8500_USB, AB8500_USB_LINE_STAT_REG, &val);

		val = (val >> 3) & 0x0F;
		lsts = (enum ab8500_usb_link_status) val;
		ab->enable_charging_detection = false;
	}

	ab->previous_link_status_state = lsts;
	switch (lsts) {
	case USB_LINK_ACA_RID_B_8500:
		event = USB_EVENT_RIDB;
	case USB_LINK_NOT_CONFIGURED_8500:
	case USB_LINK_NOT_VALID_LINK_8500:
		if (ab->mode == USB_PERIPHERAL)
			atomic_notifier_call_chain(&ab->otg.notifier,
					   USB_EVENT_CLEAN,
					   &ab->vbus_draw);
		ab->mode = USB_IDLE;
		ab->otg.default_a = false;
		ab->vbus_draw = 0;
		if (event != USB_EVENT_RIDB)
			event = USB_EVENT_NONE;
		break;
	case USB_LINK_ACA_RID_C_NM_8500:
	case USB_LINK_ACA_RID_C_HS_8500:
	case USB_LINK_ACA_RID_C_HS_CHIRP_8500:
		event = USB_EVENT_RIDC;
	case USB_LINK_STD_HOST_NC_8500:
	case USB_LINK_STD_HOST_C_NS_8500:
	case USB_LINK_STD_HOST_C_S_8500:
	case USB_LINK_HOST_CHG_NM_8500:
	case USB_LINK_HOST_CHG_HS_8500:
	case USB_LINK_HOST_CHG_HS_CHIRP_8500:
		if (ab->mode == USB_HOST) {
			ab->mode = USB_PERIPHERAL;
			ab8500_usb_host_phy_dis(ab);
			ab8500_usb_peri_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_PERIPHERAL;
			ab8500_usb_peri_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		if (event != USB_EVENT_RIDC)
			event = USB_EVENT_VBUS;
		break;
	case USB_LINK_ACA_RID_A_8500:
		event = USB_EVENT_RIDA;
	case USB_LINK_HM_IDGND_8500:
		if (ab->mode == USB_PERIPHERAL) {
			ab->mode = USB_HOST;
			ab8500_usb_peri_phy_dis(ab);
			ab8500_usb_host_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_HOST;
			ab8500_usb_host_phy_en(ab);
			atomic_notifier_call_chain(&ab->otg.notifier,
						   USB_EVENT_PREPARE,
						   &ab->vbus_draw);
		}
		ab->otg.default_a = true;
		if (event != USB_EVENT_RIDA)
			event = USB_EVENT_ID;
		atomic_notifier_call_chain(&ab->otg.notifier,
				event,
				&ab->vbus_draw);
		break;

	case USB_LINK_DEDICATED_CHG_8500:
		ab->mode = USB_DEDICATED_CHG;
		event = USB_EVENT_CHARGER;
		atomic_notifier_call_chain(&ab->otg.notifier,
					event,
					&ab->vbus_draw);
		break;
	case USB_LINK_RESERVED_8500:
		break;
	}
	return 0;
}

static int abx500_usb_link_status_update(struct ab8500_usb *ab)
{
	u8 reg;
	int ret = 0;

	if (!(ab->sysfs_flag)) {
		if (is_ab8500(ab->ab8500)) {
			enum ab8500_usb_link_status lsts;

			abx500_get_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_LINE_STAT_REG,
				&reg);
			lsts = (reg >> 3) & 0x0F;
			ret = ab8500_usb_link_status_update(ab, lsts);
		}
		if (is_ab8505(ab->ab8500)) {
			enum ab8505_usb_link_status lsts;

			abx500_get_register_interruptible(ab->dev,
			AB8500_USB,
			AB8505_USB_LINE_STAT_REG,
			&reg);
			lsts = (reg >> 3) & 0x1F;
			ret = ab8505_usb_link_status_update(ab, lsts);
		}
	}
	return ret;
}


static void ab8500_usb_delayed_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ab8500_usb *ab = container_of(dwork, struct ab8500_usb, dwork);
	abx500_usb_link_status_update(ab);
}

static irqreturn_t ab8500_usb_disconnect_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;
	enum usb_xceiv_events event = USB_EVENT_NONE;

	/* Link status will not be updated till phy is disabled. */
	if (ab->mode == USB_HOST) {
		ab->otg.default_a = false;
		ab->vbus_draw = 0;
		atomic_notifier_call_chain(&ab->otg.notifier,
					event, &ab->vbus_draw);
		ab8500_usb_host_phy_dis(ab);
		ab->mode = USB_IDLE;
	}
	if (ab->mode == USB_PERIPHERAL) {
		atomic_notifier_call_chain(&ab->otg.notifier,
				event, &ab->vbus_draw);
		ab8500_usb_peri_phy_dis(ab);
	}
	if (is_ab8500_2p0(ab->ab8500)) {
		if (ab->mode == USB_DEDICATED_CHG) {
			ab8500_usb_wd_linkstatus(ab, AB8500_BIT_PHY_CTRL_DEVICE_EN);
			abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				AB8500_BIT_PHY_CTRL_DEVICE_EN,
				0);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t ab8500_usb_v20_link_status_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;

	abx500_usb_link_status_update(ab);

	return IRQ_HANDLED;
}

static void ab8500_usb_phy_disable_work(struct work_struct *work)
{
	struct ab8500_usb *ab = container_of(work, struct ab8500_usb,
						phy_dis_work);

	if (!ab->otg.host)
		ab8500_usb_host_phy_dis(ab);

	if (!ab->otg.gadget)
		ab8500_usb_peri_phy_dis(ab);

}

static unsigned ab8500_eyediagram_workaroud(struct ab8500_usb *ab, unsigned mA)
{
	/* AB V2 has eye diagram issues when drawing more
	 * than 100mA from VBUS.So setting charging current
	 * to 100mA in case of standard host
	 */
	if (is_ab8500_2p0_or_earlier(ab->ab8500))
		if (mA > 100)
			mA = 100;

	return mA;
}

#ifdef CONFIG_USB_OTG_20
static int ab8500_usb_start_srp(struct otg_transceiver *otg, unsigned mA)
{
	struct ab8500_usb *ab;
	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);
	ab8500_usb_peri_phy_en(ab);
	atomic_notifier_call_chain(&ab->otg.notifier,
				   USB_EVENT_PREPARE,
				   &ab->vbus_draw);

	return 0;
}
#endif

static int ab8500_usb_set_power(struct otg_transceiver *otg, unsigned mA)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	mA = ab8500_eyediagram_workaroud(ab, mA);

	ab->vbus_draw = mA;

	atomic_notifier_call_chain(&ab->otg.notifier,
				USB_EVENT_VBUS, &ab->vbus_draw);
	return 0;
}

static int ab8500_usb_set_suspend(struct otg_transceiver *x, int suspend)
{
	/* TODO */
	return 0;
}

static int ab8500_usb_set_peripheral(struct otg_transceiver *otg,
		struct usb_gadget *gadget)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	ab->otg.gadget = gadget;
	/* Some drivers call this function in atomic context.
	 * Do not update ab8500 registers directly till this
	 * is fixed.
	 */
	if (!gadget)
		schedule_work(&ab->phy_dis_work);

	return 0;
}

static int ab8500_usb_set_host(struct otg_transceiver *otg,
					struct usb_bus *host)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	ab->otg.host = host;

	/* Some drivers call this function in atomic context.
	 * Do not update ab8500 registers directly till this
	 * is fixed.
	 */
	if (!host)
		schedule_work(&ab->phy_dis_work);

	return 0;
}
/**
 * ab8500_usb_boot_detect : detect the USB cable during boot time.
 * @device: value for device.
 *
 * This function is used to detect the USB cable during boot time.
 */
static int ab8500_usb_boot_detect(struct ab8500_usb *ab)
{
	/* Disabling PHY before selective enable or disable */
	abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				AB8500_BIT_PHY_CTRL_DEVICE_EN,
				AB8500_BIT_PHY_CTRL_DEVICE_EN);

	udelay(100);

	abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				AB8500_BIT_PHY_CTRL_DEVICE_EN,
				0);

	abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				AB8500_BIT_PHY_CTRL_HOST_EN,
				AB8500_BIT_PHY_CTRL_HOST_EN);

	udelay(100);

	abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				AB8500_BIT_PHY_CTRL_HOST_EN,
				0);

	return 0;
}

static int ab8500_usb_handle_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	int ret = 0;

	struct ab8500_usb *ab =
		container_of(nb, struct ab8500_usb, usb_nb);

	switch(event) {
		case USB_PHY_ENABLE:
			ret = abx500_mask_and_set_register_interruptible(
					ab->dev,
					AB8500_USB,
					AB8500_USB_PHY_CTRL_REG,
					AB8500_BIT_PHY_CTRL_DEVICE_EN,
					AB8500_BIT_PHY_CTRL_DEVICE_EN);
			if (ret < 0)
				dev_err(ab->dev, "device enable failed\n");
			break;
		case USB_PHY_DISABLE:
			ret = abx500_mask_and_set_register_interruptible(ab->dev,
					AB8500_USB,
					AB8500_USB_PHY_CTRL_REG,
					AB8500_BIT_PHY_CTRL_DEVICE_EN |
					AB8500_BIT_PHY_CTRL_HOST_EN,
					0);
			if (ret < 0)
				dev_err(ab->dev, "host & device disable"
						"failed\n");
			break;
		case USB_PHY_RESET:
			ab8500_usb_boot_detect(ab);
			break;
		default:
			dev_info(ab->dev, "Wrong event\n");
			break;
	}

	return ret;
}

static void ab8500_usb_regulator_put(struct ab8500_usb *ab)
{

	if (ab->v_ape)
		regulator_put(ab->v_ape);

	if (ab->v_ulpi)
		regulator_put(ab->v_ulpi);

	if (ab->v_musb)
		regulator_put(ab->v_musb);
}

static int ab8500_usb_regulator_get(struct ab8500_usb *ab)
{
	int err;

	ab->v_ape = regulator_get(ab->dev, "v-ape");
	if (IS_ERR(ab->v_ape)) {
		dev_err(ab->dev, "Could not get v-ape supply\n");
		err = PTR_ERR(ab->v_ape);
		return err;
	}

	ab->v_ulpi = regulator_get(ab->dev, "vddulpivio18");
	if (IS_ERR(ab->v_ulpi)) {
		dev_err(ab->dev, "Could not get vddulpivio18 supply\n");
		err = PTR_ERR(ab->v_ulpi);
		return err;
	}

	ab->v_musb = regulator_get(ab->dev, "musb_1v8");
	if (IS_ERR(ab->v_musb)) {
		dev_err(ab->dev, "Could not get musb_1v8 supply\n");
		err = PTR_ERR(ab->v_musb);
		return err;
	}

	return 0;
}

static void ab8500_usb_irq_free(struct ab8500_usb *ab)
{
	if (ab->irq_num_id_rise)
		free_irq(ab->irq_num_id_rise, ab);

	if (ab->irq_num_id_fall)
		free_irq(ab->irq_num_id_fall, ab);

	if (ab->irq_num_vbus_rise)
		free_irq(ab->irq_num_vbus_rise, ab);

	if (ab->irq_num_vbus_fall)
		free_irq(ab->irq_num_vbus_fall, ab);

	if (ab->irq_num_link_status)
		free_irq(ab->irq_num_link_status, ab);
}

static int ab8500_usb_irq_setup(struct platform_device *pdev,
				struct ab8500_usb *ab)
{
	int err;
	int irq;

	if (!is_ab8500_1p0_or_earlier(ab->ab8500)) {
		irq = platform_get_irq_byname(pdev, "USB_LINK_STATUS");
		if (irq < 0) {
			err = irq;
			dev_err(&pdev->dev, "Link status irq not found\n");
			goto irq_fail;
		}

		err = request_threaded_irq(irq, NULL,
			ab8500_usb_v20_link_status_irq,
			IRQF_NO_SUSPEND | IRQF_SHARED,
			"usb-link-status", ab);
		if (err < 0) {
			dev_err(ab->dev,
				"request_irq failed for link status irq\n");
			return err;
		}
		ab->irq_num_link_status = irq;
	}

	irq = platform_get_irq_byname(pdev, "ID_WAKEUP_F");
	if (irq < 0) {
		err = irq;
		dev_err(&pdev->dev, "ID fall irq not found\n");
		return ab->irq_num_id_fall;
	}
	err = request_threaded_irq(irq, NULL,
		ab8500_usb_disconnect_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-id-fall", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for ID fall irq\n");
		goto irq_fail;
	}
	ab->irq_num_id_fall = irq;

	irq = platform_get_irq_byname(pdev, "VBUS_DET_F");
	if (irq < 0) {
		err = irq;
		dev_err(&pdev->dev, "VBUS fall irq not found\n");
		goto irq_fail;
	}
	err = request_threaded_irq(irq, NULL,
		ab8500_usb_disconnect_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-vbus-fall", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for Vbus fall irq\n");
		goto irq_fail;
	}
	ab->irq_num_vbus_fall = irq;

	return 0;

irq_fail:
	ab8500_usb_irq_free(ab);
	return err;
}

/* Sys interfaces */
static ssize_t
serial_number_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u32 bufer[5];
	void __iomem *backup_ram = NULL;

	backup_ram = ioremap(PUBLIC_ID_BACKUPRAM1, 0x14);

	if (backup_ram) {
		bufer[0] = readl(backup_ram);
		bufer[1] = readl(backup_ram + 4);
		bufer[2] = readl(backup_ram + 8);
		bufer[3] = readl(backup_ram + 0x0c);
		bufer[4] = readl(backup_ram + 0x10);

		snprintf(buf, MAX_USB_SERIAL_NUMBER_LEN+1,
				"%.8X%.8X%.8X%.8X%.8X",
			bufer[0], bufer[1], bufer[2], bufer[3], bufer[4]);

		iounmap(backup_ram);
	} else
			dev_err(dev, "$$\n");

	return strlen(buf);
}

static DEVICE_ATTR(serial_number, 0644, serial_number_show, NULL);

static ssize_t
boot_time_device_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ab8500_usb *ab = dev_get_drvdata(dev);
	u8 val = ab->sysfs_flag;

	snprintf(buf, 2, "%d", val);

	return strlen(buf);
}

static ssize_t
boot_time_device_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t n)
{
	struct ab8500_usb *ab = dev_get_drvdata(dev);

	ab->sysfs_flag = false;

	abx500_usb_link_status_update(ab);

	return n;
}
static DEVICE_ATTR(boot_time_device, 0644,
			boot_time_device_show, boot_time_device_store);


static struct attribute *ab8500_usb_attributes[] = {
	&dev_attr_serial_number.attr,
	&dev_attr_boot_time_device.attr,
	NULL
};
static const struct attribute_group ab8500_attr_group = {
	.attrs = ab8500_usb_attributes,
};

static int ab8500_create_sysfsentries(struct ab8500_usb *ab)
{
	int err;

	err = sysfs_create_group(&ab->dev->kobj, &ab8500_attr_group);
	if (err)
		sysfs_remove_group(&ab->dev->kobj, &ab8500_attr_group);

	return err;
}

static int __devinit ab8500_usb_probe(struct platform_device *pdev)
{
	struct ab8500_usb	*ab;
	struct ab8500 *ab8500;
	int err;
	int rev;
	int ret = -1;

	ab8500 = dev_get_drvdata(pdev->dev.parent);
	rev = abx500_get_chip_id(&pdev->dev);

	if (is_ab8500_1p1_or_earlier(ab8500)) {
		dev_err(&pdev->dev, "Unsupported AB8500 chip rev=%d\n", rev);
		return -ENODEV;
	}

	ab = kzalloc(sizeof *ab, GFP_KERNEL);
	if (!ab)
		return -ENOMEM;

	ab->dev			= &pdev->dev;
	ab->ab8500		= ab8500;
	ab->otg.dev		= ab->dev;
	ab->otg.label		= "ab8500";
	ab->otg.state		= OTG_STATE_B_IDLE;
	ab->otg.set_host	= ab8500_usb_set_host;
	ab->otg.set_peripheral	= ab8500_usb_set_peripheral;
	ab->otg.set_suspend	= ab8500_usb_set_suspend;
	ab->otg.set_power	= ab8500_usb_set_power;
#ifdef CONFIG_USB_OTG_20
	ab->otg.start_srp	= ab8500_usb_start_srp;
#endif
	ab->sysfs_flag = true;

	platform_set_drvdata(pdev, ab);
	dev_set_drvdata(ab->dev, ab);
	ATOMIC_INIT_NOTIFIER_HEAD(&ab->otg.notifier);

	/* v1: Wait for link status to become stable.
	 * all: Updates form set_host and set_peripheral as they are atomic.
	 */
	INIT_DELAYED_WORK(&ab->dwork, ab8500_usb_delayed_work);

	/* all: Disable phy when called from set_host and set_peripheral */
	INIT_WORK(&ab->phy_dis_work, ab8500_usb_phy_disable_work);

	INIT_DELAYED_WORK_DEFERRABLE(&ab->work_usb_workaround,
							ab8500_usb_load);
	err = ab8500_usb_regulator_get(ab);
	if (err)
		goto fail0;

	ab->sysclk = clk_get(ab->dev, "sysclk");
	if (IS_ERR(ab->sysclk)) {
		err = PTR_ERR(ab->sysclk);
		goto fail1;
	}

	err = ab8500_usb_irq_setup(pdev, ab);
	if (err < 0)
		goto fail2;

	err = otg_set_transceiver(&ab->otg);
	if (err) {
		dev_err(&pdev->dev, "Can't register transceiver\n");
		goto fail3;
	}

	/* Write Phy tuning values */
	if (!is_ab8500_2p0_or_earlier(ab->ab8500)) {
		/* Enable the PBT/Bank 0x12 access */
		ret = abx500_set_register_interruptible(ab->dev,
							AB8500_DEVELOPMENT,
							AB8500_BANK12_ACCESS,
							0x01);
		if (ret < 0)
			printk(KERN_ERR "Failed to enable bank12"
						" access ret=%d\n", ret);

		if (is_ab8505(ab->ab8500)) {
			/* Apply new Phy tuning values
			 * for devices that use ab8505's internal micro USB switch */
			ret = abx500_set_register_interruptible(ab->dev,
								AB8500_DEBUG,
								AB8500_USB_PHY_TUNE1,
								0xD9);
			if (ret < 0)
				printk(KERN_ERR "Failed to set PHY_TUNE1"
							" register ret=%d\n", ret);

				ret = abx500_set_register_interruptible(ab->dev,
								AB8500_DEBUG,
								AB8500_USB_PHY_TUNE2,
								0x00);
			if (ret < 0)
				printk(KERN_ERR "Failed to set PHY_TUNE2"
							" register ret=%d\n", ret);

				ret = abx500_set_register_interruptible(ab->dev,
								AB8500_DEBUG,
								AB8500_USB_PHY_TUNE3,
								0xFC);

			if (ret < 0)
				printk(KERN_ERR "Failed to set PHY_TUNE3"
							" regester ret=%d\n", ret);
		} else {
			ret = abx500_set_register_interruptible(ab->dev,
								AB8500_DEBUG,
								AB8500_USB_PHY_TUNE1,
								0xD8);
			if (ret < 0)
				printk(KERN_ERR "Failed to set PHY_TUNE1"
							" register ret=%d\n", ret);

				ret = abx500_set_register_interruptible(ab->dev,
								AB8500_DEBUG,
								AB8500_USB_PHY_TUNE2,
								0x00);
			if (ret < 0)
				printk(KERN_ERR "Failed to set PHY_TUNE2"
							" register ret=%d\n", ret);

				ret = abx500_set_register_interruptible(ab->dev,
								AB8500_DEBUG,
								AB8500_USB_PHY_TUNE3,
								0xFC);

			if (ret < 0)
				printk(KERN_ERR "Failed to set PHY_TUNE3"
							" regester ret=%d\n", ret);
		}

		/* Switch to normal mode/disable Bank 0x12 access */
		ret = abx500_set_register_interruptible(ab->dev,
						AB8500_DEVELOPMENT,
						AB8500_BANK12_ACCESS,
						0x00);

		if (ret < 0)
			printk(KERN_ERR "Failed to switch bank12"
						" access ret=%d\n", ret);
	}
	/* Needed to enable ID detection. */
	ab8500_usb_wd_workaround(ab);

	prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
			(char *)dev_name(ab->dev), PRCMU_QOS_DEFAULT_VALUE);
	dev_info(&pdev->dev, "revision 0x%2x driver initialized\n", rev);

	prcmu_qos_add_requirement(PRCMU_QOS_ARM_KHZ, "usb",
				  PRCMU_QOS_DEFAULT_VALUE);
	wake_lock_init(&ab8500_musb_wakelock, WAKE_LOCK_SUSPEND, "ab8500-usb");

	if (is_ab8500(ab->ab8500)) {
		err = ab8500_usb_boot_detect(ab);
		if (err < 0)
			goto fail3;
	} else {
		ab->usb_nb.notifier_call = ab8500_usb_handle_notifier;
		err = blocking_notifier_chain_register(&micro_usb_switch_notifier,
				&ab->usb_nb);
		if (err < 0)
			goto fail3;
	}

	err = ab8500_create_sysfsentries(ab);
	if (err)
		goto fail3;

	/* Start Charging detection */
	abx500_mask_and_set_register_interruptible(ab->dev,
		AB8500_USB,
		AB8500_USB_LINE_CTRL2_REG,
		AB8500_USBCHARGDETENA,
		AB8500_USB_CHARGER_DETECTION_ENABLE);

	ab->enable_charging_detection = true;

	return 0;
fail3:
	ab8500_usb_irq_free(ab);
fail2:
	clk_put(ab->sysclk);
fail1:
	ab8500_usb_regulator_put(ab);
fail0:
	kfree(ab);
	return err;
}

static int __devexit ab8500_usb_remove(struct platform_device *pdev)
{
	struct ab8500_usb *ab = platform_get_drvdata(pdev);

	ab8500_usb_irq_free(ab);

	cancel_delayed_work_sync(&ab->dwork);

	cancel_work_sync(&ab->phy_dis_work);

	otg_set_transceiver(NULL);

	if (ab->mode == USB_HOST)
		ab8500_usb_host_phy_dis(ab);
	else if (ab->mode == USB_PERIPHERAL)
		ab8500_usb_peri_phy_dis(ab);

	clk_put(ab->sysclk);

	ab8500_usb_regulator_put(ab);

	platform_set_drvdata(pdev, NULL);

	if (is_ab8505(ab->ab8500))
		blocking_notifier_chain_unregister(&micro_usb_switch_notifier,
				&ab->usb_nb);

	kfree(ab);

	return 0;
}

static struct platform_driver ab8500_usb_driver = {
	.probe		= ab8500_usb_probe,
	.remove		= __devexit_p(ab8500_usb_remove),
	.driver		= {
		.name	= "ab8500-usb",
		.owner	= THIS_MODULE,
	},
};

static int __init ab8500_usb_init(void)
{
	return platform_driver_register(&ab8500_usb_driver);
}
subsys_initcall(ab8500_usb_init);

static void __exit ab8500_usb_exit(void)
{
	platform_driver_unregister(&ab8500_usb_driver);
}
module_exit(ab8500_usb_exit);

MODULE_ALIAS("platform:ab8500_usb");
MODULE_AUTHOR("ST-Ericsson AB");
MODULE_DESCRIPTION("AB8500 usb transceiver driver");
MODULE_LICENSE("GPL");
