include ../../../../make.inc

SYS_ROOT_DIR = $(patsubst ~%,$(HOME)%,$(EWOKOS_SYS_DIR))

FFMPEG_SRC_DIR = $(CURDIR)
SYS_BUILD_DIR = $(abspath $(SYS_ROOT_DIR)/build_$(ARCH)/$(HW))
SDK_DIR := $(SYS_BUILD_DIR)
BUILD_DIR := $(SYS_BUILD_DIR)
FFMPEG_STAGE_DIR = $(FFMPEG_BUILD_DIR)/_install
PREFIX = $(FFMPEG_STAGE_DIR)
# ffmpeg's local build tree only needs to distinguish object compatibility.
# Keep installed SDK/archive outputs keyed by HW, but share .o intermediates by ARCH.
FFMPEG_ARCH_VER_KEY =
ifeq ($(ARCH),arm)
FFMPEG_ARCH_VER_KEY = $(if $(ARCH_VER),-$(ARCH_VER),)
endif
FFMPEG_BUILD_KEY = $(ARCH)$(FFMPEG_ARCH_VER_KEY)
FFMPEG_BUILD_DIR = $(FFMPEG_SRC_DIR)/.build/$(FFMPEG_BUILD_KEY)
FFMPEG_MAKE = $(MAKE) -C $(FFMPEG_BUILD_DIR) -f Makefile
CROSS_PREFIX = $(patsubst %gcc,%,$(CC))
FFMPEG_ARCH = $(ARCH)
FFMPEG_EXTRA_CFLAGS =
FFMPEG_SYNC_EXCLUDES = \
	--exclude=.build \
	--exclude=.git \
	--exclude=ffbuild/.ewok-config-* \
	--exclude=config.h \
	--exclude=config.asm \
	--exclude=config_components.h \
	--exclude=ffbuild/config.mak \
	--exclude=ffbuild/config.log \
	--exclude=ffbuild/config.err \
	--exclude=compat/strtod.d \
	--exclude='*.o' \
	--exclude='*.a' \
	--exclude='*.d' \
	--exclude='*.so' \
	--exclude='*.so.*' \
	--exclude='*.ver'

ifeq ($(ARCH),arm)
ifeq ($(ARCH_VER),v7)
FFMPEG_EXTRA_CFLAGS += -march=armv7-a
endif
endif

ifeq ($(ARCH),x86)
FFMPEG_EXTRA_CONFIGURE_FLAGS += --disable-x86asm
endif

CONFIGURE_FLAGS = \
	--prefix=$(PREFIX) \
	--libdir=$(PREFIX)/lib \
	--incdir=$(PREFIX)/include \
	--pkgconfigdir=$(PREFIX)/lib/pkgconfig \
	--enable-cross-compile \
	--arch=$(FFMPEG_ARCH) \
	--target-os=linux \
	--cross-prefix=$(CROSS_PREFIX) \
	--cc=$(CC) \
	--cxx=$(CXX) \
	--ar=$(AR) \
	--ranlib=$(CROSS_PREFIX)ranlib \
	--nm=$(CROSS_PREFIX)nm \
	--disable-programs \
	--disable-doc \
	--disable-debug \
	--disable-autodetect \
	--disable-network \
	--disable-iconv \
	--disable-zlib \
	--disable-bzlib \
	--disable-lzma \
	--disable-sdl2 \
	--disable-xlib \
	--disable-vulkan \
	--disable-vaapi \
	--disable-vdpau \
	--disable-postproc \
	--disable-avdevice \
	--disable-avfilter \
	--disable-pthreads \
	--disable-w32threads \
	--disable-os2threads \
	--enable-asm \
	--disable-everything \
	--enable-static \
	--disable-shared \
	--enable-small \
	--enable-avcodec \
	--enable-avformat \
	--enable-avutil \
	--enable-swresample \
	--enable-swscale \
	--enable-protocol=file \
	--enable-demuxer=avi,flac,matroska,mov,mp3,ogg,wav \
	--enable-parser=aac,aac_latm,flac,h264,hevc,mpegaudio,mpeg4video,opus,vorbis \
	--enable-decoder=aac,flac,h264,hevc,mjpeg,mp3,mpeg2video,mpeg4,opus,pcm_f32le,pcm_s16be,pcm_s16le,pcm_u8,vorbis \
	--enable-bsf=aac_adtstoasc \
	--extra-cflags="$(CFLAGS) $(FFMPEG_EXTRA_CFLAGS) -isystem $(SYS_BUILD_DIR)/include -include string.h -include math.h" \
	--extra-ldflags="-nostartfiles -nostdlib -L $(SYS_BUILD_DIR)/lib -Wl,-Ttext=100" \
	--extra-libs="-Wl,--start-group -lewoksys -lc -lgloss -lgcc -Wl,--end-group -lm -lopenlibm" \
	$(FFMPEG_EXTRA_CONFIGURE_FLAGS)

