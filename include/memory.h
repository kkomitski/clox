#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * count)
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

/**
 * This is a macro.
 * This macro will basically replace every occurrence
 * of the keyword `GROW_CAPACITY` in the source code with
 * `((capacity) < 8 ? 8 : (capacity) * 2)`
 *
 * @example
 * ```c
 * chunk->capacity = ((capacity) < 8 ? 8 : (capacity) * 2)
 * ```
 */
#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

#define SHRINK_CAPACITY(capacity) ((capacity) > 8 ? (capacity) / 2 : 8)

/**
 * The allocator doesn't know what type of the new block is so it returns
 * `void*` which basically means a pointer to something unknown
 *
 * The `(uint8_t*)` syntax is type casting the result of the reallocate function
 * (which will be void*) into a uint8_t* type.
 *
 * The `sizeof(uint8_t)` syntax is a built-in C operator which returns the size
 * of a type or variable in bytes. Returns a `size_t` type.
 *
 * (uint8_t *)reallocate(chunk->code, sizeof(uint8_t) * (oldCapacity),
 * sizeof(uint8_t) * (newCapacity));
 *
 *
 * The whole function basically takes the handle to the current array (pointer
 * to the first element) as well as the old count and new count of elements.
 * Uses the old count to free and the new count to reallocate. Finally it
 * returns a void* which gets casted to the provided type
 *
 * *element is the type of data stored in the array - can be primitive or struct
 */
#define GROW_ARRAY(type, pointer, oldCount, newCount)                          \
  (type*)reallocate(pointer, sizeof(type) * (oldCount),                        \
                    sizeof(type) * (newCount))

#define SHRINK_ARRAY(type, pointer, oldCount, newCount)                        \
  (type*)reallocate(pointer, sizeof(type) * (oldCount),                        \
                    sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount)                                    \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

/**
 * The pointer is unknown, hence - void*
 *
 * The `size_t` is an unsigned integer type (returned by sizeof() operator)
 *
 *
 * | oldSize   | newSize                | Operation                  |
 * |-----------|------------------------|----------------------------|
 * | 0         | Non-zero               | Allocate new block         |
 * | Non-zero  | 0                      | Free allocation            |
 * | Non-zero  | Smaller than oldSize   | Shrink existing allocation |
 * | Non-zero  | Larger than oldSize    | Grow existing allocation   |
 */
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// Chase down the obj linked list and free all the memory
void freeObjects();

#endif