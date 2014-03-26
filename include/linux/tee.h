/*
 * Trusted Execution Environment (TEE) interface for TrustZone enabled ARM CPUs.
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com>
 * Author: Martin Hovang <martin.xm.hovang@stericsson.com>
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef TEE_H
#define TEE_H

/* tee_cmd id values */
#define TEED_OPEN_SESSION    0x00000000U
#define TEED_CLOSE_SESSION   0x00000001U
#define TEED_INVOKE          0x00000002U

/* tee_retval id values */
#define TEED_SUCCESS                0x00000000U
#define TEED_ERROR_GENERIC          0xFFFF0000U
#define TEED_ERROR_ACCESS_DENIED    0xFFFF0001U
#define TEED_ERROR_CANCEL           0xFFFF0002U
#define TEED_ERROR_ACCESS_CONFLICT  0xFFFF0003U
#define TEED_ERROR_EXCESS_DATA      0xFFFF0004U
#define TEED_ERROR_BAD_FORMAT       0xFFFF0005U
#define TEED_ERROR_BAD_PARAMETERS   0xFFFF0006U
#define TEED_ERROR_BAD_STATE        0xFFFF0007U
#define TEED_ERROR_ITEM_NOT_FOUND   0xFFFF0008U
#define TEED_ERROR_NOT_IMPLEMENTED  0xFFFF0009U
#define TEED_ERROR_NOT_SUPPORTED    0xFFFF000AU
#define TEED_ERROR_NO_DATA          0xFFFF000BU
#define TEED_ERROR_OUT_OF_MEMORY    0xFFFF000CU
#define TEED_ERROR_BUSY             0xFFFF000DU
#define TEED_ERROR_COMMUNICATION    0xFFFF000EU
#define TEED_ERROR_SECURITY         0xFFFF000FU
#define TEED_ERROR_SHORT_BUFFER     0xFFFF0010U

/* TEE origin codes */
#define TEED_ORIGIN_DRIVER          0x00000002U
#define TEED_ORIGIN_TEE             0x00000003U
#define TEED_ORIGIN_TEE_APPLICATION 0x00000004U

#define TEE_UUID_CLOCK_SIZE 8

#define TEEC_CONFIG_PAYLOAD_REF_COUNT 4

/*
 * Flag constants indicating which of the memory references in an open session
 * or invoke command operation payload (TEEC_Operation) that are used.
 */
#define TEEC_MEMREF_0_USED  0x00000001
#define TEEC_MEMREF_1_USED  0x00000002
#define TEEC_MEMREF_2_USED  0x00000004
#define TEEC_MEMREF_3_USED  0x00000008

/*
 * Flag constants indicating the data transfer direction of memory in
 * TEEC_SharedMemory and TEEC_MemoryReference. TEEC_MEM_INPUT signifies data
 * transfer direction from the client application to the TEE. TEEC_MEM_OUTPUT
 * signifies data transfer direction from the TEE to the client application.
 */
#define TEEC_MEM_INPUT  0x00000001
#define TEEC_MEM_OUTPUT 0x00000002

/*
 * Session login methods, for use in TEEC_OpenSession() as parameter
 * connectionMethod. Type is t_uint32.
 *
 * TEEC_LOGIN_PUBLIC  No login data is provided.
 */
#define TEEC_LOGIN_PUBLIC 0x0

/*
 * Exposed functions (command_id) in the static TA
 */
#define TEE_STA_GET_PRODUCT_CONFIG		10
#define TEE_STA_SET_L2CC_PREFETCH_CTRL_REGISTER 11
#define TEE_STA_OPEN_SHARED_MEMORY 39

/* Flags indicating run-time environment */
#define TEE_RT_FLAGS_NORMAL		0x00000000
#define TEE_RT_FLAGS_MASK_ITP_PROD      0x00000001
#define TEE_RT_FLAGS_MODEM_DEBUG	0x00000002
#define TEE_RT_FLAGS_RNG_REG_PUBLIC     0x00000004
#define TEE_RT_FLAGS_JTAG_ENABLED       0x00000008

/*
 * Product id numbers
 */
