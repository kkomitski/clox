#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() {
  /*
  Because in C, the value an array gives you is just a pointer into
  its first element, by setting the stackTop to the stack itself
  it will set the stackTop to the pointer of the first element in the
  stack array
  */
  vm.stackTop = vm.stack;
}

void push(Value value) {
  /*
  Before:
    stack:    [a][b][c][ ][ ][ ]
                        ^
                    stackTop

  push(d):

  After:
    stack:    [a][b][c][d][ ][ ]
                           ^
                       stackTop

  The value is written at the current stackTop position,
  then stackTop is incremented to point to the next empty slot.
*/

  if (((vm.stackTop - vm.stack) + 1) > STACK_MAX) {
    fprintf(stderr, "Fatal error: Stack overflow\n");
    exit(EXIT_FAILURE);
  }

  if (vm.stackCapacity < (vm.stackTop - vm.stack) + 1) {
    int oldCapacity = vm.stackCapacity;
    int newCapacity = GROW_CAPACITY(oldCapacity);
    int stackIdx = vm.stackTop - vm.stack; // Count of currently utilized slots

    vm.stackCapacity = newCapacity;
    vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, newCapacity);
    // Assign stackTop to the currently utilized slots
    vm.stackTop = vm.stack + stackIdx;
  }

  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
#ifdef DEBUG_TRACE_EXECUTION
  *vm.stackTop = 0;
#endif
  if ((vm.stackTop - vm.stack) < vm.stackCapacity / 2) {
    int oldCapacity = vm.stackCapacity;
    int newCapacity = SHRINK_CAPACITY(oldCapacity);
    int stackIdx = vm.stackTop - vm.stack; // Count of currently utilized slots

    vm.stackCapacity = newCapacity;
    vm.stack = SHRINK_ARRAY(Value, vm.stack, oldCapacity, newCapacity);
    vm.stackTop = vm.stack + stackIdx;
  }

  vm.stackTop--;
  return *vm.stackTop;
}

void initVM() { resetStack(); }

void freeVM() {
  FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
  resetStack();
  vm.stack = NULL;
  vm.stackTop = NULL;
  vm.stackCapacity = 0;
}

/*
When the interpreter executes a user’s program.
It will spend something like 90% of its time inside run().
It is the beating heart of the VM.
*/
static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op)                                                          \
  do {                                                                         \
    double b = pop();                                                          \
    double a = pop();                                                          \
    push(a op b);                                                              \
  } while (false)
  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    // Stack can be seen in debugger, printing is not really needed
    // printf("          ");
    // // start at the beginning of the stack, keep going while not at the stack
    // top for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
    // {
    //   printf("[ ");
    //   printValue(*slot);
    //   printf(" ]");
    // }
    // printf("\n");

    /*
        Since disassembleInstruction() takes an integer byte offset and
        we store the current instruction reference as a direct pointer,
        we first do a little pointer math to convert ip back to a relative
       offset from the beginning of the bytecode. Then we disassemble the
       instruction that begins at that byte.

        (pointer arithmetic) subtracting the

         Visualisation
         Memory layout:

           +-------------------+-------------------+-------------------+
           |   code[0]         |   code[1]         |   code[2]         |
           +-------------------+-------------------+-------------------+
           ^                   ^                   ^
           |                   |                   |
          chunk->code        chunk->code+1       chunk->code+2

         Suppose:
           vm.ip = chunk->code + 2;

         To get the offset:
           offset = vm.ip - chunk->code; // == 2

           +-------------------+-------------------+-------------------+
           |   code[0]         |   code[1]         |   code[2]         |
           +-------------------+-------------------+-------------------+
                                                   ^
                                                   |
                                                 vm.ip

         So, disassembleInstruction(chunk, offset) will disassemble the
       instruction at code[2].
    */
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      /*
      When we hit a `OP_CONSTANT` code, we read the constant and push it on the
      stack
      */
    case OP_CONSTANT: {
      Value constant = READ_CONSTANT();
      push(constant);
      break;
    }
    case OP_ADD:
      BINARY_OP(+);
      break;
    case OP_SUBTRACT:
      BINARY_OP(-);
      break;
    case OP_MULTIPLY:
      BINARY_OP(*);
      break;
    case OP_DIVIDE:
      BINARY_OP(/);
      break;
    case OP_NEGATE: {
      push(-pop());
      break;
    }
    case OP_RETURN: {
      printf("Returning - ");
      printValue(pop());
      printf("\n");
      return INTERPRET_OK;
    }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
  // Start empty chunk
  Chunk chunk;
  initChunk(&chunk);

  // Compile to bytecode and fill up the chunk
  if (!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILER_ERROR;
  }

  // If compilation is successful - pass the chunk the the VM for interpretation
  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;

  InterpretResult result = run();

  freeChunk(&chunk);
  return result;

  // compile(source);
  // return INTERPRET_OK;
};