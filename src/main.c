#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "io.h"
#include "vm.h"

static void repl() {
  char line[1024];
  for (;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(line, "repl");
  }
}

int main(int argc, const char* argv[]) {
  if (!initVM()) exit(2);

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    InterpretResult result = interpretFile(argv[1]);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
  } else if (false) {
    compileFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: nat [path]\n");
    exit(64);
  }

  freeVM();

  return 0;
}
