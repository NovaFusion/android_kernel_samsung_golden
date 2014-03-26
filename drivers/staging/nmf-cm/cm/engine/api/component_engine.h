/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Components Component Manager User Engine API.
 *
 * This file contains the Component Manager Engine API for manipulating components.
 *
 */

#ifndef COMPONENT_ENGINE_H_
#define COMPONENT_ENGINE_H_

#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/component/inc/component_type.h>
#include <cm/engine/communication/inc/communication_type.h>
#include <inc/nmf-limits.h>

/*!
 * \brief Instantiate a new component.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_InstantiateComponent(
    const char templateName[MAX_TEMPLATE_NAME_LENGTH],          //!< [in] Null terminated string (Max size=\ref MAX_TEMPLATE_NAME_LENGTH)
    t_cm_domain_id domainId,                                    //!< [in] Domain
    t_nmf_client_id clientId,                                   //!< [in] Client ID (aka PID)
    t_nmf_ee_priority priority,                                 //!< [in] Component priority
    const char localName[MAX_COMPONENT_NAME_LENGTH],            //!< [in] Null terminated string (Max size=\ref MAX_COMPONENT_NAME_LENGTH)
    const char *dataFile,                                       //!< [in] Optional reference on file where component is stored
    t_cm_instance_handle *component                             //!< [out] component
    );

/*!
 * \brief Start a component.
 *
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_StartComponent(
    t_cm_instance_handle component,
    t_nmf_client_id clientId);

/*!
 * \brief Stop a component.
 *
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_StopComponent(
    t_cm_instance_handle component,
    t_nmf_client_id clientId);

/*!
 * \brief Destroy a component.
 *
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_DestroyComponent(
    t_cm_instance_handle component,
    t_nmf_client_id clientId);

/*!
 * \brief Stop and destroy all components belonging to the given client.
 *
 * \param[in] client
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_FlushComponents(
        t_nmf_client_id client);

/*!
 * \brief Bind two components together.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_BindComponent(
    const t_cm_instance_handle      client,                                             //!<
    const char                      requiredItfClientName[MAX_INTERFACE_NAME_LENGTH],   //!< Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
    const t_cm_instance_handle      server,                                             //!<
    const char                      providedItfServerName[MAX_INTERFACE_NAME_LENGTH],   //!< Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
    t_bool                          traced,                                             //!< FALSE for synchronous binding, TRUE for traced one
    t_nmf_client_id                 clientId,                                           //!< Client ID
    const char                      *dataFileTrace                                      //!< Component file data in case on traced (Note: could be null if file already in cache)
    );

/*!
 * \brief Unbind a component.
 *
 * \param[in] client
 * \param[in] requiredItfClientName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_UnbindComponent(
    const t_cm_instance_handle      client,
    const char                      * requiredItfClientName,
    t_nmf_client_id                 clientId);

/*!
 * \brief Bind a component to void (silently ignore a call).
 *
 * \param[in] client
 * \param[in] requiredItfClientName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_BindComponentToVoid(
    const t_cm_instance_handle      client,
    const char                      requiredItfClientName[MAX_INTERFACE_NAME_LENGTH],
    t_nmf_client_id                 clientId);

/*!
 * \brief Bind two components together in an asynchronous way
 * (the components can be on the same MPC or on two different MPC)
 *
 * \param[in] client
 * \param[in] requiredItfClientName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 * \param[in] server
 * \param[in] providedItfServerName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 * \param[in] fifosize
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_BindComponentAsynchronous(
    const t_cm_instance_handle      client,
    const char                      * requiredItfClientName,
    const t_cm_instance_handle      server,
    const char                      * providedItfServerName,
    t_uint32                        fifosize,
    t_cm_mpc_memory_type            eventMemType,
    t_nmf_client_id                 clientId,
    const char                      *dataFileSkeletonOrEvent,
    const char                      *dataFileStub);

/*!
 * \brief Unbind a component previously binded asynchronously
 *
 * \param[in] client
 * \param[in] requiredItfClientName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_UnbindComponentAsynchronous(
    const t_cm_instance_handle      client,
    const char                      * requiredItfClientName,
    t_nmf_client_id                 clientId);

/*!
 * \brief Bind the Host to a component.
 *
 * \param[in] server
 * \param[in] providedItfServerName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 * \param[in] fifosize
 * \param[out] host2mpcId
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_BindComponentFromCMCore(
    const t_cm_instance_handle      server,
    const char                      * providedItfServerName,
    t_uint32                        fifosize,
    t_cm_mpc_memory_type            eventMemType,
    t_cm_bf_host2mpc_handle         *host2mpcId,
    t_nmf_client_id                 clientId,
    const char                      *dataFileSkeleton);

/*!
 * \brief Unbind a component from the Host.
 *
 * \param[in] host2mpcId
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_UnbindComponentFromCMCore(
        t_cm_bf_host2mpc_handle         host2mpcId);

/*!
 * \brief Bind a component to the Host, see \ref CM_ENGINE_BindComponentToCMCore.
 *
 * See \ref MPC2HOST "MPC->Host binding" for seeing an integration example.
 *
 * \note This method is not called from CM Proxy, its only there for wrapping purpose!!!
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_BindComponentToCMCore(
        const t_cm_instance_handle  client,
        const char                  *requiredItfClientName,
        t_uint32                    fifosize,
        t_nmf_mpc2host_handle       upLayerThis,
        const char                  *dataFileStub,
        t_cm_bf_mpc2host_handle     *mpc2hostId,
        t_nmf_client_id             clientId);

/*!
 * \brief Unbind a component to the Host, see \ref CM_ENGINE_UnbindComponentToCMCore.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_UnbindComponentToCMCore(
        const t_cm_instance_handle      client,
        const char                      *requiredItfClientName,
        t_nmf_mpc2host_handle           *upLayerThis,
        t_nmf_client_id                 clientId);

/*!
 * \brief Read a value on an attribute exported by a component instance.
 *
 * \param[in] component
 * \param[in] attrName  Null terminated string (Max size=\ref MAX_ATTRIBUTE_NAME_LENGTH).
 * \param[out] value
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_ReadComponentAttribute(
    const t_cm_instance_handle component,
    const char* attrName,
    t_uint24 *value);

/*!
 * \brief Write a value on an attribute exported by a component instance.
 *
 * \param[in] component
 * \param[in] attrName  Null terminated string (Max size=\ref MAX_ATTRIBUTE_NAME_LENGTH).
 * \param[out] value
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_WriteComponentAttribute(
    const t_cm_instance_handle component,
    const char* attrName,
    t_uint24 value);


/*!
 * \brief Get the older component.
 *
 * \param[in] client
 * \param[out] headerComponent
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentListHeader(
        const t_nmf_client_id       client,
	t_cm_instance_handle        *headerComponent);

/*!
 * \brief Get the next component.
 *
 * \param[in] client
 * \param[in] prevComponent
 * \param[out] nextComponent
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentListNext(
        const t_nmf_client_id       client,
	const t_cm_instance_handle  prevComponent,
	t_cm_instance_handle        *nextComponent);

/*!
 * \brief Get a component description
 *
 * \param[in] component
 * \param[in] templateNameLength
 * \param[in] localNameLength
 * \param[out] templateName         Null terminated string (Size=templateNameLength, Max size=\ref MAX_TEMPLATE_NAME_LENGTH).
 * \param[out] coreId
 * \param[out] localName            Null terminated string (Size=localNameLength, Max size=\ref MAX_COMPONENT_NAME_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentDescription(
        const t_cm_instance_handle  component,
        char                        *templateName,
        t_uint32                    templateNameLength,
        t_nmf_core_id               *coreId,
        char                        *localName,
        t_uint32                    localNameLength,
	t_nmf_ee_priority           *priority);

/*!
 * \brief Get number of interface required by a component.
 *
 * \param[in] component
 * \param[out] numberRequiredInterfaces
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentRequiredInterfaceNumber(
    const t_cm_instance_handle  component,
    t_uint8                     *numberRequiredInterfaces);

/*!
 * \brief Return information about required interface.
 *
 * \param[in] component
 * \param[in] index
 * \param[in] itfNameLength
 * \param[in] itfTypeLength
 * \param[out] itfName          Null terminated string (Size=itfNameLength, Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 * \param[out] itfType          Null terminated string (Size=itfTypeLength, Max size=\ref MAX_INTERFACE_TYPE_NAME_LENGTH).
 * \param[out] collectionSize
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentRequiredInterface(
        const t_cm_instance_handle  component,
        const t_uint8               index,
        char                        *itfName,
        t_uint32                    itfNameLength,
        char                        *itfType,
        t_uint32                    itfTypeLength,
        t_cm_require_state          *requireState,
        t_sint16                    *collectionSize);

/*!
 * \brief Get the component binded to a required interface.
 *
 * \param[in] component
 * \param[in] itfName               Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 * \param[in] serverItfNameLength
 * \param[out] server
 * \param[out] serverItfName        Null terminated string (Size=serverItfNameLength, Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentRequiredInterfaceBinding(
        const t_cm_instance_handle  component,
        const char                  *itfName,
        t_cm_instance_handle        *server,
        char                        *serverItfName,
        t_uint32                    serverItfNameLength);

/*!
 * \brief Get number of interface provided by a component.
 *
 * \param[in] component
 * \param[out] numberProvidedInterfaces
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentProvidedInterfaceNumber(
    const t_cm_instance_handle  component,
    t_uint8                     *numberProvidedInterfaces);

/*!
 * \brief Return information about provided interface.
 *
 * \param[in] component
 * \param[in] index
 * \param[in] itfNameLength
 * \param[in] itfTypeLength
 * \param[out] itfName          Null terminated string (Size=itfNameLength, Max size=\ref MAX_INTERFACE_NAME_LENGTH).
 * \param[out] itfType          Null terminated string (Size=itfTypeLength, Max size=\ref MAX_INTERFACE_TYPE_NAME_LENGTH).
 * \param[out] collectionSize
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentProvidedInterface(
        const t_cm_instance_handle  component,
        const t_uint8               index,
        char                        *itfName,
        t_uint32                    itfNameLength,
        char                        *itfType,
        t_uint32                    itfTypeLength,
        t_sint16                    *collectionSize);

/*!
 * \brief Get number of properties of a component.
 *
 * \param[in] component
 * \param[out] numberProperties
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentPropertyNumber(
    const t_cm_instance_handle  component,
    t_uint8                     *numberProperties);

/*!
 * \brief Return the name of a property.
 *
 * \param[in] component
 * \param[in] index
 * \param[in] propertyNameLength
 * \param[out] propertyName         Null terminated string (Size=propertyNameLength, Max size=\ref MAX_PROPERTY_NAME_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentPropertyName(
        const t_cm_instance_handle  component,
        const t_uint8               index,
        char                        *propertyName,
        t_uint32                    propertyNameLength);

/*!
 * \brief Get property value of a component.
 *
 * \param[in] component
 * \param[in] propertyName
 * \param[in] propertyValueLength
 * \param[out] propertyValue         Null terminated string (Size=propertyValueLength, Max size=\ref MAX_PROPERTY_VALUE_LENGTH).
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetComponentPropertyValue(
        const t_cm_instance_handle  component,
        const char                  *propertyName,
        char                        *propertyValue,
        t_uint32                    propertyValueLength);

#endif /*COMPONENT_ENGINE_H_*/
