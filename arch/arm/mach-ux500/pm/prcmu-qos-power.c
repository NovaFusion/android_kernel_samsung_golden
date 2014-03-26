/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Martin Persson
 *         Per Fransson <per.xx.fransson@stericsson.com>
 *
 * Quality of Service for the U8500 PRCM Unit interface driver
 *
 * Strongly influenced by kernel/pm_qos_params.c.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <mach/prcmu-debug.h>

#define ARM_THRESHOLD_FREQ 400000

#define AB8500_VAPESEL1_REG 0x0E   /* APE OPP 100 voltage */
#define AB8500_VAPESEL2_REG 0x0F   /* APE OPP 50 voltage  */

static int qos_delayed_cpufreq_notifier(struct notifier_block *,
					unsigned long, void *);

static s32 cpufreq_requirement_queued;
static s32 cpufreq_requirement_set;

static int update_target_max_recur = 1;

/*
 * locking rule: all changes to requirements or prcmu_qos_object list
 * and prcmu_qos_objects need to happen with prcmu_qos_lock
 * held, taken with _irqsave.  One lock to rule them all
 */
struct requirement_list {
	struct list_head list;
	union {
		s32 value;
		s32 usec;
		s32 kbps;
	};
	char *name;
};

static s32 max_compare(s32 v1, s32 v2);

static int __prcmu_qos_update_requirement(int prcmu_qos_class, char *name,
		s32 new_value, bool sem);

struct prcmu_qos_object {
	struct requirement_list requirements;
	struct blocking_notifier_head *notifiers;
	struct miscdevice prcmu_qos_power_miscdev;
	char *name;
	s32 default_value;
	s32 max_value;
	s32 force_value;
	atomic_t target_value;
	s32 (*comparitor)(s32, s32);
};

static struct prcmu_qos_object null_qos;
static BLOCKING_NOTIFIER_HEAD(prcmu_ape_opp_notifier);
static BLOCKING_NOTIFIER_HEAD(prcmu_ddr_opp_notifier);
static BLOCKING_NOTIFIER_HEAD(prcmu_vsafe_opp_notifier);

static struct prcmu_qos_object ape_opp_qos = {
	.requirements =	{
		LIST_HEAD_INIT(ape_opp_qos.requirements.list)
	},
	.notifiers = &prcmu_ape_opp_notifier,
	.name = "ape_opp",
	/* Target value in % APE OPP */
	.default_value = 50,
	.max_value = 100,
	.force_value = 0,
	.target_value = ATOMIC_INIT(100),
	.comparitor = max_compare
};

static struct prcmu_qos_object ddr_opp_qos = {
	.requirements =	{
		LIST_HEAD_INIT(ddr_opp_qos.requirements.list)
	},
	.notifiers = &prcmu_ddr_opp_notifier,
	.name = "ddr_opp",
	/* Target value in % DDR OPP */
	.default_value = 25,
	.max_value = 100,
	.force_value = 0,
	.target_value = ATOMIC_INIT(100),
	.comparitor = max_compare
};

static struct prcmu_qos_object vsafe_opp_qos = {
	.requirements =	{
		LIST_HEAD_INIT(vsafe_opp_qos.requirements.list)
	},
	.notifiers = &prcmu_vsafe_opp_notifier,
	.name = "vsafe_opp",
	/* Target value in % VSAFE OPP */
	.default_value = 50,
	.force_value = 0,
	.target_value = ATOMIC_INIT(50),
	.comparitor = max_compare
};

static struct prcmu_qos_object arm_khz_qos = {
	.requirements =	{
		LIST_HEAD_INIT(arm_khz_qos.requirements.list)
	},
	/*
	 * No notifier on ARM kHz qos request, since this won't actually
	 * do anything, except changing limits for cpufreq
	 */
	.name = "arm_khz",
	/* Notice that arm is in kHz, not in % */
	.force_value = 0,
	.comparitor = max_compare
};

static struct prcmu_qos_object *prcmu_qos_array[] = {
	&null_qos,
	&ape_opp_qos,
	&ddr_opp_qos,
	&arm_khz_qos,
	&vsafe_opp_qos,
};

