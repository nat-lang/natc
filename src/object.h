#ifndef nat_object_h
#define nat_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_MAP(value) isObjType(value, OBJ_MAP)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_SEQUENCE(value) isObjType(value, OBJ_SEQUENCE)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_SEQUENCE(value) (((ObjSequence *)AS_OBJ(value)))

typedef enum {
  OBJ_BOUND_METHOD,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_INSTANCE,
  OBJ_MAP,
  OBJ_NATIVE,
  OBJ_SEQUENCE,
  OBJ_STRING,
  OBJ_UPVALUE,
} ObjType;

struct Obj {
  ObjType type;
  bool isMarked;
  struct Obj *next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef bool (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  int arity;
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
  struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  int upvalueCount;
} ObjClosure;

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
  ObjMap methods;
  ObjMap fields;
} ObjClass;

typedef struct {
  Obj obj;
  ObjClass *klass;
  ObjMap fields;
} ObjInstance;

typedef struct {
  Obj obj;
  Value receiver;
  ObjClosure *method;
} ObjBoundMethod;

typedef struct {
  Obj obj;
  ValueArray values;
} ObjSequence;

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);
ObjClass *newClass(ObjString *name);
ObjClosure *newClosure(ObjFunction *function);
ObjFunction *newFunction();
ObjInstance *newInstance(ObjClass *klass);
ObjMap *newMap();
ObjNative *newNative(int arity, ObjString *name, NativeFn function);
ObjSequence *newSequence();
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjString *intern(const char *chars);
ObjUpvalue *newUpvalue(Value *slot);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

void initMap(ObjMap *map);
void freeMap(ObjMap *map);
bool mapHas(ObjMap *map, Value key);
bool mapGetHash(ObjMap *map, Value key, Value *value, uint32_t hash);
bool mapGet(ObjMap *map, Value key, Value *value);
bool mapSetHash(ObjMap *map, Value key, Value value, uint32_t);
bool mapSet(ObjMap *map, Value key, Value value);
bool mapDelete(ObjMap *map, Value key);
void mapAddAll(ObjMap *from, ObjMap *to);
ObjString *mapFindString(ObjMap *map, const char *chars, int length,
                         uint32_t hash);
void mapRemoveWhite(ObjMap *map);
void markMap(ObjMap *map);

uint32_t hashObject(Obj *object);

#endif