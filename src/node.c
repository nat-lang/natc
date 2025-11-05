#include "node.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"
#include "vm.h"

bool nodesEqual(AstNode* a, AstNode* b);

// vector.
// ============================================================

static void ensureAstVec(AstVec* v, int need) {
  if (v->capacity >= need) return;
  int newCap = v->capacity ? v->capacity * 2 : 8;
  while (newCap < need) newCap *= 2;
  size_t oldBytes = (size_t)v->capacity * sizeof(void*);
  size_t newBytes = (size_t)newCap * sizeof(void*);
  v->items = (AstNode**)reallocate(v->items, oldBytes, newBytes);
  v->capacity = newCap;
}

void initAstVec(AstVec* v) {
  v->items = NULL;
  v->count = 0;
  v->capacity = 0;
}

void pushAstVec(AstVec* v, AstNode* node) {
  ensureAstVec(v, v->count + 1);
  v->items[v->count++] = node;
}

void freeAstVec(AstVec* v) {
  if (!v) return;
  size_t oldBytes = (size_t)v->capacity * sizeof(void*);
  (void)reallocate(v->items, oldBytes, 0);
  v->items = NULL;
  v->count = 0;
  v->capacity = 0;
}

bool astVecsEqual(AstVec* a, AstVec* b) {
  if (a->count != b->count) return false;

  for (int i = 0; i < a->count; i++)
    if (!nodesEqual(a->items[i], b->items[i])) return false;

  return true;
}

// constructors.
// ============================================================

static AstNode* allocNode(AstType kind) {
  AstNode* n = ALLOCATE(AstNode, 1);
  memset(n, 0, sizeof(AstNode));
  n->type = kind;
  n->line = -1;
  return n;
}

AstNode* newAssignmentNode(AstNode* lhs, AstNode* rhs) {
  AstNode* n = allocNode(AST_ASSIGNMENT);
  n->as.assignment.lhs = lhs;
  n->as.assignment.rhs = rhs;
  return n;
}

AstNode* newBlockNode() {
  AstNode* n = allocNode(AST_BLOCK);
  initAstVec(&n->as.block.stmts);
  return n;
}

AstNode* newCallInfixNode(AstNode* callee, AstNode* lhs, AstNode* rhs) {
  AstNode* n = allocNode(AST_CALL_INFIX);
  n->as.callInfix.callee = callee;
  n->as.callInfix.lhs = lhs;
  n->as.callInfix.rhs = rhs;
  return n;
}

AstNode* newCallNode(AstNode* callee) {
  AstNode* n = allocNode(AST_CALL);
  n->as.call.callee = callee;
  initAstVec(&n->as.call.args);
  return n;
}

AstNode* newExprStmtNode(AstNode* expr) {
  AstNode* n = allocNode(AST_EXPR_STMT);
  n->as.exprStmt.expr = expr;
  return n;
}

AstNode* newFunctionNode(ObjString* name, AstNode* module) {
  AstNode* n = allocNode(AST_FUNCTION);
  n->as.function.name = NULL;
  n->as.function.name = name;
  n->as.function.signature = NULL;
  n->as.function.body = NULL;
  n->as.function.module = NULL;
  n->as.function.module = module;
  n->as.function.localCount = 0;
  n->as.function.upvalueCount = 0;

  for (int i = 0; i < UINT8_COUNT; i++) {
    initToken(&n->as.function.locals[i].name);
    n->as.function.locals[i].depth = 0;
    n->as.function.locals[i].isCaptured = false;
    n->as.function.upvalues[i].index = 0;
    n->as.function.upvalues[i].isLocal = false;
  }
  // the first local slot is always reserved.
  Local* local = &n->as.function.locals[n->as.function.localCount++];
  n->as.function.locals[n->as.function.localCount].depth = 0;
  n->as.function.locals[n->as.function.localCount].isCaptured = false;
  n->as.function.locals[n->as.function.localCount].name.type = TOKEN_IDENTIFIER;
  local->name.start = "";
  local->name.length = 0;
  return n;
}

AstNode* newIfNode(AstNode* cond, AstNode* then, AstNode* elseBranch) {
  AstNode* n = allocNode(AST_IF);
  n->as.ifStmt.cond = cond;
  n->as.ifStmt.then = then;
  n->as.ifStmt.elseBranch = elseBranch;
  return n;
}

