// Sub-module (includes the common)
#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "memory.h"
#include "value.h"

/*
  Each operation has an // *instruction format* \\
  This specifies the memory layout of the incoming instruction
  and how many bytes it uses so that the assembler can know how many
  bytes to write and the disassembler - how many to read.

  A OP_RETURN will have an instruction format of a single byte,
  which only represents the opcode
  [01] - Return opcode

  An OP_CONSTANT will have 2 bytes
  [01] - The opcode
  [01] - The index of the constant in the constants array (0-255)

  An OP_CONSTANT_LONG will have 3 bytes
  [01] - The opcode
  [00][01] - The index to constant but (0-65,535)
*/
typedef enum
{
  /*
  Push a constant value from the constants array onto the stack
  This will be 2 parts:
  - [1 byte] for the opcode (0-255)
  - [1 byte] for the index of the constants (0-255)
  */
  OP_CONSTANT, // 00
  /*
  This will be 2 parts:
  - [1 byte] for the opcode (0-255)
  - [2 bytes] for the index of the constants (0-65,536)
  */
  // OP_CONSTANT_LONG, // 00
  /*
  This will be 2 parts:
  - [1 byte] for the opcode (0-255)
  - [4 bytes] for the index of the constants (0-4,294,967,295)
  */
  // OP_CONSTANT_LONG_LONG, // 02

  // Return from the current function or exit the VM (close the stack frame)
  OP_RETURN, // 01
} OpCode;

/*
This is a dynamic array holding the chunk of bytecode

A single chunk of bytecode can have multiple bytecode operations and generally corresponds
to a function, method or script.

It will hold the bytecode instructions in an array block of memory
Where each instruction will have its own format (and hence a different size).
See OpCode enum for instruction formats.

It will also hold an embedded struct which holds the literals as constants
*/
typedef struct
{
  int count;            // 32 bits (4 bytes)
  int capacity;         // 32 bits (4 bytes)
  uint8_t *code;        // 64 bits (8 bytes) on a 64-bit system
  int *lines;           // 32 bits (4 bytes)
  ValueArray constants; // Embedded struct, typically 128 bits (16 bytes: 2 ints + pointer)
} Chunk;                // Total: 288 bits (36 bytes) on a 64-bit system

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
void freeChunk(Chunk *chunk);

int addConstant(Chunk *chunk, Value value);

#endif

/*

address =     12 13 14 15 16 17
uint8_t foo = {0, 0, 0, 0, 0, 0};

in this scenario foo holds the value of "12", because its just a pointer
to the first element in the array

if we pass this array to something for processing, we are just passing the handle

once processing starts we start incrementing foo, which will be 13, 14, 15 etc

*/