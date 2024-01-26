
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "object.h"
#include "value.h"
#include "vm.h"

static void defineNative(const char* name, int arity, NativeFn function) {
  // intern the name.
  ObjString* strName = copyString(name, (int)strlen(name));

  push(OBJ_VAL(strName));
  push(OBJ_VAL(newNative(arity, strName, function)));
  mapSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

static bool validateMapKey(Value key) {
  if (!IS_STRING(key)) {
    runtimeError("Map key must be a string.");
    return false;
  }
  return true;
}

static bool validateMap(Value map, char* msg) {
  if (!IS_MAP(map)) {
    runtimeError(msg);
    return false;
  }
  return true;
}

static bool __mapNew__(int argCount, Value* args) {
  vm.stackTop -= argCount + 1;
  push(OBJ_VAL(newMap()));
  return true;
}

static bool __mapSet__(int argCount, Value* args) {
  Value val = pop();
  Value key = pop();
  pop();  // native fn.
  Value map = pop();

  if (!validateMapKey(key)) return false;
  if (!validateMap(map, "Can only set property of a map.")) return false;

  mapSet(AS_MAP(map), AS_STRING(key), val);

  push(map);  // return the map.
  return true;
}

static bool __mapGet__(Value key, Value map, Value* val) {
  if (!validateMapKey(key)) return false;
  if (!validateMap(map, "Can only get property of a map.")) return false;

  mapGet(AS_MAP(map), AS_STRING(key), val);

  return true;
}

static bool __subscript__(int argCount, Value* args) {
  Value arg = pop();
  pop();  // native fn.
  Value obj = pop();

  switch (OBJ_TYPE(obj)) {
    case OBJ_MAP: {
      Value val;
      if (!__mapGet__(arg, obj, &val)) return false;
      push(val);
      break;
    }
    default: {
      runtimeError("Only maps support subscripts.");
      return false;
    }
  }

  return true;
}

void initializeCore(VM* vm) {
  defineNative("__mapNew__", 0, __mapNew__);
  defineNative("__mapSet__", 2, __mapSet__);
  defineNative("__subscript__", 1, __subscript__);
}
