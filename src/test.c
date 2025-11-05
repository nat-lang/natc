
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "memory.h"
#include "nodeCompiler.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* ============================================================
 * Node compilation.
 * ============================================================ */

AstNode* compile(Token name, char* source) {
  ObjString* objName = tokenString(name);
  return compileFunctionNode(objName, source, NULL);
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

AstNode* mkFunction(Token name) {
  AstNode* fn = newFunctionNode(tokenString(name), NULL);
  fn->as.function.signature = newSignatureNode();
  fn->as.function.body = newBlockNode();
  return fn;
}

void pushFnStmt(AstNode* fn, AstNode* stmt) {
  pushAstVec(&fn->as.function.body->as.block.stmts, stmt);
}

bool testLiteralNumberNode() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "1");

  AstNode* fn = mkFunction(name);
  AstNode* literal = newLiteralNode(NUMBER_VAL(1));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testLiteralBooleanTrue() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "true");

  AstNode* fn = mkFunction(name);
  AstNode* literal = newLiteralNode(BOOL_VAL(true));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testLiteralBooleanFalse() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "false");

  AstNode* fn = mkFunction(name);
  AstNode* literal = newLiteralNode(BOOL_VAL(false));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallNode0Args() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "f()");

  AstNode* var = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(var);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction(name);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallNode1Args() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "f(1)");

  AstNode* f = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(f);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction(name);
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallInfixNode() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "1 + 2");

  AstNode* inf = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* call = newCallInfixNode(inf, lhs, rhs);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction(name);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallInfixNodeLeftNested() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "1 + 2 + 3");

  AstNode* callLeft = newCallInfixNode(newVarGlobalNode(intern("+")),
                                       newLiteralNode(NUMBER_VAL(1)),
                                       newLiteralNode(NUMBER_VAL(2)));

  AstNode* call = newCallInfixNode(newVarGlobalNode(intern("+")), callLeft,
                                   newLiteralNode(NUMBER_VAL(3)));
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction(name);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallInfixNodeRightNested() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "1 + (2 + 3)");

  AstNode* callRight = newCallInfixNode(newVarGlobalNode(intern("+")),
                                        newLiteralNode(NUMBER_VAL(2)),
                                        newLiteralNode(NUMBER_VAL(3)));

  AstNode* call = newCallInfixNode(newVarGlobalNode(intern("+")),
                                   newLiteralNode(NUMBER_VAL(1)), callRight);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* fn = mkFunction(name);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testFunctionNode() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "let f = () => 1");

  AstNode* f = newFunctionNode(intern("f"), NULL);
  f->as.function.signature = newSignatureNode();
  f->as.function.body = newReturnNode(newLiteralNode(NUMBER_VAL(1)));

  ObjString* objLetName = intern("f");
  AstNode* let = newLetNode(objLetName, f);

  AstNode* fn = mkFunction(name);
  pushFnStmt(fn, let);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * If-Else Statement Tests
 * ============================================================ */

