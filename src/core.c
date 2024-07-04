#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
#include "io.h"

static void defineNativeFn(char* name, int arity, bool variadic,
                           NativeFn function, ObjMap* dest) {
  // keep the values on the stack so they're
  // not gc'd if/when the [dest] map is recapacitated.
  ObjString* objName = intern(name);
  vmPush(OBJ_VAL(objName));
  ObjNative* native = newNative(arity, variadic, objName, function);
  Value fn = OBJ_VAL(native);
  vmPush(fn);
  mapSet(dest, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();
}

static void defineNativeInfixGlobal(char* name, int arity, NativeFn function,
                                    Precedence prec) {
  defineNativeFn(name, arity, false, function, &vm.globals);

  Value fn;
  Value fnName = INTERN(name);
  mapGet(&vm.globals, fnName, &fn);
  mapSet(&vm.infixes, fnName, NUMBER_VAL(prec));
}

static void defineNativeFnGlobal(char* name, int arity, NativeFn function) {
  defineNativeFn(name, arity, false, function, &vm.globals);
}

static void defineNativeFnMethod(char* name, int arity, bool variadic,
                                 NativeFn function, ObjClass* klass) {
  defineNativeFn(name, arity, variadic, function, &klass->fields);
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

ObjClosure* getGlobalClosure(char* name) {
  Value obj;

  if (!mapGet(&vm.globals, INTERN(name), &obj)) {
    vmRuntimeError("Couldn't find function '%s'.", name);
    return NULL;
  }

  if (!IS_CLOSURE(obj)) {
    vmRuntimeError("Not a closure: '%s'.", name);
    return NULL;
  }

  return AS_CLOSURE(obj);
}

ObjClass* getGlobalClass(char* name) {
  Value obj;

  if (!mapGet(&vm.globals, INTERN(name), &obj)) {
    vmRuntimeError("Couldn't find class '%s'.", name);
    return NULL;
  }

  if (!IS_CLASS(obj)) {
    vmRuntimeError("Not a class: '%s'.", name);
    return NULL;
  }

  return AS_CLASS(obj);
}

ObjSequence* __sequentialInit__(int argCount) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(argCount));

  vmPush(INTERN("values"));
  ObjSequence* seq = newSequence();
  vmPush(OBJ_VAL(seq));
  mapSet(&obj->fields, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();

  int i = argCount;
  while (i-- > 0) writeValueArray(&seq->values, vmPeek(i));
  while (++i < argCount) vmPop();

  return seq;
}

bool __sequenceInit__(int argCount, Value* args) {
  __sequentialInit__(argCount);

  return true;
}

bool __sequencePush__(int argCount, Value* args) {
  Value val = vmPeek(0);
  ObjInstance* obj = AS_INSTANCE(vmPeek(1));
  Value seq;

  if (!vmSequenceValueField(obj, &seq)) return false;
  writeValueArray(&AS_SEQUENCE(seq)->values, val);
  vmPop();
  return true;
}

bool __sequencePop__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(0));
  Value seq;
  if (!vmSequenceValueField(obj, &seq)) return false;
  Value value = popValueArray(&AS_SEQUENCE(seq)->values);

  vmPop();
  vmPush(value);
  return true;
}

bool __tupleInit__(int argCount, Value* args) {
  __sequentialInit__(argCount);

  return true;
}

bool __objKeys__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(0));

  // the key sequence.
  vmPush(OBJ_VAL(vm.core.sequence));
  if (!vmCallValue(vmPeek(0), 0)) return false;

  for (int i = 0; i < obj->fields.capacity; i++) {
    MapEntry* entry = &obj->fields.entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;

    // add to sequence. seq's 'push' method
    // leaves itself on the stack for us.
    vmPush(entry->key);
    if (!vmInvoke(intern("push"), 1)) return false;
  }

  Value keys = vmPop();
  vmPop();  // obj.
  vmPush(keys);

  return true;
}

bool __objHas__(int argCount, Value* args) {
  uint32_t hash;
  if (!vmHashValue(vmPeek(0), &hash)) return false;

  bool hasKey =
      mapHasHash(&AS_INSTANCE(vmPeek(1))->fields, vmPeek(0), hash) ||
      mapHasHash(&AS_INSTANCE(vmPeek(1))->klass->fields, vmPeek(0), hash);
  vmPop();  // key.
  vmPop();  // obj.
  vmPush(BOOL_VAL(hasKey));
  return true;
}

