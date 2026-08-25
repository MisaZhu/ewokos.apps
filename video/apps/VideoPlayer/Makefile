SW_ROOT_DIR=../..
include $(SW_ROOT_DIR)/../make.inc

SYS_ROOT_DIR = $(patsubst ~%,$(HOME)%,$(EWOKOS_SYS_DIR))

TASK = VideoPlayer
TASK_OBJS = $(ARCH)/VideoPlayer.o
BUILD_DIR = $(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)
EXTRA_LIBS_DIR = $(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)
LIBGCC = $(shell $(CC) -print-libgcc-file-name)

CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib -lm -lopenlibm

FFMPEG_LIBS = -lavformat -lavcodec -lswscale -lswresample
FFMPEG_AVUTIL_LIBS = --whole-archive -lavutil --no-whole-archive

VIDEO_LIBS = $(EXTRA_LIBS_DIR)/lib/libWidgetVideo.a \
	$(FFMPEG_LIBS) $(FFMPEG_AVUTIL_LIBS)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(EXTRA_LIBS_DIR)/lib/libWidgetVideo.a \
		$(EXTRA_LIBS_DIR)/lib/libavformat.a \
		$(EXTRA_LIBS_DIR)/lib/libavcodec.a \
		$(EXTRA_LIBS_DIR)/lib/libswscale.a \
		$(EXTRA_LIBS_DIR)/lib/libswresample.a \
		$(EXTRA_LIBS_DIR)/lib/libavutil.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libewoksys.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libewokstl.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libx++.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libx.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libfont.a \
		$(EXTRA_LIBS_DIR)/lib/libgraph.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libwidget++.a \
		$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libcxx.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(VIDEO_LIBS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) $(LIBGCC) -lcxx
	cp -r res $(BUILD_DIR)

clean:
	rm -f $(TASK_OBJS)
