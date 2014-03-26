/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 *
 * Heavily adapted from Regulator framework.
 * Provides mechanisms for registering platform specific access
 * mechanisms for modem.
 * Also, exposes APIs for gettng/releasing the access and even
 * query the access status, and the modem usage status.
 */
#include <linux/modem/modem.h>
#include <linux/modem/modem_client.h>
#include <linux/slab.h>
#include <linux/err.h>

static DEFINE_MUTEX(modem_list_mutex);
static LIST_HEAD(modem_list);

struct modem {
	struct device *dev;
	struct list_head list;
	char *modem_name;
	struct device_attribute dev_attr;
	struct modem_dev *mdev;
	atomic_t use;
};

static const char *mdev_get_name(struct modem_dev *mdev)
{
	if (mdev->desc->name)
		return mdev->desc->name;
	else
		return "";
}

static int _modem_is_requested(struct modem_dev *mdev)
{
	/* If we don't know then assume that the modem is always on */
	if (!mdev->desc->ops->is_requested)
		return 0;

	return mdev->desc->ops->is_requested(mdev);
}

/**
 * modem_is_requested - check if modem access is requested
 * @modem: modem device
 *
 * Checks whether modem is accessed or not by querying
 * the underlying platform specific modem access
 * implementation.
 */
int modem_is_requested(struct modem *modem)
{
	int ret;

	mutex_lock(&modem->mdev->mutex);
	ret = _modem_is_requested(modem->mdev);
	mutex_unlock(&modem->mdev->mutex);

	return ret;
}
EXPORT_SYMBOL(modem_is_requested);

static int _modem_request(struct modem_dev *mdev)
{
	int ret;

	if (++mdev->use_count == 1) {
		ret = _modem_is_requested(mdev);
		if (ret == 0)
			mdev->desc->ops->request(mdev);
	}

	return 0;
}

/**
 * modem_request - Request access the modem
 * @modem: modem device
 *
 * API to access the modem. It keeps a client
 * specific check on whether the particular modem
 * requested is accessed or not.
 */
void modem_request(struct modem *modem)
{
	struct modem_dev *mdev = modem->mdev;
	int ret = 0;


	mutex_lock(&mdev->mutex);
	if (atomic_read(&modem->use) == 1) {
		mutex_unlock(&mdev->mutex);
		return;
	}
	ret = _modem_request(mdev);
	if (ret == 0)
		atomic_set(&modem->use, 1);
	mutex_unlock(&mdev->mutex);
}
EXPORT_SYMBOL(modem_request);

static int _modem_release(struct modem_dev *mdev)
{
	if (WARN(mdev->use_count <= 0,
				"unbalanced releases for %s\n",
				mdev_get_name(mdev)))
		return -EIO;

	if (--mdev->use_count == 0)
		mdev->desc->ops->release(mdev);

	return 0;
}

/**
 * modem_release - Release access to modem
 * @modem: modem device
 *
 * Releases accesss to the modem. It keeps a client
 * specific check on whether a particular modem
 * is released or not.
 */
void modem_release(struct modem *modem)
{
	struct modem_dev *mdev = modem->mdev;
	int ret = 0;

	mutex_lock(&mdev->mutex);
	if (atomic_read(&modem->use) == 0) {
		mutex_unlock(&mdev->mutex);
		return;
	}
	ret = _modem_release(mdev);
	if (ret == 0)
		atomic_set(&modem->use, 0);
	mutex_unlock(&mdev->mutex);
}
EXPORT_SYMBOL(modem_release);

/**
 * modem_get_usage - Check if particular client is using modem
 * @modem: modem device
 *
 * Checks whether the particular client is using access to modem.
 * This API could be used by client drivers in making their
 * suspend decisions.
 */
int modem_get_usage(struct modem *modem)
{
	return atomic_read(&modem->use);
}
EXPORT_SYMBOL(modem_get_usage);

static struct modem *create_modem(struct modem_dev *mdev,
		struct device *dev,
		const char *id)
{
	struct modem *modem;

	modem = kzalloc(sizeof(*modem), GFP_KERNEL);
	if (modem == NULL)
		return NULL;

	mutex_lock(&mdev->mutex);
	modem->mdev = mdev;
	modem->dev = dev;
	list_add(&modem->list, &mdev->client_list);

	mutex_unlock(&mdev->mutex);
	return modem;

}

static struct modem *_modem_get(struct device *dev, const char *id,
		int exclusive)
{
	struct modem_dev *mdev_ptr;
	struct modem *modem = ERR_PTR(-ENODEV);
	int ret;

	if (id == NULL) {
		pr_err("modem_get with no identifier\n");
		return modem;
	}

	mutex_lock(&modem_list_mutex);
	list_for_each_entry(mdev_ptr, &modem_list, modem_list) {
		if (strcmp(mdev_get_name(mdev_ptr), id) == 0)
			goto found;
	}

	goto out;

found:
	if (!try_module_get(mdev_ptr->owner))
		goto out;

	modem = create_modem(mdev_ptr, dev, id);
	if (modem == NULL) {
		modem = ERR_PTR(-ENOMEM);
		module_put(mdev_ptr->owner);
	}

	mdev_ptr->open_count++;
	ret = _modem_is_requested(mdev_ptr);
	if (ret)
		mdev_ptr->use_count = 1;
	else
		mdev_ptr->use_count = 0;

out:
	mutex_unlock(&modem_list_mutex);
	return modem;

}

