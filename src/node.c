#include "node.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "memory.h"
#include "object.h"
#include "value.h"

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
  /* n->as.line can be set by parser later */
  return n;
}

AstNode* newLetNode(ObjString* name, AstNode* value) {
  AstNode* n = allocNode(AST_LET);
  n->as.let.name = name;
  n->as.let.value = value;
  return n;
}

AstNode* newBlockNode() {
  AstNode* n = allocNode(AST_BLOCK);
  initAstVec(&n->as.block.stmts);
  return n;
}

AstNode* newModuleNode(ObjString* name) {
  AstNode* n = allocNode(AST_MODULE);
  n->as.module.name = name;
  initAstVec(&n->as.module.stmts);
  return n;
}

AstNode* newSequenceNode() {
  AstNode* n = allocNode(AST_SEQUENCE);
  initAstVec(&n->as.sequence.values);
  return n;
}

AstNode* newSpreadNode(AstNode* expr) {
  AstNode* n = allocNode(AST_SPREAD);
  n->as.spread.expr = expr;
  return n;
}

AstNode* newLiteralNode(Value v) {
  AstNode* n = allocNode(AST_LITERAL);
  n->as.literal.value = v;
  return n;
}

AstNode* newVariableNode(ObjString* name) {
  AstNode* n = allocNode(AST_VARIABLE);
  n->as.variable.name = name;
  return n;
}

AstNode* newCallNode(AstNode* callee) {
  AstNode* n = allocNode(AST_CALL);
  n->as.call.callee = callee;
  initAstVec(&n->as.call.args);
  return n;
}

AstNode* newCallInfixNode(AstNode* callee, AstNode* lhs, AstNode* rhs) {
  AstNode* n = allocNode(AST_CALL_INFIX);
  n->as.callInfix.callee = callee;
  n->as.callInfix.lhs = lhs;
  n->as.callInfix.rhs = rhs;
  return n;
}

AstNode* newMemberNode(AstNode* object, ObjString* member, int isSet) {
  AstNode* n = allocNode(AST_MEMBER);
  n->as.member.object = object;
  n->as.member.member = member;
  n->as.member.isSet = isSet;
  return n;
}

AstNode* newSubscriptNode(AstNode* coll, AstNode* index, int isSet) {
  AstNode* n = allocNode(AST_SUBSCRIPT);
  n->as.subscript.collection = coll;
  n->as.subscript.index = index;
  n->as.subscript.isSet = isSet;
  return n;
}

AstNode* newComprehensionNode(AstNode* body, ObjString* var, AstNode* iterable,
                              AstNode* pred) {
  AstNode* n = allocNode(AST_COMPREHENSION);
  n->as.comprehension.body = body;
  n->as.comprehension.var = var;
  n->as.comprehension.iterable = iterable;
  n->as.comprehension.pred = pred;
  return n;
}

AstNode* newSignatureNode() {
  AstNode* n = allocNode(AST_SIGNATURE);
  initAstVec(&n->as.signature.params);
  n->as.signature.varargs = -1;
  return n;
}

AstNode* newClosureNode() {
  AstNode* n = allocNode(AST_CLOSURE);
  n->as.closure.signature = NULL;
  n->as.closure.body = NULL;
  return n;
}

AstNode* newReturnNode(AstNode* value) {
  AstNode* n = allocNode(AST_RETURN);
  n->as.iReturn.value = value;
  return n;
}

AstNode* newClassNode(ObjString* name, ObjString* super) {
  AstNode* n = allocNode(AST_CLASS);
  n->as.classDef.name = name;
  n->as.classDef.super = super;
  initAstVec(&n->as.classDef.methods);
  return n;
}

AstNode* newImportNode(ObjString* module, ObjString* asName, ObjString* from) {
  AstNode* n = allocNode(AST_IMPORT);
  n->as.import.module = module;
  n->as.import.asName = asName;
  n->as.import.from = from;
  return n;
}

AstNode* newThrowNode(AstNode* expr) {
  AstNode* n = allocNode(AST_THROW);
  n->as.throw.expr = expr;
  return n;
}

AstNode* newSetTypeNode(ObjString* name, AstNode* typeExpr, int isGlobal) {
  AstNode* n = allocNode(AST_SET_TYPE);
  n->as.setType.name = name;
  n->as.setType.typeExpr = typeExpr;
  return n;
}

AstNode* newQuantifyNode(ObjString* quant, ObjString* var, AstNode* scope) {
  AstNode* n = allocNode(AST_QUANTIFY);
  n->as.quantify.quant = quant;
  n->as.quantify.var = var;
  n->as.quantify.scope = scope;
  return n;
}

AstNode* newIterNode(ObjString* var, AstNode* iterable, AstNode* body) {
  AstNode* n = allocNode(AST_ITER);
  n->as.iter.var = var;
  n->as.iter.iterable = iterable;
  n->as.iter.body = body;
  return n;
}