AstNode* newUseNode(AstNode* module, ObjString* alias) {
  AstNode* n = allocNode(AST_IMPORT);
  n->as.use.module = module;
  n->as.use.alias = alias;
  return n;
}

AstNode* newUnknownNode() {
  AstNode* n = allocNode(AST_UNKNOWN);
  return n;
}

AstNode* newWhileNode(AstNode* cond, AstNode* body) {
  AstNode* n = allocNode(AST_WHILE);
  n->as.whileStmt.cond = cond;
  n->as.whileStmt.body = body;
  return n;
}

AstNode* newLetNode(ObjString* name, AstNode* value) {
  AstNode* n = allocNode(AST_LET);
  n->as.let.name = name;
  n->as.let.value = value;
  return n;
}

AstNode* newLiteralNode(Value v) {
  AstNode* n = allocNode(AST_LITERAL);
  n->as.literal.value = v;
  return n;
}

AstNode* newModuleNode(ObjString* dirName, ObjString* baseName,
                       ObjString* source) {
  AstNode* n = allocNode(AST_MODULE);
  n->as.module.dirName = dirName;
  n->as.module.baseName = baseName;
  n->as.module.source = source;
  initAstVec(&n->as.module.stmts);
  return n;
}

AstNode* newParamNode(ObjString* name, AstNode* annotation) {
  AstNode* n = allocNode(AST_PARAM);
  n->as.param.name = name;
  n->as.param.annotation = annotation;
  return n;
}

AstNode* newSequenceNode() {
  AstNode* n = allocNode(AST_SEQUENCE);
  initAstVec(&n->as.sequence.values);
  return n;
}

AstNode* newObjectNode() {
  AstNode* n = allocNode(AST_OBJECT);
  initAstVec(&n->as.object.entries);
  return n;
}

AstNode* newObjectEntryNode(AstNode* key, AstNode* value) {
  AstNode* n = allocNode(AST_OBJECT_ENTRY);
  n->as.objectEntry.key = key;
  n->as.objectEntry.value = value;
  return n;
}

AstNode* newVarGlobalNode(ObjString* name) {
  AstNode* n = allocNode(AST_VAR_GLOBAL);
  n->as.global.name = name;
  return n;
}

AstNode* newVarLocalNode(uint8_t index, ObjString* name) {
  AstNode* n = allocNode(AST_VAR_LOCAL);
  n->as.local.index = index;
  n->as.local.name = name;
  return n;
}

AstNode* newVarUpvalueNode(uint8_t index, ObjString* name) {
  AstNode* n = allocNode(AST_VAR_UPVALUE);
  n->as.upvalue.index = index;
  n->as.upvalue.name = name;
  return n;
}

AstNode* newSignatureNode() {
  AstNode* n = allocNode(AST_SIGNATURE);
  initAstVec(&n->as.signature.params);
  n->as.signature.varargs = -1;
  return n;
}

AstNode* newReturnNode(AstNode* value) {
  AstNode* n = allocNode(AST_RETURN);
  n->as.xReturn.value = value;
  return n;
}

AstNode* newThrowNode(AstNode* expr) {
  AstNode* n = allocNode(AST_THROW);
  n->as.throwStmt.expr = expr;
  return n;
}

// api.
// ============================================================

void printNodeAt(AstNode* node, int depth);

void printStrAt(char* str, int depth) {
  printf("%*s", depth, "");
  printf("%s", str);
}

void printNodeVecAt(AstVec* nodes, int depth) {
  for (int i = 0; i < nodes->count; i++) {
    printNodeAt(nodes->items[i], depth);
  }
}

