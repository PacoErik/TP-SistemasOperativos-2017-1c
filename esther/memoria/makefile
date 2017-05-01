PROG=memoria
LIB=../QEPDLibrary
LIBBIN=$(LIB)/build

all: compile

compile:
	gcc $(PROG).c  -I$(LIB) -L$(LIBBIN) -lcommons -lqepd -Wl,-rpath=$(LIBBIN) -o $(PROG) -pthread
	

clean:
	rm -f memoria

.PHONY: compile clean
