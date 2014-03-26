/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Component repository internal methods.
 *
 * \defgroup REPOSITORY_INTERNAL Component repository.
 */
#ifndef __INC_CM_REP_MGT_H
#define __INC_CM_REP_MGT_H

#include <cm/inc/cm_type.h>
#include <inc/nmf-limits.h>

/*!
 * \brief Identification of a component entry.
 * \ingroup REPOSITORY_INTERNAL
 */
typedef struct t_rep_component {
    t_dup_char             name;
	struct t_rep_component *prev;
	struct t_rep_component *next;
	t_elfdescription *elfhandle;                      //!< Must be last as data will be stored here
} t_rep_component;

/*!
 * \brief Search a component entry by name.
 *
 * \param[in]  name The name of the component to look for.
 * \param[out] component The corresponding component entry in the repository
 *
 * \retval t_cm_error
 *
 * \ingroup REPOSITORY_INTERNAL
 */
PUBLIC t_cm_error cm_REP_lookupComponent(const char *name, t_rep_component **component);

/*!
 * \brief Helper method that return the dataFile found in parameter or in the cache
 */
t_elfdescription* cm_REP_getComponentFile(t_dup_char templateName, t_elfdescription* elfhandle);

/*!
 * \brief Destroy the full repository (remove and free all components)
 *
 * \retval none
 *
 * \ingroup REPOSITORY_INTERNAL
 */
PUBLIC void cm_REP_Destroy(void);

#endif /* __INC_CM_REP_MGT_H */
