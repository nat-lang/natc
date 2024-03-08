#ifndef nat_compiler_h
#define nat_compiler_h

#include "common.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"
#include "vm.h"

ObjFunction* compile(const char* source, char* path);
void markCompilerRoots();

#endif