void printNodeAt(AstNode* node, int depth) {
  if (node == NULL) {
    printStrAt("NULL\n", depth);
    return;
  }

  switch (node->type) {
    case AST_ASSIGNMENT: {
      printStrAt("Assignment\n", depth);
      printNodeAt(node->as.assignment.lhs, depth + 1);
      printNodeAt(node->as.assignment.rhs, depth + 1);
      break;
    }
    case AST_BLOCK:
      printStrAt("Block\n", depth);
      printNodeVecAt(&node->as.block.stmts, depth + 1);
      break;

    case AST_CALL: {
      printStrAt("Call\n", depth);
      printNodeAt(node->as.call.callee, depth + 1);
      printNodeVecAt(&node->as.call.args, depth + 1);
      break;
    }
    case AST_CALL_INFIX: {
      printStrAt("CallInfix\n", depth);
      printNodeAt(node->as.callInfix.callee, depth + 1);
      printNodeAt(node->as.callInfix.lhs, depth + 1);
      printNodeAt(node->as.callInfix.rhs, depth + 1);
      break;
    }
    case AST_EXPR_STMT:
      printStrAt("ExprStmt\n", depth);
      printNodeAt(node->as.exprStmt.expr, depth + 1);
      break;

    case AST_FUNCTION: {
      printStrAt("Function", depth);
      printf(" (%s)\n", node->as.function.name->chars);
      printNodeAt(node->as.function.signature, depth + 1);
      printNodeAt(node->as.function.body, depth + 1);
      break;
    }
    case AST_IF: {
      printStrAt("If\n", depth);
      printNodeAt(node->as.ifStmt.cond, depth + 1);
      printNodeAt(node->as.ifStmt.then, depth + 1);
      printNodeAt(node->as.ifStmt.elseBranch, depth + 1);
      break;
    }
    case AST_IMPORT: {
      printStrAt("Import ", depth);
      printf("\"%s/%s\"", node->as.use.module->as.module.dirName->chars,
             node->as.use.module->as.module.baseName->chars);
      if (node->as.use.alias != NULL) {
        printf(" as \"%s\"", node->as.use.alias->chars);
      }
      printf("\n");
      printNodeAt(node->as.use.module, depth + 1);
      break;
    }
    case AST_WHILE: {
      printStrAt("While\n", depth);
      printNodeAt(node->as.whileStmt.cond, depth + 1);
      printNodeAt(node->as.whileStmt.body, depth + 1);
      break;
    }
    case AST_LET: {
      printStrAt("Let ", depth);
      printf("\"%s\"\n", node->as.let.name->chars);
      printNodeAt(node->as.let.value, depth + 1);
      break;
    }
    case AST_LITERAL: {
      printStrAt("Literal ", depth);
      printValue(node->as.literal.value);
      printf("\n");
      break;
    }
    case AST_MODULE:
      printStrAt("Module\n", depth);
      printNodeVecAt(&node->as.module.stmts, depth + 1);
      break;

    case AST_PARAM:
      printStrAt("Param ", depth);
      printf("\"%s\"\n", node->as.param.name->chars);
      break;

    case AST_RETURN:
      printStrAt("Return\n", depth);
      printNodeAt(node->as.xReturn.value, depth + 1);
      break;
    case AST_THROW: {
      printStrAt("Throw\n", depth);
      printNodeAt(node->as.throwStmt.expr, depth + 1);
      break;
    }
    case AST_SEQUENCE:
      printStrAt("Sequence\n", depth);
      printNodeVecAt(&node->as.sequence.values, depth + 1);
      break;
    case AST_OBJECT:
      printStrAt("Object\n", depth);
      printNodeVecAt(&node->as.object.entries, depth + 1);
      break;
    case AST_OBJECT_ENTRY:
      printStrAt("Entry\n", depth);
      printNodeAt(node->as.objectEntry.key, depth + 1);
      printNodeAt(node->as.objectEntry.value, depth + 1);
      break;
    case AST_SIGNATURE:
      printStrAt("Signature\n", depth);
      printNodeVecAt(&node->as.signature.params, depth + 1);
      break;

    case AST_VAR_GLOBAL:
      printStrAt("Global ", depth);
      printf("\"%s\"\n", node->as.global.name->chars);
      break;
    case AST_VAR_LOCAL:
      printStrAt("Local ", depth);
      printf("[%d] \"%s\"\n", node->as.local.index, node->as.local.name->chars);
      break;
    case AST_VAR_UPVALUE:
      printStrAt("Upvalue ", depth);
      printf("[%d] \"%s\"\n", node->as.upvalue.index,
             node->as.upvalue.name->chars);
      break;

    case AST_UNKNOWN:
      printStrAt("Unknown\n", depth);
      break;
  }
}

