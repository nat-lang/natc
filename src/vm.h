#ifndef nat_vm_h
#define nat_vm_h

#include "ast.h"
#include "chunk.h"
#include "object.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  ObjString* init;
  ObjString* call;
  ObjString* iter;
  ObjString* next;
  ObjString* add;
  ObjString* member;
  ObjString* subscriptGet;
  ObjString* subscriptSet;
  ObjString* length;
  ObjString* equal;
  ObjString* hash;
} Strings;

typedef struct {
  ObjClass* sequence;
  ObjClass* object;
  ObjClass* astNode;
} Classes;

typedef struct {
  Value stack[STACK_MAX];
  Value* stackTop;

  CallFrame frames[FRAMES_MAX];
  int frameCount;
  ObjMap interned;
  ObjMap globals;
  Obj* objects;
  ObjUpvalue* openUpvalues;

  // memory.
  int grayCount;
  int grayCapacity;
  Obj** grayStack;

  size_t bytesAllocated;
  size_t nextGC;

  Strings strings;
  Classes classes;
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
InterpretResult execute(int baseFrame);
void vmPush(Value value);
Value vmPop();
Value vmPeek(int distance);
bool initClass(ObjClass* klass, int argCount);
bool invoke(ObjString* name, int argCount);
bool validateHashable(Value value);
bool vmCallValue(Value value, int argCount);
bool vmInstanceHas(ObjInstance* instance, Value value);
void captureUpvalues(ObjClosure* closure, CallFrame* frame);

#endif
