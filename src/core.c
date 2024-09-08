#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
#include "debug.h"
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

bool getGlobalObj(char* name, Value* obj, ObjType type) {
  if (!mapGet(&vm.globals, INTERN(name), obj)) {
    vmRuntimeError("Couldn't find global '%s'.", name);
    return NULL;
  }

  if (!IS_OBJ(*obj)) {
    vmRuntimeError("Global is not an object.");
    return false;
  }

  if (OBJ_TYPE(*obj) != type) {
    vmRuntimeError("Global has wrong type. Expected '%i' but got '%i.", type,
                   obj->vType);
    return false;
  }

  return true;
}

ObjClass* getGlobalClass(char* name) {
  Value obj;
  if (getGlobalObj(name, &obj, OBJ_CLASS)) return AS_CLASS(obj);
  return NULL;
}

ObjInstance* getGlobalInstance(char* name) {
  Value obj;
  if (getGlobalObj(name, &obj, OBJ_INSTANCE)) return AS_INSTANCE(obj);
  return NULL;
}

ObjClosure* getGlobalClosure(char* name) {
  Value obj;
  if (getGlobalObj(name, &obj, OBJ_CLOSURE)) return AS_CLOSURE(obj);
  return NULL;
}

ObjSequence* __sequentialInit__(int argCount) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(argCount));

  ObjSequence* seq = newSequence();
  vmPush(OBJ_VAL(seq));
  mapSet(&obj->fields, OBJ_VAL(vm.core.sValues), vmPeek(0));
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

bool __stringInit__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(1));

  disassembleStack();
  printf("\n");
  if (!IS_STRING(vmPeek(0))) {
    vmRuntimeError("Expecting string.");
    return false;
  }

  mapSet(&obj->fields, OBJ_VAL(vm.core.sValues), vmPeek(0));
  vmPop();

  return true;
}

bool __stringEq__(int argCount, Value* args) {
  Value a = vmPeek(0);
  Value b = vmPeek(1);

  Value aString;
  if (!vmStringValueField(a, &aString)) return false;
  Value bString;
  if (!vmStringValueField(b, &bString)) return false;

  vmPop();
  vmPop();
  bool eq = objectsEqual((Obj*)AS_STRING(aString), (Obj*)AS_STRING(bString));
  vmPush(BOOL_VAL(eq));

  return true;
}

bool __stringHash__(int argCount, Value* args) {
  Value obj = vmPeek(0);

  Value vString;
  if (!vmStringValueField(obj, &vString)) return false;
  vmPush(NUMBER_VAL(AS_STRING(vString)->hash));

  return true;
}

bool __stringSeq__(int argCount, Value* args) {
  Value obj = vmPeek(0);

  Value vString;
  if (!vmStringValueField(obj, &vString)) return false;
  ObjString* string = AS_STRING(vString);

  for (int i = 0; i < string->length; i++) {
    ObjString* character = copyString(string->chars + i, 1);
    vmPush(OBJ_VAL(vm.core.string));
    vmPush(OBJ_VAL(character));
    if (!vmInitInstance(vm.core.string, 1)) return false;
  }

  if (!vmTuplify(string->length, true)) return false;

  Value seq = vmPop();
  vmPop();
  vmPush(seq);

  return true;
}

bool __stringSet__(int argCount, Value* args) {
  Value obj = vmPeek(2);
  Value key = vmPeek(1);
  Value val = vmPeek(0);

  Value objString;
  if (!vmStringValueField(obj, &objString)) return false;
  Value valString;
  if (!vmStringValueField(val, &valString)) return false;

  if (AS_STRING(valString)->length != 1) {
    vmRuntimeError("Expecting single character.");
    return false;
  }

  if (!IS_NUMBER(key)) {
    vmRuntimeError("String index must be number.");
    return false;
  }

  char* objChars = AS_STRING(objString)->chars;
  char* valChars = AS_STRING(valString)->chars;
  int idx = AS_NUMBER(key);

  if (idx >= AS_STRING(objString)->length) {
    vmRuntimeError("String index out of bounds.");
    return false;
  }

  *(objChars + idx) = *valChars;

  vmPop();  // val.
  vmPop();  // key.
  vmPop();  // obj.

  vmPush(NIL_VAL);
  return true;
}