/**
 * modem_get - Get reference to a particular platform specific modem
 * @dev: device
 * @id: modem device name
 *
 * Get reference to a particular modem device.
 */
struct modem *modem_get(struct device *dev, const char *id)
{
	return _modem_get(dev, id, 0);
}
EXPORT_SYMBOL(modem_get);

/**
 * modem_put - Release reference to a modem device
 * @modem: modem device
 *
 * Release reference to a modem device.
 */
void modem_put(struct modem *modem)
{
	struct modem_dev *mdev;

	if (modem == NULL || IS_ERR(modem))
		return;

	mutex_lock(&modem_list_mutex);
	mdev = modem->mdev;

	list_del(&modem->list);
	kfree(modem);

	mdev->open_count--;

	module_put(mdev->owner);
	mutex_unlock(&modem_list_mutex);
}
EXPORT_SYMBOL(modem_put);

static ssize_t modem_print_state(char *buf, int state)
{
	if (state > 0)
		return sprintf(buf, "accessed\n");
	else if (state == 0)
		return sprintf(buf, "released\n");
	else
		return sprintf(buf, "unknown\n");
}

static ssize_t modem_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct modem_dev *mdev = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&mdev->mutex);
	ret = modem_print_state(buf, _modem_is_requested(mdev));
	mutex_unlock(&mdev->mutex);

	return ret;
}
static DEVICE_ATTR(state, 0444, modem_state_show, NULL);

static ssize_t modem_use_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct modem_dev *mdev = dev_get_drvdata(dev);
	struct modem *mod;
	size_t size = 0;

	list_for_each_entry(mod, &mdev->client_list, list) {
		if (mod->dev != NULL)
			size += sprintf((buf + size), "%s (%d)\n",
				dev_name(mod->dev), atomic_read(&mod->use));
		else
			size += sprintf((buf + size), "unknown (%d)\n",
				atomic_read(&mod->use));
	}
	size += sprintf((buf + size), "\n");

	return size;
}
static DEVICE_ATTR(use, 0444, modem_use_show, NULL);

static ssize_t modem_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct modem_dev *mdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", mdev_get_name(mdev));
}
static DEVICE_ATTR(name, 0444, modem_name_show, NULL);

static ssize_t modem_num_active_users_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct modem_dev *mdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", mdev->use_count);
}
static DEVICE_ATTR(num_active_users, 0444, modem_num_active_users_show, NULL);

static int add_modem_attributes(struct modem_dev *mdev)
{
	struct device      *dev = &mdev->dev;
	struct modem_ops   *ops = mdev->desc->ops;
	int                status = 0;

	status = device_create_file(dev, &dev_attr_use);
	if (status < 0)
		return status;

	status = device_create_file(dev, &dev_attr_name);
	if (status < 0)
		return status;

	status = device_create_file(dev, &dev_attr_num_active_users);
	if (status < 0)
		return status;

	if (ops->is_requested) {
		status = device_create_file(dev, &dev_attr_state);
		if (status < 0)
			return status;
	}

	return 0;
}

/**
 * modem_register - register a modem
 * @modem_desc: - description for modem
 * @dev:        - device
 * @driver_data:- driver specific data
 *
 * Register a modem with the modem access framework, so that
 * it could be used by client drivers for accessing the
 * modem.
 */
struct modem_dev *modem_register(struct modem_desc *modem_desc,
		struct device *dev,
		void *driver_data)
{
	static atomic_t modem_no = ATOMIC_INIT(0);
	struct modem_dev *mdev;
	int ret;

	if (modem_desc == NULL)
		return ERR_PTR(-EINVAL);

	if (modem_desc->name == NULL || modem_desc->ops == NULL)
		return ERR_PTR(-EINVAL);

	mdev = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if (mdev == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&modem_list_mutex);

	mutex_init(&mdev->mutex);
	mdev->modem_data = driver_data;
	mdev->owner = modem_desc->owner;
	mdev->desc = modem_desc;
	INIT_LIST_HEAD(&mdev->client_list);
	INIT_LIST_HEAD(&mdev->modem_list);
	BLOCKING_INIT_NOTIFIER_HEAD(&mdev->notifier);

	/* mdev->dev.class = &modem_class;*/
	mdev->dev.parent = dev;
	dev_set_name(&mdev->dev, "modem.%d", atomic_inc_return(&modem_no) - 1);
	ret = device_register(&mdev->dev);
	if (ret != 0)
		goto clean;

	dev_set_drvdata(&mdev->dev, mdev);

	ret = add_modem_attributes(mdev);
	if (ret < 0)
		goto backoff;

	list_add(&mdev->modem_list, &modem_list);

out:
	mutex_unlock(&modem_list_mutex);
	return mdev;

backoff:
	device_unregister(&mdev->dev);
	mdev = ERR_PTR(ret);
	goto out;

clean:
	kfree(mdev);
	mdev = ERR_PTR(ret);
	goto out;
}
EXPORT_SYMBOL(modem_register);
