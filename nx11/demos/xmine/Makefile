PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = xmine
TASK_OBJS = $(ARCH)/xmine.o
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

## <X11/Xlib.h> and <nano-X.h> both resolve from the namespaced install dir
CFLAGS += -I $(SDK_DIR)/include/nx11

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(SDK_DIR)/lib/libNX11.a \
		$(SDK_DIR)/lib/libnano-X.a \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) \
		--start-group -lNX11 -lnano-X $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) --end-group \
		$(EWOK_LIBC) -lcxx
	cp -r res $(BUILD_DIR)

clean:
	rm -f $(TASK_OBJS)
