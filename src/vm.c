#include "vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "common.h"
#include "compiler.h"
#include "core.h"
#include "debug.h"
#include "io.h"
#include "memory.h"
#include "object.h"

VM vm;

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

void vmRuntimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s\n", function->name->chars);
    }
  }

  resetStack();
}

void initCore(Core* core) {
  core->base = NULL;
  core->object = NULL;
  core->tuple = NULL;
  core->sequence = NULL;
  core->map = NULL;
  core->set = NULL;
  core->iterator = NULL;
  core->astClosure = NULL;
  core->astUpvalue = NULL;
  core->astSignature = NULL;
  core->astParameter = NULL;
  core->astOverload = NULL;
  core->astVariable = NULL;
  core->vTypeBool = NULL;
  core->vTypeNil = NULL;
  core->vTypeNumber = NULL;
  core->vTypeUndef = NULL;
  core->oTypeClass = NULL;
  core->oTypeInstance = NULL;
  core->oTypeString = NULL;
  core->oTypeClosure = NULL;
  core->oTypeOverload = NULL;
  core->oTypeSequence = NULL;
  core->unify = NULL;
}

bool initVM() {
  resetStack();
  vm.objects = NULL;

  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  vm.compiler = NULL;

  initMap(&vm.globals);
  initMap(&vm.typeEnv);
  initMap(&vm.strings);
  initMap(&vm.infixes);

  initCore(&vm.core);

  return initializeCore() == INTERPRET_OK;
}

void freeVM() {
  freeMap(&vm.globals);
  freeMap(&vm.strings);

  initCore(&vm.core);

  freeObjects();
}

void vmPush(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value vmPop() {
  vm.stackTop--;
  return *vm.stackTop;
}

Value vmPeek(int distance) { return vm.stackTop[-1 - distance]; }

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
  Value method;
  if (!mapGet(&klass->fields, OBJ_VAL(name), &method)) {
    vmRuntimeError("Undefined property '%s' for class '%s'.", name->chars,
                   klass->name->chars);
    return false;
  }

  return vmCallValue(method, argCount);
}

