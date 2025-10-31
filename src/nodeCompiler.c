
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "parser.h"
#include "scanner.h"
#include "vm.h"

typedef AstNode* (*ParseFn)(NodeCompiler* cmp, bool canAssign);
typedef AstNode* (*InfixFn)(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                            Precedence prec);

typedef struct {
  ParseFn prefix;
  InfixFn infix;
  Precedence leftPrec;
  Precedence rightPrec;
} ParseRule;

static AstNode* statement(NodeCompiler* cmp);
static AstNode* expression(NodeCompiler* cmp);
static AstNode* parsePrecedence(NodeCompiler* cmp, Precedence precedence);

void initNodeCompiler(NodeCompiler* cmp, NodeCompiler* enclosing,
                      AstNode* node) {
  cmp->enclosing = NULL;
  cmp->enclosing = enclosing;
  cmp->node = NULL;
  cmp->node = node;
  cmp->scopeDepth = 0;
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(NodeCompiler* cmp, Token* name) {
  for (int i = cmp->node->as.function.localCount - 1; i >= 0; i--) {
    Local* local = &cmp->node->as.function.locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(NodeCompiler* cmp, uint8_t index, bool isLocal) {
  int upvalueCount = cmp->node->as.function.upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &cmp->node->as.function.upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  cmp->node->as.function.upvalues[upvalueCount].isLocal = isLocal;
  cmp->node->as.function.upvalues[upvalueCount].index = index;
  return cmp->node->as.function.upvalueCount++;
}

static int resolveUpvalue(NodeCompiler* cmp, Token* name) {
  if (cmp->enclosing == NULL) return -1;

  int local = resolveLocal(cmp->enclosing, name);
  if (local != -1) {
    cmp->enclosing->node->as.function.locals[local].isCaptured = true;
    return addUpvalue(cmp, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(cmp->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(cmp, (uint8_t)upvalue, false);
  }

  return -1;
}

static uint8_t addLocal(NodeCompiler* cmp, Token name) {
  if (cmp->node->as.function.localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return 0;
  }

  Local* local =
      &cmp->node->as.function.locals[cmp->node->as.function.localCount++];

  local->name = name;
  local->depth = -1;
  local->isCaptured = false;

  return cmp->node->as.function.localCount - 1;
}

static uint8_t declareLocal(NodeCompiler* cmp, Token* name) {
  for (int i = cmp->node->as.function.localCount - 1; i >= 0; i--) {
    Local* local = &cmp->node->as.function.locals[i];
    if (local->depth != -1 && local->depth < cmp->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  return addLocal(cmp, *name);
}

static void markInitialized(NodeCompiler* cmp) {
  if (cmp->scopeDepth == 0) return;

  cmp->node->as.function.locals[cmp->node->as.function.localCount - 1].depth =
      cmp->scopeDepth;
}

static AstNode* variable(NodeCompiler* cmp, bool canAssign) {
  Token name = parser.previous;
  ObjString* objName = tokenString(name);

  int local = resolveLocal(cmp, &name);
  if (local >= 0) {
    return newVarLocalNode((uint8_t)local, objName);
  }

  int upvalue = resolveUpvalue(cmp, &name);
  if (upvalue >= 0) {
    return newVarUpvalueNode((uint8_t)upvalue, objName);
  }

  return newVarGlobalNode(objName);
}

static AstNode* variableParameter(NodeCompiler* cmp) {
  advance();
  return variable(cmp, false);
}

static AstNode* parameter(NodeCompiler* cmp) {
  if (checkVariable()) return variableParameter(cmp);

  return NULL;
}

static AstNode* signature(NodeCompiler* cmp) {
  AstNode* node = newSignatureNode();

  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* paramNode = parameter(cmp);
      pushAstVec(&node->as.signature.params, paramNode);
    } while (match(TOKEN_COMMA));
  }

  return node;
}

static AstNode* block(NodeCompiler* cmp) {
  AstNode* node = newBlockNode();

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    AstNode* stmtNode = statement(cmp);
    pushAstVec(&node->as.block.stmts, stmtNode);
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");

  return node;
}

static AstNode* blockOrExpression(NodeCompiler* cmp) {
  AstNode* node = NULL;

  if (check(TOKEN_LEFT_BRACE)) {
    advance();
    node = block(cmp);
  } else {
    node = expression(cmp);
    node = newReturnNode(node);
  }

  return node;
}

static AstNode* function(NodeCompiler* enclosing) {
  AstNode* node = newFunctionNode();
  NodeCompiler cmp;
  initNodeCompiler(&cmp, enclosing, node);

  consume(TOKEN_PAREN_LEFT, "Expect '(' after function name.");
  node->as.function.signature = signature(&cmp);
  consume(TOKEN_PAREN_RIGHT, "Expect ')' after parameters.");
  consume(TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = blockOrExpression(&cmp);

  return node;
}

static AstNode* tryFunction(NodeCompiler* cmp) {
  Parser checkpoint = saveParser();
  SignatureType signatureType = peekSignatureType();
  gotoParser(checkpoint);

  switch (signatureType) {
    case SIG_NAKED:
      return NULL;
    case SIG_PAREN:
      return function(cmp);
    case SIG_NOT:
      return NULL;
  }
  return NULL;
}

static AstNode* number(NodeCompiler* cmp, bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  AstNode* node = newLiteralNode(NUMBER_VAL(value));
  node->line = parser.previous.line;
  return node;
}

static void argumentList(NodeCompiler* cmp, AstVec* vec) {
  uint8_t argCount = 0;
  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* node = NULL;

      if (match(TOKEN_DOUBLE_DOT))
        node = newSpreadNode(expression(cmp));
      else
        node = expression(cmp);

      pushAstVec(vec, node);

      if (argCount == 255) error("Can't have more than 255 arguments.");

      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_PAREN_RIGHT, "Expect ')' after arguments.");
}

static AstNode* userInfix(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                          Precedence prec) {
  AstNode* fn = variable(cmp, false);
  AstNode* rhs = parsePrecedence(cmp, prec);
  return newCallInfixNode(fn, lhs, rhs);
}

static AstNode* call(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                     Precedence prec) {
  AstNode* node = newCallNode(lhs);
  argumentList(cmp, &node->as.call.args);
  return node;
}

static AstNode* parentheses(NodeCompiler* cmp, bool canAssign) {
  AstNode* node = expression(cmp);

  if (check(TOKEN_COMMA)) {
    AstNode* seq = newSequenceNode();
    pushAstVec(&seq->as.sequence.values, node);
    do {
      advance();
      pushAstVec(&node->as.sequence.values, expression(cmp));
    } while (check(TOKEN_COMMA));

    node = seq;
  }
  consume(TOKEN_PAREN_RIGHT, "Expect ')' after expression.");
  return node;
}

static ParseRule rules[] = {
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_PAREN_LEFT] = {parentheses, call, PREC_CALL, PREC_NONE},
    [TOKEN_PAREN_RIGHT] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_USER_INFIX] = {NULL, userInfix, PREC_NONE, PREC_NONE},
};

#define PREC_STEP 1

static int sign(int x) { return (x > 0) - (x < 0); }

static void setPrecedence(NodeCompiler* cmp, ParseRule* rule, int prec) {
  // the sign indicates associativity: -1 right, 1 left. 0 is not allowed.
  switch (sign(prec)) {
    case 1: {
      rule->leftPrec = prec;
      rule->rightPrec = prec + PREC_STEP;
      break;
    }
    case -1:
      rule->leftPrec = rule->rightPrec = prec * -1;
      break;
    default:
      error("Unexpected precedence");
  }
}

// Look up the rule for the [token]'s type, unless the
// [token] is an identifier, in which case check the vm's
// infix tables for a user-defined infixation precedence.
static ParseRule* getInfixRule(NodeCompiler* cmp, Token token) {
  if (token.type == TOKEN_IDENTIFIER) {
    Value name = tokenValue(token);
    Value prec;

    if (mapGet(&vm.infixes, name, &prec) ||
        mapGet(&vm.methodInfixes, name, &prec)) {
      setPrecedence(cmp, &rules[TOKEN_USER_INFIX], AS_NUMBER(prec));
      return &rules[TOKEN_USER_INFIX];
    }
  }

  return &rules[token.type];
}

static AstNode* parsePrecedence(NodeCompiler* cmp, Precedence precedence) {
  AstNode* node = NULL;

  advance();

  ParseFn prefixRule = rules[parser.previous.type].prefix;

  if (prefixRule == NULL) {
    error("Expect expression.");
    return node;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  node = prefixRule(cmp, canAssign);

  ParseRule* infixRule;
  while (precedence <=
         (infixRule = getInfixRule(cmp, parser.current))->leftPrec) {
    advance();
    node = infixRule->infix(cmp, canAssign, node, infixRule->rightPrec);
  }

  if (canAssign && match(TOKEN_EQUAL)) error("Invalid assignment target.");

  return node;
}

static AstNode* expression(NodeCompiler* cmp) {
  AstNode* node = tryFunction(cmp);

  if (node != NULL) return node;

  return parsePrecedence(cmp, PREC_ASSIGNMENT);
}

static AstNode* letDeclaration(NodeCompiler* cmp) {
  consumeIdentifier("Expect variable name.");
  Token nameToken = parser.previous;

  declareLocal(cmp, &nameToken);

  AstNode* node = NULL;
  if (match(TOKEN_EQUAL)) {
    node = expression(cmp);
  } else {
    node = newLiteralNode(UNDEF_VAL);
    node->line = parser.previous.line;
  }

  markInitialized(cmp);

  ObjString* name = tokenString(nameToken);
  return newLetNode(name, node);
}

static AstNode* statement(NodeCompiler* cmp) {
  AstNode* node;
  if (match(TOKEN_LET)) {
    node = letDeclaration(cmp);
  } else {
    node = expression(cmp);
    node = newExprStmtNode(node);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after statement.");
  return node;
}

static void statements(NodeCompiler* cmp) {
  while (!match(TOKEN_EOF)) {
    AstNode* node = statement(cmp);
    pushAstVec(&cmp->node->as.function.body->as.block.stmts, node);
  }
}

AstNode* compileModuleNode(Token path, const char* source) {
  Scanner sc = initScanner(source);
  initParser(sc);

  NodeCompiler cmp;
  AstNode* node = newFunctionNode();
  node->as.function.body = newBlockNode();
  initNodeCompiler(&cmp, NULL, node);

  statements(&cmp);

  ObjString* objName = tokenString(path);
  return newModuleNode(objName, node);
}

void markNodeCompilerRoots(NodeCompiler* cmp) {
  printf("\n");
  while (cmp != NULL) {
    markAstNode(cmp->node);
    cmp = cmp->enclosing;
  }
}