
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

static bool __callMethod__(Value value, ObjString* name, int arity) {
  if (IS_INSTANCE(value)) {
    ObjInstance* instance = AS_INSTANCE(value);
    Value method;
    if (mapGet(&instance->klass->methods, OBJ_VAL(name), &method)) {
      // set up the context for the function call.
      push(value);  // receiver.
      callMethod(method, arity);
      return true;
    }
  }
  return false;
}

// Use the user-defined hash function if defined, otherwise if
// the object supports it, fall back to the native hash, otherwise
// throw an error. Assert that the hash is a number and leave it on
// the stack.
static bool __hashValue__(Value value) {
  printf("value to hash: ");
  printValue(value);
  printf("\n-------------\n");
  if (__callMethod__(value, vm.hashString, 0)) {
    if (!IS_NUMBER(vmPeek(0))) {
      printf("result of hash: ");
      printValue(vmPeek(0));
      printf("\n-------------\n");
      // runtimeError("%s function must return a number.",
      // vm.hashString->chars); return false;
    }

    return true;
  }

  if (!isHashable(value)) {
    runtimeError(
        "Must be a hashable type: num, nil, bool, string, or object with a "
        "'%s' method.",
        vm.hashString->chars);
    return false;
  }

  uint32_t hash = hashValue(value);
  push(NUMBER_VAL(hash));

  return true;
}

static bool __objGet__(ObjInstance* obj, Value key) {
  if (!__hashValue__(key)) return false;
  uint32_t hash = AS_NUMBER(vmPop());
  Value value;

  if (mapGetHash(&obj->fields, key, &value, hash)) {
    push(value);
  } else {
    push(NIL_VAL);
  }

  return true;
}

static bool __objSet__(ObjInstance* obj, Value key, Value value) {
  if (!__hashValue__(key)) return false;
  uint32_t hash = AS_NUMBER(vmPop());

  mapSetHash(&obj->fields, key, value, hash);
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
  Value val = vmPop();
  Value key = vmPop();
  vmPop();  // native fn.
  Value obj = vmPop();

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
  Value key = vmPop();
  vmPop();  // native fn.
  Value obj = vmPop();

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
  ObjInstance* obj = AS_INSTANCE(vmPop());
  mapSet(&obj->fields, OBJ_VAL(intern("values")), OBJ_VAL(newSequence()));
  push(OBJ_VAL(obj));
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

  if (!validateSeq(seq)) return false;

  writeValueArray(&AS_SEQUENCE(seq)->values, val);
  push(OBJ_VAL(obj));

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
  ObjInstance* map = AS_INSTANCE(vmPop());
  ObjString* addMethod = intern("add");

  // the entry sequence.
  seqInstance();

  for (int i = 0; i < map->fields.capacity; i++) {
    MapEntry* entry = &map->fields.entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;

    // key/val sequence.
    seqInstance();

    push(entry->key);
    invoke(addMethod, 1);
    push(entry->value);
    invoke(addMethod, 1);

    // add key/val seq to entry seq.
    invoke(addMethod, 1);
  }

  return true;
}

bool __length__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  if (IS_SEQUENCE(value)) {
    ObjSequence* seq = AS_SEQUENCE(value);
    push(NUMBER_VAL(seq->values.count));
    return true;
  }

  if (__callMethod__(value, vm.lengthString, 0)) return true;

  runtimeError("Only sequences and objects with a '%s' method have length.",
               vm.lengthString->chars);
  return false;
}

bool __hash__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  return __hashValue__(value);
}

InterpretResult initializeCore() {
  // native functions.

  // these two primarily used be the compiler.
  // they probably deserve their own instructions.
  defineNativeFn(intern("__subscriptGet__"), 1, __subscriptGet__, &vm.globals);
  defineNativeFn(intern("__subscriptSet__"), 2, __subscriptSet__, &vm.globals);

  // user land.
  defineNativeFn(intern("len"), 1, __length__, &vm.globals);
  defineNativeFn(intern("hash"), 1, __hash__, &vm.globals);

  // native classes.
  vm.seqClass = defineNativeClass(intern("__seq__"), &vm.globals);
  defineNativeFn(vm.initString, 0, __seqInit__, &vm.seqClass->methods);
  defineNativeFn(intern("add"), 1, __seqAdd__, &vm.seqClass->methods);
  defineNativeFn(vm.memberString, 1, __seqIn__, &vm.seqClass->methods);

  vm.mapClass = defineNativeClass(intern("__map__"), &vm.globals);
  defineNativeFn(intern("entries"), 0, __mapEntries__, &vm.mapClass->methods);

  // core definitions.
  return interpretFile("src/core/__index__");
}