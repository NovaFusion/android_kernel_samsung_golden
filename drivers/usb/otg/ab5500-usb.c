/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Avinash Kumar <avinash.kumar@stericsson.com> for ST-Ericsson
 * Author: Ravi Kant SINGH <ravikant.singh@stericsson.com> for ST-Ericsson
 * Author: Supriya  s KARANTH <supriya.karanth@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/usb.h>
#include <linux/kernel_stat.h>
#include <mach/gpio.h>
#include <mach/reboot_reasons.h>
#include <linux/pm_qos_params.h>

#include <linux/wakelock.h>
static struct wake_lock ab5500_musb_wakelock;

/* AB5500 USB macros
 */
#define AB5500_MAIN_WATCHDOG_ENABLE 0x1
#define AB5500_MAIN_WATCHDOG_KICK 0x2
#define AB5500_MAIN_WATCHDOG_DISABLE 0x0
#define AB5500_USB_ADP_ENABLE 0x1
#define AB5500_WATCHDOG_DELAY 10
#define AB5500_WATCHDOG_DELAY_US 100
#define AB5500_PHY_DELAY_US 100
#define AB5500_MAIN_WDOG_CTRL_REG      0x01
#define AB5500_USB_LINE_STAT_REG       0x80
#define AB5500_USB_PHY_CTRL_REG        0x8A
#define AB5500_MAIN_WATCHDOG_ENABLE 0x1
#define AB5500_MAIN_WATCHDOG_KICK 0x2
#define AB5500_MAIN_WATCHDOG_DISABLE 0x0
#define AB5500_SYS_CTRL2_BLOCK	0x2

/* UsbLineStatus register bit masks */
#define AB5500_USB_LINK_STATUS_MASK_V1		0x78
#define AB5500_USB_LINK_STATUS_MASK_V2		0xF8

#define USB_PROBE_DELAY 1000 /* 1 seconds */
#define USB_LIMIT (200) /* If we have more than 200 irqs per second */

static struct pm_qos_request_list usb_pm_qos_latency;
static bool usb_pm_qos_is_latency_0;

#define PUBLIC_ID_BACKUPRAM1 (U5500_BACKUPRAM1_BASE + 0x0FC0)
#define MAX_USB_SERIAL_NUMBER_LEN 31

/* UsbLineStatus register - usb types */
enum ab5500_usb_link_status {
	USB_LINK_NOT_CONFIGURED,
	USB_LINK_STD_HOST_NC,
	USB_LINK_STD_HOST_C_NS,
	USB_LINK_STD_HOST_C_S,
	USB_LINK_HOST_CHG_NM,
	USB_LINK_HOST_CHG_HS,
	USB_LINK_HOST_CHG_HS_CHIRP,
	USB_LINK_DEDICATED_CHG,
	USB_LINK_ACA_RID_A,
	USB_LINK_ACA_RID_B,
	USB_LINK_ACA_RID_C_NM,
	USB_LINK_ACA_RID_C_HS,
	USB_LINK_ACA_RID_C_HS_CHIRP,
	USB_LINK_HM_IDGND,
	USB_LINK_OTG_HOST_NO_CURRENT,
	USB_LINK_NOT_VALID_LINK,
	USB_LINK_PHY_EN_NO_VBUS_NO_IDGND,
	USB_LINK_STD_UPSTREAM_NO_VBUS_NO_IDGND,
	USB_LINK_HM_IDGND_V2
};

/**
 * ab5500_usb_mode - Different states of ab usb_chip
 *
 * Used for USB cable plug-in state machine
 */
enum ab5500_usb_mode {
	USB_IDLE,
	USB_DEVICE,
	USB_HOST,
	USB_DEDICATED_CHG,
};
struct ab5500_usb {
	struct otg_transceiver otg;
	struct device *dev;
	int irq_num_id_fall;
	int irq_num_vbus_rise;
	int irq_num_vbus_fall;
	int irq_num_link_status;
	unsigned vbus_draw;
	struct delayed_work dwork;
	struct work_struct phy_dis_work;
	unsigned long link_status_wait;
	int rev;
	int usb_cs_gpio;
	enum ab5500_usb_mode mode;
	struct clk *sysclk;
	struct regulator *v_ape;
	struct abx500_usbgpio_platform_data *usb_gpio;
	struct delayed_work work_usb_workaround;
	bool phy_enabled;
};

