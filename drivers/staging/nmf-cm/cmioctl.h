/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */

#ifndef __CMIOCTL_H
#define __CMIOCTL_H

#ifndef __KERNEL__
#define BITS_PER_BYTE 8
#endif

#include <cm/engine/component/inc/component_type.h>
#include <cm/engine/communication/inc/communication_type.h>
#include <cm/engine/configuration/inc/configuration_type.h>
#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/memory/inc/memory_type.h>
#include <cm/engine/perfmeter/inc/perfmeter_type.h>
#include <cm/engine/repository_mgt/inc/repository_type.h>

#define DEBUGFS_ROOT "nmf-cm"
#define DEBUGFS_DUMP_FLAG (1 << (sizeof(t_panic_source)*BITS_PER_BYTE - 1))

enum cmdma_type {
    CMDMA_MEM_2_PER,
    CMDMA_PER_2_MEM
};

#define CMLD_DEV_NAME     \
	{ "cm_control",   \
	  "cm_channel",	  \
	  "cm_sia_trace", \
	  "cm_sva_trace", \
	}

/*
 * The following structures are used to exchange CM_SYSCALL parameters with
 * the driver. There is one structure per ioctl command, ie per CM_SYSCAL.
 * Each of them contains:
 *  - One set of fields placed in a struture 'in' which are all input
 *    parameters of the syscall (parameters that kernel side must retrieve
 *    from user space)
 *  - One set of fields placed in a struture 'out' which contains all output
 *    parameters of the syscall plus the error code.
 *
 * NOTE: all pointers to (user) buffer are always placed in struct 'in', including
 * buffers used as output parameters; because the pointer itself is considered as
 * an input parameter, as it is directly accessed from kernel space.
 */
typedef struct{
	struct {
		const char * templateName;
		t_cm_domain_id domainId;
		t_nmf_ee_priority priority;
		const char * localName;
		const char *dataFile;
		t_uint32 dataFileSize;
	} in;
	struct {
		t_cm_instance_handle component; /** < Output parameter */
		t_cm_error error;
	} out;
} CM_InstantiateComponent_t;

typedef struct {
	struct {
		t_cm_bf_host2mpc_handle host2mpcId;
		t_event_params_handle h;
		t_uint32 size;
		t_uint32 methodIndex;
	} in;
	struct {
		t_cm_error error;
	} out;
} CM_PushEventWithSize_t;

typedef struct {
	struct {
		t_cm_instance_handle server;
		const char * providedItfServerName;
		t_uint32 fifosize;
		t_cm_mpc_memory_type eventMemType;
		const char *dataFileSkeleton;
		t_uint32 dataFileSkeletonSize;
	} in;
	struct {
		t_cm_bf_host2mpc_handle host2mpcId;  /** < Output parameter */
		t_cm_error error;
	} out;
} CM_BindComponentFromCMCore_t;

typedef struct {
	struct {
		t_cm_bf_host2mpc_handle host2mpcId;
	} in;
	struct {
		t_cm_error error;
	} out;
} CM_UnbindComponentFromCMCore_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char *requiredItfClientName;
		t_uint32 fifosize;
		t_nmf_mpc2host_handle upLayerThis;
		const char *dataFileStub;
		t_uint32 dataFileStubSize;
	} in;
	struct {
		t_cm_bf_mpc2host_handle mpc2hostId; /** < Output parameter */
		t_cm_error error;
	} out;
} CM_BindComponentToCMCore_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char *requiredItfClientName;
	} in;
	struct {
		t_nmf_mpc2host_handle upLayerThis;  /** < Output parameter */
		t_cm_error error;
	} out;
} CM_UnbindComponentToCMCore_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
	} in;
	struct {
		t_cm_error error;  /** < Output parameter */
	} out;  /** < Output parameter */
} CM_DestroyComponent_t;

typedef struct {
	struct {
		const t_cm_domain_memory  *domain;
		t_nmf_client_id            client;
	} in;
	struct {
		t_cm_domain_id handle; /** < Out parameter */
		t_cm_error error;  /** < Out parameter */
	} out; /** < Out parameter */
} CM_CreateMemoryDomain_t;

typedef struct {
	struct {
		t_cm_domain_id parentId;
		const t_cm_domain_memory *domain;
	} in;
	struct {
		t_cm_domain_id handle; /** < Out parameter */
		t_cm_error error;  /** < Out parameter */
	} out; /** < Out parameter */
} CM_CreateMemoryDomainScratch_t;

