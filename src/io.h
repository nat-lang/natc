#ifndef nat_io_h
#define nat_io_h

#include "vm.h"

char* readPath(const char* path);
InterpretResult interpretFile(const char* path);

#endif