
#include <stdio.h>
#include <string.h>

#include "ast.h"
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

  AstNode* var = newVariableNode(intern("f"));
  AstNode* call = newCallNode(var);
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallNode1Args() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "f(1);");

  AstNode* fn = newVariableNode(intern("f"));
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

  AstNode* inf = newVariableNode(intern("+"));
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

  AstNode* callLeft = newCallInfixNode(newVariableNode(intern("+")),
                                       newLiteralNode(NUMBER_VAL(1)),
                                       newLiteralNode(NUMBER_VAL(2)));

  AstNode* call = newCallInfixNode(newVariableNode(intern("+")), callLeft,
                                   newLiteralNode(NUMBER_VAL(3)));
  AstNode* exprStmt = newExprStmtNode(call);
  AstNode* module = mkModule(name);
  pushModuleStmt(module, exprStmt);

  return assertNodesEqual(node, module);
}

bool testCallInfixNodeRightNested() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + (2 + 3);");

  AstNode* callRight = newCallInfixNode(newVariableNode(intern("+")),
                                        newLiteralNode(NUMBER_VAL(2)),
                                        newLiteralNode(NUMBER_VAL(3)));

  AstNode* call = newCallInfixNode(newVariableNode(intern("+")),
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

  freeVM();
  return 0;
}
