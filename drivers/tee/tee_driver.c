/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Martin Hovang <martin.xm.hovang@stericsson.com>
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/tee.h>
#include <linux/slab.h>
#include <linux/hwmem.h>

#define TEED_NAME "tee"
#define TEED_PFX "TEE: "

#define TEED_STATE_OPEN_DEV 0
#define TEED_STATE_OPEN_SESSION 1

static struct mutex sync;

static int tee_open(struct inode *inode, struct file *file);
static int tee_release(struct inode *inode, struct file *file);
static int tee_read(struct file *filp, char __user *buffer,
		    size_t length, loff_t *offset);
static int tee_write(struct file *filp, const char __user *buffer,
		     size_t length, loff_t *offset);

static inline void set_emsg(struct tee_session *ts, u32 msg, int line)
{
	pr_err(TEED_PFX "msg: 0x%08x at line: %d\n", msg, line);
	ts->err = msg;
	ts->origin = TEED_ORIGIN_DRIVER;
}

static void reset_session(struct tee_session *ts)
{
	int i;

	ts->state = TEED_STATE_OPEN_DEV;
	ts->err = TEED_SUCCESS;
	ts->origin = TEED_ORIGIN_DRIVER;
	ts->id = 0;
	for (i = 0; i < TEEC_CONFIG_PAYLOAD_REF_COUNT; i++)
		ts->vaddr[i] = NULL;
	ts->ta = NULL;
	ts->uuid = NULL;
	ts->cmd = 0;
	ts->driver_cmd = TEED_OPEN_SESSION;
	ts->ta_size = 0;
	ts->op = NULL;
}

static int copy_ta(struct tee_session *ts,
		   struct tee_session *ku_buffer)
{
	int ret = -EINVAL;
	size_t mem_chunks_length = 1;
	struct hwmem_mem_chunk mem_chunks;
	struct ta_addr *ta_addr;

	ts->ta_size = ku_buffer->ta_size;

	if (ts->ta_size == 0)
		return 0;

	ta_addr = kmalloc(sizeof(struct ta_addr), GFP_KERNEL);
	if (!ta_addr)
		return -ENOMEM;

	ta_addr->paddr = NULL;
	ta_addr->vaddr = NULL;

	ta_addr->alloc = hwmem_alloc(ts->ta_size,
				     (HWMEM_ALLOC_HINT_WRITE_COMBINE |
				      HWMEM_ALLOC_HINT_CACHED |
				      HWMEM_ALLOC_HINT_CACHE_WB |
				      HWMEM_ALLOC_HINT_CACHE_AOW |
				      HWMEM_ALLOC_HINT_INNER_AND_OUTER_CACHE),
				     (HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE |
				      HWMEM_ACCESS_IMPORT),
				     HWMEM_MEM_CONTIGUOUS_SYS);

	if (IS_ERR(ta_addr->alloc)) {
		set_emsg(ts, TEED_ERROR_OUT_OF_MEMORY, __LINE__);
		pr_err(TEED_PFX "[%s] couldn't alloc hwmem for TA\n",
		       __func__);
		kfree(ta_addr);
		return PTR_ERR(ta_addr->alloc);
	}

	ret = hwmem_pin(ta_addr->alloc, &mem_chunks, &mem_chunks_length);
	if (ret) {
		pr_err(TEED_PFX "[%s] couldn't pin TA buffer\n",
		       __func__);
		kfree(ta_addr);
		return ret;
	}

	ta_addr->paddr = (void *)mem_chunks.paddr;
	ta_addr->vaddr = hwmem_kmap(ta_addr->alloc);
	ts->ta = ta_addr;

	if (copy_from_user(ta_addr->vaddr, ku_buffer->ta, ts->ta_size)) {
		pr_err(TEED_PFX "[%s] error, copy_from_user failed\n",
		       __func__);
	}

	return 0;
}

static int copy_uuid(struct tee_session *ts,
		     struct tee_session *ku_buffer)
{
	ts->uuid = kmalloc(sizeof(struct tee_uuid), GFP_KERNEL);

