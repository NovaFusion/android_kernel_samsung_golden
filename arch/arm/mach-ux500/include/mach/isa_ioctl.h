/*---------------------------------------------------------------------------*/
/* Copyright ST Ericsson, 2009.                                              */
/* This program is free software; you can redistribute it and/or modify it   */
/* under the terms of the GNU General Public License as published by the     */
/* Free Software Foundation; either version 2.1 of the License, or	     */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful, but	     */
/* WITHOUT ANY WARRANTY; without even the implied warranty of		     */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.			     */
/* See the GNU General Public License for more details.			     */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program. If not, see <http://www.gnu.org/licenses/>.      */
/*---------------------------------------------------------------------------*/
#ifndef __MODEM_IPC_INCLUDED
#define __MODEM_IPC_INCLUDED

#define DLP_IOCTL_MAGIC_NUMBER 'M'
#define COMMON_BUFFER_SIZE (1024*1024)

/**
DLP Message Structure for Userland
*/
struct t_dlp_message{
	unsigned int offset;
	unsigned int size;
};

/**
mmap constants.
*/
enum t_dlp_mmap_params {
	MMAP_DLQUEUE,
	MMAP_ULQUEUE
};

/**
DLP IOCTLs for Userland
*/
#define DLP_IOC_ALLOCATE_BUFFER \
	_IOWR(DLP_IOCTL_MAGIC_NUMBER, 0, struct t_dlp_message *)
#define DLP_IOC_DEALLOCATE_BUFFER \
	_IOWR(DLP_IOCTL_MAGIC_NUMBER, 1, struct t_dlp_message *)
#define DLP_IOC_GET_MESSAGE \
	_IOWR(DLP_IOCTL_MAGIC_NUMBER, 2, struct t_dlp_message *)
#define DLP_IOC_PUT_MESSAGE \
	_IOWR(DLP_IOCTL_MAGIC_NUMBER, 3, struct t_dlp_message *)

#endif /*__MODEM_IPC_INCLUDED*/
