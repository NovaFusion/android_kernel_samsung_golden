/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * License Terms: GNU General Public License v2
 * Author: Michel Jaouen <michel.jaouen@stericsson.com>
 * Author: Alexandre Torgue <alexandre.torgues@stericsson.com>
 * Author: David Paris <david.paris@stericsson.com>
 * Author: Etienne Carriere <etienne.carriere@stericsson.com>
 * Author: Guillaume KOUADIO CARRY <guillaume.kouadio-carry@stericsson.com>
 * DBX540 PRCM Unit interface driver
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/cpufreq.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/mfd/core.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/mfd/ux500_wdt.h>
#include <linux/mfd/dbx500_temp.h>
#include <linux/regulator/db8500-prcmu.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/abx500.h>
#include <linux/time.h>
#include <linux/sched.h>
#ifdef CONFIG_C2C
#include <linux/c2c_genio.h>
#endif


#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/db8500-regs.h>
#include <mach/hardware.h>
#include <mach/prcmu-debug.h>

#include "dbx500-prcmu-regs.h"
#include "dbx540-prcmu-regs.h"

#include "dbx500-prcmu-trace.h"

#define KHZ_TO_HZ(A) A*1000
#define HZ_TO_KHZ(A) A/1000
/* Global var to runtime determine TCDM base for v2 or v1 */
static __iomem void *tcdm_base;
static __iomem void *prcmu_tcdm_base;

/* Offset for the firmware version within the TCPM */
#define PRCMU_FW_VERSION_OFFSET 0xA4


#define PRCM_BOOT_STATUS	0xFFF

#define PRCM_SW_RST_REASON 0xFF8 /* 2 bytes */

#define PRCM_TCDM_VOICE_CALL_FLAG 0xDD4 /* 4 bytes */

#define _PRCM_MBOX_HEADER		0xFE8 /* 16 bytes */
#define PRCM_MBOX_HEADER_REQ_MB0	(_PRCM_MBOX_HEADER + 0x0)
#define U9540_PRCM_MBOX_MB1_ADRESS	0x801B8A00
#define U9540_PRCM_MBOX_MB1		(U9540_PRCM_MBOX_MB1_ADRESS - U9540_PRCMU_TCDM_BASE)
#define PRCM_MBOX_HEADER_REQ_MB2	(_PRCM_MBOX_HEADER + 0x2)
#define PRCM_MBOX_HEADER_REQ_MB3	(_PRCM_MBOX_HEADER + 0x3)
#define PRCM_MBOX_HEADER_REQ_MB4	(_PRCM_MBOX_HEADER + 0x4)
#define PRCM_MBOX_HEADER_REQ_MB5	(_PRCM_MBOX_HEADER + 0x5)
#define PRCM_MBOX_HEADER_ACK_MB0	(_PRCM_MBOX_HEADER + 0x8)

/* Req Mailboxes */
#define PRCM_REQ_MB0		0xFDC /* 12 bytes  */
#define U8500_PRCM_REQ_MB1	0xFD0 /* 12 bytes  */
#define PRCM_REQ_MB2		0xFC0 /* 16 bytes  */
#define PRCM_REQ_MB3		0xE4C /* 372 bytes  */
#define PRCM_REQ_MB4		0xE48 /* 4 bytes  */
#define PRCM_REQ_MB5		0xE44 /* 4 bytes  */

/* Ack Mailboxes */
#define PRCM_ACK_MB0		0xE08 /* 52 bytes  */
#define U8500_PRCM_ACK_MB1	0xE04 /* 4 bytes */
#define PRCM_ACK_MB2		0xE00 /* 4 bytes */
#define PRCM_ACK_MB3		0xDFC /* 4 bytes */
#define PRCM_ACK_MB4		0xDF8 /* 4 bytes */
#define PRCM_ACK_MB5		0xDF4 /* 4 bytes */

/* Mailbox 0 headers */
#define MB0H_POWER_STATE_TRANS		0
#define MB0H_CONFIG_WAKEUPS_EXE		1
#define MB0H_READ_WAKEUP_ACK		3
#define MB0H_CONFIG_WAKEUPS_SLEEP	4

#define MB0H_WAKEUP_EXE 2
#define MB0H_WAKEUP_SLEEP 5

/* Mailbox 0 REQs */
#define PRCM_REQ_MB0_AP_POWER_STATE	(PRCM_REQ_MB0 + 0x0)
#define PRCM_REQ_MB0_AP_PLL_STATE	(PRCM_REQ_MB0 + 0x1)
#define PRCM_REQ_MB0_ULP_CLOCK_STATE	(PRCM_REQ_MB0 + 0x2)
#define PRCM_REQ_MB0_DO_NOT_WFI		(PRCM_REQ_MB0 + 0x3)
#define PRCM_REQ_MB0_WAKEUP_8500	(PRCM_REQ_MB0 + 0x4)
#define PRCM_REQ_MB0_WAKEUP_4500	(PRCM_REQ_MB0 + 0x8)

/* Mailbox 0 ACKs */
#define PRCM_ACK_MB0_AP_PWRSTTR_STATUS	(PRCM_ACK_MB0 + 0x0)
#define PRCM_ACK_MB0_READ_POINTER	(PRCM_ACK_MB0 + 0x1)
#define PRCM_ACK_MB0_WAKEUP_0_8500	(PRCM_ACK_MB0 + 0x4)
#define PRCM_ACK_MB0_WAKEUP_0_4500	(PRCM_ACK_MB0 + 0x8)
#define PRCM_ACK_MB0_WAKEUP_1_8500	(PRCM_ACK_MB0 + 0x1C)
#define PRCM_ACK_MB0_WAKEUP_1_4500	(PRCM_ACK_MB0 + 0x20)
#define PRCM_ACK_MB0_EVENT_4500_NUMBERS	20

/*
 * Mailbox 1 (Request/Response/Notify) - U9540
 */
struct u9540_mb1_arm_opp_req_data {
	u32 freq;
	u16 volt;
	u16 bias;
	u16 vbbp;
	u16 vbbn;
};

struct u9540_mb1_req {
	u32 req_state;
	u32 service_id;
	u32 command_id;
	u32 status;
	union {
		u32 data; /*  default: single 32bit data */
		struct u9540_mb1_arm_opp_req_data arm_opp;
		u8 full_data_buf[4*16];	 /*  TODO: check size from xp70 API */
	} data;
};

struct u9540_mb1_nfy {
	u32 nfy_state;
	u32 service_id;
	u32 command_id;
	union {
		u32 data; /*  default: single 32bit data */
		u8 full_data_buf[4];	 /*  TODO: check size from xp70 API */
	} data;
};

/*  Mailbox 1 timeout */
#define MB1TIM HZ/10
#define MB4TIM HZ/10

#define U9540_PRCM_MBOX_MB1_REQ	((struct u9540_mb1_req *) \
	(tcdm_base + U9540_PRCM_MBOX_MB1))
#define U9540_PRCM_MBOX_MB1_NFY	((struct u9540_mb1_nfy *) \
	(tcdm_base + U9540_PRCM_MBOX_MB1 + sizeof(struct u9540_mb1_req)))

enum u9540_mb1_req_state {
	U9540_PRCM_MB1_REQ_STATE_REQ_IDLE = 0,
	U9540_PRCM_MB1_REQ_STATE_REQ_SENT,
	U9540_PRCM_MB1_REQ_STATE_REQ_EXECUTING,
	U9540_PRCM_MB1_REQ_STATE_ACK_SENT,
};

enum u9540_mb1_nfy_state {
	U9540_PRCM_MB1_NFY_STATE_IDLE = 0,
	U9540_PRCM_MB1_NFY_STATE_SENT,
};

enum u9540_mb1_service {
	U9540_PRCM_MB1H_SERVICE_DDR = 0,
	U9540_PRCM_MB1H_SERVICE_DVFS,
	U9540_PRCM_MB1H_SERVICE_MODEM,
	U9540_PRCM_MB1H_SERVICE_USB,
	U9540_PRCM_MB1H_SERVICE_CLOCK,
	U9540_PRCM_MB1H_SERVICE_C2C,
	U9540_PRCM_MB1H_SERVICE_CPUHOTPLUG,
	U9540_PRCM_MB1H_SERVICE_THSENSOR,
};

/*
 * FIXME: to be removed once OPP_MAX fuse is available
 * #define PRCMU_USE_API_OPPMAX_ENABLE_FUSE
 * #warning "Building for PRCMU API without OPP MAX fuse"
 * #define PRCMU_USE_API_1_0_5 
 * FIXME: to be removed once 2.0.X is available
 * #warning "Building for PRCMU API 1.0.X"
 */

enum u9540_mb1_command {
	/* req/resp commands */
#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	U9540_PRCM_MB1H_COMMAND_SET_ARM_OPP = 0x02,
	U9540_PRCM_MB1H_COMMAND_SET_APE_OPP = 0x03,
#else
	U9540_PRCM_MB1H_COMMAND_SET_ARM_OPP = 0x1002,
	U9540_PRCM_MB1H_COMMAND_SET_APE_OPP = 0x1003,
	U9540_PRCM_MB1H_COMMAND_SET_SAFE_OPP = 0x1004,
#endif
	U9540_PRCM_MB1H_COMMAND_RESET_MODEM = 0x3001,
	U9540_PRCM_MB1H_COMMAND_USB_WAKEUP_REL = 0x4001,
	U9540_PRCM_MB1H_COMMAND_PLL_ON_OFF = 0x5001,
#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	U9540_PRCM_MB1H_COMMAND_C2CINIT = 0x1B,
	U9540_PRCM_MB1H_COMMAND_C2CNOTIFYME = 0x1C,
	U9540_PRCM_MB1H_COMMAND_C2CTESTWAKEUP = 0x1D,
#else
	U9540_PRCM_MB1H_COMMAND_C2CINIT = 0x6001,
	U9540_PRCM_MB1H_COMMAND_C2CNOTIFYME = 0x6002,
	U9540_PRCM_MB1H_COMMAND_C2CTESTWAKEUP = 0x6003,
	U9540_PRCM_MB1H_COMMAND_C2CTESTSLEEP = 0x6004,
	U9540_PRCM_MB1H_COMMAND_C2CRESET = 0x6005,
#endif
	U9540_PRCM_MB1H_COMMAND_CPU1_UNPLUG = 0x7001,
	U9540_PRCM_MB1H_COMMAND_CPU1_REPLUG = 0x7002,
	/* nfy commands */
#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	U9540_PRCM_MB1H_COMMAND_C2CNOTIFICATION = 0x1E,
#else
	U9540_PRCM_MB1H_COMMAND_C2CNOTIFICATION = 0x601,
#endif
	U9540_PRCM_MB1H_COMMAND_THSENSOR_GET_TEMP = 0x8001,
};

enum u9540_mb1_status {
	U9540_PRCM_MB1_STATUS_OK = 0,
	/* all non-0 IDs below report an error */
	U9540_PRCM_MB1_STATUS_UNKNOWN_CMD_ID,
	U9540_PRCM_MB1_STATUS_BAD_PARAM,
	U9540_PRCM_MB1_STATUS_PARTIAL_SELF_REFRESH_DDR_EXEC,
	U9540_PRCM_MB1_STATUS_QOS_DDR_EXEC,
	U9540_PRCM_MB1_STATUS_SET_ARM_OPP_EXEC,
	U9540_PRCM_MB1_STATUS_SET_ARM_OPP_INVAL,
	U9540_PRCM_MB1_STATUS_SET_APE_OPP_EXEC,
	U9540_PRCM_MB1_STATUS_SET_APE_OPP_INVAL,
	U9540_PRCM_MB1_STATUS_SET_SAFE_OPP_EXEC,
	U9540_PRCM_MB1_STATUS_SET_SAFE_OPP_INVAL,
	U9540_PRCM_MB1_STATUS_DVFS_PLL_NOT_LOCKED,
	U9540_PRCM_MB1_STATUS_C2C_UNKNOWN_ERR,
	U9540_PRCM_MB1_STATUS_BAD_STATE,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_ALRDY_UNPLUGED,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_NOT_UNPLUGED,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_SECURE_ROM_ERR,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_UNKNOWN_ERR,
	U9540_PRCM_MB1_STATUS_INVALID_STATE,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_ARMVOK_TIMEOUT,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_ROMCODESAVEOWNCTX_ERR,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_WAKEUPNORESP_ROM_ERR,
	U9540_PRCM_MB1_STATUS_CPUHOTPLUG_RESPLSNOTDSTOREADY,
	U9540_PRCM_MB1_STATUS_OVERFLOW,
	U9540_PRCM_MB1_STATUS_BUSY,
	U9540_PRCM_MB1_STATUS_SET_ARM_OPP_FREQ_ERR,
	U9540_PRCM_MB1_STATUS_THSENSOR_ALL_READY,
};

enum u9540_mb1_ape_opp_ids {
	U9540_PRCM_REQ_MB1_APE_OPP_RET = 0,
	U9540_PRCM_REQ_MB1_APE_OPP_1,
	U9540_PRCM_REQ_MB1_APE_OPP_2,
};

enum u9540_mb1_pll_on_off_ids {
	U9540_PRCM_REQ_MB1_PLL_SOC0_OFF = 1,
	U9540_PRCM_REQ_MB1_PLL_SOC0_ON	= 2,
	U9540_PRCM_REQ_MB1_PLL_SOC1_OFF = 4,
	U9540_PRCM_REQ_MB1_PLL_SOC1_ON	= 8,
};

enum u9540_mb1_vsafe_opp_ids {
	U9540_PRCM_REQ_MB1_VSAFE_OPP0 = 0,
	U9540_PRCM_REQ_MB1_VSAFE_OPP1,
	U9540_PRCM_REQ_MB1_VSAFE_OPP2,
};

enum u9540_mb1_c2c_ids {
#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	U9540_PRCM_MB1H_NFYDAT_C2CNOTIF_OK = 0x1,
	U9540_PRCM_REQ_DATA_C2C_NOTIFYME = 0x1,
#else
	U9540_PRCM_MB1H_NFYDAT_C2CNOTIF_OK = 0x601,
	U9540_PRCM_REQ_DATA_C2C_NOTIFYME = 0x601,
#endif
};


/* Mailbox 2 headers */
#define MB2H_DPS	0x0
#define MB2H_AUTO_PWR	0x1

/* Mailbox 2 REQs */
#define PRCM_REQ_MB2_SVA_MMDSP		(PRCM_REQ_MB2 + 0x0)
#define PRCM_REQ_MB2_SVA_PIPE		(PRCM_REQ_MB2 + 0x1)
#define PRCM_REQ_MB2_SIA_MMDSP		(PRCM_REQ_MB2 + 0x2)
#define PRCM_REQ_MB2_SIA_PIPE		(PRCM_REQ_MB2 + 0x3)
#define PRCM_REQ_MB2_SGA		(PRCM_REQ_MB2 + 0x4)
#define PRCM_REQ_MB2_B2R2_MCDE		(PRCM_REQ_MB2 + 0x5)
#define PRCM_REQ_MB2_ESRAM12		(PRCM_REQ_MB2 + 0x6)
#define PRCM_REQ_MB2_ESRAM34		(PRCM_REQ_MB2 + 0x7)
#define PRCM_REQ_MB2_AUTO_PM_SLEEP	(PRCM_REQ_MB2 + 0x8)
#define PRCM_REQ_MB2_AUTO_PM_IDLE	(PRCM_REQ_MB2 + 0xC)

/* Mailbox 2 ACKs */
#define PRCM_ACK_MB2_DPS_STATUS (PRCM_ACK_MB2 + 0x0)
#define HWACC_PWR_ST_OK 0xFE

/* Mailbox 3 headers */
#define MB3H_ANC	0x0
#define MB3H_SIDETONE	0x1
#define MB3H_SYSCLK	0xE

/* Mailbox 3 Requests */
#define PRCM_REQ_MB3_ANC_FIR_COEFF	(PRCM_REQ_MB3 + 0x0)
#define PRCM_REQ_MB3_ANC_IIR_COEFF	(PRCM_REQ_MB3 + 0x20)
#define PRCM_REQ_MB3_ANC_SHIFTER	(PRCM_REQ_MB3 + 0x60)
#define PRCM_REQ_MB3_ANC_WARP		(PRCM_REQ_MB3 + 0x64)
#define PRCM_REQ_MB3_SIDETONE_FIR_GAIN	(PRCM_REQ_MB3 + 0x68)
#define PRCM_REQ_MB3_SIDETONE_FIR_COEFF	(PRCM_REQ_MB3 + 0x6C)
#define PRCM_REQ_MB3_SYSCLK_MGT		(PRCM_REQ_MB3 + 0x16C)

/* Mailbox 4 headers */
#define MB4H_DDR_INIT	0x0
#define MB4H_MEM_ST	0x1
#define MB4H_HOTDOG	0x12
#define MB4H_HOTMON	0x13
#define MB4H_HOT_PERIOD	0x14
#define MB4H_A9WDOG_CONF 0x16
#define MB4H_A9WDOG_EN   0x17
#define MB4H_A9WDOG_DIS  0x18
#define MB4H_A9WDOG_LOAD 0x19
#define MB4H_A9WDOG_KICK 0x20

/* Mailbox 4 Requests */
#define PRCM_REQ_MB4_DDR_ST_AP_SLEEP_IDLE	(PRCM_REQ_MB4 + 0x0)
#define PRCM_REQ_MB4_DDR_ST_AP_DEEP_IDLE	(PRCM_REQ_MB4 + 0x1)
#define PRCM_REQ_MB4_ESRAM0_ST			(PRCM_REQ_MB4 + 0x3)
#define PRCM_REQ_MB4_HOTDOG_THRESHOLD		(PRCM_REQ_MB4 + 0x0)
#define PRCM_REQ_MB4_HOTMON_LOW			(PRCM_REQ_MB4 + 0x0)
#define PRCM_REQ_MB4_HOTMON_HIGH		(PRCM_REQ_MB4 + 0x1)
#define PRCM_REQ_MB4_HOTMON_CONFIG		(PRCM_REQ_MB4 + 0x2)
#define PRCM_REQ_MB4_HOT_PERIOD			(PRCM_REQ_MB4 + 0x0)
#define HOTMON_CONFIG_LOW			BIT(0)
#define HOTMON_CONFIG_HIGH			BIT(1)
#define PRCM_REQ_MB4_A9WDOG_0			(PRCM_REQ_MB4 + 0x0)
#define PRCM_REQ_MB4_A9WDOG_1			(PRCM_REQ_MB4 + 0x1)
#define PRCM_REQ_MB4_A9WDOG_2			(PRCM_REQ_MB4 + 0x2)
#define PRCM_REQ_MB4_A9WDOG_3			(PRCM_REQ_MB4 + 0x3)
#define A9WDOG_AUTO_OFF_EN			BIT(7)
#define A9WDOG_AUTO_OFF_DIS			0
#define A9WDOG_ID_MASK				0xf

