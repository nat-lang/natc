#include "ast.h"

#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "object.h"
#include "value.h"
#include "vm.h"

bool astClosure(Value* enclosing, ObjClosure* closure);
bool astLocal(uint8_t slot);
bool astOverload(Value* enclosing, ObjOverload* overload);

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
        if (!astLocal(slot)) return false;
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_SHORT();
        Value value = vmPeek(0);

        vmPush(root);

        if (!astLocal(slot)) return false;
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
        vmPush(OBJ_VAL(upvalue));

        if (!executeMethod("opGetUpvalue", 3)) return false;
        break;
      }
      case OP_GET_PROPERTY: {
        Value key = READ_CONSTANT();
        Value obj = vmPop();

        vmPush(root);
        vmPush(obj);
        vmPush(key);
        if (!executeMethod("opGetProperty", 2)) return false;

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
      case OP_EQUAL: {
        Value left = vmPop();
        Value right = vmPop();

        vmPush(root);
        vmPush(left);
        vmPush(right);

        if (!executeMethod("opEqual", 2)) return false;
        break;
      }
      case OP_VARIABLE: {
        vmPush(root);
        vmVariable(frame);
        if (!executeMethod("opVariable", 1)) return false;
        break;
      }
      case OP_SIGN: {
        vmClosure(frame);
        Value closure = vmPeek(1);
        if (!executeMethod("opSignature", 1)) return false;
        // pop the signature and leave the signed
        // function's ast on the stack.
        vmPop();
        vmPush(closure);
        break;
      }
      case OP_OVERLOAD: {
        if (!vmOverload(frame)) return false;
        if (!astOverload(&root, AS_OVERLOAD(vmPeek(0)))) return false;
        break;
      }
      case OP_CLOSURE: {
        vmClosure(frame);
        if (!astClosure(&root, AS_CLOSURE(vmPeek(0)))) return false;
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

      case OP_MEMBER: {
        Value obj = vmPop();
        Value val = vmPop();

        vmPush(root);
        vmPush(val);
        vmPush(obj);
        if (!executeMethod("opMember", 2)) return false;

        break;
      }

      case OP_SET_TYPE_LOCAL: {
        uint8_t slot = READ_SHORT();
        Value type = vmPeek(0);

        vmPush(root);
        if (!astLocal(slot)) return false;
        vmPush(type);

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

bool astLocal(uint8_t slot) {
  vmPush(OBJ_VAL(vm.core.astLocal));
  vmPush(NUMBER_VAL(slot));
  return initInstance(vm.core.astLocal, 1);
}

bool astUpvalues(ObjClosure* closure, bool root) {
  ObjClass* upvalue =
      root ? vm.core.astExternalUpvalue : vm.core.astInternalUpvalue;

  for (int i = 0; i < closure->upvalueCount; i++) {
    vmPush(OBJ_VAL(upvalue));
    vmPush(NUMBER_VAL((uintptr_t)closure->upvalues[i]));
    vmPush(NUMBER_VAL(closure->upvalues[i]->slot));
    vmPush(OBJ_VAL(closure->upvalues[i]));

    // if the closure is the root of the ast, any upvalues
    // it closes over are resolvable, so we distinguish them.
    if (!initInstance(upvalue, 3)) return false;
  }
  return vmTuplify(closure->upvalueCount, true);
}

bool astClosure(Value* enclosing, ObjClosure* closure) {
  vmInitFrame(closure, 0);

  // the root of the tree is an [ASTClosure] instance that
  // has three arguments.
  vmPush(OBJ_VAL(vm.core.astClosure));
  // (0) the enclosing function.
  vmPush(enclosing == NULL ? NIL_VAL : *enclosing);
  // (1) the function itself.
  vmPush(OBJ_VAL(closure));
  // (2) the closure's upvalues.
  if (!astUpvalues(closure, enclosing == NULL)) return false;

  if (!initInstance(vm.core.astClosure, 3)) return false;
  Value astClosure = vmPeek(0);

  // the function's arguments are undefined values.
  // the [ASTClosure] instance occupies the reserved slot.
  for (int i = 0; i < closure->function->arity; i++) vmPush(UNDEF_VAL);

  return astFrame(astClosure);
}

bool astBoundFunction(Value* enclosing, ObjBoundFunction* obj) {
  vmPop();

  if (obj->type == BOUND_NATIVE) {
    vmRuntimeError("Undestructurable native.");
    return false;
  }

  if (!IS_INSTANCE(obj->receiver) && !IS_CLASS(obj->receiver)) {
    vmRuntimeError("Receiver not instance or class.");
    return false;
  }

  ObjClosure* closure = obj->bound.method;
  ObjClass* klass = AS_INSTANCE(obj->receiver)->klass;

  vmPush(OBJ_VAL(vm.core.astMethod));
  vmPush(OBJ_VAL(klass));
  vmPush(OBJ_VAL(obj));
  vmPush(obj->receiver);
  vmPush(OBJ_VAL(closure));
  if (!astClosure(enclosing, closure)) return false;

  return initInstance(vm.core.astMethod, 4);
}

bool astOverload(Value* enclosing, ObjOverload* overload) {
  vmPop();

  vmPush(OBJ_VAL(vm.core.astOverload));
  vmPush(OBJ_VAL(overload));
  vmPush(NUMBER_VAL(overload->cases));

  for (int i = 0; i < overload->cases; i++) {
    vmPush(OBJ_VAL(overload->closures[i]));
    if (!astClosure(enclosing, overload->closures[i])) return false;
  }

  if (!vmTuplify(overload->cases, true)) return false;

  return initInstance(vm.core.astOverload, 3);
}

bool ast(Value value) {
  switch (value.vType) {
    case VAL_OBJ: {
      switch (OBJ_TYPE(value)) {
        case OBJ_BOUND_FUNCTION:
          return astBoundFunction(NULL, AS_BOUND_FUNCTION(value));
        case OBJ_CLOSURE:
          return astClosure(NULL, AS_CLOSURE(value));
        case OBJ_OVERLOAD:
          return astOverload(NULL, AS_OVERLOAD(value));
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