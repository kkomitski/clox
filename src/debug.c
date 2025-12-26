#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
  printf("CHUNK == %s ==\n", name);
  printf("OFFSET | LINE | INSTRUCTION        | OPERAND | VALUE\n");
  printf("-------|------|--------------------|---------|-----------\n");

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

// The `static` keyword prevents external linkage - so the fn is
// only available inside this file
static int simpleInstruction(const char* name, int offset) {
  printf("%-18s |         |\n", name);
  // Because the `OP_RETURN` is just the opcode
  return offset + 1;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-18s | %7d | ", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("\n");
  // Because the `OP_CONSTANT` is actually 2 bytes, one for the opcode, one for
  // the index of the constant
  return offset + 2;
}

/*
  Prints the current instruction's offset and source line number in a formatted
  way.

  - The offset is printed as a zero-padded 4-digit decimal (e.g., 0003).
  - If the current instruction is on the same source line as the previous
  instruction, a dash ("   - | ") is printed to indicate the line hasn't
  changed.
  - Otherwise, the actual line number is printed, right-aligned to 4 spaces.

  This helps visually group bytecode instructions by their original source lines
  when disassembling a chunk.
*/
int disassembleInstruction(Chunk* chunk, int offset) {
  // %d - decimal format
  // 0 - pad with zeroes
  // 4 - up to four digits
  printf("%04d   | ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   - | ");
  } else {
    printf("%4d | ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];

  switch (instruction) {
  case OP_CONSTANT:
    return constantInstruction("OP_CONSTANT", chunk, offset);
  case OP_NEGATE:
    return simpleInstruction("OP_NEGATE", offset);
  case OP_ADD:
    return simpleInstruction("OP_ADD", offset);
  case OP_SUBTRACT:
    return simpleInstruction("OP_SUBTRACT", offset);
  case OP_MULTIPLY:
    return simpleInstruction("OP_MULTIPLY", offset);
  case OP_DIVIDE:
    return simpleInstruction("OP_DIVIDE", offset);
  case OP_RETURN:
    return simpleInstruction("OP_RETURN", offset);
  case OP_PRINT:
    return simpleInstruction("OP_PRINT", offset);
  case OP_NIL:
    return simpleInstruction("OP_NIL", offset);
  case OP_FALSE:
    return simpleInstruction("OP_FALSE", offset);
  case OP_POP:
    return simpleInstruction("OP_POP", offset);
  case OP_DEFINE_GLOBAL:
    return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
  case OP_GET_GLOBAL:
    return constantInstruction("OP_GET_GLOBAL", chunk, offset);
  case OP_SET_GLOBAL:
    return constantInstruction("OP_SET_GLOBAL", chunk, offset);
  case OP_NOT:
    return simpleInstruction("OP_NOT", offset);
  case OP_TRUE:
    return simpleInstruction("OP_TRUE", offset);
  case OP_EQUAL:
    return simpleInstruction("OP_EQUAL", offset);
  case OP_GREATER:
    return simpleInstruction("OP_GREATER", offset);
  case OP_LESS:
    return simpleInstruction("OP_LESS", offset);

  default:
    printf("Unknown opcode %d\n", instruction);
    return offset + 1;
  }
}