/* Mailbox 5 Requests */
#define PRCM_REQ_MB5_I2C_SLAVE_OP	(PRCM_REQ_MB5 + 0x0)
#define PRCM_REQ_MB5_I2C_HW_BITS	(PRCM_REQ_MB5 + 0x1)
#define PRCM_REQ_MB5_I2C_REG		(PRCM_REQ_MB5 + 0x2)
#define PRCM_REQ_MB5_I2C_VAL		(PRCM_REQ_MB5 + 0x3)
#define PRCMU_I2C_WRITE(slave) (((slave) << 1) | BIT(6))
#define PRCMU_I2C_READ(slave) (((slave) << 1) | BIT(0) | BIT(6))
#define PRCMU_I2C_STOP_EN		BIT(3)

/* Mailbox 5 ACKs */
#define PRCM_ACK_MB5_I2C_STATUS	(PRCM_ACK_MB5 + 0x1)
#define PRCM_ACK_MB5_I2C_VAL	(PRCM_ACK_MB5 + 0x3)
#define I2C_WR_OK 0x1
#define I2C_RD_OK 0x2

#define NUM_MB 8
#define MBOX_BIT BIT
#define ALL_MBOX_BITS (MBOX_BIT(NUM_MB) - 1)

/*
 * Wakeups/IRQs
 */

#define WAKEUP_BIT_RTC BIT(0)
#define WAKEUP_BIT_RTT0 BIT(1)
#define WAKEUP_BIT_RTT1 BIT(2)
#define WAKEUP_BIT_HSI0 BIT(3)
#define WAKEUP_BIT_HSI1 BIT(4)
#define WAKEUP_BIT_CA_WAKE BIT(5)
#define WAKEUP_BIT_USB BIT(6)
#define WAKEUP_BIT_ABB BIT(7)
#define WAKEUP_BIT_ABB_FIFO BIT(8)
#define WAKEUP_BIT_SYSCLK_OK BIT(9)
#define WAKEUP_BIT_CA_SLEEP BIT(10)
#define WAKEUP_BIT_AC_WAKE_ACK BIT(11)
#define WAKEUP_BIT_SIDE_TONE_OK BIT(12)
#define WAKEUP_BIT_ANC_OK BIT(13)
#define WAKEUP_BIT_SW_ERROR BIT(14)
#define WAKEUP_BIT_AC_SLEEP_ACK BIT(15)
#define WAKEUP_BIT_ARM BIT(17)
#define WAKEUP_BIT_HOTMON_LOW BIT(18)
#define WAKEUP_BIT_HOTMON_HIGH BIT(19)
#define WAKEUP_BIT_MODEM_SW_RESET_REQ BIT(20)
#define WAKEUP_BIT_GPIO0 BIT(23)
#define WAKEUP_BIT_GPIO1 BIT(24)
#define WAKEUP_BIT_GPIO2 BIT(25)
#define WAKEUP_BIT_GPIO3 BIT(26)
#define WAKEUP_BIT_GPIO4 BIT(27)
#define WAKEUP_BIT_GPIO5 BIT(28)
#define WAKEUP_BIT_GPIO6 BIT(29)
#define WAKEUP_BIT_GPIO7 BIT(30)
#define WAKEUP_BIT_GPIO8 BIT(31)


static int cpu1_unplug_ongoing;
static int prcmu_driver_initialised;
static int set_arm_freq(u32 freq);
static int get_arm_freq(void);


static struct {
	bool valid;
	struct prcmu_fw_version version;
} fw_info;

/*
 * This vector maps irq numbers to the bits in the bit field used in
 * communication with the PRCMU firmware.
 *
 * The reason for having this is to keep the irq numbers contiguous even though
 * the bits in the bit field are not. (The bits also have a tendency to move
 * around, to further complicate matters.)
 */
#define IRQ_INDEX(_name) ((IRQ_PRCMU_##_name) - IRQ_PRCMU_BASE)
#define IRQ_ENTRY(_name)[IRQ_INDEX(_name)] = (WAKEUP_BIT_##_name)
static u32 prcmu_irq_bit[NUM_PRCMU_WAKEUPS] = {
	IRQ_ENTRY(RTC),
	IRQ_ENTRY(RTT0),
	IRQ_ENTRY(RTT1),
	IRQ_ENTRY(HSI0),
	IRQ_ENTRY(HSI1),
	IRQ_ENTRY(CA_WAKE),
	IRQ_ENTRY(USB),
	IRQ_ENTRY(ABB),
	IRQ_ENTRY(ABB_FIFO),
	IRQ_ENTRY(CA_SLEEP),
	IRQ_ENTRY(ARM),
	IRQ_ENTRY(HOTMON_LOW),
	IRQ_ENTRY(HOTMON_HIGH),
	IRQ_ENTRY(MODEM_SW_RESET_REQ),
	IRQ_ENTRY(GPIO0),
	IRQ_ENTRY(GPIO1),
	IRQ_ENTRY(GPIO2),
	IRQ_ENTRY(GPIO3),
	IRQ_ENTRY(GPIO4),
	IRQ_ENTRY(GPIO5),
	IRQ_ENTRY(GPIO6),
	IRQ_ENTRY(GPIO7),
	IRQ_ENTRY(GPIO8)
};

#define VALID_WAKEUPS (BIT(NUM_PRCMU_WAKEUP_INDICES) - 1)
#define WAKEUP_ENTRY(_name)[PRCMU_WAKEUP_INDEX_##_name] = (WAKEUP_BIT_##_name)
static u32 prcmu_wakeup_bit[NUM_PRCMU_WAKEUP_INDICES] = {
	WAKEUP_ENTRY(RTC),
	WAKEUP_ENTRY(RTT0),
	WAKEUP_ENTRY(RTT1),
	WAKEUP_ENTRY(HSI0),
	WAKEUP_ENTRY(HSI1),
	WAKEUP_ENTRY(USB),
	WAKEUP_ENTRY(ABB),
	WAKEUP_ENTRY(ABB_FIFO),
	WAKEUP_ENTRY(ARM)
};
static unsigned long latest_armss_rate;

#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	int tmpvsafe = VSAFE_50_OPP;
#endif

/*The timer time-base is in nano-seconde*/
#define TIME_NS 1000000000ULL
/* profiling cycle time (in second)*/
#define PROFILING_CYCLE_TIME 4ULL
/* STORE_CYCLE = TIME_NS*PROFILING_CYCLE_TIME in NS*/
#define STORE_CYCLE (TIME_NS * PROFILING_CYCLE_TIME)
/* 9540 aging in second (8 years by default)*/
#define	DB9540_AGING 252288000ULL
/* 9540 aging in nano-second*/
#define	DB9540_AGING_TRADE (DB9540_AGING * TIME_NS)

/* SecMap is at 0x200 from tcdm_base adress*/
#define PRCMU_SECMAP 0x0200
/* InitOppData is at 0x5A0 from SecMap */
#define PRCM_INIT_OPP_DATA (PRCMU_SECMAP + 0x05A0)
/* OPP0 table is at 0x48 from InitOppData */
#define PRCMU_OPP0_TABLE (PRCM_INIT_OPP_DATA + 0x0048)
/* OPP0 enable/disable is at 0x6 from OPP0 table*/
#define PRCMU_OPP0_IS_ENABLE (PRCMU_OPP0_TABLE + 0x0006)

struct max_opp_profile {
	u32 last_arm_opp;
	u64 max_opp_cnt;
	u64 secure_memory;
	u64 cumul;
	u64 start;
};

static struct max_opp_profile arm_max_opp_profile = {
	.last_arm_opp = 0,
	.max_opp_cnt = 0,
	.secure_memory = 0,
	.cumul = 0,
	.start = 0,
};

/*
 * mb0_transfer - state needed for mailbox 0 communication.
 * @lock:		The transaction lock.
 * @dbb_events_lock:	A lock used to handle concurrent access to (parts of)
 *			the request data.
 * @mask_work:		Work structure used for (un)masking wakeup interrupts.
 * @req:		Request data that need to persist between requests.
 */
static struct {
	spinlock_t lock;
	spinlock_t dbb_irqs_lock;
	struct work_struct mask_work;
	struct mutex ac_wake_lock;
	struct completion ac_wake_work;
	struct {
		u32 dbb_irqs;
		u32 dbb_wakeups;
		u32 abb_events;
	} req;
} mb0_transfer;

/* U9540 MB1 Acknowledgement data: data copied from mb1 buffer */
struct mb1_ack_u9540 {
	u32 service_id;
	u32 command_id;
	u32 status;
	u32 arm_freq;
	u32 sensor_read;
};

/*
 * mb1_transfer - state needed for mailbox 1 communication.
 * @lock:	The transaction lock.
 * @work:	The transaction completion structure.
 * @ape_opp:	The current APE OPP.
 * @arm_freq:	The current ARM Freq (U9540 only)
 * @ack:	Reply ("acknowledge") data. Structure used selected at run-
 *		time based on chip-set detected.
 */
static struct {
	struct mutex lock;
	struct completion work;
	u8 ape_opp;
	u32 arm_freq;
	u8 vsafe_opp;
	union {
		struct mb1_ack_u9540 u9540;
	} ack;
} mb1_transfer;

/*
 * mb2_transfer - state needed for mailbox 2 communication.
 * @lock:            The transaction lock.
 * @work:            The transaction completion structure.
 * @auto_pm_lock:    The autonomous power management configuration lock.
 * @auto_pm_enabled: A flag indicating whether autonomous PM is enabled.
 * @req:             Request data that need to persist between requests.
 * @ack:             Reply ("acknowledge") data.
 */
static struct {
	struct mutex lock;
	struct completion work;
	spinlock_t auto_pm_lock;
	bool auto_pm_enabled;
	struct {
		u8 status;
	} ack;
} mb2_transfer;

/*
 * mb3_transfer - state needed for mailbox 3 communication.
 * @lock:		The request lock.
 * @sysclk_lock:	A lock used to handle concurrent sysclk requests.
 * @sysclk_work:	Work structure used for sysclk requests.
 */
static struct {
	spinlock_t lock;
	struct mutex sysclk_lock;
	struct completion sysclk_work;
} mb3_transfer;

/*
 * mb4_transfer - state needed for mailbox 4 communication.
 * @lock:	The transaction lock.
 * @work:	The transaction completion structure.
 */
static struct {
	struct mutex lock;
	struct completion work;
} mb4_transfer;

/*
 * mb5_transfer - state needed for mailbox 5 communication.
 * @lock:	The transaction lock.
 * @work:	The transaction completion structure.
 * @ack:	Reply ("acknowledge") data.
 */
static struct {
	struct mutex lock;
	struct completion work;
	struct {
		u8 status;
		u8 value;
	} ack;
} mb5_transfer;

static atomic_t ac_wake_req_state = ATOMIC_INIT(0);

/* Spinlocks */
static DEFINE_SPINLOCK(prcmu_lock);
static DEFINE_SPINLOCK(clkout_lock);

/*
 * Copies of the startup values of the reset status register and the SW reset
 * code.
 */
static u32 reset_status_copy;
static u16 reset_code_copy;

struct clk_mgt {
	void __iomem *reg;
	u32 pllsw;
	int branch;
	bool clk38div;
};

enum {
	PLL_RAW,
	PLL_FIX,
	PLL_DIV
};

static DEFINE_SPINLOCK(clk_mgt_lock);

