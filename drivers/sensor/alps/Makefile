#
#  Makefile for the kernel Sensor device drivers.
#

obj-$(CONFIG_SENSORS_ALPS_CORE)		= sensors_core.o
obj-$(CONFIG_SENSORS_ACCEL)		+= k3dm_i2c.o alps-input.o bma222e.o
obj-$(CONFIG_SENSORS_HSCD)		+= hscd_i2c.o alps-input.o
obj-$(CONFIG_SENSORS_GOLDEN)		+= hscd007_i2c.o alps-input_golden.o
obj-$(CONFIG_SENSORS_HSCDTD008A)	+= hscdtd008a_i2c.o alps-input_kyle.o
obj-$(CONFIG_SENSORS_BMA254)		+= bma254.o
