#ifndef nat_compiler_h
#define nat_compiler_h

#include "common.h"
#include "node.h"
#include "scanner.h"

typedef struct NodeCompiler {
  struct NodeCompiler* enclosing;
  AstNode* fn;
  int scopeDepth;
  bool hadError;
} NodeCompiler;

void compileModuleImportBody(NodeCompiler* cmp, AstNode* module);
AstNode* compileFunctionNode(ObjString* name, char* source, AstNode* module);

void markNodeCompilerRoots(NodeCompiler* cmp);

#endif