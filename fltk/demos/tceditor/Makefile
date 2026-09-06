PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = tceditor
TASK_OBJS = $(ARCH)/editor.o
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

## Tiny Core's fltk_projects/editor (Ver 0.2): the FLTK editor with syntax
## highlighting removed and a Version menu added, plus optional word-wrap and
## line-numbers toggles.  Those two toggles use FLTK 1.3+ APIs that the vendored
## 1.1.11 lacks (Fl_Text_Display::wrap_mode()/WRAP_* and
## Fl_Text_Editor::linenumber_width/font/size), so ENA_LINEWRAP/ENA_LINENUMS are
## disabled in editor.cxx; the rest is 1.1-native.
## <FL/*.H> resolves from $(SDK_DIR)/include; <X11/Xlib.h> from the NX11 dir.
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/nx11 -Wno-cast-function-type

all: $(TASK)

## FLTK apps use the .cxx extension, which make.rule has no rule for.
$(ARCH)/%.o: %.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

## Core libfltk.a only (Fl_Text_Editor + Fl_File_Chooser); no images lib needed.
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
