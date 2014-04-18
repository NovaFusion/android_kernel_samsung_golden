#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <mach/hardware.h>

#define dbx500_prcmu_warn(a) do {\
	printk(KERN_WARN "%s : dbx500-prcmu driver %s",\
	__func__, a);\
	} while (0)


#define dbx500_prcmu_error(a) do {\
	printk(KERN_ERR "%s : dbx500-prcmu driver %s",\
	__func__, a);\
	} while (0)

#define dbx500_prcmu_early_trap_void do {\
	printk(KERN_ERR "%s called :dbx500-prcmu driver not initialized",\
		__func__); \
	} while (0)


#define dbx500_prcmu_early_trap(a) do {\
	printk(KERN_ERR "%s called :dbx500-prcmu driver not initialized",\
		__func__);\
	return a;\
	} while (0)

#define dbx500_prcmu_trap(a)	do {\
	printk(KERN_ERR "%s called : dbx500-prcmu driver not probed",\
	__func__);\
	return a;\
	} while (0)

#define dbx500_prcmu_trap_void do {\
	printk(KERN_ERR "%s called : dbx500-prcmu driver not probed",\
	__func__);\
	} while (0)

/* dummy handler */

static int dummy_set_power_state(u8 state, bool keep_ulp_clk,
		bool keep_ap_pll) {
	dbx500_prcmu_trap(-EINVAL);
}

static u8  dummy_get_power_state_result(void)
{
	dbx500_prcmu_trap(-EINVAL);
}

static int dummy_config_clkout(u8 clkout, u8 source, u8 div)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static int dummy_request_clock(u8 clock, bool enable)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static long dummy_round_clock_rate(u8 clock, unsigned long rate)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static int dummy_set_clock_rate(u8 clock, unsigned long rate)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	unsigned long dummy_clock_rate(u8 clock)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	int dummy_set_val(enum prcmu_val type, u8 value)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	int dummy_get_val(enum prcmu_val type)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	void dummy_system_reset(u16 reset_code)
{
	dbx500_prcmu_early_trap_void;
}
static	u16 dummy_get_reset_code(void)
{
	dbx500_prcmu_trap(-EINVAL);
}
static	u32 dummy_get_reset_status(void)
{
	dbx500_prcmu_trap(-EINVAL);
}

static	void dummy_enable_wakeups(u32 wakeups)
{
	dbx500_prcmu_trap_void;
}

static	bool dummy_is_ac_wake_requested(void)
{
	dbx500_prcmu_trap(false);
}

static	int  dummy_disable(enum prcmu_out out)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	int  dummy_enable(enum prcmu_out out)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	u32  dummy_read(unsigned int reg)
{
	dbx500_prcmu_early_trap(-EINVAL);
}

static	void  dummy_write(unsigned int reg, u32 value)
{
	dbx500_prcmu_early_trap_void;
}

static	void  default_write_masked(unsigned int reg, u32 mask, u32 value)
{
	u32 val;
	val = readl(_PRCMU_BASE + reg);
	val = ((val & ~mask) | (value & mask));
	writel(val, (_PRCMU_BASE + reg));
}

static	int  dummy_config_esram0_deep_sleep(u8 state)
{
	dbx500_prcmu_trap(-EINVAL);
}

static	void  dummy_config_abb_event_readout(u32 abb_events)
{
	dbx500_prcmu_trap_void;
}

static	void  dummy_get_abb_event_buffer(void __iomem **buf)
{
	dbx500_prcmu_trap_void;
}

static int  dummy_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	dbx500_prcmu_trap(-EINVAL);
}

static	int  dummy_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	dbx500_prcmu_trap(-EINVAL);
}

static	int  dummy_abb_write_masked(u8 slave, u8 reg, u8 *value,
		u8 *mask, u8 size)
{
	dbx500_prcmu_trap(-EINVAL);
}

static	void  dummy_modem_reset(void)
{
	dbx500_prcmu_trap_void;
}

static	struct prcmu_fw_version *dummy_get_fw_version(void)
{
	dbx500_prcmu_early_trap(NULL);
}

static int dummy_request_ape_opp_100_voltage(bool enable)
{
	dbx500_prcmu_trap(-EINVAL);
}

