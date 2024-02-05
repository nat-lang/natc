
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
#include "debug.h"
#include "io.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static void defineNative(ObjString* name, Value native, ObjMap* dest) {
  push(OBJ_VAL(name));
  push(native);
  mapSet(dest, vm.stack[0], vm.stack[1]);
  pop();
  pop();
}

static void defineNativeFn(ObjString* name, int arity, NativeFn function,
                           ObjMap* dest) {
  Value fn = OBJ_VAL(newNative(arity, name, function));
  defineNative(name, fn, dest);
}

ObjClass* defineNativeClass(ObjString* name, ObjMap* dest) {
  ObjClass* klass = newClass(name);

  defineNative(name, OBJ_VAL(klass), dest);

  return klass;
}

ObjClass* getClass(char* name) {
  Value value;
  if (!mapGet(&vm.globals, OBJ_VAL(intern(name)), &value)) {
    runtimeError("Undefined class '%s'.", name);
    return NULL;
  }
  if (!IS_CLASS(value)) {
    runtimeError("Expected class for '%s'.", name);
    return NULL;
  }
  return AS_CLASS(value);
}

static bool validateObj(Value obj, char* msg) {
  if (!IS_INSTANCE(obj)) {
    runtimeError(msg);
    return false;
  }
  return true;
}

static bool validateSeq(Value seq) {
  if (!IS_SEQUENCE(seq)) {
    runtimeError("Expecting sequence.");
    return false;
  }
  return true;
}

static bool __objGet__(Value key, ObjInstance* obj) {
  Value val;

  if (!validateMapKey(key)) return false;

  ObjMap fields = obj->fields;

  if (mapGet(&fields, key, &val))
    push(val);
  else
    push(NIL_VAL);

  return true;
}

static bool __subscriptSet__(int argCount, Value* args) {
  Value val = pop();
  Value key = pop();
  pop();  // native fn.
  Value obj = pop();

  if (!validateMapKey(key)) return false;
  if (!validateObj(obj, "Can only set property of object.")) return false;

  mapSet(&AS_INSTANCE(obj)->fields, key, val);
  push(obj);  // return the object.
  return true;
}

static bool __subscriptGet__(int argCount, Value* args) {
  Value key = pop();
  pop();  // native fn.
  Value obj = pop();

  if (!validateObj(obj, "Can only get property of an object.")) return false;

  switch (OBJ_TYPE(obj)) {
    case OBJ_INSTANCE: {
      __objGet__(key, AS_INSTANCE(obj));
      break;
    }
    default: {
      runtimeError("Only objects support subscript.");
      return false;
    }
  }

  return true;
}

bool __seqInit__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(pop());
  mapSet(&obj->fields, OBJ_VAL(intern("values")), OBJ_VAL(newSequence()));
  push(OBJ_VAL(obj));
  return true;
}

bool __seqAdd__(int argCount, Value* args) {
  Value val = pop();
  ObjInstance* obj = AS_INSTANCE(pop());

  Value seq;
  if (!mapGet(&obj->fields, OBJ_VAL(intern("values")), &seq)) {
    runtimeError("Sequence instance missing its values!");
    return false;
  }

  if (!validateSeq(seq)) return false;

  writeValueArray(&AS_SEQUENCE(seq)->values, val);
  push(OBJ_VAL(obj));

  return true;
}

bool __seqIn__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(pop());
  Value val = pop();

  Value seq;
  if (!mapGet(&obj->fields, OBJ_VAL(intern("values")), &seq)) {
    runtimeError("Sequence instance missing its values!");
    return false;
  }

  bool seqHasVal = findInValueArray(&AS_SEQUENCE(seq)->values, val);
  push(BOOL_VAL(seqHasVal));

  return true;
}

static void seqInstance() {
  ObjInstance* seq = newInstance(vm.seqClass);
  push(OBJ_VAL(seq));
  initClass(vm.seqClass, 0);
}

bool __mapEntries__(int argCount, Value* args) {
  ObjInstance* map = AS_INSTANCE(pop());

  // the entry sequence.
  seqInstance();

  for (int i = 0; i < map->fields.capacity; i++) {
    MapEntry* entry = &map->fields.entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;

    // key/val sequence.
    seqInstance();

    push(entry->key);
    invoke(intern("add"), 1);
    push(entry->value);
    invoke(intern("add"), 1);

    // add key/val seq to entry seq.
    invoke(intern("add"), 1);
  }

  return true;
}

void initializeCore() {
  // core functions.
  defineNativeFn(intern("__subscriptGet__"), 1, __subscriptGet__, &vm.globals);
  defineNativeFn(intern("__subscriptSet__"), 2, __subscriptSet__, &vm.globals);

  // core classes.
  vm.seqClass = defineNativeClass(intern("__seq__"), &vm.globals);
  defineNativeFn(vm.initString, 0, __seqInit__, &vm.seqClass->methods);
  defineNativeFn(intern("add"), 1, __seqAdd__, &vm.seqClass->methods);
  defineNativeFn(intern("in"), 1, __seqIn__, &vm.seqClass->methods);

  // native definitions.
  runFile("./src/core.nl");

  // augment native definitions.
  vm.mapClass = getClass("Map");
  defineNativeFn(intern("entries"), 0, __mapEntries__, &vm.mapClass->methods);
}