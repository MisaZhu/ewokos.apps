PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = flpicsee
TASK_OBJS = $(ARCH)/flpicsee.o
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

## Tiny Core's flpicsee (FLTK image viewer, v1.4.0 upstream).  It is a .cpp
## source (not .cxx) and decodes images via fl_register_images(), so it links
## libfltk_images (PNG/JPEG/GIF/PNM/BMP loaders) on top of core libfltk.
## The only FLTK-1.4-only calls (Fl::screen_num / Fl::screen_scale) are stubbed
## to a fixed 1.0 scale factor in flpicsee.cpp; the rest is 1.1-native.
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/nx11 -Wno-cast-function-type

all: $(TASK)

## flpicsee uses the .cpp extension, which make.rule has no rule for.
$(ARCH)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TASK): $(TASK_OBJS) \
		$(SDK_DIR)/lib/libfltk.a \
		$(SDK_DIR)/lib/libfltk_images.a \
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
		--start-group -lfltk -lfltk_images -lNX11 -lnano-X $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) --end-group \
		$(EWOK_LIBC) -lcxx
	mkdir -p $(BUILD_DIR)/res
	cp -f res/icon.png $(BUILD_DIR)/res/

clean:
	rm -f $(TASK_OBJS)
	rm -rf $(BUILD_DIR)
