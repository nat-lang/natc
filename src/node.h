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
  AST_COMPREHENSION,
  AST_COMPREHENSION_ITER,
  AST_COMPREHENSION_PRED,
  AST_EXPR_STMT,
  AST_FUNCTION,
  AST_IF,
  AST_FOR,
  AST_IMPORT,
  AST_ITER,
  AST_DECL_LET,
  AST_DECL_GLOBAL,
  AST_LITERAL,
  AST_MODULE,
  AST_MAP,
  AST_MAP_ENTRY,
  AST_PARAM,
  AST_RETURN,
  AST_SEQUENCE,
  AST_SET,
  AST_SUBSCRIPT_GET,
  AST_SUBSCRIPT_SET,
  AST_PROPERTY_GET,
  AST_PROPERTY_SET,
  AST_SIGNATURE,
  AST_THROW,
  AST_TREE,
  AST_UNKNOWN,
  AST_VAR_GLOBAL,
  AST_VAR_LOCAL,
  AST_VAR_UPVALUE,
  AST_WHILE,

} AstType;

typedef enum {
  COMPREHENSION_SEQ,
  COMPREHENSION_SET,
} ComprehensionType;

typedef struct {
  AstNode* items;
  int count;
  int capacity;
} AstVec;

void initAstVec(AstVec* v);
void pushAstVec(AstVec* v, AstNode* item);
void freeAstVec(AstVec* v);

struct AstNode {
  AstType type;
  int line;
  int col;

  AstNode* next;

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

    struct {
      ObjString* name;
      AstNode* signature;
      AstNode* body;
      AstNode* module;

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
      AstNode* initializer;
      AstNode* condition;
      AstNode* increment;
      AstNode* body;
    } forStmt;

    struct {
      AstNode* expr;
    } exprStmt;

    struct {
      AstNode* module;
      ObjString* alias;  // can be NULL
    } use;

    struct {
      AstVec bits;
    } interpolation;

    struct {
      AstNode* var;
      AstNode* iterable;
      AstNode* body;
      uint8_t iterLocal;
    } iter;

    struct {
      AstNode* local;
      AstNode* value;
    } declLet;
    struct {
      ObjString* name;
      AstNode* value;
    } declGlobal;

    struct {
      Value value;
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
      AstVec values;
    } set;
    struct {
      AstNode* value;
      AstVec values;
    } tree;
    struct {
      AstNode* object;
      AstNode* index;
    } subscript;
    struct {
      AstNode* object;
      AstNode* index;
      AstNode* value;
    } subscriptSet;
    struct {
      AstNode* object;
      ObjString* property;
    } propertyGet;
    struct {
      AstNode* object;
      ObjString* property;
      AstNode* value;
    } propertySet;
    struct {
      AstVec entries;
    } map;
    struct {
      AstNode* key;
      AstNode* value;
    } mapEntry;
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

    struct {
      AstNode* body;
      AstVec conditions;
      ComprehensionType type;
      AstNode* compLocal;
    } comprehension;

    struct {
      AstNode* var;
      AstNode* iterable;
      uint8_t iterLocal;
    } comprehensionIter;

    struct {
      AstNode* predicate;
    } comprehensionPred;

  } as;
};

/* constructors */

AstNode* newAssignmentNode(AstNode* lhs, AstNode* rhs);
AstNode* newBlockNode();
AstNode* newCallNode(AstNode* callee);
AstNode* newCallInfixNode(AstNode* callee, AstNode* lhs, AstNode* rhs);
AstNode* newComprehensionNode(AstNode* body, ComprehensionType type);
AstNode* newComprehensionIterNode(AstNode* var, AstNode* iterable);
AstNode* newComprehensionPredNode(AstNode* predicate);
AstNode* newExprStmtNode(AstNode* expr);
AstNode* newForNode(AstNode* initializer, AstNode* condition,
                    AstNode* increment, AstNode* body);
AstNode* newFunctionNode(AstNode* module);
AstNode* newIfNode(AstNode* cond, AstNode* then, AstNode* elseBranch);
AstNode* newInterpolationNode();
AstNode* newIterNode(AstNode* var, AstNode* iterable, AstNode* body);
AstNode* newDeclLetNode(AstNode* local, AstNode* value);
AstNode* newDeclGlobalNode(AstNode* value);
AstNode* newLiteralValueNode(Value value);
AstNode* newLiteralNode();
AstNode* newModuleNode(ObjString* dirName, ObjString* baseName,
                       ObjString* source);
AstNode* newMapNode();
AstNode* newMapEntryNode(AstNode* key, AstNode* value);
AstNode* newParamNode(AstNode* annotation);
AstNode* newReturnNode(AstNode* value);
AstNode* newSequenceNode();
AstNode* newSetNode();
AstNode* newTreeNode();
AstNode* newSubscriptGetNode(AstNode* object, AstNode* index);
AstNode* newSubscriptSetNode(AstNode* object, AstNode* index, AstNode* value);
AstNode* newPropertyGetNode(AstNode* object);
AstNode* newPropertySetNode(AstNode* object, AstNode* value);
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
void markAstNodes(AstNode* node);
void freeAstNodes(AstNode* node);

#endif
