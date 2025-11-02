#ifndef nat_nodeCompiler_h
#define nat_nodeCompiler_h

#include "common.h"
#include "node.h"
#include "scanner.h"

typedef struct NodeCompiler {
  struct NodeCompiler* enclosing;
  AstNode* node;
  int scopeDepth;
} NodeCompiler;

AstNode* compileFunctionNode(Token path, const char* source);
void markNodeCompilerRoots(NodeCompiler* cmp);

#endif