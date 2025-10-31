
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "chunk.h"
#include "memory.h"
#include "nodeCompiler.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* ============================================================
 * NodeCompiler.
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

AstNode* mkModule(Token name) {
  AstNode* fn = newFunctionNode();
  AstNode* body = newBlockNode();
  AstNode* module = newModuleNode(tokenString(name), fn);
  module->as.module.fn->as.function.body = body;
  return module;
}

void pushModuleStmt(AstNode* mod, AstNode* stmt) {
  pushAstVec(&mod->as.module.fn->as.function.body->as.block.stmts, stmt);
}

bool testLiteralNumberNode() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1;");

  AstNode* module = mkModule(name);
  AstNode* literal = newLiteralNode(NUMBER_VAL(1));
  AstNode* exprStmt = newExprStmtNode(literal);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallNode0Args() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "f();");

  AstNode* var = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(var);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallNode1Args() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "f(1);");

  AstNode* fn = newVarGlobalNode(intern("f"));
  AstNode* call = newCallNode(fn);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);

  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallInfixNode() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + 2;");

  AstNode* inf = newVarGlobalNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* call = newCallInfixNode(inf, lhs, rhs);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallInfixNodeLeftNested() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + 2 + 3;");

  AstNode* callLeft = newCallInfixNode(newVarGlobalNode(intern("+")),
                                       newLiteralNode(NUMBER_VAL(1)),
                                       newLiteralNode(NUMBER_VAL(2)));

  AstNode* call = newCallInfixNode(newVarGlobalNode(intern("+")), callLeft,
                                   newLiteralNode(NUMBER_VAL(3)));
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallInfixNodeRightNested() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + (2 + 3);");

  AstNode* callRight = newCallInfixNode(newVarGlobalNode(intern("+")),
                                        newLiteralNode(NUMBER_VAL(2)),
                                        newLiteralNode(NUMBER_VAL(3)));

  AstNode* call = newCallInfixNode(newVarGlobalNode(intern("+")),
                                   newLiteralNode(NUMBER_VAL(1)), callRight);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testFunctionNode() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "let f = () => 1;");

  AstNode* function = newFunctionNode();
  function->as.function.signature = newSignatureNode();
  function->as.function.body = newReturnNode(newLiteralNode(NUMBER_VAL(1)));

  ObjString* objLetName = intern("f");
  AstNode* let = newLetNode(objLetName, function);

  AstNode* module = mkModule(name);
  pushModuleStmt(module, let);

  return assertNodesEqual(node, module);
}

