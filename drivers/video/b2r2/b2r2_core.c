/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 core driver
 *
 * Author: Robert Fekete <robert.fekete@stericsson.com>
 * Author: Paul Wannback
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

/*
 * TODO: Clock address from platform data
 * Platform data should have string id instead of numbers
 * b2r2_remove, some type of runtime problem when kernel hacking
 * debug features on
 *
 * Is there already a priority list in kernel?
 * Is it possible to handle clock using clock framework?
 * uTimeOut, use mdelay instead?
 * Measure performance
 *
 * Exchange our home-cooked ref count with kernel kref? See
 *    http://lwn.net/Articles/336224/
 *
 * B2R2:
 * Source fill 2 bug
 * Check with Symbian?
 */

/* include file */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kref.h>
#include <linux/ktime.h>

#include "b2r2_internal.h"
#include "b2r2_core.h"
#include "b2r2_global.h"
#include "b2r2_structures.h"
#include "b2r2_control.h"
#include "b2r2_profiler_api.h"
#include "b2r2_timing.h"
#include "b2r2_debug.h"

/**
 * B2R2 Hardware defines below
 */

/* - BLT_AQ_CTL */
#define	B2R2_AQ_Enab			(0x80000000)
#define B2R2_AQ_PRIOR_0 		(0x0)
#define	B2R2_AQ_PRIOR_1 		(0x1)
#define	B2R2_AQ_PRIOR_2			(0x2)
#define	B2R2_AQ_PRIOR_3			(0x3)
#define B2R2_AQ_NODE_REPEAT_INT	(0x100000)
#define B2R2_AQ_STOP_INT		(0x200000)
#define B2R2_AQ_LNA_REACH_INT	(0x400000)
#define B2R2_AQ_COMPLETED_INT	(0x800000)

/* - BLT_CTL */
#define	B2R2BLT_CTLGLOBAL_soft_reset	(0x80000000)
#define	B2R2BLT_CTLStep_By_Step		(0x20000000)
#define	B2R2BLT_CTLBig_not_little	(0x10000000)
#define	B2R2BLT_CTLMask			(0xb0000000)
#define	B2R2BLT_CTLTestMask		(0xb0000000)
#define	B2R2BLT_CTLInitialValue		(0x0)
#define	B2R2BLT_CTLAccessType		(INITIAL_TEST)
#define	B2R2BLT_CTL			(0xa00)

/* - BLT_ITS */
#define	B2R2BLT_ITSRLD_ERROR		(0x80000000)
#define	B2R2BLT_ITSAQ4_Node_Notif	(0x8000000)
#define	B2R2BLT_ITSAQ4_Node_repeat	(0x4000000)
#define	B2R2BLT_ITSAQ4_Stopped		(0x2000000)
#define	B2R2BLT_ITSAQ4_LNA_Reached	(0x1000000)
#define	B2R2BLT_ITSAQ3_Node_Notif	(0x800000)
#define	B2R2BLT_ITSAQ3_Node_repeat	(0x400000)
#define	B2R2BLT_ITSAQ3_Stopped		(0x200000)
#define	B2R2BLT_ITSAQ3_LNA_Reached	(0x100000)
#define	B2R2BLT_ITSAQ2_Node_Notif	(0x80000)
#define	B2R2BLT_ITSAQ2_Node_repeat	(0x40000)
#define	B2R2BLT_ITSAQ2_Stopped		(0x20000)
#define	B2R2BLT_ITSAQ2_LNA_Reached	(0x10000)
#define	B2R2BLT_ITSAQ1_Node_Notif	(0x8000)
#define	B2R2BLT_ITSAQ1_Node_repeat	(0x4000)
#define	B2R2BLT_ITSAQ1_Stopped		(0x2000)
#define	B2R2BLT_ITSAQ1_LNA_Reached	(0x1000)
#define	B2R2BLT_ITSCQ2_Repaced		(0x80)
#define	B2R2BLT_ITSCQ2_Node_Notif	(0x40)
#define	B2R2BLT_ITSCQ2_retriggered	(0x20)
#define	B2R2BLT_ITSCQ2_completed	(0x10)
#define	B2R2BLT_ITSCQ1_Repaced		(0x8)
#define	B2R2BLT_ITSCQ1_Node_Notif	(0x4)
#define	B2R2BLT_ITSCQ1_retriggered	(0x2)
#define	B2R2BLT_ITSCQ1_completed	(0x1)
#define	B2R2BLT_ITSMask			(0x8ffff0ff)
#define	B2R2BLT_ITSTestMask		(0x8ffff0ff)
#define	B2R2BLT_ITSInitialValue		(0x0)
#define	B2R2BLT_ITSAccessType		(INITIAL_TEST)
#define	B2R2BLT_ITS			(0xa04)

/* - BLT_STA1 */
#define	B2R2BLT_STA1BDISP_IDLE		(0x1)
#define	B2R2BLT_STA1Mask		(0x1)
#define	B2R2BLT_STA1TestMask		(0x1)
#define	B2R2BLT_STA1InitialValue	(0x1)
#define	B2R2BLT_STA1AccessType		(INITIAL_TEST)
#define	B2R2BLT_STA1			(0xa08)

/**
 * b2r2_core - Quick link to administration data for B2R2
 */
static struct b2r2_core   *b2r2_core[B2R2_MAX_NBR_DEVICES];

/* Local functions */
static void check_prio_list(struct b2r2_core *core, bool atomic);
static void  clear_interrupts(struct b2r2_core *core);
static void trigger_job(struct b2r2_core *core, struct b2r2_core_job *job);
static void exit_job_list(struct b2r2_core *core,
		struct list_head *job_list);
static void job_work_function(struct work_struct *ptr);
static void init_job(struct b2r2_core_job *job);
static void insert_into_prio_list(struct b2r2_core *core,
		struct b2r2_core_job *job);
static struct b2r2_core_job *find_job_in_list(int job_id,
		struct list_head *list);
static struct b2r2_core_job *find_job_in_active_jobs(struct b2r2_core *core,
		int job_id);
static struct b2r2_core_job *find_tag_in_list(struct b2r2_core *core,
		int tag, struct list_head *list);
static struct b2r2_core_job *find_tag_in_active_jobs(struct b2r2_core *core,
		int tag);

static int domain_enable(struct b2r2_core *core);
static void domain_disable(struct b2r2_core *core);

static void stop_queue(enum b2r2_core_queue queue);

#ifdef HANDLE_TIMEOUTED_JOBS
static void printk_regs(struct b2r2_core *core);
static int hw_reset(struct b2r2_core *core);
static void timeout_work_function(struct work_struct *ptr);
#endif

static void reset_hw_timer(struct b2r2_core_job *job);
static void start_hw_timer(struct b2r2_core_job *job);
static void stop_hw_timer(struct b2r2_core *core,
		struct b2r2_core_job *job);

static int init_hw(struct b2r2_core *core);
static void exit_hw(struct b2r2_core *core);

/* Tracking release bug... */
#ifdef DEBUG_CHECK_ADDREF_RELEASE
/**
 * ar_add() - Adds an addref or a release to the array
 *
 * @core: The b2r2 core entity
 * @job: The job that has been referenced
 * @caller: The caller of addref / release
 * @addref: true if it is an addref else false for release
 */
static void ar_add(struct b2r2_core *core, struct b2r2_core_job *job,
		const char *caller, bool addref)
{
	core->ar[core->ar_write].addref = addref;
	core->ar[core->ar_write].job = job;
	core->ar[core->ar_write].caller = caller;
	core->ar[core->ar_write].ref_count = job->ref_count;
	core->ar_write = (core->ar_write + 1) %
		ARRAY_SIZE(core->ar);
	if (core->ar_write == core->ar_read)
		core->ar_read = (core->ar_read + 1) %
			ARRAY_SIZE(core->ar);
}

/**
 * printk_ar() - Writes all addref / release using dev_info
 *
 * @core: The b2r2 core entity
 * @job: Which job to write or NULL for all
 */
static void printk_ar(struct b2r2_core *core, struct b2r2_core_job *job)
{
	int i;

	for (i = core->ar_read; i != core->ar_write;
			i = (i + 1) % ARRAY_SIZE(core->ar)) {
		struct addref_release *ar = &core->ar[i];
		if (!job || job == ar->job)
			b2r2_log_info(core->dev, "%s on %p from %s,"
				" ref = %d\n",
				ar->addref ? "addref" : "release",
				ar->job, ar->caller, ar->ref_count);
	}
}
#endif

/**
 * internal_job_addref() - Increments the reference count for a job
 *
 * @core: The b2r2 core entity
 * @job: Which job to increment reference count for
 * @caller: Name of function calling addref (for debug)
 *
 * Note that core->lock _must_ be held
 */
static void internal_job_addref(struct b2r2_core *core,
		struct b2r2_core_job *job, const char *caller)
{
	u32 ref_count;

	/* Sanity checks */
	BUG_ON(core == NULL);
	BUG_ON(job == NULL);

	b2r2_log_info(core->dev, "%s (core: %p, job: %p) (from %s)\n",
		__func__, core, job, caller);


	if (job->start_sentinel != START_SENTINEL ||
			job->end_sentinel != END_SENTINEL ||
			job->ref_count == 0 || job->ref_count > 10)	{
		b2r2_log_info(core->dev, "%s: (core: %p, job: %p) "
			"start=%X end=%X ref_count=%d\n",
			__func__, core, job, job->start_sentinel,
			job->end_sentinel, job->ref_count);

	/* Something is wrong, print the addref / release array */
#ifdef DEBUG_CHECK_ADDREF_RELEASE
		printk_ar(core, NULL);
#endif
	}


	BUG_ON(job->start_sentinel != START_SENTINEL);
	BUG_ON(job->end_sentinel != END_SENTINEL);

	/* Do the actual reference count increment */
	ref_count = ++job->ref_count;

#ifdef DEBUG_CHECK_ADDREF_RELEASE
	/* Keep track of addref / release */
	ar_add(core, job, caller, true);
#endif

	b2r2_log_info(core->dev, "%s called from %s (core: %p, job: %p): Ref "
		"Count is %d\n", __func__, caller, core, job, job->ref_count);
}

/**
 * internal_job_release() - Decrements the reference count for a job
 *
 * @core: The b2r2 core entity
 * @job: Which job to decrement reference count for
 * @caller: Name of function calling release (for debug)
 *
 * Returns true if job_release should be called by caller
 * (reference count reached zero).
 *
 * Note that core->lock _must_ be held
 */
static bool internal_job_release(struct b2r2_core *core,
		struct b2r2_core_job *job, const char *caller)
{
	u32 ref_count;
	bool call_release = false;

	/* Sanity checks */
	BUG_ON(job == NULL);

	b2r2_log_info(core->dev, "%s (core: %p, job: %p) (from %s)\n",
		__func__, core, job, caller);

	if (job->start_sentinel != START_SENTINEL ||
			job->end_sentinel != END_SENTINEL ||
			job->ref_count == 0 || job->ref_count > 10) {
		b2r2_log_info(core->dev, "%s: (core: %p, job: %p) start=%X "
			"end=%X ref_count=%d\n", __func__, core, job,
			job->start_sentinel, job->end_sentinel,
			job->ref_count);

#ifdef DEBUG_CHECK_ADDREF_RELEASE
		printk_ar(core, NULL);
#endif
	}

	BUG_ON(job->start_sentinel != START_SENTINEL);
	BUG_ON(job->end_sentinel != END_SENTINEL);
	BUG_ON(job->ref_count == 0 || job->ref_count > 10);

	/* Do the actual decrement */
	ref_count = --job->ref_count;
#ifdef DEBUG_CHECK_ADDREF_RELEASE
	ar_add(core, job, caller, false);
#endif
	b2r2_log_info(core->dev, "%s called from %s (core: %p, job: %p) "
		"Ref Count is %d\n", __func__, caller, core, job, ref_count);

	if (!ref_count && job->release) {
		call_release = true;
		/* Job will now cease to exist */
		job->start_sentinel = 0xFFFFFFFF;
		job->end_sentinel = 0xFFFFFFFF;
	}
	return call_release;
}



/* Exported functions */

/**
 * core->lock _must_ _NOT_ be held when calling this function
 */
void b2r2_core_job_addref(struct b2r2_core_job *job, const char *caller)
{
	unsigned long flags;
	struct b2r2_core *core;

	BUG_ON(job == NULL || job->data == 0);
	core = (struct b2r2_core *) job->data;

	spin_lock_irqsave(&core->lock, flags);
	internal_job_addref(core, job, caller);
	spin_unlock_irqrestore(&core->lock, flags);
}

/**
 * core->lock _must_ _NOT_ be held when calling this function
 */
