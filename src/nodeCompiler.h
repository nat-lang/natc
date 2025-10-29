#ifndef nat_nodeCompiler_h
#define nat_nodeCompiler_h

#include "common.h"
#include "node.h"

typedef struct NodeCompiler {
  struct NodeCompiler* enclosing;
  AstNode* node;
  int scopeDepth;
} NodeCompiler;

AstNode* compileModuleNode(Token path, const char* source);
void markNodeCompilerRoots(NodeCompiler* cmp);

#endif