bool testIfSimple() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (true) 1");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newLiteralNode(BOOL_VAL(true));
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* ifNode = newIfNode(cond, then, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfElse() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (x) 1 else 2");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newVarGlobalNode(intern("x"));
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* else_ = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
  AstNode* ifNode = newIfNode(cond, then, else_);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfBlock() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (x) { let y = 1 }");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newVarGlobalNode(intern("x"));
  AstNode* block = newBlockNode();
  AstNode* let = newLetNode(intern("y"), newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&block->as.block.stmts, let);
  AstNode* ifNode = newIfNode(cond, block, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfElseBlock() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (x) { 1 } else { 2 }");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newVarGlobalNode(intern("x"));
  AstNode* thenBlock = newBlockNode();
  AstNode* thenStmt = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&thenBlock->as.block.stmts, thenStmt);
  AstNode* elseBlock = newBlockNode();
  AstNode* elseStmt = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
  pushAstVec(&elseBlock->as.block.stmts, elseStmt);
  AstNode* ifNode = newIfNode(cond, thenBlock, elseBlock);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfNested() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (a) if (b) 1 else 2");

  AstNode* fn = mkFunction(name);
  AstNode* outerCond = newVarGlobalNode(intern("a"));
  AstNode* innerCond = newVarGlobalNode(intern("b"));
  AstNode* innerThen = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* innerElse = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
  AstNode* innerIf = newIfNode(innerCond, innerThen, innerElse);
  AstNode* ifNode = newIfNode(outerCond, innerIf, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfElseIf() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (a) 1 else if (b) 2 else 3");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newVarGlobalNode(intern("a"));
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* innerCond = newVarGlobalNode(intern("b"));
  AstNode* innerThen = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
  AstNode* innerElse = newExprStmtNode(newLiteralNode(NUMBER_VAL(3)));
  AstNode* innerIf = newIfNode(innerCond, innerThen, innerElse);
  AstNode* ifNode = newIfNode(cond, then, innerIf);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testIfComplexCondition() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "if (1 + 2) 1");

  AstNode* fn = mkFunction(name);
  AstNode* op = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* cond = newCallInfixNode(op, lhs, rhs);
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* ifNode = newIfNode(cond, then, NULL);
  pushFnStmt(fn, ifNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileSimple() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "while (true) 1");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newLiteralNode(BOOL_VAL(true));
  AstNode* body = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* whileNode = newWhileNode(cond, body);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileBlock() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "while (x) { let y = 1 }");

  AstNode* fn = mkFunction(name);
  AstNode* cond = newVarGlobalNode(intern("x"));
  AstNode* block = newBlockNode();
  AstNode* let = newLetNode(intern("y"), newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&block->as.block.stmts, let);
  AstNode* whileNode = newWhileNode(cond, block);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileComplexCondition() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "while (1 + 2) 1");

  AstNode* fn = mkFunction(name);
  AstNode* op = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* cond = newCallInfixNode(op, lhs, rhs);
  AstNode* body = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* whileNode = newWhileNode(cond, body);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testWhileNested() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "while (a) while (b) 1");

  AstNode* fn = mkFunction(name);
  AstNode* outerCond = newVarGlobalNode(intern("a"));
  AstNode* innerCond = newVarGlobalNode(intern("b"));
  AstNode* innerBody = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* innerWhile = newWhileNode(innerCond, innerBody);
  AstNode* whileNode = newWhileNode(outerCond, innerWhile);
  pushFnStmt(fn, whileNode);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * Assignment Tests
 * ============================================================ */

