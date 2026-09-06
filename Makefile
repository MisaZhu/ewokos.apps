
DIRS = macemu nesemu soft3d doom cards mine previous nx11 fltk
#	video browser saver minivmac

all: $(DIRS)

$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
