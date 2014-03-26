#
# Copyright (C) ST-Ericsson SA 2010
# Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
# License terms: GNU General Public License (GPL), version 2.
#

#
# Rules to build kernel modules
#
ifneq ($(findstring KERNELRELEASE,$(.VARIABLES)),)

  # $(src): current relative dir; $(kbuild-dir): cur absolute dir
  ifdef kbuild-dir
    SRCDIR = $(realpath $(kbuild-dir))
  else
    SRCDIR = $(realpath $(src))
  endif
  include $(SRCDIR)/Make.config
  ifndef FIXED_CPPFLAGS
    # In Android env, we can not depend on files that are out of kernel tree.
    # and thus we can't include $(SRCDIR)/../../../../mmenv/SharedARMFlags.mk
    # where FIXED_CPPFLAGS is defined.
    # So, define FIXED_CPPFLAGS here
    FIXED_CPPFLAGS=-D__STN_8500=30 -DLINUX -D__ARM_LINUX
  endif
  EXTRA_CFLAGS := -I$(SRCDIR) $(FIXED_CPPFLAGS)
  EXTRA_CFLAGS += -Wall -Werror
  #EXTRA_CFLAGS += -DCM_DEBUG_ALLOC

  #
  # CM object files to compile with
  #
  GENERIC_CM_FILES:=$(shell cd $(SRCDIR); find cm -name "*.c")
  GENERIC_CM_FILES := $(filter-out $(CMENGINESRC_COPY_NO_BUILD), $(GENERIC_CM_FILES))

  CM_OBJS := $(GENERIC_CM_FILES:.c=.o)
  CM_OBJS += cmld.o cm_syscall.o osal-kernel.o cm_service.o cm_debug.o configuration.o
  CM_OBJS += cm_dma.o

  obj-$(CONFIG_U8500_CM) := cm.o

  #Note: build system prepends the $(PWD) directory to these objects paths
  cm-objs := $(CM_OBJS)

else

  # CM module is built in kernel in android env
  # or as module otherwise (OSI env, ...)
  export CONFIG_U8500_CM ?= m

  ifeq ($(findstring install,$(MAKECMDGOALS)),)
    # If not only performing install then include needed files for build
    include $(MM_MAKEFILES_DIR)/SharedARMFlags.mk
    export FIXED_CPPFLAGS
    -include $(MM_MAKEFILES_DIR)/KernelConfig.mk

    ifeq ($(findstring clean,$(MAKECMDGOALS)),)
      ifndef KERNEL_BUILD_DIR
        $(error KERNEL_BUILD_DIR not defined)
      endif
    endif
  endif

  include $(MM_MAKEFILES_DIR)/SharedConfig.mk

  module:
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_BUILD_DIR) \
          M=$(PWD) INSTALL_HEADER_DIR=$(INSTALL_HEADER_DIR) \
          modules

  all: module
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_BUILD_DIR) \
          M=$(PWD) INSTALL_HEADER_DIR=$(INSTALL_HEADER_DIR) \
          INSTALL_MOD_PATH=$(PWD)/lib/$(PLATFORM) \
          modules_install
	rm -f $(PWD)/lib/$(PLATFORM)/lib/modules/*/modules.*

  #
  # Rules to clean and install
  #
  clean:
	@rm -rf $(PLATFORM) $(CM_OBJS) .built-in.o.cmd .cm*o.cmd Module.symvers \
                .tmp_versions modules.order cm.ko cm.o cm.mod.* lib \
		$(foreach f,$(CM_OBJS), $(dir $f).$(notdir $f).cmd)

  realclean: clean
	$(foreach platform, \
          $(shell grep property ../../component/component.xml | cut -d\" -f 4), \
          rm -rf $(platform);)
	@rm -rf *~

  install:
	$(GEN_LN) -d lib/$(PLATFORM)/lib $(INSTALL_LIB_DIR)/lib

  uninstall:
	$(GEN_LN) -r -d lib/$(PLATFORM)/lib $(INSTALL_LIB_DIR)/lib

endif #ifdef KERNELRELEASE

