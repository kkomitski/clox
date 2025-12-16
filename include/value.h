#ifndef clox_value_h
#define clox_value_h

/*
  Endianness:
  Big Endian - the most significant byte goes first (lowest address)
  Little Endian - the least significant byte goes first (lowest address)

  So, if you have a 4-byte value stored at addresses 0x1000, 0x1001, 0x1002, and
  0x1003:

  The lowest address is 0x1000.
  In Big Endian, the most significant byte is stored at 0x1000.
  In Little Endian, the least significant byte is stored at 0x1000.

  Little Endian is much more common in modern systems.

  Endianness only applies to multi-byte values as its dictates the arrangement
  of the bytes themselves.
*/
#include "common.h"
#include "memory.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
} ValueType;

// ValueType is enum which is inherently 32bit
typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number; // 64 bit / 8 byte
    Obj *obj;
  } as;
} Value; // 128 bits/16 bytes

// +----------------+-----------------+----------------+---------------+
// |   type (4B)    |   padding (4B)  |  union as (8B: double/boolean) |
// +----------------+-----------------+----------------+---------------+

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj *)object}})

/* Dynamic array limited to 65536 indexes. */
typedef struct {
  u_int16_t capacity;
  u_int16_t count;
  Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

#endif