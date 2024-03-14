#include "ast.h"

#include <stdio.h>

// Read the syntax tree of a function off the tape.
bool readAST(ObjClosure* closure) {
  // the root of the tree.
  ObjInstance* node = newInstance(vm.classes.astNode);
  vmPush(OBJ_VAL(node));
  initClass(vm.classes.astNode, 0);

  printf("\nD 1\n");
  printf("\nSTART loop at framecount %i\n", vm.frameCount - 1);
  if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;
  vmPush(OBJ_VAL(node));
  printf("\nFINISH loop at framecount %i\n", vm.frameCount);

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
        printf("\nD 2\n");
        Value constant = READ_CONSTANT();

        vmPush(constant);

        if (!invoke(intern("addChild"), 1)) return false;
        if (execute(vm.frameCount - 1) != INTERPRET_OK) return false;

        break;
      }
      case OP_CALL: {
      }
    }
  }

  printf("\nD 2\n");

  vmPop();  // the node.
  vmPop();  // the destructured expression.

  vmPush(OBJ_VAL(node));

  return true;
}