bool vmInvoke(ObjString* name, int argCount) {
  Value receiver = vmPeek(argCount);

  if (IS_INSTANCE(receiver)) {
    ObjInstance* instance = AS_INSTANCE(receiver);
    Value field;
    if (mapGet(&instance->fields, OBJ_VAL(name), &field)) {
      vm.stackTop[-argCount - 1] = receiver;
      return vmCallValue(field, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
  }

  ObjMap* fields;
  switch (OBJ_TYPE(receiver)) {
    case OBJ_CLASS:
      fields = &AS_CLASS(receiver)->fields;
      break;
    case OBJ_CLOSURE:
      fields = &AS_CLOSURE(receiver)->function->fields;
      break;
    default:
      vmRuntimeError("Only instances and classes have methods.");
      return false;
  }

  Value field = NIL_VAL;
  mapGet(fields, OBJ_VAL(name), &field);
  vm.stackTop[-argCount - 1] = receiver;
  return vmCallValue(field, argCount);
}

bool vmExecuteMethod(char* method, int argCount, int frames) {
  return (vmInvoke(intern(method), argCount)) &&
         (vmExecute(vm.frameCount - frames) == INTERPRET_OK);
}

// If [value] is natively hashable, then hash it. Otherwise, if it's
// an instance and it has a hash function, then call the function.
bool vmHashValue(Value value, uint32_t* hash) {
  if (isHashable(value)) {
    *hash = hashValue(value);
    return true;
  }

  if (!IS_INSTANCE(value)) {
    vmRuntimeError(
        "Not a hashable type: num, nil, bool, string, or instance with '%s' "
        "method.",
        S_HASH);
    return false;
  }

  vmPush(value);
  if (!vmExecuteMethod(S_HASH, 0, 1)) return false;

  if (!IS_NUMBER(vmPeek(0)) || AS_NUMBER(vmPeek(0)) < 0) {
    vmRuntimeError("'%s' function must return a natural number.", S_HASH);
    return false;
  }

  *hash = AS_NUMBER(vmPop());
  AS_INSTANCE(value)->obj.hash = *hash;

  return true;
}

static bool vmExtendClass(ObjClass* subclass, ObjClass* superclass) {
  mapAddAll(&superclass->fields, &subclass->fields);
  mapSet(&subclass->fields, INTERN(S_SUPERCLASS), OBJ_VAL(superclass));
  subclass->super = superclass;
  return true;
}

static bool vmInstantiateClass(ObjClass* klass, int argCount) {
  Value initializer;

  mapSet(&AS_INSTANCE(vmPeek(argCount))->fields, OBJ_VAL(intern(S_CLASS)),
         OBJ_VAL(klass));

  if (mapGet(&klass->fields, OBJ_VAL(intern(S_INIT)), &initializer)) {
    return vmCallValue(initializer, argCount);
  } else if (argCount != 0) {
    vmRuntimeError("Expected 0 arguments but got %d.", argCount);
    return false;
  }
  return true;
}

bool vmInitInstance(ObjClass* klass, int argCount, int frames) {
  if (!vmCallValue(OBJ_VAL(klass), argCount)) return false;

  if (mapHas(&klass->fields, INTERN(S_INIT))) {
    if (vmExecute(vm.frameCount - frames) != INTERPRET_OK) return false;
  }

  return true;
}

static bool checkArity(int paramCount, int argCount) {
  if (argCount == paramCount) return true;

  vmRuntimeError("Expected %d arguments but got %d.", paramCount, argCount);
  return false;
}

// Walk the frame delimited by [argCount] and expand any
// [ObjSpread]s. (Note: maybe we could devise a way to do
// this without shuffling the whole frame of arguments for
// every function call.)
static bool spread(int* argCount) {
  Value args[255];
  int newArgCount = 0;

  for (int i = 0; i < *argCount; i++) {
    Value arg = vmPop();

    if (IS_SPREAD(arg)) {
      Value vSeq;
      if (!vmSequenceValueField(AS_INSTANCE(AS_SPREAD(arg)->value), &vSeq))
        return false;

      ObjSequence* seq = AS_SEQUENCE(vSeq);
      for (int j = seq->values.count - 1; j >= 0; j--) {
        args[newArgCount++] = seq->values.values[j];
      }
    } else {
      args[newArgCount++] = arg;
    }
  }

  *argCount = newArgCount;
  while (newArgCount > 0) vmPush(args[--newArgCount]);

  return true;
}

// Collapse [argCount] - [arity] + 1 arguments into a final
// [Sequence] argument.
static bool variadify(ObjClosure* closure, int* argCount) {
  // put a sequence on the stack.
  vmPush(OBJ_VAL(newInstance(vm.core.sequence)));
  vmInstantiateClass(vm.core.sequence, 0);

  // either the function was called (a) with arity - 1 arguments
  // or (b) with arity - n arguments for n > 1. (a) is valid;
  // *args is just an empty sequence. (b) is invalid and will be
  // picked up by the arity check downstream.
  if (*argCount < closure->function->arity) {
    *argCount = *argCount + 1;
    return true;
  }

  // walk the variadic arguments in the order they were
  // applied, peeking at and adding each to the sequence.
  int i = *argCount - closure->function->arity;
  while (i >= 0) {
    Value seq = vmPop();
    Value arg = vmPeek(i);

    vmPush(seq);
    vmPush(arg);

    if (!vmInvoke(intern(S_PUSH), 1)) return false;
    i--;
  }

  // now pop the sequence, all the variadic arguments,
  // and leave the sequence on the stack in their place.
  Value seq = vmPop();
  i = *argCount - closure->function->arity;
  while (i >= 0) {
    vmPop();
    i--;
  }
  vmPush(seq);

  // what we've done is made these two equal.
  *argCount = closure->function->arity;

  return true;
}

CallFrame* vmInitFrame(ObjClosure* closure, int offset) {
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - offset;
  return frame;
}

static bool call(ObjClosure* closure, int argCount) {
  if (!spread(&argCount)) return false;

  if (closure->function->variadic)
    if (!variadify(closure, &argCount)) return false;

  if (!checkArity(closure->function->arity, argCount)) return false;

  if (vm.frameCount == FRAMES_MAX) {
    vmRuntimeError("Stack overflow.");
    return false;
  }

  vmInitFrame(closure, argCount + 1);

  return true;
}

static bool callNative(ObjNative* native, int argCount) {
  if (!spread(&argCount)) return false;

  if (!native->variadic && !checkArity(native->arity, argCount)) return false;

  return (native->function)(argCount, vm.stackTop - argCount);
}

// Wrap the top [count] values in a tuple and put it
// on the stack, optionally leaving the values below it.
bool vmTuplify(int count, bool replace) {
  int i = count;
  Value args[count];

  if (replace)
    while (i--) args[i] = vmPop();
  else
    while (i--) args[count - i - 1] = vmPeek(i);

  vmPush(OBJ_VAL(vm.core.tuple));
  while (++i < count) vmPush(args[i]);

  return vmCallValue(OBJ_VAL(vm.core.tuple), count);
}

bool pushPatternElement(PatternElement element) {
  vmPush(element.value);
  vmPush(element.type);
  if (!vmTuplify(2, true) && vmExecute(vm.frameCount - 1) == INTERPRET_OK)
    return false;
  return true;
}

bool unify(ObjPattern* pattern, Value value) {
  Value unifyFn = OBJ_VAL(vm.core.unify);

  vmPush(unifyFn);

  for (int i = 0; i < pattern->count; i++)
    pushPatternElement(pattern->elements[i]);

  if (pattern->count > 0) vmTuplify(pattern->count, true);

  vmPush(value);

  return vmCallValue(unifyFn, 2) &&
         vmExecute(vm.frameCount - 1) == INTERPRET_OK;
}

static bool callCases(ObjClosure** cases, int caseCount, int argCount) {
  if (!vmTuplify(argCount, false)) return false;

  Value scrutinee = vmPeek(0);

  for (int i = 0; i < caseCount; i++) {
    if (!unify(cases[i]->function->pattern, scrutinee)) return false;

    if (AS_BOOL(vmPop())) {
      vmPop();  // the tuplified scrutinee.
      return call(cases[i], argCount);
    }
  }

  // no match: replace the arguments and the case object with undef.
  vmPop();                     // tuplified scrutinee.
  while (argCount--) vmPop();  // args.
  vmPop();                     // case.
  vmPush(UNDEF_VAL);

  return true;
}

bool vmCallValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_BOUND_FUNCTION: {
        ObjBoundFunction* obj = AS_BOUND_FUNCTION(callee);
        vm.stackTop[-argCount - 1] = obj->receiver;
        switch (BOUND_FUNCTION_TYPE(callee)) {
          case BOUND_METHOD:
            return call(obj->bound.method, argCount);
          case BOUND_NATIVE:
            return callNative(obj->bound.native, argCount);
          default:
            vmRuntimeError("Unexpected bound function type");
            return false;
        }
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
        return vmInstantiateClass(klass, argCount);
      }
      case OBJ_CLOSURE: {
        ObjClosure* closure = AS_CLOSURE(callee);
        ObjPattern* pattern = closure->function->pattern;

        if (pattern != NULL && pattern->isLiteral)
          return callCases(&closure, 1, argCount);
        return call(AS_CLOSURE(callee), argCount);
      }
      case OBJ_OVERLOAD: {
        ObjOverload* overload = AS_OVERLOAD(callee);
        return callCases(overload->closures, overload->cases, argCount);
      }
      case OBJ_NATIVE:
        return callNative(AS_NATIVE(callee), argCount);
      case OBJ_INSTANCE: {
        ObjInstance* instance = AS_INSTANCE(callee);
        Value callFn;
        if (mapGet(&instance->klass->fields, INTERN(S_CALL), &callFn)) {
          return call(AS_CLOSURE(callFn), argCount);
        } else {
          vmRuntimeError("Objects require a '%s' method to be called.", S_CALL);
          return false;
        }
        return true;
      }
      default:
        break;  // Non-callable object type.
    }
  }

  vmRuntimeError(
      "Can only call functions, classes, and objects with a 'call' method.");
  return false;
}

