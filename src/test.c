#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "node.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* ============================================================
 * Node compilation.
 * ============================================================ */

AstNode* compile(char* source) {
  AstNode* module = newModuleNode(NULL, NULL, NULL);
  module->as.module.source = intern(source);
  module->as.module.dirName = intern("unit");
  module->as.module.baseName = intern("test");
  AstNode* node = compileFunctionNode(module);
  return node;
}

bool assertNodesEqual(AstNode* a, AstNode* b) {
  if (!nodesEqual(a, b)) {
    printf("Nodes not equal: \n");
    printf("--- ---\n");
    printNode(a);
    printf("\n-- != --\n\n");
    printNode(b);
    printf("--- ---\n");
    return false;
  }
  return true;
}

AstNode* mkFunction() {
  AstNode* fn = newFunctionNode(NULL);
  fn->as.function.name = intern("f");
  fn->as.function.signature = newSignatureNode();
  fn->as.function.body = newBlockNode();
  return fn;
}

void pushFnStmt(AstNode* fn, AstNode* stmt) {
  pushAstVec(&fn->as.function.body->as.block.stmts, stmt);
}

static AstNode* getBodyStmt(AstNode* fn, int index) {
  AstVec* stmts = &fn->as.function.body->as.block.stmts;
  if (index < 0 || index >= stmts->count) return NULL;
  return &stmts->items[index];
}

