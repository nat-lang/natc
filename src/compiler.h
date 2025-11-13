#ifndef nat_compiler_h
#define nat_compiler_h

#include "common.h"
#include "node.h"
#include "scanner.h"

typedef struct NodeCompiler {
  struct NodeCompiler* enclosing;
  ObjAst* fn;
  int scopeDepth;
  bool hadError;
} NodeCompiler;

void compileModuleImportBody(NodeCompiler* cmp, ObjAst* module);
ObjAst* compileFunctionNode(ObjAst* module);
ObjAst* compileModuleNode(ObjString* dirName, ObjString* baseName,
                          ObjString* source);

void markNodeCompilerRoots(NodeCompiler* cmp);

#endif