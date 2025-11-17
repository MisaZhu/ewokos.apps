SW_ROOT_DIR=../..
include $(SW_ROOT_DIR)/make.inc

TASK_OBJS = main.o \
		src/InfoNES.o \
		src/InfoNES_Mapper.o \
		src/InfoNES_pAPU.o  \
		src/K6502.o

TASK = emu
BUILD_DIR = $(SW_ROOT_DIR)/build/$(HW)/apps/$(TASK)

all: $(TASK)

$(TASK): $(TASK_OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -Ttext=100 $(TASK_OBJS) -o $(BUILD_DIR)/$(TASK) $(LDFLAGS) $(EWOK_LIB_X) $(EWOK_LIB_GRAPH)  -lopenlibm $(EWOK_LIBC)  -lcxx
	cp -r res $(BUILD_DIR)

clean:
	rm -f $(TASK_OBJS)
