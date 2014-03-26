/*
 * Copyright (C) 2010 ST-Ericsson AB
 * Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 *
 * Based on omap2430.c
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/wakelock.h>
#include <mach/id.h>
#include <mach/usb.h>

#include "musb_core.h"

static void ux500_musb_set_vbus(struct musb *musb, int is_on);

struct ux500_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct clk		*clk;
};
#define glue_to_musb(g)	platform_get_drvdata(g->musb)

static struct timer_list notify_timer;
static struct musb_context_registers context;
static bool context_stored;
struct musb *_musb;
static struct wake_lock ux500_usb_wakelock;

static void ux500_store_context(struct musb *musb)
{
#ifdef CONFIG_PM
	int i;
	void __iomem *musb_base;
	void __iomem *epio;

	if (cpu_is_u5500()) {
		if (musb != NULL)
			_musb = musb;
		else
			return;
	}

	musb_base = musb->mregs;

	if (is_host_enabled(musb)) {
		context.frame = musb_readw(musb_base, MUSB_FRAME);
		context.testmode = musb_readb(musb_base, MUSB_TESTMODE);
		context.busctl = musb_read_ulpi_buscontrol(musb->mregs);
	}
	context.intrtxe = musb_readw(musb_base, MUSB_INTRTXE);
	context.intrrxe = musb_readw(musb_base, MUSB_INTRRXE);
	context.index = musb_readb(musb_base, MUSB_INDEX);
	context.intrusbe = musb_readb(musb_base, MUSB_INTRUSBE);


	for (i = 0; i < musb->config->num_eps; ++i) {
		struct musb_hw_ep       *hw_ep;

		musb_writeb(musb_base, MUSB_INDEX, i);
		hw_ep = &musb->endpoints[i];
		if (!hw_ep)
			continue;

		epio = hw_ep->regs;
		if (!epio)
			continue;

		context.index_regs[i].txmaxp =
				musb_readw(epio, MUSB_TXMAXP);
		context.index_regs[i].txcsr =
		musb_readw(epio, MUSB_TXCSR);
		context.index_regs[i].rxmaxp =
			musb_readw(epio, MUSB_RXMAXP);
		context.index_regs[i].rxcsr =
			musb_readw(epio, MUSB_RXCSR);

		if (musb->dyn_fifo) {
			context.index_regs[i].txfifoadd =
				musb_read_txfifoadd(musb_base);
			context.index_regs[i].rxfifoadd =
				musb_read_rxfifoadd(musb_base);
			context.index_regs[i].txfifosz =
				musb_read_txfifosz(musb_base);
			context.index_regs[i].rxfifosz =
				musb_read_rxfifosz(musb_base);
		}
		if (is_host_enabled(musb)) {
			context.index_regs[i].txtype =
				musb_readb(epio, MUSB_TXTYPE);
			context.index_regs[i].txinterval =
			musb_readb(epio, MUSB_TXINTERVAL);
			context.index_regs[i].rxtype =
				musb_readb(epio, MUSB_RXTYPE);
			context.index_regs[i].rxinterval =
				musb_readb(epio, MUSB_RXINTERVAL);

			context.index_regs[i].txfunaddr =
			musb_read_txfunaddr(musb_base, i);
			context.index_regs[i].txhubaddr =
				musb_read_txhubaddr(musb_base, i);
			context.index_regs[i].txhubport =
				musb_read_txhubport(musb_base, i);

			context.index_regs[i].rxfunaddr =
				musb_read_rxfunaddr(musb_base, i);
			context.index_regs[i].rxhubaddr =
				musb_read_rxhubaddr(musb_base, i);
			context.index_regs[i].rxhubport =
				musb_read_rxhubport(musb_base, i);
		}
	}
	context_stored = true;
#endif
}

void ux500_restore_context(struct musb *musb)
{
#ifdef CONFIG_PM
	int i;
	void __iomem *musb_base;
	void __iomem *ep_target_regs;
	void __iomem *epio;

	if (!context_stored)
		return;

	if (cpu_is_u5500()) {
		if (_musb != NULL)
			musb = _musb;
		else
			return;
	}

	musb_base = musb->mregs;
	/*
	 * Controller reset needs to be done before the context is
	 * restored to ensure incorrect values are not  present.
	 */
	musb_writeb(musb_base, MUSB_SOFT_RST, MUSB_SOFT_RST_NRST
			| MUSB_SOFT_RST_NRSTX);
	if (is_host_enabled(musb)) {
		musb_writew(musb_base, MUSB_FRAME, context.frame);
		musb_writeb(musb_base, MUSB_TESTMODE, context.testmode);
		musb_write_ulpi_buscontrol(musb->mregs, context.busctl);
	 }

	musb_writeb(musb_base, MUSB_POWER, MUSB_POWER_SOFTCONN
						| MUSB_POWER_HSENAB);
	musb_writew(musb_base, MUSB_INTRTXE, context.intrtxe);
	musb_writew(musb_base, MUSB_INTRRXE, context.intrrxe);
	musb_writeb(musb_base, MUSB_INTRUSBE, context.intrusbe);

	for (i = 0; i < musb->config->num_eps; ++i) {
		struct musb_hw_ep       *hw_ep;

		musb_writeb(musb_base, MUSB_INDEX, i);
		hw_ep = &musb->endpoints[i];
		if (!hw_ep)
			continue;

		epio = hw_ep->regs;
		if (!epio)
			continue;

		musb_writew(epio, MUSB_TXMAXP,
			context.index_regs[i].txmaxp);
		musb_writew(epio, MUSB_TXCSR,
			context.index_regs[i].txcsr);
		musb_writew(epio, MUSB_RXMAXP,
			context.index_regs[i].rxmaxp);
		musb_writew(epio, MUSB_RXCSR,
			context.index_regs[i].rxcsr);

	if (musb->dyn_fifo) {
		musb_write_txfifosz(musb_base,
			context.index_regs[i].txfifosz);
		musb_write_rxfifosz(musb_base,
			context.index_regs[i].rxfifosz);
		musb_write_txfifoadd(musb_base,
			context.index_regs[i].txfifoadd);
		musb_write_rxfifoadd(musb_base,
		context.index_regs[i].rxfifoadd);
		}

	if (is_host_enabled(musb)) {
		musb_writeb(epio, MUSB_TXTYPE,
			context.index_regs[i].txtype);
		musb_writeb(epio, MUSB_TXINTERVAL,
			context.index_regs[i].txinterval);
		musb_writeb(epio, MUSB_RXTYPE,
			context.index_regs[i].rxtype);
		musb_writeb(epio, MUSB_RXINTERVAL,

		musb->context.index_regs[i].rxinterval);
		musb_write_txfunaddr(musb_base, i,
			context.index_regs[i].txfunaddr);
		musb_write_txhubaddr(musb_base, i,
			context.index_regs[i].txhubaddr);
		musb_write_txhubport(musb_base, i,
			context.index_regs[i].txhubport);

		ep_target_regs =
			musb_read_target_reg_base(i, musb_base);

		musb_write_rxfunaddr(ep_target_regs,
				context.index_regs[i].rxfunaddr);
		musb_write_rxhubaddr(ep_target_regs,
			context.index_regs[i].rxhubaddr);
		musb_write_rxhubport(ep_target_regs,
		context.index_regs[i].rxhubport);
		}
	}
	musb_writeb(musb_base, MUSB_INDEX, context.index);