	if (ts->uuid == NULL) {
		pr_err(TEED_PFX "[%s] error, out of memory (uuid)\n",
		       __func__);
		set_emsg(ts, TEED_ERROR_OUT_OF_MEMORY, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(ts->uuid, ku_buffer->uuid,
			   sizeof(struct tee_uuid))) {
		pr_err(TEED_PFX "[%s] error, copy_from_user failed\n",
		       __func__);
		set_emsg(ts, TEED_ERROR_COMMUNICATION, __LINE__);
		return -EIO;
	}

	return 0;
}

static inline void free_ta(struct ta_addr *ta)
{
	if (ta->alloc) {
		hwmem_kunmap(ta->alloc);
		hwmem_unpin(ta->alloc);
		hwmem_release(ta->alloc);
	}

	ta->alloc = NULL;
	ta->paddr = NULL;
	ta->vaddr = NULL;

	kfree(ta);
	ta = NULL;
}

static inline void free_operation(struct tee_session *ts,
				  struct hwmem_alloc **alloc,
				  int memrefs_allocated)
{
	int i;

	for (i = 0; i < memrefs_allocated; ++i) {
		if (ts->op->shm[i].buffer) {
			hwmem_kunmap(alloc[i]);
			hwmem_unpin(alloc[i]);
			hwmem_release(alloc[i]);
			ts->op->shm[i].buffer = NULL;
		}

		if (ts->vaddr[i])
			ts->vaddr[i] = NULL;
	}

	kfree(ts->op);
	ts->op = NULL;
}

static inline void memrefs_phys_to_virt(struct tee_session *ts)
{
	int i;

	for (i = 0; i < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++i) {
		if (ts->op->flags & (1 << i)) {
			ts->op->shm[i].buffer =
				phys_to_virt((unsigned long)
					     ts->op->shm[i].buffer);
		}
	}
}

static inline void memrefs_virt_to_phys(struct tee_session *ts)
{
	int i;

	for (i = 0; i < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++i) {
		if (ts->op->flags & (1 << i)) {
			ts->op->shm[i].buffer =
				(void *)virt_to_phys(ts->op->shm[i].buffer);
		}
	}
}

static int copy_memref_to_user(struct tee_session *ts,
			       struct tee_operation __user *ubuf_op,
			       int memref)
{
	unsigned long bytes_left;

	bytes_left = copy_to_user(ubuf_op->shm[memref].buffer,
				  ts->vaddr[memref],
				  ts->op->shm[memref].size);

	if (bytes_left != 0) {
		pr_err(TEED_PFX "[%s] failed to copy result to user space (%lu "
		       "bytes left of buffer).\n", __func__, bytes_left);
		return bytes_left;
	}

	bytes_left = put_user(ts->op->shm[memref].size,
			      &ubuf_op->shm[memref].size);

	if (bytes_left != 0) {
		pr_err(TEED_PFX "[%s] failed to copy result to user space (%lu "
		       "bytes left of size).\n", __func__, bytes_left);
		return -EINVAL;
	}

	bytes_left = put_user(ts->op->shm[memref].flags,
			      &ubuf_op->shm[memref].flags);
	if (bytes_left != 0) {
		pr_err(TEED_PFX "[%s] failed to copy result to user space (%lu "
		       "bytes left of flags).\n", __func__, bytes_left);
		return -EINVAL;
	}

	return 0;
}

static int copy_memref_to_kernel(struct tee_session *ts,
				 struct tee_session *ku_buffer,
				 struct hwmem_alloc **alloc,
				 int memref)
{
	int ret = -EINVAL;
	size_t mem_chunks_length = 1;
	struct hwmem_mem_chunk mem_chunks;

	if (ku_buffer->op->shm[memref].size != 0) {
		alloc[memref] = hwmem_alloc(
				    ku_buffer->op->shm[memref].size,
				    (HWMEM_ALLOC_HINT_WRITE_COMBINE |
				     HWMEM_ALLOC_HINT_CACHED |
				     HWMEM_ALLOC_HINT_CACHE_WB |
				     HWMEM_ALLOC_HINT_CACHE_AOW |
				     HWMEM_ALLOC_HINT_INNER_AND_OUTER_CACHE),
				    (HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE |
				     HWMEM_ACCESS_IMPORT),
				    HWMEM_MEM_CONTIGUOUS_SYS);

		if (IS_ERR(alloc[memref])) {
			pr_err(TEED_PFX "[%s] couldn't alloc hwmem_alloc "
			       " (memref: %d)\n", __func__, memref);
			return PTR_ERR(alloc[memref]);
		}

		ret = hwmem_pin(alloc[memref], &mem_chunks, &mem_chunks_length);
		if (ret) {
			pr_err(TEED_PFX "[%s] couldn't pin buffer "
			       "(memref: %d)\n", __func__, memref);
			return ret;
		}

		/*
		 * Since phys_to_virt is not working for hwmem memory we are
		 * storing the virtual addresses in separate array in
		 * tee_session and we keep the address of the physical pointers
		 * in the memref buffer.
		 */
		ts->op->shm[memref].buffer = (void *)mem_chunks.paddr;
		ts->vaddr[memref] = hwmem_kmap(alloc[memref]);

		/*
		 * Buffer unmapped/freed in invoke_command if this function
		 * fails.
		 */
		if (!ts->op->shm[memref].buffer || !ts->vaddr[memref]) {
			pr_err(TEED_PFX "[%s] out of memory (memref: %d)\n",
			       __func__, memref);
			return -ENOMEM;
		}

		if (ku_buffer->op->shm[memref].flags & TEEC_MEM_INPUT) {
			if (copy_from_user(ts->vaddr[memref],
					   ku_buffer->op->shm[memref].buffer,
					   ku_buffer->op->shm[memref].size)) {
				pr_err(TEED_PFX "[%s] error, copy_from_user "
				       "failed\n", __func__);
				set_emsg(ts, TEED_ERROR_COMMUNICATION,
					 __LINE__);
				return -EIO;
			}
		}
	} else {
		ts->op->shm[memref].buffer = NULL;
		ts->vaddr[memref] = NULL;
	}

	ts->op->shm[memref].size = ku_buffer->op->shm[memref].size;
	ts->op->shm[memref].flags = ku_buffer->op->shm[memref].flags;

	return 0;
}

static int open_tee_device(struct tee_session *ts,
			   struct tee_session *ku_buffer)
{
	int ret;

