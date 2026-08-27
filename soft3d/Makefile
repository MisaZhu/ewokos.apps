
DIRS = libs apps

all: $(DIRS)

apps: libs
$(DIRS):
	@$(MAKE) -C $@

.PHONY: all clean $(DIRS)

clean:	
	@for dir in $(DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
