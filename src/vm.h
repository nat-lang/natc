#ifndef nat_vm_h
#define nat_vm_h

#include "chunk.h"
#include "object.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
  (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  ObjClass* base;
  ObjClass* object;
  ObjClass* tuple;
  ObjClass* sequence;
  ObjClass* map;
  ObjClass* set;
  ObjClass* iterator;

  ObjClass* astClosure;

  ObjClass* vTypeBool;
  ObjClass* vTypeNil;
  ObjClass* vTypeNumber;
  ObjClass* vTypeUndef;
  ObjClass* oTypeClass;
  ObjClass* oTypeInstance;
  ObjClass* oTypeString;
  ObjClass* oTypeClosure;
  ObjClass* oTypeSequence;
} Classes;

typedef struct {
  Value stack[STACK_MAX];
  Value* stackTop;

  CallFrame frames[FRAMES_MAX];
  int frameCount;

  ObjMap strings;
  ObjMap globals;
  ObjMap typeEnv;
  ObjMap infixes;

  Obj* objects;
  ObjUpvalue* openUpvalues;

  // core defs.
  Classes classes;

  // memory.
  int grayCount;
  int grayCapacity;
  Obj** grayStack;

  size_t bytesAllocated;
  size_t nextGC;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

bool initVM();
void freeVM();

void vmRuntimeError(const char* format, ...);

InterpretResult vmInterpret(char* path, const char* source);
InterpretResult vmExecute(int baseFrame);
void vmPush(Value value);
Value vmPop();
Value vmPeek(int distance);
bool vmInitInstance(ObjClass* klass, int argCount, int frames);
bool vmInvoke(ObjString* name, int argCount);
bool vmExecuteMethod(char* method, int argCount, int frames);
bool vmHashValue(Value value, uint32_t* hash);
bool vmCallValue(Value value, int argCount);
void vmCaptureUpvalues(ObjClosure* closure, CallFrame* frame);
bool vmSequenceValueField(ObjInstance* obj, Value* seq);

#endif
