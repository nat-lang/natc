#ifndef nat_node_h
#define nat_node_h

#include "common.h"
#include "object.h"
#include "value.h"

typedef enum {
  AST_BLOCK,
  AST_CALL,
  AST_CALL_INFIX,
  AST_EXPR_STMT,
  AST_FUNCTION,
  AST_IF,
  AST_LET,
  AST_LITERAL,
  AST_MODULE,
  AST_PARAM,
  AST_RETURN,
  AST_SEQUENCE,
  AST_SIGNATURE,
  AST_UNKNOWN,
  AST_VAR_GLOBAL,
  AST_VAR_LOCAL,
  AST_VAR_UPVALUE,
  AST_WHILE,

} AstType;

typedef struct {
  AstNode** items;
  int count;
  int capacity;
} AstVec;

void initAstVec(AstVec* v);
void pushAstVec(AstVec* v, AstNode* item);
void freeAstVec(AstVec* v);

struct AstNode {
  AstType type;
  int line;
  int chr;

  union {
    struct {
      AstVec stmts;
    } block;

    struct {
      AstNode* callee;
      AstVec args;
    } call;

    struct {
      AstNode* callee;
      AstNode* lhs;
      AstNode* rhs;
    } callInfix;

    struct {
      ObjString* name;
      AstNode* signature;
      AstNode* body;

      bool variadic;
      bool patterned;

      Local locals[UINT8_COUNT];
      int localCount;
      Upvalue upvalues[UINT8_COUNT];
      int upvalueCount;
    } function;

    struct {
      AstNode* cond;
      AstNode* then;
      AstNode* elseBranch;
    } ifStmt;

    struct {
      AstNode* expr;
    } exprStmt;

    struct {
      ObjString* name;
      AstNode* value;
    } let;

    struct {
      Value value;
    } literal;

    struct {
      ObjString* name;
      AstNode* fn;
    } module;

    struct {
      ObjString* name;
      AstNode* annotation;
    } param;

    struct {
      AstNode* value;
    } xReturn;

    struct {
      ObjString* name;
    } global;

    struct {
      uint8_t index;
      ObjString* name;
    } local;

    struct {
      uint8_t index;
      ObjString* name;
    } upvalue;

    struct {
      AstVec values;
    } sequence;
    struct {
      AstVec params;
      int varargs;
    } signature;
    struct {
      AstNode* expr;
    } spread;

    struct {
      AstNode* cond;
      AstNode* body;
    } whileStmt;

  } as;
};

/* constructors */

AstNode* newBlockNode();
AstNode* newCallNode(AstNode* callee);
AstNode* newCallInfixNode(AstNode* callee, AstNode* lhs, AstNode* rhs);
AstNode* newExprStmtNode(AstNode* expr);
AstNode* newFunctionNode(ObjString* name);
AstNode* newIfNode(AstNode* cond, AstNode* then, AstNode* elseBranch);
AstNode* newLetNode(ObjString* name, AstNode* value);
AstNode* newLiteralNode(Value v);
AstNode* newModuleNode(ObjString* name, AstNode* fn);
AstNode* newParamNode(ObjString* name, AstNode* annotation);
AstNode* newReturnNode(AstNode* value);
AstNode* newSequenceNode();
AstNode* newSignatureNode();
AstNode* newVarGlobalNode(ObjString* name);
AstNode* newVarLocalNode(uint8_t index, ObjString* name);
AstNode* newVarUpvalueNode(uint8_t index, ObjString* name);
AstNode* newWhileNode(AstNode* cond, AstNode* body);

/* api */

bool nodesEqual(AstNode* a, AstNode* b);
void printNode(AstNode* node);

/* api - bytecode */

bool toChunk(AstNode* node, Chunk* chunk);
ObjFunction* toFunction(AstNode* node);

/* memory */

void markAstNode(AstNode* n);
void freeAstNode(AstNode* n);

#endif
