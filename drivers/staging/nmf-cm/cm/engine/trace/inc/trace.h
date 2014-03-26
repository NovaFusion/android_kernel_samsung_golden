/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Trace facilities management API
 *
 * \defgroup Trace Facilities
 */
#ifndef __INC_CM_TRACE_H
#define __INC_CM_TRACE_H

#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/configuration/inc/configuration_status.h>

/*********************/
/* Log related stuff */
/*********************/
#define ERROR(format, param1, param2, param3, param4, param5, param6) \
do { \
    if (cm_debug_level != -1)    \
    OSAL_Log("Error: " format, (int)(param1), (int)(param2), (int)(param3), (int)(param4), (int)(param5), (int)(param6)); \
    while(cm_error_break);\
} while(0)

#define WARNING(format, param1, param2, param3, param4, param5, param6) \
do { \
    if (cm_debug_level != -1)    \
    OSAL_Log("Warning: " format, (int)(param1), (int)(param2), (int)(param3), (int)(param4), (int)(param5), (int)(param6)); \
} while(0)

#define LOG_INTERNAL(level, format, param1, param2, param3, param4, param5, param6) \
do { \
    if (level <= cm_debug_level)    \
        OSAL_Log((const char *)format, (int)(param1), (int)(param2), (int)(param3), (int)(param4), (int)(param5), (int)(param6)); \
} while(0)

/*************************/
/* Panic related stuff   */
/*************************/
#define CM_ASSERT(cond) \
do { \
	if(!(cond)) { OSAL_Log("CM_ASSERT at %s:%d\n", (int)__FILE__, (int)__LINE__, 0, 0, 0, 0); OSAL_Panic(); while(1); } \
} while (0)

#endif /* __INC_CM_TRACE_H */
