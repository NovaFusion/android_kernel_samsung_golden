/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 *
 * U5500 PRCM Unit interface driver
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
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/regulator/db5500-prcmu.h>
#include <linux/regulator/machine.h>
#include <linux/interrupt.h>
#include <linux/mfd/ux500_wdt.h>
#include <linux/mfd/dbx500_temp.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/db5500-regs.h>
#include <mach/prcmu-debug.h>

#include "db5500-prcmu-regs.h"

#define PRCMU_FW_VERSION_OFFSET 0xA4
#define PRCM_SW_RST_REASON (tcdm_base + 0xFF8) /* 2 bytes */

#define _PRCM_MB_HEADER (tcdm_base + 0xFE8)
#define PRCM_REQ_MB0_HEADER (_PRCM_MB_HEADER + 0x0)
#define PRCM_REQ_MB1_HEADER (_PRCM_MB_HEADER + 0x1)
#define PRCM_REQ_MB2_HEADER (_PRCM_MB_HEADER + 0x2)
#define PRCM_REQ_MB3_HEADER (_PRCM_MB_HEADER + 0x3)
#define PRCM_REQ_MB4_HEADER (_PRCM_MB_HEADER + 0x4)
#define PRCM_REQ_MB5_HEADER (_PRCM_MB_HEADER + 0x5)
#define PRCM_REQ_MB6_HEADER (_PRCM_MB_HEADER + 0x6)
#define PRCM_REQ_MB7_HEADER (_PRCM_MB_HEADER + 0x7)
#define PRCM_ACK_MB0_HEADER (_PRCM_MB_HEADER + 0x8)
#define PRCM_ACK_MB1_HEADER (_PRCM_MB_HEADER + 0x9)
#define PRCM_ACK_MB2_HEADER (_PRCM_MB_HEADER + 0xa)
#define PRCM_ACK_MB3_HEADER (_PRCM_MB_HEADER + 0xb)
#define PRCM_ACK_MB4_HEADER (_PRCM_MB_HEADER + 0xc)
#define PRCM_ACK_MB5_HEADER (_PRCM_MB_HEADER + 0xd)
#define PRCM_ACK_MB6_HEADER (_PRCM_MB_HEADER + 0xe)
#define PRCM_ACK_MB7_HEADER (_PRCM_MB_HEADER + 0xf)

/* Req Mailboxes */
#define PRCM_REQ_MB0 (tcdm_base + 0xFD8)
#define PRCM_REQ_MB1 (tcdm_base + 0xFCC)
#define PRCM_REQ_MB2 (tcdm_base + 0xFC4)
#define PRCM_REQ_MB3 (tcdm_base + 0xFC0)
#define PRCM_REQ_MB4 (tcdm_base + 0xF98)
#define PRCM_REQ_MB5 (tcdm_base + 0xF90)
#define PRCM_REQ_MB6 (tcdm_base + 0xF8C)
#define PRCM_REQ_MB7 (tcdm_base + 0xF84)

/* Ack Mailboxes */
#define PRCM_ACK_MB0 (tcdm_base + 0xF38)
#define PRCM_ACK_MB1 (tcdm_base + 0xF30)
#define PRCM_ACK_MB2 (tcdm_base + 0xF24)
#define PRCM_ACK_MB3 (tcdm_base + 0xF20)
#define PRCM_ACK_MB4 (tcdm_base + 0xF1C)
#define PRCM_ACK_MB5 (tcdm_base + 0xF14)
#define PRCM_ACK_MB6 (tcdm_base + 0xF0C)
#define PRCM_ACK_MB7 (tcdm_base + 0xF08)

/* Share info */
#define PRCM_SHARE_INFO (tcdm_base + 0xEC8)

#define PRCM_SHARE_INFO_HOTDOG (PRCM_SHARE_INFO + 62)

/* Mailbox 0 REQs */
#define PRCM_REQ_MB0_AP_POWER_STATE    (PRCM_REQ_MB0 + 0x0)
#define PRCM_REQ_MB0_ULP_CLOCK_STATE   (PRCM_REQ_MB0 + 0x1)
#define PRCM_REQ_MB0_AP_PLL_STATE      (PRCM_REQ_MB0 + 0x2)
#define PRCM_REQ_MB0_DDR_STATE         (PRCM_REQ_MB0 + 0x3)
#define PRCM_REQ_MB0_ESRAM0_STATE      (PRCM_REQ_MB0 + 0x4)
#define PRCM_REQ_MB0_WAKEUP_DBB        (PRCM_REQ_MB0 + 0x8)
#define PRCM_REQ_MB0_WAKEUP_ABB        (PRCM_REQ_MB0 + 0xC)

/* Mailbox 0 ACKs */
#define PRCM_ACK_MB0_AP_PWRSTTR_STATUS (PRCM_ACK_MB0 + 0x0)
#define PRCM_ACK_MB0_READ_POINTER      (PRCM_ACK_MB0 + 0x1)
#define PRCM_ACK_MB0_WAKEUP_0_DBB      (PRCM_ACK_MB0 + 0x4)
#define PRCM_ACK_MB0_WAKEUP_0_ABB      (PRCM_ACK_MB0 + 0x8)
#define PRCM_ACK_MB0_WAKEUP_1_DBB      (PRCM_ACK_MB0 + 0x28)
#define PRCM_ACK_MB0_WAKEUP_1_ABB      (PRCM_ACK_MB0 + 0x2C)
#define PRCM_ACK_MB0_EVENT_ABB_NUMBERS 20

/* Request mailbox 1 fields. */
#define PRCM_REQ_MB1_ARM_OPP			(PRCM_REQ_MB1 + 0x0)
#define PRCM_REQ_MB1_APE_OPP			(PRCM_REQ_MB1 + 0x1)

/* Mailbox 1 ACKs */
#define PRCM_ACK_MB1_CURRENT_ARM_OPP	(PRCM_ACK_MB1 + 0x0)
#define PRCM_ACK_MB1_CURRENT_APE_OPP	(PRCM_ACK_MB1 + 0x1)
#define PRCM_ACK_MB1_ARM_VOLT_STATUS	(PRCM_ACK_MB1 + 0x2)
#define PRCM_ACK_MB1_APE_VOLT_STATUS	(PRCM_ACK_MB1 + 0x3)

/* Mailbox 2 REQs */
#define PRCM_REQ_MB2_EPOD_CLIENT (PRCM_REQ_MB2 + 0x0)
#define PRCM_REQ_MB2_EPOD_STATE  (PRCM_REQ_MB2 + 0x1)
#define PRCM_REQ_MB2_CLK_CLIENT  (PRCM_REQ_MB2 + 0x2)
#define PRCM_REQ_MB2_CLK_STATE   (PRCM_REQ_MB2 + 0x3)
#define PRCM_REQ_MB2_PLL_CLIENT  (PRCM_REQ_MB2 + 0x4)
#define PRCM_REQ_MB2_PLL_STATE   (PRCM_REQ_MB2 + 0x5)

/* Mailbox 2 ACKs */
#define PRCM_ACK_MB2_EPOD_STATUS (PRCM_ACK_MB2 + 0x2)
#define PRCM_ACK_MB2_CLK_STATUS  (PRCM_ACK_MB2 + 0x6)
#define PRCM_ACK_MB2_PLL_STATUS  (PRCM_ACK_MB2 + 0xA)

enum mb_return_code {
	RC_SUCCESS,
	RC_FAIL,
};

/* Mailbox 0 headers. */
enum mb0_header {
	/* acknowledge */
	MB0H_WAKE_UP = 0,
	/* request */
	MB0H_PWR_STATE_TRANS,
	MB0H_WAKE_UP_CFG,
	MB0H_RD_WAKE_UP_ACK,
};

/* Mailbox 1 headers.*/
enum mb1_header {
	MB1H_ARM_OPP = 1,
	MB1H_APE_OPP,
	MB1H_ARM_APE_OPP,
};

/* Mailbox 2 headers. */
enum mb2_header {
	MB2H_EPOD_REQUEST = 1,
	MB2H_CLK_REQUEST,
	MB2H_PLL_REQUEST,
};

/* Mailbox 3 headers. */
enum mb3_header {
	MB3H_REFCLK_REQUEST = 1,
};

enum sysclk_state {
	SYSCLK_OFF,
	SYSCLK_ON,
};

/* Mailbox 4 headers */
enum mb4_header {
	MB4H_CFG_HOTDOG = 7,
	MB4H_CFG_HOTMON = 8,
	MB4H_CFG_HOTPERIOD = 10,
	MB4H_CGF_MODEM_RESET = 13,
	MB4H_CGF_A9WDOG_EN_PREBARK = 14,
	MB4H_CGF_A9WDOG_EN_NOPREBARK = 15,
	MB4H_CGF_A9WDOG_DIS = 16,
};

/* Mailbox 4 ACK headers */
enum mb4_ack_header {
	MB4H_ACK_CFG_HOTDOG = 5,
	MB4H_ACK_CFG_HOTMON = 6,
	MB4H_ACK_CFG_HOTPERIOD = 8,
	MB4H_ACK_CFG_MODEM_RESET = 11,
	MB4H_ACK_CGF_A9WDOG_EN_PREBARK = 12,
	MB4H_ACK_CGF_A9WDOG_EN_NOPREBARK = 13,
	MB4H_ACK_CGF_A9WDOG_DIS = 14,
};

/* Mailbox 5 headers. */
enum mb5_header {
	MB5H_I2C_WRITE = 1,
	MB5H_I2C_READ,
};

enum db5500_arm_opp {
	DB5500_ARM_100_OPP = 1,
	DB5500_ARM_50_OPP,
	DB5500_ARM_EXT_OPP,
};

enum db5500_ape_opp {
	DB5500_APE_100_OPP = 1,
	DB5500_APE_50_OPP
};

enum epod_state {
	EPOD_OFF,
	EPOD_ON,
};
enum epod_onoffret_state {
	EPOD_OOR_OFF,
	EPOD_OOR_RET,
	EPOD_OOR_ON,
};
enum db5500_prcmu_pll {
	DB5500_PLL_SOC0,
	DB5500_PLL_SOC1,
	DB5500_PLL_DDR,
	DB5500_NUM_PLL_ID,
};

enum db5500_prcmu_clk {
	DB5500_MSP1CLK,
	DB5500_CDCLK,
	DB5500_IRDACLK,
	DB5500_TVCLK,
	DB5500_NUM_CLK_CLIENTS,
};

enum on_off_ret {
	OFF_ST,
	RET_ST,
	ON_ST,
};

enum db5500_ap_pwr_state {
	DB5500_AP_SLEEP = 2,
	DB5500_AP_DEEP_SLEEP,
	DB5500_AP_IDLE,
};

