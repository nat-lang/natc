#ifndef native_h
#define native_h

#include "object.h"

void defineNativeFn(char* name, int arity, bool variadic, NativeFn function,
                    ObjMap* dest);
void defineNatives();

#endif
