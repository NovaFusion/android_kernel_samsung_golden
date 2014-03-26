/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 internal definitions
 *
 * Author: Robert Fekete <robert.fekete@stericsson.com>
 * Author: Paul Wannback
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _LINUX_DRIVERS_VIDEO_B2R2_INTERNAL_H_
#define _LINUX_DRIVERS_VIDEO_B2R2_INTERNAL_H_

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/ktime.h>
#include <video/b2r2_blt.h>
#include <linux/debugfs.h>

#include "b2r2_global.h"
#include "b2r2_hw.h"

/**
 * B2R2_MAX_NBR_DEVICES - The maximum number of B2R2s handled
 */
#define B2R2_MAX_NBR_DEVICES 2

/* The maximum possible number of temporary buffers needed */
#define MAX_TMP_BUFS_NEEDED 2

/* Size of the color look-up table */
#define CLUT_SIZE 1024

/* The defined bits of the Interrupt Status Register */
#define B2R2_ITS_MASK 0x0FFFF0FF

/* The maximum possible number of blits */
#define MAX_LAST_REQUEST 5

/**
 * b2r2_op_type - the type of B2R2 operation to configure
 */
enum b2r2_op_type {
	B2R2_DIRECT_COPY,
	B2R2_DIRECT_FILL,
	B2R2_COPY,
	B2R2_FILL,
	B2R2_SCALE,
	B2R2_ROTATE,
	B2R2_SCALE_AND_ROTATE,
	B2R2_FLIP,
};

/**
 * b2r2_fmt_type - the type of buffer for a given format
 */
enum b2r2_fmt_type {
	B2R2_FMT_TYPE_RASTER,
	B2R2_FMT_TYPE_SEMI_PLANAR,
	B2R2_FMT_TYPE_PLANAR,
};

/**
 * b2r2_fmt_conv - the type of format conversion to do
 */
enum b2r2_fmt_conv {
	B2R2_FMT_CONV_NONE,
	B2R2_FMT_CONV_RGB_TO_YUV,
	B2R2_FMT_CONV_YUV_TO_RGB,
	B2R2_FMT_CONV_YUV_TO_YUV,
	B2R2_FMT_CONV_RGB_TO_BGR,
	B2R2_FMT_CONV_BGR_TO_RGB,
	B2R2_FMT_CONV_YUV_TO_BGR,
	B2R2_FMT_CONV_BGR_TO_YUV,
};

/**
 * enum b2r2_core_queue - Indicates the B2R2 queue that the job belongs to
 *
 * @B2R2_CORE_QUEUE_AQ1: Application queue 1
 * @B2R2_CORE_QUEUE_AQ2: Application queue 2
 * @B2R2_CORE_QUEUE_AQ3: Application queue 3
 * @B2R2_CORE_QUEUE_AQ4: Application queue 4
 * @B2R2_CORE_QUEUE_CQ1: Composition queue 1
 * @B2R2_CORE_QUEUE_CQ2: Composition queue 2
 * @B2R2_CORE_QUEUE_NO_OF: Number of queues
 */
enum b2r2_core_queue {
	B2R2_CORE_QUEUE_AQ1 = 0,
	B2R2_CORE_QUEUE_AQ2,
	B2R2_CORE_QUEUE_AQ3,
	B2R2_CORE_QUEUE_AQ4,
	B2R2_CORE_QUEUE_CQ1,
	B2R2_CORE_QUEUE_CQ2,
	B2R2_CORE_QUEUE_NO_OF,
};

#define B2R2_NUM_APPLICATIONS_QUEUES 4

/**
 * enum b2r2_core_job_state - Indicates the current state of the job
 *
 * @B2R2_CORE_JOB_IDLE: Never queued
 * @B2R2_CORE_JOB_QUEUED: In queue but not started yet
 * @B2R2_CORE_JOB_RUNNING: Running, executed by B2R2
 * @B2R2_CORE_JOB_DONE: Completed
 * @B2R2_CORE_JOB_CANCELED: Canceled
 */
