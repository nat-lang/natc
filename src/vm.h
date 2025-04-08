#ifndef nat_vm_h
#define nat_vm_h

#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "value.h"

#define FRAMES_MAX 2000
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
  ObjString* sName;
  ObjString* sArity;
  ObjString* sPatterned;
  ObjString* sVariadic;
  ObjString* sValues;
  ObjString* sSignature;
  ObjString* sFunction;
  ObjString* sModule;
  ObjString* sArgv;
  ObjString* sMain;
  ObjString* sQuote;
  ObjString* sBackslash;

  ObjClass* base;
  ObjClass* object;
  ObjClass* module;
  ObjClass* tuple;
  ObjClass* sequence;
  ObjClass* map;
  ObjClass* set;
  ObjClass* iterator;

  ObjClass* astClosure;
  ObjClass* astMethod;
  ObjClass* astExternalUpvalue;
  ObjClass* astInternalUpvalue;
  ObjClass* astLocal;
  ObjClass* astGlobal;
  ObjClass* astOverload;
  ObjClass* astMembership;
  ObjClass* astBlock;
  ObjClass* astQuantification;

  ObjClass* vTypeUnit;
  ObjClass* vTypeBool;
  ObjClass* vTypeNil;
  ObjClass* vTypeNumber;
  ObjClass* vTypeUndef;
  ObjClass* oTypeVariable;
  ObjClass* oTypeClass;
  ObjClass* oTypeInstance;
  ObjClass* oTypeString;
  ObjClass* oTypeNative;
  ObjClass* oTypeFunction;
  ObjClass* oTypeBoundFunction;
  ObjClass* oTypeOverload;
  ObjClass* oTypeSequence;

  ObjClosure* unify;
  ObjInstance* typeSystem;
  ObjInstance* grammar;
} Core;

typedef struct {
  // stack.
  Value stack[STACK_MAX];
  Value* stackTop;
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  // heap.
  Obj* objects;
  ObjUpvalue* openUpvalues;
  ObjMap strings;
  ObjMap globals;
  ObjMap typeEnv;
  ObjMap infixes;
  ObjMap methodInfixes;

  // core defs.
  Core core;

  // memory.
  int grayCount;
  int grayCapacity;
  Obj** grayStack;
  size_t bytesAllocated;
  size_t nextGC;

  // root compiler.
  Compiler* compiler;

  // currently executing module.
  ObjModule* module;
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

InterpretResult vmInterpretExpr(char* path, char* expr);
InterpretResult vmInterpretModule(char* path);
InterpretResult vmInterpretEntrypoint(char* path, int argc, char* argv[]);
InterpretResult vmInterpretModule_wasm(char* path, int argc, char* argv[]);
InterpretResult vmInterpretEntrypoint_wasm(char* path, int argc, char* argv[]);
ObjModule* vmCompileModule(Token path, ModuleType type);
ObjClosure* vmCompileClosure(Token path, char* source, ObjModule* module);
bool vmImport(ObjModule* module, ObjMap* target);
bool vmImportAsInstance(ObjModule* module);
InterpretResult vmExecute(int baseFrame);
void vmPush(Value value);
Value vmPop();
Value vmPeek(int distance);
bool vmInitInstance(ObjClass* klass, int argCount);
bool vmInvoke(ObjString* name, int argCount);
bool vmExecuteMethod(char* method, int argCount);
bool vmHashValue(Value value, uint32_t* hash);
void vmInitFrame(ObjClosure* closure, int offset);
bool vmCallValue(Value value, int argCount);
void vmCloseUpvalues(Value* last);
void vmClosure(CallFrame* frame);
bool vmOverload(CallFrame* frame);
void vmVariable(CallFrame* frame);
void vmSign(CallFrame* frame);
bool vmSequenceValueField(ObjInstance* obj, Value* seq);
bool vmTuplify(int count, bool replace);
ObjUpvalue* vmCaptureUpvalue(Value* local, uint8_t slot, ObjString* name);

#endif
