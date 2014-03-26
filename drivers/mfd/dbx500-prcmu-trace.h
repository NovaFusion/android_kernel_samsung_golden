/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * License Terms: GNU General Public License v2
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 *
 * PRCMU tracing
 */

#if !defined(_TRACE_PRCMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PRCMU_H

#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM prcmu
#define TRACE_INCLUDE_FILE dbx500-prcmu-trace

TRACE_EVENT(u8500_irq_mailbox_0,

	TP_PROTO(u8 header, u32 ev, u32 dbb_irqs_mask),

	TP_ARGS(header, ev, dbb_irqs_mask),

	TP_STRUCT__entry(
		__field(u8, header)
		__field(u32, ev)
		__field(u32, dbb_irqs_mask)
	),

	TP_fast_assign(
		__entry->header	 = header;
		__entry->ev = ev;
		__entry->dbb_irqs_mask = dbb_irqs_mask;
	),

	TP_printk("header=%d event=0x%08x mask=0x%08x",
		 __entry->header, __entry->ev, __entry->dbb_irqs_mask)
);

TRACE_EVENT(u8500_irq_mailbox_1,

	TP_PROTO(u8 header, u8 arm_opp, u8 ape_opp, u8 ape_voltage_status),

	TP_ARGS(header, arm_opp, ape_opp, ape_voltage_status),

	TP_STRUCT__entry(
		__field(u8, header)
		__field(u8, arm_opp)
		__field(u8, ape_opp)
		__field(u8, ape_voltage_status)
	),

	TP_fast_assign(
		__entry->header = header;
		__entry->arm_opp = arm_opp;
		__entry->ape_opp = ape_opp;
		__entry->ape_voltage_status  = ape_voltage_status;
	),

	TP_printk("header=%d arm_opp=%d ape_opp=%d ape_voltage_status=%d",
		__entry->header, __entry->arm_opp, __entry->ape_opp,
		__entry->ape_voltage_status)
);

TRACE_EVENT(u8500_irq_mailbox_2,

	TP_PROTO(u8 status),

	TP_ARGS(status),

	TP_STRUCT__entry(
		__field(u8, status)
	),

	TP_fast_assign(
		__entry->status = status;
	),

	TP_printk("status=%d", __entry->status)
);

DECLARE_EVENT_CLASS(u8500_no_arg,

	TP_PROTO(u8 dummy),

	TP_ARGS(dummy),

	TP_STRUCT__entry(
		__field(u8, dummy)
	),

	TP_fast_assign(
		__entry->dummy = dummy;
	),

	TP_printk("%d", __entry->dummy)
);

DEFINE_EVENT(u8500_no_arg, u8500_irq_mailbox_3,

	TP_PROTO(u8 dummy),

	TP_ARGS(dummy)
);

DEFINE_EVENT(u8500_no_arg, u8500_irq_mailbox_6,

	TP_PROTO(u8 dummy),

	TP_ARGS(dummy)
);

DEFINE_EVENT(u8500_no_arg, u8500_irq_mailbox_7,

	TP_PROTO(u8 dummy),

	TP_ARGS(dummy)
);

TRACE_EVENT(u8500_irq_mailbox_4,

	TP_PROTO(u8 header),

	TP_ARGS(header),

	TP_STRUCT__entry(
		__field(u8, header)
	),

	TP_fast_assign(
		__entry->header = header;
	),

	TP_printk("header=%d", __entry->header)
);

TRACE_EVENT(u8500_irq_mailbox_5,

	TP_PROTO(u8 status, u8 value),

	TP_ARGS(status, value),

	TP_STRUCT__entry(
		__field(u8, status)
		__field(u8, value)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->value = value;
	),

	TP_printk("status=%d, value=%d", __entry->status, __entry->value)
);

TRACE_EVENT(u8500_set_power_state,

	TP_PROTO(u8 state, bool keep_ulp_clk, bool keep_ap_pll),

	TP_ARGS(state, keep_ulp_clk, keep_ap_pll),

	TP_STRUCT__entry(
		__field(u8, state)
		__field(bool, keep_ulp_clk)
		__field(bool, keep_ap_pll)
	),

	TP_fast_assign(
		__entry->state = state;
		__entry->keep_ulp_clk = keep_ulp_clk;
		__entry->keep_ap_pll = keep_ap_pll;
	),

	TP_printk("state=%d, keep_ulp_clk=%d, keep_ap_pll=%d",
		__entry->state, __entry->keep_ulp_clk, __entry->keep_ap_pll)
);

TRACE_EVENT(u8500_get_power_state_result,

	TP_PROTO(u8 status),

	TP_ARGS(status),

	TP_STRUCT__entry(
		__field(u8, status)
	),

	TP_fast_assign(
		__entry->status = status;
	),

	TP_printk("status=0x%02x", __entry->status)
);

