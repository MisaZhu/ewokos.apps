PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -I. -Iinclude -Iinclude/glcommon -I $(BUILD_DIR)/include -I../external -I../portablegl/include -I../portablegl/include/portablegl
CXXFLAGS += -I. -Iinclude -Iinclude/glcommon -I $(BUILD_DIR)/include -I../external -I../portablegl/include -I../portablegl/include/portablegl

# C source files
LIB_GLCOMMON_C_OBJS = \
	$(ARCH)/src/c_utils.o \
	$(ARCH)/src/cprimitives.o \
	$(ARCH)/src/chalfedge.o \
	$(ARCH)/src/gltools.o \
	$(ARCH)/src/stb_vorbis.o

# C++ source files
LIB_GLCOMMON_CPP_OBJS = \
	$(ARCH)/src/Frame.o

LIB_GLCOMMON_OBJS = $(LIB_GLCOMMON_C_OBJS) $(LIB_GLCOMMON_CPP_OBJS)

GLCOMMON = $(BUILD_DIR)/lib/libglcommon.a

$(GLCOMMON): $(LIB_GLCOMMON_OBJS)
	$(AR) rcs $(GLCOMMON) $(LIB_GLCOMMON_OBJS)
	cp -r include/* $(BUILD_DIR)/include

# Compile C files
$(ARCH)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile C++ files
$(ARCH)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(LIB_GLCOMMON_OBJS)
	rm -f $(GLCOMMON)
