PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS += -L $(BUILD_DIR)/lib \
	-lz -lopenlibm

CFLAGS += -I $(BUILD_DIR)/include
CXXFLAGS += -I $(BUILD_DIR)/include

## Basilisk II build configuration for EwokOS
B2_DEFS = -D_REENTRANT -DDATADIR=\"/apps/macemu/res\" -include src/ewok/ewok_compat.h
B2_INC = -I src/include -I src -I src/Unix -I src/ewok \
	-I src/uae_cpu -I src/CrossPlatform

B2_CFLAGS = $(B2_DEFS) $(B2_INC) -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
B2_CXXFLAGS = $(B2_DEFS) $(B2_INC) -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-pedantic

UAE = src/uae_cpu

## Host tools that generate the 68k CPU core
HOSTCC = cc
HOSTCXX = c++
HOST_DIR = host
HOST_INCS = -I $(UAE) -I src/Unix -I src/ewok

## Object groups
CORE_SRCS = adb audio cdrom disk emul_op ether extfs macos_util main \
	prefs prefs_items rom_patches rsrc_patches scsi serial slot_rom \
	sony timer user_strings video xpram
CORE_OBJS = $(patsubst %,$(ARCH)/core/%.o,$(CORE_SRCS))

UNIX_SRCS = main_unix sys_unix timer_unix prefs_unix xpram_unix \
	extfs_unix posix_sem user_strings_unix
UNIX_OBJS = $(patsubst %,$(ARCH)/unix/%.o,$(UNIX_SRCS))

DUMMY_SRCS = clip_dummy serial_dummy scsi_dummy prefs_editor_dummy
DUMMY_OBJS = $(patsubst %,$(ARCH)/dummy/%.o,$(DUMMY_SRCS))

XPLAT_OBJS = $(ARCH)/xplat/video_blit.o

EWOK_OBJS = $(ARCH)/ewok/vm_alloc_ewok.o $(ARCH)/ewok/sigsegv_ewok.o \
	$(ARCH)/ewok/rpc_stub.o $(ARCH)/ewok/ewok_compat.o \
	$(ARCH)/ewok/video_xwin.o $(ARCH)/ewok/audio_pcm.o \
	$(ARCH)/ewok/ether_ewok.o

UAE_SRCS = basilisk_glue memory newcpu readcpu
UAE_OBJS = $(patsubst %,$(ARCH)/uae/%.o,$(UAE_SRCS)) \
	$(ARCH)/uae/fpu/fpu_uae.o \
	$(ARCH)/uae/cpudefs.o $(ARCH)/uae/cpustbl.o

# the generated interpreter is the emulation hot path: split in 8 parts, compile hotter
GEN_OBJS = $(ARCH)/gen/cpuemu1.o $(ARCH)/gen/cpuemu2.o $(ARCH)/gen/cpuemu3.o $(ARCH)/gen/cpuemu4.o \
	$(ARCH)/gen/cpuemu5.o $(ARCH)/gen/cpuemu6.o $(ARCH)/gen/cpuemu7.o $(ARCH)/gen/cpuemu8.o

TASK_OBJS = $(CORE_OBJS) $(UNIX_OBJS) $(DUMMY_OBJS) $(XPLAT_OBJS) \
	$(EWOK_OBJS) $(UAE_OBJS) $(GEN_OBJS)

TASK = macemu
TASK_DIR = $(TARGET_DIR)/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(BUILD_DIR)/lib/libewoksys.a \
		$(BUILD_DIR)/lib/libewokstl.a \
		$(BUILD_DIR)/lib/libx++.a \
		$(BUILD_DIR)/lib/libx.a \
		$(BUILD_DIR)/lib/libfont.a \
		$(BUILD_DIR)/lib/libgraph.a \
		$(BUILD_DIR)/lib/libwidget++.a \
		$(BUILD_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

## CPU core generation (host tools)
$(HOST_DIR):
	mkdir -p $(HOST_DIR)

$(HOST_DIR)/build68k: $(UAE)/build68k.c | $(HOST_DIR)
	$(HOSTCC) -O2 $(HOST_INCS) -o $@ $<

$(UAE)/cpudefs.cpp: $(HOST_DIR)/build68k $(UAE)/table68k
	$(HOST_DIR)/build68k < $(UAE)/table68k > $@

$(HOST_DIR)/cpudefs.o: $(UAE)/cpudefs.cpp | $(HOST_DIR)
	$(HOSTCXX) -O2 $(HOST_INCS) -c $< -o $@

$(HOST_DIR)/gencpu.o: $(UAE)/gencpu.c | $(HOST_DIR)
	$(HOSTCC) -O2 $(HOST_INCS) -c $< -o $@

$(HOST_DIR)/readcpu.o: $(UAE)/readcpu.cpp | $(HOST_DIR)
	$(HOSTCXX) -O2 $(HOST_INCS) -c $< -o $@

$(HOST_DIR)/gencpu: $(HOST_DIR)/gencpu.o $(HOST_DIR)/readcpu.o $(HOST_DIR)/cpudefs.o
	$(HOSTCXX) -o $@ $(HOST_DIR)/gencpu.o $(HOST_DIR)/readcpu.o $(HOST_DIR)/cpudefs.o

# gencpu reads "table68k" from its working directory and emits
# cpuemu.cpp, cputbl.h and cpustbl.cpp
$(UAE)/cpuemu.cpp $(UAE)/cputbl.h $(UAE)/cpustbl.cpp: $(HOST_DIR)/gencpu $(UAE)/table68k
	cd $(UAE) && ../../$(HOST_DIR)/gencpu

## Generated CPU core objects
$(ARCH)/gen/cpuemu%.o: $(UAE)/cpuemu.cpp $(UAE)/cputbl.h
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -O3 -DPART_$* -c $< -o $@

$(ARCH)/uae/cpustbl.o: $(UAE)/cpustbl.cpp $(UAE)/cputbl.h
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ARCH)/uae/cpudefs.o: $(UAE)/cpudefs.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

## UAE CPU core
$(ARCH)/uae/%.o: $(UAE)/%.c
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -O2 -c $< -o $@

$(ARCH)/uae/%.o: $(UAE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -O2 -c $< -o $@

$(ARCH)/uae/fpu/%.o: $(UAE)/fpu/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -O2 -c $< -o $@

## Core emulation sources
$(ARCH)/core/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

## Platform sources
$(ARCH)/unix/%.o: src/Unix/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ARCH)/dummy/%.o: src/dummy/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ARCH)/xplat/%.o: src/CrossPlatform/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ARCH)/ewok/%.o: src/ewok/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(B2_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ARCH)/ewok/%.o: src/ewok/%.c
	@mkdir -p $(dir $@)
	$(CC) $(B2_CFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TASK_OBJS)
	rm -rf $(HOST_DIR)
	rm -f $(UAE)/cpudefs.cpp $(UAE)/cpuemu.cpp $(UAE)/cputbl.h \
		$(UAE)/cpustbl.cpp $(UAE)/cpustbl_nf.cpp
