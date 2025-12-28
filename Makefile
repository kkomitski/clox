# Default variable
COMPILER=clang
INPUTS = \
  ./src/chunk.c \
  ./src/main.c \
  ./src/memory.c \
  ./src/debug.c \
  ./src/value.c \
  ./src/vm.c \
  ./src/compiler.c \
  ./src/scanner.c \
  ./src/object.c \
  ./src/table.c

# -g flag allows for metadata for debugging
# -I flag specifies the include directory
.PHONY: build
build:
	mkdir -p ./dist
	$(COMPILER) -I./include $(INPUTS) -o ./dist/main

.PHONY: debug
debug:
	mkdir -p ./dist
	$(COMPILER) -g -I./include $(INPUTS) -o ./dist/main

.PHONY: run
run: build
	./dist/main $(ARGS)

.PHONY: clean
clean:
	rm -rf ./dist
