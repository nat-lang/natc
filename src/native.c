#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "debug.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

void defineNativeFn(char* name, int arity, bool variadic, NativeFn function,
                    Map* dest) {
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

static void defineNativeFnGlobal(char* name, int arity, NativeFn function) {
  defineNativeFn(name, arity, false, function, &vm.globals);
}

static void defineNativeAffixGlobal(char* name, int arity, NativeFn function,
                                    Precedence prec, Map* affixMap) {
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

bool __seqPush__(int argCount, Value* args) {
  if (!IS_SEQUENCE(vmPeek(1))) {
    vmRuntimeError("Expecting sequence.");
    return false;
  }
  ObjSequence* seq = AS_SEQUENCE(vmPeek(1));
  writeValueArray(&seq->values, vmPeek(0));
  vmPop();  // value.
  vmPop();  // sequence.
  vmPop();  // native fn.
  vmPush(NIL_VAL);
  return true;
}

bool __set__(int argCount, Value* args) {
  ObjSet* set = newSet();
  vm.stackTop[-argCount - 1] = OBJ_VAL(set);

  int i = argCount;
  while (i-- > 0) {
    Value element = vmPeek(i);
    mapSet(&set->elements, element, BOOL_VAL(true));
  }

  while (++i < argCount) vmPop();

  return true;
}

bool __setAdd__(int argCount, Value* args) {
  if (!IS_SET(vmPeek(1))) {
    vmRuntimeError("Expecting set.");
    return false;
  }

  ObjSet* set = AS_SET(vmPeek(1));
  mapSet(&set->elements, vmPeek(0), BOOL_VAL(true));
  vmPop();  // value.
  vmPop();  // set.
  vmPop();  // native fn.
  vmPush(NIL_VAL);
  return true;
}

bool __obj__(int argCount, Value* args) {
  ObjMap* map = newMap();
  vm.stackTop[-argCount - 1] = OBJ_VAL(map);

  for (int i = argCount - 1; i >= 1; i -= 2) {
    Value key = vmPeek(i);
    Value value = vmPeek(i - 1);
    mapSet(&map->obj.fields, key, value);
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
  }

  vmPop();
  vmPop();  // native fn.
  vmPush(OBJ_VAL(string));

  return true;
}

bool __valuesEqual__(Value a, Value b);

bool __subMap__(Map* a, Map* b) {
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
          return __subMap__(&aMap->obj.fields, &bMap->obj.fields) &&
                 __subMap__(&bMap->obj.fields, &aMap->obj.fields);
        }
        case OBJ_SET: {
          ObjSet* aSet = AS_SET(a);
          ObjSet* bSet = AS_SET(b);
          return __subMap__(&aSet->elements, &bSet->elements) &&
                 __subMap__(&bSet->elements, &aSet->elements);
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

void defineNatives() {
  // native functions.

  defineNativeFnGlobal("assert", 1, __assert__);
  defineNativeFnGlobal("len", 1, __length__);

  defineNativeFn("seq", 0, true, __seq__, &vm.globals);
  defineNativeFn("seqPush", 2, false, __seqPush__, &vm.globals);
  defineNativeFn("set", 0, true, __set__, &vm.globals);
  defineNativeFn("setAdd", 2, false, __setAdd__, &vm.globals);

  defineNativeFn("obj", 0, true, __obj__, &vm.globals);
  defineNativeFnGlobal("str", 1, __str__);

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

  defineNativePrefixGlobal("print", __print__);

  //

  defineNativeFnGlobal("ord", 1, __ord__);
  defineNativeFnGlobal("hash", 1, __hash__);
  defineNativeFnGlobal("vmHashable", 1, __vmHashable__);
  defineNativeFnGlobal("clock", 0, __clock__);
  defineNativeFnGlobal("random", 1, __randomNumber__);
  defineNativeFnGlobal("resolveUpvalue", 1, __resolveUpvalue__);
  defineNativeFnGlobal("stackTrace", 0, __stackTrace__);
  defineNativeFnGlobal("address", 1, __address__);
}