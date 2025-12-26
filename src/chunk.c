#include <stdlib.h>

#include "chunk.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  // If the capacity is smaller the the count with one more added
  if (chunk->capacity < chunk->count + 1) {
    // Save the current capacity
    int oldCapacity = chunk->capacity;

    // Calculate a new capacity
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    int newCapacity = chunk->capacity;
    // Allocate new memory, move the existing into the new and free the old
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, newCapacity);
    chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, newCapacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;

  chunk->count++;
}

void freeChunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  freeValueArray(&chunk->constants);
  // We leave the chunk in a well-defined known state (zeroed out)
  initChunk(chunk);
}

/*
Adds a constant into the pool of constants of the passed chunk.

@return -The index of the constant
*/
uint16_t addConstant(Chunk* chunk, Value value) {
  writeValueArray(&chunk->constants, value);
  // The arrow syntax `->` is for accessing struct members through a pointer
  // The dot syntax `.` is for accessing struct members directly from a struct
  // variable
  return chunk->constants.count - 1;

  /*
    The `writeValueArray` function will end up incrementing the count of the
    ValueArray struct internally, so we return count-1 to return the index of
    the item we actually added
  */
}