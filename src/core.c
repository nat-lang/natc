
#include <string.h>
#include <time.h>

#include "object.h"
#include "value.h"
#include "vm.h"

static void defineNative(const char* name, int arity, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(arity, function)));
  mapSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
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

  ObjString* objKey;
  if (IS_STRING(key))
    objKey = AS_STRING(key);
  else {
    runtimeError("Map key must be a string.");
    return false;
  }

  ObjMap* objMap;
  if (IS_MAP(map))
    objMap = AS_MAP(map);
  else {
    runtimeError("Can only set variable property of map.");
    return false;
  }

  mapSet(objMap, objKey, val);

  vm.stackTop -= argCount + 1;
  push(map);
  return true;
}

void initializeCore(VM* vm) {
  defineNative("__mapNew__", 0, __mapNew__);
  defineNative("__mapSet__", 2, __mapSet__);
}
