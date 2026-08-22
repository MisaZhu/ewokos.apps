
all: 
	cd macemu; make
	cd minivmac; make
	cd nesemu; make
	cd soft3d; make
#	cd video; make
#	cd browser; make
#	cd saver; make


clean:	
	cd macemu; make clean
	cd minivmac; make clean
	cd nesemu; make clean
	cd soft3d; make clean
#	cd video; make clean
#	cd browser; make clean
#	cd saver; make clean
