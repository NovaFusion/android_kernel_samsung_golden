/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Marten Olsson <marten.xm.olsson@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/kernel.h>
#include <linux/tee.h>

#define STATIC_TEE_TA_START_LOW	 0xBC765EDE
#define STATIC_TEE_TA_START_MID	 0x6724
#define STATIC_TEE_TA_START_HIGH	0x11DF
#define STATIC_TEE_TA_START_CLOCKSEQ  \
	{0x8E, 0x12, 0xEC, 0xDB, 0xDF, 0xD7, 0x20, 0x85}

int open_modem_shared_memory(void)
{
	int err = TEED_ERROR_GENERIC;
	int origin_err;
	struct tee_operation operation = { { { 0 } } };
	struct tee_context context;
	struct tee_session session;
	u32 status;
	/* Selects trustzone application needed for the job. */
	struct tee_uuid static_uuid = {
		STATIC_TEE_TA_START_LOW,
		STATIC_TEE_TA_START_MID,
		STATIC_TEE_TA_START_HIGH,
		STATIC_TEE_TA_START_CLOCKSEQ,
	};

	status = false;

	err = teec_initialize_context(NULL, &context);
	if (err) {
		pr_warning("open_modem_shared_memory: unable to initialize "
			" tee context, err = %d\n", err);
		goto error0;
	}

	err = teec_open_session(&context, &session, &static_uuid,
			TEEC_LOGIN_PUBLIC, NULL, NULL, &origin_err);
	if (err) {
		pr_warning("open_modem_shared_memory: unable to open tee "
			"session, tee error = %d, origin error = %d\n",
			err, origin_err);
		goto error1;
	}

	operation.shm[0].buffer = &status;
	operation.shm[0].size = sizeof(&status);
	operation.shm[0].flags = TEEC_MEM_OUTPUT;
	operation.flags = TEEC_MEMREF_0_USED;

	err = teec_invoke_command(&session, TEE_STA_OPEN_SHARED_MEMORY,
			&operation, &origin_err);
	if (err) {
		pr_warning("open_modem_shared_memory: open shared memory "
			"failed, err=%d", err);
		goto error2;
	}

error2:
	(void) teec_close_session(&session);
error1:
	(void) teec_finalize_context(&context);
error0:
	if (status == false)
		err = -1;
	return err;
}

