
all: 
	mkdir -p build
	cd libs; make
	cd apps; make

clean:	
	cd libs; make clean
	cd apps; make clean
	rm -fr build
