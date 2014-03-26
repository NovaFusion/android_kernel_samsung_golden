/*
 * Bluetooth Broadcom GPIO and Low Power Mode control
 *
 *  Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *  Copyright (C) 2011 Google, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/rfkill.h>
#include <linux/wakelock.h>

#include <asm/mach-types.h>
#include <mach/board-sec-u8500.h>
#include <mach/gpio.h>

#include "board-bluetooth-bcm4334.h"

#define BT_LPM_ENABLE

static struct rfkill *bt_rfkill;

struct bcm_bt_lpm {
	int wake;
	int host_wake;

	struct hrtimer enter_lpm_timer;
	ktime_t enter_lpm_delay;

	struct uart_port *uport;

	struct wake_lock wake_lock;
	char wake_lock_name[100];
} bt_lpm;

static int bcm4334_bt_rfkill_set_power(void *data, bool blocked)
{
	/* rfkill_ops callback. Turn transmitter on when blocked is false */
	if (!blocked) {
		pr_info("[BT] Bluetooth Power On.\n");
		gpio_set_value(BT_VREG_EN_GTI9060_R0_1, 1);
		msleep(50);
	} else {
		pr_info("[BT] Bluetooth Power Off.\n");
		gpio_set_value(BT_VREG_EN_GTI9060_R0_1, 0);
	}
	return 0;
}

static const struct rfkill_ops bcm4334_bt_rfkill_ops = {
	.set_block = bcm4334_bt_rfkill_set_power,
};

#ifdef BT_LPM_ENABLE
static void set_wake_locked(int wake)
{
	bt_lpm.wake = wake;

	if (!wake)
		wake_unlock(&bt_lpm.wake_lock);

	gpio_set_value(BT_WAKE_GTI9060_R0_1, wake);
}

static enum hrtimer_restart enter_lpm(struct hrtimer *timer)
{
	unsigned long flags;
	spin_lock_irqsave(&bt_lpm.uport->lock, flags);
	set_wake_locked(0);
	spin_unlock_irqrestore(&bt_lpm.uport->lock, flags);

	return HRTIMER_NORESTART;
}

void bcm_bt_lpm_exit_lpm_locked(struct uart_port *uport)
{
	bt_lpm.uport = uport;

	hrtimer_try_to_cancel(&bt_lpm.enter_lpm_timer);
	set_wake_locked(1);

	hrtimer_start(&bt_lpm.enter_lpm_timer, bt_lpm.enter_lpm_delay,
		HRTIMER_MODE_REL);
}
EXPORT_SYMBOL(bcm_bt_lpm_exit_lpm_locked);

static void update_host_wake_locked(int host_wake)
{
	if (host_wake == bt_lpm.host_wake)
		return;

	bt_lpm.host_wake = host_wake;

	if (host_wake) {
		wake_lock(&bt_lpm.wake_lock);
	} else  {
		/* Take a timed wakelock, so that upper layers can take it.
		 * The chipset deasserts the hostwake lock, when there is no
		 * more data to send.
		 */
		wake_lock_timeout(&bt_lpm.wake_lock, HZ/2);
	}
}

static irqreturn_t host_wake_isr(int irq, void *dev)
{
	int host_wake;
	unsigned long flags;

	host_wake = gpio_get_value(BT_HOST_WAKE_GTI9060_R0_1);

	irq_set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);

	if (!bt_lpm.uport) {
		bt_lpm.host_wake = host_wake;
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&bt_lpm.uport->lock, flags);
	update_host_wake_locked(host_wake);
	spin_unlock_irqrestore(&bt_lpm.uport->lock, flags);

	return IRQ_HANDLED;
}

static int bcm_bt_lpm_init(struct platform_device *pdev)
{
	int irq;
	int ret;

	hrtimer_init(&bt_lpm.enter_lpm_timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL);
	bt_lpm.enter_lpm_delay = ktime_set(1, 0);  /* 1 sec */
	bt_lpm.enter_lpm_timer.function = enter_lpm;

	bt_lpm.host_wake = 0;

	irq = gpio_to_irq(BT_HOST_WAKE_GTI9060_R0_1);
	ret = request_irq(irq, host_wake_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"bt host_wake", NULL);
	if (ret) {
		pr_err("[BT] Request_host wake irq failed.\n");
		return ret;
	}

	ret = irq_set_irq_wake(irq, 1);
	if (ret) {
		pr_err("[BT] Set_irq_wake failed.\n");
		return ret;
	}

	snprintf(bt_lpm.wake_lock_name, sizeof(bt_lpm.wake_lock_name),
			"BTLowPower");
	wake_lock_init(&bt_lpm.wake_lock, WAKE_LOCK_SUSPEND,
			 bt_lpm.wake_lock_name);
	return 0;
}
#endif