enum b2r2_core_job_state {
	B2R2_CORE_JOB_IDLE = 0,
	B2R2_CORE_JOB_QUEUED,
	B2R2_CORE_JOB_RUNNING,
	B2R2_CORE_JOB_DONE,
	B2R2_CORE_JOB_CANCELED,
};

/**
 * b2r2_work_buf - specification for a temporary work buffer
 *
 * @size      - the size of the buffer (set by b2r2_node_split)
 * @phys_addr - the physical address of the buffer (set by b2r2_blt_main)
 */
struct b2r2_work_buf {
	u32 size;
	u32 phys_addr;
	void *virt_addr;
	u32 mem_handle;
};

struct tmp_buf {
	struct b2r2_work_buf buf;
	bool in_use;
};

/**
 * struct b2r2_control_instance - Represents the B2R2 instance
 *                                (one per open and blitter core)
 *
 * @lock: Lock to protect the instance
 * @control_id: The b2r2 core core control identifier
 * @control: The b2r2 core control entity
 *
 * @report_list: Ready requests that should be reported,
 * @report_list_waitq: Wait queue for report list
 * @no_of_active_requests: Number of requests added but not reported
 *                         in callback.
 * @synching: true if any client is waiting for b2r2_blt_synch(0)
 * @synch_done_waitq: Wait queue to handle synching on request_id 0
 */
struct b2r2_control_instance {
	struct mutex lock;
	int control_id;
	struct b2r2_control *control;

	/* Requests to be reported */
	struct list_head report_list;
	wait_queue_head_t report_list_waitq;

	/* Below for synching */
	u32 no_of_active_requests;
	bool synching;
	wait_queue_head_t synch_done_waitq;
};

/**
 * struct b2r2_node - Represents a B2R2 node with reqister values, executed
 *                    by B2R2. Should be allocated non-cached.
 *
 * @next: Next node
 * @physical_address: Physical address to be given to B2R2
 *                    (physical address of "node" member below)
 * @node: The B2R2 node with register settings. This is the data
 *        that B2R2 will use.
 *
 */
struct b2r2_node {
	struct b2r2_node *next;
	u32 physical_address;

	int src_tmp_index;
	int dst_tmp_index;

	int src_index;

	/* B2R2 regs comes here */
	struct b2r2_link_list node;
};

/**
 * struct b2r2_resolved_buf - Contains calculated information about
 *                            image buffers.
 *
 * @physical_address: Physical address of the buffer
 * @virtual_address: Virtual address of the buffer
 * @is_pmem: true if buffer is from pmem
 * @hwmem_session: Hwmem session
 * @hwmem_alloc: Hwmem alloc
 * @filep: File pointer of mapped file (like pmem device, frame buffer device)
 * @file_physical_start: Physical address of file start
 * @file_virtual_start: Virtual address of file start
 * @file_len: File len
 *
 */
struct b2r2_resolved_buf {
	u32                   physical_address;
	void                 *virtual_address;
	bool                  is_pmem;
	struct hwmem_alloc   *hwmem_alloc;
	/* Data for validation below */
	struct file          *filep;
	u32                   file_physical_start;
	u32                   file_virtual_start;
	u32                   file_len;
};

/**
 * b2r2_node_split_buf - information about a source or destination buffer
 *
 * @addr            - the physical base address
 * @chroma_addr     - the physical address of the chroma plane
 * @chroma_cr_addr  - the physical address of the Cr chroma plane
 * @fmt             - the buffer format
 * @fmt_type        - the buffer format type
 * @rect            - the rectangle of the buffer to use
 * @color           - the color value to use is case of a fill operation
 * @pitch           - the pixmap byte pitch
 * @height          - the pixmap height
 * @alpha_range     - the alpha range of the buffer (0-128 or 0-255)
 * @hso             - the horizontal scan order
 * @vso             - the vertical scan order
 * @endian          - the endianess of the buffer
 * @plane_selection - the plane to write if buffer is planar or semi-planar
 */