static int ab5500_usb_irq_setup(struct platform_device *pdev,
				struct ab5500_usb *ab);
static int ab5500_usb_boot_detect(struct ab5500_usb *ab);
static int ab5500_usb_link_status_update(struct ab5500_usb *ab);

static void ab5500_usb_phy_enable(struct ab5500_usb *ab, bool sel_host);
static void ab5500_usb_phy_disable(struct ab5500_usb *ab, bool sel_host);

static inline struct ab5500_usb *xceiv_to_ab(struct otg_transceiver *x)
{
	return container_of(x, struct ab5500_usb, otg);
}

/**
 * ab5500_usb_wd_workaround() - Kick the watch dog timer
 *
 * This function used to Kick the watch dog timer
 */
static void ab5500_usb_wd_workaround(struct ab5500_usb *ab)
{
	abx500_set_register_interruptible(ab->dev,
			AB5500_SYS_CTRL2_BLOCK,
			AB5500_MAIN_WDOG_CTRL_REG,
			AB5500_MAIN_WATCHDOG_ENABLE);

		udelay(AB5500_WATCHDOG_DELAY_US);

	abx500_set_register_interruptible(ab->dev,
			AB5500_SYS_CTRL2_BLOCK,
			AB5500_MAIN_WDOG_CTRL_REG,
			(AB5500_MAIN_WATCHDOG_ENABLE
			 | AB5500_MAIN_WATCHDOG_KICK));

		udelay(AB5500_WATCHDOG_DELAY_US);

	abx500_set_register_interruptible(ab->dev,
			AB5500_SYS_CTRL2_BLOCK,
			AB5500_MAIN_WDOG_CTRL_REG,
			AB5500_MAIN_WATCHDOG_DISABLE);

		udelay(AB5500_WATCHDOG_DELAY_US);
}

static void ab5500_usb_load(struct work_struct *work)
{
	int cpu;
	unsigned int num_irqs = 0;
	static unsigned int old_num_irqs = UINT_MAX;
	struct delayed_work *work_usb_workaround = to_delayed_work(work);
	struct ab5500_usb *ab = container_of(work_usb_workaround,
				struct ab5500_usb, work_usb_workaround);

	for_each_online_cpu(cpu)
	num_irqs += kstat_irqs_cpu(IRQ_DB5500_USBOTG, cpu);

	if ((num_irqs > old_num_irqs) &&
		(num_irqs - old_num_irqs) > USB_LIMIT) {

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
	}
	old_num_irqs = num_irqs;

	schedule_delayed_work_on(0,
				&ab->work_usb_workaround,
				msecs_to_jiffies(USB_PROBE_DELAY));
}

static void ab5500_usb_phy_enable(struct ab5500_usb *ab, bool sel_host)
{
	int ret = 0;
	/* Workaround for spurious interrupt to be checked with Hardware Team*/
	if (ab->phy_enabled == true)
		return;
	ab->phy_enabled = true;

	wake_lock(&ab5500_musb_wakelock);
	ab->usb_gpio->enable();
	clk_enable(ab->sysclk);
	regulator_enable(ab->v_ape);
	/* TODO: Remove ux500_resotore_context and handle similar to ab8500 */
	ux500_restore_context(NULL);
	ret = gpio_direction_output(ab->usb_cs_gpio, 0);
	if (ret < 0) {
		dev_err(ab->dev, "usb_cs_gpio: gpio direction failed\n");
		gpio_free(ab->usb_cs_gpio);
		return;
	}
	gpio_set_value(ab->usb_cs_gpio, 1);
	if (sel_host) {
		schedule_delayed_work_on(0,
					&ab->work_usb_workaround,
					msecs_to_jiffies(USB_PROBE_DELAY));
	}
}

