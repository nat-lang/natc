
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
    printf("-- -= --\n\n");
    printNode(b);
    printf("--- ---\n");
    return false;
  }
  return true;
}

bool testLiteralNumberNode() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1;");

  AstNode* target = newModuleNode(tokenString(name));
  pushAstVec(&target->as.module.stmts, newLiteralNode(NUMBER_VAL(1)));

  return assertNodesEqual(node, target);
}

bool testCallNode0Args() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "f();");

  AstNode* fn = newVariableNode(intern("f"));
  AstNode* call = newCallNode(fn);
  AstNode* target = newModuleNode(tokenString(name));

  pushAstVec(&target->as.module.stmts, call);

  return assertNodesEqual(node, target);
}

bool testCallNode1Args() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "f(1);");

  AstNode* fn = newVariableNode(intern("f"));
  AstNode* call = newCallNode(fn);
  AstNode* target = newModuleNode(tokenString(name));

  pushAstVec(&call->as.call.args, newLiteralNode(NUMBER_VAL(1)));
  pushAstVec(&target->as.module.stmts, call);

  return assertNodesEqual(node, target);
}

bool testCallInfixNode() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + 2;");

  AstNode* inf = newVariableNode(intern("+"));
  AstNode* lhs = newLiteralNode(NUMBER_VAL(1));
  AstNode* rhs = newLiteralNode(NUMBER_VAL(2));
  AstNode* call = newCallInfixNode(inf, lhs, rhs);
  AstNode* target = newModuleNode(tokenString(name));

  pushAstVec(&target->as.module.stmts, call);

  return assertNodesEqual(node, target);
}

bool testCallInfixNodeLeftNested() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + 2 + 3;");

  AstNode* callLeft = newCallInfixNode(newVariableNode(intern("+")),
                                       newLiteralNode(NUMBER_VAL(1)),
                                       newLiteralNode(NUMBER_VAL(2)));

  AstNode* call = newCallInfixNode(newVariableNode(intern("+")), callLeft,
                                   newLiteralNode(NUMBER_VAL(3)));

  AstNode* target = newModuleNode(tokenString(name));
  pushAstVec(&target->as.module.stmts, call);

  return assertNodesEqual(node, target);
}

bool testCallInfixNodeRightNested() {
  Token name = syntheticToken("test");
  AstNode* node = compileModuleNode(name, "1 + (2 + 3);");

  AstNode* callRight = newCallInfixNode(newVariableNode(intern("+")),
                                        newLiteralNode(NUMBER_VAL(2)),
                                        newLiteralNode(NUMBER_VAL(3)));

  AstNode* call = newCallInfixNode(newVariableNode(intern("+")),
                                   newLiteralNode(NUMBER_VAL(1)), callRight);

  AstNode* target = newModuleNode(tokenString(name));
  pushAstVec(&target->as.module.stmts, call);

  return assertNodesEqual(node, target);
}

bool testClosureNode() {
  Token modName = syntheticToken("test");
  AstNode* node = compileModuleNode(modName, "let f = () => 1;");

  AstNode* closure = newClosureNode();
  closure->as.closure.signature = newSignatureNode();
  closure->as.closure.body = newReturnNode(newLiteralNode(NUMBER_VAL(1)));

  ObjString* objLetName = intern("f");
  AstNode* let = newLetNode(objLetName, closure);

  ObjString* objModName = tokenString(modName);
  AstNode* target = newModuleNode(objModName);
  pushAstVec(&target->as.module.stmts, let);

  return assertNodesEqual(node, target);
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
  fmt("    ", testClosureNode(), "Closure - Implicit Return - Literal");

  printf("  Memory\n");

  fmt("    ", testAstGC(), "Literal Number - Marked on stack");

  printf("  Bytecode\n");

  freeVM();
  return 0;
}