void b2r2_core_job_release(struct b2r2_core_job *job, const char *caller)
{
	unsigned long flags;
	bool call_release = false;
	struct b2r2_core *core;

	BUG_ON(job == NULL || job->data == 0);
	core = (struct b2r2_core *) job->data;

	spin_lock_irqsave(&core->lock, flags);
	call_release = internal_job_release(core, job, caller);
	spin_unlock_irqrestore(&core->lock, flags);

	if (call_release)
		job->release(job);
}

/**
 * core->lock _must_ _NOT_ be held when calling this function
 */
int b2r2_core_job_add(struct b2r2_control *control,
		struct b2r2_core_job *job)
{
	unsigned long flags;
	struct b2r2_core *core = control->data;

	b2r2_log_info(core->dev, "%s (core: %p, job: %p)\n",
		__func__, core, job);

	/* Enable B2R2 */
	domain_enable(core);

	spin_lock_irqsave(&core->lock, flags);
	/* Check that we have not been powered down */
	if (!core->domain_enabled) {
		spin_unlock_irqrestore(&core->lock, flags);
		return -ENOSYS;
	}

	core->stat_n_jobs_added++;

	/* Initialise internal job data */
	init_job(job);

	/* Initial reference, should be released by caller of this function */
	job->ref_count = 1;

	/* Insert job into prio list */
	insert_into_prio_list(core, job);

	/* Check if we can dispatch job */
	check_prio_list(core, false);
	spin_unlock_irqrestore(&core->lock, flags);

	return job->job_id;
}

/**
 * core->lock _must_ _NOT_ be held when calling this function
 */
struct b2r2_core_job *b2r2_core_job_find(struct b2r2_control *control,
		int job_id)
{
	unsigned long flags;
	struct b2r2_core_job *job;
	struct b2r2_core *core = control->data;

	b2r2_log_info(core->dev, "%s (core: %p, job_id: %d)\n",
		__func__, core, job_id);

	spin_lock_irqsave(&core->lock, flags);
	/* Look through prio queue */
	job = find_job_in_list(job_id, &core->prio_queue);

	if (!job)
		job = find_job_in_active_jobs(core, job_id);

	spin_unlock_irqrestore(&core->lock, flags);

	return job;
}

/**
 * core->lock _must_ _NOT_ be held when calling this function
 */
struct b2r2_core_job *b2r2_core_job_find_first_with_tag(
		struct b2r2_control *control, int tag)
{
	unsigned long flags;
	struct b2r2_core_job *job;
	struct b2r2_core *core = control->data;

	b2r2_log_info(core->dev,
		"%s (core: %p, tag: %d)\n", __func__, core, tag);

	spin_lock_irqsave(&core->lock, flags);
	/* Look through prio queue */
	job = find_tag_in_list(core, tag, &core->prio_queue);

	if (!job)
		job = find_tag_in_active_jobs(core, tag);

	spin_unlock_irqrestore(&core->lock, flags);

	return job;
}

/**
 * is_job_done() - Spin lock protected check if job is done
 *
 * @job: Job to check
 *
 * Returns true if job is done or cancelled
 *
 * core->lock must _NOT_ be held when calling this function
 */
static bool is_job_done(struct b2r2_core_job *job)
{
	unsigned long flags;
	bool job_is_done;
	struct b2r2_core *core = (struct b2r2_core *) job->data;

	spin_lock_irqsave(&core->lock, flags);
	job_is_done =
		job->job_state != B2R2_CORE_JOB_QUEUED &&
		job->job_state != B2R2_CORE_JOB_RUNNING;
	spin_unlock_irqrestore(&core->lock, flags);

	return job_is_done;
}

/**
 * b2r2_core_job_wait()
 *
 * @job:
 *
 * core->lock _must_ _NOT_ be held when calling this function
 */
int b2r2_core_job_wait(struct b2r2_core_job *job)
{
	int ret = 0;
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_core *core = (struct b2r2_core *) job->data;
#endif

	b2r2_log_info(core->dev, "%s (core: %p, job: %p)\n",
		__func__, core, job);
	/* Check that we have the job */
	if (job->job_state == B2R2_CORE_JOB_IDLE) {
		/* Never or not queued */
		b2r2_log_info(core->dev, "%s: Job not queued\n", __func__);
		return -ENOENT;
	}

	/* Wait for the job to be done */
	ret = wait_event_interruptible(
		job->event,
		is_job_done(job));

	if (ret)
		b2r2_log_warn(core->dev,
			"%s: wait_event_interruptible returns %d state is %d",
			__func__, ret, job->job_state);
	return ret;
}

/**
 * cancel_job() - Cancels a job (removes it from prio list or active jobs) and
 *                calls the job callback
 *
 * @job: Job to cancel
 *
 * Returns true if the job was found and cancelled
 *
 * core->lock must be held when calling this function
 */
static bool cancel_job(struct b2r2_core *core, struct b2r2_core_job *job)
{
	bool found_job = false;
	bool job_was_active = false;

	/* Remove from prio list */
	if (job->job_state == B2R2_CORE_JOB_QUEUED) {
		list_del_init(&job->list);
		found_job = true;
	}

	/* Remove from active jobs */
	if (!found_job && core->n_active_jobs > 0) {
		int i;

		/* Look for timeout:ed jobs and put them in tmp list */
		for (i = 0; i < ARRAY_SIZE(core->active_jobs); i++) {
			if (core->active_jobs[i] == job) {
				stop_queue((enum b2r2_core_queue)i);
				stop_hw_timer(core, job);
				core->active_jobs[i] = NULL;
				core->n_active_jobs--;
				found_job = true;
				job_was_active = true;
			}
		}
	}

	/* Handle done list & callback */
	if (found_job) {
		/* Job is canceled */
		job->job_state = B2R2_CORE_JOB_CANCELED;

		queue_work(core->work_queue, &job->work);

		/* Statistics */
		if (!job_was_active)
			core->stat_n_jobs_in_prio_list--;

	}

	return found_job;
}

/* core->lock _must_ _NOT_ be held when calling this function */
int b2r2_core_job_cancel(struct b2r2_core_job *job)
{
	unsigned long flags;
	int ret = 0;
	struct b2r2_core *core = (struct b2r2_core *) job->data;

	b2r2_log_info(core->dev, "%s (core: %p, job: %p) (st: %d)\n",
		__func__, core, job, job->job_state);
	/* Check that we have the job */
	if (job->job_state == B2R2_CORE_JOB_IDLE) {
		/* Never or not queued */
		b2r2_log_info(core->dev, "%s: Job not queued\n", __func__);
		return -ENOENT;
	}

	/* Remove from prio list */
	spin_lock_irqsave(&core->lock, flags);
	cancel_job(core, job);
	spin_unlock_irqrestore(&core->lock, flags);

	return ret;
}

/* LOCAL FUNCTIONS BELOW */

/**
 * domain_disable_work_function()
 *
 * @core: The b2r2 core entity
 */
static void domain_disable_work_function(struct work_struct *work)
{
	struct delayed_work *twork = to_delayed_work(work);
	struct b2r2_core *core = container_of(
			twork, struct b2r2_core, domain_disable_work);

	if (!mutex_trylock(&core->domain_lock))
		return;

	if (core->domain_request_count == 0) {
		core->valid = false;
		exit_hw(core);
		clk_disable(core->b2r2_clock);
		regulator_disable(core->b2r2_reg);
		/* VANA is tighly coupled to DSS EPOD */
		if (core->vana_reg)
			regulator_disable(core->vana_reg);
		core->domain_enabled = false;
	}

	mutex_unlock(&core->domain_lock);
}

/**
 * domain_enable()
 *
 * @core: The b2r2 core entity
 */
static int domain_enable(struct b2r2_core *core)
{
	mutex_lock(&core->domain_lock);
	if (core->lockdown) {
		mutex_unlock(&core->domain_lock);
		return -ENOSYS;
	}
	core->domain_request_count++;

	if (!core->domain_enabled) {
		int retry = 0;
		int ret;

		/* VANA is tighly coupled to DSS EPOD */
		if (core->vana_reg)
			WARN_ON_ONCE(regulator_enable(core->vana_reg));
again:
		/*
		 * Since regulator_enable() may sleep we have to handle
		 * interrupts.
		 */
		ret = regulator_enable(core->b2r2_reg);
		if ((ret == -EAGAIN) &&
				((retry++) < B2R2_REGULATOR_RETRY_COUNT))
			goto again;
		else if (ret < 0)
			goto regulator_enable_failed;

		ret = clk_enable(core->b2r2_clock);
		if (ret < 0) {
			b2r2_log_err(core->dev,
				"%s: Could not enable clock\n", __func__);
			goto enable_clk_failed;
		}
		if (init_hw(core) < 0)
			goto init_hw_failed;
		core->domain_enabled = true;
		core->valid = true;
	}

	mutex_unlock(&core->domain_lock);

	return 0;

init_hw_failed:
	b2r2_log_err(core->dev,
		"%s: Could not initialize hardware!\n", __func__);
	clk_disable(core->b2r2_clock);

enable_clk_failed:
	if (regulator_disable(core->b2r2_reg) < 0)
		b2r2_log_err(core->dev, "%s: regulator_disable failed!\n",
				__func__);
	if (core->vana_reg)
		WARN_ON_ONCE(regulator_disable(core->vana_reg));

regulator_enable_failed:
	core->domain_request_count--;
	mutex_unlock(&core->domain_lock);

	return -EFAULT;
}

/**
 * domain_disable()
 *
 * @core: The b2r2 core entity
 */
static void domain_disable(struct b2r2_core *core)
{
	mutex_lock(&core->domain_lock);

	if (core->domain_request_count == 0) {
		b2r2_log_err(core->dev,
			"%s: Unbalanced domain_disable()\n", __func__);
	} else {
		core->domain_request_count--;

		/* Cancel any existing work */
		cancel_delayed_work_sync(&core->domain_disable_work);

		/* Add a work to disable the power and clock after a delay */
		queue_delayed_work(core->work_queue, &core->domain_disable_work,
				B2R2_DOMAIN_DISABLE_TIMEOUT);
	}

	mutex_unlock(&core->domain_lock);
}

/**
 * stop_queue() - Stops the specified queue.
 */
static void stop_queue(enum b2r2_core_queue queue)
{
	/* TODO: Implement! If this function is not implemented canceled jobs
	 * will use b2r2 which is a waste of resources. Not stopping jobs will
	 * also screw up the hardware timing, the job the canceled job
	 * intrerrupted (if any) will be billed for the time between the point
	 * where the job is cancelled and when it stops.
	 */
}

/**
 * exit_job_list() - Empties a job queue by canceling the jobs
 *
 * @core: The b2r2 core entity
 *
 * core->lock _must_ be held when calling this function
 */
static void exit_job_list(struct b2r2_core *core,
		struct list_head *job_queue)
{
	while (!list_empty(job_queue)) {
		struct b2r2_core_job *job =
			list_entry(job_queue->next,
				struct b2r2_core_job,
				list);
		/*
		 * Add reference to prevent job from disappearing
		 * in the middle of our work, released below
		 */
		internal_job_addref(core, job, __func__);

		cancel_job(core, job);

		/* Matching release to addref above */
		internal_job_release(core, job, __func__);

	}
}

/**
 * job_work_function() - Work queue function that calls callback(s) and
 *                       checks if B2R2 can accept a new job
 *
 * @ptr: Pointer to work struct (embedded in struct b2r2_core_job)
 */
static void job_work_function(struct work_struct *ptr)
{
	unsigned long flags;
	struct b2r2_core_job *job =
			container_of(ptr, struct b2r2_core_job, work);
	struct b2r2_core *core = (struct b2r2_core *) job->data;

	/* Disable B2R2 */
	domain_disable(core);

	/* Release resources */
	if (job->release_resources)
		job->release_resources(job, false);

	spin_lock_irqsave(&core->lock, flags);

	/* Dispatch a new job if possible */
	check_prio_list(core, false);

	spin_unlock_irqrestore(&core->lock, flags);

	/* Tell the client */
	if (job->callback)
		job->callback(job);

	/*
	 * Drop our reference, matches the
	 * addref in handle_queue_event or b2r2_core_job_cancel
	 */
	b2r2_core_job_release(job, __func__);
}

#ifdef HANDLE_TIMEOUTED_JOBS
/**
 * timeout_work_function() - Work queue function that checks for
 *                           timeout:ed jobs. B2R2 might silently refuse
 *                           to execute some jobs, i.e. SRC2 fill
 *
 * @ptr: Pointer to work struct (embedded in struct b2r2_core)
 *
 */