static DEFINE_MUTEX(prcmu_qos_mutex);
static DEFINE_SPINLOCK(prcmu_qos_lock);

static bool ape_opp_50_partly_25_enabled;

#define CPUFREQ_OPP_DELAY (HZ/5)
#define CPUFREQ_OPP_DELAY_VOICECALL HZ
static unsigned long cpufreq_opp_delay = CPUFREQ_OPP_DELAY;

static bool prcmu_qos_cpufreq_init_done;

static bool lpa_override_enabled;
static u8 opp50_voltage_val;

unsigned long prcmu_qos_get_cpufreq_opp_delay(void)
{
	return cpufreq_opp_delay;
}

static struct notifier_block qos_delayed_cpufreq_notifier_block = {
	.notifier_call = qos_delayed_cpufreq_notifier,
};

void prcmu_qos_set_cpufreq_opp_delay(unsigned long n)
{
	if (n == 0) {
		cpufreq_unregister_notifier(&qos_delayed_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);
		prcmu_qos_update_requirement(PRCMU_QOS_DDR_OPP, "cpufreq",
					     PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "cpufreq",
					     PRCMU_QOS_DEFAULT_VALUE);
		cpufreq_requirement_set = PRCMU_QOS_DEFAULT_VALUE;
		cpufreq_requirement_queued = PRCMU_QOS_DEFAULT_VALUE;
	} else if (cpufreq_opp_delay != 0) {
		cpufreq_register_notifier(&qos_delayed_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);
	}
	cpufreq_opp_delay = n;
}
#ifdef CONFIG_CPU_FREQ
static void update_cpu_limits(s32 min_freq)
{
	int cpu;
	struct cpufreq_policy policy;
	int ret;

	for_each_online_cpu(cpu) {
		ret = cpufreq_get_policy(&policy, cpu);
		if (ret) {
			pr_err("prcmu qos: get cpufreq policy failed (cpu%d)\n",
			       cpu);
			continue;
		}

		ret = cpufreq_update_freq(cpu, min_freq, policy.max);
		if (ret)
			pr_err("prcmu qos: update cpufreq "
			       "frequency limits failed\n");
	}
}
#else
static inline void update_cpu_limits(s32 min_freq) { }
#endif
/* static helper function */
static s32 max_compare(s32 v1, s32 v2)
{
	return max(v1, v2);
}

static inline void __prcmu_qos_update_ddr_opp(s32 arm_kz_new_value,
		s32 vsafe_new_value)
{
	if (cpu_is_u9540()) {
		__prcmu_qos_update_requirement(PRCMU_QOS_ARM_KHZ,
				"cross_opp_ddr", arm_kz_new_value, false);
		__prcmu_qos_update_requirement(PRCMU_QOS_VSAFE_OPP,
				"cross_opp_ddr", vsafe_new_value, false);
	}
}


