#
# Makefile for ST-Ericsson CG2900 connectivity combo controller
#

ccflags-y :=					\
	-Idrivers/staging/cg2900/include \
	-Iarch/arm/mach-ux500

obj-$(CONFIG_CG2900)	+= devices-cg2900.o \
				devices-cg2900-ux500.o \
				clock-cg2900.o

obj-y				+= mfd/
obj-y				+= bluetooth/
