#ifndef nat_common_h
#define nat_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define NAT_EXT ".nat"

#define UINT8_COUNT (UINT8_MAX + 1)
#define UINT16_COUNT (UINT16_MAX + 1)

#define S_EQ "__eq__"
#define S_IN "__in__"
#define S_LEN "__len__"
#define S_SUBSCRIPT_GET "__get__"
#define S_SUBSCRIPT_SET "__set__"

#define S_SUPERCLASS "__superclass__"
#define S_CLASS "__class__"

#define S_ADD "add"
#define S_CALL "call"
#define S_INIT "init"
#define S_ITER "iter"
#define S_PUSH "push"
#define S_POP "pop"
#define S_HASH "hash"

#define S_BASE "Base"
#define S_OBJECT "Object"
#define S_MODULE "Module"
#define S_SEQUENCE "Sequence"
#define S_TUPLE "Tuple"
#define S_MAP "Map"
#define S_SET "Set"
#define S_TREE "Tree"
#define S_ITERATOR "Iterator"

#define S_AST_CLOSURE "ASTClosure"
#define S_AST_METHOD "ASTMethod"
#define S_AST_EXTERNAL_UPVALUE "ASTExternalUpvalue"
#define S_AST_INTERNAL_UPVALUE "ASTInternalUpvalue"
#define S_AST_LOCAL "ASTLocal"
#define S_AST_GLOBAL "ASTGlobal"
#define S_AST_OVERLOAD "ASTOverload"
#define S_AST_MEMBERSHIP "ASTMembership"
#define S_AST_BLOCK "ASTBlock"
#define S_AST_QUANTIFICATION "ASTQuantification"

#define S_CTYPE_UNIT "CUnit"
#define S_CTYPE_BOOL "CBool"
#define S_CTYPE_NIL "CNil"
#define S_CTYPE_NUMBER "CNumber"
#define S_CTYPE_UNDEF "CUndef"
#define S_OTYPE_VARIABLE "OVariable"
#define S_OTYPE_CLASS "OClass"
#define S_OTYPE_INSTANCE "OInstance"
#define S_OTYPE_STRING "OString"
#define S_OTYPE_FUNCTION "OFunction"
#define S_OTYPE_BOUND_FUNCTION "OBoundFunction"
#define S_OTYPE_NATIVE "ONative"
#define S_OTYPE_OVERLOAD "OOverload"
#define S_OTYPE_SEQUENCE "OSequence"

#define S_GRAMMAR "Grammar"
#define S_TYPE_SYSTEM "TypeSystem"
#define S_UNIFY "unify"

typedef enum {
  // Single-character tokens.
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_DOUBLE_DOT,
  TOKEN_COLON,
  TOKEN_SEMICOLON,
  TOKEN_PIPE,
  // One or two character tokens.
  TOKEN_FAT_ARROW,
  TOKEN_ARROW_LEFT,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  // Literals.
  TOKEN_UNDEFINED,
  TOKEN_IDENTIFIER,
  TOKEN_TYPE_VARIABLE,
  TOKEN_STRING,
  TOKEN_INTERPOLATION,
  TOKEN_NUMBER,
  // Keywords.
  TOKEN_AND,
  TOKEN_AS,
  TOKEN_CLASS,
  TOKEN_CONST,
  TOKEN_DOM,
  TOKEN_ELSE,
  TOKEN_EXTENDS,
  TOKEN_FALSE,
  TOKEN_FOR,
  TOKEN_IF,
  TOKEN_IN,
  TOKEN_INFIX,
  TOKEN_INFIX_LEFT,
  TOKEN_INFIX_RIGHT,
  TOKEN_LET,
  TOKEN_NIL,
  TOKEN_NOT,
  TOKEN_OR,
  TOKEN_PRINT,
  TOKEN_RETURN,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_THROW,
  TOKEN_TRUE,
  TOKEN_WHILE,
  TOKEN_IMPORT,
  TOKEN_ERROR,
  TOKEN_EOF,
} TokenType;

typedef struct {
  TokenType type;
  const char* start;
  int length;
  int line;
} Token;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

#endif