struct b2r2_node_split_buf {
	u32 addr;
	u32 chroma_addr;
	u32 chroma_cr_addr;

	enum b2r2_blt_fmt fmt;
	enum b2r2_fmt_type type;

	struct b2r2_blt_rect rect;
	struct b2r2_blt_rect win;

	s32 dx;
	s32 dy;

	u32 color;
	u16 pitch;
	u16 width;
	u16 height;

	enum b2r2_ty alpha_range;
	enum b2r2_ty hso;
	enum b2r2_ty vso;
	enum b2r2_ty endian;
	enum b2r2_tty dither;

	/* Plane selection (used when writing to a multibuffer format) */
	enum b2r2_tty plane_selection;

	/* Chroma plane selection (used when writing planar formats) */
	enum b2r2_tty chroma_selection;

	int tmp_buf_index;
};

/**
 * b2r2_node_split_job - an instance of a node split job
 *
 * @type          - the type of operation
 * @ivmx          - the ivmx matrix to use for color conversion
 * @ovmx          - the ovmx matrix to use for color conversion
 * @blend         - determines if blending is enabled
 * @clip          - determines if destination clipping is enabled
 * @rotation      - determines if rotation is requested
 * @scaling       - determines if scaling is needed
 * @fullrange     - determines YUV<->RGB conversion matrix (iVMx)
 * @swap_fg_bg    - determines if FG and BG should be swapped when blending
 * @flags         - the flags passed in the blt request
 * @flag_param    - parameter required by certain flags,
 *                  e.g. color for source color keying.
 * @transform     - the transforms passed in the blt request
 * @global_alpha  - the global alpha
 * @clip_rect     - the clipping rectangle to use
 * @h_rescale     - determmines if horizontal rescaling is enabled
 * @h_rsf         - the horizontal rescale factor
 * @v_rescale     - determines if vertical rescale is enabled
 * @v_rsf         - the vertical rescale factor
 * @src           - the incoming source buffer
 * @bg            - the incoming background buffer
 * @dst           - the outgoing destination buffer
 * @work_bufs     - work buffer specifications
 * @tmp_bufs      - temporary buffers
 * @buf_count     - the number of temporary buffers used for the job
 * @node_count    - the number of nodes used for the job
 * @max_buf_size  - the maximum size of temporary buffers
 */
struct b2r2_node_split_job {
	enum b2r2_op_type type;

	const u32 *ivmx;
	const u32 *ovmx;

	bool blend;
	bool clip;
	bool rotation;
	bool scaling;
	bool fullrange;

	bool swap_fg_bg;

	u32 flags;
	u32 flag_param;
	u32 transform;
	u32 global_alpha;

	struct b2r2_blt_rect clip_rect;

	bool h_rescale;
	u16 h_rsf;

	bool v_rescale;
	u16 v_rsf;

	struct b2r2_node_split_buf src;
	struct b2r2_node_split_buf bg;
	struct b2r2_node_split_buf dst;

	struct b2r2_work_buf work_bufs[MAX_TMP_BUFS_NEEDED];
	struct b2r2_node_split_buf tmp_bufs[MAX_TMP_BUFS_NEEDED];

	u32 buf_count;
	u32 node_count;
	u32 max_buf_size;
};

