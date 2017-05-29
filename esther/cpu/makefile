CC=gcc
MAKE=make

PROG=cpu
LIB=../QEPDLibrary
LIBBIN=$(LIB)/build

all: compile

compile: $(LIB)/build
	$(CC) $(PROG).c -I$(LIB) -L$(LIBBIN) -lcommons -lqepd -lparser-ansisop -Wl,-rpath=$(LIBBIN) -o $(PROG)

$(LIB)/build:
	cd $(LIB); $(MAKE)
	
debug: CC += -DDEBUG -g
debug: MAKE += debug
debug: all

clean:
	rm -f cpu

.PHONY: compile clean