static void timeout_work_function(struct work_struct *ptr)
{
	unsigned long flags;
	struct list_head job_list;
	struct delayed_work *twork = to_delayed_work(ptr);
	struct b2r2_core *core = container_of(twork, struct b2r2_core,
			timeout_work);

	INIT_LIST_HEAD(&job_list);

	/* Cancel all jobs if too long time since last irq */
	spin_lock_irqsave(&core->lock, flags);
	if (core->n_active_jobs > 0) {
		unsigned long diff =
			(long) jiffies - (long) core->jiffies_last_irq;
		if (diff > JOB_TIMEOUT) {
			/* Active jobs and more than a second since last irq! */
			int i;

			b2r2_core_print_stats(core);

			/*
			 * Look for timeout:ed jobs and put them in tmp list.
			 * It's important that the application queues are
			 * killed in order of decreasing priority
			 */
			for (i = 0; i < ARRAY_SIZE(core->active_jobs); i++) {
				struct b2r2_core_job *job =
					core->active_jobs[i];

				if (job) {
					stop_hw_timer(core, job);
					core->active_jobs[i] = NULL;
					core->n_active_jobs--;
					list_add_tail(&job->list, &job_list);
				}
			}

			/* Print the B2R2 register and reset B2R2 */
			printk_regs(core);
			hw_reset(core);
		}
	}
	spin_unlock_irqrestore(&core->lock, flags);

	/* Handle timeout:ed jobs */
	spin_lock_irqsave(&core->lock, flags);
	while (!list_empty(&job_list)) {
		struct b2r2_core_job *job =
			list_entry(job_list.next,
				struct b2r2_core_job,
				list);

		b2r2_log_warn(core->dev, "%s: Job timeout\n", __func__);

		list_del_init(&job->list);

		/* Job is cancelled */
		job->job_state = B2R2_CORE_JOB_CANCELED;

		/* Handle done */
		wake_up_interruptible(&job->event);

		/* Job callbacks handled via work queue */
		queue_work(core->work_queue, &job->work);
	}

	/* Requeue delayed work */
	if (core->n_active_jobs)
		queue_delayed_work(
			core->work_queue,
			&core->timeout_work, JOB_TIMEOUT);

	spin_unlock_irqrestore(&core->lock, flags);
}
#endif

/**
 * reset_hw_timer() - Resets a job's hardware timer. Must be called before
 *                    the timer is used.
 *
 * @job: Pointer to job struct
 *
 * core->lock _must_ be held when calling this function
 */
static void reset_hw_timer(struct b2r2_core_job *job)
{
	job->nsec_active_in_hw = 0;
}

/**
 * start_hw_timer() - Times how long a job spends in hardware (active).
 *                    Should be called immediatly before starting the
 *                    hardware.
 *
 * @job: Pointer to job struct
 *
 * core->lock _must_ be held when calling this function
 */
static void start_hw_timer(struct b2r2_core_job *job)
{
	ktime_get_ts(&job->hw_ts_start);
}

/**
 * stop_hw_timer() - Times how long a job spends in hardware (active).
 *                   Should be called immediatly after the hardware has
 *                   finished.
 *
 * @core: The b2r2 core entity
 * @job: Pointer to job struct
 *
 * core->lock _must_ be held when calling this function
 */
static void stop_hw_timer(struct b2r2_core *core, struct b2r2_core_job *job)
{
	/* Assumes only app queues are used, which is the case right now. */
	/*
	 * Not 100% accurate. When a higher prio job interrupts a lower prio
	 * job it does so after the current node of the low prio job has
	 * finished. Currently we can not sense when the actual switch takes
	 * place so the time reported for a job that interrupts a lower prio
	 * job will on average contain the time it takes to process half a node
	 * in the lower prio job in addition to the time it takes to process
	 * the job's own nodes. This could possibly be solved by adding node
	 * notifications but that would involve a significant amount of work
	 * and consume system resources due to the extra interrupts.
	 */

	int i;
	struct timespec ts_stop;
	struct timespec ts_diff;
	s64 nsec_in_hw;

	ktime_get_ts(&ts_stop);
	ts_diff = timespec_sub(ts_stop, job->hw_ts_start);
	nsec_in_hw = timespec_to_ns(&ts_diff);
	job->nsec_active_in_hw += nsec_in_hw;

	/*
	 * Check if we have delayed the start of higher prio jobs. Can happen
	 * as queue switching only can be done between nodes.
	 */
	for (i = (int)job->queue - 1; i >= (int)B2R2_CORE_QUEUE_AQ1; i--) {
		struct b2r2_core_job *qj = core->active_jobs[i];

		if (NULL == qj)
			continue;

		qj->hw_ts_start = ts_stop;
	}

	/* Check if the job has stolen time from lower prio jobs */
	for (i = (int)job->queue + 1; i < B2R2_NUM_APPLICATIONS_QUEUES; i++) {
		struct b2r2_core_job *qj = core->active_jobs[i];

		if (NULL == qj)
			continue;

		if (timespec_to_ns(&qj->hw_ts_start) > 0 &&
				timespec_compare(&qj->hw_ts_start,
					&ts_stop) < 0) {
			struct timespec qj_ts_diff =
				timespec_sub(ts_stop, qj->hw_ts_start);
			s64 qj_nsec_in_hw = timespec_to_ns(&qj_ts_diff);

			s64 num_stolen_nsec = min(qj_nsec_in_hw,
				nsec_in_hw);

			qj->nsec_active_in_hw -= num_stolen_nsec;

			nsec_in_hw -= num_stolen_nsec;
			set_normalized_timespec(&ts_stop, ts_stop.tv_sec,
				ts_stop.tv_nsec - num_stolen_nsec);
		}

		if (nsec_in_hw <= 0)
			break;
	}
}

/**
 * init_job() - Initializes a job structure from filled in client data.
 *              Reference count will be set to 1
 *
 * @job: Job to initialize
 */
static void init_job(struct b2r2_core_job *job)
{

	job->start_sentinel = START_SENTINEL;
	job->end_sentinel = END_SENTINEL;

	/* Job is idle, never queued */
	job->job_state = B2R2_CORE_JOB_IDLE;

	/* Initialize internal data */
	INIT_LIST_HEAD(&job->list);
	init_waitqueue_head(&job->event);
	INIT_WORK(&job->work, job_work_function);

	/* Map given prio to B2R2 queues */
	if (job->prio < B2R2_CORE_LOWEST_PRIO)
		job->prio = B2R2_CORE_LOWEST_PRIO;
	else if (job->prio > B2R2_CORE_HIGHEST_PRIO)
		job->prio = B2R2_CORE_HIGHEST_PRIO;

	if (job->prio > 10) {
		job->queue = B2R2_CORE_QUEUE_AQ1;
		job->interrupt_context =
			(B2R2BLT_ITSAQ1_LNA_Reached);
		job->control = (B2R2_AQ_Enab | B2R2_AQ_PRIOR_3);
	} else if (job->prio > 0) {
		job->queue = B2R2_CORE_QUEUE_AQ2;
		job->interrupt_context =
			(B2R2BLT_ITSAQ2_LNA_Reached);
		job->control = (B2R2_AQ_Enab | B2R2_AQ_PRIOR_2);
	} else if (job->prio > -10) {
		job->queue = B2R2_CORE_QUEUE_AQ3;
		job->interrupt_context =
			(B2R2BLT_ITSAQ3_LNA_Reached);
		job->control = (B2R2_AQ_Enab | B2R2_AQ_PRIOR_1);
	} else {
		job->queue = B2R2_CORE_QUEUE_AQ4;
		job->interrupt_context =
			(B2R2BLT_ITSAQ4_LNA_Reached);
		job->control = (B2R2_AQ_Enab | B2R2_AQ_PRIOR_0);
	}
}

/**
 * clear_interrupts() - Disables all interrupts
 *
 * core->lock _must_ be held
 */
static void  clear_interrupts(struct b2r2_core *core)
{
	writel(0x0, &core->hw->BLT_ITM0);
	writel(0x0, &core->hw->BLT_ITM1);
	writel(0x0, &core->hw->BLT_ITM2);
	writel(0x0, &core->hw->BLT_ITM3);
}

/**
 * insert_into_prio_list() - Inserts the job into the sorted list of jobs.
 *                           The list is sorted by priority.
 *
 * @core: The b2r2 core entity
 * @job: Job to insert
 *
 * core->lock _must_ be held
 */
static void insert_into_prio_list(struct b2r2_core *core,
		struct b2r2_core_job *job)
{
	/*
	 * Ref count is increased when job put in list,
	 * should be released when job is removed from list
	 */
	internal_job_addref(core, job, __func__);

	core->stat_n_jobs_in_prio_list++;

	/* Sort in the job */
	if (list_empty(&core->prio_queue))
		list_add_tail(&job->list, &core->prio_queue);
	else {
		struct b2r2_core_job *first_job = list_entry(
			core->prio_queue.next,
				   struct b2r2_core_job, list);
		struct b2r2_core_job *last_job = list_entry(
			core->prio_queue.prev,
				   struct b2r2_core_job, list);

		if (job->prio > first_job->prio)
			list_add(&job->list, &core->prio_queue);
		else if (job->prio <= last_job->prio)
			list_add_tail(&job->list, &core->prio_queue);
		else {
			/* We need to find where to put it */
			struct list_head *ptr;

			list_for_each(ptr, &core->prio_queue) {
				struct b2r2_core_job *list_job =
					list_entry(ptr, struct b2r2_core_job,
						list);
				if (job->prio > list_job->prio) {
					list_add_tail(&job->list,
						&list_job->list);
					break;
				}
			}
		}
	}
	/* The job is now queued */
	job->job_state = B2R2_CORE_JOB_QUEUED;
}

/**
 * check_prio_list() - Checks if the first job(s) in the prio list can
 *                     be dispatched to B2R2
 *
 * @core: The b2r2 core entity
 * @atomic: true if in atomic context (i.e. interrupt context)
 *
 * core->lock _must_ be held
 */
static void check_prio_list(struct b2r2_core *core, bool atomic)
{
	bool dispatched_job;
	int n_dispatched = 0;
	struct b2r2_core_job *job;

	do {
		dispatched_job = false;

		/* Do we have anything in our prio list? */
		if (list_empty(&core->prio_queue))
			break;

		/* The first job waiting */
		job = list_first_entry(&core->prio_queue,
				 struct b2r2_core_job, list);

		/* Is the B2R2 queue available? */
		if (core->active_jobs[job->queue] != NULL)
			break;

		/* Can we acquire resources? */
		if (!job->acquire_resources ||
			job->acquire_resources(job, atomic) == 0) {
			/* Ok to dispatch job */

			/* Remove from list */
			list_del_init(&job->list);

			/* The job is now active */
			core->active_jobs[job->queue] = job;
			core->n_active_jobs++;
			job->jiffies = jiffies;
			core->jiffies_last_active = jiffies;

			/* Kick off B2R2 */
			trigger_job(core, job);
			dispatched_job = true;
			n_dispatched++;

#ifdef HANDLE_TIMEOUTED_JOBS
			/* Check in one half second if it hangs */
			queue_delayed_work(core->work_queue,
				&core->timeout_work, JOB_TIMEOUT);
#endif
		} else {
			/* No resources */
			if (!atomic && core->n_active_jobs == 0) {
				b2r2_log_warn(core->dev,
					"%s: No resource", __func__);
				cancel_job(core, job);
			}
		}
	} while (dispatched_job);

	core->stat_n_jobs_in_prio_list -= n_dispatched;
}

/**
 * find_job_in_list() - Finds job with job_id in list
 *
 * @jobid: Job id to find
 * @list: List to find job id in
 *
 * Reference count will be incremented for found job.
 *
 * core->lock _must_ be held
 */
static struct b2r2_core_job *find_job_in_list(int job_id,
					      struct list_head *list)
{
	struct list_head *ptr;

	list_for_each(ptr, list) {
		struct b2r2_core_job *job = list_entry(
				ptr, struct b2r2_core_job, list);
		if (job->job_id == job_id) {
			struct b2r2_core *core = (struct b2r2_core *) job->data;
			/*
			 * Increase reference count, should be released by
			 * the caller of b2r2_core_job_find
			 */
			internal_job_addref(core, job, __func__);
			return job;
		}
	}
	return NULL;
}

/**
 * find_job_in_active_jobs() - Finds job in active job queues
 *
 * @core: The b2r2 core entity
 * @job_id: Job id to find
 *
 * Reference count will be incremented for found job.
 *
 * core->lock _must_ be held
 */
static struct b2r2_core_job *find_job_in_active_jobs(struct b2r2_core *core,
		int job_id)
{
	int i;
	struct b2r2_core_job *found_job = NULL;

	if (core->n_active_jobs) {
		for (i = 0; i < ARRAY_SIZE(core->active_jobs); i++) {
			struct b2r2_core_job *job = core->active_jobs[i];

			if (job && job->job_id == job_id) {
				internal_job_addref(core, job, __func__);
				found_job = job;
				break;
			}
		}
	}
	return found_job;
}

