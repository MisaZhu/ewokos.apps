PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = fltkhello
TASK_OBJS = $(ARCH)/hello.o
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

## <FL/Fl.H> resolves from $(SDK_DIR)/include (installed by ../../Makefile);
## <X11/Xlib.h> and friends resolve from the NX11 install dir.
## -Wno-cast-function-type silences FLTK 1.1's inline Fl_Widget::callback()
## overloads (Fl_Widget.H), which legitimately cast between callback fn types;
## every app that includes <FL/Fl.H> hits it.
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/nx11 -Wno-cast-function-type

all: $(TASK)

## FLTK apps use the .cxx extension, which make.rule has no rule for.
$(ARCH)/%.o: %.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TASK): $(TASK_OBJS) \
		$(SDK_DIR)/lib/libfltk.a \
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
		--start-group -lfltk -lNX11 -lnano-X $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) --end-group \
		$(EWOK_LIBC) -lcxx
	mkdir -p $(BUILD_DIR)/res
	cp -f res/icon.png $(BUILD_DIR)/res/

clean:
	rm -f $(TASK_OBJS)
	rm -rf $(BUILD_DIR)
