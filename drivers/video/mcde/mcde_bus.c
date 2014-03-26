/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson MCDE display bus driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/notifier.h>

#include <video/mcde_display.h>
#include <video/mcde_dss.h>

#define to_mcde_display_driver(__drv) \
	container_of((__drv), struct mcde_display_driver, driver)

static BLOCKING_NOTIFIER_HEAD(bus_notifier_list);

static int mcde_drv_suspend(struct device *_dev, pm_message_t state);
static int mcde_drv_resume(struct device *_dev);
struct bus_type mcde_bus_type;

static int mcde_suspend_device(struct device *dev, void *data)
{
	pm_message_t* state = (pm_message_t *) data;
	if (dev->driver && dev->driver->suspend)
		return dev->driver->suspend(dev, *state);
	return 0;
}

static int mcde_resume_device(struct device *dev, void *data)
{
	if (dev->driver && dev->driver->resume)
		return dev->driver->resume(dev);
	return 0;
}

/* Bus driver */

static int mcde_bus_match(struct device *_dev, struct device_driver *driver)
{
	pr_debug("Matching device %s with driver %s\n",
		dev_name(_dev), driver->name);

	return strncmp(dev_name(_dev), driver->name, strlen(driver->name)) == 0;
}

static int mcde_bus_suspend(struct device *_dev, pm_message_t state)
{
	int ret;
	ret = bus_for_each_dev(&mcde_bus_type, NULL, &state,
				mcde_suspend_device);
	if (ret) {
		/* TODO Resume all suspended devices */
		/* mcde_bus_resume(dev); */
		return ret;
	}
	return 0;
}

static int mcde_bus_resume(struct device *_dev)
{
	return bus_for_each_dev(&mcde_bus_type, NULL, NULL, mcde_resume_device);
}

struct bus_type mcde_bus_type = {
	.name = "mcde_bus",
	.match = mcde_bus_match,
	.suspend = mcde_bus_suspend,
	.resume = mcde_bus_resume,
};

static int mcde_drv_probe(struct device *_dev)
{
	struct mcde_display_driver *drv = to_mcde_display_driver(_dev->driver);
	struct mcde_display_device *dev = to_mcde_display_device(_dev);

	return drv->probe(dev);
}

static int mcde_drv_remove(struct device *_dev)
{
	struct mcde_display_driver *drv = to_mcde_display_driver(_dev->driver);
	struct mcde_display_device *dev = to_mcde_display_device(_dev);

	return drv->remove(dev);
}

static void mcde_drv_shutdown(struct device *_dev)
{
	struct mcde_display_driver *drv = to_mcde_display_driver(_dev->driver);
	struct mcde_display_device *dev = to_mcde_display_device(_dev);

	drv->shutdown(dev);
}

static int mcde_drv_suspend(struct device *_dev, pm_message_t state)
{
	struct mcde_display_driver *drv = to_mcde_display_driver(_dev->driver);
	struct mcde_display_device *dev = to_mcde_display_device(_dev);

	if (drv->suspend)
		return drv->suspend(dev, state);
	else
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
		return dev->set_power_mode(dev, MCDE_DISPLAY_PM_OFF);
#else
		return 0;
#endif
}

static int mcde_drv_resume(struct device *_dev)
{
	struct mcde_display_driver *drv = to_mcde_display_driver(_dev->driver);
	struct mcde_display_device *dev = to_mcde_display_device(_dev);

	if (drv->resume)
		return drv->resume(dev);
	else
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
		return dev->set_power_mode(dev, MCDE_DISPLAY_PM_STANDBY);
#else
		return 0;
#endif
}

/* Bus device */

static void mcde_bus_release(struct device *dev)
{
}

struct device mcde_bus = {
	.init_name = "mcde_bus",
	.release  = mcde_bus_release
};

/* Public bus API */

int mcde_display_driver_register(struct mcde_display_driver *drv)
{
	drv->driver.bus = &mcde_bus_type;
	if (drv->probe)
		drv->driver.probe = mcde_drv_probe;
	if (drv->remove)
		drv->driver.remove = mcde_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = mcde_drv_shutdown;
	drv->driver.suspend = mcde_drv_suspend;
	drv->driver.resume = mcde_drv_resume;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(mcde_display_driver_register);

void mcde_display_driver_unregister(struct mcde_display_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(mcde_display_driver_unregister);

static void mcde_display_dev_release(struct device *dev)
{
	/* Do nothing */
}

int mcde_display_device_register(struct mcde_display_device *dev)
{
	/* Setup device */
	if (!dev)
		return -EINVAL;
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dev.bus = &mcde_bus_type;
	if (dev->dev.parent != NULL)
		dev->dev.parent = &mcde_bus;
	dev->dev.release = mcde_display_dev_release;
	if (dev->id != -1)
		dev_set_name(&dev->dev, "%s.%d", dev->name,  dev->id);
	else
		dev_set_name(&dev->dev, dev->name);

	mcde_display_init_device(dev);

	return device_register(&dev->dev);
}
EXPORT_SYMBOL(mcde_display_device_register);

void mcde_display_device_unregister(struct mcde_display_device *dev)
{
	device_unregister(&dev->dev);
}
EXPORT_SYMBOL(mcde_display_device_unregister);

/* Notifications */
int mcde_dss_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&bus_notifier_list, nb);
}
EXPORT_SYMBOL(mcde_dss_register_notifier);

int mcde_dss_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&bus_notifier_list, nb);
}
EXPORT_SYMBOL(mcde_dss_unregister_notifier);

static int bus_notify_callback(struct notifier_block *nb,
	unsigned long event, void *dev)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);

	if (event == BUS_NOTIFY_BOUND_DRIVER) {
		ddev->initialized = true;
		blocking_notifier_call_chain(&bus_notifier_list,
			MCDE_DSS_EVENT_DISPLAY_REGISTERED, ddev);
	} else if (event == BUS_NOTIFY_UNBIND_DRIVER) {
		ddev->initialized = false;
		blocking_notifier_call_chain(&bus_notifier_list,
			MCDE_DSS_EVENT_DISPLAY_UNREGISTERED, ddev);
	}
	return 0;
}

struct notifier_block bus_nb = {
	.notifier_call = bus_notify_callback,
};

/* Driver init/exit */

int __init mcde_display_init(void)
{
	int ret;

	ret = bus_register(&mcde_bus_type);
	if (ret) {
		pr_warning("Unable to register bus type\n");
		goto no_bus_registration;
	}
	ret = device_register(&mcde_bus);
	if (ret) {
		pr_warning("Unable to register bus device\n");
		goto no_device_registration;
	}
	ret = bus_register_notifier(&mcde_bus_type, &bus_nb);
	if (ret) {
		pr_warning("Unable to register bus notifier\n");
		goto no_bus_notifier;
	}

	goto out;

no_bus_notifier:
	device_unregister(&mcde_bus);
no_device_registration:
	bus_unregister(&mcde_bus_type);
no_bus_registration:
out:
	return ret;
}

void mcde_display_exit(void)
{
	bus_unregister_notifier(&mcde_bus_type, &bus_nb);
	device_unregister(&mcde_bus);
	bus_unregister(&mcde_bus_type);
}
