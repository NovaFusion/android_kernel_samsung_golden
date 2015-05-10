#
# Makefile for the linux kernel, UX500 machine.
#

ccflags-y :=	\
	-Idrivers/staging/cg2900/include \
	-Iarch/arm/mach-ux500 \
	-Idrivers/staging/cg2900

obj-y				:= clock.o cpu.o devices.o dcache.o \
				   devices-common.o id.o pins.o \
				   usb.o reboot_reasons.o timer.o \
				   uart-db8500.o clock-debug.o
obj-y				+= pm/ test/



ifeq ($(CONFIG_UX500_SOC_DB5500), y)
obj-$(CONFIG_UX500_SOC_DBX500)	+= cpu-db5500.o dma-db5500.o \
				   devices-db5500.o clock-db5500.o
obj-$(CONFIG_TOUCHSCREEN_CYTTSP_SPI)    += board-u5500-cyttsp.o
board-mcde-objs			+= board-u5500-mcde.o
endif
ifeq ($(CONFIG_UX500_SOC_DB8500), y)

obj-$(CONFIG_UX500_SOC_DBX500)	+= cpu-db8500.o devices-db8500.o \
				   clock-db8500.o dma-db8500.o cpu-db9500.o
endif
obj-$(CONFIG_MACH_U8500)	+= board-mop500.o board-mop500-sdi.o \
				board-mop500-regulators.o \
				board-mop500-uib.o board-mop500-stuib.o \
				board-mop500-u8500uib.o board-mop500-pins.o \
				board-mop500-msp.o board-mop500-bm.o \
				board-pins-sleep-force.o \
				board-mop500-wlan.o \
				board-mop500-cyttsp.o \
				board-mop500-u9540uib.o 

obj-$(CONFIG_MACH_CCU9540)      +=  board-ccu9540.o \
				board-ccu9540-ver.o \
				board-ccu9540-regulators.o \
				board-ccu9540-pins.o

ifeq ($(CONFIG_MACH_U8500), y)
board-mcde-objs			+= board-mop500-mcde.o
endif
obj-$(CONFIG_ANDROID_STE_TIMED_VIBRA)	+= board-mop500-vibra.o
obj-$(CONFIG_MACH_U5500)	+= board-u5500.o board-u5500-sdi.o \
				   board-u5500-regulators.o \
				   board-u5500-pins.o \
				   board-u5500-wlan.o
obj-$(CONFIG_MCDE)		+= board-mcde.o
obj-$(CONFIG_U5500_MMIO)	+= board-u5500-mmio.o
obj-$(CONFIG_U8500_MMIO)	+= board-sec-mmio.o
obj-$(CONFIG_MACH_SAMSUNG_U8500)	+= board-pins-sleep-force.o board-mop500-msp.o
obj-$(CONFIG_MACH_JANICE)		+= board-janice-pins.o board-janice.o board-janice-regulators.o \
					    board-janice-bm.o board-janice-sdi.o
ifeq ($(CONFIG_MACH_JANICE), y)
board-mcde-objs			+= board-janice-mcde.o
endif
obj-$(CONFIG_MACH_GAVINI)		+= board-gavini-pins.o board-gavini.o board-gavini-regulators.o \
					    board-gavini-bm.o board-gavini-sdi.o
ifeq ($(CONFIG_MACH_GAVINI), y)
board-mcde-objs			+= board-gavini-mcde.o
endif
obj-$(CONFIG_MACH_CODINA)		+= board-codina-pins.o board-codina.o board-codina-regulators.o \
					   board-mop500-wlan.o board-codina-bm.o board-codina-battery.o board-codina-sdi.o
ifeq ($(CONFIG_MACH_CODINA), y)
board-mcde-objs			+= board-codina-mcde.o
endif

obj-$(CONFIG_MACH_SEC_GOLDEN)		+= board-golden-pins.o board-golden.o board-golden-regulators.o \
					    board-golden-bm.o board-golden-battery.o board-golden-sdi.o
ifeq ($(CONFIG_MACH_SEC_GOLDEN), y)
board-mcde-objs			+= board-golden-mcde.o
endif


obj-$(CONFIG_MACH_SEC_KYLE)	+= board-kyle-pins.o board-kyle.o board-kyle-regulators.o \
				    board-kyle-bm.o board-kyle-battery.o board-kyle-sdi.o board-kyle-wlan.o
ifeq ($(CONFIG_MACH_SEC_KYLE), y)
board-mcde-objs			+= board-kyle-mcde.o
endif
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o
obj-$(CONFIG_U5500_MODEM_IRQ)	+= modem-irq-db5500.o
obj-$(CONFIG_TEE_UX500) 	+= tee_ux500.o product.o
obj-$(CONFIG_TEE_SVP)		+= tee_service_svp.o
obj-$(CONFIG_TEE_SVP)		+= tee_ta_start_modem_svp.o
obj-$(CONFIG_DB8500_MLOADER) 	+= mloader-db8500.o open_modem_shared_memory.o
obj-$(CONFIG_U5500_MLOADER)	+= mloader-db5500.o
obj-$(CONFIG_U8500_REGULATOR_DEBUG)	+= virt-regulator-u8500.o
obj-$(CONFIG_UX500_DEBUG_HWREG) 	+= hwreg.o
obj-$(CONFIG_HWMEM)			+= hwmem-int.o
obj-$(CONFIG_UX500_L2X0_PREFETCH_CTRL)	+= l2x0-prefetch.o
obj-$(CONFIG_AB5500_BM)			+= board-u5500-bm.o
obj-$(CONFIG_DBX500_PRCMU_DEBUG)	+= prcmu-debug.o
obj-$(CONFIG_UX500_DB_DUMP)		+= dbx500_dump.o
obj-$(CONFIG_UX500_DEBUG_LAST_IO)	+= debug-last-io.o
obj-$(CONFIG_RTC_HCTOHC)		+= hctohc.o
obj-$(CONFIG_UX500_HW_OBSERVER)		+= hw-observer-debug.o
obj-$(CONFIG_CG2900)		+= board-ux500-cg2900.o
ifeq ($(CONFIG_UX500_SOC_DB8500), y)
obj-$(CONFIG_STM_TRACE)			+= board-mop500-stm.o
endif
obj-$(CONFIG_INPUT_AB8505_MICRO_USB_DETECT)	+= ssg_micro_usb_iddet.o abx_micro_usb_iddet.o
ifneq ($(CONFIG_SAMSUNG_JACK), y)
endif
obj-$(CONFIG_INPUT_AB8505_MICRO_USB_DETECT)	+= ssg_micro_usb_iddet.o abx_micro_usb_iddet.o
obj-$(CONFIG_BT_BCM4330)			+= board-bluetooth-bcm4330.o
obj-$(CONFIG_BT_BCM4334)			+= board-bluetooth-bcm4334.o
obj-$(CONFIG_MACH_SAMSUNG_UX500)	+= sec_common.o
obj-y                                  += sec_debug/
obj-$(CONFIG_SEC_DEBUG)			+= sec_debug.o
obj-$(CONFIG_SAMSUNG_USE_SEC_LOG_BUF)	+= sec_log_buf.o
obj-$(CONFIG_SAMSUNG_ADD_GAFORENSICINFO)+= sec_gaf.o
obj-$(CONFIG_SAMSUNG_USE_GETLOG)	+= sec_getlog.o
obj-$(CONFIG_SAMSUNG_USE_LOGGER_ADDON)	+= sec_logger.o

