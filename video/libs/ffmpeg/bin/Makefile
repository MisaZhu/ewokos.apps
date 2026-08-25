FFMPEG_ROOT_DIR=..
EWOKOS_ROOT_DIR = $(FFMPEG_ROOT_DIR)/../../..
SYS_ROOT_DIR = $(EWOKOS_ROOT_DIR)/system
include $(FFMPEG_ROOT_DIR)/../../../make.inc

TASK = mp4player
TASK_OBJS = $(ARCH)/mp4player.o
BUILD_DIR = $(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/rootfs/bin/x
LIBGCC = $(shell $(CC) -print-libgcc-file-name)

FFMPEG_LIBS = -lavformat -lavcodec -lswscale -lswresample
FFMPEG_AVUTIL_LIBS = --whole-archive -lavutil --no-whole-archive

LDFLAGS += \
	$(FFMPEG_LIBS) $(FFMPEG_AVUTIL_LIBS) \
	-lSDL2 -lm -lopenlibm

all: $(TASK)

$(TASK): $(TASK_OBJS) \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libavformat.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libavcodec.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libswscale.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libswresample.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libavutil.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libewoksys.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libewokstl.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libx++.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libx.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libfont.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libgraph.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libwidget++.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libSDL2.a \
	$(SYS_ROOT_DIR)/build_$(ARCH)/$(HW)/lib/libcxx.a
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) $(LIBGCC) -lcxx
clean:
	rm -f $(TASK_OBJS)