static void bindClosure(Value receiver, Value* value) {
  if (IS_CLOSURE(*value)) {
    ObjBoundFunction* obj = newBoundMethod(receiver, AS_CLOSURE(*value));
    *value = OBJ_VAL(obj);
  } else if (IS_NATIVE(*value)) {
    ObjBoundFunction* obj = newBoundNative(receiver, AS_NATIVE(*value));
    *value = OBJ_VAL(obj);
  }
}

static ObjUpvalue* captureUpvalue(Value* local, uint8_t slot) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local, slot);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

void vmCaptureUpvalues(ObjClosure* closure, CallFrame* frame) {
  for (int i = 0; i < closure->upvalueCount; i++) {
    uint8_t isLocal = READ_BYTE();
    uint8_t index = READ_BYTE();
    if (isLocal) {
      closure->upvalues[i] = captureUpvalue(frame->slots + index, index);
    } else {
      closure->upvalues[i] = frame->closure->upvalues[index];
    }
  }
}

void vmCloseUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

void vmSequence(CallFrame* frame) {
  int count = READ_BYTE();

  ObjSequence* seq = newSequence();
  vmPush(OBJ_VAL(seq));

  for (int i = 0; i < count; i++)
    writeValueArray(&seq->values, vmPeek(count - i));

  vmPop();  // seq.
  while (count--) vmPop();
  vmPush(OBJ_VAL(seq));
}