AstNode* newOverloadNode(ObjString* symbol, AstNode* impl) {
  AstNode* n = allocNode(AST_OVERLOAD);
  n->as.overload.symbol = symbol;
  n->as.overload.impl = impl;
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
  switch (node->type) {
    case AST_BLOCK:
      printStrAt("Block\n", depth);
      printNodeVecAt(&node->as.block.stmts, depth + 1);
      break;

    case AST_CALL:
      printStrAt("Call\n", depth);
      printNodeAt(node->as.call.callee, depth + 1);
      printNodeVecAt(&node->as.call.args, depth + 1);
      break;

    case AST_CALL_INFIX:
      printStrAt("CallInfix\n", depth);
      printNodeAt(node->as.callInfix.callee, depth + 1);
      printNodeAt(node->as.callInfix.lhs, depth + 1);
      printNodeAt(node->as.callInfix.rhs, depth + 1);
      break;

    case AST_CLOSURE:
      printStrAt("Closure\n", depth);
      printNodeAt(node->as.closure.signature, depth + 1);
      printNodeAt(node->as.closure.body, depth + 1);
      break;

    case AST_LET:
      printStrAt("Let\n", depth);
      printStrAt(node->as.let.name->chars, depth + 1);
      printNodeAt(node->as.closure.body, depth + 1);
      break;

    case AST_LITERAL:
      printStrAt("Literal ", depth);
      printValue(node->as.literal.value);
      break;

    case AST_MODULE:
      printStrAt("Module\n", depth);
      printNodeVecAt(&node->as.module.stmts, depth + 1);
      break;

    case AST_RETURN:
      printStrAt("Return\n", depth);
      printNodeAt(node->as.iReturn.value, depth + 1);
      break;

    case AST_SIGNATURE:
      printStrAt("Signature\n", depth);
      printNodeVecAt(&node->as.signature.params, depth + 1);
      break;

    case AST_SPREAD:
      printStrAt("Spread\n", depth);
      printNodeVecAt(&node->as.signature.params, depth + 1);
      break;

    case AST_VARIABLE:
      printStrAt("Var ", depth);
      printf("\"%s\"", node->as.variable.name->chars);
      break;

    case AST_UNKNOWN:
    default: {
      fprintf(stderr, "Unexpected ast type (%i)", node->type);
      exit(2);
    }
  }

  printf("\n");
}

void printNode(AstNode* node) { printNodeAt(node, 0); }

bool nodesEqual(AstNode* a, AstNode* b) {
  if (a->type != b->type) return false;

  switch (a->type) {
    case AST_BLOCK:
      return astVecsEqual(&a->as.block.stmts, &b->as.block.stmts);

    case AST_CALL:
      return nodesEqual(a->as.call.callee, b->as.call.callee) &&
             astVecsEqual(&a->as.call.args, &b->as.call.args);

    case AST_CALL_INFIX:
      return nodesEqual(a->as.callInfix.callee, b->as.callInfix.callee) &&
             nodesEqual(a->as.callInfix.lhs, b->as.callInfix.lhs) &&
             nodesEqual(a->as.callInfix.rhs, b->as.callInfix.rhs);

    case AST_CLOSURE:
      return nodesEqual(a->as.closure.signature, b->as.closure.signature) &&
             nodesEqual(a->as.closure.body, b->as.closure.body);

    case AST_LET:
      return a->as.let.name == b->as.let.name &&
             nodesEqual(a->as.let.value, b->as.let.value);

    case AST_LITERAL:
      return valuesEqual(a->as.literal.value, b->as.literal.value);

    case AST_MODULE:
      return a->as.module.name == b->as.module.name &&
             astVecsEqual(&a->as.module.stmts, &b->as.module.stmts);

    case AST_RETURN:
      return nodesEqual(a->as.iReturn.value, b->as.iReturn.value);

    case AST_SIGNATURE:
      return a->as.signature.varargs == b->as.signature.varargs &&
             astVecsEqual(&a->as.signature.params, &b->as.signature.params);
    case AST_SPREAD:
      return nodesEqual(a->as.spread.expr, b->as.spread.expr);
    case AST_VARIABLE:
      return a->as.variable.name == b->as.variable.name;

    case AST_UNKNOWN:
    default: {
      fprintf(stderr, "Unexpected ast type (%i)", a->type);
      exit(2);
    }
  }
}

// memory.
// ============================================================