bool __randomNumber__(int argCount, Value* args) {
  Value upperBound = vmPop();
  vmPop();  // native fn.

  if (!IS_NUMBER(upperBound)) {
    vmRuntimeError("Upper bound must be a number.");
    return false;
  }

  int x = rand() % (uint32_t)AS_NUMBER(upperBound);
  vmPush(NUMBER_VAL(x));
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
    vmRuntimeError("Only sequences and objects with a '%s' method have length.",
                   S_LEN);
    vmPop();
    vmPop();  // native fn.
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(vmPeek(0));
  Value method;
  if (mapGet(&instance->klass->fields, INTERN(S_LEN), &method)) {
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

bool __hash__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  uint32_t hash;
  if (!vmHashValue(value, &hash)) return false;
  vmPush(NUMBER_VAL(hash));
  return true;
}

bool __hashable__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.
  vmPush(BOOL_VAL(isHashable(value)));
  return true;
}

bool __vType__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  switch (value.vType) {
    case VAL_UNIT:
      vmPush(value);
      break;
    case VAL_BOOL:
      vmPush(OBJ_VAL(vm.core.vTypeBool));
      break;
    case VAL_NUMBER:
      vmPush(OBJ_VAL(vm.core.vTypeNumber));
      break;
    case VAL_NIL:
      vmPush(OBJ_VAL(vm.core.vTypeNil));
      break;
    case VAL_OBJ:
      switch (AS_OBJ(value)->oType) {
        case OBJ_VARIABLE:
          vmPush(OBJ_VAL(vm.core.oTypeVariable));
          break;
        case OBJ_CLASS:
          vmPush(OBJ_VAL(vm.core.oTypeClass));
          break;
        case OBJ_INSTANCE:
          vmPush(OBJ_VAL(vm.core.oTypeInstance));
          break;
        case OBJ_STRING:
          vmPush(OBJ_VAL(vm.core.oTypeString));
          break;
        case OBJ_NATIVE:
        case OBJ_CLOSURE:
          vmPush(OBJ_VAL(vm.core.oTypeClosure));
          break;
        case OBJ_OVERLOAD:
          vmPush(OBJ_VAL(vm.core.oTypeOverload));
          break;
        case OBJ_SEQUENCE:
          vmPush(OBJ_VAL(vm.core.oTypeSequence));
          break;
        default: {
          printValue(value);
          vmRuntimeError("Unexpected object (type %i).", AS_OBJ(value)->oType);
          return false;
        }
      }
      break;
    case VAL_UNDEF:
      vmPush(OBJ_VAL(vm.core.vTypeUndef));
      break;
    default:
      vmRuntimeError("Unexpected value.");
      return false;
  }
  return true;
}

bool __globals__(int argCount, Value* args) {
  vmPop();  // native fn.

  vmPush(OBJ_VAL(vm.core.map));
  vmInitInstance(vm.core.map, 0, 1);
  mapAddAll(&vm.globals, &AS_INSTANCE(vmPeek(0))->fields);

  return true;
}

bool __globalTypeEnv__(int argCount, Value* args) {
  vmPop();  // native fn.
  vmPush(OBJ_VAL(newInstance(vm.core.map)));
  vmInitInstance(vm.core.map, 0, 1);
  mapAddAll(&vm.typeEnv, &AS_INSTANCE(vmPeek(0))->fields);
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

  switch (value.vType) {
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
    case VAL_OBJ: {
      switch (OBJ_TYPE(value)) {
        case OBJ_CLASS: {
          string = AS_CLASS(value)->name;
          break;
        }
        case OBJ_STRING: {
          string = AS_STRING(value);
          break;
        }
        default: {
          vmRuntimeError("Can't turn object into a string.");
          return false;
        }
      }
      break;
    }
    default: {
      vmRuntimeError("Can't turn value into a string.");
      return false;
    }
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
    vmRuntimeError("Operands must be two numbers or two strings.");
    return false;
  }
  return true;
}

