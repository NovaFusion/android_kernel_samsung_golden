# Make file for compiling and loadable module B2R2

obj-$(CONFIG_FB_B2R2) += b2r2.o

b2r2-objs = b2r2_api.o b2r2_blt_main.o b2r2_core.o b2r2_mem_alloc.o b2r2_generic.o b2r2_node_gen.o b2r2_node_split.o b2r2_profiler_socket.o b2r2_timing.o b2r2_filters.o b2r2_utils.o b2r2_input_validation.o b2r2_hw_convert.o

ifdef CONFIG_B2R2_DEBUG
b2r2-objs += b2r2_debug.o
endif

ifeq ($(CONFIG_FB_B2R2),m)
obj-y += b2r2_kernel_if.o
endif

obj-$(CONFIG_B2R2_PROFILER) += b2r2_profiler/