static void update_target(int target, bool sem)
{
	static int recursivity;
	s32 extreme_value;
	struct requirement_list *node;
	unsigned long flags;
	bool update = false;
	u8 op;

	if (sem)
		mutex_lock(&prcmu_qos_mutex);

	BUG_ON(recursivity++ == update_target_max_recur);

	spin_lock_irqsave(&prcmu_qos_lock, flags);
	extreme_value = prcmu_qos_array[target]->default_value;

	if (prcmu_qos_array[target]->force_value != 0) {
		extreme_value = prcmu_qos_array[target]->force_value;
		update = true;
	} else {
		list_for_each_entry(node,
				    &prcmu_qos_array[target]->requirements.list,
				    list) {
			extreme_value = prcmu_qos_array[target]->comparitor(
				extreme_value, node->value);
		}
		if (atomic_read(&prcmu_qos_array[target]->target_value)
		    != extreme_value) {
			update = true;
			atomic_set(&prcmu_qos_array[target]->target_value,
				   extreme_value);
			pr_debug("prcmu qos: new target for qos %d is %d\n",
				 target, atomic_read(
					 &prcmu_qos_array[target]->target_value
					 ));
		}
	}

	spin_unlock_irqrestore(&prcmu_qos_lock, flags);

	if (!update)
		goto unlock_and_return;

	if (prcmu_qos_array[target]->notifiers)
		blocking_notifier_call_chain(prcmu_qos_array[target]->notifiers,
					     (unsigned long)extreme_value,
					     NULL);
	switch (target) {
	case PRCMU_QOS_DDR_OPP:
		switch (extreme_value) {
		case 50:
			op = DDR_50_OPP;
			prcmu_set_ddr_opp(op);
			pr_debug("prcmu qos: set ddr opp to 50%%\n");
			/*
			 * 9540 cross table matrix :
			 * release ARM & vsafe constraint
			 */
			__prcmu_qos_update_ddr_opp(PRCMU_QOS_DEFAULT_VALUE,
					PRCMU_QOS_DEFAULT_VALUE);
			break;
		case 100:
			/*
			 * 9540 cross table matrix:set vsafe to 100% and
			 * ARM  freq min to 400000
			 */
			__prcmu_qos_update_ddr_opp(400000, 100);
			op = DDR_100_OPP;
			prcmu_set_ddr_opp(op);
			pr_debug("prcmu qos: set ddr opp to 100%%\n");
			break;
		case 25:
			/* 25% DDR OPP is not supported on 5500 */
			if (!cpu_is_u5500()) {
				op = DDR_25_OPP;
				prcmu_set_ddr_opp(op);
				pr_debug("prcmu qos: set ddr opp to 25%%\n");
				/*
				 * 9540 cross table matrix :
				 * release ARM constraint
				 * and set vsafe opp to 50%
				 */
				__prcmu_qos_update_ddr_opp
					(PRCMU_QOS_DEFAULT_VALUE,
						PRCMU_QOS_DEFAULT_VALUE);
				}
				break;
		default:
			pr_err("prcmu qos: Incorrect ddr target value (%d)",
			       extreme_value);
			goto unlock_and_return;
		}
		prcmu_debug_ddr_opp_log(op);
		break;
	case PRCMU_QOS_VSAFE_OPP:
			switch (extreme_value) {
			case 50:
				op = VSAFE_50_OPP;
				pr_debug("prcmu qos: set vsafe opp to 50%%\n");
				break;
			case 100:
				op = VSAFE_100_OPP;
				pr_debug("prcmu qos: set vsafe opp to 100%%\n");
				break;
			default:
			  pr_err("prcmu qos: Incorrect vsafe target value (%d)",
				       extreme_value);
				goto unlock_and_return;
			}
			prcmu_set_vsafe_opp(op);
			prcmu_debug_vsafe_opp_log(op);
			break;
	case PRCMU_QOS_APE_OPP:
		switch (extreme_value) {
		case 50:
			if (ape_opp_50_partly_25_enabled)
				op = APE_50_PARTLY_25_OPP;
			else
				op = APE_50_OPP;
			pr_debug("prcmu qos: set ape opp to 50%%\n");

			/* 9540 cross table matrix : release ARM constraint */
			if (cpu_is_u9540()) {
				__prcmu_qos_update_requirement(
					PRCMU_QOS_ARM_KHZ, "cross_opp_ape",
					PRCMU_QOS_DEFAULT_VALUE, false);
			}
			break;
		case 100:
			/* 9540 cross table matrix: set ARM min freq to 400000 */
			if (cpu_is_u9540()) {
				__prcmu_qos_update_requirement(
					PRCMU_QOS_ARM_KHZ, "cross_opp_ape",
					400000, false);
			}
			op = APE_100_OPP;
			pr_debug("prcmu qos: set ape opp to 100%%\n");
			break;
		default:
			pr_err("prcmu qos: Incorrect ape target value (%d)",
			       extreme_value);
			goto unlock_and_return;
		}
		(void)prcmu_set_ape_opp(op);
		prcmu_debug_ape_opp_log(op);
		break;
	case PRCMU_QOS_ARM_KHZ:
		recursivity--;
		if (sem)
			mutex_unlock(&prcmu_qos_mutex);
		/*
		 * We can't hold the mutex since changing cpufreq
		 * will trigger an prcmu fw callback.
		 */
		update_cpu_limits(extreme_value);
		/* Return since the lock is unlocked */
		return;
		break;
	default:
		pr_err("prcmu qos: Incorrect target\n");
		break;
	}

unlock_and_return:
	recursivity--;
	if (sem)
		mutex_unlock(&prcmu_qos_mutex);
}