#define TEE_PRODUCT_ID_UNKNOWN  0
#define TEE_PRODUCT_ID_8400     1 /* Obsolete */
#define TEE_PRODUCT_ID_8500B    2 /* 1080p/1GHz/400MHz */
#define TEE_PRODUCT_ID_9500     3 /* 1080p/1GHz/400MHz */
#define TEE_PRODUCT_ID_5500     4 /* Obsolete */
#define TEE_PRODUCT_ID_7400     5
#define TEE_PRODUCT_ID_8500C    6 /* 720p/1GHz/400MHz */
#define TEE_PRODUCT_ID_8500A    7 /* 720p/800MHz/320MHz */
#define TEE_PRODUCT_ID_8500E    8 /* 1080p/1.15GHz/533MHz */
#define TEE_PRODUCT_ID_8520F    9 /* 720p/1.15GHz/533MHz */
#define TEE_PRODUCT_ID_8520H   10 /* 720p/1GHz/200MHz */
#define TEE_PRODUCT_ID_9540    11
#define TEE_PRODUCT_ID_9500C   12 /* 1080p/1.15GHz/533MHz */

/* Flags indicating fuses */
#define TEE_FUSE_FLAGS_MODEM_DISABLE    0x00000001

/**
 * struct tee_product_config - System configuration structure.
 *
 * @product_id: Product identification.
 * @rt_flags: Runtime configuration flags.
 * @fuse_flags: Fuse flags.
 *
 */
struct tee_product_config {
	uint32_t product_id;
	uint32_t rt_flags;
	uint32_t fuse_flags;
};

/**
 * struct tee_uuid - Structure that represent an uuid.
 * @timeLow: The low field of the time stamp.
 * @timeMid: The middle field of the time stamp.
 * @timeHiAndVersion: The high field of the timestamp multiplexed
 *                    with the version number.
 * @clockSeqAndNode: The clock sequence and the node.
 *
 * This structure have different naming (camel case) to comply with Global
 * Platforms TEE Client API spec. This type is defined in RFC4122.
 */
struct tee_uuid {
	uint32_t timeLow;
	uint16_t timeMid;
	uint16_t timeHiAndVersion;
	uint8_t clockSeqAndNode[TEE_UUID_CLOCK_SIZE];
};

/**
 * struct tee_sharedmemory - Shared memory block for TEE.
 * @buffer: The in/out data to TEE.
 * @size: The size of the data.
 * @flags: Variable telling whether it is a in, out or in/out parameter.
 */
struct tee_sharedmemory {
	void *buffer;
	size_t size;
	uint32_t flags;
};

/**
 * struct tee_operation - Payload for sessions or invoke operation.
 * @shm: Array containing the shared memory buffers.
 * @flags: Tells which if memory buffers that are in use.
 */
struct tee_operation {
	struct tee_sharedmemory shm[TEEC_CONFIG_PAYLOAD_REF_COUNT];
	uint32_t flags;
};

struct tee_context {};

/**
 * struct tee_session - The session of an open tee device.
 * @state: The current state in the linux kernel.
 * @err: Error code (as in Global Platform TEE Client API spec)
 * @origin: Origin for the error code (also from spec).
 * @id: Implementation defined type, 0 if not used.
 * @vaddr: Virtual address for the memrefs.
 * @ta: The trusted application.
 * @uuid: The uuid for the trusted application.
 * @cmd: The command to be executed in the trusted application.
 * @driver_cmd: The command type in the driver. This is used from a client (user
 *              space to tell the Linux kernel whether it's a open-,
 *              close-session or if it is an invoke command.
 * @ta_size: The size of the trusted application.
 * @op: The payload for the trusted application.
 * @sync: Mutex to handle multiple use of clients.
 *
 * This structure is mainly used in the Linux kernel as a session context for
 * ongoing operations. Other than that it is also used in the communication with
 * the user space.
 */
struct tee_session {
	uint32_t state;
	uint32_t err;
	uint32_t origin;
	uint32_t id;
	uint32_t *vaddr[TEEC_CONFIG_PAYLOAD_REF_COUNT];
	void *ta;
	struct tee_uuid *uuid;
	unsigned int cmd;
	unsigned int driver_cmd;
	unsigned int ta_size;
	struct tee_operation *op;
	struct mutex *sync;
};

/**
 * struct tee_read - Contains the error message and the origin.
 * @err: Error code (as in Global Platform TEE Client API spec)
 * @origin: Origin for the error code (also from spec).
 *
 * This is used by user space when a user space application wants to get more
 * information about an error.
 */
struct tee_read {
	unsigned int err; /* return value */
	unsigned int origin; /* error origin */
};

