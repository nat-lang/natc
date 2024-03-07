#ifndef nat_value_h
#define nat_value_h

#include <math.h>

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum { VAL_BOOL, VAL_NIL, VAL_NUMBER, VAL_OBJ, VAL_UNDEF } ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
} Value;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_INTEGER(value) \
  ((value).type == VAL_NUMBER && rintf((value).as.number) == (value).as.number)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_UNDEF(value) ((value).type == VAL_UNDEF)

#define AS_OBJ(value) ((value).as.obj)
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj*)object}})
#define UNDEF_VAL ((Value){VAL_UNDEF, {}})

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
bool findInValueArray(ValueArray* array, Value value);
void printValue(Value value);
void printValueArray(ValueArray* array);
uint32_t hashValue(Value value);
bool isHashable(Value value);
Value typeValue(Value value);

#endif
