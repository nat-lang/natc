#include "chunk.h"

#include <stdlib.h>

#include "memory.h"
#include "vm.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  freeValueArray(&chunk->constants);
  initChunk(chunk);
}

// Write a new opcode [byte] to the chunk, keep the books,
// and append a null char to the end of the chunk to delimit.
void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  // +1 for the null delimiter.
  if (chunk->capacity < chunk->count + 2) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->code[chunk->count + 1] = OP_END;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
  vmPush(value);
  writeValueArray(&chunk->constants, value);
  vmPop();
  return chunk->constants.count - 1;
}