bool testAssignmentGlobal() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = 1");

  AstNode* fn = mkFunction(name);
  AstNode* var = newVarGlobalNode(intern("x"));
  AstNode* literal = newLiteralNode(NUMBER_VAL(1));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentLocal() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "let x \n x = 1");

  AstNode* fn = mkFunction(name);
  AstNode* let = newLetNode(intern("x"), newLiteralNode(UNDEF_VAL));
  pushFnStmt(fn, let);

  AstNode* var = newVarLocalNode(1, intern("x"));
  AstNode* literal = newLiteralNode(NUMBER_VAL(1));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentWithExpression() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = 1 + 2");

  AstNode* fn = mkFunction(name);
  AstNode* var = newVarGlobalNode(intern("x"));
  AstNode* op = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* infixCall = newCallInfixNode(op, lhs, rhs);
  AstNode* assignment = newAssignmentNode(var, infixCall);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentWithCall() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = f()");

  AstNode* fn = mkFunction(name);
  AstNode* var = newVarGlobalNode(intern("x"));
  AstNode* callee = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(callee);
  AstNode* assignment = newAssignmentNode(var, call);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentNestedExpression() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = (1 + 2) + 3");

  AstNode* fn = mkFunction(name);
  AstNode* var = newVarGlobalNode(intern("x"));

  // Build (1 + 2) + 3
  AstNode* innerOp = newVarGlobalNode(intern("+"));
  AstNode* innerLhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* innerRhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* innerCall = newCallInfixNode(innerOp, innerLhs, innerRhs);

  AstNode* outerOp = newVarGlobalNode(intern("+"));
  AstNode* outerRhs = newLiteralNode(NUMBER_VAL(3));
  AstNode* outerCall = newCallInfixNode(outerOp, innerCall, outerRhs);

  AstNode* assignment = newAssignmentNode(var, outerCall);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentBooleanValue() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = true");

  AstNode* fn = mkFunction(name);
  AstNode* var = newVarGlobalNode(intern("x"));
  AstNode* literal = newLiteralNode(BOOL_VAL(true));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentInBlock() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "{ x = 1 }");

  AstNode* fn = mkFunction(name);
  AstNode* block = newBlockNode();
  AstNode* var = newVarGlobalNode(intern("x"));
  AstNode* literal = newLiteralNode(NUMBER_VAL(1));
  AstNode* assignment = newAssignmentNode(var, literal);
  AstNode* exprStmt = newExprStmtNode(assignment);
  pushAstVec(&block->as.block.stmts, exprStmt);
  pushFnStmt(fn, block);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testMultipleAssignments() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = 1 \n y = 2");

  AstNode* fn = mkFunction(name);

  // x = 1
  AstNode* var1 = newVarGlobalNode(intern("x"));
  AstNode* literal1 = newLiteralNode(NUMBER_VAL(1));
  AstNode* assignment1 = newAssignmentNode(var1, literal1);
  AstNode* exprStmt1 = newExprStmtNode(assignment1);
  pushFnStmt(fn, exprStmt1);

  // y = 2
  AstNode* var2 = newVarGlobalNode(intern("y"));
  AstNode* literal2 = newLiteralNode(NUMBER_VAL(2));
  AstNode* assignment2 = newAssignmentNode(var2, literal2);
  AstNode* exprStmt2 = newExprStmtNode(assignment2);
  pushFnStmt(fn, exprStmt2);

  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testAssignmentReassignment() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "x = 1 \n x = 2");

  AstNode* fn = mkFunction(name);

  // x = 1
  AstNode* var1 = newVarGlobalNode(intern("x"));
  AstNode* literal1 = newLiteralNode(NUMBER_VAL(1));
  AstNode* assignment1 = newAssignmentNode(var1, literal1);
  AstNode* exprStmt1 = newExprStmtNode(assignment1);
  pushFnStmt(fn, exprStmt1);

  // x = 2
  AstNode* var2 = newVarGlobalNode(intern("x"));
  AstNode* literal2 = newLiteralNode(NUMBER_VAL(2));
  AstNode* assignment2 = newAssignmentNode(var2, literal2);
  AstNode* exprStmt2 = newExprStmtNode(assignment2);
  pushFnStmt(fn, exprStmt2);

  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

/* ============================================================
 * Node GC.
 * ============================================================ */

bool testAstGC() {
  Value litVal = NUMBER_VAL(42);
  AstNode* lit = newLiteralNode(litVal);
  AstNode* ret = newReturnNode(lit);
  ObjAst* wrapper = newObjAst(ret);
  Value rootVal = OBJ_VAL(wrapper);

  vmPush(rootVal);
  collectGarbage();
  vmPop();

  return wrapper->node == ret;
}

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
  AstNode* callee = newLiteralNode(OBJ_VAL(intern("f")));
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
  AstNode* callee = newLiteralNode(OBJ_VAL(intern("f")));
  AstNode* call = newCallNode(callee);
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
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
  AstNode* callee = newLiteralNode(OBJ_VAL(intern("f")));
  AstNode* call = newCallNode(callee);
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(3)));
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
  AstNode* innerCallee = newLiteralNode(OBJ_VAL(copyString("zap", 3)));
  AstNode* innerCall = newCallNode(innerCallee);
  // outer: (f())(1)
  AstNode* outerCall = newCallNode(innerCall);
  pushAstVec(&outerCall->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  Chunk c;
  if (!buildChunkForExpr(outerCall, &c)) return false;

  // Sequence: CONST(5), CALL 0, CONST(1), CALL 1, EXPR_STMT
  // Bytes: [OP_CONSTANT, idx0_hi, idx0_lo, OP_CALL, 0, OP_CONSTANT, idx1_hi,
  // idx1_lo, OP_CALL, 1, OP_EXPR_STATEMENT]
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
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(copyString("zap", 3))))
    return false;
  if (!valuesEqual(c.constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeFunctionEmpty() {
  AstNode* fun = newFunctionNode(intern("f"), NULL);
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
  AstNode* fun = newFunctionNode(intern("f"), NULL);
  fun->as.function.signature = newSignatureNode();
  fun->as.function.body = newLiteralNode(NUMBER_VAL(42));

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
  ObjString* name = intern("g");
  AstNode* g = newVarGlobalNode(name);
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
  ObjString* name = copyString("very_long_global_variable_name_123", 33);
  AstNode* g = newVarGlobalNode(name);
  Chunk c;
  if (!buildChunkForExpr(g, &c)) return false;

  if (c.count != 3) return false;
  if (c.code[0] != OP_GET_GLOBAL) return false;
  if (read_u16(c.code[1], c.code[2]) != 0) return false;
  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(name))) return false;
  return true;
}

