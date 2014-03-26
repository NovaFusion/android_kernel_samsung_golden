/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Service type and data used through service callback.
 * \defgroup NMF_SERVICE NMF Service Callback types and data definition
 * \ingroup NMF_COMMON
 */
#ifndef SERVICE_TYPE_H
#define SERVICE_TYPE_H

#include <ee/api/panic.idt>
#include <nmf/inc/component_type.h>
#include <share/inc/nmf.h>

/*!
 * \brief Define t_nmf_service_type type
 *
 * It gives the type of service message passed to service callback.
 * \ingroup NMF_SERVICE
 */
typedef t_uint32 t_nmf_service_type;
#define NMF_SERVICE_PANIC                          ((t_nmf_service_type)0) //!< \ingroup NMF_SERVICE
#define NMF_SERVICE_SHUTDOWN                       ((t_nmf_service_type)1) //!< \ingroup NMF_SERVICE

/*
 * The following structured define each data structure used for each service type
 * and given to each serviceCallback
 */

/*!
 * \brief Define t_nmf_panic_data type
 *
 * This is the data structure passed to the service callback (inside \ref t_nmf_service_data)
 * when t_nmf_service_type == NMF_SERVICE_PANIC
 * \ingroup NMF_SERVICE
 */
typedef struct {
	t_panic_reason       panicReason; //!< The reason of the panic
	t_panic_source       panicSource; //!< THe source of the panic (One of the MPC or the ARM-EE)
	/*!
	 * union of structures containing specific info, depending on the panicSource
	 */
	union {
		struct {
			t_nmf_core_id        coreid; //!< The coreId of the MPC on which the panic occured
			t_cm_instance_handle faultingComponent; //!< The faulting component handle
			t_uint32             panicInfo1; //!< First info (depend on \ref panicReason)
			t_uint32             panicInfo2; //!< Second info (depend on \ref panicReason)
		} mpc; //!< member to use if panicSource == MPC_EE
		struct {
			void *               faultingComponent; //!< The faulting component handle
			t_uint32             panicInfo1; //!< First info (depend on \ref panicReason)
			t_uint32             panicInfo2; //!< Second info (depend on \ref panicReason)
		} host; //!< member to use if panicSource == HOST_EE
	} info; //!< union of structures containing specific info, depending on the panicSource
} t_nmf_panic_data;

/*!
 * \brief Define t_nmf_shutdown_data type
 *
 * This is the data structure passed to the service callback (inside \ref t_nmf_service_data)
 * when t_nmf_service_type == NMF_SERVICE_SHUTDOWN
 * \ingroup NMF_SERVICE
 */
typedef struct {
    t_nmf_core_id        coreid; //!< The coreId of the MPC on which has been shutdown
} t_nmf_shutdown_data;

/*!
 * \brief Define t_nmf_service_data type
 *
 * It gives the data passed to the service callbacks for each service type
 * This is an union whose member to use is defined by the given \ref t_nmf_service_type
 *
 * \ingroup NMF_SERVICE
 */
typedef union {
	t_nmf_panic_data panic; //!< if service_type == NMF_SERVICE_PANIC
    t_nmf_shutdown_data shutdown; //!< if service_type == NMF_SERVICE_SHUTDOWN
} t_nmf_service_data;

/*!
 * \brief Define t_nmf_serviceCallback function type to allow to dispatch service message to user.
 * \ingroup NMF_SERVICE
 */
typedef void (*t_nmf_serviceCallback)(void *contextHandler, t_nmf_service_type serviceType, t_nmf_service_data *serviceData);

#endif //SERVICE_TYPE_H