void prcmu_qos_force_opp(int prcmu_qos_class, s32 i)
{
	prcmu_qos_array[prcmu_qos_class]->force_value = i;
	update_target(prcmu_qos_class, true);
}

#define LPA_OVERRIDE_VOLTAGE_SETTING 0x22 /* 1.125V */

int prcmu_qos_lpa_override(bool enable)
{
	int ret = 0;

	mutex_lock(&prcmu_qos_mutex);

	if (enable) {
		if (!lpa_override_enabled) {
			u8 opp100_voltage_val;
			u8 override_voltage_val;

			/* Get the APE OPP 100% setting. */
			ret = prcmu_abb_read(AB8500_REGU_CTRL2,
					     AB8500_VAPESEL1_REG,
					     &opp100_voltage_val, 1);
			if (ret)
				goto out;

			/* Save the APE OPP 50% setting. */
			ret = prcmu_abb_read(AB8500_REGU_CTRL2,
					     AB8500_VAPESEL2_REG,
					     &opp50_voltage_val, 1);
			if (ret)
				goto out;

			override_voltage_val = min(opp100_voltage_val,
						(u8)LPA_OVERRIDE_VOLTAGE_SETTING);

			/* Use the APE OPP 100% setting also for APE OPP 50%. */
			ret = prcmu_abb_write(AB8500_REGU_CTRL2,
					      AB8500_VAPESEL2_REG,
					      &override_voltage_val, 1);

			lpa_override_enabled = true;
		}
	} else {
		if (lpa_override_enabled) {
			/* Restore the original APE OPP 50% setting. */
			ret = prcmu_abb_write(AB8500_REGU_CTRL2,
					      AB8500_VAPESEL2_REG,
					      &opp50_voltage_val, 1);

			lpa_override_enabled = false;
		}
	}
out:
	mutex_unlock(&prcmu_qos_mutex);
	return ret;
}

void prcmu_qos_voice_call_override(bool enable)
{
	int ape_opp;

	mutex_lock(&prcmu_qos_mutex);

	/*
	 * All changes done through debugfs will be lost when voice-call is
	 * enabled.
	 */
	if (enable)
		cpufreq_opp_delay = CPUFREQ_OPP_DELAY_VOICECALL;
	else
		cpufreq_opp_delay = CPUFREQ_OPP_DELAY;

	ape_opp_50_partly_25_enabled = enable;

	ape_opp = prcmu_get_ape_opp();

	if (ape_opp == APE_50_OPP) {
		if (enable)
			prcmu_set_ape_opp(APE_50_PARTLY_25_OPP);
		else
			prcmu_set_ape_opp(APE_50_OPP);
	}

	mutex_unlock(&prcmu_qos_mutex);
}

/**
 * prcmu_qos_requirement - returns current prcmu qos expectation
 * @prcmu_qos_class: identification of which qos value is requested
 *
 * This function returns the current target value in an atomic manner.
 */
int prcmu_qos_requirement(int prcmu_qos_class)
{
	return atomic_read(&prcmu_qos_array[prcmu_qos_class]->target_value);
}
EXPORT_SYMBOL_GPL(prcmu_qos_requirement);

/**
 * prcmu_qos_add_requirement - inserts new qos request into the list
 * @prcmu_qos_class: identifies which list of qos request to us
 * @name: identifies the request
 * @value: defines the qos request
 * @sem: manage update_target recursivity
 *
 * This function inserts a new entry in the prcmu_qos_class list of requested
 * qos performance characteristics.  It recomputes the aggregate QoS
 * expectations for the prcmu_qos_class of parameters.
 */
