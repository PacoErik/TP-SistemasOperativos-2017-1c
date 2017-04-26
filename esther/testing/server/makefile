SRC=$(wildcard *.c)
BIN=$(SRC:%.c=Debug/%)

all: $(BIN)

Debug/%: %.c
	mkdir -p Debug
	gcc $< -o $@ -pthread
	
clean:
	rm -rf Debug

.PHONY: clean