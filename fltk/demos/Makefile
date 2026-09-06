PROJS_ROOT_DIR=../..
include $(PROJS_ROOT_DIR)/make.inc

## FLTK demo apps.  Each subdir builds one program against libfltk.a + the
## NX11/nano-X stack and installs it into rootfs/apps/<name>.  `hello` is the
## minimal widget/event-loop demo; `image` exercises libfltk_images (PNG/JPEG
## decode via the SDK's libpng/libjpeg, drawn through fl_draw_image/XPutImage);
## `editor` is FLTK's own test/editor.cxx (Fl_Text_Editor + syntax highlighting);
## `tceditor` is Tiny Core's fltk_projects/editor (no highlighting); `flpicsee`
## is the Tiny Core image viewer; `fluff` is the Tiny Core file manager.
DIRS = hello image editor tceditor flpicsee fluff

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
