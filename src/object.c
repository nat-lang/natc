#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

#define MAP_MAX_LOAD 0.75

#define ALLOCATE_OBJ(type, objectType) \
  (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);

  object->type = type;
  object->isMarked = false;
  object->next = vm.objects;
  object->hash = 0;
  vm.objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

  return object;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
  ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
  bound->receiver = receiver;
  bound->method = method;
  return bound;
}

ObjClass* newClass(ObjString* name) {
  ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
  klass->name = name;
  initMap(&klass->methods);
  return klass;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction* newFunction() {
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  function->variadic = false;
  initChunk(&function->chunk);
  initMap(&function->constants);
  initMap(&function->signature);
  return function;
}

ObjInstance* newInstance(ObjClass* klass) {
  ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
  instance->klass = klass;
  initMap(&instance->fields);
  return instance;
}

ObjNative* newNative(int arity, ObjString* name, NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->arity = arity;
  native->name = name;
  native->function = function;
  return native;
}

ObjSequence* newSequence() {
  ObjSequence* sequence = ALLOCATE_OBJ(ObjSequence, OBJ_SEQUENCE);
  initValueArray(&sequence->values);
  return sequence;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  vmPush(OBJ_VAL(string));
  mapSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
  vmPop();

  return string;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

// Generate a hash code for [object].
uint32_t hashObject(Obj* object) {
  switch (object->type) {
    case OBJ_STRING:
      return ((ObjString*)object)->hash;
    default:
      return object->hash;
  }
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = mapFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash);
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = mapFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocateString(chars, length, hash);
}

ObjString* concatenateStrings(ObjString* a, ObjString* b) {
  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  return takeString(chars, length);
}

ObjString* intern(const char* chars) {
  return copyString(chars, strlen(chars));
}

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->closed = NIL_VAL;
  upvalue->next = NULL;
  return upvalue;
}

static void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
  } else {
    printf("<fn %s>", function->name->chars);
  }
}

void initMap(ObjMap* map) {
  map->count = 0;
  map->capacity = 0;
  map->entries = NULL;
}

void freeMap(ObjMap* map) {
  FREE_ARRAY(MapEntry, map->entries, map->capacity);
  initMap(map);
}