static int __prcmu_qos_add_requirement(int prcmu_qos_class, char *name,
		s32 value, bool sem)
{
	struct requirement_list *dep;
	unsigned long flags;

	dep = kzalloc(sizeof(struct requirement_list), GFP_KERNEL);
	if (dep == NULL)
		return -ENOMEM;

	if (value == PRCMU_QOS_DEFAULT_VALUE)
		dep->value = prcmu_qos_array[prcmu_qos_class]->default_value;
	else if (value == PRCMU_QOS_MAX_VALUE)
		dep->value = prcmu_qos_array[prcmu_qos_class]->max_value;
	else
		dep->value = value;
	dep->name = kstrdup(name, GFP_KERNEL);
	if (!dep->name)
		goto cleanup;

	spin_lock_irqsave(&prcmu_qos_lock, flags);
	list_add(&dep->list,
		 &prcmu_qos_array[prcmu_qos_class]->requirements.list);
	spin_unlock_irqrestore(&prcmu_qos_lock, flags);

	if (!prcmu_qos_cpufreq_init_done && prcmu_qos_class == PRCMU_QOS_ARM_KHZ) {
		if (value != PRCMU_QOS_DEFAULT_VALUE) {
			pr_err("prcmu-qos: ERROR: Not possible to request any "
				"other kHz than DEFAULT during boot!\n");
			dump_stack();
		}
	} else {
		update_target(prcmu_qos_class, sem);
	}

	return 0;

cleanup:
	kfree(dep);
	return -ENOMEM;
}

int prcmu_qos_add_requirement(int prcmu_qos_class, char *name, s32 val)
{
	return __prcmu_qos_add_requirement(prcmu_qos_class, name, val, true);
}
EXPORT_SYMBOL_GPL(prcmu_qos_add_requirement);


/**
 * prcmu_qos_update_requirement - modifies an existing qos request
 * @prcmu_qos_class: identifies which list of qos request to us
 * @name: identifies the request
 * @value: defines the qos request
 * @sem: manage update_target recursivity
 *
 * Updates an existing qos requirement for the prcmu_qos_class of parameters
 * along with updating the target prcmu_qos_class value.
 *
 * If the named request isn't in the list then no change is made.
 */
static int __prcmu_qos_update_requirement(int prcmu_qos_class, char *name,
		s32 new_value, bool sem)
{
	unsigned long flags;
	struct requirement_list *node;
	int pending_update = 0;

	spin_lock_irqsave(&prcmu_qos_lock, flags);
	list_for_each_entry(node,
		&prcmu_qos_array[prcmu_qos_class]->requirements.list, list) {
		if (strcmp(node->name, name))
			continue;

		if (new_value == PRCMU_QOS_DEFAULT_VALUE)
			node->value =
				prcmu_qos_array[prcmu_qos_class]->default_value;
		else if (new_value == PRCMU_QOS_MAX_VALUE)
			node->value =
				prcmu_qos_array[prcmu_qos_class]->max_value;
		else
			node->value = new_value;
		pending_update = 1;
		break;
	}
	spin_unlock_irqrestore(&prcmu_qos_lock, flags);

	if (pending_update) {
		if (!prcmu_qos_cpufreq_init_done && prcmu_qos_class == PRCMU_QOS_ARM_KHZ) {
			if (new_value != PRCMU_QOS_DEFAULT_VALUE) {
				pr_err("prcmu-qos: ERROR: Not possible to request any "
					"other kHz than DEFAULT during boot!\n");
				dump_stack();
			}
		} else {
			update_target(prcmu_qos_class, sem);
		}
	}

	return 0;
}

int prcmu_qos_update_requirement(int prcmu_qos_class, char *name,
		s32 val)
{

	return __prcmu_qos_update_requirement(prcmu_qos_class, name,
			val, true);
}
EXPORT_SYMBOL_GPL(prcmu_qos_update_requirement);


/**
 * prcmu_qos_remove_requirement - modifies an existing qos request
 * @prcmu_qos_class: identifies which list of qos request to us
 * @name: identifies the request
 * @sem: manage update_target recursivity
 *
 * Will remove named qos request from prcmu_qos_class list of parameters and
 * recompute the current target value for the prcmu_qos_class.
 */