void printNode(AstNode* node) { printNodeAt(node, 0); }

bool nodesEqual(AstNode* a, AstNode* b) {
  if (a == NULL && b == NULL) return true;
  if (a == NULL || b == NULL) return false;

  if (a->type != b->type) return false;

  switch (a->type) {
    case AST_ASSIGNMENT:
      return nodesEqual(a->as.assignment.lhs, b->as.assignment.lhs) &&
             nodesEqual(a->as.assignment.rhs, b->as.assignment.rhs);

    case AST_BLOCK:
      return astVecsEqual(&a->as.block.stmts, &b->as.block.stmts);

    case AST_CALL:
      return nodesEqual(a->as.call.callee, b->as.call.callee) &&
             astVecsEqual(&a->as.call.args, &b->as.call.args);

    case AST_CALL_INFIX:
      return nodesEqual(a->as.callInfix.callee, b->as.callInfix.callee) &&
             nodesEqual(a->as.callInfix.lhs, b->as.callInfix.lhs) &&
             nodesEqual(a->as.callInfix.rhs, b->as.callInfix.rhs);

    case AST_EXPR_STMT:
      return nodesEqual(a->as.exprStmt.expr, b->as.exprStmt.expr);

    case AST_FUNCTION:
      return nodesEqual(a->as.function.signature, b->as.function.signature) &&
             nodesEqual(a->as.function.body, b->as.function.body);

    case AST_IF:
      return nodesEqual(a->as.ifStmt.cond, b->as.ifStmt.cond) &&
             nodesEqual(a->as.ifStmt.then, b->as.ifStmt.then) &&
             nodesEqual(a->as.ifStmt.elseBranch, b->as.ifStmt.elseBranch);

    case AST_IMPORT:
      return nodesEqual(a->as.use.module, b->as.use.module) &&
             a->as.use.alias == b->as.use.alias;

    case AST_WHILE:
      return nodesEqual(a->as.whileStmt.cond, b->as.whileStmt.cond) &&
             nodesEqual(a->as.whileStmt.body, b->as.whileStmt.body);

    case AST_LET:
      return a->as.let.name == b->as.let.name &&
             nodesEqual(a->as.let.value, b->as.let.value);

    case AST_LITERAL:
      return valuesEqual(a->as.literal.value, b->as.literal.value);

    case AST_MODULE:
      return a->as.module.dirName == b->as.module.dirName &&
             a->as.module.baseName == b->as.module.baseName &&
             astVecsEqual(&a->as.module.stmts, &b->as.module.stmts);

    case AST_PARAM:
      return a->as.param.name == b->as.param.name;

    case AST_RETURN:
      return nodesEqual(a->as.xReturn.value, b->as.xReturn.value);
    case AST_THROW:
      return nodesEqual(a->as.throwStmt.expr, b->as.throwStmt.expr);
    case AST_SEQUENCE:
      return a->as.sequence.values.count == b->as.sequence.values.count &&
             astVecsEqual(&a->as.sequence.values, &b->as.sequence.values);
    case AST_OBJECT:
      return a->as.object.entries.count == b->as.object.entries.count &&
             astVecsEqual(&a->as.object.entries, &b->as.object.entries);
    case AST_OBJECT_ENTRY:
      return nodesEqual(a->as.objectEntry.key, b->as.objectEntry.key) &&
             nodesEqual(a->as.objectEntry.value, b->as.objectEntry.value);
    case AST_SIGNATURE:
      return a->as.signature.varargs == b->as.signature.varargs &&
             astVecsEqual(&a->as.signature.params, &b->as.signature.params);

    case AST_VAR_GLOBAL:
      return a->as.global.name == b->as.global.name;
    case AST_VAR_LOCAL:
      return a->as.local.index == b->as.local.index;
    case AST_VAR_UPVALUE:
      return a->as.upvalue.index == b->as.upvalue.index;
    case AST_UNKNOWN:
      return true;
  }
}

static void error(AstNode* node, const char* format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error in AST to bytecode at node type %i:%d ", node->type,
          node->line);
  vfprintf(stderr, format, args);
  printf("\n");
  va_end(args);
}

static void emitByte(Chunk* chunk, AstNode* node, uint8_t byte) {
  writeChunk(chunk, byte, node->line);
}