#define CLK_MGT_ENTRY(_name, _branch, _clk38div)[PRCMU_##_name] = \
	{ (PRCM_##_name##_MGT), 0 , _branch, _clk38div}
static struct clk_mgt clk_mgt[PRCMU_NUM_REG_CLOCKS] = {
	CLK_MGT_ENTRY(SGACLK, PLL_DIV, false),
	CLK_MGT_ENTRY(UARTCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(MSP02CLK, PLL_FIX, true),
	CLK_MGT_ENTRY(MSP1CLK, PLL_FIX, true),
	CLK_MGT_ENTRY(I2CCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(SDMMCCLK, PLL_DIV, true),
	CLK_MGT_ENTRY(SLIMCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(PER1CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(PER2CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(PER3CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(PER5CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(PER6CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(PER7CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(LCDCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(BMLCLK, PLL_DIV, true),
	CLK_MGT_ENTRY(HSITXCLK, PLL_DIV, true),
	CLK_MGT_ENTRY(HSIRXCLK, PLL_DIV, true),
	CLK_MGT_ENTRY(HDMICLK, PLL_FIX, false),
	CLK_MGT_ENTRY(APEATCLK, PLL_DIV, true),
	CLK_MGT_ENTRY(APETRACECLK, PLL_DIV, true),
	CLK_MGT_ENTRY(MCDECLK, PLL_DIV, true),
	CLK_MGT_ENTRY(IPI2CCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(DSIALTCLK, PLL_FIX, false),
	CLK_MGT_ENTRY(DMACLK, PLL_DIV, true),
	CLK_MGT_ENTRY(B2R2CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(TVCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(SSPCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(RNGCLK, PLL_FIX, true),
	CLK_MGT_ENTRY(UICCCLK, PLL_FIX, false),
	CLK_MGT_ENTRY(HVACLK, PLL_DIV, true),
	CLK_MGT_ENTRY(G1CLK, PLL_DIV, true),
	CLK_MGT_ENTRY(SPARE1CLK, PLL_FIX, true),
};

struct dsiclk {
	u32 divsel_mask;
	u32 divsel_shift;
	u32 divsel;
	u32 divsel_lcd_mask; /* For LCD DSI PLL supported by U9540 */
};

static struct dsiclk u9540_dsiclk[2] = {
	{
		.divsel_mask =
			U9540_PRCM_DSI_PLLOUT_SEL_DSI0_PLLOUT_DIVSEL_MASK,
		.divsel_shift = PRCM_DSI_PLLOUT_SEL_DSI0_PLLOUT_DIVSEL_SHIFT,
		.divsel = PRCM_DSI_PLLOUT_SEL_PHI,
		.divsel_lcd_mask = BIT(3),
	},
	{
		.divsel_mask =
			U9540_PRCM_DSI_PLLOUT_SEL_DSI1_PLLOUT_DIVSEL_MASK,
		.divsel_shift = PRCM_DSI_PLLOUT_SEL_DSI1_PLLOUT_DIVSEL_SHIFT,
		.divsel = PRCM_DSI_PLLOUT_SEL_PHI,
		.divsel_lcd_mask = BIT(11),
	}
};

struct dsiescclk {
	u32 en;
	u32 div_mask;
	u32 div_shift;
};

static struct dsiescclk dsiescclk[3] = {
	{
		.en = PRCM_DSITVCLK_DIV_DSI0_ESC_CLK_EN,
		.div_mask = PRCM_DSITVCLK_DIV_DSI0_ESC_CLK_DIV_MASK,
		.div_shift = PRCM_DSITVCLK_DIV_DSI0_ESC_CLK_DIV_SHIFT,
	},
	{
		.en = PRCM_DSITVCLK_DIV_DSI1_ESC_CLK_EN,
		.div_mask = PRCM_DSITVCLK_DIV_DSI1_ESC_CLK_DIV_MASK,
		.div_shift = PRCM_DSITVCLK_DIV_DSI1_ESC_CLK_DIV_SHIFT,
	},
	{
		.en = PRCM_DSITVCLK_DIV_DSI2_ESC_CLK_EN,
		.div_mask = PRCM_DSITVCLK_DIV_DSI2_ESC_CLK_DIV_MASK,
		.div_shift = PRCM_DSITVCLK_DIV_DSI2_ESC_CLK_DIV_SHIFT,
	}
};

/*
* Used by MCDE to setup all necessary PRCMU registers
*/
#define PRCMU_RESET_DSIPLLTV		0x00004000
#define PRCMU_RESET_DSIPLLLCD		0x00008000
#define PRCMU_UNCLAMP_DSIPLL		0x00400800

#define PRCMU_CLK_PLL_DIV_SHIFT		0
#define PRCMU_CLK_PLL_SW_SHIFT		5
#define PRCMU_CLK_38			(1 << 9)
#define PRCMU_CLK_38_SRC		(1 << 10)
#define PRCMU_CLK_38_DIV		(1 << 11)

/* PLLDIV=12, PLLSW=4 (PLLDDR) */
#define PRCMU_DSI_CLOCK_SETTING		0x0000008C
/* PLLDIV = 12, PLLSW=1 (PLLSOC0) */
#define U9540_PRCMU_DSI_CLOCK_SETTING	0x0000002C

/* DPI 50000000 Hz */
#define PRCMU_DPI_CLOCK_SETTING		((1 << PRCMU_CLK_PLL_SW_SHIFT) | \
					  (16 << PRCMU_CLK_PLL_DIV_SHIFT))
#define PRCMU_DSI_LP_CLOCK_SETTING	0x00000E00

/* D=101, N=1, R=4, SELDIV2=0 */
#define PRCMU_PLLDSI_FREQ_SETTING	0x00040165

#define PRCMU_ENABLE_PLLDSI		0x00000001
#define PRCMU_DISABLE_PLLDSI		0x00000000
#define PRCMU_RELEASE_RESET_DSS		0x0000400C
#define PRCMU_TV_DSI_PLLOUT_SEL_SETTING	0x00000202
#define PRCMU_LCD_DSI_PLLOUT_SEL_SETTING	0x00000A0A
/* ESC clk, div0=1, div1=1, div2=3 */
#define PRCMU_ENABLE_ESCAPE_CLOCK_DIV	0x07030101
#define PRCMU_DISABLE_ESCAPE_CLOCK_DIV	0x00030101
#define PRCMU_DSI_RESET_SW		0x00000007

#define PRCMU_PLLDSI_LOCKP_LOCKED	0x3

/**
 * dbx500_prcmu_mb1_wait_released
 * Utility function which blocks until Mailbox 1 is released.
 */
static inline void dbx500_prcmu_mb1_wait_released(void)
{
	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(1))
		cpu_relax();
}

/**
 * db9540_prcmu_mb1_wait_for_idle
 * Utility function which blocks until Mailbox 1 is in its uniqPAP Idle state.
 */
static inline void db9540_prcmu_mb1_wait_for_idle(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	while (req->req_state != U9540_PRCM_MB1_REQ_STATE_REQ_IDLE)
		cpu_relax();
}

/**
 * unplug_cpu1 - Power gate OFF CPU1 for U9540
 * * void:
 * Returns:
 */

static int unplug_cpu1(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int r = 0;

	mutex_lock(&mb1_transfer.lock);

	/* Set flag start Hotplug sequence */
	cpu1_unplug_ongoing = 1;

	/* Wait for MBOX to become idle */
	dbx500_prcmu_mb1_wait_released();
	/* Ensure MB1 is in Idle state */
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM (header and data, then req_state) */
	req->service_id = U9540_PRCM_MB1H_SERVICE_CPUHOTPLUG;
	req->command_id = U9540_PRCM_MB1H_COMMAND_CPU1_UNPLUG;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

	/* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/* Check response from PRCMU */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_CPUHOTPLUG) &&
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_CPU1_UNPLUG)) {
		switch (ack->status) {
		case U9540_PRCM_MB1_STATUS_CPUHOTPLUG_UNKNOWN_ERR:
			pr_err("PRCMU: %s, unknown error\n", __func__);
			WARN_ON(1);
			break;
		case U9540_PRCM_MB1_STATUS_CPUHOTPLUG_ROMCODESAVEOWNCTX_ERR:
			pr_err("PRCMU: %s, CPU1 ROM code err: save own context error\n"
			, __func__);
			break;
	}
	} else {
		r = -EIO;
		pr_err("PRCMU - bad ack in %s. %u %u %u\n", __func__,
		ack->service_id, ack->command_id, ack->status);
	}
	/* set flag HotPlug sequence end */
	cpu1_unplug_ongoing = 0;

	mutex_unlock(&mb1_transfer.lock);

	return r;


}


/**
 * replug_cpu1 - Power gate ON CPU1 for U9540
 * * void
 * * Returns:
 */
static int replug_cpu1(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int r = 0;

	if (prcmu_driver_initialised == 0) {
		pr_info("PRCMU: %s, PRCMU DRIVER NOT INITIALISED\n", __func__);
		return 0;
	}

	mutex_lock(&mb1_transfer.lock);

	/* Wait for MBOX to become idle */
	dbx500_prcmu_mb1_wait_released();
	/* Ensure MB1 is in Idle state */
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM (header and data, then req_state) */
	req->service_id = U9540_PRCM_MB1H_SERVICE_CPUHOTPLUG;
	req->command_id = U9540_PRCM_MB1H_COMMAND_CPU1_REPLUG;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;


	/* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/* Check response from PRCMU */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_CPUHOTPLUG) &&
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_CPU1_REPLUG)) {
		switch (ack->status) {
		case U9540_PRCM_MB1_STATUS_CPUHOTPLUG_UNKNOWN_ERR:
			pr_err("PRCMU: %s, unknown error\n", __func__);
			WARN_ON(1);
			break;
		case U9540_PRCM_MB1_STATUS_CPUHOTPLUG_WAKEUPNORESP_ROM_ERR:
			pr_err("PRCMU: %s, CPU1 Rom code err: no resp at wake up\n"
					, __func__);
			WARN_ON(1);
			break;
		case U9540_PRCM_MB1_STATUS_CPUHOTPLUG_RESPLSNOTDSTOREADY:
			pr_err("PRCMU: %s, CPU1 Rom code err: no Ds to Rdy\n"
					, __func__);
			WARN_ON(1);
			break;
	}
	} else {
		r = -EIO;
		pr_err("PRCMU - bad ack in %s. %u %u %u\n", __func__,
		ack->service_id, ack->command_id, ack->status);
	}

	mutex_unlock(&mb1_transfer.lock);

	return r;
}

static u32 dbx540_prcmu_read(unsigned int reg)
{
	return readl(_PRCMU_BASE + reg);
}

static void dbx540_prcmu_write(unsigned int reg, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&prcmu_lock, flags);
	writel(value, (_PRCMU_BASE + reg));
	spin_unlock_irqrestore(&prcmu_lock, flags);
}

static void dbx540_prcmu_write_masked(unsigned int reg, u32 mask, u32 value)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&prcmu_lock, flags);
	val = readl(_PRCMU_BASE + reg);
	val = ((val & ~mask) | (value & mask));
	writel(val, (_PRCMU_BASE + reg));
	spin_unlock_irqrestore(&prcmu_lock, flags);
}

/*
 * Dump AB8500 registers, PRCMU registers and PRCMU data memory
 * on critical errors.
 */
static void dbx540_prcmu_debug_dump(const char *func,
				bool dump_prcmu, bool dump_abb)
{
	printk(KERN_DEBUG"%s: timeout\n", func);

	/* Dump AB8500 registers */
	if (dump_abb)
		abx500_dump_all_banks();

	/* Dump prcmu registers and data memory */
	if (dump_prcmu) {
		prcmu_debug_dump_regs();
		prcmu_debug_dump_data_mem();
	}
}
static struct cpufreq_frequency_table *freq_table;

static struct prcmu_fw_version *get_fw_version(void)
{
	return fw_info.valid ? &fw_info.version : NULL;
}

static bool db9540_check_ap9540_age(void);

bool has_arm_maxopp(void)
{
#ifdef PRCMU_USE_API_OPPMAX_ENABLE_FUSE /* TODO: remove once useless */
	return false;
#else
	if ((!db9540_check_ap9540_age()) ||
			(readw(prcmu_tcdm_base+PRCMU_OPP0_IS_ENABLE) != 1))
		return false;
	else
		return true;

#endif
}

static void update_freq_table(struct cpufreq_frequency_table *table)
{
	if (has_arm_maxopp())
		table[5].frequency = 1850000;
}

static void vc(bool enable)
{
	writel((enable ? 0xF : 0), (tcdm_base + PRCM_TCDM_VOICE_CALL_FLAG));
}

/**
 * config_clkout - Configure one of the programmable clock outputs.
 * @clkout:	The CLKOUT number (0 or 1).
 * @source:	The clock to be used (one of the PRCMU_CLKSRC_*).
 * @div:	The divider to be applied.
 *
 * Configures one of the programmable clock outputs (CLKOUTs).
 * @div should be in the range [1,63] to request a configuration, or 0 to
 * inform that the configuration is no longer requested.
 */
static int config_clkout(u8 clkout, u8 source, u8 div)
{
	static int requests[2];
	int r = 0;
	unsigned long flags;
	u32 val;
	u32 bits;
	u32 mask;
	u32 div_mask;

	BUG_ON(clkout > 1);
	BUG_ON(div > 63);
	BUG_ON((clkout == 0) && (source > PRCMU_CLKSRC_CLK009));

	if (!div && !requests[clkout])
		return -EINVAL;

	switch (clkout) {
	case 0:
		div_mask = PRCM_CLKOCR_CLKODIV0_MASK;
		mask = (PRCM_CLKOCR_CLKODIV0_MASK | PRCM_CLKOCR_CLKOSEL0_MASK);
		bits = ((source << PRCM_CLKOCR_CLKOSEL0_SHIFT) |
			(div << PRCM_CLKOCR_CLKODIV0_SHIFT));
		break;
	case 1:
		div_mask = PRCM_CLKOCR_CLKODIV1_MASK;
		mask = (PRCM_CLKOCR_CLKODIV1_MASK | PRCM_CLKOCR_CLKOSEL1_MASK |
			PRCM_CLKOCR_CLK1TYPE);
		bits = ((source << PRCM_CLKOCR_CLKOSEL1_SHIFT) |
			(div << PRCM_CLKOCR_CLKODIV1_SHIFT));
		break;
	}
	bits &= mask;

	spin_lock_irqsave(&clkout_lock, flags);

	val = readl(PRCM_CLKOCR);
	if (val & div_mask) {
		if (div) {
			if ((val & mask) != bits) {
				r = -EBUSY;
				goto unlock_and_return;
			}
		} else {
			if ((val & mask & ~div_mask) != bits) {
				r = -EINVAL;
				goto unlock_and_return;
			}
		}
	}
	writel((bits | (val & ~mask)), PRCM_CLKOCR);
	requests[clkout] += (div ? 1 : -1);

unlock_and_return:
	spin_unlock_irqrestore(&clkout_lock, flags);

	return r;
}

/*  transition translation table to FW magic number */
static u8 fw_trans[] = {
	0x00,/* PRCMU_AP_NO_CHANGE */
	0x10,/* PRCMU_AP_SLEEP */
	0x43,/* PRCMU_AP_DEEP_SLEEP */
	0x50,/* PRCMU_AP_IDLE */
	0x73,/*	PRCMU_AP_DEEP_IDLE */
};

static int set_power_state(u8 state, bool keep_ulp_clk,
		bool keep_ap_pll)
{
	unsigned long flags;

	BUG_ON((state == PRCMU_AP_NO_CHANGE) ||
			(state >= ARRAY_SIZE(fw_trans)));

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
		cpu_relax();

	writeb(MB0H_POWER_STATE_TRANS, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB0));
	writeb(fw_trans[state], (tcdm_base + PRCM_REQ_MB0_AP_POWER_STATE));
	writeb((keep_ap_pll ? 1 : 0), (tcdm_base + PRCM_REQ_MB0_AP_PLL_STATE));
	writeb((keep_ulp_clk ? 1 : 0),
		(tcdm_base + PRCM_REQ_MB0_ULP_CLOCK_STATE));
	writeb(0, (tcdm_base + PRCM_REQ_MB0_DO_NOT_WFI));
	writel(MBOX_BIT(0), PRCM_MBOX_CPU_SET);

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);

	trace_u8500_set_power_state(fw_trans[state], keep_ulp_clk, keep_ap_pll);
	return 0;
}

static u8 get_power_state_result(void)
{
	u8 status;
	status = readb(tcdm_base + PRCM_ACK_MB0_AP_PWRSTTR_STATUS);
	trace_u8500_get_power_state_result(status);
	return status;
}

static int stay_in_wfi_check(void)
{
	int stay_in_wfi = 0;
	u8 status;

	status = readb(tcdm_base + PRCM_ACK_MB0_AP_PWRSTTR_STATUS);

	if ((status == EXECUTETODEEPSLEEP)
			|| (status == EXECUTETODEEPIDLE)) {
		stay_in_wfi = 1;
	}
	if (cpu1_unplug_ongoing == 1)
		stay_in_wfi = 1;

	return stay_in_wfi;
}

/* This function should only be called while mb0_transfer.lock is held. */
static void config_wakeups(void)
{
	const u8 header[2] = {
		MB0H_CONFIG_WAKEUPS_EXE,
		MB0H_CONFIG_WAKEUPS_SLEEP
	};
	static u32 last_dbb_events;
	static u32 last_abb_events;
	u32 dbb_events;
	u32 abb_events;
	unsigned int i;

	dbb_events = mb0_transfer.req.dbb_irqs | mb0_transfer.req.dbb_wakeups;
	dbb_events |= (WAKEUP_BIT_AC_WAKE_ACK | WAKEUP_BIT_AC_SLEEP_ACK);

	abb_events = mb0_transfer.req.abb_events;

	if ((dbb_events == last_dbb_events) && (abb_events == last_abb_events))
		return;

	for (i = 0; i < 2; i++) {
		while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
			cpu_relax();
		writel(dbb_events, (tcdm_base + PRCM_REQ_MB0_WAKEUP_8500));
		writel(abb_events, (tcdm_base + PRCM_REQ_MB0_WAKEUP_4500));
		writeb(header[i], (tcdm_base + PRCM_MBOX_HEADER_REQ_MB0));
		writel(MBOX_BIT(0), PRCM_MBOX_CPU_SET);
	}
	last_dbb_events = dbb_events;
	last_abb_events = abb_events;
	trace_u8500_config_wakeups(dbb_events, abb_events);
}

static void enable_wakeups(u32 wakeups)
{
	unsigned long flags;
	u32 bits;
	int i;

	BUG_ON(wakeups != (wakeups & VALID_WAKEUPS));

	for (i = 0, bits = 0; i < NUM_PRCMU_WAKEUP_INDICES; i++) {
		if (wakeups & BIT(i))
			bits |= prcmu_wakeup_bit[i];
	}

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	mb0_transfer.req.dbb_wakeups = bits;
	config_wakeups();

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

static void config_abb_event_readout(u32 abb_events)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	mb0_transfer.req.abb_events = abb_events;
	config_wakeups();

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

static void get_abb_event_buffer(void __iomem **buf)
{
	if (readb(tcdm_base + PRCM_ACK_MB0_READ_POINTER) & 1)
		*buf = (tcdm_base + PRCM_ACK_MB0_WAKEUP_1_4500);
	else
		*buf = (tcdm_base + PRCM_ACK_MB0_WAKEUP_0_4500);
}

/*
 * db9540_write_arm_max_opp : mission profile
 * write accumulated arm_max_opp value
 * The parameter val is in second
 */
static void db9540_write_arm_max_opp(u64 val)
{
	/*Call the routine to write arm_max_opp value  */
	arm_max_opp_profile.secure_memory = val;
}


/*
 * db9540_read_arm_max_opp_counter : mission profile
 * read accumulated arm_max_opp
 * The return value is in second
 */
u64 db9540_read_arm_max_opp_counter(void)
{
	/*
	 * Call the routine to read arm_max_opp value
	 The return value should replace "arm_max_opp_profile.max_opp_cnt"
	 */
	return arm_max_opp_profile.secure_memory;
}

/*
 * return false if AP9540 current age is higher than the max allowed
 */
static bool db9540_check_ap9540_age(void)
{
	return ((arm_max_opp_profile.max_opp_cnt >=
			DB9540_AGING_TRADE) ? false : true);
}

/*
 * db9540_memorize_arm_max_opp accumulate the time spent at arm_opp_max
 * If the store time is grater than "STORE_CYCLE",
 * call the function .... to memorize in FLASH Mem
 * The timer time-base is in nano-second
 */
void db9540_memorize_arm_max_opp(u32 freq)
{
	if (arm_max_opp_profile.last_arm_opp == 1850000) {
		bool save;
		u64 this_time , delta_time;

		this_time = sched_clock();
		delta_time = this_time - arm_max_opp_profile.start;
		arm_max_opp_profile.cumul += delta_time;
		arm_max_opp_profile.max_opp_cnt += delta_time;
		arm_max_opp_profile.start = this_time;
		arm_max_opp_profile.last_arm_opp = freq;
		save = false;
		while (arm_max_opp_profile.cumul >= STORE_CYCLE) {
			save = true;
			arm_max_opp_profile.cumul -=  STORE_CYCLE;
		}
		/* call to save the counter content*/
		if (save)
			db9540_write_arm_max_opp(
					arm_max_opp_profile.max_opp_cnt);
	 }
	else if (freq == 1850000) {
		arm_max_opp_profile.start = sched_clock();
		arm_max_opp_profile.last_arm_opp = freq;
	}
}

/*
 * set_arm_freq - set the appropriate ARM frequency for U9540
 * @freq: The new ARM frequency to which transition is to be made (kHz)
 * Returns: 0 on success, non-zero on failure
 */
static int set_arm_freq(u32 freq)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int r = 0;

	if (mb1_transfer.arm_freq == freq)
		return 0;

	mutex_lock(&mb1_transfer.lock);

	dbx500_prcmu_mb1_wait_released();
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM (header and data, then req_state) */
	req->service_id = U9540_PRCM_MB1H_SERVICE_DVFS;
	req->command_id = U9540_PRCM_MB1H_COMMAND_SET_ARM_OPP;
	req->data.arm_opp.freq = freq;
	req->data.arm_opp.volt = 0;
	req->data.arm_opp.bias = 0;
	req->data.arm_opp.vbbp = 0;
	req->data.arm_opp.vbbn = 0;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

	/* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	wait_for_completion(&mb1_transfer.work);

	/* Check response from PRCMU */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_DVFS) &&
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_SET_ARM_OPP) &&
		(ack->status == U9540_PRCM_MB1_STATUS_OK)) {
		mb1_transfer.arm_freq = freq;
		latest_armss_rate = freq;
		db9540_memorize_arm_max_opp(freq);
	} else {
		r = -EIO;
		pr_info("PRCMU - bad ack in %s. %u %u %u %u %u\n", __func__,
		ack->service_id, ack->command_id, ack->status, ack->arm_freq,
		freq);
	}

	mutex_unlock(&mb1_transfer.lock);

	return r;
}

/**
 * get_arm_freq - get the current ARM freq
 *
 * Returns: the current ARM freq (kHz).
 * Not supported by U8500
 */
static int get_arm_freq(void)
{
	u32 val;
	/*
	 * U9540 is not able to read ARM OPP value from TCDM. Therefore
	 * determine if the ARM OPP has been set, or not.
	 */
	if (mb1_transfer.arm_freq != 0)
		return mb1_transfer.arm_freq;

	/* ARM OPP value not yet initialised. Read value from register. */
	val = readl(PRCM_POWER_STATE_VAL);
	val &= PRCM_POWER_STATE_VAL_VARM_STATE_OPP_MASK;
	val >>= PRCM_POWER_STATE_VAL_VARM_STATE_OPP_SHIFT;

	switch (val) {
	case 0x00:
		return 1850000;
	case 0x01:
		return 1500000;
	case 0x02:
		return 1200000;
	case 0x03:
		return 800000;
	case 0x04:
		return 400000;
	case 0x05:
		return 266000;
	default:
		pr_warn("prcmu: %s Unknown ARM OPP val %d\n", __func__, val);
		/* Return fastest non-"speed-binned" frequency */
		return 1500000;
	}
}

/**
 * prcmu_get_vsafe_opp - get the current VSAFE OPP
 *
 * Returns: the current VSAFE OPP
 */
