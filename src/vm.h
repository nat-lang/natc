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
  // the tape and its machinery.
  Value stack[STACK_MAX];
  Value* stackTop;

  CallFrame frames[FRAMES_MAX];
  int frameCount;
  ObjMap strings;
  ObjMap globals;
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
  ObjString* memberString;

  ObjClass* seqClass;
  ObjClass* mapClass;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
void runtimeError(const char* format, ...);

InterpretResult interpret(const char* source);
void push(Value value);
Value pop();
Value peek(int distance);
bool validateMapKey(Value value);
bool initClass(ObjClass* klass, int argCount);
bool invoke(ObjString* name, int argCount);

#endif
