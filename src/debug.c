#include <stdio.h>

#include "debug.h"
#include "chunk.h"
#include "object.h"
#include "value.h"

static void printStackColumn(Value* stack, Value* stackTop) {
  printf(" [");
  if (stack == stackTop) {
    printf("]");
    return;
  }
  
  for (Value* slot = stack; slot < stackTop; slot++) {
    if (slot != stack) printf(", ");
    printValue(*slot);
  }
  printf("]");
}

static void printValueColumn(Value value) {
  char buffer[32];
  
  if (IS_NUMBER(value)) {
    snprintf(buffer, sizeof(buffer), "%.14g", AS_NUMBER(value));
  } else if (IS_BOOL(value)) {
    snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    snprintf(buffer, sizeof(buffer), "nil");
  } else if (IS_OBJ(value)) {
    if (IS_STRING(value)) {
      ObjString* str = AS_STRING(value);
      snprintf(buffer, sizeof(buffer), "\"%.*s\"", 
                     (int)(sizeof(buffer) - 3 < str->length ? sizeof(buffer) - 3 : str->length), 
                     str->chars);
    } else if (IS_FUNCTION(value)) {
      ObjFunction* fn = AS_FUNCTION(value);
      if (fn->name == NULL) {
        snprintf(buffer, sizeof(buffer), "<script>");
      } else {
        snprintf(buffer, sizeof(buffer), "<fn %s>", fn->name->chars);
      }
    } else {
      snprintf(buffer, sizeof(buffer), "<obj>");
    }
  } else {
    snprintf(buffer, sizeof(buffer), "?");
  }
  
  printf("%-14s", buffer);
}

void disassembleChunk(Chunk* chunk, const char* name) {
  printf("CHUNK == %s ==\n", name);
  printf("SIZE | OFFSET | LINE | INSTRUCTION        | OPERAND | VALUE\n");
  printf("-----|-------|------|--------------------|---------|-----------\n");

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset, NULL, NULL);
  }
}

// The `static` keyword prevents external linkage - so the fn is
// only available inside this file
static int simpleInstruction(const char* name, int offset, Value* stack, Value* stackTop) {
  printf("%-18s |         | %-14s |", name, "");
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  // Because the `OP_RETURN` is just the opcode
  return offset + 1;
}

static int returnInstruction(const char* name, int offset, Value* stack, Value* stackTop) {
  printf("%-18s |         | ", name);
  // Show the value being returned (top of stack)
  if (stack != NULL && stackTop > stack) {
    printValueColumn(stackTop[-1]);
  } else {
    printf("%-14s", "");
  }
  printf(" |");
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-18s | %7d | %-14s |", name, slot, "");
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  return offset + 2; 
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04d -> %04d", offset, offset + 3 + sign * jump);
  printf("%-18s | %7s | %-14s |", name, "", buffer);
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-18s | %7d | ", name, constant);
  
  // Defensive check: ensure constant index is valid
  if (chunk == NULL || chunk->constants.values == NULL || constant >= chunk->constants.count) {
    printf("INVALID CONST  |");
  } else {
    printValueColumn(chunk->constants.values[constant]);
    printf(" |");
  }
  
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  // Because the `OP_CONSTANT` is actually 2 bytes, one for the opcode, one for
  // the index of the constant
  return offset + 2;
}

static int constantLongInstruction(const char* name, Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  uint16_t constant = (uint16_t)(chunk->code[offset + 1] << 8);
  constant |= chunk->code[offset + 2];
  printf("%-18s | %7u | ", name, constant);
  
  // Defensive check: ensure constant index is valid
  if (chunk == NULL || chunk->constants.values == NULL || constant >= chunk->constants.count) {
    printf("INVALID CONST  |");
  } else {
    printValueColumn(chunk->constants.values[constant]);
    printf(" |");
  }
  
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  return offset + 3;
}