static void __prcmu_qos_remove_requirement(int prcmu_qos_class, char *name,
		bool sem)
{
	unsigned long flags;
	struct requirement_list *node;
	int pending_update = 0;

	spin_lock_irqsave(&prcmu_qos_lock, flags);
	list_for_each_entry(node,
		&prcmu_qos_array[prcmu_qos_class]->requirements.list, list) {
		if (strcmp(node->name, name) == 0) {
			kfree(node->name);
			list_del(&node->list);
			kfree(node);
			pending_update = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&prcmu_qos_lock, flags);

	if (pending_update &&
		((prcmu_qos_cpufreq_init_done && prcmu_qos_class == PRCMU_QOS_ARM_KHZ) ||
		prcmu_qos_class != PRCMU_QOS_ARM_KHZ) )
		update_target(prcmu_qos_class, sem);
}

void prcmu_qos_remove_requirement(int prcmu_qos_class, char *name)
{
	__prcmu_qos_remove_requirement(prcmu_qos_class, name, true);
}

EXPORT_SYMBOL_GPL(prcmu_qos_remove_requirement);

/**
 * prcmu_qos_add_notifier - sets notification entry for changes to target value
 * @prcmu_qos_class: identifies which qos target changes should be notified.
 * @notifier: notifier block managed by caller.
 *
 * will register the notifier into a notification chain that gets called
 * upon changes to the prcmu_qos_class target value.
 */
int prcmu_qos_add_notifier(int prcmu_qos_class, struct notifier_block *notifier)
{
	int retval = -EINVAL;

	if (prcmu_qos_array[prcmu_qos_class]->notifiers)
		retval = blocking_notifier_chain_register(
			prcmu_qos_array[prcmu_qos_class]->notifiers, notifier);

	return retval;
}
EXPORT_SYMBOL_GPL(prcmu_qos_add_notifier);

/**
 * prcmu_qos_remove_notifier - deletes notification entry from chain.
 * @prcmu_qos_class: identifies which qos target changes are notified.
 * @notifier: notifier block to be removed.
 *
 * will remove the notifier from the notification chain that gets called
 * upon changes to the prcmu_qos_class target value.
 */
int prcmu_qos_remove_notifier(int prcmu_qos_class,
			      struct notifier_block *notifier)
{
	int retval = -EINVAL;
	if (prcmu_qos_array[prcmu_qos_class]->notifiers)
		retval = blocking_notifier_chain_unregister(
			prcmu_qos_array[prcmu_qos_class]->notifiers, notifier);

	return retval;
}
EXPORT_SYMBOL_GPL(prcmu_qos_remove_notifier);

#define USER_QOS_NAME_LEN 32

static int prcmu_qos_power_open(struct inode *inode, struct file *filp,
				long prcmu_qos_class)
{
	int ret;
	char name[USER_QOS_NAME_LEN];

	filp->private_data = (void *)prcmu_qos_class;
	snprintf(name, USER_QOS_NAME_LEN, "file_%08x", (unsigned int)filp);

	ret = prcmu_qos_add_requirement(prcmu_qos_class, name,
					PRCMU_QOS_DEFAULT_VALUE);
	if (ret >= 0)
		return 0;

	return -EPERM;
}


static int prcmu_qos_ape_power_open(struct inode *inode, struct file *filp)
{
	return prcmu_qos_power_open(inode, filp, PRCMU_QOS_APE_OPP);
}

static int prcmu_qos_ddr_power_open(struct inode *inode, struct file *filp)
{
	return prcmu_qos_power_open(inode, filp, PRCMU_QOS_DDR_OPP);
}

static int prcmu_qos_vsafe_power_open(struct inode *inode, struct file *filp)
{
	return prcmu_qos_power_open(inode, filp, PRCMU_QOS_VSAFE_OPP);
}

static int prcmu_qos_arm_power_open(struct inode *inode, struct file *filp)
{
	return prcmu_qos_power_open(inode, filp, PRCMU_QOS_ARM_KHZ);
}

static int prcmu_qos_power_release(struct inode *inode, struct file *filp)
{
	int prcmu_qos_class;
	char name[USER_QOS_NAME_LEN];

	prcmu_qos_class = (long)filp->private_data;
	snprintf(name, USER_QOS_NAME_LEN, "file_%08x", (unsigned int)filp);
	prcmu_qos_remove_requirement(prcmu_qos_class, name);

	return 0;
}

static ssize_t prcmu_qos_power_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	s32 value;
	int prcmu_qos_class;
	char name[USER_QOS_NAME_LEN];

	prcmu_qos_class = (long)filp->private_data;
	if (count != sizeof(s32))
		return -EINVAL;
	if (copy_from_user(&value, buf, sizeof(s32)))
		return -EFAULT;
	snprintf(name, USER_QOS_NAME_LEN, "file_%08x", (unsigned int)filp);

	prcmu_qos_update_requirement(prcmu_qos_class, name, value);

	return  sizeof(s32);
}

