/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson DSI link device header
 *
 * Author: Joseph V P <joseph.vp@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DSILINK_DEBUGFS__H__
#define __DSILINK_DEBUGFS__H__

int dsilink_debugfs_create(void);
int dsilink_debugfs_print_cmd(u8 cmd, u8 *data, int len, u8 *rd_wr_str);

#endif /* __DSILINK_DEBUGFS__H__ */