/**
 * struct b2r2_core_job - Represents a B2R2 core job
 *
 * @start_sentinel: Memory overwrite guard
 *
 * @tag: Client value. Used by b2r2_core_job_find_first_with_tag().
 * @data: Used to store a reference to b2r2_core
 * @prio: Job priority, from -19 up to 20. Mapped to the
 *        B2R2 application queues. Filled in by the client.
 * @first_node_address: Physical address of the first node. Filled
 *                      in by the client.
 * @last_node_address: Physical address of the last node. Filled
 *                     in by the client.
 *
 * @callback: Function that will be called when the job is done.
 * @acquire_resources: Function that allocates the resources needed
 *                     to execute the job (i.e. SRAM alloc). Must not
 *                     sleep if atomic, should fail with negative error code
 *                     if resources not available.
 * @release_resources: Function that releases the resources previously
 *                     allocated by acquire_resources (i.e. SRAM alloc).
 * @release: Function that will be called when the reference count reaches
 *           zero.
 *
 * @job_id: Unique id for this job, assigned by B2R2 core
 * @job_state: The current state of the job
 * @jiffies: Number of jiffies needed for this request
 * @ref_count: The number of references to the job
 * @list: List entry element for internal list management
 * @event: Wait queue event to wait for job done
 * @work: Work queue structure, for callback implementation
 *
 * @queue: The queue that this job shall be submitted to
 * @control: B2R2 Queue control
 * @pace_control: For composition queue only
 * @interrupt_context: Context for interrupt
 * @hw_ts_start: The point when the b2r2 HW queue is activated for this job
 * @nsec_active_in_hw: Time spent on the b2r2 HW queue for this job
 *
 * @end_sentinel: Memory overwrite guard
 */
struct b2r2_core_job {
	u32 start_sentinel;

	/* Data to be filled in by client */
	int tag;
	int data;
	int prio;
	u32 first_node_address;
	u32 last_node_address;
	void (*callback)(struct b2r2_core_job *);
	int (*acquire_resources)(struct b2r2_core_job *,
		bool atomic);
	void (*release_resources)(struct b2r2_core_job *,
		bool atomic);
	void (*release)(struct b2r2_core_job *);

	/* Output data, do not modify */
	int  job_id;
	enum b2r2_core_job_state job_state;
	unsigned long jiffies;

	/* Data below is internal to b2r2_core, do not modify */

	/* Reference counting */
	u32 ref_count;

	/* Internal data */
	struct list_head  list;
	wait_queue_head_t event;
	struct work_struct work;

	/* B2R2 HW data */
	enum b2r2_core_queue queue;
	u32 control;
	u32 pace_control;
	u32 interrupt_context;

	/* Profiler timing data */
	struct timespec hw_ts_start;
	s64 nsec_active_in_hw;

	u32 end_sentinel;
};

/**
 * struct b2r2_blt_request - Represents one B2R2 blit request
 *
 * @instance:           Back pointer to the instance structure
 * @list:               List item to keep track of requests per instance
 * @user_req:           The request received from userspace
 * @job:                The administration structure for the B2R2 job
 *                      consisting of one or more nodes
 * @node_split_job:     The administration structure for the B2R2
 *                      node split job
 * @first_node:         Pointer to the first B2R2 node
 * @request_id:         Request id for this job
 * @core_mask:          Bit mask with the cores doing part of the job
 * @node_split_handle:  Handle of the node split
 * @src_resolved:       Calculated info about the source buffer
 * @src_mask_resolved:  Calculated info about the source mask buffer
 * @bg_resolved:        Calculated info about the background buffer
 * @dst_resolved:       Calculated info about the destination buffer
 * @profile:            True if the blit shall be profiled, false otherwise
 * @ts_start:           Timestamp for start of job processing.
 * @nsec_active_in_cpu: Time between ts_start and that the hardware starts
 *                      processing the job.
 * @total_time_nsec:    Total job execution time including context switches and
 *                      queue time.
 */
struct b2r2_blt_request {
	struct b2r2_control_instance   *instance;
	struct list_head           list;
	struct b2r2_blt_req        user_req;
	struct b2r2_core_job       job;
	struct b2r2_node_split_job node_split_job;
	struct b2r2_node           *first_node;
	int                        request_id;
	u32                        core_mask;

