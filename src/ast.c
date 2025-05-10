#include "ast.h"

#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "object.h"
#include "value.h"
#include "vm.h"

bool astClosure(Value* enclosing, ObjClosure* closure, ObjClass* closureClass);
bool astLocal(uint8_t slot, ObjFunction* function);
bool astGlobal(ObjString* name);
bool astOverload(ObjOverload* overload);
bool astChunk(CallFrame* frame, uint8_t* ipEnd, Value root);
bool astBlock(Value* enclosing);
bool astIter(CallFrame* frame, Value root, char* method);
bool astConditional(CallFrame* frame, Value root, char* method);

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

      OK_IF(vmExecuteMethod("opLiteral", 1));
    }
    case OP_CONSTANT: {
      vmPush(root);
      vmPush(READ_CONSTANT());
      OK_IF(vmExecuteMethod("opLiteral", 1));
    }
    case OP_NIL: {
      vmPush(root);
      vmPush(NIL_VAL);
      OK_IF(vmExecuteMethod("opLiteral", 1));
    }
    case OP_TRUE: {
      vmPush(root);
      vmPush(BOOL_VAL(true));
      OK_IF(vmExecuteMethod("opLiteral", 1));
    }
    case OP_FALSE: {
      vmPush(root);
      vmPush(BOOL_VAL(false));
      OK_IF(vmExecuteMethod("opLiteral", 1));
    }
    case OP_NOT: {
      Value value = vmPop();
      vmPush(root);
      vmPush(value);
      OK_IF(vmExecuteMethod("opNot", 1));
    }
    case OP_EXPR_STATEMENT: {
      Value value = vmPop();
      vmPush(root);
      vmPush(value);

      FAIL_UNLESS(vmExecuteMethod("opExprStatement", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_POP:
      vmPop();
      return AST_INSTRUCTION_OK;
    case OP_RETURN: {
      RETURN();

      FAIL_UNLESS(vmExecuteMethod("opReturn", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_IMPLICIT_RETURN: {
      RETURN();

      FAIL_UNLESS(vmExecuteMethod("opImplicitReturn", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_COMPLETE;
    }
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      Chunk chunk = frame->closure->function->chunk;
      // translate until the last two instructions: the implicit
      // return and its payload, which mark the end of the ast.
      OK_IF(astChunk(frame, chunk.code + chunk.count - 1, root));
    }
    case OP_JUMP_IF_FALSE:
      OK_IF(astConditional(frame, root, "opConditional"));
    case OP_ITER:
      OK_IF(astIter(frame, root, "opIter"));
    case OP_LOOP: {
      READ_SHORT();
      // here OP_LOOP just concludes a scope.
      return AST_INSTRUCTION_COMPLETE;
    }
    case OP_GET_GLOBAL: {
      ObjString* name = READ_STRING();
      vmPush(root);
      vmPush(OBJ_VAL(name));

      OK_IF(vmExecuteMethod("opGetGlobal", 1));
    }
    case OP_DEFINE_GLOBAL:
    case OP_SET_GLOBAL: {
      ObjString* name = READ_STRING();
      Value value = vmPeek(0);

      vmPush(root);

      if (!astGlobal(name)) return AST_INSTRUCTION_FAIL;
      vmPush(value);

      FAIL_UNLESS(vmExecuteMethod("opSetGlobalValue", 2));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_GET_LOCAL:
      OK_IF(astLocal(READ_SHORT(), frame->closure->function));
    case OP_SET_LOCAL: {
      uint16_t slot = READ_SHORT();
      Value value = vmPeek(0);

      vmPush(root);

      if (!astLocal(slot, frame->closure->function))
        return AST_INSTRUCTION_FAIL;
      vmPush(value);

      FAIL_UNLESS(vmExecuteMethod("opSetLocalValue", 2));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = READ_SHORT();
      ObjUpvalue* upvalue = frame->closure->upvalues[slot];

      vmPush(root);
      vmPush(NUMBER_VAL((uintptr_t)upvalue));

      OK_IF(vmExecuteMethod("opGetUpvalue", 1));
    }
    case OP_GET_PROPERTY: {
      Value key = READ_CONSTANT();
      Value obj = vmPop();

      vmPush(root);
      vmPush(obj);
      vmPush(key);
      OK_IF(vmExecuteMethod("opGetProperty", 2));
    }
    case OP_SET_PROPERTY: {
      Value key = READ_CONSTANT();

      Value value = vmPop();
      Value object = vmPop();

      vmPush(root);
      vmPush(object);
      vmPush(key);
      vmPush(value);

      FAIL_UNLESS(vmExecuteMethod("opSetProperty", 3));
      vmPush(object);
      return AST_INSTRUCTION_OK;
    }
    case OP_EQUAL: {
      Value left = vmPop();
      Value right = vmPop();

      vmPush(root);
      vmPush(right);
      vmPush(left);

      OK_IF(vmExecuteMethod("opEqual", 2));
    }
    case OP_VARIABLE: {
      vmPush(root);
      vmVariable(frame);
      OK_IF(vmExecuteMethod("opVariable", 1));
    }
    case OP_CLOSE_UPVALUE: {
      vmCloseUpvalues(vm.stackTop - 1);
      vmPop();
      return AST_INSTRUCTION_OK;
    }
    case OP_SIGN: {
      vmClosure(frame);
      Value closure = vmPeek(1);
      FAIL_UNLESS(vmExecuteMethod("opSignature", 1));
      // pop the signature and leave the signed
      // function's ast on the stack.
      vmPop();
      vmPush(closure);
      return AST_INSTRUCTION_OK;
    }
    case OP_OVERLOAD: {
      int i = *frame->ip;
      int count = i;

      // we have a stack of ASTClosure instances that need
      // to turn into an ObjOverload. extract the raw closures
      // and put them on the stack for vmOverload.

      Value closures[count];
      while (--i >= 0) {
        Value closureAst = vmPeek(i);

        if (!IS_INSTANCE(closureAst) ||
            AS_INSTANCE(closureAst)->klass != vm.core.astClosure) {
          vmRuntimeError("Expecting ASTClosure instance.");
          return AST_INSTRUCTION_FAIL;
        }
        Value rawClosure;
        if (!mapGet(&AS_INSTANCE(closureAst)->fields,
                    OBJ_VAL(vm.core.sFunction), &rawClosure)) {
          vmRuntimeError("ASTClosure missing its 'function' field.");
          return AST_INSTRUCTION_FAIL;
        }

        closures[count - i - 1] = rawClosure;
      }

      while (++i < count) vmPush(closures[i]);

      FAIL_UNLESS(vmOverload(frame));

      // now put it beneath the stack of closures.
      Value overload = vmPop();
      Value astClosures[count];
      while (--i >= 0) astClosures[i] = vmPop();
      vmPush(overload);
      while (++i < count) vmPush(astClosures[i]);

      // finally create the ast overload object.
      OK_IF(astOverload(AS_OVERLOAD(overload)));
    }
    case OP_CLOSURE:
      vmClosure(frame);
      OK_IF(astClosure(&root, AS_CLOSURE(vmPeek(0)), vm.core.astClosure));
    case OP_COMPREHENSION:
      vmClosure(frame);
      OK_IF(astClosure(&root, AS_CLOSURE(vmPeek(0)), vm.core.astComprehension));
    case OP_COMPREHENSION_PRED:
      OK_IF(astConditional(frame, root, "opComprehensionPred"));
    case OP_COMPREHENSION_ITER:
      OK_IF(astIter(frame, root, "opComprehensionIter"));
    case OP_COMPREHENSION_BODY: {
      Value expr = vmPeek(0);
      vmPush(root);
      vmPush(expr);
      FAIL_UNLESS(vmExecuteMethod("opComprehensionBody", 1));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_CALL: {
      int argCount = READ_BYTE();
      Value args[argCount];

      for (int i = 0; i < argCount; i++) args[i] = vmPop();
      Value fn = vmPop();
      vmPush(root);
      vmPush(fn);
      for (int i = argCount - 1; i >= 0; i--) vmPush(args[i]);

      OK_IF(vmExecuteMethod("opCall", argCount + 1));
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

      OK_IF(vmExecuteMethod("opCallInfix", 3));
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

      OK_IF(vmExecuteMethod("opCall", argCount + 1));
    }
    case OP_MEMBER: {
      Value obj = vmPop();
      Value val = vmPop();

      vmPush(root);
      vmPush(val);
      vmPush(obj);
      OK_IF(vmExecuteMethod("opMember", 2));
    }
    case OP_IMPORT: {
      ObjModule* module = AS_MODULE(READ_CONSTANT());

      vmPush(root);

      FAIL_UNLESS(vmImportAsInstance(module));
      frame = &vm.frames[vm.frameCount - 1];

      if (!vmExecuteMethod("opImport", 1)) return AST_INSTRUCTION_FAIL;
      vmPop();  // nil.

      return AST_INSTRUCTION_OK;
    }
    case OP_IMPORT_AS: {
      ObjModule* module = AS_MODULE(READ_CONSTANT());
      Value alias = READ_CONSTANT();
      vmPush(root);

      FAIL_UNLESS(vmImportAsInstance(module));
      frame = &vm.frames[vm.frameCount - 1];

      vmPush(alias);

      if (!vmExecuteMethod("opImportAs", 2)) return AST_INSTRUCTION_FAIL;
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_SET_TYPE_LOCAL: {
      uint8_t slot = READ_SHORT();
      Value type = vmPeek(0);

      vmPush(root);
      if (!astLocal(slot, frame->closure->function))
        return AST_INSTRUCTION_FAIL;
      vmPush(type);

      FAIL_UNLESS(vmExecuteMethod("opSetLocalType", 2));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_SET_TYPE_GLOBAL: {
      ObjString* name = READ_STRING();
      Value type = vmPeek(0);

      vmPush(root);
      if (!astGlobal(name)) return AST_INSTRUCTION_FAIL;
      vmPush(type);

      FAIL_UNLESS(vmExecuteMethod("opSetGlobalType", 2));
      vmPop();  // nil.
      return AST_INSTRUCTION_OK;
    }
    case OP_UNIT: {
      vmPush(root);
      vmPush(UNIT_VAL);
      OK_IF(vmExecuteMethod("opLiteral", 1));
    }
    case OP_QUANTIFY: {
      Value body = vmPop();
      Value restriction = vmPop();
      Value quantifier = vmPop();

      vmPush(OBJ_VAL(vm.core.astLocal));

      vmPush(quantifier);
      vmPush(restriction);
      vmPush(body);

      OK_IF(vmInitInstance(vm.core.astQuantification, 3));
    }
    default: {
      vmRuntimeError("Unhandled destructured opcode (%i).", instruction);
      return AST_INSTRUCTION_FAIL;
    }
  }

  return AST_INSTRUCTION_FAIL;
}

bool astIter(CallFrame* frame, Value root, char* method) {
  uint16_t offset = READ_SHORT();
  uint8_t* ip = frame->ip;
  uint16_t local = READ_SHORT();
  uint8_t iter = READ_SHORT();

  Value iterator = frame->slots[iter];

  vmPush(root);
  // local slot.
  if (!astLocal(local, frame->closure->function)) return false;
  // iterator.
  vmPush(iterator);
  // body; translate up to OP_LOOP.
  if (!astBlock(&root)) return false;
  Value branch = vmPeek(0);
  if (!astChunk(frame, ip + offset, branch)) return false;

  if (!vmExecuteMethod(method, 3)) return false;
  vmPop();  // nil.

  // now resume the negative branch on the root.
  frame->ip = ip + offset;

  return true;
}

bool astConditional(CallFrame* frame, Value root, char* method) {
  uint16_t offset = READ_SHORT();
  uint8_t* ip = frame->ip;
  Value condition = vmPeek(0);

  // we translate this instruction into an
  // [ASTConditional] node with two children.
  vmPush(root);

  // 1) the condition itself.
  vmPush(condition);

  // 2) the branch if the condition is true.
  if (!astBlock(&root)) return false;
  Value branch = vmPeek(0);
  // the true branch expects the condition on the stack.
  vmPush(condition);
  if (!astChunk(frame, ip + offset, branch)) return false;
  if (!vmExecuteMethod(method, 2)) return false;
  vmPop();  // nil.

  // now resume the negative branch on the root.
  frame->ip = ip + offset;

  return true;
}

bool astBlock(Value* enclosing) {
  vmPush(OBJ_VAL(vm.core.astBlock));
  vmPush(enclosing == NULL ? NIL_VAL : *enclosing);
  return vmInitInstance(vm.core.astBlock, 1);
}

// Append the segment of [frame]'s instructions
// delimited by [ipEnd] to [root].
bool astChunk(CallFrame* frame, uint8_t* ipEnd, Value root) {
  while (frame->ip <= ipEnd) {
    TRACE_EXECUTION("\n (ast chunk) ");
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
  for (;;) {
    TRACE_EXECUTION("\n (ast frame) ");

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

  vmRuntimeError("Expected to be unreachable.");
  return false;
}

bool astLocal(uint8_t slot, ObjFunction* function) {
  Token token = function->locals[slot].name;
  vmPush(OBJ_VAL(vm.core.astLocal));
  vmPush(NUMBER_VAL(slot));
  vmPush(OBJ_VAL(copyString(token.start, token.length)));
  return vmInitInstance(vm.core.astLocal, 2);
}

bool astGlobal(ObjString* name) {
  vmPush(OBJ_VAL(vm.core.astGlobal));
  vmPush(OBJ_VAL(name));
  return vmInitInstance(vm.core.astGlobal, 1);
}

bool astUpvalues(ObjClosure* closure, bool root) {
  // if the closure is the root of the ast, any upvalues
  // it closes over are resolvable, so we distinguish them.
  ObjClass* astClass =
      root ? vm.core.astExternalUpvalue : vm.core.astInternalUpvalue;

  for (int i = 0; i < closure->upvalueCount; i++) {
    vmPush(OBJ_VAL(astClass));
    vmPush(NUMBER_VAL((uintptr_t)closure->upvalues[i]));
    vmPush(NUMBER_VAL(closure->upvalues[i]->slot));
    vmPush(OBJ_VAL(closure->upvalues[i]));
    vmPush(OBJ_VAL(closure->upvalues[i]->name));

    if (!vmInitInstance(astClass, 4)) return false;
  }
  return vmTuplify(closure->upvalueCount, true);
}

bool astClosure(Value* enclosing, ObjClosure* closure, ObjClass* closureClass) {
  vmInitFrame(closure, 0);

  // the root of the tree is an [ASTClosure] instance that
  // has three arguments.
  vmPush(OBJ_VAL(closureClass));
  // (0) the enclosing function.
  vmPush(enclosing == NULL ? NIL_VAL : *enclosing);
  // (1) the function itself.
  vmPush(OBJ_VAL(closure));
  // (2) the closure's upvalues.
  if (!astUpvalues(closure, enclosing == NULL)) return false;

  if (!vmInitInstance(closureClass, 3)) return false;
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
  if (!astClosure(enclosing, closure, vm.core.astClosure)) return false;

  return vmInitInstance(vm.core.astMethod, 4);
}

bool astOverload(ObjOverload* overload) {
  if (!vmTuplify(overload->cases, true)) return false;
  Value cases = vmPop();
  vmPop();  // the overload;

  vmPush(OBJ_VAL(vm.core.astOverload));
  vmPush(OBJ_VAL(overload));
  vmPush(NUMBER_VAL(overload->cases));
  vmPush(cases);

  return vmInitInstance(vm.core.astOverload, 3);
}

bool ast(Value value) {
  switch (value.vType) {
    case VAL_OBJ: {
      switch (OBJ_TYPE(value)) {
        case OBJ_BOUND_FUNCTION:
          return astBoundFunction(NULL, AS_BOUND_FUNCTION(value));
        case OBJ_CLOSURE:
          return astClosure(NULL, AS_CLOSURE(value), vm.core.astClosure);
        case OBJ_OVERLOAD: {
          ObjOverload* overload = AS_OVERLOAD(vmPeek(0));

          // wrap the cases in ast.
          for (int i = 0; i < overload->cases; i++) {
            vmPush(OBJ_VAL(overload->closures[i]));
            if (!astClosure(NULL, overload->closures[i], vm.core.astClosure))
              return false;
          }

          return astOverload(AS_OVERLOAD(value));
        }
        case OBJ_MODULE:
          return astClosure(NULL, AS_MODULE(value)->closure,
                            vm.core.astClosure);
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

  vmRuntimeError("Expected to be unreachable.");
  return false;
}