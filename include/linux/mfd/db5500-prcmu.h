/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * U5500 PRCMU API.
 */
#ifndef __MFD_DB5500_PRCMU_H
#define __MFD_DB5500_PRCMU_H

#ifdef CONFIG_MFD_DB5500_PRCMU

void db5500_prcmu_early_init(void);
int db5500_prcmu_set_display_clocks(void);
int db5500_prcmu_disable_dsipll(void);
int db5500_prcmu_enable_dsipll(void);
int db5500_prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size);
int db5500_prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size);
void db5500_prcmu_enable_wakeups(u32 wakeups);
int db5500_prcmu_request_clock(u8 clock, bool enable);
void db5500_prcmu_config_abb_event_readout(u32 abb_events);
void db5500_prcmu_get_abb_event_buffer(void __iomem **buf);
int prcmu_resetout(u8 resoutn, u8 state);
int db5500_prcmu_set_power_state(u8 state, bool keep_ulp_clk,
	bool keep_ap_pll);
u8 db5500_prcmu_get_power_state_result(void);
int db5500_prcmu_config_esram0_deep_sleep(u8 state);
void db5500_prcmu_system_reset(u16 reset_code);
u16 db5500_prcmu_get_reset_code(void);
#ifdef CONFIG_UX500_SOC_DB5500
void prcmu_modem_req(void);
void prcmu_modem_rel(void);
void prcmu_ape_ack(void);
#endif
bool db5500_prcmu_is_modem_requested(void);
void db5500_prcmu_modem_reset(void);
int db5500_prcmu_set_arm_opp(u8 opp);
int db5500_prcmu_get_arm_opp(void);
int db5500_prcmu_set_ape_opp(u8 opp);
int db5500_prcmu_get_ape_opp(void);
int db5500_prcmu_set_ddr_opp(u8 opp);
int db5500_prcmu_get_ddr_opp(void);

u32 db5500_prcmu_read(unsigned int reg);
void db5500_prcmu_write(unsigned int reg, u32 value);
void db5500_prcmu_write_masked(unsigned int reg, u32 mask, u32 value);

static inline unsigned long prcmu_clock_rate(u8 clock)
{
	return 0;
}

static inline long prcmu_round_clock_rate(u8 clock, unsigned long rate)
{
	return 0;
}

static inline int prcmu_set_clock_rate(u8 clock, unsigned long rate)
{
	return 0;
}
int db5500_prcmu_get_hotdog(void);


#else /* !CONFIG_UX500_SOC_DB5500 */

static inline void db5500_prcmu_early_init(void) {}

static inline int db5500_prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int db5500_prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int db5500_prcmu_request_clock(u8 clock, bool enable)
{
	return 0;
}

static inline unsigned long db5500_prcmu_clock_rate(u8 clock)
{
	return 0;
}

static inline int db5500_prcmu_set_display_clocks(void)
{
	return 0;
}

static inline int db5500_prcmu_disable_dsipll(void)
{
	return 0;
}

static inline int db5500_prcmu_enable_dsipll(void)
{
	return 0;
}

static inline int db5500_prcmu_config_esram0_deep_sleep(u8 state)
{
	return 0;
}

static inline void db5500_prcmu_enable_wakeups(u32 wakeups) {}

static inline long db5500_prcmu_round_clock_rate(u8 clock, unsigned long rate)
{
	return 0;
}

static inline int db5500_prcmu_set_clock_rate(u8 clock, unsigned long rate)
{
	return 0;
}

static inline int prcmu_resetout(u8 resoutn, u8 state)
{
	return 0;
}

static inline int db5500_prcmu_set_epod(u16 epod_id, u8 epod_state)
{
	return 0;
}

static inline void db5500_prcmu_get_abb_event_buffer(void __iomem **buf) {}
static inline void db5500_prcmu_config_abb_event_readout(u32 abb_events) {}

static inline int db5500_prcmu_set_power_state(u8 state, bool keep_ulp_clk,
	bool keep_ap_pll)
{
	return 0;
}

static inline u8 db5500_prcmu_get_power_state_result(void)
{
	return 0;
}

static inline void db5500_prcmu_system_reset(u16 reset_code) {}

static inline u16 db5500_prcmu_get_reset_code(void)
{
	return 0;
}

static inline void db5500_prcmu_modem_reset(void) {}
static inline bool db5500_prcmu_is_modem_requested(void)
{
	return 0;
}

#ifdef CONFIG_UX500_SOC_DB5500
static void prcmu_ape_ack(void) {}
static void prcmu_modem_req(void) {}
static void prcmu_modem_rel(void) {}
#endif

static inline int db5500_prcmu_set_arm_opp(u8 opp)
{
	return 0;
}

static inline int db5500_prcmu_get_arm_opp(void)
{
	return 0;
}

static inline int db5500_prcmu_set_ape_opp(u8 opp)
{
	return 0;
}

static inline int db5500_prcmu_get_ape_opp(void)
{
	return 0;
}

static inline int db5500_prcmu_set_ddr_opp(u8 opp)
{
	return 0;
}

static inline int db5500_prcmu_get_ddr_opp(void)
{
	return 0;
}

static inline u32 db5500_prcmu_read(unsigned int reg)
{
	return 0;
}

static inline void db5500_prcmu_write(unsigned int reg, u32 value) {}

static inline void db5500_prcmu_write_masked(unsigned int reg, u32 mask,
	u32 value) {}

static inline int db5500_prcmu_get_hotdog(void)
{
	return -ENOSYS;
}
static inline int db5500_prcmu_config_hotdog(u8 threshold)
{
	return 0;
}

static inline int db5500_prcmu_config_hotmon(u8 low, u8 high)
{
	return 0;
}

static inline int db5500_prcmu_start_temp_sense(u16 cycles32k)
{
	return 0;
}
static inline int db5500_prcmu_stop_temp_sense(void)
{
	return 0;
}

#endif /* CONFIG_MFD_DB5500_PRCMU */

#endif /* __MFD_DB5500_PRCMU_H */
