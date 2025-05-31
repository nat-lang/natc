#ifndef nat_io_h
#define nat_io_h

#include "vm.h"

char* pathToUri(char* path);
char* readSource(const char* path);
char* readRelativeSource(const char* path);

#endif