int prcmu_get_vsafe_opp(void)
{


	/*
	 * U9540 is not able to read VSAFE OPP value from TCDM. Therefore
	 * determine if the VSAFE OPP has been set, or not.
	 */
#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	return tmpvsafe;
#else

	if (mb1_transfer.vsafe_opp != 0) {
		return mb1_transfer.vsafe_opp;
	} else {
		/*
		 * VSAFE OPP value not yet initialised.
		 * Return default (reset) value.
		 */
		return VSAFE_100_OPP;
	}
#endif
}

/**
 * prcmu_set_vsafe_opp - set the appropriate VSAFE OPP
 * @opp: The new VSAFE operating point to which transition is to be made
 * Returns: 0 on success, non-zero on failure
 *
 * This function sets the operating point of the VSAFE.
 */
int prcmu_set_vsafe_opp(u8 opp)
{

#ifdef PRCMU_USE_API_1_0_5 /* TODO: remove once useles */
	if ((opp == VSAFE_50_OPP) || (opp == VSAFE_100_OPP))
		tmpvsafe = opp;
	return 0;
#else

	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int r = 0;
	u32 prcmu_opp;

	switch (opp) {
	case VSAFE_50_OPP:
		prcmu_opp = U9540_PRCM_REQ_MB1_VSAFE_OPP1;
		break;
	case VSAFE_100_OPP:
		prcmu_opp = U9540_PRCM_REQ_MB1_VSAFE_OPP2;
		break;
	default:
		/* Do nothing */
		return 0;
	}

	mutex_lock(&mb1_transfer.lock);

	/* Ensure MB1 is in Idle state */
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM */
	req->service_id = U9540_PRCM_MB1H_SERVICE_DVFS;
	req->command_id = U9540_PRCM_MB1H_COMMAND_SET_SAFE_OPP;
	req->data.data = prcmu_opp;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

	/* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/*
	 * Check response from PRCMU. U9540 TCDM does not contain current OPP
	 * so we cannot check its value.
	 */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_DVFS) &&
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_SET_SAFE_OPP) &&
		(ack->status == U9540_PRCM_MB1_STATUS_OK)) {
		mb1_transfer.vsafe_opp = prcmu_opp;
	} else {
		r = -EIO;
		pr_info("PRCMU - bad ack in %s. %u %u %u %u\n", __func__,
		ack->service_id, ack->command_id, ack->status, opp);
	}

	mutex_unlock(&mb1_transfer.lock);

	return r;

#endif
}

/**
 * get_ddr_opp - get the current DDR OPP
 *
 * Returns: the current DDR OPP
 */
int get_ddr_opp(void)
{
	return readb(PRCM_DDR_SUBSYS_APE_MINBW);
}

/**
 * get_ddr1_opp - get the current DDR1 OPP
 *
 * Returns: the current DDR1 OPP
 */
int get_ddr1_opp(void)
{
	return readb(PRCM_DDR1_SUBSYS_APE_MINBW);
}

/**
 * set_ddr_opp - set the appropriate DDR OPP
 * @opp: The new DDR operating point to which transition is to be made
 * Returns: 0 on success, non-zero on failure
 *
 * This function sets the operating point of the DDR.
 */
int set_ddr_opp(u8 opp)
{
	if (opp < DDR_100_OPP || opp > DDR_25_OPP)
		return -EINVAL;
	/* Changing the DDR OPP can hang the hardware pre-v21 */
	if (!cpu_is_u8500v20())
		writeb(opp, PRCM_DDR_SUBSYS_APE_MINBW);

	if (cpu_is_u9540())
		writeb(opp, PRCM_DDR1_SUBSYS_APE_MINBW);

	trace_u8500_set_ddr_opp(opp);
	return 0;
}

/* Divide the frequency of certain clocks by 2 for APE_50_PARTLY_25_OPP. */
static void request_even_slower_clocks(bool enable)
{
	void __iomem *clock_reg[] = {
		PRCM_ACLK_MGT,
		PRCM_DMACLK_MGT
	};
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&clk_mgt_lock, flags);

	/* Grab the HW semaphore. */
	while ((readl(PRCM_SEM) & PRCM_SEM_PRCM_SEM) != 0)
		cpu_relax();

	for (i = 0; i < ARRAY_SIZE(clock_reg); i++) {
		u32 val;
		u32 div;

		val = readl(clock_reg[i]);
		div = (val & PRCM_CLK_MGT_CLKPLLDIV_MASK);
		if (enable) {
			if ((div <= 1) || (div > 15)) {
				pr_err("prcmu: Bad clock divider %d in %s\n",
					div, __func__);
				goto unlock_and_return;
			}
			div <<= 1;
		} else {
			if (div <= 2)
				goto unlock_and_return;
			div >>= 1;
		}
		val = ((val & ~PRCM_CLK_MGT_CLKPLLDIV_MASK) |
			(div & PRCM_CLK_MGT_CLKPLLDIV_MASK));
		writel(val, clock_reg[i]);
	}

unlock_and_return:
	/* Release the HW semaphore. */
	writel(0, PRCM_SEM);

	spin_unlock_irqrestore(&clk_mgt_lock, flags);
}

static int db9540_prcmu_write_ape_opp(u8 opp)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int r = 0;
	u32 prcmu_opp;

	switch (opp) {
	case APE_50_OPP:
	case APE_50_PARTLY_25_OPP:
		prcmu_opp = U9540_PRCM_REQ_MB1_APE_OPP_1;
		break;
	case APE_100_OPP:
		prcmu_opp = U9540_PRCM_REQ_MB1_APE_OPP_2;
		break;
	case APE_OPP_INIT:
	case APE_NO_CHANGE:
	default:
		/* Do nothing */
		return 0;
	}

	/* Ensure MB1 is in Idle state */
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM */
	req->service_id = U9540_PRCM_MB1H_SERVICE_DVFS;
	req->command_id = U9540_PRCM_MB1H_COMMAND_SET_APE_OPP;
	req->data.data = prcmu_opp;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

	/* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/*
	 * Check response from PRCMU. U9540 TCDM does not contain current OPP
	 * so we cannot check its value.
	 */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_DVFS) &&
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_SET_APE_OPP) &&
		(ack->status == U9540_PRCM_MB1_STATUS_OK)) {
		r = 0;
	} else {
		r = -EIO;
		pr_info("PRCMU - bad ack in %s. %u %u %u %u\n", __func__,
		ack->service_id, ack->command_id, ack->status, opp);
	}

	return r;
}

/**
 * set_ape_opp - set the appropriate APE OPP
 * @opp: The new APE operating point to which transition is to be made
 * Returns: 0 on success, non-zero on failure
 *
 * This function sets the operating point of the APE.
 */
static int set_ape_opp(u8 opp)
{
	int r = 0;

	trace_u8500_set_ape_opp(opp);
	if (opp == mb1_transfer.ape_opp)
		return 0;

	mutex_lock(&mb1_transfer.lock);

	/* Exit APE_50_PARTLY_25_OPP */
	if (mb1_transfer.ape_opp == APE_50_PARTLY_25_OPP)
		request_even_slower_clocks(false);

	if ((opp != APE_100_OPP) && (mb1_transfer.ape_opp != APE_100_OPP))
		goto skip_message;

	dbx500_prcmu_mb1_wait_released();

	r = db9540_prcmu_write_ape_opp(opp);
skip_message:
	if ((!r && (opp == APE_50_PARTLY_25_OPP)) ||
			/* Set APE_50_PARTLY_25_OPP back in case new opp failed */
			(r && (mb1_transfer.ape_opp == APE_50_PARTLY_25_OPP)))
		request_even_slower_clocks(true);
	if (!r)
		mb1_transfer.ape_opp = opp;

	mutex_unlock(&mb1_transfer.lock);

	return r;
}

/**
 * get_ape_opp - get the current APE OPP
 *
 * Returns: the current APE OPP
 */
static int get_ape_opp(void)
{
	u32 val;
	/*
	 * U9540 is not able to read APE OPP value from TCDM. Therefore
	 * determine if the APE OPP has been set, or not.
	 */
	if (mb1_transfer.ape_opp != APE_OPP_INIT)
		return mb1_transfer.ape_opp;

	/*
	 * APE OPP value not yet initialised. Read value from
	 * register.
	 */
	val = readl(PRCM_POWER_STATE_VAL);
	val &= PRCM_POWER_STATE_VAL_VAPE_STATE_OPP_MASK;
	val >>= PRCM_POWER_STATE_VAL_VAPE_STATE_OPP_SHIFT;
	switch (val) {
	case 0x00:
		return APE_100_OPP;
	case 0x01:
		return APE_50_OPP;
	default:
		pr_warn("prcmu: %s Unknown APE OPP val %d\n", __func__, val);
		return APE_OPP_INIT;
	}
}

/**
 * request_ape_opp_100_voltage - Request APE OPP 100% voltage
 * @enable: true to request the higher voltage, false to drop a request.
 *
 * Calls to this function to enable and disable requests must be balanced.
 * Not supported by U9540
 */
static int request_ape_opp_100_voltage(bool enable)
{
	pr_debug("prcmu: %s not supported\n", __func__);
	return 0;
}


static int db9540_prcmu_release_usb_wakeup_state(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int r = 0;

	/* Ensure MB1 is in Idle state */
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM */
	req->service_id = U9540_PRCM_MB1H_SERVICE_USB;
	req->command_id = U9540_PRCM_MB1H_COMMAND_USB_WAKEUP_REL;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

	/* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/* Check response from PRCMU */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_USB) &&
			(ack->command_id ==
			 U9540_PRCM_MB1H_COMMAND_USB_WAKEUP_REL) &&
			(ack->status == U9540_PRCM_MB1_STATUS_OK)) {
		r = 0;
	} else {
		r = -EIO;
		pr_info("PRCMU - bad ack in %s. %u %u %u\n", __func__,
				ack->service_id, ack->command_id, ack->status);
	}

	return r;
}

/**
 * dbx540_prcmu_release_usb_wakeup_state - release the state required by a USB wakeup
 *
 * This function releases the power state requirements of a USB wakeup.
 */
int dbx540_prcmu_release_usb_wakeup_state(void)
{
	int r;

	mutex_lock(&mb1_transfer.lock);

	dbx500_prcmu_mb1_wait_released();

	r = db9540_prcmu_release_usb_wakeup_state();
	mutex_unlock(&mb1_transfer.lock);

	return r;
}

static int db9540_request_pll(u8 clock, bool enable)
{
	int r;
	u32 prcmu_clock;
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;

	if (clock == PRCMU_PLLSOC0)
		prcmu_clock = (enable ? U9540_PRCM_REQ_MB1_PLL_SOC0_ON :
				U9540_PRCM_REQ_MB1_PLL_SOC0_OFF);
	else if (clock == PRCMU_PLLSOC1)
		prcmu_clock = (enable ? U9540_PRCM_REQ_MB1_PLL_SOC1_ON :
				U9540_PRCM_REQ_MB1_PLL_SOC1_OFF);

	/* Ensure MB1 is in Idle state */
	db9540_prcmu_mb1_wait_for_idle();

	/* Write to TCDM */
	req->service_id = U9540_PRCM_MB1H_SERVICE_CLOCK;
	req->command_id = U9540_PRCM_MB1H_COMMAND_PLL_ON_OFF;
	req->data.data = prcmu_clock;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/* Check response from PRCMU */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_CLOCK) &&
			(ack->command_id == U9540_PRCM_MB1H_COMMAND_PLL_ON_OFF)
			&& (ack->status == U9540_PRCM_MB1_STATUS_OK))
		r = 0;
	else {
		r = -EIO;
		pr_info("PRCMU - bad ack in %s. %u %u %u\n", __func__,
				ack->service_id, ack->command_id, ack->status);
	}

	return r;
}

static int request_pll(u8 clock, bool enable)
{
	int r;

	if (clock != PRCMU_PLLSOC1)
		return -EINVAL;

	mutex_lock(&mb1_transfer.lock);

	dbx500_prcmu_mb1_wait_released();

	r = db9540_request_pll(clock, enable);

	mutex_unlock(&mb1_transfer.lock);

	return r;
}

/**
 * set_epod - set the state of a EPOD (power domain)
 * @epod_id: The EPOD to set
 * @epod_state: The new EPOD state
 *
 * This function sets the state of a EPOD (power domain). It may not be called
 * from interrupt context.
 */
int set_epod(u16 epod_id, u8 epod_state)
{
	int r = 0;
	bool ram_retention = false;
	int i;

	/* check argument */
	BUG_ON(epod_id >= NUM_EPOD_ID);

	/* set flag if retention is possible */
	switch (epod_id) {
	case EPOD_ID_SVAMMDSP:
	case EPOD_ID_SIAMMDSP:
	case EPOD_ID_ESRAM12:
	case EPOD_ID_ESRAM34:
		ram_retention = true;
		break;
	}

	/* check argument */
	BUG_ON(epod_state > EPOD_STATE_ON);
	BUG_ON(epod_state == EPOD_STATE_RAMRET && !ram_retention);

	trace_u8500_set_epod(epod_id, epod_state);
	/* get lock */
	mutex_lock(&mb2_transfer.lock);

	/* wait for mailbox */
	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(2))
		cpu_relax();

	/* fill in mailbox */
	for (i = 0; i < NUM_EPOD_ID; i++)
		writeb(EPOD_STATE_NO_CHANGE, (tcdm_base + PRCM_REQ_MB2 + i));
	writeb(epod_state, (tcdm_base + PRCM_REQ_MB2 + epod_id));

	writeb(MB2H_DPS, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB2));

	writel(MBOX_BIT(2), PRCM_MBOX_CPU_SET);

	/*
	 * The current firmware version does not handle errors correctly,
	 * and we cannot recover if there is an error.
	 * This is expected to change when the firmware is updated.
	 */
	if (!wait_for_completion_timeout(&mb2_transfer.work,
			msecs_to_jiffies(20000))) {
		pr_err("prcmu: %s timed out (20 s) waiting for a reply.\n",
			__func__);
		r = -EIO;
		dbx540_prcmu_debug_dump(__func__, true, true);
		goto unlock_and_return;
	}

	if (mb2_transfer.ack.status != HWACC_PWR_ST_OK)
		r = -EIO;

unlock_and_return:
	mutex_unlock(&mb2_transfer.lock);
	return r;
}

/**
 * configure_auto_pm - Configure autonomous power management.
 * @sleep: Configuration for ApSleep.
 * @idle:  Configuration for ApIdle.
 */
static void configure_auto_pm(struct prcmu_auto_pm_config *sleep,
	struct prcmu_auto_pm_config *idle)
{
	u32 sleep_cfg;
	u32 idle_cfg;
	unsigned long flags;

	BUG_ON((sleep == NULL) || (idle == NULL));

	sleep_cfg = (sleep->sva_auto_pm_enable & 0xF);
	sleep_cfg = ((sleep_cfg << 4) | (sleep->sia_auto_pm_enable & 0xF));
	sleep_cfg = ((sleep_cfg << 8) | (sleep->sva_power_on & 0xFF));
	sleep_cfg = ((sleep_cfg << 8) | (sleep->sia_power_on & 0xFF));
	sleep_cfg = ((sleep_cfg << 4) | (sleep->sva_policy & 0xF));
	sleep_cfg = ((sleep_cfg << 4) | (sleep->sia_policy & 0xF));

	idle_cfg = (idle->sva_auto_pm_enable & 0xF);
	idle_cfg = ((idle_cfg << 4) | (idle->sia_auto_pm_enable & 0xF));
	idle_cfg = ((idle_cfg << 8) | (idle->sva_power_on & 0xFF));
	idle_cfg = ((idle_cfg << 8) | (idle->sia_power_on & 0xFF));
	idle_cfg = ((idle_cfg << 4) | (idle->sva_policy & 0xF));
	idle_cfg = ((idle_cfg << 4) | (idle->sia_policy & 0xF));

	spin_lock_irqsave(&mb2_transfer.auto_pm_lock, flags);

	/*
	 * The autonomous power management configuration is done through
	 * fields in mailbox 2, but these fields are only used as shared
	 * variables - i.e. there is no need to send a message.
	 */
	writel(sleep_cfg, (tcdm_base + PRCM_REQ_MB2_AUTO_PM_SLEEP));
	writel(idle_cfg, (tcdm_base + PRCM_REQ_MB2_AUTO_PM_IDLE));

	mb2_transfer.auto_pm_enabled =
		((sleep->sva_auto_pm_enable == PRCMU_AUTO_PM_ON) ||
		 (sleep->sia_auto_pm_enable == PRCMU_AUTO_PM_ON) ||
		 (idle->sva_auto_pm_enable == PRCMU_AUTO_PM_ON) ||
		 (idle->sia_auto_pm_enable == PRCMU_AUTO_PM_ON));

	spin_unlock_irqrestore(&mb2_transfer.auto_pm_lock, flags);
}

static int request_sysclk(bool enable)
{
	int r = 0;
	unsigned long flags;

	mutex_lock(&mb3_transfer.sysclk_lock);

	spin_lock_irqsave(&mb3_transfer.lock, flags);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(3))
		cpu_relax();

	writeb((enable ? ON : OFF), (tcdm_base + PRCM_REQ_MB3_SYSCLK_MGT));

	writeb(MB3H_SYSCLK, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB3));
	writel(MBOX_BIT(3), PRCM_MBOX_CPU_SET);

	spin_unlock_irqrestore(&mb3_transfer.lock, flags);

	/*
	 * The firmware only sends an ACK if we want to enable the
	 * SysClk, and it succeeds.
	 */
	if (enable && !wait_for_completion_timeout(&mb3_transfer.sysclk_work,
			msecs_to_jiffies(20000))) {
		pr_err("prcmu: %s timed out (20 s) waiting for a reply.\n",
			__func__);
		r = -EIO;
		dbx540_prcmu_debug_dump(__func__, true, true);
	}

	mutex_unlock(&mb3_transfer.sysclk_lock);

	return r;
}

static int request_timclk(bool enable)
{
	u32 val = (PRCM_TCR_DOZE_MODE | PRCM_TCR_TENSEL_MASK);

	if (!enable)
		val |= PRCM_TCR_STOP_TIMERS;
	writel(val, PRCM_TCR);

	return 0;
}