TRACE_EVENT(u8500_config_wakeups,

	TP_PROTO(u32 dbb, u32 abb),

	TP_ARGS(dbb, abb),

	TP_STRUCT__entry(
		__field(u32, dbb)
		__field(u32, abb)
	),

	TP_fast_assign(
		__entry->dbb = dbb;
		__entry->abb = abb;
	),

	TP_printk("dbb_wakeups=0x%08x, abb_wakeups=0x%08x",
		__entry->dbb, __entry->abb)
);

DECLARE_EVENT_CLASS(u8500_set_opp,

	TP_PROTO(u8 opp),

	TP_ARGS(opp),

	TP_STRUCT__entry(
		__field(u8, opp)
	),

	TP_fast_assign(
		__entry->opp = opp;
	),

	TP_printk("arm_opp=%d", __entry->opp)
);

DEFINE_EVENT(u8500_set_opp, u8500_set_arm_opp,

	TP_PROTO(u8 opp),

	TP_ARGS(opp)
);

DEFINE_EVENT_PRINT(u8500_set_opp, u8500_set_ddr_opp,

	TP_PROTO(u8 opp),

	TP_ARGS(opp),

	TP_printk("ddr_opp=%d", __entry->opp)
);

DEFINE_EVENT_PRINT(u8500_set_opp, u8500_set_ape_opp,

	TP_PROTO(u8 opp),

	TP_ARGS(opp),

	TP_printk("ape_opp=%d", __entry->opp)
);

TRACE_EVENT(u8500_set_epod,

	TP_PROTO(u16 id, u8 state),

	TP_ARGS(id, state),

	TP_STRUCT__entry(
		__field(u16, id)
		__field(u8, state)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->state = state;
	),

	TP_printk("epod_id=%d, epod_state=0x%x",
		__entry->id, __entry->state)
);

TRACE_EVENT(u8500_request_clock,

	TP_PROTO(u8 clock, u8 enable),

	TP_ARGS(clock, enable),

	TP_STRUCT__entry(
		__field(u8, clock)
		__field(u8, enable)
	),

	TP_fast_assign(
		__entry->clock = clock;
		__entry->enable = enable;
	),

	TP_printk("clock=%d, enable=%d",
		__entry->clock, __entry->enable)
);

TRACE_EVENT(u8500_set_clock_rate,

	TP_PROTO(u8 clock, unsigned long rate),

	TP_ARGS(clock, rate),

	TP_STRUCT__entry(
		__field(u8, clock)
		__field(unsigned long, rate)
	),

	TP_fast_assign(
		__entry->clock = clock;
		__entry->rate = rate;
	),

	TP_printk("clock=%d, rate=%lu",
		__entry->clock, __entry->rate)
);

TRACE_EVENT(u8500_a9_wdog,

	TP_PROTO(u8 cmd, u8 d0, u8 d1, u8 d2, u8 d3),

	TP_ARGS(cmd, d0, d1, d2, d3),

	TP_STRUCT__entry(
		__field(u8, cmd)
		__field(u8, d0)
		__field(u8, d1)
		__field(u8, d2)
		__field(u8, d3)
	),

	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->d0 = d0;
		__entry->d1 = d1;
		__entry->d2 = d2;
		__entry->d3 = d3;
	),

	TP_printk("cmd=0x%x, d0=0x%x, d1=0x%x, d2=0x%x, d3=0x%x",
		__entry->cmd, __entry->d0, __entry->d1, __entry->d2,
		__entry->d3)
);

DECLARE_EVENT_CLASS(u8500_ac_req,

	TP_PROTO(u32 val),

	TP_ARGS(val),

	TP_STRUCT__entry(
		__field(u32, val)
	),

	TP_fast_assign(
		__entry->val = val;
	),

	TP_printk("initial PRCM_HOSTACCESS_REQ=0x%x", __entry->val)
);

DEFINE_EVENT(u8500_ac_req, u8500_ac_wake_req,

	TP_PROTO(u32 val),

	TP_ARGS(val)
);

DEFINE_EVENT_PRINT(u8500_ac_req, u8500_mod_awake_status,

	TP_PROTO(u32 val),

	TP_ARGS(val),

	TP_printk("PRCM_MOD_AWAKE_STATUS=0x%x", __entry->val)
);

DEFINE_EVENT(u8500_ac_req, u8500_ac_sleep_req,

	TP_PROTO(u32 val),

	TP_ARGS(val)
);

TRACE_EVENT(u8500_system_reset,

	TP_PROTO(u16 reason),

	TP_ARGS(reason),

	TP_STRUCT__entry(
		__field(u16, reason)
	),

	TP_fast_assign(
		__entry->reason = reason;
	),

	TP_printk("reset_reason=%d", __entry->reason)
);

DEFINE_EVENT(u8500_no_arg, u8500_modem_reset,

	TP_PROTO(u8 dummy),

	TP_ARGS(dummy)
);

#endif /* _TRACE_PRCMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/mfd/

/* This part must be outside protection */
#include <trace/define_trace.h>
