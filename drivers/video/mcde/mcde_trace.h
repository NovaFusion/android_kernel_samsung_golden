/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * MCDE trace events
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#if !defined(_MCDE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MCDE_TRACE_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mcde
#define TRACE_SYSTEM_STRING __stringify(TRACE_SYSTEM)
#define TRACE_INCLUDE_FILE mcde_trace

#include <linux/tracepoint.h>

TRACE_EVENT(state,
	TP_PROTO(int chnl, int state),
	TP_ARGS(chnl, state),
	TP_STRUCT__entry(
		__field(	int,	chnl	)
		__field(	int,	state	)
	),
	TP_fast_assign(
		__entry->chnl = chnl;
		__entry->state = state;
	),
	TP_printk("chnl=%d, state=%d", __entry->chnl, __entry->state)
);

TRACE_EVENT(keyvalue,
	TP_PROTO(int ovly, char* key, int value),
	TP_ARGS(ovly, key, value),
	TP_STRUCT__entry(
		__field(	int,	ovly	)
		__field(	char*,	key	)
		__field(	int,	value	)
	),
	TP_fast_assign(
		__entry->ovly = ovly;
		__entry->key = key;
		__entry->value = value;
	),
	TP_printk("ovly=0x%02X: %s == %d", __entry->ovly, __entry->key, __entry->value)
);

TRACE_EVENT(update,
	TP_PROTO(int chnl, bool begin),
	TP_ARGS(chnl, begin),
	TP_STRUCT__entry(
		__field(	int,	chnl	)
		__field(	bool,	begin	)
	),
	TP_fast_assign(
		__entry->chnl = chnl;
		__entry->begin = begin;
	),
	TP_printk("chnl=%d %s", __entry->chnl, __entry->begin ? "begin" : "end")
);

TRACE_EVENT(vcmp,
	TP_PROTO(int chnl, int state),
	TP_ARGS(chnl, state),
	TP_STRUCT__entry(
		__field(	int,	chnl	)
		__field(	int,	state	)
	),
	TP_fast_assign(
		__entry->chnl = chnl;
		__entry->state = state;
	),
	TP_printk("chnl=%d %d", __entry->chnl, __entry->state)
);

TRACE_EVENT(vsync,
	TP_PROTO(int chnl, int state),
	TP_ARGS(chnl, state),
	TP_STRUCT__entry(
		__field(	int,	chnl	)
		__field(	int,	state	)
	),
	TP_fast_assign(
		__entry->chnl = chnl;
		__entry->state = state;
	),
	TP_printk("chnl=%d %d", __entry->chnl, __entry->state)
);

TRACE_EVENT(chnl_err,
	TP_PROTO(unsigned int err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		__field(	unsigned int,	err	)
	),
	TP_fast_assign(
		__entry->err = err;
	),
	TP_printk("error=%u", __entry->err)
);

TRACE_EVENT(err,
	TP_PROTO(unsigned int err),
	TP_ARGS(err),
	TP_STRUCT__entry(
		__field(	unsigned int,	err	)
	),
	TP_fast_assign(
		__entry->err = err;
	),
	TP_printk("error=%u", __entry->err)
);
#endif /* _MCDE_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/video/mcde

#define TRACE_INCLUDE_FILE mcde_trace
#include <trace/define_trace.h>

