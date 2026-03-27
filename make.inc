EWOKOS_ROOT_DIR = $(SW_ROOT_DIR)/$(ewokos)
SYS_ROOT_DIR = $(EWOKOS_ROOT_DIR)/system

SDK_DIR= $(SYS_ROOT_DIR)/build/$(HW)

include $(SYS_ROOT_DIR)/platform/$(ARCH)/make.rule

LDFLAGS = -L$(SDK_DIR)/lib
CFLAGS += -I include -I $(SDK_DIR)/include
CXXFLAGS += -I include -I $(SDK_DIR)/include