static int request_clock(u8 clock, bool enable)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&clk_mgt_lock, flags);

	/* Grab the HW semaphore. */
	while ((readl(PRCM_SEM) & PRCM_SEM_PRCM_SEM) != 0)
		cpu_relax();

	val = readl(clk_mgt[clock].reg);
	if (enable) {
		val |= (PRCM_CLK_MGT_CLKEN | clk_mgt[clock].pllsw);
	} else {
		clk_mgt[clock].pllsw = (val & PRCM_CLK_MGT_CLKPLLSW_MASK);
		val &= ~(PRCM_CLK_MGT_CLKEN | PRCM_CLK_MGT_CLKPLLSW_MASK);
	}
	writel(val, clk_mgt[clock].reg);

	/* Release the HW semaphore. */
	writel(0, PRCM_SEM);

	spin_unlock_irqrestore(&clk_mgt_lock, flags);

	return 0;
}

static int request_sga_clock(u8 clock, bool enable)
{
	u32 val;
	int ret;

	if (enable) {
		val = readl(PRCM_CGATING_BYPASS);
		writel(val | PRCM_CGATING_BYPASS_ICN2, PRCM_CGATING_BYPASS);
	}

	ret = request_clock(clock, enable);

	if (!ret && !enable) {
		val = readl(PRCM_CGATING_BYPASS);
		writel(val & ~PRCM_CGATING_BYPASS_ICN2, PRCM_CGATING_BYPASS);
	}

	return ret;
}

static inline bool plldsi_tv_locked(void)
{
	return (readl(PRCM_PLLDSITV_LOCKP) &
		(PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP10 |
		 PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP3)) ==
		(PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP10 |
		 PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP3);
}

static inline bool plldsi_lcd_locked(void)
{
	return (readl(PRCM_PLLDSILCD_LOCKP) &
		(PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP10 |
		 PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP3)) ==
		(PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP10 |
		 PRCM_PLLDSI_LOCKP_PRCM_PLLDSI_LOCKP3);
}

static int request_plldsi(bool enable, bool lcd)
{
	int r = 0;
	u32 val;
	void __iomem *pll_dsi_enable_reg;
	u32 pll_dsi_resetn_bit;
	bool (*plldsi_locked)(void);

	if (lcd) {
		pll_dsi_enable_reg = PRCM_PLLDSILCD_ENABLE;
		pll_dsi_resetn_bit = PRCM_APE_RESETN_DSIPLL_LCD_RESETN;
		plldsi_locked = plldsi_lcd_locked;
	} else {
		pll_dsi_enable_reg = PRCM_PLLDSITV_ENABLE;
		pll_dsi_resetn_bit = PRCM_APE_RESETN_DSIPLL_TV_RESETN;
		plldsi_locked = plldsi_tv_locked;
	}

	if (enable) {
		/* Only clamp for enable if both are unlocked */
		if (!plldsi_lcd_locked() && !plldsi_tv_locked())
			writel((PRCM_MMIP_LS_CLAMP_DSIPLL_CLAMP |
				PRCM_MMIP_LS_CLAMP_DSIPLL_CLAMPI),
							PRCM_MMIP_LS_CLAMP_CLR);
	} else {
		/* Only clamp for disable if one are locked */
		bool tv_locked = plldsi_tv_locked();
		bool lcd_locked = plldsi_lcd_locked();
		if ((!lcd_locked && tv_locked) || (lcd_locked && !tv_locked))
			writel((PRCM_MMIP_LS_CLAMP_DSIPLL_CLAMP |
				PRCM_MMIP_LS_CLAMP_DSIPLL_CLAMPI),
							PRCM_MMIP_LS_CLAMP_SET);
	}

	val = readl(pll_dsi_enable_reg);
	if (enable)
		val |= PRCM_PLLDSI_ENABLE_PRCM_PLLDSI_ENABLE;
	else
		val &= ~PRCM_PLLDSI_ENABLE_PRCM_PLLDSI_ENABLE;
	writel(val, pll_dsi_enable_reg);

	if (enable) {
		unsigned int i;
		bool locked = plldsi_locked();

		for (i = 10; !locked && (i > 0); --i) {
			udelay(100);
			locked = plldsi_locked();
		}
		if (locked) {
			writel(pll_dsi_resetn_bit,
				PRCM_APE_RESETN_SET);
		} else {
			writel((PRCM_MMIP_LS_CLAMP_DSIPLL_CLAMP |
				PRCM_MMIP_LS_CLAMP_DSIPLL_CLAMPI),
				PRCM_MMIP_LS_CLAMP_SET);
			val &= ~PRCM_PLLDSI_ENABLE_PRCM_PLLDSI_ENABLE;
			writel(val, pll_dsi_enable_reg);
			r = -EAGAIN;
		}
	} else {
		writel(pll_dsi_resetn_bit, PRCM_APE_RESETN_CLR);
	}
	return r;
}
#define NO_LCD false
#define LCD true

static int request_dsiclk(u8 n, bool enable, bool lcd)
{
	u32 val;
	struct dsiclk *dsiclk;

	dsiclk = u9540_dsiclk;

	val = readl(PRCM_DSI_PLLOUT_SEL);
	val &= ~dsiclk[n].divsel_mask;
	val |= ((enable ? dsiclk[n].divsel : PRCM_DSI_PLLOUT_SEL_OFF) <<
			dsiclk[n].divsel_shift);
	if (lcd)
		val |= dsiclk[n].divsel_lcd_mask;
	writel(val, PRCM_DSI_PLLOUT_SEL);
	return 0;
}

static int request_dsiescclk(u8 n, bool enable)
{
	u32 val;

	val = readl(PRCM_DSITVCLK_DIV);
	enable ? (val |= dsiescclk[n].en) : (val &= ~dsiescclk[n].en);
	writel(val, PRCM_DSITVCLK_DIV);
	return 0;
}

/**
 * dbx540_request_clock() - Request for a clock to be enabled or disabled.
 * @clock:      The clock for which the request is made.
 * @enable:     Whether the clock should be enabled (true) or disabled (false).
 *
 * This function should only be used by the clock implementation.
 * Do not use it from any other place!
 */
static int dbx540_prcmu_request_clock(u8 clock, bool enable)
{
	trace_u8500_request_clock(clock, enable);
	if (clock == PRCMU_SGACLK)
		return request_sga_clock(clock, enable);
	else if (clock < PRCMU_NUM_REG_CLOCKS)
		return request_clock(clock, enable);
	else if (clock == PRCMU_TIMCLK)
		return request_timclk(enable);
	else if ((clock == PRCMU_DSI0CLK) || (clock == PRCMU_DSI1CLK))
		return request_dsiclk((clock - PRCMU_DSI0CLK), enable, NO_LCD);
	else if ((PRCMU_DSI0ESCCLK <= clock) && (clock <= PRCMU_DSI2ESCCLK))
		return request_dsiescclk((clock - PRCMU_DSI0ESCCLK), enable);
	else if (clock == PRCMU_PLLDSI)
		return request_plldsi(enable, false);
	else if ((clock == PRCMU_DSI0CLK_LCD) || (clock == PRCMU_DSI1CLK_LCD))
		return request_dsiclk((clock - PRCMU_DSI0CLK_LCD),
			enable, LCD);
	else if (clock == PRCMU_PLLDSI_LCD)
		return request_plldsi(enable, true);
	else if (clock == PRCMU_SYSCLK)
		return request_sysclk(enable);
	else if ((clock == PRCMU_PLLSOC0) || (clock == PRCMU_PLLSOC1))
		return request_pll(clock, enable);
	else
		return -EINVAL;
}

static unsigned long pll_rate(void __iomem *reg, unsigned long src_rate,
	int branch)
{
	u64 rate;
	u32 val;
	u32 d;
	u32 div = 1;

	val = readl(reg);

	rate = src_rate;
	rate *= ((val & PRCM_PLL_FREQ_D_MASK) >> PRCM_PLL_FREQ_D_SHIFT);

	d = ((val & PRCM_PLL_FREQ_N_MASK) >> PRCM_PLL_FREQ_N_SHIFT);
	if (d > 1)
		div *= d;

	d = ((val & PRCM_PLL_FREQ_R_MASK) >> PRCM_PLL_FREQ_R_SHIFT);
	if (d > 1)
		div *= d;

	if (val & PRCM_PLL_FREQ_SELDIV2)
		div *= 2;

	if ((branch == PLL_FIX) || ((branch == PLL_DIV) &&
		(val & PRCM_PLL_FREQ_DIV2EN) &&
		((reg == PRCM_PLLSOC0_FREQ) ||
		 (reg == PRCM_PLLDDR_FREQ))))
		div *= 2;

	(void)do_div(rate, div);

	return (unsigned long)rate;
}

#define ROOT_CLOCK_RATE 38400000

static unsigned long clock_rate(u8 clock)
{
	u32 val;
	u32 pllsw;
	unsigned long rate = ROOT_CLOCK_RATE;

	val = readl(clk_mgt[clock].reg);

	if (val & PRCM_CLK_MGT_CLK38) {
		if (clk_mgt[clock].clk38div && (val & PRCM_CLK_MGT_CLK38DIV))
			rate /= 2;
		return rate;
	}

	val |= clk_mgt[clock].pllsw;
	pllsw = (val & PRCM_CLK_MGT_CLKPLLSW_MASK);

	if (pllsw == PRCM_CLK_MGT_CLKPLLSW_SOC0)
		rate = pll_rate(PRCM_PLLSOC0_FREQ, rate, clk_mgt[clock].branch);
	else if (pllsw == PRCM_CLK_MGT_CLKPLLSW_SOC1)
		rate = pll_rate(PRCM_PLLSOC1_FREQ, rate, clk_mgt[clock].branch);
	else if (pllsw == PRCM_CLK_MGT_CLKPLLSW_DDR)
		rate = pll_rate(PRCM_PLLDDR_FREQ, rate, clk_mgt[clock].branch);
	else
		return 0;

	if ((clock == PRCMU_SGACLK) &&
		(val & PRCM_SGACLK_MGT_SGACLKDIV_BY_2_5_EN)) {
		u64 r = (rate * 10);

		(void)do_div(r, 25);
		return (unsigned long)r;
	}
	val &= PRCM_CLK_MGT_CLKPLLDIV_MASK;
	if (val)
		return rate / val;
	else
		return 0;
}

static unsigned long armss_rate(void)
{
	return latest_armss_rate;
}

static unsigned long dsiclk_rate(u8 n, bool lcd)
{
	u32 divsel;
	u32 div = 1;
	struct dsiclk *dsiclk;

	dsiclk = u9540_dsiclk;

	divsel = readl(PRCM_DSI_PLLOUT_SEL);
	divsel = ((divsel & dsiclk[n].divsel_mask) >> dsiclk[n].divsel_shift);

	if (divsel == PRCM_DSI_PLLOUT_SEL_OFF)
		divsel = dsiclk[n].divsel;

	switch (divsel) {
	case PRCM_DSI_PLLOUT_SEL_PHI_4:
		div *= 2;
	case PRCM_DSI_PLLOUT_SEL_PHI_2:
		div *= 2;
	case PRCM_DSI_PLLOUT_SEL_PHI:
		if (lcd)
			return pll_rate(PRCM_PLLDSILCD_FREQ,
					clock_rate(PRCMU_SPARE1CLK), PLL_RAW) / div;
		else
			return pll_rate(PRCM_PLLDSITV_FREQ,
					clock_rate(PRCMU_HDMICLK), PLL_RAW) / div;
	default:
		return 0;
	}
}

static unsigned long dsiescclk_rate(u8 n)
{
	u32 div;

	div = readl(PRCM_DSITVCLK_DIV);
	div = ((div & dsiescclk[n].div_mask) >> (dsiescclk[n].div_shift));
	return clock_rate(PRCMU_TVCLK) / max((u32)1, div);
}

static unsigned long dbx540_prcmu_clock_rate(u8 clock)
{
	if (clock < PRCMU_NUM_REG_CLOCKS)
		return clock_rate(clock);
	else if (clock == PRCMU_TIMCLK)
		return ROOT_CLOCK_RATE / 16;
	else if (clock == PRCMU_SYSCLK)
		return ROOT_CLOCK_RATE;
	else if (clock == PRCMU_PLLSOC0)
		return pll_rate(PRCM_PLLSOC0_FREQ, ROOT_CLOCK_RATE, PLL_RAW);
	else if (clock == PRCMU_PLLSOC1)
		return pll_rate(PRCM_PLLSOC1_FREQ, ROOT_CLOCK_RATE, PLL_RAW);
	else if (clock == PRCMU_PLLDDR)
		return pll_rate(PRCM_PLLDDR_FREQ, ROOT_CLOCK_RATE, PLL_RAW);
	else if (clock == PRCMU_PLLDSI)
		return pll_rate(PRCM_PLLDSITV_FREQ, clock_rate(PRCMU_HDMICLK),
			PLL_RAW);
	else if (clock == PRCMU_ARMSS)
		return KHZ_TO_HZ(armss_rate());
	else if (clock == PRCMU_ARMCLK)
		return KHZ_TO_HZ(get_arm_freq());
	else if ((clock == PRCMU_DSI0CLK) || (clock == PRCMU_DSI1CLK))
		return dsiclk_rate(clock - PRCMU_DSI0CLK, false);
	else if ((PRCMU_DSI0ESCCLK <= clock) && (clock <= PRCMU_DSI2ESCCLK))
		return dsiescclk_rate(clock - PRCMU_DSI0ESCCLK);
	else if (clock == PRCMU_PLLDSI_LCD)
		return pll_rate(PRCM_PLLDSILCD_FREQ,
					clock_rate(PRCMU_SPARE1CLK), PLL_RAW);
	else if ((clock == PRCMU_DSI0CLK_LCD) || (clock == PRCMU_DSI1CLK_LCD))
		return dsiclk_rate(clock - PRCMU_DSI0CLK_LCD, true);
	else
		return 0;
}

static unsigned long clock_source_rate(u32 clk_mgt_val, int branch)
{
	if (clk_mgt_val & PRCM_CLK_MGT_CLK38)
		return ROOT_CLOCK_RATE;
	clk_mgt_val &= PRCM_CLK_MGT_CLKPLLSW_MASK;
	if (clk_mgt_val == PRCM_CLK_MGT_CLKPLLSW_SOC0)
		return pll_rate(PRCM_PLLSOC0_FREQ, ROOT_CLOCK_RATE, branch);
	else if (clk_mgt_val == PRCM_CLK_MGT_CLKPLLSW_SOC1)
		return pll_rate(PRCM_PLLSOC1_FREQ, ROOT_CLOCK_RATE, branch);
	else if (clk_mgt_val == PRCM_CLK_MGT_CLKPLLSW_DDR)
		return pll_rate(PRCM_PLLDDR_FREQ, ROOT_CLOCK_RATE, branch);
	else
		return 0;
}

static u32 clock_divider(unsigned long src_rate, unsigned long rate)
{
	u32 div;

	div = (src_rate / rate);
	if (div == 0)
		return 1;
	if (rate < (src_rate / div))
		div++;
	return div;
}

static long round_clock_rate(u8 clock, unsigned long rate)
{
	u32 val;
	u32 div;
	unsigned long src_rate;
	long rounded_rate;

	val = readl(clk_mgt[clock].reg);
	src_rate = clock_source_rate((val | clk_mgt[clock].pllsw),
		clk_mgt[clock].branch);
	div = clock_divider(src_rate, rate);
	if (val & PRCM_CLK_MGT_CLK38) {
		if (clk_mgt[clock].clk38div) {
			if (div > 2)
				div = 2;
		} else {
			div = 1;
		}
	} else if ((clock == PRCMU_SGACLK) && (div == 3)) {
		u64 r = (src_rate * 10);

		(void)do_div(r, 25);
		if (r <= rate)
			return (unsigned long)r;
	}
	rounded_rate = (src_rate / min(div, (u32)31));

	return rounded_rate;
}

#define MIN_PLL_VCO_RATE 600000000ULL
#define MAX_PLL_VCO_RATE 1680640000ULL

static long round_plldsi_rate(unsigned long rate)
{
	long rounded_rate = 0;
	unsigned long src_rate;
	unsigned long rem;
	u32 r;

	src_rate = clock_rate(PRCMU_HDMICLK);
	rem = rate;

	for (r = 7; (rem > 0) && (r > 0); r--) {
		u64 d;

		d = (r * rate);
		(void)do_div(d, src_rate);
		if (d < 6)
			d = 6;
		else if (d > 255)
			d = 255;
		d *= src_rate;
		if (((2 * d) < (r * MIN_PLL_VCO_RATE)) ||
			((r * MAX_PLL_VCO_RATE) < (2 * d)))
			continue;
		(void)do_div(d, r);
		if (rate < d) {
			if (rounded_rate == 0)
				rounded_rate = (long)d;
			break;
		}
		if ((rate - d) < rem) {
			rem = (rate - d);
			rounded_rate = (long)d;
		}
	}
	return rounded_rate;
}

static long round_dsiclk_rate(unsigned long rate, bool lcd)
{
	u32 div;
	unsigned long src_rate;
	long rounded_rate;

	if (lcd)
		src_rate = pll_rate(PRCM_PLLDSILCD_FREQ,
			clock_rate(PRCMU_SPARE1CLK), PLL_RAW);
	else
		src_rate = pll_rate(PRCM_PLLDSITV_FREQ,
			clock_rate(PRCMU_HDMICLK), PLL_RAW);
	div = clock_divider(src_rate, rate);
	rounded_rate = (src_rate / ((div > 2) ? 4 : div));

	return rounded_rate;
}

static long round_dsiescclk_rate(unsigned long rate)
{
	u32 div;
	unsigned long src_rate;
	long rounded_rate;

	src_rate = clock_rate(PRCMU_TVCLK);
	div = clock_divider(src_rate, rate);
	rounded_rate = (src_rate / min(div, (u32)255));

	return rounded_rate;
}

