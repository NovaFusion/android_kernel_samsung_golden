/**
 * arch/arm/mach-omap2/sec_common.c
 *
 * Copyright (C) 2010-2011, Samsung Electronics, Co., Ltd. All Rights Reserved.
 *  Written by System S/W Group, Open OS S/W R&D Team,
 *  Mobile Communication Division.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * Project Name : OMAP-Samsung Linux Kernel for Android
 *
 * Project Description :
 *
 * Comments : tabstop = 8, shiftwidth = 8, noexpandtab
 */

/**
 * File Name : sec_common.c
 *
 * File Description :
 *
 * Author : System Platform 2
 * Dept : System S/W Group (Open OS S/W R&D Team)
 * Created : 11/Mar/2011
 * Version : Baby-Raccoon
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/err.h>
#include <linux/device.h>
#include <mach/hardware.h>
#include <mach/id.h>
#include <mach/io.h>

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)

#include <plat/io.h>
#include <plat/system.h>

#include "mux.h"
#include "omap_muxtbl.h"

#include "sec_common.h"
#include "sec_param.h"
#elif defined (CONFIG_MACH_SAMSUNG_U9540)
#include <mach/sec_common.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/thread_notify.h>
#include <asm/stacktrace.h>
#include <asm/mach/time.h>
#include <mach/sec_param.h>
#include <mach/system.h>
#include <mach/board-sec-ux500.h>
#include <asm/io.h>
#elif defined(CONFIG_MACH_SAMSUNG_UX500)
#include <mach/sec_common.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/thread_notify.h>
#include <asm/stacktrace.h>
#include <asm/mach/time.h>
#include <asm/io.h>
#include <mach/sec_common.h>
#include <mach/sec_param.h>
#include <mach/sec_log_buf.h>
#include <mach/board-sec-ux500.h>
#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_PMIC
#include <mach/sec_pmic.h>
#endif
#ifdef CONFIG_SAMSUNG_PANIC_LCD_DISPLAY
#include "sec_debug/khb_main.h"
#endif
#if defined(CONFIG_SAMSUNG_ADD_GAFORENSICINFO)
#include <mach/sec_gaf.h>
#include <linux/sched.h>
#endif
#endif

#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>


#ifdef CONFIG_SAMSUNG_LOG_BUF

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/time.h>

/* L1 & L2 cache management */
#include <asm/cacheflush.h>

#if defined(CONFIG_MACH_SAMSUNG_UX500)
void ux500_clean_l2_cache_all(void);
#endif
static DEFINE_SPINLOCK(sec_common_input_log_event_lock);

#define SEC_KEY_LOG_SIZE_SHIFT 4 /*16*/
#define SEC_KEY_LOG_SIZE (1 << SEC_KEY_LOG_SIZE_SHIFT)

#define SEC_TOUCH_LOG_SIZE_SHIFT 5 /*32*/
#define SEC_TOUCH_LOG_SIZE (1 << SEC_TOUCH_LOG_SIZE_SHIFT)

#define SEC_TOUCH_LOG_TIMESTAMP_MS (1000000)
#define SEC_TOUCH_LOG_INTERVAL 200 /*ms*/

#define SEC_MOUNT_PATH_SIZE 32
#define SEC_MOUNT_LOG_SIZE 10

struct sec_key_log_entry {
	unsigned int event_type;
	unsigned int event_code;
	int value;
	unsigned long long timestamp;
};

struct sec_touch_info {
	int16_t x;
	int16_t y;
	int16_t width;
	int16_t touch;
	int16_t id;
	unsigned long long timestamp;
};

static struct sec_key_log_entry sec_key_log[SEC_KEY_LOG_SIZE];
static int sec_key_log_idx;

static struct sec_touch_info sec_touch_log[SEC_TOUCH_LOG_SIZE];
static int sec_touch_log_idx;

static const struct input_device_id sec_common_input_log_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_ABS) },
	},
	{ },    /* Terminating entry */
};

#include <linux/cpufreq.h>
#include <linux/notifier.h>

static int sec_cpufreq_notifier(struct notifier_block *,
					unsigned long, void *);
static int sec_policy_notifier(struct notifier_block *,
					unsigned long, void *);

static struct notifier_block sec_cpufreq_notifier_block = {
	.notifier_call = sec_cpufreq_notifier,
};
static struct notifier_block sec_policy_notifier_block = {
	.notifier_call = sec_policy_notifier,
};

static DEFINE_SPINLOCK(sec_cpufreq_log_lock);
#define SEC_CPUFREQ_LOG_MAX 3
static int sec_cpufreq_log_idx = -1;
static struct sec_cpufreq_info
{
	unsigned int		freq;
	unsigned long long	timestamp;
} sec_cpufreq_log[SEC_CPUFREQ_LOG_MAX+1];

#endif
#if defined(CONFIG_ARCH_OMAP3)
#define SEC_REBOOT_MODE_ADDR		(OMAP343X_CTRL_BASE + 0x0918)
#define SEC_REBOOT_FLAG_ADDR		(OMAP343X_CTRL_BASE + 0x09C4)
#define SEC_REBOOT_CMD_ADDR		NULL
#elif defined(CONFIG_ARCH_OMAP4)
#define OMAP_SW_BOOT_CFG_ADDR		0x4A326FF8
#define SEC_REBOOT_MODE_ADDR		(OMAP_SW_BOOT_CFG_ADDR)
#define SEC_REBOOT_FLAG_ADDR		(OMAP_SW_BOOT_CFG_ADDR - 0x04)
/* -0x08/-0x0C are reserved for debug */
#define SEC_REBOOT_CMD_ADDR		(OMAP_SW_BOOT_CFG_ADDR - 0x10)
#elif defined (CONFIG_MACH_SAMSUNG_U9540) || defined (CONFIG_MACH_SAMSUNG_U8500) 
#else
#error "unsupported mach-type for OMAP-Samsung"
#endif



struct class *sec_class;
EXPORT_SYMBOL(sec_class);

struct class *camera_class;
EXPORT_SYMBOL(camera_class);

void (*sec_set_param_value) (int idx, void *value) = NULL;
EXPORT_SYMBOL(sec_set_param_value);

void (*sec_get_param_value) (int idx, void *value) = NULL;
EXPORT_SYMBOL(sec_get_param_value);

u32 sec_bootmode;
EXPORT_SYMBOL(sec_bootmode);