bool __stringGet__(int argCount, Value* args) {
  Value obj = vmPeek(1);
  Value key = vmPeek(0);

  Value objString;
  if (!vmStringValueField(obj, &objString)) return false;
  if (!IS_NUMBER(key)) {
    vmRuntimeError("String index must be number.");
    return false;
  }

  char* objChars = AS_STRING(objString)->chars;
  int idx = AS_NUMBER(key);

  if (idx >= AS_STRING(objString)->length) {
    vmRuntimeError("String index out of bounds.");
    return false;
  }

  ObjString* objChar = copyString(objChars + idx, 1);

  vmPop();  // key.
  vmPop();  // obj.

  vmPush(OBJ_VAL(vm.core.string));
  vmPush(OBJ_VAL(objChar));
  return vmInitInstance(vm.core.string, 1);
}

bool __stringLength__(int argCount, Value* args) {
  Value obj = vmPeek(0);

  Value string;
  if (!vmStringValueField(obj, &string)) return false;

  vmPop();
  vmPush(NUMBER_VAL(AS_STRING(string)->length));

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
    if (IS_STRING(entry->key)) {
      vmPush(OBJ_VAL(vm.core.string));
      vmPush(entry->key);
      if (!vmInitInstance(vm.core.string, 1)) return false;
    } else {
      vmPush(entry->key);
    }
    if (!vmInvoke(intern("push"), 1)) return false;
  }

  Value keys = vmPop();
  vmPop();  // obj.
  vmPush(keys);

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
  vmInitInstance(vm.core.map, 0);
  mapAddAll(&vm.globals, &AS_INSTANCE(vmPeek(0))->fields);

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
    default: {
      vmRuntimeError("Can't turn value into a string.");
      return false;
    }
  }

  vmPop();
  vmPop();  // native fn.
  vmPush(OBJ_VAL(vm.core.string));
  vmPush(OBJ_VAL(string));

  return vmInitInstance(vm.core.string, 1);
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

