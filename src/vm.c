#include "vm.h"

#include <libgen.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "io.h"
#include "memory.h"
#include "native.h"
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

  int leftOffset = 0;

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    if (frame->closure->function->module->closure == frame->closure) continue;
    int fnNameLen = strlen(frame->closure->function->name->chars);
    if (fnNameLen > leftOffset) leftOffset = fnNameLen;
  }

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjClosure* closure = frame->closure;
    ObjFunction* function = closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;

    // it's a script.
    if (frame->closure->function->module->closure == frame->closure) {
      fprintf(stderr, "        %*s %s/%s:%d\n", leftOffset, "in",
              function->module->dirName->chars,
              function->module->baseName->chars,
              function->chunk.lines[instruction]);

    } else {
      // it's a function.
      fprintf(stderr, "  in %-*s at %s/%s:%d\n", leftOffset,
              function->name->chars, function->module->dirName->chars,
              function->module->baseName->chars,
              function->chunk.lines[instruction]);
    }
  }

  resetStack();
}

bool initVM() {
  resetStack();
  vm.objects = NULL;

  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  vm.astRoot = NULL;

  vm.module = NULL;

  vm.comprehensionDepth = 0;
  for (int i = 0; i < COMPREHENSION_DEPTH_MAX; i++) vm.comprehensions[i] = NULL;

  initMap(&vm.globals);
  initMap(&vm.strings);
  initMap(&vm.prefixes);
  initMap(&vm.infixes);

  vm.core.sName = intern("name");
  vm.core.sArity = intern("arity");
  vm.core.sPatterned = intern("patterned");
  vm.core.sVariadic = intern("variadic");
  vm.core.sValues = intern("values");
  vm.core.sSignature = intern("signature");
  vm.core.sFunction = intern("function");
  vm.core.sModule = intern("__module__");
  vm.core.sQuote = intern("\"");
  vm.core.sBackslash = intern("\\");

  vm.core.sMain = intern("main");
  vm.core.sExecMain = intern("let out = main();");
  vm.core.sOut = intern("out");

  vm.core.sSeq = intern("seq");
  vm.core.sObj = intern("obj");
  vm.core.sSet = intern("set");

  defineNatives();

  return true;
}