static __init int setup_boot_mode(char *opt)
{
	sec_bootmode = (u32) memparse(opt, &opt);
	return 0;
}

__setup("bootmode=", setup_boot_mode);

u32 sec_lpm_bootmode;
EXPORT_SYMBOL(sec_lpm_bootmode);

static __init int setup_lpm_boot_mode(char *opt)
{
	sec_lpm_bootmode = (u32) memparse(opt, &opt);
	return 0;
}

__setup("lpm_boot=", setup_lpm_boot_mode);

u32 sec_dbug_level;
EXPORT_SYMBOL(sec_dbug_level);

static __init int setup_dbug_level(char *str)
{
	if (get_option(&str, &sec_dbug_level) != 1)
		sec_dbug_level = 0;

	return 0;
}

__setup("androidboot.debug_level=", setup_dbug_level);

u32 set_default_param;
EXPORT_SYMBOL(set_default_param);

static __init int setup_default_param(char *str)
{
	if (get_option(&str, &set_default_param) != 1)
		set_default_param = 0;

	return 0;
}

__setup("set_default_param=", setup_default_param);

/* movinand checksum */
static struct device *sec_checksum;
static unsigned int sec_checksum_pass;
static unsigned int sec_checksum_done;

static __init int setup_checksum_pass(char *str)
{
	if (get_option(&str, &sec_checksum_pass) != 1)
		sec_checksum_pass = 0;

	return 0;
}

__setup("checksum_pass=", setup_checksum_pass);

static __init int setup_checksum_done(char *str)
{
	if (get_option(&str, &sec_checksum_done) != 1)
		sec_checksum_done = 0;

	return 0;
}

__setup("checksum_done=", setup_checksum_done);

static ssize_t checksum_pass_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (u8) sec_checksum_pass);
}

static ssize_t checksum_done_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (u8) sec_checksum_done);
}

struct sec_reboot_code {
	char *cmd;
	int mode;
};

static int __sec_common_reboot_call(struct notifier_block *this,
				    unsigned long code, void *cmd)
{
	int mode = REBOOT_MODE_NONE;
	int temp_mode;
	int default_switchsel = 1;
	size_t i, n;
	unsigned long value;
	int debug_level;
	struct sec_reboot_code reboot_tbl[] = {
		{"arm11_fota", REBOOT_MODE_ARM11_FOTA},
		{"arm9_fota", REBOOT_MODE_ARM9_FOTA},
		{"recovery", REBOOT_MODE_RECOVERY},
		{"cp_crash", REBOOT_MODE_CP_CRASH},
		{"download", REBOOT_MODE_DOWNLOAD},
		{"prerecovery_done", REBOOT_MODE_RECOVERY},
		{"prerecovery", REBOOT_MODE_PRERECOVERY},
	};

	printk(KERN_INFO "%s: code: 0x%lx, cmd: %s\n", __func__, code,
	       (cmd) ? (char *)cmd : "none");

	if ((code == SYS_RESTART) && cmd) {
		n = ARRAY_SIZE(reboot_tbl);
		for (i = 0; i < n; i++) {
			if (!strcmp((char *)cmd, reboot_tbl[i].cmd)) {
				if(!strcmp((char *)cmd, "recovery")) {
					u8 prerecovery_state = 0;
					printk(KERN_INFO "%s: clear prerecovery flag=%d\n", __func__,
					       prerecovery_state);
					sec_set_param_value(__FORCE_PRERECOVERY, &prerecovery_state);
				}
				mode = reboot_tbl[i].mode;
				break;
			} else if(!strncmp(cmd, "sud", 3)) {
				mode = REBOOT_MODE_DOWNLOAD;
				break;
			} else if (!strncmp(cmd, "debug", 5)
				&& !kstrtoul(cmd + 5, 0, &value)) {
				mode = REBOOT_MODE_NONE;

				switch ((int)value) {
				case DEBUG_LEVEL_LOW:
					debug_level = 0;
					break;
				case DEBUG_LEVEL_MID:
					debug_level = 1;
					break;
				case DEBUG_LEVEL_HIGH:
					debug_level = 2;
					break;
				default:
					debug_level = -1;
				}
				if (sec_set_param_value && debug_level != -1)
					sec_set_param_value(__DEBUG_LEVEL, &debug_level);
			}
		}
	}

	if (code != SYS_POWER_OFF) {
		if (sec_get_param_value && sec_set_param_value) {
			/* in case of RECOVERY mode we set switch_sel
			 * with default value */
			sec_get_param_value(__REBOOT_MODE, &temp_mode);
			if (temp_mode == REBOOT_MODE_RECOVERY)
				sec_set_param_value(__SWITCH_SEL,
						    &default_switchsel);
		}

		/* save __REBOOT_MODE, if CMD is NULL then REBOOT_MODE_NONE will be saved */
		if (sec_set_param_value)
			sec_set_param_value(__REBOOT_MODE, &mode);
	}
	if (sec_get_param_value) {
		sec_get_param_value(__REBOOT_MODE, &temp_mode);
		printk(KERN_INFO "%s: __REBOOT_MODE: 0x%x\n",
			__func__, temp_mode);
		sec_get_param_value(__SWITCH_SEL, &temp_mode);
		printk(KERN_INFO "%s: __SWITCH_SEL: 0x%x\n",
			__func__, temp_mode);
	}
	
	return NOTIFY_DONE;
}				/* end fn __sec_common_reboot_call */

static struct notifier_block __sec_common_reboot_notifier = {
	.notifier_call = __sec_common_reboot_call,
};

/*
 * Store a handy board information string which we can use elsewhere like
 * like in panic situation
 */