/* Functions to provide QoS to user space */
static const struct file_operations prcmu_qos_ape_power_fops = {
	.write = prcmu_qos_power_write,
	.open = prcmu_qos_ape_power_open,
	.release = prcmu_qos_power_release,
};

/* Functions to provide QoS to user space */
static const struct file_operations prcmu_qos_ddr_power_fops = {
	.write = prcmu_qos_power_write,
	.open = prcmu_qos_ddr_power_open,
	.release = prcmu_qos_power_release,
};

/* Functions to provide QoS to user space */
static const struct file_operations prcmu_qos_vsafe_power_fops = {
	.write = prcmu_qos_power_write,
	.open = prcmu_qos_vsafe_power_open,
	.release = prcmu_qos_power_release,
};

static const struct file_operations prcmu_qos_arm_power_fops = {
	.write = prcmu_qos_power_write,
	.open = prcmu_qos_arm_power_open,
	.release = prcmu_qos_power_release,
};

static int register_prcmu_qos_misc(struct prcmu_qos_object *qos,
				   const struct file_operations *fops)
{
	qos->prcmu_qos_power_miscdev.minor = MISC_DYNAMIC_MINOR;
	qos->prcmu_qos_power_miscdev.name = qos->name;
	qos->prcmu_qos_power_miscdev.fops = fops;

	return misc_register(&qos->prcmu_qos_power_miscdev);
}

static void qos_delayed_work_up_fn(struct work_struct *work)
{
	prcmu_qos_update_requirement(PRCMU_QOS_DDR_OPP, "cpufreq",
				     PRCMU_QOS_DDR_OPP_MAX);
	prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "cpufreq",
				     PRCMU_QOS_APE_OPP_MAX);
	cpufreq_requirement_set = PRCMU_QOS_MAX_VALUE;
}

static void qos_delayed_work_down_fn(struct work_struct *work)
{
	prcmu_qos_update_requirement(PRCMU_QOS_DDR_OPP, "cpufreq",
				     PRCMU_QOS_DEFAULT_VALUE);
	prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "cpufreq",
				     PRCMU_QOS_DEFAULT_VALUE);
	cpufreq_requirement_set = PRCMU_QOS_DEFAULT_VALUE;
}

static DECLARE_DELAYED_WORK(qos_delayed_work_up, qos_delayed_work_up_fn);
static DECLARE_DELAYED_WORK(qos_delayed_work_down, qos_delayed_work_down_fn);

static int qos_delayed_cpufreq_notifier(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct cpufreq_freqs *freq = data;
	s32 new_ddr_target;

	/* Only react once per transition and only for one core, e.g. core 0 */
	if (event != CPUFREQ_POSTCHANGE || freq->cpu != 0)
		return 0;

	/*
	 * APE and DDR OPP are always handled together in this solution.
	 * Hence no need to check both DDR and APE opp in the code below.
	 */

	/* Which DDR OPP are we aiming for? */
	if (freq->new > ARM_THRESHOLD_FREQ)
		new_ddr_target = PRCMU_QOS_DDR_OPP_MAX;
	else
		new_ddr_target = PRCMU_QOS_DEFAULT_VALUE;

	if (new_ddr_target == cpufreq_requirement_queued) {
		/*
		 * We're already at, or going to, the target requirement.
		 * This is only a fluctuation within the interval
		 * corresponding to the same DDR requirement.
		 */
		return 0;
	}
	cpufreq_requirement_queued = new_ddr_target;

	if (freq->new > ARM_THRESHOLD_FREQ) {
		cancel_delayed_work_sync(&qos_delayed_work_down);
		/*
		 * Only schedule this requirement if it is not the current
		 * one.
		 */
		if (new_ddr_target != cpufreq_requirement_set)
			schedule_delayed_work(&qos_delayed_work_up,
					      cpufreq_opp_delay);
	} else {
		cancel_delayed_work_sync(&qos_delayed_work_up);
		/*
		 * Only schedule this requirement if it is not the current
		 * one.
		 */
		if (new_ddr_target != cpufreq_requirement_set)
			schedule_delayed_work(&qos_delayed_work_down,
					      cpufreq_opp_delay);
	}

	return 0;
}

