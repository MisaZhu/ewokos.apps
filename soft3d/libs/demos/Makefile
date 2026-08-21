SW_ROOT_DIR=../../..
include $(SW_ROOT_DIR)/make.inc

BUILD_DIR = $(SYS_ROOT_DIR)/build/$(HW)
ROOTFS_BIN = $(BUILD_DIR)/rootfs/bin/x

CFLAGS += -I.. -I../external -I $(BUILD_DIR)/include/SDL2 -D__ewokos__
CXXFLAGS += -I.. -I../external -I $(BUILD_DIR)/include/SDL2 -I../glcommon/include -I../glcommon/include/glcommon -D__ewokos__ -fno-threadsafe-statics

# Common LDFLAGS
LDFLAGS += -lSDL2_image -lSDL2_mixer -lSDL2_gfx -lSDL2_ttf -lSDL2 -lfreetype -lpng -lvorbis -logg -lz -lm -lopenlibm -lportablegl $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx

# glcommon library
GLCOMMON_LIB = $(BUILD_DIR)/lib/libglcommon.a \
	$(BUILD_DIR)/lib/libportablegl.a
LD_LIB_DEPS = \
	$(GLCOMMON_LIB) \
	$(BUILD_DIR)/lib/libSDL2.a \
	$(BUILD_DIR)/lib/libSDL2_image.a \
	$(BUILD_DIR)/lib/libSDL2_mixer.a \
	$(BUILD_DIR)/lib/libSDL2_gfx.a \
	$(BUILD_DIR)/lib/libSDL2_ttf.a \
	$(BUILD_DIR)/lib/libfreetype.a \
	$(BUILD_DIR)/lib/libpng.a \
	$(BUILD_DIR)/lib/libvorbis.a \
	$(BUILD_DIR)/lib/libogg.a \
	$(BUILD_DIR)/lib/libgif.a \
	$(BUILD_DIR)/lib/libz.a

# C++ files - all refactored to use portablegl's built-in types with PGL_PREFIX_TYPES
# Now all demos compile with C-style code using pgl_compat.h
CPP_SRCS = shadertoy.cpp sphere.cpp cubemap.cpp grass.cpp

C_SRCS = balls.c

# Object files
C_OBJS = $(patsubst %.c,$(ARCH)/%.o,$(C_SRCS))
CPP_OBJS = $(patsubst %.cpp,$(ARCH)/%.o,$(CPP_SRCS))

# Targets in rootfs
C_BINS = $(patsubst %.c,$(ROOTFS_BIN)/%,$(C_SRCS))
CPP_BINS = $(patsubst %.cpp,$(ROOTFS_BIN)/%,$(CPP_SRCS))

ALL_BINS = $(C_BINS) $(CPP_BINS)

.PHONY: all clean dirs

all: dirs $(ALL_BINS)

dirs:
	@mkdir -p $(ROOTFS_BIN)

# Compile C files
$(ARCH)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I../glcommon/include -I../glcommon/include/glcommon -I../portablegl/include -I../portablegl/include/portablegl -c $< -o $@

# Compile C++ files (include glcommon for rsw_math and gltools)
$(ARCH)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I../portablegl/include -I../portablegl/include/portablegl -I../glcommon/include -I../glcommon/include/glcommon -I $(BUILD_DIR)/include -c $< -o $@

# Link C programs
$(ROOTFS_BIN)/%: $(ARCH)/%.o $(LD_LIB_DEPS) \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libewoksys.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libewokstl.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libx++.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libx.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libfont.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libgraph.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libwidget++.a \
		$(SYS_ROOT_DIR)/build/$(HW)/lib/libcxx.a
	$(LD) -Ttext=100 $< $(GLCOMMON_LIB) -o $@ $(LDFLAGS)

clean:
	rm -f $(C_OBJS) $(CPP_OBJS)
	rm -f $(ALL_BINS)