static long dbx540_prcmu_round_clock_rate(u8 clock, unsigned long rate)
{
	if (clock < PRCMU_NUM_REG_CLOCKS)
		return round_clock_rate(clock, rate);
	else if (clock == PRCMU_PLLDSI)
		return round_plldsi_rate(rate);
	else if ((clock == PRCMU_DSI0CLK) || (clock == PRCMU_DSI1CLK))
		return round_dsiclk_rate(rate, false);
	else if ((PRCMU_DSI0ESCCLK <= clock) && (clock <= PRCMU_DSI2ESCCLK))
		return round_dsiescclk_rate(rate);
	else if (clock == PRCMU_PLLDSI_LCD)
		return round_plldsi_rate(rate);
	else if ((clock == PRCMU_DSI0CLK_LCD) || (clock == PRCMU_DSI1CLK_LCD))
		return round_dsiclk_rate(rate, true);
	else
		return (long)prcmu_clock_rate(clock);
}

static void set_clock_rate(u8 clock, unsigned long rate)
{
	u32 val;
	u32 div;
	unsigned long src_rate;
	unsigned long flags;

	spin_lock_irqsave(&clk_mgt_lock, flags);

	/* Grab the HW semaphore. */
	while ((readl(PRCM_SEM) & PRCM_SEM_PRCM_SEM) != 0)
		cpu_relax();

	val = readl(clk_mgt[clock].reg);
	src_rate = clock_source_rate((val | clk_mgt[clock].pllsw),
		clk_mgt[clock].branch);
	div = clock_divider(src_rate, rate);
	if (val & PRCM_CLK_MGT_CLK38) {
		if (clk_mgt[clock].clk38div) {
			if (div > 1)
				val |= PRCM_CLK_MGT_CLK38DIV;
			else
				val &= ~PRCM_CLK_MGT_CLK38DIV;
		}
	} else if (clock == PRCMU_SGACLK) {
		val &= ~(PRCM_CLK_MGT_CLKPLLDIV_MASK |
			PRCM_SGACLK_MGT_SGACLKDIV_BY_2_5_EN);
		if (div == 3) {
			u64 r = (src_rate * 10);

			(void)do_div(r, 25);
			if (r <= rate) {
				val |= PRCM_SGACLK_MGT_SGACLKDIV_BY_2_5_EN;
				div = 0;
			}
		}
		val |= min(div, (u32)31);
	} else {
		val &= ~PRCM_CLK_MGT_CLKPLLDIV_MASK;
		val |= min(div, (u32)31);
	}
	writel(val, clk_mgt[clock].reg);

	/* Release the HW semaphore. */
	writel(0, PRCM_SEM);

	spin_unlock_irqrestore(&clk_mgt_lock, flags);
}

static int set_plldsi_rate(unsigned long rate, bool lcd)
{
	unsigned long src_rate;
	unsigned long rem;
	u32 pll_freq = 0;
	u32 r;

	if (lcd)
		src_rate = clock_rate(PRCMU_SPARE1CLK);
	else
		src_rate = clock_rate(PRCMU_HDMICLK);

	rem = rate;

	for (r = 7; (rem > 0) && (r > 0); r--) {
		u64 d;
		u64 hwrate;

		d = (r * rate);
		(void)do_div(d, src_rate);
		if (d < 6)
			d = 6;
		else if (d > 255)
			d = 255;
		hwrate = (d * src_rate);
		if (((2 * hwrate) < (r * MIN_PLL_VCO_RATE)) ||
			((r * MAX_PLL_VCO_RATE) < (2 * hwrate)))
			continue;
		(void)do_div(hwrate, r);
		if (rate < hwrate) {
			if (pll_freq == 0)
				pll_freq = (((u32)d << PRCM_PLL_FREQ_D_SHIFT) |
					(r << PRCM_PLL_FREQ_R_SHIFT));
			break;
		}
		if ((rate - hwrate) < rem) {
			rem = (rate - hwrate);
			pll_freq = (((u32)d << PRCM_PLL_FREQ_D_SHIFT) |
				(r << PRCM_PLL_FREQ_R_SHIFT));
		}
	}
	if (pll_freq == 0)
		return -EINVAL;

	pll_freq |= (1 << PRCM_PLL_FREQ_N_SHIFT);
	writel(pll_freq, lcd ? PRCM_PLLDSILCD_FREQ : PRCM_PLLDSITV_FREQ);

	return 0;
}

static void set_dsiclk_rate(u8 n, unsigned long rate, bool lcd)
{
	unsigned long src_rate;
	u32 val;
	u32 div;
	struct dsiclk *dsiclk;

	dsiclk = u9540_dsiclk;

	if (lcd)
		src_rate = clock_rate(PRCMU_SPARE1CLK);
	else
		src_rate = clock_rate(PRCMU_HDMICLK);

	div = clock_divider(pll_rate(
				lcd ? PRCM_PLLDSILCD_FREQ : PRCM_PLLDSITV_FREQ,
				src_rate, PLL_RAW), rate);

	dsiclk[n].divsel = (div == 1) ? PRCM_DSI_PLLOUT_SEL_PHI :
		(div == 2) ? PRCM_DSI_PLLOUT_SEL_PHI_2 :
		/* else */	PRCM_DSI_PLLOUT_SEL_PHI_4;

	val = readl(PRCM_DSI_PLLOUT_SEL);
	val &= ~dsiclk[n].divsel_mask;
	val |= (dsiclk[n].divsel << dsiclk[n].divsel_shift);
	if (lcd)
		val |= dsiclk[n].divsel_lcd_mask;
	writel(val, PRCM_DSI_PLLOUT_SEL);
}

static void set_dsiescclk_rate(u8 n, unsigned long rate)
{
	u32 val;
	u32 div;

	div = clock_divider(clock_rate(PRCMU_TVCLK), rate);
	val = readl(PRCM_DSITVCLK_DIV);
	val &= ~dsiescclk[n].div_mask;
	val |= (min(div, (u32)255) << dsiescclk[n].div_shift);
	writel(val, PRCM_DSITVCLK_DIV);
}

static int dbx540_prcmu_set_clock_rate(u8 clock, unsigned long rate)
{
	if (clock < PRCMU_NUM_REG_CLOCKS)
		set_clock_rate(clock, rate);
	else if (clock == PRCMU_PLLDSI)
		return set_plldsi_rate(rate, false);
	else if (clock == PRCMU_ARMCLK)
		return set_arm_freq(HZ_TO_KHZ(rate));
	else if ((clock == PRCMU_DSI0CLK) || (clock == PRCMU_DSI1CLK))
		set_dsiclk_rate((clock - PRCMU_DSI0CLK), rate, false);
	else if ((PRCMU_DSI0ESCCLK <= clock) && (clock <= PRCMU_DSI2ESCCLK))
		set_dsiescclk_rate((clock - PRCMU_DSI0ESCCLK), rate);
	else if (clock == PRCMU_PLLDSI_LCD)
		return set_plldsi_rate(rate, true);
	else if ((clock == PRCMU_DSI0CLK_LCD) || (clock == PRCMU_DSI1CLK_LCD))
		set_dsiclk_rate((clock - PRCMU_DSI0CLK_LCD), rate, true);
	trace_u8500_set_clock_rate(clock, rate);
	return 0;
}

static int config_esram0_deep_sleep(u8 state)
{
	if ((state > ESRAM0_DEEP_SLEEP_STATE_RET) ||
	    (state < ESRAM0_DEEP_SLEEP_STATE_OFF))
		return -EINVAL;

	mutex_lock(&mb4_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writeb(MB4H_MEM_ST, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB4));
	writeb(((DDR_PWR_STATE_OFFHIGHLAT << 4) | DDR_PWR_STATE_ON),
	       (tcdm_base + PRCM_REQ_MB4_DDR_ST_AP_SLEEP_IDLE));
	writeb(DDR_PWR_STATE_ON,
	       (tcdm_base + PRCM_REQ_MB4_DDR_ST_AP_DEEP_IDLE));
	writeb(state, (tcdm_base + PRCM_REQ_MB4_ESRAM0_ST));

	writel(MBOX_BIT(4), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb4_transfer.work, MB4TIM)== 0);

	mutex_unlock(&mb4_transfer.lock);

	return 0;
}

static int config_hotdog(u8 threshold)
{
	mutex_lock(&mb4_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writeb(threshold, (tcdm_base + PRCM_REQ_MB4_HOTDOG_THRESHOLD));
	writeb(MB4H_HOTDOG, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB4));

	writel(MBOX_BIT(4), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb4_transfer.work, MB4TIM)== 0);

	mutex_unlock(&mb4_transfer.lock);

	return 0;
}

static int config_hotmon(u8 low, u8 high)
{
	mutex_lock(&mb4_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writeb(low, (tcdm_base + PRCM_REQ_MB4_HOTMON_LOW));
	writeb(high, (tcdm_base + PRCM_REQ_MB4_HOTMON_HIGH));
	writeb((HOTMON_CONFIG_LOW | HOTMON_CONFIG_HIGH),
		(tcdm_base + PRCM_REQ_MB4_HOTMON_CONFIG));
	writeb(MB4H_HOTMON, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB4));

	writel(MBOX_BIT(4), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb4_transfer.work, MB4TIM)== 0);

	mutex_unlock(&mb4_transfer.lock);

	return 0;
}

static int config_hot_period(u16 val)
{
	mutex_lock(&mb4_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writew(val, (tcdm_base + PRCM_REQ_MB4_HOT_PERIOD));
	writeb(MB4H_HOT_PERIOD, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB4));

	writel(MBOX_BIT(4), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb4_transfer.work, MB4TIM)== 0);

	mutex_unlock(&mb4_transfer.lock);

	return 0;
}

static int start_temp_sense(u16 cycles32k)
{
	if (cycles32k == 0xFFFF)
		return -EINVAL;

	return config_hot_period(cycles32k);
}

static int stop_temp_sense(void)
{
	return config_hot_period(0xFFFF);
}

/**
* db9540_prcmu_thsensor_get_temp - get AP9540 currrent temperature
* Returns: current temperature value on success, non-zero on failure
*/
static int thsensor_get_temp(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;
	int val = 0;

	/* Write to TCDM (header and req_state) */
        db9540_prcmu_mb1_wait_for_idle();
        req->service_id = U9540_PRCM_MB1H_SERVICE_THSENSOR;
        req->command_id = U9540_PRCM_MB1H_COMMAND_THSENSOR_GET_TEMP;
        req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;

        /* Set interrupt ARM -> PRCMU */
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/* Check response from PRCMU */
	if ((ack->service_id == U9540_PRCM_MB1H_SERVICE_THSENSOR) &&
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_THSENSOR_GET_TEMP)
		&& (ack->status == U9540_PRCM_MB1_STATUS_OK)) {
		pr_debug("PRCMU sensor read: %d\n", ack->sensor_read);
		val = ack->sensor_read;
	} else {
		pr_info("PRCMU - bad ack in %s. %u %u %u %u\n", __func__,
		ack->service_id, ack->command_id,
		ack->status, ack->sensor_read);
		return -EIO;
	}

	return val;
}

static int prcmu_a9wdog(u8 cmd, u8 d0, u8 d1, u8 d2, u8 d3)
{
	trace_u8500_a9_wdog(cmd, d0, d1, d2, d3);

	mutex_lock(&mb4_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writeb(d0, (tcdm_base + PRCM_REQ_MB4_A9WDOG_0));
	writeb(d1, (tcdm_base + PRCM_REQ_MB4_A9WDOG_1));
	writeb(d2, (tcdm_base + PRCM_REQ_MB4_A9WDOG_2));
	writeb(d3, (tcdm_base + PRCM_REQ_MB4_A9WDOG_3));

	writeb(cmd, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB4));

	writel(MBOX_BIT(4), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb4_transfer.work, MB4TIM)== 0);

	mutex_unlock(&mb4_transfer.lock);

	return 0;

}

static int config_a9wdog(u8 num, bool sleep_auto_off)
{
	BUG_ON(num == 0 || num > 0xf);
	return prcmu_a9wdog(MB4H_A9WDOG_CONF, num, 0, 0,
			    sleep_auto_off ? A9WDOG_AUTO_OFF_EN :
			    A9WDOG_AUTO_OFF_DIS);
}

static int enable_a9wdog(u8 id)
{
	return prcmu_a9wdog(MB4H_A9WDOG_EN, id, 0, 0, 0);
}

static int disable_a9wdog(u8 id)
{
	return prcmu_a9wdog(MB4H_A9WDOG_DIS, id, 0, 0, 0);
}

static int kick_a9wdog(u8 id)
{
	return prcmu_a9wdog(MB4H_A9WDOG_KICK, id, 0, 0, 0);
}

/*
 * timeout is 28 bit, in ms.
 */
static int load_a9wdog(u8 id, u32 timeout)
{
	return prcmu_a9wdog(MB4H_A9WDOG_LOAD,
			    (id & A9WDOG_ID_MASK) |
			    /*
			     * Put the lowest 28 bits of timeout at
			     * offset 4. Four first bits are used for id.
			     */
			    (u8)((timeout << 4) & 0xf0),
			    (u8)((timeout >> 4) & 0xff),
			    (u8)((timeout >> 12) & 0xff),
			    (u8)((timeout >> 20) & 0xff));
}

/**
 * prcmu_abb_read() - Read register value(s) from the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The read out value(s).
 * @size:	The number of registers to read.
 *
 * Reads register value(s) from the ABB.
 * @size has to be 1 for the current firmware version.
 */
int abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if (size != 1)
		return -EINVAL;

	mutex_lock(&mb5_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();

	writeb(0, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB5));
	writeb(PRCMU_I2C_READ(slave), (tcdm_base + PRCM_REQ_MB5_I2C_SLAVE_OP));
	writeb(PRCMU_I2C_STOP_EN, (tcdm_base + PRCM_REQ_MB5_I2C_HW_BITS));
	writeb(reg, (tcdm_base + PRCM_REQ_MB5_I2C_REG));
	writeb(0, (tcdm_base + PRCM_REQ_MB5_I2C_VAL));

	writel(MBOX_BIT(5), PRCM_MBOX_CPU_SET);

	if (!wait_for_completion_timeout(&mb5_transfer.work,
				msecs_to_jiffies(20000))) {
		pr_err("prcmu: %s timed out (20 s) waiting for a reply.\n",
			__func__);
		r = -EIO;
		dbx540_prcmu_debug_dump(__func__, true, false);
	} else {
		r = ((mb5_transfer.ack.status == I2C_RD_OK) ? 0 : -EIO);
	}

	if (!r)
		*value = mb5_transfer.ack.value;

	mutex_unlock(&mb5_transfer.lock);

	return r;
}

/**
 * abb_write_masked() - Write masked register value(s) to the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The value(s) to write.
 * @mask:	The mask(s) to use.
 * @size:	The number of registers to write.
 *
 * Writes masked register value(s) to the ABB.
 * For each @value, only the bits set to 1 in the corresponding @mask
 * will be written. The other bits are not changed.
 * @size has to be 1 for the current firmware version.
 */
static int abb_write_masked(u8 slave, u8 reg, u8 *value, u8 *mask, u8 size)
{
	int r;

	if (size != 1)
		return -EINVAL;

	mutex_lock(&mb5_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();

	writeb(~*mask, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB5));
	writeb(PRCMU_I2C_WRITE(slave), (tcdm_base + PRCM_REQ_MB5_I2C_SLAVE_OP));
	writeb(PRCMU_I2C_STOP_EN, (tcdm_base + PRCM_REQ_MB5_I2C_HW_BITS));
	writeb(reg, (tcdm_base + PRCM_REQ_MB5_I2C_REG));
	writeb(*value, (tcdm_base + PRCM_REQ_MB5_I2C_VAL));

	writel(MBOX_BIT(5), PRCM_MBOX_CPU_SET);

	if (!wait_for_completion_timeout(&mb5_transfer.work,
				msecs_to_jiffies(20000))) {
		pr_err("prcmu: %s timed out (20 s) waiting for a reply.\n",
			__func__);
		r = -EIO;
		dbx540_prcmu_debug_dump(__func__, true, false);
	} else {
		r = ((mb5_transfer.ack.status == I2C_WR_OK) ? 0 : -EIO);
	}

	mutex_unlock(&mb5_transfer.lock);

	return r;
}

/**
 * abb_write() - Write register value(s) to the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The value(s) to write.
 * @size:	The number of registers to write.
 *
 * Writes register value(s) to the ABB.
 * @size has to be 1 for the current firmware version.
 */
static int abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	u8 mask = ~0;

	return abb_write_masked(slave, reg, value, &mask, size);
}

static bool is_ac_wake_requested(void)
{
	return (atomic_read(&ac_wake_req_state) != 0);
}

/**
 * db8500_prcmu_system_reset - System reset
 *
 * Saves the reset reason code and then sets the APE_SOFTRST register which
 * fires interrupt to fw
 */
static void system_reset(u16 reset_code)
{
	trace_u8500_system_reset(reset_code);
	writew(reset_code, (tcdm_base + PRCM_SW_RST_REASON));
	writel(1, PRCM_APE_SOFTRST);
}

/**
 * get_reset_code - Retrieve SW reset reason code
 *
 * Retrieves the reset reason code stored by prcmu_system_reset() before
 * last restart.
 */
static u16 get_reset_code(void)
{
	return reset_code_copy;
}

/**
 * get_reset_status - Retrieve reset status
 *
 * Retrieves the value of the reset status register as read at startup.
 */
u32 get_reset_status(void)
{
	return reset_status_copy;
}

static void prcmu_modem_reset_db9540(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;

	db9540_prcmu_mb1_wait_for_idle();

	req->service_id = U9540_PRCM_MB1H_SERVICE_MODEM;
	req->command_id = U9540_PRCM_MB1H_COMMAND_RESET_MODEM;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	/*
	 * No need to check return from PRCMU as modem should go in reset state
	 * This state is already managed by upper layer
	 */
}

/**
 * prcmu_reset_modem - ask the PRCMU to reset modem
 */
void modem_reset(void)
{
	trace_u8500_modem_reset(0);
	mutex_lock(&mb1_transfer.lock);

	dbx500_prcmu_mb1_wait_released();

	prcmu_modem_reset_db9540();
	mutex_unlock(&mb1_transfer.lock);
}

#ifdef CONFIG_C2C
void prcmu_c2c_request_notif_up(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	mutex_lock(&mb1_transfer.lock);

	db9540_prcmu_mb1_wait_for_idle();
	req->service_id = U9540_PRCM_MB1H_SERVICE_C2C;
	req->command_id = U9540_PRCM_MB1H_COMMAND_C2CNOTIFYME;
	req->data.data = U9540_PRCM_REQ_DATA_C2C_NOTIFYME;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);
	mutex_unlock(&mb1_transfer.lock);

}
EXPORT_SYMBOL(prcmu_c2c_request_notif_up);

