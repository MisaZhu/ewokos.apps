SW_ROOT_DIR=../..
include $(SW_ROOT_DIR)/make.inc

TASK = kimi_chat
TASK_OBJS = $(ARCH)/kimi_chat.o $(ARCH)/kimi_client.o
BUILD_DIR = $(SYS_DIR)/build/$(HW)/rootfs/apps/$(TASK)
LIBTINYHTTPSC = $(SDK_DIR)/lib/libtinyhttpsc.a

CFLAGS += -I $(SW_ROOT_DIR)/libs/libtinyhttpsc/include

all: $(TASK)

$(LIBTINYHTTPSC):
	$(MAKE) -C $(SW_ROOT_DIR)/libs/libtinyhttpsc

$(TASK): $(TASK_OBJS) $(LIBTINYHTTPSC)
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -ltinyhttpsc -lsocket $(EWOK_LIBC) -lcxx
	cp -r res $(BUILD_DIR)

clean:
	rm -f $(TASK_OBJS)
