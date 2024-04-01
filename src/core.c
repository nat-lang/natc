#include "core.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void defineNativeFn(char* name, int arity, NativeFn function,
                           ObjMap* dest) {
  // first put the values on the stack so they're
  // not gc'd if/when the map is recapacitated.
  ObjString* objName = intern(name);
  vmPush(OBJ_VAL(objName));
  ObjNative* native = newNative(arity, objName, function);
  Value fn = OBJ_VAL(native);
  vmPush(fn);
  mapSet(dest, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();
}

static void defineNativeFnGlobal(char* name, int arity, NativeFn function) {
  defineNativeFn(name, arity, function, &vm.globals);
}

static void defineNativeFnMethod(char* name, int arity, NativeFn function,
                                 ObjClass* klass) {
  defineNativeFn(name, arity, function, &klass->methods);
}

ObjClass* defineNativeClass(char* name) {
  ObjString* objName = intern(name);
  vmPush(OBJ_VAL(objName));
  ObjClass* klass = newClass(objName);
  vmPush(OBJ_VAL(klass));
  mapSet(&vm.globals, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();
  return klass;
}

bool __seqInit__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(0));

  vmPush(OBJ_VAL(intern("values")));
  vmPush(OBJ_VAL(newSequence()));
  mapSet(&obj->fields, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();

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
  vmPush(OBJ_VAL(entries));

  for (int i = 0; i < map->fields.capacity; i++) {
    MapEntry* entry = &map->fields.entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;

    ObjSequence* entrySeq = newSequence();
    vmPush(OBJ_VAL(entrySeq));

    writeValueArray(&entrySeq->values, entry->key);
    writeValueArray(&entrySeq->values, entry->value);
    writeValueArray(&entries->values, OBJ_VAL(entrySeq));

    vmPop();
  }

  // pop the entries, map, and native fn, now that we're done
  // with them and leave the entries on the stack.
  vmPop();
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
  if (!validateHashable(vmPeek(0))) return false;

  Value value = NIL_VAL;

  if (!mapGet(&AS_INSTANCE(vmPeek(1))->fields, vmPeek(0), &value))
    mapGet(&AS_INSTANCE(vmPeek(1))->klass->methods, vmPeek(0), &value);

  vmPop();
  vmPop();
  vmPush(value);
  return true;
}

bool __objSet__(int argCount, Value* args) {
  if (!validateHashable(vmPeek(1))) return false;
  mapSet(&AS_INSTANCE(vmPeek(2))->fields, vmPeek(1), vmPeek(0));

  vmPop();
  vmPop();
  vmPop();
  return true;
}

bool __objHas__(int argCount, Value* args) {
  if (!validateHashable(vmPeek(0))) return false;

  bool hasKey = mapHas(&AS_INSTANCE(vmPeek(1))->fields, vmPeek(0)) ||
                mapHas(&AS_INSTANCE(vmPeek(1))->klass->methods, vmPeek(0));
  vmPop();
  vmPop();
  vmPush(BOOL_VAL(hasKey));
  return true;
}

bool __length__(int argCount, Value* args) {
  if (IS_SEQUENCE(vmPeek(0))) {
    ObjSequence* seq = AS_SEQUENCE(vmPeek(0));
    vmPop();
    vmPop();  // native fn.
    vmPush(NUMBER_VAL(seq->values.count));
    return true;
  }

  // use the object's length method if defined.
  if (!IS_INSTANCE(vmPeek(0))) {
    runtimeError("Only sequences and objects with a '%s' method have length.",
                 vm.lengthString->chars);
    vmPop();
    vmPop();  // native fn.
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(vmPeek(0));
  Value method;
  if (mapGet(&instance->klass->methods, OBJ_VAL(vm.lengthString), &method)) {
    // set up the context for the function call.
    vmPop();
    vmPop();                    // native fn.
    vmPush(OBJ_VAL(instance));  // receiver.
    return vmCallValue(method, 0);
  }

  // otherwise default to the instance's field count.
  vmPop();
  vmPop();  // native fn.
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

  defineNativeFnGlobal("len", 1, __length__);
  defineNativeFnGlobal("str", 1, __str__);
  defineNativeFnGlobal("getHash", 1, __getHash__);
  defineNativeFnGlobal("setHash", 2, __setHash__);
  defineNativeFnGlobal("type", 1, __type__);
  defineNativeFnGlobal("entries", 1, __objEntries__);
  defineNativeFnGlobal("clock", 0, __clock__);

  defineNativeFnGlobal("__gt__", 2, __gt__);
  defineNativeFnGlobal("__lt__", 2, __lt__);
  defineNativeFnGlobal("__gte__", 2, __gte__);
  defineNativeFnGlobal("__lte__", 2, __lte__);
  defineNativeFnGlobal("__add__", 2, __add__);
  defineNativeFnGlobal("__sub__", 2, __sub__);
  defineNativeFnGlobal("__div__", 2, __div__);
  defineNativeFnGlobal("__mul__", 2, __mul__);

  vm.objClass = defineNativeClass("__obj__");
  defineNativeFnMethod("get", 1, __objGet__, vm.objClass);
  defineNativeFnMethod("set", 2, __objSet__, vm.objClass);
  defineNativeFnMethod("has", 1, __objHas__, vm.objClass);

  // native classes.

  InterpretResult coreIntpt = interpretFile("src/core/__index__");
  if (coreIntpt != INTERPRET_OK) return coreIntpt;

  vm.seqClass = getClass("Sequence");
  defineNativeFnMethod("init", 0, __seqInit__, vm.seqClass);
  defineNativeFnMethod("add", 1, __seqAdd__, vm.seqClass);

  return INTERPRET_OK;
}
