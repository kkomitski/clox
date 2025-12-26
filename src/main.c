// <> means its a global lib/bin, available across the system
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// means its a relative/specified path
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "vm.h"

static void repl() {
  char line[1024];
  for (;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(line);
  }
}

/*
We open the file, but before reading it, we seek to the very end using fseek().
Then we call ftell() which tells us how many bytes we are from the start of the
file. Since we sought to the end, that’s the size. We rewind back to the
beginning, allocate a string of that size, and read the whole file in a single
batch.
 */
static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");

  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);     // go to last byte of file
  size_t fileSize = ftell(file); // tell the size
  rewind(file);                  // rewind back to first byte

  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  // [buffer] is the ptr to where it will start reading bytes into
  // [sizeof(char)] is the size of each element/increment
  // [fileSize] number of elements to read
  // [file] file handle to read from
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\n';

  fclose(file);
  return buffer;
}

static void runFile(const char* path) {
  char* source = readFile(path);
  InterpretResult result = interpret(source);
  free(source);

  if (result == INTERPRET_COMPILER_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, char* argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox [path]\n");
    exit(64);
  }

  freeVM();

  // Chunk chunk;
  // Chunk *ptr = &chunk;

  // initChunk(&chunk);

  // for (int i = 0; i < 3000; i++) {
  //   uint8_t constant = addConstant(&chunk, (double)i);
  //   writeChunk(&chunk, OP_CONSTANT, 123);
  //   writeChunk(&chunk, constant, 123);
  // }

  // // If you have OP_POP:
  // for (int i = 0; i < 16; i++) {
  //   writeChunk(&chunk, OP_ADD, 123);
  // }

  // writeChunk(&chunk, OP_NEGATE, 123);
  // writeChunk(&chunk, OP_RETURN, 123);
  // // disassembleChunk(&chunk, "test chunk");

  // freeVM();
  // interpret(&chunk);
  // freeChunk(&chunk);

  return 0;
}

/*
  Chapter 15 Challenges

  (1.2 + 3.4) / 5.6
  [OP_CONSTANT, 1.2]
  [OP_CONSTANT, 3.4]
  [OP_ADD]
  [OP_CONSTANT, 5.6]
  [OP_DIVIDE]

  1 * 2 + 3
  [OP_CONST, 1]
  [OP_CONST, 2]
  [OP_MULTIPLY]
  [OP_CONST, 3]
  [OP_ADD]

  1 + 2 * 3
  [OP_CONST, 1]
  [OP_CONST, 2]
  [OP_CONST, 3]
  [OP_MULTIPLY]
  [OP_ADD]

  3 - 2 - 1
  [OP_CONST, 3]
  [OP_CONST, 2]
  [OP_SUBTRACT]
  [OP_CONST, 1]
  [OP_SUBTRACT]

  1 + 2 * 3 - 4 / -5
  [OP_CONST, 1]
  [OP_CONST, 2]
  [OP_CONST, 3]
  [OP_MULTIPLY]
  [OP_ADD]
  [OP_CONST, 4]
  [OP_CONST, 5]
  [OP_NEGATE]
  [OP_DIVIDE]
  [OP_SUBTRACT]
*/