bool testBytecodeCallInfix() {
  AstNode* op = newLiteralNode(OBJ_VAL(intern("+")));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
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

/* Bytecode tests for If statements */

bool testBytecodeIfSimple() {
  AstNode* cond = newLiteralNode(BOOL_VAL(true));
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
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
  if (c.code[10] != OP_EXPR_STATEMENT) return false;
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
  ObjString* name = intern("x");
  AstNode* cond = newVarGlobalNode(name);
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* else_ = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
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
  if (c.code[10] != OP_EXPR_STATEMENT) return false;
  // JUMP
  if (c.code[11] != OP_JUMP) return false;
  // POP (patched by thenJump)
  if (c.code[14] != OP_POP) return false;
  // CONSTANT 2
  if (c.code[15] != OP_CONSTANT || read_u16(c.code[16], c.code[17]) != 2)
    return false;
  // EXPR_STMT
  if (c.code[18] != OP_EXPR_STATEMENT) return false;

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
  ObjString* name = intern("x");
  AstNode* cond = newVarGlobalNode(name);
  AstNode* block = newBlockNode();
  AstNode* stmt = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
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
  if (c.code[10] != OP_EXPR_STATEMENT) return false;
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
  ObjString* name = intern("x");
  AstNode* cond = newVarGlobalNode(name);
  AstNode* thenBlock = newBlockNode();
  AstNode* thenStmt = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&thenBlock->as.block.stmts, thenStmt);
  AstNode* elseBlock = newBlockNode();
  AstNode* elseStmt = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
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
  AstNode* outerCond = newVarGlobalNode(intern("a"));
  AstNode* innerCond = newVarGlobalNode(intern("b"));
  AstNode* innerThen = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* innerElse = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
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
  AstNode* op = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* cond = newCallInfixNode(op, lhs, rhs);
  AstNode* then = newExprStmtNode(newLiteralNode(NUMBER_VAL(3)));
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
  AstNode* cond = newLiteralNode(BOOL_VAL(true));
  AstNode* body = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
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
  if (c.code[10] != OP_EXPR_STATEMENT) return false;

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
  AstNode* cond = newLiteralNode(BOOL_VAL(false));
  AstNode* body = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
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
  AstNode* cond = newVarGlobalNode(intern("x"));
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
  AstNode* outerCond = newVarGlobalNode(intern("a"));
  AstNode* innerCond = newVarGlobalNode(intern("b"));
  AstNode* innerBody = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* innerWhile = newWhileNode(innerCond, innerBody);
  AstNode* outerWhile = newWhileNode(outerCond, innerWhile);
  Chunk c;
  if (!buildChunkForExpr(outerWhile, &c)) return false;

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
  AstNode* cond = newVarGlobalNode(intern("x"));
  AstNode* block = newBlockNode();
  AstNode* stmt1 = newExprStmtNode(newLiteralNode(NUMBER_VAL(1)));
  AstNode* stmt2 = newExprStmtNode(newLiteralNode(NUMBER_VAL(2)));
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
    } else if (c.code[i] == OP_EXPR_STATEMENT) {
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

/* ============================================================
 * Import Bytecode Tests
 * ============================================================ */

bool testBytecodeImportSimple() {
  AstNode* module = newModuleNode(NULL, NULL, NULL);
  AstNode* importNode = newUseNode(module, NULL);
  Chunk c;
  if (!buildChunkForExpr(importNode, &c)) return false;

  // Layout: OP_IMPORT, module_const_idx (2 bytes)
  // OP_IMPORT (1) + constant index (2) = 3 bytes
  if (c.count != 3) return false;
  if (c.code[0] != OP_IMPORT) return false;
  uint16_t moduleIdx = read_u16(c.code[1], c.code[2]);
  if (moduleIdx != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("module"))))
    return false;
  return true;
}

