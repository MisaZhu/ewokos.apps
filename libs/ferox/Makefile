SW_ROOT_DIR = ../..
include $(SW_ROOT_DIR)/make.inc
SYS_DIR = $(EWOKOS_ROOT_DIR)/system

CFLAGS += -Iinclude -Isrc -Isrc/external

LIB_FEROX_OBJS = $(ARCH)/src/broadphase.o \
	$(ARCH)/src/collision.o \
	$(ARCH)/src/dynamics.o \
	$(ARCH)/src/geometry.o \
	$(ARCH)/src/timer.o \
	$(ARCH)/src/utils.o \
	$(ARCH)/src/world.o

FEROX = libferox.a
BUILD_DIR = $(SYS_DIR)/build/$(HW)

$(FEROX): $(LIB_FEROX_OBJS)
	mkdir -p $(BUILD_DIR)/lib
	$(AR) rcs $(BUILD_DIR)/lib/$(FEROX) $(LIB_FEROX_OBJS)
	cp -r include $(BUILD_DIR)/

clean:
	rm -f $(LIB_FEROX_OBJS)

