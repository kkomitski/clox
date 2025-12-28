#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64 // Max recursion depth
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjFunction* function;
  uint8_t* ip;
  // Points into the VM's value stack at the first slot that this function can
  // use
  Value* slots;
} CallFrame;

// The indexes for this stack can be fit into a single byte
// #define STACK_MAX 256

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;
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