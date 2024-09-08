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
bool astChunk(CallFrame* frame, uint8_t* ipEnd, Value root);
bool astBlock(Value* enclosing);

static bool executeMethod(char* method, int argCount) {
  return vmExecuteMethod(method, argCount, 1);
}

static bool initInstance(ObjClass* klass, int argCount) {
  return vmInitInstance(klass, argCount, 1);
}

typedef enum {
  AST_INSTRUCTION_OK,
  AST_INSTRUCTION_FAIL,
  AST_INSTRUCTION_COMPLETE
} ASTInstructionResult;

ASTInstructionResult astInstruction(CallFrame* frame, Value root) {
  uint8_t instruction = READ_BYTE();

#define OK_IF(success) \
  return success ? AST_INSTRUCTION_OK : AST_INSTRUCTION_FAIL
#define FAIL_UNLESS(success) \
  if (!success) return AST_INSTRUCTION_FAIL
#define RETURN()                   \
  do {                             \
    Value value = vmPop();         \
    vmCloseUpvalues(frame->slots); \
    vmPush(root);                  \
    vmPush(value);                 \
  } while (0)

  switch (instruction) {
    case OP_UNDEFINED: {
      vmPush(root);
      vmPush(UNDEF_VAL);

      OK_IF(executeMethod("opLiteral", 1));
    }
    case OP_CONSTANT: {
      vmPush(root);
      vmPush(READ_CONSTANT());
      OK_IF(executeMethod("opLiteral", 1));
    }
    case OP_NIL: {
      vmPush(root);
      vmPush(NIL_VAL);
      OK_IF(executeMethod("opLiteral", 1));
    }
    case OP_TRUE: {
      vmPush(root);
      vmPush(BOOL_VAL(true));
      OK_IF(executeMethod("opLiteral", 1));
    }
    case OP_FALSE: {
      vmPush(root);
      vmPush(BOOL_VAL(false));
      OK_IF(executeMethod("opLiteral", 1));
    }
    case OP_NOT: {
      Value value = vmPop();
      vmPush(root);
      vmPush(value);
      OK_IF(executeMethod("opNot", 1));
    }
    case OP_EXPR_STATEMENT: {
      Value value = vmPop();
      vmPush(root);
      vmPush(value);

      FAIL_UNLESS(executeMethod("opExprStatement", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_POP:
      vmPop();
      return AST_INSTRUCTION_OK;
    case OP_RETURN: {
      RETURN();

      FAIL_UNLESS(executeMethod("opReturn", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_IMPLICIT_RETURN: {
      RETURN();

      FAIL_UNLESS(executeMethod("opImplicitReturn", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_COMPLETE;
    }
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;

      // translate until the last two instructions: the implicit
      // return and its payload, which mark the end of the ast.
      FAIL_UNLESS(astChunk(frame,
                           frame->closure->function->chunk.code +
                               frame->closure->function->chunk.count,
                           root));

      return AST_INSTRUCTION_OK;
    }
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();
      Value condition = vmPeek(0);
      uint8_t* ip = frame->ip;

      // we translate this instruction into an
      // [ASTConditional] node with two children.
      vmPush(root);
      // 1) the condition itself.
      vmPush(condition);
      // 2) the branch if the condition is true.
      FAIL_UNLESS(astBlock(&root));

      Value branch = vmPeek(0);
      vmPush(condition);
      FAIL_UNLESS(astChunk(frame, frame->ip + offset, branch));
      // push the conditional onto the tree.
      FAIL_UNLESS(executeMethod("opConditional", 2));
      vmPop();  // nil.

      // now resume the negative branch on the root.
      frame->ip = ip + offset;

      return AST_INSTRUCTION_OK;
    }
    case OP_GET_GLOBAL: {
      ObjString* name = READ_STRING();
      vmPush(root);
      vmPush(OBJ_VAL(name));

      OK_IF(executeMethod("opGetGlobal", 1));
    }
    case OP_GET_LOCAL:
      OK_IF(astLocal(READ_SHORT()));
    case OP_SET_LOCAL: {
      uint8_t slot = READ_SHORT();
      Value value = vmPeek(0);

      vmPush(root);

      if (!astLocal(slot)) return AST_INSTRUCTION_FAIL;
      vmPush(value);

      FAIL_UNLESS(executeMethod("opSetLocalValue", 2));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = READ_SHORT();
      ObjUpvalue* upvalue = frame->closure->upvalues[slot];

      vmPush(root);
      vmPush(NUMBER_VAL((uintptr_t)upvalue));

      OK_IF(executeMethod("opGetUpvalue", 1));
    }
    case OP_GET_PROPERTY: {
      Value key = READ_CONSTANT();
      Value obj = vmPop();

      vmPush(root);
      vmPush(obj);
      vmPush(key);
      OK_IF(executeMethod("opGetProperty", 2));
    }
    case OP_SET_PROPERTY: {
      Value key = READ_CONSTANT();

      Value value = vmPop();
      Value object = vmPop();

      vmPush(root);
      vmPush(object);
      vmPush(key);
      vmPush(value);

      FAIL_UNLESS(executeMethod("opSetProperty", 3));
      vmPush(object);
      return AST_INSTRUCTION_OK;
    }
    case OP_EQUAL: {
      Value left = vmPop();
      Value right = vmPop();

      vmPush(root);
      vmPush(left);
      vmPush(right);

      OK_IF(executeMethod("opEqual", 2));
    }
    case OP_VARIABLE: {
      vmPush(root);
      vmVariable(frame);
      OK_IF(executeMethod("opVariable", 1));
    }
    case OP_SIGN: {
      vmClosure(frame);
      Value closure = vmPeek(1);
      FAIL_UNLESS(executeMethod("opSignature", 1));
      // pop the signature and leave the signed
      // function's ast on the stack.
      vmPop();
      vmPush(closure);
      return AST_INSTRUCTION_OK;
    }
    case OP_OVERLOAD:
      OK_IF(vmOverload(frame) && astOverload(&root, AS_OVERLOAD(vmPeek(0))));
    case OP_CLOSURE:
      vmClosure(frame);
      OK_IF(astClosure(&root, AS_CLOSURE(vmPeek(0))));
    case OP_CALL: {
      int argCount = READ_BYTE();
      Value args[argCount];

      for (int i = 0; i < argCount; i++) args[i] = vmPop();
      Value fn = vmPop();
      vmPush(root);
      vmPush(fn);
      for (int i = argCount - 1; i >= 0; i--) vmPush(args[i]);

      OK_IF(executeMethod("opCall", argCount + 1));
    }
    case OP_CALL_INFIX: {
      READ_SHORT();
      Value right = vmPop();
      Value infix = vmPop();
      Value left = vmPop();

      vmPush(root);
      vmPush(infix);
      vmPush(left);
      vmPush(right);

      OK_IF(executeMethod("opCall", 3));
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

      OK_IF(executeMethod("opCall", argCount + 1));
    }
    case OP_MEMBER: {
      Value obj = vmPop();
      Value val = vmPop();

      vmPush(root);
      vmPush(val);
      vmPush(obj);
      OK_IF(executeMethod("opMember", 2));
    }

    case OP_SET_TYPE_LOCAL: {
      uint8_t slot = READ_SHORT();
      Value type = vmPeek(0);

      vmPush(root);
      if (!astLocal(slot)) return AST_INSTRUCTION_FAIL;
      vmPush(type);

      FAIL_UNLESS(executeMethod("opSetLocalType", 2));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_SET_TYPE_GLOBAL:
      return AST_INSTRUCTION_OK;
    case OP_UNIT: {
      vmPush(root);
      vmPush(UNIT_VAL);
      OK_IF(executeMethod("opLiteral", 1));
    }
    default: {
      vmRuntimeError("Unhandled destructured opcode (%i).", instruction);
      return AST_INSTRUCTION_FAIL;
    }
  }

  return AST_INSTRUCTION_FAIL;
}

bool astBlock(Value* enclosing) {
  vmPush(OBJ_VAL(vm.core.astBlock));
  vmPush(enclosing == NULL ? NIL_VAL : *enclosing);
  return initInstance(vm.core.astBlock, 1);
}

// Append the segment of [frame]'s instructions
// delimited by [ipEnd] to [root].
bool astChunk(CallFrame* frame, uint8_t* ipEnd, Value root) {
  while (frame->ip <= ipEnd) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    disassembleStack();
    printf("\n");
    printf("  (ast chunk) ");
    printf("  ");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    switch (astInstruction(frame, root)) {
      case AST_INSTRUCTION_OK:
        break;
      case AST_INSTRUCTION_FAIL:
        return false;
      case AST_INSTRUCTION_COMPLETE:
        return true;
    }
  }
  return true;
}

// Translate a [CallFrame] into an [ASTNode].
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
    printf("  (ast frame) ");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    switch (astInstruction(frame, root)) {
      case AST_INSTRUCTION_OK:
        break;
      case AST_INSTRUCTION_FAIL:
        return false;
      case AST_INSTRUCTION_COMPLETE: {
        vm.frameCount--;
        vm.stackTop = frame->slots - 1;  // point at [closure] - 1.
        vmPush(root);                    // leave the ast on the stack.
        return true;
      }
    }
  }

  vmRuntimeError("Supposedly unreachable.");
  return false;
}

bool astLocal(uint8_t slot) {
  vmPush(OBJ_VAL(vm.core.astLocal));
  vmPush(NUMBER_VAL(slot));
  return initInstance(vm.core.astLocal, 1);
}

bool astUpvalues(ObjClosure* closure, bool root) {
  // if the closure is the root of the ast, any upvalues
  // it closes over are resolvable, so we distinguish them.
  ObjClass* upvalue =
      root ? vm.core.astExternalUpvalue : vm.core.astInternalUpvalue;

  for (int i = 0; i < closure->upvalueCount; i++) {
    vmPush(OBJ_VAL(upvalue));
    vmPush(NUMBER_VAL((uintptr_t)closure->upvalues[i]));
    vmPush(NUMBER_VAL(closure->upvalues[i]->slot));
    vmPush(OBJ_VAL(closure->upvalues[i]));

    if (!initInstance(upvalue, 3)) return AST_INSTRUCTION_FAIL;
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
  if (!astUpvalues(closure, enclosing == NULL)) return AST_INSTRUCTION_FAIL;

  if (!initInstance(vm.core.astClosure, 3)) return AST_INSTRUCTION_FAIL;
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
    return AST_INSTRUCTION_FAIL;
  }

  if (!IS_INSTANCE(obj->receiver) && !IS_CLASS(obj->receiver)) {
    vmRuntimeError("Receiver not instance or class.");
    return AST_INSTRUCTION_FAIL;
  }

  ObjClosure* closure = obj->bound.method;
  ObjClass* klass = AS_INSTANCE(obj->receiver)->klass;

  vmPush(OBJ_VAL(vm.core.astMethod));
  vmPush(OBJ_VAL(klass));
  vmPush(OBJ_VAL(obj));
  vmPush(obj->receiver);
  vmPush(OBJ_VAL(closure));
  if (!astClosure(enclosing, closure)) return AST_INSTRUCTION_FAIL;

  return initInstance(vm.core.astMethod, 4);
}

bool astOverload(Value* enclosing, ObjOverload* overload) {
  vmPop();

  vmPush(OBJ_VAL(vm.core.astOverload));
  vmPush(OBJ_VAL(overload));
  vmPush(NUMBER_VAL(overload->cases));

  for (int i = 0; i < overload->cases; i++) {
    vmPush(OBJ_VAL(overload->closures[i]));
    if (!astClosure(enclosing, overload->closures[i]))
      return AST_INSTRUCTION_FAIL;
  }

  if (!vmTuplify(overload->cases, true)) return AST_INSTRUCTION_FAIL;

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
          return AST_INSTRUCTION_FAIL;
      }
      break;
    }
    default:
      vmRuntimeError("Undestructurable value.");
      return AST_INSTRUCTION_FAIL;
  }

  vmRuntimeError("This should be unreachable.");
  return AST_INSTRUCTION_FAIL;  // unreachable.
}