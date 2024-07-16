#include "ast.h"

#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "object.h"
#include "value.h"
#include "vm.h"

bool astClosure(ObjClosure* closure);
bool astOverload(ObjOverload* overload);

static bool executeMethod(char* method, int argCount) {
  return vmExecuteMethod(method, argCount, 1);
}

static bool initInstance(ObjClass* klass, int argCount) {
  return vmInitInstance(klass, argCount, 1);
}

// Read the syntax tree of [closure] off the tape.
bool astFrame(Value root) {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#ifdef DEBUG_TRACE_EXECUTION
  disassembleChunk(&frame->closure->function->chunk,
                   frame->closure->function->name->chars);
  printf("\n");
#endif

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    disassembleStack();
    printf("\n");
    printf("  (destruct) ");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction = READ_BYTE();

    switch (instruction) {
      case OP_UNDEFINED: {
        vmPush(root);
        vmPush(UNDEF_VAL);
        if (!executeMethod("opLiteral", 1)) return false;
        break;
      }
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
      case OP_NOT: {
        Value value = vmPop();
        vmPush(root);
        vmPush(value);
        if (!executeMethod("opNot", 1)) return false;
        break;
      }
      case OP_EXPR_STATEMENT: {
        Value value = vmPop();
        vmPush(root);
        vmPush(value);

        if (!executeMethod("opExprStatement", 1)) return false;

        vmPop();  // nil.
        break;
      }
      case OP_POP: {
        vmPop();
        break;
      }
      case OP_RETURN: {
        Value value = vmPop();
        vmCloseUpvalues(frame->slots);
        vmPush(root);
        vmPush(value);

        if (!executeMethod("opReturn", 1)) return false;

        vmPop();  // nil.
        break;
      }
      case OP_IMPLICIT_RETURN: {
        Value value = vmPop();
        vmCloseUpvalues(frame->slots);
        vmPush(root);
        vmPush(value);
        if (!executeMethod("opImplicitReturn", 1)) return false;
        vmPop();  // nil.

        // we've reached the end of the closure we're
        // destructuring. replace it with its ast.

        vm.frameCount--;
        vm.stackTop = frame->slots - 1;  // point at [closure] - 1.
        vmPush(root);                    // leave the ast on the stack.
        return true;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        vmPush(root);
        vmPush(OBJ_VAL(name));

        if (!executeMethod("opGetGlobal", 1)) return false;
        break;
      }
      case OP_GET_LOCAL: {
        uint8_t slot = READ_SHORT();
        vmPush(root);
        vmPush(NUMBER_VAL(slot));

        if (!executeMethod("opGetLocal", 1)) return false;
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_SHORT();
        Value value = vmPeek(0);

        vmPush(root);
        vmPush(NUMBER_VAL(slot));
        vmPush(value);

        if (!executeMethod("opSetLocalValue", 2)) return false;
        vmPop();  // nil.
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_SHORT();
        ObjUpvalue* upvalue = frame->closure->upvalues[slot];

        vmPush(root);
        vmPush(NUMBER_VAL((uintptr_t)upvalue));
        vmPush(NUMBER_VAL(upvalue->slot));

        if (!executeMethod("opGetUpvalue", 2)) return false;
        break;
      }
      case OP_SET_PROPERTY: {
        Value key = READ_CONSTANT();

        Value value = vmPop();
        Value object = vmPop();

        vmPush(root);
        vmPush(object);
        vmPush(key);
        vmPush(value);

        if (!executeMethod("opSetProperty", 3)) return false;

        vmPush(object);
        break;
      }
      case OP_VARIABLE: {
        vmPush(root);
        vmVariable(frame);
        if (!executeMethod("opVariable", 1)) return false;
        break;
      }
      case OP_SIGN: {
        // replace the signature's ast with the signature itself.
        Value astSignature = vmPeek(0);
        Value signature;
        mapGet(&AS_INSTANCE(astSignature)->fields, INTERN("function"),
               &signature);
        vmPop();
        vmPush(signature);

        vmClosure(frame);
        vmSign(frame);

        if (!astClosure(AS_CLOSURE(vmPeek(0)))) return false;
        break;
      }
      case OP_OVERLOAD: {
        if (!vmOverload(frame)) return false;
        if (!astOverload(AS_OVERLOAD(vmPeek(0)))) return false;
        break;
      }
      case OP_CLOSURE: {
        vmClosure(frame);
        if (!astClosure(AS_CLOSURE(vmPeek(0)))) return false;
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
      case OP_CALL_POSTFIX: {
        int argCount = READ_BYTE();
        Value postfix = vmPop();
        Value args[argCount];

        int i = argCount;
        while (i-- > 0) args[i] = vmPop();

        vmPush(root);

        vmPush(postfix);
        while (++i < argCount) vmPush(args[i]);

        if (!executeMethod("opCall", argCount + 1)) return false;
        break;
      }
      case OP_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();

        Value args[argCount];
        int i = argCount;
        while (i-- > 0) args[i] = vmPop();

        Value receiver = vmPop();
        vmPush(root);
        vmPush(receiver);
        vmPush(OBJ_VAL(method));
        while (++i < argCount) vmPush(args[i]);

        if (!executeMethod("opInvoke", argCount + 2)) return false;
        break;
      }

      case OP_SET_TYPE_LOCAL: {
        uint8_t slot = READ_SHORT();
        Value value = vmPeek(0);

        vmPush(root);
        vmPush(NUMBER_VAL(slot));
        vmPush(value);

        if (!executeMethod("opSetLocalType", 2)) return false;

        vmPop();  // nil.
        break;
      }
      case OP_SET_TYPE_GLOBAL:
        break;
      case OP_UNIT: {
        vmPush(root);
        vmPush(UNIT_VAL);
        if (!executeMethod("opLiteral", 1)) return false;
        break;
      }
      default: {
        vmRuntimeError("Unhandled destructured opcode (%i).", instruction);
        return false;
      }
    }
  }

  vmRuntimeError("This should be unreachable.");
  return false;
}