/**
 * find_tag_in_list() - Finds first job with tag in list
 *
 * @tag: Tag to find
 * @list: List to find job id in
 *
 * Reference count will be incremented for found job.
 *
 * core->lock must be held
 */
static struct b2r2_core_job *find_tag_in_list(struct b2r2_core *core,
		int tag, struct list_head *list)
{
	struct list_head *ptr;

	list_for_each(ptr, list) {
		struct b2r2_core_job *job =
				list_entry(ptr, struct b2r2_core_job, list);
		if (job->tag == tag) {
			/*
			 * Increase reference count, should be released by
			 * the caller of b2r2_core_job_find
			 */
			internal_job_addref(core, job, __func__);
			return job;
		}
	}
	return NULL;
}

/**
 * find_tag_in_active_jobs() - Finds job with tag in active job queues
 *
 * @tag: Tag to find
 *
 * Reference count will be incremented for found job.
 *
 * core->lock must be held
 */
static struct b2r2_core_job *find_tag_in_active_jobs(struct b2r2_core *core,
		int tag)
{
	int i;
	struct b2r2_core_job *found_job = NULL;

	if (core->n_active_jobs) {
		for (i = 0; i < ARRAY_SIZE(core->active_jobs); i++) {
			struct b2r2_core_job *job = core->active_jobs[i];

			if (job && job->tag == tag) {
				internal_job_addref(core, job, __func__);
				found_job = job;
				break;
			}
		}
	}
	return found_job;
}


#ifdef HANDLE_TIMEOUTED_JOBS
/**
 * hw_reset() - Resets B2R2 hardware
 *
 * core->lock must be held
 */
static int hw_reset(struct b2r2_core *core)
{
	u32 uTimeOut = B2R2_RESET_TIMEOUT_VALUE;

	/* Tell B2R2 to reset */
	writel(readl(&core->hw->BLT_CTL) | B2R2BLT_CTLGLOBAL_soft_reset,
		&core->hw->BLT_CTL);
	writel(0x00000000, &core->hw->BLT_CTL);

	b2r2_log_info(core->dev, "wait for B2R2 to be idle..\n");

	/* Wait for B2R2 to be idle (on a timeout rather than while loop) */
	while ((uTimeOut > 0) &&
	       ((readl(&core->hw->BLT_STA1) &
		 B2R2BLT_STA1BDISP_IDLE) == 0x0))
		uTimeOut--;

	if (uTimeOut == 0) {
		b2r2_log_warn(core->dev,
			 "error-> after software reset B2R2 is not idle\n");
		return -EAGAIN;
	}

	return 0;

}
#endif

/**
 * trigger_job() - Put job in B2R2 HW queue
 *
 * @job: Job to trigger
 *
 * core->lock must be held
 */
static void trigger_job(struct b2r2_core *core, struct b2r2_core_job *job)
{
	/* Debug prints */
	b2r2_log_info(core->dev, "queue 0x%x\n", job->queue);
	b2r2_log_info(core->dev, "BLT TRIG_IP 0x%x (first node)\n",
		job->first_node_address);
	b2r2_log_info(core->dev, "BLT LNA_CTL 0x%x (last node)\n",
		job->last_node_address);
	b2r2_log_info(core->dev, "BLT TRIG_CTL 0x%x\n", job->control);
	b2r2_log_info(core->dev, "BLT PACE_CTL 0x%x\n", job->pace_control);

	reset_hw_timer(job);
	job->job_state = B2R2_CORE_JOB_RUNNING;

	/* Enable interrupt */
	writel(readl(&core->hw->BLT_ITM0) | job->interrupt_context,
			&core->hw->BLT_ITM0);

	writel(min_t(u8, max_t(u8, core->op_size, B2R2_PLUG_OPCODE_SIZE_8),
			B2R2_PLUG_OPCODE_SIZE_64), &core->hw->PLUGS1_OP2);
	writel(min_t(u8, core->ch_size, B2R2_PLUG_CHUNK_SIZE_128),
			&core->hw->PLUGS1_CHZ);
	writel(min_t(u8, core->mg_size, B2R2_PLUG_MESSAGE_SIZE_128) |
			(core->min_req_time << 16), &core->hw->PLUGS1_MSZ);
	writel(min_t(u8, core->pg_size, B2R2_PLUG_PAGE_SIZE_256),
			&core->hw->PLUGS1_PGZ);

	writel(min_t(u8, max_t(u8, core->op_size, B2R2_PLUG_OPCODE_SIZE_8),
			B2R2_PLUG_OPCODE_SIZE_64), &core->hw->PLUGS2_OP2);
	writel(min_t(u8, core->ch_size, B2R2_PLUG_CHUNK_SIZE_128),
			&core->hw->PLUGS2_CHZ);
	writel(min_t(u8, core->mg_size, B2R2_PLUG_MESSAGE_SIZE_128) |
			(core->min_req_time << 16), &core->hw->PLUGS2_MSZ);
	writel(min_t(u8, core->pg_size, B2R2_PLUG_PAGE_SIZE_256),
			&core->hw->PLUGS2_PGZ);

	writel(min_t(u8, max_t(u8, core->op_size, B2R2_PLUG_OPCODE_SIZE_8),
			B2R2_PLUG_OPCODE_SIZE_64), &core->hw->PLUGS3_OP2);
	writel(min_t(u8, core->ch_size, B2R2_PLUG_CHUNK_SIZE_128),
			&core->hw->PLUGS3_CHZ);
	writel(min_t(u8, core->mg_size, B2R2_PLUG_MESSAGE_SIZE_128) |
			(core->min_req_time << 16), &core->hw->PLUGS3_MSZ);
	writel(min_t(u8, core->pg_size, B2R2_PLUG_PAGE_SIZE_256),
			&core->hw->PLUGS3_PGZ);

	writel(min_t(u8, max_t(u8, core->op_size, B2R2_PLUG_OPCODE_SIZE_8),
			B2R2_PLUG_OPCODE_SIZE_64), &core->hw->PLUGT_OP2);
	writel(min_t(u8, core->ch_size, B2R2_PLUG_CHUNK_SIZE_128),
			&core->hw->PLUGT_CHZ);
	writel(min_t(u8, core->mg_size, B2R2_PLUG_MESSAGE_SIZE_128) |
			(core->min_req_time << 16), &core->hw->PLUGT_MSZ);
	writel(min_t(u8, core->pg_size, B2R2_PLUG_PAGE_SIZE_256),
			&core->hw->PLUGT_PGZ);

	/* B2R2 kicks off when LNA is written, LNA write must be last! */
	switch (job->queue) {
	case B2R2_CORE_QUEUE_CQ1:
		writel(job->first_node_address, &core->hw->BLT_CQ1_TRIG_IP);
		writel(job->control, &core->hw->BLT_CQ1_TRIG_CTL);
		writel(job->pace_control, &core->hw->BLT_CQ1_PACE_CTL);
		break;

	case B2R2_CORE_QUEUE_CQ2:
		writel(job->first_node_address, &core->hw->BLT_CQ2_TRIG_IP);
		writel(job->control, &core->hw->BLT_CQ2_TRIG_CTL);
		writel(job->pace_control, &core->hw->BLT_CQ2_PACE_CTL);
		break;

	case B2R2_CORE_QUEUE_AQ1:
		writel(job->control, &core->hw->BLT_AQ1_CTL);
		writel(job->first_node_address, &core->hw->BLT_AQ1_IP);
		wmb();
		start_hw_timer(job);
		writel(job->last_node_address, &core->hw->BLT_AQ1_LNA);
		break;

	case B2R2_CORE_QUEUE_AQ2:
		writel(job->control, &core->hw->BLT_AQ2_CTL);
		writel(job->first_node_address, &core->hw->BLT_AQ2_IP);
		wmb();
		start_hw_timer(job);
		writel(job->last_node_address, &core->hw->BLT_AQ2_LNA);
		break;

	case B2R2_CORE_QUEUE_AQ3:
		writel(job->control, &core->hw->BLT_AQ3_CTL);
		writel(job->first_node_address, &core->hw->BLT_AQ3_IP);
		wmb();
		start_hw_timer(job);
		writel(job->last_node_address, &core->hw->BLT_AQ3_LNA);
		break;

	case B2R2_CORE_QUEUE_AQ4:
		writel(job->control, &core->hw->BLT_AQ4_CTL);
		writel(job->first_node_address, &core->hw->BLT_AQ4_IP);
		wmb();
		start_hw_timer(job);
		writel(job->last_node_address, &core->hw->BLT_AQ4_LNA);
		break;

		/* Handle the default case */
	default:
		break;

	} /* end switch */

}

/**
 * handle_queue_event() - Handles interrupt event for specified B2R2 queue
 *
 * @queue: Queue to handle event for
 *
 * core->lock must be held
 */
static void handle_queue_event(struct b2r2_core *core,
		enum b2r2_core_queue queue)
{
	struct b2r2_core_job *job;

	job = core->active_jobs[queue];
	if (job) {
		if (job->job_state != B2R2_CORE_JOB_RUNNING)
			/*
			 * Should be running
			 * Severe error. TBD
			 */
			b2r2_log_warn(core->dev,
				 "%s: Job is not running", __func__);

		stop_hw_timer(core, job);

		/* Remove from queue */
		BUG_ON(core->n_active_jobs == 0);
		core->active_jobs[queue] = NULL;
		core->n_active_jobs--;
	}

	if (!job) {
		/* No job, error?  */
		b2r2_log_warn(core->dev, "%s: No job", __func__);
		return;
	}


	/*
	 * Atomic context release resources, release resources will
	 * be called again later from process context (work queue)
	 */
	if (job->release_resources)
		job->release_resources(job, true);

	/* Job is done */
	job->job_state = B2R2_CORE_JOB_DONE;

	/* Handle done */
	wake_up_interruptible(&job->event);

	/* Dispatch to work queue to handle callbacks */
	queue_work(core->work_queue, &job->work);
}

/**
 * process_events() - Handles interrupt events
 *
 * @status: Contents of the B2R2 ITS register
 */
static void process_events(struct b2r2_core *core, u32 status)
{
	u32 mask = 0xF;
	u32 disable_itm_mask = 0;

	b2r2_log_info(core->dev, "Enters process_events\n");
	b2r2_log_info(core->dev, "status 0x%x\n", status);

	/* Composition queue 1 */
	if (status & mask) {
		handle_queue_event(core, B2R2_CORE_QUEUE_CQ1);
		disable_itm_mask |= mask;
	}
	mask <<= 4;

	/* Composition queue 2 */
	if (status & mask) {
		handle_queue_event(core, B2R2_CORE_QUEUE_CQ2);
		disable_itm_mask |= mask;
	}
	mask <<= 8;

	/* Application queue 1 */
	if (status & mask) {
		handle_queue_event(core, B2R2_CORE_QUEUE_AQ1);
		disable_itm_mask |= mask;
	}
	mask <<= 4;

	/* Application queue 2 */
	if (status & mask) {
		handle_queue_event(core, B2R2_CORE_QUEUE_AQ2);
		disable_itm_mask |= mask;
	}
	mask <<= 4;

	/* Application queue 3 */
	if (status & mask) {
		handle_queue_event(core, B2R2_CORE_QUEUE_AQ3);
		disable_itm_mask |= mask;
	}
	mask <<= 4;

	/* Application queue 4 */
	if (status & mask) {
		handle_queue_event(core, B2R2_CORE_QUEUE_AQ4);
		disable_itm_mask |= mask;
	}

	/* Clear received interrupt flags */
	writel(status, &core->hw->BLT_ITS);
	/* Disable handled interrupts */
	writel(readl(&core->hw->BLT_ITM0) & ~disable_itm_mask,
		 &core->hw->BLT_ITM0);

	b2r2_log_info(core->dev, "Returns process_events\n");
}

/**
 * b2r2_irq_handler() - B2R2 interrupt handler
 *
 * @irq: Interrupt number (not used)
 * @dev_id: A pointer to the b2r2 core entity
 */
