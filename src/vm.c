#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
  /*
  Because in C, the value an array gives you is just a pointer into
  its first element, by setting the stackTop to the stack itself
  it will set the stackTop to the pointer of the first element in the
  stack array
  */
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }
  resetStack();
}

static void defineNative(const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
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

    Value* oldStack = vm.stack;
    vm.stackCapacity = newCapacity;
    vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, newCapacity);

    // Update stackTop to point to the new stack
    vm.stackTop = vm.stack + stackIdx;

    // Update all frame slots pointers to point to the new stack
    for (int i = 0; i < vm.frameCount; i++) {
      vm.frames[i].slots = vm.stack + (vm.frames[i].slots - oldStack);
    }
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

    Value* oldStack = vm.stack;
    vm.stackCapacity = newCapacity;
    vm.stack = SHRINK_ARRAY(Value, vm.stack, oldCapacity, newCapacity);
    vm.stackTop = vm.stack + stackIdx;

    // Update all frame slots pointers to point to the new stack
    for (int i = 0; i < vm.frameCount; i++) {
      vm.frames[i].slots = vm.stack + (vm.frames[i].slots - oldStack);
    }
  }

  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

static bool call(ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d", function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case OBJ_FUNCTION:
      return call(AS_FUNCTION(callee), argCount);
    case OBJ_NATIVE: {
      NativeFn native = AS_NATIVE(callee);
      Value result = native(argCount, vm.stackTop - argCount);
      vm.stackTop -= argCount + 1;
      push(result);
      return true;
    }
    default:
      break; // Non-callable object
    }
  }

  runtimeError("Can only call functions and classes.");
  return false;
}

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
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());
  // Calculate the results string based on length of operands
  int length = a->length + b->length;
  // Allocate a char array for the result
  char* chars = ALLOCATE(char, length + 1); // +1 for the null terminator '\0'
  // Copy the first half
  memcpy(chars, a->chars, a->length);
  // Copy the second half
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  push(OBJ_VAL(result));
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.strings);
  initTable(&vm.globals);

  defineNative("clock", clockNative);
}

void freeVM() {
  freeObjects();
  freeTable(&vm.strings);
  freeTable(&vm.globals);
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
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG()                                                   \
  (frame->function->chunk.constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())
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
  printf("SIZE | OFFSET | LINE | INSTRUCTION        | OPERAND | VALUE          "
         "| STACK\n");
  printf("-----|--------|------|--------------------|---------|----------------"
         "|-----------\n");

#endif

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    disassembleInstructionWithStack(
        &frame->function->chunk, (int)(frame->ip - frame->function->chunk.code),
        vm.stack, vm.stackTop);
#endif
    uint8_t instruction;
    // clang-format off
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_POP: pop(); break;
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if(!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }

        push(value);
        break;
      }
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if(tableSet(&vm.globals, name, peek(0))) {
          tableDelete(&vm.globals, name);
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }
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
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }

        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_PRINT: {
        printf("[OP_PRINT] ");
        printValue(pop());
        printf("\n");
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if(isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        if(!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount-1];
        break;
      }
      case OP_RETURN: {
        // Pop the return value from the top of the stack
        Value result = pop();
        // Discard the completed call frame
        vm.frameCount--;
        
        // If we're back to the top-level (frameCount == 0), we're exiting the script
        if(vm.frameCount == 0) {
          pop(); // Pop the implicit script function from slot 0
          return INTERPRET_OK; // Finish the program
        }

        // Otherwise, we're returning from a function call
        // Discard all the slots used by the callee (including the function itself and arguments)
        // by resetting stackTop to where the current frame started (frame->slots points to slot 0 of the callee)
        vm.stackTop = frame->slots;
        // Push the return value onto the stack (replacing where the function was)
        push(result);
        // Update the frame pointer to the previous (caller's) frame
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
    }
    // clang-format on
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
#undef READ_STRING
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILER_ERROR;

  push(OBJ_VAL(function));
  call(function, 0); // Call the implicit function
  // CallFrame* frame = &vm.frames[vm.frameCount++];
  // frame->function = function;
  // frame->ip = function->chunk.code;
  // frame->slots = vm.stack;

  return run();
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