/* Request mailbox 3 fields */
#define PRCM_REQ_MB3_REFCLK_MGT		(PRCM_REQ_MB3 + 0x0)

/* Ack. mailbox 3 fields */
#define PRCM_ACK_MB3_REFCLK_REQ		(PRCM_ACK_MB3 + 0x0)


/* Request mailbox 4 fields */
#define PRCM_REQ_MB4_HOTDOG_THRESHOLD           (PRCM_REQ_MB4 + 32)
#define PRCM_REQ_MB4_HOT_PERIOD                 (PRCM_REQ_MB4 + 34)
#define PRCM_REQ_MB4_HOTMON_LOW                 (PRCM_REQ_MB4 + 36)
#define PRCM_REQ_MB4_HOTMON_HIGH                (PRCM_REQ_MB4 + 38)

/* Ack. mailbox 4 field */
#define PRCM_ACK_MB4_REQUESTS                   (PRCM_ACK_MB4 + 0x0)

/* Request mailbox 5 fields. */
#define PRCM_REQ_MB5_I2C_SLAVE (PRCM_REQ_MB5 + 0)
#define PRCM_REQ_MB5_I2C_REG (PRCM_REQ_MB5 + 1)
#define PRCM_REQ_MB5_I2C_SIZE (PRCM_REQ_MB5 + 2)
#define PRCM_REQ_MB5_I2C_DATA (PRCM_REQ_MB5 + 4)

/* Acknowledge mailbox 5 fields. */
#define PRCM_ACK_MB5_RETURN_CODE (PRCM_ACK_MB5 + 0)
#define PRCM_ACK_MB5_I2C_DATA (PRCM_ACK_MB5 + 4)

#define NUM_MB 8
#define MBOX_BIT BIT
#define ALL_MBOX_BITS (MBOX_BIT(NUM_MB) - 1)

/*
* Used by MCDE to setup all necessary PRCMU registers
*/
#define PRCMU_RESET_DSIPLL			0x00004000
#define PRCMU_UNCLAMP_DSIPLL			0x00400800

/* HDMI CLK MGT PLLSW=001 (PLLSOC0), PLLDIV=0xC, = 33.33 Mhz*/
#define PRCMU_DSI_CLOCK_SETTING			0x0000012C
/* TVCLK_MGT PLLSW=001 (PLLSOC0) PLLDIV=0x13, = 19.05 MHZ */
#define PRCMU_DSI_LP_CLOCK_SETTING		0x00000135
/* PRCM_PLLDSI_FREQ R=4, N=1, D= 0x65 */
#define PRCMU_PLLDSI_FREQ_SETTING		0x00040165
#define PRCMU_DSI_PLLOUT_SEL_SETTING		0x00000002
#define PRCMU_ENABLE_ESCAPE_CLOCK_DIV		0x03000201
#define PRCMU_DISABLE_ESCAPE_CLOCK_DIV		0x00000101

#define PRCMU_ENABLE_PLLDSI			0x00000001
#define PRCMU_DISABLE_PLLDSI			0x00000000

#define PRCMU_DSI_RESET_SW			0x00000003
#define PRCMU_RESOUTN0_PIN			0x00000001
#define PRCMU_RESOUTN1_PIN			0x00000002
#define PRCMU_RESOUTN2_PIN			0x00000004

#define PRCMU_PLLDSI_LOCKP_LOCKED		0x3

/*
 * Wakeups/IRQs
 */

#define WAKEUP_BIT_RTC BIT(0)
#define WAKEUP_BIT_RTT0 BIT(1)
#define WAKEUP_BIT_RTT1 BIT(2)
#define WAKEUP_BIT_CD_IRQ BIT(3)
#define WAKEUP_BIT_SRP_TIM BIT(4)
#define WAKEUP_BIT_APE_REQ BIT(5)
#define WAKEUP_BIT_USB BIT(6)
#define WAKEUP_BIT_ABB BIT(7)
#define WAKEUP_BIT_LOW_POWER_AUDIO BIT(8)
#define WAKEUP_BIT_TEMP_SENSOR_LOW BIT(9)
#define WAKEUP_BIT_ARM BIT(10)
#define WAKEUP_BIT_AC_WAKE_ACK BIT(11)
#define WAKEUP_BIT_TEMP_SENSOR_HIGH BIT(12)
#define WAKEUP_BIT_MODEM_SW_RESET_REQ BIT(20)
#define WAKEUP_BIT_GPIO0 BIT(23)
#define WAKEUP_BIT_GPIO1 BIT(24)
#define WAKEUP_BIT_GPIO2 BIT(25)
#define WAKEUP_BIT_GPIO3 BIT(26)
#define WAKEUP_BIT_GPIO4 BIT(27)
#define WAKEUP_BIT_GPIO5 BIT(28)
#define WAKEUP_BIT_GPIO6 BIT(29)
#define WAKEUP_BIT_GPIO7 BIT(30)
#define WAKEUP_BIT_AC_REL_ACK BIT(30)

/*
 * This vector maps irq numbers to the bits in the bit field used in
 * communication with the PRCMU firmware.
 *
 * The reason for having this is to keep the irq numbers contiguous even though
 * the bits in the bit field are not. (The bits also have a tendency to move
 * around, to further complicate matters.)
 */
#define IRQ_INDEX(_name) ((IRQ_DB5500_PRCMU_##_name) - IRQ_DB5500_PRCMU_BASE)
#define IRQ_ENTRY(_name)[IRQ_INDEX(_name)] = (WAKEUP_BIT_##_name)
static u32 prcmu_irq_bit[NUM_DB5500_PRCMU_WAKEUPS] = {
	IRQ_ENTRY(RTC),
	IRQ_ENTRY(RTT0),
	IRQ_ENTRY(RTT1),
	IRQ_ENTRY(CD_IRQ),
	IRQ_ENTRY(SRP_TIM),
	IRQ_ENTRY(APE_REQ),
	IRQ_ENTRY(USB),
	IRQ_ENTRY(ABB),
	IRQ_ENTRY(LOW_POWER_AUDIO),
	IRQ_ENTRY(TEMP_SENSOR_LOW),
	IRQ_ENTRY(TEMP_SENSOR_HIGH),
	IRQ_ENTRY(ARM),
	IRQ_ENTRY(AC_WAKE_ACK),
	IRQ_ENTRY(MODEM_SW_RESET_REQ),
	IRQ_ENTRY(GPIO0),
	IRQ_ENTRY(GPIO1),
	IRQ_ENTRY(GPIO2),
	IRQ_ENTRY(GPIO3),
	IRQ_ENTRY(GPIO4),
	IRQ_ENTRY(GPIO5),
	IRQ_ENTRY(GPIO6),
	IRQ_ENTRY(GPIO7),
	IRQ_ENTRY(AC_REL_ACK),
};

#define VALID_WAKEUPS (BIT(NUM_PRCMU_WAKEUP_INDICES) - 1)
#define WAKEUP_ENTRY(_name)[PRCMU_WAKEUP_INDEX_##_name] = (WAKEUP_BIT_##_name)
static u32 prcmu_wakeup_bit[NUM_PRCMU_WAKEUP_INDICES] = {
	WAKEUP_ENTRY(RTC),
	WAKEUP_ENTRY(RTT0),
	WAKEUP_ENTRY(RTT1),
	WAKEUP_ENTRY(CD_IRQ),
	WAKEUP_ENTRY(USB),
	WAKEUP_ENTRY(ABB),
	WAKEUP_ENTRY(ARM)
};

/*
 * mb0_transfer - state needed for mailbox 0 communication.
 * @lock                The transaction lock.
 * @dbb_irqs_lock       lock used for (un)masking DBB wakeup interrupts
 * @mask_work:          Work structure used for (un)masking wakeup interrupts.
 * @ac_wake_lock:	mutex to lock modem_req and modem_rel
 * @req:                Request data that need to persist between requests.
 */
static struct {
	spinlock_t lock;
	spinlock_t dbb_irqs_lock;
	struct work_struct mask_work;
	struct mutex ac_wake_lock;
	struct {
		u32 dbb_irqs;
		u32 dbb_wakeups;
		u32 abb_events;
	} req;
} mb0_transfer;


/*
 * mb1_transfer - state needed for mailbox 1 communication.
 * @lock:	The transaction lock.
 * @work:	The transaction completion structure.
 * @req_arm_opp Requested arm opp
 * @req_ape_opp Requested ape opp
 * @ack:	Reply ("acknowledge") data.
 */
static struct {
	struct mutex lock;
	struct completion work;
	u8 req_arm_opp;
	u8 req_ape_opp;
	struct {
		u8 header;
		u8 arm_opp;
		u8 ape_opp;
		u8 arm_voltage_st;
		u8 ape_voltage_st;
	} ack;
} mb1_transfer;

/*
 * mb2_transfer - state needed for mailbox 2 communication.
 * @lock:      The transaction lock.
 * @work:      The transaction completion structure.
 * @req:       Request data that need to persist between requests.
 * @ack:       Reply ("acknowledge") data.
 */
static struct {
	struct mutex lock;
	struct completion work;
	struct {
		u8 epod_st[DB5500_NUM_EPOD_ID];
		u8 pll_st[DB5500_NUM_PLL_ID];
	} req;
	struct {
		u8 header;
		u8 status;
	} ack;
} mb2_transfer;

/*
 * mb3_transfer - state needed for mailbox 3 communication.
 * @sysclk_lock:	A lock used to handle concurrent sysclk requests.
 * @sysclk_work:	Work structure used for sysclk requests.
 * @req_st:		Requested clock state.
 * @ack:		Acknowledgement data
 */
static struct {
	struct mutex sysclk_lock;
	struct completion sysclk_work;
	enum sysclk_state req_st;
	struct {
		u8 header;
		u8 status;
	} ack;
} mb3_transfer;

/*
 * mb4_transfer - state needed for mailbox 4 communication.
 * @lock:       The transaction lock.
 * @work:       The transaction completion structure.
 * @ack:        Acknowledgement data
 */
static struct {
	struct mutex lock;
	struct completion work;
	struct {
		u8 header;
		u8 status;
	} ack;
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
		u8 header;
		u8 status;
		u8 value[4];
	} ack;
} mb5_transfer;

/* Spinlocks */
static DEFINE_SPINLOCK(clkout_lock);

/* PRCMU TCDM base IO address */
static __iomem void *tcdm_base;

/* PRCMU MTIMER base IO address */
static __iomem void *mtimer_base;

struct clk_mgt {
	unsigned int offset;
	u32 pllsw;
	u32 div;
	bool scalable;
	bool force50;
};

/* PRCMU Firmware Details */
static struct {
	u16 board;
	u8 fw_version;
	u8 api_version;
} prcmu_version;

static struct {
	u32 timeout;
	bool enabled;
} a9wdog_timer;

static DEFINE_SPINLOCK(clk_mgt_lock);