	/* Resolved buffer addresses */
	struct b2r2_resolved_buf src_resolved;
	struct b2r2_resolved_buf src_mask_resolved;
	struct b2r2_resolved_buf bg_resolved;
	struct b2r2_resolved_buf dst_resolved;

	/* TBD: Info about SRAM usage & needs */
	struct b2r2_work_buf *bufs;
	u32 buf_count;

	/* color look-up table */
	void *clut;
	u32 clut_phys_addr;

	/* Profiling stuff */
	bool profile;
	struct timespec ts_start;
	s64 nsec_active_in_cpu;
	s64 total_time_nsec;
};

/**
 * struct b2r2_mem_heap - The memory heap
 *
 * @start_phys_addr: Physical memory start address
 * @start_virt_ptr: Virtual pointer to start
 * @size: Memory size
 * @align: Alignment
 * @blocks: List of all blocks
 * @heap_lock: Protection for the heap
 * @node_size: Size of each B2R2 node
 * @node_heap: Heap for B2R2 node allocations
 * @debugfs_root_dir: Debugfs B2R2 mem root dir
 * @debugfs_heap_stats: Debugfs B2R2 memory status
 * @debugfs_dir_blocks: Debugfs B2R2 free blocks dir
 */
struct b2r2_mem_heap {
	dma_addr_t       start_phys_addr;
	void             *start_virt_ptr;
	u32              size;
	u32              align;
	struct list_head blocks;
	spinlock_t       heap_lock;
	u32              node_size;
	struct dma_pool *node_heap;
#ifdef CONFIG_DEBUG_FS
	struct dentry   *debugfs_root_dir;
	struct dentry   *debugfs_heap_stats;
	struct dentry   *debugfs_dir_blocks;
#endif
};

/**
 * struct b2r2_mem_dump - The b2r2 memory dump parameters
 *
 */
struct b2r2_mem_dump {
	struct dentry                 *debugfs_root_dir;
	enum b2r2_blt_fmt             src_filter;
	enum b2r2_blt_fmt             dst_filter;
	bool                          capture;
	struct mutex                  lock;
	bool                          buffers_valid;
	size_t                        src_size;
	unsigned char                 *src_buffer;
	struct debugfs_blob_wrapper   src_info;
	struct dentry                 *debugfs_src_file;
	struct dentry                 *debugfs_src_info;
	size_t                        dst_size;
	unsigned char                 *dst_buffer;
	struct debugfs_blob_wrapper   dst_info;
	struct dentry                 *debugfs_dst_file;
	struct dentry                 *debugfs_dst_info;
};

/**
 * struct b2r2_control - The b2r2 core control structure
 *
 * @dev: The device handle of the b2r2 instance
 * @data: Used to store a reference to b2r2_core
 * @id: The id of the b2r2 instance
 * @ref: The b2r2 control reference count
 * @enabled: Indicated if the b2r2 core is enabled
 * @bypass: Indicates if the blitter operation should be omitted (aka dryrun)
 * @tmp_bufs: Temporary buffers needed in the node splitter
 * @filters_initialized: Indicating of filters has been
 *                       initialized for this b2r2 instance
 * @mem_heap: The b2r2 heap, e.g. used to allocate nodes
 * @last_req_lock: Lock protecting request array and index
 * @latest_request: Array with copies of previous requests issued
 * @latest_request_count: Count of previous requests required
 * @buf_index: Index were the next request will be stored
 * @debugfs_root_dir: The debugfs root directory, e.g. /debugfs/b2r2
 * @debugfs_debug_root_dir: The b2r2 debug root directory,
 *                          e.g. /debugfs/b2r2/debug
 * @stat_lock: Spin lock protecting the statistics
 * @stat_n_jobs_added: Number of jobs added to b2r2_core
 * @stat_n_jobs_released: Number of jobs released (job_release called)
 * @stat_n_jobs_in_report_list: Number of jobs currently in the report list
 * @stat_n_in_blt: Number of client threads currently exec inside b2r2_blt()
 * @stat_n_in_blt_synch: Number of client threads currently waiting for synch
 * @stat_n_in_blt_add: Number of client threads currenlty adding in b2r2_blt
 * @stat_n_in_blt_wait: Number of client threads currently waiting in b2r2_blt
 * @stat_n_in_synch_0: Number of client threads currently in b2r2_blt_sync
 *                     waiting for all client jobs to finish
 * @stat_n_in_synch_job: Number of client threads currently in b2r2_blt_sync
 *                       waiting specific job to finish
 * @stat_n_in_query_cap: Number of clients currently in query cap
 * @stat_n_in_open: Number of clients currently in b2r2_blt_open
 * @stat_n_in_release: Number of clients currently in b2r2_blt_release
 * @last_job_lock: Mutex protecting last_job
 * @last_job: The last running job on this b2r2 instance
 * @last_job_chars: Temporary buffer used in printing last_job
 * @prev_node_count: Node cound of last_job
 */