static void dummy_configure_auto_pm(struct prcmu_auto_pm_config *sleep,
	struct prcmu_auto_pm_config *idle)
{
	dbx500_prcmu_trap_void;
}

static void dummy_vc(bool enable)
{
	dbx500_prcmu_early_trap_void;
}

struct prcmu_probe_data dummy_fops = {
	/* sysfs soc inf */
	.get_reset_code = dummy_get_reset_code,

	/* pm/suspend.c/cpu freq */
	.config_esram0_deep_sleep = dummy_config_esram0_deep_sleep,
	.set_power_state = dummy_set_power_state,
	.get_power_state_result = dummy_get_power_state_result,
	.enable_wakeups = dummy_enable_wakeups,
	.is_ac_wake_requested = dummy_is_ac_wake_requested,

	/* modem */
	.modem_reset = dummy_modem_reset,

	/* no used at all */
	.config_abb_event_readout = dummy_config_abb_event_readout,
	.get_abb_event_buffer = dummy_get_abb_event_buffer,

	/* abb access */
	.abb_read = dummy_abb_read,
	.abb_write = dummy_abb_write,
	.get_reset_status = dummy_get_reset_status,
	/*  other u8500 specific */
	.request_ape_opp_100_voltage = dummy_request_ape_opp_100_voltage,
	.configure_auto_pm = dummy_configure_auto_pm,

	/* abb specific access */
	.abb_write_masked = dummy_abb_write_masked,

};

static struct prcmu_early_data default_early_fops = {
	/*  system reset  */
	.system_reset = dummy_system_reset,

	/*  clock service */
	.config_clkout = dummy_config_clkout,
	.request_clock = dummy_request_clock,

	/*  direct register access */
	.read = dummy_read,
	.write =  dummy_write,
	.write_masked = default_write_masked,
	/* others */
	.round_clock_rate = dummy_round_clock_rate,
	.set_clock_rate = dummy_set_clock_rate,
	.clock_rate = dummy_clock_rate,
	.get_fw_version = dummy_get_fw_version,
	.vc = dummy_vc,
};

static int dummy_return_null(void)
{
	return 0;
}

static struct prcmu_probe_ux540_data dummy_fops_ux540 = {
	. stay_in_wfi_check = dummy_return_null,
	. replug_cpu1 = dummy_return_null,
	. unplug_cpu1 = dummy_return_null,
};

static struct {
	struct prcmu_early_data *pearly;
	struct prcmu_probe_data *pprobe;
	struct prcmu_probe_ux540_data *pprobeux540;
	struct prcmu_val_data tab_val[PRCMU_VAL_MAX];
	int (*set_val)(enum prcmu_val type, u8 val);
	int (*get_val) (enum prcmu_val type);
	struct prcmu_out_data tab_out[PRCMU_OUT_MAX];
	int (*disable) (enum prcmu_out out);
	int (*enable) (enum prcmu_out out);
} dbx500_prcmu_context = {
	.pearly = &default_early_fops,
	.pprobe = &dummy_fops,
	.pprobeux540 = &dummy_fops_ux540,
	.set_val = dummy_set_val,
	.get_val = dummy_get_val,
	.disable = dummy_disable,
	.enable = dummy_enable
};

/* early service */

struct prcmu_fw_version *prcmu_get_fw_version(void)
{
	return dbx500_prcmu_context.pearly->get_fw_version();
}

void prcmu_system_reset(u16 reset_code)
{
	dbx500_prcmu_context.pearly->system_reset(reset_code);
}

u32 prcmu_read(unsigned int reg)
{
	return	dbx500_prcmu_context.pearly->read(reg);
}

void prcmu_write(unsigned int reg, u32 value)
{
	return	dbx500_prcmu_context.pearly->write(reg, value);
}

void prcmu_write_masked(unsigned int reg, u32 mask, u32 value)
{
	 dbx500_prcmu_context.pearly->write_masked(reg, mask, value);
}

int prcmu_config_clkout(u8 clkout, u8 source, u8 div)
{
	return dbx500_prcmu_context.pearly->config_clkout(clkout, source, div);
}

int prcmu_request_clock(u8 clock, bool enable)
{
	return  dbx500_prcmu_context.pearly->request_clock(clock, enable);
}