#endif
}

static void musb_notify_idle(unsigned long _musb)
{
	struct musb	*musb = (void *)_musb;
	unsigned long	flags;

	u8	devctl;
	dev_dbg(musb->controller, "musb_notify_idle %s\n",
				otg_state_string(musb->xceiv->state));
	spin_lock_irqsave(&musb->lock, flags);

	switch (musb->xceiv->state) {
	case OTG_STATE_A_WAIT_BCON:
		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		if (cpu_is_u8500() && !((devctl & MUSB_DEVCTL_SESSION) == 1)) {
			pm_runtime_mark_last_busy(musb->controller);
			pm_runtime_put_autosuspend(musb->controller);
		}
		wake_unlock(&ux500_usb_wakelock);
		break;

	case OTG_STATE_A_SUSPEND:
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}

/* blocking notifier support */
static int musb_otg_notifications(struct notifier_block *nb,
		unsigned long event, void *unused)
{
	struct musb	*musb = container_of(nb, struct musb, nb);
	unsigned long	flags;

	dev_dbg(musb->controller, "musb_otg_notifications %ld %s\n",
				event, otg_state_string(musb->xceiv->state));
	switch (event) {

	case USB_EVENT_PREPARE:
		wake_lock(&ux500_usb_wakelock);
		pm_runtime_get_sync(musb->controller);
		ux500_restore_context(musb);
		break;
	case USB_EVENT_ID:
	case USB_EVENT_RIDA:
		dev_dbg(musb->controller, "ID GND\n");
		if (is_otg_enabled(musb)) {
				ux500_musb_set_vbus(musb, 1);
		}
		break;

	case USB_EVENT_VBUS:
		dev_dbg(musb->controller, "VBUS Connect\n");

		break;
	case USB_EVENT_RIDB:
	case USB_EVENT_NONE:
		dev_dbg(musb->controller, "VBUS Disconnect\n");
		if (is_otg_enabled(musb) && musb->is_host)
			ux500_musb_set_vbus(musb, 0);
		else {
			spin_lock_irqsave(&musb->lock, flags);
			musb_g_disconnect(musb);
			spin_unlock_irqrestore(&musb->lock, flags);
			musb->xceiv->state = OTG_STATE_B_IDLE;
		}
		break;
	case USB_EVENT_CLEAN:
		pm_runtime_mark_last_busy(musb->controller);
		pm_runtime_put_autosuspend(musb->controller);
		wake_unlock(&ux500_usb_wakelock);
		break;
	default:
		dev_dbg(musb->controller, "ID float\n");
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static void ux500_musb_set_vbus(struct musb *musb, int is_on)
{
	u8		devctl;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	int ret = 1;
#ifdef	CONFIG_USB_OTG_20
	int val = 0;
#endif
	/* HDRC controls CPEN, but beware current surges during device
	 * connect.  They can trigger transient overcurrent conditions
	 * that must be ignored.
	 */
#ifdef	CONFIG_USB_OTG_20
	val = musb_readb(musb->mregs, MUSB_MISC);
	val |= 0x1C;
	musb_writeb(musb->mregs, MUSB_MISC, val);
#endif
	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	if (is_on) {
#ifdef CONFIG_USB_OTG_20
		/*
		 * OTG 2.0 Compliance
		 * When the ID is grounded, device should go into the A
		 * mode. This is applicable when the device is in B_IDLE
		 * state also. When the UUT is in B_PERIPHERAL mode, the
		 * ID grounding should transition it back to B_IDLE and
		 * then to A_IDLE. Since the RID_A interrupt occurs only
		 * once, OTG state is set to A_IDLE skipping the B_IDLE
		 * state inbetween.
		 * OTG 2.0 - 7.2/Fig 7.3
		 */
		if (musb->xceiv->state == OTG_STATE_A_IDLE ||
				musb->xceiv->state == OTG_STATE_B_IDLE ||
				musb->xceiv->state == OTG_STATE_B_PERIPHERAL) {
#else
		if (musb->xceiv->state == OTG_STATE_A_IDLE) {
#endif
			/* put the state back to A_IDLE */
			musb->xceiv->state = OTG_STATE_A_IDLE;
			/* start the session */
			devctl |= MUSB_DEVCTL_SESSION;
			musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
			/*
			 * Wait for the musb to set as A device to enable the
			 * VBUS
			 */
			while (musb_readb(musb->mregs, MUSB_DEVCTL) & 0x80) {

				if (time_after(jiffies, timeout)) {
					dev_err(musb->controller,
					"configured as A device timeout");
					ret = -EINVAL;
					break;
				}
			}

		} else {
			musb->is_active = 1;
			musb->xceiv->default_a = 1;
			musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
			devctl |= MUSB_DEVCTL_SESSION;
			MUSB_HST_MODE(musb);
		}
	} else {
		musb->is_active = 0;

		/* NOTE:  we're skipping A_WAIT_VFALL -> A_IDLE and
		 * jumping right to B_IDLE...
		 */
		musb->xceiv->default_a = 0;
		devctl &= ~MUSB_DEVCTL_SESSION;
		MUSB_DEV_MODE(musb);
	}
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	/*
	 * Devctl values will be updated after vbus goes below
	 * session_valid. The time taken depends on the capacitance
	 * on VBUS line. The max discharge time can be upto 1 sec
	 * as per the spec. Typically on our platform, it is 200ms
	 */

	/* TODO: Check discharge time values for other platforms */
	if (!is_on)
		mdelay(200);
	dev_dbg(musb->controller, "VBUS %s, devctl %02x "
		/* otg %3x conf %08x prcm %08x */ "\n",
		otg_state_string(musb->xceiv->state),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

static void ux500_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	static unsigned long	last_timer;

	if (timeout == 0)
		timeout = jiffies + msecs_to_jiffies(3);

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || ((musb->a_wait_bcon == 0)
			&& (musb->xceiv->state == OTG_STATE_A_WAIT_BCON))) {
		dev_dbg(musb->controller, "%s active, deleting timer\n",
			otg_state_string(musb->xceiv->state));
		del_timer(&notify_timer);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout)) {
		if (!timer_pending(&notify_timer))
			last_timer = timeout;
		else {
			dev_dbg(musb->controller, "Longer idle timer "
						"already pending, ignoring\n");
			return;
		}
	}
	last_timer = timeout;

	dev_dbg(musb->controller, "%s inactive, for idle timer for %lu ms\n",
		otg_state_string(musb->xceiv->state),
		(unsigned long)jiffies_to_msecs(timeout - jiffies));
	mod_timer(&notify_timer, timeout);
}

static void ux500_musb_enable(struct musb *musb)
{
	ux500_store_context(musb);
}

static struct usb_ep *ux500_musb_configure_endpoints(struct musb *musb,
		u8 type, struct usb_endpoint_descriptor  *desc)
{
	struct usb_ep *ep = NULL;
	struct usb_gadget *gadget = &musb->g;
	char name[4];

	if (USB_ENDPOINT_XFER_INT == type) {
		list_for_each_entry(ep, &gadget->ep_list, ep_list) {
			if (ep->maxpacket == 512)
				continue;
			if (NULL == ep->driver_data) {
				strncpy(name, (ep->name + 3), 4);
				if (USB_DIR_IN & desc->bEndpointAddress)
					if (strcmp("in", name) == 0)
						return ep;
			}
		}
	}
	return ep;
}

static int ux500_musb_init(struct musb *musb)
{
	int status;

	musb->xceiv = otg_get_transceiver();
	if (!musb->xceiv) {
		pr_err("HS USB OTG: no transceiver configured\n");
		return -ENODEV;
	}
	status = pm_runtime_get_sync(musb->controller);
	if (status < 0) {
		dev_err(musb->controller, "pm_runtime_get_sync FAILED");
		goto err1;
	}
	musb->nb.notifier_call = musb_otg_notifications;
	status = otg_register_notifier(musb->xceiv, &musb->nb);

	if (status < 0) {
		dev_dbg(musb->controller, "notification register failed\n");
		goto err1;
	}

	setup_timer(&notify_timer, musb_notify_idle, (unsigned long) musb);

	return 0;
err1:
	pm_runtime_disable(musb->controller);
	return status;
}

/**
 * ux500_musb_exit() - unregister the platform USB driver.
 * @musb: struct musb pointer.
 *
 * This function unregisters the USB controller.
 */
static int ux500_musb_exit(struct musb *musb)
{
	otg_put_transceiver(musb->xceiv);

	return 0;
}

static const struct musb_platform_ops ux500_ops = {
	.init		= ux500_musb_init,
	.exit		= ux500_musb_exit,

	.set_vbus	= ux500_musb_set_vbus,
	.try_idle	= ux500_musb_try_idle,

	.enable		= ux500_musb_enable,
	.configure_endpoints	= ux500_musb_configure_endpoints,
};

/**
 * ux500_probe() - Allocate the resources.
 * @pdev: struct platform_device.
 *
 * This function allocates the required memory for the
 * structures and initialize interrupts.
 */
static int __init ux500_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct platform_device		*musb;
	struct ux500_glue		*glue;
	struct clk			*clk;
	int				ret = -ENOMEM;

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

	musb = platform_device_alloc("musb-hdrc", -1);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err1;
	}

	clk = clk_get(&pdev->dev, "usb");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(clk);
		goto err2;
	}

	wake_lock_init(&ux500_usb_wakelock, WAKE_LOCK_SUSPEND, "ux500-usb");

	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= pdev->dev.dma_mask;
	musb->dev.coherent_dma_mask	= pdev->dev.coherent_dma_mask;

	glue->dev			= &pdev->dev;
	glue->musb			= musb;
	glue->clk			= clk;

	pdata->platform_ops		= &ux500_ops;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err3;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err3;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err3;
	}

	/*
	 * Ensure we suspend (resume) along with the other on-chip devices and
	 * therefore after (before) our external transceiver.
	 */
	ret = device_move(&musb->dev, &pdev->dev, DPM_ORDER_DEV_AFTER_PARENT);
	if (ret) {
		dev_err(&pdev->dev, "failed to alter musb device DPM order\n");
		goto err4;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
err4:
	platform_device_del(musb);
err3:
	if (cpu_is_u5500())
		clk_disable(clk);
	clk_put(clk);

err2:
	platform_device_put(musb);

err1:
	kfree(glue);

err0:
	return ret;
}