struct b2r2_control {
	struct device                   *dev;
	void                            *data;
	int                             id;
	struct kref                     ref;
	bool                            enabled;
	bool                            bypass;
	struct tmp_buf                  tmp_bufs[MAX_TMP_BUFS_NEEDED];
	int                             filters_initialized;
	struct b2r2_mem_heap            mem_heap;
#ifdef CONFIG_DEBUG_FS
	struct mutex                    last_req_lock;
	struct b2r2_blt_request         latest_request[MAX_LAST_REQUEST];
	unsigned int                    last_request_count;
	int                             buf_index;
	struct dentry                   *debugfs_root_dir;
	struct dentry                   *debugfs_debug_root_dir;
#endif
	struct mutex                    stat_lock;
	unsigned long                   stat_n_jobs_added;
	unsigned long                   stat_n_jobs_released;
	unsigned long                   stat_n_jobs_in_report_list;
	unsigned long                   stat_n_in_blt;
	unsigned long                   stat_n_in_blt_synch;
	unsigned long                   stat_n_in_blt_add;
	unsigned long                   stat_n_in_blt_wait;
	unsigned long                   stat_n_in_synch_0;
	unsigned long                   stat_n_in_synch_job;
	unsigned long                   stat_n_in_query_cap;
	unsigned long                   stat_n_in_open;
	unsigned long                   stat_n_in_release;
	struct mutex                    last_job_lock;
	struct b2r2_node                *last_job;
	char                            *last_job_chars;
	int                             prev_node_count;
	struct b2r2_mem_dump            dump;
};

/* FIXME: The functions below should be removed when we are
   switching to the new Robert Lind allocator */

/**
 * b2r2_blt_alloc_nodes() - Allocate nodes
 *
 * @node_count: Number of nodes to allocate
 *
 * Return:
 *   Returns a pointer to the first node in the node list.
 */
struct b2r2_node *b2r2_blt_alloc_nodes(struct b2r2_control *cont,
		int node_count);

/**
 * b2r2_blt_free_nodes() - Release nodes previously allocated via
 *                         b2r2_generate_nodes
 *
 * @first_node: First node in linked list of nodes
 */
void b2r2_blt_free_nodes(struct b2r2_control *cont,
		struct b2r2_node *first_node);

/**
 * b2r2_blt_module_init() - Initialize the B2R2 blt module
 */
int b2r2_blt_module_init(struct b2r2_control *cont);

/**
 * b2r2_blt_module_exit() - Un-initialize the B2R2 blt module
 */
void b2r2_blt_module_exit(struct b2r2_control *cont);

/**
 * b2r2_blt_add_control() - Add the b2r2 core control
 */
void b2r2_blt_add_control(struct b2r2_control *cont);

/**
 * b2r2_blt_remove_control() - Remove the b2r2 core control
 */
void b2r2_blt_remove_control(struct b2r2_control *cont);

#endif