static void emitBytes(Chunk* chunk, AstNode* node, uint8_t byte1,
                      uint8_t byte2) {
  emitByte(chunk, node, byte1);
  emitByte(chunk, node, byte2);
}

static void emitConstant(Chunk* chunk, AstNode* node, uint16_t constant) {
  emitBytes(chunk, node, constant >> 8, constant & 0xff);
}

#if defined(DEBUG_PRINT_CODE)
#define DEBUG_CHUNK() \
  disassembleChunk(&fn->chunk, fn->name != NULL ? fn->name->chars : "<script>");
#else
#define DEBUG_CHUNK()
#endif

void closeUpvalues(Chunk* chunk, AstNode* node) {
  for (int i = 0; i < node->as.function.upvalueCount; i++) {
    emitByte(chunk, node, node->as.function.upvalues[i].isLocal ? 1 : 0);
    emitByte(chunk, node, node->as.function.upvalues[i].index);
  }
}

// Helper functions for control flow bytecode generation
static int emitJump(Chunk* chunk, AstNode* node, uint8_t instruction) {
  emitByte(chunk, node, instruction);
  emitByte(chunk, node, 0xff);
  emitByte(chunk, node, 0xff);
  return chunk->count - 2;
}

static void patchJump(Chunk* chunk, AstNode* node, int offset) {
  int jump = chunk->count - offset - 2;

  if (jump > UINT16_MAX) {
    error(node, "Too much code to jump over.");
    return;
  }

  chunk->code[offset] = (jump >> 8) & 0xff;
  chunk->code[offset + 1] = jump & 0xff;
}

static void emitLoop(Chunk* chunk, AstNode* node, int loopStart) {
  emitByte(chunk, node, OP_LOOP);
  int offset = chunk->count - loopStart + 2;
  if (offset > UINT16_MAX) {
    error(node, "Loop body too large.");
    return;
  }

  emitByte(chunk, node, (offset >> 8) & 0xff);
  emitByte(chunk, node, offset & 0xff);
}

ObjFunction* toFunction(AstNode* node);

bool toChunkVec(AstVec* nodes, Chunk* chunk) {
  for (int i = 0; i < nodes->count; i++) {
    if (!toChunk(nodes->items[i], chunk)) return false;
  }
  return true;
}

