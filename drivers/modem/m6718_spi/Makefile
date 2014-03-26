#
# Makefile for M6718 SPI driver
#
ifeq ($(CONFIG_MODEM_M6718_SPI_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

m6718_modem_spi-objs := modem_driver.o protocol.o util.o queue.o debug.o \
			netlink.o statemachine.o

ifeq ($(CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE),y)
m6718_modem_spi-objs += modem_state.o
endif

obj-$(CONFIG_MODEM_M6718_SPI) += m6718_modem_spi.o
