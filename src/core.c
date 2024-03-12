#include "core.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

  bool hasKey = mapHas(&AS_INSTANCE(obj)->fields, key) ||
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

bool __clock__(int argCount, Value* args) {
  vmPop();  // native fn.
  vmPush(NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
  return true;
}

bool __str__(int argCount, Value* args) {
  Value value = vmPeek(0);
  ObjString* string;

  switch (value.type) {
    case VAL_NUMBER: {
      double num = AS_NUMBER(value);

      char buffer[24];
      int length = sprintf(buffer, "%.14g", num);

      string = copyString(buffer, length);
      break;
    }
    case VAL_NIL: {
      string = copyString("nil", 4);

      break;
    }
    case VAL_BOOL: {
      string =
          (AS_BOOL(value) ? copyString("true", 4) : copyString("false", 5));

      break;
    }
    default:
      return false;
  }

  vmPop();
  vmPop();  // native fn.
  vmPush(OBJ_VAL(string));

  return true;
}

BINARY_NATIVE(gt__, BOOL_VAL, >);
BINARY_NATIVE(lt__, BOOL_VAL, <);
BINARY_NATIVE(gte__, BOOL_VAL, >=);
BINARY_NATIVE(lte__, BOOL_VAL, <=);

BINARY_NATIVE(sub__, NUMBER_VAL, -);
BINARY_NATIVE(div__, NUMBER_VAL, /);
BINARY_NATIVE(mul__, NUMBER_VAL, *);

bool __add__(int argCount, Value* args) {
  if (IS_STRING(vmPeek(0)) && IS_STRING(vmPeek(1))) {
    // can't pop them until after the concatenation,
    // which allocates memory for the new string.
    ObjString* b = AS_STRING(vmPeek(0));
    ObjString* a = AS_STRING(vmPeek(1));

    ObjString* result = concatenateStrings(a, b);
    vmPop();
    vmPop();
    vmPop();  // fn
    vmPush(OBJ_VAL(result));
  } else if (IS_NUMBER(vmPeek(0)) && IS_NUMBER(vmPeek(1))) {
    double b = AS_NUMBER(vmPop());
    double a = AS_NUMBER(vmPop());
    vmPop();  // fn
    vmPush(NUMBER_VAL(a + b));
  } else {
    runtimeError("Operands must be two numbers or two strings.");
    return false;
  }
  return true;
}

InterpretResult initializeCore() {
  // native functions.

  defineNativeFn(intern("len"), 1, __length__, &vm.globals);
  defineNativeFn(intern("str"), 1, __str__, &vm.globals);
  defineNativeFn(intern("getHash"), 1, __getHash__, &vm.globals);
  defineNativeFn(intern("setHash"), 2, __setHash__, &vm.globals);
  defineNativeFn(intern("type"), 1, __type__, &vm.globals);
  defineNativeFn(intern("entries"), 1, __objEntries__, &vm.globals);
  defineNativeFn(intern("clock"), 0, __clock__, &vm.globals);

  defineNativeFn(intern("__gt__"), 2, __gt__, &vm.globals);
  defineNativeFn(intern("__lt__"), 2, __lt__, &vm.globals);
  defineNativeFn(intern("__gte__"), 2, __gte__, &vm.globals);
  defineNativeFn(intern("__lte__"), 2, __lte__, &vm.globals);
  defineNativeFn(intern("__add__"), 2, __add__, &vm.globals);
  defineNativeFn(intern("__sub__"), 2, __sub__, &vm.globals);
  defineNativeFn(intern("__div__"), 2, __div__, &vm.globals);
  defineNativeFn(intern("__mul__"), 2, __mul__, &vm.globals);

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