bool toChunk(AstNode* node, Chunk* chunk) {
  switch (node->type) {
    case AST_ASSIGNMENT: {
      switch (node->as.assignment.lhs->type) {
        case AST_VAR_LOCAL: {
          toChunk(node->as.assignment.rhs, chunk);
          emitByte(chunk, node, OP_SET_LOCAL);
          emitConstant(chunk, node, node->as.assignment.lhs->as.local.index);
          break;
        }
        case AST_VAR_UPVALUE: {
          toChunk(node->as.assignment.rhs, chunk);
          emitByte(chunk, node, OP_SET_UPVALUE);
          emitConstant(chunk, node, node->as.assignment.lhs->as.upvalue.index);
          break;
        }
        case AST_VAR_GLOBAL: {
          toChunk(node->as.assignment.rhs, chunk);
          emitByte(chunk, node, OP_SET_GLOBAL);
          uint16_t constant = addConstant(
              chunk, OBJ_VAL(node->as.assignment.lhs->as.global.name));
          emitConstant(chunk, node, constant);
          break;
        }
        default: {
          error(node, "Invalid assignment target.");
          return false;
        }
      }
      break;
    }
    case AST_BLOCK: {
      AstVec stmts = node->as.block.stmts;
      if (!toChunkVec(&stmts, chunk)) return false;
      break;
    }
    case AST_CALL: {
      // 1. Emit code for the callee, which leaves the function/object to call
      // on the stack
      if (!toChunk(node->as.call.callee, chunk)) return false;

      // 2. Emit code for each argument, in order, leaving them on the stack
      AstVec* args = &node->as.call.args;
      if (!toChunkVec(args, chunk)) return false;

      // 3. Emit the OP_CALL instruction with argument count
      emitByte(chunk, node, OP_CALL);
      emitByte(chunk, node, (uint8_t)args->count);
      break;
    }
    case AST_CALL_INFIX: {
      if (!toChunk(node->as.callInfix.callee, chunk)) return false;
      if (!toChunk(node->as.callInfix.lhs, chunk)) return false;
      if (!toChunk(node->as.callInfix.rhs, chunk)) return false;
      emitByte(chunk, node, OP_CALL);
      emitByte(chunk, node, (uint8_t)2);
      break;
    }
    case AST_EXPR_STMT: {
      if (!toChunk(node->as.exprStmt.expr, chunk)) return false;
      emitByte(chunk, node, OP_EXPR_STATEMENT);
      break;
    }
    case AST_FUNCTION: {
      ObjFunction* newFn = toFunction(node);

      uint16_t fnConst = addConstant(chunk, OBJ_VAL(newFn));

      emitByte(chunk, node, OP_CLOSURE);
      emitConstant(chunk, node, fnConst);
      for (int i = 0; i < node->as.function.upvalueCount; i++) {
        emitByte(chunk, node, node->as.function.upvalues[i].isLocal ? 1 : 0);
        emitByte(chunk, node, node->as.function.upvalues[i].index);
      }
      break;
    }
    case AST_IF: {
      // Emit condition code
      if (!toChunk(node->as.ifStmt.cond, chunk)) return false;

      // Jump if false (to else branch or end)
      int thenJump = emitJump(chunk, node, OP_JUMP_IF_FALSE);
      emitByte(chunk, node, OP_POP);

      // Emit then branch code
      if (!toChunk(node->as.ifStmt.then, chunk)) return false;

      // Jump to end (skip else branch)
      int elseJump = emitJump(chunk, node, OP_JUMP);

      // Patch the conditional jump to here
      patchJump(chunk, node, thenJump);
      emitByte(chunk, node, OP_POP);

      // Emit else branch if present
      if (node->as.ifStmt.elseBranch != NULL) {
        if (!toChunk(node->as.ifStmt.elseBranch, chunk)) return false;
      }

      // Patch the else jump to here
      patchJump(chunk, node, elseJump);
      break;
    }
    case AST_WHILE: {
      int loopStart = chunk->count;

      // Emit condition code
      if (!toChunk(node->as.whileStmt.cond, chunk)) return false;

      // Jump if false (exit loop)
      int exitJump = emitJump(chunk, node, OP_JUMP_IF_FALSE);
      emitByte(chunk, node, OP_POP);

      // Emit body code
      if (!toChunk(node->as.whileStmt.body, chunk)) return false;

      // Loop back to condition
      emitLoop(chunk, node, loopStart);

      // Patch exit jump to here
      patchJump(chunk, node, exitJump);
      emitByte(chunk, node, OP_POP);
      break;
    }
    case AST_IMPORT: {
      if (!toChunk(node->as.use.module, chunk)) return false;
      break;
    }
    case AST_LET:
      if (!toChunk(node->as.let.value, chunk)) return false;
      break;

    case AST_LITERAL: {
      uint16_t constant = addConstant(chunk, node->as.literal.value);
      emitByte(chunk, node, OP_CONSTANT);
      emitConstant(chunk, node, constant);
      break;
    }
    case AST_MODULE:
      if (!toChunkVec(&node->as.module.stmts, chunk)) return false;
      break;
    case AST_PARAM:
      break;
    case AST_RETURN: {
      if (!toChunk(node->as.xReturn.value, chunk)) return false;
      emitByte(chunk, node, OP_RETURN);
      break;
    }
    case AST_THROW: {
      if (!toChunk(node->as.throwStmt.expr, chunk)) return false;
      emitByte(chunk, node, OP_THROW);
      break;
    }
    case AST_SEQUENCE: {
      emitByte(chunk, node, OP_GET_GLOBAL);
      uint16_t constant = addConstant(chunk, OBJ_VAL(vm.core.sSeq));
      emitConstant(chunk, node, constant);
      if (!toChunkVec(&node->as.sequence.values, chunk)) return false;

      emitByte(chunk, node, OP_CALL);
      emitByte(chunk, node, (uint8_t)node->as.sequence.values.count);
      break;
    }
    case AST_OBJECT: {
      emitByte(chunk, node, OP_GET_GLOBAL);
      uint16_t constant = addConstant(chunk, OBJ_VAL(vm.core.sObj));
      emitConstant(chunk, node, constant);

      if (!toChunkVec(&node->as.object.entries, chunk)) return false;

      emitByte(chunk, node, OP_CALL);
      emitByte(chunk, node, (uint8_t)(node->as.object.entries.count * 2));
      break;
    }
    case AST_OBJECT_ENTRY: {
      if (!toChunk(node->as.objectEntry.key, chunk)) return false;
      if (!toChunk(node->as.objectEntry.value, chunk)) return false;
      break;
    }
    case AST_SIGNATURE: {
      if (!toChunkVec(&node->as.signature.params, chunk)) return false;
      break;
    }
    case AST_VAR_GLOBAL: {
      uint16_t constant = addConstant(chunk, OBJ_VAL(node->as.global.name));
      emitByte(chunk, node, OP_GET_GLOBAL);
      emitConstant(chunk, node, constant);
      break;
    }
    case AST_VAR_LOCAL: {
      emitByte(chunk, node, OP_GET_LOCAL);
      emitConstant(chunk, node, node->as.local.index);
      break;
    }
    case AST_VAR_UPVALUE: {
      emitByte(chunk, node, OP_GET_UPVALUE);
      emitConstant(chunk, node, node->as.upvalue.index);
      break;
    }
    case AST_UNKNOWN: {
      error(node, "Unknown node.");
      exit(2);
      break;
    }
  }

  return true;
}