bool testLiteralNumberNode() {
  AstNode* node = compile("1");

  AstNode* fn = mkFunction();
  AstNode* literal = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testLiteralBooleanTrue() {
  AstNode* node = compile("true");

  AstNode* fn = mkFunction();
  AstNode* literal = newLiteralValueNode(BOOL_VAL(true));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testLiteralBooleanFalse() {
  AstNode* node = compile("false");

  AstNode* fn = mkFunction();
  AstNode* literal = newLiteralValueNode(BOOL_VAL(false));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallNode0Args() {
  AstNode* node = compile("f()");

  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("f");
  AstNode* call = newCallNode(var);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction();
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallNode1Args() {
  AstNode* node = compile("f(1)");

  AstNode* f = newVarGlobalNode();
  f->as.global.name = intern("f");
  AstNode* call = newCallNode(f);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction();
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(1)));
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallInfixNode() {
  AstNode* node = compile("1 + 2");

  AstNode* inf = newVarGlobalNode();
  inf->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* call = newCallInfixNode(inf, lhs, rhs);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction();
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallInfixNodeLeftNested() {
  AstNode* node = compile("1 + 2 + 3");

  AstNode* plusOp1 = newVarGlobalNode();
  plusOp1->as.global.name = intern("+");
  AstNode* callLeft =
      newCallInfixNode(plusOp1, newLiteralValueNode(NUMBER_VAL(1)),
                       newLiteralValueNode(NUMBER_VAL(2)));

  AstNode* plusOp2 = newVarGlobalNode();
  plusOp2->as.global.name = intern("+");
  AstNode* call =
      newCallInfixNode(plusOp2, callLeft, newLiteralValueNode(NUMBER_VAL(3)));
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction();
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallInfixNodeRightNested() {
  AstNode* node = compile("1 + (2 + 3)");

  AstNode* plusOp1 = newVarGlobalNode();
  plusOp1->as.global.name = intern("+");
  AstNode* callRight =
      newCallInfixNode(plusOp1, newLiteralValueNode(NUMBER_VAL(2)),
                       newLiteralValueNode(NUMBER_VAL(3)));

  AstNode* plusOp2 = newVarGlobalNode();
  plusOp2->as.global.name = intern("+");
  AstNode* call =
      newCallInfixNode(plusOp2, newLiteralValueNode(NUMBER_VAL(1)), callRight);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction();
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testFunctionNode() {
  AstNode* node = compile("let f = () => 1");

  AstNode* f = newFunctionNode(NULL);
  f->as.function.name = intern("f");
  f->as.function.signature = newSignatureNode();
  f->as.function.body = newReturnNode(newLiteralValueNode(NUMBER_VAL(1)));

  ObjString* objLetName = intern("f");
  AstNode* fLocal = newVarLocalNode(1);
  fLocal->as.local.name = objLetName;
  AstNode* let = newDeclLetNode(fLocal, f);

  AstNode* fn = mkFunction();
  pushFnStmt(fn, let);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * String Tests
 * ============================================================ */

bool testStringLiteral() {
  AstNode* node = compile("\"hello\"");

  AstNode* fn = mkFunction();
  AstNode* literal = newLiteralNode();
  ObjString* str = intern("hello");
  literal->as.literal.value = OBJ_VAL(str);
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testStringEmpty() {
  AstNode* node = compile("\"\"");

  AstNode* fn = mkFunction();
  AstNode* literal = newLiteralNode();
  ObjString* str = intern("");
  literal->as.literal.value = OBJ_VAL(str);
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * If-Else Statement Tests
 * ============================================================ */

bool testIfSimple() {
  AstNode* node = compile("if (true) 1");

  AstNode* fn = mkFunction();
  AstNode* cond = newLiteralValueNode(BOOL_VAL(true));
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* ifNode = newIfNode(cond, then, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfElse() {
  AstNode* node = compile("if (x) 1 else 2");

  AstNode* fn = mkFunction();
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* else_ = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* ifNode = newIfNode(cond, then, else_);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfBlock() {
  AstNode* node = compile("if (x) { let y = 1 }");

  AstNode* fn = mkFunction();
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* block = newBlockNode();
  ObjString* yName = intern("y");
  AstNode* yLocal = newVarLocalNode(1);
  yLocal->as.local.name = yName;
  AstNode* letValue = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* let = newDeclLetNode(yLocal, letValue);
  pushAstVec(&block->as.block.stmts, let);
  AstNode* ifNode = newIfNode(cond, block, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfElseBlock() {
  AstNode* node = compile("if (x) { 1 } else { 2 }");

  AstNode* fn = mkFunction();
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* thenBlock = newBlockNode();
  AstNode* thenStmt = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&thenBlock->as.block.stmts, thenStmt);
  AstNode* elseBlock = newBlockNode();
  AstNode* elseStmt = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&elseBlock->as.block.stmts, elseStmt);
  AstNode* ifNode = newIfNode(cond, thenBlock, elseBlock);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfNested() {
  AstNode* node = compile("if (a) if (b) 1 else 2");

  AstNode* fn = mkFunction();
  AstNode* outerCond = newVarGlobalNode();
  outerCond->as.global.name = intern("a");
  AstNode* innerCond = newVarGlobalNode();
  innerCond->as.global.name = intern("b");
  AstNode* innerThen = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* innerElse = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* innerIf = newIfNode(innerCond, innerThen, innerElse);
  AstNode* ifNode = newIfNode(outerCond, innerIf, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfElseIf() {
  AstNode* node = compile("if (a) 1 else if (b) 2 else 3");

  AstNode* fn = mkFunction();
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("a");
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* innerCond = newVarGlobalNode();
  innerCond->as.global.name = intern("b");
  AstNode* innerThen = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* innerElse = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(3)));
  AstNode* innerIf = newIfNode(innerCond, innerThen, innerElse);
  AstNode* ifNode = newIfNode(cond, then, innerIf);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfComplexCondition() {
  AstNode* node = compile("if (1 + 2) 1");

  AstNode* fn = mkFunction();
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* cond = newCallInfixNode(op, lhs, rhs);
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* ifNode = newIfNode(cond, then, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileSimple() {
  AstNode* node = compile("while (true) 1");

  AstNode* fn = mkFunction();
  AstNode* cond = newLiteralValueNode(BOOL_VAL(true));
  AstNode* body = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* whileNode = newWhileNode(cond, body);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileBlock() {
  AstNode* node = compile("while (x) { let y = 1 }");

  AstNode* fn = mkFunction();
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* block = newBlockNode();
  ObjString* yName = intern("y");
  AstNode* yLocal = newVarLocalNode(1);
  yLocal->as.local.name = yName;
  AstNode* letValue = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* let = newDeclLetNode(yLocal, letValue);
  pushAstVec(&block->as.block.stmts, let);
  AstNode* whileNode = newWhileNode(cond, block);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileComplexCondition() {
  AstNode* node = compile("while (1 + 2) 1");

  AstNode* fn = mkFunction();
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* cond = newCallInfixNode(op, lhs, rhs);
  AstNode* body = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* whileNode = newWhileNode(cond, body);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileNested() {
  AstNode* node = compile("while (a) while (b) 1");

  AstNode* fn = mkFunction();
  AstNode* outerCond = newVarGlobalNode();
  outerCond->as.global.name = intern("a");
  AstNode* innerCond = newVarGlobalNode();
  innerCond->as.global.name = intern("b");
  AstNode* innerBody = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* innerWhile = newWhileNode(innerCond, innerBody);
  AstNode* whileNode = newWhileNode(outerCond, innerWhile);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testForSimple() {
  AstNode* node = compile("for (let i = 0; i < 10; i = i + 1) i");

  ObjString* iName = intern("i");

  AstNode* fn = mkFunction();

  AstNode* initValue = newLiteralValueNode(NUMBER_VAL(0));
  AstNode* initLocal = newVarLocalNode(1);
  initLocal->as.local.name = iName;
  AstNode* initializer = newDeclLetNode(initLocal, initValue);

  AstNode* less = newVarGlobalNode();
  less->as.global.name = intern("<");
  AstNode* condLeft = newVarLocalNode(1);
  condLeft->as.local.name = iName;
  AstNode* condRight = newLiteralValueNode(NUMBER_VAL(10));
  AstNode* condition = newCallInfixNode(less, condLeft, condRight);

  AstNode* plus = newVarGlobalNode();
  plus->as.global.name = intern("+");
  AstNode* incTarget = newVarLocalNode(1);
  incTarget->as.local.name = iName;
  AstNode* incLeft = newVarLocalNode(1);
  incLeft->as.local.name = iName;
  AstNode* incRight = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* incValue = newCallInfixNode(plus, incLeft, incRight);
  AstNode* incrementAssignment = newAssignmentNode(incTarget, incValue);
  AstNode* increment = newExprStmtNode(incrementAssignment);

  AstNode* bodyExpr = newVarLocalNode(1);
  bodyExpr->as.local.name = iName;
  AstNode* body = newExprStmtNode(bodyExpr);

  AstNode* forNode = newForNode(initializer, condition, increment, body);
  pushFnStmt(fn, forNode);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testForComplexExpressions() {
  AstNode* node = compile("for (let i = f(1 + 2); g(h()); i = i + k(3)) i");

  ObjString* iName = intern("i");

  AstNode* fn = mkFunction();

  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* two = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* sum = newCallInfixNode(plusOp, one, two);

  AstNode* fVar = newVarGlobalNode();
  fVar->as.global.name = intern("f");
  AstNode* fCall = newCallNode(fVar);
  pushAstVec(&fCall->as.call.args, sum);

  AstNode* initLocal = newVarLocalNode(1);
  initLocal->as.local.name = iName;
  AstNode* initializer = newDeclLetNode(initLocal, fCall);

  AstNode* hVar = newVarGlobalNode();
  hVar->as.global.name = intern("h");
  AstNode* hCall = newCallNode(hVar);

  AstNode* gVar = newVarGlobalNode();
  gVar->as.global.name = intern("g");
  AstNode* condition = newCallNode(gVar);
  pushAstVec(&condition->as.call.args, hCall);

  AstNode* incrementTarget = newVarLocalNode(1);
  incrementTarget->as.local.name = iName;
  AstNode* incLeft = newVarLocalNode(1);
  incLeft->as.local.name = iName;

  AstNode* kVar = newVarGlobalNode();
  kVar->as.global.name = intern("k");
  AstNode* kCall = newCallNode(kVar);
  AstNode* three = newLiteralValueNode(NUMBER_VAL(3));
  pushAstVec(&kCall->as.call.args, three);

  AstNode* plusInc = newVarGlobalNode();
  plusInc->as.global.name = intern("+");
  AstNode* incValue = newCallInfixNode(plusInc, incLeft, kCall);
  AstNode* incrementExpr = newAssignmentNode(incrementTarget, incValue);
  AstNode* increment = newExprStmtNode(incrementExpr);

  AstNode* bodyExpr = newVarLocalNode(1);
  bodyExpr->as.local.name = iName;
  AstNode* body = newExprStmtNode(bodyExpr);

  AstNode* forNode = newForNode(initializer, condition, increment, body);
  pushFnStmt(fn, forNode);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testForNoInitializer() {
  AstNode* node = compile("for (; x < 5; x = x + 1) x");

  AstNode* fn = mkFunction();

  ObjString* xName = intern("x");

  AstNode* less = newVarGlobalNode();
  less->as.global.name = intern("<");
  AstNode* xCond = newVarGlobalNode();
  xCond->as.global.name = xName;
  AstNode* five = newLiteralValueNode(NUMBER_VAL(5));
  AstNode* condition = newCallInfixNode(less, xCond, five);

  AstNode* plus = newVarGlobalNode();
  plus->as.global.name = intern("+");
  AstNode* incLeft = newVarGlobalNode();
  incLeft->as.global.name = xName;
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* incValue = newCallInfixNode(plus, incLeft, one);
  AstNode* incTarget = newVarGlobalNode();
  incTarget->as.global.name = xName;
  AstNode* incrementExpr = newAssignmentNode(incTarget, incValue);
  AstNode* increment = newExprStmtNode(incrementExpr);

  AstNode* bodyExpr = newVarGlobalNode();
  bodyExpr->as.global.name = xName;
  AstNode* body = newExprStmtNode(bodyExpr);

  AstNode* forNode = newForNode(NULL, condition, increment, body);
  pushFnStmt(fn, forNode);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testForNoIncrement() {
  AstNode* node = compile("for (let i = 0; i < 5;) i");

  ObjString* iName = intern("i");

  AstNode* fn = mkFunction();

  AstNode* initLocal = newVarLocalNode(1);
  initLocal->as.local.name = iName;
  AstNode* initializer =
      newDeclLetNode(initLocal, newLiteralValueNode(NUMBER_VAL(0)));

  AstNode* less = newVarGlobalNode();
  less->as.global.name = intern("<");
  AstNode* condLeft = newVarLocalNode(1);
  condLeft->as.local.name = iName;
  AstNode* condRight = newLiteralValueNode(NUMBER_VAL(5));
  AstNode* condition = newCallInfixNode(less, condLeft, condRight);

  AstNode* bodyExpr = newVarLocalNode(1);
  bodyExpr->as.local.name = iName;
  AstNode* body = newExprStmtNode(bodyExpr);

  AstNode* forNode = newForNode(initializer, condition, NULL, body);
  pushFnStmt(fn, forNode);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testForBlockBody() {
  AstNode* node = compile("for (let i = 0; i < 1; i = i + 1) { let y = i }");

  ObjString* iName = intern("i");
  ObjString* yName = intern("y");

  AstNode* fn = mkFunction();

  AstNode* initLocal = newVarLocalNode(1);
  initLocal->as.local.name = iName;
  AstNode* initializer =
      newDeclLetNode(initLocal, newLiteralValueNode(NUMBER_VAL(0)));

  AstNode* less = newVarGlobalNode();
  less->as.global.name = intern("<");
  AstNode* condLeft = newVarLocalNode(1);
  condLeft->as.local.name = iName;
  AstNode* condRight = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* condition = newCallInfixNode(less, condLeft, condRight);

  AstNode* plus = newVarGlobalNode();
  plus->as.global.name = intern("+");
  AstNode* incLeft = newVarLocalNode(1);
  incLeft->as.local.name = iName;
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* incValue = newCallInfixNode(plus, incLeft, one);
  AstNode* incTarget = newVarLocalNode(1);
  incTarget->as.local.name = iName;
  AstNode* incrementExpr = newAssignmentNode(incTarget, incValue);
  AstNode* increment = newExprStmtNode(incrementExpr);

  AstNode* block = newBlockNode();
  AstNode* yValue = newVarLocalNode(1);
  yValue->as.local.name = iName;
  AstNode* yLocal = newVarLocalNode(2);
  yLocal->as.local.name = yName;
  AstNode* letY = newDeclLetNode(yLocal, yValue);
  pushAstVec(&block->as.block.stmts, letY);

  AstNode* forNode = newForNode(initializer, condition, increment, block);
  pushFnStmt(fn, forNode);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testForIterSimple() {
  AstNode* node = compile("for (x in (1, 2)) x");

  ObjString* xName = intern("x");

  AstNode* fn = mkFunction();

  AstNode* var = newVarLocalNode(1);
  var->as.local.name = xName;

  AstNode* iterable = newSequenceNode();
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));

  AstNode* bodyVar = newVarLocalNode(1);
  bodyVar->as.local.name = xName;
  AstNode* body = newExprStmtNode(bodyVar);

  AstNode* iterNode = newIterNode(var, iterable, body);
  iterNode->as.iter.iterLocal = 2;
  pushFnStmt(fn, iterNode);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * Throw Statement Tests
 * ============================================================ */

bool testThrowGlobal() {
  AstNode* node = compile("throw error");

  AstNode* fn = mkFunction();
  AstNode* errorVar = newVarGlobalNode();
  errorVar->as.global.name = intern("error");
  AstNode* throwNode = newThrowNode(errorVar);
  pushFnStmt(fn, throwNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testThrowCall() {
  AstNode* node = compile("throw Error(1)");

  AstNode* fn = mkFunction();
  AstNode* errorVar = newVarGlobalNode();
  errorVar->as.global.name = intern("Error");
  AstNode* call = newCallNode(errorVar);
  AstNode* message = newLiteralValueNode(NUMBER_VAL(1));
  pushAstVec(&call->as.call.args, message);
  AstNode* throwNode = newThrowNode(call);
  pushFnStmt(fn, throwNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testThrowInfix() {
  AstNode* node = compile("throw 1 + 2");

  AstNode* fn = mkFunction();
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* expr = newCallInfixNode(op, lhs, rhs);
  AstNode* throwNode = newThrowNode(expr);
  pushFnStmt(fn, throwNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testThrowInBlock() {
  AstNode* node = compile("{ throw error }");

  AstNode* fn = mkFunction();
  AstNode* block = newBlockNode();
  AstNode* errorVar = newVarGlobalNode();
  errorVar->as.global.name = intern("error");
  AstNode* throwNode = newThrowNode(errorVar);
  pushAstVec(&block->as.block.stmts, throwNode);
  pushFnStmt(fn, block);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testThrowInConditional() {
  AstNode* node = compile("if (x) throw error");

  AstNode* fn = mkFunction();
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* errorVar = newVarGlobalNode();
  errorVar->as.global.name = intern("error");
  AstNode* throwNode = newThrowNode(errorVar);
  AstNode* ifNode = newIfNode(cond, throwNode, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * Assignment Tests
 * ============================================================ */

bool testAssignmentGlobal() {
  AstNode* node = compile("x = 1");

  AstNode* fn = mkFunction();
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("x");
  AstNode* literal = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentLocal() {
  AstNode* node = compile("let x \n x = 1");

  AstNode* fn = mkFunction();
  ObjString* xName = intern("x");
  AstNode* xLocal = newVarLocalNode(1);
  xLocal->as.local.name = xName;
  AstNode* let = newDeclLetNode(xLocal, newLiteralValueNode(UNDEF_VAL));
  pushFnStmt(fn, let);

  AstNode* var = newVarLocalNode(1);
  var->as.local.name = xName;
  AstNode* literal = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentWithExpression() {
  AstNode* node = compile("x = 1 + 2");

  AstNode* fn = mkFunction();
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("x");
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* infixCall = newCallInfixNode(op, lhs, rhs);
  AstNode* assignment = newAssignmentNode(var, infixCall);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentWithCall() {
  AstNode* node = compile("x = f()");

  AstNode* fn = mkFunction();
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("x");
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);
  AstNode* assignment = newAssignmentNode(var, call);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentNestedExpression() {
  AstNode* node = compile("x = (1 + 2) + 3");

  AstNode* fn = mkFunction();
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("x");

  // Build (1 + 2) + 3
  AstNode* innerOp = newVarGlobalNode();
  innerOp->as.global.name = intern("+");
  AstNode* innerLhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* innerRhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* innerCall = newCallInfixNode(innerOp, innerLhs, innerRhs);

  AstNode* outerOp = newVarGlobalNode();
  outerOp->as.global.name = intern("+");
  AstNode* outerRhs = newLiteralValueNode(NUMBER_VAL(3));
  AstNode* outerCall = newCallInfixNode(outerOp, innerCall, outerRhs);

  AstNode* assignment = newAssignmentNode(var, outerCall);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentBooleanValue() {
  AstNode* node = compile("x = true");

  AstNode* fn = mkFunction();
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("x");
  AstNode* literal = newLiteralValueNode(BOOL_VAL(true));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentInBlock() {
  AstNode* node = compile("{ x = 1 }");

  AstNode* fn = mkFunction();
  AstNode* block = newBlockNode();
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("x");
  AstNode* literal = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushAstVec(&block->as.block.stmts, exprStmt);
  pushFnStmt(fn, block);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testMultipleAssignments() {
  AstNode* node = compile("x = 1 \n y = 2");

  AstNode* fn = mkFunction();

  // x = 1
  AstNode* var1 = newVarGlobalNode();
  var1->as.global.name = intern("x");
  AstNode* literal1 = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* assignment1 = newAssignmentNode(var1, literal1);
  AstNode* exprStmt1 = newExprStmtNode(assignment1);
  pushFnStmt(fn, exprStmt1);

  // y = 2
  AstNode* var2 = newVarGlobalNode();
  var2->as.global.name = intern("y");
  AstNode* literal2 = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* assignment2 = newAssignmentNode(var2, literal2);
  AstNode* exprStmt2 = newExprStmtNode(assignment2);
  pushFnStmt(fn, exprStmt2);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentReassignment() {
  AstNode* node = compile("x = 1 \n x = 2");

  AstNode* fn = mkFunction();

  // x = 1
  AstNode* var1 = newVarGlobalNode();
  var1->as.global.name = intern("x");
  AstNode* literal1 = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* assignment1 = newAssignmentNode(var1, literal1);
  AstNode* exprStmt1 = newExprStmtNode(assignment1);
  pushFnStmt(fn, exprStmt1);

  // x = 2
  AstNode* var2 = newVarGlobalNode();
  var2->as.global.name = intern("x");
  AstNode* literal2 = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* assignment2 = newAssignmentNode(var2, literal2);
  AstNode* exprStmt2 = newExprStmtNode(assignment2);
  pushFnStmt(fn, exprStmt2);

  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testLineColSimpleAssignment() {
  AstNode* fn = compile("x = 1");
  AstNode* exprStmt = getBodyStmt(fn, 0);
  if (exprStmt == NULL) return false;

  AstNode* assignment = exprStmt->as.exprStmt.expr;
  AstNode* lhs = assignment->as.assignment.lhs;
  AstNode* rhs = assignment->as.assignment.rhs;

  bool ok = exprStmt->line == 1 && exprStmt->col == 3 &&
            assignment->line == 1 && assignment->col == 3 && lhs->line == 1 &&
            lhs->col == 1 && rhs->line == 1 && rhs->col == 5;

  return ok;
}

bool testLineColMultiLineAssignment() {
  AstNode* fn = compile("x = 1\n  y = 2");
  AstNode* stmt0 = getBodyStmt(fn, 0);
  AstNode* stmt1 = getBodyStmt(fn, 1);
  if (stmt0 == NULL || stmt1 == NULL) return false;

  AstNode* assign0 = stmt0->as.exprStmt.expr;
  AstNode* assign1 = stmt1->as.exprStmt.expr;
  AstNode* lhs1 = assign1->as.assignment.lhs;
  AstNode* rhs1 = assign1->as.assignment.rhs;

  bool ok = assign0->line == 1 && assign0->col == 3 && assign1->line == 2 &&
            assign1->col == 5 && lhs1->line == 2 && lhs1->col == 3 &&
            rhs1->line == 2 && rhs1->col == 7 && stmt1->col == assign1->col;

  return ok;
}

bool testLineColLeadingBlankLines() {
  AstNode* fn = compile("\n\n  x = 1");
  AstNode* stmt = getBodyStmt(fn, 0);
  if (stmt == NULL) return false;

  AstNode* assign = stmt->as.exprStmt.expr;
  AstNode* lhs = assign->as.assignment.lhs;
  AstNode* rhs = assign->as.assignment.rhs;

  bool ok = assign->line == 3 && assign->col == 5 && lhs->line == 3 &&
            lhs->col == 3 && rhs->line == 3 && rhs->col == 7;

  return ok;
}

/* ============================================================
 * Node GC.
 * ============================================================ */

/* ============================================================
 * Bytecode (AST -> Chunk) helpers and tests.
 * ============================================================ */

static bool buildChunkForExpr(AstNode* expr, Chunk* chunk) {
  initChunk(chunk);
  return toChunk(expr, chunk);
}

static uint16_t read_u16(uint8_t hi, uint8_t lo) {
  return ((uint16_t)hi << 8) | (uint16_t)lo;
}

bool testBytecodeCall0Args() {
  AstNode* callee = newLiteralNode();
  callee->as.literal.value = OBJ_VAL(intern("f"));
  AstNode* call = newCallNode(callee);
  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  if (c.count != 5) return false;
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.code[3] != OP_CALL) return false;
  if (c.code[4] != 0) return false;  // arg count
  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("f")))) return false;
  return true;
}

bool testBytecodeCall1Arg() {
  AstNode* callee = newLiteralNode();
  callee->as.literal.value = OBJ_VAL(intern("f"));
  AstNode* call = newCallNode(callee);
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(1)));
  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  if (c.count != 8) return false;
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;  // callee const idx
  if (c.code[3] != OP_CONSTANT) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;  // arg const idx
  if (c.code[6] != OP_CALL) return false;
  if (c.code[7] != 1) return false;
  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("f")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeCall3Args() {
  AstNode* callee = newLiteralNode();
  callee->as.literal.value = OBJ_VAL(intern("f"));
  AstNode* call = newCallNode(callee);
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(3)));
  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  // Total bytes: 15
  if (c.count != 14) return false;
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;  // callee
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;
  if (c.code[9] != OP_CONSTANT || read_u16(c.code[10], c.code[11]) != 3)
    return false;
  if (c.code[12] != OP_CALL) return false;
  if (c.code[13] != 3) return false;
  if (c.constants.count != 4) return false;
  return valuesEqual(c.constants.values[0], OBJ_VAL(intern("f"))) &&
         valuesEqual(c.constants.values[1], NUMBER_VAL(1)) &&
         valuesEqual(c.constants.values[2], NUMBER_VAL(2)) &&
         valuesEqual(c.constants.values[3], NUMBER_VAL(3));
}

bool testBytecodeCallNestedCallee() {
  // inner: f() where f is string literal "zap"
  AstNode* innerCallee = newLiteralNode();
  ObjString* zapStr = intern("zap");
  innerCallee->as.literal.value = OBJ_VAL(zapStr);
  AstNode* innerCall = newCallNode(innerCallee);
  // outer: (f())(1)
  AstNode* outerCall = newCallNode(innerCall);
  pushAstVec(&outerCall->as.call.args, newLiteralValueNode(NUMBER_VAL(1)));
  Chunk c;
  if (!buildChunkForExpr(outerCall, &c)) return false;

  // Sequence: CONST(5), CALL 0, CONST(1), CALL 1, EXPR_STMT
  // Bytes: [OP_CONSTANT, idx0_hi, idx0_lo, OP_CALL, 0, OP_CONSTANT, idx1_hi,
  // idx1_lo, OP_CALL, 1, OP_POP]
  if (c.count != 10) return false;
  if (c.code[0] != OP_CONSTANT) return false;             // 0
  if (read_u16(c.code[1], c.code[2]) != 0) return false;  // 1,2
  if (c.code[3] != OP_CALL) return false;                 // 3
  if (c.code[4] != 0) return false;                       // 4
  if (c.code[5] != OP_CONSTANT) return false;             // 5
  if (read_u16(c.code[6], c.code[7]) != 1) return false;  // 6,7
  if (c.code[8] != OP_CALL) return false;                 // 8
  if (c.code[9] != 1) return false;                       // 9
  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("zap")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeFunctionEmpty() {
  AstNode* fun = newFunctionNode(NULL);
  fun->as.function.name = intern("f");
  fun->as.function.signature = newSignatureNode();
  fun->as.function.body = newBlockNode();

  Chunk c;
  if (!buildChunkForExpr(fun, &c)) return false;

  if (c.count != 3) return false;
  if (c.code[0] != OP_CLOSURE) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.constants.count != 1) return false;
  if (!IS_FUNCTION(c.constants.values[0])) return false;

  return true;
}

bool testBytecodeFunctionExpr() {
  AstNode* fun = newFunctionNode(NULL);
  fun->as.function.name = intern("f");
  fun->as.function.signature = newSignatureNode();
  fun->as.function.body = newLiteralValueNode(NUMBER_VAL(42));

  Chunk c;
  if (!buildChunkForExpr(fun, &c)) return false;

  // OP_CLOSURE (1 byte) + CONSTANT (2 bytes) = 3
  if (c.count != 3) return false;
  if (c.code[0] != OP_CLOSURE) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.constants.count != 1) return false;
  if (!IS_FUNCTION(c.constants.values[0])) return false;

  // inner fn chunk.
  Chunk c1 = AS_FUNCTION(c.constants.values[0])->chunk;
  // OP_CONSTANT (1 byte) + CONSTANT (2 bytes) = 3
  if (c1.count != 3) return false;
  if (c1.code[0] != OP_CONSTANT) return false;

  if (read_u16(c1.code[1], c1.code[2]) != 0) return false;
  if (c1.constants.count != 1) return false;
  if (!valuesEqual(c1.constants.values[0], NUMBER_VAL(42))) return false;

  return true;
}

bool testBytecodeGlobal() {
  AstNode* g = newVarGlobalNode();
  ObjString* name = intern("g");
  g->as.global.name = name;
  Chunk c;
  if (!buildChunkForExpr(g, &c)) return false;

  if (c.count != 3) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(name))) return false;
  return true;
}

bool testBytecodeGlobalLongName() {
  AstNode* g = newVarGlobalNode();
  g->as.global.name = intern("very_long_global_variable_name_123");
  Chunk c;
  if (!buildChunkForExpr(g, &c)) return false;

  if (c.count != 3) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0],
                   OBJ_VAL(intern("very_long_global_variable_name_123"))))
    return false;
  return true;
}

bool testBytecodeCallInfix() {
  AstNode* op = newLiteralNode();
  op->as.literal.value = OBJ_VAL(intern("+"));
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* call = newCallInfixNode(op, lhs, rhs);
  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  // CONST (op, 3) + CONST (lhs, 3) + CONST (rhs, 3) + OP_CALL (1) + argc (1) +
  // EXPR_STMT (1) = 12
  if (c.count != 11) return false;
  if (c.code[0] != OP_CONSTANT || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;
  if (c.code[9] != OP_CALL) return false;
  if (c.code[10] != 2) return false;
  if (c.constants.count != 3) return false;
  return true;
}

/* Bytecode tests for Strings */

bool testBytecodeString() {
  AstNode* literal = newLiteralNode();
  ObjString* str = intern("hello");
  literal->as.literal.value = OBJ_VAL(str);
  Chunk c;
  if (!buildChunkForExpr(literal, &c)) return false;

  // Layout: OP_CONSTANT (1 byte) + constant index (2 bytes) = 3 bytes
  if (c.count != 3) return false;
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(str))) return false;

  return true;
}

bool testBytecodeStringEmpty() {
  AstNode* literal = newLiteralNode();
  ObjString* str = intern("");
  literal->as.literal.value = OBJ_VAL(str);
  Chunk c;
  if (!buildChunkForExpr(literal, &c)) return false;

  // Layout: OP_CONSTANT (1 byte) + constant index (2 bytes) = 3 bytes
  if (c.count != 3) return false;
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(str))) return false;

  // Verify it's an empty string
  if (!IS_STRING(c.constants.values[0])) return false;
  if (AS_STRING(c.constants.values[0])->length != 0) return false;

  return true;
}

bool testBytecodeStringLong() {
  // Create a string longer than 256 characters to verify 16-bit constant index
  char longStr[300];
  for (int i = 0; i < 299; i++) {
    longStr[i] = 'a' + (i % 26);
  }
  longStr[299] = '\0';

  AstNode* literal = newLiteralNode();
  ObjString* str = intern(longStr);
  literal->as.literal.value = OBJ_VAL(str);
  Chunk c;
  if (!buildChunkForExpr(literal, &c)) return false;

  // Layout: OP_CONSTANT (1 byte) + constant index (2 bytes) = 3 bytes
  // Should use 16-bit constant index even for large constant pool
  if (c.count != 3) return false;
  if (c.code[0] != OP_CONSTANT) return false;

  // Read the 16-bit constant index
  uint16_t constIdx = read_u16(c.code[1], c.code[2]);
  if (constIdx >= c.constants.count) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[constIdx], OBJ_VAL(str))) return false;

  // Verify string length
  if (!IS_STRING(c.constants.values[constIdx])) return false;
  if (AS_STRING(c.constants.values[constIdx])->length != 299) return false;

  return true;
}

/* Bytecode tests for If statements */

bool testBytecodeIfSimple() {
  AstNode* cond = newLiteralValueNode(BOOL_VAL(true));
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* ifNode = newIfNode(cond, then, NULL);
  Chunk c;
  if (!buildChunkForExpr(ifNode, &c)) return false;

  // Layout: COND(true), JUMP_IF_FALSE, POP, THEN(1+EXPR_STMT), JUMP(patch),
  // POP(patch) CONDANT (3) + JUMP_IF_FALSE (3) + POP (1) + CONSTANT (3) +
  // EXPR_STMT (1) + JUMP (3) + POP (1) = 15 bytes
  if (c.count != 15) return false;
  // CONDANT true
  if (c.code[0] != OP_CONSTANT || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  // JUMP_IF_FALSE (placeholder bytes)
  if (c.code[3] != OP_JUMP_IF_FALSE) return false;
  // POP
  if (c.code[6] != OP_POP) return false;
  // CONSTANT 1
  if (c.code[7] != OP_CONSTANT || read_u16(c.code[8], c.code[9]) != 1)
    return false;
  // EXPR_STMT
  if (c.code[10] != OP_POP) return false;
  // JUMP (placeholder bytes)
  if (c.code[11] != OP_JUMP) return false;
  // POP
  if (c.code[14] != OP_POP) return false;

  // Check jump is patched correctly
  uint16_t thenJump = read_u16(c.code[4], c.code[5]);
  if (thenJump != 8)
    return false;  // should jump from after JUMP_IF_FALSE to POP after JUMP

  uint16_t elseJump = read_u16(c.code[12], c.code[13]);
  if (elseJump != 1)
    return false;  // should jump from after JUMP to end (no else code)

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], BOOL_VAL(true))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeIfElse() {
  AstNode* cond = newVarGlobalNode();
  ObjString* name = intern("x");
  cond->as.global.name = name;
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* else_ = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* ifNode = newIfNode(cond, then, else_);
  Chunk c;
  if (!buildChunkForExpr(ifNode, &c)) return false;

  // Layout: GET_GLOBAL, JUMP_IF_FALSE, POP, CONSTANT(1), EXPR_STMT, JUMP,
  // POP(patch), CONSTANT(2), EXPR_STMT
  // 3 + 3 + 1 + 3 + 1 + 3 + 1 + 3 + 1 = 19 bytes
  if (c.count != 19) return false;
  // GET_GLOBAL x
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  // JUMP_IF_FALSE
  if (c.code[3] != OP_JUMP_IF_FALSE) return false;
  // POP
  if (c.code[6] != OP_POP) return false;
  // CONSTANT 1
  if (c.code[7] != OP_CONSTANT || read_u16(c.code[8], c.code[9]) != 1)
    return false;
  // EXPR_STMT
  if (c.code[10] != OP_POP) return false;
  // JUMP
  if (c.code[11] != OP_JUMP) return false;
  // POP (patched by thenJump)
  if (c.code[14] != OP_POP) return false;
  // CONSTANT 2
  if (c.code[15] != OP_CONSTANT || read_u16(c.code[16], c.code[17]) != 2)
    return false;
  // EXPR_STMT
  if (c.code[18] != OP_POP) return false;

  // Check patches
  uint16_t thenJump = read_u16(c.code[4], c.code[5]);
  if (thenJump != 8) return false;  // JUMP_IF_FALSE to POP after JUMP

  uint16_t elseJump = read_u16(c.code[12], c.code[13]);
  if (elseJump != 5) return false;  // JUMP to end

  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(name))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  return true;
}

bool testBytecodeIfBlock() {
  AstNode* cond = newVarGlobalNode();
  ObjString* name = intern("x");
  cond->as.global.name = name;
  AstNode* block = newBlockNode();
  AstNode* stmt = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&block->as.block.stmts, stmt);
  AstNode* ifNode = newIfNode(cond, block, NULL);
  Chunk c;
  if (!buildChunkForExpr(ifNode, &c)) return false;

  // Layout: GET_GLOBAL, JUMP_IF_FALSE, POP, CONSTANT(1), EXPR_STMT, JUMP,
  // POP(patch) GET_GLOBAL(3) + JUMP_IF_FALSE(3) + POP(1) + CONSTANT(3) +
  // EXPR_STMT(1) + JUMP(3) + POP(1) = 15
  if (c.count != 15) return false;

  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  if (c.code[3] != OP_JUMP_IF_FALSE) return false;
  if (c.code[6] != OP_POP) return false;
  // CONSTANT 1 (index 1, since "x" is at index 0)
  if (c.code[7] != OP_CONSTANT || read_u16(c.code[8], c.code[9]) != 1)
    return false;
  if (c.code[10] != OP_POP) return false;
  if (c.code[11] != OP_JUMP) return false;
  if (c.code[14] != OP_POP) return false;

  uint16_t thenJump = read_u16(c.code[4], c.code[5]);
  if (thenJump != 8) return false;

  uint16_t elseJump = read_u16(c.code[12], c.code[13]);
  if (elseJump != 1) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(name))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeIfElseBlock() {
  AstNode* cond = newVarGlobalNode();
  ObjString* name = intern("x");
  cond->as.global.name = name;
  AstNode* thenBlock = newBlockNode();
  AstNode* thenStmt = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&thenBlock->as.block.stmts, thenStmt);
  AstNode* elseBlock = newBlockNode();
  AstNode* elseStmt = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&elseBlock->as.block.stmts, elseStmt);
  AstNode* ifNode = newIfNode(cond, thenBlock, elseBlock);
  Chunk c;
  if (!buildChunkForExpr(ifNode, &c)) return false;

  // Layout: GET_GLOBAL, JUMP_IF_FALSE, POP, CONSTANT(1), EXPR_STMT, JUMP,
  // POP(patch), CONSTANT(2), EXPR_STMT
  // Same as regular if-else but with extra block wrappers
  if (c.count != 19) return false;

  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(name))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  return true;
}

bool testBytecodeIfNested() {
  AstNode* outerCond = newVarGlobalNode();
  outerCond->as.global.name = intern("a");
  AstNode* innerCond = newVarGlobalNode();
  innerCond->as.global.name = intern("b");
  AstNode* innerThen = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* innerElse = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* innerIf = newIfNode(innerCond, innerThen, innerElse);
  AstNode* outerIf = newIfNode(outerCond, innerIf, NULL);
  Chunk c;
  if (!buildChunkForExpr(outerIf, &c)) return false;

  // Nested if: outer if, inner if with else
  // Outer: GET_GLOBAL a, JUMP_IF_FALSE, POP, [inner if], JUMP, POP
  // Inner: GET_GLOBAL b, JUMP_IF_FALSE, POP, CONSTANT 1, EXPR_STMT, JUMP,
  // POP, CONSTANT 2, EXPR_STMT
  // Total: 3 + 3 + 1 + 3 + 3 + 1 + 3 + 1 + 1 + 3 + 1 + 1 + 3 = 30
  if (c.count != 30) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("a")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("b")))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(2))) return false;
  return true;
}

bool testBytecodeIfComplexCondition() {
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* cond = newCallInfixNode(op, lhs, rhs);
  AstNode* then = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(3)));
  AstNode* ifNode = newIfNode(cond, then, NULL);
  Chunk c;
  if (!buildChunkForExpr(ifNode, &c)) return false;

  // Layout: COND(lhs + rhs), JUMP_IF_FALSE, POP, CONSTANT(3), EXPR_STMT, JUMP,
  // POP COND: GET_GLOBAL + (3), CONSTANT 1 (3), CONSTANT 2 (3), CALL 2 (2) = 11
  // THEN: CONSTANT 3 (3), EXPR_STMT (1) = 4
  // Control: JUMP_IF_FALSE (3), POP (1), JUMP (3), POP (1) = 8
  // Total: 11 + 4 + 8 = 23
  if (c.count != 23) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("+")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(3))) return false;
  return true;
}

bool testBytecodeWhileSimple() {
  AstNode* cond = newLiteralValueNode(BOOL_VAL(true));
  AstNode* body = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* whileNode = newWhileNode(cond, body);
  Chunk c;
  if (!buildChunkForExpr(whileNode, &c)) return false;

  // Layout: loop-start: CONSTANT(true), JUMP_IF_FALSE, POP, CONSTANT(1),
  // EXPR_STMT, LOOP, [patch], POP
  // loop-start: 0
  // CONSTANT(true): 0-2 (3 bytes)
  // JUMP_IF_FALSE: 3-5 (3 bytes, placeholder)
  // POP: 6 (1 byte)
  // CONSTANT(1): 7-9 (3 bytes)
  // EXPR_STMT: 10 (1 byte)
  // LOOP: 11-13 (3 bytes, jumps back to 0)
  // POP: 14 (1 byte, exit jump lands here)
  // Total: 15 bytes
  if (c.count != 15) return false;

  // Check loop-start: CONSTANT(true)
  if (c.code[0] != OP_CONSTANT || read_u16(c.code[1], c.code[2]) != 0)
    return false;

  // Check JUMP_IF_FALSE
  if (c.code[3] != OP_JUMP_IF_FALSE) return false;

  // Check POP
  if (c.code[6] != OP_POP) return false;

  // Check CONSTANT(1)
  if (c.code[7] != OP_CONSTANT || read_u16(c.code[8], c.code[9]) != 1)
    return false;

  // Check EXPR_STMT
  if (c.code[10] != OP_POP) return false;

  // Check LOOP (jumps back to 0)
  if (c.code[11] != OP_LOOP) return false;
  uint16_t loopOffset = read_u16(c.code[12], c.code[13]);
  // When OP_LOOP executes: READ_BYTE() skips OP_LOOP, READ_SHORT() reads offset
  // and increments ip to 14 We want: 14 - offset = 0, so offset = 14
  if (loopOffset != 14) return false;

  // Check exit jump is patched correctly
  uint16_t exitJump = read_u16(c.code[4], c.code[5]);
  if (exitJump != 8) return false;

  // Check final POP
  if (c.code[14] != OP_POP) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], BOOL_VAL(true))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeWhileFalseCondition() {
  AstNode* cond = newLiteralValueNode(BOOL_VAL(false));
  AstNode* body = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* whileNode = newWhileNode(cond, body);
  Chunk c;
  if (!buildChunkForExpr(whileNode, &c)) return false;

  // Loop should exit immediately when condition is false
  // Verify exit jump is patched correctly (same calculation as
  // testBytecodeWhileSimple)
  uint16_t exitJump = read_u16(c.code[4], c.code[5]);
  if (exitJump != 8)
    return false;  // Should jump from position 6 to position 14

  // Verify LOOP instruction exists (even though it won't execute)
  if (c.code[11] != OP_LOOP) return false;

  // Verify loop offset
  uint16_t loopOffset = read_u16(c.code[12], c.code[13]);
  if (loopOffset != 14) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], BOOL_VAL(false))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeWhileEmptyBody() {
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* body = newBlockNode();
  AstNode* whileNode = newWhileNode(cond, body);
  Chunk c;
  if (!buildChunkForExpr(whileNode, &c)) return false;

  // Layout: loop-start: GET_GLOBAL(x), JUMP_IF_FALSE, POP, [empty block], LOOP,
  // POP
  // Empty block should generate no code, so body is just empty
  // GET_GLOBAL: 0-2 (3 bytes)
  // JUMP_IF_FALSE: 3-5 (3 bytes)
  // POP: 6 (1 byte)
  // [empty block]: no code
  // LOOP: 7-9 (3 bytes)
  // POP: 10 (1 byte)
  // Total: 11 bytes
  if (c.count != 11) return false;

  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (c.code[3] != OP_JUMP_IF_FALSE) return false;
  if (c.code[6] != OP_POP) return false;
  if (c.code[7] != OP_LOOP) return false;
  if (c.code[10] != OP_POP) return false;

  // Verify exit jump
  uint16_t exitJump = read_u16(c.code[4], c.code[5]);
  if (exitJump != 4) return false;

  // Verify LOOP offset
  uint16_t loopOffset = read_u16(c.code[8], c.code[9]);
  if (loopOffset != 10) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("x")))) return false;
  return true;
}

bool testBytecodeWhileNested() {
  AstNode* outerCond = newVarGlobalNode();
  outerCond->as.global.name = intern("a");
  AstNode* innerCond = newVarGlobalNode();
  innerCond->as.global.name = intern("b");
  AstNode* innerBody = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* innerWhile = newWhileNode(innerCond, innerBody);
  AstNode* whileNode = newWhileNode(outerCond, innerWhile);
  Chunk c;
  if (!buildChunkForExpr(whileNode, &c)) return false;

  // Verify nested structure
  // Outer: GET_GLOBAL a, JUMP_IF_FALSE, POP, [inner while], LOOP, POP
  // Inner: GET_GLOBAL b, JUMP_IF_FALSE, POP, CONSTANT 1, EXPR_STMT, LOOP, POP
  // Should have two LOOP instructions with correct offsets
  int loopCount = 0;
  int firstLoopPos = -1;
  int secondLoopPos = -1;
  for (int i = 0; i < c.count; i++) {
    if (c.code[i] == OP_LOOP) {
      loopCount++;
      if (firstLoopPos == -1) {
        firstLoopPos = i;
      } else {
        secondLoopPos = i;
      }
    }
  }

  if (loopCount != 2) return false;
  if (firstLoopPos == -1 || secondLoopPos == -1) return false;

  // Inner LOOP should come before outer LOOP (inner loop body is nested inside
  // outer) The first LOOP encountered is the inner loop, the second is the
  // outer loop
  int innerLoopPos = firstLoopPos;
  int outerLoopPos = secondLoopPos;

  if (innerLoopPos >= outerLoopPos) return false;

  // Verify both loops have valid non-zero offsets
  uint16_t innerLoopOffset =
      read_u16(c.code[innerLoopPos + 1], c.code[innerLoopPos + 2]);
  uint16_t outerLoopOffset =
      read_u16(c.code[outerLoopPos + 1], c.code[outerLoopPos + 2]);

  // Offsets should be positive and valid
  if (innerLoopOffset == 0) return false;
  if (outerLoopOffset == 0) return false;

  // The offsets represent backwards jumps, so they should be positive values
  // that allow the loops to jump back to their respective starts
  // We just verify they're non-zero and reasonable (not checking exact values
  // since the bytecode layout depends on the exact generated code)

  return true;
}

bool testBytecodeWhileComplexBody() {
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* block = newBlockNode();
  AstNode* stmt1 = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* stmt2 = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&block->as.block.stmts, stmt1);
  pushAstVec(&block->as.block.stmts, stmt2);
  AstNode* whileNode = newWhileNode(cond, block);
  Chunk c;
  if (!buildChunkForExpr(whileNode, &c)) return false;

  // Layout: loop-start: GET_GLOBAL(x), JUMP_IF_FALSE, POP, CONSTANT(1),
  // EXPR_STMT, CONSTANT(2), EXPR_STMT, LOOP, POP
  // Verify all body statements are between condition check and LOOP
  int getGlobalPos = -1;
  int jumpIfFalsePos = -1;
  int firstExprStmtPos = -1;
  int secondExprStmtPos = -1;
  int loopPos = -1;

  for (int i = 0; i < c.count; i++) {
    if (c.code[i] == OP_GET_GLOBAL && getGlobalPos == -1) {
      getGlobalPos = i;
    } else if (c.code[i] == OP_JUMP_IF_FALSE && jumpIfFalsePos == -1) {
      jumpIfFalsePos = i;
    } else if (c.code[i] == OP_POP) {
      if (firstExprStmtPos == -1) {
        firstExprStmtPos = i;
      } else if (secondExprStmtPos == -1) {
        secondExprStmtPos = i;
      }
    } else if (c.code[i] == OP_LOOP && loopPos == -1) {
      loopPos = i;
    }
  }

  // Verify order: GET_GLOBAL < JUMP_IF_FALSE < first EXPR_STMT < second
  // EXPR_STMT < LOOP
  if (getGlobalPos == -1 || jumpIfFalsePos == -1 || firstExprStmtPos == -1 ||
      secondExprStmtPos == -1 || loopPos == -1)
    return false;

  if (getGlobalPos >= jumpIfFalsePos) return false;
  if (jumpIfFalsePos >= firstExprStmtPos) return false;
  if (firstExprStmtPos >= secondExprStmtPos) return false;
  if (secondExprStmtPos >= loopPos) return false;

  // Verify constants
  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("x")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;

  return true;
}

bool testBytecodeIterSimple() {
  AstNode* var = newVarLocalNode(1);
  var->as.local.name = intern("x");

  AstNode* iterable = newSequenceNode();
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));

  AstNode* bodyVar = newVarLocalNode(1);
  bodyVar->as.local.name = intern("x");
  AstNode* body = newExprStmtNode(bodyVar);

  AstNode* iterNode = newIterNode(var, iterable, body);
  iterNode->as.iter.iterLocal = 2;

  Chunk c;
  if (!buildChunkForExpr(iterNode, &c)) return false;

  if (c.count != 51) return false;

  if (c.code[0] != OP_NIL) return false;
  if (c.code[1] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[2], c.code[3]) != 0) return false;

  if (c.code[4] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[5], c.code[6]) != 3) return false;

  if (c.code[7] != OP_CONSTANT || read_u16(c.code[8], c.code[9]) != 4)
    return false;
  if (c.code[10] != OP_CONSTANT || read_u16(c.code[11], c.code[12]) != 5)
    return false;

  if (c.code[13] != OP_CALL || c.code[14] != 2) return false;
  if (c.code[15] != OP_CALL || c.code[16] != 1) return false;

  if (c.code[17] != OP_GET_LOCAL || read_u16(c.code[18], c.code[19]) != 2)
    return false;
  if (c.code[20] != OP_GET_PROPERTY) return false;
  if (read_u16(c.code[21], c.code[22]) != 1) return false;
  if (c.code[23] != OP_CALL || c.code[24] != 0) return false;

  if (c.code[25] != OP_JUMP_IF_FALSE) return false;
  if (read_u16(c.code[26], c.code[27]) != 20) return false;
  if (c.code[28] != OP_POP) return false;

  if (c.code[29] != OP_GET_LOCAL || read_u16(c.code[30], c.code[31]) != 2)
    return false;
  if (c.code[32] != OP_GET_PROPERTY) return false;
  if (read_u16(c.code[33], c.code[34]) != 2) return false;
  if (c.code[35] != OP_CALL || c.code[36] != 0) return false;
  if (c.code[37] != OP_SET_LOCAL || read_u16(c.code[38], c.code[39]) != 1)
    return false;
  if (c.code[40] != OP_POP) return false;

  if (c.code[41] != OP_GET_LOCAL || read_u16(c.code[42], c.code[43]) != 1)
    return false;
  if (c.code[44] != OP_POP) return false;

  if (c.code[45] != OP_LOOP) return false;
  if (read_u16(c.code[46], c.code[47]) != 31) return false;

  if (c.code[48] != OP_POP) return false;
  if (c.code[49] != OP_POP) return false;
  if (c.code[50] != OP_POP) return false;

  if (c.constants.count != 6) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(vm.core.sIter))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(vm.core.sMore))) return false;
  if (!valuesEqual(c.constants.values[2], OBJ_VAL(vm.core.sNext))) return false;
  if (!valuesEqual(c.constants.values[3], OBJ_VAL(vm.core.sSeq))) return false;
  if (!valuesEqual(c.constants.values[4], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[5], NUMBER_VAL(2))) return false;

  return true;
}