void freeVM() {
  freeMap(&vm.globals);
  freeMap(&vm.strings);
  freeMap(&vm.prefixes);
  freeMap(&vm.infixes);

  freeAstNodes(vm.astRoot);
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

// If [value] is natively hashable, then hash it.
bool vmHashValue(Value value, uint32_t* hash) {
  if (vHashable(value)) {
    *hash = hashValue(value);
    return true;
  }

  return false;
}

static bool checkArity(ObjString* name, int arity, int argCount) {
  if (argCount == arity) return true;

  vmRuntimeError("%s expected %d arguments but got %d.", name->chars, arity,
                 argCount);
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
      if (!IS_SEQUENCE(AS_SPREAD(arg)->value)) {
        vmRuntimeError("Only sequences can be spread.");
        return false;
      }

      ObjSequence* seq = AS_SEQUENCE(AS_SPREAD(arg)->value);
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
  vmPush(OBJ_VAL(newSequence()));

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

    writeValueArray(&AS_SEQUENCE(seq)->values, arg);

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

void vmInitFrame(ObjClosure* closure, int offset) {
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - offset;
}

static bool callClosure(ObjClosure* closure, int argCount) {
  if (!spread(&argCount)) return false;

  if (closure->function->variadic)
    if (!variadify(closure, &argCount)) return false;

  if (!checkArity(closure->function->name, closure->function->arity, argCount))
    return false;

  if (vm.frameCount == FRAMES_MAX) {
    vmRuntimeError("Stack overflow.");
    return false;
  }

  vmInitFrame(closure, argCount + 1);

  return true;
}

static bool callModule(ObjModule* module) {
  if (vm.frameCount == FRAMES_MAX) {
    vmRuntimeError("Stack overflow.");
    return false;
  }

  vmInitFrame(module->closure, 1);

  return true;
}

static bool callNative(ObjNative* native, int argCount) {
  if (!spread(&argCount)) return false;

  if (!native->variadic && !checkArity(native->name, native->arity, argCount))
    return false;

  return (native->function)(argCount, vm.stackTop - argCount);
}

// Wrap the top [count] values in a sequence and put it
// on the stack, optionally leaving the values below it.
bool vmTuplify(int count, bool replace) {
  int i = count;
  Value args[count];

  if (replace)
    while (i--) args[i] = vmPop();
  else
    while (i--) args[count - i - 1] = vmPeek(i);

  vmPush(OBJ_VAL(vm.core.sSeq));
  while (++i < count) vmPush(args[i]);

  return vmCallValue(OBJ_VAL(vm.core.sSeq), count);
}

static bool callCases(ObjClosure** cases, int caseCount, int argCount) {
  vmRuntimeError("Unification not implemented.");
  return false;

  if (!vmTuplify(argCount, false)) return false;

  // Value scrutinee = vmPeek(0);

  for (int i = 0; i < caseCount; i++) {
    // unification fix me.
    // if (!unify(cases[i], scrutinee)) return false;
    return false;

    if (AS_BOOL(vmPop())) {
      vmPop();  // the tuplified scrutinee.

      vm.stackTop[-1 - argCount] = OBJ_VAL(cases[i]);

      return callClosure(cases[i], argCount);
    }
  }

  // no match: replace the arguments and the case object with undef.
  vmPop();                     // tuplified scrutinee.
  while (argCount--) vmPop();  // args.
  vmPop();                     // case.
  vmPush(UNDEF_VAL);

  return true;
}

bool vmCallValue(Value caller, int argCount) {
  if (IS_OBJ(caller)) {
    switch (OBJ_TYPE(caller)) {
      case OBJ_CLOSURE: {
        ObjClosure* closure = AS_CLOSURE(caller);

        if (closure->function->patterned)
          return callCases(&closure, 1, argCount);
        return callClosure(AS_CLOSURE(caller), argCount);
      }
      case OBJ_OVERLOAD: {
        ObjOverload* overload = AS_OVERLOAD(caller);
        return callCases(overload->closures, overload->cases, argCount);
      }
      case OBJ_NATIVE:
        return callNative(AS_NATIVE(caller), argCount);
      default:
        break;  // Non-callable object type.
    }
  }

  vmRuntimeError(
      "Can only call functions, classes, and objects with a 'call' method.");
  return false;
}

ObjUpvalue* vmCaptureUpvalue(Value* local, uint8_t slot, ObjString* name) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local, slot, name);
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
      Token token = frame->closure->function->locals[index].name;
      ObjString* name = copyString(token.start, token.length);
      vmPush(OBJ_VAL(name));
      closure->upvalues[i] =
          vmCaptureUpvalue(frame->slots + index, index, name);
      vmPop();
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

void vmVariable(CallFrame* frame) {
  ObjString* name = READ_STRING();
  ObjVariable* var = newVariable(name);
  vmPush(OBJ_VAL(var));
}

void vmClosure(CallFrame* frame) {
  ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
  ObjClosure* closure = newClosure(function);

  vmPush(OBJ_VAL(closure));
  vmCaptureUpvalues(closure, frame);
}

bool vmOverload(CallFrame* frame) {
  int cases = READ_BYTE();
  READ_CONSTANT();  // name.
  int arity = 0;
  ObjOverload* overload = newOverload(cases);

  for (int i = cases; i > 0; i--) {
    ObjClosure** closures = overload->closures;

    if (!IS_CLOSURE(vmPeek(i - 1))) {
      vmRuntimeError("Overload operand must be a function.");
      return false;
    }

    closures[cases - i] = AS_CLOSURE(vmPeek(i - 1));

    if (i < cases && closures[cases - i]->function->arity != arity) {
      vmRuntimeError("Overload operands must have uniform arity.");
      return false;
    }

    arity = closures[cases - i]->function->arity;
  }

  while (cases--) vmPop();
  vmPush(OBJ_VAL(overload));
  return true;
}

bool vmCallModule(ObjModule* module) {
  ObjModule* enclosing = vm.module;
  vm.module = module;

  if (!callModule(module) || vmExecute(vm.frameCount - 1) != INTERPRET_OK)
    return false;
  vmPop();  // nil.

  vm.module = enclosing;
  return true;
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || IS_UNDEF(value) ||
         (IS_BOOL(value) && !AS_BOOL(value));
}

static bool assertInt(Value value, char* msg) {
  if (!IS_INTEGER(value)) {
    vmRuntimeError(msg);
    return false;
  }
  return true;
}

static bool validateStrIdx(ObjString* str, Value idx) {
  if (!assertInt(idx, "Strings must be indexed by integer.")) return false;
  int intIdx = AS_NUMBER(idx);
  if (intIdx > str->length - 1 || intIdx < 0) {
    vmRuntimeError("Index %i out of bounds for string of length %i.", intIdx,
                   str->length);
    return false;
  }

  return true;
}

static bool validateSeqIdx(ObjSequence* seq, Value idx) {
  if (!assertInt(idx, "Sequences must be indexed by integer.")) return false;
  int intIdx = AS_NUMBER(idx);

  if (intIdx > seq->values.count - 1 || intIdx < 0) {
    vmRuntimeError("Index %i out of bounds for sequence of length %i.", intIdx,
                   seq->values.count);
    return false;
  }

  return true;
}

// Loop until we're back to [baseFrame] frames. Typically this
// is just 0, but if we want to execute a single function in the
// middle of execution we can let [baseFrame] = the current frame.
InterpretResult vmExecute(int baseFrame) {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

  for (;;) {
    if (vm.frameCount == baseFrame) return INTERPRET_OK;

    TRACE_EXECUTION("");

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

        if (!IS_OBJ(vmPeek(0))) {
          vmRuntimeError("Can only get property of object.");
          return INTERPRET_RUNTIME_ERROR;
        }
        mapGet(&AS_OBJ(vmPeek(0))->fields, name, &value);

        vmPop();
        vmPush(value);

        break;
      }
      case OP_SET_PROPERTY: {
        Value name = READ_CONSTANT();

        if (!IS_OBJ(vmPeek(1))) {
          vmRuntimeError("Can only set property of object.");
          return INTERPRET_RUNTIME_ERROR;
        }

        mapSet(&AS_OBJ(vmPeek(1))->fields, name, vmPeek(0));
        vmPop();
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
        Value caller = vmPeek(argCount);

        if (!vmCallValue(caller, argCount) ||
            vmExecute(vm.frameCount - 1) != INTERPRET_OK)
          return INTERPRET_RUNTIME_ERROR;
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
      case OP_CLOSE_UPVALUE: {
        vmCloseUpvalues(vm.stackTop - 1);
        vmPop();
        break;
      }
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
      case OP_MEMBER: {
        Value obj = vmPop();
        vmPop();

        char* error =
            "Only objects, classes, and sequences may be tested for "
            "membership.";

        if (!IS_OBJ(obj)) {
          vmRuntimeError(error);
          return INTERPRET_RUNTIME_ERROR;
        }

        switch (OBJ_TYPE(obj)) {
          default: {
            vmRuntimeError(error);
            return INTERPRET_RUNTIME_ERROR;
          }
        }

        break;
      }
      case OP_THROW: {
        Value value = vmPop();

        Value msg;
        if (!IS_MAP(value) ||
            !mapGet(&AS_MAP(value)->obj.fields, INTERN("message"), &msg) ||
            !IS_STRING(msg))
          vmRuntimeError("Error must define a 'message' string.");

        vmRuntimeError("%s", AS_STRING(msg)->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      case OP_SUBSCRIPT_GET: {
        Value key = vmPop();
        Value obj = vmPop();

        if (!IS_OBJ(obj)) {
          vmRuntimeError("Only objects support subscription.");
          return INTERPRET_RUNTIME_ERROR;
        }

        switch (OBJ_TYPE(obj)) {
          case OBJ_SEQUENCE: {
            ObjSequence* seq = AS_SEQUENCE(obj);
            if (!validateSeqIdx(seq, key)) return INTERPRET_RUNTIME_ERROR;
            int idx = AS_NUMBER(key);
            vmPush(seq->values.values[idx]);
            break;
          }
          case OBJ_STRING: {
            ObjString* string = AS_STRING(obj);
            if (!validateStrIdx(string, key)) return INTERPRET_RUNTIME_ERROR;
            int idx = AS_NUMBER(key);
            ObjString* character = copyString(string->chars + idx, 1);
            vmPush(OBJ_VAL(character));
            break;
          }
          default: {
            Value value = NIL_VAL;
            mapGet(&AS_OBJ(obj)->fields, key, &value);
            vmPush(value);
            break;
          }
        }
        break;
      }
      case OP_SUBSCRIPT_SET: {
        if (!IS_OBJ(vmPeek(2))) {
          vmRuntimeError("Only objects support subscription.");
          return INTERPRET_RUNTIME_ERROR;
        }

        switch (OBJ_TYPE(vmPeek(2))) {
          case OBJ_SEQUENCE: {
            ObjSequence* seq = AS_SEQUENCE(vmPeek(2));

            if (!validateSeqIdx(seq, vmPeek(1))) return INTERPRET_RUNTIME_ERROR;
            int idx = AS_NUMBER(vmPeek(1));
            seq->values.values[idx] = vmPeek(0);

            // leave the sequence on the stack.
            vmPop();  // val.
            vmPop();  // key.
            break;
          }
          case OBJ_STRING: {
            ObjString* string = AS_STRING(vmPeek(2));
            if (!validateStrIdx(string, vmPeek(1)))
              return INTERPRET_RUNTIME_ERROR;
            int idx = AS_NUMBER(vmPeek(1));

            if (!IS_STRING(vmPeek(0)) && AS_STRING(vmPeek(0))->length == 1) {
              vmRuntimeError("Must be character.");
              return false;
            }

            ObjString* character = AS_STRING(vmPeek(0));
            setStringChar(string, character, idx);
            // leave the string on the stack.
            vmPop();  // val.
            vmPop();  // key.
            break;
          }
          default: {
            mapSet(&AS_OBJ(vmPeek(2))->fields, vmPeek(1), vmPeek(0));
            vmPop();  // val.
            vmPop();  // key.
            break;
          }
        }
        break;
      }
      case OP_SPREAD: {
        Value value = vmPeek(0);
        if (!IS_SEQUENCE(value)) {
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
      case OP_QUANTIFY: {
        Value body = vmPop();
        Value restriction = vmPop();
        Value quantifier = vmPop();

        vmPush(quantifier);
        vmPush(restriction);
        vmPush(body);

        if (!vmCallValue(quantifier, 2)) return INTERPRET_RUNTIME_ERROR;
        frame = &vm.frames[vm.frameCount - 1];

        break;
      }
      default:
        vmRuntimeError("Unexpected op code: %i", instruction);
        return INTERPRET_RUNTIME_ERROR;
    }
  }
}

// Compilation routines that use the stack.

ObjClosure* vmCompileAST(char* source, AstNode* module) {
  AstNode* node =
      compileFunctionNode(module->as.module.baseName, source, module);
  printf("node: ");
  printNode(node);
  printf("\n");

  ObjFunction* fn = toFunction(node);

  vmPush(OBJ_VAL(fn));
  ObjClosure* closure = newClosure(fn);
  vmPop();  // function.

  return closure;
}

ObjClosure* vmCompileClosure(Token path, char* source, ObjModule* module) {
  AstNode* moduleNode =
      newModuleNode(module->dirName, module->baseName, module->source);
  ObjClosure* closure = vmCompileAST(source, moduleNode);
  closure->function->module = module;
  return closure;
}

bool vmPathBits(char* enclosingDir, Token path) {
  // path can be of the form a/b/c, in which case we need to
  // separate a/b from c.
  ObjString* objPath = copyString(path.start, path.length);
  vmPush(OBJ_VAL(objPath));

  char* absPath = pathToUri(enclosingDir, objPath->chars);
  char *c1 = malloc(strlen(absPath) + 1), *c2 = malloc(strlen(absPath) + 1);
  if (c1 == 0 || c2 == 0) return false;
  strcpy(c1, absPath);
  strcpy(c2, absPath);
  char* dir = dirname(c1);
  char* base = basename(c2);

  vmPop();

  ObjString* objDirName = intern(dir);
  vmPush(OBJ_VAL(objDirName));
  ObjString* objBaseName = intern(base);
  vmPush(OBJ_VAL(objBaseName));
  ObjString* objAbsPath = intern(absPath);
  vmPush(OBJ_VAL(objAbsPath));

  free(c1);
  free(c2);
  return true;
}

AstNode* vmCompileModuleImportBody(NodeCompiler* cmp, char* enclosingDir,
                                   Token path) {
  if (!vmPathBits(enclosingDir, path)) return NULL;

  ObjString* objDirName = AS_STRING(vmPeek(2));
  ObjString* objBaseName = AS_STRING(vmPeek(1));
  ObjString* objAbsPath = AS_STRING(vmPeek(0));
  char* source = readFile(objAbsPath->chars);
  ObjString* objSource = intern(source);
  vmPush(OBJ_VAL(objSource));
  free(source);

  AstNode* module = newModuleNode(objDirName, objBaseName, objSource);
  compileModuleImportBody(cmp, module);

  vmPop();  // objSource.
  vmPop();  // objAbsPath.
  vmPop();  // objBaseName.
  vmPop();  // objDirName.

  return module;
}

ObjModule* vmCompileModule(char* enclosingDir, Token path) {
  if (!vmPathBits(enclosingDir, path)) return NULL;

  ObjString* objDirName = AS_STRING(vmPeek(2));
  ObjString* objBaseName = AS_STRING(vmPeek(1));
  ObjString* objAbsPath = AS_STRING(vmPeek(0));

  char* source = readFile(objAbsPath->chars);
  ObjString* objSource = intern(source);
  vmPush(OBJ_VAL(objSource));
  free(source);

  ObjModule* module = newModule(objDirName, objBaseName, objSource);
  vmPush(OBJ_VAL(module));

  ObjClosure* closure = vmCompileClosure(syntheticToken(objAbsPath->chars),
                                         objSource->chars, module);
  if (closure == NULL) return NULL;

  module->closure = closure;

  vmPop();  // module.
  vmPop();  // objAbsPath.
  vmPop();  // objSource.
  vmPop();  // objBaseName.
  vmPop();  // objDirName.

  return module;
}

// Entrypoints.

InterpretResult vmInterpretExpr(char* path, char* expr) {
  Token tokPath = syntheticToken(path);
  ObjClosure* closure = vmCompileClosure(tokPath, expr, NULL);

  if (closure == NULL) return INTERPRET_COMPILE_ERROR;

  vmPush(OBJ_VAL(closure));
  if (!callClosure(closure, 0)) return INTERPRET_RUNTIME_ERROR;

  return vmExecute(vm.frameCount - 1);
}

InterpretResult vmExecuteModule(ObjModule* module) {
  if (!callModule(module)) return INTERPRET_RUNTIME_ERROR;

  return vmExecute(vm.frameCount - 1);
}

InterpretResult vmInterpretEntrypoint(char* path) {
  ObjModule* module = vmCompileModule(NULL, syntheticToken(path));

  if (module == NULL) return INTERPRET_COMPILE_ERROR;

  vmPush(OBJ_VAL(module));
  vm.module = module;
  return vmExecuteModule(module);
}

// wasm api.

void vmInit_wasm() {
  if (!initVM()) exit(2);
}

void vmFree_wasm() { freeVM(); }