ObjFunction* toFunction(AstNode* node) {
  ObjFunction* fn = newFunction();
  fn->name =
      copyString(node->as.function.name->chars, node->as.function.name->length);
  fn->arity = node->as.function.signature->as.signature.params.count;
  fn->node = node;

  if (!toChunk(node->as.function.signature, &fn->chunk)) return false;
  if (!toChunk(node->as.function.body, &fn->chunk)) return false;

  DEBUG_CHUNK()
  return fn;
}

// memory.
// ============================================================

void markAstNode(AstNode* n) {
  if (n == NULL) return;

  switch (n->type) {
    case AST_ASSIGNMENT:
      markAstNode(n->as.assignment.lhs);
      markAstNode(n->as.assignment.rhs);
      break;
    case AST_BLOCK:
      for (int i = 0; i < n->as.block.stmts.count; i++) {
        markAstNode((AstNode*)n->as.block.stmts.items[i]);
      }
      break;

    case AST_CALL: {
      markAstNode(n->as.call.callee);
      for (int i = 0; i < n->as.call.args.count; i++) {
        markAstNode((AstNode*)n->as.call.args.items[i]);
      }
      break;
    }
    case AST_CALL_INFIX: {
      markAstNode(n->as.callInfix.callee);
      markAstNode(n->as.callInfix.lhs);
      markAstNode(n->as.callInfix.rhs);
      break;
    }
    case AST_EXPR_STMT:
      markAstNode(n->as.exprStmt.expr);
      break;

    case AST_FUNCTION: {
      markAstNode(n->as.function.module);
      markAstNode(n->as.function.signature);
      markAstNode(n->as.function.body);
      break;
    }
    case AST_IF: {
      markAstNode(n->as.ifStmt.cond);
      markAstNode(n->as.ifStmt.then);
      markAstNode(n->as.ifStmt.elseBranch);
      break;
    }
    case AST_IMPORT:
      markAstNode(n->as.use.module);
      if (n->as.use.alias != NULL) {
        markObject((Obj*)n->as.use.alias);
      }
      break;
    case AST_WHILE:
      markAstNode(n->as.whileStmt.cond);
      markAstNode(n->as.whileStmt.body);
      break;
    case AST_LET:
      markObject((Obj*)n->as.let.name);
      markAstNode(n->as.let.value);
      break;
    case AST_LITERAL:
      /* Value may reference Obj* (e.g., strings); mark via markValue */
      markValue(n->as.literal.value);
      break;
    case AST_MODULE: {
      markObject((Obj*)n->as.module.dirName);
      markObject((Obj*)n->as.module.baseName);
      markObject((Obj*)n->as.module.source);
      for (int i = 0; i < n->as.module.stmts.count; i++) {
        markAstNode((AstNode*)n->as.module.stmts.items[i]);
      }
      break;
    }
    case AST_PARAM:
      markObject((Obj*)n->as.param.name);
      markAstNode(n->as.param.annotation);
      break;
    case AST_RETURN:
      markAstNode(n->as.xReturn.value);
      break;
    case AST_THROW:
      markAstNode(n->as.throwStmt.expr);
      break;
    case AST_SEQUENCE:
      for (int i = 0; i < n->as.sequence.values.count; i++)
        markAstNode((AstNode*)n->as.sequence.values.items[i]);
      break;
    case AST_OBJECT:
      for (int i = 0; i < n->as.object.entries.count; i++)
        markAstNode((AstNode*)n->as.object.entries.items[i]);
      break;
    case AST_OBJECT_ENTRY:
      markAstNode(n->as.objectEntry.key);
      markAstNode(n->as.objectEntry.value);
      break;
    case AST_SIGNATURE:
      for (int i = 0; i < n->as.signature.params.count; i++)
        markObject((Obj*)n->as.signature.params.items[i]);
      break;
    case AST_VAR_GLOBAL:
      markObject((Obj*)n->as.global.name);
      break;
    case AST_VAR_LOCAL:
      markObject((Obj*)n->as.local.name);
      break;
    case AST_VAR_UPVALUE:
      markObject((Obj*)n->as.upvalue.name);
      break;

    case AST_UNKNOWN:
      break;
  }
}

