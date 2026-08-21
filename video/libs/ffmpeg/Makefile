
PROJS_ROOT_DIR=../../..
include $(PROJS_ROOT_DIR)/make.inc

all: 
	$(MAKE) -C lib -f ewok.mk HW=$(HW) ARCH=$(ARCH) ARCH_VER=$(ARCH_VER)
	cd bin; $(MAKE) HW=$(HW) ARCH=$(ARCH)
#	@cp -r data ${TARGET_DIR}

clean:	
	$(MAKE) -C lib -f ewok.mk HW=$(HW) ARCH=$(ARCH) ARCH_VER=$(ARCH_VER) clean
	cd bin; $(MAKE) HW=$(HW) ARCH=$(ARCH) clean
