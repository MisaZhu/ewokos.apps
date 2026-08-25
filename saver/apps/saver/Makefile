PROJS_ROOT_DIR = ../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

EXTRA_LIBS_DIR = $(SW_ROOT_DIR)/build_$(ARCH)/$(HW)

CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include 
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

TASK_OBJS = $(ARCH)/main.o

TASK = saver
TASK_DIR = $(TARGET_DIR)/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libferox.a \
		$(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lferox -lopenlibm $(EWOK_LIBC)  -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -f $(TASK_OBJS)
