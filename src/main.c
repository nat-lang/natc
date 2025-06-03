#include <getopt.h>
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

    vmInterpretExpr("repl", line);
  }
}

int main(int argc, char* argv[]) {
  int exitStatus = 0;

  if (!initVM()) exit(2);

  if (argc == 1) {
    repl();
  } else {
    InterpretResult status = vmInterpretEntrypoint((char*)argv[optind]);

    if (status == INTERPRET_COMPILE_ERROR) exitStatus = 65;
    if (status == INTERPRET_RUNTIME_ERROR) exitStatus = 70;

    freeVM();
    return exitStatus;
  }
}