static irqreturn_t b2r2_irq_handler(int irq, void *dev_id)
{
	unsigned long flags;
	struct b2r2_core *core;
	int i;
	static unsigned int irq_count;

	/* Interleave access to eliminate starvation of cores >= 1 */
	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		core = b2r2_core[irq_count++ % B2R2_MAX_NBR_DEVICES];
		if (core != NULL)
			break;
	}

	if (core == NULL)
		/* ERROR */
		return IRQ_HANDLED;

	/* Spin lock is need in irq handler (SMP) */
	spin_lock_irqsave(&core->lock, flags);

	/* Make a quick exit if this device was not interrupting */
	if (!core->valid ||
		((readl(&core->hw->BLT_ITS) & B2R2_ITS_MASK) == 0)) {
		core->stat_n_irq_skipped++;
		spin_unlock_irqrestore(&core->lock, flags);
		return IRQ_NONE;
	}

	/* Remember time for last irq (for timeout mgmt) */
	core->jiffies_last_irq = jiffies;
	core->stat_n_irq++;

	/* Handle the interrupt(s) */
	process_events(core, readl(&core->hw->BLT_ITS));

	/* Check if we can dispatch new jobs */
	check_prio_list(core, true);

	core->stat_n_irq_exit++;

	spin_unlock_irqrestore(&core->lock, flags);

	return IRQ_HANDLED;
}


#ifdef CONFIG_DEBUG_FS
/**
 * struct debugfs_reg - Represents one B2R2 register in debugfs
 *
 * @name: Register name
 * @offset: Byte offset in B2R2 for register
 */
struct debugfs_reg {
	const char name[30];
	u32        offset;
};

/**
 * debugfs_regs - Array of B2R2 debugfs registers
 */
static const struct debugfs_reg debugfs_regs[] = {
	{"BLT_SSBA17", offsetof(struct b2r2_memory_map, BLT_SSBA17)},
	{"BLT_SSBA18", offsetof(struct b2r2_memory_map, BLT_SSBA18)},
	{"BLT_SSBA19", offsetof(struct b2r2_memory_map, BLT_SSBA19)},
	{"BLT_SSBA20", offsetof(struct b2r2_memory_map, BLT_SSBA20)},
	{"BLT_SSBA21", offsetof(struct b2r2_memory_map, BLT_SSBA21)},
	{"BLT_SSBA22", offsetof(struct b2r2_memory_map, BLT_SSBA22)},
	{"BLT_SSBA23", offsetof(struct b2r2_memory_map, BLT_SSBA23)},
	{"BLT_SSBA24", offsetof(struct b2r2_memory_map, BLT_SSBA24)},
	{"BLT_STBA5", offsetof(struct b2r2_memory_map, BLT_STBA5)},
	{"BLT_STBA6", offsetof(struct b2r2_memory_map, BLT_STBA6)},
	{"BLT_STBA7", offsetof(struct b2r2_memory_map, BLT_STBA7)},
	{"BLT_STBA8", offsetof(struct b2r2_memory_map, BLT_STBA8)},
	{"BLT_CTL", offsetof(struct b2r2_memory_map, BLT_CTL)},
	{"BLT_ITS", offsetof(struct b2r2_memory_map, BLT_ITS)},
	{"BLT_STA1", offsetof(struct b2r2_memory_map, BLT_STA1)},
	{"BLT_SSBA1", offsetof(struct b2r2_memory_map, BLT_SSBA1)},
	{"BLT_SSBA2", offsetof(struct b2r2_memory_map, BLT_SSBA2)},
	{"BLT_SSBA3", offsetof(struct b2r2_memory_map, BLT_SSBA3)},
	{"BLT_SSBA4", offsetof(struct b2r2_memory_map, BLT_SSBA4)},
	{"BLT_SSBA5", offsetof(struct b2r2_memory_map, BLT_SSBA5)},
	{"BLT_SSBA6", offsetof(struct b2r2_memory_map, BLT_SSBA6)},
	{"BLT_SSBA7", offsetof(struct b2r2_memory_map, BLT_SSBA7)},
	{"BLT_SSBA8", offsetof(struct b2r2_memory_map, BLT_SSBA8)},
	{"BLT_STBA1", offsetof(struct b2r2_memory_map, BLT_STBA1)},
	{"BLT_STBA2", offsetof(struct b2r2_memory_map, BLT_STBA2)},
	{"BLT_STBA3", offsetof(struct b2r2_memory_map, BLT_STBA3)},
	{"BLT_STBA4", offsetof(struct b2r2_memory_map, BLT_STBA4)},
	{"BLT_CQ1_TRIG_IP", offsetof(struct b2r2_memory_map, BLT_CQ1_TRIG_IP)},
	{"BLT_CQ1_TRIG_CTL", offsetof(struct b2r2_memory_map,
				      BLT_CQ1_TRIG_CTL)},
	{"BLT_CQ1_PACE_CTL", offsetof(struct b2r2_memory_map,
				      BLT_CQ1_PACE_CTL)},
	{"BLT_CQ1_IP", offsetof(struct b2r2_memory_map, BLT_CQ1_IP)},
	{"BLT_CQ2_TRIG_IP", offsetof(struct b2r2_memory_map, BLT_CQ2_TRIG_IP)},
	{"BLT_CQ2_TRIG_CTL", offsetof(struct b2r2_memory_map,
				      BLT_CQ2_TRIG_CTL)},
	{"BLT_CQ2_PACE_CTL", offsetof(struct b2r2_memory_map,
				      BLT_CQ2_PACE_CTL)},
	{"BLT_CQ2_IP", offsetof(struct b2r2_memory_map, BLT_CQ2_IP)},
	{"BLT_AQ1_CTL", offsetof(struct b2r2_memory_map, BLT_AQ1_CTL)},
	{"BLT_AQ1_IP", offsetof(struct b2r2_memory_map, BLT_AQ1_IP)},
	{"BLT_AQ1_LNA", offsetof(struct b2r2_memory_map, BLT_AQ1_LNA)},
	{"BLT_AQ1_STA", offsetof(struct b2r2_memory_map, BLT_AQ1_STA)},
	{"BLT_AQ2_CTL", offsetof(struct b2r2_memory_map, BLT_AQ2_CTL)},
	{"BLT_AQ2_IP", offsetof(struct b2r2_memory_map, BLT_AQ2_IP)},
	{"BLT_AQ2_LNA", offsetof(struct b2r2_memory_map, BLT_AQ2_LNA)},
	{"BLT_AQ2_STA", offsetof(struct b2r2_memory_map, BLT_AQ2_STA)},
	{"BLT_AQ3_CTL", offsetof(struct b2r2_memory_map, BLT_AQ3_CTL)},
	{"BLT_AQ3_IP", offsetof(struct b2r2_memory_map, BLT_AQ3_IP)},
	{"BLT_AQ3_LNA", offsetof(struct b2r2_memory_map, BLT_AQ3_LNA)},
	{"BLT_AQ3_STA", offsetof(struct b2r2_memory_map, BLT_AQ3_STA)},
	{"BLT_AQ4_CTL", offsetof(struct b2r2_memory_map, BLT_AQ4_CTL)},
	{"BLT_AQ4_IP", offsetof(struct b2r2_memory_map, BLT_AQ4_IP)},
	{"BLT_AQ4_LNA", offsetof(struct b2r2_memory_map, BLT_AQ4_LNA)},
	{"BLT_AQ4_STA", offsetof(struct b2r2_memory_map, BLT_AQ4_STA)},
	{"BLT_SSBA9", offsetof(struct b2r2_memory_map, BLT_SSBA9)},
	{"BLT_SSBA10", offsetof(struct b2r2_memory_map, BLT_SSBA10)},
	{"BLT_SSBA11", offsetof(struct b2r2_memory_map, BLT_SSBA11)},
	{"BLT_SSBA12", offsetof(struct b2r2_memory_map, BLT_SSBA12)},
	{"BLT_SSBA13", offsetof(struct b2r2_memory_map, BLT_SSBA13)},
	{"BLT_SSBA14", offsetof(struct b2r2_memory_map, BLT_SSBA14)},
	{"BLT_SSBA15", offsetof(struct b2r2_memory_map, BLT_SSBA15)},
	{"BLT_SSBA16", offsetof(struct b2r2_memory_map, BLT_SSBA16)},
	{"BLT_SGA1", offsetof(struct b2r2_memory_map, BLT_SGA1)},
	{"BLT_SGA2", offsetof(struct b2r2_memory_map, BLT_SGA2)},
	{"BLT_ITM0", offsetof(struct b2r2_memory_map, BLT_ITM0)},
	{"BLT_ITM1", offsetof(struct b2r2_memory_map, BLT_ITM1)},
	{"BLT_ITM2", offsetof(struct b2r2_memory_map, BLT_ITM2)},
	{"BLT_ITM3", offsetof(struct b2r2_memory_map, BLT_ITM3)},
	{"BLT_DFV2", offsetof(struct b2r2_memory_map, BLT_DFV2)},
	{"BLT_DFV1", offsetof(struct b2r2_memory_map, BLT_DFV1)},
	{"BLT_PRI", offsetof(struct b2r2_memory_map, BLT_PRI)},
	{"PLUGS1_OP2", offsetof(struct b2r2_memory_map, PLUGS1_OP2)},
	{"PLUGS1_CHZ", offsetof(struct b2r2_memory_map, PLUGS1_CHZ)},
	{"PLUGS1_MSZ", offsetof(struct b2r2_memory_map, PLUGS1_MSZ)},
	{"PLUGS1_PGZ", offsetof(struct b2r2_memory_map, PLUGS1_PGZ)},
	{"PLUGS2_OP2", offsetof(struct b2r2_memory_map, PLUGS2_OP2)},
	{"PLUGS2_CHZ", offsetof(struct b2r2_memory_map, PLUGS2_CHZ)},
	{"PLUGS2_MSZ", offsetof(struct b2r2_memory_map, PLUGS2_MSZ)},
	{"PLUGS2_PGZ", offsetof(struct b2r2_memory_map, PLUGS2_PGZ)},
	{"PLUGS3_OP2", offsetof(struct b2r2_memory_map, PLUGS3_OP2)},
	{"PLUGS3_CHZ", offsetof(struct b2r2_memory_map, PLUGS3_CHZ)},
	{"PLUGS3_MSZ", offsetof(struct b2r2_memory_map, PLUGS3_MSZ)},
	{"PLUGS3_PGZ", offsetof(struct b2r2_memory_map, PLUGS3_PGZ)},
	{"PLUGT_OP2", offsetof(struct b2r2_memory_map, PLUGT_OP2)},
	{"PLUGT_CHZ", offsetof(struct b2r2_memory_map, PLUGT_CHZ)},
	{"PLUGT_MSZ", offsetof(struct b2r2_memory_map, PLUGT_MSZ)},
	{"PLUGT_PGZ", offsetof(struct b2r2_memory_map, PLUGT_PGZ)},
	{"BLT_NIP", offsetof(struct b2r2_memory_map, BLT_NIP)},
	{"BLT_CIC", offsetof(struct b2r2_memory_map, BLT_CIC)},
	{"BLT_INS", offsetof(struct b2r2_memory_map, BLT_INS)},
	{"BLT_ACK", offsetof(struct b2r2_memory_map, BLT_ACK)},
	{"BLT_TBA", offsetof(struct b2r2_memory_map, BLT_TBA)},
	{"BLT_TTY", offsetof(struct b2r2_memory_map, BLT_TTY)},
	{"BLT_TXY", offsetof(struct b2r2_memory_map, BLT_TXY)},
	{"BLT_TSZ", offsetof(struct b2r2_memory_map, BLT_TSZ)},
	{"BLT_S1CF", offsetof(struct b2r2_memory_map, BLT_S1CF)},
	{"BLT_S2CF", offsetof(struct b2r2_memory_map, BLT_S2CF)},
	{"BLT_S1BA", offsetof(struct b2r2_memory_map, BLT_S1BA)},
	{"BLT_S1TY", offsetof(struct b2r2_memory_map, BLT_S1TY)},
	{"BLT_S1XY", offsetof(struct b2r2_memory_map, BLT_S1XY)},
	{"BLT_S2BA", offsetof(struct b2r2_memory_map, BLT_S2BA)},
	{"BLT_S2TY", offsetof(struct b2r2_memory_map, BLT_S2TY)},
	{"BLT_S2XY", offsetof(struct b2r2_memory_map, BLT_S2XY)},
	{"BLT_S2SZ", offsetof(struct b2r2_memory_map, BLT_S2SZ)},
	{"BLT_S3BA", offsetof(struct b2r2_memory_map, BLT_S3BA)},
	{"BLT_S3TY", offsetof(struct b2r2_memory_map, BLT_S3TY)},
	{"BLT_S3XY", offsetof(struct b2r2_memory_map, BLT_S3XY)},
	{"BLT_S3SZ", offsetof(struct b2r2_memory_map, BLT_S3SZ)},
	{"BLT_CWO", offsetof(struct b2r2_memory_map, BLT_CWO)},
	{"BLT_CWS", offsetof(struct b2r2_memory_map, BLT_CWS)},
	{"BLT_CCO", offsetof(struct b2r2_memory_map, BLT_CCO)},
	{"BLT_CML", offsetof(struct b2r2_memory_map, BLT_CML)},
	{"BLT_FCTL", offsetof(struct b2r2_memory_map, BLT_FCTL)},
	{"BLT_PMK", offsetof(struct b2r2_memory_map, BLT_PMK)},
	{"BLT_RSF", offsetof(struct b2r2_memory_map, BLT_RSF)},
	{"BLT_RZI", offsetof(struct b2r2_memory_map, BLT_RZI)},
	{"BLT_HFP", offsetof(struct b2r2_memory_map, BLT_HFP)},
	{"BLT_VFP", offsetof(struct b2r2_memory_map, BLT_VFP)},
	{"BLT_Y_RSF", offsetof(struct b2r2_memory_map, BLT_Y_RSF)},
	{"BLT_Y_RZI", offsetof(struct b2r2_memory_map, BLT_Y_RZI)},
	{"BLT_Y_HFP", offsetof(struct b2r2_memory_map, BLT_Y_HFP)},
	{"BLT_Y_VFP", offsetof(struct b2r2_memory_map, BLT_Y_VFP)},
	{"BLT_KEY1", offsetof(struct b2r2_memory_map, BLT_KEY1)},
	{"BLT_KEY2", offsetof(struct b2r2_memory_map, BLT_KEY2)},
	{"BLT_SAR", offsetof(struct b2r2_memory_map, BLT_SAR)},
	{"BLT_USR", offsetof(struct b2r2_memory_map, BLT_USR)},
	{"BLT_IVMX0", offsetof(struct b2r2_memory_map, BLT_IVMX0)},
	{"BLT_IVMX1", offsetof(struct b2r2_memory_map, BLT_IVMX1)},
	{"BLT_IVMX2", offsetof(struct b2r2_memory_map, BLT_IVMX2)},
	{"BLT_IVMX3", offsetof(struct b2r2_memory_map, BLT_IVMX3)},
	{"BLT_OVMX0", offsetof(struct b2r2_memory_map, BLT_OVMX0)},
	{"BLT_OVMX1", offsetof(struct b2r2_memory_map, BLT_OVMX1)},
	{"BLT_OVMX2", offsetof(struct b2r2_memory_map, BLT_OVMX2)},
	{"BLT_OVMX3", offsetof(struct b2r2_memory_map, BLT_OVMX3)},
	{"BLT_VC1R", offsetof(struct b2r2_memory_map, BLT_VC1R)},
	{"BLT_Y_HFC0", offsetof(struct b2r2_memory_map, BLT_Y_HFC0)},
	{"BLT_Y_HFC1", offsetof(struct b2r2_memory_map, BLT_Y_HFC1)},
	{"BLT_Y_HFC2", offsetof(struct b2r2_memory_map, BLT_Y_HFC2)},
	{"BLT_Y_HFC3", offsetof(struct b2r2_memory_map, BLT_Y_HFC3)},
	{"BLT_Y_HFC4", offsetof(struct b2r2_memory_map, BLT_Y_HFC4)},
	{"BLT_Y_HFC5", offsetof(struct b2r2_memory_map, BLT_Y_HFC5)},
	{"BLT_Y_HFC6", offsetof(struct b2r2_memory_map, BLT_Y_HFC6)},
	{"BLT_Y_HFC7", offsetof(struct b2r2_memory_map, BLT_Y_HFC7)},
	{"BLT_Y_HFC8", offsetof(struct b2r2_memory_map, BLT_Y_HFC8)},
	{"BLT_Y_HFC9", offsetof(struct b2r2_memory_map, BLT_Y_HFC9)},
	{"BLT_Y_HFC10", offsetof(struct b2r2_memory_map, BLT_Y_HFC10)},
	{"BLT_Y_HFC11", offsetof(struct b2r2_memory_map, BLT_Y_HFC11)},
	{"BLT_Y_HFC12", offsetof(struct b2r2_memory_map, BLT_Y_HFC12)},
	{"BLT_Y_HFC13", offsetof(struct b2r2_memory_map, BLT_Y_HFC13)},
	{"BLT_Y_HFC14", offsetof(struct b2r2_memory_map, BLT_Y_HFC14)},
	{"BLT_Y_HFC15", offsetof(struct b2r2_memory_map, BLT_Y_HFC15)},
	{"BLT_Y_VFC0", offsetof(struct b2r2_memory_map, BLT_Y_VFC0)},
	{"BLT_Y_VFC1", offsetof(struct b2r2_memory_map, BLT_Y_VFC1)},
	{"BLT_Y_VFC2", offsetof(struct b2r2_memory_map, BLT_Y_VFC2)},
	{"BLT_Y_VFC3", offsetof(struct b2r2_memory_map, BLT_Y_VFC3)},
	{"BLT_Y_VFC4", offsetof(struct b2r2_memory_map, BLT_Y_VFC4)},
	{"BLT_Y_VFC5", offsetof(struct b2r2_memory_map, BLT_Y_VFC5)},
	{"BLT_Y_VFC6", offsetof(struct b2r2_memory_map, BLT_Y_VFC6)},
	{"BLT_Y_VFC7", offsetof(struct b2r2_memory_map, BLT_Y_VFC7)},
	{"BLT_Y_VFC8", offsetof(struct b2r2_memory_map, BLT_Y_VFC8)},
	{"BLT_Y_VFC9", offsetof(struct b2r2_memory_map, BLT_Y_VFC9)},
	{"BLT_HFC0", offsetof(struct b2r2_memory_map, BLT_HFC0)},
	{"BLT_HFC1", offsetof(struct b2r2_memory_map, BLT_HFC1)},
	{"BLT_HFC2", offsetof(struct b2r2_memory_map, BLT_HFC2)},
	{"BLT_HFC3", offsetof(struct b2r2_memory_map, BLT_HFC3)},
	{"BLT_HFC4", offsetof(struct b2r2_memory_map, BLT_HFC4)},
	{"BLT_HFC5", offsetof(struct b2r2_memory_map, BLT_HFC5)},
	{"BLT_HFC6", offsetof(struct b2r2_memory_map, BLT_HFC6)},
	{"BLT_HFC7", offsetof(struct b2r2_memory_map, BLT_HFC7)},
	{"BLT_HFC8", offsetof(struct b2r2_memory_map, BLT_HFC8)},
	{"BLT_HFC9", offsetof(struct b2r2_memory_map, BLT_HFC9)},
	{"BLT_HFC10", offsetof(struct b2r2_memory_map, BLT_HFC10)},
	{"BLT_HFC11", offsetof(struct b2r2_memory_map, BLT_HFC11)},
	{"BLT_HFC12", offsetof(struct b2r2_memory_map, BLT_HFC12)},
	{"BLT_HFC13", offsetof(struct b2r2_memory_map, BLT_HFC13)},
	{"BLT_HFC14", offsetof(struct b2r2_memory_map, BLT_HFC14)},
	{"BLT_HFC15", offsetof(struct b2r2_memory_map, BLT_HFC15)},
	{"BLT_VFC0", offsetof(struct b2r2_memory_map, BLT_VFC0)},
	{"BLT_VFC1", offsetof(struct b2r2_memory_map, BLT_VFC1)},
	{"BLT_VFC2", offsetof(struct b2r2_memory_map, BLT_VFC2)},
	{"BLT_VFC3", offsetof(struct b2r2_memory_map, BLT_VFC3)},
	{"BLT_VFC4", offsetof(struct b2r2_memory_map, BLT_VFC4)},
	{"BLT_VFC5", offsetof(struct b2r2_memory_map, BLT_VFC5)},
	{"BLT_VFC6", offsetof(struct b2r2_memory_map, BLT_VFC6)},
	{"BLT_VFC7", offsetof(struct b2r2_memory_map, BLT_VFC7)},
	{"BLT_VFC8", offsetof(struct b2r2_memory_map, BLT_VFC8)},
	{"BLT_VFC9", offsetof(struct b2r2_memory_map, BLT_VFC9)},
};

