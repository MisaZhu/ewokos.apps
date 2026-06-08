SW_ROOT_DIR=../..
include $(SW_ROOT_DIR)/make.inc

BUILD_DIR = $(SYS_DIR)/build/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

LDFLAGS += -L $(BUILD_DIR)/lib \
	-lSDL2_image -lSDL2_mixer -lSDL2_gfx -lSDL2_ttf -lSDL2 -lfreetype -lpng -lvorbis -logg -lz -lopenlibm

CFLAGS += -I $(BUILD_DIR)/include
CXXFLAGS += -I $(BUILD_DIR)/include

TASK_OBJS = \
		src/GLOBGLUE.o \
		src/PROGMAIN.o \
		src/MYOSGLUE.o \
		src/MINEM68K.o \
		src/M68KITAB.o \
		src/VIAEMDEV.o \
		src/IWMEMDEV.o \
		src/SCCEMDEV.o \
		src/RTCEMDEV.o \
		src/ROMEMDEV.o \
		src/SCSIEMDV.o \
		src/SONYEMDV.o \
		src/SCRNEMDV.o \
		src/KBRDEMDV.o \
		src/MOUSEMDV.o \
		src/SNDEMDEV.o

TASK = minivmac
TASK_DIR = $(SYS_DIR)/build/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) \
		$(EWOK_LIBC_A)  \
		$(BUILD_DIR)/lib/libSDL2.a \
		$(BUILD_DIR)/lib/libSDL2_image.a \
		$(BUILD_DIR)/lib/libSDL2_mixer.a \
		$(BUILD_DIR)/lib/libSDL2_ttf.a \
		$(BUILD_DIR)/lib/libfreetype.a \
		$(BUILD_DIR)/lib/libpng.a \
		$(BUILD_DIR)/lib/libewokstl.a \
		$(BUILD_DIR)/lib/libx++.a \
		$(BUILD_DIR)/lib/libx.a \
		$(BUILD_DIR)/lib/libfont.a \
		$(BUILD_DIR)/lib/libwidget++.a \
		$(BUILD_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TASK_OBJS)