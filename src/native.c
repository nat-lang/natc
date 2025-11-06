#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "core.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

void defineNativeFn(char* name, int arity, bool variadic, NativeFn function,
                    ObjMap* dest) {
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

void defineNativeFnMethod(char* name, int arity, bool variadic,
                          NativeFn function, ObjClass* klass) {
  defineNativeFn(name, arity, variadic, function, &klass->fields);
}

static void defineNativeFnGlobal(char* name, int arity, NativeFn function) {
  defineNativeFn(name, arity, false, function, &vm.globals);
}

static void defineNativeAffixGlobal(char* name, int arity, NativeFn function,
                                    Precedence prec, ObjMap* affixMap) {
  defineNativeFn(name, arity, false, function, &vm.globals);

  Value fn;
  Value fnName = INTERN(name);
  mapGet(&vm.globals, fnName, &fn);
  mapSet(affixMap, fnName, NUMBER_VAL(prec));
}

static void defineNativeInfixGlobal(char* name, NativeFn function,
                                    Precedence prec) {
  defineNativeAffixGlobal(name, 2, function, prec, &vm.infixes);
}

static void defineNativePrefixGlobal(char* name, NativeFn function) {
  defineNativeAffixGlobal(name, 1, function, 1, &vm.prefixes);
}

static ObjClass* defineNativeClass(char* name) {
  ObjString* objName = intern(name);
  vmPush(OBJ_VAL(objName));
  ObjClass* klass = newClass(objName);
  vmPush(OBJ_VAL(klass));
  mapSet(&vm.globals, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();
  return klass;
}

bool __globals__(int argCount, Value* args) {
  vmPop();  // native fn.

  vmPush(OBJ_VAL(vm.core.map));
  vmInitInstance(vm.core.map, 0);
  mapAddAll(&vm.globals, &AS_INSTANCE(vmPeek(0))->fields);
  mapAddAll(&vm.module->namespace, &AS_INSTANCE(vmPeek(0))->fields);

  return true;
}

bool __clock__(int argCount, Value* args) {
  vmPop();  // native fn.
  vmPush(NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
  return true;
}

bool __address__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.
  if (!IS_OBJ(value)) {
    vmPush(NUMBER_VAL(0));

  } else {
    Obj* obj = AS_OBJ(value);
    vmPush((NUMBER_VAL((uintptr_t)obj)));
  }

  return true;
}

bool __assert__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.
  if (!AS_BOOL(value)) {
    vmRuntimeError("Assertion failed.");
    return false;
  }
  vmPush(NIL_VAL);
  return true;
}

bool __ord__(int argCount, Value* args) {
  Value value = vmPop();
  if (!IS_STRING(value) || AS_STRING(value)->length != 1) {
    vmRuntimeError("Expecting string of length 1.");
    return false;
  }

  vmPop();  // fn.
  vmPush(NUMBER_VAL(AS_STRING(value)->chars[0]));
  return true;
}

bool __seq__(int argCount, Value* args) {
  ObjSequence* seq = newSequence();
  vm.stackTop[-argCount - 1] = OBJ_VAL(seq);

  int i = argCount;
  while (i-- > 0) writeValueArray(&seq->values, vmPeek(i));
  while (++i < argCount) vmPop();

  return true;
}

bool __obj__(int argCount, Value* args) {
  ObjMap* map = newMap();
  vm.stackTop[-argCount - 1] = OBJ_VAL(map);

  for (int i = argCount - 1; i >= 1; i -= 2) {
    Value key = vmPeek(i);
    Value value = vmPeek(i - 1);
    mapSet(map, key, value);
  }

  int i = argCount;
  while (i-- > 0) vmPop();

  return true;
}

bool __str__(int argCount, Value* args) {
  Value value = vmPeek(0);
  ObjString* string;

  switch (value.vmType) {
    case VAL_UNIT: {
      string = copyString("()", 2);
      break;
    }
    case VAL_NUMBER: {
      double num = AS_NUMBER(value);

      char buffer[24];
      int length = sprintf(buffer, "%.14g", num);

      string = copyString(buffer, length);
      break;
    }
    case VAL_NIL: {
      string = copyString("nil", 3);
      break;
    }
    case VAL_UNDEF: {
      string = copyString("undefined", 9);
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
        case OBJ_INSTANCE: {
          ObjInstance* instance = AS_INSTANCE(value);
          char buffer[instance->klass->name->length + 9];
          int length =
              sprintf(buffer, "<%s object>", instance->klass->name->chars);

          string = copyString(buffer, length);
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
    default:
      vmRuntimeError("Can't turn value into a string.");
      return false;
  }

  vmPop();
  vmPop();  // native fn.
  vmPush(OBJ_VAL(string));

  return true;
}

bool __valuesEqual__(Value a, Value b);

bool __subMap__(ObjMap* a, ObjMap* b) {
  for (int i = 0; i < a->count; i++) {
    MapEntry* entry = &a->entries[i];
    if (IS_UNDEF(entry->key) || IS_UNDEF(entry->value)) continue;
    Value bValue;
    if (!mapGet(b, entry->key, &bValue)) return false;
    if (!__valuesEqual__(entry->value, bValue)) return false;
  }
  return true;
}

bool __valuesEqual__(Value a, Value b) {
  if (a.vmType != b.vmType) return false;

  switch (a.vmType) {
    case VAL_UNDEF:
    case VAL_UNIT:
    case VAL_NIL:
      return true;
    case VAL_BOOL:
      return AS_BOOL(a) == AS_BOOL(b);
      break;
    case VAL_NUMBER:
      return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
      Obj* aObj = AS_OBJ(a);
      Obj* bObj = AS_OBJ(b);

      if (aObj->oType != bObj->oType) return false;

      switch (aObj->oType) {
        case OBJ_SEQUENCE: {
          ObjSequence* aSeq = AS_SEQUENCE(a);
          ObjSequence* bSeq = AS_SEQUENCE(b);
          if (aSeq->values.count != bSeq->values.count) return false;
          for (int i = 0; i < aSeq->values.count; i++) {
            if (!__valuesEqual__(aSeq->values.values[i],
                                 bSeq->values.values[i]))
              return false;
          }
          return true;
        }
        case OBJ_MAP: {
          ObjMap* aMap = AS_MAP(a);
          ObjMap* bMap = AS_MAP(b);
          return __subMap__(aMap, bMap) && __subMap__(bMap, aMap);
        }
        default:
          return false;
      }
    }
  }
}

bool __eq__(int argCount, Value* args) {
  Value a = vmPop();
  Value b = vmPop();
  vmPop();  // native fn.

  vmPush(BOOL_VAL(__valuesEqual__(a, b)));
  return true;
}

bool __neq__(int argCount, Value* args) {
  Value a = vmPop();
  Value b = vmPop();
  vmPop();  // native fn.

  vmPush(BOOL_VAL(!__valuesEqual__(a, b)));
  return true;
}

#define BINARY_NATIVE(name, valueType, op)                  \
  static bool name(int argCount, Value* args) {             \
    do {                                                    \
      if (!IS_NUMBER(vmPeek(0)) || !IS_NUMBER(vmPeek(1))) { \
        vmRuntimeError("Operands must be numbers.");        \
        return false;                                       \
      }                                                     \
      double b = AS_NUMBER(vmPop());                        \
      double a = AS_NUMBER(vmPop());                        \
      vmPop();                                              \
      vmPush(valueType(a op b));                            \
    } while (false);                                        \
    return true;                                            \
  }

BINARY_NATIVE(__gt__, BOOL_VAL, >)
BINARY_NATIVE(__lt__, BOOL_VAL, <)
BINARY_NATIVE(__gte__, BOOL_VAL, >=)
BINARY_NATIVE(__lte__, BOOL_VAL, <=)

BINARY_NATIVE(__sub__, NUMBER_VAL, -)
BINARY_NATIVE(__div__, NUMBER_VAL, /)
BINARY_NATIVE(__mul__, NUMBER_VAL, *)

bool __print__(int argCount, Value* args) {
  printValue(vmPop());
  vmPop();  // fn.
  printf("\n");
  vmPush(NIL_VAL);
  return true;
}

bool __add__(int argCount, Value* args) {
  if (IS_STRING(vmPeek(0)) && IS_STRING(vmPeek(1))) {
    // can't pop them until after the concatenation,
    // which allocates memory for the new string.
    ObjString* b = AS_STRING(vmPeek(0));
    ObjString* a = AS_STRING(vmPeek(1));

    ObjString* result = concatenateStrings(a, b);
    vmPop();
    vmPop();
    vmPop();  // fn.
    vmPush(OBJ_VAL(result));
  } else if (IS_NUMBER(vmPeek(0)) && IS_NUMBER(vmPeek(1))) {
    double b = AS_NUMBER(vmPop());
    double a = AS_NUMBER(vmPop());
    vmPop();  // fn.
    vmPush(NUMBER_VAL(a + b));
  } else {
    vmRuntimeError("Operands must be two numbers or two strings.");
    return false;
  }
  return true;
}

bool __resolveUpvalue__(int argCount, Value* args) {
  Value value = vmPop();

  if (!IS_UPVALUE(value)) {
    vmRuntimeError("Not an upvalue.");
    return false;
  }

  vmPop();  // fn.
  vmPush(*AS_UPVALUE(value)->location);
  return true;
}

bool __stackTrace__(int argCount, Value* args) {
  vmPop();
  disassembleStack();
  printf("\n");
  return true;
}

bool __annotations__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // fn.

  if (!IS_OBJ(value)) {
    vmRuntimeError("Only objects have annotations.");
    return false;
  }

  Obj* obj = AS_OBJ(value);
  for (int i = 0; i < obj->annotations.count; i++)
    vmPush(obj->annotations.values[i]);
  vmTuplify(obj->annotations.count, true);

  return true;
}

bool __length__(int argCount, Value* args) {
  Value obj = vmPop();
  vmPop();  // native fn.

  if (!IS_OBJ(obj)) {
    vmRuntimeError("Only objects have length.");
    return false;
  }

  switch (OBJ_TYPE(obj)) {
    case OBJ_SEQUENCE: {
      ObjSequence* seq = AS_SEQUENCE(obj);
      vmPush(NUMBER_VAL(seq->values.count));
      return true;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = AS_INSTANCE(obj);
      Value method;
      if (mapGet(&instance->klass->fields, INTERN(S_LEN), &method)) {
        vmPush(OBJ_VAL(instance));  // receiver.
        return vmCallValue(method, 0);
      }

      // otherwise default to the instance's field count.
      vmPush(NUMBER_VAL(instance->fields.count));
      return true;
    }
    case OBJ_STRING: {
      ObjString* string = AS_STRING(obj);
      vmPush(NUMBER_VAL(string->length));
      return true;
    }
    default: {
      vmRuntimeError(
          "Only sequences and objects with a '%s' method have length.", S_LEN);
      return false;
    }
  }
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

bool __hash__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  uint32_t hash;
  if (!vmHashValue(value, &hash)) return false;
  vmPush(NUMBER_VAL(hash));
  return true;
}

bool __vmHashable__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.
  vmPush(BOOL_VAL(vHashable(value)));
  return true;
}

