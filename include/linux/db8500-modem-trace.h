/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors: Michel JAOUEN <michel.jaouen@stericsson.com>
 *          Maxime COQUELIN <maxime.coquelin-nonst@stericsson.com>
 *		for ST-Ericsson
 * License terms:  GNU General Public License (GPL), version 2
 */
/*  macro for requesting a trace read */

struct modem_trace_req {
	__u32 phys_addr;
	__u8 filler;
	__u8 *buff;
	__u32 size;
};

#define TM_IO_NUMBER 0xfc
#define TM_GET_DUMPINFO _IOR(TM_IO_NUMBER, 1, unsigned long)
#define TM_TRACE_REQ _IOWR(TM_IO_NUMBER, 2, unsigned long)

struct db8500_trace_platform_data {
	unsigned long ape_base;
	unsigned long modem_base;
};
