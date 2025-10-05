#ifndef nat_nodeCompiler_h
#define nat_nodeCompiler_h

#include "common.h"
#include "node.h"

AstNode* compileModuleNode(Token path, const char* source);

#endif