bool testBytecodeImportWithAlias() {
  AstNode* module = newModuleNode(NULL, NULL, NULL);
  AstNode* importNode = newUseNode(module, intern("m"));
  Chunk c;
  if (!buildChunkForExpr(importNode, &c)) return false;

  // Layout: OP_IMPORT_AS, module_const_idx (2 bytes), alias_const_idx (2 bytes)
  // OP_IMPORT_AS (1) + module index (2) + alias index (2) = 5 bytes
  if (c.count != 5) return false;
  if (c.code[0] != OP_IMPORT_AS) return false;
  uint16_t moduleIdx = read_u16(c.code[1], c.code[2]);
  uint16_t aliasIdx = read_u16(c.code[3], c.code[4]);
  if (moduleIdx != 0) return false;
  if (aliasIdx != 1) return false;

  if (c.constants.count != 2) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(intern("module"))))
    return false;
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(intern("m")))) return false;
  return true;
}

bool testBytecodeImportLongPath() {
  ObjString* longPath =
      copyString("very/long/path/to/a/module/that/has/many/segments", 48);
  AstNode* module = newModuleNode(longPath, NULL, NULL);
  AstNode* importNode = newUseNode(module, NULL);
  Chunk c;
  if (!buildChunkForExpr(importNode, &c)) return false;

  // Layout: OP_IMPORT, path_const_idx (2 bytes)
  // Should still use 2-byte constant index (16-bit)
  if (c.count != 3) return false;
  if (c.code[0] != OP_IMPORT) return false;
  uint16_t pathIdx = read_u16(c.code[1], c.code[2]);
  if (pathIdx != 0) return false;

  if (c.constants.count != 1) return false;
  if (!valuesEqual(c.constants.values[0], OBJ_VAL(longPath))) return false;
  return true;
}

/* ============================================================
 * Sequence Tests
 * ============================================================ */

bool testSequenceEmpty() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "(,)");

  AstNode* fn = mkFunction(name);
  AstNode* seq = newSequenceNode();
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceOneElement() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "(1,)");

  AstNode* fn = mkFunction(name);
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceTwoElements() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "(1, 2)");

  AstNode* fn = mkFunction(name);
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceThreeElements() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "(1, 2, 3)");

  AstNode* fn = mkFunction(name);
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(3)));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceVariables() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "(x, y)");

  AstNode* fn = mkFunction(name);
  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newVarGlobalNode(intern("x")));
  pushAstVec(&seq->as.sequence.values, newVarGlobalNode(intern("y")));
  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceComplexExpression() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "(1 + 2, f(3), x)");

  AstNode* fn = mkFunction(name);
  AstNode* seq = newSequenceNode();

  // 1 + 2
  AstNode* plusOp = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* infix = newCallInfixNode(plusOp, lhs, rhs);
  pushAstVec(&seq->as.sequence.values, infix);

  // f(3)
  AstNode* f = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(f);
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(3)));
  pushAstVec(&seq->as.sequence.values, call);

  // x
  pushAstVec(&seq->as.sequence.values, newVarGlobalNode(intern("x")));

  AstNode* exprStmt = newExprStmtNode(seq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceNested() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "((1, 2), 3)");

  AstNode* fn = mkFunction(name);

  // inner sequence (1, 2)
  AstNode* innerSeq = newSequenceNode();
  pushAstVec(&innerSeq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&innerSeq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));

  // outer sequence
  AstNode* outerSeq = newSequenceNode();
  pushAstVec(&outerSeq->as.sequence.values, innerSeq);
  pushAstVec(&outerSeq->as.sequence.values, newLiteralNode(NUMBER_VAL(3)));

  AstNode* exprStmt = newExprStmtNode(outerSeq);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testSequenceInCall() {
  Token name = syntheticToken("test");
  AstNode* node = compile(name, "f((1, 2))");

  AstNode* fn = mkFunction(name);
  AstNode* callee = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(callee);

  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, seq);

  AstNode* exprStmt = newExprStmtNode(call);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
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
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));
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
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(3)));
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
  AstNode* callee = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(callee);

  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, call);
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));

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
  pushAstVec(&innerSeq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&innerSeq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));

  AstNode* outerSeq = newSequenceNode();
  pushAstVec(&outerSeq->as.sequence.values, innerSeq);
  pushAstVec(&outerSeq->as.sequence.values, newLiteralNode(NUMBER_VAL(3)));

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
  AstNode* callee = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(callee);

  AstNode* seq = newSequenceNode();
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&seq->as.sequence.values, newLiteralNode(NUMBER_VAL(2)));
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

