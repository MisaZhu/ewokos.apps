PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK = fltkeditor
TASK_OBJS = $(ARCH)/editor.o
BUILD_DIR = $(SDK_DIR)/rootfs/apps/$(TASK)

## FLTK's own test/editor.cxx: a Fl_Text_Editor text editor with C/C++ syntax
## highlighting, File/Edit/Search menus and Fl_File_Chooser dialogs.  It is
## 1.1-native (highlight_data / Style_Table_Entry / kf_copy all exist in the
## vendored FLTK 1.1.11), so it builds verbatim with no source changes.
## <FL/*.H> resolves from $(SDK_DIR)/include; <X11/Xlib.h> from the NX11 dir.
## -Wno-cast-function-type silences FLTK 1.1's inline callback() overloads.
CXXFLAGS += -I$(SDK_DIR)/include -I$(SDK_DIR)/include/nx11 -Wno-cast-function-type

all: $(TASK)

## FLTK apps use the .cxx extension, which make.rule has no rule for.
$(ARCH)/%.o: %.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

## Fl_Text_Editor / Fl_File_Chooser live in core libfltk.a; no images lib is
## needed (Fl_File_Chooser.o carries no libfltk_images references).
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
