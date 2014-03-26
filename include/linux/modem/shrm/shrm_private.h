/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghavi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __SHRM_PRIVATE_INCLUDED
#define __SHRM_PRIVATE_INCLUDED

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/modem/shrm/shrm.h>

#define GOP_OUTPUT_REGISTER_BASE (0x0)
#define GOP_SET_REGISTER_BASE    (0x4)
#define GOP_CLEAR_REGISTER_BASE  (0x8)
#define GOP_TOGGLE_REGISTER_BASE (0xc)


#define GOP_AUDIO_AC_READ_NOTIFICATION_BIT (0)
#define GOP_AUDIO_CA_MSG_PENDING_NOTIFICATION_BIT (1)
#define GOP_COMMON_AC_READ_NOTIFICATION_BIT (2)
#define GOP_COMMON_CA_MSG_PENDING_NOTIFICATION_BIT (3)
#define GOP_CA_WAKE_REQ_BIT (7)
#define GOP_AUDIO_CA_READ_NOTIFICATION_BIT (23)
#define GOP_AUDIO_AC_MSG_PENDING_NOTIFICATION_BIT (24)
#define GOP_COMMON_CA_READ_NOTIFICATION_BIT (25)
#define GOP_COMMON_AC_MSG_PENDING_NOTIFICATION_BIT (26)
#define GOP_CA_WAKE_ACK_BIT (27)

#define L2_MSG_MAPID_OFFSET (24)
#define L1_MSG_MAPID_OFFSET (28)

#define SHRM_SLEEP_STATE (0)
#define SHRM_PTR_FREE (1)
#define SHRM_PTR_BUSY (2)
#define SHRM_IDLE (3)

#define ISI_MESSAGING (0)
#define RPC_MESSAGING (1)
#define AUDIO_MESSAGING (2)
#define SECURITY_MESSAGING (3)
#define COMMON_LOOPBACK_MESSAGING (0xC0)
#define AUDIO_LOOPBACK_MESSAGING (0x80)
#define CIQ_MESSAGING (0xC3)
#define RTC_CAL_MESSAGING (0xC8)
#define IPCCTRL (0xDC)
#define IPCDATA (0xDD)
#define SYSCLK3_MESSAGING (0xE6)

#define COMMON_CHANNEL		0
#define AUDIO_CHANNEL		1

typedef void (*MSG_PENDING_NOTIF)(const u32 Wptr);

/**
 * struct fifo_write_params - parameters used for FIFO write operation.
 * @writer_local_rptr:	pointer to local read buffer
 * @writer_local_wptr:	pointer to local write buffer
 * @shared_wptr:	write pointer shared by cmt and ape
 * @shared_rptr:	read pointer shared by cmt and ape
 * @availablesize:	available memory in fifo
 * @end_addr_fifo:	fifo end addr
 * @fifo_virtual_addr:	fifo virtual addr
 * @fifo_update_lock:	spin lock to update fifo.
 *
 * On writting a message to FIFO the same has to be read by the modem before
 * writing the next message to the FIFO. In oder to over come this a local
 * write and read pointer is used for internal purpose.
 */
struct fifo_write_params {
	u32 writer_local_rptr;
	u32 writer_local_wptr;
	u32 shared_wptr;
	u32 shared_rptr;
	u32 availablesize;
	u32 end_addr_fifo;
	u32 *fifo_virtual_addr;
	spinlock_t fifo_update_lock;
} ;

/**
 * struct fifo_read_params - parameters used for FIFO read operation
 * @reader_local_rptr:	pointer to local read buffer
 * @reader_local_wptr:	pointer to local write buffer
 * @shared_wptr:	write pointer shared by cmt and ape
 * @shared_rptr:	read pointer shared by cmt and ape
 * @availablesize:	available memory in fifo
 * @end_addr_fifo:	fifo end add
 * @fifo_virtual_addr:	fifo virtual addr
 */
struct fifo_read_params{
	u32 reader_local_rptr;
	u32 reader_local_wptr;
	u32 shared_wptr;
	u32 shared_rptr;
	u32 availablesize;
	u32 end_addr_fifo;
	u32 *fifo_virtual_addr;

} ;

