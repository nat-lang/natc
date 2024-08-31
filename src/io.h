#ifndef nat_io_h
#define nat_io_h

#include "vm.h"

char* readSource(const char* path);
char* pathToUri(char* path);
InterpretResult interpretFile(const char* path);
#endif