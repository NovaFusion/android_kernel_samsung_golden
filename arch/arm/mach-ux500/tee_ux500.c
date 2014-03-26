/*
 * TEE service to handle the calls to trusted applications.
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/kernel.h>
#include <linux/tee.h>
#include <linux/io.h>
#include <linux/errno.h>

#include <mach/hardware.h>

#define ISSWAPI_EXECUTE_TA 0x11000001
#define ISSWAPI_CLOSE_TA   0x11000002

#define SEC_ROM_NO_FLAG_MASK    0x0000

static u32 call_sec_rom_bridge(u32 service_id, u32 cfg, ...)
{
	typedef u32 (*bridge_func)(u32, u32, va_list);
	bridge_func hw_sec_rom_pub_bridge;
	va_list ap;
	u32 ret;

	if (cpu_is_u9540())
		hw_sec_rom_pub_bridge = (bridge_func)
			((u32)IO_ADDRESS_DB9540_ROM
			 (U9540_BOOT_ROM_BASE + 0x17300));
	else if (cpu_is_u8500())
		hw_sec_rom_pub_bridge = (bridge_func)
			((u32)IO_ADDRESS(U8500_BOOT_ROM_BASE + 0x17300));
	else if (cpu_is_u5500())
		hw_sec_rom_pub_bridge = (bridge_func)
			((u32)IO_ADDRESS(U5500_BOOT_ROM_BASE + 0x18300));
	else
		ux500_unknown_soc();

	va_start(ap, cfg);
	ret = hw_sec_rom_pub_bridge(service_id, cfg, ap);
	va_end(ap);

	return ret;
}

int call_sec_world(struct tee_session *ts, int sec_cmd)
{
	/*
	 * ts->ta and ts->uuid is set to NULL when opening the device, hence it
	 * should be safe to just do the call here.
	 */
	switch (sec_cmd) {
	case TEED_INVOKE:
	if (!ts->uuid) {
		call_sec_rom_bridge(ISSWAPI_EXECUTE_TA,
				SEC_ROM_NO_FLAG_MASK,
				virt_to_phys(&ts->id),
				NULL,
				((struct ta_addr *)ts->ta)->paddr,
				ts->cmd,
				virt_to_phys((void *)(ts->op)),
				virt_to_phys((void *)(&ts->err)),
				virt_to_phys((void *)(&ts->origin)));
	} else {
		call_sec_rom_bridge(ISSWAPI_EXECUTE_TA,
				SEC_ROM_NO_FLAG_MASK,
				virt_to_phys(&ts->id),
				virt_to_phys(ts->uuid),
				NULL,
				ts->cmd,
				virt_to_phys((void *)(ts->op)),
				virt_to_phys((void *)(&ts->err)),
				virt_to_phys((void *)(&ts->origin)));
	}
	break;

	case TEED_CLOSE_SESSION:
	call_sec_rom_bridge(ISSWAPI_CLOSE_TA,
			    SEC_ROM_NO_FLAG_MASK,
			    ts->id,
			    NULL,
			    NULL,
			    virt_to_phys((void *)(&ts->err)));

	/*
	 * Since the TEE Client API does NOT take care of the return value, we
	 * print a warning here if something went wrong in secure world.
	 */
	if (ts->err != TEED_SUCCESS)
		pr_warning("[%s] failed in secure world\n", __func__);

	break;
	}

	return 0;
}
