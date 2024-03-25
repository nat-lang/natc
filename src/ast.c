#include "ast.h"

#include <stdio.h>

#include "core.h"

bool newNode(char* className, int argCount) {
  ObjClass* klass = getClass(className);
  ObjInstance* node = newInstance(klass);
  // initialize the instance.
  vmPush(OBJ_VAL(node));
  if (!initClass(klass, argCount)) return false;
  if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;

  return true;
}

bool executeMethod(char* method, int argCount) {
  return (invoke(intern(method), argCount)) &&
         (execute(vm.frameCount - 1) == INTERPRET_OK);
}

// Read the syntax tree of a function off the tape.
bool readAST(ObjClosure* closure) {
#ifdef DEBUG_TRACE_EXECUTION
  disassembleChunk(&closure->function->chunk, closure->function->name->chars);
  printf("\n");
#endif

  // the root of the tree.
  if (!newNode("ASTClosure", 0)) return false;

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
    disassembleInstruction(&frame->closure->function->chunk,
                           (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction = READ_BYTE();

    switch (instruction) {
      case OP_CONSTANT: {
        vmPush(root);
        vmPush(READ_CONSTANT());

        if (!executeMethod("opConstant", 1)) return false;
        break;
      }
      case OP_RETURN: {
        if (!executeMethod("opReturn", 1)) return false;

        break;
      }
      case OP_NIL: {
        vmPush(root);
        if (!executeMethod("opNil", 0)) return false;
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
      case OP_CALL_INFIX: {
        if (!executeMethod("opCallInfix", 3)) return false;
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
        runtimeError("Unhandled destructured opcode.");
        return false;
      }
    }
  }

  runtimeError("This should be unreachable.");
  return false;
}