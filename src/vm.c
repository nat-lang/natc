#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

void runtimeError(const char* format, ...) {
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

bool validateHashable(Value value) {
  if (IS_OBJ(value) && !IS_STRING(value)) {
    if (AS_OBJ(value)->hash == 0) {
      runtimeError("Object lacks a valid hash.");
      return false;
    }
    return true;
  }

  if (!isHashable(value)) {
    runtimeError("Not a hashable type: num, nil, bool, or string.");
    return false;
  }
  return true;
}

bool initVM() {
  resetStack();
  vm.objects = NULL;

  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initMap(&vm.globals);
  initMap(&vm.strings);

  vm.initString = NULL;
  vm.initString = intern("init");
  vm.callString = NULL;
  vm.callString = intern("call");
  vm.iterString = NULL;
  vm.iterString = intern("iter");
  vm.nextString = NULL;
  vm.nextString = intern("next");
  vm.addString = NULL;
  vm.addString = intern("add");
  vm.memberString = NULL;
  vm.memberString = intern("__in__");
  vm.subscriptGetString = NULL;
  vm.subscriptGetString = intern("__get__");
  vm.subscriptSetString = NULL;
  vm.subscriptSetString = intern("__set__");
  vm.lengthString = NULL;
  vm.lengthString = intern("__len__");
  vm.equalString = NULL;
  vm.equalString = intern("__eq__");

  vm.seqClass = NULL;
  vm.objClass = NULL;

  initInfixes();

  return initializeCore() == INTERPRET_OK;
}

void freeVM() {
  freeMap(&vm.globals);
  freeMap(&vm.strings);
  vm.initString = NULL;
  vm.callString = NULL;
  vm.iterString = NULL;
  vm.memberString = NULL;
  vm.subscriptGetString = NULL;
  vm.subscriptSetString = NULL;
  vm.lengthString = NULL;
  vm.equalString = NULL;
  freeObjects();
  freeInfixes();
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

static bool checkArity(int paramCount, int argCount) {
  if (argCount == paramCount) return true;

  runtimeError("Expected %d arguments but got %d.", paramCount, argCount);
  return false;
}

// Collapse [argCount] - [arity] + 1 arguments into a final
// sequence argument.
static bool variadify(ObjClosure* closure, int* argCount) {
  // put a sequence on the stack.
  vmPush(OBJ_VAL(newInstance(vm.seqClass)));
  initClass(vm.seqClass, 0);

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

    if (!invoke(vm.addString, 1)) return false;
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

static bool call(ObjClosure* closure, int argCount) {
  if (closure->function->variadic)
    if (!variadify(closure, &argCount)) return false;

  if (!checkArity(closure->function->arity, argCount)) return false;

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;

  return true;
}

static bool callNative(ObjNative* native, int argCount) {
  if (!checkArity(native->arity, argCount)) return false;

  return (native->function)(argCount, vm.stackTop - argCount);
}

bool vmCallValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        vm.stackTop[-argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
        return initClass(klass, argCount);
      }
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE:
        return callNative(AS_NATIVE(callee), argCount);
      case OBJ_INSTANCE: {
        ObjInstance* instance = AS_INSTANCE(callee);
        Value callFn;
        if (mapGet(&instance->klass->methods, OBJ_VAL(vm.callString),
                   &callFn)) {
          return call(AS_CLOSURE(callFn), argCount);
        } else {
          printValue(callee);
          runtimeError("Objects require a 'call' method to be called.");
          return false;
        }
        return true;
      }
      default:
        break;  // Non-callable object type.
    }
  }

  runtimeError(
      "Can only call functions, classes, and objects with a 'call' method.");
  return false;
}

bool initClass(ObjClass* klass, int argCount) {
  Value initializer;

  if (mapGet(&klass->methods, OBJ_VAL(vm.initString), &initializer)) {
    return vmCallValue(initializer, argCount);
  } else if (argCount != 0) {
    runtimeError("Expected 0 arguments but got %d.", argCount);
    return false;
  }
  return true;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
  Value method;
  if (!mapGet(&klass->methods, OBJ_VAL(name), &method)) {
    runtimeError("Undefined property '%s' for class '%s'.", name->chars,
                 klass->name->chars);
    return false;
  }

  return vmCallValue(method, argCount);
}

