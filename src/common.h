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
#define S_SEQUENCE "Sequence"
#define S_TUPLE "Tuple"
#define S_MAP "Map"
#define S_SET "Set"
#define S_TREE "Tree"
#define S_ITERATOR "Iterator"

#define S_AST_CLOSURE "ASTClosure"
#define S_AST_UPVALUE "ASTUpvalue"
#define S_AST_SIGNATURE "ASTSignature"
#define S_AST_PARAMETER "ASTParameter"
#define S_AST_OVERLOAD "ASTOverload"

#define S_CTYPE_BOOL "CBool"
#define S_CTYPE_NIL "CNil"
#define S_CTYPE_NUMBER "CNumber"
#define S_CTYPE_UNDEF "CUndef"
#define S_OTYPE_VARIABLE "OVariable"
#define S_OTYPE_CLASS "OClass"
#define S_OTYPE_INSTANCE "OInstance"
#define S_OTYPE_STRING "OString"
#define S_OTYPE_CLOSURE "OClosure"
#define S_OTYPE_OVERLOAD "OOverload"
#define S_OTYPE_SEQUENCE "OSequence"

#define S_UNIFY "unify"

#endif