void freeAstNode(AstNode* n) {
  if (!n) return;

  switch (n->type) {
    case AST_ASSIGNMENT:
      freeAstNode(n->as.assignment.lhs);
      freeAstNode(n->as.assignment.rhs);
      break;
    case AST_LITERAL:
      /* Value/ObjString inside Value are GC-managed; nothing to free */
      break;

    case AST_VAR_GLOBAL:
    case AST_VAR_LOCAL:
    case AST_VAR_UPVALUE:
      /* ObjString* name is GC-managed */
      break;

    case AST_IMPORT:
      /* ObjString* modulePath and alias are GC-managed; nothing to free */
      break;

    case AST_CALL:
      freeAstNode(n->as.call.callee);
      for (int i = 0; i < n->as.call.args.count; i++) {
        freeAstNode(n->as.call.args.items[i]);
      }
      freeAstVec(&n->as.call.args);
      break;

    case AST_CALL_INFIX:
      freeAstNode(n->as.callInfix.callee);
      freeAstNode(n->as.callInfix.lhs);
      freeAstNode(n->as.callInfix.rhs);

    case AST_SIGNATURE:
      freeAstVec(&n->as.signature.params);
      break;

    case AST_RETURN:
      freeAstNode(n->as.xReturn.value);
      break;

    case AST_UNKNOWN:
      break;

    case AST_BLOCK:
      freeAstVec(&n->as.block.stmts);
      break;
    case AST_EXPR_STMT:
      freeAstNode(n->as.exprStmt.expr);
      break;
    case AST_FUNCTION:
      freeAstNode(n->as.function.signature);
      freeAstNode(n->as.function.body);
      break;
    case AST_IF:
      freeAstNode(n->as.ifStmt.cond);
      freeAstNode(n->as.ifStmt.then);
      freeAstNode(n->as.ifStmt.elseBranch);
      break;
    case AST_WHILE:
      freeAstNode(n->as.whileStmt.cond);
      freeAstNode(n->as.whileStmt.body);
      break;
    case AST_LET:
      freeAstNode(n->as.let.value);
      break;
    case AST_MODULE:
      for (int i = 0; i < n->as.module.stmts.count; i++) {
        freeAstNode((AstNode*)n->as.module.stmts.items[i]);
      }
      break;
    case AST_PARAM:
      freeAstNode(n->as.param.annotation);
      break;
    case AST_THROW:
      freeAstNode(n->as.throwStmt.expr);
      break;
    case AST_SEQUENCE:
      freeAstVec(&n->as.sequence.values);
      break;
    case AST_OBJECT:
      freeAstVec(&n->as.object.entries);
      break;
    case AST_OBJECT_ENTRY:
      freeAstNode(n->as.objectEntry.key);
      freeAstNode(n->as.objectEntry.value);
      break;
  }

  /* finally free the node struct itself */
  FREE(AstNode, n);
}