void vmVariable(CallFrame* frame) {
  ObjString* name = READ_STRING();
  ObjVariable* var = newVariable(name);
  vmPush(OBJ_VAL(var));
}

void vmPattern(CallFrame* frame) {
  int count = READ_BYTE();
  ObjPattern* pattern = newPattern(count);

  int i = count;
  while (i > 0) {
    Value value = vmPeek(i * 2 - 1);
    Value type = vmPeek(i * 2 - 2);
    pattern->elements[count - i].value = value;
    pattern->elements[count - i].type = type;

    if (!IS_VARIABLE(value)) pattern->isLiteral = true;
    i--;
  }

  for (int i = 0; i < count * 2; i++) vmPop();

  vmPush(OBJ_VAL(pattern));
}

void vmClosure(CallFrame* frame) {
  ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
  ObjClosure* closure = newClosure(function);

  vmPush(OBJ_VAL(closure));
  vmCaptureUpvalues(closure, frame);
}

void vmSignature(CallFrame* frame) {
  ObjClosure* closure = AS_CLOSURE(vmPeek(0));

  mapSet(&closure->function->fields, INTERN("signature"), vmPeek(1));
  vmPop();
  vmPop();
  vmPush(OBJ_VAL(closure));
}

bool vmOverload(CallFrame* frame) {
  int cases = READ_BYTE();
  ObjOverload* overload = newOverload(cases);

  for (int i = cases; i > 0; i--) {
    if (!IS_CLOSURE(vmPeek(i - 1))) {
      vmRuntimeError("Overload operand must be a function.");
      return false;
    }

    overload->closures[cases - i] = AS_CLOSURE(vmPeek(i - 1));
  }

  while (cases--) vmPop();
  vmPush(OBJ_VAL(overload));
  return true;
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || IS_UNDEF(value) ||
         (IS_BOOL(value) && !AS_BOOL(value));
}

static bool validateSeqIdx(ObjSequence* seq, Value idx) {
  if (!IS_INTEGER(idx)) {
    vmRuntimeError("Sequences must be indexed by integer.");
    return false;
  }

  int intIdx = AS_NUMBER(idx);

  if (intIdx > seq->values.count - 1 || intIdx < 0) {
    vmRuntimeError("Index %i out of bounds for sequence (length %i).", intIdx,
                   seq->values.count);
    return false;
  }

  return true;
}

