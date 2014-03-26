/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors: Jonas Aaberg <jonas.aberg@stericsson.com>
 *          Paer-Olof Haakansson <par-olof.hakansson@stericsson.com>
 * for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

static ssize_t db5500_mloader_sysfs_addr(struct device *dev,
				  struct device_attribute *attr,
				  char *buf);

static ssize_t db5500_mloader_sysfs_finalize(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count);

static ssize_t db5500_mloader_sysfs_itpmode(struct device *dev,
				  struct device_attribute *attr,
				  char *buf);

static DEVICE_ATTR(addr, S_IRUSR|S_IRGRP, db5500_mloader_sysfs_addr, NULL);
static DEVICE_ATTR(finalize, S_IWUSR, NULL, db5500_mloader_sysfs_finalize);
static DEVICE_ATTR(is_itpmode, S_IRUSR|S_IRGRP, db5500_mloader_sysfs_itpmode, NULL);

static unsigned int db5500_bootargs_memmap_modem_start;
static unsigned int db5500_bootargs_memmap_modem_total_size;
static unsigned int db5500_mloader_itpmode;
static unsigned int db5500_mloader_shm_total_size;
module_param_named(shm_total_size, db5500_mloader_shm_total_size, uint, 0600);
MODULE_PARM_DESC(shm_total_size, "Total Size of SHM shared memory");

static int __init db5500_bootargs_modem_memmap(char *p)
{
	db5500_bootargs_memmap_modem_total_size = memparse(p, &p);
	if (*p == '@')
		db5500_bootargs_memmap_modem_start = memparse(p + 1, &p);

	return 0;
}
early_param("mem_modem", db5500_bootargs_modem_memmap);

static int __init db5500_bootargs_shm_total_size(char *str)
{
	int ret;
	ret = strict_strtoul(str, 0, &db5500_mloader_shm_total_size);
	if (ret < 0)
		return -EINVAL;
	return 1;
}
early_param("mloader.shm_total_size", db5500_bootargs_shm_total_size);

static int __init db5500_bootargs_itpmode(char *p)
{
	int ret;
	int count = 3;
	if (!memcmp(p, "itp", count))
		db5500_mloader_itpmode = true;
	else
		db5500_mloader_itpmode = false;
	return 1;
}
early_param("modem_boot_type", db5500_bootargs_itpmode);

static int __exit db5500_mloader_remove(struct platform_device *pdev)
{
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_addr.attr);
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_finalize.attr);
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_is_itpmode.attr);

	return 0;
}


static struct platform_driver db5500_mloader_driver = {
	.driver = {
		.name = "db5500_mloader",
	},
	.remove = __exit_p(db5500_mloader_remove),
};

struct db5500_mloader {
	struct work_struct work;
	struct platform_device *pdev;
};

static void db5500_mloader_clean_up(struct work_struct *work)
{
	struct db5500_mloader *m = container_of(work,
						struct db5500_mloader,
						work);

	/* Remove this module */
	platform_device_unregister(m->pdev);

	platform_driver_unregister(&db5500_mloader_driver);
	kfree(m);

}

static ssize_t db5500_mloader_sysfs_addr(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	return sprintf(buf, "0x%x 0x%x 0x%x\n",
		       db5500_bootargs_memmap_modem_start,
		       db5500_bootargs_memmap_modem_total_size,
		       db5500_mloader_shm_total_size);
}

static ssize_t db5500_mloader_sysfs_itpmode(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	return sprintf(buf, "0x%x\n",
		       db5500_mloader_itpmode);
}

static ssize_t db5500_mloader_sysfs_finalize(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct db5500_mloader *m;

	m = kmalloc(sizeof(struct db5500_mloader), GFP_KERNEL);

	m->pdev = container_of(dev,
			       struct platform_device,
			       dev);

	INIT_WORK(&m->work, db5500_mloader_clean_up);

	/* The module can not remove itself while being in a sysfs function,
	 * it has to use a workqueue.
	 */
	schedule_work(&m->work);

	return count;
}

static void db5500_mloader_release(struct device *dev)
{
	/* Nothing to release */
}

static int __init db5500_mloader_probe(struct platform_device *pdev)
{
	int ret = 0;

	pdev->dev.release = db5500_mloader_release;

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_addr.attr);
	if (ret)
		return ret;
	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_finalize.attr);

	if (ret) {
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_addr.attr);
		return ret;
	}
	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_is_itpmode.attr);
	if (ret) {
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_finalize.attr);
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_addr.attr);
		return ret;
	}
	return 0;

}

static int __init db5500_mloader_init(void)
{
/*
 * mloader for Fairbanks. It exports the physical
 * address where the modem side ELF should be located in a sysfs
 * file to make it available for a user space utility.
 * When the mLoader utility has picked up these settings, this module is no
 * longer needed and can be removed by writing to sysfs finalize.
 *
 * The modem side should be loaded via mmap'ed /dev/mem
 *
 */

	return platform_driver_probe(&db5500_mloader_driver,
			db5500_mloader_probe);
}
module_init(db5500_mloader_init);


static void __exit mloader_exit(void)
{
	platform_driver_unregister(&db5500_mloader_driver);
}
module_exit(mloader_exit);

MODULE_AUTHOR("Jonas Aaberg <jonas.aberg@stericsson.com>");
MODULE_LICENSE("GPL");