	if (ku_buffer->driver_cmd != TEED_OPEN_SESSION) {
		set_emsg(ts, TEED_ERROR_BAD_STATE, __LINE__);
		return -EINVAL;
	}

	if (ku_buffer->ta && ku_buffer->ta_size > 0) {
		ret = copy_ta(ts, ku_buffer);
	} else if (ku_buffer->uuid) {
		ret = copy_uuid(ts, ku_buffer);
	} else {
		set_emsg(ts, TEED_ERROR_COMMUNICATION, __LINE__);
		return -EINVAL;
	}

	ts->id = 0;
	ts->state = TEED_STATE_OPEN_SESSION;
	return ret;
}

static int invoke_command(struct tee_session *ts,
			  struct tee_session *ku_buffer,
			  struct tee_session __user *u_buffer)
{
	int i;
	int ret = 0;
	/* To keep track of which memrefs to free when failure occurs. */
	int memrefs_allocated = 0;
	struct hwmem_alloc *alloc[TEEC_CONFIG_PAYLOAD_REF_COUNT];

	ts->op = kmalloc(sizeof(struct tee_operation), GFP_KERNEL);

	if (!ts->op) {
		if (ts->op == NULL) {
			pr_err(TEED_PFX "[%s] error, out of memory "
			       "(op)\n", __func__);
			set_emsg(ts, TEED_ERROR_OUT_OF_MEMORY, __LINE__);
			return -ENOMEM;
		}
	}

	ts->op->flags = ku_buffer->op->flags;
	ts->cmd = ku_buffer->cmd;

	for (i = 0; i < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++i) {
		ts->op->shm[i].buffer = NULL;
		memrefs_allocated++;

		/* We only want to copy memrefs in use to kernel space. */
		if (ku_buffer->op->flags & (1 << i)) {
			ret = copy_memref_to_kernel(ts, ku_buffer, alloc, i);
			if (ret) {
				pr_err(TEED_PFX "[%s] failed copy memref[%d] "
				       "to kernel", __func__, i);
				goto err;
			}
		} else {
			ts->op->shm[i].size = 0;
			ts->op->shm[i].flags = 0;
		}
	}

	if (call_sec_world(ts, TEED_INVOKE)) {
		set_emsg(ts, TEED_ERROR_COMMUNICATION, __LINE__);
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++i) {
		if ((ku_buffer->op->flags & (1 << i)) &&
		    (ku_buffer->op->shm[i].flags & TEEC_MEM_OUTPUT) &&
		    (ts->vaddr[i] != NULL)) {
			ret = copy_memref_to_user(ts, u_buffer->op, i);
			if (ret) {
				pr_err(TEED_PFX "[%s] failed copy memref[%d] "
				       "to user", __func__, i);
				goto err;
			}
		}
	}
err:
	free_operation(ts, alloc, memrefs_allocated);

