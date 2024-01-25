
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

static Value mapNew(int argCount, Value* args) { return OBJ_VAL(newMap()); }
static bool mapSet(int argCount, Value* args) {}

void initializeCore(VM* vm) {
  defineNative("__map__", 0, mapNew);
  defineNative("__mapSet__", 2, mapSet);
}
