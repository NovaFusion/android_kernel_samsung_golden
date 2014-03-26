/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 
/*!
 * \brief Component Manager API.
 *
 * This file contains the Component Manager API for manipulating components.
 *
 */

#ifndef __INC_CM_DEF_H
#define __INC_CM_DEF_H

#include <cm/inc/cm_type.h>
#include <inc/nmf-def.h>

/*!
 * \brief Get the version of the NMF CM engine at runtime
 *
 * This method should be used to query the version number  of  the
 * NMF Component Manager engine at runtime. This is useful when using
 * to check if version of the engine linked with application correspond
 * to engine used for development. 
 *
 * Such code can be used to check compatibility: \code
    t_uint32 nmfversion;

    // Print NMF version
    CM_GetVersion(&nmfversion);
    LOG("NMF Version %d-%d-%d\n",
            VERSION_MAJOR(nmfversion),
            VERSION_MINOR(nmfversion),
            VERSION_PATCH(nmfversion));
    if(NMF_VERSION != nmfversion) {
        LOG("Error: Incompatible API version %d != %d\n", NMF_VERSION, nmfversion);
        EXIT();
    }
 * \endcode
 *
 * \param[out] version Internal hardcoded version (use \ref VERSION_MAJOR, \ref  VERSION_MINOR, \ref VERSION_PATCH  macros to decode it).
 *
 * \ingroup CM
 */
PUBLIC IMPORT_SHARED void CM_GetVersion(t_uint32 *version);

#endif /* __INC_CM_H */