static int __init prcmu_qos_power_preinit(void)
{
	/* 25% DDR OPP is not supported on u5500 */
	if (cpu_is_u5500())
		ddr_opp_qos.default_value = 50;
	return 0;

}
arch_initcall(prcmu_qos_power_preinit);

static int __init prcmu_qos_power_init(void)
{
	int ret;
	struct cpufreq_frequency_table *table;
	unsigned int min_freq = UINT_MAX;
	unsigned int max_freq = 0;
	int i;

	if (cpu_is_u9540())
		update_target_max_recur = 2;

	table = cpufreq_frequency_get_table(0);

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (min_freq > table[i].frequency)
			min_freq = table[i].frequency;
		if (max_freq < table[i].frequency)
			max_freq = table[i].frequency;
	}

	arm_khz_qos.max_value = max_freq;
	arm_khz_qos.default_value = min_freq;
	/* CPUs start at max */
	atomic_set(&arm_khz_qos.target_value, arm_khz_qos.max_value);

	prcmu_qos_cpufreq_init_done = true;

	ret = register_prcmu_qos_misc(&ape_opp_qos, &prcmu_qos_ape_power_fops);
	if (ret < 0) {
		pr_err("prcmu ape qos: setup failed\n");
		return ret;
	}

	ret = register_prcmu_qos_misc(&ddr_opp_qos, &prcmu_qos_ddr_power_fops);
	if (ret < 0) {
		pr_err("prcmu ddr qos: setup failed\n");
		goto ddr_opp_qos_error;
	}

	ret = register_prcmu_qos_misc(&arm_khz_qos, &prcmu_qos_arm_power_fops);
	if (ret < 0) {
		pr_err("prcmu arm qos: setup failed\n");
		goto arm_khz_qos_error;
	}

	if (cpu_is_u9540()) {
		ret = register_prcmu_qos_misc(&vsafe_opp_qos,
				&prcmu_qos_vsafe_power_fops);
		if (ret < 0) {
			pr_err("prcmu vsafe qos: setup failed\n");
			goto vsafe_opp_qos_error;
		}
	}

	prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP, "cpufreq",
			PRCMU_QOS_DEFAULT_VALUE);
	prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP, "cpufreq",
			PRCMU_QOS_DEFAULT_VALUE);
	cpufreq_requirement_set = PRCMU_QOS_DEFAULT_VALUE;
	cpufreq_requirement_queued = PRCMU_QOS_DEFAULT_VALUE;
	cpufreq_register_notifier(&qos_delayed_cpufreq_notifier_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	if (cpu_is_u9540()) {
		prcmu_qos_add_requirement(PRCMU_QOS_ARM_KHZ, "cpufreq",
				PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_add_requirement(PRCMU_QOS_VSAFE_OPP, "cross_opp_ddr",
				PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_add_requirement(PRCMU_QOS_ARM_KHZ, "cross_opp_ddr",
				PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_add_requirement(PRCMU_QOS_ARM_KHZ, "cross_opp_ape",
				PRCMU_QOS_DEFAULT_VALUE);
		prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP, "cross_opp_arm",
				PRCMU_QOS_DEFAULT_VALUE);
	}

	return ret;

vsafe_opp_qos_error: 
	misc_deregister(&arm_khz_qos.prcmu_qos_power_miscdev); 
arm_khz_qos_error: 
	misc_deregister(&ddr_opp_qos.prcmu_qos_power_miscdev); 
ddr_opp_qos_error: 
	misc_deregister(&ape_opp_qos.prcmu_qos_power_miscdev); 
	return ret;
}
late_initcall(prcmu_qos_power_init);
