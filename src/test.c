
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
  AstNode* node = compileFunctionNode(name, "1;");

  AstNode* fn = mkFunction(name);
  AstNode* literal = newLiteralNode(NUMBER_VAL(1));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushFnStmt(fn, exprStmt);
  AstNode* nil = newLiteralNode(NIL_VAL);
  AstNode* returnStmt = newReturnNode(nil);
  pushFnStmt(fn, returnStmt);

  return assertNodesEqual(node, fn);
}

bool testCallNode0Args() {
  Token name = syntheticToken("test");
  AstNode* node = compileFunctionNode(name, "f();");

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
  AstNode* node = compileFunctionNode(name, "f(1);");

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
  AstNode* node = compileFunctionNode(name, "1 + 2;");

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
  AstNode* node = compileFunctionNode(name, "1 + 2 + 3;");

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
  AstNode* node = compileFunctionNode(name, "1 + (2 + 3);");

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
  AstNode* node = compileFunctionNode(name, "let f = () => 1;");

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

void fmt(char* pref, bool success, char* msg) {
  printf("%s%s %s\n", pref, success ? "✔" : "✗", msg);
}

int testMain(void) {
  initVM();

  printf("AST\n");

  printf("  Compilation\n");
  fmt("    ", testLiteralNumberNode(), "Literal Number");
  fmt("    ", testCallNode0Args(), "Call (0 args)");
  fmt("    ", testCallNode1Args(), "Call (1 args)");
  fmt("    ", testCallInfixNode(), "Call Infix");
  fmt("    ", testCallInfixNodeLeftNested(), "Call Infix - Left Nested");
  fmt("    ", testCallInfixNodeRightNested(), "Call Infix - Right Nested");
  fmt("    ", testFunctionNode(), "Function - Implicit Return - Literal");

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

  freeVM();
  return 0;
}
