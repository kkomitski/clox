// <> means its a global lib/bin, available across the system
#include <stdio.h>
// means its a relative/specified path
#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, char *argv[])
{
  Chunk chunk;
  Chunk *ptr = &chunk;

  initChunk(&chunk);

  uint8_t constant = addConstant(&chunk, 1.2);
  uint8_t constant2 = addConstant(&chunk, 1.3);
  uint8_t constant3 = addConstant(&chunk, 1.4);
  writeChunk(&chunk, OP_RETURN, 123);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, OP_RETURN, 123);
  writeChunk(&chunk, constant, 123);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant2, 123);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant3, 123);
  writeChunk(&chunk, OP_RETURN, 123);
  // 0x0000000100599e60
  disassembleChunk(&chunk, "test chunk");
  freeChunk(&chunk);

  // Add();
  return 0;
}