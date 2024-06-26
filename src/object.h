#ifndef nat_object_h
#define nat_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->oType)

#define IS_BOUND_FUNCTION(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_OVERLOAD(value) isObjType(value, OBJ_OVERLOAD)
#define IS_VARIABLE(value) isObjType(value, OBJ_VARIABLE)
#define IS_PATTERN(value) isObjType(value, OBJ_PATTERN)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_MAP(value) isObjType(value, OBJ_MAP)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_SEQUENCE(value) isObjType(value, OBJ_SEQUENCE)
#define IS_SPREAD(value) isObjType(value, OBJ_SPREAD)

#define AS_BOUND_FUNCTION(value) ((ObjBoundFunction *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_OVERLOAD(value) ((ObjOverload *)AS_OBJ(value))
#define AS_VARIABLE(value) (((ObjVariable *)AS_OBJ(value)))
#define AS_PATTERN(value) (((ObjPattern *)AS_OBJ(value)))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_SEQUENCE(value) (((ObjSequence *)AS_OBJ(value)))
#define AS_SPREAD(value) (((ObjSpread *)AS_OBJ(value)))

#define BOUND_FUNCTION_TYPE(value) (AS_BOUND_FUNCTION(value)->type)

#define INTERN(value) ((OBJ_VAL(intern(value))))

typedef enum {
  OBJ_BOUND_FUNCTION,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_OVERLOAD,
  OBJ_INSTANCE,
  OBJ_MAP,
  OBJ_NATIVE,
  OBJ_SEQUENCE,
  OBJ_STRING,
  OBJ_UPVALUE,
  OBJ_SPREAD,
  OBJ_VARIABLE,
  OBJ_PATTERN,
} ObjType;

struct Obj {
  ObjType oType;
  bool isMarked;
  uint32_t hash;
  struct Obj *next;
};

typedef struct {
  Value key;
  Value value;
} MapEntry;

typedef struct {
  Obj obj;
  int count;
  int capacity;
  MapEntry *entries;
} ObjMap;

typedef struct {
  Obj obj;
  ObjString *name;
} ObjVariable;

typedef struct {
  Value value;
  Value type;
} PatternElement;

typedef struct {
  Obj obj;
  int count;
  PatternElement *elements;
} ObjPattern;

typedef struct {
  Obj obj;
  int arity;
  bool variadic;
  int upvalueCount;
  ObjPattern *pattern;
  Chunk chunk;
  ObjString *name;
  // cache from values to constant indices
  // in the function's chunk.constants.
  ObjMap constants;
} ObjFunction;

typedef bool (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  int arity;
  bool variadic;
  ObjString *name;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj obj;
  int length;
  char *chars;
  uint32_t hash;
};

typedef struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  // the address of the local that's closed over.
  // we stash this only to reconstruct the ast.
  uint8_t slot;
  struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  int upvalueCount;
} ObjClosure;

typedef struct {
  Obj obj;
  int cases;
  ObjClosure *functions[UINT8_MAX];
} ObjOverload;

typedef struct ObjClass {
  Obj obj;
  ObjString *name;
  ObjMap fields;
  struct ObjClass *super;
} ObjClass;

typedef struct {
  Obj obj;
  ObjClass *klass;
  ObjMap fields;
} ObjInstance;

typedef enum { BOUND_METHOD, BOUND_NATIVE } BoundFunctionType;

typedef struct {
  Obj obj;
  BoundFunctionType type;
  Value receiver;
  union {
    ObjClosure *method;
    ObjNative *native;
  } bound;
} ObjBoundFunction;

typedef struct {
  Obj obj;
  ValueArray values;
} ObjSequence;

typedef struct {
  Obj obj;
  Value value;
} ObjSpread;

ObjBoundFunction *newBoundMethod(Value receiver, ObjClosure *method);
ObjBoundFunction *newBoundNative(Value receiver, ObjNative *native);
ObjClass *newClass(ObjString *name);
ObjClosure *newClosure(ObjFunction *function);
ObjFunction *newFunction();
ObjOverload *newOverload(int cases);
ObjVariable *newVariable(ObjString *name);
ObjPattern *newPattern(int count);
ObjInstance *newInstance(ObjClass *klass);
ObjNative *newNative(int arity, bool variadic, ObjString *name,
                     NativeFn function);
ObjSequence *newSequence();
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjString *concatenateStrings(ObjString *a, ObjString *b);
ObjString *intern(const char *chars);
ObjUpvalue *newUpvalue(Value *value, uint8_t slot);
ObjSpread *newSpread(Value value);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->oType == type;
}

void initMap(ObjMap *map);
void freeMap(ObjMap *map);
bool mapHas(ObjMap *map, Value key);
bool mapHasHash(ObjMap *map, Value key, uint32_t hash);
bool mapGet(ObjMap *map, Value key, Value *value);
bool mapGetHash(ObjMap *map, Value key, Value *value, uint32_t hash);
bool mapSet(ObjMap *map, Value key, Value value);
bool mapSetHash(ObjMap *map, Value key, Value value, uint32_t hash);
bool mapDelete(ObjMap *map, Value key);
void mapAddAll(ObjMap *from, ObjMap *to);
ObjString *mapFindString(ObjMap *map, const char *chars, int length,
                         uint32_t hash);
void mapRemoveWhite(ObjMap *map);
void markMap(ObjMap *map);
bool leastCommonAncestor(ObjClass *a, ObjClass *b, ObjClass *ancestor);
uint32_t hashObject(Obj *object);
bool objectsEqual(Obj *a, Obj *b);
#endif