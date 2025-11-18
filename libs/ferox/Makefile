EWOKOS_ROOT_DIR = ../../..
SYS_ROOT_DIR = $(EWOKOS_ROOT_DIR)/system
SYS_BUILD_DIR = $(SYS_ROOT_DIR)/build

SW_ROOT_DIR=../..
include $(SW_ROOT_DIR)/make.inc

CFLAGS += -Iinclude -Isrc -Isrc/external

LIB_FEROX_OBJS = src/broadphase.o \
	src/collision.o \
	src/dynamics.o \
	src/geometry.o \
	src/timer.o \
	src/utils.o \
	src/world.o

FEROX = libferox.a
BUILD_DIR = $(SW_ROOT_DIR)/build/$(HW)

$(FEROX): $(LIB_FEROX_OBJS)
	mkdir -p $(BUILD_DIR)/lib
	$(AR) rcs $(BUILD_DIR)/lib/$(FEROX) $(LIB_FEROX_OBJS)
	cp -r include $(BUILD_DIR)/

clean:
	rm -f $(LIB_FEROX_OBJS)