static int __exit ux500_remove(struct platform_device *pdev)
{
	struct ux500_glue	*glue = platform_get_drvdata(pdev);

	platform_device_del(glue->musb);
	platform_device_put(glue->musb);
	if (cpu_is_u5500())
		clk_disable(glue->clk);
	clk_put(glue->clk);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	kfree(glue);

	return 0;
}

#ifdef CONFIG_PM
/**
 * ux500_suspend() - Handles the platform suspend.
 * @dev: struct device
 *
 * This function gets triggered when the platform
 * is going to suspend
 */
static int ux500_suspend(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);
	struct musb		*musb = glue_to_musb(glue);

	otg_set_suspend(musb->xceiv, 1);

	if (cpu_is_u5500())
		/*
		 * Since this clock is in the APE domain, it will
		 * automatically be disabled on suspend.
		 * (And enabled on resume automatically.)
		 */
		clk_disable(glue->clk);

	dev_dbg(dev, "ux500_suspend\n");
	return 0;
}

/**
 * ux500_resume() - Handles the platform resume.
 * @dev: struct device
 *
 * This function gets triggered when the platform
 * is going to resume
 */
static int ux500_resume(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);

	if (cpu_is_u5500())
		/* No point in propagating errors on resume */
		(void) clk_enable(glue->clk);
	dev_dbg(dev, "ux500_resume\n");
	return 0;
}
#ifdef CONFIG_UX500_SOC_DB8500
static int ux500_musb_runtime_resume(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);
	int ret;

	if (cpu_is_u5500())
		return 0;

	ret = clk_enable(glue->clk);
	if (ret) {
		dev_dbg(dev, "Unable to enable clk\n");
		return ret;
	}
	dev_dbg(dev, "ux500_musb_runtime_resume\n");
	return 0;
}