InterpretResult initializeCore() {
  // native functions.

  defineNativeFnGlobal("len", 1, __length__);
  defineNativeFnGlobal("str", 1, __str__);
  defineNativeFnGlobal("hash", 1, __hash__);
  defineNativeFnGlobal("hashable", 1, __hashable__);
  defineNativeFnGlobal("vType", 1, __vType__);
  defineNativeFnGlobal("globals", 0, __globals__);
  defineNativeFnGlobal("clock", 0, __clock__);
  defineNativeFnGlobal("random", 1, __randomNumber__);
  defineNativeFnGlobal("resolveUpvalue", 1, __resolveUpvalue__);
  defineNativeFnGlobal("stackTrace", 0, __stackTrace__);
  defineNativeFnGlobal("address", 1, __address__);
  defineNativeFnGlobal("annotations", 1, __annotations__);

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
  defineNativeFnMethod("keys", 0, false, __objKeys__, vm.core.base);

  // core classes.

  InterpretResult coreIntpt = vmInterpretModule(NAT_CORE_LOC);
  if (coreIntpt != INTERPRET_OK) return coreIntpt;

  if ((vm.core.object = getGlobalClass(S_OBJECT)) == NULL) return false;

  if ((vm.core.tuple = getGlobalClass(S_TUPLE)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 0, true, __tupleInit__, vm.core.tuple);

  if ((vm.core.sequence = getGlobalClass(S_SEQUENCE)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 0, true, __sequenceInit__, vm.core.sequence);
  defineNativeFnMethod(S_PUSH, 1, false, __sequencePush__, vm.core.sequence);
  defineNativeFnMethod(S_ADD, 1, false, __sequencePush__, vm.core.sequence);
  defineNativeFnMethod(S_POP, 0, false, __sequencePop__, vm.core.sequence);

  if ((vm.core.string = getGlobalClass(S_STRING)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 1, false, __stringInit__, vm.core.string);
  defineNativeFnMethod(S_EQ, 1, false, __stringEq__, vm.core.string);
  defineNativeFnMethod(S_LEN, 0, false, __stringLength__, vm.core.string);
  defineNativeFnMethod(S_SEQ, 0, false, __stringSeq__, vm.core.string);
  defineNativeFnMethod(S_HASH, 0, false, __stringHash__, vm.core.string);
  defineNativeFnMethod(S_SUBSCRIPT_GET, 1, false, __stringGet__,
                       vm.core.string);
  defineNativeFnMethod(S_SUBSCRIPT_SET, 2, false, __stringSet__,
                       vm.core.string);

  if ((vm.core.map = getGlobalClass(S_MAP)) == NULL ||
      (vm.core.set = getGlobalClass(S_SET)) == NULL ||
      (vm.core.astClosure = getGlobalClass(S_AST_CLOSURE)) == NULL ||
      (vm.core.astMethod = getGlobalClass(S_AST_METHOD)) == NULL ||
      (vm.core.astExternalUpvalue = getGlobalClass(S_AST_EXTERNAL_UPVALUE)) ==
          NULL ||
      (vm.core.astInternalUpvalue = getGlobalClass(S_AST_INTERNAL_UPVALUE)) ==
          NULL ||
      (vm.core.astLocal = getGlobalClass(S_AST_LOCAL)) == NULL ||
      (vm.core.astOverload = getGlobalClass(S_AST_OVERLOAD)) == NULL ||
      (vm.core.astMembership = getGlobalClass(S_AST_MEMBERSHIP)) == NULL ||
      (vm.core.astBlock = getGlobalClass(S_AST_BLOCK)) == NULL ||
      (vm.core.vTypeUnit = getGlobalClass(S_CTYPE_UNIT)) == NULL ||
      (vm.core.vTypeBool = getGlobalClass(S_CTYPE_BOOL)) == NULL ||
      (vm.core.vTypeNil = getGlobalClass(S_CTYPE_NIL)) == NULL ||
      (vm.core.vTypeNumber = getGlobalClass(S_CTYPE_NUMBER)) == NULL ||
      (vm.core.vTypeUndef = getGlobalClass(S_CTYPE_UNDEF)) == NULL ||
      (vm.core.oTypeVariable = getGlobalClass(S_OTYPE_VARIABLE)) == NULL ||
      (vm.core.oTypeClass = getGlobalClass(S_OTYPE_CLASS)) == NULL ||
      (vm.core.oTypeInstance = getGlobalClass(S_OTYPE_INSTANCE)) == NULL ||
      (vm.core.oTypeString = getGlobalClass(S_OTYPE_STRING)) == NULL ||
      (vm.core.oTypeNative = getGlobalClass(S_OTYPE_NATIVE)) == NULL ||
      (vm.core.oTypeFunction = getGlobalClass(S_OTYPE_FUNCTION)) == NULL ||
      (vm.core.oTypeBoundFunction = getGlobalClass(S_OTYPE_BOUND_FUNCTION)) ==
          NULL ||
      (vm.core.oTypeOverload = getGlobalClass(S_OTYPE_OVERLOAD)) == NULL ||
      (vm.core.oTypeSequence = getGlobalClass(S_OTYPE_SEQUENCE)) == NULL)
    return INTERPRET_RUNTIME_ERROR;

  // system objects and functions.

  InterpretResult systemIntpt = vmInterpretModule(NAT_SYSTEM_LOC);
  if (systemIntpt != INTERPRET_OK) return systemIntpt;

  if ((vm.core.unify = getGlobalClosure(S_UNIFY)) == NULL ||
      (vm.core.typeSystem = getGlobalInstance(S_TYPE_SYSTEM)) == NULL ||
      (vm.core.grammar = getGlobalInstance(S_GRAMMAR)) == NULL)
    return INTERPRET_RUNTIME_ERROR;

  return INTERPRET_OK;
}
