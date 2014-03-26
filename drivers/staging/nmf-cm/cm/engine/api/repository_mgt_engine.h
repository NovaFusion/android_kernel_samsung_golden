/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Repository Component Manager User Engine API.
 *
 * This file contains the Component Manager Engine API for manipulating the components files.
 */

#ifndef REPOSITORY_MGT_ENGINE_H_
#define REPOSITORY_MGT_ENGINE_H_

#include <inc/nmf-limits.h>
#include <cm/engine/repository_mgt/inc/repository_type.h>

/*!
 * \brief Get the name(s) of the component(s) to load.
 *
 * \param[in] client                Handle of the client component (optional)
 * \param[in] requiredItfClientName Null terminated string (Max size=\ref MAX_INTERFACE_NAME_LENGTH)  (optional).
 * \param[in] server                Handle of the server component  (optional)
 * \param[in] providedItfServerName Null terminated string (Max size==\ref MAX_INTERFACE_NAME_LENGTH) (optional).
 * \param[out] fileList             List of required component(s).
 * \param[in,out] listSize          Initial size of the list as input. Updated with the number of entries really used.
 * \param[out] type                 Interface type of the client required or server provided interface. Null terminated string (Max size=\ref MAX_INTERFACE_TYPE_NAME_LENGTH) (optional) .
 * \param[out] methodNumber         Number of method in the interface type of the client required interface. (only used when called from CM_BindComponentToUser) (optional)
 *
 * \note It returns the component(s) name(s) to load, depending on the first four parameters.
 *
 *  - If all 4 are NULL, it returns the name of the Executive Engine components to load
 *  - If 'client' is NULL, it returns the name of the required components for a Bind From CMCore.
 *  - If 'server' is NULL, it returns the name of the required components for a Bind To CMCore.
 *  - If none is NULL, it returns the name of the required components for an asynchronous binding
 *
 * The names are returned in fileList, whose initial size is specified in listSize.
 * (sizeList must be the number of provided entries of \ref MAX_INTERFACE_TYPE_NAME_LENGTH length
 * If not enough space is provided, CM_NO_MORE_MEMORY is returned
 *
 * sizeList is updated with the number entries really filled.
 *
 * This method is also used to retrieve the interface type when called from CM_BindComponentToUser and CM_BindComponentFromUser
 * and the number of methods when called from CM_BindComponentToUser.
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetRequiredComponentFiles(
        // IN
        t_action_to_do action,
        const t_cm_instance_handle client,
        const char *requiredItfClientName,
        const t_cm_instance_handle server,
        const char *providedItfServerName,
        // OUT component to be pushed
        char fileList[][MAX_INTERFACE_TYPE_NAME_LENGTH],
        // IN max component allowed to be pushed
        t_uint32 listSize,
        // OUT interface information
        char type[MAX_INTERFACE_TYPE_NAME_LENGTH],
        t_uint32 *methodNumber);

/*!
 * \brief Push a component into the CM Component Cache.
 *
 * \param[in] name Component name, null terminated string (Max size=\ref MAX_INTERFACE_TYPE_NAME_LENGTH)
 * \param[in] data Pointer to _user_ data of the component.
 * \param[in] size Size of the data.
 *
 * \note Push a component in the Component Cache
 *       The 'data' must be provided such a way that they can be freed by a call to OSAL_Free()
 *       The caller doesn't need and must NOT free the data, even in case of failure.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_PushComponent(const char *name, const void *data, t_cm_size size);

/*!
 * \brief Remove a component from the CM Component Cache.
 *
 * \param[in] name Component name, null terminated string (Max size=\ref MAX_INTERFACE_TYPE_NAME_LENGTH)
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_ReleaseComponent (const char *name);

/*!
 * \brief Check if the CM Component Cache is empty.
 *
 * \return a boolean value TRUE or FALSE.
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_bool CM_ENGINE_IsComponentCacheEmpty(void);
#endif /*REPOSITORY_MGT_ENGINE_H_*/