unsigned long prcmu_clock_rate(u8 clock)
{
	return   dbx500_prcmu_context.pearly->clock_rate(clock);
}

long prcmu_round_clock_rate(u8 clock, unsigned long rate)
{
	return  dbx500_prcmu_context.pearly->round_clock_rate(clock, rate);
}

int prcmu_set_clock_rate(u8 clock, unsigned long rate)
{
	return  dbx500_prcmu_context.pearly->set_clock_rate(clock, rate);
}

int prcmu_set_val(enum prcmu_val type, u32 value)
{
	return dbx500_prcmu_context.set_val(type, value);
}

int prcmu_get_val(enum prcmu_val type)
{
	return dbx500_prcmu_context.get_val(type);
}

int prcmu_enable_out(enum prcmu_out out)
{
	return dbx500_prcmu_context.enable(out);
}

int prcmu_disable_out(enum prcmu_out out)
{
	return dbx500_prcmu_context.disable(out);
}

int prcmu_set_ddr_opp(u8 opp)
{
	return dbx500_prcmu_context.set_val(DDR_OPP, opp);
}

int prcmu_get_ddr_opp(void)
{
	return dbx500_prcmu_context.get_val(DDR_OPP);
}

int prcmu_get_arm_opp(void)
{
	return dbx500_prcmu_context.get_val(ARM_OPP);
}

int prcmu_set_ape_opp(u8 opp)
{
	return dbx500_prcmu_context.set_val(APE_OPP, opp);

}

int prcmu_get_ape_opp(void)
{
	return dbx500_prcmu_context.get_val(APE_OPP);
}

/*  other service available after the probe */

int prcmu_set_power_state(u8 state, bool keep_ulp_clk,
		bool keep_ap_pll)
{
	return	 dbx500_prcmu_context.pprobe->set_power_state(state,
			keep_ulp_clk,
			keep_ap_pll);
}

u8 prcmu_get_power_state_result(void)
{
	return	dbx500_prcmu_context.pprobe->get_power_state_result();
}

void prcmu_enable_wakeups(u32 wakeups)
{
	dbx500_prcmu_context.pprobe->enable_wakeups(wakeups);
}

void prcmu_disable_wakeups(void)
{
	dbx500_prcmu_context.pprobe->enable_wakeups(0);
}

void prcmu_config_abb_event_readout(u32 abb_events)
{
	dbx500_prcmu_context.pprobe->config_abb_event_readout(abb_events);
}

void prcmu_get_abb_event_buffer(void __iomem **buf)
{
	dbx500_prcmu_context.pprobe->get_abb_event_buffer(buf);
}

u16 prcmu_get_reset_code(void)
{
	return dbx500_prcmu_context.pprobe->get_reset_code();
}

void prcmu_modem_reset(void)
{
	dbx500_prcmu_context.pprobe->modem_reset();
}

int  prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	return dbx500_prcmu_context.pprobe->abb_read(slave, reg, value, size);
}

int  prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	return dbx500_prcmu_context.pprobe->abb_write(slave, reg, value, size);
}

int  prcmu_abb_write_masked(u8 slave, u8 reg, u8 *value,
		u8 *mask, u8 size)
{
	return dbx500_prcmu_context.pprobe->abb_write_masked(
			slave, reg, value, mask, size);
}

u32  prcmu_get_reset_status(void)
{
	return dbx500_prcmu_context.pprobe->get_reset_status();
}

int  prcmu_replug_cpu1(void)
{
	return dbx500_prcmu_context.pprobeux540->replug_cpu1();
}

int prcmu_stay_in_wfi_check(void)
{
	return dbx500_prcmu_context.pprobeux540->stay_in_wfi_check();
}

int prcmu_unplug_cpu1(void)
{
	return dbx500_prcmu_context.pprobeux540->unplug_cpu1();
}


bool prcmu_is_ac_wake_requested(void)
{
	return dbx500_prcmu_context.pprobe->is_ac_wake_requested();
}

int prcmu_config_esram0_deep_sleep(u8 state)
{
	 return dbx500_prcmu_context.pprobe->config_esram0_deep_sleep(state);
}

/**
 * prcmu_enable_spi2 - Enables pin muxing for SPI2 on OtherAlternateC1.
 */
