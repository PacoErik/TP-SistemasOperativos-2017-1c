CC=gcc
MAKE=make

PROG=filesystem
LIB=../QEPDLibrary
LIBBIN=$(LIB)/build

BITMAP=mnt/SADICA_FS/Metadata/Bitmap.bin

all: compile

compile: $(LIB)/build
	$(CC) $(PROG).c -I$(LIB) -L$(LIBBIN) -lcommons -lqepd -Wl,-rpath=$(LIBBIN) -o $(PROG)

$(LIB)/build:
	cd $(LIB); $(MAKE)

debug: CC += -DDEBUG -g
debug: MAKE += debug
debug: all

clean:
	rm -f $(PROG)
	rm -f $(PROG)_*.log
	rm -rf mnt/SADICA_FS/Archivos/*
	rm -rf mnt/SADICA_FS/Bloques/*
	dd if=/dev/zero of="$(BITMAP)" count=1 bs=$$(stat -c%s "$(BITMAP)")

.PHONY: compile clean
