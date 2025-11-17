
all: 
	mkdir -p build
	cd apps; make

clean:	
	cd apps; make clean
	rm -fr build
