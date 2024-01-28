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

    interpret(line);
  }
}

int main(int argc, const char* argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else if (false) {
    compileFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: nat [path]\n");
    exit(64);
  }

  freeVM();

  return 0;
}
