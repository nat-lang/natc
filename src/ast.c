#include "ast.h"

#include <stdio.h>

bool readAST(ObjClosure* closure) {
  ObjInstance* node = newInstance(vm.classes.astNode);

  vmPush(OBJ_VAL(node));

  CallFrame* frame = &vm.frames[vm.frameCount + 1];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = NULL;

  printf("\n\tINSTRUCTION COUNT: %i\n", closure->function->chunk.count);
  disassembleChunk(&closure->function->chunk, "CLO");

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

    uint8_t* instruction = frame->ip++;

    if (instruction == NULL) break;

    switch (*instruction) {
      case OP_CONSTANT: {
        printf("\nD 1\n");
        Value constant = READ_CONSTANT();

        vmPush(constant);

        if (!invoke(intern("addChild"), 1)) return false;

        break;
      }
    }
  }

  printf("\nD 2\n");

  vmPop();  // the node.
  vmPop();  // the destructured expression.

  vmPush(OBJ_VAL(node));

  return true;
}
