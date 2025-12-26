#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "value.h"
#include "table.h"

#define STACK_MAX 2048

// The indexes for this stack can be fit into a single byte
// #define STACK_MAX 256

typedef struct {
  Chunk* chunk; // 40 bytes
  /*
  Instruction Position
  this will be an actual pointer, pointed directly at the position

  [OP_CONST, 3, OP_ADD, OP_RETURN] - it will skip the 3 here, and always point
  to the OPs
  */
  uint8_t* ip; // 8 bytes
  int stackCapacity;
  Value* stack; // 8 bytes * 256 = 2048 bytes (2.048kb)
  /*
  The stack top is just a pointer to the next box in the stack,
  we just add the values directly into the box and then we increment the pointer
  */
  Value* stackTop; // 8 bytes
  Table globals;
  Table strings;
  Obj* objects;
} VM; // 2072 bytes (2.072kb)

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILER_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

// Expose the global vm variable
extern VM vm;

void initVM();
void freeVM();
// By saying "const" we prevent anything from manipulating the source downstream
InterpretResult interpret(const char* source);

// Stack methods
void push(Value value);
Value pop();

#endif