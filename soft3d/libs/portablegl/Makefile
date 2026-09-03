PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

BUILD_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CFLAGS += -Iinclude -I $(TARGET_DIR)/include

LIB_PORTABLEGL_OBJS = $(ARCH)/src/portablegl.o

PORTABLEGL = $(BUILD_DIR)/lib/libportablegl.a

$(PORTABLEGL): $(LIB_PORTABLEGL_OBJS)
	$(AR) rcs $(PORTABLEGL) $(LIB_PORTABLEGL_OBJS)
	cp -r include/* $(BUILD_DIR)/include

.PHONY: clean

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f src/*.o
	rm -f $(PORTABLEGL)