InterpretResult initializeCore() {
  // native functions.

  defineNativeFnGlobal("len", 1, __length__);
  defineNativeFnGlobal("str", 1, __str__);
  defineNativeFnGlobal("hash", 1, __hash__);
  defineNativeFnGlobal("hashable", 1, __hashable__);
  defineNativeFnGlobal("vType", 1, __vType__);
  defineNativeFnGlobal("globals", 0, __globals__);
  defineNativeFnGlobal("globalTypeEnv", 0, __globalTypeEnv__);
  defineNativeFnGlobal("clock", 0, __clock__);
  defineNativeFnGlobal("random", 1, __randomNumber__);

  defineNativeInfixGlobal(">", 2, __gt__, PREC_COMPARISON);
  defineNativeInfixGlobal("<", 2, __lt__, PREC_COMPARISON);
  defineNativeInfixGlobal(">=", 2, __gte__, PREC_COMPARISON);
  defineNativeInfixGlobal("<=", 2, __lte__, PREC_COMPARISON);
  defineNativeInfixGlobal("+", 2, __add__, PREC_TERM);
  defineNativeInfixGlobal("-", 2, __sub__, PREC_TERM);
  defineNativeInfixGlobal("/", 2, __div__, PREC_FACTOR);
  defineNativeInfixGlobal("*", 2, __mul__, PREC_FACTOR);

  // native classes.

  vm.core.base = defineNativeClass(S_BASE);
  defineNativeFnMethod("has", 1, false, __objHas__, vm.core.base);
  defineNativeFnMethod("keys", 0, false, __objKeys__, vm.core.base);

  // core classes.

  InterpretResult coreIntpt = interpretFile(NAT_CORE_LOC);

  if (coreIntpt != INTERPRET_OK) return coreIntpt;

  if ((vm.core.object = getGlobalClass(S_OBJECT)) == NULL) return false;

  if ((vm.core.tuple = getGlobalClass(S_TUPLE)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 0, true, __tupleInit__, vm.core.tuple);

  if ((vm.core.sequence = getGlobalClass(S_SEQUENCE)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 0, true, __sequenceInit__, vm.core.sequence);
  defineNativeFnMethod(S_PUSH, 1, false, __sequencePush__, vm.core.sequence);
  defineNativeFnMethod(S_ADD, 1, false, __sequencePush__, vm.core.sequence);
  defineNativeFnMethod(S_POP, 0, false, __sequencePop__, vm.core.sequence);

  if ((vm.core.map = getGlobalClass(S_MAP)) == NULL ||
      (vm.core.set = getGlobalClass(S_SET)) == NULL ||
      (vm.core.astClosure = getGlobalClass(S_AST_CLOSURE)) == NULL ||
      (vm.core.astUpvalue = getGlobalClass(S_AST_UPVALUE)) == NULL ||
      (vm.core.astSignature = getGlobalClass(S_AST_SIGNATURE)) == NULL ||
      (vm.core.astParameter = getGlobalClass(S_AST_PARAMETER)) == NULL ||
      (vm.core.astOverload = getGlobalClass(S_AST_OVERLOAD)) == NULL ||
      (vm.core.astVariable = getGlobalClass(S_AST_VARIABLE)) == NULL ||
      (vm.core.vTypeBool = getGlobalClass(S_CTYPE_BOOL)) == NULL ||
      (vm.core.vTypeNil = getGlobalClass(S_CTYPE_NIL)) == NULL ||
      (vm.core.vTypeNumber = getGlobalClass(S_CTYPE_NUMBER)) == NULL ||
      (vm.core.vTypeUndef = getGlobalClass(S_CTYPE_UNDEF)) == NULL ||
      (vm.core.oTypeVariable = getGlobalClass(S_OTYPE_VARIABLE)) == NULL ||
      (vm.core.oTypeClass = getGlobalClass(S_OTYPE_CLASS)) == NULL ||
      (vm.core.oTypeInstance = getGlobalClass(S_OTYPE_INSTANCE)) == NULL ||
      (vm.core.oTypeString = getGlobalClass(S_OTYPE_STRING)) == NULL ||
      (vm.core.oTypeClosure = getGlobalClass(S_OTYPE_CLOSURE)) == NULL ||
      (vm.core.oTypeOverload = getGlobalClass(S_OTYPE_OVERLOAD)) == NULL ||
      (vm.core.oTypeSequence = getGlobalClass(S_OTYPE_SEQUENCE)) == NULL ||

      (vm.core.unify = getGlobalClosure(S_UNIFY)) == NULL)
    return INTERPRET_RUNTIME_ERROR;

  return INTERPRET_OK;
}