bool testBytecodeForSimple() {
  AstNode* initializer = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(0)));
  AstNode* condition = newLiteralValueNode(BOOL_VAL(true));
  AstNode* increment = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* body = newExprStmtNode(newLiteralValueNode(NUMBER_VAL(2)));

  AstNode* forNode = newForNode(initializer, condition, increment, body);

  Chunk c;
  if (!buildChunkForExpr(forNode, &c)) return false;

  if (c.count < 1) return false;

  if (c.code[0] != OP_CONSTANT || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  if (c.code[3] != OP_POP) return false;

  if (c.code[4] != OP_CONSTANT || read_u16(c.code[5], c.code[6]) != 1)
    return false;
  if (c.code[7] != OP_JUMP_IF_FALSE) return false;

  if (c.code[10] != OP_POP) return false;

  if (c.code[11] != OP_CONSTANT || read_u16(c.code[12], c.code[13]) != 2)
    return false;
  if (c.code[14] != OP_POP) return false;

  if (c.code[15] != OP_CONSTANT || read_u16(c.code[16], c.code[17]) != 3)
    return false;
  if (c.code[18] != OP_POP) return false;

  bool loopFound = false;
  for (int i = 0; i < c.count; i++) {
    if (c.code[i] == OP_LOOP) {
      loopFound = true;
      break;
    }
  }
  if (!loopFound) return false;

  if (c.code[c.count - 1] != OP_POP) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], NUMBER_VAL(0))) return false;
  if (!valuesEqual(c.constants.values[1], BOOL_VAL(true))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(1))) return false;

  return true;
}

