
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
  vmPush(OBJ_VAL(name));
  vmPush(native);
  mapSet(dest, vm.stack[0], vm.stack[1]);
  vmPop();
  vmPop();
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

bool __seqInit__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(0));

  mapSet(&obj->fields, OBJ_VAL(intern("values")), OBJ_VAL(newSequence()));

  return true;
}

bool __seqAdd__(int argCount, Value* args) {
  Value val = vmPeek(0);
  ObjInstance* obj = AS_INSTANCE(vmPeek(1));

  Value seq;
  if (!mapGet(&obj->fields, OBJ_VAL(intern("values")), &seq)) {
    runtimeError("Sequence instance missing its values!");
    return false;
  }
  if (!IS_SEQUENCE(seq)) {
    runtimeError("Expecting sequence.");
    return false;
  }

  writeValueArray(&AS_SEQUENCE(seq)->values, val);
  vmPop();

  return true;
}

static ObjClass* getClass(char* name) {
  Value obj;

  if (!mapGet(&vm.globals, OBJ_VAL(intern(name)), &obj)) {
    runtimeError("Couldn't find class %s", name);
    return NULL;
  }

  if (!IS_CLASS(obj)) {
    runtimeError("Not a class: %s", name);
    return NULL;
  }

  return AS_CLASS(obj);
}

bool seqInstance() {
  ObjInstance* seq = newInstance(getClass("Sequence"));
  vmPush(OBJ_VAL(seq));
  return initClass(vm.seqClass, 0);
}

bool __objEntries__(int argCount, Value* args) {
  if (!IS_INSTANCE(vmPeek(0))) {
    runtimeError("Only objects have entries.");
    return false;
  }

  ObjInstance* map = AS_INSTANCE(vmPeek(0));

  // the entry sequence.
  ObjSequence* entries = newSequence();

  for (int i = 0; i < map->fields.capacity; i++) {
    MapEntry* entry = &map->fields.entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;

    ObjSequence* entrySeq = newSequence();

    writeValueArray(&entrySeq->values, entry->key);
    writeValueArray(&entrySeq->values, entry->value);
    writeValueArray(&entries->values, OBJ_VAL(entrySeq));
  }

  // pop the map (and native fn) now that we're done
  // with them and leave the entries on the stack.
  vmPop();
  vmPop();
  vmPush(OBJ_VAL(entries));

  return true;
}

static bool validateHash(Value value) {
  if (!IS_NUMBER(value)) {
    runtimeError("Hash must be a number.");
    return false;
  }
  return true;
}

bool __objGet__(int argCount, Value* args) {
  Value key = vmPop();
  Value obj = vmPop();

  if (!validateHashable(key)) return false;

  Value value;
  if (mapGet(&AS_INSTANCE(obj)->fields, key, &value) ||
      mapGet(&AS_INSTANCE(obj)->klass->methods, key, &value)) {
    vmPush(value);
  } else {
    vmPush(NIL_VAL);
  }

  return true;
}

bool __objSet__(int argCount, Value* args) {
  Value val = vmPop();
  Value key = vmPop();
  Value obj = vmPop();

  if (!validateHashable(key)) return false;

  mapSet(&AS_INSTANCE(obj)->fields, key, val);
  return true;
}

bool __objHas__(int argCount, Value* args) {
  Value key = vmPop();
  Value obj = vmPop();

  if (!validateHashable(key)) return false;

  bool hasKey =
      mapHas(&AS_INSTANCE(obj)->fields, key) ||
      mapHas(&AS_INSTANCE(obj)->klass->methods, key);

  vmPush(BOOL_VAL(hasKey));
  return true;
}

bool __length__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  if (IS_SEQUENCE(value)) {
    ObjSequence* seq = AS_SEQUENCE(value);
    vmPush(NUMBER_VAL(seq->values.count));
    return true;
  }

  if (!IS_INSTANCE(value)) {
    runtimeError("Only sequences and objects with a '%s' method have length.",
                 vm.lengthString->chars);
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(value);

  Value method;
  if (mapGet(&instance->klass->methods, OBJ_VAL(vm.lengthString), &method)) {
    // set up the context for the function call.
    vmPush(value);  // receiver.
    return vmCallValue(method, 0);
  }

  // default to the instance's field count.
  vmPush(NUMBER_VAL(instance->fields.count));
  return true;
}

bool __getHash__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  uint32_t hash = hashValue(value);
  vmPush(NUMBER_VAL(hash));

  return true;
}

bool __setHash__(int argCount, Value* args) {
  Value hash = vmPop();
  Value obj = vmPop();
  vmPop();  // native fn.

  if (IS_STRING(obj)) {
    runtimeError("Can't set hash of a string.");
    return false;
  }
  if (!IS_OBJ(obj)) {
    runtimeError("Can only set hash of an object.");
    return false;
  }

  if (!validateHash(hash)) return false;

  obj.as.obj->hash = AS_NUMBER(hash);

  // return nothing.
  vmPush(NIL_VAL);

  return true;
}

bool __type__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  vmPush(typeValue(value));
  return true;
}

InterpretResult initializeCore() {
  // native functions.

  defineNativeFn(intern("len"), 1, __length__, &vm.globals);
  defineNativeFn(intern("getHash"), 1, __getHash__, &vm.globals);
  defineNativeFn(intern("setHash"), 2, __setHash__, &vm.globals);
  defineNativeFn(intern("type"), 1, __type__, &vm.globals);
  defineNativeFn(intern("entries"), 1, __objEntries__, &vm.globals);

  vm.objClass = defineNativeClass(intern("__obj__"), &vm.globals);
  defineNativeFn(intern("get"), 1, __objGet__, &vm.objClass->methods);
  defineNativeFn(intern("set"), 2, __objSet__, &vm.objClass->methods);
  defineNativeFn(intern("has"), 1, __objHas__, &vm.objClass->methods);

  // native classes.

  InterpretResult coreIntpt = interpretFile("src/core/__index__");
  if (coreIntpt != INTERPRET_OK) return coreIntpt;

  vm.seqClass = getClass("Sequence");
  defineNativeFn(vm.initString, 0, __seqInit__, &vm.seqClass->methods);
  defineNativeFn(intern("add"), 1, __seqAdd__, &vm.seqClass->methods);

  return INTERPRET_OK;
}
