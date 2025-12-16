#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"
#include "value.h"

void initValueArray(ValueArray *array) {
  array->count = 0;
  array->capacity = 0;
  array->values = NULL;
}

void writeValueArray(ValueArray *array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    int newCapacity = GROW_CAPACITY(oldCapacity); // Double the capacity

    if (newCapacity > UINT16_MAX) {
      newCapacity = UINT16_MAX;

      if (array->count + 1 > UINT16_MAX) {
        fprintf(stderr, "ValueArray capacity exceeded 16-bit limit \n");
        exit(EXIT_FAILURE);
      }
    }

    array->capacity = newCapacity;
    array->values = GROW_ARRAY(Value, array->values, oldCapacity, newCapacity);
  }
  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray *array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

void printValue(Value value) {
  // %g flag is for shortest possible representation of a float
  switch (value.type) {
  case VAL_BOOL:
    printf(AS_BOOL(value) ? "true" : "false");
    break;
  case VAL_NIL:
    printf("nil");
    break;
  case VAL_NUMBER:
    printf("%g", AS_NUMBER(value));
    break;
  case VAL_OBJ:
    printObject(value);
    break;
  }
}

// clang-format off
bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;

  switch(a.type) {
    case VAL_NIL: return true; // nil == nil = true
    case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
      ObjString *aString = AS_STRING(a);
      ObjString *bString = AS_STRING(b);

      return aString->length == bString->length && memcmp(aString->chars, bString->chars, aString->length) == 0;
    }
    default: return false; // unreachable
  }
}
// clang-format on