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

#ifndef __B2R2_CORE_H__
#define __B2R2_CORE_H__

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

/**
 * B2R2_RESET_TIMEOUT_VALUE - The number of times to read the status register
 *                             waiting for b2r2 to go idle after soft reset.
 */
#define B2R2_RESET_TIMEOUT_VALUE (1500)

/**
 * B2R2_CLK_FLAG - Value to write into clock reg to turn clock on
 */
#define B2R2_CLK_FLAG (0x125)

/**
 * DEBUG_CHECK_ADDREF_RELEASE - Define this to enable addref / release debug
 */
#define DEBUG_CHECK_ADDREF_RELEASE 1

#ifdef CONFIG_DEBUG_FS
/**
 * HANDLE_TIMEOUTED_JOBS - Define this to check jobs for timeout and cancel them
 */
#define HANDLE_TIMEOUTED_JOBS
#define JOB_TIMEOUT (HZ/2)
#endif

/**
 * B2R2_CLOCK_ALWAYS_ON - Define this to disable power save clock turn off
 */
/* #define B2R2_CLOCK_ALWAYS_ON 1 */

/**
 * START_SENTINEL - Watch guard to detect job overwrites
 */
#define START_SENTINEL 0xBABEDEEA

/**
 * STOP_SENTINEL - Watch guard to detect job overwrites
 */
#define END_SENTINEL 0xDADBDCDD

/**
 * B2R2_CORE_LOWEST_PRIO - Lowest prio allowed
 */
#define B2R2_CORE_LOWEST_PRIO -19
/**
 * B2R2_CORE_HIGHEST_PRIO - Highest prio allowed
 */
#define B2R2_CORE_HIGHEST_PRIO 20

/**
 * B2R2_DOMAIN_DISABLE -
 */
#define B2R2_DOMAIN_DISABLE_TIMEOUT (HZ/100)

/**
 * B2R2_REGULATOR_RETRY_COUNT -
 */
#define B2R2_REGULATOR_RETRY_COUNT 10


#ifdef DEBUG_CHECK_ADDREF_RELEASE

/**
 * struct addref_release - Represents one addref or release. Used
 *                         to debug addref / release problems
 *
 * @addref: true if this represents an addref else it represents
 *          a release.
 * @job: The job that was referenced
 * @caller: The caller of the addref or release
 * @ref_count: The job reference count after addref / release
 */
struct addref_release {
	bool addref;
	struct b2r2_core_job *job;
	const char *caller;
	int ref_count;
};

#endif

/**
 * struct b2r2_core - Administration data for B2R2 core
 *
 * @lock: Spin lock protecting the b2r2_core structure and the B2R2 HW
 * @hw: B2R2 registers memory mapped
 * @pmu_b2r2_clock: Control of B2R2 clock
 * @log_dev: Device used for logging via dev_... functions
 *
 * @prio_queue: Queue of jobs sorted in priority order
 * @active_jobs: Array containing pointer to zero or one job per queue
 * @n_active_jobs: Number of active jobs
 * @jiffies_last_active: jiffie value when adding last active job
 * @jiffies_last_irq: jiffie value when last irq occured
 * @timeout_work: Work structure for timeout work
 *
 * @next_job_id: Contains the job id that will be assigned to the next
 *               added job.
 *
 * @clock_request_count: When non-zero, clock is on
 * @clock_off_timer: Kernel timer to handle delayed turn off of clock
 *
 * @work_queue: Work queue to handle done jobs (callbacks) and timeouts in
 *              non-interrupt context.
 *
 * @stat_n_irq: Number of interrupts (statistics)
 * @stat_n_jobs_added: Number of jobs added (statistics)
 * @stat_n_jobs_removed: Number of jobs removed (statistics)
 * @stat_n_jobs_in_prio_list: Number of jobs in prio list (statistics)
 *
 * @debugfs_root_dir: Root directory for B2R2 debugfs
 *
 * @ar: Circular array of addref / release debug structs
 * @ar_write: Where next write will occur
 * @ar_read: First valid place to read. When ar_read == ar_write then
 *           the array is empty.
 */
struct b2r2_core {
	spinlock_t       lock;

	struct b2r2_memory_map *hw;

	u8 op_size;
	u8 ch_size;
	u8 pg_size;
	u8 mg_size;
	u16 min_req_time;
	int irq;

	char name[16];
	struct device *dev;

	struct list_head prio_queue;

	struct b2r2_core_job *active_jobs[B2R2_CORE_QUEUE_NO_OF];
	unsigned long    n_active_jobs;

	unsigned long    jiffies_last_active;
	unsigned long    jiffies_last_irq;
#ifdef HANDLE_TIMEOUTED_JOBS
	struct delayed_work     timeout_work;
#endif
	int              next_job_id;

	unsigned long    clock_request_count;
	struct timer_list clock_off_timer;

	struct workqueue_struct *work_queue;

	/* Statistics */
	unsigned long    stat_n_irq_exit;
	unsigned long    stat_n_irq_skipped;
	unsigned long    stat_n_irq;
	unsigned long    stat_n_jobs_added;
	unsigned long    stat_n_jobs_removed;

	unsigned long    stat_n_jobs_in_prio_list;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root_dir;
	struct dentry *debugfs_core_root_dir;
	struct dentry *debugfs_regs_dir;
#endif

#ifdef DEBUG_CHECK_ADDREF_RELEASE
	/* Tracking release bug...*/
	struct addref_release ar[100];
	int ar_write;
	int ar_read;
#endif

	/* Power management variables */
	struct mutex domain_lock;
	struct delayed_work domain_disable_work;

	/*
	 * We need to keep track of both the number of domain_enable/disable()
	 * calls and whether the power was actually turned off, since the
	 * power off is done in a delayed job.
	 */
	bool domain_enabled;
	volatile bool valid;
	int domain_request_count;
	bool lockdown;

	struct clk *b2r2_clock;
	struct regulator *b2r2_reg;
	struct regulator *vana_reg;

	struct b2r2_control *control;
};

/**
 * b2r2_core_job_add() - Adds a job to B2R2 job queues
 *
 * The job reference count will be increased after this function
 * has been called and b2r2_core_job_release() must be called to
 * release the reference. The job callback function will be always
 * be called after the job is done or cancelled.
 *
 * @control: The b2r2 control entity
 * @job: Job to be added
 *
 * Returns 0 if OK else negative error code
 *
 */
int b2r2_core_job_add(struct b2r2_control *control,
		struct b2r2_core_job *job);

/**
 * b2r2_core_job_wait() - Waits for an added job to be done.
 *
 * @job: Job to wait for
 *
 * Returns 0 if job done else negative error code
 *
 */
int b2r2_core_job_wait(struct b2r2_core_job *job);

/**
 * b2r2_core_job_cancel() - Cancel an already added job.
 *
 * @job: Job to cancel
 *
 * Returns 0 if job cancelled or done else negative error code
 *
 */
int b2r2_core_job_cancel(struct b2r2_core_job *job);

/**
 * b2r2_core_job_find() - Finds job with given job id
 *
 * Reference count will be increased for the found job
 *
 * @control: The b2r2 control entity
 * @job_id: Job id to find
 *
 * Returns job if found, else NULL
 *
 */
struct b2r2_core_job *b2r2_core_job_find(struct b2r2_control *control,
		int job_id);

/**
 * b2r2_core_job_find_first_with_tag() - Finds first job with given tag
 *
 * Reference count will be increased for the found job.
 * This function can be used to find all jobs for a client, i.e.
 * when cancelling all jobs for a client.
 *
 * @control: The b2r2 control entity
 * @tag: Tag to find
 *
 * Returns job if found, else NULL
 *
 */
struct b2r2_core_job *b2r2_core_job_find_first_with_tag(
		struct b2r2_control *control, int tag);

/**
 * b2r2_core_job_addref() - Increase the job reference count.
 *
 * @job: Job to increase reference count for.
 * @caller: The function calling this function (for debug)
 */
void b2r2_core_job_addref(struct b2r2_core_job *job, const char *caller);

/**
 * b2r2_core_job_release() - Decrease the job reference count. The
 *                           job will be released (the release() function
 *                           will be called) when the reference count
 *                           reaches zero.
 *
 * @job: Job to decrease reference count for.
 * @caller: The function calling this function (for debug)
 */
void b2r2_core_job_release(struct b2r2_core_job *job, const char *caller);

void b2r2_core_print_stats(struct b2r2_core *core);

void b2r2_core_release(struct kref *control_ref);

#endif /* !defined(__B2R2_CORE_JOB_H__) */
