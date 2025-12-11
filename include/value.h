#ifndef clox_value_h
#define clox_value_h

/*
  Endianness:
  Big Endian - the most significant byte goes first (lowest address)
  Little Endian - the least significant byte goes first (lowest address)

  So, if you have a 4-byte value stored at addresses 0x1000, 0x1001, 0x1002, and 0x1003:

  The lowest address is 0x1000.
  In Big Endian, the most significant byte is stored at 0x1000.
  In Little Endian, the least significant byte is stored at 0x1000.

  Little Endian is much more common in modern systems.

  Endianness only applies to multi-byte values as its dictates the arrangement
  of the bytes themselves.
*/
#include "common.h"
#include "memory.h"
// A 'double' is a double float - 64bit float (8 bytes)
typedef double Value;

/* Dynamic array limited to 65536 indexes. */
typedef struct
{
  u_int16_t capacity;
  u_int16_t count;
  Value *values;
} ValueArray;

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

#endif