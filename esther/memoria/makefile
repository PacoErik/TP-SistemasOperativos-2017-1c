CC=gcc
MAKE=make

LIBS = -lcommons -lqepd -lpthread
LIBPATH=../QEPDLibrary
LIBBINPATH=$(LIBPATH)/build

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

all: memoria

memoria: $(SRCS) $(LIBPATH)/build/libqepd.so
	$(CC) -o $@ $^ -I$(LIBPATH) -L$(LIBBINPATH) $(LIBS) -Wl,-rpath=$(LIBBINPATH)
	
$(LIBPATH)/build/libqepd.so:
	cd $(LIBPATH); $(MAKE)

debug: CC += -DDEBUG -g
debug: MAKE += debug
debug: all

clean:
	rm -f memoria
	rm -f memoria_*.log
	rm -f dump_*.txt

.PHONY: compile clean
