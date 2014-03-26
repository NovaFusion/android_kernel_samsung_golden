/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/modem/u8500_client.h>

enum {
	CLK_ENABLE_REQ,
	CLK_ENABLE_RESP,
	CLK_DISABLE_REQ,
	CLK_DISABLE_RESP,
};

enum {
	SUCCESS,
	FAILURE,
};

struct sysclk3_msg {
	u8 msg_type;
	u8 param;
};

struct u8500_kernel_client_info {
	struct workqueue_struct *u8500_kernel_client_wq;
	struct work_struct u8500_handle_sysclk3_req; 
	struct clk *sysclk3;
	struct shrm_dev *shrm;
	struct sysclk3_msg clk_msg;
	u8 l2_header;
};

static struct u8500_kernel_client_info *cli_info;

void u8500_handle_sysclk3_work(struct work_struct *work)
{
	int ret;

	struct sysclk3_msg *resp = NULL;

	switch (cli_info->clk_msg.msg_type) {
	case CLK_ENABLE_REQ:
		resp = kzalloc(sizeof(struct sysclk3_msg), GFP_KERNEL);
		resp->msg_type = CLK_ENABLE_RESP;
		resp->param = SUCCESS;
		ret = clk_enable(cli_info->sysclk3);
		if (ret) {
			printk(KERN_ERR "failed to enable sysclk3\n");
			resp->param = FAILURE;
		}
		shm_write_msg(cli_info->shrm, cli_info->l2_header, resp,
				sizeof(resp));
		kfree(resp);
		break;
	case CLK_DISABLE_REQ:
		resp = kzalloc(sizeof(struct sysclk3_msg), GFP_KERNEL);
		clk_disable(cli_info->sysclk3);
		resp->msg_type = CLK_DISABLE_RESP;
		resp->param = SUCCESS;
		shm_write_msg(cli_info->shrm, cli_info->l2_header, resp,
				sizeof(resp));
		kfree(resp);
		break;
	case CLK_ENABLE_RESP:
	case CLK_DISABLE_RESP:
	default:
		printk(KERN_ERR "unknown messgae type %d in sysclk3 message\n",
				cli_info->clk_msg.msg_type);
		break;
	};
}

int u8500_kernel_client(u8 l2_header, void *data)
{
	switch (l2_header) {
	case SYSCLK3_MESSAGING:
		memcpy(&cli_info->clk_msg, data, sizeof(struct sysclk3_msg));
		cli_info->l2_header = l2_header;
		queue_work(cli_info->u8500_kernel_client_wq,
				&cli_info->u8500_handle_sysclk3_req);
		break;
	default:
		printk(KERN_ERR "unknown l2header %d\n", l2_header);
		break;
	}
	return 0;
}

int u8500_kernel_client_init(struct shrm_dev *shrm)
{
	struct u8500_kernel_client_info *info;

	info = kzalloc(sizeof(struct u8500_kernel_client_info), GFP_KERNEL);
	if (info == NULL) {
		printk(KERN_ERR "unable to allocate kernel client struct\n");
		return -ENOMEM;
	}
	/* create single threaded work queue */
	info->u8500_kernel_client_wq = create_singlethread_workqueue(
			"u8500_kernel_client");
	if (!info->u8500_kernel_client_wq) {
		printk(KERN_ERR
			"unable to create single threaded work queue\n");
		return -ENOMEM;
	}
	INIT_WORK(&info->u8500_handle_sysclk3_req, u8500_handle_sysclk3_work);

	info->sysclk3 = clk_get(NULL, "sysclk3");
	if (IS_ERR(info->sysclk3)) {
		printk(KERN_ERR "request for sysclk3 failed\n");
		return PTR_ERR(info->sysclk3);
	}

	info->shrm = shrm;
	cli_info = info;
	return 0;
}

void u8500_kernel_client_exit(void)
{
	clk_put(cli_info->sysclk3);
	destroy_workqueue(cli_info->u8500_kernel_client_wq);
	kfree(cli_info);
}

