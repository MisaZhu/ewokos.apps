PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

.DEFAULT_GOAL := all

BUILD_DIR = $(EWOKOS_SYS_DIR)/build/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS = -L $(TARGET_DIR)/lib -L $(BUILD_DIR)/lib

CXXFLAGS += -I $(BUILD_DIR)/include -I $(SW_ROOT_DIR)/libs/widget++/include -I libs/litehtml/include

BROWSER_OBJS = $(ARCH)/src/main.o
LD_LIB_DEPS = \
	$(BUILD_DIR)/lib/liblitehtml.a \
	$(BUILD_DIR)/lib/libtinyhttpsc.a \
	$(BUILD_DIR)/lib/libsocket.a \
	$(BUILD_DIR)/lib/libWidgetWebview.a \
	$(BUILD_DIR)/lib/libwidget++.a \
	$(BUILD_DIR)/lib/libx++.a \
	$(BUILD_DIR)/lib/libx.a \
	$(BUILD_DIR)/lib/libtinyjson.a \
	$(BUILD_DIR)/lib/libewokstl.a \
	$(BUILD_DIR)/lib/libfont.a \
	$(BUILD_DIR)/lib/libfreetype.a \
	$(BUILD_DIR)/lib/libiconbuf.a \
	$(BUILD_DIR)/lib/libgraph.a \
	$(BUILD_DIR)/lib/libpng.a \
	$(BUILD_DIR)/lib/libjpeg.a \
	$(BUILD_DIR)/lib/libgif.a \
	$(BUILD_DIR)/lib/libtga.a \
	$(BUILD_DIR)/lib/libsvg.a \
	$(BUILD_DIR)/lib/libz.a \
	$(BUILD_DIR)/lib/libdisplayman.a \
	$(BUILD_DIR)/lib/libdisplay.a \
	$(BUILD_DIR)/lib/libkeyb.a \
	$(BUILD_DIR)/lib/libmouse.a \
	$(BUILD_DIR)/lib/libewoksys.a \
	$(BUILD_DIR)/lib/libc.a \
	$(BUILD_DIR)/lib/libgloss.a \
	$(BUILD_DIR)/lib/libcxx.a

BROWSER_DIR = $(TARGET_DIR)/apps/xBrowser
BROWSER = $(BROWSER_DIR)/xBrowser

all: $(BROWSER)

$(BROWSER): $(BROWSER_OBJS) $(LD_LIB_DEPS)
	mkdir -p $(BROWSER_DIR)
	$(LD) -Ttext=100 $(BROWSER_OBJS) -o $(BROWSER) $(LDFLAGS) \
		$(EWOK_LIB_X) -lWidgetWebview -llitehtml -ltinyhttpsc -lsocket \
		$(EWOK_LIB_GRAPH) \
		$(EWOK_LIBC) -lcxx
	cp -r res $(BROWSER_DIR)

$(ARCH)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARCH)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(BROWSER_OBJS)
	rm -fr $(BROWSER_DIR)
