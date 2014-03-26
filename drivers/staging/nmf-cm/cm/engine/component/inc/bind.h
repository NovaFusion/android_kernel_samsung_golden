/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 * \brief Binding Factories internal API.
 *
 * \defgroup BF_COMMON Binding factories: Common API
 * \defgroup BF_PRIMITIVE Binding Factories: Primitive API
 * \defgroup BF_TRACE Binding Factories: Trace API
 * \defgroup BF_ASYNCHRONOUS Binding Factories: Asynchronous API
 * \defgroup BF_DISTRIBUTED Binding Factories: Distributed API
 */
#ifndef __INC_CM_BIND_H
#define __INC_CM_BIND_H

#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/communication/inc/communication.h>
#include <cm/engine/utils/inc/table.h>

/**
 * \internal
 * \ingroup BF_COMMON
 *
 * \brief Identification number of prefedined Binding Factories
 */
typedef enum {
    BF_SYNCHRONOUS,     //!< Intra-DSP Synchronous Binding Factory Identifier
    BF_TRACE,           //!< Intra-DSP trace synchronous Binding Factory Identifier
    BF_ASYNCHRONOUS, 	//!< Intra-DSP Asynchronous Binding Factory Identifier
    BF_DSP2HOST,        //!< DSP to Host Binding Factory Identifier
    BF_HOST2DSP,        //!< Host to DSP Binding Factory Identifier
    BF_DSP2DSP,         //!< DSP to DSP Binding Factory Identifier
} t_bf_info_ID;

/*!
 * \internal
 * \brief Description of a provided interface
 *
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct _t_interface_reference {
	const t_component_instance          *instance;      //!< Component instance that provide this interface
    t_uint8                             provideIndex;   //!< Index of the interface in the provide array
    t_uint8                             collectionIndex;//!< Index in the collection if provided interface is a collection
    t_bf_info_ID                        bfInfoID;       //!< Identification of BF used for creating binding
    void*                               bfInfo;         //!< Storage of the binding factory info
} t_interface_reference;

/**
 * \internal
 * \ingroup BF_COMMON
 *
 * Make some basic sanity check for a client:
 *  - component stopped
 *  - Interface really required
 *
 * \param[in] client The client component instance handle.
 * \param[in] requiredItfClientName The client required interface name
 * \param[out] requiredItf return the required interface (avoid user searching)
 */
t_cm_error cm_checkValidClient(
        const t_component_instance* client,
        const char* requiredItfClientName,
        t_interface_require_description *itfRequire,
        t_bool *bindable);
/**
 * \internal
 * \ingroup BF_COMMON
 *
 * Make some basic sanity check for a server:
 *  - Interface really provided
 *
 * \param[in] server The server component instance handle.
 * \param[in] providedItfServerName The server provided interface name
 * \param[out] itf return the provided interface (avoid user searching)
 */
t_cm_error cm_checkValidServer(
        const t_component_instance* server,
        const char* providedItfServerName,
        t_interface_provide_description *itfProvide);

/**
 * \internal
 * \ingroup BF_COMMON
 *
 * Make some basic sanity check for a binding:
 *  - Sanity check for a server
 *  - Sanity check for a client (and potentially wait initialisation)
 *  - Provided and required interface matches
 *
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 * \param[in] server The server component instance handle
 * \param[in] providedItfServerName The server provided interface name
 * \param[out] requiredItf return the required interface (avoid user searching)
 * \param[out] itf return the provided interface (avoid user searching)
 */
t_cm_error cm_checkValidBinding(
        const t_component_instance* client,
        const char* requiredItfClientName,
        const t_component_instance* server,
        const char* requiredItfServerName,
        t_interface_require_description *itfRequire,
        t_interface_provide_description *itfProvide,
        t_bool *bindable);

/**
 * \internal
 * \ingroup BF_COMMON
 *
 * Make some basic sanity check for each unbinding:
 *  - Interface really required
 *  - Component stopped
 *
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 * \param[out] itfRequire return the previously binded required interface (avoid user searching)
 * \param[out] itfProvide return the previously binded provided interface (avoid user searching)
 * \param[out] bfInfoID return the binding factory identifiant which done the previously bind
 * \param[out] bfInfo return the binding factory information which done the previously bind
 */