#define CLK_MGT_ENTRY(_name, _scalable)[PRCMU_##_name] = {	\
	.offset = DB5500_PRCM_##_name##_MGT,			\
	.scalable = _scalable,					\
}

static struct clk_mgt clk_mgt[PRCMU_NUM_REG_CLOCKS] = {
	CLK_MGT_ENTRY(SGACLK, true),
	CLK_MGT_ENTRY(UARTCLK, false),
	CLK_MGT_ENTRY(MSP02CLK, false),
	CLK_MGT_ENTRY(I2CCLK, false),
	[PRCMU_SDMMCCLK] {
		.offset		= DB5500_PRCM_SDMMCCLK_MGT,
		.force50	= true,
		.scalable	= false,

	},
	[PRCMU_SPARE1CLK] {
		.offset		= DB5500_PRCM_SPARE1CLK_MGT,
		.force50	= true,
		.scalable	= false,

	},
	CLK_MGT_ENTRY(PER1CLK, false),
	CLK_MGT_ENTRY(PER2CLK, true),
	CLK_MGT_ENTRY(PER3CLK, true),
	CLK_MGT_ENTRY(PER5CLK, false),	/* used for SPI */
	CLK_MGT_ENTRY(PER6CLK, true),
	CLK_MGT_ENTRY(PWMCLK, false),
	CLK_MGT_ENTRY(IRDACLK, false),
	CLK_MGT_ENTRY(IRRCCLK, false),
	CLK_MGT_ENTRY(HDMICLK, false),
	CLK_MGT_ENTRY(APEATCLK, false),
	CLK_MGT_ENTRY(APETRACECLK, true),
	CLK_MGT_ENTRY(MCDECLK, true),
	CLK_MGT_ENTRY(DSIALTCLK, false),
	CLK_MGT_ENTRY(DMACLK, true),
	CLK_MGT_ENTRY(B2R2CLK, true),
	CLK_MGT_ENTRY(TVCLK, false),
	CLK_MGT_ENTRY(RNGCLK, false),
	CLK_MGT_ENTRY(SIACLK, false),
	CLK_MGT_ENTRY(SVACLK, false),
	CLK_MGT_ENTRY(ACLK, true),
};

static atomic_t modem_req_state = ATOMIC_INIT(0);

bool db5500_prcmu_is_modem_requested(void)
{
	return (atomic_read(&modem_req_state) != 0);
}

/**
 * prcmu_modem_req - APE requests Modem to wake up
 *
 * Whenever APE wants to send message to the modem, it will have to call this
 * function to make sure that modem is awake.
 */
void prcmu_modem_req(void)
{
	u32 val;

	mutex_lock(&mb0_transfer.ac_wake_lock);

	val = readl(_PRCMU_BASE + PRCM_HOSTACCESS_REQ);
	if (val & PRCM_HOSTACCESS_REQ_BIT)
		goto unlock_and_return;

	writel((val | PRCM_HOSTACCESS_REQ_BIT),
		(_PRCMU_BASE + PRCM_HOSTACCESS_REQ));
	atomic_set(&modem_req_state, 1);

unlock_and_return:
	mutex_unlock(&mb0_transfer.ac_wake_lock);

}

/**
 * prcmu_modem_rel - APE has no more messages to send and hence releases modem.
 *
 * APE to Modem communication is initiated by modem_req and once the
 * communication is completed, APE sends modem_rel to complete the protocol.
 */
void prcmu_modem_rel(void)
{
	u32 val;

	mutex_lock(&mb0_transfer.ac_wake_lock);

	val = readl(_PRCMU_BASE + PRCM_HOSTACCESS_REQ);
	if (!(val & PRCM_HOSTACCESS_REQ_BIT))
		goto unlock_and_return;

	writel((val & ~PRCM_HOSTACCESS_REQ_BIT),
		(_PRCMU_BASE + PRCM_HOSTACCESS_REQ));

	atomic_set(&modem_req_state, 0);

unlock_and_return:
	mutex_unlock(&mb0_transfer.ac_wake_lock);
}

/**
 * prcm_ape_ack - send an acknowledgement to modem
 *
 * On ape receiving ape_req, APE will have to acknowledge for the interrupt
 * received. This function will send the acknowledgement by writing to the
 * prcmu register and an interrupt is trigerred to modem.
 */
void prcmu_ape_ack(void)
{
	writel(PRCM_APE_ACK_BIT, (_PRCMU_BASE + PRCM_APE_ACK));
}

/**
 * db5500_prcmu_modem_reset - Assert a Reset on modem
 *
 * This function will assert a reset request to the modem. Prior to that
 * PRCM_HOSTACCESS_REQ must be '0'.
 */
void db5500_prcmu_modem_reset(void)
{
	mutex_lock(&mb4_transfer.lock);

	/* PRCM_HOSTACCESS_REQ = 0, before asserting a reset */
	prcmu_modem_rel();
	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writeb(MB4H_CGF_MODEM_RESET, PRCM_REQ_MB4_HEADER);
	writel(MBOX_BIT(4), _PRCMU_BASE + PRCM_MBOX_CPU_SET);
	wait_for_completion(&mb4_transfer.work);
	if (mb4_transfer.ack.status != RC_SUCCESS ||
			mb4_transfer.ack.header != MB4H_CGF_MODEM_RESET)
		printk(KERN_ERR,
				"ACK not received for modem reset interrupt\n");
	mutex_unlock(&mb4_transfer.lock);
}

/**
 * prcmu_config_clkout - Configure one of the programmable clock outputs.
 * @clkout:	The CLKOUT number (0 or 1).
 * @source:	Clock source.
 * @div:	The divider to be applied.
 *
 * Configures one of the programmable clock outputs (CLKOUTs).
 */
int prcmu_config_clkout(u8 clkout, u8 source, u8 div)
{
	static bool configured[2] = {false, false};
	int r = 0;
	unsigned long flags;
	u32 sel_val;
	u32 div_val;
	u32 sel_bits;
	u32 div_bits;
	u32 sel_mask;
	u32 div_mask;
	u8 sel0 = CLKOUT_SEL0_SEL_CLK;
	u16 sel = 0;

	BUG_ON(clkout > DB5500_CLKOUT1);
	BUG_ON(source > DB5500_CLKOUT_IRDACLK);
	BUG_ON(div > 7);

	switch (source) {
	case DB5500_CLKOUT_REF_CLK_SEL0:
		sel0 = CLKOUT_SEL0_REF_CLK;
		break;
	case DB5500_CLKOUT_RTC_CLK0_SEL0:
		sel0 = CLKOUT_SEL0_RTC_CLK0;
		break;
	case DB5500_CLKOUT_ULP_CLK_SEL0:
		sel0 = CLKOUT_SEL0_ULP_CLK;
		break;
	case DB5500_CLKOUT_STATIC0:
		sel = CLKOUT_SEL_STATIC0;
		break;
	case DB5500_CLKOUT_REFCLK:
		sel = CLKOUT_SEL_REFCLK;
		break;
	case DB5500_CLKOUT_ULPCLK:
		sel = CLKOUT_SEL_ULPCLK;
		break;
	case DB5500_CLKOUT_ARMCLK:
		sel = CLKOUT_SEL_ARMCLK;
		break;
	case DB5500_CLKOUT_SYSACC0CLK:
		sel = CLKOUT_SEL_SYSACC0CLK;
		break;
	case DB5500_CLKOUT_SOC0PLLCLK:
		sel = CLKOUT_SEL_SOC0PLLCLK;
		break;
	case DB5500_CLKOUT_SOC1PLLCLK:
		sel = CLKOUT_SEL_SOC1PLLCLK;
		break;
	case DB5500_CLKOUT_DDRPLLCLK:
		sel = CLKOUT_SEL_DDRPLLCLK;
		break;
	case DB5500_CLKOUT_TVCLK:
		sel = CLKOUT_SEL_TVCLK;
		break;
	case DB5500_CLKOUT_IRDACLK:
		sel = CLKOUT_SEL_IRDACLK;
		break;
	}

	switch (clkout) {
	case DB5500_CLKOUT0:
		sel_mask = PRCM_CLKOCR_CLKOUT0_SEL0_MASK |
			PRCM_CLKOCR_CLKOUT0_SEL_MASK;
		sel_bits = ((sel0 << PRCM_CLKOCR_CLKOUT0_SEL0_SHIFT) |
			(sel << PRCM_CLKOCR_CLKOUT0_SEL_SHIFT));
		div_mask = PRCM_CLKODIV_CLKOUT0_DIV_MASK;
		div_bits = div << PRCM_CLKODIV_CLKOUT0_DIV_SHIFT;
		break;
	case DB5500_CLKOUT1:
		sel_mask = PRCM_CLKOCR_CLKOUT1_SEL0_MASK |
			PRCM_CLKOCR_CLKOUT1_SEL_MASK;
		sel_bits = ((sel0 << PRCM_CLKOCR_CLKOUT1_SEL0_SHIFT) |
			(sel << PRCM_CLKOCR_CLKOUT1_SEL_SHIFT));
		div_mask = PRCM_CLKODIV_CLKOUT1_DIV_MASK;
		div_bits = div << PRCM_CLKODIV_CLKOUT1_DIV_SHIFT;
		break;
	}

	spin_lock_irqsave(&clkout_lock, flags);

	if (configured[clkout]) {
		r = -EINVAL;
		goto unlock_and_return;
	}

	sel_val = readl(_PRCMU_BASE + PRCM_CLKOCR);
	writel((sel_bits | (sel_val & ~sel_mask)),
		(_PRCMU_BASE + PRCM_CLKOCR));

	div_val = readl(_PRCMU_BASE + PRCM_CLKODIV);
	writel((div_bits | (div_val & ~div_mask)),
		(_PRCMU_BASE + PRCM_CLKODIV));

	configured[clkout] = true;

unlock_and_return:
	spin_unlock_irqrestore(&clkout_lock, flags);

	return r;
}

static int request_sysclk(bool enable)
{
	int r;

	r = 0;
	mutex_lock(&mb3_transfer.sysclk_lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(3))
		cpu_relax();

	if (enable)
		mb3_transfer.req_st = SYSCLK_ON;
	else
		mb3_transfer.req_st = SYSCLK_OFF;

	writeb(mb3_transfer.req_st, (PRCM_REQ_MB3_REFCLK_MGT));

	writeb(MB3H_REFCLK_REQUEST, (PRCM_REQ_MB3_HEADER));
	writel(MBOX_BIT(3), _PRCMU_BASE + PRCM_MBOX_CPU_SET);

	/*
	 * The firmware only sends an ACK if we want to enable the
	 * SysClk, and it succeeds.
	 */
	if (!wait_for_completion_timeout(&mb3_transfer.sysclk_work,
			msecs_to_jiffies(20000))) {
		pr_err("prcmu: %s timed out (20 s) waiting for a reply.\n",
			__func__);
		r = -EIO;
		WARN(1, "Failed to set sysclk");
		goto unlock_and_return;
	}

	if ((mb3_transfer.ack.header != MB3H_REFCLK_REQUEST) ||
			(mb3_transfer.ack.status != mb3_transfer.req_st)) {
		r = -EIO;
	}

unlock_and_return:
	mutex_unlock(&mb3_transfer.sysclk_lock);

	return r;
}

