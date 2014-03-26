#ifndef __PROX_IOCTLS_H
#define __PROX_IOCTLS_H

/*IOCTLS*/
/*magic no*/
#define PROX_IOC_MAGIC  		0xFF
/*max seq no*/
#define PROX_IOC_NR_MAX 		2 

#define PROX_IOC_NORMAL_MODE         	_IO(PROX_IOC_MAGIC, 0)
#define PROX_IOC_SHUTDOWN_MODE         	_IO(PROX_IOC_MAGIC, 1)

#endif