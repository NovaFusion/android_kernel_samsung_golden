/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/tee.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/hardware/cache-l2x0.h>

static struct tee_session session;
static struct tee_context context;
static void __iomem *l2x0_base;

#define L2X0_PREFETCH_CTRL_REG (0x00000F60)
#define L2X0_PREFETCH_CTRL_BIT_DATA_EN (1 << 28)
#define L2X0_PREFETCH_CTRL_BIT_INST_EN (1 << 29)

#define L2X0_UUID_TEE_TA_START_LOW	0xBC765EDE
#define L2X0_UUID_TEE_TA_START_MID	0x6724
#define L2X0_UUID_TEE_TA_START_HIGH	0x11DF
#define L2X0_UUID_TEE_TA_START_CLOCKSEQ  \
	{0x8E, 0x12, 0xEC, 0xDB, 0xDF, 0xD7, 0x20, 0x85}

static void prefetch_enable(void)
{
	struct tee_operation operation;
	u32 data;
	int err;
	int origin_err;

	data = readl(l2x0_base + L2X0_PREFETCH_CTRL_REG);

	pr_debug("l2x0-prefetch: %s start, preftect_ctrl=0x%08x\n", __func__,
									data);
	if (!(data & L2X0_PREFETCH_CTRL_BIT_INST_EN) ||
		!(data & L2X0_PREFETCH_CTRL_BIT_DATA_EN)) {

		data |= (L2X0_PREFETCH_CTRL_BIT_INST_EN |
				L2X0_PREFETCH_CTRL_BIT_DATA_EN);

		operation.shm[0].buffer = &data;
		operation.shm[0].size = sizeof(data);
		operation.shm[0].flags = TEEC_MEM_INPUT;
		operation.flags = TEEC_MEMREF_0_USED;

		err = teec_invoke_command(&session,
			TEE_STA_SET_L2CC_PREFETCH_CTRL_REGISTER,
			&operation, &origin_err);
		if (err)
			pr_err("l2x0-prefetch: prefetch enable failed, err=%d",
									err);
	}
	pr_debug("l2x0-prefetch: %s end, prefetch_ctrl=0x%08x\n", __func__,
		readl(l2x0_base + L2X0_PREFETCH_CTRL_REG));
}

static void prefetch_disable(void)
{
	struct tee_operation operation;
	u32 data;
	int err;
	int origin_err;

	data = readl(l2x0_base + L2X0_PREFETCH_CTRL_REG);

	pr_debug("l2x0-prefetch: %s start, preftect_ctrl=0x%08x\n", __func__,
									data);
	if (data & (L2X0_PREFETCH_CTRL_BIT_INST_EN |
			L2X0_PREFETCH_CTRL_BIT_DATA_EN)) {

		data &= ~(L2X0_PREFETCH_CTRL_BIT_INST_EN |
				L2X0_PREFETCH_CTRL_BIT_DATA_EN);

		operation.shm[0].buffer = &data;
		operation.shm[0].size = sizeof(data);
		operation.shm[0].flags = TEEC_MEM_INPUT;
		operation.flags = TEEC_MEMREF_0_USED;

		err = teec_invoke_command(&session,
			TEE_STA_SET_L2CC_PREFETCH_CTRL_REGISTER,
			&operation, &origin_err);
		if (err)
			pr_err("l2x0-prefetch: prefetch disable failed, err=%d",
									err);
	}
	pr_debug("l2x0-prefetch: %s end, prefetch_ctrl=0x%08x\n", __func__,
		readl(l2x0_base + L2X0_PREFETCH_CTRL_REG));
}

static int __init prefetch_ctrl_init(void)
{
	int err;
	int origin_err;
	/* Selects trustzone application needed for the job. */
	struct tee_uuid static_uuid = {
		L2X0_UUID_TEE_TA_START_LOW,
		L2X0_UUID_TEE_TA_START_MID,
		L2X0_UUID_TEE_TA_START_HIGH,
		L2X0_UUID_TEE_TA_START_CLOCKSEQ,
	};

	/* Get PL310 base address. It will be used as readonly. */
	if (cpu_is_u5500())
		l2x0_base = __io_address(U5500_L2CC_BASE);
	else if (cpu_is_u8500() || cpu_is_u9540())
		l2x0_base = __io_address(U8500_L2CC_BASE);
	else
		ux500_unknown_soc();

	err = teec_initialize_context(NULL, &context);
	if (err) {
		pr_err("l2x0-prefetch: unable to initialize tee context,"
			" err = %d\n", err);
		err = -EINVAL;
		goto error0;
	}

	err = teec_open_session(&context, &session, &static_uuid,
			TEEC_LOGIN_PUBLIC, NULL, NULL, &origin_err);
	if (err) {
		pr_err("l2x0-prefetch: unable to open tee session,"
			" tee error = %d, origin error = %d\n",
			err, origin_err);
		err = -EINVAL;
		goto error1;
	}

	outer_cache.prefetch_enable = prefetch_enable;
	outer_cache.prefetch_disable = prefetch_disable;

	pr_info("l2x0-prefetch: initialized.\n");

	return 0;

error1:
	(void)teec_finalize_context(&context);
error0:
	return err;
}

static void __exit prefetch_ctrl_exit(void)
{
	outer_cache.prefetch_enable = NULL;
	outer_cache.prefetch_disable = NULL;

	(void)teec_close_session(&session);
	(void)teec_finalize_context(&context);
}

/* Wait for TEE driver to be initialized. */
late_initcall(prefetch_ctrl_init);
module_exit(prefetch_ctrl_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PL310 prefetch control");
