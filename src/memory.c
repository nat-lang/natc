#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>

#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

static void markArray(ValueArray* array);

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;

  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);

  if (result == NULL) exit(1);

  return result;
}

void markObject(Obj* object) {
  if (object == NULL) return;
  if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

    if (vm.grayStack == NULL) exit(1);
  }

  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  markArray(&object->annotations);

  switch (object->oType) {
    case OBJ_BOUND_FUNCTION: {
      ObjBoundFunction* obj = (ObjBoundFunction*)object;
      markValue(obj->receiver);

      switch (obj->type) {
        case BOUND_METHOD: {
          markObject((Obj*)obj->bound.method);
          break;
        }
        case BOUND_NATIVE:
          break;
      }
      break;
    }
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      markObject((Obj*)klass->name);
      markObject((Obj*)klass->super);
      markMap(&klass->fields);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++)
        markObject((Obj*)closure->upvalues[i]);
      break;
    }
    case OBJ_OVERLOAD: {
      ObjOverload* overload = (ObjOverload*)object;
      for (int i = 0; i < overload->cases; i++)
        markObject((Obj*)overload->closures[i]);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      markObject((Obj*)instance->klass);
      markMap(&instance->fields);
      break;
    }
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markMap(&function->fields);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_VARIABLE: {
      markObject((Obj*)((ObjVariable*)object)->name);
      break;
    }
    case OBJ_MAP: {
      ObjMap* map = (ObjMap*)object;
      markMap(map);
      break;
    }
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
    case OBJ_SEQUENCE: {
      ObjSequence* seq = (ObjSequence*)object;
      markArray(&seq->values);
      break;
    }
    case OBJ_SPREAD: {
      ObjSpread* spread = (ObjSpread*)object;
      markValue(spread->value);
      break;
    }
  }
}

static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  freeValueArray(&object->annotations);

  switch (object->oType) {
    case OBJ_BOUND_FUNCTION:
      FREE(ObjBoundFunction, object);
      break;
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      klass->super = NULL;
      freeMap(&klass->fields);
      FREE(ObjClass, object);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      freeMap(&function->fields);
      freeMap(&function->constants);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_OVERLOAD: {
      ObjOverload* overload = (ObjOverload*)object;
      FREE_ARRAY(ObjOverload*, overload->closures, overload->cases);
      FREE(ObjOverload, object);
      break;
    }
    case OBJ_VARIABLE: {
      FREE(ObjVariable, object);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      freeMap(&instance->fields);
      FREE(ObjInstance, object);
      break;
    }
    case OBJ_NATIVE:
      FREE(ObjNative, object);
      break;
    case OBJ_MAP: {
      ObjMap* map = (ObjMap*)object;
      freeMap(map);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object);
      break;
    case OBJ_SEQUENCE: {
      ObjSequence* seq = (ObjSequence*)object;
      freeValueArray(&seq->values);
      FREE(ObjSequence, seq);
      break;
    }
    case OBJ_SPREAD: {
      FREE(ObjSpread, object);
      break;
    }
  }
}

static void markRoots() {
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }
  for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }

  markMap(&vm.globals);
  markMap(&vm.infixes);

  markObject((Obj*)vm.core.sName);
  markObject((Obj*)vm.core.sArity);
  markObject((Obj*)vm.core.sPatterned);
  markObject((Obj*)vm.core.sVariadic);
  markObject((Obj*)vm.core.sValues);
  markObject((Obj*)vm.core.sSignature);

  markCompilerRoots(vm.compiler);
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  markRoots();
  traceReferences();
  mapRemoveWhite(&vm.strings);
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
  free(vm.grayStack);
}
