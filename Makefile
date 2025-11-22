# Default variable
COMPILER=clang

.PHONY: build
build:
# -g flag allows for metadata for debugging
# -I flag specifies the include directory
	mkdir -p ./dist
	$(COMPILER) -g -I./include ./src/chunk.c ./src/main.c ./src/memory.c ./src/debug.c ./src/value.c -o ./dist/main

.PHONY: run
run: build
	./dist/main