typedef struct {
	struct {
		t_cm_domain_id domainId;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out; /** < Out parameter */
} CM_DestroyMemoryDomain_t;

typedef struct {
	struct {
		t_cm_domain_id domainId; /** < In parameter */
	} in;
	struct {
		t_nmf_core_id coreId; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out; /** < Out parameter */
} CM_GetDomainCoreId_t;

typedef struct {
	struct {
		t_cm_domain_id domainId;
		t_cm_mpc_memory_type memType;
		t_cm_size size;
		t_cm_memory_alignment memAlignment;
	} in;
	struct {
		t_cm_memory_handle pHandle; /** < Output parameter */
		t_cm_error error;
	} out;  /** < Output parameter */
} CM_AllocMpcMemory_t;

typedef struct{
	struct {
		t_cm_memory_handle handle;
	} in;
	struct {
		t_cm_error error;
	} out;  /** < Output parameter */
} CM_FreeMpcMemory_t;

typedef struct {
	struct {
		t_cm_memory_handle handle;
	} in;
	struct {
		t_uint32 size; /** < Out parameter */
		t_uint32 physAddr; /** < Out parameter */
		t_uint32 kernelLogicalAddr; /** < Out parameter */
		t_uint32 userLogicalAddr; /** < Out parameter */
		t_uint32 mpcPhysAddr; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;  /** < Output parameter */
} CM_PrivGetMPCMemoryDesc_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char *requiredItfClientName;
		t_cm_instance_handle server;
		const char *providedItfServerName;
		t_uint32 fifosize;
		t_cm_mpc_memory_type eventMemType;
		const char *dataFileSkeletonOrEvent;
		t_uint32 dataFileSkeletonOrEventSize;
		const char *dataFileStub;
		t_uint32 dataFileStubSize;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_BindComponentAsynchronous_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char* requiredItfClientName;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_UnbindComponentAsynchronous_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char *requiredItfClientName;
		t_cm_instance_handle server;
		const char *providedItfServerName;
		t_bool traced;
		const char *dataFileTrace;
		t_uint32 dataFileTraceSize;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_BindComponent_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char* requiredItfClientName;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_UnbindComponent_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
		const char* requiredItfClientName;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_BindComponentToVoid_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_StartComponent_t;

typedef struct {
	struct {
		t_cm_instance_handle client;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_StopComponent_t;

typedef struct {
	struct {
		t_nmf_core_id coreId;
	} in;
	struct {
		t_cm_mpc_load_counter pMpcLoadCounter; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetMpcLoadCounter_t;

typedef struct {
	struct {
		t_nmf_core_id coreId;
		t_cm_mpc_memory_type memType;
	} in;
	struct {
		t_cm_allocator_status pStatus; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out; /** < Out parameter */
} CM_GetMpcMemoryStatus_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		t_uint32 templateNameLength;
		t_uint32 localNameLength;
		char *templateName; /** < Out parameter */
		char *localName; /** < Out parameter */
	} in;
	struct {
		t_nmf_core_id coreId; /** < Out parameter */
		t_nmf_ee_priority priority; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out; /** < Out parameter */
} CM_GetComponentDescription_t;

typedef struct {
	struct {
		t_cm_instance_handle headerComponent; /** < Output parameter */
		t_cm_error error; /** < Out parameter */
	} out; /** < Out parameter */
} CM_GetComponentListHeader_t;

typedef struct {
	struct {
		t_cm_instance_handle prevComponent;
	} in;
	struct {
		t_cm_instance_handle nextComponent; /** < Output parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentListNext_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
	} in;
	struct {
		t_uint8 numberRequiredInterfaces; /** < Output parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentRequiredInterfaceNumber_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		t_uint8 index;
		t_uint32 itfNameLength;
		t_uint32 itfTypeLength;
		char *itfName; /** < Out parameter */
		char *itfType; /** < Out parameter */
	} in;
	struct {
		t_cm_require_state requireState; /** < Out parameter */
		t_sint16 collectionSize; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentRequiredInterface_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		const char *itfName;
		t_uint32 serverItfNameLength;
		char *serverItfName; /** < Out parameter */
	} in;
	struct {
		t_cm_instance_handle server; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentRequiredInterfaceBinding_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
	} in;
	struct {
		t_uint8 numberProvidedInterfaces; /** < Output parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentProvidedInterfaceNumber_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		t_uint8 index;
		t_uint32 itfNameLength;
		t_uint32 itfTypeLength;
		char *itfName; /** < Out parameter */
		char *itfType; /** < Out parameter */
	} in;
	struct {
		t_sint16 collectionSize; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentProvidedInterface_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
	} in;
	struct {
		t_uint8 numberProperties; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentPropertyNumber_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		const char *attrName;
		t_uint8 index;
		t_uint32 propertyNameLength;
		char *propertyName; /** < Out parameter */
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentPropertyName_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		const char *propertyName;
		t_uint32 propertyValueLength;
		char *propertyValue; /** < Out parameter */
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetComponentPropertyValue_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		const char *attrName;
	} in;
	struct {
		t_uint32 value; /** < Out parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_ReadComponentAttribute_t;

typedef struct {
	struct {
		t_cm_instance_handle component;
		const char *attrName;
		t_uint32 value; /** < In parameter */
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_WriteComponentAttribute_t;

typedef struct{
	struct {
		t_cm_domain_id domainId;
	} in;
	struct {
		t_cm_instance_handle executiveEngineHandle;
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetExecutiveEngineHandle_t;

typedef struct {
	struct {
		t_cm_cmd_id aCmdID;
		t_sint32 aParam;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_SetMode_t;

typedef struct {
	struct {
		t_action_to_do action;
		t_cm_instance_handle client;
		const char *requiredItfClientName;
		t_cm_instance_handle server;
		const char *providedItfServerName;
		char **fileList;
		unsigned int listSize;
		char *type;
	} in;
	struct {
		t_uint32 methodNumber; /** < Output parameter */
		t_cm_error error; /** < Out parameter */
	} out;
} CM_GetRequiredComponentFiles_t;

typedef struct {
	struct {
		const char *name;
		const void *data;
		t_cm_size size;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_PushComponent_t;

typedef struct {
	struct {
		const char *name;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_ReleaseComponent_t;

typedef struct {
	struct {
		t_cm_domain_id srcShared;
		t_cm_domain_id src;
		t_cm_domain_id dst;
	} in;
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_Migrate_t;

typedef struct {
	struct {
		t_cm_error error; /** < Out parameter */
	} out;
} CM_Unmigrate_t;

typedef struct{
    struct {
        t_cm_memory_handle mem_handle;
        unsigned int peripheral_addr;
        unsigned int segments;
        unsigned int segmentsize;
        unsigned int LOS;
        enum cmdma_type type;
    } in;
    struct {
        t_cm_error error;
    } out;
} CM_SetupRelinkArea_t;

#define CM_PUSHEVENTWITHSIZE 			_IOWR('c', 0, CM_PushEventWithSize_t)
#define CM_GETVERSION 				_IOR('c', 1, t_uint32)
#define CM_INSTANTIATECOMPONENT 		_IOWR('c', 2, CM_InstantiateComponent_t)
#define CM_BINDCOMPONENTFROMCMCORE 		_IOWR('c', 3, CM_BindComponentFromCMCore_t)
#define CM_UNBINDCOMPONENTFROMCMCORE 		_IOWR('c', 4, CM_UnbindComponentFromCMCore_t)
#define CM_BINDCOMPONENTTOCMCORE 		_IOWR('c', 5, CM_BindComponentToCMCore_t)
#define CM_UNBINDCOMPONENTTOCMCORE 		_IOWR('c', 6, CM_UnbindComponentToCMCore_t)
#define CM_DESTROYCOMPONENT 			_IOWR('c', 7, CM_DestroyComponent_t)
#define CM_CREATEMEMORYDOMAIN 			_IOWR('c', 8, CM_CreateMemoryDomain_t)
#define CM_CREATEMEMORYDOMAINSCRATCH 		_IOWR('c', 9, CM_CreateMemoryDomainScratch_t)
#define CM_DESTROYMEMORYDOMAIN 			_IOWR('c', 10, CM_DestroyMemoryDomain_t)
#define CM_GETDOMAINCOREID 			_IOWR('c', 11, CM_GetDomainCoreId_t)
#define CM_ALLOCMPCMEMORY 			_IOWR('c', 12, CM_AllocMpcMemory_t)
#define CM_FREEMPCMEMORY 			_IOWR('c', 13, CM_FreeMpcMemory_t)
#define CM_BINDCOMPONENTASYNCHRONOUS 		_IOWR('c', 14, CM_BindComponentAsynchronous_t)
#define CM_UNBINDCOMPONENTASYNCHRONOUS 		_IOWR('c', 15, CM_UnbindComponentAsynchronous_t)
#define CM_BINDCOMPONENT 			_IOWR('c', 16, CM_BindComponent_t)
#define CM_UNBINDCOMPONENT 			_IOWR('c', 17, CM_UnbindComponent_t)
#define CM_BINDCOMPONENTTOVOID 			_IOWR('c', 18, CM_BindComponentToVoid_t)
#define CM_STARTCOMPONENT 			_IOWR('c', 19, CM_StartComponent_t)
#define CM_STOPCOMPONENT 			_IOWR('c', 20, CM_StopComponent_t)
#define CM_GETMPCLOADCOUNTER 			_IOWR('c', 21, CM_GetMpcLoadCounter_t)
#define CM_GETMPCMEMORYSTATUS 			_IOWR('c', 22, CM_GetMpcMemoryStatus_t)
#define CM_GETCOMPONENTDESCRIPTION 		_IOWR('c', 23, CM_GetComponentDescription_t)
#define CM_GETCOMPONENTLISTHEADER 		_IOWR('c', 24, CM_GetComponentListHeader_t)
#define CM_GETCOMPONENTLISTNEXT 		_IOWR('c', 25, CM_GetComponentListNext_t)
#define CM_GETCOMPONENTREQUIREDINTERFACENUMBER  _IOWR('c', 26, CM_GetComponentRequiredInterfaceNumber_t)
#define CM_GETCOMPONENTREQUIREDINTERFACE        _IOWR('c', 27, CM_GetComponentRequiredInterface_t)
#define CM_GETCOMPONENTREQUIREDINTERFACEBINDING _IOWR('c', 28, CM_GetComponentRequiredInterfaceBinding_t)
#define CM_GETCOMPONENTPROVIDEDINTERFACENUMBER 	_IOWR('c', 29, CM_GetComponentProvidedInterfaceNumber_t)
#define CM_GETCOMPONENTPROVIDEDINTERFACE 	_IOWR('c', 30, CM_GetComponentProvidedInterface_t)
#define CM_GETCOMPONENTPROPERTYNUMBER 		_IOWR('c', 31, CM_GetComponentPropertyNumber_t)
#define CM_GETCOMPONENTPROPERTYNAME 		_IOWR('c', 32, CM_GetComponentPropertyName_t)
#define CM_GETCOMPONENTPROPERTYVALUE 		_IOWR('c', 33, CM_GetComponentPropertyValue_t)
#define CM_READCOMPONENTATTRIBUTE 		_IOWR('c', 34, CM_ReadComponentAttribute_t)
#define CM_WRITECOMPONENTATTRIBUTE 		_IOWR('c', 44, CM_WriteComponentAttribute_t)
#define CM_GETEXECUTIVEENGINEHANDLE 		_IOWR('c', 35, CM_GetExecutiveEngineHandle_t)
#define CM_SETMODE 				_IOWR('c', 36, CM_SetMode_t)
#define CM_GETREQUIREDCOMPONENTFILES 		_IOWR('c', 37, CM_GetRequiredComponentFiles_t)
#define CM_PUSHCOMPONENT 			_IOWR('c', 38, CM_PushComponent_t)
#define CM_FLUSHCHANNEL 			_IO('c', 39)
#define CM_MIGRATE 				_IOWR('c', 40, CM_Migrate_t)
#define CM_UNMIGRATE 				_IOR('c', 41, CM_Unmigrate_t)
#define CM_RELEASECOMPONENT 			_IOWR('c', 42, CM_ReleaseComponent_t)
#define CM_SETUPRELINKAREA             		_IOWR('c', 43, CM_SetupRelinkArea_t)

#define CM_PRIVGETMPCMEMORYDESC			_IOWR('c', 100, CM_PrivGetMPCMemoryDesc_t)
#define CM_PRIVRESERVEMEMORY 			_IOW('c', 101, unsigned int)
#define CM_PRIV_GETBOARDVERSION 		_IOR('c', 102, unsigned int)
#define CM_PRIV_ISCOMPONENTCACHEEMPTY 		_IO('c', 103)
#define CM_PRIV_DEBUGFS_READY 			_IO('c', 104)
#define CM_PRIV_DEBUGFS_WAIT_DUMP 		_IO('c', 105)
#define CM_PRIV_DEBUGFS_DUMP_DONE 		_IO('c', 106)

enum board_version {
	U8500_V2
};
#endif
