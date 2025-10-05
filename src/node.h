#ifndef nat_node_h
#define nat_node_h

#include "common.h"
#include "object.h"
#include "value.h"

typedef enum {
  AST_LET,
  AST_LITERAL,
  AST_BLOCK,
  AST_CALL,
  AST_CALL_INFIX,
  AST_MODULE,
  AST_RETURN,
  AST_SPREAD,
  AST_SEQUENCE,

  AST_UNKNOWN,

  AST_VARIABLE,
  AST_UNARY,
  AST_BINARY,

  AST_MEMBER,
  AST_SUBSCRIPT,
  AST_COMPREHENSION,
  AST_SIGNATURE,
  AST_CLOSURE,
  AST_CLASS,
  AST_IMPORT,
  AST_THROW,
  AST_DESTRUCTURE,
  AST_SET_TYPE,
  AST_UNIT,
  AST_QUANTIFY,
  AST_ITER,
  AST_OVERLOAD,
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
      AstNode* signature;
      AstNode* body;
    } closure;

    struct {
      ObjString* name;
      AstNode* value;
    } let;

    struct {
      Value value;
    } literal;

    struct {
      ObjString* name;
      AstVec stmts;
    } module;

    struct {
      AstNode* value;
    } iReturn;
    struct {
      AstNode* value;
    } xReturn;

    struct {
      ObjString* name;
    } variable;
    struct {
      AstVec values;
    } sequence;
    struct {
      AstNode* expr;
    } spread;

    //

    struct {
      AstNode* object;
      ObjString* member;
      int isSet;
    } member;
    struct {
      AstNode* collection;
      AstNode* index;
      int isSet;
    } subscript;
    struct {
      AstNode* body;
      ObjString* var;
      AstNode* iterable;
      AstNode* pred;
    } comprehension;
    struct {
      AstVec params;
      int varargs;
    } signature;

    struct {
      ObjString* name;
      AstVec methods;
      ObjString* super;
    } classDef;
    struct {
      ObjString* module;
      ObjString* asName;
      ObjString* from;
    } import;
    struct {
      AstNode* expr;
    } throw;
    struct {
      AstNode* expr;
    } exprStmt;
    struct {
      ObjString* name;
      AstNode* typeExpr;
    } setType;

    struct {
      ObjString* quant;
      ObjString* var;
      AstNode* scope;
    } quantify;
    struct {
      AstNode* iterable;
      ObjString* var;
      AstNode* body;
    } iter;
    struct {
      ObjString* symbol;
      AstNode* impl;
    } overload;
  } as;
};

/* constructors */

AstNode* newBlockNode();
AstNode* newCallNode(AstNode* callee);
AstNode* newCallInfixNode(AstNode* callee, AstNode* lhs, AstNode* rhs);
AstNode* newClosureNode();
AstNode* newLetNode(ObjString* name, AstNode* value);
AstNode* newLiteralNode(Value v);
AstNode* newModuleNode(ObjString* name);
AstNode* newSpreadNode(AstNode* expr);
AstNode* newSequenceNode();
AstNode* newSignatureNode();
AstNode* newVariableNode(ObjString* name);

AstNode* newMemberNode(AstNode* object, ObjString* member, int isSet);
AstNode* newSubscriptNode(AstNode* coll, AstNode* index, int isSet);
AstNode* newComprehensionNode(AstNode* body, ObjString* var, AstNode* iterable,
                              AstNode* pred);

AstNode* newReturnNode(AstNode* value);
AstNode* newClassNode(ObjString* name, ObjString* super);
AstNode* newImportNode(ObjString* module, ObjString* asName, ObjString* from);
AstNode* newThrowNode(AstNode* expr);
AstNode* newSetTypeNode(ObjString* name, AstNode* typeExpr, int isGlobal);
AstNode* newQuantifyNode(ObjString* quant, ObjString* var, AstNode* scope);
AstNode* newIterNode(ObjString* var, AstNode* iterable, AstNode* body);
AstNode* newOverloadNode(ObjString* symbol, AstNode* impl);

/* api */

bool nodesEqual(AstNode* a, AstNode* b);
void printNode(AstNode* node);

/* memory */

void markAstNode(AstNode* n);
void freeAstNode(AstNode* n);

#endif
