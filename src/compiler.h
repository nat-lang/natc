#ifndef nat_compiler_h
#define nat_compiler_h

#include "common.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"
#include "vm.h"

#define S_EQ "__eq__"
#define S_IN "__in__"
#define S_LEN "__len__"
#define S_SUBSCRIPT_GET "__get__"
#define S_SUBSCRIPT_SET "__set__"

#define S_ADD "add"
#define S_CALL "call"
#define S_INIT "init"
#define S_ITER "iter"

#define S_OBJ "__obj__"
#define S_SEQUENCE "Sequence"
#define S_AST_NODE "ASTNode"
#define S_AST_CLOSURE "ASTClosure"

ObjFunction* compile(const char* source, char* path);
void markCompilerRoots();

#endif