#ifdef HANDLE_TIMEOUTED_JOBS
/**
 * printk_regs() - Print B2R2 registers to printk
 */
static void printk_regs(struct b2r2_core *core)
{
#ifdef CONFIG_B2R2_DEBUG
	int i;

	for (i = 0; i < ARRAY_SIZE(debugfs_regs); i++) {
		unsigned long value = readl(
			(unsigned long *) (((u8 *) core->hw) +
				debugfs_regs[i].offset));
		b2r2_log_regdump(core->dev, "%s: %08lX\n",
			debugfs_regs[i].name,
			value);
	}
#endif
}
#endif

/**
 * debugfs_b2r2_reg_read() - Implements debugfs read for B2R2 register
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_reg_read(struct file *filp, char __user *buf,
				 size_t count, loff_t *f_pos)
{
	size_t dev_size;
	int ret = 0;
	unsigned long value;
	char *tmpbuf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);

	if (tmpbuf == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Read from B2R2 */
	value = readl((unsigned long *)
		filp->f_dentry->d_inode->i_private);

	/* Build the string */
	dev_size = sprintf(tmpbuf, "%8lX\n", value);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	/* Return it to user space */
	if (copy_to_user(buf, tmpbuf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;

out:
	if (tmpbuf != NULL)
		kfree(tmpbuf);
	return ret;
}

/**
 * debugfs_b2r2_reg_write() - Implements debugfs write for B2R2 register
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to write
 * @f_pos: File position
 *
 * Returns number of bytes written or negative error code
 */
static int debugfs_b2r2_reg_write(struct file *filp, const char __user *buf,
				  size_t count, loff_t *f_pos)
{
	char tmpbuf[80];
	u32 reg_value;
	int ret = 0;

	/* Adjust count */
	if (count >= sizeof(tmpbuf))
		count = sizeof(tmpbuf) - 1;
	/* Get it from user space */
	if (copy_from_user(tmpbuf, buf, count))
		return -EINVAL;
	tmpbuf[count] = 0;
	/* Convert from hex string */
	if (sscanf(tmpbuf, "%8lX", (unsigned long *) &reg_value) != 1)
		return -EINVAL;

	writel(reg_value, (u32 *)
		filp->f_dentry->d_inode->i_private);

	*f_pos += count;
	ret = count;

	return ret;
}

/**
 * debugfs_b2r2_reg_fops() - File operations for B2R2 register debugfs
 */
static const struct file_operations debugfs_b2r2_reg_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_reg_read,
	.write = debugfs_b2r2_reg_write,
};

/**
 * debugfs_b2r2_regs_read() - Implements debugfs read for B2R2 register dump
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes written or negative error code
 */
static int debugfs_b2r2_regs_read(struct file *filp, char __user *buf,
				  size_t count, loff_t *f_pos)
{
	size_t dev_size = 0;
	int ret = 0;
	int i;
	char *tmpbuf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);

	if (tmpbuf == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Build a giant string containing all registers */
	for (i = 0; i < ARRAY_SIZE(debugfs_regs); i++) {
		unsigned long value =
			readl((u32 *) (((u8 *)
				filp->f_dentry->d_inode->i_private) +
				debugfs_regs[i].offset));
		dev_size += sprintf(tmpbuf + dev_size, "%s: %08lX\n",
			debugfs_regs[i].name,
			value);
	}

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, tmpbuf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;

out:
	if (tmpbuf != NULL)
		kfree(tmpbuf);
	return ret;
}

/**
 * debugfs_b2r2_regs_fops() - File operations for B2R2 register dump debugfs
 */
static const struct file_operations debugfs_b2r2_regs_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_regs_read,
};

/**
 * debugfs_b2r2_stat_read() - Implements debugfs read for B2R2 statistics
 *
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_stat_read(struct file *filp, char __user *buf,
				  size_t count, loff_t *f_pos)
{
	size_t dev_size = 0;
	int ret = 0;
	int i = 0;
	char *tmpbuf = kmalloc(sizeof(char) * 4096, GFP_KERNEL);
	struct b2r2_core *core = filp->f_dentry->d_inode->i_private;

	if (tmpbuf == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Build a string containing all statistics */
	dev_size += sprintf(tmpbuf + dev_size, "Interrupts        : %lu\n",
			core->stat_n_irq);
	dev_size += sprintf(tmpbuf + dev_size, "Added jobs        : %lu\n",
			core->stat_n_jobs_added);
	dev_size += sprintf(tmpbuf + dev_size, "Removed jobs      : %lu\n",
			core->stat_n_jobs_removed);
	dev_size += sprintf(tmpbuf + dev_size, "Jobs in prio list : %lu\n",
			core->stat_n_jobs_in_prio_list);
	dev_size += sprintf(tmpbuf + dev_size, "Active jobs       : %lu\n",
			core->n_active_jobs);
	for (i = 0; i < ARRAY_SIZE(core->active_jobs); i++)
		dev_size += sprintf(tmpbuf + dev_size,
				"   Job in queue %d : 0x%08lx\n",
				i, (unsigned long) core->active_jobs[i]);
	dev_size += sprintf(tmpbuf + dev_size, "Clock requests    : %lu\n",
			core->clock_request_count);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, tmpbuf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;

