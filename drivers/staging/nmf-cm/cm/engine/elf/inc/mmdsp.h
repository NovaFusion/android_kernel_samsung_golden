/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief MMDSP elf.
 */
#ifndef __INC_CM_ELF_MMDSP_H
#define __INC_CM_ELF_MMDSP_H

#include <cm/engine/elf/inc/common.h>

#define CODE_MEMORY_INDEX           0
#define ECODE_MEMORY_INDEX          7

#define XROM_MEMORY_INDEX           1
#define YROM_MEMORY_INDEX           2
#define PRIVATE_DATA_MEMORY_INDEX   8
#define SHARE_DATA_MEMORY_INDEX     1

/*
 * Relocation
 */
#define R_MMDSP_IMM16       5
#define R_MMDSP_IMM20_16    6
#define R_MMDSP_IMM20_4     7
#define R_MMDSP_24          13

#endif