static MapEntry* mapFindHash(MapEntry* entries, int capacity, Value key,
                             uint32_t hash) {
  uint32_t index = hash & (capacity - 1);
  MapEntry* tombstone = NULL;

  for (;;) {
    MapEntry* entry = &entries[index];

    if (IS_UNDEF(entry->key)) {
      if (IS_NIL(entry->value)) {
        // Empty entry.
        return tombstone != NULL ? tombstone : entry;
      } else {
        // We found a tombstone.
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (valuesEqual(entry->key, key)) {
      // We found the key.
      return entry;
    }

    index = (index + 1) & (capacity - 1);
  }
}

static MapEntry* mapFindEntry(MapEntry* entries, int capacity, Value key) {
  return mapFindHash(entries, capacity, key, hashValue(key));
}

static void mapAdjustCapacity(ObjMap* map, int capacity) {
  MapEntry* entries = ALLOCATE(MapEntry, capacity);

  for (int i = 0; i < capacity; i++) {
    entries[i].key = UNDEF_VAL;
    entries[i].value = NIL_VAL;
  }

  map->count = 0;

  for (int i = 0; i < map->capacity; i++) {
    MapEntry* entry = &map->entries[i];
    if (IS_UNDEF(entry->key)) continue;

    MapEntry* dest = mapFindEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    map->count++;
  }

  FREE_ARRAY(MapEntry, map->entries, map->capacity);
  map->entries = entries;
  map->capacity = capacity;
}

static bool mapHasHash(ObjMap* map, Value key, uint32_t hash) {
  if (map->count == 0) return false;

  MapEntry* entry = mapFindHash(map->entries, map->capacity, key, hash);

  return !IS_UNDEF(entry->key);
}

bool mapHas(ObjMap* map, Value key) {
  return mapHasHash(map, key, hashValue(key));
}

static bool mapGetHash(ObjMap* map, Value key, Value* value, uint32_t hash) {
  if (map->count == 0) return false;

  MapEntry* entry = mapFindHash(map->entries, map->capacity, key, hash);
  if (IS_UNDEF(entry->key)) return false;

  *value = entry->value;
  return true;
}

bool mapGet(ObjMap* map, Value key, Value* value) {
  return mapGetHash(map, key, value, hashValue(key));
}

static bool mapSetHash(ObjMap* map, Value key, Value value, uint32_t hash) {
  if (map->count + 1 > map->capacity * MAP_MAX_LOAD) {
    int capacity = GROW_CAPACITY(map->capacity);
    mapAdjustCapacity(map, capacity);
  }

  MapEntry* entry = mapFindHash(map->entries, map->capacity, key, hash);

  bool isNewKey = IS_UNDEF(entry->key);
  if (isNewKey && IS_NIL(entry->value)) map->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool mapSet(ObjMap* map, Value key, Value value) {
  return mapSetHash(map, key, value, hashValue(key));
}

bool mapDelete(ObjMap* map, Value key) {
  if (map->count == 0) return false;

  // Find the entry.
  MapEntry* entry = mapFindEntry(map->entries, map->capacity, key);
  if (IS_UNDEF(entry->key)) return false;

  // Place a tombstone in the entry.
  entry->key = UNDEF_VAL;
  entry->value = BOOL_VAL(true);
  return true;
}

void mapAddAll(ObjMap* from, ObjMap* to) {
  for (int i = 0; i < from->capacity; i++) {
    MapEntry* entry = &from->entries[i];
    if (!IS_UNDEF(entry->key)) {
      mapSet(to, entry->key, entry->value);
    }
  }
}

ObjString* mapFindString(ObjMap* map, const char* chars, int length,
                         uint32_t hash) {
  if (map->count == 0) return NULL;

  uint32_t index = hash & (map->capacity - 1);
  for (;;) {
    MapEntry* entry = &map->entries[index];
    if (IS_UNDEF(entry->key)) {
      // Stop if we find an empty non-tombstone entry.
      if (IS_NIL(entry->value)) return NULL;
    } else if (IS_STRING(entry->key) &&
               AS_STRING(entry->key)->length == length &&
               AS_STRING(entry->key)->hash == hash &&
               memcmp(AS_STRING(entry->key)->chars, chars, length) == 0) {
      // We found it.
      return AS_STRING(entry->key);
    }

    index = (index + 1) & (map->capacity - 1);
  }
}

void mapRemoveWhite(ObjMap* map) {
  for (int i = 0; i < map->capacity; i++) {
    MapEntry* entry = &map->entries[i];
    if (!IS_UNDEF(entry->key) && IS_OBJ(entry->key) &&
        !AS_OBJ(entry->key)->isMarked) {
      mapDelete(map, entry->key);
    }
  }
}

void markMap(ObjMap* map) {
  for (int i = 0; i < map->capacity; i++) {
    MapEntry* entry = &map->entries[i];
    markValue(entry->key);
    markValue(entry->value);
  }
}

static void printMap(ObjMap* map) { printf("<map>"); }

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_BOUND_METHOD:
      printFunction(AS_BOUND_METHOD(value)->method->function);
      break;
    case OBJ_CLASS:
      printf("%s", AS_CLASS(value)->name->chars);
      break;
    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_INSTANCE:
      printf("<%s object at %p>", AS_INSTANCE(value)->klass->name->chars,
             AS_INSTANCE(value));
      break;
    case OBJ_MAP:
      printMap(AS_MAP(value));
      break;
    case OBJ_NATIVE:
      printf("<fn %s>", AS_NATIVE(value)->name->chars);
      break;
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_UPVALUE:
      printf("upvalue");
      break;
    case OBJ_SEQUENCE:
      printValueArray(&AS_SEQUENCE(value)->values);
      break;
  }
}