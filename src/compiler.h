#ifndef nat_compiler_h
#define nat_compiler_h

#include "common.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_ANONYMOUS,
  TYPE_IMPLICIT,
  TYPE_BOUND,
  TYPE_METHOD,
  TYPE_MODULE,
  TYPE_INITIALIZER,
} FunctionType;

typedef struct {
  // local slot of the bound variable.
  int var;
  // local slot of the object that implements the protocol.
  int obj;
  // stack offset of the first instruction of the body.
  int loopStart;
} Iterator;

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
} ClassCompiler;

typedef struct Compiler {
  struct Compiler* enclosing;
  struct Compiler* signature;
  ObjFunction* function;
  FunctionType functionType;

  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
} Compiler;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,       // =
  PREC_TYPE_ASSIGNMENT,  // : _ =
  PREC_OR,               // or
  PREC_AND,              // and
  PREC_EQUALITY,         // == !=
  PREC_COMPARISON,       // < > <= >=
  PREC_TERM,             // + -
  PREC_FACTOR,           // * /
  PREC_UNARY,            // ! -
  PREC_CALL,             // . ()
  PREC_PRIMARY
} Precedence;

ObjFunction* compileModule(Compiler* root, const char* source, Token path,
                           ObjModule* module);
void markCompilerRoots(Compiler* cmp);

#endif
