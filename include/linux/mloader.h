/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Ludovic Barre <ludovic.barre@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _MLOADER_H_
#define _MLOADER_H_

/* not use in ioctl-number.txt */
#define ML_IO_NUMBER 0xFE

#define ML_UPLOAD _IO(ML_IO_NUMBER, 1)
#define ML_GET_NBIMAGES _IOR(ML_IO_NUMBER, 2, int)
#define ML_GET_DUMPINFO _IOR(ML_IO_NUMBER, 3, struct dump_image*)
#define ML_GET_FUSEINFO _IOR(ML_IO_NUMBER, 4, char*)

#define MAX_NAME 16

struct dump_image {
	char name[MAX_NAME];
	unsigned int offset;
	unsigned int size;
};

#endif /* _MLOADER_H_ */
