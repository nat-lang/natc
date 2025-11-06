#ifndef nat_nodeCompiler_h
#define nat_nodeCompiler_h

#include "common.h"
#include "node.h"
#include "scanner.h"

void compileModuleImportBody(AstNode* parent);
AstNode* compileFunctionNode(AstNode* module);
void compileModuleNode(AstNode* module);

#endif