obj-$(CONFIG_U8500_MMIO) := st_mmio.o st_mmio_common.o st_mmio_raw.o st_mmio_yuv.o
ifeq ($(CONFIG_REGULATOR_RN5T592),y)
obj-$(CONFIG_U8500_MMIO) += subpmic_rn5t592.o
else
obj-$(CONFIG_U8500_MMIO) += subpmic_ncp6914.o
obj-$(CONFIG_U8500_MMIO) += subpmic_sm5103.o
endif