int shrm_protocol_init(struct shrm_dev *shrm,
			received_msg_handler common_rx_handler,
			received_msg_handler audio_rx_handler);
void shrm_protocol_deinit(struct shrm_dev *shrm);
void shm_fifo_init(struct shrm_dev *shrm);
int shm_write_msg_to_fifo(struct shrm_dev *shrm, u8 channel,
				u8 l2header, void *addr, u32 length);
int shm_write_msg(struct shrm_dev *shrm,
			u8 l2_header, void *addr, u32 length);

u8 is_the_only_one_unread_message(struct shrm_dev *shrm,
						u8 channel, u32 length);
u8 read_remaining_messages_common(void);
u8 read_remaining_messages_audio(void);
u8 read_one_l2msg_audio(struct shrm_dev *shrm,
			u8 *p_l2_msg, u32 *p_len);
u8 read_one_l2msg_common(struct shrm_dev *shrm,
				u8 *p_l2_msg, u32 *p_len);
void receive_messages_common(struct shrm_dev *shrm);
void receive_messages_audio(struct shrm_dev *shrm);

void update_ac_common_local_rptr(struct shrm_dev *shrm);
void update_ac_audio_local_rptr(struct shrm_dev *shrm);
void update_ca_common_local_wptr(struct shrm_dev *shrm);
void update_ca_audio_local_wptr(struct shrm_dev *shrm);
void update_ac_common_shared_wptr(struct shrm_dev *shrm);
void update_ac_audio_shared_wptr(struct shrm_dev *shrm);
void update_ca_common_shared_rptr(struct shrm_dev *shrm);
void update_ca_audio_shared_rptr(struct shrm_dev *shrm);


void get_writer_pointers(u8 msg_type, u32 *WriterLocalRptr, \
			u32 *WriterLocalWptr, u32 *SharedWptr);
void get_reader_pointers(u8 msg_type, u32 *ReaderLocalRptr, \
			u32 *ReaderLocalWptr, u32 *SharedRptr);
u8 read_boot_info_req(struct shrm_dev *shrm,
				u32 *pConfig,
				u32 *pVersion);
void write_boot_info_resp(struct shrm_dev *shrm, u32 Config,
							u32 Version);

void send_ac_msg_pending_notification_0(struct shrm_dev *shrm);
void send_ac_msg_pending_notification_1(struct shrm_dev *shrm);
void ca_msg_read_notification_0(struct shrm_dev *shrm);
void ca_msg_read_notification_1(struct shrm_dev *shrm);

void set_ca_msg_0_read_notif_send(u8 val);
u8 get_ca_msg_0_read_notif_send(void);
void set_ca_msg_1_read_notif_send(u8 val);
u8 get_ca_msg_1_read_notif_send(void);

irqreturn_t ca_wake_irq_handler(int irq, void *ctrlr);
irqreturn_t ac_read_notif_0_irq_handler(int irq, void *ctrlr);
irqreturn_t ac_read_notif_1_irq_handler(int irq, void *ctrlr);
irqreturn_t ca_msg_pending_notif_0_irq_handler(int irq, void *ctrlr);
irqreturn_t ca_msg_pending_notif_1_irq_handler(int irq, void *ctrlr);

void shm_ca_msgpending_0_tasklet(unsigned long);
void shm_ca_msgpending_1_tasklet(unsigned long);
void shm_ac_read_notif_0_tasklet(unsigned long);
void shm_ac_read_notif_1_tasklet(unsigned long);
void shm_ca_wake_req_tasklet(unsigned long);

u8 get_boot_state(void);

int get_ca_wake_req_state(void);

/* shrm character interface */
int isa_init(struct shrm_dev *shrm);
void isa_exit(struct shrm_dev *shrm);
int add_msg_to_queue(struct message_queue *q, u32 size);
ssize_t isa_read(struct file *filp, char __user *buf, size_t len,
							loff_t *ppos);
int get_size_of_new_msg(struct message_queue *q);
int remove_msg_from_queue(struct message_queue *q);
void shrm_char_reset_queues(struct shrm_dev *shrm);
int shrm_get_cdev_index(u8 l2_header);
int shrm_get_cdev_l2header(u8 idx);

#endif
