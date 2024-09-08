#include "value.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "vm.h"

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

Value popValueArray(ValueArray* array) { return array->values[--array->count]; }

bool findInValueArray(ValueArray* array, Value value) {
  for (int i = 0; i < array->count; i++) {
    if (valuesEqual(array->values[i], value)) return true;
  }
  return false;
}

void printValue(Value value) {
  switch (value.vType) {
    case VAL_UNIT: {
      printf("()");
      break;
    }
    case VAL_BOOL: {
      printf(AS_BOOL(value) ? "true" : "false");
      break;
    }
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

void printValueArray(ValueArray* array) {
  printf("[");
  for (int i = 0; i < array->count; i++) {
    printValue(array->values[i]);
    if (i != array->count - 1) printf(", ");
  }
  printf("]");
}

bool valuesEqual(Value a, Value b) {
  if (a.vType != b.vType) return false;
  switch (a.vType) {
    case VAL_UNDEF:
    case VAL_UNIT:
    case VAL_NIL:
      return true;
    case VAL_BOOL:
      return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NUMBER:
      return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
      return objectsEqual(AS_OBJ(a), AS_OBJ(b));
    default:
      return false;  // Unreachable.
  }
}

static inline uint32_t hashBits(uint64_t hash) {
  // From wren's hashBits, which cites v8's ComputeLongHash(),
  // which in turn cites Thomas Wang, Integer Hash Functions.
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
  return (IS_BOOL(value) || IS_NIL(value) || IS_UNDEF(value) ||
          IS_UNIT(value) || IS_NUMBER(value) || IS_STRING(value)) ||
         IS_CLASS(value);
}

// Generates a hash code for [value], which must be one of
// nil, bool, num, or string.
uint32_t hashValue(Value value) {
  switch (value.vType) {
    case VAL_UNIT:
      return 4;
    case VAL_UNDEF:
      return 3;
    case VAL_NIL:
      return 2;
    case VAL_BOOL:
      return AS_BOOL(value);
    case VAL_NUMBER:
      return hashNumber(AS_NUMBER(value));
    case VAL_OBJ: {
      Obj* object = AS_OBJ(value);
      switch (object->oType) {
        case OBJ_STRING:
          return ((ObjString*)object)->hash;
        case OBJ_CLASS:
          return hashNumber((uintptr_t)object);
        default:
          return object->hash;
      }
    }
  }

  // unreachable.
  return 0;
}
