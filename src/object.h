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
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_SEQUENCE(value) isObjType(value, OBJ_SEQUENCE)
#define IS_SPREAD(value) isObjType(value, OBJ_SPREAD)
#define IS_UPVALUE(value) isObjType(value, OBJ_UPVALUE)
#define IS_MODULE(value) isObjType(value, OBJ_MODULE)

#define AS_BOUND_FUNCTION(value) ((ObjBoundFunction *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_OVERLOAD(value) ((ObjOverload *)AS_OBJ(value))
#define AS_VARIABLE(value) (((ObjVariable *)AS_OBJ(value)))
#define AS_PATTERN(value) (((ObjPattern *)AS_OBJ(value)))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_SEQUENCE(value) (((ObjSequence *)AS_OBJ(value)))
#define AS_SPREAD(value) (((ObjSpread *)AS_OBJ(value)))
#define AS_UPVALUE(value) (((ObjUpvalue *)AS_OBJ(value)))
#define AS_MODULE(value) (((ObjModule *)AS_OBJ(value)))

#define BOUND_FUNCTION_TYPE(value) (AS_BOUND_FUNCTION(value)->type)

#define INTERN(value) ((OBJ_VAL(intern(value))))

typedef enum {
  OBJ_BOUND_FUNCTION,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_OVERLOAD,
  OBJ_INSTANCE,
  OBJ_NATIVE,
  OBJ_SEQUENCE,
  OBJ_STRING,
  OBJ_UPVALUE,
  OBJ_SPREAD,
  OBJ_VARIABLE,
  OBJ_MODULE,
} ObjType;

typedef struct {
  Value key;
  Value value;
} MapEntry;

typedef struct {
  int count;
  int capacity;
  MapEntry *entries;
} Map;

typedef struct ObjModule ObjModule;

struct Obj {
  ObjType oType;
  bool isMarked;
  uint32_t hash;
  struct Obj *next;
  ValueArray annotations;
  Map fields;
};

typedef struct {
  Obj obj;
  int length;
  char *chars;
} ObjString;

typedef struct {
  Obj obj;
  ObjString *name;
} ObjVariable;

typedef struct {
  Obj obj;
  int arity;
  bool variadic;
  bool patterned;
  int upvalueCount;

  Chunk chunk;
  Local locals[UINT8_COUNT];
  int localCount;

  ObjString *name;
  ObjModule *module;

  // cache from values to constant indices
  // in the function's chunk.constants.
  Map constants;
} ObjFunction;

typedef bool (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  int arity;
  bool variadic;
  ObjString *name;
  NativeFn function;
} ObjNative;

typedef struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  struct ObjUpvalue *next;
  // the address of the local that's closed over.
  // we stash this only to reconstruct the ast.
  uint8_t slot;
  ObjString *name;
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
  ObjClosure **closures;
} ObjOverload;

typedef struct ObjClass {
  Obj obj;
  ObjString *name;
  struct ObjClass *super;
} ObjClass;

typedef struct {
  Obj obj;
  ObjClass *klass;
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

typedef enum { MODULE_ENTRYPOINT, MODULE_IMPORT } ModuleType;

struct ObjModule {
  Obj obj;
  ModuleType type;
  ObjString *path;
  ObjString *source;
  ObjClosure *closure;
  Map namespace;
};

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
ObjFunction *newFunction(ObjModule *module);
ObjOverload *newOverload(int cases);
ObjVariable *newVariable(ObjString *name);
ObjInstance *newInstance(ObjClass *klass);
ObjModule *newModule(ObjString *path, ObjString *source, ModuleType type);
ObjNative *newNative(int arity, bool variadic, ObjString *name,
                     NativeFn function);
ObjSequence *newSequence();
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjString *concatenateStrings(ObjString *a, ObjString *b);
ObjString *intern(const char *chars);
ObjUpvalue *newUpvalue(Value *value, uint8_t slot, ObjString *name);
ObjSpread *newSpread(Value value);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->oType == type;
}

void initMap(Map *map);
void freeMap(Map *map);
bool mapHas(Map *map, Value key);
bool mapHasHash(Map *map, Value key, uint32_t hash);
bool mapGet(Map *map, Value key, Value *value);
bool mapGetHash(Map *map, Value key, Value *value, uint32_t hash);
bool mapSet(Map *map, Value key, Value value);
bool mapSetHash(Map *map, Value key, Value value, uint32_t hash);
bool mapDelete(Map *map, Value key);
void mapAddAll(Map *from, Map *to);
void setStringChar(ObjString *string, ObjString *character, int idx);
ObjString *mapFindString(Map *map, const char *chars, int length,
                         uint32_t hash);
void mapRemoveWhite(Map *map);
void markMap(Map *map);
bool leastCommonAncestor(ObjClass *a, ObjClass *b, ObjClass *ancestor);
#endif