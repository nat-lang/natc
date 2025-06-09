#ifndef nat_io_h
#define nat_io_h

#include "vm.h"

char* pathToUri(const char* dirName, const char* baseName);
char* readFile(const char* path);

#endif