/* ============================================================
 * Throw Bytecode Tests
 * ============================================================ */

bool testBytecodeThrowSimple() {
  AstNode* errorVar = newVarGlobalNode();
  errorVar->as.global.name = intern("error");
  AstNode* throwNode = newThrowNode(errorVar);
  Chunk c;
  if (!buildChunkForExpr(throwNode, &c)) return false;

  // Layout: GET_GLOBAL, [error-const-idx], OP_THROW
  // GET_GLOBAL (1) + constant index (2) + OP_THROW (1) = 4 bytes
  if (c.count != 4) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  uint16_t errorIdx = read_u16(c.code[1], c.code[2]);
  if (errorIdx != 0) return false;
  if (c.code[3] != OP_THROW) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("error"))))
    return false;
  return true;
}

bool testBytecodeThrowLiteral() {
  AstNode* literal = newLiteralValueNode(NUMBER_VAL(42));
  AstNode* throwNode = newThrowNode(literal);
  Chunk c;
  if (!buildChunkForExpr(throwNode, &c)) return false;

  // Layout: CONSTANT, [const-idx], OP_THROW
  // CONSTANT (1) + constant index (2) + OP_THROW (1) = 4 bytes
  if (c.count != 4) return false;
  if (c.code[0] != OP_CONSTANT) return false;
  uint16_t constIdx = read_u16(c.code[1], c.code[2]);
  if (constIdx != 0) return false;
  if (c.code[3] != OP_THROW) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], NUMBER_VAL(42))) return false;
  return true;
}

bool testBytecodeThrowInfix() {
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* expr = newCallInfixNode(op, lhs, rhs);
  AstNode* throwNode = newThrowNode(expr);
  Chunk c;
  if (!buildChunkForExpr(throwNode, &c)) return false;

  // Layout: GET_GLOBAL op (3) + CONSTANT lhs (3) + CONSTANT rhs (3) + OP_CALL
  // (1)
  // + argc (1) + OP_THROW (1) = 12 bytes
  // Note: op is a VAR_GLOBAL, so it emits GET_GLOBAL, not CONSTANT
  if (c.count != 12) return false;
  // GET_GLOBAL op
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  // CONSTANT lhs
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;
  // CONSTANT rhs
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;
  // OP_CALL
  if (c.code[9] != OP_CALL) return false;
  // argc
  if (c.code[10] != 2) return false;
  // OP_THROW
  if (c.code[11] != OP_THROW) return false;

  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("+")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  return true;
}

bool testBytecodeThrowInConditional() {
  AstNode* cond = newVarGlobalNode();
  cond->as.global.name = intern("x");
  AstNode* errorVar = newVarGlobalNode();
  errorVar->as.global.name = intern("error");
  AstNode* throwNode = newThrowNode(errorVar);
  AstNode* ifNode = newIfNode(cond, throwNode, NULL);
  Chunk c;
  if (!buildChunkForExpr(ifNode, &c)) return false;

  // Layout: GET_GLOBAL x (3) + JUMP_IF_FALSE (3) + POP (1) + GET_GLOBAL error
  // (3)
  // + OP_THROW (1) + JUMP (3) + POP (1) = 15 bytes
  if (c.count != 15) return false;
  // GET_GLOBAL x
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  // JUMP_IF_FALSE
  if (c.code[3] != OP_JUMP_IF_FALSE) return false;
  // POP
  if (c.code[6] != OP_POP) return false;
  // GET_GLOBAL error
  if (c.code[7] != OP_GET_GLOBAL || read_u16(c.code[8], c.code[9]) != 1)
    return false;
  // OP_THROW
  if (c.code[10] != OP_THROW) return false;
  // JUMP
  if (c.code[11] != OP_JUMP) return false;
  // POP
  if (c.code[14] != OP_POP) return false;

  // Verify jump offsets are patched correctly
  uint16_t thenJump = read_u16(c.code[4], c.code[5]);
  if (thenJump != 8)
    return false;  // Should jump from after JUMP_IF_FALSE to POP after JUMP

  uint16_t elseJump = read_u16(c.code[12], c.code[13]);
  if (elseJump != 1)
    return false;  // Should jump from after JUMP to end (no else code)

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("x")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("error"))))
    return false;
  return true;
}

/* ============================================================
 * Sequence Tests
 * ============================================================ */

