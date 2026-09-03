PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

TASK_OBJS = $(ARCH)/matrix.o

EXTRA_LIBS_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)

CXXFLAGS += -I $(EXTRA_LIBS_DIR)/include -I $(EXTRA_LIBS_DIR)/include/glcommon
LDFLAGS += -L $(EXTRA_LIBS_DIR)/lib -lSDL2_image -lSDL2_mixer -lSDL2_gfx -lSDL2_ttf -lSDL2

GLCOMMON_LIB = $(EXTRA_LIBS_DIR)/lib/libglcommon.a \
	$(EXTRA_LIBS_DIR)/lib/libportablegl.a

TASK = matrix
TASK_DIR = $(EWOKOS_SYS_DIR)/build_$(ARCH)/$(HW)/rootfs/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS) $(GLCOMMON_LIB) \
		$(SDK_DIR)/lib/libewoksys.a \
		$(SDK_DIR)/lib/libewokstl.a \
		$(SDK_DIR)/lib/libx++.a \
		$(SDK_DIR)/lib/libx.a \
		$(SDK_DIR)/lib/libfont.a \
		$(SDK_DIR)/lib/libgraph.a \
		$(SDK_DIR)/lib/libwidget++.a \
		$(SDK_DIR)/lib/libSDL2_image.a \
		$(SDK_DIR)/lib/libSDL2_mixer.a \
		$(SDK_DIR)/lib/libSDL2_gfx.a \
		$(SDK_DIR)/lib/libSDL2_ttf.a \
		$(SDK_DIR)/lib/libSDL2.a \
		$(SDK_DIR)/lib/libopenlibm.a \
		$(SDK_DIR)/lib/libcxx.a
	mkdir -p $(TASK_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) $(GLCOMMON_LIB) -o $(TASK_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH) -lopenlibm $(EWOK_LIBC) -lcxx
	cp -r res $(TASK_DIR)

clean:
	rm -rf $(ALL_ARCH_DIRS)
	rm -f *.o