static int bcm4334_bluetooth_probe(struct platform_device *pdev)
{
	int rc = 0;
	int ret;

	rc = gpio_request(BT_VREG_EN_GTI9060_R0_1, "bcm4334_bten_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] BT_VREG_EN_GTI9060_R0_1 request failed.\n");
		return rc;
	}

	rc = gpio_request(BT_WAKE_GTI9060_R0_1, "bcm4334_btwake_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] BT_WAKE_GTI9060_R0_1 request failed.\n");
		gpio_free(BT_VREG_EN_GTI9060_R0_1);
		return rc;
	}
	rc = gpio_request(BT_HOST_WAKE_GTI9060_R0_1, "bcm4334_bthostwake_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] BT_HOST_WAKE_GTI9060_R0_1 request failed.\n");
		gpio_free(BT_WAKE_GTI9060_R0_1);
		gpio_free(BT_VREG_EN_GTI9060_R0_1);
		return rc;
	}
	gpio_direction_input(BT_HOST_WAKE_GTI9060_R0_1);
	gpio_direction_output(BT_WAKE_GTI9060_R0_1, 0);
	gpio_direction_output(BT_VREG_EN_GTI9060_R0_1, 0);

	gpio_set_value(BT_VREG_EN_GTI9060_R0_1, 1);
	msleep(5);
	gpio_set_value(BT_VREG_EN_GTI9060_R0_1, 0);
	
	bt_rfkill = rfkill_alloc("bcm4334 Bluetooth", &pdev->dev,
				RFKILL_TYPE_BLUETOOTH, &bcm4334_bt_rfkill_ops,
				NULL);

	if (unlikely(!bt_rfkill)) {
		pr_err("[BT] bt_rfkill alloc failed.\n");
		gpio_free(BT_HOST_WAKE_GTI9060_R0_1);
		gpio_free(BT_WAKE_GTI9060_R0_1);
		gpio_free(BT_VREG_EN_GTI9060_R0_1);
		return -ENOMEM;
	}

	rfkill_init_sw_state(bt_rfkill, 0);

	rc = rfkill_register(bt_rfkill);

	if (unlikely(rc)) {
		pr_err("[BT] bt_rfkill register failed.\n");
		rfkill_destroy(bt_rfkill);
		gpio_free(BT_HOST_WAKE_GTI9060_R0_1);
		gpio_free(BT_WAKE_GTI9060_R0_1);
		gpio_free(BT_VREG_EN_GTI9060_R0_1);
		return -1;
	}


#ifdef BT_LPM_ENABLE
	ret = bcm_bt_lpm_init(pdev);
	if (ret) {
		rfkill_unregister(bt_rfkill);
		rfkill_destroy(bt_rfkill);

		gpio_free(BT_HOST_WAKE_GTI9060_R0_1);
		gpio_free(BT_WAKE_GTI9060_R0_1);
		gpio_free(BT_VREG_EN_GTI9060_R0_1);
	}
#endif
	return rc;
}

static int bcm4334_bluetooth_remove(struct platform_device *pdev)
{
	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);

	gpio_free(BT_VREG_EN_GTI9060_R0_1);
	gpio_free(BT_WAKE_GTI9060_R0_1);
	gpio_free(BT_HOST_WAKE_GTI9060_R0_1);

	wake_lock_destroy(&bt_lpm.wake_lock);
	return 0;
}

static struct platform_driver bcm4334_bluetooth_platform_driver = {
	.probe = bcm4334_bluetooth_probe,
	.remove = bcm4334_bluetooth_remove,
	.driver = {
		   .name = "bcm4334_bluetooth",
		   .owner = THIS_MODULE,
		   },
};

static int __init bcm4334_bluetooth_init(void)
{
	return platform_driver_register(&bcm4334_bluetooth_platform_driver);
}

static void __exit bcm4334_bluetooth_exit(void)
{
	platform_driver_unregister(&bcm4334_bluetooth_platform_driver);
}

module_init(bcm4334_bluetooth_init);
module_exit(bcm4334_bluetooth_exit);

MODULE_ALIAS("platform:bcm4334");
MODULE_DESCRIPTION("bcm4334_bluetooth");
MODULE_LICENSE("GPL");
