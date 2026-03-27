ifeq ($(ewokos),)
ewokos=../ewokos
endif

ifeq ($(ARCH),)
export ARCH=aarch64
endif

ifeq ($(HW),)
export HW=virt
endif

all: 
	mkdir -p build
	cd libs; make
	cd apps; make

clean:	
	cd libs; make clean
	cd apps; make clean
	rm -fr build
