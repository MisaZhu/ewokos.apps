PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/imgui_demo.o $(ARCH)/imgui_main.o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include/glcommon -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__
CXXFLAGS += -I.. -I $(EXTRA_LIBS_DIR)/include/glcommon -I $(EXTRA_LIBS_DIR)/include/imgui -D__ewokos__ -fno-threadsafe-statics
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib

IMGUI_LIBS = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libimgui.a \
	$(EXTRA_LIBS_DIR)/lib/libportablegl.a

TASK = imgui_demo
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) $(IMGUI_LIBS) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(IMGUI_LIBS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