bool __vmType__(int argCount, Value* args) {
  Value value = vmPop();
  vmPop();  // native fn.

  switch (value.vmType) {
    case VAL_UNIT:
      vmPush(value);
      break;
    case VAL_BOOL:
      vmPush(OBJ_VAL(vm.core.vmTypeBool));
      break;
    case VAL_NUMBER:
      vmPush(OBJ_VAL(vm.core.vmTypeNumber));
      break;
    case VAL_NIL:
      vmPush(OBJ_VAL(vm.core.vmTypeNil));
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
        case OBJ_BOUND_FUNCTION: {
          ObjBoundFunction* obj = AS_BOUND_FUNCTION(value);
          if (obj->type == BOUND_NATIVE)
            vmPush(OBJ_VAL(vm.core.oTypeNative));
          else
            vmPush(OBJ_VAL(vm.core.oTypeBoundFunction));
          break;
        }
        case OBJ_CLOSURE:
          vmPush(OBJ_VAL(vm.core.oTypeFunction));
          break;
        case OBJ_NATIVE:
          vmPush(OBJ_VAL(vm.core.oTypeNative));
          break;
        case OBJ_OVERLOAD:
          vmPush(OBJ_VAL(vm.core.oTypeOverload));
          break;
        case OBJ_SEQUENCE:
          vmPush(OBJ_VAL(vm.core.oTypeSequence));
          break;
        default: {
          vmRuntimeError("Unexpected object (type %i).", AS_OBJ(value)->oType);
          return false;
        }
      }
      break;
    case VAL_UNDEF:
      vmPush(OBJ_VAL(vm.core.vmTypeUndef));
      break;
    default:
      vmRuntimeError("Unexpected value.");
      return false;
  }
  return true;
}

