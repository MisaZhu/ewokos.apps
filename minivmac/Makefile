PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS += -L $(BUILD_DIR)/lib

CFLAGS += -I $(BUILD_DIR)/include
CXXFLAGS += -I $(BUILD_DIR)/include

TASK_OBJS = \
		$(ARCH)/src/GLOBGLUE.o \
		$(ARCH)/src/PROGMAIN.o \
		$(ARCH)/src/MYOSGLUE.o \
		$(ARCH)/src/MINEM68K.o \
		$(ARCH)/src/M68KITAB.o \
		$(ARCH)/src/VIAEMDEV.o \
		$(ARCH)/src/IWMEMDEV.o \
		$(ARCH)/src/SCCEMDEV.o \
		$(ARCH)/src/RTCEMDEV.o \
		$(ARCH)/src/ROMEMDEV.o \
		$(ARCH)/src/SCSIEMDV.o \
		$(ARCH)/src/SONYEMDV.o \
		$(ARCH)/src/SCRNEMDV.o \
		$(ARCH)/src/KBRDEMDV.o \
		$(ARCH)/src/MOUSEMDV.o \
		$(ARCH)/src/SNDEMDEV.o

TASK = minivmac
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

# the 68K interpreter is the emulation hot path: compile it hotter
$(ARCH)/src/MINEM68K.o: src/MINEM68K.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 -c $< -o $@

$(ARCH)/src/M68KITAB.o: src/M68KITAB.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 -c $< -o $@

$(ARCH)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TASK_OBJS)
