PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/doom.o

TASK = doom
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC) -lcxx
	cp -r res $(BUILD_DIR)

clean:
	rm -f $(TASK_OBJS)
