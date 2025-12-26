#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  // Use modulo to map the hash to an index in the array
  uint32_t index = key->hash % capacity;
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];

    // Compare the memory addresses (due to interning there should only ever be
    // a single instance of every string) IF they are the same - we've found it
    // and return it IF its NULL its an empty slot - ready to be used
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // Empty entry
        return tombstone != NULL ? tombstone : entry;
      } else {
        // Save the first found entry to the tombstone
        if (tombstone == NULL) tombstone = entry;
      }

    } else if (entry->key == key) {
      return entry;
    }

    // If we haven't found the key, but its also NULL - its a collision
    // So we start probing forwards
    // modulo by the capacity to ensure we wrap around
    index = (index + 1) % capacity;
  }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

static void adjustCapacity(Table* table, int capacity) {
  // Allocate a new array and wipe all its memory
  Entry* entries = ALLOCATE(Entry, capacity);
  table->count = 0; // reset the tombstones

  // Zero-out all the new memory
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  // Loop over the old table
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    // Skip the NULLs as the new array is already filled with NULLs
    if (entry->key == NULL) continue;

    // If that hash slot is NULL it will return it as NULL so we just fill it in
    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  // Free the old pointer in the table
  FREE_ARRAY(Entry, table->entries, table->capacity);
  // Point the table to the new memory
  table->entries = entries;
  table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value) {
  // We grow the array before then, when the array becomes at least 75% full.
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(capacity);
    adjustCapacity(table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);

  bool isNewKey = entry->key == NULL;
  bool isTombStone = IS_NIL(entry->value);
  if (isNewKey && isTombStone) table->count++;

  entry->key = key;
  entry->value = value;

  return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
  if (table->count == 0) return false;

  // Find the entry
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];

    if (entry->key != NULL) { tableSet(to, entry->key, entry->value); }
  }
}

ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t index = hash % table->capacity;
  for (;;) {
    Entry* entry = &table->entries[index];

    if (entry->key == NULL) {
      // Stop if we find an empty non-tombstone entry
      if (IS_NIL(entry->value)) return NULL;

    } else if (entry->key->length == length && entry->key->hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      // We found it
      return entry->key;
    }

    index = (index + 1) % table->capacity;
  }
}