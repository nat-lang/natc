#ifndef nat_compiler_h
#define nat_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction* compile(const char* source, char* path);
void markCompilerRoots();

#endif
