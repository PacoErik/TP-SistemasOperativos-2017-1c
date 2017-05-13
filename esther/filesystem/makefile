CC=gcc

PROG=filesystem
LIB=../QEPDLibrary
LIBBIN=$(LIB)/build

all: compile

compile: $(LIB)/build
	$(CC) $(PROG).c  -I$(LIB) -L$(LIBBIN) -lcommons -lqepd -Wl,-rpath=$(LIBBIN) -o $(PROG)

$(LIB)/build:
	cd $(LIB); make

debug: CC += -DDEBUG -g
debug: all

clean:
	rm -f filesystem

.PHONY: compile clean
