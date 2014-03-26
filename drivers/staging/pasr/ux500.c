/*
 * Copyright (C) ST-Ericsson SA 2012
 * Author: Maxime Coquelin <maxime.coquelin@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pasr.h>
#include <linux/ux500-pasr.h>


static void ux500_pasr_apply_mask(long unsigned int *mem_reg, void *cookie)
{
	struct ux500_pasr_data *data = (struct ux500_pasr_data *)cookie;

	dev_dbg(data->dev, "Apply PASR mask %#010lx to %s (%#010x)\n",
			*mem_reg, data->name, data->base_addr);

	data->apply_mask(data->mailbox, mem_reg);

	return;
}

static int ux500_pasr_probe(struct platform_device *pdev)
{
	int i;
	struct ux500_pasr_data *pasr_data = dev_get_platdata(&pdev->dev);

	if (!pasr_data)
		return -ENODEV;

	for (i = 0; pasr_data[i].base_addr != 0xFFFFFFFF; i++) {
		phys_addr_t base = pasr_data[i].base_addr;
		void *cookie = (void *)(pasr_data + i);

		pasr_data[i].dev = &pdev->dev;

		if (pasr_register_mask_function(base,
				&ux500_pasr_apply_mask,
				cookie))
			dev_err(&pdev->dev, "Pasr register failed (%s)\n",
					pasr_data[i].name);
	}

	return 0;
}

static struct platform_driver ux500_pasr_driver = {
	.probe = ux500_pasr_probe,
	.driver = {
		.name = "ux500-pasr",
		.owner = THIS_MODULE,
	},
};

static int __init ux500_pasr_init(void)
{
	return platform_driver_register(&ux500_pasr_driver);
}
module_init(ux500_pasr_init);