static char sec_panic_string[256];
static void __init sec_common_set_panic_string(void)
{
	char *cpu_type = "UNKNOWN";

#if defined(CONFIG_ARCH_OMAP3)
	cpu_type = cpu_is_omap34xx() ? "OMAP3430" : "OMAP3630";
#elif defined(CONFIG_ARCH_OMAP4)
	cpu_type = cpu_is_omap443x() ? "OMAP4430" : "OMAP4460";
#elif defined (CONFIG_MACH_SAMSUNG_U9540)
	cpu_type = cpu_is_u9540() ? "U9540" : "Unknown";
#elif defined(CONFIG_MACH_SAMSUNG_UX500)
	cpu_type = cpu_is_u8500() ? "U8500" : "Unknown";
#endif 

#if defined (CONFIG_MACH_SAMSUNG_U9540)
	snprintf(sec_panic_string, ARRAY_SIZE(sec_panic_string),
		"Venus: %02X, cpu %s ES%d",
//		CONFIG_SAMSUNG_BOARD_NAME,
//		CONFIG_SAMSUNG_MODEL_NAME,
		system_rev, cpu_type,
		dbx500_revision());
#elif defined(CONFIG_MACH_SAMSUNG_UX500)
	snprintf(sec_panic_string, ARRAY_SIZE(sec_panic_string),
		"UX500: %02X, cpu %s ES%d",
//		CONFIG_SAMSUNG_BOARD_NAME,
//		CONFIG_SAMSUNG_MODEL_NAME,
		system_rev, cpu_type,
		dbx500_revision());
#else
	snprintf(sec_panic_string, ARRAY_SIZE(sec_panic_string),
		"%s (%s): %02X, cpu %s ES%d.%d",
		CONFIG_SAMSUNG_BOARD_NAME,
		CONFIG_SAMSUNG_MODEL_NAME,
		system_rev, cpu_type,
		(GET_OMAP_REVISION() >> 4) & 0xf,
		GET_OMAP_REVISION() & 0xf);
#endif
	mach_panic_string = sec_panic_string;
}

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
static const char * const omap_types[] = {
	[OMAP2_DEVICE_TYPE_TEST]	= "TST",
	[OMAP2_DEVICE_TYPE_EMU]		= "EMU",
	[OMAP2_DEVICE_TYPE_SEC]		= "HS",
	[OMAP2_DEVICE_TYPE_GP]		= "GP",
	[OMAP2_DEVICE_TYPE_BAD]		= "BAD"
};
#endif
static ssize_t sec_common_soc_family_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	return sprintf(buf, "OMAP%04x\n", GET_OMAP_TYPE);
#elif defined (CONFIG_MACH_SAMSUNG_U9540)
	return sprintf(buf, "STE U9540");
#elif defined(CONFIG_MACH_SAMSUNG_UX500)
	return sprintf(buf, "STE U8500");
#endif
}

static ssize_t sec_common_soc_revision_show(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    char *buf)
{
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	return sprintf(buf, "ES%d.%d\n",
		       (GET_OMAP_REVISION() >> 4) & 0x0F,
		       (GET_OMAP_REVISION()) & 0xF);
#elif defined (CONFIG_MACH_SAMSUNG_U9540)|| defined(CONFIG_MACH_SAMSUNG_UX500)
return sprintf(buf, "ES%d\n", dbx500_revision());
#endif 
}

static ssize_t sec_common_soc_die_id_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	struct omap_die_id oid;

	omap_get_die_id(&oid);

	return sprintf(buf, "%08X-%08X-%08X-%08X\n",
		       oid.id_3, oid.id_2, oid.id_1, oid.id_0);
#elif defined (CONFIG_MACH_SAMSUNG_U9540)|| defined(CONFIG_MACH_SAMSUNG_UX500)
	return sprintf(buf, "Unknown\n");
#endif
}

static ssize_t sec_common_soc_prod_id_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	struct omap_die_id oid;

	omap_get_production_id(&oid);

	return sprintf(buf, "%08X-%08X\n", oid.id_1, oid.id_0);
#elif defined (CONFIG_MACH_SAMSUNG_U9540)|| defined(CONFIG_MACH_SAMSUNG_UX500)
	return sprintf(buf, "Unknown\n");
#endif
}

static ssize_t sec_common_soc_type_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	return sprintf(buf, "%s\n", omap_types[omap_type()]);
#elif defined (CONFIG_MACH_SAMSUNG_U9540)
	return sprintf(buf, "STE U9540\n");
#elif defined(CONFIG_MACH_SAMSUNG_UX500)
	return sprintf(buf, "STE U8500");
#endif
}

#define SEC_COMMON_ATTR_RO(_type, _name)				\
	struct kobj_attribute sec_common_##_type##_prop_attr_##_name =	\
		__ATTR(_name, S_IRUGO,					\
		       sec_common_##_type##_##_name##_show, NULL)

static SEC_COMMON_ATTR_RO(soc, family);
static SEC_COMMON_ATTR_RO(soc, revision);
static SEC_COMMON_ATTR_RO(soc, type);
static SEC_COMMON_ATTR_RO(soc, die_id);
static SEC_COMMON_ATTR_RO(soc, prod_id);

static struct attribute *sec_common_soc_prop_attrs[] = {
	&sec_common_soc_prop_attr_family.attr,
	&sec_common_soc_prop_attr_revision.attr,
	&sec_common_soc_prop_attr_type.attr,
	&sec_common_soc_prop_attr_die_id.attr,
	&sec_common_soc_prop_attr_prod_id.attr,
	NULL,
};

static struct attribute_group sec_common_soc_prop_attr_group = {
	.attrs = sec_common_soc_prop_attrs,
};

static ssize_t sec_common_board_revision_show(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      char *buf)
{
	char *machine_name = *(char **)kallsyms_lookup_name("machine_name");

	return sprintf(buf, "%s Samsung board (0x%02X)\n",
		       machine_name, system_rev);
}

static SEC_COMMON_ATTR_RO(board, revision);

static struct attribute *sec_common_board_prop_attrs[] = {
	&sec_common_board_prop_attr_revision.attr,
	NULL,
};

static struct attribute_group sec_common_board_prop_attr_group = {
	.attrs = sec_common_board_prop_attrs,
};

static void __init sec_common_create_board_props(void)
{
	struct kobject *board_props_kobj;
	struct kobject *soc_kobj;
	int ret = 0;

	board_props_kobj = kobject_create_and_add("board_properties", NULL);
	if (!board_props_kobj)
		goto err_board_obj;

	soc_kobj = kobject_create_and_add("soc", board_props_kobj);
	if (!soc_kobj)
		goto err_soc_obj;

	ret = sysfs_create_group(board_props_kobj,
				 &sec_common_board_prop_attr_group);
	if (ret)
		goto err_board_sysfs_create;

	ret = sysfs_create_group(soc_kobj, &sec_common_soc_prop_attr_group);
	if (ret)
		goto err_soc_sysfs_create;

	return;

err_soc_sysfs_create:
	sysfs_remove_group(board_props_kobj,
			   &sec_common_board_prop_attr_group);
err_board_sysfs_create:
	kobject_put(soc_kobj);
err_soc_obj:
	kobject_put(board_props_kobj);
err_board_obj:
	if (!board_props_kobj || !soc_kobj || ret)
		pr_err("failed to create board_properties\n");
}

int __init sec_common_init_early(void)
{
	sec_common_set_panic_string();
	return 0;
}				/* end fn sec_common_init_early */

static DEVICE_ATTR(checksum_pass, S_IRUGO, checksum_pass_show, NULL);
static DEVICE_ATTR(checksum_done, S_IRUGO, checksum_done_show, NULL);

int __init sec_common_init(void)
{
	sec_class = class_create(THIS_MODULE, "sec");
	if (IS_ERR(sec_class))
		pr_err("Class(sec) Creating Fail!!!\n");

	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class))
		pr_err("Class(camera) Creating Fail!!!\n");

	sec_checksum = device_create(sec_class, NULL, 0, NULL, "sec_checksum");
	if (IS_ERR(sec_checksum))
		printk(KERN_ERR "Failed to create device(sec_checksum)!\n");
	if (device_create_file(sec_checksum, &dev_attr_checksum_pass) < 0)
		printk(KERN_ERR "%s device_create_file fail dev_attr_checksum_pass\n", __func__);
	if (device_create_file(sec_checksum, &dev_attr_checksum_done) < 0)
		printk(KERN_ERR "%s device_create_file fail dev_attr_checksum_done\n", __func__);

	sec_common_create_board_props();
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	for (i = 0; i < ARRAY_SIZE(hwrev_gpio); i++) {
		gpio_pin = omap_muxtbl_get_gpio_by_name(hwrev_gpio[i]);
		if (likely(gpio_pin != -EINVAL))
			gpio_request(gpio_pin, hwrev_gpio[i]);
	}