void prcmu_enable_spi2(void)
{
	 dbx500_prcmu_context.enable(SPI2_MUX);
}

/**
 * prcmu_disable_spi2 - Disables pin muxing for SPI2 on OtherAlternateC1.
 */
void prcmu_disable_spi2(void)
{
	dbx500_prcmu_context.disable(SPI2_MUX);
}

/**
 * prcmu_enable_stm_mod_uart - Enables pin muxing for STMMOD
 * and UARTMOD on OtherAlternateC3.
 */
void prcmu_enable_stm_mod_uart(void)
{
	dbx500_prcmu_context.enable(STM_MOD_UART_MUX);
}

/**
 * prcmu_disable_stm_mod_uart - Disables pin muxing for STMMOD
 * and UARTMOD on OtherAlternateC3.
 */
void prcmu_disable_stm_mod_uart(void)
{
	dbx500_prcmu_context.disable(STM_MOD_UART_MUX);
}

/**
 * prcmu_enable_stm_ape - Enables pin muxing for STM APE on OtherAlternateC1.
 */
void prcmu_enable_stm_ape(void)
{
	dbx500_prcmu_context.enable(STM_APE_MUX);
}

/**
 * prcmu_disable_stm_ape - Disables pin muxing for STM APE on OtherAlternateC1.
 */
void prcmu_disable_stm_ape(void)
{
	dbx500_prcmu_context.disable(STM_APE_MUX);
}

void prcmu_configure_auto_pm(struct prcmu_auto_pm_config *sleep,
	struct prcmu_auto_pm_config *idle)
{
	dbx500_prcmu_context.pprobe->configure_auto_pm(sleep, idle);
}
EXPORT_SYMBOL(prcmu_configure_auto_pm);

int prcmu_request_ape_opp_100_voltage(bool enable)
{
	return	dbx500_prcmu_context.
		pprobe->request_ape_opp_100_voltage(enable);
}
void prcmu_vc(bool enable)
{
	dbx500_prcmu_context.pearly->vc(enable);
}

static int dbx500_prcmu_set_val(enum prcmu_val type, u8 value)
{
	if (type < PRCMU_VAL_MAX)
		return dbx500_prcmu_context.tab_val[type].set_val(value);
	dbx500_prcmu_error("request out of range");
		return -EIO;

}

static int dbx500_prcmu_get_val(enum prcmu_val type)
{
	if (type < PRCMU_VAL_MAX)
		return dbx500_prcmu_context.tab_val[type].get_val();
	dbx500_prcmu_error("request out of range");
		return -EIO;

}

static int dbx500_prcmu_enable_out(enum prcmu_out out)
{
	if (out < PRCMU_OUT_MAX)
		return dbx500_prcmu_context.tab_out[out].enable();
	dbx500_prcmu_error("request out of range");
		return -EIO;
}

static int dbx500_prcmu_disable_out(enum prcmu_out out)
{
	if (out < PRCMU_OUT_MAX)
		return	dbx500_prcmu_context.tab_out[out].disable();
	dbx500_prcmu_error("request out of range");
		return -EIO;
}

/*  used for enable , disable and get */
static int dbx500_default_handler(void)
{
	return 0;
}
static int dbx500_default_set(u8 val)
{
	return 0;
}
static int default_get_ape_opp(void)
{
	return APE_100_OPP;
}


static int default_get_ddr_opp(void)
{
	return DDR_100_OPP;
}

static struct prcmu_val_data default_ape = {
	.set_val = dbx500_default_set,
	.get_val = default_get_ape_opp,
};


static struct prcmu_val_data default_ddr = {
	.set_val = dbx500_default_set,
	.get_val = default_get_ddr_opp,
};

static struct prcmu_out_data default_out = {
	.enable = dbx500_default_handler,
	.disable = dbx500_default_handler,
};

static struct prcmu_val_data default_val = {
	.set_val = dbx500_default_set,
	.get_val = dbx500_default_handler,
};