bool astClosure(ObjClosure* closure) {
  // we'll populate the frame's local slots as we build
  // the tree, and exit the frame once we're done.
  vmInitFrame(closure, 0);

  // the root of the tree is an [ASTClosure] instance that has
  // two arguments.
  vmPush(OBJ_VAL(vm.core.astClosure));

  // (1) the function itself.
  vmPush(OBJ_VAL(closure));

  // (2) the closure's upvalues.
  for (int i = 0; i < closure->upvalueCount; i++) {
    vmPush(OBJ_VAL(vm.core.astUpvalue));
    vmPush(NUMBER_VAL((uintptr_t)closure->upvalues[i]));
    vmPush(NUMBER_VAL(closure->upvalues[i]->slot));
    if (!initInstance(vm.core.astUpvalue, 2)) return false;
  }
  if (!vmTuplify(closure->upvalueCount, true)) return false;

  if (!initInstance(vm.core.astClosure, 2)) return false;

  return astFrame(vmPeek(0));
}

bool astOverload(ObjOverload* overload) {
  vmPop();

  vmPush(OBJ_VAL(vm.core.astOverload));
  vmPush(OBJ_VAL(overload));
  vmPush(NUMBER_VAL(overload->cases));

  for (int i = 0; i < overload->cases; i++) {
    vmPush(OBJ_VAL(overload->closures[i]));
    if (!astClosure(overload->closures[i])) return false;
  }

  if (!vmTuplify(overload->cases, true)) return false;

  return initInstance(vm.core.astOverload, 3);
}

bool ast(Value value) {
  switch (value.vType) {
    case VAL_OBJ: {
      switch (OBJ_TYPE(value)) {
        case OBJ_CLOSURE:
          return astClosure(AS_CLOSURE(value));
        case OBJ_OVERLOAD:
          return astOverload(AS_OVERLOAD(value));
        default:
          vmRuntimeError("Undestructurable object.");
          return false;
      }
      break;
    }
    default:
      vmRuntimeError("Undestructurable value.");
      return false;
  }

  vmRuntimeError("This should be unreachable.");
  return false;  // unreachable.
}