TARGET_LIBS = \
	$(SYS_BUILD_DIR)/lib/libavcodec.a \
	$(SYS_BUILD_DIR)/lib/libavformat.a \
	$(SYS_BUILD_DIR)/lib/libavutil.a \
	$(SYS_BUILD_DIR)/lib/libswresample.a \
	$(SYS_BUILD_DIR)/lib/libswscale.a

FFMPEG_STAGE_HEADER = $(FFMPEG_STAGE_DIR)/include/libavcodec/avcodec.h
FFMPEG_BUILD_LIBS = \
	$(FFMPEG_BUILD_DIR)/libavcodec/libavcodec.a \
	$(FFMPEG_BUILD_DIR)/libavformat/libavformat.a \
	$(FFMPEG_BUILD_DIR)/libavutil/libavutil.a \
	$(FFMPEG_BUILD_DIR)/libswresample/libswresample.a \
	$(FFMPEG_BUILD_DIR)/libswscale/libswscale.a
TARGET_LIBS_STAMP = $(SYS_BUILD_DIR)/lib/.ffmpeg-libs-$(HW)-$(ARCH)$(FFMPEG_ARCH_VER_KEY)

CONFIG_STAMP = $(FFMPEG_BUILD_DIR)/ffbuild/.ewok-config-$(ARCH)$(FFMPEG_ARCH_VER_KEY)

.DEFAULT_GOAL := all

.PHONY: sync-source

sync-source:
	@mkdir -p $(FFMPEG_BUILD_DIR)
	rsync -a $(FFMPEG_SYNC_EXCLUDES) $(FFMPEG_SRC_DIR)/ $(FFMPEG_BUILD_DIR)/

all: $(TARGET_LIBS_STAMP)

$(CONFIG_STAMP): | sync-source
	@test -f $(FFMPEG_BUILD_DIR)/libavfilter/allfilters.c || : > $(FFMPEG_BUILD_DIR)/libavfilter/allfilters.c
	@test -f $(FFMPEG_BUILD_DIR)/libavdevice/alldevices.c || : > $(FFMPEG_BUILD_DIR)/libavdevice/alldevices.c
	@if [ -f $(FFMPEG_BUILD_DIR)/ffbuild/config.mak ] && \
		grep -Fq -- "--prefix=$(PREFIX)" $(FFMPEG_BUILD_DIR)/ffbuild/config.mak && \
		grep -Fq -- "--arch=$(FFMPEG_ARCH)" $(FFMPEG_BUILD_DIR)/ffbuild/config.mak && \
		grep -Fq -- "--cc=$(CC)" $(FFMPEG_BUILD_DIR)/ffbuild/config.mak; then \
		:; \
	else \
		$(FFMPEG_MAKE) distclean >/dev/null 2>&1 || true; \
		rm -f $(FFMPEG_BUILD_DIR)/ffbuild/.ewok-config-*; \
		cd $(FFMPEG_BUILD_DIR) && ./configure $(CONFIGURE_FLAGS); \
	fi
	@touch $@

$(FFMPEG_STAGE_HEADER): $(CONFIG_STAMP)
	$(FFMPEG_MAKE)
	$(FFMPEG_MAKE) install

$(TARGET_LIBS_STAMP): $(FFMPEG_STAGE_HEADER) $(FFMPEG_BUILD_LIBS)
	mkdir -p $(SYS_BUILD_DIR)/include
	mkdir -p $(SYS_BUILD_DIR)/lib
	cp -r $(FFMPEG_STAGE_DIR)/include/. $(SYS_BUILD_DIR)/include/
	cp $(FFMPEG_BUILD_DIR)/libavcodec/libavcodec.a $(SYS_BUILD_DIR)/lib/
	cp $(FFMPEG_BUILD_DIR)/libavformat/libavformat.a $(SYS_BUILD_DIR)/lib/
	cp $(FFMPEG_BUILD_DIR)/libavutil/libavutil.a $(SYS_BUILD_DIR)/lib/
	cp $(FFMPEG_BUILD_DIR)/libswresample/libswresample.a $(SYS_BUILD_DIR)/lib/
	cp $(FFMPEG_BUILD_DIR)/libswscale/libswscale.a $(SYS_BUILD_DIR)/lib/
	touch $@

clean:
	rm -rf $(FFMPEG_BUILD_DIR)
	rm -f $(SYS_ROOT_DIR)/build/*/lib/.ffmpeg-libs-*
	rm -f $(TARGET_LIBS)