static int ux500_musb_runtime_suspend(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);

	if (cpu_is_u5500())
		return 0;

	clk_disable(glue->clk);
	dev_dbg(dev, "ux500_musb_runtime_suspend\n");
	return 0;
}
#endif
static const struct dev_pm_ops ux500_pm_ops = {
#ifdef CONFIG_UX500_SOC_DB8500
	SET_RUNTIME_PM_OPS(ux500_musb_runtime_suspend,
			   ux500_musb_runtime_resume, NULL)
#endif
	.suspend	= ux500_suspend,
	.resume		= ux500_resume,
};

#define DEV_PM_OPS	(&ux500_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static struct platform_driver ux500_driver = {
	.remove		= __exit_p(ux500_remove),
	.driver		= {
		.name	= "musb-ux500",
		.pm	= DEV_PM_OPS,
	},
};

MODULE_DESCRIPTION("UX500 MUSB Glue Layer");
MODULE_AUTHOR("Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>");
MODULE_LICENSE("GPL v2");

static int __init ux500_init(void)
{
	return platform_driver_probe(&ux500_driver, ux500_probe);
}
subsys_initcall(ux500_init);

static void __exit ux500_exit(void)
{
	platform_driver_unregister(&ux500_driver);
}
module_exit(ux500_exit);
