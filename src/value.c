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
  switch (value.vmType) {
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
  printf("(");
  for (int i = 0; i < array->count; i++) {
    printValue(array->values[i]);
    if (i != array->count - 1) printf(", ");
  }
  if (array->count == 0 || array->count == 1) printf(",");
  printf(")");
}

bool valuesEqual(Value a, Value b);

bool subMap(Map* a, Map* b) {
  for (int i = 0; i < a->count; i++) {
    MapEntry* entry = &a->entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;
    Value bValue;
    if (!mapGet(b, entry->key, &bValue)) return false;
    if (!valuesEqual(entry->value, bValue)) return false;
  }
  return true;
}

bool valuesEqual(Value a, Value b) {
  if (a.vmType != b.vmType) return false;

  switch (a.vmType) {
    case VAL_UNDEF:
    case VAL_UNIT:
    case VAL_NIL:
      return true;
    case VAL_BOOL:
      return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NUMBER:
      return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
      Obj* aObj = AS_OBJ(a);
      Obj* bObj = AS_OBJ(b);

      if (aObj->oType != bObj->oType) return false;

      switch (OBJ_TYPE(a)) {
        case OBJ_SEQUENCE: {
          ObjSequence* aSeq = AS_SEQUENCE(a);
          ObjSequence* bSeq = AS_SEQUENCE(b);
          if (aSeq->values.count != bSeq->values.count) return false;
          for (int i = 0; i < aSeq->values.count; i++) {
            if (!valuesEqual(aSeq->values.values[i], bSeq->values.values[i]))
              return false;
          }
          return true;
        }
        case OBJ_MAP: {
          ObjMap* aMap = AS_MAP(a);
          ObjMap* bMap = AS_MAP(b);
          return subMap(&aMap->obj.fields, &bMap->obj.fields) &&
                 subMap(&bMap->obj.fields, &aMap->obj.fields);
        }
        case OBJ_SET: {
          ObjSet* aSet = AS_SET(a);
          ObjSet* bSet = AS_SET(b);
          return subMap(&aSet->elements, &bSet->elements) &&
                 subMap(&bSet->elements, &aSet->elements);
        }
        case OBJ_TREE: {
          ObjTree* aTree = AS_TREE(a);
          ObjTree* bTree = AS_TREE(b);
          Value aVal, bVal;
          if (!mapGet(&aTree->obj.fields, OBJ_VAL(vm.core.sValue), &aVal))
            return false;
          if (!mapGet(&bTree->obj.fields, OBJ_VAL(vm.core.sValue), &bVal))
            return false;
          if (!valuesEqual(aVal, bVal)) return false;
          if (aTree->children.count != bTree->children.count) return false;
          for (int i = 0; i < aTree->children.count; i++) {
            if (!valuesEqual(aTree->children.values[i],
                             bTree->children.values[i])) {
              return false;
            }
          }
          return true;
        }
        case OBJ_STRING: {
          ObjString* aString = AS_STRING(a);
          ObjString* bString = AS_STRING(b);
          return aString->hash == bString->hash;
        }
        default:
          // do they point to the same place on the heap?
          return aObj == bObj;
      }
    }
  }
  return false;
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

bool vHashable(Value value) {
  return (IS_BOOL(value) || IS_NIL(value) || IS_UNDEF(value) ||
          IS_UNIT(value) || IS_NUMBER(value) || IS_STRING(value) ||
          IS_SET(value));
}

// Generates a hash code for [value], which must be one of
// nil, bool, num, or string.
uint32_t hashValue(Value value) {
  switch (value.vmType) {
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
      switch (OBJ_TYPE(value)) {
        case OBJ_SET: {
          Map* elements = &AS_SET(value)->elements;
          uint32_t hash = 0;
          for (int i = 0; i < elements->capacity; i++) {
            MapEntry* entry = &elements->entries[i];
            if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value) ||
                !IS_BOOL(entry->value) || !AS_BOOL(entry->value))
              continue;
            hash = hash + hashValue(entry->key);
          }

          return hash;
        }
        case OBJ_STRING: {
          ObjString* string = AS_STRING(value);
          return string->hash;
        }
        default: {
          return 0;
        }
      }
    }
  }

  // unreachable.
  return 0;
}

Value tokenValue(Token token) {
  return OBJ_VAL(copyString(token.start, token.length));
}
