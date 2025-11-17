
all: 
	mkdir -p build
	cd apps; make
	cd mariox; make

clean:	
	cd apps; make clean
	cd mariox; make clean
	rm -fr build
