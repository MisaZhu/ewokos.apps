PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = fluff
TASK_OBJS = $(ARCH)/fluff.o
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

## Tiny Core's fluff (FLTK file manager, v1.1.7 upstream).  It is a .cpp source
## and uses Fl_PNG_Image / Fl_Help_Dialog, so it links libfltk_images on top of
## core libfltk.  POSIX gaps are covered by libewoksys shims added for EwokOS:
## <pwd.h>/<grp.h> (passwd/group parsing) and <sys/inotify.h> (ENOSYS stubs so
## directory monitoring degrades gracefully); <sys/xattr.h> is a no-op stub and
## <cstdlib> is replaced by <stdlib.h> in fluff.cpp.
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/nx11 -Wno-cast-function-type

all: $(TASK)

## fluff uses the .cpp extension, which make.rule has no rule for.
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
