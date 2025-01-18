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

static void uErr() {
  fprintf(stderr,
          "Usage: nat [file] \n [-a argument] call [file] with argument");
  freeVM();
  exit(64);
}

int main(int argc, char* argv[]) {
  int exitStatus = 0;

  if (!initVM()) exit(2);

  if (argc == 1) {
    repl();
  } else {
    int opt;

    int paramc = 0;
    char* paramv[10];  // max 10 params.

    while ((opt = getopt(argc, argv, "a:")) != -1) {
      switch (opt) {
        case 'a':
          paramv[paramc++] = optarg;
          break;
        default: {
          freeVM();
          uErr();
        }
      }
    }

    if (argc - optind > 1) uErr();

    InterpretResult status;
    if (paramc > 0)
      status = vmInterpretEntrypoint((char*)argv[optind], paramc, paramv);
    else
      status = vmInterpretModule((char*)argv[optind]);

    if (status == INTERPRET_COMPILE_ERROR) exitStatus = 65;
    if (status == INTERPRET_RUNTIME_ERROR) exitStatus = 70;

    freeVM();
    return exitStatus;
  }
}
