/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Maxime Coquelin <maxime.coquelin-nonst@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */
#ifndef __SYS_SOC_H
#define __SYS_SOC_H

#include <linux/kobject.h>

/**
 * struct sys_soc_info - SoC exports related informations
 * @name: name of the export
 * @info: pointer on the key to export
 * @get_info: callback to retrieve key if info field is NULL
 * @attr: export's sysdev class attribute
 */
struct sysfs_soc_info {
	const char *info;
	ssize_t (*get_info)(char *buf, struct sysfs_soc_info *);
	struct kobj_attribute attr;
};

ssize_t show_soc_info(struct kobject *, struct kobj_attribute *, char *);

#define SYSFS_SOC_ATTR_VALUE(_name, _value) {		\
	.attr.attr.name = _name,			\
	.attr.attr.mode = S_IRUGO,			\
	.attr.show	= show_soc_info,		\
	.info           = _value,			\
}

#define SYSFS_SOC_ATTR_CALLBACK(_name, _callback) {	\
	.attr.attr.name = _name,			\
	.attr.attr.mode = S_IRUGO,			\
	.attr.show	= show_soc_info,		\
	.get_info       = _callback,			\
}

/**
 * register_sys_soc - register the soc information
 * @name: name of the machine
 * @info: pointer on the info table to export
 * @num: number of info to export
 *
 * NOTE: This function must only be called once
 */
int register_sysfs_soc(struct sysfs_soc_info *info, size_t num);

#endif /* __SYS_SOC_H */
