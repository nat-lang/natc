#ifndef nat_node_h
#define nat_node_h

#include "common.h"
#include "object.h"
#include "value.h"

typedef enum {
  AST_ASSIGNMENT,
  AST_BLOCK,
  AST_CALL,
  AST_CALL_INFIX,
  AST_EXPR_STMT,
  AST_FUNCTION,
  AST_IF,
  AST_IMPORT,
  AST_LET,
  AST_LITERAL,
  AST_MODULE,
  AST_OBJECT,
  AST_OBJECT_ENTRY,
  AST_PARAM,
  AST_RETURN,
  AST_SEQUENCE,
  AST_SIGNATURE,
  AST_THROW,
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

typedef struct AstFunction AstFunction;
typedef struct AstBlock AstBlock;
typedef struct AstCall AstCall;
typedef struct AstCallInfix AstCallInfix;
typedef struct AstFunction AstFunction;
typedef struct AstIfStmt AstIfStmt;
typedef struct AstExprStmt AstExprStmt;
typedef struct AstUse AstUse;
typedef struct AstLet AstLet;
struct AstNode {
  AstType type;
  int line;
  int chr;

  AstNode* fn;

  union {
    struct {
      AstNode* lhs;
      AstNode* rhs;
    } assignment;

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

    struct AstFunction {
      ObjString* name;
      AstNode* signature;
      AstNode* body;
      AstNode* module;

      bool variadic;
      bool patterned;

      bool depth;
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
      AstNode* module;
      ObjString* alias;  // can be NULLå
    } use;

    struct {
      ObjString* name;
      AstNode* value;
    } let;

    struct {
      Value* value;
    } literal;

    struct {
      ObjString* dirName;
      ObjString* baseName;
      ObjString* source;
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
      AstNode* expr;
    } throwStmt;

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
      AstVec entries;
    } object;
    struct {
      AstNode* key;
      AstNode* value;
    } objectEntry;
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

AstNode* newAssignmentNode(AstNode* lhs, AstNode* rhs);
AstNode* newBlockNode();
AstNode* newCallNode(AstNode* callee);
AstNode* newCallInfixNode(AstNode* callee, AstNode* lhs, AstNode* rhs);
AstNode* newExprStmtNode(AstNode* expr);
AstFunction* newFunctionNode(AstNode* module);
AstNode* newIfNode(AstNode* cond, AstNode* then, AstNode* elseBranch);
AstNode* newLetNode(AstNode* value);
AstNode* newLiteralNode();
AstNode* newModuleNode();
AstNode* newObjectNode();
AstNode* newObjectEntryNode(AstNode* key, AstNode* value);
AstNode* newParamNode(AstNode* annotation);
AstNode* newReturnNode(AstNode* value);
AstNode* newSequenceNode();
AstNode* newThrowNode(AstNode* expr);
AstNode* newSignatureNode();
AstNode* newUnknownNode();
AstNode* newUseNode(AstNode* module);
AstNode* newVarGlobalNode();
AstNode* newVarLocalNode(uint8_t index);
AstNode* newVarUpvalueNode(uint8_t index);
AstNode* newWhileNode(AstNode* cond, AstNode* body);

/* api */

bool nodesEqual(AstNode* a, AstNode* b);
void printNode(AstNode* node);

/* api - bytecode */

bool toChunk(AstNode* node, Chunk* chunk);
ObjFunction* toFunction(AstNode* node);
ObjModule* toModule(AstNode* node);

/* memory */

void markAstNode(AstNode* n);
void freeAstNode(AstNode* n);

#endif
