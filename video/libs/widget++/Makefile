SW_ROOT_DIR=../..
include $(SW_ROOT_DIR)/../make.inc

SYS_ROOT_DIR = $(patsubst ~%,$(HOME)%,$(EWOKOS_SYS_DIR))

BUILD_DIR = $(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)
TARGET_DIR = $(BUILD_DIR)
FFMPEG_DIR = $(SW_ROOT_DIR)/libs/ffmpeg
FFMPEG_HEADER = $(TARGET_DIR)/include/libavcodec/avcodec.h
FFMPEG_LIBS = \
	$(TARGET_DIR)/lib/libavcodec.a \
	$(TARGET_DIR)/lib/libavformat.a \
	$(TARGET_DIR)/lib/libavutil.a \
	$(TARGET_DIR)/lib/libswresample.a \
	$(TARGET_DIR)/lib/libswscale.a

LIB_WIDGET_VIDEO_OBJS = $(ARCH)/src/WidgetVideo/WidgetVideo.o
LIB_WIDGET_VIDEO = $(TARGET_DIR)/lib/libWidgetVideo.a


LIB_WIDGET_WEBVIEW_OBJS = $(ARCH)/src/WidgetWebview/el_input.o \
	$(ARCH)/src/WidgetWebview/XContainer.o \
	$(ARCH)/src/WidgetWebview/WidgetWebview.o
LIB_WIDGET_WEBVIEW = $(TARGET_DIR)/lib/libWidgetWebview.a

# Some targets already have ffmpeg static libraries in the SDK lib dir but are
# missing the installed public headers. Keep the SDK include first, then fall
# back to the local ffmpeg source tree so standalone widget++ builds still work.
CXXFLAGS += -I $(FFMPEG_DIR)/lib

all: $(LIB_WIDGET_VIDEO) $(LIB_WIDGET_WEBVIEW)

$(FFMPEG_HEADER) $(FFMPEG_LIBS):
	$(MAKE) -C $(FFMPEG_DIR)

$(LIB_WIDGET_VIDEO_OBJS): $(FFMPEG_HEADER)

$(LIB_WIDGET_VIDEO): $(FFMPEG_LIBS) $(LIB_WIDGET_VIDEO_OBJS)
	@mkdir -p $(TARGET_DIR)/lib
	@mkdir -p $(TARGET_DIR)/include
	$(AR) rcs $(LIB_WIDGET_VIDEO) $(LIB_WIDGET_VIDEO_OBJS)
	cp -r include/* $(TARGET_DIR)/include/

$(LIB_WIDGET_WEBVIEW): $(LIB_WIDGET_WEBVIEW_OBJS)
	@mkdir -p $(TARGET_DIR)/lib
	@mkdir -p $(TARGET_DIR)/include
	$(AR) rcs $(LIB_WIDGET_WEBVIEW) $(LIB_WIDGET_WEBVIEW_OBJS)
	cp -r include/* $(TARGET_DIR)/include/


$(ARCH)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(LIB_WIDGET_VIDEO_OBJS)
	rm -f $(LIB_WIDGET_VIDEO)
	rm -f $(LIB_WIDGET_WEBVIEW_OBJS)
	rm -f $(LIB_WIDGET_WEBVIEW)
