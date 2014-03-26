/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Configuration Component Manager API type.
 */
#ifndef CONFIGURATION_TYPE_H
#define CONFIGURATION_TYPE_H

#include <cm/inc/cm_type.h>

/*!
 * @defgroup t_cm_cmd_id t_cm_cmd_id
 * \brief Definition of the command ID
 * \ingroup CM_CONFIGURATION_API
 *
 * CM_CMD_XXX designates the command ID used by the \ref CM_SetMode routine.
 *
 * \remarks Other command IDs are not yet implemented.
 */

typedef t_uint32 t_cm_cmd_id;                                     //!< Fake enumeration type \ingroup t_cm_cmd_id
#define CM_CMD_SYNC            ((t_cm_cmd_id)0x01)  //!< Synchronize on-going operations (no parameter) \ingroup t_cm_cmd_id

#define CM_CMD_WARM_RESET      ((t_cm_cmd_id)0x02)  //!< Reset a part of the CM-engine (parameter indicates the part which must be reseted) \ingroup t_cm_cmd_id

#define CM_CMD_PWR_MGR         ((t_cm_cmd_id)0x10)  //!< Enable/Disable the internal power management module (0=Disable, 1=Enable) \ingroup t_cm_cmd_id

#define CM_CMD_DBG_MODE        ((t_cm_cmd_id)0x40)  //!< Enable/Disable DEBUG mode, Pwr Mgr is also disabled (0=Disable, 1=Enable) \ingroup t_cm_cmd_id

#define CM_CMD_TRACE_ON             ((t_cm_cmd_id)0x41)     //!< Enable STM/XTI tracing and force network resetting and dumping \note Since MPC trace will be usable, you can enable them if not \ingroup t_cm_cmd_id
#define CM_CMD_TRACE_OFF            ((t_cm_cmd_id)0x42)     //!< Disable STM/XTI tracing \note Since MPC trace will not be usable, you can also disable them \ingroup t_cm_cmd_id

#define CM_CMD_MPC_TRACE_ON         ((t_cm_cmd_id)0x50)     //!< Enable MPC STM/XTI tracing (param == coreId). \note This command is not execute if execution engine not started on the coreId \ingroup t_cm_cmd_id
#define CM_CMD_MPC_TRACE_OFF        ((t_cm_cmd_id)0x51)     //!< Disable MPC STM/XTI tracing (param == coreId) This is the default configuration. \note This command is not execute if execution engine not started on the coreId \ingroup t_cm_cmd_id

#define CM_CMD_MPC_PRINT_OFF        ((t_cm_cmd_id)0x52)     //!< Set to OFF the level of MPC traces (param == coreId) \note This command is not execute if execution engine not started on the coreId \ingroup t_cm_cmd_id
#define CM_CMD_MPC_PRINT_ERROR      ((t_cm_cmd_id)0x53)     //!< Set to ERROR the level of MPC traces param == coreId) \note This command is not execute if execution engine not started on the coreId \ingroup t_cm_cmd_id
#define CM_CMD_MPC_PRINT_WARNING    ((t_cm_cmd_id)0x54)     //!< Set to WARNING the level of MPC traces param == coreId) \note This command is not execute if execution engine not started on the coreId \ingroup t_cm_cmd_id
#define CM_CMD_MPC_PRINT_INFO       ((t_cm_cmd_id)0x55)     //!< Set to INFO the level of MPC traces (param == coreId) \note This command is not execute if execution engine not started on the coreId This is the default configuration. \ingroup t_cm_cmd_id
#define CM_CMD_MPC_PRINT_VERBOSE    ((t_cm_cmd_id)0x56)     //!< Set to VERBOSE the level of MPC traces param == coreId) \note This command is not execute if execution engine not started on the coreId \ingroup t_cm_cmd_id

/*!
 * \brief  Define the level of internal CM log traces
 *
 * Define the level of internal CM log traces (-1 to 3)
 *    -# <b>-1 </b> all internal LOG/ERROR traces are disabled
 *    -# <b> 0 </b> all internal LOG traces are disabled (<b>default/reset value</b>)
 *    -# <b> 1, 2, 3 </b> Most and most
 *
 * \ingroup t_cm_cmd_id
 */
#define CM_CMD_TRACE_LEVEL     ((t_cm_cmd_id)0x80)

/*!
 * \brief  Enable/Disable intensive internal check
 *
 * Enable/Disable intensive internal check (0=Disable, 1=Enable):
 *    - Component handle checking
 *
 * Must be used during the integration phase (additional process is time consuming).
 *
 * \ingroup t_cm_cmd_id
 */
#define CM_CMD_INTENSIVE_CHECK ((t_cm_cmd_id)0x100)

/*!
 * \brief  Enable/Disable ulp mode
 *
 * Enable/Disable Ultra Low Power mode.
 *
 * \ingroup t_cm_cmd_id
 */
#define CM_CMD_ULP_MODE_ON      ((t_cm_cmd_id)0x111)    //!< Enable ULP mode \ingroup t_cm_cmd_id
#define CM_CMD_ULP_MODE_OFF     ((t_cm_cmd_id)0x110)    //!< Deprecated (must be removed in 2.10) !!!

#endif /* CONFIGURATION_TYPE_H */