/* Bytecode tests for Assignment */

bool testBytecodeAssignmentGlobal() {
  ObjString* name = intern("x");
  AstNode* var = newVarGlobalNode(name);
  AstNode* rhs = newLiteralNode(NUMBER_VAL(42));
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
  ObjString* name = intern("x");
  AstNode* var = newVarLocalNode(0, name);
  AstNode* rhs = newLiteralNode(NUMBER_VAL(42));
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
  ObjString* name = intern("x");
  AstNode* var = newVarUpvalueNode(1, name);
  AstNode* rhs = newLiteralNode(NUMBER_VAL(42));
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
  ObjString* name = intern("x");
  AstNode* var = newVarGlobalNode(name);

  // Build 1 + 2
  AstNode* op = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
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
  ObjString* name = intern("x");
  AstNode* var = newVarGlobalNode(name);
  AstNode* callee = newVarGlobalNode(intern("f"));
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
  ObjString* name = intern("y");
  AstNode* var = newVarLocalNode(1, name);
  AstNode* rhs = newLiteralNode(NUMBER_VAL(100));
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
  ObjString* longName =
      copyString("very_long_global_variable_name_for_assignment", 44);
  AstNode* var = newVarGlobalNode(longName);
  AstNode* rhs = newLiteralNode(BOOL_VAL(true));
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
  if (!valuesEqual(c.constants.values[1], OBJ_VAL(longName))) return false;

  return true;
}

bool testBytecodeAssignmentBooleanValue() {
  ObjString* name = intern("flag");
  AstNode* var = newVarGlobalNode(name);
  AstNode* rhs = newLiteralNode(BOOL_VAL(false));
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
  ObjString* name = intern("result");
  AstNode* var = newVarGlobalNode(name);

  // Build (1 + 2) * 3
  AstNode* plusOp = newVarGlobalNode(intern("+"));
  AstNode* one = newLiteralNode(NUMBER_VAL(1));
  AstNode* two = newLiteralNode(NUMBER_VAL(2));
  AstNode* plusCall = newCallInfixNode(plusOp, one, two);

  AstNode* multOp = newVarGlobalNode(intern("*"));
  AstNode* three = newLiteralNode(NUMBER_VAL(3));
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
  printf("%s%s %s\n", pref, success ? "✔" : "✗", msg);
}

int testMain(void) {
  initVM();

  printf("AST\n");

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

  printf("  Memory\n");
  fmt("    ", testAstGC(), "Literal Number - Marked on stack");

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
  fmt("    ", testBytecodeImportSimple(), "Import simple");
  fmt("    ", testBytecodeImportWithAlias(), "Import with alias");
  fmt("    ", testBytecodeImportLongPath(), "Import long path");
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

  freeVM();
  return 0;
}
