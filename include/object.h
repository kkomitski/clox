#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

// Extracts the exact object type
#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSE)

#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
} ObjType;

struct Obj {
  ObjType type;
  // Linked list of Objs used for memory deallocation
  struct Obj* next;
};

// Function Objects have their own chunk for the body
typedef struct {
  Obj obj;
  int arity; // "arity" is the number of parameters a func takes
  int upvalueCount;
  Chunk chunk;
  ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);
typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

struct ObjString {
  // Metadata
  struct Obj obj;
  int length;
  uint32_t hash;
  // Value
  char* chars;
};

typedef struct ObjUpvalue {
  Obj obj;
  Value* location;
  struct ObjUpvalue* next;
  Value closed;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  // This is only a double pointer because it points to an array 
  // and an array is just a pointer to the first element
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;

ObjClosure* newClosure(ObjFunction* function);

struct ObjString* takeString(char* chars, int length);
struct ObjString* copyString(const char* chars, int length);

ObjFunction* newFunction();
ObjNative* newNative(NativeFn function);
ObjUpvalue* newUpvalue(Value* slot);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif