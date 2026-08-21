PROJS_ROOT_DIR = ../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build/$(HW)
TARGET_DIR = $(BUILD_DIR)/rootfs

CFLAGS += -Iinclude -Isrc -Isrc/external

LIB_FEROX_OBJS = $(ARCH)/src/broadphase.o \
	$(ARCH)/src/collision.o \
	$(ARCH)/src/dynamics.o \
	$(ARCH)/src/geometry.o \
	$(ARCH)/src/timer.o \
	$(ARCH)/src/utils.o \
	$(ARCH)/src/world.o

FEROX = libferox.a

$(FEROX): $(LIB_FEROX_OBJS)
	mkdir -p $(BUILD_DIR)/lib
	$(AR) rcs $(BUILD_DIR)/lib/$(FEROX) $(LIB_FEROX_OBJS)
	cp -r include $(BUILD_DIR)/

clean:
	rm -f $(LIB_FEROX_OBJS)

