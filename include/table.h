#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
  ObjString* key;
  Value value;
} Entry;

typedef struct {
  int count;
  int capacity;
  Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);

/* Get a value from a table */
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
/*
Deletions in this implementation cannot just use find and set to NULL
because of the probe sequencing of open addressing.

The trick is to use a tombstone, basically telling the probe to keep iterating
current "tombstone" is NULL key and true value
*/
bool tableDelete(Table* table, ObjString* key);

/* Copies an entire table  */
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash);

#endif