static void dbx500_prcmu_init_ctx(void)
{
	int i;
	struct prcmu_val_data *pdefault;
	for (i = 0; i < PRCMU_VAL_MAX; i++) {
		switch (i) {
		case DDR_OPP:
			pdefault = &default_ddr;
			break;
		case APE_OPP:
			pdefault = &default_ape;
			break;
		default:
			pdefault = &default_val;
		}

		memcpy(&dbx500_prcmu_context.tab_val[i], pdefault,
				sizeof(struct prcmu_val_data));
	}

	for (i = 0; i < PRCMU_OUT_MAX; i++)
		memcpy(&dbx500_prcmu_context.tab_out[i], &default_out,
				sizeof(struct prcmu_out_data));
	dbx500_prcmu_context.enable = dbx500_prcmu_enable_out;
	dbx500_prcmu_context.disable = dbx500_prcmu_disable_out;
	dbx500_prcmu_context.set_val = dbx500_prcmu_set_val;
	dbx500_prcmu_context.get_val = dbx500_prcmu_get_val;
}

static void dbx500_prcmu_register_pout(struct prcmu_out_data *data, int size)
{
	int i;
	for (i = 0; i < size; i++)
		if (data[i].out < PRCMU_OUT_MAX)
			memcpy(&dbx500_prcmu_context.tab_out[data[i].out],
				&data[i], sizeof(struct prcmu_out_data));
		else
			dbx500_prcmu_error("ops out of range");
}

static void dbx500_prcmu_register_pval(struct prcmu_val_data *data, int size)
{
	int i;
	for (i = 0; i < size; i++)
		if (data[i].val < PRCMU_VAL_MAX)
			memcpy(&dbx500_prcmu_context.tab_val[data[i].val],
				&data[i], sizeof(struct prcmu_val_data));
		else
			dbx500_prcmu_error("registering ops out of range");
}

/**
 * @brief register prcmu handler
 *
 * @param fops
 */
void __init prcmu_early_init(void)
{
	int i, ret = 0;
	struct prcmu_fops_register_data *data;
	if (cpu_is_u9540())
		data =  dbx540_prcmu_early_init();
	else
		data = db8500_prcmu_early_init();

	if (data == NULL)
		return;

	dbx500_prcmu_init_ctx();

	for (i = 0; i < data->size; i++) {
		switch (data->tab[i].fops) {
		case PRCMU_EARLY:
			dbx500_prcmu_context.pearly = data->tab[i].data.pearly;
			break;
		case PRCMU_VAL:
			dbx500_prcmu_register_pval(data->tab[i].data.pval,
					data->tab[i].size);
			break;
		case PRCMU_OUT:
			dbx500_prcmu_register_pout(data->tab[i].data.pout,
					data->tab[i].size);
			break;
		default:
			dbx500_prcmu_error("ops out of range");
			ret = -EIO;
		}
	}
	return;
}

/**
 * @brief dbx500-prcmu probe function
 *
 * @param pdev
 *
 * @return
 */
static int __devinit dbx500_prcmu_probe(struct platform_device *pdev)
{
	struct prcmu_fops_register_data *data = dev_get_platdata(&pdev->dev);
	int i, ret = 0;
	for (i = 0; i < data->size; i++) {
		switch (data->tab[i].fops) {
		case PRCMU_VAL:
			dbx500_prcmu_register_pval(data->tab[i].data.pval,
					data->tab[i].size);
			break;
		case PRCMU_OUT:
			dbx500_prcmu_register_pout(data->tab[i].data.pout,
					data->tab[i].size);
			break;
		case PRCMU_PROBE:
			dbx500_prcmu_context.pprobe =
				data->tab[i].data.pprobe;
			break;
		case PRCMU_PROBE_UX540:
			dbx500_prcmu_context.pprobeux540 =
				data->tab[i].data.pprobeux540;
			break;

		default:
			dbx500_prcmu_error("ops out of range");
			ret = -EIO;
		}
	}
	return ret;
}

/* No action required in suspend/resume, thus the lack of functions */
static struct platform_driver dbx500_prcmu_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "dbx500-prcmu",
	},
	.probe = dbx500_prcmu_probe,
};

static int __init dbx500_prcmu_init(void)
{
	return platform_driver_register(&dbx500_prcmu_driver);
}

MODULE_AUTHOR("Michel JAOUEN <michel.jaouen@stericsson.com>");
MODULE_DESCRIPTION("DBX500 PRCMU DRIVER");
MODULE_LICENSE("GPL");

arch_initcall(dbx500_prcmu_init);

