PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I. -Iinclude -Iinclude/imgui -I $(BUILD_DIR)/include -I../external
CXXFLAGS += -I. -Iinclude -Iinclude/imgui -I $(BUILD_DIR)/include -I../external

# ImGui C++ source files
LIB_IMGUI_OBJS = \
	$(ARCH)/imgui.o \
	$(ARCH)/imgui_draw.o \
	$(ARCH)/imgui_tables.o \
	$(ARCH)/imgui_widgets.o \
	$(ARCH)/backends/imgui_impl_pgl_geometry.o \
	$(ARCH)/backends/imgui_impl_portablegl.o

IMGUI = $(BUILD_DIR)/lib/libimgui.a

$(IMGUI): $(LIB_IMGUI_OBJS)
	$(AR) rcs $(IMGUI) $(LIB_IMGUI_OBJS)
	mkdir -p $(BUILD_DIR)/include/imgui
	cp -r *.h $(BUILD_DIR)/include/imgui/
	mkdir -p $(BUILD_DIR)/include/imgui/backends
	cp -r backends/*.h $(BUILD_DIR)/include/imgui/backends/
	mkdir -p $(BUILD_DIR)/include/imgui/misc/cpp
	cp -r misc/cpp/*.h $(BUILD_DIR)/include/imgui/misc/cpp/
	mkdir -p $(BUILD_DIR)/include/imgui/misc/freetype
	cp -r misc/freetype/*.h $(BUILD_DIR)/include/imgui/misc/freetype/

# Compile C++ files
$(ARCH)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile backend C++ files
$(ARCH)/backends/%.o: backends/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o backends/*.o
	rm -f $(IMGUI)
