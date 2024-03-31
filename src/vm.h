#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  Value stack[STACK_MAX];
  Value* stackTop;

  CallFrame frames[FRAMES_MAX];
  int frameCount;
  ObjMap strings;
  ObjMap globals;
  ObjMap infixes;

  Obj* objects;
  ObjUpvalue* openUpvalues;

  // memory.
  int grayCount;
  int grayCapacity;
  Obj** grayStack;

  size_t bytesAllocated;
  size_t nextGC;

  // methods with special semantics.
  ObjString* initString;
  ObjString* callString;
  ObjString* iterString;
  ObjString* nextString;
  ObjString* addString;
  ObjString* memberString;
  ObjString* subscriptGetString;
  ObjString* subscriptSetString;
  ObjString* lengthString;
  ObjString* equalString;
  ObjString* hashString;

  ObjClass* seqClass;
  ObjClass* objClass;

} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

bool initVM();
void freeVM();
void runtimeError(const char* format, ...);

InterpretResult interpret(char* path, const char* source);
void vmPush(Value value);
Value vmPop();
Value vmPeek(int distance);
bool initClass(ObjClass* klass, int argCount);
bool invoke(ObjString* name, int argCount);
bool validateHashable(Value value);
bool vmCallValue(Value value, int argCount);
bool vmInstanceHas(ObjInstance* instance, Value value);

#endif