t_cm_error cm_checkValidUnbinding(
        const t_component_instance* client,
        const char* requiredItfClientName,
        t_interface_require_description *itfRequire,
        t_interface_provide_description *itfProvide);

/**
 * \internal
 * \ingroup BF_PRIMITIVE
 *
 * Create a primitive binding between a client to a server interface.
 *
 * \param[in] itfRequire The client required interface description
 * \param[in] itfProvide The server provided interface description
 */
t_cm_error cm_bindInterface(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide);

/**
 * \internal
 * \ingroup BF_PRIMITIVE
 *
 * Unbind a previously binded client.
 *
 * \param[in] itfRequire The client required interafce description
 */
void cm_unbindInterface(
        const t_interface_require_description *itfRequire);

/**
 * \internal
 * \ingroup BF_PRIMITIVE
 *
 * Get a server interface previouly binded to a client
 *
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 * \param[out] itf The server interface
 */
t_cm_error cm_lookupInterface(
        const t_interface_require_description *itfRequire,
        t_interface_provide_description *itfProvide);

/**
 * \internal
 * \ingroup BF_PRIMITIVE
 *
 * Create a void binding.
 *
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 */
t_cm_error cm_bindInterfaceToVoid(
        const t_interface_require_description *itfRequire);

/**
 * \internal
 * \ingroup BF_TRACE
 *
 * Trace synchronous binding factory Information
 */
typedef struct {
    t_component_instance    *traceInstance;   //!< Trace binding component instance
} t_trace_bf_info;

/**
 * \internal
 * \ingroup BF_TRACE
 *
 * Create a traced binding between a client to a server interface.
 *
 * \param[in] itfRequire The client required interface description
 * \param[in] itfProvide The server provided interface description
 */
t_cm_error cm_bindInterfaceTrace(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide,
        t_elfdescription *elfhandleTrace);

/**
 * \internal
 * \ingroup BF_TRACE
 *
 * Unbind a previously binded client.
 *
 * \param[in] itfRequire The client required interafce description
 */
void cm_unbindInterfaceTrace(
        const t_interface_require_description *itfRequire,
        t_trace_bf_info                       *bfInfo);


/**
 * \internal
 * \ingroup BF_ASYNCHRONOUS
 *
 * Asynchronous binding factory Information
 */
typedef struct {
    t_component_instance 	*eventInstance;   //!< Event binding component instance
    t_memory_handle 		dspfifoHandle;    //!< Memory handle of allocated event fifo (pass to the event binding component)
} t_async_bf_info;

/**
 * \internal
 * \ingroup BF_ASYNCHRONOUS
 *
 * Create a asynchronous binding between a client to a server interface.
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 * \param[in] itf The server interface
 * \param[in] fifosize Number of waited event in the fifo
 */
t_cm_error cm_bindInterfaceAsynchronous(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType,
        t_elfdescription *elfhandleEvent);
/**
 * \internal
 * \ingroup BF_ASYNCHRONOUS
 *
 * Destroy a asynchronous binding between a client to a server interface.
 * \param[in] itfRequire the required interface
 */
void cm_unbindInterfaceAsynchronous(
        const t_interface_require_description   *itfRequire,
        t_async_bf_info                         *bfInfo);

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Stub information in distributed binding factory (client side)
 */
typedef struct {
    t_component_instance    *stubInstance;      //!< Stub
} t_dspstub_bf_info;

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Skeleton information in distributed binding factory (server side)
 */
typedef struct {
    t_component_instance    *skelInstance;      //!< Skeleton binding component instance
    t_memory_handle         dspfifoHandle;      //!< Memory handle of allocated event fifo (pass to the event binding component)
} t_dspskel_bf_info;

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Host to DSP distributed binding factory Information
 */
typedef struct {
    t_dspskel_bf_info       dspskeleton;            //!< Information about the DSP skeleton (server side)
    t_nmf_fifo_arm_desc*    fifo;                   //!< Handle of the fifo params
    t_nmf_client_id         clientId;               //!< Client ID of the host client
} t_host2mpc_bf_info;

/*
 * Table of instantiated of host2mpc bindings
 */
extern t_nmf_table Host2MpcBindingTable; /**< list (table) of host2mpc bindings */

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Create a Host to DSP distributed binding between a host client interface to a server interface.
 * (Not manage in the same way as distributed binding since the Host programming model is not component aware).
 * \param[in] itfServer The server interface
 * \param[in] fifosize Number of waited event in the fifo
 * \param[in] dspEventMemType The type of memory to use
 * \param[in] bfInfo info structure
 */
