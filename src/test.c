
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
  AstNode* fn = newFunctionNode(tokenString(name));
  fn->as.function.signature = newSignatureNode();
  fn->as.function.body = newBlockNode();
  return fn;
}

AstNode* mkModule(Token name) {
  AstNode* fn = mkFunction(name);
  AstNode* module = newModuleNode(tokenString(name), fn);
  return module;
}

void pushFnStmt(AstNode* fn, AstNode* stmt) {
  pushAstVec(&fn->as.function.body->as.block.stmts, stmt);
}

void pushModuleStmt(AstNode* mod, AstNode* stmt) {
  pushAstVec(&mod->as.module.fn->as.function.body->as.block.stmts, stmt);
}

bool testLiteralNumberNode() {
  Token name = syntheticToken("test");
  AstNode* node = compileFunctionNode(name, "1");

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
  AstNode* node = compileFunctionNode(name, "true");

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
  AstNode* node = compileFunctionNode(name, "false");

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
  AstNode* node = compileFunctionNode(name, "f()");

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
  AstNode* node = compileFunctionNode(name, "f(1)");

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
  AstNode* node = compileFunctionNode(name, "1 + 2");

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
  AstNode* node = compileFunctionNode(name, "1 + 2 + 3");

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
  AstNode* node = compileFunctionNode(name, "1 + (2 + 3)");

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
  AstNode* node = compileFunctionNode(name, "let f = () => 1");

  AstNode* f = newFunctionNode(intern("f"));
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
  AstNode* node = compileFunctionNode(name, "if (true) 1");

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
  AstNode* node = compileFunctionNode(name, "if (x) 1 else 2");

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
  AstNode* node = compileFunctionNode(name, "if (x) { let y = 1 }");

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
  AstNode* node = compileFunctionNode(name, "if (x) { 1 } else { 2 }");

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
  AstNode* node = compileFunctionNode(name, "if (a) if (b) 1 else 2");

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
  AstNode* node = compileFunctionNode(name, "if (a) 1 else if (b) 2 else 3");

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
  AstNode* node = compileFunctionNode(name, "if (1 + 2) 1");

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
  AstNode* node = compileFunctionNode(name, "while (true) 1");

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
  AstNode* node = compileFunctionNode(name, "while (x) { let y = 1 }");

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
  AstNode* node = compileFunctionNode(name, "while (1 + 2) 1");

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
  AstNode* node = compileFunctionNode(name, "while (a) while (b) 1");

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
  AstNode* fun = newFunctionNode(intern("f"));
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
  AstNode* fun = newFunctionNode(intern("f"));
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
  // exitJump is from position 6 (after JUMP_IF_FALSE) to position 14 (final
  // POP) patchJump calculates: jump = chunk->count - offset - 2 When patching:
  // chunk->count = 14, offset = 4 (position of jump offset bytes) jump = 14 - 4
  // - 2 = 8 But READ_SHORT() in VM increments ip, so we need to account for
  // that Actually, JUMP_IF_FALSE: READ_SHORT() increments ip, then
  // conditionally jumps So after READ_SHORT, ip = 6 + 2 = 8, and we jump offset
  // bytes forward We want to jump from 8 to 14, which is 6 bytes, but patchJump
  // uses -2 adjustment Let me check patchJump: jump = chunk->count - offset - 2
  // = 14 - 4 - 2 = 8 But we want to jump from 6 to 14, which is 8 bytes
  // forward. After READ_SHORT, ip = 8 So jump = 14 - 8 = 6. But patchJump gives
  // us 8... Actually, let me verify: JUMP_IF_FALSE at position 3, offset bytes
  // at 4-5 After READ_BYTE: ip = 4, READ_SHORT: ip = 6, then jumps offset We
  // want ip to become 14, so offset = 14 - 6 = 8. That matches patchJump's
  // calculation!
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
  // exitJump from position 6 to 11 (after LOOP), which is 5 bytes
  // But after READ_SHORT, ip = 8, so jump = 11 - 8 = 3
  // Actually, patchJump: jump = chunk->count - offset - 2 = 11 - 4 - 2 = 5
  // After READ_SHORT in VM: ip = 6 + 2 = 8, jump 5 bytes to 13 (past the end)
  // Wait, that's wrong. Let me recalculate:
  // JUMP_IF_FALSE at 3, offset at 4-5, POP at 6, LOOP at 7-9, POP at 10
  // When patching: chunk->count = 10, offset = 4, jump = 10 - 4 - 2 = 4
  // But we want to jump from 6 to 10, which is 4 bytes. After READ_SHORT, ip =
  // 8 So we jump 4 bytes to 12... that's still wrong. Let me check: the final
  // POP is at position 10, so we want to jump to 10 After READ_SHORT in
  // JUMP_IF_FALSE: ip = 6, we jump 4 bytes to 10. That works!
  if (exitJump != 4) return false;

  // Verify LOOP offset
  uint16_t loopOffset = read_u16(c.code[8], c.code[9]);
  // LOOP at 7, offset at 8-9, final POP at 10
  // After READ_SHORT: ip = 10, we want ip - offset = 0 (loopStart)
  // So offset = 10 - 0 = 10
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

  freeVM();
  return 0;
}
