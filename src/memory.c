
#include <stdlib.h>
#include <stdio.h>

#include "memory.h"

void *reallocate(void *pointer, size_t oldSize, size_t newSize)
{
  if (newSize == 0)
  {
    free(pointer);
    return NULL;
  }

  void *result = realloc(pointer, newSize);
  if (result == NULL)
  {
    fprintf(stderr, "Failed to allocate memory: oldSize=%zu, newSize=%zu\n", oldSize, newSize);
    exit(1);
  }

  return result;
}