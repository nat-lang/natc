#ifndef nat_io_h
#define nat_io_h

#import "vm.h"

void compileFile(const char* path);
InterpretResult interpretFile(const char* path);

#endif