out:
	if (tmpbuf != NULL)
		kfree(tmpbuf);
	return ret;
}

/**
 * debugfs_b2r2_stat_fops() - File operations for B2R2 statistics debugfs
 */
static const struct file_operations debugfs_b2r2_stat_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_stat_read,
};


/**
 * debugfs_b2r2_clock_read() - Implements debugfs read for
 *                             PMU B2R2 clock register
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_clock_read(struct file *filp, char __user *buf,
				   size_t count, loff_t *f_pos)
{
	/* 10 characters hex number + newline + string terminator; */
	char tmpbuf[10+2];
	size_t dev_size;
	int ret = 0;
	struct b2r2_core *core = filp->f_dentry->d_inode->i_private;

	unsigned long value = clk_get_rate(core->b2r2_clock);

	dev_size = sprintf(tmpbuf, "%#010lx\n", value);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, tmpbuf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;

out:
	return ret;
}

/**
 * debugfs_b2r2_clock_write() - Implements debugfs write for
 *                              PMU B2R2 clock register
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to write
 * @f_pos: File position
 *
 * Returns number of bytes written or negative error code
 */
static int debugfs_b2r2_clock_write(struct file *filp, const char __user *buf,
				    size_t count, loff_t *f_pos)
{
	char tmpbuf[80];
	u32 reg_value;
	int ret = 0;

	if (count >= sizeof(tmpbuf))
		count = sizeof(tmpbuf) - 1;
	if (copy_from_user(tmpbuf, buf, count))
		return -EINVAL;
	tmpbuf[count] = 0;
	if (sscanf(tmpbuf, "%8lX", (unsigned long *) &reg_value) != 1)
		return -EINVAL;

	/* NOTE: Not working yet */
	/*clk_set_rate(b2r2_core.b2r2_clock, (unsigned long) reg_value);*/

	*f_pos += count;
	ret = count;

	return ret;
}

/**
 * debugfs_b2r2_clock_fops() - File operations for PMU B2R2 clock debugfs
 */
static const struct file_operations debugfs_b2r2_clock_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_clock_read,
	.write = debugfs_b2r2_clock_write,
};

/**
 * debugfs_b2r2_enabled_read() - Implements debugfs read for
 *                             B2R2 Core Enable/Disable
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to read
 * @f_pos: File position
 *
 * Returns number of bytes read or negative error code
 */
static int debugfs_b2r2_enabled_read(struct file *filp, char __user *buf,
				   size_t count, loff_t *f_pos)
{
	/* 4 characters hex number + newline + string terminator; */
	char tmpbuf[4+2];
	size_t dev_size;
	int ret = 0;
	struct b2r2_core *core = filp->f_dentry->d_inode->i_private;

	dev_size = sprintf(tmpbuf, "%02X\n", core->control->enabled);

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (*f_pos + count > dev_size)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, tmpbuf, count))
		ret = -EINVAL;
	*f_pos += count;
	ret = count;
out:
	return ret;
}

/**
 * debugfs_b2r2_enabled_write() - Implements debugfs write for
 *                              B2R2 Core Enable/Disable
 * @filp: File pointer
 * @buf: User space buffer
 * @count: Number of bytes to write
 * @f_pos: File position
 *
 * Returns number of bytes written or negative error code
 */
static int debugfs_b2r2_enabled_write(struct file *filp, const char __user *buf,
				    size_t count, loff_t *f_pos)
{
	char tmpbuf[80];
	unsigned int enable;
	int ret = 0;
	struct b2r2_core *core = filp->f_dentry->d_inode->i_private;

	if (count >= sizeof(tmpbuf))
		count = sizeof(tmpbuf) - 1;
	if (copy_from_user(tmpbuf, buf, count))
		return -EINVAL;
	tmpbuf[count] = 0;
	if (sscanf(tmpbuf, "%02X", &enable) != 1)
		return -EINVAL;

	if (enable)
		core->control->enabled = true;
	else
		core->control->enabled = false;

	*f_pos += count;
	ret = count;

	return ret;
}

/**
 * debugfs_b2r2_enabled_fops() - File operations for B2R2 Core Enable/Disable debugfs
 */
static const struct file_operations debugfs_b2r2_enabled_fops = {
	.owner = THIS_MODULE,
	.read  = debugfs_b2r2_enabled_read,
	.write = debugfs_b2r2_enabled_write,
};

#endif

/**
 * init_hw() - B2R2 Hardware reset & initiliaze
 *
 * @pdev: B2R2 platform device
 *
 * 1)Register interrupt handler
 *
 * 2)B2R2 Register map
 *
 * 3)For resetting B2R2 hardware,write to B2R2 Control register the
 * B2R2BLT_CTLGLOBAL_soft_reset and then polling for on
 * B2R2 status register for B2R2BLT_STA1BDISP_IDLE flag.
 *
 * 4)Wait for B2R2 hardware to be idle (on a timeout rather than while loop)
 *
 * 5)Driver status reset
 *
 * 6)Recover from any error without any leaks.
 */
static int init_hw(struct b2r2_core *core)
{
	int result = 0;
	u32 uTimeOut = B2R2_RESET_TIMEOUT_VALUE;

	/* Put B2R2 into reset */
	clear_interrupts(core);

	writel(readl(&core->hw->BLT_CTL) | B2R2BLT_CTLGLOBAL_soft_reset,
		&core->hw->BLT_CTL);

	/* Enable interrupt handler */
	enable_irq(core->irq);

	b2r2_log_info(core->dev, "do a global reset..\n");

	/* Release reset */
	writel(0x00000000, &core->hw->BLT_CTL);

	b2r2_log_info(core->dev, "wait for B2R2 to be idle..\n");

	/* Wait for B2R2 to be idle (on a timeout rather than while loop) */
	while ((uTimeOut > 0) &&
	       ((readl(&core->hw->BLT_STA1) &
		 B2R2BLT_STA1BDISP_IDLE) == 0x0))
		uTimeOut--;
	if (uTimeOut == 0) {
		b2r2_log_err(core->dev,
			 "%s: B2R2 not idle after SW reset\n", __func__);
		result = -EAGAIN;
		goto b2r2_core_init_hw_timeout;
	}

#ifdef CONFIG_DEBUG_FS
	/* Register debug fs files for register access */
	if (!IS_ERR_OR_NULL(core->debugfs_core_root_dir) &&
			IS_ERR_OR_NULL(core->debugfs_regs_dir)) {
		core->debugfs_regs_dir = debugfs_create_dir("regs",
				core->debugfs_core_root_dir);
	}
	if (!IS_ERR_OR_NULL(core->debugfs_regs_dir)) {
		int i;
		debugfs_create_file("all", 0666, core->debugfs_regs_dir,
				(void *)core->hw, &debugfs_b2r2_regs_fops);
		/* Create debugfs entries for all static registers */
		for (i = 0; i < ARRAY_SIZE(debugfs_regs); i++)
			debugfs_create_file(debugfs_regs[i].name, 0666,
					core->debugfs_regs_dir,
					(void *)(((u8 *) core->hw) +
							debugfs_regs[i].offset),
					&debugfs_b2r2_reg_fops);
	}
#endif

	b2r2_log_info(core->dev, "%s ended..\n", __func__);
	return result;

	/* Recover from any error without any leaks */
b2r2_core_init_hw_timeout:
	/* Free B2R2 interrupt handler */
	free_irq(core->irq, core);

b2r2_init_request_irq_failed:
	if (core->hw)
		iounmap(core->hw);
	core->hw = NULL;

	return result;
}


/**
 * exit_hw() - B2R2 Hardware exit
 *
 * core->lock _must_ NOT be held
 */
static void exit_hw(struct b2r2_core *core)
{
	unsigned long flags;

	b2r2_log_info(core->dev, "%s started..\n", __func__);

#ifdef CONFIG_DEBUG_FS
	/* Unregister our debugfs entries */
	if (!IS_ERR_OR_NULL(core->debugfs_regs_dir)) {
		debugfs_remove_recursive(core->debugfs_regs_dir);
		core->debugfs_regs_dir = NULL;
	}
#endif
	b2r2_log_debug(core->dev, "%s: locking core->lock\n", __func__);
	spin_lock_irqsave(&core->lock, flags);

	/* Cancel all pending jobs */
	b2r2_log_debug(core->dev, "%s: canceling pending jobs\n", __func__);
	exit_job_list(core, &core->prio_queue);

	/*
	 * Soft reset B2R2 (Close all DMA,
	 * reset all state to idle, reset regs)
	 */
	b2r2_log_debug(core->dev, "%s: putting b2r2 in reset\n", __func__);
	writel(readl(&core->hw->BLT_CTL) | B2R2BLT_CTLGLOBAL_soft_reset,
		&core->hw->BLT_CTL);

	b2r2_log_debug(core->dev, "%s: clearing interrupts\n", __func__);
	clear_interrupts(core);

	/* Disable B2R2 interrupt handler */
	b2r2_log_debug(core->dev, "%s: disable interrupt handler\n", __func__);
	disable_irq(core->irq);

	b2r2_log_debug(core->dev, "%s: unlocking core->lock\n", __func__);
	spin_unlock_irqrestore(&core->lock, flags);

	b2r2_log_info(core->dev, "%s ended...\n", __func__);
}

/**
 * b2r2_probe() - This routine loads the B2R2 core driver
 *
 * @pdev: platform device.
 */
static int b2r2_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res = NULL;
	struct b2r2_core *core = NULL;
	struct b2r2_control *control = NULL;
	struct b2r2_platform_data *pdata = NULL;
	int debug_init = 0;

	BUG_ON(pdev == NULL);
	BUG_ON(pdev->id < 0 || pdev->id >= B2R2_MAX_NBR_DEVICES);

	pdata = pdev->dev.platform_data;

	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core) {
		dev_err(&pdev->dev, "b2r2 core alloc failed\n");
		ret = -EINVAL;
		goto error_exit;
	}

	core->dev = &pdev->dev;
	dev_set_drvdata(core->dev, core);
	if (pdev->id)
		snprintf(core->name, sizeof(core->name), "b2r2_%d", pdev->id);
	else
		snprintf(core->name, sizeof(core->name), "b2r2");

	dev_info(&pdev->dev, "init started.\n");

	/* Init spin locks */
	spin_lock_init(&core->lock);

	/* Init job queues */
	INIT_LIST_HEAD(&core->prio_queue);

#ifdef HANDLE_TIMEOUTED_JOBS
	/* Create work queue for callbacks & timeout */
	INIT_DELAYED_WORK(&core->timeout_work, timeout_work_function);
#endif

	/* Work queue for callbacks and timeout management */
	core->work_queue = create_workqueue("B2R2");
	if (!core->work_queue) {
		ret = -ENOMEM;
		goto error_exit;
	}

	/* Get the clock for B2R2 */
	core->b2r2_clock = clk_get(core->dev, pdata->clock_id);
	if (IS_ERR(core->b2r2_clock)) {
		ret = PTR_ERR(core->b2r2_clock);
		dev_err(&pdev->dev, "clk_get %s failed\n", pdata->clock_id);
		goto error_exit;
	}

	/* Get the B2R2 regulator */
	core->b2r2_reg = regulator_get(core->dev, pdata->regulator_id);
	if (IS_ERR(core->b2r2_reg)) {
		ret = PTR_ERR(core->b2r2_reg);
		dev_err(&pdev->dev, "regulator_get %s failed "
			"(dev_name=%s)\n", pdata->regulator_id,
			dev_name(core->dev));
		goto error_exit;
	}

	/* Get the VANA regulator */
	core->vana_reg = regulator_get(core->dev, "vdddsi1v2");
	/* For some platforms vana does not exist as a regulator */
	if (IS_ERR(core->vana_reg))
		dev_err(&pdev->dev, "regulator_get vana failed (dev_name=%s)\n",
				dev_name(core->dev));

	/* Init power management */
	mutex_init(&core->domain_lock);
	INIT_DELAYED_WORK_DEFERRABLE(&core->domain_disable_work,
			domain_disable_work_function);
	core->domain_enabled = false;
	core->valid = false;
	core->lockdown = false;

	/* Map B2R2 into kernel virtual memory space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		goto error_exit;

	/* Hook up irq */
	core->irq = platform_get_irq(pdev, 0);
	if (core->irq <= 0) {
		dev_err(&pdev->dev, "%s: Failed to request irq (irq=%d)\n",
			__func__, core->irq);
		goto error_exit;
	}

	/* Set up interrupt handler */
	ret = request_irq(core->irq, b2r2_irq_handler, IRQF_SHARED,
			     "b2r2-interrupt", core);
	if (ret) {
		b2r2_log_err(core->dev,
			"%s: failed to register IRQ for B2R2\n", __func__);
		goto error_exit;
	}
	disable_irq(core->irq);

	core->hw = (struct b2r2_memory_map *) ioremap(res->start,
			 res->end - res->start + 1);
	if (core->hw == NULL) {
		dev_err(&pdev->dev, "%s: ioremap failed\n", __func__);
		ret = -ENOMEM;
		goto error_exit;
	}

	dev_dbg(core->dev, "b2r2 structure address %p\n", core->hw);

	control = kzalloc(sizeof(*control), GFP_KERNEL);
	if (!control) {
		dev_err(&pdev->dev, "b2r2 control alloc failed\n");
		ret = -EINVAL;
		goto error_exit;
	}

	control->data = (void *)core;
	control->id = pdev->id;
	control->dev = &pdev->dev; /* Temporary device */

	core->op_size = B2R2_PLUG_OPCODE_SIZE_DEFAULT;
	core->ch_size = B2R2_PLUG_CHUNK_SIZE_DEFAULT;
	core->pg_size = B2R2_PLUG_PAGE_SIZE_DEFAULT;
	core->mg_size = B2R2_PLUG_MESSAGE_SIZE_DEFAULT;
	core->min_req_time = 0;

