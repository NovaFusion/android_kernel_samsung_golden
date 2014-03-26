/*
 *  linux/include/amba/bus.h
 *
 *  This device type deals with ARM PrimeCells and anything else that
 *  presents a proper CID (0xB105F00D) at the end of the I/O register
 *  region or that is derived from a PrimeCell.
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ASMARM_AMBA_H
#define ASMARM_AMBA_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/resource.h>
#include <linux/regulator/consumer.h>

#define AMBA_NR_IRQS	2
#define AMBA_CID	0xb105f00d

struct clk;

struct amba_device {
	struct device		dev;
	struct resource		res;
	struct clk		*pclk;
	struct regulator	*vcore;
	u64			dma_mask;
	unsigned int		periphid;
	unsigned int		irq[AMBA_NR_IRQS];
};

struct amba_id {
	unsigned int		id;
	unsigned int		mask;
	void			*data;
};

struct amba_driver {
	struct device_driver	drv;
	int			(*probe)(struct amba_device *, const struct amba_id *);
	int			(*remove)(struct amba_device *);
	void			(*shutdown)(struct amba_device *);
	int			(*suspend)(struct amba_device *, pm_message_t);
	int			(*resume)(struct amba_device *);
	const struct amba_id	*id_table;
};

enum amba_vendor {
	AMBA_VENDOR_ARM = 0x41,
	AMBA_VENDOR_ST = 0x80,
};

extern struct bus_type amba_bustype;

#define to_amba_device(d)	container_of(d, struct amba_device, dev)

#define amba_get_drvdata(d)	dev_get_drvdata(&d->dev)
#define amba_set_drvdata(d,p)	dev_set_drvdata(&d->dev, p)

int amba_driver_register(struct amba_driver *);
void amba_driver_unregister(struct amba_driver *);
int amba_device_register(struct amba_device *, struct resource *);
void amba_device_unregister(struct amba_device *);
struct amba_device *amba_find_device(const char *, struct device *, unsigned int, unsigned int);
int amba_request_regions(struct amba_device *, const char *);
void amba_release_regions(struct amba_device *);

#define amba_pclk_enable(d)	\
	(IS_ERR((d)->pclk) ? 0 : clk_enable((d)->pclk))

#define amba_pclk_disable(d)	\
	do { if (!IS_ERR((d)->pclk)) clk_disable((d)->pclk); } while (0)

#define amba_vcore_enable(d)	\
	(IS_ERR((d)->vcore) ? 0 : regulator_enable((d)->vcore))

#define amba_vcore_disable(d)	\
	do { if (!IS_ERR((d)->vcore)) regulator_disable((d)->vcore); } while (0)

/* Some drivers don't use the struct amba_device */
#define AMBA_CONFIG_BITS(a) (((a) >> 24) & 0xff)
#define AMBA_REV_BITS(a) (((a) >> 20) & 0x0f)
#define AMBA_MANF_BITS(a) (((a) >> 12) & 0xff)
#define AMBA_PART_BITS(a) ((a) & 0xfff)

#define amba_config(d)	AMBA_CONFIG_BITS((d)->periphid)
#define amba_rev(d)	AMBA_REV_BITS((d)->periphid)
#define amba_manf(d)	AMBA_MANF_BITS((d)->periphid)
#define amba_part(d)	AMBA_PART_BITS((d)->periphid)

#ifdef CONFIG_PM_SLEEP
extern int amba_pm_prepare(struct device *dev);
extern void amba_pm_complete(struct device *dev);
#else
#define amba_pm_prepare	NULL
#define amba_pm_complete	NULL
#endif

#ifdef CONFIG_SUSPEND
extern int amba_pm_suspend(struct device *dev);
extern int amba_pm_suspend_noirq(struct device *dev);
extern int amba_pm_resume(struct device *dev);
extern int amba_pm_resume_noirq(struct device *dev);
#else
#define amba_pm_suspend		NULL
#define amba_pm_resume		NULL
#define amba_pm_suspend_noirq	NULL
#define amba_pm_resume_noirq	NULL
#endif

#ifdef CONFIG_HIBERNATE_CALLBACKS
extern int amba_pm_freeze(struct device *dev);
extern int amba_pm_freeze_noirq(struct device *dev);
extern int amba_pm_thaw(struct device *dev);
extern int amba_pm_thaw_noirq(struct device *dev);
extern int amba_pm_poweroff(struct device *dev);
extern int amba_pm_poweroff_noirq(struct device *dev);
extern int amba_pm_restore(struct device *dev);
extern int amba_pm_restore_noirq(struct device *dev);
#else
#define amba_pm_freeze		NULL
#define amba_pm_thaw		NULL
#define amba_pm_poweroff		NULL
#define amba_pm_restore		NULL
#define amba_pm_freeze_noirq	NULL
#define amba_pm_thaw_noirq		NULL
#define amba_pm_poweroff_noirq	NULL
#define amba_pm_restore_noirq	NULL
#endif

#ifdef CONFIG_PM_SLEEP
#define USE_AMBA_PM_SLEEP_OPS \
	.prepare = amba_pm_prepare, \
	.complete = amba_pm_complete, \
	.suspend = amba_pm_suspend, \
	.resume = amba_pm_resume, \
	.freeze = amba_pm_freeze, \
	.thaw = amba_pm_thaw, \
	.poweroff = amba_pm_poweroff, \
	.restore = amba_pm_restore, \
	.suspend_noirq = amba_pm_suspend_noirq, \
	.resume_noirq = amba_pm_resume_noirq, \
	.freeze_noirq = amba_pm_freeze_noirq, \
	.thaw_noirq = amba_pm_thaw_noirq, \
	.poweroff_noirq = amba_pm_poweroff_noirq, \
	.restore_noirq = amba_pm_restore_noirq,
#else
#define USE_AMBA_PM_SLEEP_OPS
#endif

#endif
