#include "value.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void initValueArray(ValueArray* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values =
        GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray* array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

bool findInValueArray(ValueArray* array, Value value) {
  for (int i = 0; i < array->count; i++) {
    if (valuesEqual(array->values[i], value)) return true;
  }
  return false;
}

void printValue(Value value) {
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
    case VAL_UNDEF:
      printf("undefined");
      break;
  }
}

bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL:
      return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
      return true;
    case VAL_NUMBER:
      return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
      return AS_OBJ(a) == AS_OBJ(b);
    default:
      return false;  // Unreachable.
  }
}

static inline uint32_t hashBits(uint64_t hash) {
  // From wren's hashBits, which in turn cites
  // v8's ComputeLongHash() which in turn cites:
  // Thomas Wang, Integer Hash Functions.
  // http://www.concentric.net/~Ttwang/tech/inthash.htm
  hash = ~hash + (hash << 18);  // hash = (hash << 18) - hash - 1;
  hash = hash ^ (hash >> 31);
  hash = hash * 21;  // hash = (hash + (hash << 2)) + (hash << 4);
  hash = hash ^ (hash >> 11);
  hash = hash + (hash << 6);
  hash = hash ^ (hash >> 22);
  return (uint32_t)(hash & 0x3fffffff);
}

typedef union {
  uint64_t bits64;
  uint32_t bits32[2];
  double num;
} DoubleBits;

static inline uint64_t doubleToBits(double num) {
  DoubleBits data;
  data.num = num;
  return data.bits64;
}

// Generates a hash code for [num].
static inline uint32_t hashNumber(double num) {
  // Hash the raw bits of the value.
  return hashBits(doubleToBits(num));
}

bool isHashable(Value value) {
  return (value.type == VAL_BOOL || value.type == VAL_NIL ||
          value.type == VAL_NUMBER || value.type == VAL_OBJ);
}

// Generates a hash code for [value], which must be one of
// nil, bool, num, or string.
uint32_t hashValue(Value value) {
  switch (value.type) {
    case VAL_BOOL:
      return AS_BOOL(value);
    case VAL_NIL:
      return 2;
    case VAL_NUMBER:
      return hashNumber(AS_NUMBER(value));
    case VAL_OBJ:
      return hashObject(AS_OBJ(value));
    case VAL_UNDEF:
      return 3;
  }

  return 0;
}