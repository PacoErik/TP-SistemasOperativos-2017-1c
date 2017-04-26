all:
	@echo "Compilando consola"
	@echo "======================"
	cd consola; make
	@echo " "
	@echo "Compilando cpu"
	@echo "======================"
	cd cpu; make
	@echo " "
	@echo "Compilando filesystem"
	@echo "======================"
	cd filesystem; make
	@echo " "
	@echo "Compilando kernel"
	@echo "======================"
	cd kernel; make
	@echo " "
	@echo "Compilando memoria"
	@echo "======================"
	cd memoria; make
	@echo " "

clean:
	cd QEPDLibrary; make clean
	cd consola; make clean
	cd cpu; make clean
	cd filesystem; make clean
	cd kernel; make clean
	cd memoria; make clean
