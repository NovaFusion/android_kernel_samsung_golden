/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Maxime Coquelin <maxime.coquelin-nonst@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

struct kobject *soc_object;

ssize_t show_soc_info(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct sysfs_soc_info *si = container_of(attr,
				struct sysfs_soc_info, attr);

	if (si->info)
		return sprintf(buf, "%s\n", si->info);

	return si->get_info(buf, si);
}

int __init register_sysfs_soc_info(struct sysfs_soc_info *info, int nb_info)
{
	int i, ret;

	for (i = 0; i < nb_info; i++) {
		ret = sysfs_create_file(soc_object, &info[i].attr.attr);
		if (ret) {
			for (i -= 1; i >= 0; i--)
				sysfs_remove_file(soc_object, &info[i].attr.attr);
			break;
		}
	}

	return ret;
}

static struct attribute *soc_attrs[] = {
	NULL,
};

static struct attribute_group soc_attr_group = {
	.attrs = soc_attrs,
};

int __init register_sysfs_soc(struct sysfs_soc_info *info, size_t num)
{
	int ret;

	soc_object = kobject_create_and_add("socinfo", NULL);
	if (!soc_object) {
		ret = -ENOMEM;
		goto exit;
	}

	ret = sysfs_create_group(soc_object, &soc_attr_group);
	if (ret)
		goto kset_exit;

	ret = register_sysfs_soc_info(info, num);
	if (ret)
		goto group_exit;

	return 0;

group_exit:
	sysfs_remove_group(soc_object, &soc_attr_group);
kset_exit:
	kobject_put(soc_object);
exit:
	return ret;
}

