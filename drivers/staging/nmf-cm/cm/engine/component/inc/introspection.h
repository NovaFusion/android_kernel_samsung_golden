/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Components Management internal methods - Introspection.
 *
 */
#ifndef __INC_CM_INTROSPECTION_H
#define __INC_CM_INTROSPECTION_H

#include <cm/engine/component/inc/instance.h>

/*!
 * \internal
 * \brief Description of a required interface reference
 *
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    const t_component_instance          *client;            //!< Component that provide this interface
    t_uint8                             requireIndex;       //!< Index of the interface in the require array
    t_uint8                             collectionIndex;    //!< Index in the collection if required interface is a collection
    const char*                         origName;           //!< Name of the component interface
} t_interface_require_description;

/*!
 * \internal
 * \brief Description of a provided interface
 *
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    const t_component_instance          *server;            //!< Component that provide this interface
    t_uint8                             provideIndex;       //!< Index of the interface in the provide array
    t_uint8                             collectionIndex;    //!< Index in the collection if provided interface is a collection
    const char*                         origName;           //!< Name of the component interface
} t_interface_provide_description;


/*!
 * \internal
 * \brief Get property of a component.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_getComponentProperty(
	const t_component_instance *component,
	const char                 *propName,
	char                       value[MAX_PROPERTY_VALUE_LENGTH],
    t_uint32                   valueLength);


t_dsp_address cm_getAttributeMpcAddress(
        const t_component_instance  *component,
        const char                  *attrName);

t_cm_logical_address cm_getAttributeHostAddr(
        const t_component_instance  *component,
        const char                  *attrName);

t_uint32 cm_readAttributeNoError(
        const t_component_instance  *component,
                const char                  *attrName);

t_cm_error cm_readAttribute(
        const t_component_instance  *component,
        const char                  *attrName,
        t_uint32                    *value);

t_cm_error cm_writeAttribute(
        const t_component_instance  *component,
        const char                  *attrName,
        t_uint32                    value);

/*!
 * \internal
 * \brief Get internal component symbol
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_dsp_address cm_getFunction(
        const t_component_instance* component,
        const char* interfaceName,
        const char* methodName);

/*!
 * \internal
 * \brief Get interface provided by a component instance.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_getProvidedInterface(const t_component_instance* server,
    const char* itfName,
    t_interface_provide_description *itfProvide);

/*!
 * \internal
 * \brief Get interface required by a component instance.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_getRequiredInterface(const t_component_instance* server,
    const char* itfName,
    t_interface_require_description *itfRequire);

#endif