static int request_timclk(bool enable)
{
	u32 val = (PRCM_TCR_DOZE_MODE | PRCM_TCR_TENSEL_MASK);

	if (!enable)
		val |= PRCM_TCR_STOP_TIMERS;
	writel(val, _PRCMU_BASE + PRCM_TCR);

	return 0;
}

static int request_clk(u8 clock, bool enable)
{
	int r = 0;

	BUG_ON(clock >= DB5500_NUM_CLK_CLIENTS);

	mutex_lock(&mb2_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(2))
		cpu_relax();

	/* fill in mailbox */
	writeb(clock, PRCM_REQ_MB2_CLK_CLIENT);
	writeb(enable, PRCM_REQ_MB2_CLK_STATE);

	writeb(MB2H_CLK_REQUEST, PRCM_REQ_MB2_HEADER);

	writel(MBOX_BIT(2), _PRCMU_BASE + PRCM_MBOX_CPU_SET);
	if (!wait_for_completion_timeout(&mb2_transfer.work,
		msecs_to_jiffies(20000))) {
		pr_err("prcmu: request_clk() failed.\n");
		r = -EIO;
		WARN(1, "Failed in request_clk");
		goto unlock_and_return;
	}
	if (mb2_transfer.ack.status != RC_SUCCESS ||
		mb2_transfer.ack.header != MB2H_CLK_REQUEST)
		r = -EIO;

unlock_and_return:
	mutex_unlock(&mb2_transfer.lock);
	return r;
}

static int request_reg_clock(u8 clock, bool enable)
{
	u32 val;
	unsigned long flags;

	WARN_ON(!clk_mgt[clock].offset);

	spin_lock_irqsave(&clk_mgt_lock, flags);

	/* Grab the HW semaphore. */
	while ((readl(_PRCMU_BASE + PRCM_SEM) & PRCM_SEM_PRCM_SEM) != 0)
		cpu_relax();

	val = readl(_PRCMU_BASE + clk_mgt[clock].offset);
	if (enable) {
		val |= (PRCM_CLK_MGT_CLKEN | clk_mgt[clock].pllsw);
	} else {
		clk_mgt[clock].pllsw = (val & PRCM_CLK_MGT_CLKPLLSW_MASK);
		val &= ~(PRCM_CLK_MGT_CLKEN | PRCM_CLK_MGT_CLKPLLSW_MASK);
	}
	writel(val, (_PRCMU_BASE + clk_mgt[clock].offset));

	/* Release the HW semaphore. */
	writel(0, _PRCMU_BASE + PRCM_SEM);

	spin_unlock_irqrestore(&clk_mgt_lock, flags);

	return 0;
}

/*
 * request_pll() - Request for a pll to be enabled or disabled.
 * @pll:        The pll for which the request is made.
 * @enable:     Whether the clock should be enabled (true) or disabled (false).
 *
 * This function should only be used by the clock implementation.
 * Do not use it from any other place!
 */
static int request_pll(u8 pll, bool enable)
{
	int r = 0;

	BUG_ON(pll >= DB5500_NUM_PLL_ID);
	mutex_lock(&mb2_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(2))
		cpu_relax();

	mb2_transfer.req.pll_st[pll] = enable;

	/* fill in mailbox */
	writeb(pll, PRCM_REQ_MB2_PLL_CLIENT);
	writeb(mb2_transfer.req.pll_st[pll], PRCM_REQ_MB2_PLL_STATE);

	writeb(MB2H_PLL_REQUEST, PRCM_REQ_MB2_HEADER);

	writel(MBOX_BIT(2), _PRCMU_BASE + PRCM_MBOX_CPU_SET);
	if (!wait_for_completion_timeout(&mb2_transfer.work,
		msecs_to_jiffies(20000))) {
		pr_err("prcmu: set_pll() failed.\n");
		r = -EIO;
		WARN(1, "Failed to set pll");
		goto unlock_and_return;
	}
	if (mb2_transfer.ack.status != RC_SUCCESS ||
		mb2_transfer.ack.header != MB2H_PLL_REQUEST)
		r = -EIO;

unlock_and_return:
	mutex_unlock(&mb2_transfer.lock);

	return r;
}

/**
 * db5500_prcmu_request_clock() - Request for a clock to be enabled or disabled.
 * @clock:      The clock for which the request is made.
 * @enable:     Whether the clock should be enabled (true) or disabled (false).
 *
 * This function should only be used by the clock implementation.
 * Do not use it from any other place!
 */
int db5500_prcmu_request_clock(u8 clock, bool enable)
{
	/* MSP1 & CD clocks are handled by FW */
	if (clock == PRCMU_MSP1CLK)
		return request_clk(DB5500_MSP1CLK, enable);
	else if (clock == PRCMU_CDCLK)
		return request_clk(DB5500_CDCLK, enable);
	else if (clock == PRCMU_IRDACLK)
		return request_clk(DB5500_IRDACLK, enable);
	else if (clock < PRCMU_NUM_REG_CLOCKS)
		return request_reg_clock(clock, enable);
	else if (clock == PRCMU_TIMCLK)
		return request_timclk(enable);
	else if (clock == PRCMU_PLLSOC0)
		return request_pll(DB5500_PLL_SOC0, enable);
	else if (clock == PRCMU_PLLSOC1)
		return request_pll(DB5500_PLL_SOC1, enable);
	else if (clock == PRCMU_PLLDDR)
		return request_pll(DB5500_PLL_DDR, enable);
	else if (clock == PRCMU_SYSCLK)
		return request_sysclk(enable);
	else
		return -EINVAL;
}

/* This function should only be called while mb0_transfer.lock is held. */
static void config_wakeups(void)
{
	static u32 last_dbb_events;
	static u32 last_abb_events;
	u32 dbb_events;
	u32 abb_events;

	dbb_events = mb0_transfer.req.dbb_irqs | mb0_transfer.req.dbb_wakeups;

	abb_events = mb0_transfer.req.abb_events;

	if ((dbb_events == last_dbb_events) && (abb_events == last_abb_events))
		return;

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
		cpu_relax();

	writel(dbb_events, PRCM_REQ_MB0_WAKEUP_DBB);
	writel(abb_events, PRCM_REQ_MB0_WAKEUP_ABB);
	writeb(MB0H_WAKE_UP_CFG, PRCM_REQ_MB0_HEADER);
	writel(MBOX_BIT(0), _PRCMU_BASE + PRCM_MBOX_CPU_SET);

	last_dbb_events = dbb_events;
	last_abb_events = abb_events;
}

int db5500_prcmu_config_esram0_deep_sleep(u8 state)
{
	unsigned long flags;

	if ((state > ESRAM0_DEEP_SLEEP_STATE_RET) ||
	    (state < ESRAM0_DEEP_SLEEP_STATE_OFF))
		return -EINVAL;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	if (state == ESRAM0_DEEP_SLEEP_STATE_RET)
		writeb(RET_ST, PRCM_REQ_MB0_ESRAM0_STATE);
	else
		writeb(OFF_ST, PRCM_REQ_MB0_ESRAM0_STATE);

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);

	return 0;
}

int db5500_prcmu_set_power_state(u8 state, bool keep_ulp_clk, bool keep_ap_pll)
{
	int r = 0;
	unsigned long flags;

	/* Deep Idle is not supported in DB5500 */
	BUG_ON((state < PRCMU_AP_SLEEP) || (state >= PRCMU_AP_DEEP_IDLE));

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
		cpu_relax();

	switch (state) {
	case PRCMU_AP_IDLE:
		writeb(DB5500_AP_IDLE, PRCM_REQ_MB0_AP_POWER_STATE);
		/* TODO: Can be high latency */
		writeb(DDR_PWR_STATE_UNCHANGED, PRCM_REQ_MB0_DDR_STATE);
		break;
	case PRCMU_AP_SLEEP:
		writeb(DB5500_AP_SLEEP, PRCM_REQ_MB0_AP_POWER_STATE);
		break;
	case PRCMU_AP_DEEP_SLEEP:
		writeb(DB5500_AP_DEEP_SLEEP, PRCM_REQ_MB0_AP_POWER_STATE);
		break;
	default:
		r = -EINVAL;
		goto unlock_return;
	}
	writeb((keep_ap_pll ? 1 : 0), PRCM_REQ_MB0_AP_PLL_STATE);
	writeb((keep_ulp_clk ? 1 : 0), PRCM_REQ_MB0_ULP_CLOCK_STATE);

	writeb(MB0H_PWR_STATE_TRANS, PRCM_REQ_MB0_HEADER);
	writel(MBOX_BIT(0), _PRCMU_BASE + PRCM_MBOX_CPU_SET);

unlock_return:
	spin_unlock_irqrestore(&mb0_transfer.lock, flags);

	return r;
}

u8 db5500_prcmu_get_power_state_result(void)
{
	u8 status = readb_relaxed(PRCM_ACK_MB0_AP_PWRSTTR_STATUS);

	/*
	 * Callers expect all the status values to match 8500.  Adjust for
	 * PendingReq_Er (0x2b).
	 */
	if (status == 0x2b)
		status = PRCMU_PRCMU2ARMPENDINGIT_ER;

	return status;
}