	return ret;
}

static int tee_open(struct inode *inode, struct file *filp)
{
	struct tee_session *ts;
	filp->private_data = kmalloc(sizeof(struct tee_session),
				     GFP_KERNEL);

	if (filp->private_data == NULL) {
		pr_err(TEED_PFX "[%s] allocation failed", __func__);
		return -ENOMEM;
	}

	ts = (struct tee_session *)(filp->private_data);
	reset_session(ts);

	return 0;
}

static int tee_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	filp->private_data = NULL;

	return 0;
}

/*
 * Called when a process, which already opened the dev file, attempts
 * to read from it. This function gets the current status of the session.
 */
static int tee_read(struct file *filp, char __user *buffer,
		    size_t length, loff_t *offset)
{
	struct tee_read buf;
	struct tee_session *ts;

	if (length != sizeof(struct tee_read)) {
		pr_err(TEED_PFX "[%s] error, incorrect input length\n",
		       __func__);
		return -EINVAL;
	}

	ts = (struct tee_session *)(filp->private_data);

	if (ts == NULL) {
		pr_err(TEED_PFX "[%s] error, private_data not "
		       "initialized\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sync);

	buf.err = ts->err;
	buf.origin = ts->origin;

	mutex_unlock(&sync);

	if (copy_to_user(buffer, &buf, length)) {
		pr_err(TEED_PFX "[%s] error, copy_to_user failed!\n",
		       __func__);
		return -EINVAL;
	}

	return length;
}

/*
 * Called when a process writes to a dev file.
 */
static int tee_write(struct file *filp, const char __user *buffer,
		     size_t length, loff_t *offset)
{
	struct tee_session ku_buffer;
	struct tee_session *ts;
	int ret = 0;

	if (length != sizeof(struct tee_session)) {
		pr_err(TEED_PFX "[%s] error, incorrect input length\n",
		       __func__);
		return -EINVAL;
	}

	if (copy_from_user(&ku_buffer, buffer, length)) {
		pr_err(TEED_PFX "[%s] error, tee_session "
		       "copy_from_user failed\n", __func__);
		return -EINVAL;
	}

	ts = (struct tee_session *)(filp->private_data);

	if (ts == NULL) {
		pr_err(TEED_PFX "[%s] error, private_data not "
		       "initialized\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sync);

	switch (ts->state) {
	case TEED_STATE_OPEN_DEV:
		ret = open_tee_device(ts, &ku_buffer);
		break;

	case TEED_STATE_OPEN_SESSION:
		switch (ku_buffer.driver_cmd) {
		case TEED_INVOKE:
			ret = invoke_command(ts, &ku_buffer,
					     (struct tee_session *)buffer);
			break;

		case TEED_CLOSE_SESSION:
			/* no caching implemented yet... */
			if (call_sec_world(ts, TEED_CLOSE_SESSION)) {
				set_emsg(ts, TEED_ERROR_COMMUNICATION,
					 __LINE__);
				ret = -EINVAL;
			}

			if (ts->ta)
				free_ta(ts->ta);

			reset_session(ts);
			break;

		default:
			set_emsg(ts, TEED_ERROR_BAD_PARAMETERS, __LINE__);
			ret = -EINVAL;
		}
		break;
	default:
		pr_err(TEED_PFX "[%s] unknown state\n", __func__);
		set_emsg(ts, TEED_ERROR_BAD_STATE, __LINE__);
		ret = -EINVAL;
	}

	/*
	 * We expect that ret has value zero when reaching the end here.
	 * If it has any other value some error must have occured.
	 */
	if (!ret) {
		ret = length;
	} else {
		pr_err(TEED_PFX "[%s], forcing error to -EINVAL\n", __func__);
		ret = -EINVAL;
	}

	mutex_unlock(&sync);

	return ret;
}

int teec_initialize_context(const char *name, struct tee_context *context)
{
	return TEED_SUCCESS;
}
EXPORT_SYMBOL(teec_initialize_context);

int teec_finalize_context(struct tee_context *context)
{
	return TEED_SUCCESS;
}
EXPORT_SYMBOL(teec_finalize_context);

int teec_open_session(struct tee_context *context,
		      struct tee_session *session,
		      const struct tee_uuid *destination,
		      unsigned int connection_method,
		      void *connection_data, struct tee_operation *operation,
		      unsigned int *error_origin)
{
	int res = TEED_SUCCESS;

	if (session == NULL || destination == NULL) {
		pr_err(TEED_PFX "[%s] session or destination == NULL\n",
		       __func__);
		if (error_origin != NULL)
			*error_origin = TEED_ORIGIN_DRIVER;
		res = TEED_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	reset_session(session);

	/*
	 * Open a session towards an application already loaded inside
	 * the TEE.
	 */
	session->uuid = kmalloc(sizeof(struct tee_uuid), GFP_KERNEL);

	if (session->uuid == NULL) {
		pr_err(TEED_PFX "[%s] error, out of memory (uuid)\n",
		       __func__);
		if (error_origin != NULL)
			*error_origin = TEED_ORIGIN_DRIVER;
		res = TEED_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	memcpy(session->uuid, destination, sizeof(struct tee_uuid));

	session->ta = NULL;
	session->id = 0;

exit:
	return res;
}
EXPORT_SYMBOL(teec_open_session);

int teec_close_session(struct tee_session *session)
{
	int res = TEED_SUCCESS;

	mutex_lock(&sync);

	if (session == NULL) {
		pr_err(TEED_PFX "[%s] error, session == NULL\n", __func__);
		res = TEED_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	if (call_sec_world(session, TEED_CLOSE_SESSION)) {
		pr_err(TEED_PFX "[%s] error, call_sec_world failed\n",
		       __func__);
		res = TEED_ERROR_GENERIC;
		goto exit;
	}

exit:
	if (session != NULL) {
		kfree(session->uuid);
		session->uuid = NULL;
	}

	mutex_unlock(&sync);
	return res;
}
EXPORT_SYMBOL(teec_close_session);

int teec_invoke_command(
	struct tee_session *session, unsigned int command_id,
	struct tee_operation *operation,
	unsigned int *error_origin)
{
	int res = TEED_SUCCESS;
	int i;

	mutex_lock(&sync);

	if (session == NULL || operation == NULL || error_origin == NULL) {
		pr_err(TEED_PFX "[%s] error, input parameters == NULL\n",
		       __func__);
		if (error_origin != NULL)
			*error_origin = TEED_ORIGIN_DRIVER;
		res = TEED_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	for (i = 0; i < 4; ++i) {
		/* We only want to translate memrefs in use. */
		if (operation->flags & (1 << i)) {
			operation->shm[i].buffer =
				(void *)virt_to_phys(
					operation->shm[i].buffer);
		}
	}
	session->op = operation;
	session->cmd = command_id;

	/*
	 * Call secure world
	 */
	if (call_sec_world(session, TEED_INVOKE)) {
		pr_err(TEED_PFX "[%s] error, call_sec_world failed\n",
		       __func__);
		if (error_origin != NULL)
			*error_origin = TEED_ORIGIN_DRIVER;
		res = TEED_ERROR_GENERIC;
	}
	if (session->err != TEED_SUCCESS) {
		pr_err(TEED_PFX "[%s] error, call_sec_world failed\n",
		       __func__);
		if (error_origin != NULL)
			*error_origin = session->origin;
		res = session->err;
	}

	memrefs_phys_to_virt(session);
	session->op = NULL;

exit:
	mutex_unlock(&sync);
	return res;
}
EXPORT_SYMBOL(teec_invoke_command);

int teec_allocate_shared_memory(struct tee_context *context,
				struct tee_sharedmemory *shared_memory)
{
	int res = TEED_SUCCESS;

	if (shared_memory == NULL) {
		res = TEED_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	shared_memory->buffer = kmalloc(shared_memory->size,
					GFP_KERNEL);

	if (shared_memory->buffer == NULL) {
		res = TEED_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

exit:
	return res;
}
EXPORT_SYMBOL(teec_allocate_shared_memory);

void teec_release_shared_memory(struct tee_sharedmemory *shared_memory)
{
	kfree(shared_memory->buffer);
}
EXPORT_SYMBOL(teec_release_shared_memory);

static const struct file_operations tee_fops = {
	.owner = THIS_MODULE,
	.read = tee_read,
	.write = tee_write,
	.open = tee_open,
	.release = tee_release,
};

static struct miscdevice tee_dev = {
	MISC_DYNAMIC_MINOR,
	TEED_NAME,
	&tee_fops
};

static int __init tee_init(void)
{
	int err = 0;

	err = misc_register(&tee_dev);

	if (err) {
		pr_err(TEED_PFX "[%s] error %d adding character device "
		       "TEE\n", __func__, err);
	}

	mutex_init(&sync);

	return err;
}

static void __exit tee_exit(void)
{
	misc_deregister(&tee_dev);
}

subsys_initcall(tee_init);
module_exit(tee_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Trusted Execution Enviroment driver");