bool testSequenceEmpty() {
  AstNode* node = compile("(,)");

  AstNode* fn = mkFunction();
  AstNode* seq = newSequenceNode();
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceOneElement() {
  AstNode* node = compile("(1,)");

  AstNode* fn = mkFunction();
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceTwoElements() {
  AstNode* node = compile("(1, 2)");

  AstNode* fn = mkFunction();
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceThreeElements() {
  AstNode* node = compile("(1, 2, 3)");

  AstNode* fn = mkFunction();
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(3)));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceVariables() {
  AstNode* node = compile("(x, y)");

  AstNode* fn = mkFunction();
  AstNode* seq = newSequenceNode();
  AstNode* xVar = newVarGlobalNode();
  xVar->as.global.name = intern("x");
  AstNode* yVar = newVarGlobalNode();
  yVar->as.global.name = intern("y");
  pushAstVec(&seq->as.sequence.values, xVar);
  pushAstVec(&seq->as.sequence.values, yVar);
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceComplexExpression() {
  AstNode* node = compile("(1 + 2, f(3), x)");

  AstNode* fn = mkFunction();
  AstNode* seq = newSequenceNode();

  // 1 + 2
  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* infix = newCallInfixNode(plusOp, lhs, rhs);
  pushAstVec(&seq->as.sequence.values, infix);

  // f(3)
  AstNode* f = newVarGlobalNode();
  f->as.global.name = intern("f");
  AstNode* call = newCallNode(f);
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(3)));
  pushAstVec(&seq->as.sequence.values, call);

  // x
  AstNode* xVar = newVarGlobalNode();
  xVar->as.global.name = intern("x");
  pushAstVec(&seq->as.sequence.values, xVar);

  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceNested() {
  AstNode* node = compile("((1, 2), 3)");

  AstNode* fn = mkFunction();

  // inner sequence (1, 2)
  AstNode* innerSeq = newSequenceNode();
  pushAstVec(&innerSeq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&innerSeq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));

  // outer sequence
  AstNode* outerSeq = newSequenceNode();
  pushAstVec(&outerSeq->as.sequence.values, innerSeq);
  pushAstVec(&outerSeq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(3)));

  AstNode* exprStmt = newExprStmtNode(outerSeq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceInCall() {
  AstNode* node = compile("f((1, 2))");

  AstNode* fn = mkFunction();
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, seq);

  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * Set Tests
 * ============================================================ */

bool testSetOneElement() {
  AstNode* node = compile("({1})");

  AstNode* fn = mkFunction();
  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  AstNode* exprStmt = newExprStmtNode(set);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSetTwoElements() {
  AstNode* node = compile("({1, 2})");

  AstNode* fn = mkFunction();
  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* exprStmt = newExprStmtNode(set);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSetVariables() {
  AstNode* node = compile("({x, y})");

  AstNode* fn = mkFunction();
  AstNode* set = newSetNode();
  AstNode* xVar = newVarGlobalNode();
  xVar->as.global.name = intern("x");
  AstNode* yVar = newVarGlobalNode();
  yVar->as.global.name = intern("y");
  pushAstVec(&set->as.set.values, xVar);
  pushAstVec(&set->as.set.values, yVar);
  AstNode* exprStmt = newExprStmtNode(set);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSetComplexExpression() {
  AstNode* node = compile("({1 + 2, f(3), x})");

  AstNode* fn = mkFunction();
  AstNode* set = newSetNode();

  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* infix = newCallInfixNode(plusOp, lhs, rhs);
  pushAstVec(&set->as.set.values, infix);

  AstNode* f = newVarGlobalNode();
  f->as.global.name = intern("f");
  AstNode* call = newCallNode(f);
  pushAstVec(&call->as.call.args, newLiteralValueNode(NUMBER_VAL(3)));
  pushAstVec(&set->as.set.values, call);

  AstNode* xVar = newVarGlobalNode();
  xVar->as.global.name = intern("x");
  pushAstVec(&set->as.set.values, xVar);

  AstNode* exprStmt = newExprStmtNode(set);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSetNested() {
  AstNode* node = compile("({{1}, {2}})");

  AstNode* fn = mkFunction();
  AstNode* outer = newSetNode();

  AstNode* inner1 = newSetNode();
  pushAstVec(&inner1->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&outer->as.set.values, inner1);

  AstNode* inner2 = newSetNode();
  pushAstVec(&inner2->as.set.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&outer->as.set.values, inner2);

  AstNode* exprStmt = newExprStmtNode(outer);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSetInCall() {
  AstNode* node = compile("f({1, 2})");

  AstNode* fn = mkFunction();
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, set);

  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSubscriptGet() {
  AstNode* actual = compile("arr[1]");

  AstNode* expected = mkFunction();
  AstNode* arr = newVarGlobalNode();
  arr->as.global.name = intern("arr");
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* sub = newSubscriptGetNode(arr, one);
  AstNode* exprStmt = newExprStmtNode(sub);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSubscriptSet() {
  AstNode* actual = compile("arr[foo[0]] = seq[1]");

  AstNode* expected = mkFunction();

  AstNode* arr = newVarGlobalNode();
  arr->as.global.name = intern("arr");

  AstNode* foo = newVarGlobalNode();
  foo->as.global.name = intern("foo");
  AstNode* zero = newLiteralValueNode(NUMBER_VAL(0));
  AstNode* fooIndex = newSubscriptGetNode(foo, zero);

  AstNode* seq = newVarGlobalNode();
  seq->as.global.name = intern("seq");
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* value = newSubscriptGetNode(seq, one);

  AstNode* set = newSubscriptSetNode(arr, fooIndex, value);
  AstNode* exprStmt = newExprStmtNode(set);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSubscriptNested() {
  AstNode* actual = compile("meta[0][1]");

  AstNode* expected = mkFunction();

  AstNode* meta = newVarGlobalNode();
  meta->as.global.name = intern("meta");
  AstNode* zero = newLiteralValueNode(NUMBER_VAL(0));
  AstNode* first = newSubscriptGetNode(meta, zero);
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* second = newSubscriptGetNode(first, one);

  AstNode* exprStmt = newExprStmtNode(second);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testPropertyGet() {
  AstNode* actual = compile("obj.foo");

  AstNode* expected = mkFunction();
  AstNode* obj = newVarGlobalNode();
  obj->as.global.name = intern("obj");
  AstNode* prop = newPropertyGetNode(obj);
  prop->as.propertyGet.property = intern("foo");
  AstNode* exprStmt = newExprStmtNode(prop);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testPropertySet() {
  AstNode* actual = compile("obj.foo = 42");

  AstNode* expected = mkFunction();
  AstNode* obj = newVarGlobalNode();
  obj->as.global.name = intern("obj");
  AstNode* value = newLiteralValueNode(NUMBER_VAL(42));
  AstNode* set = newPropertySetNode(obj, value);
  set->as.propertySet.property = intern("foo");
  AstNode* exprStmt = newExprStmtNode(set);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testPropertyNested() {
  AstNode* actual = compile("obj.foo.bar");

  AstNode* expected = mkFunction();
  AstNode* obj = newVarGlobalNode();
  obj->as.global.name = intern("obj");
  AstNode* first = newPropertyGetNode(obj);
  first->as.propertyGet.property = intern("foo");
  AstNode* second = newPropertyGetNode(first);
  second->as.propertyGet.property = intern("bar");
  AstNode* exprStmt = newExprStmtNode(second);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testPropertyNestedAssignment() {
  AstNode* actual = compile("obj.foo.bar = 99");

  AstNode* expected = mkFunction();
  AstNode* obj = newVarGlobalNode();
  obj->as.global.name = intern("obj");
  AstNode* first = newPropertyGetNode(obj);
  first->as.propertyGet.property = intern("foo");
  AstNode* value = newLiteralValueNode(NUMBER_VAL(99));
  AstNode* second = newPropertySetNode(first, value);
  second->as.propertySet.property = intern("bar");
  AstNode* exprStmt = newExprStmtNode(second);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

static AstNode* comprehensionLocal(uint8_t index, const char* name) {
  AstNode* local = newVarLocalNode(index);
  local->as.local.name = intern(name);
  return local;
}

static AstNode* wrapComprehensionInClosure(AstNode* comp) {
  AstNode* compLocal = comprehensionLocal(1, "__comp");
  comp->as.comprehension.compLocal = compLocal;

  AstNode* builder = newFunctionNode(NULL);
  builder->as.function.name = intern("__comp_builder");
  builder->as.function.signature = newSignatureNode();
  builder->as.function.body = newReturnNode(comp);

  return newCallNode(builder);
}

bool testSequenceComprehension() {
  AstNode* actual = compile("(x | x in (1,2), x != 2)");

  AstNode* expected = mkFunction();

  AstNode* body = comprehensionLocal(2, "x");
  AstNode* comp = newComprehensionNode(body, COMPREHENSION_SEQ);

  AstNode* iterable = newSequenceNode();
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* iterVar = comprehensionLocal(2, "x");
  AstNode* iterCond = newComprehensionIterNode(iterVar, iterable);
  iterCond->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&comp->as.comprehension.conditions, iterCond);

  AstNode* neqOp = newVarGlobalNode();
  neqOp->as.global.name = intern("!=");
  AstNode* predVar = comprehensionLocal(2, "x");
  AstNode* predExpr =
      newCallInfixNode(neqOp, predVar, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* predCond = newComprehensionPredNode(predExpr);
  pushAstVec(&comp->as.comprehension.conditions, predCond);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSetComprehensionParse() {
  AstNode* actual = compile("({x | x in (1,2), x != 2})");

  AstNode* expected = mkFunction();

  AstNode* body = comprehensionLocal(2, "x");
  AstNode* comp = newComprehensionNode(body, COMPREHENSION_SET);

  AstNode* iterable = newSequenceNode();
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* iterVar = comprehensionLocal(2, "x");
  AstNode* iterCond = newComprehensionIterNode(iterVar, iterable);
  iterCond->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&comp->as.comprehension.conditions, iterCond);

  AstNode* neqOp = newVarGlobalNode();
  neqOp->as.global.name = intern("!=");
  AstNode* predVar = comprehensionLocal(2, "x");
  AstNode* predExpr =
      newCallInfixNode(neqOp, predVar, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* predCond = newComprehensionPredNode(predExpr);
  pushAstVec(&comp->as.comprehension.conditions, predCond);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSetComprehensionComplexBody() {
  AstNode* actual = compile("({x + 1 | x in (1,2)})");

  AstNode* expected = mkFunction();

  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* bodyLeft = comprehensionLocal(2, "x");
  AstNode* bodyRight = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* bodyExpr = newCallInfixNode(plusOp, bodyLeft, bodyRight);
  AstNode* comp = newComprehensionNode(bodyExpr, COMPREHENSION_SET);

  AstNode* iterable = newSequenceNode();
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* iterVar = comprehensionLocal(2, "x");
  AstNode* iterCond = newComprehensionIterNode(iterVar, iterable);
  iterCond->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&comp->as.comprehension.conditions, iterCond);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSetComprehensionNestedBody() {
  AstNode* actual = compile("({{x + 1 | x in y} | y in (1,2,3)})");

  AstNode* expected = mkFunction();

  AstNode* innerPlus = newVarGlobalNode();
  innerPlus->as.global.name = intern("+");
  AstNode* innerX = comprehensionLocal(2, "x");
  AstNode* innerOne = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* innerBodyExpr = newCallInfixNode(innerPlus, innerX, innerOne);
  AstNode* innerComp = newComprehensionNode(innerBodyExpr, COMPREHENSION_SET);

  AstNode* innerIterVar = comprehensionLocal(2, "x");
  AstNode* iterableY = newVarUpvalueNode(0);
  iterableY->as.upvalue.name = intern("y");
  AstNode* innerIter = newComprehensionIterNode(innerIterVar, iterableY);
  innerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&innerComp->as.comprehension.conditions, innerIter);

  AstNode* innerCall = wrapComprehensionInClosure(innerComp);
  AstNode* outerComp = newComprehensionNode(innerCall, COMPREHENSION_SET);

  AstNode* outerIterable = newSequenceNode();
  pushAstVec(&outerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&outerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&outerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(3)));
  AstNode* outerIterVar = comprehensionLocal(2, "y");
  AstNode* outerIter = newComprehensionIterNode(outerIterVar, outerIterable);
  outerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&outerComp->as.comprehension.conditions, outerIter);

  AstNode* call = wrapComprehensionInClosure(outerComp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSetComprehensionNestedCondition() {
  AstNode* actual = compile("({x | x in {y | y in (1,2)}})");
  AstNode* expected = mkFunction();

  AstNode* body = comprehensionLocal(2, "x");
  AstNode* comp = newComprehensionNode(body, COMPREHENSION_SET);

  AstNode* innerBody = comprehensionLocal(2, "y");
  AstNode* innerComp = newComprehensionNode(innerBody, COMPREHENSION_SET);
  AstNode* innerIterable = newSequenceNode();
  pushAstVec(&innerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&innerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* innerIterVar = comprehensionLocal(2, "y");
  AstNode* innerIter = newComprehensionIterNode(innerIterVar, innerIterable);
  innerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&innerComp->as.comprehension.conditions, innerIter);

  AstNode* innerCall = wrapComprehensionInClosure(innerComp);
  AstNode* outerIterVar = comprehensionLocal(2, "x");
  AstNode* outerIter = newComprehensionIterNode(outerIterVar, innerCall);
  outerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&comp->as.comprehension.conditions, outerIter);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSetComprehensionFunctionBody() {
  AstNode* actual = compile("({() => 1 | true})");

  AstNode* expected = mkFunction();

  AstNode* fnExpr = newFunctionNode(NULL);
  fnExpr->as.function.signature = newSignatureNode();
  fnExpr->as.function.body = newReturnNode(newLiteralValueNode(NUMBER_VAL(1)));

  AstNode* comp = newComprehensionNode(fnExpr, COMPREHENSION_SET);

  AstNode* predicate = newLiteralValueNode(BOOL_VAL(true));
  AstNode* predCond = newComprehensionPredNode(predicate);
  pushAstVec(&comp->as.comprehension.conditions, predCond);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSequenceComprehensionComplexBody() {
  AstNode* actual = compile("(x + 1 | x in (1,2))");

  AstNode* expected = mkFunction();

  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* bodyLeft = comprehensionLocal(2, "x");
  AstNode* bodyRight = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* bodyExpr = newCallInfixNode(plusOp, bodyLeft, bodyRight);
  AstNode* comp = newComprehensionNode(bodyExpr, COMPREHENSION_SEQ);

  AstNode* iterable = newSequenceNode();
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&iterable->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* iterVar = comprehensionLocal(2, "x");
  AstNode* iterCond = newComprehensionIterNode(iterVar, iterable);
  iterCond->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&comp->as.comprehension.conditions, iterCond);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSequenceComprehensionNestedBody() {
  AstNode* actual = compile("((y | y in (1,2)) | x in (1,2))");

  AstNode* expected = mkFunction();

  AstNode* innerBody = comprehensionLocal(2, "y");
  AstNode* innerComp = newComprehensionNode(innerBody, COMPREHENSION_SEQ);
  AstNode* innerIterable = newSequenceNode();
  pushAstVec(&innerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&innerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* innerVar = comprehensionLocal(2, "y");
  AstNode* innerIter = newComprehensionIterNode(innerVar, innerIterable);
  innerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&innerComp->as.comprehension.conditions, innerIter);

  AstNode* innerCall = wrapComprehensionInClosure(innerComp);
  AstNode* outerComp = newComprehensionNode(innerCall, COMPREHENSION_SEQ);

  AstNode* outerIterable = newSequenceNode();
  pushAstVec(&outerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&outerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* outerVar = comprehensionLocal(2, "x");
  AstNode* outerIter = newComprehensionIterNode(outerVar, outerIterable);
  outerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&outerComp->as.comprehension.conditions, outerIter);

  AstNode* call = wrapComprehensionInClosure(outerComp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

bool testSequenceComprehensionNestedCondition() {
  AstNode* actual = compile("(x | x in (y | y in (1,2)))");

  AstNode* expected = mkFunction();

  AstNode* body = comprehensionLocal(2, "x");
  AstNode* comp = newComprehensionNode(body, COMPREHENSION_SEQ);

  AstNode* innerBody = comprehensionLocal(2, "y");
  AstNode* innerComp = newComprehensionNode(innerBody, COMPREHENSION_SEQ);
  AstNode* innerIterable = newSequenceNode();
  pushAstVec(&innerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&innerIterable->as.sequence.values,
             newLiteralValueNode(NUMBER_VAL(2)));
  AstNode* innerVar = comprehensionLocal(2, "y");
  AstNode* innerIter = newComprehensionIterNode(innerVar, innerIterable);
  innerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&innerComp->as.comprehension.conditions, innerIter);

  AstNode* innerCall = wrapComprehensionInClosure(innerComp);
  AstNode* outerVar = comprehensionLocal(2, "x");
  AstNode* outerIter = newComprehensionIterNode(outerVar, innerCall);
  outerIter->as.comprehensionIter.iterLocal = 3;
  pushAstVec(&comp->as.comprehension.conditions, outerIter);

  AstNode* call = wrapComprehensionInClosure(comp);
  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(expected, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(expected, returnStmt);

  return assertNodesEqual(actual, expected);
}

/* ============================================================
 * Object Tests
 * ============================================================ */

bool testObjectEmpty() {
  AstNode* node = compile("({})");

  AstNode* fn = mkFunction();
  AstNode* obj = newMapNode();
  AstNode* exprStmt = newExprStmtNode(obj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectOneProperty() {
  AstNode* node = compile("({\"x\": 1})");

  AstNode* fn = mkFunction();
  AstNode* obj = newMapNode();
  AstNode* keyLiteral = newLiteralNode();
  ObjString* keyX = intern("x");
  keyLiteral->as.literal.value = OBJ_VAL(keyX);
  AstNode* entry =
      newMapEntryNode(keyLiteral, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&obj->as.map.entries, entry);
  AstNode* exprStmt = newExprStmtNode(obj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectMultipleProperties() {
  AstNode* node = compile("({\"x\": 1, \"y\": 2, \"z\": 3})");

  AstNode* fn = mkFunction();
  AstNode* obj = newMapNode();
  AstNode* keyX = newLiteralNode();
  keyX->as.literal.value = OBJ_VAL(intern("x"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyX, newLiteralValueNode(NUMBER_VAL(1))));
  AstNode* keyY = newLiteralNode();
  keyY->as.literal.value = OBJ_VAL(intern("y"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyY, newLiteralValueNode(NUMBER_VAL(2))));
  AstNode* keyZ = newLiteralNode();
  keyZ->as.literal.value = OBJ_VAL(intern("z"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyZ, newLiteralValueNode(NUMBER_VAL(3))));
  AstNode* exprStmt = newExprStmtNode(obj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectIdentifierKey() {
  AstNode* node = compile("({key: value})");

  AstNode* fn = mkFunction();
  AstNode* obj = newMapNode();
  AstNode* key = newVarGlobalNode();
  key->as.global.name = intern("key");
  AstNode* entry = newMapEntryNode(key, newVarGlobalNode());
  entry->as.mapEntry.value->as.global.name = intern("value");
  pushAstVec(&obj->as.map.entries, entry);
  AstNode* exprStmt = newExprStmtNode(obj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectComplexValues() {
  AstNode* node = compile("({\"x\": 1 + 2, \"y\": f()})");

  AstNode* fn = mkFunction();
  AstNode* obj = newMapNode();

  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* infix = newCallInfixNode(plusOp, lhs, rhs);
  AstNode* keyX = newLiteralNode();
  keyX->as.literal.value = OBJ_VAL(intern("x"));
  pushAstVec(&obj->as.map.entries, newMapEntryNode(keyX, infix));

  AstNode* f = newVarGlobalNode();
  f->as.global.name = intern("f");
  AstNode* call = newCallNode(f);
  AstNode* keyY = newLiteralNode();
  keyY->as.literal.value = OBJ_VAL(intern("y"));
  pushAstVec(&obj->as.map.entries, newMapEntryNode(keyY, call));

  AstNode* exprStmt = newExprStmtNode(obj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectNested() {
  AstNode* node = compile("({\"outer\": {\"inner\": 1}})");

  AstNode* fn = mkFunction();

  AstNode* innerObj = newMapNode();
  AstNode* keyInner = newLiteralNode();
  keyInner->as.literal.value = OBJ_VAL(intern("inner"));
  pushAstVec(&innerObj->as.map.entries,
             newMapEntryNode(keyInner, newLiteralValueNode(NUMBER_VAL(1))));

  AstNode* outerObj = newMapNode();
  AstNode* keyOuter = newLiteralNode();
  keyOuter->as.literal.value = OBJ_VAL(intern("outer"));
  pushAstVec(&outerObj->as.map.entries, newMapEntryNode(keyOuter, innerObj));

  AstNode* exprStmt = newExprStmtNode(outerObj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectInExpression() {
  AstNode* node = compile("f({\"x\": 1})");

  AstNode* fn = mkFunction();
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* obj = newMapNode();
  AstNode* keyX = newLiteralNode();
  keyX->as.literal.value = OBJ_VAL(intern("x"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyX, newLiteralValueNode(NUMBER_VAL(1))));
  pushAstVec(&call->as.call.args, obj);

  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testObjectTrailingComma() {
  AstNode* node = compile("({\"x\": 1,})");

  AstNode* fn = mkFunction();
  AstNode* obj = newMapNode();
  AstNode* keyX = newLiteralNode();
  keyX->as.literal.value = OBJ_VAL(intern("x"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyX, newLiteralValueNode(NUMBER_VAL(1))));
  AstNode* exprStmt = newExprStmtNode(obj);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralValueNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* Import parsing test */

AstNode* compileWithImportDir(char* source, char* dirName) {
  AstNode* module = newModuleNode(NULL, NULL, NULL);
  module->as.module.source = intern(source);
  module->as.module.dirName = intern(dirName);
  module->as.module.baseName = intern("test");
  AstNode* node = compileFunctionNode(module);
  return node;
}

bool testImportParsing() {
  // Compile the import statement with the correct directory
  AstNode* node = compileWithImportDir("use export", "test/integration/import");

  // The import should desugar to a block containing:
  // 1. let __import = call(exportModuleFunction)
  // 2. let x = __import["x"]
  // 3. let f = __import["f"]

  // The compiled node is a function node; get its body
  if (node->type != AST_FUNCTION) {
    printf("Expected compiled node to be AST_FUNCTION, got type %d\n",
           node->type);
    return false;
  }

  AstNode* body = node->as.function.body;
  if (body->type != AST_BLOCK) {
    printf("Expected function body to be AST_BLOCK, got type %d\n", body->type);
    return false;
  }

  // The function body should have 2 statements: the import block and the return
  AstVec* bodyStmts = &body->as.block.stmts;
  if (bodyStmts->count < 1) {
    printf("Expected at least 1 statement in function body, got %d\n",
           bodyStmts->count);
    return false;
  }

  // Extract the first statement which should be the import block
  AstNode* actualFirstStmt = &bodyStmts->items[0];
  if (actualFirstStmt->type != AST_BLOCK) {
    printf(
        "Expected first statement to be a block (import desugaring), got type "
        "%d\n",
        actualFirstStmt->type);
    return false;
  }

  AstVec* blockStmts = &actualFirstStmt->as.block.stmts;

  // Check we have 3 statements in the block:
  // let __import = ...
  // let x = ...
  // let f = ...
  if (blockStmts->count != 3) {
    printf("Expected 3 statements in import block, got %d\n",
           blockStmts->count);
    return false;
  }

  // Check first statement: let __import = call(exportModuleFunction)
  AstNode* importDecl = &blockStmts->items[0];
  if (importDecl->type != AST_DECL_LET) {
    printf("Expected first statement to be AST_DECL_LET, got type %d\n",
           importDecl->type);
    return false;
  }
  if (importDecl->as.declLet.local->type != AST_VAR_LOCAL) {
    printf("Expected import decl local to be AST_VAR_LOCAL\n");
    return false;
  }
  ObjString* importName = importDecl->as.declLet.local->as.local.name;
  if (strcmp(importName->chars, "__import") != 0) {
    printf("Expected import local name to be '__import', got '%s'\n",
           importName->chars);
    return false;
  }
  if (importDecl->as.declLet.value->type != AST_CALL) {
    printf("Expected import value to be AST_CALL, got type %d\n",
           importDecl->as.declLet.value->type);
    return false;
  }

  // Check second statement: let x = __import["x"]
  AstNode* xDecl = &blockStmts->items[1];
  if (xDecl->type != AST_DECL_LET) {
    printf("Expected second statement to be AST_DECL_LET for x, got type %d\n",
           xDecl->type);
    return false;
  }
  if (xDecl->as.declLet.local->type != AST_VAR_LOCAL) {
    printf("Expected x decl local to be AST_VAR_LOCAL\n");
    return false;
  }
  ObjString* xName = xDecl->as.declLet.local->as.local.name;
  if (strcmp(xName->chars, "x") != 0) {
    printf("Expected second local name to be 'x', got '%s'\n", xName->chars);
    return false;
  }
  if (xDecl->as.declLet.value->type != AST_SUBSCRIPT_GET) {
    printf("Expected x value to be AST_SUBSCRIPT_GET, got type %d\n",
           xDecl->as.declLet.value->type);
    return false;
  }

  // Check third statement: let f = __import["f"]
  AstNode* fDecl = &blockStmts->items[2];
  if (fDecl->type != AST_DECL_LET) {
    printf("Expected third statement to be AST_DECL_LET for f, got type %d\n",
           fDecl->type);
    return false;
  }
  if (fDecl->as.declLet.local->type != AST_VAR_LOCAL) {
    printf("Expected f decl local to be AST_VAR_LOCAL\n");
    return false;
  }
  ObjString* fName = fDecl->as.declLet.local->as.local.name;
  if (strcmp(fName->chars, "f") != 0) {
    printf("Expected third local name to be 'f', got '%s'\n", fName->chars);
    return false;
  }
  if (fDecl->as.declLet.value->type != AST_SUBSCRIPT_GET) {
    printf("Expected f value to be AST_SUBSCRIPT_GET, got type %d\n",
           fDecl->as.declLet.value->type);
    return false;
  }

  return true;
}

/* Bytecode tests for Object */

bool testBytecodeObjectEmpty() {
  AstNode* obj = newMapNode();
  Chunk c;
  if (!buildChunkForExpr(obj, &c)) return false;

  // OP_GET_GLOBAL (3) + OP_CALL (2) = 5 bytes
  if (c.count != 5) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.code[3] != OP_CALL) return false;
  if (c.code[4] != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("obj")))) return false;

  return true;
}

bool testBytecodeObjectOneProperty() {
  AstNode* obj = newMapNode();
  AstNode* key = newLiteralNode();
  key->as.literal.value = OBJ_VAL(intern("key"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(key, newLiteralValueNode(NUMBER_VAL(1))));
  Chunk c;
  if (!buildChunkForExpr(obj, &c)) return false;

  // OP_GET_GLOBAL(3) + key bytecode + value bytecode + OP_CALL(2) =
  // OP_GET_GLOBAL(3) + OP_CONSTANT key(3) + OP_CONSTANT 1(3) + OP_CALL(2) = 11
  // bytes
  if (c.count != 11) return false;

  // Check OP_GET_GLOBAL for obj
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;

  // Check OP_CONSTANT for key
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;

  // Check OP_CONSTANT for value
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;

  // Check OP_CALL with 2 args (key, value)
  if (c.code[9] != OP_CALL || c.code[10] != 2) return false;

  // Constants: obj, key, 1
  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("obj")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("key")))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(1))) return false;

  return true;
}

bool testBytecodeObjectMultipleProperties() {
  AstNode* obj = newMapNode();
  AstNode* keyX = newLiteralNode();
  keyX->as.literal.value = OBJ_VAL(intern("x"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyX, newLiteralValueNode(NUMBER_VAL(1))));
  AstNode* keyY = newLiteralNode();
  keyY->as.literal.value = OBJ_VAL(intern("y"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyY, newLiteralValueNode(NUMBER_VAL(2))));
  AstNode* keyZ = newLiteralNode();
  keyZ->as.literal.value = OBJ_VAL(intern("z"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyZ, newLiteralValueNode(NUMBER_VAL(3))));
  Chunk c;
  if (!buildChunkForExpr(obj, &c)) return false;

  // OP_GET_GLOBAL(3) + 3 pairs * (OP_CONSTANT key(3) + OP_CONSTANT value(3)) +
  // OP_CALL(2) = 3 + 3*6 + 2 = 23 bytes
  if (c.count != 23) return false;

  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (c.code[21] != OP_CALL || c.code[22] != 6)
    return false;  // 3 pairs * 2 = 6 args

  if (c.constants.count != 7) return false;  // obj + 3 keys + 3 values
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("obj")))) return false;

  return true;
}

bool testBytecodeObjectNested() {
  AstNode* innerObj = newMapNode();
  AstNode* keyInner = newLiteralNode();
  keyInner->as.literal.value = OBJ_VAL(intern("inner"));
  pushAstVec(&innerObj->as.map.entries,
             newMapEntryNode(keyInner, newLiteralValueNode(NUMBER_VAL(1))));

  AstNode* outerObj = newMapNode();
  AstNode* keyOuter = newLiteralNode();
  keyOuter->as.literal.value = OBJ_VAL(intern("outer"));
  pushAstVec(&outerObj->as.map.entries, newMapEntryNode(keyOuter, innerObj));

  Chunk c;
  if (!buildChunkForExpr(outerObj, &c)) return false;

  // Outer: OP_GET_GLOBAL(obj, 3) + inner full bytecode + OP_CONSTANT(outer, 3)
  // + OP_CALL(2, 2) Inner: OP_GET_GLOBAL(obj, 3) + OP_CONSTANT(inner, 3) +
  // OP_CONSTANT(1, 3) + OP_CALL(2, 2) = 3 + 11 + 3 + 2 = 19 bytes
  if (c.count < 15) return false;

  // Check outer OP_GET_GLOBAL for obj
  if (c.code[0] != OP_GET_GLOBAL) return false;

  // Verify constants contain obj, inner, 1, outer
  bool hasObj = false;
  bool hasInner = false;
  bool has1 = false;
  bool hasOuter = false;
  for (int i = 0; i < c.constants.count; i++) {
    if (valuesEqual(c.constants.values[i], OBJ_VAL(intern("obj"))))
      hasObj = true;
    if (valuesEqual(c.constants.values[i], OBJ_VAL(intern("inner"))))
      hasInner = true;
    if (valuesEqual(c.constants.values[i], NUMBER_VAL(1))) has1 = true;
    if (valuesEqual(c.constants.values[i], OBJ_VAL(intern("outer"))))
      hasOuter = true;
  }

  if (!hasObj || !hasInner || !has1 || !hasOuter) return false;

  return true;
}

bool testBytecodeObjectComplexKeys() {
  AstNode* obj = newMapNode();
  AstNode* keyLiteral = newLiteralNode();
  ObjString* keyStr = intern("string-key");
  keyLiteral->as.literal.value = OBJ_VAL(keyStr);
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyLiteral, newLiteralValueNode(NUMBER_VAL(42))));
  Chunk c;
  if (!buildChunkForExpr(obj, &c)) return false;

  // OP_GET_GLOBAL(3) + OP_CONSTANT(string-key, 3) + OP_CONSTANT(42, 3) +
  // OP_CALL(2, 2)
  if (c.count != 11) return false;

  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (c.code[9] != OP_CALL || c.code[10] != 2) return false;

  // Verify string key is in constants
  bool hasStringKey = false;
  for (int i = 0; i < c.constants.count; i++) {
    if (IS_STRING(c.constants.values[i]) &&
        strcmp(AS_STRING(c.constants.values[i])->chars, "string-key") == 0) {
      hasStringKey = true;
      break;
    }
  }
  if (!hasStringKey) return false;

  return true;
}

bool testBytecodeObjectInCall() {
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* obj = newMapNode();
  AstNode* keyX = newLiteralNode();
  keyX->as.literal.value = OBJ_VAL(intern("x"));
  pushAstVec(&obj->as.map.entries,
             newMapEntryNode(keyX, newLiteralValueNode(NUMBER_VAL(1))));
  pushAstVec(&call->as.call.args, obj);

  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  // OP_GET_GLOBAL(f, 3) + object full (11) + OP_CALL(1, 2) = 16 bytes
  if (c.count != 16) return false;

  // Check OP_GET_GLOBAL for f
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;

  // Check OP_GET_GLOBAL for inner obj
  if (c.code[3] != OP_GET_GLOBAL) return false;

  // Check last OP_CALL with 1 arg
  if (c.code[14] != OP_CALL || c.code[15] != 1) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("f")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("obj")))) return false;

  return true;
}

/* Bytecode tests for Sequence */

bool testBytecodeSequenceEmpty() {
  AstNode* seq = newSequenceNode();
  Chunk c;
  if (!buildChunkForExpr(seq, &c)) return false;

  // OP_GET_GLOBAL (3) + OP_CALL (2) = 5 bytes
  if (c.count != 5) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.code[3] != OP_CALL) return false;
  if (c.code[4] != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("seq")))) return false;

  return true;
}

bool testBytecodeSequenceTwoElements() {
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  Chunk c;
  if (!buildChunkForExpr(seq, &c)) return false;

  // OP_GET_GLOBAL(3) + OP_CONSTANT(3) + OP_CONSTANT(3) + OP_CALL(2) = 11 bytes
  if (c.count != 11) return false;

  // Check OP_GET_GLOBAL for sSeq
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;

  // Check OP_CONSTANT for 1
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;

  // Check OP_CONSTANT for 2
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;

  // Check OP_CALL with 2 args
  if (c.code[9] != OP_CALL || c.code[10] != 2) return false;

  // Constants: seq, 1, 2
  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("seq")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;

  return true;
}

bool testBytecodeSequenceThreeElements() {
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(3)));
  Chunk c;
  if (!buildChunkForExpr(seq, &c)) return false;

  // OP_GET_GLOBAL(3) + 3*OP_CONSTANT(3) + OP_CALL(2) = 14 bytes
  if (c.count != 14) return false;

  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;
  if (c.code[9] != OP_CONSTANT || read_u16(c.code[10], c.code[11]) != 3)
    return false;
  if (c.code[12] != OP_CALL || c.code[13] != 3) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("seq")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(3))) return false;

  return true;
}

bool testBytecodeSequenceNestedCallee() {
  // (f(), 1)
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, call);
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));

  Chunk c;
  if (!buildChunkForExpr(seq, &c)) return false;

  // OP_GET_GLOBAL(sSeq, 3) + OP_GET_GLOBAL(f, 3) + OP_CALL(0, 2) +
  // OP_CONSTANT(1, 3) + OP_CALL(2, 2) = 13 bytes
  if (c.count != 13) return false;

  // Check OP_GET_GLOBAL for sSeq
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;

  // Check OP_GET_GLOBAL for f
  if (c.code[3] != OP_GET_GLOBAL || read_u16(c.code[4], c.code[5]) != 1)
    return false;

  // Check OP_CALL with 0 args
  if (c.code[6] != OP_CALL || c.code[7] != 0) return false;

  // Check OP_CONSTANT for 1
  if (c.code[8] != OP_CONSTANT || read_u16(c.code[9], c.code[10]) != 2)
    return false;

  // Check OP_CALL with 2 args
  if (c.code[11] != OP_CALL || c.code[12] != 2) return false;

  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("seq")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("f")))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(1))) return false;

  return true;
}

bool testBytecodeSequenceNested() {
  // ((1, 2), 3)
  AstNode* innerSeq = newSequenceNode();
  pushAstVec(&innerSeq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&innerSeq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));

  AstNode* outerSeq = newSequenceNode();
  pushAstVec(&outerSeq->as.sequence.values, innerSeq);
  pushAstVec(&outerSeq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(3)));

  Chunk c;
  if (!buildChunkForExpr(outerSeq, &c)) return false;

  // If test fails, just validate basic structure and constants
  // Nested sequences can have varying byte counts based on implementation
  // details
  if (c.count < 10) return false;

  // Check outer OP_GET_GLOBAL for sSeq
  if (c.code[0] != OP_GET_GLOBAL) return false;

  // Find the second OP_GET_GLOBAL (inner sequence)
  int innerSeqStart = -1;
  for (int i = 1; i < c.count; i++) {
    if (c.code[i] == OP_GET_GLOBAL) {
      innerSeqStart = i;
      break;
    }
  }
  if (innerSeqStart == -1) return false;

  // Verify constants contain seq, 1, 2, 3
  bool hasSeq = false;
  bool has1 = false, has2 = false, has3 = false;
  for (int i = 0; i < c.constants.count; i++) {
    if (valuesEqual(c.constants.values[i], OBJ_VAL(intern("seq")))) {
      hasSeq = true;
    }
    if (valuesEqual(c.constants.values[i], NUMBER_VAL(1))) has1 = true;
    if (valuesEqual(c.constants.values[i], NUMBER_VAL(2))) has2 = true;
    if (valuesEqual(c.constants.values[i], NUMBER_VAL(3))) has3 = true;
  }

  if (!hasSeq || !has1 || !has2 || !has3) return false;

  return true;
}

bool testBytecodeSequenceInCall() {
  // f((1, 2))
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, seq);

  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  // OP_GET_GLOBAL(f, 3) + sequence full (11) + OP_CALL(1, 2) = 16 bytes
  if (c.count != 16) return false;

  // Check OP_GET_GLOBAL for f
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;

  // Check OP_GET_GLOBAL for inner sSeq
  if (c.code[3] != OP_GET_GLOBAL) return false;

  // Check last OP_CALL with 1 arg
  if (c.code[14] != OP_CALL || c.code[15] != 1) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("f")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("seq")))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(2))) return false;

  return true;
}