void markAstNode(AstNode* n) {
  if (!n) return;

  switch (n->type) {
    case AST_LITERAL:
      /* Value may reference Obj* (e.g., strings); mark via markValue */
      markValue(n->as.literal.value);
      break;

    case AST_VARIABLE:
      markObject((Obj*)n->as.variable.name);
      break;

    case AST_CALL:
      markAstNode(n->as.call.callee);
      for (int i = 0; i < n->as.call.args.count; i++) {
        markAstNode((AstNode*)n->as.call.args.items[i]);
      }
      break;

    case AST_MEMBER:
      markAstNode(n->as.member.object);
      markObject((Obj*)n->as.member.member);
      break;

    case AST_SUBSCRIPT:
      markAstNode(n->as.subscript.collection);
      markAstNode(n->as.subscript.index);
      break;

    case AST_COMPREHENSION:
      markAstNode(n->as.comprehension.body);
      markObject((Obj*)n->as.comprehension.var);
      markAstNode(n->as.comprehension.iterable);
      markAstNode(n->as.comprehension.pred);
      break;

    case AST_SIGNATURE:
      for (int i = 0; i < n->as.signature.params.count; i++)
        markObject((Obj*)n->as.signature.params.items[i]);
      break;

    case AST_SPREAD:
      markAstNode(n->as.spread.expr);
      break;

    case AST_CLOSURE:
      markAstNode(n->as.closure.signature);
      markAstNode(n->as.closure.body);
      break;

    case AST_RETURN:
      markAstNode(n->as.iReturn.value);
      break;

    case AST_CLASS:
      markObject((Obj*)n->as.classDef.name);
      markObject((Obj*)n->as.classDef.super);
      for (int i = 0; i < n->as.classDef.methods.count; i++) {
        markAstNode((AstNode*)n->as.classDef.methods.items[i]);
      }
      break;

    case AST_IMPORT:
      markObject((Obj*)n->as.import.module);
      markObject((Obj*)n->as.import.asName);
      markObject((Obj*)n->as.import.from);
      break;

    case AST_THROW:
      markAstNode(n->as.throw.expr);
      break;

    case AST_SET_TYPE:
      markObject((Obj*)n->as.setType.name);
      markAstNode(n->as.setType.typeExpr);
      break;

    case AST_QUANTIFY:
      markObject((Obj*)n->as.quantify.quant);
      markObject((Obj*)n->as.quantify.var);
      markAstNode(n->as.quantify.scope);
      break;

    case AST_ITER:
      markObject((Obj*)n->as.iter.var);
      markAstNode(n->as.iter.iterable);
      markAstNode(n->as.iter.body);
      break;

    case AST_OVERLOAD:
      markObject((Obj*)n->as.overload.symbol);
      markAstNode(n->as.overload.impl);
      break;

    case AST_UNIT:
    case AST_UNKNOWN:
    default:
      /* nothing extra to mark */
      break;
  }
}

void freeAstNode(AstNode* n) {
  if (!n) return;

  switch (n->type) {
    case AST_LITERAL:
      /* Value/ObjString inside Value are GC-managed; nothing to free */
      break;

    case AST_VARIABLE:
      /* ObjString* name is GC-managed */
      break;

    case AST_CALL:
      freeAstNode(n->as.call.callee);
      for (int i = 0; i < n->as.call.args.count; i++) {
        freeAstNode((AstNode*)n->as.call.args.items[i]);
      }
      freeAstVec(&n->as.call.args);
      break;

    case AST_CALL_INFIX:
      freeAstNode(n->as.callInfix.callee);
      freeAstNode(n->as.callInfix.lhs);
      freeAstNode(n->as.callInfix.rhs);

    case AST_MEMBER:
      freeAstNode(n->as.member.object);
      /* member (ObjString*) is GC-managed */
      break;

    case AST_SUBSCRIPT:
      freeAstNode(n->as.subscript.collection);
      freeAstNode(n->as.subscript.index);
      break;

    case AST_COMPREHENSION:
      freeAstNode(n->as.comprehension.body);
      /* var (ObjString*) is GC-managed */
      freeAstNode(n->as.comprehension.iterable);
      freeAstNode(n->as.comprehension.pred);
      break;

    case AST_SIGNATURE:
      freeAstVec(&n->as.signature.params);
      break;

    case AST_CLOSURE:
      freeAstNode(n->as.closure.signature);
      freeAstNode(n->as.closure.body);
      break;

    case AST_RETURN:
      freeAstNode(n->as.iReturn.value);
      break;

    case AST_CLASS:
      /* name/super (ObjString*) are GC-managed */
      for (int i = 0; i < n->as.classDef.methods.count; i++) {
        freeAstNode((AstNode*)n->as.classDef.methods.items[i]);
      }
      freeAstVec(&n->as.classDef.methods);
      break;

    case AST_IMPORT:
      /* module/asName/from are ObjString* (GC-managed) */
      break;

    case AST_THROW:
      freeAstNode(n->as.throw.expr);
      break;

    case AST_SET_TYPE:
      /* name (ObjString*) is GC-managed */
      freeAstNode(n->as.setType.typeExpr);
      break;

    case AST_SPREAD:
      freeAstNode(n->as.spread.expr);
      break;

    case AST_QUANTIFY:
      /* quant/var are ObjString* (GC-managed) */
      freeAstNode(n->as.quantify.scope);
      break;

    case AST_ITER:
      /* var (ObjString*) is GC-managed */
      freeAstNode(n->as.iter.iterable);
      freeAstNode(n->as.iter.body);
      break;

    case AST_OVERLOAD:
      /* symbol (ObjString*) is GC-managed */
      freeAstNode(n->as.overload.impl);
      break;

    case AST_UNIT:
    case AST_UNKNOWN:
    default:
      /* nothing extra */
      break;
  }

  /* finally free the node struct itself */
  FREE(AstNode, n);
}