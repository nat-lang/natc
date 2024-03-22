#include "ast.h"

#include <stdio.h>

// Read the syntax tree of a function off the tape.
bool readAST(ObjClosure* closure) {
#ifdef DEBUG_TRACE_EXECUTION
  disassembleChunk(&closure->function->chunk, closure->function->name->chars);
  printf("\n");
#endif

  // the root of the tree.
  ObjInstance* node = newInstance(vm.classes.astNode);
  vmPush(OBJ_VAL(node));
  initClass(vm.classes.astNode, 0);
  if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;
  vmPush(OBJ_VAL(node));

  CallFrame* frame = &vm.frames[vm.frameCount + 1];
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

    if (instruction == OP_END) break;

    switch (instruction) {
      case OP_CONSTANT: {
        vmPush(READ_CONSTANT());

        if (!invoke(intern("opConstant"), 1)) return false;
        if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;

        break;
      }
      case OP_RETURN: {
        printf("here 0?\n");
        if (!invoke(intern("opReturn"), 0)) return false;
        printf("here 1?\n");
        if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;
        printf("here 2?\n");
        // skip the implicit final return statement.
        goto exit_loop;
      }
      case OP_CALL: {
      }
      case OP_NIL: {
      }
    }
  }

  printf("\nD 2\n");

exit_loop:
  vmPop();  // the node.
  vmPop();  // the destructured expression.

  vmPush(OBJ_VAL(node));
  ;

  return true;
}
