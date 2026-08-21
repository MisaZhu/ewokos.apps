PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build/$(HW)
TARGET_DIR = $(BUILD_DIR)

LIB_WIDGET_WEBVIEW_OBJS = $(ARCH)/src/WidgetWebview/el_input.o \
	$(ARCH)/src/WidgetWebview/XContainer.o \
	$(ARCH)/src/WidgetWebview/WidgetWebview.o
LIB_WIDGET_WEBVIEW = $(TARGET_DIR)/lib/libWidgetWebview.a

all: $(LIB_WIDGET_WEBVIEW)

$(LIB_WIDGET_WEBVIEW): $(LIB_WIDGET_WEBVIEW_OBJS)
	@mkdir -p $(TARGET_DIR)/lib
	@mkdir -p $(TARGET_DIR)/include
	$(AR) rcs $(LIB_WIDGET_WEBVIEW) $(LIB_WIDGET_WEBVIEW_OBJS)
	cp -r include/* $(TARGET_DIR)/include/


$(ARCH)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(LIB_WIDGET_WEBVIEW_OBJS)
	rm -f $(LIB_WIDGET_WEBVIEW)
