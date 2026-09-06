PROJS_ROOT_DIR=../..
include $(PROJS_ROOT_DIR)/make.inc

## FLTK demo apps.  Each subdir builds one program against libfltk.a + the
## NX11/nano-X stack and installs it into rootfs/apps/<name>.
DIRS = hello

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
