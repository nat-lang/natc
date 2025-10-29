#include "core.h"

#include <string.h>

#include "compiler.h"
#include "debug.h"
#include "io.h"
#include "native.h"

static void defineInstanceProperty(char* name, ObjInstance* instance,
                                   Value property) {
  ObjString* objName = intern(name);
  vmPush(OBJ_VAL(objName));
  vmPush(property);
  mapSet(&instance->fields, vmPeek(1), vmPeek(0));
  vmPop();
  vmPop();
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
                   obj->vmType);
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
  if (AS_SEQUENCE(seq)->values.count == 0) {
    vmRuntimeError("Can't pop from sequence with length 0.");
    return false;
  }
  Value value = popValueArray(&AS_SEQUENCE(seq)->values);

  vmPop();
  vmPush(value);
  return true;
}

bool __tupleInit__(int argCount, Value* args) {
  __sequentialInit__(argCount);

  return true;
}

bool __moduleImport__(int argCount, Value* args) {
  ObjInstance* obj = AS_INSTANCE(vmPeek(0));

  Value module;
  if (!mapGet(&obj->fields, OBJ_VAL(vm.core.sModule), &module)) {
    vmRuntimeError("Module instance missing its module field!");
    return false;
  }

  if (!IS_MODULE(module)) {
    vmRuntimeError("Module.module is not a module!");
    return false;
  }

  ObjMap* target = vm.module->type == MODULE_ENTRYPOINT ? &vm.globals
                                                        : &vm.module->namespace;
  mapAddAll(&AS_MODULE(module)->namespace, target);
  vmPop();
  vmPush(NIL_VAL);
  return true;
}

InterpretResult loadCore() {
  // core classes.

  InterpretResult coreIntpt = vmInterpretEntrypoint(NAT_CORE_LOC);
  if (coreIntpt != INTERPRET_OK) return coreIntpt;

  if ((vm.core.object = getGlobalClass(S_OBJECT)) == NULL) return false;

  if ((vm.core.tuple = getGlobalClass(S_TUPLE)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 0, true, __tupleInit__, vm.core.tuple);

  if ((vm.core.sequence = getGlobalClass(S_SEQUENCE)) == NULL) return false;
  defineNativeFnMethod(S_INIT, 0, true, __sequenceInit__, vm.core.sequence);
  defineNativeFnMethod(S_PUSH, 1, false, __sequencePush__, vm.core.sequence);
  defineNativeFnMethod(S_ADD, 1, false, __sequencePush__, vm.core.sequence);
  defineNativeFnMethod(S_POP, 0, false, __sequencePop__, vm.core.sequence);

  if ((vm.core.generator = getGlobalClass(S_GENERATOR)) == NULL) return false;

  if ((vm.core.module = getGlobalClass(S_MODULE)) == NULL) return false;
  defineNativeFnMethod("__import__", 0, false, __moduleImport__,
                       vm.core.module);

  if ((vm.core.map = getGlobalClass(S_MAP)) == NULL ||
      (vm.core.set = getGlobalClass(S_SET)) == NULL ||
      (vm.core.astClosure = getGlobalClass(S_AST_CLOSURE)) == NULL ||
      (vm.core.astComprehension = getGlobalClass(S_AST_COMPREHENSION)) ==
          NULL ||
      (vm.core.astClassMethod = getGlobalClass(S_AST_CLASS_METHOD)) == NULL ||
      (vm.core.astMethod = getGlobalClass(S_AST_METHOD)) == NULL ||
      (vm.core.astExternalUpvalue = getGlobalClass(S_AST_EXTERNAL_UPVALUE)) ==
          NULL ||
      (vm.core.astInternalUpvalue = getGlobalClass(S_AST_INTERNAL_UPVALUE)) ==
          NULL ||
      (vm.core.astLocal = getGlobalClass(S_AST_LOCAL)) == NULL ||
      (vm.core.astGlobal = getGlobalClass(S_AST_GLOBAL)) == NULL ||
      (vm.core.astOverload = getGlobalClass(S_AST_OVERLOAD)) == NULL ||
      (vm.core.astMembership = getGlobalClass(S_AST_MEMBERSHIP)) == NULL ||
      (vm.core.astBlock = getGlobalClass(S_AST_BLOCK)) == NULL ||
      (vm.core.astQuantification = getGlobalClass(S_AST_QUANTIFICATION)) ==
          NULL ||
      (vm.core.vmTypeUnit = getGlobalClass(S_CTYPE_UNIT)) == NULL ||
      (vm.core.vmTypeBool = getGlobalClass(S_CTYPE_BOOL)) == NULL ||
      (vm.core.vmTypeNil = getGlobalClass(S_CTYPE_NIL)) == NULL ||
      (vm.core.vmTypeNumber = getGlobalClass(S_CTYPE_NUMBER)) == NULL ||
      (vm.core.vmTypeUndef = getGlobalClass(S_CTYPE_UNDEF)) == NULL ||
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

  InterpretResult systemIntpt = vmInterpretEntrypoint(NAT_SYSTEM_LOC);
  if (systemIntpt != INTERPRET_OK) return systemIntpt;

  if ((vm.core.unify = getGlobalClosure(S_UNIFY)) == NULL ||
      (vm.core.typeSystem = getGlobalInstance(S_TYPE_SYSTEM)) == NULL)
    return INTERPRET_RUNTIME_ERROR;

  ObjInstance* strings = getGlobalInstance("Strings");
  if (strings == NULL) return INTERPRET_RUNTIME_ERROR;

  defineInstanceProperty("quote", strings, OBJ_VAL(vm.core.sQuote));
  defineInstanceProperty("backslash", strings, OBJ_VAL(vm.core.sBackslash));

  return INTERPRET_OK;
}
