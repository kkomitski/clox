#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
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

static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = vm.ip - vm.chunk->code - 1;
  int line = vm.chunk->lines[instruction];
  fprintf(stderr, "[line %d] in script\n", line);
  resetStack();
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
  // #ifdef DEBUG_TRACE_EXECUTION
  //   *vm.stackTop = 0;
  // #endif
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

static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

/*
If the value is null it will return true
Else it will check if the value is bool and return it as bool
otherwise it will return false

Basically nil and false are falsey, everything else is truthy
*/
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString *b = AS_STRING(pop());
  ObjString *a = AS_STRING(pop());
  // Calculate the results string based on length of operands
  int length = a->length + b->length;
  // Allocate a char array for the result
  char *chars = ALLOCATE(char, length + 1); // +1 for the null terminator '\0'
  // Copy the first half
  memcpy(chars, a->chars, a->length);
  // Copy the second half
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = takeString(chars, length);
  push(OBJ_VAL(result));
}

void initVM() {
  resetStack();
  vm.objects = NULL;
}

void freeVM() {
  freeObjects();

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
#define BINARY_OP(valueType, op)                                               \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = AS_NUMBER(pop());                                               \
    double a = AS_NUMBER(pop());                                               \
    push(valueType(a op b));                                                   \
  } while (false)
#ifdef DEBUG_TRACE_EXECUTION

  disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
  uint8_t instruction;
  // clang-format off
  switch (instruction = READ_BYTE()) {
  case OP_CONSTANT: { 
    Value constant = READ_CONSTANT(); push(constant); break; 
  }
  case OP_NIL: push(NIL_VAL); break;
  case OP_TRUE: push(BOOL_VAL(true)); break;
  case OP_FALSE: push(BOOL_VAL(false)); break;
  case OP_EQUAL: {
    Value b = pop();
    Value a = pop();

    push(BOOL_VAL(valuesEqual(a, b)));
    break;
  }
  case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
  case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
  case OP_ADD: {
    if(IS_STRING(peek(0)) && IS_STRING(peek(1))) {
      concatenate();
    }
    else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
      double b = AS_NUMBER(pop());
      double a = AS_NUMBER(pop());

      push(NUMBER_VAL(a + b));
    }
    else {
      runtimeError("Operands must be two numbers or two strings.");
      return INTERPRET_RUNTIME_ERROR;
    }
    break;
  }; 
  case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
  case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
  case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
  case OP_NOT: push(BOOL_VAL(isFalsey(pop())));
  case OP_NEGATE: {
    if (!IS_NUMBER(peek(0))) {
      runtimeError("Operand must be a number.");
      return INTERPRET_RUNTIME_ERROR;
    }

    push(NUMBER_VAL(AS_NUMBER(pop())));
    break;
  }
  case OP_RETURN: {
    printf("Returning - ");
    printValue(pop());
    printf("\n");
    return INTERPRET_OK;
  }
  }

  return INTERPRET_RUNTIME_ERROR;
  // clang-format on
}

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP

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