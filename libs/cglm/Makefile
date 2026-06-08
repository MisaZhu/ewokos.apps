SW_ROOT_DIR = ../..
include $(SW_ROOT_DIR)/make.inc
SYS_DIR = $(EWOKOS_ROOT_DIR)/system

BUILD_DIR = $(SYS_DIR)/build/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS = -L $(BUILD_DIR)/lib

CFLAGS += -Iinclude -I $(BUILD_DIR)/include

TASK_OBJS = src/cglmdemo.o
TASK_DIR = $(TARGET_DIR)/bin

TASK = cglmdemo

$(TASK): $(TASK_OBJS) \
		$(EWOK_LIBC_A)  
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIBC) -lm 
	cp -r include/* $(BUILD_DIR)/include

clean:
	rm -f $(TASK_OBJS)

