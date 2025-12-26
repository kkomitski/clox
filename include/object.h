#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

// Extracts the exact object type
#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
  OBJ_STRING,
} ObjType;

struct Obj {
  ObjType type;
  // Linked list of Objs used for memory deallocation
  struct Obj* next;
};

struct ObjString {
  // Metadata
  struct Obj obj;
  int length;
  uint32_t hash;
  // Value
  char* chars;
};

struct ObjString* takeString(char* chars, int length);
struct ObjString* copyString(const char* chars, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif