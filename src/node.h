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
  ObjAst** items;
  int count;
  int capacity;
} AstVec;

void initAstVec(AstVec* v);
void pushAstVec(AstVec* v, ObjAst* item);
void freeAstVec(AstVec* v);

struct ObjAst {
  Obj obj;

  AstType type;
  int line;
  int col;

  union {
    struct {
      ObjAst* lhs;
      ObjAst* rhs;
    } assignment;

    struct {
      AstVec stmts;
    } block;

    struct {
      ObjAst* callee;
      AstVec args;
    } call;

    struct {
      ObjAst* callee;
      ObjAst* lhs;
      ObjAst* rhs;
    } callInfix;

    struct {
      ObjString* name;
      ObjAst* signature;
      ObjAst* body;
      ObjAst* module;

      bool variadic;
      bool patterned;

      Local locals[UINT8_COUNT];
      int localCount;
      Upvalue upvalues[UINT8_COUNT];
      int upvalueCount;
    } function;

    struct {
      ObjAst* cond;
      ObjAst* then;
      ObjAst* elseBranch;
    } ifStmt;
    struct {
      ObjAst* initializer;
      ObjAst* condition;
      ObjAst* increment;
      ObjAst* body;
    } forStmt;

    struct {
      ObjAst* expr;
    } exprStmt;

    struct {
      ObjAst* module;
      ObjString* alias;  // can be NULL
    } use;

    struct {
      AstVec bits;
    } interpolation;

    struct {
      ObjAst* var;
      ObjAst* iterable;
      ObjAst* body;
      uint8_t iterLocal;
    } iter;

    struct {
      ObjAst* local;
      ObjAst* value;
    } declLet;
    struct {
      ObjString* name;
      ObjAst* value;
    } declGlobal;

    struct {
      Value value;
    } literal;

    struct {
      ObjString* dirName;
      ObjString* baseName;
      ObjString* source;
      ObjAst* fn;
    } module;

    struct {
      ObjString* name;
      ObjAst* annotation;
    } param;

    struct {
      ObjAst* value;
    } xReturn;

    struct {
      ObjAst* expr;
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
      ObjAst* value;
      AstVec children;
    } tree;
    struct {
      ObjAst* object;
      ObjAst* index;
    } subscript;
    struct {
      ObjAst* object;
      ObjAst* index;
      ObjAst* value;
    } subscriptSet;
    struct {
      ObjAst* object;
      ObjString* property;
    } propertyGet;
    struct {
      ObjAst* object;
      ObjString* property;
      ObjAst* value;
    } propertySet;
    struct {
      AstVec entries;
    } map;
    struct {
      ObjAst* key;
      ObjAst* value;
    } mapEntry;
    struct {
      AstVec params;
      int varargs;
    } signature;
    struct {
      ObjAst* expr;
    } spread;

    struct {
      ObjAst* cond;
      ObjAst* body;
    } whileStmt;

    struct {
      ObjAst* body;
      AstVec conditions;
      ComprehensionType type;
      ObjAst* compLocal;
    } comprehension;

    struct {
      ObjAst* var;
      ObjAst* iterable;
      uint8_t iterLocal;
    } comprehensionIter;

    struct {
      ObjAst* predicate;
    } comprehensionPred;

  } as;
};

/* constructors */

ObjAst* newAssignmentNode(ObjAst* lhs, ObjAst* rhs);
ObjAst* newBlockNode();
ObjAst* newCallNode(ObjAst* callee);
ObjAst* newCallInfixNode(ObjAst* callee, ObjAst* lhs, ObjAst* rhs);
ObjAst* newComprehensionNode(ObjAst* body, ComprehensionType type);
ObjAst* newComprehensionIterNode(ObjAst* var, ObjAst* iterable);
ObjAst* newComprehensionPredNode(ObjAst* predicate);
ObjAst* newExprStmtNode(ObjAst* expr);
ObjAst* newForNode(ObjAst* initializer, ObjAst* condition, ObjAst* increment,
                   ObjAst* body);
ObjAst* newFunctionNode(ObjAst* module);
ObjAst* newIfNode(ObjAst* cond, ObjAst* then, ObjAst* elseBranch);
ObjAst* newInterpolationNode();
ObjAst* newIterNode(ObjAst* var, ObjAst* iterable, ObjAst* body);
ObjAst* newDeclLetNode(ObjAst* local, ObjAst* value);
ObjAst* newDeclGlobalNode(ObjAst* value);
ObjAst* newLiteralValueNode(Value value);
ObjAst* newLiteralNode();
ObjAst* newModuleNode(ObjString* dirName, ObjString* baseName,
                      ObjString* source);
ObjAst* newMapNode();
ObjAst* newMapEntryNode(ObjAst* key, ObjAst* value);
ObjAst* newParamNode(ObjAst* annotation);
ObjAst* newReturnNode(ObjAst* value);
ObjAst* newSequenceNode();
ObjAst* newSetNode();
ObjAst* newTreeNode();
ObjAst* newSubscriptGetNode(ObjAst* object, ObjAst* index);
ObjAst* newSubscriptSetNode(ObjAst* object, ObjAst* index, ObjAst* value);
ObjAst* newPropertyGetNode(ObjAst* object);
ObjAst* newPropertySetNode(ObjAst* object, ObjAst* value);
ObjAst* newThrowNode(ObjAst* expr);
ObjAst* newSignatureNode();
ObjAst* newUnknownNode();
ObjAst* newUseNode(ObjAst* module);
ObjAst* newVarGlobalNode();
ObjAst* newVarLocalNode(uint8_t index);
ObjAst* newVarUpvalueNode(uint8_t index);
ObjAst* newWhileNode(ObjAst* cond, ObjAst* body);

/* api */

bool nodesEqual(ObjAst* a, ObjAst* b);
void printNode(ObjAst* node);

/* api - bytecode */

bool toChunk(ObjAst* node, Chunk* chunk);
ObjFunction* toFunction(ObjAst* node);
ObjModule* toModule(ObjAst* node);

/* memory */

void markObjectAst(ObjAst* n);
void freeObjectAst(ObjAst* n);

#endif
