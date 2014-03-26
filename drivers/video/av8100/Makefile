# Make file for compiling and loadable module HDMI

obj-$(CONFIG_AV8100)		+= av8100.o hdmi.o

ifdef CONFIG_HDMI_AV8100_DEBUG
EXTRA_CFLAGS += -DDEBUG
endif

clean-files := av8100.o hdmi.o built-in.o modules.order