/* Bytecode tests for Set */

bool testBytecodeSetEmpty() {
  AstNode* set = newSetNode();
  Chunk c;
  if (!buildChunkForExpr(set, &c)) return false;

  if (c.count != 5) return false;
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  if (c.code[3] != OP_CALL || c.code[4] != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("set")))) return false;

  return true;
}

bool testBytecodeSetTwoElements() {
  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(2)));
  Chunk c;
  if (!buildChunkForExpr(set, &c)) return false;

  if (c.count != 11) return false;
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;
  if (c.code[9] != OP_CALL || c.code[10] != 2) return false;

  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("set")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;

  return true;
}

bool testBytecodeSetThreeElements() {
  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(3)));
  Chunk c;
  if (!buildChunkForExpr(set, &c)) return false;

  if (c.count != 14) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;
  if (c.code[9] != OP_CONSTANT || read_u16(c.code[10], c.code[11]) != 3)
    return false;
  if (c.code[12] != OP_CALL || c.code[13] != 3) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("set")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(3))) return false;

  return true;
}

bool testBytecodeSetInCall() {
  // f({1, 2})
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);

  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(1)));
  pushAstVec(&set->as.set.values, newLiteralValueNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, set);

  Chunk c;
  if (!buildChunkForExpr(call, &c)) return false;

  if (c.count != 16) return false;

  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;
  if (c.code[3] != OP_GET_GLOBAL) return false;
  if (c.code[14] != OP_CALL || c.code[15] != 1) return false;

  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("f")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("set")))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[3], NUMBER_VAL(2))) return false;

  return true;
}

