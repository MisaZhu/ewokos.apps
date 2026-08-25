PROJS_ROOT_DIR = ../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS = -L $(BUILD_DIR)/lib

CFLAGS += -Iinclude -I $(BUILD_DIR)/include

TASK_OBJS = $(ARCH)/src/cglmdemo.o
TASK_DIR = $(TARGET_DIR)/bin

TASK = cglmdemo

$(TASK): $(TASK_OBJS) \
		$(BUILD_DIR)/lib/libewoksys.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIBC) -lm 
	cp -r include/* $(BUILD_DIR)/include

clean:
	rm -f $(TASK_OBJS)