bool vmSequenceValueField(ObjInstance* obj, Value* seq) {
  if (!mapGet(&obj->fields, INTERN("values"), seq)) {
    vmRuntimeError("Sequence instance missing its values!");
    return false;
  }
  if (!IS_SEQUENCE(*seq)) {
    vmRuntimeError("Expecting sequence.");
    return false;
  }
  return true;
}

static bool vmInstanceHas(ObjInstance* instance, Value value) {
  uint32_t hash;
  if (!vmHashValue(value, &hash)) return false;

  bool hasKey = mapHasHash(&instance->fields, value, hash) ||
                mapHasHash(&instance->klass->fields, value, hash);
  vmPush(BOOL_VAL(hasKey));
  return true;
}

void vmInitPattern(ObjFunction* function) {
  ObjPattern* pattern = newPattern(function->arity);

  int i = pattern->count;
  int j = i;
  while (i > 0) {
    pattern->elements[j - i].value = vmPeek(i * 2 - 1);
    pattern->elements[j - i].type = vmPeek(i * 2 - 2);
    i--;
  }

  function->pattern = pattern;
}

// Loop until we're back to [baseFrame] frames. Typically this
// is just 0, but if we want to execute a single function in the
// middle of interpretation without overflowing its scope, we can
// let [baseFrame] = the current frame.
InterpretResult vmExecute(int baseFrame) {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

  for (;;) {
    if (vm.frameCount == baseFrame) return INTERPRET_OK;

#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    disassembleStack();
    printf("\n");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    uint8_t instruction;

    switch (instruction = READ_BYTE()) {
      case OP_UNDEFINED:
        vmPush(UNDEF_VAL);
        break;
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        vmPush(constant);
        break;
      }
      case OP_NIL:
        vmPush(NIL_VAL);
        break;
      case OP_TRUE:
        vmPush(BOOL_VAL(true));
        break;
      case OP_FALSE:
        vmPush(BOOL_VAL(false));
        break;
      case OP_EXPR_STATEMENT:
      case OP_POP:
        vmPop();
        break;
      case OP_GET_LOCAL: {
        uint8_t slot = READ_SHORT();
        vmPush(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_SHORT();
        frame->slots[slot] = vmPeek(0);
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();

        Value value;
        if (!mapGet(&vm.globals, OBJ_VAL(name), &value)) {
          vmRuntimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }

        vmPush(value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        Value name = READ_CONSTANT();
        mapSet(&vm.globals, name, vmPeek(0));
        vmPop();
        break;
      }
      case OP_SET_GLOBAL: {
        Value name = READ_CONSTANT();
        if (mapSet(&vm.globals, name, vmPeek(0))) {
          mapDelete(&vm.globals, name);
          vmRuntimeError("Undefined variable '%s'.", AS_STRING(name)->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_PROPERTY: {
        Value name = READ_CONSTANT();
        Value value = NIL_VAL;

#define ERROR "Can only get property of object, class, or function."

        if (!IS_OBJ(vmPeek(0))) {
          vmRuntimeError(ERROR);
          return INTERPRET_RUNTIME_ERROR;
        }

        switch (OBJ_TYPE(vmPeek(0))) {
          case OBJ_INSTANCE: {
            ObjInstance* instance = AS_INSTANCE(vmPeek(0));

            if (!mapGet(&instance->fields, name, &value)) {
              // class prop. must be a method.
              if (mapGet(&instance->klass->fields, name, &value))
                bindClosure(vmPeek(0), &value);
            }

            vmPop();  // instance.
            vmPush(value);
            break;
          }
          case OBJ_CLASS: {
            ObjClass* klass = AS_CLASS(vmPeek(0));

            mapGet(&klass->fields, name, &value);
            bindClosure(vmPeek(0), &value);
            vmPop();  // class.
            vmPush(value);
            break;
          }
          case OBJ_VARIABLE: {
            if (strcmp(AS_STRING(name)->chars, "name") == 0) {
              value = OBJ_VAL(AS_VARIABLE(vmPeek(0))->name);
              vmPop();
            }

            vmPush(value);
            break;
          }

          case OBJ_CLOSURE: {
            ObjClosure* closure = AS_CLOSURE(vmPeek(0));

            mapGet(&closure->function->fields, name, &value);

            vmPop();  // closure.
            vmPush(value);
            break;
          }
          default:
            vmRuntimeError(ERROR);
            return INTERPRET_RUNTIME_ERROR;
        }

        break;
      }
      case OP_SET_PROPERTY: {
        Value name = READ_CONSTANT();
        ObjMap* fields;

        switch (OBJ_TYPE(vmPeek(1))) {
          case OBJ_INSTANCE:
            fields = &AS_INSTANCE(vmPeek(1))->fields;
            break;
          case OBJ_CLASS:
            fields = &AS_CLASS(vmPeek(1))->fields;
            break;
          case OBJ_CLOSURE:
            fields = &AS_CLOSURE(vmPeek(1))->function->fields;
            break;
          default:
            vmRuntimeError(
                "Can only set property of object, class, or function.");
            return INTERPRET_RUNTIME_ERROR;
        }

        mapSet(fields, name, vmPeek(0));
        vmPop();
        break;
      }
      case OP_EQUAL: {
        Value a = vmPop();
        Value b = vmPop();

        // classes can override the equality relation.
        if (IS_INSTANCE(a) && IS_INSTANCE(b)) {
          ObjInstance* instanceA = AS_INSTANCE(a);
          ObjInstance* instanceB = AS_INSTANCE(b);

          Value equalFn;
          ObjClass lca;
          if (leastCommonAncestor(instanceA->klass, instanceB->klass, &lca) &&
              mapGet(&lca.fields, INTERN(S_EQ), &equalFn)) {
            vmPush(b);
            vmPush(a);
            if (!vmCallValue(equalFn, 1)) return INTERPRET_RUNTIME_ERROR;

            frame = &vm.frames[vm.frameCount - 1];
            break;
          }
        }

        vmPush(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GET_SUPER: {
        Value name = READ_CONSTANT();
        Value value = NIL_VAL;

        mapGet(&AS_CLASS(vmPeek(0))->fields, name, &value);

        bindClosure(vmPeek(1), &value);

        vmPop();  // superclass.
        vmPop();  // instance.
        vmPush(value);

        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_SHORT();
        vmPush(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_SHORT();
        *frame->closure->upvalues[slot]->location = vmPeek(0);
        break;
      }
      case OP_NOT:
        vmPush(BOOL_VAL(isFalsey(vmPop())));
        break;
      case OP_PRINT: {
        printValue(vmPop());
        printf("\n");
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(vmPeek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        if (!vmCallValue(vmPeek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CALL_INFIX: {
        Value right = vmPop();
        Value infix = vmPop();
        Value left = vmPop();

        vmPush(infix);
        vmPush(left);
        vmPush(right);

        if (!vmCallValue(infix, 2)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CALL_POSTFIX: {
        int argCount = READ_BYTE();
        Value postfix = vmPop();
        Value args[argCount];

        int i = argCount;
        while (i-- > 0) args[i] = vmPop();
        vmPush(postfix);
        while (++i < argCount) vmPush(args[i]);

        if (!vmCallValue(postfix, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      // invocation is a composite instruction:
      // property access followed by a call.
      case OP_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();

        if (!vmInvoke(method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLOSURE: {
        vmClosure(frame);
        break;
      }
      case OP_OVERLOAD: {
        if (!vmOverload(frame)) return INTERPRET_RUNTIME_ERROR;
        break;
      }
      case OP_VARIABLE: {
        vmVariable(frame);
        break;
      }
      case OP_PATTERN: {
        vmPattern(frame);
        break;
      }
      case OP_SIGNATURE: {
        vmClosure(frame);
        vmSignature(frame);
        break;
      }
      case OP_CLOSE_UPVALUE: {
        vmCloseUpvalues(vm.stackTop - 1);
        vmPop();
        break;
      }
      case OP_IMPLICIT_RETURN:
      case OP_RETURN: {
        Value value = vmPop();
        vmCloseUpvalues(frame->slots);
        vm.frameCount--;

        if (vm.frameCount == 0) {
          vmPop();
          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        vmPush(value);

        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLASS:
        vmPush(OBJ_VAL(newClass(READ_STRING())));
        break;
      case OP_INHERIT: {
        Value superclass = vmPeek(1);

        if (!IS_CLASS(superclass)) {
          vmRuntimeError("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        vmExtendClass(AS_CLASS(vmPeek(0)), AS_CLASS(superclass));
        vmPop();  // Subclass.
        break;
      }
      case OP_METHOD: {
        Value name = READ_CONSTANT();
        Value method = vmPeek(0);
        ObjClass* klass = AS_CLASS(vmPeek(1));
        mapSet(&klass->fields, name, method);
        vmPop();
        break;
      }
      case OP_MEMBER: {
        Value obj = vmPop();
        Value val = vmPop();

        if (IS_SEQUENCE(obj)) {
          bool seqHasVal = findInValueArray(&AS_SEQUENCE(obj)->values, val);
          vmPush(BOOL_VAL(seqHasVal));
          break;
        }

        if (!IS_INSTANCE(obj)) {
          vmRuntimeError(
              "Only objects or sequences may be tested for membership.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(obj);

        // classes can override the membership predicate.
        Value memFn;
        if (mapGet(&instance->klass->fields, INTERN(S_IN), &memFn)) {
          vmPush(obj);
          vmPush(val);

          if (!vmCallValue(memFn, 1)) return INTERPRET_RUNTIME_ERROR;

          frame = &vm.frames[vm.frameCount - 1];
          break;
        }

        if (!vmInstanceHas(instance, val)) return INTERPRET_RUNTIME_ERROR;

        break;
      }
      case OP_IMPORT: {
        Value path = vmPop();
        if (!IS_STRING(path)) {
          vmRuntimeError("Import path must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjString* baseDir = intern(NAT_BASE_DIR);
        vmPush(OBJ_VAL(baseDir));
        ObjString* importLoc = concatenateStrings(baseDir, AS_STRING(path));
        vmPop();

        InterpretResult result = interpretFile(importLoc->chars);

        if (result != INTERPRET_OK) return result;
        // pop the return value of the module's function.
        // if/when import statements become selective, this'll
        // probably stick around.
        vmPop();
        break;
      }
      case OP_THROW: {
        Value value = vmPop();

        if (!IS_INSTANCE(value)) {
          vmRuntimeError("Can only throw instance of 'Error'.");
          return INTERPRET_RUNTIME_ERROR;
        }

        Value msg;
        if (!mapGet(&AS_INSTANCE(value)->fields, INTERN("message"), &msg)) {
          vmRuntimeError("Error must define a 'message'.");
          return INTERPRET_RUNTIME_ERROR;
        }

        if (!IS_STRING(msg)) {
          vmRuntimeError("Error 'message' must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }

        vmRuntimeError("%s: %s", AS_INSTANCE(value)->klass->name->chars,
                       AS_STRING(msg)->chars);
        return INTERPRET_RUNTIME_ERROR;
      }

      case OP_SUBSCRIPT_GET: {
        Value key = vmPop();
        Value obj = vmPop();

        // indexed access to sequences.
        if (IS_SEQUENCE(obj)) {
          ObjSequence* seq = AS_SEQUENCE(obj);
          if (!validateSeqIdx(seq, key)) return INTERPRET_RUNTIME_ERROR;
          int idx = AS_NUMBER(key);
          vmPush(seq->values.values[idx]);
          break;
        }

        if (!IS_INSTANCE(obj)) {
          vmRuntimeError(
              "Only objects, sequences, and instances with a '%s' method "
              "support access by subscript.",
              S_SUBSCRIPT_GET);
          return INTERPRET_RUNTIME_ERROR;
        }

        // classes may define their own subscript access operator.
        ObjInstance* instance = AS_INSTANCE(obj);
        Value getFn;
        if (mapGet(&instance->klass->fields, INTERN(S_SUBSCRIPT_GET), &getFn)) {
          // set up the context for the function call.
          vmPush(obj);  // receiver.
          vmPush(key);

          if (!vmCallValue(getFn, 1)) return INTERPRET_RUNTIME_ERROR;
          frame = &vm.frames[vm.frameCount - 1];
          break;
        }

        // otherwise fall back to property access.
        uint32_t hash;
        if (!vmHashValue(key, &hash)) return INTERPRET_RUNTIME_ERROR;

        Value value;
        if (mapGetHash(&instance->fields, key, &value, hash)) {
          vmPush(value);
        } else {
          // we don't throw an error if the property doesn't exist.
          vmPush(NIL_VAL);
        }
        break;
      }
      case OP_SUBSCRIPT_SET: {
        // indexed assignment to sequences.
        if (IS_SEQUENCE(vmPeek(2))) {
          ObjSequence* seq = AS_SEQUENCE(vmPeek(2));

          if (!validateSeqIdx(seq, vmPeek(1))) return INTERPRET_RUNTIME_ERROR;
          int idx = AS_NUMBER(vmPeek(1));
          seq->values.values[idx] = vmPeek(0);

          // leave the sequence on the stack.
          vmPop();  // val.
          vmPop();  // key.
          break;
        }

        if (!IS_INSTANCE(vmPeek(2))) {
          vmRuntimeError(
              "Only objects, sequences, and instances with a '%s' method "
              "support assignment by subscript.",
              S_SUBSCRIPT_SET);
          return INTERPRET_RUNTIME_ERROR;
        }

        // classes may define their own subscript setting operator.
        ObjInstance* instance = AS_INSTANCE(vmPeek(2));
        Value setFn;
        if (mapGet(&instance->klass->fields, INTERN(S_SUBSCRIPT_SET), &setFn)) {
          // the stack is already ready for the function call.
          if (!vmCallValue(setFn, 2)) return INTERPRET_RUNTIME_ERROR;
          frame = &vm.frames[vm.frameCount - 1];
          break;
        }

        // otherwise fall back to property access.
        uint32_t hash;
        if (!vmHashValue(vmPeek(1), &hash)) return INTERPRET_RUNTIME_ERROR;

        mapSetHash(&instance->fields, vmPeek(1), vmPeek(0), hash);
        // leave the object on the stack.
        vmPop();  // val.
        vmPop();  // key.
        break;
      }
      case OP_DESTRUCTURE: {
        Value value = vmPeek(0);
        if (!ast(value)) return INTERPRET_RUNTIME_ERROR;
        break;
      }
      case OP_SET_TYPE_LOCAL: {
        READ_SHORT();
        break;
      }
      case OP_SET_TYPE_GLOBAL: {
        Value name = READ_CONSTANT();
        if (!mapHas(&vm.globals, name)) {
          vmRuntimeError("Undefined variable '%s'.", AS_STRING(name)->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        mapSet(&vm.typeEnv, name, vmPeek(0));
        break;
      }
      case OP_SPREAD: {
        Value value = vmPeek(0);

        ObjClass lca;
        if (!IS_INSTANCE(value) ||
            (leastCommonAncestor(AS_INSTANCE(value)->klass, vm.core.sequence,
                                 &lca) &&
             &lca == vm.core.sequence)) {
          vmRuntimeError("Only sequential values can spread.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjSpread* spread = newSpread(value);
        vmPop();
        vmPush(OBJ_VAL(spread));
        break;
      }
      case OP_UNIT: {
        vmPush(UNIT_VAL);
        break;
      }
      case OP_SEQUENCE: {
        vmSequence(frame);
        break;
      }
      default:
        vmRuntimeError("Unexpected op code: %i", instruction);
        return INTERPRET_RUNTIME_ERROR;
    }
  }
}

InterpretResult vmInterpret(char* path, const char* source) {
  ObjFunction* function = compile(vm.compiler, source, path);

  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  vmPush(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  vmPop();
  vmPush(OBJ_VAL(closure));
  call(closure, 0);

  return vmExecute(vm.frameCount - 1);
}
