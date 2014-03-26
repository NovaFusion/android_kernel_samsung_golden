/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 /*!
 * \brief NMF Version.
 *
 * This file contains the NMF Version.
 *
 * \defgroup NMF_VERSION NMF Version
 * \ingroup COMMON
 */

#ifndef __INC_NMF_DEF_H
#define __INC_NMF_DEF_H

/*!
 * \brief Current NMF version number
 *
 * \ingroup NMF_VERSION
 */
#define NMF_VERSION ((2 << 16) | (10 << 8) | (123))

/*!
 * \brief Get NMF major version corresponding to NMF version number
 * \ingroup NMF_VERSION
 */
#define VERSION_MAJOR(version)  (((version) >> 16) & 0xFF)
/*!
 * \brief Get NMF minor version corresponding to NMF version number
 * \ingroup NMF_VERSION
 */
#define VERSION_MINOR(version)  (((version) >> 8) & 0xFF)
/*!
 * \brief Get NMF patch version corresponding to NMF version number
 * \ingroup NMF_VERSION
 */
#define VERSION_PATCH(version)  (((version) >> 0) & 0xFF)

#endif /* __INC_NMF_DEF_H */
