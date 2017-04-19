PROG=consola
LIB=../QEPDLibrary
LIBBIN=$(LIB)/build

all: compile

compile:
	gcc $(PROG).c  -I$(LIB) -L$(LIBBIN) -lcommons -lqepd -Wl,-rpath=$(LIBBIN) -o $(PROG)

clean:
	rm -f consola

.PHONY: compile clean