#ifdef CONFIG_DEBUG_FS
	core->debugfs_root_dir = debugfs_create_dir(core->name, NULL);
	if (!IS_ERR_OR_NULL(core->debugfs_root_dir)) {
		core->debugfs_core_root_dir = debugfs_create_dir("core",
				core->debugfs_root_dir);
		control->debugfs_debug_root_dir = debugfs_create_dir("debug",
				core->debugfs_root_dir);
		control->mem_heap.debugfs_root_dir = debugfs_create_dir("mem",
				core->debugfs_root_dir);
		control->debugfs_root_dir = debugfs_create_dir("blt",
				core->debugfs_root_dir);
	}

	if (!IS_ERR_OR_NULL(core->debugfs_core_root_dir)) {
		debugfs_create_file("stats", 0666, core->debugfs_core_root_dir,
				core, &debugfs_b2r2_stat_fops);
		debugfs_create_file("clock", 0666, core->debugfs_core_root_dir,
				core, &debugfs_b2r2_clock_fops);
		debugfs_create_file("enabled", 0666,
				core->debugfs_core_root_dir,
				core, &debugfs_b2r2_enabled_fops);
		debugfs_create_u8("op_size", 0666, core->debugfs_core_root_dir,
				&core->op_size);
		debugfs_create_u8("ch_size", 0666, core->debugfs_core_root_dir,
				&core->ch_size);
		debugfs_create_u8("pg_size", 0666, core->debugfs_core_root_dir,
				&core->pg_size);
		debugfs_create_u8("mg_size", 0666, core->debugfs_core_root_dir,
				&core->mg_size);
		debugfs_create_u16("min_req_time", 0666,
			core->debugfs_core_root_dir, &core->min_req_time);
	}
#endif

	ret = b2r2_debug_init(control);
	if (ret < 0) {
		dev_err(&pdev->dev, "b2r2_debug_init failed\n");
		goto error_exit;
	}
	debug_init = 1;

	/* Initialize b2r2_control */
	ret = b2r2_control_init(control);
	if (ret < 0) {
		b2r2_log_err(&pdev->dev, "b2r2_control_init failed\n");
		goto error_exit;
	}
	core->control = control;

	/* Add the control to the blitter */
	kref_init(&control->ref);
	control->enabled = true;
	control->bypass = false;
	b2r2_blt_add_control(control);

	b2r2_core[pdev->id] = core;
	dev_info(&pdev->dev, "%s done.\n", __func__);

	return ret;

	/* Recover from any error if something fails */
error_exit:
	kfree(control);

	if (!IS_ERR_OR_NULL(core->b2r2_reg))
		regulator_put(core->b2r2_reg);

	if (!IS_ERR_OR_NULL(core->vana_reg))
		regulator_put(core->vana_reg);

	if (!IS_ERR_OR_NULL(core->b2r2_clock))
		clk_put(core->b2r2_clock);

	if (!IS_ERR_OR_NULL(core->work_queue))
		destroy_workqueue(core->work_queue);

	if (core->hw)
		iounmap(core->hw);

	if (debug_init)
		b2r2_debug_exit();

#ifdef CONFIG_DEBUG_FS
	if (!IS_ERR_OR_NULL(core->debugfs_root_dir)) {
		debugfs_remove_recursive(core->debugfs_root_dir);
		core->debugfs_root_dir = NULL;
	}
#endif
	kfree(core);

	dev_info(&pdev->dev, "%s done with errors (%d).\n", __func__, ret);

	return ret;
}

void b2r2_core_release(struct kref *control_ref)
{
	struct b2r2_control *control = container_of(
			control_ref, struct b2r2_control, ref);
	struct b2r2_core *core = control->data;
	int id = control->id;
	unsigned long flags;
#ifdef CONFIG_B2R2_DEBUG
	struct device *dev = core->dev;
#endif

	b2r2_log_info(dev, "%s: enter\n", __func__);

	/* Exit b2r2 control module */
	b2r2_control_exit(control);
	kfree(control);
	b2r2_debug_exit();

#ifdef HANDLE_TIMEOUTED_JOBS
	cancel_delayed_work(&core->timeout_work);
#endif

	/*
	 * Flush B2R2 work queue (call all callbacks for
	 * cancelled jobs)
	 */
	flush_workqueue(core->work_queue);

	/* Make sure the power is turned off */
	cancel_delayed_work_sync(&core->domain_disable_work);

	/* Unmap B2R2 registers */
	b2r2_log_info(dev, "%s: unmap b2r2 registers..\n", __func__);
	if (core->hw) {
		iounmap(core->hw);
		core->hw = NULL;
	}

	destroy_workqueue(core->work_queue);

	spin_lock_irqsave(&core->lock, flags);
	core->work_queue = NULL;
	spin_unlock_irqrestore(&core->lock, flags);

	/* Return the clock */
	clk_put(core->b2r2_clock);
	regulator_put(core->b2r2_reg);
	if (core->vana_reg)
		regulator_put(core->vana_reg);

	core->dev = NULL;
	kfree(core);
	b2r2_core[id] = NULL;

	b2r2_log_info(dev, "%s: exit\n", __func__);
}


/**
 * b2r2_remove - This routine unloads b2r2 driver
 *
 * @pdev: platform device.
 */
static int b2r2_remove(struct platform_device *pdev)
{
	struct b2r2_core *core;

	BUG_ON(pdev == NULL);

	core = dev_get_drvdata(&pdev->dev);
	BUG_ON(core == NULL);
	b2r2_log_info(&pdev->dev, "%s: Started\n", __func__);

	/* Free B2R2 interrupt handler */
	b2r2_log_debug(core->dev, "%s: freeing interrupt handler\n", __func__);
	free_irq(core->irq, core);

#ifdef CONFIG_DEBUG_FS
	if (!IS_ERR_OR_NULL(core->debugfs_root_dir)) {
		debugfs_remove_recursive(core->debugfs_root_dir);
		core->debugfs_root_dir = NULL;
	}
#endif

	/* Flush B2R2 work queue (call all callbacks) */
	flush_workqueue(core->work_queue);

	/* Remove control from blitter */
	core->control->enabled = false;
	b2r2_blt_remove_control(core->control);
	kref_put(&core->control->ref, b2r2_core_release);

	b2r2_log_info(&pdev->dev, "%s: Ended\n", __func__);

	return 0;
}

/**
 * b2r2_reset_hold() - This routine resets b2r2 in a controlled fashion and
 *                     holds the cores in a suspended state.
 *
 */
int b2r2_reset_hold(void)
{
	struct b2r2_core *core;
	int i;

	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		core = b2r2_core[i];
		if (core) {
			mutex_lock(&core->domain_lock);
			core->lockdown = true;
			core->domain_request_count = 0;
			exit_hw(core);
			clk_disable(core->b2r2_clock);
			regulator_disable(core->b2r2_reg);
			/* VANA is tighly coupled to DSS EPOD */
			if (core->vana_reg)
				regulator_disable(core->vana_reg);
			core->domain_enabled = false;

			/* Flush B2R2 work queue (call all callbacks) */
			flush_workqueue(core->work_queue);

#ifdef HANDLE_TIMEOUTED_JOBS
			cancel_delayed_work(&core->timeout_work);
#endif

			/*
			 * Flush B2R2 work queue (call all callbacks for
			 * cancelled jobs)
			 */
			flush_workqueue(core->work_queue);

			/* Make sure power is turned off */
			cancel_delayed_work_sync(&core->domain_disable_work);

			mutex_unlock(&core->domain_lock);
		}
	}

	return 0;
}

/**
 * b2r2_reset_release() - This routine lets go of the b2r2 reset
 *
 */
int b2r2_reset_release(void)
{
	struct b2r2_core *core;
	int i;

	for (i = 0; i < B2R2_MAX_NBR_DEVICES; i++) {
		core = b2r2_core[i];
		if (core) {
			mutex_lock(&core->domain_lock);
			core->lockdown = false;
			mutex_unlock(&core->domain_lock);
		}
	}

	return 0;
}

/**
 * b2r2_suspend() - This routine puts the B2R2 device in to sustend state.
 * @pdev: platform device.
 *
 * This routine stores the current state of the b2r2 device and puts in to
 * suspend state.
 *
 */
static int b2r2_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct b2r2_core *core;

	BUG_ON(pdev == NULL);
	core = dev_get_drvdata(&pdev->dev);
	BUG_ON(core == NULL);
	b2r2_log_info(core->dev, "%s\n", __func__);

	/* Flush B2R2 work queue (call all callbacks) */
	flush_workqueue(core->work_queue);

#ifdef HANDLE_TIMEOUTED_JOBS
	cancel_delayed_work(&core->timeout_work);
#endif

	/*
	 * Flush B2R2 work queue (call all callbacks for
	 * cancelled jobs)
	 */
	flush_workqueue(core->work_queue);

	/* Make sure power is turned off */
	cancel_delayed_work_sync(&core->domain_disable_work);

	return 0;
}


/**
 * b2r2_resume() - This routine resumes the B2R2 device from sustend state.
 * @pdev: platform device.
 *
 * This routine restore back the current state of the b2r2 device resumes.
 *
 */
static int b2r2_resume(struct platform_device *pdev)
{
	struct b2r2_core *core;

	BUG_ON(pdev == NULL);
	core = dev_get_drvdata(&pdev->dev);
	BUG_ON(core == NULL);
	b2r2_log_info(core->dev, "%s\n", __func__);

	return 0;
}

void b2r2_core_print_stats(struct b2r2_core *core)
{
	b2r2_log_info(core->dev,
		"%s: n_irq %ld, n_irq_exit %ld, n_irq_skipped %ld,\n"
		"n_jobs_added %ld, n_active_jobs %ld, "
		"n_jobs_in_prio_list %ld,\n"
		"n_jobs_removed %ld\n",
		__func__,
		core->stat_n_irq,
		core->stat_n_irq_exit,
		core->stat_n_irq_skipped,
		core->stat_n_jobs_added,
		core->n_active_jobs,
		core->stat_n_jobs_in_prio_list,
		core->stat_n_jobs_removed);
}

/**
 * struct platform_b2r2_driver - Platform driver configuration for the
 * B2R2 core driver
 */
static struct platform_driver platform_b2r2_driver = {
	.remove = b2r2_remove,
	.driver = {
		.name	= "b2r2",
	},
	/* TODO implement power mgmt functions */
	.suspend = b2r2_suspend,
	.resume =  b2r2_resume,
};


/**
 * b2r2_init() - Module init function for the B2R2 core module
 */
static int __init b2r2_init(void)
{
	printk(KERN_INFO "%s\n", __func__);
	return platform_driver_probe(&platform_b2r2_driver, b2r2_probe);
}
module_init(b2r2_init);

/**
 * b2r2_exit() - Module exit function for the B2R2 core module
 */
static void __exit b2r2_exit(void)
{
	printk(KERN_INFO "%s\n", __func__);
	platform_driver_unregister(&platform_b2r2_driver);
	return;
}
module_exit(b2r2_exit);


/** Module is having GPL license */

MODULE_LICENSE("GPL");

/** Module author & discription */

MODULE_AUTHOR("Robert Fekete (robert.fekete@stericsson.com)");
MODULE_DESCRIPTION("B2R2 Core driver");