static int closureInstruction(const char* name, Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  offset++;
  uint8_t constant = chunk->code[offset++];
  printf("%-18s | %7d | ", name, constant);
  
  // Defensive check: ensure constant index is valid
  if (chunk == NULL || chunk->constants.values == NULL || constant >= chunk->constants.count) {
    printf("INVALID CONST  |");
  } else {
    printValueColumn(chunk->constants.values[constant]);
    printf(" |");
  }
  
  if (stack != NULL) {
    printStackColumn(stack, stackTop);
  }
  printf("\n");
  
  ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
  for (int j = 0; j < function->upvalueCount; j++) {
    int isLocal = chunk->code[offset++];
    int index = chunk->code[offset++];
    printf("%04d      |                     %s %d\n",
           offset - 2, isLocal ? "local" : "upvalue", index);
  }
  
  return offset;
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
  
  Pass NULL for stack and stackTop to omit stack visualization.
*/
int disassembleInstruction(Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  uint8_t instruction = chunk->code[offset];
  int size = 1; // default size for simple instructions
  switch (instruction) {
    case OP_CLOSURE:
    case OP_CONSTANT:
    case OP_DEFINE_GLOBAL:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
      size = 2;
      break;
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_CALL:
      size = 2;
      break;
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_LOOP:
      size = 3;
      break;
    default:
      size = 1;
      break;
  }

  // Print size and offset
  printf("%4d | %04d   | ", size, offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   - | ");
  } else {
    printf("%4d | ", chunk->lines[offset]);
  }

  switch (instruction) {
  case OP_CONSTANT:
    return constantInstruction("OP_CONSTANT", chunk, offset, stack, stackTop);
  case OP_NEGATE:
    return simpleInstruction("OP_NEGATE", offset, stack, stackTop);
  case OP_ADD:
    return simpleInstruction("OP_ADD", offset, stack, stackTop);
  case OP_SUBTRACT:
    return simpleInstruction("OP_SUBTRACT", offset, stack, stackTop);
  case OP_MULTIPLY:
    return simpleInstruction("OP_MULTIPLY", offset, stack, stackTop);
  case OP_DIVIDE:
    return simpleInstruction("OP_DIVIDE", offset, stack, stackTop);
  case OP_RETURN:
    return returnInstruction("OP_RETURN", offset, stack, stackTop);
  case OP_CLOSE_UPVALUE:
    return returnInstruction("OP_CLOSE_UPVALUE", offset, stack, stackTop);
  case OP_PRINT:
    return simpleInstruction("OP_PRINT", offset, stack, stackTop);
  case OP_NIL:
    return simpleInstruction("OP_NIL", offset, stack, stackTop);
  case OP_FALSE:
    return simpleInstruction("OP_FALSE", offset, stack, stackTop);
  case OP_POP:
    return simpleInstruction("OP_POP", offset, stack, stackTop);
  case OP_GET_LOCAL:
    return byteInstruction("OP_GET_LOCAL", chunk, offset, stack, stackTop);
  case OP_SET_LOCAL:
    return byteInstruction("OP_SET_LOCAL", chunk, offset, stack, stackTop);
  case OP_DEFINE_GLOBAL:
    return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset, stack, stackTop);
  case OP_GET_GLOBAL:
    return constantInstruction("OP_GET_GLOBAL", chunk, offset, stack, stackTop);
  case OP_SET_GLOBAL:
    return constantInstruction("OP_SET_GLOBAL", chunk, offset, stack, stackTop);
  case OP_NOT:
    return simpleInstruction("OP_NOT", offset, stack, stackTop);
  case OP_TRUE:
    return simpleInstruction("OP_TRUE", offset, stack, stackTop);
  case OP_EQUAL:
    return simpleInstruction("OP_EQUAL", offset, stack, stackTop);
  case OP_GREATER:
    return simpleInstruction("OP_GREATER", offset, stack, stackTop);
  case OP_LESS:
    return simpleInstruction("OP_LESS", offset, stack, stackTop);
  case OP_JUMP:
    return jumpInstruction("OP_JUMP", 1, chunk, offset, stack, stackTop);
  case OP_JUMP_IF_FALSE:
    return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset, stack, stackTop);
  case OP_CALL:
    return byteInstruction("OP_CALL", chunk, offset, stack, stackTop);
  case OP_LOOP:
    return jumpInstruction("OP_LOOP", -1, chunk, offset, stack, stackTop);    
  case OP_CLOSURE:
    return closureInstruction("OP_CLOSURE", chunk, offset, stack, stackTop);
  case OP_GET_UPVALUE:
    return byteInstruction("OP_GET_UPVALUE", chunk, offset, stack, stackTop);
  case OP_SET_UPVALUE:
    return byteInstruction("OP_SET_UPVALUE", chunk, offset, stack, stackTop);

  default:
    printf("%-18s | %7d | %-14s |", "Unknown opcode", (int)instruction, "");
    if (stack != NULL) {
      printStackColumn(stack, stackTop);
    }
    printf("\n");
    return offset + 1;
  }
}

void disassembleInstructionWithStack(Chunk* chunk, int offset, Value* stack, Value* stackTop) {
  disassembleInstruction(chunk, offset, stack, stackTop);
}