bool __compile__(int argCount, Value* args) {
  Value source = vmPeek(0), baseName = vmPeek(1), dirName = vmPeek(2);

  if (!IS_STRING(baseName)) {
    vmRuntimeError("Expecting string for name.");
    return false;
  }
  if (!IS_STRING(dirName)) {
    vmRuntimeError("Expecting string for dir.");
    return false;
  }
  if (!IS_STRING(source)) {
    vmRuntimeError("Expecting string for source.");
    return false;
  }

  ObjString* objDirName = AS_STRING(dirName);
  ObjString* objBaseName = AS_STRING(baseName);
  ObjString* objSource = AS_STRING(source);
  ObjModule* module =
      newModule(objDirName, objBaseName, objSource, MODULE_IMPORT);

  vmPush(OBJ_VAL(module));
  ObjClosure* closure = vmCompileClosure(syntheticToken(objBaseName->chars),
                                         objSource->chars, module);

  if (closure == NULL) {
    vmRuntimeError("Failed to compile module at '%s'.", objBaseName->chars);
    return false;
  }

  module->closure = closure;

  vmPush(OBJ_VAL(vm.core.module));
  if (!vmInitInstance(vm.core.module, 0)) {
    vmRuntimeError("Failed to init module.");
    return false;
  }

  ObjInstance* objModule = AS_INSTANCE(vmPeek(0));

  if (!vmImport(module, &objModule->fields)) {
    vmRuntimeError("Failed to import.");
    return false;
  }

  mapSet(&objModule->fields, OBJ_VAL(vm.core.sModule), OBJ_VAL(module));

  vmPop();  // objModule.
  vmPop();  // module.
  vmPop();  // objSource.
  vmPop();  // objPath.
  vmPop();  // native fn.

  vmPush(OBJ_VAL(objModule));
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

void defineNatives() {
  // native functions.

  defineNativeFnGlobal("assert", 1, __assert__);
  defineNativeFnGlobal("len", 1, __length__);
  defineNativeFn("seq", 0, true, __seq__, &vm.globals);
  defineNativeFn("obj", 0, true, __obj__, &vm.globals);

  defineNativeInfixGlobal("==", __eq__, PREC_COMPARISON);
  defineNativeInfixGlobal("!=", __neq__, PREC_COMPARISON);
  defineNativeInfixGlobal(">", __gt__, PREC_COMPARISON);
  defineNativeInfixGlobal("<", __lt__, PREC_COMPARISON);
  defineNativeInfixGlobal(">=", __gte__, PREC_COMPARISON);
  defineNativeInfixGlobal("<=", __lte__, PREC_COMPARISON);
  defineNativeInfixGlobal("+", __add__, PREC_TERM);
  defineNativeInfixGlobal("-", __sub__, PREC_TERM);
  defineNativeInfixGlobal("/", __div__, PREC_FACTOR);
  defineNativeInfixGlobal("*", __mul__, PREC_FACTOR);

  defineNativePrefixGlobal("__print__", __print__);

  //

  defineNativeFnGlobal("str", 1, __str__);
  defineNativeFnGlobal("ord", 1, __ord__);
  defineNativeFnGlobal("hash", 1, __hash__);
  defineNativeFnGlobal("vmHashable", 1, __vmHashable__);
  defineNativeFnGlobal("vmType", 1, __vmType__);
  defineNativeFnGlobal("globals", 0, __globals__);
  defineNativeFnGlobal("clock", 0, __clock__);
  defineNativeFnGlobal("random", 1, __randomNumber__);
  defineNativeFnGlobal("resolveUpvalue", 1, __resolveUpvalue__);
  defineNativeFnGlobal("stackTrace", 0, __stackTrace__);
  defineNativeFnGlobal("address", 1, __address__);
  defineNativeFnGlobal("annotations", 1, __annotations__);
  defineNativeFnGlobal("compile", 3, __compile__);

  // native classes.

  vm.core.base = defineNativeClass(S_BASE);
  defineNativeFnMethod("keys", 0, false, __objKeys__, vm.core.base);
}