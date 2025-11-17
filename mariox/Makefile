SW_ROOT_DIR=..
SYS_DIR=$(SW_ROOT_DIR)/../system
include $(SW_ROOT_DIR)/make.inc

BUILD_DIR = $(SYS_DIR)/build/$(HW)

MARIO_VM = mario_vm
MARIOX = .
include $(MARIOX)/lang/js/lang.mk

mario_OBJS = $(MARIO_VM)/mario/mario.o $(MARIO_VM)/mario/bcdump/bcdump.o
platform_OBJS = $(MARIO_VM)/platform/platform.o \
		$(MARIO_VM)/platform/mem.o

lib_OBJS =  $(MARIO_VM)/bin/lib/mbc.o $(MARIO_VM)/bin/lib/js.o bin/lib/vm_json_var.o bin/lib/native_UniObject.o
wjsvm_OBJS = bin/wjs/wjs.o bin/wjs/native_wjs.o $(lib_OBJS)
mvm_OBJS = $(MARIO_VM)/bin/mario/mario.o $(lib_OBJS)

WJS_OBJS = $(mario_OBJS) $(platform_OBJS) $(wjsvm_OBJS) $(lang_OBJS) \
		$(NATIVE_OBJS)

MARIO_OBJS = $(mario_OBJS) $(platform_OBJS) $(mvm_OBJS) $(lang_OBJS) \
		$(NATIVE_OBJS)

ifneq ($(MARIO_DEBUG), no)
CFLAGS += -g -DMARIO_DEBUG
CXXFLAGS += -g -DMARIO_DEBUG
else
CFLAGS += -O2
CXXFLAGS += -O2
endif

ifneq ($(MARIO_CACHE), no)
CFLAGS += -DMARIO_CACHE
CXXFLAGS += -DMARIO_CACHE
endif

ifneq ($(MARIO_THREAD), no)
CFLAGS += -DMARIO_THREAD
CXXFLAGS += -DMARIO_THREAD
endif

HEADS = \
	-I$(NATIVE_PATH_BUILTIN) \
	-I$(NATIVE_PATH_GRAPH) \
	-I$(NATIVE_PATH_X) \
	-I$(MARIO_VM)/mario \
	-I$(MARIOX)/bin/lib \
	-I$(MARIO_VM)/bin/lib \
	-I$(MARIO_VM)/platform

CFLAGS += $(HEADS)
CXXFLAGS += $(HEADS)

TARGET_DIR = $(SW_ROOT_DIR)/build/$(HW)/bin
WJS = $(TARGET_DIR)/wjs
MARIO = $(TARGET_DIR)/mario

all: $(MARIO) $(WJS)
	@echo "done"

$(MARIO): $(MARIO_OBJS) \
		$(EWOK_LIBC_A)  \
		$(BUILD_DIR)/lib/libpng.a \
		$(BUILD_DIR)/lib/libewokstl.a \
		$(BUILD_DIR)/lib/libx++.a \
		$(BUILD_DIR)/lib/libx.a \
		$(BUILD_DIR)/lib/libfont.a \
		$(BUILD_DIR)/lib/libwidget++.a \
		$(BUILD_DIR)/lib/libcxx.a 
	mkdir -p $(TARGET_DIR)
	$(LD) -Ttext=100 $(MARIO_OBJS) -o $(MARIO) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx

$(WJS): $(WJS_OBJS) \
		$(EWOK_LIBC_A)  \
		$(BUILD_DIR)/lib/libpng.a \
		$(BUILD_DIR)/lib/libewokstl.a \
		$(BUILD_DIR)/lib/libx++.a \
		$(BUILD_DIR)/lib/libx.a \
		$(BUILD_DIR)/lib/libfont.a \
		$(BUILD_DIR)/lib/libwidget++.a \
		$(BUILD_DIR)/lib/libcxx.a 
	mkdir -p $(TARGET_DIR)
	$(LD) -Ttext=100 $(WJS_OBJS) -o $(WJS) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) $(EWOK_LIBC) -lcxx

clean:
	rm -fr $(MARIO) $(WJS)
	rm -fr $(MARIO_OBJS) $(WJS_OBJS)
