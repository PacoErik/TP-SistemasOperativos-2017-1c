MAKE=make

all:
	@echo "Compilando consola"
	@echo "======================"
	cd consola; $(MAKE)
	@echo " "
	@echo "Compilando cpu"
	@echo "======================"
	cd cpu; $(MAKE)
	@echo " "
	@echo "Compilando filesystem"
	@echo "======================"
	cd filesystem; $(MAKE)
	@echo " "
	@echo "Compilando kernel"
	@echo "======================"
	cd kernel; $(MAKE)
	@echo " "
	@echo "Compilando memoria"
	@echo "======================"
	cd memoria; $(MAKE)
	@echo " "

debug: MAKE += debug
debug: all

clean:
	cd QEPDLibrary; make clean
	cd consola; make clean
	cd cpu; make clean
	cd filesystem; make clean
	cd kernel; make clean
	cd memoria; make clean
