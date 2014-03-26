/*
 * Mac80211 SDIO driver for ST-Ericsson CW1200 device
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/spinlock.h>
#include <asm/mach-types.h>
#include <net/mac80211.h>

#include "cw1200.h"
#include "sbus.h"
#include "cw1200_plat.h"

MODULE_AUTHOR("Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>");
MODULE_DESCRIPTION("mac80211 ST-Ericsson CW1200 SDIO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cw1200_wlan");

struct sbus_priv {
	struct sdio_func	*func;
	struct cw1200_common	*core;
	const struct cw1200_platform_data *pdata;
	spinlock_t		lock;
	sbus_irq_handler	irq_handler;
	void			*irq_priv;
};

static const struct sdio_device_id if_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_ANY_ID, SDIO_ANY_ID) },
	{ /* end: all zeroes */			},
};

/* sbus_ops implemetation */

static int cw1200_sdio_memcpy_fromio(struct sbus_priv *self,
				     unsigned int addr,
				     void *dst, int count)
{
	int ret = sdio_memcpy_fromio(self->func, dst, addr, count);
	if (ret) {
		printk(KERN_ERR "!!! Can't read %d bytes from 0x%.8X. Err %d.\n",
				count, addr, ret);
	}
	return ret;
}

static int cw1200_sdio_memcpy_toio(struct sbus_priv *self,
				   unsigned int addr,
				   const void *src, int count)
{
	return sdio_memcpy_toio(self->func, addr, (void *)src, count);
}

static void cw1200_sdio_lock(struct sbus_priv *self)
{
	sdio_claim_host(self->func);
}

static void cw1200_sdio_unlock(struct sbus_priv *self)
{
	sdio_release_host(self->func);
}

#ifndef CONFIG_CW1200_USE_GPIO_IRQ
static void cw1200_sdio_irq_handler(struct sdio_func *func)
{
	struct sbus_priv *self = sdio_get_drvdata(func);
	unsigned long flags;

	BUG_ON(!self);
	spin_lock_irqsave(&self->lock, flags);
	if (self->irq_handler)
		self->irq_handler(self->irq_priv);
	spin_unlock_irqrestore(&self->lock, flags);
}
#else /* CONFIG_CW1200_USE_GPIO_IRQ */
static irqreturn_t cw1200_gpio_irq_handler(int irq, void *dev_id)
{
	struct sbus_priv *self = dev_id;

	BUG_ON(!self);
	if (self->irq_handler)
		self->irq_handler(self->irq_priv);
	return IRQ_HANDLED;
}

static int cw1200_request_irq(struct sbus_priv *self,
			      irq_handler_t handler)
{
	int ret;
	int func_num;
	const struct resource *irq = self->pdata->irq;
	u8 cccr;

	ret = request_any_context_irq(irq->start, handler,
			IRQF_TRIGGER_RISING, irq->name, self);
	if (WARN_ON(ret < 0))
		goto exit;

	ret = enable_irq_wake(irq->start);
	if (WARN_ON(ret))
		goto free_irq;

	/* Hack to access Fuction-0 */
	func_num = self->func->num;
	self->func->num = 0;

	cccr = sdio_readb(self->func, SDIO_CCCR_IENx, &ret);
	if (WARN_ON(ret))
		goto set_func;

	/* Master interrupt enable ... */
	cccr |= 1;

	/* ... for our function */
	cccr |= 1 << func_num;

	sdio_writeb(self->func, cccr, SDIO_CCCR_IENx, &ret);
	if (WARN_ON(ret))
		goto set_func;

	/* Restore the WLAN function number */
	self->func->num = func_num;
	return 0;

set_func:
	self->func->num = func_num;
	disable_irq_wake(irq->start);
free_irq:
	free_irq(irq->start, self);
exit:
	return ret;
}
#endif /* CONFIG_CW1200_USE_GPIO_IRQ */