void prcmu_c2c_request_reset(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	mutex_lock(&mb1_transfer.lock);

	db9540_prcmu_mb1_wait_for_idle();
	req->service_id = U9540_PRCM_MB1H_SERVICE_C2C;
	req->command_id = U9540_PRCM_MB1H_COMMAND_C2CRESET;
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_SENT;
	writel(MBOX_BIT(1), PRCM_MBOX_CPU_SET);
	WARN_ON(wait_for_completion_timeout(&mb1_transfer.work, MB1TIM)== 0);

	mutex_unlock(&mb1_transfer.lock);

}
EXPORT_SYMBOL(prcmu_c2c_request_reset);
#endif

static void ack_dbb_wakeup(void)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
		cpu_relax();

	writeb(MB0H_READ_WAKEUP_ACK, (tcdm_base + PRCM_MBOX_HEADER_REQ_MB0));
	writel(MBOX_BIT(0), PRCM_MBOX_CPU_SET);

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

static inline void print_unknown_header_warning(u8 n, u8 header)
{
	pr_warning("prcmu: Unknown message header (%d) in mailbox %d.\n",
		header, n);
}

static bool read_mailbox_0(void)
{
	bool r;
	u32 ev = 0;
	u32 mask = 0;
	u32 dbb_irqs;
	unsigned int n;
	u8 header;

	header = readb(tcdm_base + PRCM_MBOX_HEADER_ACK_MB0);
	switch (header) {
	case MB0H_WAKEUP_EXE:
	case MB0H_WAKEUP_SLEEP:
		if (readb(tcdm_base + PRCM_ACK_MB0_READ_POINTER) & 1)
			ev = readl(tcdm_base + PRCM_ACK_MB0_WAKEUP_1_8500);
		else
			ev = readl(tcdm_base + PRCM_ACK_MB0_WAKEUP_0_8500);

		if (ev & (WAKEUP_BIT_AC_WAKE_ACK | WAKEUP_BIT_AC_SLEEP_ACK))
			complete(&mb0_transfer.ac_wake_work);
		if (ev & WAKEUP_BIT_SYSCLK_OK)
			complete(&mb3_transfer.sysclk_work);

		prcmu_debug_register_mbox0_event(ev,
						 (mb0_transfer.req.dbb_irqs |
						  mb0_transfer.req.dbb_wakeups |
						  WAKEUP_BIT_AC_WAKE_ACK |
						  WAKEUP_BIT_AC_SLEEP_ACK |
						  WAKEUP_BIT_SYSCLK_OK));

		mask = mb0_transfer.req.dbb_irqs;
		dbb_irqs = ev & mask;

		for (n = 0; n < NUM_PRCMU_WAKEUPS; n++) {
			if (dbb_irqs & prcmu_irq_bit[n])
				generic_handle_irq(IRQ_PRCMU_BASE + n);
		}
		r = true;
		break;
	default:
		print_unknown_header_warning(0, header);
		r = false;
		break;
	}
	writel(MBOX_BIT(0), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_0(header, ev, mask);
	return r;
}

static void db9540_print_mb1_unknown_header_warning(
	u32 service, u32 command, u32 status)
{
	pr_warning("prcmu: Unknown service (%u) and command (%u) in MB1."
			"Returned status (%u)\n",
		service, command, status);
}

static void db9540_read_mb1_service_dvfs(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	switch (ack->command_id) {
	case U9540_PRCM_MB1H_COMMAND_SET_ARM_OPP:
		ack->arm_freq = req->data.data;
		break;

	case U9540_PRCM_MB1H_COMMAND_SET_APE_OPP:
		/* No response data for this service ID and command ID. */
		break;

	case U9540_PRCM_MB1H_COMMAND_SET_SAFE_OPP:
		/* No response data for this service ID and command ID. */
		break;

	default:
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
		break;
	}
}

static void db9540_read_mb1_service_usb(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	/* No response data for this service ID. Just check command ID is OK */
	if (unlikely(ack->command_id != U9540_PRCM_MB1H_COMMAND_USB_WAKEUP_REL))
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
}

static void db9540_read_mb1_service_clock(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	/* No response data for this service ID. Just check command ID is OK */
	if (unlikely(ack->command_id != U9540_PRCM_MB1H_COMMAND_PLL_ON_OFF))
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
}

static void db9540_read_mb1_service_modem(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	/* No response data for this service ID. Just check command ID is OK */
	if (unlikely(ack->command_id != U9540_PRCM_MB1H_COMMAND_RESET_MODEM))
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
}

static void db9540_read_mb1_service_cpuhotplug(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	/* No response data for this service ID. Just check command ID is OK */
	if (unlikely((ack->command_id != U9540_PRCM_MB1H_COMMAND_CPU1_UNPLUG) &&
			(ack->command_id != U9540_PRCM_MB1H_COMMAND_CPU1_REPLUG)
			))
	  db9540_print_mb1_unknown_header_warning(ack->service_id,
	  ack->command_id, ack->status);
}
#ifdef CONFIG_C2C
static void db9540_read_mb1_service_c2c(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	if ((ack->command_id == U9540_PRCM_MB1H_COMMAND_C2CNOTIFYME) ||
		(ack->command_id == U9540_PRCM_MB1H_COMMAND_C2CRESET)) {
		if (ack->status != 0)
			pr_warning("prcmu: C2C service (%u) / command (%u) in "
				"MB1. Error status (%u)\n", ack->service_id,
				ack->command_id, ack->status);
	} else {
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
	}
}
#endif

static void db9540_read_mb1_service_thsensor(struct u9540_mb1_req *req,
		struct mb1_ack_u9540 *ack)
{
	switch (ack->command_id) {
	case U9540_PRCM_MB1H_COMMAND_THSENSOR_GET_TEMP:
		ack->sensor_read = req->data.data;
		break;

	default:
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
		break;
	}
}

static void db9540_read_mailbox_1_req(struct u9540_mb1_req *req)
{
	struct mb1_ack_u9540 *ack = &mb1_transfer.ack.u9540;

	ack->service_id = req->service_id;
	ack->command_id = req->command_id;
	ack->status = req->status;

	switch (ack->service_id) {
	case U9540_PRCM_MB1H_SERVICE_DVFS:
		db9540_read_mb1_service_dvfs(req, ack);
		break;

	case U9540_PRCM_MB1H_SERVICE_USB:
		db9540_read_mb1_service_usb(req, ack);
		break;

	case U9540_PRCM_MB1H_SERVICE_CLOCK:
		db9540_read_mb1_service_clock(req, ack);
		break;

	case U9540_PRCM_MB1H_SERVICE_MODEM:
		db9540_read_mb1_service_modem(req, ack);
		break;

#ifdef CONFIG_C2C
	case U9540_PRCM_MB1H_SERVICE_C2C:
		db9540_read_mb1_service_c2c(req, ack);
		break;
#endif
	case U9540_PRCM_MB1H_SERVICE_CPUHOTPLUG:
		db9540_read_mb1_service_cpuhotplug(req, ack);
		break;

	case  U9540_PRCM_MB1H_SERVICE_THSENSOR:
		db9540_read_mb1_service_thsensor(req, ack);
		break;

	default:
		db9540_print_mb1_unknown_header_warning(ack->service_id,
			ack->command_id, ack->status);
		break;
	}

	/* Update mailbox state */
	req->req_state = U9540_PRCM_MB1_REQ_STATE_REQ_IDLE;

	complete(&mb1_transfer.work);
}

#ifdef CONFIG_C2C
static void db9540_read_mb1_nfy_service_c2c(struct u9540_mb1_nfy *nfy)
{
	switch (nfy->command_id) {
	case U9540_PRCM_MB1H_COMMAND_C2CNOTIFICATION:
		if (nfy->data.data == U9540_PRCM_REQ_DATA_C2C_NOTIFYME)
			genio_c2c_powerup_notif();
		else
			goto err;
		break;
	default:
		goto err;
	}
	return;
err:
	pr_warning("prcmu: Error detected in nfy (MB1):"
			"service (%u) / command (%u) / data (%u)\n",
			nfy->service_id, nfy->command_id, nfy->data.data);
}
#endif

static void db9540_read_mailbox_1_nfy(struct u9540_mb1_nfy *nfy)
{
	switch (nfy->service_id) {
#ifdef CONFIG_C2C
	case U9540_PRCM_MB1H_SERVICE_C2C:
		db9540_read_mb1_nfy_service_c2c(nfy);
		break;
#endif
	default:
		pr_warning("prcmu: Unknown notif service %u / cmd %u in MB1\n",
			nfy->service_id, nfy->command_id);
	}
	/* Update mailbox state */
	nfy->nfy_state = U9540_PRCM_MB1_NFY_STATE_IDLE;
}

static bool db9540_read_mailbox_1(void)
{
	struct u9540_mb1_req *req = U9540_PRCM_MBOX_MB1_REQ;
	struct u9540_mb1_nfy *nfy = U9540_PRCM_MBOX_MB1_NFY;

	/* ack interuption */
	writel(MBOX_BIT(1), PRCM_ARM_IT1_CLR);

	if (req->req_state == U9540_PRCM_MB1_REQ_STATE_ACK_SENT)
		db9540_read_mailbox_1_req(req);

	if (nfy->nfy_state == U9540_PRCM_MB1_NFY_STATE_SENT)
		db9540_read_mailbox_1_nfy(nfy);

	return false;
}

static bool read_mailbox_2(void)
{
	mb2_transfer.ack.status = readb(tcdm_base + PRCM_ACK_MB2_DPS_STATUS);
	writel(MBOX_BIT(2), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_2(mb2_transfer.ack.status);
	complete(&mb2_transfer.work);
	return false;
}

static bool read_mailbox_3(void)
{
	writel(MBOX_BIT(3), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_3(0);
	return false;
}

static bool read_mailbox_4(void)
{
	u8 header;
	bool do_complete = true;

	header = readb(tcdm_base + PRCM_MBOX_HEADER_REQ_MB4);
	switch (header) {
	case MB4H_MEM_ST:
	case MB4H_HOTDOG:
	case MB4H_HOTMON:
	case MB4H_HOT_PERIOD:
	case MB4H_A9WDOG_CONF:
	case MB4H_A9WDOG_EN:
	case MB4H_A9WDOG_DIS:
	case MB4H_A9WDOG_LOAD:
	case MB4H_A9WDOG_KICK:
		break;
	default:
		print_unknown_header_warning(4, header);
		do_complete = false;
		break;
	}

	writel(MBOX_BIT(4), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_4(header);
	if (do_complete)
		complete(&mb4_transfer.work);

	return false;
}

static bool read_mailbox_5(void)
{
	mb5_transfer.ack.status = readb(tcdm_base + PRCM_ACK_MB5_I2C_STATUS);
	mb5_transfer.ack.value = readb(tcdm_base + PRCM_ACK_MB5_I2C_VAL);
	writel(MBOX_BIT(5), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_5(mb5_transfer.ack.status,
		mb5_transfer.ack.value);
	complete(&mb5_transfer.work);
	return false;
}

static bool read_mailbox_6(void)
{
	writel(MBOX_BIT(6), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_6(0);
	return false;
}

static bool read_mailbox_7(void)
{
	writel(MBOX_BIT(7), PRCM_ARM_IT1_CLR);
	trace_u8500_irq_mailbox_7(0);
	return false;
}

static bool (*read_mailbox[NUM_MB])(void) = {
	read_mailbox_0,
	db9540_read_mailbox_1,
	read_mailbox_2,
	read_mailbox_3,
	read_mailbox_4,
	read_mailbox_5,
	read_mailbox_6,
	read_mailbox_7
};

static irqreturn_t prcmu_irq_handler(int irq, void *data)
{
	u32 bits;
	u8 n;
	irqreturn_t r;

	bits = (readl(PRCM_ARM_IT1_VAL) & ALL_MBOX_BITS);
	if (unlikely(!bits))
		return IRQ_NONE;

	r = IRQ_HANDLED;
	for (n = 0; bits; n++) {
		if (bits & MBOX_BIT(n)) {
			bits -= MBOX_BIT(n);
			if (read_mailbox[n]())
				r = IRQ_WAKE_THREAD;
			prcmu_debug_register_interrupt(n);
		}
	}
	return r;
}

static irqreturn_t prcmu_irq_thread_fn(int irq, void *data)
{
	ack_dbb_wakeup();
	return IRQ_HANDLED;
}

static void prcmu_mask_work(struct work_struct *work)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	config_wakeups();

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

static void prcmu_irq_mask(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.dbb_irqs_lock, flags);

	mb0_transfer.req.dbb_irqs &= ~prcmu_irq_bit[d->irq - IRQ_PRCMU_BASE];

	spin_unlock_irqrestore(&mb0_transfer.dbb_irqs_lock, flags);

	if (d->irq != IRQ_PRCMU_CA_SLEEP)
		schedule_work(&mb0_transfer.mask_work);
}

static void prcmu_irq_unmask(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.dbb_irqs_lock, flags);

	mb0_transfer.req.dbb_irqs |= prcmu_irq_bit[d->irq - IRQ_PRCMU_BASE];

	spin_unlock_irqrestore(&mb0_transfer.dbb_irqs_lock, flags);

	if (d->irq != IRQ_PRCMU_CA_SLEEP)
		schedule_work(&mb0_transfer.mask_work);
}

static void noop(struct irq_data *d)
{
}

static struct irq_chip prcmu_irq_chip = {
	.name		= "prcmu",
	.irq_disable	= prcmu_irq_mask,
	.irq_ack	= noop,
	.irq_mask	= prcmu_irq_mask,
	.irq_unmask	= prcmu_irq_unmask,
};

static char *fw_project_name(u8 project)
{
	switch (project) {
	case PRCMU_FW_PROJECT_U8500:
		return "U8500";
	case PRCMU_FW_PROJECT_U8500_C2:
		return "U8500 C2";
	case PRCMU_FW_PROJECT_U9500:
		return "U9500";
	case PRCMU_FW_PROJECT_U8520:
		return "U8520";
	case PRCMU_FW_PROJECT_U8420:
		return "U8420";
	case PRCMU_FW_PROJECT_U8420_SYSCLK:
		return "U8420-sysclk";
	case PRCMU_FW_PROJECT_U9540:
		return "U9540";
	default:
		return "Unknown";
	}
}
static inline void dbx540_prcmu_set(unsigned int reg, u32 bits)
{
	dbx540_prcmu_write_masked(reg, bits, bits);
}

static inline void dbx540_prcmu_clear(unsigned int reg, u32 bits)
{
	dbx540_prcmu_write_masked(reg, bits, 0);
}


static int enable_spi2(void)
{
	dbx540_prcmu_set(DB8500_PRCM_GPIOCR, DB8500_PRCM_GPIOCR_SPI2_SELECT);
	return 0;
}

/**
 * prcmu_disable_spi2 - Disables pin muxing for SPI2 on OtherAlternateC1.
 */
static int disable_spi2(void)
{
	dbx540_prcmu_clear(DB8500_PRCM_GPIOCR, DB8500_PRCM_GPIOCR_SPI2_SELECT);
	return 0;
}

/**
 * prcmu_enable_stm_mod_uart - Enables pin muxing for STMMOD
 * and UARTMOD on OtherAlternateC3.
 */
static int enable_stm_mod_uart(void)
{
	dbx540_prcmu_set(DB8500_PRCM_GPIOCR,
			(DB8500_PRCM_GPIOCR_DBG_STM_MOD_CMD1 |
			 DB8500_PRCM_GPIOCR_DBG_UARTMOD_CMD0));
	return 0;
}

/**
 * prcmu_disable_stm_mod_uart - Disables pin muxing for STMMOD
 * and UARTMOD on OtherAlternateC3.
 */
static int disable_stm_mod_uart(void)
{
	dbx540_prcmu_clear(DB8500_PRCM_GPIOCR,
			(DB8500_PRCM_GPIOCR_DBG_STM_MOD_CMD1 |
			 DB8500_PRCM_GPIOCR_DBG_UARTMOD_CMD0));
	return 0;
}

/**
 * prcmu_enable_stm_ape - Enables pin muxing for STM APE on OtherAlternateC1.
 */
static int enable_stm_ape(void)
{
	dbx540_prcmu_set(DB8500_PRCM_GPIOCR,
			DB8500_PRCM_GPIOCR_DBG_STM_APE_CMD);
	return 0;
}

/**
 * prcmu_disable_stm_ape - Disables pin muxing for STM APE on OtherAlternateC1.
 */
static int disable_stm_ape(void)
{
	dbx540_prcmu_clear(DB8500_PRCM_GPIOCR,
			DB8500_PRCM_GPIOCR_DBG_STM_APE_CMD);
	return 0;
}

static  struct prcmu_val_data val_tab[] = {
	{
		.val = APE_OPP,
		.set_val = set_ape_opp,
		.get_val = get_ape_opp,
	},
	{
		.val = DDR_OPP,
		.set_val = set_ddr_opp,
		.get_val = get_ddr_opp,
	},
};

static struct prcmu_out_data out_tab[] = {
	{
		.out = SPI2_MUX,
		.enable =  enable_spi2,
		.disable = disable_spi2,
	},
	{
		.out = STM_APE_MUX,
		.enable = enable_stm_ape,
		.disable = disable_stm_ape,
	},
	{
		.out = STM_MOD_UART_MUX,
		.enable = enable_stm_mod_uart,
		.disable = disable_stm_mod_uart,
	}
};


static struct prcmu_early_data early_fops = {
	/*  system reset  */
	.system_reset = system_reset,

	/*  clock service */
	.config_clkout = config_clkout,
	.request_clock = dbx540_prcmu_request_clock,

	/*  direct register access */
	.read = dbx540_prcmu_read,
	.write =  dbx540_prcmu_write,
	.write_masked = dbx540_prcmu_write_masked,
	/* others */
	.round_clock_rate = dbx540_prcmu_round_clock_rate,
	.set_clock_rate = dbx540_prcmu_set_clock_rate,
	.clock_rate = dbx540_prcmu_clock_rate,
	.get_fw_version = get_fw_version,
	.vc = vc,
};

static struct prcmu_fops_register early_tab[] = {
	{
		.fops = PRCMU_EARLY,
		.data.pearly = &early_fops
	},
	{
		.fops = PRCMU_VAL,
		.size = ARRAY_SIZE(val_tab),
		.data.pval = val_tab
	},
	{
		.fops = PRCMU_OUT,
		.size = ARRAY_SIZE(out_tab),
		.data.pout = out_tab
	}
};

static struct prcmu_fops_register_data early_data = {
	.size = ARRAY_SIZE(early_tab),
	.tab = early_tab
};

struct prcmu_probe_data probe_fops = {
	/* sysfs soc inf */
	.get_reset_code = get_reset_code,

	/* pm/suspend.c/cpu freq */
	.config_esram0_deep_sleep = config_esram0_deep_sleep,
	.set_power_state = set_power_state,
	.get_power_state_result =get_power_state_result,
	.enable_wakeups = enable_wakeups,
	.is_ac_wake_requested = is_ac_wake_requested,

	/* modem */
	.modem_reset = modem_reset,

	/* no used at all */
	.config_abb_event_readout = config_abb_event_readout,
	.get_abb_event_buffer = get_abb_event_buffer,

	/* abb access */
	.abb_read = abb_read,
	.abb_write = abb_write,
	.get_reset_status = get_reset_status,
	/*  other u8500 specific */
	.request_ape_opp_100_voltage = request_ape_opp_100_voltage,
	.configure_auto_pm = configure_auto_pm,

	/* abb specific access */
	.abb_write_masked = abb_write_masked,

};

struct prcmu_probe_ux540_data probex540_fops = {

	.stay_in_wfi_check = stay_in_wfi_check,
	.replug_cpu1 = replug_cpu1,
	.unplug_cpu1 = unplug_cpu1,
};

static struct prcmu_fops_register probe_tab[] = {
	{
		.fops = PRCMU_PROBE,
		.data.pprobe = &probe_fops,
	},
	{       .fops = PRCMU_PROBE_UX540,
		.data.pprobeux540 =&probex540_fops,
	}
};

struct prcmu_fops_register_data probe_data = {
	.size = ARRAY_SIZE(probe_tab),
	.tab = probe_tab,
};

struct prcmu_fops_register_data *__init dbx540_prcmu_early_init(void)
{
	unsigned int i;
	void *tcpm_base = ioremap_nocache(U8500_PRCMU_TCPM_BASE, SZ_4K);
	void __iomem *sec_base;

	if (tcpm_base != NULL) {
		u32 version;
		version = readl(tcpm_base + PRCMU_FW_VERSION_OFFSET + 4);
		fw_info.version.project = version & 0xFF;
		fw_info.version.api_version = (version >> 8) & 0xFF;
		fw_info.version.func_version = (version >> 16) & 0xFF;
		fw_info.version.errata = (version >> 24) & 0xFF;
		fw_info.valid = true;
		pr_info("PRCMU firmware: %s(%d), version %d.%d.%d\n",
				fw_project_name(fw_info.version.project),
				fw_info.version.project,
				fw_info.version.api_version,
				fw_info.version.func_version,
				fw_info.version.errata);
		iounmap(tcpm_base);
	}

	tcdm_base = __io_address(U9540_PRCMU_TCDM_BASE);
	prcmu_tcdm_base = __io_address(U8500_PRCMU_TCDM_BASE);
	/* read curent max opp counter */
	arm_max_opp_profile.max_opp_cnt =
		arm_max_opp_profile.secure_memory;
	/*
	 * Copy the value of the reset status register and if needed also
	 * the software reset code.
	 */
	sec_base = ioremap_nocache(U8500_PRCMU_SEC_BASE, SZ_4K);
	if (sec_base != NULL) {
		reset_status_copy = readl(sec_base +
				DB8500_SEC_PRCM_RESET_STATUS);
		iounmap(sec_base);
	}
	if (reset_status_copy & DB8500_SEC_PRCM_RESET_STATUS_APE_SOFTWARE_RESET)
		reset_code_copy = readw(tcdm_base + PRCM_SW_RST_REASON);

	spin_lock_init(&mb0_transfer.lock);
	spin_lock_init(&mb0_transfer.dbb_irqs_lock);
	mutex_init(&mb0_transfer.ac_wake_lock);
	init_completion(&mb0_transfer.ac_wake_work);
	mutex_init(&mb1_transfer.lock);
	init_completion(&mb1_transfer.work);
	mb1_transfer.ape_opp = APE_OPP_INIT;
	mutex_init(&mb2_transfer.lock);
	init_completion(&mb2_transfer.work);
	spin_lock_init(&mb2_transfer.auto_pm_lock);
	spin_lock_init(&mb3_transfer.lock);
	mutex_init(&mb3_transfer.sysclk_lock);
	init_completion(&mb3_transfer.sysclk_work);
	mutex_init(&mb4_transfer.lock);
	init_completion(&mb4_transfer.work);
	mutex_init(&mb5_transfer.lock);
	init_completion(&mb5_transfer.work);

	INIT_WORK(&mb0_transfer.mask_work, prcmu_mask_work);

	/* Initalize irqs. */
	for (i = 0; i < NUM_PRCMU_WAKEUPS; i++) {
		unsigned int irq;

		irq = IRQ_PRCMU_BASE + i;
		irq_set_chip_and_handler(irq, &prcmu_irq_chip,
				handle_simple_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
	/*  fixed it according to soc settings knowledge */
	latest_armss_rate = 1500000;
	return &early_data;
}

static void __init init_prcm_registers(void)
{
	u32 val;

	val = readl(PRCM_A9PL_FORCE_CLKEN);
	val &= ~(PRCM_A9PL_FORCE_CLKEN_PRCM_A9PL_FORCE_CLKEN |
		PRCM_A9PL_FORCE_CLKEN_PRCM_A9AXI_FORCE_CLKEN);
	writel(val, (PRCM_A9PL_FORCE_CLKEN));
}

/*
 * Power domain switches (ePODs) modeled as regulators for the DB8500 SoC
 */
static struct regulator_consumer_supply db8500_vape_consumers[] = {
	REGULATOR_SUPPLY("v-ape", NULL),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.0"),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.1"),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.2"),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.3"),
	/* "v-mmc" changed to "vcore" in the mainline kernel */
	REGULATOR_SUPPLY("vcore", "sdi0"),
	REGULATOR_SUPPLY("vcore", "sdi1"),
	REGULATOR_SUPPLY("vcore", "sdi2"),
	REGULATOR_SUPPLY("vcore", "sdi3"),
	REGULATOR_SUPPLY("vcore", "sdi4"),
	REGULATOR_SUPPLY("v-dma", "dma40.0"),
	REGULATOR_SUPPLY("v-ape", "ab8500-usb.0"),
	REGULATOR_SUPPLY("v-uart", "uart0"),
	REGULATOR_SUPPLY("v-uart", "uart1"),
	REGULATOR_SUPPLY("v-uart", "uart2"),
	REGULATOR_SUPPLY("v-ape", "nmk-ske-keypad.0"),
	REGULATOR_SUPPLY("v-hsi", "ste_hsi.0"),
};

static struct regulator_consumer_supply db8500_vsmps2_consumers[] = {
	REGULATOR_SUPPLY("musb_1v8", "ab9540-usb.0"),
	REGULATOR_SUPPLY("musb_1v8", "ab8500-usb.0"),
	/* AV8100 regulator */
	REGULATOR_SUPPLY("hdmi_1v8", "0-0070"),
};

static struct regulator_consumer_supply db8500_b2r2_mcde_consumers[] = {
	REGULATOR_SUPPLY("vsupply", "b2r2_core"),
	REGULATOR_SUPPLY("vsupply", "b2r2_1_core"),
	REGULATOR_SUPPLY("vsupply", "mcde"),
	REGULATOR_SUPPLY("vsupply", "dsilink.0"),
	REGULATOR_SUPPLY("vsupply", "dsilink.1"),
	REGULATOR_SUPPLY("vsupply", "dsilink.2"),
};

/* SVA MMDSP regulator switch */
static struct regulator_consumer_supply db8500_svammdsp_consumers[] = {
	REGULATOR_SUPPLY("sva-mmdsp", "cm_control"),
};

/* SVA pipe regulator switch */
static struct regulator_consumer_supply db8500_svapipe_consumers[] = {
	REGULATOR_SUPPLY("sva-pipe", "cm_control"),
	REGULATOR_SUPPLY("v-hva", NULL),
	REGULATOR_SUPPLY("v-g1", NULL),
};

/* SIA MMDSP regulator switch */
static struct regulator_consumer_supply db8500_siammdsp_consumers[] = {
	REGULATOR_SUPPLY("sia-mmdsp", "cm_control"),
};

/* SIA pipe regulator switch */
static struct regulator_consumer_supply db8500_siapipe_consumers[] = {
	REGULATOR_SUPPLY("sia-pipe", "cm_control"),
};

static struct regulator_consumer_supply db8500_sga_consumers[] = {
	REGULATOR_SUPPLY("v-mali", NULL),
};

static struct regulator_consumer_supply db8500_vpll_consumers[] = {
	REGULATOR_SUPPLY("v-vpll", NULL),
};

/* ESRAM1 and 2 regulator switch */
static struct regulator_consumer_supply db8500_esram12_consumers[] = {
	REGULATOR_SUPPLY("esram12", "cm_control"),
};

/* ESRAM3 and 4 regulator switch */
static struct regulator_consumer_supply db8500_esram34_consumers[] = {
	REGULATOR_SUPPLY("v-esram34", "mcde"),
	REGULATOR_SUPPLY("esram34", "cm_control"),
	REGULATOR_SUPPLY("lcla_esram", "dma40.0"),
};

static struct regulator_init_data db8500_regulators[DB8500_NUM_REGULATORS] = {
	[DB8500_REGULATOR_VAPE] = {
		.constraints = {
			.name = "db8500-vape",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_vape_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_vape_consumers),
	},
	[DB8500_REGULATOR_VARM] = {
		.constraints = {
			.name = "db8500-varm",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_VMODEM] = {
		.constraints = {
			.name = "db8500-vmodem",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_VPLL] = {
		.constraints = {
			.name = "db8500-vpll",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_vpll_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_vpll_consumers),
	},
	[DB8500_REGULATOR_VSMPS1] = {
		.constraints = {
			.name = "db8500-vsmps1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_VSMPS2] = {
		.constraints = {
			.name = "db8500-vsmps2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_vsmps2_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_vsmps2_consumers),
	},
	[DB8500_REGULATOR_VSMPS3] = {
		.constraints = {
			.name = "db8500-vsmps3",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_VRF1] = {
		.constraints = {
			.name = "db8500-vrf1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_SWITCH_SVAMMDSP] = {
		/* dependency to u8500-vape is handled outside regulator framework */
		.constraints = {
			.name = "db8500-sva-mmdsp",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_svammdsp_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_svammdsp_consumers),
	},
	[DB8500_REGULATOR_SWITCH_SVAMMDSPRET] = {
		.constraints = {
			/* "ret" means "retention" */
			.name = "db8500-sva-mmdsp-ret",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_SWITCH_SVAPIPE] = {
		/* dependency to u8500-vape is handled outside regulator framework */
		.constraints = {
			.name = "db8500-sva-pipe",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_svapipe_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_svapipe_consumers),
	},
	[DB8500_REGULATOR_SWITCH_SIAMMDSP] = {
		/* dependency to u8500-vape is handled outside regulator framework */
		.constraints = {
			.name = "db8500-sia-mmdsp",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_siammdsp_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_siammdsp_consumers),
	},
	[DB8500_REGULATOR_SWITCH_SIAMMDSPRET] = {
		.constraints = {
			.name = "db8500-sia-mmdsp-ret",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_SWITCH_SIAPIPE] = {
		/* dependency to u8500-vape is handled outside regulator framework */
		.constraints = {
			.name = "db8500-sia-pipe",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_siapipe_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_siapipe_consumers),
	},
	[DB8500_REGULATOR_SWITCH_SGA] = {
		.supply_regulator = "db8500-vape",
		.constraints = {
			.name = "db8500-sga",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_sga_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_sga_consumers),

	},
	[DB8500_REGULATOR_SWITCH_B2R2_MCDE] = {
		.supply_regulator = "db8500-vape",
		.constraints = {
			.name = "db8500-b2r2-mcde",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_b2r2_mcde_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_b2r2_mcde_consumers),
	},
	[DB8500_REGULATOR_SWITCH_ESRAM12] = {
		/*
		 * esram12 is set in retention and supplied by Vsafe when Vape is off,
		 * no need to hold Vape
		 */
		.constraints = {
			.name = "db8500-esram12",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_esram12_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_esram12_consumers),
	},
	[DB8500_REGULATOR_SWITCH_ESRAM12RET] = {
		.constraints = {
			.name = "db8500-esram12-ret",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	[DB8500_REGULATOR_SWITCH_ESRAM34] = {
		/*
		 * esram34 is set in retention and supplied by Vsafe when Vape is off,
		 * no need to hold Vape
		 */
		.constraints = {
			.name = "db8500-esram34",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies = db8500_esram34_consumers,
		.num_consumer_supplies = ARRAY_SIZE(db8500_esram34_consumers),
	},
	[DB8500_REGULATOR_SWITCH_ESRAM34RET] = {
		.constraints = {
			.name = "db8500-esram34-ret",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
};
static struct ux500_wdt_ops db8500_wdt_ops = {
	.enable = enable_a9wdog,
	.disable = disable_a9wdog,
	.kick = kick_a9wdog,
	.load = load_a9wdog,
	.config = config_a9wdog,
};

/*
 * Thermal Sensor
 */
static struct dbx500_temp_ops dbx540_temp_ops = {
	.config_hotdog = config_hotdog,
	.config_hotmon = config_hotmon,
	.start_temp_sense = start_temp_sense,
	.stop_temp_sense = stop_temp_sense,
	.thsensor_get_temp = thsensor_get_temp,
};

static struct dbx500_temp_pdata dbx540_temp_pdata = {
	.ops = &dbx540_temp_ops,
	.monitoring_active = true,
};


static struct resource ux540_thsens_resources[] = {
	{
		.name = "IRQ_HOTMON_LOW",
		.start  = IRQ_PRCMU_HOTMON_LOW,
		.end    = IRQ_PRCMU_HOTMON_LOW,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.name = "IRQ_HOTMON_HIGH",
		.start  = IRQ_PRCMU_HOTMON_HIGH,
		.end    = IRQ_PRCMU_HOTMON_HIGH,
		.flags  = IORESOURCE_IRQ,
	},
};
static struct db8500_regulator_init_data db8500_regulators_pdata = {
	.set_epod = set_epod,
	.regulators = db8500_regulators,
	.reg_num = DB8500_NUM_REGULATORS
};

static struct mfd_cell db8500_prcmu_devs[] = {
	{
		.name = "cpufreq-ux500",
		.id = -1,
	},
	{
		.name = "db8500-prcmu-regulators",
		.platform_data = &db8500_regulators_pdata,
		.pdata_size = sizeof(db8500_regulators_pdata),
	},
	{
		.name = "ux500_wdt",
		.id = -1,
		.platform_data = &db8500_wdt_ops,
		.pdata_size = sizeof(db8500_wdt_ops),
	},
	{
		.name = "dbx500_temp",
		.platform_data = &dbx540_temp_pdata,
		.pdata_size = sizeof(dbx540_temp_pdata),
		.resources       = ux540_thsens_resources,
		.num_resources  = ARRAY_SIZE(ux540_thsens_resources),
	},
	{
		.name = "dbx500-prcmu",
		.platform_data = &probe_data,
		.pdata_size = sizeof(probe_data),
	},
};

/**
 * prcmu_fw_init - arch init call for the Linux PRCMU fw init logic
 *
 */
static int __init dbx540_prcmu_probe(struct platform_device *pdev)
{
	int err = 0;

	init_prcm_registers();

	/* Clean up the mailbox interrupts after pre-kernel code. */
	writel(ALL_MBOX_BITS, PRCM_ARM_IT1_CLR);

	err = request_threaded_irq(IRQ_DB8500_PRCMU1, prcmu_irq_handler,
			prcmu_irq_thread_fn, IRQF_NO_SUSPEND, "prcmu", NULL);
	if (err < 0) {
		pr_err("prcmu: Failed to allocate IRQ_DB8500_PRCMU1.\n");
		err = -EBUSY;
		goto no_irq_return;
	}
	freq_table =
		(struct cpufreq_frequency_table *)dev_get_platdata(&pdev->dev);

	update_freq_table(freq_table);

	config_esram0_deep_sleep(ESRAM0_DEEP_SLEEP_STATE_RET);

	err = mfd_add_devices(&pdev->dev, 0, db8500_prcmu_devs,
			ARRAY_SIZE(db8500_prcmu_devs), NULL,
			0);

	if (err)
		pr_err("prcmu: Failed to add subdevices\n");
	else
		pr_info("DB8500 PRCMU initialized\n");

	/*
	 * Temporary U9540 bringup code - Enable all clock gates.
	 * Write 1 to all bits of PRCM_YYCLKEN0_MGT_SET and
	 * PRCM_YYCLKEN1_MGT_SET registers.
	 */
	writel(~0, _PRCMU_BASE + 0x510); /* PRCM_YYCLKEN0_MGT_SET */
	writel(~0, _PRCMU_BASE + 0x514); /* PRCM_YYCLKEN1_MGT_SET */

	/*
	 * set a flag to indicate that prcmu drv is well initialised and that
	 * prcmu driver services can be called
	 */
	prcmu_driver_initialised = 1;
	cpu1_unplug_ongoing = 0;

no_irq_return:
	return err;
}

static struct platform_driver dbx540_prcmu_driver = {
	.driver = {
		.name = "dbx540-prcmu",
		.owner = THIS_MODULE,
	},
};

static int __init dbx540_prcmu_init(void)
{
	return platform_driver_probe(&dbx540_prcmu_driver, dbx540_prcmu_probe);
}

arch_initcall(dbx540_prcmu_init);


MODULE_DESCRIPTION("DBX540 PRCMU Unit driver");
MODULE_LICENSE("GPL v2");
