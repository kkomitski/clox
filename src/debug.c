#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk *chunk, const char *name)
{
  printf("== %s ==\n", name);
  printf("OFFSET | LINE | INSTRUCTION        | OPERAND | VALUE\n");
  printf("-------|------|--------------------|---------|-----------\n");

  for (int offset = 0; offset < chunk->count;)
  {
    offset = disassembleInstruction(chunk, offset);
  }
}

// The `static` keyword prevents external linkage - so the fn is
// only available inside this file
static int simpleInstruction(const char *name, int offset)
{
  printf("%-18s |         |\n", name);
  // Because the `OP_RETURN` is just the opcode
  return offset + 1;
}

static int constantInstruction(const char *name, Chunk *chunk, int offset)
{
  uint8_t constant = chunk->code[offset + 1];
  printf("%-18s | %7d | ", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("\n");
  // Because the `OP_CONSTANT` is actually 2 bytes, one for the opcode, one for the index of the constant
  return offset + 2;
}

int disassembleInstruction(Chunk *chunk, int offset)
{
  // %d - decimal format
  // 0 - pad with zeroes
  // 4 - up to four digits
  printf("%04d   | ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
  {
    printf("   - | ");
  }
  else
  {
    printf("%4d | ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];

  switch (instruction)
  {
  case OP_RETURN:
    return simpleInstruction("OP_RETURN", offset);
  case OP_CONSTANT:
    return constantInstruction("OP_CONSTANT", chunk, offset);
  default:
    printf("Unknown opcode %d\n", instruction);
    return offset + 1;
  }
}