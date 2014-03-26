#
# Makefile for ST-Ericsson CG2900 connectivity combo controller
#

ccflags-y :=					\
	-Idrivers/staging/cg2900/include

obj-$(CONFIG_BT_CG2900)		+= btcg2900.o
obj-$(CONFIG_CG2900_UART)	+= cg2900_uart.o hci_ldisc.o
