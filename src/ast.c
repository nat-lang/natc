#include "ast.h"

#include <stdio.h>

#include "core.h"

bool newNode(char* className, int argCount) {
  ObjClass* klass = getClass(className);
  ObjInstance* node = newInstance(klass);
  // initialize the instance.
  vmPush(OBJ_VAL(node));
  printf("1.1\n");
  if (!initClass(klass, argCount)) return false;
  printf("1.2\n");
  if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;
  printf("1.3\n");
  return true;
}

// Read the syntax tree of a function off the tape.
bool readAST(ObjClosure* closure) {
#ifdef DEBUG_TRACE_EXECUTION
  disassembleChunk(&closure->function->chunk, closure->function->name->chars);
  printf("\n");
#endif
  printf("0\n");

  // the root of the tree.
  if (!newNode("ASTNode", 0)) return false;

  Value root = vmPeek(0);
  Value node = root;

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
    printf("\t");
    disassembleInstruction(&frame->closure->function->chunk,
                           (int)(frame->ip - frame->closure->function->chunk.code));
    uint8_t instruction = READ_BYTE();

    if (instruction == OP_END) {
      printf("OP ENDING\n");
      break;
    }

    switch (instruction) {
      case OP_CONSTANT: {
        vmPush(node);
        vmPush(READ_CONSTANT());

        if (!invoke(intern("opConstant"), 1)) return false;
        if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;

        break;
      }
      case OP_RETURN: {
        disassembleStack();
        printf("3\n");
        if (!invoke(intern("opReturn"), 1)) return false;
        if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;

        break;
      }
      default: {
        runtimeError("Unhandled destructured opcode.");
        return false;
      }
    }
  }

  printf("\nD 2\n");

  vmPop();  // the root.
  vmPop();  // the destructured expression.

  vmPush(root);

  return true;
}
