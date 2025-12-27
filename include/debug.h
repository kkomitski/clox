#ifndef clox_debug_h
#define clox_debug_h

#include "chunk.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset, Value* stack, Value* stackTop);
void disassembleInstructionWithStack(Chunk* chunk, int offset, Value* stack, Value* stackTop);

#endif