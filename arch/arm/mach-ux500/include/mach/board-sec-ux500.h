#ifndef __BOARD_SEC_UX500_H__
#define __BOARD_SEC_UX500_H__

#if defined(CONFIG_MACH_SAMSUNG_U8500)
#include <mach/board-sec-u8500.h>
#elif defined(CONFIG_MACH_SAMSUNG_U9540)
#include <mach/board-sec-u9540.h>
#else
#error "Must define either MACH_SAMSUNG_U8500 or MACH_SAMSUNG_U9540"
#endif

#define SEC_DBG_STM_APE_OPT	0x00000001
#define SEC_DBG_STM_MODEM_OPT	0x00000002
#define SEC_DBG_STM_VIA_SD_OPTS 0x00000003
#define SEC_DBG_STM_FIDO_OPT	0x00000004
#define SEC_DBG_1M_DBG_BUFF_OPT	0x00000010
#define SEC_DBG_KDB_ENABLE_OPT	0x00000020
#define SEC_DBG_PANIC_MSG_OPT	0x00000040
#define SEC_DBG_RAMDUMP_OPT	0x00000080
extern unsigned int sec_debug_settings;

#ifdef CONFIG_SAMSUNG_LOG_BUF

#define LOG_IRQ_BUF_SIZE		(32 * 1024) /* reserve 32Kib for irq log */
#define LOG_SCHED_BUF_SIZE		(32 * 1024) /* reserve 32KiB for scheduler log */
#define LOG_SHRM_PRCMU_BUF_SIZE		(200 * 1024) /* reserve 200KiB for shrm/prcmu log */
#define	LOG_BUF_MAGIC_CODE_SIZE		sizeof(unsigned int)
#define	LOG_BUF_INDEX_SIZE		8
#define LOGGING_RAMBUF_SIZE		((1024 * 1024) - LOG_SHRM_PRCMU_BUF_SIZE - LOG_SCHED_BUF_SIZE - LOG_IRQ_BUF_SIZE)
#define LOG_IRQ_BUF_START		LOGGING_RAMBUF_SIZE
#define LOG_SCHED_BUF_START		(LOG_IRQ_BUF_START + LOG_IRQ_BUF_SIZE)
#define LOG_SHRM_PRCMU_BUF_START	(LOG_SCHED_BUF_START + LOG_SCHED_BUF_SIZE)
#define LOGGING_RAM_MASK		1

					/* 1048567 */
#define KERNEL_LOGGING_INDEX_LIMIT (LOGGING_RAMBUF_SIZE-LOG_BUF_INDEX_SIZE-1)
#define LOGGING_RAMBUF_DATA_SIZE ((LOGGING_RAMBUF_SIZE)-LOG_BUF_INDEX_SIZE)
#endif


extern unsigned int system_rev;
extern struct device *gps_dev;
extern struct class *sec_class;

void sec_cam_init(void);
void __init ssg_pins_init(void);

#endif /* __BOARD_SEC_UX500_H__ */