/**
 * struct ta_addr - Struct that acts as a helper struct when it comes to
 * allocating physically contigous memory.
 *
 * @paddr: Represents the physical address of a buffer.
 * @vaddr: Represents the virtual address of a buffer.
 * @alloc: A pointer to the hwmem allocation structure.
 */
struct ta_addr {
	void *paddr;
	void *vaddr;
	struct hwmem_alloc *alloc;
};

/**
 * Function that handles the function calls to trusted applications.
 * @param ts: The session of a operation to be executed.
 * @param sec_cmd: The type of command to be executed, open-, close-session,
 *                 invoke command.
 */
int call_sec_world(struct tee_session *ts, int sec_cmd);


/**
 * teec_initialize_context() - Initializes a context holding connection
 * information on the specific TEE.
 * @param name: A zero-terminated string identifying the TEE to connect to.
 *              If name is set to NULL, the default TEE is connected to.
 *              NULL is the only supported value in this version of the
 *              API implementation.
 * @param context: The context structure which is to be initialized.
 *
 * Initializes a context holding connection information between the calling
 * client application and the TEE designated by the name string.
 */
int teec_initialize_context(const char *name, struct tee_context *context);

/**
 * teec_finalize_context() - Destroys a context holding connection information
 * on the specific TEE.
 * @param context: The context to be destroyed.
 *
 * This function destroys an initialized TEE context, closing the connection
 * between the client application and the TEE. This function must only be
 * called when all sessions related to this TEE context have been closed and
 * all shared memory blocks have been released.
 */
int teec_finalize_context(struct tee_context *context);

/**
 * teec_open_session() - Opens a new session with the specified trusted
 * application.
 * @param context: The initialized TEE context structure in which scope to
 *                 open the session.
 * @param session: The session to initialize.
 * @param destination: A structure identifying the trusted application with
 *                     which to open a session. If this is set to NULL the
 *                     operation TEEC_MEMREF_0 is expected to contain the blob
 *                     which holds the Trusted Application.
 * @param connection_method: The connection method to use.
 * @param connection_data: Any data necessary to connect with the chosen
 *                         connection method. Not supported should be set to
 *                         NULL.
 * @param operation: An operation structure to use in the session. May be
 *                   set to NULL to signify no operation structure needed.
 *                   If destination is set to NULL, TEEC_MEMREF_0 is
 *                   expected to hold the TA binary as described above.
 * @param error_origin: A parameter which will hold the error origin if this
 *                      function returns any value other than TEEC_SUCCESS.
 *
 * Opens a new session with the specified trusted application. Only
 * connectionMethod == TEEC_LOGIN_PUBLIC is supported. connectionData and
 * operation shall be set to NULL.
 */
int teec_open_session(struct tee_context *context, struct tee_session *session,
		      const struct tee_uuid *destination,
		      unsigned int connection_method,
		      void *connection_data, struct tee_operation *operation,
		      unsigned int *error_origin);

/**
 * teec_close_session() - Closes the session which has been opened with the
 * specific trusted application.
 * @param session: The opened session to close.
 *
 * Closes the session which has been opened with the specific trusted
 * application.
 */
int teec_close_session(struct tee_session *session);

/**
 * teec_invoke_command() - Executes a command in the specified trusted
 * application.
 * @param destination: A structure identifying the trusted application.
 * @param command_id: Identifier of the command in the trusted application to
 *                    invoke.
 * @param operation: An operation structure to use in the invoke command. May
 *                   be set to NULL to signify no operation structure needed.
 * @param error_origin: A parameter which will hold the error origin if this
 *                      function returns any value other than TEEC_SUCCESS.
 *
 * Executes a command in the specified trusted application.
 */
int teec_invoke_command(struct tee_session *session, unsigned int command_id,
			struct tee_operation *operation,
			unsigned int *error_origin);

/**
 * teec_allocate_shared_memory() - Allocate shared memory for TEE.
 * @param context: The initialized TEE context structure in which scope to
 *                 open the session.
 * @param shared_memory: Pointer to the allocated shared memory.
 */
int teec_allocate_shared_memory(struct tee_context *context,
				struct tee_sharedmemory *shared_memory);

/**
 * teec_release_shared_memory() - Free the shared memory.
 * @param shared_memory: Pointer to the shared memory to be freed.
 */
void teec_release_shared_memory(struct tee_sharedmemory *shared_memory);

#endif
