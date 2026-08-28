PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS += -L $(BUILD_DIR)/lib \
	-lSDL2 -lSDL2_gfx -lz -lopenlibm

## Previous build configuration for EwokOS.
## config.h lives in src/ewok (replaces the cmake-generated one);
## ewok_compat.h is force-included everywhere for libc shims.
PV_DEFS = -include src/ewok/ewok_compat.h -DCONFDIR=\"/apps/previous/res/\"
PV_INC = -I src/ewok -I src -I src/includes -I src/cpu -I src/debug \
	-I src/dsp -I src/dimension -I src/slirp -I src/softfloat \
	-I $(BUILD_DIR)/include -I $(BUILD_DIR)/include/SDL2

PV_CFLAGS = $(PV_DEFS) $(PV_INC) -fcommon \
	-Wno-sign-compare -Wno-shadow -Wno-unused-variable -Wno-unused-label \
	-Wno-unused-but-set-variable -Wno-missing-field-initializers \
	-Wno-format-truncation -Wno-deprecated-declarations
PV_CXXFLAGS = $(PV_DEFS) $(PV_INC) -fcommon -Wno-pedantic \
	-Wno-sign-compare -Wno-shadow -Wno-unused-variable -Wno-unused-label \
	-Wno-unused-but-set-variable -Wno-missing-field-initializers

## Host tools that generate the 68k CPU core
HOSTCC = cc
HOST_DIR = host

## Object groups (mirrors src/CMakeLists.txt and its subdirectories)
CORE_SRCS = adb audio bmap cfgopts configuration options change \
	control cycInt dialog dma esp enet_slirp ethernet file floppy \
	ioMem ioMemTabNEXT ioMemTabTurbo keymap kms m68000 main mo nbic \
	nextMemory paths printer queue ramdac reset rs rtcnvram scandir \
	scc fast_screen host scsi shortcut snd statusbar str sysReg tmc \
	unzip utils video zip
CORE_OBJS = $(patsubst %,$(ARCH)/core/%.o,$(CORE_SRCS))

CPU_SRCS = cpuemu_31 cpuemu_32 cpudefs cpummu cpummu030 cpustbl \
	cpuemu_common hatari-glue memory newcpu readcpu fpp
CPU_OBJS = $(patsubst %,$(ARCH)/cpu/%.o,$(CPU_SRCS))

DEBUG_SRCS = 68kDisass log debugui breakcond debugcpu debugInfo \
	evaluate profile symbols
DEBUG_OBJS = $(patsubst %,$(ARCH)/dbg/%.o,$(DEBUG_SRCS))

GUI_SRCS = dlgAbout dlgAdvanced dlgAlert dlgBoot dlgDimension \
	dlgDiskSelect dlgEthernet dlgFileSelect dlgFloppy dlgKeyboard \
	dlgMain dlgMemory dlgMemoryAdvanced dlgMissingFile dlgMouse \
	dlgOpticalDisk dlgPrinter dlgRom dlgSCSI dlgSound dlgSystem sdlgui
GUI_OBJS = $(patsubst %,$(ARCH)/gui/%.o,$(GUI_SRCS))

DSP_SRCS = dsp dsp_core dsp_cpu dsp_disasm
DSP_OBJS = $(patsubst %,$(ARCH)/dsp/%.o,$(DSP_SRCS))

ND_SRCS = dimension nd_mem nd_nbic nd_devs nd_vio nd_rom nd_sdl
ND_OBJS = $(patsubst %,$(ARCH)/nd/%.o,$(ND_SRCS)) \
	$(ARCH)/nd/i860.o $(ARCH)/nd/i860dis.o

SLIRP_SRCS = bootp cksum if ip_icmp ip_input ip_output mbuf misc \
	sbuf slirp slirpdebug socket tcp_input tcp_output tcp_subr \
	tcp_timer tftp udp
SLIRP_OBJS = $(patsubst %,$(ARCH)/slirp/%.o,$(SLIRP_SRCS))

SF_SRCS = softfloat softfloat_decimal softfloat_fpsp
SF_OBJS = $(patsubst %,$(ARCH)/sf/%.o,$(SF_SRCS))

EWOK_OBJS = $(ARCH)/ewok/ewok_compat.o $(ARCH)/ewok/ewok_assets.o

TASK_OBJS = $(CORE_OBJS) $(CPU_OBJS) $(DEBUG_OBJS) $(GUI_OBJS) \
	$(DSP_OBJS) $(ND_OBJS) $(SLIRP_OBJS) $(SF_OBJS) $(EWOK_OBJS)

TASK = previous
TASK_DIR = $(TARGET_DIR)/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(BUILD_DIR)/lib/libewoksys.a \
		$(BUILD_DIR)/lib/libSDL2.a \
		$(BUILD_DIR)/lib/libSDL2_gfx.a \
		$(BUILD_DIR)/lib/libewokstl.a \
		$(BUILD_DIR)/lib/libx++.a \
		$(BUILD_DIR)/lib/libx.a \
		$(BUILD_DIR)/lib/libfont.a \
		$(BUILD_DIR)/lib/libgraph.a \
		$(BUILD_DIR)/lib/libwidget++.a \
		$(BUILD_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	rm -rf $(TASK_DIR)/res
	cp -r res $(TASK_DIR)

## CPU core generation (host tools)
$(HOST_DIR):
	mkdir -p $(HOST_DIR)

$(HOST_DIR)/build68k: src/cpu/build68k.c | $(HOST_DIR)
	$(HOSTCC) -O2 -I src/cpu -o $@ $<

src/cpu/cpudefs.c: $(HOST_DIR)/build68k src/cpu/table68k
	$(HOST_DIR)/build68k < src/cpu/table68k > $@

$(HOST_DIR)/gencpu: src/cpu/gencpu.c src/cpu/readcpu.c src/cpu/cpudefs.c | $(HOST_DIR)
	$(HOSTCC) -O2 -I src/cpu -I src/ewok \
		src/cpu/cpudefs.c src/cpu/gencpu.c src/cpu/readcpu.c -o $@

# gencpu reads "table68k" from its working directory and emits
# cpuemu_*.c, cputbl.h and cpustbl.c
src/cpu/cpuemu_31.c src/cpu/cpuemu_32.c src/cpu/cputbl.h src/cpu/cpustbl.c: $(HOST_DIR)/gencpu src/cpu/table68k
	cd src/cpu && ../../$(HOST_DIR)/gencpu

$(ARCH)/cpu/cpuemu_%.o: src/cpu/cpuemu_%.c src/cpu/cputbl.h
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -O3 -c $< -o $@

$(ARCH)/cpu/cpustbl.o: src/cpu/cpustbl.c src/cpu/cputbl.h
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

## Source groups
$(ARCH)/core/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/cpu/%.o: src/cpu/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -O2 -c $< -o $@

$(ARCH)/dbg/%.o: src/debug/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/gui/%.o: src/gui-sdl/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/dsp/%.o: src/dsp/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/nd/%.o: src/dimension/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/nd/%.o: src/dimension/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(PV_CXXFLAGS) $(CXXFLAGS) -O2 -c $< -o $@

$(ARCH)/slirp/%.o: src/slirp/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/sf/%.o: src/softfloat/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

$(ARCH)/ewok/%.o: src/ewok/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PV_CFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TASK_OBJS)
	rm -rf $(HOST_DIR)
	rm -f src/cpu/cpudefs.c src/cpu/cpustbl.c src/cpu/cputbl.h \
		src/cpu/cpuemu_*.c
