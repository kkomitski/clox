#include <stdlib.h>
#include <stdio.h>

#include "value.h"

void initValueArray(ValueArray *array)
{
  array->count = 0;
  array->capacity = 0;
  array->values = NULL;
}

void writeValueArray(ValueArray *array, Value value)
{
  if (array->capacity < array->count + 1)
  {
    int oldCapacity = array->capacity;
    int newCapacity = GROW_CAPACITY(oldCapacity); // Double the capacity

    array->capacity = newCapacity;
    array->values = GROW_ARRAY(Value, array->values, oldCapacity, newCapacity);
  }
  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray *array)
{
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

void printValue(Value value)
{
  // %g flag is for shortest possible representation of a float
  printf("%g", value);
}