t_cm_error cm_bindComponentFromCMCore(
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType,
        t_elfdescription *elfhandleSkeleton,
        t_host2mpc_bf_info **bfInfo);

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Destroy a Host to DSP distributed binding between a host client interface to a server interface.
 * \param[in] bfInfo The Host to DSP distributed binding factory information
 */
void cm_unbindComponentFromCMCore(
        t_host2mpc_bf_info *bfInfo);

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * DSP to Host distributed binding factory Information
 */
typedef struct {
    t_dspstub_bf_info           dspstub;            //!< Information about the DSP stub (client side)
    t_nmf_fifo_arm_desc*        fifo;               //!< Handle of the fifo params
    t_uint32                    context;
} t_mpc2host_bf_info;

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Create a DSP to Host distributed binding between a client interface to a host server interface.
 * (Not manage in the same way as distributed binding since the Host programming model is not component aware).
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 * \param[in] itfref The host server interface to be called
 * \param[in] fifosize Number of waited event in the fifo
 */
t_cm_error cm_bindComponentToCMCore(
        const t_interface_require_description   *itfRequire,
        t_uint32                                fifosize,
        t_uint32                                context,
        t_elfdescription                        *elfhandleStub,
        t_mpc2host_bf_info                      ** bfInfo);

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Destroy a DSP to Host distributed binding between a client interface to a server interface.
 * \param[in] itfRequire The required interface
 * \param[out] upLayerThis The 'THIS' context of upper layer
 */
void cm_unbindComponentToCMCore(
        const t_interface_require_description   *itfRequire,
        t_mpc2host_bf_info                      *bfInfo);

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Asynchronous distributed binding factory Information
 */
typedef struct {
    t_nmf_fifo_arm_desc*    fifo;                   //!< Handle of the fifo params
    t_dspstub_bf_info       dspstub;                //!< Information about the DSP stub (client side)
    t_dspskel_bf_info       dspskeleton;            //!< Information about the DSP skeleton (server side)
} t_mpc2mpc_bf_info;

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Create a asynchronous distributed binding between a client interface to a server interface.
 * \param[in] client The client component instance handle
 * \param[in] requiredItfClientName The client required interface name
 * \param[in] itf The server interface
 * \param[in] fifosize Number of waited event in the fifo
 */
t_cm_error cm_bindInterfaceDistributed(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType,
        t_elfdescription                        *elfhandleSkeleton,
        t_elfdescription                        *elfhandleStub);

/**
 * \internal
 * \ingroup BF_DISTRIBUTED
 *
 * Destroy a asynchronous distributed binding between a client interface to a server interface.
 * \param[in] itfRequire The required interface
 */
void cm_unbindInterfaceDistributed(
        const t_interface_require_description   *itfRequire,
        t_mpc2mpc_bf_info                       *bfInfo);

/**
 * \internal
 *
 * Bind a static interrupt to server provide interface name.
 * \param[in] coreId The core to which component is loaded
 * \param[in] interruptLine Interrupt line number to use
 * \param[in] server Server instance that provide interrupt service
 * \param[in] providedItfServerName Interface name hat provide interrupt service
 */
t_cm_error cm_bindInterfaceStaticInterrupt(
        const t_nmf_core_id coreId,
        const int interruptLine,
        const t_component_instance *server,
        const char* providedItfServerName);

/**
 * \internal
 *
 * Unbind a static interrupt.
 * \param[in] coreId The core to which component is loaded
 * \param[in] interruptLine Interrupt line number to use
 */
t_cm_error cm_unbindInterfaceStaticInterrupt(
        const t_nmf_core_id coreId,
        const int interruptLine);

void cm_destroyRequireInterface(t_component_instance* component, t_nmf_client_id clientId);
void cm_registerSingletonBinding(
        t_component_instance*                   component,
        t_interface_require_description*        itfRequire,
        t_interface_provide_description*        itfProvide,
        t_nmf_client_id                         clientId);
t_bool cm_unregisterSingletonBinding(
        t_component_instance*                   component,
        t_interface_require_description*        itfRequire,
        t_interface_provide_description*        itfProvide,
        t_nmf_client_id                         clientId);

#endif
