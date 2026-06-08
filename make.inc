EWOKOS_ROOT_DIR = $(ewokos)
SYS_DIR = $(EWOKOS_ROOT_DIR)/system

SDK_DIR= $(SYS_DIR)/build/$(HW)

include $(SYS_DIR)/platform/$(ARCH)/make.rule

LDFLAGS = -L $(SDK_DIR)/lib
CFLAGS += -I include -I $(SDK_DIR)/include
CXXFLAGS += -I include -I $(SDK_DIR)/include