static void ab5500_usb_phy_disable(struct ab5500_usb *ab, bool sel_host)
{
	/* Workaround for spurious interrupt to be checked with Hardware Team*/
	if (ab->phy_enabled == false)
		return;
	ab->phy_enabled = false;

	/* Needed to disable the phy.*/
	ab5500_usb_wd_workaround(ab);
	clk_disable(ab->sysclk);
	regulator_disable(ab->v_ape);
	ab->usb_gpio->disable();
	gpio_set_value(ab->usb_cs_gpio, 0);

	if (sel_host) {
		if (usb_pm_qos_is_latency_0) {

				pm_qos_remove_request(&usb_pm_qos_latency);
				usb_pm_qos_is_latency_0 = false;
		}
		cancel_delayed_work_sync(&ab->work_usb_workaround);
	}

	wake_unlock(&ab5500_musb_wakelock);
}

#define ab5500_usb_peri_phy_en(ab)	ab5500_usb_phy_enable(ab, false)
#define ab5500_usb_peri_phy_dis(ab)	ab5500_usb_phy_disable(ab, false)
#define ab5500_usb_host_phy_en(ab)	ab5500_usb_phy_enable(ab, true)
#define ab5500_usb_host_phy_dis(ab)	ab5500_usb_phy_disable(ab, true)

/* Work created after an link status update handler*/
static int ab5500_usb_link_status_update(struct ab5500_usb *ab)
{
	u8 val = 0;
	enum ab5500_usb_link_status lsts;
	enum usb_xceiv_events event = USB_EVENT_NONE;

	(void)abx500_get_register_interruptible(ab->dev,
			AB5500_BANK_USB, AB5500_USB_LINE_STAT_REG, &val);

	if (ab->rev >= AB5500_2_0)
		lsts = (val & AB5500_USB_LINK_STATUS_MASK_V2) >> 3;
	else
		lsts = (val & AB5500_USB_LINK_STATUS_MASK_V1) >> 3;

	switch (lsts) {

	case USB_LINK_STD_HOST_NC:
	case USB_LINK_STD_HOST_C_NS:
	case USB_LINK_STD_HOST_C_S:
	case USB_LINK_HOST_CHG_NM:
	case USB_LINK_HOST_CHG_HS:
	case USB_LINK_HOST_CHG_HS_CHIRP:

		event = USB_EVENT_VBUS;
		ab5500_usb_peri_phy_en(ab);

		break;
	case USB_LINK_DEDICATED_CHG:
		/* TODO: vbus_draw */
		event = USB_EVENT_CHARGER;
		break;

	case USB_LINK_HM_IDGND:
		if (ab->rev >= AB5500_2_0)
			return -1;


		ab5500_usb_host_phy_en(ab);

		ab->otg.default_a = true;
		event = USB_EVENT_ID;

		break;
	case USB_LINK_PHY_EN_NO_VBUS_NO_IDGND:
		ab5500_usb_peri_phy_dis(ab);

		break;
	case USB_LINK_STD_UPSTREAM_NO_VBUS_NO_IDGND:
		ab5500_usb_host_phy_dis(ab);

		break;

	case USB_LINK_HM_IDGND_V2:
		if (!(ab->rev >= AB5500_2_0))
			return -1;


		ab5500_usb_host_phy_en(ab);

		ab->otg.default_a = true;
		event = USB_EVENT_ID;

		break;
	default:
		break;
	}

	atomic_notifier_call_chain(&ab->otg.notifier, event, &ab->vbus_draw);

	return 0;
}

static void ab5500_usb_delayed_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ab5500_usb *ab = container_of(dwork, struct ab5500_usb, dwork);

	ab5500_usb_link_status_update(ab);
}

/**
 * This function is used to signal the completion of
 * USB Link status register update
 */
static irqreturn_t ab5500_usb_link_status_irq(int irq, void *data)
{
	struct ab5500_usb *ab = (struct ab5500_usb *) data;
	ab5500_usb_link_status_update(ab);

	return IRQ_HANDLED;
}




static void ab5500_usb_irq_free(struct ab5500_usb *ab)
{
	if (ab->irq_num_link_status)
		free_irq(ab->irq_num_link_status, ab);
}

/**
 * ab5500_usb_irq_setup : register USB callback handlers for ab5500
 * @mode: value for mode.
 *
 * This function is used to register USB callback handlers for ab5500.
 */
