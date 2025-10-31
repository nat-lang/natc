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
  AST_LET,
  AST_LITERAL,
  AST_MODULE,
  AST_PARAM,
  AST_RETURN,
  AST_SPREAD,
  AST_SEQUENCE,

  AST_UNKNOWN,
  AST_VAR_GLOBAL,
  AST_VAR_LOCAL,
  AST_VAR_UPVALUE,

  //
  AST_UNARY,
  AST_BINARY,

  AST_MEMBER,
  AST_SUBSCRIPT,
  AST_COMPREHENSION,
  AST_SIGNATURE,
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
    } iReturn;
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
AstNode* newExprStmtNode(AstNode* expr);
AstNode* newFunctionNode(ObjString* name);
AstNode* newLetNode(ObjString* name, AstNode* value);
AstNode* newLiteralNode(Value v);
AstNode* newModuleNode(ObjString* name, AstNode* fn);
AstNode* newParamNode(ObjString* name, AstNode* annotation);
AstNode* newSpreadNode(AstNode* expr);
AstNode* newSequenceNode();
AstNode* newSignatureNode();
AstNode* newVarGlobalNode(ObjString* name);
AstNode* newVarLocalNode(uint8_t index, ObjString* name);
AstNode* newVarUpvalueNode(uint8_t index, ObjString* name);

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

/* api - bytecode */

bool toFunction(AstNode* node, ObjFunction* fn);

/* memory */

void markAstNode(AstNode* n);
void freeAstNode(AstNode* n);

#endif