bool invoke(ObjString* name, int argCount) {
  Value receiver = vmPeek(argCount);

  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(receiver);

  Value field;
  if (mapGet(&instance->fields, OBJ_VAL(name), &field)) {
    vm.stackTop[-argCount - 1] = field;
    return vmCallValue(field, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
  Value method;
  if (!mapGet(&klass->methods, OBJ_VAL(name), &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  ObjBoundMethod* bound = newBoundMethod(vmPeek(0), AS_CLOSURE(method));
  vmPop();
  vmPush(OBJ_VAL(bound));
  return true;
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static void defineMethod(Value name) {
  Value method = vmPeek(0);
  ObjClass* klass = AS_CLASS(vmPeek(1));
  mapSet(&klass->methods, name, method);
  vmPop();
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool validateSeqIdx(ObjSequence* seq, Value idx) {
  if (!IS_INTEGER(idx)) {
    runtimeError("Sequences must be indexed by integer.");
    return false;
  }

  int intIdx = AS_NUMBER(idx);

  if (intIdx > seq->values.count - 1 || intIdx < 0) {
    runtimeError("Index %i out of bounds [0:%i]", intIdx, seq->values.count);
    return false;
  }

  return true;
}

bool vmInstanceHas(ObjInstance* instance, Value value) {
  if (!validateHashable(value)) return false;

  bool hasKey = mapHas(&instance->fields, value) ||
                mapHas(&instance->klass->methods, value);
  vmPush(BOOL_VAL(hasKey));
  return true;
}

static InterpretResult loop() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
  (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

  for (;;) {
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
          runtimeError("Undefined variable '%s'.", name->chars);
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
          runtimeError("Undefined variable '%s'.", AS_STRING(name)->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_PROPERTY: {
        if (!IS_INSTANCE(vmPeek(0))) {
          runtimeError("Only objects have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(vmPeek(0));
        ObjString* name = READ_STRING();

        Value value;
        if (mapGet(&instance->fields, OBJ_VAL(name), &value)) {
          vmPop();  // Instance.
          vmPush(value);
          break;
        }

        if (!bindMethod(instance->klass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_PROPERTY: {
        if (!IS_INSTANCE(vmPeek(1))) {
          runtimeError("Only objects have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(vmPeek(1));
        mapSet(&instance->fields, READ_CONSTANT(), vmPeek(0));
        Value value = vmPop();
        vmPop();
        vmPush(value);
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
          if (valuesEqual(OBJ_VAL(instanceA->klass),
                          OBJ_VAL(instanceB->klass)) &&
              mapGet(&instanceA->klass->methods, OBJ_VAL(vm.equalString),
                     &equalFn)) {
            vmPush(a);
            vmPush(b);
            if (!vmCallValue(equalFn, 1)) return INTERPRET_RUNTIME_ERROR;

            frame = &vm.frames[vm.frameCount - 1];
            break;
          }
        }

        vmPush(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GET_SUPER: {
        ObjString* name = READ_STRING();
        ObjClass* superclass = AS_CLASS(vmPop());

        if (!bindMethod(superclass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
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
      // unreachable.
      case OP_NEGATE:
        if (!IS_NUMBER(vmPeek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        vmPush(NUMBER_VAL(-AS_NUMBER(vmPop())));
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
      // invocation is a composite instruction:
      // property access followed by a call.
      case OP_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();

        if (!invoke(method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_SUPER_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        ObjClass* superclass = AS_CLASS(vmPop());
        if (!invokeFromClass(superclass, method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        vmPush(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        vmPop();
        break;
      case OP_RETURN: {
        Value result = vmPop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
          vmPop();
          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        vmPush(result);
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLASS:
        vmPush(OBJ_VAL(newClass(READ_STRING())));
        break;
      case OP_INHERIT: {
        Value superclass = vmPeek(1);

        if (!IS_CLASS(superclass)) {
          runtimeError("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjClass* subclass = AS_CLASS(vmPeek(0));
        mapAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
        vmPop();  // Subclass.
        break;
      }
      case OP_METHOD:
        defineMethod(READ_CONSTANT());
        break;
      case OP_MEMBER: {
        Value obj = vmPop();
        Value val = vmPop();

        if (IS_SEQUENCE(obj)) {
          bool seqHasVal = findInValueArray(&AS_SEQUENCE(obj)->values, val);
          vmPush(BOOL_VAL(seqHasVal));
          break;
        }

        if (!IS_INSTANCE(obj)) {
          runtimeError(
              "Only objects or sequences may be tested for membership.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(obj);

        // classes can override the membership predicate.
        Value memFn;
        if (mapGet(&instance->klass->methods, OBJ_VAL(vm.memberString),
                   &memFn)) {
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
          runtimeError("Import path must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        InterpretResult result = interpretFile(AS_STRING(path)->chars);
        vmPop();
        return result;
      }
      case OP_THROW: {
        Value value = vmPop();

        if (!IS_INSTANCE(value)) {
          runtimeError("Can only throw instance of 'Error'.");
          return INTERPRET_RUNTIME_ERROR;
        }

        Value msg;
        if (!mapGet(&AS_INSTANCE(value)->fields, OBJ_VAL(intern("message")),
                    &msg)) {
          runtimeError("Error must define a 'message'.");
          return INTERPRET_RUNTIME_ERROR;
        }

        if (!IS_STRING(msg)) {
          runtimeError("Error 'message' must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }

        runtimeError("%s: %s", AS_INSTANCE(value)->klass->name->chars,
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
          runtimeError(
              "Only objects, sequences, and instances with a '%s' method "
              "support "
              "access by subscript.",
              vm.subscriptGetString->chars);
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(obj);

        // classes may define their own subscript access operator.
        Value getFn;
        if (mapGet(&instance->klass->methods, OBJ_VAL(vm.subscriptGetString),
                   &getFn)) {
          // set up the context for the function call.
          vmPush(obj);  // receiver.
          vmPush(key);

          if (!vmCallValue(getFn, 1)) return INTERPRET_RUNTIME_ERROR;
          frame = &vm.frames[vm.frameCount - 1];
          break;
        }

        if (!validateHashable(key)) return INTERPRET_RUNTIME_ERROR;

        // otherwise fall back to property access.
        Value value;
        if (mapGet(&instance->fields, key, &value)) {
          vmPush(value);
        } else {
          // we don't throw an error if the property doesn't exist.
          vmPush(NIL_VAL);
        }
        break;
      }
      case OP_SUBSCRIPT_SET: {
        Value val = vmPop();
        Value key = vmPop();
        Value obj = vmPop();

        // indexed assignment to sequences.
        if (IS_SEQUENCE(obj)) {
          ObjSequence* seq = AS_SEQUENCE(obj);

          if (!validateSeqIdx(seq, key)) return INTERPRET_RUNTIME_ERROR;

          int idx = AS_NUMBER(key);

          seq->values.values[idx] = val;

          // leave the sequence on the stack.
          vmPush(OBJ_VAL(seq));
          break;
        }

        if (!IS_INSTANCE(obj)) {
          runtimeError(
              "Only objects, sequences, and instances with a '%s' method "
              "support "
              "assignment by subscript.",
              vm.subscriptSetString->chars);
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(obj);

        // classes may define their own subscript setting operator.
        Value setFn;
        if (mapGet(&instance->klass->methods, OBJ_VAL(vm.subscriptSetString),
                   &setFn)) {
          // set up the context for the function call.
          vmPush(obj);  // receiver.
          vmPush(key);
          vmPush(val);

          if (!vmCallValue(setFn, 2)) return INTERPRET_RUNTIME_ERROR;
          frame = &vm.frames[vm.frameCount - 1];
          break;
        }

        // otherwise fall back to property setting.
        if (!validateHashable(key)) return INTERPRET_RUNTIME_ERROR;
        mapSet(&instance->fields, key, val);
        // leave the object on the stack.
        vmPush(obj);
        break;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
}

InterpretResult interpret(char* path, const char* source) {
  ObjFunction* function = compile(source, path);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  vmPush(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  vmPop();
  vmPush(OBJ_VAL(closure));
  call(closure, 0);

  return loop();
}
