/*
 * Interface for opening the shared modem memory.
 *
 * Copyright (C) ST-Ericsson SA 2012
 * Author: Marten Olsson <marten.xm.olsson@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef OPEN_MODEM_SHARED_MEMORY_H
#define OPEN_MODEM_SHARED_MEMORY_H

#include <linux/tee.h>

/**
 * open_modem_shared_memory() - Opens the shared memory if the modem has
 * started.
 * @param status: is set to true if the memory was opened, otherwise false
 *
 * Opens the shared memory if the modem has started
 */
int open_modem_shared_memory(void);

#endif