static int cw1200_sdio_irq_subscribe(struct sbus_priv *self,
				     sbus_irq_handler handler,
				     void *priv)
{
	int ret;
	unsigned long flags;

	if (!handler)
		return -EINVAL;

	spin_lock_irqsave(&self->lock, flags);
	self->irq_priv = priv;
	self->irq_handler = handler;
	spin_unlock_irqrestore(&self->lock, flags);

	printk(KERN_DEBUG "SW IRQ subscribe\n");
	sdio_claim_host(self->func);
#ifndef CONFIG_CW1200_USE_GPIO_IRQ
	ret = sdio_claim_irq(self->func, cw1200_sdio_irq_handler);
#else
	ret = cw1200_request_irq(self, cw1200_gpio_irq_handler);
#endif
	sdio_release_host(self->func);
	return ret;
}

static int cw1200_sdio_irq_unsubscribe(struct sbus_priv *self)
{
	int ret = 0;
	unsigned long flags;
#ifdef CONFIG_CW1200_USE_GPIO_IRQ
	const struct resource *irq = self->pdata->irq;
#endif

	WARN_ON(!self->irq_handler);
	if (!self->irq_handler)
		return 0;

	printk(KERN_DEBUG "SW IRQ unsubscribe\n");
#ifndef CONFIG_CW1200_USE_GPIO_IRQ
	sdio_claim_host(self->func);
	ret = sdio_release_irq(self->func);
	sdio_release_host(self->func);
#else
	disable_irq_wake(irq->start);
	free_irq(irq->start, self);
#endif

	spin_lock_irqsave(&self->lock, flags);
	self->irq_priv = NULL;
	self->irq_handler = NULL;
	spin_unlock_irqrestore(&self->lock, flags);

	return ret;
}

static int cw1200_detect_card(const struct cw1200_platform_data *pdata)
{
	/* HACK!!!
	 * Rely on mmc->class_dev.class set in mmc_alloc_host
	 * Tricky part: a new mmc hook is being (temporary) created
	 * to discover mmc_host class.
	 * Do you know more elegant way how to enumerate mmc_hosts?
	 */

	struct mmc_host *mmc = NULL;
	struct class_dev_iter iter;
	struct device *dev;

	mmc = mmc_alloc_host(0, NULL);
	if (!mmc)
		return -ENOMEM;

	BUG_ON(!mmc->class_dev.class);
	class_dev_iter_init(&iter, mmc->class_dev.class, NULL, NULL);
	for (;;) {
		dev = class_dev_iter_next(&iter);
		if (!dev) {
			printk(KERN_ERR "cw1200: %s is not found.\n",
				pdata->mmc_id);
			break;
		} else {
			struct mmc_host *host = container_of(dev,
				struct mmc_host, class_dev);

			if (dev_name(&host->class_dev) &&
				strcmp(dev_name(&host->class_dev),
					pdata->mmc_id))
				continue;

			mmc_detect_change(host, 10);
			break;
		}
	}
	mmc_free_host(mmc);
	return 0;
}

static int cw1200_sdio_off(const struct cw1200_platform_data *pdata)
{
	const struct resource *reset = pdata->reset;
	gpio_set_value(reset->start, 0);
	cw1200_detect_card(pdata);
	gpio_free(reset->start);
	return 0;
}

static int cw1200_sdio_on(const struct cw1200_platform_data *pdata)
{
	const struct resource *reset = pdata->reset;
	gpio_request(reset->start, reset->name);
	gpio_direction_output(reset->start, 1);
	/* It is not stated in the datasheet, but at least some of devices
	 * have problems with reset if this stage is omited. */
	msleep(50);
	gpio_set_value(reset->start, 0);
	/* A valid reset shall be obtained by maintaining WRESETN
	 * active (low) for at least two cycles of LP_CLK after VDDIO
	 * is stable within it operating range. */
	msleep(1);
	gpio_set_value(reset->start, 1);
	/* The host should wait 30 ms after the WRESETN release
	 * for the on-chip LDO to stabilize */
	msleep(30);
	cw1200_detect_card(pdata);
	return 0;
}