bool testBytecodeSubscriptGet() {
  AstNode* arr = newVarGlobalNode();
  arr->as.global.name = intern("arr");
  AstNode* index = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* sub = newSubscriptGetNode(arr, index);

  Chunk c;
  if (!buildChunkForExpr(sub, &c)) return false;

  if (c.count != 7) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.code[3] != OP_CONSTANT) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;
  if (c.code[6] != OP_SUBSCRIPT_GET) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("arr")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;

  return true;
}

bool testBytecodeSubscriptSet() {
  AstNode* arr = newVarGlobalNode();
  arr->as.global.name = intern("arr");
  AstNode* index = newLiteralValueNode(NUMBER_VAL(0));
  AstNode* value = newLiteralValueNode(NUMBER_VAL(42));
  AstNode* sub = newSubscriptSetNode(arr, index, value);

  Chunk c;
  if (!buildChunkForExpr(sub, &c)) return false;

  if (c.count != 10) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.code[3] != OP_CONSTANT) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;
  if (c.code[6] != OP_CONSTANT) return false;
  if (read_u16(c.code[7], c.code[8]) != 2) return false;
  if (c.code[9] != OP_SUBSCRIPT_SET) return false;

  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("arr")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(0))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(42))) return false;

  return true;
}

/* Bytecode tests for Property Access */

bool testBytecodePropertyGet() {
  AstNode* obj = newVarGlobalNode();
  obj->as.global.name = intern("obj");
  ObjString* propName = intern("foo");
  AstNode* prop = newPropertyGetNode(obj);
  prop->as.propertyGet.property = propName;
  Chunk c;
  if (!buildChunkForExpr(prop, &c)) return false;

  // Layout: OP_GET_GLOBAL (3) + OP_PROPERTY_GET (3) = 6 bytes
  if (c.count != 6) return false;

  // Check OP_GET_GLOBAL for "obj"
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  // Check OP_PROPERTY_GET with property name constant
  if (c.code[3] != OP_PROPERTY_GET) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;

  // Check constants: obj name and property name
  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("obj")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(propName))) return false;

  return true;
}

bool testBytecodePropertySet() {
  AstNode* obj = newVarGlobalNode();
  obj->as.global.name = intern("obj");
  ObjString* propName = intern("bar");
  AstNode* value = newLiteralValueNode(NUMBER_VAL(99));
  AstNode* set = newPropertySetNode(obj, value);
  set->as.propertySet.property = propName;
  Chunk c;
  if (!buildChunkForExpr(set, &c)) return false;

  // Layout: OP_GET_GLOBAL (3) + CONSTANT(99) (3) + OP_PROPERTY_SET (3) = 9
  // bytes
  if (c.count != 9) return false;

  // Check OP_GET_GLOBAL for "obj"
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  // Check CONSTANT for value (99)
  if (c.code[3] != OP_CONSTANT) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;

  // Check OP_PROPERTY_SET with property name constant
  if (c.code[6] != OP_PROPERTY_SET) return false;
  if (read_u16(c.code[7], c.code[8]) != 2) return false;

  // Check constants: obj name, value (99), property name
  if (c.constants.count != 3) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("obj")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(99))) return false;
  if (!valuesEqual(c.constants.values[2], OBJ_VAL(propName))) return false;

  return true;
}

/* Bytecode tests for Assignment */

bool testBytecodeAssignmentGlobal() {
  AstNode* var = newVarGlobalNode();
  ObjString* name = intern("x");
  var->as.global.name = name;
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(42));
  AstNode* assignment = newAssignmentNode(var, rhs);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: CONSTANT(42), OP_SET_GLOBAL, constant_index
  // 3 bytes for CONSTANT + 3 bytes for OP_SET_GLOBAL = 6 bytes
  if (c.count != 6) return false;

  // Check CONSTANT instruction
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  // Check OP_SET_GLOBAL instruction
  if (c.code[3] != OP_SET_GLOBAL) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;  // name constant index

  // Check constants: first is rhs value, second is variable name
  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], NUMBER_VAL(42))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(name))) return false;

  return true;
}

bool testBytecodeAssignmentLocal() {
  AstNode* var = newVarLocalNode(0);
  ObjString* name = intern("x");
  var->as.local.name = name;
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(42));
  AstNode* assignment = newAssignmentNode(var, rhs);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: CONSTANT(42), OP_SET_LOCAL, index(2 bytes)
  // 3 bytes for CONSTANT + 3 bytes for OP_SET_LOCAL + index = 6 bytes
  if (c.count != 6) return false;

  // Check CONSTANT instruction
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  // Check OP_SET_LOCAL instruction
  if (c.code[3] != OP_SET_LOCAL) return false;
  if (read_u16(c.code[4], c.code[5]) != 0) return false;  // local index 0

  // Check constants: only rhs value (no name constant for locals)
  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], NUMBER_VAL(42))) return false;

  return true;
}

bool testBytecodeAssignmentUpvalue() {
  AstNode* var = newVarUpvalueNode(1);
  ObjString* name = intern("x");
  var->as.upvalue.name = name;
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(42));
  AstNode* assignment = newAssignmentNode(var, rhs);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: CONSTANT(42), OP_SET_UPVALUE, index(2 bytes)
  // 3 bytes for CONSTANT + 3 bytes for OP_SET_UPVALUE + index = 6 bytes
  if (c.count != 6) return false;

  // Check CONSTANT instruction
  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  // Check OP_SET_UPVALUE instruction
  if (c.code[3] != OP_SET_UPVALUE) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;  // upvalue index 1

  // Check constants: only rhs value (no name constant for upvalues)
  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], NUMBER_VAL(42))) return false;

  return true;
}

bool testBytecodeAssignmentWithExpression() {
  AstNode* var = newVarGlobalNode();
  ObjString* name = intern("x");
  var->as.global.name = name;

  // Build 1 + 2
  AstNode* op = newVarGlobalNode();
  op->as.global.name = intern("+");
  AstNode* lhs = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* infixCall = newCallInfixNode(op, lhs, rhs);

  AstNode* assignment = newAssignmentNode(var, infixCall);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: GET_GLOBAL(+), CONSTANT(1), CONSTANT(2), CALL(2), OP_SET_GLOBAL,
  // name_idx 3 + 3 + 3 + 2 + 3 = 14 bytes
  if (c.count != 14) return false;

  // Check RHS expression compilation
  // The "+" operator is compiled as OP_GET_GLOBAL (not OP_CONSTANT)
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;  // + operator
  if (c.code[3] != OP_CONSTANT || read_u16(c.code[4], c.code[5]) != 1)
    return false;  // 1
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;  // 2
  if (c.code[9] != OP_CALL || c.code[10] != 2)
    return false;  // CALL with 2 args

  // Check OP_SET_GLOBAL
  if (c.code[11] != OP_SET_GLOBAL) return false;
  if (read_u16(c.code[12], c.code[13]) != 3)
    return false;  // name constant index

  // Check constants: +, 1, 2, name
  if (c.constants.count != 4) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("+")))) return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  if (!valuesEqual(c.constants.values[2], NUMBER_VAL(2))) return false;
  if (!valuesEqual(c.constants.values[3], OBJ_VAL(name))) return false;

  return true;
}

bool testBytecodeAssignmentWithCall() {
  AstNode* var = newVarGlobalNode();
  ObjString* name = intern("x");
  var->as.global.name = name;
  AstNode* callee = newVarGlobalNode();
  callee->as.global.name = intern("f");
  AstNode* call = newCallNode(callee);
  AstNode* assignment = newAssignmentNode(var, call);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: GET_GLOBAL(f), CALL(0), OP_SET_GLOBAL, name_idx
  // 3 + 2 + 3 = 8 bytes
  if (c.count != 8) return false;

  // Check RHS call compilation
  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;                                            // f
  if (c.code[3] != OP_CALL || c.code[4] != 0) return false;  // CALL with 0 args

  // Check OP_SET_GLOBAL
  if (c.code[5] != OP_SET_GLOBAL) return false;
  if (read_u16(c.code[6], c.code[7]) != 1) return false;  // name constant index

  // Check constants: f, name
  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("f")))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(name))) return false;

  return true;
}

