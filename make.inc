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

# All arch output dirs, so that clean removes objects of every arch,
# not only the one selected by $(ARCH).
ALL_ARCH_DIRS = aarch64 arm x86 riscv

SDK_DIR= $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

include $(EWOKOS_SYS_DIR)/platform/$(ARCH)/make.rule

LDFLAGS = -L $(SDK_DIR)/lib
CFLAGS += -I include -I $(SDK_DIR)/include
CXXFLAGS += -I include -I $(SDK_DIR)/include