static int cw1200_sdio_reset(struct sbus_priv *self)
{
	cw1200_sdio_off(self->pdata);
	msleep(1000);
	cw1200_sdio_on(self->pdata);
	return 0;
}

static size_t cw1200_align_size(struct sbus_priv *self, size_t size)
{
	size_t aligned = sdio_align_size(self->func, size);
	/* HACK!!! Problems with DMA size on u8500 platform  */
	if ((aligned & 0x1F) && (aligned & ~0x1F)) {
		aligned &= ~0x1F;
		aligned += 0x20;
	}

	return aligned;
}

static struct sbus_ops cw1200_sdio_sbus_ops = {
	.sbus_memcpy_fromio	= cw1200_sdio_memcpy_fromio,
	.sbus_memcpy_toio	= cw1200_sdio_memcpy_toio,
	.lock			= cw1200_sdio_lock,
	.unlock			= cw1200_sdio_unlock,
	.irq_subscribe		= cw1200_sdio_irq_subscribe,
	.irq_unsubscribe	= cw1200_sdio_irq_unsubscribe,
	.reset			= cw1200_sdio_reset,
	.align_size		= cw1200_align_size,
};

/* Probe Function to be called by SDIO stack when device is discovered */
static int cw1200_sdio_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	struct sbus_priv *self;
	int status;

	cw1200_dbg(CW1200_DBG_INIT, "Probe called\n");

	self = kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		cw1200_dbg(CW1200_DBG_ERROR, "Can't allocate SDIO sbus_priv.");
		return -ENOMEM;
	}

	spin_lock_init(&self->lock);
	self->pdata = cw1200_get_platform_data();
	self->func = func;
	sdio_set_drvdata(func, self);
	sdio_claim_host(func);
	sdio_enable_func(func);
	sdio_release_host(func);

	status = cw1200_probe(&cw1200_sdio_sbus_ops,
			      self, &func->dev, &self->core);
	if (status) {
		sdio_claim_host(func);
		sdio_disable_func(func);
		sdio_release_host(func);
		sdio_set_drvdata(func, NULL);
		kfree(self);
	}

	return status;
}

/* Disconnect Function to be called by SDIO stack when device is disconnected */
static void cw1200_sdio_disconnect(struct sdio_func *func)
{
	struct sbus_priv *self = sdio_get_drvdata(func);

	if (self) {
		if (self->core) {
			cw1200_release(self->core);
			self->core = NULL;
		}
		sdio_claim_host(func);
		sdio_disable_func(func);
		sdio_release_host(func);
		sdio_set_drvdata(func, NULL);
		kfree(self);
	}
}

static struct sdio_driver sdio_driver = {
	.name		= "cw1200_wlan",
	.id_table	= if_sdio_ids,
	.probe		= cw1200_sdio_probe,
	.remove		= cw1200_sdio_disconnect,
};

/* Init Module function -> Called by insmod */
static int __init cw1200_sdio_init(void)
{
	const struct cw1200_platform_data *pdata;
	int ret;

	pdata = cw1200_get_platform_data();

	ret = sdio_register_driver(&sdio_driver);
	if (ret)
		goto err_reg;

	if (pdata->power_ctrl) {
		ret = pdata->power_ctrl(pdata, true);
		if (ret)
			goto err_power;
	}

	ret = cw1200_sdio_on(pdata);
	if (ret)
		goto err_on;

	return 0;

err_on:
	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
err_power:
	sdio_unregister_driver(&sdio_driver);
err_reg:
	return ret;
}

/* Called at Driver Unloading */
static void __exit cw1200_sdio_exit(void)
{
	const struct cw1200_platform_data *pdata;
	pdata = cw1200_get_platform_data();
	sdio_unregister_driver(&sdio_driver);
	cw1200_sdio_off(pdata);
	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
}


module_init(cw1200_sdio_init);
module_exit(cw1200_sdio_exit);