#endif

	return 0;
}				/* end fn sec_common_init */

#ifdef CONFIG_SAMSUNG_LOG_BUF

/*  This function logs key and touch events.  Key and touch events are stored separately in sec_key_log
      and sec_touch_log.   Touch events come rapidly when a touch is moved, so we skip any events that occur
      within SEC_TOUCH_LOG_INTERVAL ms (currently 200ms).  sec_disp_input_log() combines the 2 lists
      to display key and events in the correct time sequence */

static void sec_common_input_log_event(struct input_handle *handle, unsigned int event_type,
		      unsigned int event_code, int value)
{
	static unsigned long long prev_timestamp;
	static unsigned long long current_timestamp;
	static unsigned long long log_interval = SEC_TOUCH_LOG_INTERVAL * SEC_TOUCH_LOG_TIMESTAMP_MS;
	static bool new_timestamp = true;

	spin_lock(&sec_common_input_log_event_lock);

	switch (event_type) {
	case EV_KEY:

		current_timestamp = cpu_clock(smp_processor_id());

		sec_key_log[sec_key_log_idx].event_type = event_type;
		sec_key_log[sec_key_log_idx].event_code = event_code;
		sec_key_log[sec_key_log_idx].value = value;
		sec_key_log[sec_key_log_idx].timestamp = current_timestamp;

		sec_key_log_idx++;
		sec_key_log_idx &= (SEC_KEY_LOG_SIZE - 1);

		break;

	case EV_ABS:

		switch (event_code) {
		case ABS_MT_POSITION_X:
			sec_touch_log[sec_touch_log_idx].x = value;
			break;

		case ABS_MT_POSITION_Y:
			sec_touch_log[sec_touch_log_idx].y = value;
			break;

		case ABS_MT_WIDTH_MAJOR:
			sec_touch_log[sec_touch_log_idx].width = value;
			break;

		case ABS_MT_TOUCH_MAJOR:
			sec_touch_log[sec_touch_log_idx].touch = value;
			break;

		case ABS_MT_TRACKING_ID:
			sec_touch_log[sec_touch_log_idx].id = value;
			break;

		default:
			break;
		}
		break;

	case EV_SYN:

		switch (event_code) {
			int prev_idx;
		case SYN_MT_REPORT:
			/* SYN_MT_REPORT indicates the end of the information set for one touch */
			current_timestamp = cpu_clock(smp_processor_id());

			prev_idx = (sec_touch_log_idx == 0) ?  (SEC_TOUCH_LOG_SIZE - 1) : (sec_touch_log_idx-1);

			/* do not add to log if this is less than SEC_TOUCH_LOG_INTERVAL ms after the previous log entry,
			unless it's the start or end of a touch */
			if (((current_timestamp - prev_timestamp) > log_interval) ||
				(sec_touch_log[sec_touch_log_idx].touch != sec_touch_log[prev_idx].touch)) {
				sec_touch_log[sec_touch_log_idx].timestamp = current_timestamp;
				sec_touch_log_idx++;
				sec_touch_log_idx &= (SEC_TOUCH_LOG_SIZE - 1);
				new_timestamp = true;
			}
			break;

		case SYN_REPORT:
			/* SYN_REPORT indicates the end of a multi-touch information set */
			if (new_timestamp) {
				prev_timestamp = current_timestamp;
				new_timestamp = false;
			}
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	spin_unlock(&sec_common_input_log_event_lock);
}

static bool sec_common_input_log_match(struct input_handler *handler, struct input_dev *dev)
{
	return true;
}

static int sec_common_input_log_connect(struct input_handler *handler, struct input_dev *dev,
			const struct input_device_id *id)
{
	   struct input_handle *handle;
	   int error;

	   handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	   if (!handle)
		   return -ENOMEM;

	   handle->dev = dev;
	   handle->handler = handler;
	   handle->name = "sec_common";

	   error = input_register_handle(handle);
	   if (error)
		   goto err_free_handle;

	   error = input_open_device(handle);
	   if (error)
		   goto err_unregister_handle;

	   return 0;

	err_unregister_handle:
	   input_unregister_handle(handle);
	err_free_handle:
	   kfree(handle);
	   return error;

}

static void sec_common_input_log_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static void sec_common_input_log_start(struct input_handle *handle)
{
	memset(sec_key_log, 0, sizeof(sec_key_log));
	memset(sec_touch_log, 0, sizeof(sec_touch_log));
}

static struct input_handler sec_common_input_log_handler = {
	.event		= sec_common_input_log_event,
	.match		= sec_common_input_log_match,
	.connect		= sec_common_input_log_connect,
	.disconnect	= sec_common_input_log_disconnect,
	.start		= sec_common_input_log_start,
	.name		= "sec_input_log",
	.id_table	= sec_common_input_log_ids,
};

static int sec_cpufreq_notifier(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;

	/* Only react once per transition and only for one core, e.g. core 0 */
	if (val != CPUFREQ_POSTCHANGE || freq->cpu != 0)
		return 0;

	spin_lock(&sec_cpufreq_log_lock);
	sec_cpufreq_log_idx++;
	sec_cpufreq_log_idx &= SEC_CPUFREQ_LOG_MAX;
	sec_cpufreq_log[sec_cpufreq_log_idx].freq	= freq->new;
	sec_cpufreq_log[sec_cpufreq_log_idx].timestamp	= cpu_clock(smp_processor_id());
	spin_unlock(&sec_cpufreq_log_lock);

	return 0;
}
static int sec_policy_notifier(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;

	if (val == CPUFREQ_NOTIFY)
		printk(KERN_INFO "DVFS Governor Policy set to - %s\n", policy->governor->name);

	return 0;
}
#endif

int __init sec_common_init_post(void)
{
	int retval;

	register_reboot_notifier(&__sec_common_reboot_notifier);

#if defined(CONFIG_SAMSUNG_LOG_BUF)
	retval = input_register_handler(&sec_common_input_log_handler);
	if (retval)
		printk(KERN_WARNING "%s: input_register_handler() failed (%d) ", __func__, retval);

	cpufreq_register_notifier(&sec_cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
	cpufreq_register_notifier(&sec_policy_notifier_block, CPUFREQ_POLICY_NOTIFIER);
#endif
	return 0;
}		/* end fn sec_common_init_post */

struct sec_reboot_mode {
	char *cmd;
	char mode;
};

static __inline char __sec_common_convert_reboot_mode(char mode,
						      const char *cmd)
{
	char new_mode = mode;
	struct sec_reboot_mode mode_tbl[] = {
		{"arm11_fota", 'f'},
		{"arm9_fota", 'f'},
		{"recovery", 'r'},
		{"download", 'd'},
		{"cp_crash", 'C'},
		{"Checkin scheduled forced", 'c'} /* Note - c means REBOOTMODE_NORMAL */
	};
	size_t i, n;
#ifdef CONFIG_SAMSUNG_KERNEL_DEBUG
	if (mode == 'L' || mode == 'U' || mode == 'K') {
		new_mode = mode;
		goto __return;
	}
#endif /* CONFIG_SAMSUNG_KERNEL_DEBUG */
	if (cmd == NULL)
		goto __return;
	n = ARRAY_SIZE(mode_tbl);
	for (i = 0; i < n; i++) {
		if (!strcmp(cmd, mode_tbl[i].cmd)) {
			new_mode = mode_tbl[i].mode;
			goto __return;
		} else if(!strncmp(cmd, "sud", 3)) {
			new_mode = 's';
			goto __return;
		}
	}

__return:
	return new_mode;
}

#define SEC_REBOOT_MODE_ADDR		0
#define SEC_REBOOT_FLAG_ADDR		0

unsigned short sec_common_update_reboot_reason(char mode, const char *cmd)
{
	unsigned short scpad = 0;
	const u32 scpad_addr = SEC_REBOOT_MODE_ADDR;
	unsigned short reason = REBOOTMODE_NORMAL;
	unsigned short ret;

#if 0
	/* for the compatibility with LSI chip-set based products */

	printk(KERN_INFO "sec_common_update_reboot_reason: scpad_addr: 0x%x\n",
			scpad_addr);
	if (cmd)
		printk(KERN_INFO "sec_common_update_reboot_reason: mode= %c, cmd= %s\n",
				mode, cmd);
	mode = __sec_common_convert_reboot_mode(mode, cmd);
	printk(KERN_INFO "mode: %c\n", mode);
#else
	mode = __sec_common_convert_reboot_mode(mode, cmd);
#endif

	switch (mode) {
	case 'r':		/* reboot mode = recovery */
		reason = REBOOTMODE_RECOVERY;
		break;
	case 'f':		/* reboot mode = fota */
		reason = REBOOTMODE_FOTA;
		break;
	case 't':		/* reboot mode = shutdown with TA */
	case 'u':		/* reboot mode = shutdown with USB */
	case 'j':		/* reboot mode = shutdown with JIG */
		reason = REBOOTMODE_SHUTDOWN;
		break;
	case 's':		/* reboot mode = download */
	case 'd':		/* reboot mode = download */
		reason = REBOOTMODE_DOWNLOAD;
		break;
	default:		/* reboot mode = normal */
		reason = REBOOTMODE_NORMAL;
		break;
	}

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
		omap_writel(scpad | reason, scpad_addr);
		omap_writel(*(u32 *)rebootflag, SEC_REBOOT_FLAG_ADDR);
#endif
	ret = (scpad | reason);
	return ret;
} /* sec_common_update_reboot_reason */


#ifdef CONFIG_SAMSUNG_LOG_BUF

#define LOGGING_RAM_MASK    1

extern void __iomem *log_buf_base;
extern unsigned long logging_mode;

void dump_ram_buffer(void)
{
	char *bufAddr = NULL;
	unsigned temp, index;
	int i;
	int max_limit = 1048567;

	if (!log_buf_base)
		return;

	/* printk("**** Kernel ram buffer logging start\n"); */
	bufAddr = (((char *)log_buf_base) + 8);

	index = *((unsigned long *)log_buf_base);

	/*
	find out if the buffer is already overwritten
	As this cant be done reliably, safely assume that if 3 consequtive characters are null
	then the buffer is not overwritten
	*/
	if ((bufAddr[index] == 0x00) && (bufAddr[index+1] == 0x00) && (bufAddr[index+2] == 0x00)) {
		max_limit = index;
		index = 0;
	}

	temp = index;
	for (i = 0; i <= max_limit; i++, index++) {
		if (index > 1048567) {
			printk(KERN_INFO "*** starting of the buffer ***\n");
			index = 0;
			temp = index;
		}
		if (bufAddr[index] == '\n') {
			if (index == 1048567) {
				bufAddr[index-1] = '\n';
				bufAddr[index] = '\0';
			} else {
				index++;
				i++;
				bufAddr[index] = '\0';
			}
			printk(KERN_INFO "%s", &bufAddr[temp]);
			temp = index+1;
		}
	}
	/* printk("**** Kernel ram buffer logging done\n"); */
}
EXPORT_SYMBOL(dump_ram_buffer);

static inline void disp_panic_msg(void)
{
#ifdef CONFIG_SAMSUNG_PANIC_LCD_DISPLAY
	display_dbg_msg("Kernel panic!!. Please connect the phone to PC",
					KHB_DEFAULT_FONT_SIZE, KHB_RETAIN_PREV_MSG);
	display_dbg_msg("enable capture and press the volume down key to get the",
					KHB_DEFAULT_FONT_SIZE, KHB_RETAIN_PREV_MSG);
	display_dbg_msg("kernel log.  Repeat if required.", KHB_DEFAULT_FONT_SIZE, KHB_RETAIN_PREV_MSG);
	display_dbg_msg("Press volume up to enter upload mode.", KHB_DEFAULT_FONT_SIZE, KHB_RETAIN_PREV_MSG);
#endif
}

static void sec_disp_input_log(void)
{
	int key_idx, touch_idx, key_cnt = 0, touch_cnt = 0;

	printk(KERN_EMERG "[Last %d key press and %d touch events]\n", SEC_KEY_LOG_SIZE, SEC_TOUCH_LOG_SIZE);

	key_idx = sec_key_log_idx;
	touch_idx = sec_touch_log_idx;

	/* go through both sec_key_log and sec_touch_log and print events in correct time order */

	while ((key_cnt < SEC_KEY_LOG_SIZE) || (touch_cnt < SEC_TOUCH_LOG_SIZE)) {
		if (((sec_key_log[key_idx].timestamp <= sec_touch_log[touch_idx].timestamp) &&  (key_cnt < SEC_KEY_LOG_SIZE)) ||
			(touch_cnt >= SEC_TOUCH_LOG_SIZE)) {
			if (sec_key_log[key_idx].timestamp) {
				unsigned long nanosec_rem;

				nanosec_rem = do_div(sec_key_log[key_idx].timestamp, 1000000000);
				printk(KERN_EMERG "<KEY> key code: %d, up/down (0/1): %d @ [%5lu.%06lu]\n",
					sec_key_log[key_idx].event_code,
					sec_key_log[key_idx].value,
					(unsigned long)sec_key_log[key_idx].timestamp,
					nanosec_rem / 1000);
			}
			key_idx++;
			key_idx &= (SEC_KEY_LOG_SIZE - 1);
			key_cnt++;
			continue;
		} else {
			if (sec_touch_log[touch_idx].timestamp) {

				unsigned long nanosec_rem;

				nanosec_rem = do_div(sec_touch_log[touch_idx].timestamp, 1000000000);
				printk(KERN_EMERG "<TOUCH> x: %03d, y: %03d, width: %03d, touch: %03d, tracking id: %d @ [%5lu.%06lu]\n",
					sec_touch_log[touch_idx].x,
					sec_touch_log[touch_idx].y,
					sec_touch_log[touch_idx].width,
					sec_touch_log[touch_idx].touch,
					sec_touch_log[touch_idx].id,
					(unsigned long)sec_touch_log[touch_idx].timestamp,
					nanosec_rem / 1000);
			}
			touch_idx++;
			touch_idx &= (SEC_TOUCH_LOG_SIZE - 1);
			touch_cnt++;
		}
	}
	printk(KERN_EMERG "\n");

}


extern int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu);
static void sec_disp_dvfs_info(void)
{
	struct cpufreq_policy policy;

	if (!cpufreq_get_policy(&policy, 0)) {
		printk(KERN_EMERG "Current DVFS Governor Policy - %s\n", policy.governor->name);
	}

	if (sec_cpufreq_log_idx != -1) {
		int i, idx;

		printk(KERN_EMERG "[Last %u CPU frequency changes]\n", SEC_CPUFREQ_LOG_MAX + 1);
		idx = (sec_cpufreq_log_idx + 1) & SEC_CPUFREQ_LOG_MAX;
		for (i = 0; i <= SEC_CPUFREQ_LOG_MAX; i++) {
			if (sec_cpufreq_log[idx].timestamp != 0) {
				unsigned long nanosec_rem;

				nanosec_rem = do_div(sec_cpufreq_log[idx].timestamp, 1000000000);
				printk(KERN_EMERG "CPU Freq set to %07u @ [%5lu.%06lu]\n",
					sec_cpufreq_log[idx].freq,
					(unsigned long)sec_cpufreq_log[idx].timestamp,
					nanosec_rem / 1000);
			}
			idx++;
			idx &= SEC_CPUFREQ_LOG_MAX;
		}
		printk(KERN_EMERG "\n");
	}
}

#include <linux/syscalls.h>
#include <asm/uaccess.h>

static void sec_disp_mount_info(void)
{
	long read_size;
	unsigned long old_fs;
	long fd;
	char buf[128];

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fd = sys_open("/proc/mounts", O_RDONLY, 0);

	printk(KERN_EMERG "Current mount information:\n");

	if (fd >= 0) {
		while ((read_size = sys_read(fd, buf, 127)) > 0) {
			buf[read_size] = '\0';
			printk(KERN_INFO "%s", buf);
		}
		sys_close(fd);
		printk("\n");
	}

	set_fs(old_fs);

}
extern void sec_disp_lsof_info(void);
extern int sec_i2c_panic_config(void);
extern void sec_disp_i2c_info(void);

extern unsigned int system_serial_low;
extern unsigned int system_serial_high;

static void sec_disp_addition_crash_dbg(void)
{
	printk(KERN_EMERG "\n%s\n", linux_banner);
	printk(KERN_EMERG "%s\n", saved_command_line);

	printk(KERN_EMERG "Revision\t: %04x\n", system_rev);
	printk(KERN_EMERG "Serial\t\t: %08x%08x\n\n",
		   system_serial_high, system_serial_low);

	sec_disp_dvfs_info();
	sec_disp_mount_info();
	sec_disp_input_log();
#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_LSOF
	sec_disp_lsof_info();
#endif

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_DEVICES
	if (sec_pmic_panic_config()) {
		pr_emerg("%s: Power Managment IC not configured for panic\n", __func__);
		return;
	}

	if (sec_i2c_panic_config()) {
		pr_emerg("%s: I2C not configured for panic\n", __func__);
		return;
	}
#endif
#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_PMIC
	sec_disp_pmic_info();
#endif

#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_I2C_PERIPHS
	sec_disp_i2c_info();
#endif

}

enum {SEC_NO_KEY_PRESSED, SEC_VOL_UP_PRESSED, SEC_VOL_DOWN_PRESSED};

static inline int wait_for_key_press(void)
{
	int key = SEC_NO_KEY_PRESSED;

	do {
#if defined(CONFIG_MACH_JANICE)
		if (!gpio_get_value(VOL_DOWN_JANICE_R0_0))
			key = SEC_VOL_DOWN_PRESSED;
		else if (!gpio_get_value(VOL_UP_JANICE_R0_0))
			key = SEC_VOL_UP_PRESSED;
#elif defined(CONFIG_MACH_GAVINI)
		if (!gpio_get_value(VOL_DOWN_GAVINI_R0_0))
			key = SEC_VOL_DOWN_PRESSED;
		else if (!gpio_get_value(VOL_UP_GAVINI_R0_0))
			key = SEC_VOL_UP_PRESSED;
#elif defined(CONFIG_MACH_CODINA)
		if (!gpio_get_value(VOL_DOWN_CODINA_R0_0))
			key = SEC_VOL_DOWN_PRESSED;
		else if (!gpio_get_value(VOL_UP_CODINA_R0_0))
			key = SEC_VOL_UP_PRESSED;
#elif defined(CONFIG_MACH_SEC_GOLDEN)
		if (!gpio_get_value(VOL_DOWN_GOLDEN_BRINGUP))
			key = SEC_VOL_DOWN_PRESSED;
		else if (!gpio_get_value(VOL_UP_GOLDEN_BRINGUP))
			key = SEC_VOL_UP_PRESSED;
#elif defined(CONFIG_MACH_SEC_KYLE)
		if (!gpio_get_value(KYLE_GPIO_VOL_DOWN_KEY))
			key = SEC_VOL_DOWN_PRESSED;
		else if (!gpio_get_value(KYLE_GPIO_VOL_UP_KEY))
			key = SEC_VOL_UP_PRESSED;
#else
		key = SEC_VOL_UP_PRESSED; /* unknown board, so straight to upload */
#endif
	} while (key == SEC_NO_KEY_PRESSED);
	return key;
}

void sec_extra_die_actions(const char *str)
{
	int which_key;

	logging_mode &= (~LOGGING_RAM_MASK); /* disabled RAM buffer logging to maintain info */

#ifdef CONFIG_SAMSUNG_PANIC_LCD_DISPLAY
	smp_send_stop(); /* required to prevent framebuffer from being overwritten */
#endif

	/* L1 & L2 cache management */
	flush_cache_all();

	if (cpu_is_u8500())
		ux500_clean_l2_cache_all();

#ifdef CONFIG_SAMSUNG_PANIC_LCD_DISPLAY
	display_dbg_msg((char *)str, KHB_DEFAULT_FONT_SIZE, KHB_DISCARD_PREV_MSG);
	disp_panic_msg();
#endif

#if 0
	do {
		which_key = wait_for_key_press();
		if (which_key == SEC_VOL_DOWN_PRESSED) {
			dump_ram_buffer();
			sec_disp_addition_crash_dbg();
		}
	} while (which_key != SEC_VOL_UP_PRESSED);
#endif
}
EXPORT_SYMBOL(sec_extra_die_actions);

#ifdef CONFIG_SAMSUNG_ADD_GAFORENSICINFO
extern struct GAForensicINFO GAFINFO;
#endif

void dump_one_task_info( struct task_struct *tsk, bool isMain )
{
	char stat_array[3] = {'R', 'S', 'D'};
	char stat_ch;
#ifdef CONFIG_SAMSUNG_ADD_GAFORENSICINFO
	char *pThInf = tsk->stack;
#endif
	unsigned long wchan;
	unsigned long pc = 0;
	char symname[KSYM_NAME_LEN];
	int permitted;
	struct mm_struct *mm;


	permitted = ptrace_may_access(tsk, PTRACE_MODE_READ);
	mm = get_task_mm(tsk);
	if (mm) {
		if (permitted) {
			pc = KSTK_EIP(tsk);
		}
	}

	wchan = get_wchan(tsk);
	if (lookup_symbol_name(wchan, symname) < 0) {
		if (!ptrace_may_access(tsk, PTRACE_MODE_READ))
			sprintf(symname,"_____");
		else
			sprintf(symname, "%lu", wchan);
	}

	stat_ch = tsk->state <= TASK_UNINTERRUPTIBLE ? stat_array[tsk->state] : '?';
#ifdef CONFIG_SAMSUNG_ADD_GAFORENSICINFO
	printk( KERN_INFO "%8d %8d %8d %16lld %c(%d) %3d  %08x %08x  %08x %c %16s [%s]\n",
		tsk->pid, (int)(tsk->utime), (int)(tsk->stime),
		tsk->se.exec_start, stat_ch, (int)(tsk->state),
		*(int*)(pThInf + GAFINFO.thread_info_struct_cpu),
		(int)wchan, (int)pc, (int)tsk, isMain?'*':' ',
		tsk->comm, symname);
#else
	printk( KERN_INFO "%8d %8d %8d %16lld %c(%d) %3d  %08x  %08x %c %16s [%s]\n",
		tsk->pid, (int)(tsk->utime), (int)(tsk->stime),
		tsk->se.exec_start, stat_ch, (int)(tsk->state),
		(int)wchan, (int)pc, (int)tsk, isMain?'*':' ',
		tsk->comm, symname);
#endif
	if (tsk->state == TASK_RUNNING || tsk->state == TASK_UNINTERRUPTIBLE || tsk->mm == NULL)
		show_stack(tsk, NULL);
}

void dump_all_task_info(void)
{
	struct task_struct *frst_tsk;
	struct task_struct *curr_tsk;
	struct task_struct *frst_thr;
	struct task_struct *curr_thr;

	printk( KERN_INFO "\n" );
	printk( KERN_INFO " current proc : %d %s\n", current->pid, current->comm );
	printk( KERN_INFO " -------------------------------------------------------------------------------------------------------------\n" );
	printk( KERN_INFO "     pid      uTime    sTime      exec(ns)  stat  cpu   wchan   user_pc  task_struct          comm   sym_wchan\n" );
	printk( KERN_INFO " -------------------------------------------------------------------------------------------------------------\n" );

	local_irq_disable();

	/* processes */
	frst_tsk = &init_task;
	curr_tsk = frst_tsk;
	while( curr_tsk != NULL ) {
		dump_one_task_info( curr_tsk,  true );
		/* threads */
		if( curr_tsk->thread_group.next != NULL ) {
			frst_thr = container_of( curr_tsk->thread_group.next, struct task_struct, thread_group );
			curr_thr = frst_thr;
			if( frst_thr != curr_tsk ) {
				while( curr_thr != NULL ) {
					dump_one_task_info( curr_thr, false );
					curr_thr = container_of( curr_thr->thread_group.next, struct task_struct, thread_group );
					if( curr_thr == curr_tsk ) break;
				}
			}
		}
		curr_tsk = container_of( curr_tsk->tasks.next, struct task_struct, tasks );
		if( curr_tsk == frst_tsk ) break;
	}

	local_irq_disable();

	printk( KERN_INFO " -----------------------------------------------------------------------------------\n" );
}
EXPORT_SYMBOL(dump_all_task_info);

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif
#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

void dump_cpu_stat(void)
{
	int i, j;
	unsigned long jif;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest, guest_nice;
	u64 sum = 0;
	u64 sum_softirq = 0;
	unsigned int per_softirq_sums[NR_SOFTIRQS] = {0};
	struct timespec boottime;
	unsigned int per_irq_sum;

	char *softirq_to_name[NR_SOFTIRQS] = {
	     "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "BLOCK_IOPOLL",
	     "TASKLET", "SCHED", "HRTIMER",  "RCU"
	};

	user = nice = system = idle = iowait = irq = softirq = steal = cputime64_zero;
	guest = guest_nice = cputime64_zero;

	getboottime(&boottime);
	jif = boottime.tv_sec;
	for_each_possible_cpu(i) {
		user = cputime64_add(user, kstat_cpu(i).cpustat.user);
		nice = cputime64_add(nice, kstat_cpu(i).cpustat.nice);
		system = cputime64_add(system, kstat_cpu(i).cpustat.system);
		idle = cputime64_add(idle, kstat_cpu(i).cpustat.idle);
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = cputime64_add(iowait, kstat_cpu(i).cpustat.iowait);
		irq = cputime64_add(irq, kstat_cpu(i).cpustat.irq);
		softirq = cputime64_add(softirq, kstat_cpu(i).cpustat.softirq);

		for_each_irq_nr(j) {
			sum += kstat_irqs_cpu(j, i);
		}
		sum += arch_irq_stat_cpu(i);
		for (j = 0; j < NR_SOFTIRQS; j++) {
			unsigned int softirq_stat = kstat_softirqs_cpu(j, i);
			per_softirq_sums[j] += softirq_stat;
			sum_softirq += softirq_stat;
		}
	}
	sum += arch_irq_stat();
	printk(KERN_INFO "\n");
	printk(KERN_INFO " cpu     user:%llu  nice:%llu  system:%llu  idle:%llu  iowait:%llu  irq:%llu  softirq:%llu %llu %llu " "%llu\n",
	(unsigned long long)cputime64_to_clock_t(user),
	(unsigned long long)cputime64_to_clock_t(nice),
	(unsigned long long)cputime64_to_clock_t(system),
	(unsigned long long)cputime64_to_clock_t(idle),
	(unsigned long long)cputime64_to_clock_t(iowait),
	(unsigned long long)cputime64_to_clock_t(irq),
	(unsigned long long)cputime64_to_clock_t(softirq),
	(unsigned long long)0, //cputime64_to_clock_t(steal),
	(unsigned long long)0, //cputime64_to_clock_t(guest),
	(unsigned long long)0);//cputime64_to_clock_t(guest_nice));
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
	for_each_online_cpu(i) {
		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kstat_cpu(i).cpustat.user;
		nice = kstat_cpu(i).cpustat.nice;
		system = kstat_cpu(i).cpustat.system;
		idle = kstat_cpu(i).cpustat.idle;
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = kstat_cpu(i).cpustat.iowait;
		irq = kstat_cpu(i).cpustat.irq;
		softirq = kstat_cpu(i).cpustat.softirq;
		//steal = kstat_cpu(i).cpustat.steal;
		//guest = kstat_cpu(i).cpustat.guest;
		//guest_nice = kstat_cpu(i).cpustat.guest_nice;
		printk(KERN_INFO " cpu %d   user:%llu  nice:%llu  system:%llu  idle:%llu  iowait:%llu  irq:%llu  softirq:%llu %llu %llu " "%llu\n",
		i,
		(unsigned long long)cputime64_to_clock_t(user),
		(unsigned long long)cputime64_to_clock_t(nice),
		(unsigned long long)cputime64_to_clock_t(system),
		(unsigned long long)cputime64_to_clock_t(idle),
		(unsigned long long)cputime64_to_clock_t(iowait),
		(unsigned long long)cputime64_to_clock_t(irq),
		(unsigned long long)cputime64_to_clock_t(softirq),
		(unsigned long long)0, //cputime64_to_clock_t(steal),
		(unsigned long long)0, //cputime64_to_clock_t(guest),
		(unsigned long long)0);//cputime64_to_clock_t(guest_nice));
	}
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
	printk(KERN_INFO "\n");
	printk(KERN_INFO " irq : %llu", (unsigned long long)sum);
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
	/* sum again ? it could be updated? */
	for_each_irq_nr(j) {
		per_irq_sum = 0;
		for_each_possible_cpu(i)
		per_irq_sum += kstat_irqs_cpu(j, i);
		if(per_irq_sum) {
			printk(KERN_INFO " irq-%4d : %8u %s\n", j, per_irq_sum, irq_to_desc(j)->action ?
			irq_to_desc(j)->action->name ?: "???" : "???");
		}
	}
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
	printk(KERN_INFO "\n");
	printk(KERN_INFO " softirq : %llu", (unsigned long long)sum_softirq);
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
	for (i = 0; i < NR_SOFTIRQS; i++)
		if(per_softirq_sums[i])
			printk(KERN_INFO " softirq-%d : %8u %s \n", i, per_softirq_sums[i], softirq_to_name[i]);
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
}
EXPORT_SYMBOL(dump_cpu_stat);
#endif	/* CONFIG_SAMSUNG_LOG_BUF */