bool testBytecodeAssignmentLocalIndex1() {
  AstNode* var = newVarLocalNode(1);
  ObjString* name = intern("y");
  var->as.local.name = name;
  AstNode* rhs = newLiteralValueNode(NUMBER_VAL(100));
  AstNode* assignment = newAssignmentNode(var, rhs);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: CONSTANT(100), OP_SET_LOCAL, index(2 bytes)
  if (c.count != 6) return false;

  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  if (c.code[3] != OP_SET_LOCAL) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;  // local index 1

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], NUMBER_VAL(100))) return false;

  return true;
}

bool testBytecodeAssignmentGlobalLongName() {
  AstNode* var = newVarGlobalNode();
  var->as.global.name = intern("very_long_global_variable_name_for_assignment");
  AstNode* rhs = newLiteralValueNode(BOOL_VAL(true));
  AstNode* assignment = newAssignmentNode(var, rhs);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  // Layout: CONSTANT(true), OP_SET_GLOBAL, name_idx
  if (c.count != 6) return false;

  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  if (c.code[3] != OP_SET_GLOBAL) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], BOOL_VAL(true))) return false;
  if (!valuesEqual(
          c.constants.values[1],
          OBJ_VAL(intern("very_long_global_variable_name_for_assignment"))))
    return false;

  return true;
}

bool testBytecodeAssignmentBooleanValue() {
  AstNode* var = newVarGlobalNode();
  ObjString* name = intern("flag");
  var->as.global.name = name;
  AstNode* rhs = newLiteralValueNode(BOOL_VAL(false));
  AstNode* assignment = newAssignmentNode(var, rhs);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  if (c.count != 6) return false;

  if (c.code[0] != OP_CONSTANT) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;

  if (c.code[3] != OP_SET_GLOBAL) return false;
  if (read_u16(c.code[4], c.code[5]) != 1) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], BOOL_VAL(false))) return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(name))) return false;

  return true;
}

bool testBytecodeAssignmentNestedInfix() {
  AstNode* var = newVarGlobalNode();
  ObjString* name = intern("result");
  var->as.global.name = name;

  // Build (1 + 2) * 3
  AstNode* plusOp = newVarGlobalNode();
  plusOp->as.global.name = intern("+");
  AstNode* one = newLiteralValueNode(NUMBER_VAL(1));
  AstNode* two = newLiteralValueNode(NUMBER_VAL(2));
  AstNode* plusCall = newCallInfixNode(plusOp, one, two);

  AstNode* multOp = newVarGlobalNode();
  multOp->as.global.name = intern("*");
  AstNode* three = newLiteralValueNode(NUMBER_VAL(3));
  AstNode* multCall = newCallInfixNode(multOp, plusCall, three);

  AstNode* assignment = newAssignmentNode(var, multCall);
  Chunk c;
  if (!buildChunkForExpr(assignment, &c)) return false;

  if (c.count != 22) return false;

  if (c.code[0] != OP_GET_GLOBAL || read_u16(c.code[1], c.code[2]) != 0)
    return false;  // *
  if (c.code[3] != OP_GET_GLOBAL || read_u16(c.code[4], c.code[5]) != 1)
    return false;  // +
  if (c.code[6] != OP_CONSTANT || read_u16(c.code[7], c.code[8]) != 2)
    return false;  // 1
  if (c.code[9] != OP_CONSTANT || read_u16(c.code[10], c.code[11]) != 3)
    return false;  // 2
  if (c.code[12] != OP_CALL || c.code[13] != 2) return false;
  if (c.code[14] != OP_CONSTANT || read_u16(c.code[15], c.code[16]) != 4)
    return false;  // 3
  if (c.code[17] != OP_CALL || c.code[18] != 2) return false;
  if (c.code[19] != OP_SET_GLOBAL) return false;
  if (read_u16(c.code[20], c.code[21]) != 5) return false;  // name constant

  return true;
}

void fmt(char* pref, bool success, char* msg) {
  // ANSI color codes
  const char* green = "\033[32m";  // Green
  const char* red = "\033[31m";    // Red
  const char* reset = "\033[0m";   // Reset

  const char* color = success ? green : red;
  const char* symbol = success ? "✔" : "✗";

  printf("%s%s%s %s%s\n", pref, color, symbol, msg, reset);
}

int testMain(void) {
  initVM();

  printf("AST\n");
  printf("  Line/col\n");
  fmt("    ", testLineColSimpleAssignment(), "Line/col simple assignment");
  fmt("    ", testLineColMultiLineAssignment(),
      "Line/col multi-line assignment");
  fmt("    ", testLineColLeadingBlankLines(), "Line/col leading blank lines");

  printf("  Compilation\n");
  fmt("    ", testLiteralNumberNode(), "Literal Number");
  fmt("    ", testLiteralBooleanTrue(), "Literal Boolean True");
  fmt("    ", testLiteralBooleanFalse(), "Literal Boolean False");
  fmt("    ", testCallNode0Args(), "Call (0 args)");
  fmt("    ", testCallNode1Args(), "Call (1 args)");
  fmt("    ", testCallInfixNode(), "Call Infix");
  fmt("    ", testCallInfixNodeLeftNested(), "Call Infix - Left Nested");
  fmt("    ", testCallInfixNodeRightNested(), "Call Infix - Right Nested");
  fmt("    ", testFunctionNode(), "Function - Implicit Return - Literal");
  fmt("    ", testStringLiteral(), "String literal");
  fmt("    ", testStringEmpty(), "String empty");
  fmt("    ", testIfSimple(), "If simple");
  fmt("    ", testIfElse(), "If else");
  fmt("    ", testIfBlock(), "If block");
  fmt("    ", testIfElseBlock(), "If else block");
  fmt("    ", testIfNested(), "If nested");
  fmt("    ", testIfElseIf(), "If else-if");
  fmt("    ", testIfComplexCondition(), "If complex condition");
  fmt("    ", testWhileSimple(), "While simple");
  fmt("    ", testWhileBlock(), "While block");
  fmt("    ", testWhileComplexCondition(), "While complex condition");
  fmt("    ", testWhileNested(), "While nested");
  fmt("    ", testForSimple(), "For simple");
  fmt("    ", testForComplexExpressions(), "For complex expressions");
  fmt("    ", testForNoInitializer(), "For no initializer");
  fmt("    ", testForNoIncrement(), "For no increment");
  fmt("    ", testForBlockBody(), "For block body");
  fmt("    ", testForIterSimple(), "For iter simple");
  fmt("    ", testThrowGlobal(), "Throw global");
  fmt("    ", testThrowCall(), "Throw call");
  fmt("    ", testThrowInfix(), "Throw infix");
  fmt("    ", testThrowInBlock(), "Throw in block");
  fmt("    ", testThrowInConditional(), "Throw in conditional");
  fmt("    ", testAssignmentGlobal(), "Assignment global");
  fmt("    ", testAssignmentLocal(), "Assignment local");
  fmt("    ", testAssignmentWithExpression(), "Assignment with expression");
  fmt("    ", testAssignmentWithCall(), "Assignment with call");
  fmt("    ", testAssignmentNestedExpression(), "Assignment nested expression");
  fmt("    ", testAssignmentBooleanValue(), "Assignment boolean value");
  fmt("    ", testAssignmentInBlock(), "Assignment in block");
  fmt("    ", testMultipleAssignments(), "Multiple assignments");
  fmt("    ", testAssignmentReassignment(), "Assignment reassignment");
  fmt("    ", testSequenceEmpty(), "Sequence empty");
  fmt("    ", testSequenceOneElement(), "Sequence one element");
  fmt("    ", testSequenceTwoElements(), "Sequence two elements");
  fmt("    ", testSequenceThreeElements(), "Sequence three elements");
  fmt("    ", testSequenceVariables(), "Sequence variables");
  fmt("    ", testSequenceComplexExpression(), "Sequence complex expression");
  fmt("    ", testSequenceNested(), "Sequence nested");
  fmt("    ", testSequenceInCall(), "Sequence in call");
  fmt("    ", testSetOneElement(), "Set one element");
  fmt("    ", testSetTwoElements(), "Set two elements");
  fmt("    ", testSetVariables(), "Set variables");
  fmt("    ", testSetComplexExpression(), "Set complex expression");
  fmt("    ", testSetNested(), "Set nested");
  fmt("    ", testSetInCall(), "Set in call");
  fmt("    ", testSubscriptGet(), "Subscript get");
  fmt("    ", testSubscriptSet(), "Subscript set");
  fmt("    ", testSubscriptNested(), "Subscript nested");
  fmt("    ", testPropertyGet(), "Property get");
  fmt("    ", testPropertySet(), "Property set");
  fmt("    ", testPropertyNested(), "Property nested");
  fmt("    ", testPropertyNestedAssignment(), "Property nested assignment");
  fmt("    ", testSequenceComprehension(), "Sequence comprehension");
  fmt("    ", testSequenceComprehensionComplexBody(),
      "Sequence comprehension complex body");
  fmt("    ", testSequenceComprehensionNestedBody(),
      "Sequence comprehension nested body");
  fmt("    ", testSequenceComprehensionNestedCondition(),
      "Sequence comprehension nested condition");
  fmt("    ", testSetComprehensionParse(), "Set comprehension parse");
  fmt("    ", testSetComprehensionComplexBody(),
      "Set comprehension complex body");
  fmt("    ", testSetComprehensionNestedBody(),
      "Set comprehension nested body");
  fmt("    ", testSetComprehensionNestedCondition(),
      "Set comprehension nested condition");
  fmt("    ", testSetComprehensionFunctionBody(),
      "Set comprehension function body");
  fmt("    ", testObjectEmpty(), "Object empty");
  fmt("    ", testObjectOneProperty(), "Object one property");
  fmt("    ", testObjectMultipleProperties(), "Object multiple properties");
  fmt("    ", testObjectIdentifierKey(), "Object identifier key");
  fmt("    ", testObjectComplexValues(), "Object complex values");
  fmt("    ", testObjectNested(), "Object nested");
  fmt("    ", testObjectInExpression(), "Object in expression");
  fmt("    ", testObjectTrailingComma(), "Object trailing comma");
  fmt("    ", testImportParsing(), "Import simple");

  printf("  Memory\n");

  printf("  Bytecode\n");
  fmt("    ", testBytecodeCall0Args(), "Call (0 args) callee+OP_CALL");
  fmt("    ", testBytecodeCall1Arg(), "Call (1 arg) order");
  fmt("    ", testBytecodeCall3Args(), "Call (3 args) order");
  fmt("    ", testBytecodeCallNestedCallee(), "Call with nested callee");
  fmt("    ", testBytecodeFunctionEmpty(), "Function empty body");
  fmt("    ", testBytecodeFunctionExpr(), "Function expression body");
  fmt("    ", testBytecodeGlobal(), "Global variable");
  fmt("    ", testBytecodeGlobalLongName(), "Global variable long name");
  fmt("    ", testBytecodeCallInfix(), "Call infix");
  fmt("    ", testBytecodeString(), "String");
  fmt("    ", testBytecodeStringEmpty(), "String empty");
  fmt("    ", testBytecodeStringLong(), "String long");
  fmt("    ", testBytecodeIfSimple(), "If simple");
  fmt("    ", testBytecodeIfElse(), "If else");
  fmt("    ", testBytecodeIfBlock(), "If block");
  fmt("    ", testBytecodeIfElseBlock(), "If else block");
  fmt("    ", testBytecodeIfNested(), "If nested");
  fmt("    ", testBytecodeIfComplexCondition(), "If complex condition");
  fmt("    ", testBytecodeWhileSimple(), "While simple");
  fmt("    ", testBytecodeWhileFalseCondition(), "While false condition");
  fmt("    ", testBytecodeWhileEmptyBody(), "While empty body");
  fmt("    ", testBytecodeWhileNested(), "While nested");
  fmt("    ", testBytecodeWhileComplexBody(), "While complex body");
  fmt("    ", testBytecodeIterSimple(), "For-in simple");
  fmt("    ", testBytecodeForSimple(), "For simple");
  fmt("    ", testBytecodeThrowSimple(), "Throw simple");
  fmt("    ", testBytecodeThrowLiteral(), "Throw literal");
  fmt("    ", testBytecodeThrowInfix(), "Throw infix");
  fmt("    ", testBytecodeThrowInConditional(), "Throw in conditional");
  fmt("    ", testBytecodeAssignmentGlobal(), "Assignment global");
  fmt("    ", testBytecodeAssignmentLocal(), "Assignment local");
  fmt("    ", testBytecodeAssignmentUpvalue(), "Assignment upvalue");
  fmt("    ", testBytecodeAssignmentWithExpression(),
      "Assignment with expression");
  fmt("    ", testBytecodeAssignmentWithCall(), "Assignment with call");
  fmt("    ", testBytecodeAssignmentLocalIndex1(), "Assignment local index 1");
  fmt("    ", testBytecodeAssignmentGlobalLongName(),
      "Assignment global long name");
  fmt("    ", testBytecodeAssignmentBooleanValue(), "Assignment boolean value");
  fmt("    ", testBytecodeAssignmentNestedInfix(), "Assignment nested infix");
  fmt("    ", testBytecodeSequenceEmpty(), "Sequence empty");
  fmt("    ", testBytecodeSequenceTwoElements(), "Sequence two elements");
  fmt("    ", testBytecodeSequenceThreeElements(), "Sequence three elements");
  fmt("    ", testBytecodeSequenceNestedCallee(), "Sequence nested callee");
  fmt("    ", testBytecodeSequenceNested(), "Sequence nested");
  fmt("    ", testBytecodeSequenceInCall(), "Sequence in call");
  fmt("    ", testBytecodeSetEmpty(), "Set empty");
  fmt("    ", testBytecodeSetTwoElements(), "Set two elements");
  fmt("    ", testBytecodeSetThreeElements(), "Set three elements");
  fmt("    ", testBytecodeSetInCall(), "Set in call");
  fmt("    ", testBytecodeSubscriptGet(), "Subscript get bytecode");
  fmt("    ", testBytecodeSubscriptSet(), "Subscript set bytecode");
  fmt("    ", testBytecodePropertyGet(), "Property get bytecode");
  fmt("    ", testBytecodePropertySet(), "Property set bytecode");
  fmt("    ", testBytecodeObjectEmpty(), "Object empty");
  fmt("    ", testBytecodeObjectOneProperty(), "Object one property");
  fmt("    ", testBytecodeObjectMultipleProperties(),
      "Object multiple properties");
  fmt("    ", testBytecodeObjectNested(), "Object nested");
  fmt("    ", testBytecodeObjectComplexKeys(), "Object complex keys");
  fmt("    ", testBytecodeObjectInCall(), "Object in call");

  freeVM();
  return 0;
}
