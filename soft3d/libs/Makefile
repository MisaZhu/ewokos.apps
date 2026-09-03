
DIRS = portablegl glcommon imgui
#	demos

# demos is not built by default, but its leftovers still need cleaning
CLEAN_DIRS = $(DIRS) demos

.PHONY: all clean

all: 
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

clean: 
	@for dir in $(CLEAN_DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