static int ab5500_usb_irq_setup(struct platform_device *pdev,
				struct ab5500_usb *ab)
{
	int ret = 0;
	int irq, err;

	if (!ab->dev)
		return -EINVAL;


	irq = platform_get_irq_byname(pdev, "Link_Update");
	if (irq < 0) {
		dev_err(&pdev->dev, "Link Update irq not found\n");
		err = irq;
		goto irq_fail;
	}
	ab->irq_num_link_status = irq;

	ret = request_threaded_irq(ab->irq_num_link_status,
		NULL, ab5500_usb_link_status_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-link-status-update", ab);
	if (ret < 0) {
		printk(KERN_ERR "failed to set the callback"
				" handler for usb charge"
				" detect done\n");
		err = ret;
		goto irq_fail;
	}

	ab5500_usb_wd_workaround(ab);
	return 0;

irq_fail:
	ab5500_usb_irq_free(ab);
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

static struct attribute *ab5500_usb_attributes[] = {
	&dev_attr_serial_number.attr,
	NULL
};
static const struct attribute_group ab5500_attr_group = {
	.attrs = ab5500_usb_attributes,
};

static int ab5500_create_sysfsentries(struct ab5500_usb *ab)
{
	int err;

	err = sysfs_create_group(&ab->dev->kobj, &ab5500_attr_group);
	if (err)
		sysfs_remove_group(&ab->dev->kobj, &ab5500_attr_group);

	return err;
}

/**
 * ab5500_usb_boot_detect : detect the USB cable during boot time.
 * @mode: value for mode.
 *
 * This function is used to detect the USB cable during boot time.
 */
static int ab5500_usb_boot_detect(struct ab5500_usb *ab)
{
	int usb_status = 0;
	enum ab5500_usb_link_status lsts;
	if (!ab->dev)
		return -EINVAL;

	(void)abx500_get_register_interruptible(ab->dev,
			AB5500_BANK_USB, AB5500_USB_LINE_STAT_REG, &usb_status);

	if (ab->rev >= AB5500_2_0)
		lsts = (usb_status & AB5500_USB_LINK_STATUS_MASK_V2) >> 3;
	else
		lsts = (usb_status & AB5500_USB_LINK_STATUS_MASK_V1) >> 3;

	switch (lsts) {

	case USB_LINK_STD_HOST_NC:
	case USB_LINK_STD_HOST_C_NS:
	case USB_LINK_STD_HOST_C_S:
	case USB_LINK_HOST_CHG_NM:
	case USB_LINK_HOST_CHG_HS:
	case USB_LINK_HOST_CHG_HS_CHIRP:
		/*
		 * If Power on key was not pressed then enter charge only
		 * mode and dont enumerate
		 */
		if ((!(ab5500_get_turn_on_status() &
					(P_ON_KEY1_EVENT | P_ON_KEY2_EVENT))) &&
					(prcmu_get_reset_code() ==
					SW_RESET_COLDSTART)) {
			dev_dbg(ab->dev, "USB entered charge only mode");
			return 0;
		}
		ab5500_usb_peri_phy_en(ab);

		break;

	case USB_LINK_HM_IDGND:
	case USB_LINK_HM_IDGND_V2:
		ab5500_usb_host_phy_en(ab);

		break;
	default:
		break;
	}

	return 0;
}

static int ab5500_usb_set_power(struct otg_transceiver *otg, unsigned mA)
{
	struct ab5500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	ab->vbus_draw = mA;

	atomic_notifier_call_chain(&ab->otg.notifier,
				USB_EVENT_VBUS, &ab->vbus_draw);
	return 0;
}

static int ab5500_usb_set_suspend(struct otg_transceiver *x, int suspend)
{
	/* TODO */
	return 0;
}

static int ab5500_usb_set_host(struct otg_transceiver *otg,
					struct usb_bus *host)
{
	struct ab5500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	/* Some drivers call this function in atomic context.
	 * Do not update ab5500 registers directly till this
	 * is fixed.
	 */

	if (!host) {
		ab->otg.host = NULL;
		schedule_work(&ab->phy_dis_work);
	} else {
		ab->otg.host = host;
	}

	return 0;
}

static int ab5500_usb_set_peripheral(struct otg_transceiver *otg,
		struct usb_gadget *gadget)
{
	struct ab5500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	/* Some drivers call this function in atomic context.
	 * Do not update ab5500 registers directly till this
	 * is fixed.
	 */

	if (!gadget) {
		ab->otg.gadget = NULL;
		schedule_work(&ab->phy_dis_work);
	} else {
		ab->otg.gadget = gadget;
	}

	return 0;
}

static int __devinit ab5500_usb_probe(struct platform_device *pdev)
{
	struct ab5500_usb	*ab;
	struct abx500_usbgpio_platform_data *usb_pdata =
				pdev->dev.platform_data;
	int err;
	int ret = -1;
	ab = kzalloc(sizeof *ab, GFP_KERNEL);
	if (!ab)
		return -ENOMEM;

	ab->dev			= &pdev->dev;
	ab->otg.dev		= ab->dev;
	ab->otg.label		= "ab5500";
	ab->otg.state		= OTG_STATE_B_IDLE;
	ab->otg.set_host	= ab5500_usb_set_host;
	ab->otg.set_peripheral	= ab5500_usb_set_peripheral;
	ab->otg.set_suspend	= ab5500_usb_set_suspend;
	ab->otg.set_power	= ab5500_usb_set_power;
	ab->usb_gpio		= usb_pdata;
	ab->mode			= USB_IDLE;

	platform_set_drvdata(pdev, ab);

	ATOMIC_INIT_NOTIFIER_HEAD(&ab->otg.notifier);

	/* v1: Wait for link status to become stable.
	 * all: Updates form set_host and set_peripheral as they are atomic.
	 */
	INIT_DELAYED_WORK(&ab->dwork, ab5500_usb_delayed_work);

	INIT_DELAYED_WORK_DEFERRABLE(&ab->work_usb_workaround,
							ab5500_usb_load);

	err = otg_set_transceiver(&ab->otg);
	if (err)
		dev_err(&pdev->dev, "Can't register transceiver\n");

	ab->usb_cs_gpio = ab->usb_gpio->usb_cs;

	ab->rev = abx500_get_chip_id(ab->dev);

	ab->sysclk = clk_get(ab->dev, "sysclk");
	if (IS_ERR(ab->sysclk)) {
		ret = PTR_ERR(ab->sysclk);
		ab->sysclk = NULL;
		return ret;
	}

	ab->v_ape = regulator_get(ab->dev, "v-ape");
	if (!ab->v_ape) {
		dev_err(ab->dev, "Could not get v-ape supply\n");

		return -EINVAL;
	}

	ab5500_usb_irq_setup(pdev, ab);

	ret = gpio_request(ab->usb_cs_gpio, "usb-cs");
	if (ret < 0)
		dev_err(&pdev->dev, "usb gpio request fail\n");

	/* Aquire GPIO alternate config struct for USB */
	err = ab->usb_gpio->get(ab->dev);
	if (err < 0)
		goto fail1;

	/*
	 * wake lock is acquired when usb cable is connected and released when
	 * cable is removed
	 */
	wake_lock_init(&ab5500_musb_wakelock, WAKE_LOCK_SUSPEND, "ab5500-usb");

	err = ab5500_usb_boot_detect(ab);
	if (err < 0)
		goto fail1;

	err = ab5500_create_sysfsentries(ab);
	if (err < 0)
		dev_err(ab->dev, "usb create sysfs entries failed\n");

	return 0;

fail1:
	ab5500_usb_irq_free(ab);
	kfree(ab);
	return err;
}

static int __devexit ab5500_usb_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ab5500_usb_driver = {
	.driver		= {
		.name	= "ab5500-usb",
		.owner	= THIS_MODULE,
	},
	.probe		= ab5500_usb_probe,
	.remove		= __devexit_p(ab5500_usb_remove),
};

static int __init ab5500_usb_init(void)
{
	return platform_driver_register(&ab5500_usb_driver);
}
subsys_initcall(ab5500_usb_init);

static void __exit ab5500_usb_exit(void)
{
	platform_driver_unregister(&ab5500_usb_driver);
}
module_exit(ab5500_usb_exit);

MODULE_LICENSE("GPL v2");
