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

bool __keys__(int argCount, Value* args) {
  Value v = vmPeek(0);
  ObjSequence* seq = newSequence();

  if (!IS_OBJ(v)) {
    vmRuntimeError("Only objects and sets have keys.");
    vmPop();  // set.
    vmPop();  // native fn.
    return false;
  }

  switch (OBJ_TYPE(v)) {
    case OBJ_SET: {
      ObjSet* set = AS_SET(v);

      for (int i = 0; i < set->elements.capacity; i++) {
        MapEntry* entry = &set->elements.entries[i];
        if (!IS_UNDEF(entry->key) && !IS_UNDEF(entry->value) &&
            IS_BOOL(entry->value) && AS_BOOL(entry->value))
          writeValueArray(&seq->values, entry->key);
      }
      break;
    }
    default: {
      Obj* obj = AS_OBJ(v);
      for (int i = 0; i < obj->fields.capacity; i++) {
        MapEntry* entry = &obj->fields.entries[i];
        if (!IS_UNDEF(entry->key) && !IS_UNDEF(entry->value))
          writeValueArray(&seq->values, entry->key);
      }
      break;
    }
  }

  vmPop();  // set.
  vmPop();  // native fn.
  vmPush(OBJ_VAL(seq));
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

// Helper function to recursively convert sequences to trees
static Value convertSeqToTree(Value value) {
  if (!IS_SEQUENCE(value)) {
    return value;
  }

  ObjSequence* seq = AS_SEQUENCE(value);
  ObjTree* tree = newTree();

  if (seq->values.count == 0) {
    // Empty sequence becomes tree with nil value and no children
    mapSet(&tree->obj.fields, INTERN("value"), NIL_VAL);
    return OBJ_VAL(tree);
  }

  // First element is the value
  Value treeValue = seq->values.values[0];
  mapSet(&tree->obj.fields, INTERN("value"), treeValue);

  // Remaining elements are children - recursively convert them
  for (int i = 1; i < seq->values.count; i++) {
    Value childValue = convertSeqToTree(seq->values.values[i]);
    writeValueArray(&tree->children, childValue);
  }

  return OBJ_VAL(tree);
}

bool __tree__(int argCount, Value* args) {
  // First argument is the node value
  // Remaining arguments are children

  if (argCount == 0) {
    vmRuntimeError("tree() requires at least one argument (value).");
    return false;
  }

  Value nodeValue = vmPeek(argCount - 1);

  // Check if this is a single-argument call with a sequence
  // If so, treat it as nested sequence conversion
  if (argCount == 1 && IS_SEQUENCE(nodeValue)) {
    Value result = convertSeqToTree(nodeValue);
    vm.stackTop[-argCount - 1] = result;
    for (int i = 0; i < argCount; i++) vmPop();
    return true;
  }

  // Otherwise, create tree directly from arguments
  ObjTree* tree = newTree();
  mapSet(&tree->obj.fields, INTERN("value"), nodeValue);
  vm.stackTop[-argCount - 1] = OBJ_VAL(tree);

  // Add remaining arguments as children, converting sequences recursively
  for (int i = argCount - 2; i >= 0; i--) {
    Value childValue = vmPeek(i);

    // If child is a sequence, convert it recursively
    if (IS_SEQUENCE(childValue)) {
      childValue = convertSeqToTree(childValue);
    }

    writeValueArray(&tree->children, childValue);
  }

  // Pop all arguments
  for (int i = 0; i < argCount; i++) vmPop();

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

bool __eq__(int argCount, Value* args) {
  Value a = vmPop();
  Value b = vmPop();
  vmPop();  // native fn.

  vmPush(BOOL_VAL(valuesEqual(a, b)));
  return true;
}

bool __neq__(int argCount, Value* args) {
  Value a = vmPop();
  Value b = vmPop();
  vmPop();  // native fn.

  vmPush(BOOL_VAL(!valuesEqual(a, b)));
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
      vmRuntimeError("Only objects have length.");
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
  defineNativeFn("keys", 1, false, __keys__, &vm.globals);
  defineNativeFn("set", 0, true, __set__, &vm.globals);
  defineNativeFn("setAdd", 2, false, __setAdd__, &vm.globals);
  defineNativeFn("tree", 0, true, __tree__, &vm.globals);

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