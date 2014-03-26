/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 */

#ifndef __LINUX_MFD_AB8500_REGULATOR_DEBUG_H
#define __LINUX_MFD_AB8500_REGULATOR_DEBUG_H

#ifdef CONFIG_REGULATOR_AB8500_DEBUG
/* AB8500 debug force/restore functions */
void ab8500_regulator_debug_force(void);
void ab8500_regulator_debug_restore(void);
#else
static inline void ab8500_regulator_debug_force(void) {}
static inline void ab8500_regulator_debug_restore(void) {}
#endif

#endif
