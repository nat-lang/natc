#include "ast.h"

#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "object.h"
#include "value.h"
#include "vm.h"

bool instance(ObjClass* klass) {
  ObjInstance* node = newInstance(klass);
  // initialize the instance.
  vmPush(OBJ_VAL(node));

  if (!mapHas(&klass->methods, OBJ_VAL(intern(S_INIT)))) return true;
  if (!vmInitClass(klass, 0)) return false;
  if (vmExecute(vm.frameCount - 1) != INTERPRET_OK) return false;

  return true;
}

bool closureInstance(ObjMap signature) {
  ObjInstance* node = newInstance(vm.classes.astClosure);
  vmPush(OBJ_VAL(node));

  if (!instance(vm.classes.astSignature)) return false;
  mapAddAll(&signature, &AS_INSTANCE(vmPeek(0))->fields);

  if (!vmInitClass(vm.classes.astClosure, 1)) return false;
  if (vmExecute(vm.frameCount - 1) != INTERPRET_OK) return false;

  return true;
}

bool executeMethod(char* method, int argCount) {
  return (vmInvoke(intern(method), argCount)) &&
         (vmExecute(vm.frameCount - 1) == INTERPRET_OK);
}

// Read the syntax tree of [closure] off the tape.
bool readAST(ObjClosure* closure) {
#ifdef DEBUG_TRACE_EXECUTION
  disassembleChunk(&closure->function->chunk, closure->function->name->chars);
  printf("\n");
#endif

  // the root of the tree.
  if (!closureInstance(closure->function->signature)) return false;

  Value root = vmPeek(0);

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = NULL;

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    printf("\t");
    disassembleStack();
    printf("\n");
    printf("\t");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction = READ_BYTE();

    switch (instruction) {
      case OP_CONSTANT: {
        vmPush(root);
        vmPush(READ_CONSTANT());
        if (!executeMethod("opLiteral", 1)) return false;
        break;
      }
      case OP_NIL: {
        vmPush(root);
        vmPush(NIL_VAL);
        if (!executeMethod("opLiteral", 1)) return false;
        break;
      }
      case OP_TRUE: {
        vmPush(root);
        vmPush(BOOL_VAL(true));
        if (!executeMethod("opLiteral", 1)) return false;
        break;
      }
      case OP_FALSE: {
        vmPush(root);
        vmPush(BOOL_VAL(false));
        if (!executeMethod("opLiteral", 1)) return false;
        break;
      }
      case OP_RETURN: {
        Value value = vmPop();
        vmPush(root);
        vmPush(value);

        if (!executeMethod("opReturn", 1)) return false;

        vmPop();  // nil.
        break;
      }
      case OP_GET_GLOBAL: {
        vmPush(root);
        ObjString* name = READ_STRING();
        vmPush(OBJ_VAL(name));

        if (!executeMethod("opGetGlobal", 1)) return false;
        break;
      }
      case OP_GET_LOCAL: {
        vmPush(root);
        uint8_t slot = READ_SHORT();
        vmPush(NUMBER_VAL(slot));

        if (!executeMethod("opGetLocal", 1)) return false;
        break;
      }
      case OP_GET_UPVALUE: {
        vmPush(root);
        uint8_t slot = READ_SHORT();
        vmPush(NUMBER_VAL(slot));

        if (!executeMethod("opGetUpvalue", 1)) return false;
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        vmPush(OBJ_VAL(closure));
        vmCaptureUpvalues(closure, frame);

        if (!readAST(closure)) return false;

        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        Value args[argCount];

        for (int i = 0; i < argCount; i++) args[i] = vmPop();
        Value fn = vmPop();
        vmPush(root);
        vmPush(fn);
        for (int i = argCount - 1; i >= 0; i--) vmPush(args[i]);

        if (!executeMethod("opCall", argCount + 1)) return false;
        break;
      }
      case OP_CALL_INFIX: {
        Value right = vmPop();
        Value infix = vmPop();
        Value left = vmPop();

        vmPush(root);
        vmPush(infix);
        vmPush(left);
        vmPush(right);

        if (!executeMethod("opCall", 3)) return false;
        break;
      }
      case OP_END: {
        vmPop();  // the root.
        vmPop();  // the destructured expression.
        vmPush(root);
        vm.frameCount--;
        return true;
      }
      default: {
        vmRuntimeError("Unhandled destructured opcode.");
        return false;
      }
    }
  }

  vmRuntimeError("This should be unreachable.");
  return false;
}