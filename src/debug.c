#include "debug.h"

#include <stdio.h>

#include "object.h"
#include "value.h"
#include "vm.h"

void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);

  return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

static uint16_t readShort(Chunk* chunk, int offset) {
  uint16_t code = (uint16_t)(chunk->code[offset + 1] << 8);
  code |= chunk->code[offset + 2];
  return code;
}

static int shortInstruction(const char* name, Chunk* chunk, int offset) {
  uint16_t slot = readShort(chunk, offset);
  printf("%-16s %d\n", name, slot);
  return offset + 3;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk,
                           int offset) {
  uint16_t jump = readShort(chunk, offset);
  printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint16_t constant = readShort(chunk, offset);

  printf("%-16s %d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

static int invokeInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = readShort(chunk, offset);
  uint8_t argCount = chunk->code[offset + 3];
  printf("%-16s (%d args) %4d '", name, argCount, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 4;
}

int disassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);

  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_UNDEFINED:
      return simpleInstruction("OP_UNDEFINED", offset);
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    case OP_GET_LOCAL:
      return shortInstruction("OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL:
      return shortInstruction("OP_SET_LOCAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_GET_UPVALUE:
      return shortInstruction("OP_GET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE:
      return shortInstruction("OP_SET_UPVALUE", chunk, offset);
    case OP_GET_PROPERTY:
      return constantInstruction("OP_GET_PROPERTY", chunk, offset);
    case OP_SET_PROPERTY:
      return constantInstruction("OP_SET_PROPERTY", chunk, offset);
    case OP_GET_SUPER:
      return constantInstruction("OP_GET_SUPER", chunk, offset);
    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_PRINT:
      return simpleInstruction("OP_PRINT", offset);
    case OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case OP_CALL:
      return byteInstruction("OP_CALL", chunk, offset);
    case OP_CALL_INFIX:
      return simpleInstruction("OP_CALL_INFIX", offset);
    case OP_CALL_POSTFIX:
      return byteInstruction("OP_CALL_POSTFIX", chunk, offset);
    case OP_INVOKE:
      return invokeInstruction("OP_INVOKE", chunk, offset);
    case OP_CLOSURE: {
      uint16_t constant = readShort(chunk, offset);
      offset += 3;

      printf("%-16s %4d ", "OP_CLOSURE", constant);
      printValue(chunk->constants.values[constant]);
      printf("\n");

      ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);

      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d      |                     %s %d\n", offset - 2,
               isLocal ? "local" : "upvalue", index);
      }

      return offset;
    }
    case OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OP_IMPLICIT_RETURN:
      return simpleInstruction("OP_IMPLICIT_RETURN", offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    case OP_CASE:
      return byteInstruction("OP_CASE", chunk, offset);
    case OP_CASE_OR:
      return simpleInstruction("OP_CASE_OR", offset);
    case OP_CLASS:
      return constantInstruction("OP_CLASS", chunk, offset);
    case OP_INHERIT:
      return simpleInstruction("OP_INHERIT", offset);
    case OP_METHOD:
      return constantInstruction("OP_METHOD", chunk, offset);
    case OP_MEMBER:
      return simpleInstruction("OP_MEMBER", offset);
    case OP_IMPORT:
      return simpleInstruction("OP_IMPORT", offset);
    case OP_THROW:
      return simpleInstruction("OP_THROW", offset);
    case OP_SUBSCRIPT_GET:
      return simpleInstruction("OP_SUBSCRIPT_GET", offset);
    case OP_SUBSCRIPT_SET:
      return simpleInstruction("OP_SUBSCRIPT_SET", offset);
    case OP_EXPR_STATEMENT:
      return simpleInstruction("OP_EXPR_STATEMENT", offset);
    case OP_DESTRUCTURE:
      return simpleInstruction("OP_DESTRUCTURE", offset);
    case OP_SET_TYPE_LOCAL:
      return shortInstruction("OP_SET_TYPE_LOCAL", chunk, offset);
    case OP_SET_TYPE_GLOBAL:
      return constantInstruction("OP_SET_TYPE_GLOBAL", chunk, offset);
    case OP_SPREAD:
      return simpleInstruction("OP_SPREAD", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}

void disassembleStack() {
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    printf("[ ");
    printValue(*slot);
    printf(" ]");
  }
}