/* ============================================================
 * Node.
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

static bool buildFunctionForExpr(AstNode* expr, ObjFunction** outFn) {
  ObjModule* module =
      newModule(intern("."), intern("test"), intern(""), MODULE_ENTRYPOINT);
  ObjFunction* fn = newFunction(module);

  // Compile expression statement so OP_EXPR_STATEMENT is emitted
  AstNode* exprStmt = newExprStmtNode(expr);
  bool ok = toFunction(exprStmt, fn);
  if (!ok) return false;
  *outFn = fn;
  return true;
}

static uint16_t read_u16(uint8_t hi, uint8_t lo) {
  return ((uint16_t)hi << 8) | (uint16_t)lo;
}

bool testBytecodeCall0Args() {
  ObjFunction* fn = NULL;
  AstNode* callee = newLiteralNode(OBJ_VAL(copyString("f", 3)));
  AstNode* call = newCallNode(callee);
  if (!buildFunctionForExpr(call, &fn)) return false;

  Chunk* c = &fn->chunk;
  if (c->count != 6) return false;
  if (c->code[0] != OP_CONSTANT) return false;
  if (read_u16(c->code[1], c->code[2]) != 0) return false;
  if (c->code[3] != OP_CALL) return false;
  if (c->code[4] != 0) return false;  // arg count
  if (c->code[5] != OP_EXPR_STATEMENT) return false;
  if (c->constants.count != 1) return false;
  if (!valuesEqual(c->constants.values[0], OBJ_VAL(copyString("f", 3))))
    return false;
  return true;
}

bool testBytecodeCall1Arg() {
  ObjFunction* fn = NULL;
  AstNode* callee = newLiteralNode(OBJ_VAL(copyString("f", 3)));
  AstNode* call = newCallNode(callee);
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  if (!buildFunctionForExpr(call, &fn)) return false;

  Chunk* c = &fn->chunk;
  if (c->count != 9) return false;
  if (c->code[0] != OP_CONSTANT) return false;
  if (read_u16(c->code[1], c->code[2]) != 0) return false;  // callee const idx
  if (c->code[3] != OP_CONSTANT) return false;
  if (read_u16(c->code[4], c->code[5]) != 1) return false;  // arg const idx
  if (c->code[6] != OP_CALL) return false;
  if (c->code[7] != 1) return false;
  if (c->code[8] != OP_EXPR_STATEMENT) return false;
  if (c->constants.count != 2) return false;
  if (!valuesEqual(c->constants.values[0], OBJ_VAL(copyString("f", 3))))
    return false;
  if (!valuesEqual(c->constants.values[1], NUMBER_VAL(1))) return false;
  return true;
}

bool testBytecodeCall3Args() {
  ObjFunction* fn = NULL;
  AstNode* callee = newLiteralNode(OBJ_VAL(copyString("f", 3)));
  AstNode* call = newCallNode(callee);
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(2)));
  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(3)));
  if (!buildFunctionForExpr(call, &fn)) return false;

  Chunk* c = &fn->chunk;
  // Total bytes: 15
  if (c->count != 15) return false;
  if (c->code[0] != OP_CONSTANT) return false;
  if (read_u16(c->code[1], c->code[2]) != 0) return false;  // callee
  if (c->code[3] != OP_CONSTANT || read_u16(c->code[4], c->code[5]) != 1)
    return false;
  if (c->code[6] != OP_CONSTANT || read_u16(c->code[7], c->code[8]) != 2)
    return false;
  if (c->code[9] != OP_CONSTANT || read_u16(c->code[10], c->code[11]) != 3)
    return false;
  if (c->code[12] != OP_CALL) return false;
  if (c->code[13] != 3) return false;
  if (c->code[14] != OP_EXPR_STATEMENT) return false;
  if (c->constants.count != 4) return false;
  return valuesEqual(c->constants.values[0], OBJ_VAL(copyString("f", 3))) &&
         valuesEqual(c->constants.values[1], NUMBER_VAL(1)) &&
         valuesEqual(c->constants.values[2], NUMBER_VAL(2)) &&
         valuesEqual(c->constants.values[3], NUMBER_VAL(3));
}

bool testBytecodeCallNestedCallee() {
  ObjFunction* fn = NULL;
  // inner: f() where f is string literal "zap"
  AstNode* innerCallee = newLiteralNode(OBJ_VAL(copyString("zap", 3)));
  AstNode* innerCall = newCallNode(innerCallee);
  // outer: (f())(1)
  AstNode* outerCall = newCallNode(innerCall);
  pushAstVec(&outerCall->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  if (!buildFunctionForExpr(outerCall, &fn)) return false;

  Chunk* c = &fn->chunk;
  // Sequence: CONST(5), CALL 0, CONST(1), CALL 1, EXPR_STMT
  // Bytes: [OP_CONSTANT, idx0_hi, idx0_lo, OP_CALL, 0, OP_CONSTANT, idx1_hi,
  // idx1_lo, OP_CALL, 1, OP_EXPR_STATEMENT]
  if (c->count < 11) return false;
  if (c->code[0] != OP_CONSTANT) return false;              // 0
  if (read_u16(c->code[1], c->code[2]) != 0) return false;  // 1,2
  if (c->code[3] != OP_CALL) return false;                  // 3
  if (c->code[4] != 0) return false;                        // 4
  if (c->code[5] != OP_CONSTANT) return false;              // 5
  if (read_u16(c->code[6], c->code[7]) != 1) return false;  // 6,7
  if (c->code[8] != OP_CALL) return false;                  // 8
  if (c->code[9] != 1) return false;                        // 9
  if (c->code[10] != OP_EXPR_STATEMENT) return false;       // 10
  if (c->constants.count != 2) return false;
  if (!valuesEqual(c->constants.values[0], OBJ_VAL(copyString("zap", 3))))
    return false;
  if (!valuesEqual(c->constants.values[1], NUMBER_VAL(1))) return false;
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

  freeVM();
  return 0;
}
