PROG=cpu
LIB=../QEPDLibrary
LIBBIN=$(LIB)/build

all: compile

compile: $(LIB)/build
	gcc $(PROG).c  -I$(LIB) -L$(LIBBIN) -lcommons -lqepd -lparser-ansisop -Wl,-rpath=$(LIBBIN) -o $(PROG)

$(LIB)/build:
	cd $(LIB); make

clean:
	rm -f cpu

.PHONY: compile clean
