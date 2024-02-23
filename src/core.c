
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

ObjClass* defineNativeClass(ObjString* name, ObjMap* dest,
                            ObjClass* metaclass) {
  ObjClass* klass = newClass(name, metaclass);

  defineNative(name, OBJ_VAL(klass), dest);

  return klass;
}

bool __seqInit__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPop());
  mapSet(&obj->fields, OBJ_VAL(intern("values")), OBJ_VAL(newSequence()));
  vmPush(OBJ_VAL(obj));
  return true;
}

bool __seqAdd__(int argCount, Value* args) {
  Value val = vmPop();
  ObjInstance* obj = AS_INSTANCE(vmPop());

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
  // leave the sequence on the stack.
  vmPush(OBJ_VAL(obj));

  return true;
}

bool __seqIn__(int argCount, Value* args) {
  Value val = vmPop();
  ObjInstance* obj = AS_INSTANCE(vmPop());

  Value seq;
  if (!mapGet(&obj->fields, OBJ_VAL(intern("values")), &seq)) {
    runtimeError("Sequence instance missing its values!");
    return false;
  }

  bool seqHasVal = findInValueArray(&AS_SEQUENCE(seq)->values, val);
  vmPush(BOOL_VAL(seqHasVal));

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

static void seqInstance() {
  ObjInstance* seq = newInstance(getClass("Sequence"));
  vmPush(OBJ_VAL(seq));
  initClass(vm.seqClass, 0);
}

bool __objEntries__(int argCount, Value* args) {
  ObjInstance* map = AS_INSTANCE(vmPop());
  ObjString* addMethod = intern("add");

  // the entry sequence.
  seqInstance();

  for (int i = 0; i < map->fields.capacity; i++) {
    MapEntry* entry = &map->fields.entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;

    // key/val sequence.
    seqInstance();

    vmPush(entry->key);
    invoke(addMethod, 1);
    vmPush(entry->value);
    invoke(addMethod, 1);

    // add key/val seq to entry seq.
    invoke(addMethod, 1);
  }

  return true;
}

bool __objGet__(int argCount, Value* args) {
  Value key = vmPop();
  Value obj = vmPop();

  if (!vmAssertInstanceSubscriptGet(obj)) return false;
  ObjInstance* instance = AS_INSTANCE(obj);
  return vmObjGet(instance, key);
}

bool __objSet__(int argCount, Value* args) {
  Value val = vmPop();
  Value key = vmPop();
  Value obj = vmPop();

  if (!vmAssertInstanceSubscriptSet(obj)) return false;
  ObjInstance* instance = AS_INSTANCE(obj);
  return vmObjSet(instance, key, val);
}

bool __objHas__(int argCount, Value* args) {
  Value val = vmPop();
  Value obj = vmPop();

  if (!IS_INSTANCE(obj)) {
    runtimeError("Only objects or sequences may be tested for membership.");
    return false;
  }

  return vmInstanceHas(AS_INSTANCE(obj), val);
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

// Use the user-defined hash function if defined, otherwise if
// the object supports it, fall back to the native hash, otherwise
// throw an error. Put the hash on the stack.
bool __hash__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  Value method;
  if (IS_INSTANCE(value) && mapGet(&AS_INSTANCE(value)->klass->methods,
                                   OBJ_VAL(vm.hashString), &method)) {
    // set up the context for the function call.
    vmPush(value);  // receiver.

    return vmCallValue(method, 0);
  }

  if (!isHashable(value)) {
    runtimeError(
        "Must be a hashable type: num, nil, bool, string, or object with a "
        "'%s' method.",
        vm.hashString->chars);
    return false;
  }

  uint32_t hash = hashValue(value);
  vmPush(NUMBER_VAL(hash));

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
  defineNativeFn(intern("hash"), 1, __hash__, &vm.globals);
  defineNativeFn(intern("type"), 1, __type__, &vm.globals);

  // load the types first.
  InterpretResult coreTypeInterpretation = interpretFile("src/core/type");
  if (coreTypeInterpretation != INTERPRET_OK) return coreTypeInterpretation;
  vm.metaClass = getClass("Metaclass");

  // native classes.
  vm.seqClass = defineNativeClass(intern("__seq__"), &vm.globals, NULL);
  defineNativeFn(vm.initString, 0, __seqInit__, &vm.seqClass->methods);
  defineNativeFn(intern("add"), 1, __seqAdd__, &vm.seqClass->methods);
  defineNativeFn(vm.memberString, 1, __seqIn__, &vm.seqClass->methods);

  vm.objClass = defineNativeClass(intern("__obj__"), &vm.globals, NULL);
  defineNativeFn(intern("get"), 1, __objGet__, &vm.objClass->methods);
  defineNativeFn(intern("set"), 2, __objSet__, &vm.objClass->methods);
  defineNativeFn(intern("has"), 1, __objHas__, &vm.objClass->methods);
  defineNativeFn(intern("entries"), 0, __objEntries__, &vm.objClass->methods);

  // core definitions.
  return interpretFile("src/core/__index__");
}