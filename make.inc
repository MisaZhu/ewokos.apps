ifeq ($(ewokos),)
export ewokos= ~/work/ewokos
endif

ifeq ($(ARCH),)
export ARCH=aarch64
endif

ifeq ($(HW),)
export HW=virt
endif


EWOKOS_ROOT_DIR = $(ewokos)
EWOKOS_SYS_DIR = $(EWOKOS_ROOT_DIR)/system

SDK_DIR= $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

include $(EWOKOS_SYS_DIR)/platform/$(ARCH)/make.rule

LDFLAGS = -L $(SDK_DIR)/lib
CFLAGS += -I include -I $(SDK_DIR)/include
CXXFLAGS += -I include -I $(SDK_DIR)/include
