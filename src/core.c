
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
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

static bool __objSet__(int argCount, Value* args) {
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

bool __objGet__(Value key, ObjInstance* obj) {
  Value val;

  if (!validateMapKey(key)) return false;

  ObjMap fields = obj->fields;

  if (mapGet(&fields, key, &val))
    push(val);
  else
    push(NIL_VAL);

  return true;
}

bool __seqInit__() {
  pop();  // native fn.

  ObjSequence* seq = newSequence();

  push(OBJ_VAL(seq));
}

bool __seqAdd__() {
  Value val = pop();
  pop();  // native fn.
  Value seq = pop();
}

/*
bool __length__() {}

bool __objKeys__(int argCount, Value* args) {
  pop();  // native fn.
  Value obj = pop();

  if (!validateObj(obj, "Can only get keys of an object.")) return false;


  ObjMap fields = obj->fields;

  if (mapGet(&fields, key, &val))
    push(val);
  else
    push(NIL_VAL);

  return true;
}
*/

static bool __subscript__(int argCount, Value* args) {
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

void initializeCore(VM* vm) {
  defineNativeMap(vm);
  defineNativeFn(intern("__subscript__"), 1, __subscript__, &vm->globals);
  defineNativeFn(intern("__objSet__"), 2, __objSet__, &vm->globals);

  ObjClass* seqClass = defineNativeClass(intern("Seq"), &vm->globals);

  defineNativeFn(vm->initString, 0, __seqInit__, &seqClass->methods);
  defineNativeFn(intern("__add__"), 1, __seqAdd__, &seqClass->methods);

  runFile("./src/core.nl");
}