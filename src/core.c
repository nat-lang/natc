
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

static bool validateSeq(Value seq) {
  if (!IS_SEQUENCE(seq)) {
    runtimeError("Expecting sequence.");
    return false;
  }
  return true;
}

static bool validateSeqIdx(ObjSequence* seq, Value idx) {
  if (!IS_INTEGER(idx)) {
    runtimeError("Sequences must be indexed by integer.");
    return false;
  }

  int intIdx = AS_NUMBER(idx);

  if (intIdx > seq->values.count || intIdx < 0) {
    runtimeError("Index %i out of bounds.", intIdx);
    return false;
  }

  return true;
}

static bool __objGet__(ObjInstance* obj, Value key) {
  Value val;

  if (!validateMapKey(key)) return false;

  ObjMap fields = obj->fields;

  if (mapGet(&fields, key, &val))
    push(val);
  else
    push(NIL_VAL);

  return true;
}

static bool __objSet__(ObjInstance* obj, Value key, Value val) {
  if (!validateMapKey(key)) return false;

  mapSet(&obj->fields, key, val);
  push(OBJ_VAL(obj));  // return the object.
  return true;
}

static bool __seqGet__(ObjSequence* seq, Value key) {
  if (!validateSeqIdx(seq, key)) return false;

  int idx = AS_NUMBER(key);

  if (idx > seq->values.count - 1 || idx < 0) {
    runtimeError("Index %i out of bounds.", idx);
    return false;
  }

  push(seq->values.values[idx]);
  return true;
}

static bool __seqSet__(ObjSequence* seq, Value key, Value val) {
  if (!validateSeqIdx(seq, key)) return false;

  int idx = AS_NUMBER(key);

  seq->values.values[idx] = val;
  push(OBJ_VAL(seq));  // return the sequence.

  return true;
}

static bool __subscriptSet__(int argCount, Value* args) {
  Value val = pop();
  Value key = pop();
  pop();  // native fn.
  Value obj = pop();

  // indexed assignment to sequences.
  if (IS_SEQUENCE(obj)) {
    return __seqSet__(AS_SEQUENCE(obj), key, val);
  }

  // classes may define their own subscript setting
  // operator, otherwise fall back to property setting.
  if (IS_INSTANCE(obj)) {
    ObjInstance* instance = AS_INSTANCE(obj);

    Value setFn;
    if (mapGet(&instance->klass->methods, OBJ_VAL(vm.subscriptSetString),
               &setFn)) {
      // set up the context for the function call.
      push(obj);  // receiver.
      push(key);
      push(val);
      return callMethod(setFn, 2);
    }

    return __objSet__(instance, key, val);
  }

  runtimeError(
      "Only objects, sequences, and instances with a '%s' method support "
      "assignment by subscript.",
      vm.subscriptSetString->chars);
  return false;
}

static bool __subscriptGet__(int argCount, Value* args) {
  Value key = pop();
  pop();  // native fn.
  Value obj = pop();

  // indexed access to sequences.
  if (IS_SEQUENCE(obj)) {
    return __seqGet__(AS_SEQUENCE(obj), key);
  }

  // classes may define their own subscript access
  // operator, otherwise fall back to property access.
  if (IS_INSTANCE(obj)) {
    ObjInstance* instance = AS_INSTANCE(obj);

    Value getFn;
    if (mapGet(&instance->klass->methods, OBJ_VAL(vm.subscriptGetString),
               &getFn)) {
      // set up the context for the function call.
      push(obj);  // receiver.
      push(key);
      return callMethod(getFn, 1);
    }

    return __objGet__(instance, key);
  }

  runtimeError(
      "Only objects, sequences, and instances with a '%s' method support "
      "access by subscript.",
      vm.subscriptGetString->chars);
  return false;
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

bool __length__(int argCount, Value* args) {
  Value val = pop();
  pop();  // native fn.

  if (IS_SEQUENCE(val)) {
    ObjSequence* seq = AS_SEQUENCE(val);
    push(NUMBER_VAL(seq->values.count));
    return true;
  }

  if (IS_INSTANCE(val)) {
    ObjInstance* instance = AS_INSTANCE(val);
    Value lenFn;
    if (mapGet(&instance->klass->methods, OBJ_VAL(vm.lengthString), &lenFn)) {
      // set up the context for the function call.
      push(val);  // receiver.
      callMethod(lenFn, 0);
      return true;
    }
  }

  runtimeError("Only sequences and objects with a '%s' method have length.",
               vm.lengthString->chars);
  return false;
}

InterpretResult initializeCore() {
  // native functions.

  // these two primarily used be the compiler.
  // maybe this indicates they deserve op codes.
  defineNativeFn(intern("__subscriptGet__"), 1, __subscriptGet__, &vm.globals);
  defineNativeFn(intern("__subscriptSet__"), 2, __subscriptSet__, &vm.globals);

  // user land.
  defineNativeFn(intern("len"), 1, __length__, &vm.globals);

  // native classes.
  vm.seqClass = defineNativeClass(intern("__seq__"), &vm.globals);
  defineNativeFn(vm.initString, 0, __seqInit__, &vm.seqClass->methods);
  defineNativeFn(intern("add"), 1, __seqAdd__, &vm.seqClass->methods);
  defineNativeFn(intern("in"), 1, __seqIn__, &vm.seqClass->methods);

  vm.mapClass = defineNativeClass(intern("__map__"), &vm.globals);
  defineNativeFn(intern("entries"), 0, __mapEntries__, &vm.mapClass->methods);

  // core definitions.
  return interpretFile("src/core/__index__");
}