void db5500_prcmu_enable_wakeups(u32 wakeups)
{
	unsigned long flags;
	u32 bits;
	int i;

	BUG_ON(wakeups != (wakeups & VALID_WAKEUPS));

	for (i = 0, bits = 0; i < NUM_PRCMU_WAKEUP_INDICES; i++) {
		if (wakeups & BIT(i)) {
			if (prcmu_wakeup_bit[i] == 0)
				WARN(1, "WAKEUP NOT SUPPORTED");
			else
				bits |= prcmu_wakeup_bit[i];
		}
	}

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	mb0_transfer.req.dbb_wakeups = bits;
	config_wakeups();

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

void db5500_prcmu_config_abb_event_readout(u32 abb_events)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	mb0_transfer.req.abb_events = abb_events;
	config_wakeups();

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

void db5500_prcmu_get_abb_event_buffer(void __iomem **buf)
{
	if (readb(PRCM_ACK_MB0_READ_POINTER) & 1)
		*buf = (PRCM_ACK_MB0_WAKEUP_1_ABB);
	else
		*buf = (PRCM_ACK_MB0_WAKEUP_0_ABB);
}

/* This function should be called with lock */
static int mailbox4_request(u8 mb4_request, u8 ack_request)
{
	int ret = 0;

	writeb(mb4_request, PRCM_REQ_MB4_HEADER);
	writel(MBOX_BIT(4), (_PRCMU_BASE + PRCM_MBOX_CPU_SET));

	if (!wait_for_completion_timeout(&mb4_transfer.work,
		msecs_to_jiffies(20000))) {
		pr_err("prcmu: MB4 request %d failed", mb4_request);
		ret = -EIO;
		WARN(1, "prcmu: failed mb4 request");
		goto failed;
	}

	if (mb4_transfer.ack.header != ack_request ||
		mb4_transfer.ack.status != RC_SUCCESS)
		ret = -EIO;
failed:
	return ret;
}

int db5500_prcmu_get_hotdog(void)
{
	return readw(PRCM_SHARE_INFO_HOTDOG);
}

static int config_hotdog(u8 threshold)
{
	int r = 0;

	mutex_lock(&mb4_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writew(threshold, PRCM_REQ_MB4_HOTDOG_THRESHOLD);
	r = mailbox4_request(MB4H_CFG_HOTDOG, MB4H_ACK_CFG_HOTDOG);

	mutex_unlock(&mb4_transfer.lock);

	return r;
}

static int config_hotmon(u8 low, u8 high)
{
	int r = 0;

	mutex_lock(&mb4_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writew(low, PRCM_REQ_MB4_HOTMON_LOW);
	writew(high, PRCM_REQ_MB4_HOTMON_HIGH);

	r = mailbox4_request(MB4H_CFG_HOTMON, MB4H_ACK_CFG_HOTMON);

	mutex_unlock(&mb4_transfer.lock);

	return r;
}

static int config_hot_period(u16 val)
{
	int r = 0;

	mutex_lock(&mb4_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	writew(val, PRCM_REQ_MB4_HOT_PERIOD);
	r = mailbox4_request(MB4H_CFG_HOTPERIOD, MB4H_ACK_CFG_HOTPERIOD);

	mutex_unlock(&mb4_transfer.lock);

	return r;
}

/*
 * period in milli seconds
 */
static int start_temp_sense(u16 period)
{
	if (period == 0xFFFF)
		return -EINVAL;

	return config_hot_period(period);
}

static int stop_temp_sense(void)
{
	return config_hot_period(0xFFFF);
}

static int prcmu_a9wdog(u8 req, u8 ack)
{
	int r = 0;

	mutex_lock(&mb4_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(4))
		cpu_relax();

	r = mailbox4_request(req, ack);

	mutex_unlock(&mb4_transfer.lock);

	return r;
}

static void prcmu_a9wdog_set_interrupt(bool enable)
{
	if (enable) {
		writel(PRCM_TIMER0_IRQ_RTOS1_SET,
			(mtimer_base + PRCM_TIMER0_IRQ_EN_SET_OFFSET));
	} else {
		writel(PRCM_TIMER0_IRQ_RTOS1_CLR,
			(mtimer_base + PRCM_TIMER0_IRQ_EN_CLR_OFFSET));
	}
}

static void prcmu_a9wdog_set_timeout(u32 timeout)
{
	u32 comp_timeout;

	comp_timeout = readl(mtimer_base + PRCM_TIMER0_RTOS_COUNTER_OFFSET) +
			timeout;
	writel(comp_timeout, mtimer_base + PRCM_TIMER0_RTOS_COMP1_OFFSET);
}

static int config_a9wdog(u8 num, bool sleep_auto_off)
{
	/*
	 * Sleep auto off feature is not supported. Resume and
	 * suspend will be handled by watchdog driver.
         */
	return 0;
}

static int enable_a9wdog(u8 id)
{
	int r = 0;

	if (a9wdog_timer.enabled)
		return -EPERM;

	prcmu_a9wdog_set_interrupt(true);

	r = prcmu_a9wdog(MB4H_CGF_A9WDOG_EN_PREBARK,
			MB4H_ACK_CGF_A9WDOG_EN_PREBARK);
	if (!r)
		a9wdog_timer.enabled = true;
	else
		prcmu_a9wdog_set_interrupt(false);

	return r;
}

static int disable_a9wdog(u8 id)
{
	if (!a9wdog_timer.enabled)
		return -EPERM;

	prcmu_a9wdog_set_interrupt(false);

	a9wdog_timer.enabled = false;

	return prcmu_a9wdog(MB4H_CGF_A9WDOG_DIS,
			MB4H_ACK_CGF_A9WDOG_DIS);
}

static int kick_a9wdog(u8 id)
{
	int r = 0;

	if (a9wdog_timer.enabled)
		prcmu_a9wdog_set_timeout(a9wdog_timer.timeout);
	else
		r = -EPERM;

	return r;
}

static int load_a9wdog(u8 id, u32 timeout)
{
	if (a9wdog_timer.enabled)
		return -EPERM;

	prcmu_a9wdog_set_timeout(timeout);
	a9wdog_timer.timeout = timeout;

	return 0;
}

/**
 * db5500_prcmu_abb_read() - Read register value(s) from the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The read out value(s).
 * @size:	The number of registers to read.
 *
 * Reads register value(s) from the ABB.
 * @size has to be <= 4.
 */
int db5500_prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if ((size < 1) || (4 < size))
		return -EINVAL;

	mutex_lock(&mb5_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();
	writeb(slave, PRCM_REQ_MB5_I2C_SLAVE);
	writeb(reg, PRCM_REQ_MB5_I2C_REG);
	writeb(size, PRCM_REQ_MB5_I2C_SIZE);
	writeb(MB5H_I2C_READ, PRCM_REQ_MB5_HEADER);

	writel(MBOX_BIT(5), _PRCMU_BASE + PRCM_MBOX_CPU_SET);
	wait_for_completion(&mb5_transfer.work);

	r = 0;
	if ((mb5_transfer.ack.header == MB5H_I2C_READ) &&
		(mb5_transfer.ack.status == RC_SUCCESS))
		memcpy(value, mb5_transfer.ack.value, (size_t)size);
	else
		r = -EIO;

	mutex_unlock(&mb5_transfer.lock);

	return r;
}

/**
 * db5500_prcmu_abb_write() - Write register value(s) to the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The value(s) to write.
 * @size:	The number of registers to write.
 *
 * Writes register value(s) to the ABB.
 * @size has to be <= 4.
 */
int db5500_prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if ((size < 1) || (4 < size))
		return -EINVAL;

	mutex_lock(&mb5_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();
	writeb(slave, PRCM_REQ_MB5_I2C_SLAVE);
	writeb(reg, PRCM_REQ_MB5_I2C_REG);
	writeb(size, PRCM_REQ_MB5_I2C_SIZE);
	memcpy_toio(PRCM_REQ_MB5_I2C_DATA, value, size);
	writeb(MB5H_I2C_WRITE, PRCM_REQ_MB5_HEADER);

	writel(MBOX_BIT(5), _PRCMU_BASE + PRCM_MBOX_CPU_SET);
	wait_for_completion(&mb5_transfer.work);

	if ((mb5_transfer.ack.header == MB5H_I2C_WRITE) &&
		(mb5_transfer.ack.status == RC_SUCCESS))
		r = 0;
	else
		r = -EIO;

	mutex_unlock(&mb5_transfer.lock);

	return r;
}

/**
 * db5500_prcmu_set_arm_opp - set the appropriate ARM OPP
 * @opp: The new ARM operating point to which transition is to be made
 * Returns: 0 on success, non-zero on failure
 *
 * This function sets the the operating point of the ARM.
 */
int db5500_prcmu_set_arm_opp(u8 opp)
{
	int r;
	u8 db5500_opp;

	r = 0;

	switch (opp) {
	case ARM_EXTCLK:
		db5500_opp = DB5500_ARM_EXT_OPP;
		break;
	case ARM_50_OPP:
		db5500_opp = DB5500_ARM_50_OPP;
		break;
	case ARM_100_OPP:
		db5500_opp = DB5500_ARM_100_OPP;
		break;
	default:
		pr_err("prcmu: %s() received wrong opp value: %d\n",
				__func__, opp);
		r = -EINVAL;
		goto bailout;
	}

	mutex_lock(&mb1_transfer.lock);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(1))
		cpu_relax();

	writeb(MB1H_ARM_OPP, PRCM_REQ_MB1_HEADER);

	writeb(db5500_opp, PRCM_REQ_MB1_ARM_OPP);
	writel(MBOX_BIT(1), _PRCMU_BASE + PRCM_MBOX_CPU_SET);

	if (!wait_for_completion_timeout(&mb1_transfer.work,
		msecs_to_jiffies(20000))) {
		r = -EIO;
		WARN(1, "prcmu: failed to set arm opp");
		goto unlock_and_return;
	}

	if (mb1_transfer.ack.header != MB1H_ARM_OPP ||
		(mb1_transfer.ack.arm_opp != db5500_opp) ||
		(mb1_transfer.ack.arm_voltage_st != RC_SUCCESS))
		r = -EIO;

unlock_and_return:
	mutex_unlock(&mb1_transfer.lock);
bailout:
	if (!r)
		prcmu_debug_arm_opp_log(opp);
	return r;
}

static void __init prcmu_ape_clocks_init(void)
{
	u8 opp = db5500_prcmu_get_ape_opp();
	unsigned long flags;
	int i;

	WARN(opp != APE_100_OPP, "%s: Initial APE OPP (%u) not 100%%?\n",
	     __func__, opp);

	for (i = 0; i < PRCMU_NUM_REG_CLOCKS; i++) {
		struct clk_mgt *clkmgt = &clk_mgt[i];
		u32 clkval;
		u32 div;

		if (!clkmgt->scalable && !clkmgt->force50)
			continue;

		spin_lock_irqsave(&clk_mgt_lock, flags);

		clkval = readl(_PRCMU_BASE + clkmgt->offset);
		div = clkval & PRCM_CLK_MGT_CLKPLLDIV_MASK;
		div >>= PRCM_CLK_MGT_CLKPLLDIV_SHIFT;

		if (clkmgt->force50) {
			div *= 2;

			clkval &= ~PRCM_CLK_MGT_CLKPLLDIV_MASK;
			clkval |= div << PRCM_CLK_MGT_CLKPLLDIV_SHIFT;
			writel(clkval, _PRCMU_BASE + clkmgt->offset);

			spin_unlock_irqrestore(&clk_mgt_lock, flags);
			continue;
		}

		spin_unlock_irqrestore(&clk_mgt_lock, flags);

		clkmgt->div = div;
		if (!div)
			pr_err("%s: scalable clock at offset %#x has zero divisor\n",
			       __func__, clkmgt->offset);
	}
}

static void prcmu_ape_clocks_scale(u8 opp)
{
	unsigned long irqflags;
	unsigned int i;
	u32 clkval;

	/*
	 * Note: calling printk() under the following lock can cause lock
	 * recursion via clk_enable() for the console UART!
	 */
	spin_lock_irqsave(&clk_mgt_lock, irqflags);

	/* take a lock on HW (HWSEM)*/
	while ((readl(_PRCMU_BASE + PRCM_SEM) & PRCM_SEM_PRCM_SEM) != 0)
		cpu_relax();

	for (i = 0; i < PRCMU_NUM_REG_CLOCKS; i++) {
		u32 divval;

		if (!clk_mgt[i].scalable)
			continue;

		clkval = readl(_PRCMU_BASE + clk_mgt[i].offset);
		divval = clk_mgt[i].div;

		pr_debug("PRCMU: reg %#x prev clk = 0x%x stored div = 0x%x\n",
			 clk_mgt[i].offset, clkval, divval);

		if (opp == DB5500_APE_50_OPP)
			divval *= 2;

		clkval &= ~PRCM_CLK_MGT_CLKPLLDIV_MASK;
		clkval |= divval << PRCM_CLK_MGT_CLKPLLDIV_SHIFT;

		pr_debug("PRCMU: wr 0x%x in reg 0x%x\n",
			 clkval, clk_mgt[i].offset);

		writel(clkval, _PRCMU_BASE + clk_mgt[i].offset);
	}

	/* release lock */
	writel(0, (_PRCMU_BASE + PRCM_SEM));

	spin_unlock_irqrestore(&clk_mgt_lock, irqflags);
}
/* Divide the frequency of certain clocks by 2 for APE_50_PARTLY_25_OPP. */
static void request_even_slower_clocks(bool enable)
{
	void __iomem *clock_reg[] = {
		(_PRCMU_BASE + DB5500_PRCM_ACLK_MGT),
		(_PRCMU_BASE + DB5500_PRCM_DMACLK_MGT)
	};
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&clk_mgt_lock, flags);

	/* Grab the HW semaphore. */
	while ((readl(_PRCMU_BASE + PRCM_SEM) & PRCM_SEM_PRCM_SEM) != 0)
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
	writel(0, _PRCMU_BASE + PRCM_SEM);

	spin_unlock_irqrestore(&clk_mgt_lock, flags);
}
int db5500_prcmu_set_ape_opp(u8 opp)
{
	int ret = 0;
	u8 db5500_opp;
	if (opp == mb1_transfer.req_ape_opp)
		return 0;

	switch (opp) {
	case APE_100_OPP:
		db5500_opp = DB5500_APE_100_OPP;
		break;
	case APE_50_OPP:
	case APE_50_PARTLY_25_OPP:
		db5500_opp = DB5500_APE_50_OPP;
		break;
	default:
		pr_err("prcmu: %s() received wrong opp value: %d\n",
				__func__, opp);
		ret = -EINVAL;
		goto bailout;
	}

	mutex_lock(&mb1_transfer.lock);
	if (mb1_transfer.req_ape_opp == APE_50_PARTLY_25_OPP)
		request_even_slower_clocks(false);
	if ((opp != APE_100_OPP) && (mb1_transfer.req_ape_opp != APE_100_OPP))
		goto skip_message;

	prcmu_ape_clocks_scale(db5500_opp);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(1))
		cpu_relax();

	writeb(MB1H_APE_OPP, PRCM_REQ_MB1_HEADER);
	writeb(db5500_opp, PRCM_REQ_MB1_APE_OPP);
	writel(MBOX_BIT(1), (_PRCMU_BASE + PRCM_MBOX_CPU_SET));

	if (!wait_for_completion_timeout(&mb1_transfer.work,
		msecs_to_jiffies(20000))) {
		ret = -EIO;
		WARN(1, "prcmu: failed to set ape opp to %u", opp);
		goto unlock_and_return;
	}

	if (mb1_transfer.ack.header != MB1H_APE_OPP ||
		(mb1_transfer.ack.ape_opp != db5500_opp) ||
		(mb1_transfer.ack.arm_voltage_st != RC_SUCCESS))
		ret = -EIO;

skip_message:
	if ((!ret && (opp == APE_50_PARTLY_25_OPP)) ||
		(ret && (mb1_transfer.req_ape_opp == APE_50_PARTLY_25_OPP)))
			request_even_slower_clocks(true);
	if (!ret)
		mb1_transfer.req_ape_opp = opp;
unlock_and_return:
	mutex_unlock(&mb1_transfer.lock);
bailout:
	return ret;
}

int db5500_prcmu_get_ape_opp(void)
{
	u8 opp = readb(PRCM_ACK_MB1_CURRENT_APE_OPP);

	switch (opp) {
	case DB5500_APE_100_OPP:
		return APE_100_OPP;
	case DB5500_APE_50_OPP:
		return APE_50_OPP;
	default:
		pr_err("prcmu: %s() read unknown opp value: %d\n",
				__func__, opp);
		return APE_100_OPP;
	}
}

int db5500_prcmu_get_ddr_opp(void)
{
	return readb(_PRCMU_BASE + PRCM_DDR_SUBSYS_APE_MINBW);
}

int db5500_prcmu_set_ddr_opp(u8 opp)
{
	if (opp != DDR_100_OPP && opp != DDR_50_OPP)
		return -EINVAL;

	writeb(opp, _PRCMU_BASE + PRCM_DDR_SUBSYS_APE_MINBW);

	return 0;
}

/**
 * db5500_prcmu_get_arm_opp - get the current ARM OPP
 *
 * Returns: the current ARM OPP
 */
int db5500_prcmu_get_arm_opp(void)
{
	u8 opp = readb(PRCM_ACK_MB1_CURRENT_ARM_OPP);

	switch (opp) {
	case DB5500_ARM_EXT_OPP:
		return ARM_EXTCLK;
	case DB5500_ARM_50_OPP:
		return ARM_50_OPP;
	case DB5500_ARM_100_OPP:
		return ARM_100_OPP;
	default:
		pr_err("prcmu: %s() read unknown opp value: %d\n",
				__func__, opp);
		return ARM_100_OPP;
	}
}

int prcmu_resetout(u8 resoutn, u8 state)
{
	int offset;
	int pin = -1;

	offset = state > 0 ? PRCM_RESOUTN_SET_OFFSET : PRCM_RESOUTN_CLR_OFFSET;

	switch (resoutn) {
	case 0:
		pin = PRCMU_RESOUTN0_PIN;
		break;
	case 1:
		pin = PRCMU_RESOUTN1_PIN;
		break;
	case 2:
		pin = PRCMU_RESOUTN2_PIN;
	default:
		break;
	}

	if (pin > 0)
		writel(pin, _PRCMU_BASE + offset);
	else
		return -EINVAL;

	return 0;
}

int db5500_prcmu_enable_dsipll(void)
{
	int i;
	int ret = 0;

	/* Enable DSIPLL_RESETN resets */
	writel(PRCMU_RESET_DSIPLL, _PRCMU_BASE + PRCM_APE_RESETN_CLR);
	/* Unclamp DSIPLL in/out */
	writel(PRCMU_UNCLAMP_DSIPLL, _PRCMU_BASE + PRCM_MMIP_LS_CLAMP_CLR);
	/* Set DSI PLL FREQ */
	writel(PRCMU_PLLDSI_FREQ_SETTING, _PRCMU_BASE + PRCM_PLLDSI_FREQ);
	writel(PRCMU_DSI_PLLOUT_SEL_SETTING,
		_PRCMU_BASE + PRCM_DSI_PLLOUT_SEL);
	/* Enable Escape clocks */
	writel(PRCMU_ENABLE_ESCAPE_CLOCK_DIV, _PRCMU_BASE + PRCM_DSITVCLK_DIV);

	/* Start DSI PLL */
	writel(PRCMU_ENABLE_PLLDSI, _PRCMU_BASE + PRCM_PLLDSI_ENABLE);
	/* Reset DSI PLL */
	writel(PRCMU_DSI_RESET_SW, _PRCMU_BASE + PRCM_DSI_SW_RESET);
	for (i = 0; i < 10; i++) {
		if ((readl(_PRCMU_BASE + PRCM_PLLDSI_LOCKP) &
			PRCMU_PLLDSI_LOCKP_LOCKED) == PRCMU_PLLDSI_LOCKP_LOCKED)
			break;
		udelay(100);
	}

	if ((readl(_PRCMU_BASE + PRCM_PLLDSI_LOCKP) &
			PRCMU_PLLDSI_LOCKP_LOCKED)
					!= PRCMU_PLLDSI_LOCKP_LOCKED)
		ret = -EIO;
	/* Release DSIPLL_RESETN */
	writel(PRCMU_RESET_DSIPLL, _PRCMU_BASE + PRCM_APE_RESETN_SET);
	return ret;
}

int db5500_prcmu_disable_dsipll(void)
{
	/* Disable dsi pll */
	writel(PRCMU_DISABLE_PLLDSI, _PRCMU_BASE + PRCM_PLLDSI_ENABLE);
	/* Disable  escapeclock */
	writel(PRCMU_DISABLE_ESCAPE_CLOCK_DIV, _PRCMU_BASE + PRCM_DSITVCLK_DIV);
	return 0;
}

int db5500_prcmu_set_display_clocks(void)
{
	/* HDMI and TVCLK Should be handled somewhere else */
	/* PLLDIV=8, PLLSW=2, CLKEN=1 */
	writel(PRCMU_DSI_CLOCK_SETTING, _PRCMU_BASE + DB5500_PRCM_HDMICLK_MGT);
	/* PLLDIV=14, PLLSW=2, CLKEN=1 */
	writel(PRCMU_DSI_LP_CLOCK_SETTING, _PRCMU_BASE + DB5500_PRCM_TVCLK_MGT);
	return 0;
}

u32 db5500_prcmu_read(unsigned int reg)
{
	return readl_relaxed(_PRCMU_BASE + reg);
}

void db5500_prcmu_write(unsigned int reg, u32 value)
{
	writel_relaxed(value, _PRCMU_BASE + reg);
}

void db5500_prcmu_write_masked(unsigned int reg, u32 mask, u32 value)
{
	u32 val;

	val = readl_relaxed(_PRCMU_BASE + reg);
	val = (val & ~mask) | (value & mask);
	writel_relaxed(val, _PRCMU_BASE + reg);
}

/**
 * db5500_prcmu_system_reset - System reset
 *
 * Saves the reset reason code and then sets the APE_SOFTRST register which
 * fires an interrupt to fw
 */
void db5500_prcmu_system_reset(u16 reset_code)
{
	writew(reset_code, PRCM_SW_RST_REASON);
	writel(1, _PRCMU_BASE + PRCM_APE_SOFTRST);
}

/**
 * db5500_prcmu_get_reset_code - Retrieve SW reset reason code
 *
 * Retrieves the reset reason code stored by prcmu_system_reset() before
 * last restart.
 */
u16 db5500_prcmu_get_reset_code(void)
{
	return readw(PRCM_SW_RST_REASON);
}

static void ack_dbb_wakeup(void)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
		cpu_relax();

	writeb(MB0H_RD_WAKE_UP_ACK, PRCM_REQ_MB0_HEADER);
	writel(MBOX_BIT(0), _PRCMU_BASE + PRCM_MBOX_CPU_SET);

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

static int set_epod(u16 epod, u8 epod_state)
{
	int r = 0;
	bool ram_retention = false;

	/* check argument */
	BUG_ON(epod < DB5500_EPOD_ID_BASE);
	BUG_ON(epod_state > EPOD_STATE_ON);
	BUG_ON((epod - DB5500_EPOD_ID_BASE) >= DB5500_NUM_EPOD_ID);

	if (epod == DB5500_EPOD_ID_ESRAM12)
		ram_retention = true;

	/* check argument */
	BUG_ON(epod_state == EPOD_STATE_RAMRET && !ram_retention);

	/* get lock */
	mutex_lock(&mb2_transfer.lock);

	/* wait for mailbox */
	while (readl(_PRCMU_BASE + PRCM_MBOX_CPU_VAL) & MBOX_BIT(2))
		cpu_relax();

	/* Retention is allowed only for ESRAM12 */
	if (epod  == DB5500_EPOD_ID_ESRAM12) {
		switch (epod_state) {
		case EPOD_STATE_ON:
			mb2_transfer.req.epod_st[epod - DB5500_EPOD_ID_BASE] =
				EPOD_OOR_ON;
			break;
		case EPOD_STATE_OFF:
			mb2_transfer.req.epod_st[epod - DB5500_EPOD_ID_BASE] =
				EPOD_OOR_OFF;
			break;
		case EPOD_STATE_RAMRET:
			mb2_transfer.req.epod_st[epod - DB5500_EPOD_ID_BASE] =
				EPOD_OOR_RET;
			break;
		default:
			r = -EINVAL;
			goto unlock_and_return;
			break;
		}
	} else {
		if (epod_state == EPOD_STATE_ON)
			mb2_transfer.req.epod_st[epod - DB5500_EPOD_ID_BASE] =
				EPOD_ON;
		else if (epod_state == EPOD_STATE_OFF)
			mb2_transfer.req.epod_st[epod - DB5500_EPOD_ID_BASE] =
				EPOD_OFF;
		else {
			r = -EINVAL;
			goto unlock_and_return;
		}
	}
	/* fill in mailbox */
	writeb((epod - DB5500_EPOD_ID_BASE), PRCM_REQ_MB2_EPOD_CLIENT);
	writeb(mb2_transfer.req.epod_st[epod - DB5500_EPOD_ID_BASE],
		PRCM_REQ_MB2_EPOD_STATE);

	writeb(MB2H_EPOD_REQUEST, PRCM_REQ_MB2_HEADER);

	writel(MBOX_BIT(2), _PRCMU_BASE + PRCM_MBOX_CPU_SET);

	if (!wait_for_completion_timeout(&mb2_transfer.work,
		msecs_to_jiffies(20000))) {
		pr_err("prcmu: set_epod() failed.\n");
		r = -EIO;
		WARN(1, "Failed to set epod");
		goto unlock_and_return;
	}

	if (mb2_transfer.ack.status != RC_SUCCESS ||
		mb2_transfer.ack.header != MB2H_EPOD_REQUEST)
		r = -EIO;

unlock_and_return:
	mutex_unlock(&mb2_transfer.lock);
	return r;
}

static inline void print_unknown_header_warning(u8 n, u8 header)
{
	pr_warning("prcmu: Unknown message header (%d) in mailbox %d.\n",
		header, n);
}

static bool read_mailbox_0(void)
{
	bool r;
	u32 ev;
	unsigned int n;

	u8 header;

	header = readb(PRCM_ACK_MB0_HEADER);
	switch (header) {
	case MB0H_WAKE_UP:
		if (readb(PRCM_ACK_MB0_READ_POINTER) & 1)
			ev = readl(PRCM_ACK_MB0_WAKEUP_1_DBB);
		else
			ev = readl(PRCM_ACK_MB0_WAKEUP_0_DBB);

		prcmu_debug_register_mbox0_event(ev,
						 (mb0_transfer.req.dbb_irqs |
						  mb0_transfer.req.dbb_wakeups));

		ev &= mb0_transfer.req.dbb_irqs;

		for (n = 0; n < NUM_DB5500_PRCMU_WAKEUPS; n++) {
			if (ev & prcmu_irq_bit[n]) {
				if (n != IRQ_INDEX(ABB))
					generic_handle_irq(IRQ_DB5500_PRCMU_BASE + n);
			}
		}
		r = true;
		break;
	default:
		print_unknown_header_warning(0, header);
		r = false;
		break;
	}
	writel(MBOX_BIT(0), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
	return r;
}

static bool read_mailbox_1(void)
{
	u8 header;
	bool do_complete = true;

	header = mb1_transfer.ack.header = readb(PRCM_ACK_MB1_HEADER);

	switch (header) {
	case MB1H_ARM_OPP:
		mb1_transfer.ack.arm_opp = readb(PRCM_ACK_MB1_CURRENT_ARM_OPP);
		mb1_transfer.ack.arm_voltage_st =
			readb(PRCM_ACK_MB1_ARM_VOLT_STATUS);
		break;
	case MB1H_APE_OPP:
		mb1_transfer.ack.ape_opp = readb(PRCM_ACK_MB1_CURRENT_APE_OPP);
		mb1_transfer.ack.ape_voltage_st =
			readb(PRCM_ACK_MB1_APE_VOLT_STATUS);
		break;
	case MB1H_ARM_APE_OPP:
		mb1_transfer.ack.ape_opp = readb(PRCM_ACK_MB1_CURRENT_APE_OPP);
		mb1_transfer.ack.ape_voltage_st =
			readb(PRCM_ACK_MB1_APE_VOLT_STATUS);
		break;
	default:
		print_unknown_header_warning(1, header);
		do_complete = false;
		break;
	}

	writel(MBOX_BIT(1), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);

	if (do_complete)
		complete(&mb1_transfer.work);

	return false;
}

static bool read_mailbox_2(void)
{
	u8 header;

	header = readb(PRCM_ACK_MB2_HEADER);
	mb2_transfer.ack.header = header;
	switch (header) {
	case MB2H_EPOD_REQUEST:
		mb2_transfer.ack.status = readb(PRCM_ACK_MB2_EPOD_STATUS);
		break;
	case MB2H_CLK_REQUEST:
		mb2_transfer.ack.status = readb(PRCM_ACK_MB2_CLK_STATUS);
		break;
	case MB2H_PLL_REQUEST:
		mb2_transfer.ack.status = readb(PRCM_ACK_MB2_PLL_STATUS);
		break;
	default:
		writel(MBOX_BIT(2), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
		pr_err("prcmu: Wrong ACK received for MB2 request \n");
		return false;
		break;
	}
	writel(MBOX_BIT(2), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
	complete(&mb2_transfer.work);
	return false;
}

static bool read_mailbox_3(void)
{
	u8 header;

	header = readb(PRCM_ACK_MB3_HEADER);
	mb3_transfer.ack.header = header;
	switch (header) {
	case MB3H_REFCLK_REQUEST:
		mb3_transfer.ack.status = readb(PRCM_ACK_MB3_REFCLK_REQ);
		writel(MBOX_BIT(3), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
		complete(&mb3_transfer.sysclk_work);
		break;
	default:
		writel(MBOX_BIT(3), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
		pr_err("prcmu: wrong MB3 header\n");
		break;
	}

	return false;
}

static bool read_mailbox_4(void)
{
	u8 header;
	bool do_complete = true;

	header = readb(PRCM_ACK_MB4_HEADER);
	mb4_transfer.ack.header = header;
	switch (header) {
	case MB4H_ACK_CFG_HOTDOG:
	case MB4H_ACK_CFG_HOTMON:
	case MB4H_ACK_CFG_HOTPERIOD:
	case MB4H_ACK_CFG_MODEM_RESET:
	case MB4H_ACK_CGF_A9WDOG_EN_PREBARK:
	case MB4H_ACK_CGF_A9WDOG_EN_NOPREBARK:
	case MB4H_ACK_CGF_A9WDOG_DIS:
		mb4_transfer.ack.status = readb(PRCM_ACK_MB4_REQUESTS);
		break;
	default:
		print_unknown_header_warning(4, header);
		do_complete = false;
		break;
	}

	writel(MBOX_BIT(4), (_PRCMU_BASE + PRCM_ARM_IT1_CLEAR));

	if (do_complete)
		complete(&mb4_transfer.work);

	return false;
}

static bool read_mailbox_5(void)
{
	u8 header;

	header = readb(PRCM_ACK_MB5_HEADER);
	switch (header) {
	case MB5H_I2C_READ:
		memcpy_fromio(mb5_transfer.ack.value, PRCM_ACK_MB5_I2C_DATA, 4);
	case MB5H_I2C_WRITE:
		mb5_transfer.ack.header = header;
		mb5_transfer.ack.status = readb(PRCM_ACK_MB5_RETURN_CODE);
		complete(&mb5_transfer.work);
		break;
	default:
		print_unknown_header_warning(5, header);
		break;
	}
	writel(MBOX_BIT(5), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
	return false;
}

static bool read_mailbox_6(void)
{
	writel(MBOX_BIT(6), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
	return false;
}

static bool read_mailbox_7(void)
{
	writel(MBOX_BIT(7), _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);
	return false;
}

static bool (* const read_mailbox[NUM_MB])(void) = {
	read_mailbox_0,
	read_mailbox_1,
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

	bits = (readl(_PRCMU_BASE + PRCM_ARM_IT1_VAL) & ALL_MBOX_BITS);
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
	u32 ev;

	/*
	 * ABB needs to be handled before the wakeup because
	 * the ping/pong buffers for ABB events could change
	 * after we acknowledge the wakeup.
	 */
	if (readb(PRCM_ACK_MB0_READ_POINTER) & 1)
		ev = readl(PRCM_ACK_MB0_WAKEUP_1_DBB);
	else
		ev = readl(PRCM_ACK_MB0_WAKEUP_0_DBB);

	ev &= mb0_transfer.req.dbb_irqs;
	if (ev & WAKEUP_BIT_ABB)
		handle_nested_irq(IRQ_DB5500_PRCMU_ABB);

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

	mb0_transfer.req.dbb_irqs &= ~prcmu_irq_bit[d->irq - IRQ_DB5500_PRCMU_BASE];

	spin_unlock_irqrestore(&mb0_transfer.dbb_irqs_lock, flags);
	schedule_work(&mb0_transfer.mask_work);
}

static void prcmu_irq_unmask(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.dbb_irqs_lock, flags);

	mb0_transfer.req.dbb_irqs |= prcmu_irq_bit[d->irq - IRQ_DB5500_PRCMU_BASE];

	spin_unlock_irqrestore(&mb0_transfer.dbb_irqs_lock, flags);
	schedule_work(&mb0_transfer.mask_work);
}

static void noop(struct irq_data *d)
{
}

static struct irq_chip prcmu_irq_chip = {
	.name           = "prcmu",
	.irq_disable    = prcmu_irq_mask,
	.irq_ack        = noop,
	.irq_mask       = prcmu_irq_mask,
	.irq_unmask     = prcmu_irq_unmask,
};

void __init db5500_prcmu_early_init(void)
{
	unsigned int i;
	void *tcpm_base = ioremap_nocache(U5500_PRCMU_TCPM_BASE, SZ_4K);

	if (tcpm_base != NULL) {
		int version_high, version_low;

		version_high = readl(tcpm_base + PRCMU_FW_VERSION_OFFSET);
		version_low = readl(tcpm_base + PRCMU_FW_VERSION_OFFSET + 4);
		prcmu_version.board = (version_high >> 24) & 0xFF;
		prcmu_version.fw_version = version_high & 0xFF;
		prcmu_version.api_version = version_low & 0xFF;

		pr_info("PRCMU Firmware Version: 0x%x\n",
			prcmu_version.fw_version);
		pr_info("PRCMU API Version: 0x%x\n",
			prcmu_version.api_version);

		iounmap(tcpm_base);
	}

	tcdm_base = __io_address(U5500_PRCMU_TCDM_BASE);
	mtimer_base = __io_address(U5500_MTIMER_BASE);
	spin_lock_init(&mb0_transfer.lock);
	spin_lock_init(&mb0_transfer.dbb_irqs_lock);
	mutex_init(&mb0_transfer.ac_wake_lock);
	mutex_init(&mb1_transfer.lock);
	init_completion(&mb1_transfer.work);
	mutex_init(&mb2_transfer.lock);
	init_completion(&mb2_transfer.work);
	mutex_init(&mb3_transfer.sysclk_lock);
	init_completion(&mb3_transfer.sysclk_work);
	mutex_init(&mb4_transfer.lock);
	init_completion(&mb4_transfer.work);
	mutex_init(&mb5_transfer.lock);
	init_completion(&mb5_transfer.work);

	INIT_WORK(&mb0_transfer.mask_work, prcmu_mask_work);

	/* Initalize irqs. */
	for (i = 0; i < NUM_DB5500_PRCMU_WAKEUPS; i++) {
		unsigned int irq;

		irq = IRQ_DB5500_PRCMU_BASE + i;
		irq_set_chip_and_handler(irq, &prcmu_irq_chip,
					 handle_simple_irq);
		if (irq == IRQ_DB5500_PRCMU_ABB)
			irq_set_nested_thread(irq, true);
		set_irq_flags(irq, IRQF_VALID);
	}
	prcmu_ape_clocks_init();
}

/*
 * Power domain switches (ePODs) modeled as regulators for the DB5500 SoC
 */
static struct regulator_consumer_supply db5500_vape_consumers[] = {
	REGULATOR_SUPPLY("v-ape", NULL),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.0"),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.1"),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.2"),
	REGULATOR_SUPPLY("v-i2c", "nmk-i2c.3"),
	REGULATOR_SUPPLY("vcore", "sdi0"),
	REGULATOR_SUPPLY("vcore", "sdi1"),
	REGULATOR_SUPPLY("vcore", "sdi2"),
	REGULATOR_SUPPLY("vcore", "sdi3"),
	REGULATOR_SUPPLY("vcore", "sdi4"),
	REGULATOR_SUPPLY("v-uart", "uart0"),
	REGULATOR_SUPPLY("v-uart", "uart1"),
	REGULATOR_SUPPLY("v-uart", "uart2"),
	REGULATOR_SUPPLY("v-uart", "uart3"),
	REGULATOR_SUPPLY("v-ape", "db5500-keypad"),
};

static struct regulator_consumer_supply db5500_sga_consumers[] = {
	REGULATOR_SUPPLY("debug", "reg-virt-consumer.0"),
	REGULATOR_SUPPLY("v-mali", NULL),
};

static struct regulator_consumer_supply db5500_hva_consumers[] = {
	REGULATOR_SUPPLY("debug", "reg-virt-consumer.1"),
	REGULATOR_SUPPLY("v-hva", NULL),
};

static struct regulator_consumer_supply db5500_sia_consumers[] = {
	REGULATOR_SUPPLY("debug", "reg-virt-consumer.2"),
	REGULATOR_SUPPLY("v-sia", "mmio_camera"),
};

static struct regulator_consumer_supply db5500_disp_consumers[] = {
	REGULATOR_SUPPLY("debug", "reg-virt-consumer.3"),
	REGULATOR_SUPPLY("vsupply", "b2r2_bus"),
	REGULATOR_SUPPLY("vsupply", "mcde"),
	REGULATOR_SUPPLY("vsupply", "dsilink.0"),
	REGULATOR_SUPPLY("vsupply", "dsilink.1"),
};

static struct regulator_consumer_supply db5500_esram12_consumers[] = {
	REGULATOR_SUPPLY("debug", "reg-virt-consumer.4"),
	REGULATOR_SUPPLY("v-esram12", "mcde"),
	REGULATOR_SUPPLY("esram12", "hva"),
};

#define DB5500_REGULATOR_SWITCH(lower, upper)                           \
[DB5500_REGULATOR_SWITCH_##upper] = {                                   \
	.constraints = {                                                \
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,              \
	},                                                              \
	.consumer_supplies      = db5500_##lower##_consumers,            \
	.num_consumer_supplies  = ARRAY_SIZE(db5500_##lower##_consumers),\
}

#define DB5500_REGULATOR_SWITCH_VAPE(lower, upper)			\
[DB5500_REGULATOR_SWITCH_##upper] = {					\
	.supply_regulator = "db5500-vape",				\
	.constraints = {						\
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,		\
	},								\
	.consumer_supplies      = db5500_##lower##_consumers,		\
	.num_consumer_supplies  = ARRAY_SIZE(db5500_##lower##_consumers),\
}									\

static struct regulator_init_data db5500_regulators[DB5500_NUM_REGULATORS] = {
	[DB5500_REGULATOR_VAPE] = {
		.constraints = {
			.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		},
		.consumer_supplies	= db5500_vape_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(db5500_vape_consumers),
	},
	DB5500_REGULATOR_SWITCH_VAPE(sga, SGA),
	DB5500_REGULATOR_SWITCH_VAPE(hva, HVA),
	DB5500_REGULATOR_SWITCH_VAPE(sia, SIA),
	DB5500_REGULATOR_SWITCH_VAPE(disp, DISP),
	/*
	 * ESRAM12 is put in retention by the firmware when VAPE is
	 * turned off so there's no need to hold VAPE.
	 */
	DB5500_REGULATOR_SWITCH(esram12, ESRAM12),
};
static struct db5500_regulator_init_data db5500_regulators_pdata = {
	.set_epod = set_epod,
	.regulators = db5500_regulators,
	.reg_num = DB5500_NUM_REGULATORS
};

static struct ux500_wdt_ops db5500_wdt_ops = {
	.enable = enable_a9wdog,
	.disable = disable_a9wdog,
	.kick = kick_a9wdog,
	.load = load_a9wdog,
	.config = config_a9wdog,
};

/*
 * Thermal Sensor
 */
static struct dbx500_temp_ops db5500_temp_ops = {
	.config_hotdog = config_hotdog,
	.config_hotmon = config_hotmon,
	.start_temp_sense = start_temp_sense,
	.stop_temp_sense = stop_temp_sense,
};

static struct resource u5500_thsens_resources[] = {
	[0] = {
		.name	= "IRQ_HOTMON_LOW",
		.start  = IRQ_DB5500_PRCMU_TEMP_SENSOR_LOW,
		.end    = IRQ_DB5500_PRCMU_TEMP_SENSOR_LOW,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.name	= "IRQ_HOTMON_HIGH",
		.start  = IRQ_DB5500_PRCMU_TEMP_SENSOR_HIGH,
		.end    = IRQ_DB5500_PRCMU_TEMP_SENSOR_HIGH,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct mfd_cell db5500_prcmu_devs[] = {
	{
		.name = "db5500-prcmu-regulators",
		.platform_data = &db5500_regulators_pdata,
		.pdata_size = sizeof(db5500_regulators_pdata),
	},
	{
		.name = "ux500_wdt",
		.id = -1,
		.platform_data = &db5500_wdt_ops,
		.pdata_size = sizeof(db5500_wdt_ops),
	},
	{
		.name = "dbx500_temp",
		.platform_data = &db5500_temp_ops,
		.pdata_size = sizeof(db5500_temp_ops),
		.resources       = u5500_thsens_resources,
		.num_resources  = ARRAY_SIZE(u8500_thsens_resources),

	},
	{
		.name = "cpufreq-u5500",
	},
};

/**
 * prcmu_fw_init - arch init call for the Linux PRCMU fw init logic
 *
 */
static int __init db5500_prcmu_probe(struct platform_device *pdev)
{
	int err = 0;

	if (ux500_is_svp() || !cpu_is_u5500())
		return -ENODEV;

	/* Clean up the mailbox interrupts after pre-kernel code. */
	writel(ALL_MBOX_BITS, _PRCMU_BASE + PRCM_ARM_IT1_CLEAR);

	err = request_threaded_irq(IRQ_DB5500_PRCMU1, prcmu_irq_handler,
		prcmu_irq_thread_fn, IRQF_NO_SUSPEND, "prcmu", NULL);
	if (err < 0) {
		pr_err("prcmu: Failed to allocate IRQ_DB5500_PRCMU1.\n");
		err = -EBUSY;
		goto no_irq_return;
	}

	err = mfd_add_devices(&pdev->dev, 0, db5500_prcmu_devs,
			      ARRAY_SIZE(db5500_prcmu_devs), NULL,
			      0);

	if (err)
		pr_err("prcmu: Failed to add subdevices\n");
	else
		pr_info("DB5500 PRCMU initialized\n");

no_irq_return:
	return err;

}

static struct platform_driver db5500_prcmu_driver = {
	.driver = {
		.name = "db5500-prcmu",
		.owner = THIS_MODULE,
	},
};

static int __init db5500_prcmu_init(void)
{
	return platform_driver_probe(&db5500_prcmu_driver, db5500_prcmu_probe);
}

arch_initcall(db5500_prcmu_init);
